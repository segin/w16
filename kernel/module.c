/*
 * module.c - module table and NE loader.
 *
 * M1 scope: load an NE module's segments, apply all relocations, resolve
 * imports against already-loaded modules (only KERNEL for now). Imports
 * that cannot be resolved are bound to thunks that report the missing
 * function and end the task, so a missing API is easy to find.
 *
 * Not yet: loading dependent DLLs, demand loading / discarding, the
 * Windows module database (MDB) layout, self-loading applications,
 * floating-point OS fixups, LibMain for DLLs.
 *
 * NE format source: see include/w16/ne.h.
 */
#include "kernel.h"

module modules[MAX_MODULES];

/* ---- KERNEL constant exports (see the KCONST segment in kentry.asm) ---- */

static struct {
    u16 ordinal;
    u16 para;           /* real-mode paragraph for selector constants */
    u16 value;
} kconst[] = {
    { 113, 0, 0 },      /* __AHSHIFT */
    { 114, 0, 0 },      /* __AHINCR */
    { 178, 0, 0 },      /* __WINFLAGS */
    { 173, 0xF000, 0 }, /* __ROMBIOS  UNVERIFIED: base of the ROM BIOS selector */
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

/* ---- module table ---- */

static void set_name(module *m)
{
    const u8 FAR *p = MK_FP(m->ne_sel, NE_HDR(m)->resname_off);
    u8 n = p[0] > 8 ? 8 : p[0], i;
    for (i = 0; i < n; i++) {
        char c = (char)p[1 + i];
        m->name[i] = (c >= 'a' && c <= 'z') ? (char)(c - 32) : c;
    }
    m->name[n] = 0;
}

static module *mod_alloc(void)
{
    int i;
    for (i = 0; i < MAX_MODULES; i++)
        if (!modules[i].in_use) {
            kmemset(&modules[i], 0, sizeof modules[i]);
            modules[i].in_use = 1;
            return &modules[i];
        }
    return NULL;
}

void module_register_kernel(const bootinfo FAR *bi)
{
    module *m = mod_alloc();
    u16 i;
    m->ne_sel = bi->ne_sel;
    m->ne_len = bi->ne_len;
    m->seg_count = bi->seg_count;
    for (i = 0; i < bi->seg_count; i++)
        m->seg_sel[i] = m->seg_hnd[i] = bi->seg_sel[i];
    m->usage = 1;
    kmemcpy(m->path, bi->path, sizeof m->path);
    set_name(m);
    kconst_init();
}

module *module_find(const char FAR *name)
{
    int i;
    for (i = 0; i < MAX_MODULES; i++)
        if (modules[i].in_use && kstricmp(modules[i].name, name) == 0)
            return &modules[i];
    return NULL;
}

/* Resolve an entry ordinal to sel:off, or to a constant (is_const, value
   in *off and *sel). */
int module_entry(module *m, u16 ordinal, u16 *sel, u16 *off, int *is_const)
{
    ne_header FAR *h = NE_HDR(m);
    const u8 FAR *p = MK_FP(m->ne_sel, h->entry_off);
    const u8 FAR *end = p + h->entry_len;
    u16 ord = 1;

    *is_const = 0;
    if (m == &modules[0] && kconst_lookup(ordinal, off)) {
        *sel = *off;
        *is_const = 1;
        return 1;
    }
    while (p < end && p[0]) {
        u8 count = p[0], type = p[1];
        p += 2;
        if (type == NEB_UNUSED) {
            ord += count;
            continue;
        }
        if (ordinal < ord)
            return 0;                   /* in a gap: not exported */
        if (ordinal < ord + count) {
            u16 k = ordinal - ord, segno;
            if (type == NEB_MOVABLE) {
                const ne_entry_movable FAR *e = (const ne_entry_movable FAR *)p + k;
                segno = e->seg;
                *off = e->offset;
            } else {
                const ne_entry_fixed FAR *e = (const ne_entry_fixed FAR *)p + k;
                if (type == NEB_CONSTANT) {
                    *sel = *off = e->offset;
                    *is_const = 1;
                    return 1;
                }
                segno = type;
                *off = e->offset;
            }
            if (segno < 1 || segno > m->seg_count)
                return 0;
            *sel = m->seg_sel[segno - 1];
            return 1;
        }
        p += (type == NEB_MOVABLE ? sizeof(ne_entry_movable)
                                  : sizeof(ne_entry_fixed)) * count;
        ord += count;
    }
    return 0;
}

/* Find an exported name (length-prefixed) in the resident name table. */
static int find_resident_name(module *m, const u8 FAR *pname, u16 *ordinal)
{
    ne_header FAR *h = NE_HDR(m);
    const u8 FAR *p = MK_FP(m->ne_sel, h->resname_off);
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

/* Record the module's file as a full path (observed: GetModuleFileName
   gives e.g. C:\TK4.EXE for a program started as TK4.EXE), using DOS
   TRUENAME (INT 21h AH=60h). A path that does not fit is a DOS limit. */
static void set_path(module *m, const char FAR *path)
{
    char full[128];             /* TRUENAME's output buffer size (DOS) */
    regs16 r;
    u16 i;

    r.ax = 0x6000;
    r.si = FP_OFF(path);
    r.ds = FP_SEG(path);
    r.di = FP_OFF((char FAR *)full);
    r.es = FP_SEG((char FAR *)full);
    if (!dos21(&r) && kstrlen(full) < sizeof m->path)
        path = full;
    for (i = 0; i < sizeof m->path - 1 && path[i]; i++)
        m->path[i] = path[i];
    m->path[i] = 0;
}

/* ---- file access (INT 21h, translated by the DOS extender) ---- */

static u16 fh;

static int f_seek(u32 pos)
{
    regs16 r;
    r.ax = 0x4200;
    r.bx = fh;
    r.cx = (u16)(pos >> 16);
    r.dx = (u16)pos;
    r.ds = r.es = 0;
    return !dos21(&r);
}

static int f_read(void FAR *buf, u16 n)
{
    regs16 r;
    r.ax = 0x3F00;
    r.bx = fh;
    r.cx = n;
    r.dx = FP_OFF(buf);
    r.ds = FP_SEG(buf);
    r.es = 0;
    return !dos21(&r) && r.ax == n;
}

static int f_read_big(u16 sel, u32 len)
{
    if (len > 0x8000UL)
        return f_read(MK_FP(sel, 0), 0x8000) &&
               f_read(MK_FP(sel, 0x8000), (u16)(len - 0x8000UL));
    return len ? f_read(MK_FP(sel, 0), (u16)len) : 1;
}

static void f_close(void)
{
    regs16 r;
    r.ax = 0x3E00;
    r.bx = fh;
    r.ds = r.es = 0;
    dos21(&r);
}

static module *load_fail(const char FAR *path, const char *why)
{
    kputs("w16: cannot load ");
    kputs(path);
    kputs(": ");
    kputs(why);
    kputs("\r\n");
    return NULL;
}

/* ---- relocation ---- */

/* Copy a length-prefixed string (at most 8 chars) to a C string. */
static void pstr_to_c(const u8 FAR *p, char *out, u16 max)
{
    u16 n = p[0] < max - 1 ? p[0] : max - 1, i;
    for (i = 0; i < n; i++)
        out[i] = (char)p[1 + i];
    out[n] = 0;
}

static const u8 FAR *imported_name(module *m, u16 offset)
{
    return MK_FP(m->ne_sel, NE_HDR(m)->impname_off + offset);
}

static const u8 FAR *modref_name(module *m, u16 index)
{
    ne_header FAR *h = NE_HDR(m);
    if (index < 1 || index > h->modref_count)
        return NULL;
    return imported_name(m, *(const u16 FAR *)MK_FP(m->ne_sel, h->modref_off + 2 * (index - 1)));
}

static void patch(u16 sel, const ne_reloc FAR *rr, u16 tsel, u16 toff)
{
    u8 FAR *p = MK_FP(sel, 0);
    u16 off = rr->offset;
    u8 src = rr->src & NERS_MASK;
    int add = (rr->flags & NERF_ADDITIVE) != 0;

    for (;;) {
        u16 FAR *w = (u16 FAR *)(p + off);
        u16 next = *w;
        switch (src) {
        case NERS_LOBYTE:
            p[off] = add ? (u8)(p[off] + toff) : (u8)toff;
            add = 1;
            break;
        case NERS_SEGMENT:
            *w = tsel;
            break;
        case NERS_OFFSET:
            *w = add ? *w + toff : toff;
            break;
        case NERS_FARPTR:
            w[0] = add ? w[0] + toff : toff;
            w[1] = tsel;
            break;
        default:
            return;             /* 32-bit fixups: not valid in Win16 */
        }
        if (add || next == NE_CHAIN_END)
            break;
        off = next;
    }
}

/* Resolve one relocation's target. Returns 0 if the record is skipped. */
static int target(module *m, const ne_reloc FAR *rr, u16 *tsel, u16 *toff)
{
    int is_const;
    u16 segno, ord;
    const u8 FAR *modname;
    module *tm;
    char name[9];

    switch (rr->flags & NERF_TYPEMASK) {
    case NERF_INTERNAL:
        segno = rr->t1 & 0xFF;
        if (segno == NE_MOVABLE_SEG)
            return module_entry(m, rr->t2, tsel, toff, &is_const);
        if (segno < 1 || segno > m->seg_count)
            return 0;
        *tsel = m->seg_sel[segno - 1];
        *toff = rr->t2;
        return 1;

    case NERF_IMPORTORD:
    case NERF_IMPORTNAME:
        modname = modref_name(m, rr->t1);
        if (!modname)
            return 0;
        pstr_to_c(modname, name, sizeof name);
        tm = module_find(name);
        if ((rr->flags & NERF_TYPEMASK) == NERF_IMPORTORD) {
            if (tm && module_entry(tm, rr->t2, tsel, toff, &is_const))
                return 1;
            return thunk_make(name, rr->t2, NULL, tsel, toff);
        }
        if (tm && find_resident_name(tm, imported_name(m, rr->t2), &ord) &&
            module_entry(tm, ord, tsel, toff, &is_const))
            return 1;
        return thunk_make(name, 0, imported_name(m, rr->t2), tsel, toff);

    default:
        return 0;               /* OS fixups: TODO floating-point patching */
    }
}

static int relocate_segment(module *m, u16 i, u32 data_end)
{
    ne_reloc buf[32];
    u16 n, done = 0;

    if (!f_seek(data_end) || !f_read(&n, 2))
        return 0;
    while (done < n) {
        u16 k, batch = n - done > 32 ? 32 : n - done;
        if (!f_read(buf, batch * sizeof(ne_reloc)))
            return 0;
        for (k = 0; k < batch; k++) {
            u16 tsel, toff;
            if (target(m, &buf[k], &tsel, &toff))
                patch(m->seg_sel[i], &buf[k], tsel, toff);
        }
        done += batch;
    }
    return 1;
}

/* ---- loader ---- */

module *module_load(const char FAR *path)
{
    regs16 r;
    u8 mz[64];
    ne_header h;
    u32 ne_base;
    u16 i, shift, sel;
    module *m;

    r.ax = 0x3D00;
    r.dx = FP_OFF(path);
    r.ds = FP_SEG(path);
    r.es = 0;
    if (dos21(&r))
        return load_fail(path, "file not found");
    fh = r.ax;

    if (!f_read(mz, sizeof mz) || *(u16 *)mz != MZ_MAGIC) {
        f_close();
        return load_fail(path, "not an executable");
    }
    ne_base = *(u32 *)(mz + MZ_LFANEW_OFF);
    if (!f_seek(ne_base) || !f_read(&h, sizeof h) || h.magic != NE_MAGIC) {
        f_close();
        return load_fail(path, "not a Windows (NE) executable");
    }
    if (h.flags & NEF_SELFLOAD) {
        f_close();
        return load_fail(path, "self-loading applications are not supported yet");
    }
    if (h.seg_count > MAX_MODSEGS) {
        f_close();
        return load_fail(path, "too many segments");
    }
    m = mod_alloc();
    if (!m) {
        f_close();
        return load_fail(path, "module table full");
    }
    m->ne_len = h.entry_off + h.entry_len;
    if (!galloc(GMEM_FIXED, m->ne_len, 0, &m->ne_sel) ||
        !f_seek(ne_base) || !f_read(MK_FP(m->ne_sel, 0), m->ne_len)) {
        f_close();
        m->in_use = 0;
        return load_fail(path, "cannot read module tables");
    }
    set_name(m);
    m->seg_count = h.seg_count;
    m->usage = 1;
    set_path(m, path);
    shift = h.align_shift ? h.align_shift : 9;

    /* allocate and read every segment */
    for (i = 0; i < h.seg_count; i++) {
        const ne_segment FAR *s = (const ne_segment FAR *)MK_FP(m->ne_sel, h.seg_off) + i;
        u32 size = s->min_alloc ? s->min_alloc : 0x10000UL;
        u32 flen = s->sector ? (s->file_len ? s->file_len : 0x10000UL) : 0;
        if (flen > size)
            size = flen;
        if (i + 1 == h.auto_data) {
            size += (u32)h.heap_size + h.stack_size;
            if (size > 0x10000UL)
                size = 0x10000UL;
        }
        m->seg_hnd[i] = galloc((s->flags & NESEG_MOVABLE) ? GMEM_MOVEABLE : GMEM_FIXED,
                               size, m->ne_sel, &sel);
        m->seg_sel[i] = sel;
        if (!m->seg_hnd[i]) {
            f_close();
            return load_fail(path, "out of memory");
        }
        if (flen && (!f_seek((u32)s->sector << shift) || !f_read_big(sel, flen))) {
            f_close();
            return load_fail(path, "cannot read segment");
        }
    }

    /* relocations: all segments are writable data selectors until now */
    for (i = 0; i < h.seg_count; i++) {
        const ne_segment FAR *s = (const ne_segment FAR *)MK_FP(m->ne_sel, h.seg_off) + i;
        if ((s->flags & NESEG_RELOCS) && s->sector) {
            u32 flen = s->file_len ? s->file_len : 0x10000UL;
            if (!relocate_segment(m, i, ((u32)s->sector << shift) + flen)) {
                f_close();
                return load_fail(path, "bad relocation data");
            }
        }
    }
    f_close();

    for (i = 0; i < h.seg_count; i++) {
        const ne_segment FAR *s = (const ne_segment FAR *)MK_FP(m->ne_sel, h.seg_off) + i;
        if (!(s->flags & NESEG_DATA))
            gset_code(m->seg_sel[i]);
    }
    return m;
}

/* ---- API ---- */

static int same_sel(u16 a, u16 b)
{
    return (a | 3) == (b | 3);          /* ignore RPL and the handle bit */
}

/* The module a module handle, instance handle or other segment of the
   module refers to. Observed: GetModuleHandle(MAKELP(0, h)) accepts the
   hModule, hInstance and hInstance | 1. */
static module *module_of(u16 h)
{
    int i;
    u16 s;
    if (!h)
        return NULL;
    for (i = 0; i < MAX_MODULES; i++) {
        module *m = &modules[i];
        if (!m->in_use)
            continue;
        if (same_sel(m->ne_sel, h))
            return m;
        for (s = 0; s < m->seg_count; s++)
            if (same_sel(m->seg_sel[s], h))
                return m;
    }
    return NULL;
}

/* Observed on Windows 3.1: the hModule is the selector of the module's
   NE header in memory (odd: RPL bits set; word 0 is "NE"); names match
   regardless of case; "TK4.EXE" and "C:\TK4.EXE" find module TK4, "TK4 "
   does not; MAKELP(0, handle) looks a handle up; NULL gives 0.
   UNVERIFIED: whether a file name is matched against the module name (as
   here) or the module's file name; they were the same in the probe. */
u16 KAPI GetModuleHandle(const char FAR *name)                /* KERNEL.47 */
{
    char base[9];
    const char FAR *p, *s;
    u16 n = 0;
    module *m;

    if (!FP_SEG(name)) {
        m = module_of(FP_OFF(name));
        return m ? m->ne_sel : 0;
    }
    s = name;
    for (p = name; *p; p++)
        if (*p == '\\' || *p == '/' || *p == ':')
            s = p + 1;
    for (p = s; *p && *p != '.'; p++)
        if (n < sizeof base - 1)
            base[n++] = *p;
        else
            return 0;                   /* longer than any module name */
    base[n] = 0;
    if (!n)
        return 0;
    m = module_find((const char FAR *)base);
    return m ? m->ne_sel : 0;
}

/* Observed: returns the length copied; at most n - 1 characters and a
   NUL; nothing written for n = 0; an unknown handle gives 0 and an empty
   string. */
u16 KAPI GetModuleFileName(u16 h, char FAR *buf, u16 n)       /* KERNEL.49 */
{
    module *m = module_of(h);
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
   0; a constant export comes back as FFFF:value.
   UNVERIFIED: the non-resident name table is not searched yet (KERNEL
   puts all its names in the resident one); hModule 0. */
u32 KAPI GetProcAddress(u16 h, const char FAR *name)          /* KERNEL.50 */
{
    module *m = module_of(h);
    u16 ord = 0, sel, off;
    int is_const;

    if (!m || !name)
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
        if (!find_resident_name(m, (const u8 FAR *)pname, &ord))
            return 0;
    }
    if (!ord || !module_entry(m, ord, &sel, &off, &is_const))
        return 0;
    if (is_const)
        sel = 0xFFFF;
    return ((u32)sel << 16) | off;
}
