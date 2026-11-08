/*
 * module.c - the module list, entry and name tables, and the module APIs
 * that look things up (GetModuleHandle, GetModuleFileName,
 * GetProcAddress, GetModuleUsage). Loading and freeing: loader.c.
 *
 * Every loaded module has a record (struct module, kernel.h) in its own
 * global block; the records form a list that starts with KERNEL. The
 * hModule is the selector of the module's in-memory module table in the
 * Windows 3.1 layout (mdb.c).
 *
 * NE format source: see include/w16/ne.h.
 */
#include "kernel.h"

module FAR *module_first;

/* ---- KERNEL constant exports (see the KCONST segment in kentry.asm) ---- */

static struct {
    u16 ordinal;
    u16 para;           /* real-mode paragraph for selector constants */
    u16 value;
} kconst[] = {
    { 113, 0, 0 },      /* __AHSHIFT */
    { 114, 0, 0 },      /* __AHINCR */
    { 178, 0, 0 },      /* __WINFLAGS */
    { 173, 0xF000, 0 }, /* __ROMBIOS: the same selector as __F000H (tk17.c) */
    { 174, 0xA000, 0 }, /* __A000H */
    { 179, 0xD000, 0 }, /* __D000H */
    { 181, 0xB000, 0 }, /* __B000H */
    { 182, 0xB800, 0 }, /* __B800H */
    { 183, 0x0000, 0 }, /* __0000H */
    { 190, 0xE000, 0 }, /* __E000H */
    { 193, 0x0040, 0 }, /* __0040H */
    { 194, 0xF000, 0 }, /* __F000H */
    { 195, 0xC000, 0 }, /* __C000H */
};
#define N_KCONST (sizeof kconst / sizeof kconst[0])

static void kconst_init(void)
{
    u16 i;
    for (i = 0; i < N_KCONST; i++) {
        switch (kconst[i].ordinal) {
        case 113: kconst[i].value = sel_shift; break;
        case 114: kconst[i].value = sel_inc; break;
        case 178: kconst[i].value = get_winflags(); break;
        default:  kconst[i].value = dpmi_seg_to_desc(kconst[i].para); break;
        }
    }
}

static int kconst_lookup(u16 ordinal, u16 *value)
{
    u16 i;
    for (i = 0; i < N_KCONST; i++)
        if (kconst[i].ordinal == ordinal) {
            *value = kconst[i].value;
            return 1;
        }
    return 0;
}

/* ---- module list ---- */

void mod_set_name(module FAR *m)
{
    const u8 FAR *p = MK_FP(m->ne_sel, NE_HDR(m)->resname_off);
    u8 n = p[0] > MODNAME_MAX ? MODNAME_MAX : p[0], i;
    for (i = 0; i < n; i++) {
        char c = (char)p[1 + i];
        m->name[i] = (c >= 'a' && c <= 'z') ? (char)(c - 32) : c;
    }
    m->name[n] = 0;
}

/* A new, zeroed record with room for seg_count segments; not yet in the
   list. */
module FAR *mod_new(u16 seg_count)
{
    u32 size = sizeof(module) + (u32)seg_count * sizeof(modseg);
    module FAR *m;
    u16 sel;

    if (size > 0xFFF0UL || !galloc(GMEM_FIXED, size, 0, &sel))
        return NULL;
    m = MK_FP(sel, 0);                  /* galloc zeroes new memory */
    m->self = sel;
    m->seg_count = seg_count;
    return m;
}

/* Append to the list; the module tables are chained too (offset 06h:
   observed on Windows 3.1, a module's table is linked when it is made,
   before the modules it imports from). */
void mod_link(module FAR *m)
{
    module FAR *p = module_first;
    m->next = NULL;
    MDB_W(m->ne_sel, MDB_NEXT) = 0;
    if (!p) {
        module_first = m;
        return;
    }
    while (p->next)
        p = p->next;
    p->next = m;
    MDB_W(p->ne_sel, MDB_NEXT) = m->ne_sel;
}

void mod_unlink(module FAR *m)
{
    module FAR *p;
    if (module_first == m) {
        module_first = m->next;
        return;
    }
    for (p = module_first; p; p = p->next)
        if (p->next == m) {
            p->next = m->next;
            MDB_W(p->ne_sel, MDB_NEXT) = m->next ? m->next->ne_sel : 0;
            return;
        }
}

/* Record the module's file as a full path (observed: GetModuleFileName
   gives e.g. C:\TK4.EXE for a program started as TK4.EXE), using DOS
   TRUENAME (INT 21h AH=60h). */
void mod_set_path(module FAR *m, const char FAR *path)
{
    char full[DOS_PATHMAX];
    regs16 r;
    u16 i;

    r.ax = 0x6000;
    r.si = FP_OFF(path);
    r.ds = FP_SEG(path);
    r.di = FP_OFF((char FAR *)full);
    r.es = FP_SEG((char FAR *)full);
    if (!dos21(&r))
        path = full;
    for (i = 0; i < sizeof m->path - 1 && path[i]; i++)
        m->path[i] = path[i];
    m->path[i] = 0;
}

void module_register_kernel(const bootinfo FAR *bi)
{
    module FAR *m = mod_new(bi->seg_count);
    u16 i;
    if (!m) {
        kputs("w16: out of memory\r\n");
        kexit_to_dos(0xFF);
    }
    for (i = 0; i < sizeof m->path - 1 && i < BOOT_PATHMAX && bi->path[i]; i++)
        m->path[i] = bi->path[i];
    /* the bootstrap's copy of KERNEL's file tables becomes its module
       table (UNVERIFIED: KERNEL's file date and time, 0 here) */
    m->flags = ((const ne_header FAR *)MK_FP(bi->ne_sel, 0))->flags;
    m->ne_sel = mdb_build(MK_FP(bi->ne_sel, 0), m->path, 0, 0, 1);
    if (!m->ne_sel) {
        kputs("w16: out of memory\r\n");
        kexit_to_dos(0xFF);
    }
    for (i = 0; i < bi->seg_count; i++) {
        ne_segment FAR *s = MOD_SEG(m, i);
        m->seg[i].sel = m->seg[i].hnd = bi->seg_sel[i];
        MOD_SEGHND(m, i) = bi->seg_sel[i];
        s->flags = (s->flags & ~NESEG_DPL) | NESEG_ALLOCED | NESEG_LOADED | NESEG_MOVABLE;
    }
    MOD_USAGE(m) = 1;
    mod_set_name(m);
    mod_link(m);
    kconst_init();
    loader_init(m->path);
}

module FAR *module_find(const char FAR *name)
{
    module FAR *m;
    for (m = module_first; m; m = m->next)
        if (kstricmp(m->name, name) == 0)
            return m;
    return NULL;
}

static int same_sel(u16 a, u16 b)
{
    return (a | 3) == (b | 3);          /* ignore RPL and the handle bit */
}

/* The module a module handle, instance handle or other segment of the
   module refers to. Observed: GetModuleHandle(MAKELP(0, h)) accepts the
   hModule, hInstance and hInstance | 1. */
module FAR *module_of(u16 h)
{
    module FAR *m;
    u16 s;
    if (!h)
        return NULL;
    for (m = module_first; m; m = m->next) {
        if (same_sel(m->ne_sel, h))
            return m;
        for (s = 0; s < m->seg_count; s++)
            if (same_sel(m->seg[s].sel, h))
                return m;
    }
    return task_module_of_sel(h);       /* a later instance's DGROUP */
}

/* What LoadLibrary returns for a module. Observed on Windows 3.1: the
   handle of the automatic data segment (DS & ~1) for a DLL with one,
   the hModule for a DLL without (TKNOD.DLL), and for KERNEL an odd
   selector other than its hModule (here: KERNEL's DS, which the bootstrap
   made a plain selector). */
u16 module_instance(module FAR *m)
{
    u16 ad = NE_HDR(m)->auto_data;
    if (ad && ad <= m->seg_count)
        return m->seg[ad - 1].hnd;
    return m->ne_sel;
}

/* ---- entry and name tables ---- */

/* Resolve an entry ordinal to sel:off, or to a constant (is_const, value
   in *off and *sel). */
int module_entry(module FAR *m, u16 ordinal, u16 *sel, u16 *off, int *is_const)
{
    u16 t = m->ne_sel, b = MDB_W(t, MDB_ENTRY);

    *is_const = 0;
    if (m == module_first && kconst_lookup(ordinal, off)) {
        *sel = *off;
        *is_const = 1;
        return 1;
    }
    /* in-memory entry table (mdb.c): bundles {first - 1, last, next} */
    for (; b; b = MDB_W(t, b + 4)) {
        u16 first = MDB_W(t, b), last = MDB_W(t, b + 2);
        if (ordinal > first && ordinal <= last) {
            const u8 FAR *e = MK_FP(t, b + 6 + 5 * (ordinal - first - 1));
            *off = *(const u16 FAR *)(e + 3);
            if (e[0] == NEB_CONSTANT) {
                *sel = *off;
                *is_const = 1;
                return 1;
            }
            if (e[2] < 1 || e[2] > m->seg_count)
                return 0;
            *sel = m->seg[e[2] - 1].sel;
            return 1;
        }
    }
    return 0;                           /* not in any bundle: not exported */
}

/* Look a length-prefixed name up in a name table (resident or
   non-resident); the first entry (module name or description) is not an
   export. */
static int find_in_table(const u8 FAR *p, const u8 FAR *pname, u16 *ordinal)
{
    int first = 1;

    while (p[0]) {
        u8 n = p[0], i;
        if (!first && n == pname[0]) {
            for (i = 0; i < n; i++) {
                char a = (char)p[1 + i], b = (char)pname[1 + i];
                if (a >= 'a' && a <= 'z') a -= 32;
                if (b >= 'a' && b <= 'z') b -= 32;
                if (a != b)
                    break;
            }
            if (i == n) {
                *ordinal = *(const u16 FAR *)(p + 1 + n);
                return 1;
            }
        }
        first = 0;
        p += 1 + n + 2;
    }
    return 0;
}

/* An exported name, in the resident and then the non-resident name table
   (observed: GetProcAddress and imports by name find both). */
int mod_find_name(module FAR *m, const u8 FAR *pname, u16 *ordinal)
{
    if (find_in_table(MK_FP(m->ne_sel, NE_HDR(m)->resname_off), pname, ordinal))
        return 1;
    return m->nonres_sel && find_in_table(MK_FP(m->nonres_sel, 0), pname, ordinal);
}

/* The module name a file name stands for: the part after the last path
   separator, up to the first dot. 0 if empty or longer than any module
   name. */
int mod_base_name(const char FAR *name, char *base)
{
    const char FAR *p, *s = name;
    u16 n = 0;

    for (p = name; *p; p++)
        if (*p == '\\' || *p == '/' || *p == ':')
            s = p + 1;
    for (p = s; *p && *p != '.'; p++)
        if (n < MODNAME_MAX)
            base[n++] = *p;
        else
            return 0;
    base[n] = 0;
    return n != 0;
}

/* ---- API ---- */

/* Observed on Windows 3.1: the hModule is the selector of the module's
   NE header in memory (odd: RPL bits set; word 0 is "NE"); names match
   regardless of case; "TK4.EXE" and "C:\TK4.EXE" find module TK4, "TK4 "
   does not; MAKELP(0, handle) looks a handle up; NULL gives 0.
   UNVERIFIED: whether a file name is matched against the module name (as
   here) or the module's file name; they were the same in the probe. */
u16 KAPI GetModuleHandle(const char FAR *name)                /* KERNEL.47 */
{
    char base[MODNAME_MAX + 1];
    module FAR *m;

    if (!FP_SEG(name)) {
        m = module_of(FP_OFF(name));
        return m ? m->ne_sel : 0;
    }
    if (!mod_base_name(name, base))
        return 0;
    m = module_find((const char FAR *)base);
    return m ? m->ne_sel : 0;
}

/* Observed: returns the length copied; at most n - 1 characters and a
   NUL; nothing written for n = 0; an unknown handle gives 0 and an empty
   string. */
u16 KAPI GetModuleFileName(u16 h, char FAR *buf, u16 n)       /* KERNEL.49 */
{
    module FAR *m = module_of(h);
    u16 i;

    if (!n)
        return 0;
    if (!m) {
        buf[0] = 0;
        return 0;
    }
    for (i = 0; i + 1 < n && m->path[i]; i++)
        buf[i] = m->path[i];
    buf[i] = 0;
    return i;
}

/* Observed: names match regardless of case (also "#n" for ordinal n);
   bit 15 of an ordinal is ignored; ordinal 0, gaps and unknown names give
   0; a constant export comes back as FFFF:value; names in the
   non-resident table are found too; a program's own exports are not
   (TK5.EXE: 0 by name and by ordinal, through hModule or hInstance).
   UNVERIFIED: whether that is decided by the library flag (as here) or by
   the module having a task; hModule 0. */
u32 KAPI GetProcAddress(u16 h, const char FAR *name)          /* KERNEL.50 */
{
    module FAR *m = module_of(h);
    u16 ord = 0, sel, off;
    int is_const;

    if (!m || !name || !(m->flags & NEF_LIBRARY))
        return 0;
    if (!FP_SEG(name)) {
        ord = FP_OFF(name) & 0x7FFF;
    } else if (name[0] == '#') {
        const char FAR *p = name + 1;
        if (!*p)
            return 0;
        for (; *p; p++) {
            if (*p < '0' || *p > '9')
                return 0;
            ord = ord * 10 + (u16)(*p - '0');
        }
    } else {
        u8 pname[256];          /* length-prefixed: at most 255 characters */
        u16 i;
        for (i = 0; name[i]; i++)
            if (i == 255)
                return 0;
            else
                pname[1 + i] = (u8)name[i];
        pname[0] = (u8)i;
        if (!mod_find_name(m, (const u8 FAR *)pname, &ord))
            return 0;
    }
    if (!ord || !module_entry(m, ord, &sel, &off, &is_const))
        return 0;
    if (is_const)
        sel = 0xFFFF;
    return ((u32)sel << 16) | off;
}

/* Observed (tests/apps/tk15.c): the hModule for an hInstance (also a
   later instance's) or hModule; 0 for an hTask and for 0. */
u16 KAPI GetExePtr(u16 h)                                     /* KERNEL.133 */
{
    module FAR *m = module_of(h);
    return m ? m->ne_sel : 0;
}

/* Observed: 1 for a module loaded once (implicitly or by LoadLibrary);
   each LoadLibrary / LoadModule adds 1, each FreeLibrary / FreeModule
   takes 1 away; hModule and hInstance both work.
   UNVERIFIED: the result for a handle that is not a module (0 here). */
u16 KAPI GetModuleUsage(u16 h)                                /* KERNEL.48 */
{
    module FAR *m = module_of(h);
    return m ? MOD_USAGE(m) : 0;
}
