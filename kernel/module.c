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

    /* a full path stays as it is; a relative one gets the current drive
       and directory (INT 21h 19h, 47h). DOS's TRUENAME (60h), used
       before, returned garbage for some names under DOSX on DOSBox-X. */
    /* ('/' as '\\', as DOS takes it: "C:/DIR/X.DLL" got the current
       directory put in front; KERNEL audit, medium 15) */
    if (!(path[0] && path[1] == ':' && (path[2] == '\\' || path[2] == '/'))) {
        u16 n = 0, j = 0;
        r.ax = 0x1900;
        r.ds = r.es = 0;
        dos21(&r);
        if (path[0] && path[1] == ':') {
            full[n++] = path[0];        /* "X:NAME": that drive's directory */
            j = 2;
        } else {
            full[n++] = (char)('A' + (r.ax & 0xFF));
        }
        full[n++] = ':';
        full[n++] = '\\';
        if (path[j] != '\\' && path[j] != '/') {
            r.ax = 0x4700;
            r.dx = (full[0] | 0x20) - 'a' + 1;
            r.si = FP_OFF((char FAR *)(full + n));
            r.ds = FP_SEG((char FAR *)(full + n));
            r.es = 0;
            full[n] = 0;
            if (!dos21(&r)) {
                while (full[n] && n < sizeof full - 2)
                    n++;
                if (full[n - 1] != '\\')
                    full[n++] = '\\';
            }
        } else {
            j++;
        }
        while (path[j] && n < sizeof full - 1)
            full[n++] = path[j++];
        full[n] = 0;
        path = full;
    }
    /* observed on Windows 3.1 (TK22): the file name in upper case, e.g.
       "C:\WINDOWS\SYSTEM\SYSTEM.DRV" for [boot] system.drv=system.drv
       (UNVERIFIED: characters beyond ASCII) */
    for (i = 0; i < sizeof m->path - 1 && path[i]; i++)
        m->path[i] = path[i] >= 'a' && path[i] <= 'z' ? path[i] - 'a' + 'A' :
                     path[i] == '/' ? '\\' : path[i];
    m->path[i] = 0;
}

/* KERNEL's own segments: 3.1's are fixed blocks of the heap (TK62
   "kseg", "codehandle"; TK50), and so are w16's once gadopt_kernel
   (global*.c) has moved them there; for one it could not move (no
   memory, DPMI refused), the global heap's APIs answer as for a block
   (KERNEL audit, medium 33). Which one sel is (its number + 1, 0 for
   none), and its GlobalFlags (0900h for a discardable code segment, as
   3.1 shows segments 2 and 3; 0 for the fixed ones). */
u16 kernel_seg(u16 sel)
{
    u16 i;
    if (!sel || !module_first)
        return 0;
    for (i = 0; i < module_first->seg_count; i++)
        if (module_first->seg[i].sel && (module_first->seg[i].sel | 3) == (sel | 3))
            return i + 1;
    return 0;
}

u16 kernel_seg_flags(u16 k)
{
    return (MOD_SEG(module_first, k - 1)->flags & 0x1000) ? 0x0900 : 0;
}

void module_register_kernel(const bootinfo FAR *bi)
{
    module FAR *m = mod_new(bi->seg_count);
    u16 i;
    if (!m) {
        kdbg("w16: out of memory\r\n");
        kexit_to_dos(0xFF);
    }
    for (i = 0; i < sizeof m->path - 1 && i < BOOT_PATHMAX && bi->path[i]; i++)
        m->path[i] = bi->path[i];
    /* the bootstrap's copy of KERNEL's file tables becomes its module
       table (UNVERIFIED: KERNEL's file date and time, 0 here) */
    m->flags = ((const ne_header FAR *)MK_FP(bi->ne_sel, 0))->flags;
    m->ne_sel = mdb_build(MK_FP(bi->ne_sel, 0), m->path, 0, 0, 1);
    if (!m->ne_sel) {
        kdbg("w16: out of memory\r\n");
        kexit_to_dos(0xFF);
    }
    for (i = 0; i < bi->seg_count; i++) {
        ne_segment FAR *s = MOD_SEG(m, i);
        m->seg[i].sel = m->seg[i].hnd = bi->seg_sel[i];
        MOD_SEGHND(m, i) = bi->seg_sel[i];
        /* as 3.1's (TK62 "kseg": C146h for the fixed code segment,
           D156h for the discardable ones, C067h for DGROUP): the file's
           flags but the privilege bits, code not "pure", DGROUP without
           relocations, allocated, loaded, and C000h */
        s->flags = (s->flags & ~(NESEG_DPL | ((s->flags & NESEG_DATA) ? NESEG_RELOCS : NESEG_PURE))) |
                   NESEG_ALLOCED | NESEG_LOADED | 0xC000;
    }
    MOD_USAGE(m) = 1;
    mod_set_name(m);
    mod_link(m);
    kconst_init();
    loader_init(m->path);
}

/* The loaded module whose module name is name, regardless of case (the
   whole string: no path, no extension). */
module FAR *module_find(const char FAR *name)
{
    module FAR *m;
    for (m = module_first; m; m = m->next)
        if (kstricmp(m->name, name) == 0)
            return m;
    return NULL;
}

/* A path's last component: what follows its last \, / or :. */
static const char FAR *last_part(const char FAR *s)
{
    const char FAR *p, *r = s;
    for (p = s; *p; p++)
        if (*p == '\\' || *p == '/' || *p == ':')
            r = p + 1;
    return r;
}

static char upc(char c)
{
    return c >= 'a' && c <= 'z' ? (char)(c - 32) : c;
}

/* The first loaded module (in list order) whose file name begins the
   last component of name, regardless of case: the directories are not
   compared, and whatever follows the file name in name is ignored.
   Observed on Windows 3.1 (tests/apps/tk60.c, KRNL286 and KRNL386,
   Standard and 386 Enhanced mode alike): for a module loaded from
   C:\TKMN\TKFA.DLL, "TKFA.DLL", "C:\NOWHERE\TKFA.DLL", "C:TKFA.DLL",
   "C:/X/TKFA.DLL", "tkfa.dll", "TKFA.DLLX" and "TKFA.DLL." match;
   "TKFA", "TKFA.", "TKFA.DL" and "TKFA.DLL\X" do not; for one loaded from
   C:\TKMN\TKFN (no extension), "TKFN", "TKFN.", "TKFNX" and "TKFN.DLL"
   match. */
module FAR *module_find_file(const char FAR *name)
{
    const char FAR *n = last_part(name);
    module FAR *m;
    for (m = module_first; m; m = m->next) {
        const char FAR *f = last_part(m->path);
        u16 i;
        if (!f[0])
            continue;
        for (i = 0; f[i] && upc(f[i]) == upc(n[i]); i++)
            ;
        if (!f[i])
            return m;
    }
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
            if (m->seg[s].sel && same_sel(m->seg[s].sel, h))  /* (an empty slot is not 1-3: audit, low) */
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

/* Does the counted string at p (its length byte and its characters) lie
   inside its segment? A malformed file's name table or import name
   offset can run past it (KERNEL audit, medium 16). */
int pstr_fits(const u8 FAR *p)
{
    u32 lim = (u32)sel_lim16(FP_SEG(p));
    return FP_OFF(p) <= lim && (u32)FP_OFF(p) + p[0] <= lim;
}

/* Look a length-prefixed name up in a name table (resident or
   non-resident), byte for byte; the first entry (module name or
   description) is not an export. The table ends at its zero byte, or at
   its segment's end (a malformed file: KERNEL audit, medium 16). */
static int find_in_table(const u8 FAR *p, const u8 FAR *pname, u16 *ordinal)
{
    int first = 1;
    u32 lim = (u32)sel_lim16(FP_SEG(p));

    while ((u32)FP_OFF(p) <= lim && p[0]) {
        u8 n = p[0], i;
        if ((u32)FP_OFF(p) + n + 2 > lim)       /* name and ordinal */
            return 0;
        if (!first && n == pname[0]) {
            for (i = 0; i < n && p[1 + i] == pname[1 + i]; i++)
                ;
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

/* The ordinal a name stands for, as GetProcAddress, imports by name and
   the search for WEP find it. Observed on Windows 3.1 (tests/apps/tk65.c,
   both kernels, both modes):
   - "#" and decimal digits is that ordinal, in 16 bits ("#65537" is 1;
     "#00000000000000000002" is 2); any other character after the "#"
     ("#1x", "# 1", "#+1") or none gives 0. Imports by name too ("#4"
     imports ordinal 4).
   - any other name is upper-cased (a-z, and E0h-FEh but F7h: E4h finds
     an export C4h...; 9Ah and FFh stay, unlike Reserved3's case) and then
     compared byte for byte with the resident and then the non-resident
     names: an export whose name is in mixed or lower case is found by no
     spelling, by GetProcAddress or as an import. (Pietrek, Windows
     Internals ch. 8, FindOrdinal: "uppercases each character of the input
     parameter string"; the linker upper-cases the tables.) Before, w16
     ignored case on both sides and found mixed-case exports. */
static u8 fo_name[256];         /* (not on the caller's stack) */

int mod_find_name(module FAR *m, const u8 FAR *pname, u16 *ordinal)
{
    u8 n = pname[0], i;

    if (n && pname[1] == '#') {
        u16 v = 0;
        if (n == 1)
            return 0;
        for (i = 2; i <= n; i++) {
            if (pname[i] < '0' || pname[i] > '9')
                return 0;
            v = v * 10 + (u16)(pname[i] - '0');
        }
        *ordinal = v;
        return v != 0;
    }
    fo_name[0] = n;
    for (i = 1; i <= n && i; i++) {
        u8 c = pname[i];
        if ((c >= 'a' && c <= 'z') || (c >= 0xE0 && c <= 0xFE && c != 0xF7))
            c -= 0x20;
        fo_name[i] = c;
    }
    if (find_in_table(MK_FP(m->ne_sel, NE_HDR(m)->resname_off), fo_name, ordinal))
        return 1;
    if (m->nonres_sel)
        return find_in_table(MK_FP(m->nonres_sel, 0), fo_name, ordinal);
    /* KERNEL's own non-resident names (SetErrorMode, 107; GetModuleUsage,
       48 ...), which its module table, built by the bootstrap, does not
       hold: read from KRNL286.EXE or KRNL386.EXE for the lookup. Observed
       on Windows 3.1 (tests/apps/tk76.c): GetProcAddress finds them by
       name, and a module imports them by name. w16 found none: Win32s
       1.30c's Setup (Microsoft Test Driver) stopped at once with
       "Bindtime error: 'SETERRORMODE' not found in specified library". */
    {
        u16 sel = loader_read_nonres(m);
        int r;
        if (!sel)
            return 0;
        r = find_in_table(MK_FP(sel, 0), fo_name, ordinal);
        GlobalFree(sel);
        return r;
    }
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
   NE header in memory (odd: RPL bits set; word 0 is "NE"); MAKELP(0, h)
   looks a handle up (an hModule, an hInstance, hInstance | 1, a code
   selector; not an hTask); NULL gives 0.
   A name (tests/apps/tk60.c, the same under KRNL286 and KRNL386, in
   Standard and 386 Enhanced mode): first the whole string is compared
   with the module names, regardless of case ("TKFB" and "tkfb" find
   module TKFB; "TKFB.", "TKFB ", "C:\X\TKFB" and "TKFB.DLL" do not); then
   with the modules' file names as module_find_file does ("TKFA.DLL" and
   "C:\X\TKFA.DLL" find module TKFB loaded from TKFA.DLL; "TKFA" does
   not). With a module TKFN and another module loaded from the file TKFN,
   "TKFN" finds the module named TKFN. */
u32 KAPI GetModuleHandle(const char FAR *name)                /* KERNEL.47 */
{
    module FAR *m;
    /* DX: the first module table of the list, KERNEL's (Pietrek ch. 3,
       "upon return from this call, DX contains the module handle of the
       first module in the system"; TK60 on 3.1 under KRNL386, Standard
       and 386 Enhanced mode: KERNEL's for a name found or not and for
       MAKELP(0, h) whatever h is; NULL gives 0:0). Visual C++ 1.52's
       Windows Process Status (WPS.EXE) walks the module list from it;
       w16 left a selector of its own in DX, and WPS faulted on the "KE"
       of KERNEL's name read as a selector. */
    u32 head = (u32)(module_first ? module_first->ne_sel : 0) << 16;

    if (!name)
        return 0;
    if (!FP_SEG(name)) {
        m = module_of(FP_OFF(name));
        return head | (m ? m->ne_sel : 0);
    }
    m = module_find(name);
    if (!m)
        m = module_find_file(name);
    return head | (m ? m->ne_sel : 0);
}

/* Observed: returns the length copied; at most n - 1 characters and a
   NUL; nothing written for n = 0; an unknown handle gives 0 and an empty
   string. */
u16 KAPI_ES GetModuleFileName(u16 h, char FAR *buf, u16 n)       /* KERNEL.49 */
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

/* Observed: the name is upper-cased and then must match an export's name
   exactly, or is "#n" for ordinal n (mod_find_name, TK65); bit 15 of an
   ordinal is ignored; ordinal 0, gaps and unknown names give 0; a
   constant export comes back as FFFF:value; names in the non-resident
   table are found too; a program's own exports are not (TK5.EXE: 0 by
   name and by ordinal, through hModule or hInstance), but with hInstance
   0 they are: 0 is the current task's module (TK65, as Pietrek's
   pseudocode has it). UNVERIFIED: whether the program check is decided
   by the library flag (as here) or by the module having a task. */
u32 KAPI GetProcAddress(u16 h, const char FAR *name)          /* KERNEL.50 */
{
    module FAR *m = module_of(h);
    u16 ord = 0, sel, off;
    int is_const;

    if (!h)                     /* observed (TK65): the current task's own */
        m = task_module();      /* exports (Pietrek, ch. 8) */
    else if (m && !(m->flags & NEF_LIBRARY))
        return 0;
    if (!m)
        return 0;
    if (!FP_SEG(name)) {
        ord = FP_OFF(name);     /* (NULL: ordinal 0) */
    } else {
        static u8 pname[256];   /* length-prefixed: at most 255 characters;
                                   not on the caller's stack (KERNEL
                                   audit #16) */
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
    ord &= 0x7FFF;              /* observed: also "#32769" is 1 */
    if (!ord || !module_entry(m, ord, &sel, &off, &is_const))
        return 0;
    if (is_const)
        sel = 0xFFFF;
    else if (trace_wanted(m->name, ord))
        thunk_trace(m->name, ord, &sel, &off);  /* [w16] trace=: a procedure
                                                   found at run time too */
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
