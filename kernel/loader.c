/*
 * loader.c - the NE loader: finding a module's file, loading it and the
 * modules it imports from, relocating, initialising and freeing it;
 * LoadLibrary, FreeLibrary, LoadModule, FreeModule, GetWindowsDirectory,
 * GetSystemDirectory.
 *
 * Behaviour observed black-box on Windows 3.1 (Standard and 386 Enhanced
 * mode agree) with tests/apps/tk5.c and its DLLs; ARCHITECTURE.md 9.2
 * lists the findings. Not yet: demand loading and discarding, the
 * Windows module database (MDB) layout, self-loading modules,
 * floating-point OS fixups, starting a second task (LoadModule of a
 * program).
 *
 * NE format source: see include/w16/ne.h.
 */
#include "kernel.h"

/* LoadLibrary / LoadModule results below 32 (observed on Windows 3.1) */
#define LE_FAILED       0       /* the DLL's initialisation returned 0 */
#define LE_NOTFOUND     2       /* also: a module it imports is missing */
#define LE_NOPATH       3
#define LE_BADFORMAT    20      /* not MZ, or MZ without an NE header */

/* UNVERIFIED: these are w16's own choices. */
#define LE_NOMEM        8       /* out of memory */
#define LE_PROGRAM      16      /* a program, not a DLL: w16 has one task */

/* Windows' own modules that w16 does not provide yet. A program that
   imports from one of them still loads: the imports go to the
   "unimplemented" thunks (thunk.c). Any other missing module fails the
   load, as on Windows. */
static const char *const pending_modules[] = {
    "USER", "GDI", "KEYBOARD", "SOUND", "SYSTEM", "DISPLAY", "MOUSE",
    "COMM", "WIN87EM", NULL
};

static char win_dir[DOS_PATHMAX], sys_dir[DOS_PATHMAX];

/* ---- file access (INT 21h, translated by the DOS extender) ---- */

static int f_open(const char FAR *path, u16 *fh, u16 *err)
{
    regs16 r;
    r.ax = 0x3D00;
    r.dx = FP_OFF(path);
    r.ds = FP_SEG(path);
    r.es = 0;
    if (dos21(&r)) {
        *err = r.ax;
        return 0;
    }
    *fh = r.ax;
    return 1;
}

static int f_seek(u16 fh, u32 pos)
{
    regs16 r;
    r.ax = 0x4200;
    r.bx = fh;
    r.cx = (u16)(pos >> 16);
    r.dx = (u16)pos;
    r.ds = r.es = 0;
    return !dos21(&r);
}

static int f_read(u16 fh, void FAR *buf, u16 n)
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

static int f_read_big(u16 fh, u16 sel, u32 len)
{
    if (len > 0x8000UL)
        return f_read(fh, MK_FP(sel, 0), 0x8000) &&
               f_read(fh, MK_FP(sel, 0x8000), (u16)(len - 0x8000UL));
    return len ? f_read(fh, MK_FP(sel, 0), (u16)len) : 1;
}

static void f_close(u16 fh)
{
    regs16 r;
    r.ax = 0x3E00;
    r.bx = fh;
    r.ds = r.es = 0;
    dos21(&r);
}

/* ---- directories and the search ---- */

static void str_copy(char *d, const char FAR *s, u16 max)
{
    u16 i;
    for (i = 0; i + 1 < max && s[i]; i++)
        d[i] = s[i];
    d[i] = 0;
}

/* dir of a full path, without the trailing backslash except for a root */
static void dir_of(char *d, const char FAR *path)
{
    u16 i, cut = 0;
    str_copy(d, path, DOS_PATHMAX);
    for (i = 0; d[i]; i++)
        if (d[i] == '\\')
            cut = i;
    if (cut == 2 && d[1] == ':')
        cut = 3;                        /* "C:\" */
    d[cut] = 0;
}

/* The value of an environment variable of KERNEL's environment (the
   name in exact case), or NULL. */
static const char FAR *env_var(const char *name)
{
    u16 env = *(u16 FAR *)MK_FP(psp_sel, 0x2C);
    const char FAR *e;
    if (!env)
        return NULL;
    for (e = MK_FP(env, 0); *e; ) {
        const char *n = name;
        const char FAR *p = e;
        while (*n && *p == *n) {
            n++;
            p++;
        }
        if (!*n && *p == '=')
            return p + 1;
        while (*e)
            e++;
        e++;
    }
    return NULL;
}

/* The system directory is the one KRNL286.EXE is in (as on Windows,
   where it is in WINDOWS\SYSTEM). The Windows directory is the value of
   the environment variable "windir", which WIN.COM (or LOSE.COM) passes:
   observed on Windows 3.1, every task's environment holds lowercase
   "windir=C:\WINDOWS", the same as GetWindowsDirectory. Without it (KERNEL
   started directly), w16 takes the parent of the system directory if that
   is called SYSTEM, else the system directory itself. */
void loader_init(const char FAR *kernel_path)
{
    const char FAR *w = env_var("windir");
    u16 n;
    dir_of(sys_dir, kernel_path);
    if (w && *w && kstrlen(w) < sizeof win_dir) {
        str_copy(win_dir, w, sizeof win_dir);
        n = kstrlen(win_dir);
        if (n > 3 && win_dir[n - 1] == '\\')
            win_dir[n - 1] = 0;         /* as GetWindowsDirectory gives it */
        return;
    }
    str_copy(win_dir, sys_dir, sizeof win_dir);
    n = kstrlen(win_dir);
    if (n > 7 && kstricmp(win_dir + n - 7, "\\SYSTEM") == 0) {
        win_dir[n - 7] = 0;
        if (n - 7 == 2)
            str_copy(win_dir, sys_dir, 4);     /* "C:\SYSTEM" -> "C:\" */
    }
}

static int has_path(const char FAR *name)
{
    for (; *name; name++)
        if (*name == '\\' || *name == '/' || *name == ':')
            return 1;
    return 0;
}

/* dir + "\" + name (+ ext) into out; 0 if it does not fit a DOS path */
static int join(char *out, const char FAR *dir, u16 dlen, const char FAR *name,
                const char *ext)
{
    u16 n = 0, i;
    for (i = 0; i < dlen; i++) {
        if (n >= DOS_PATHMAX - 1)
            return 0;
        out[n++] = dir[i];
    }
    if (n && out[n - 1] != '\\' && out[n - 1] != ':') {
        if (n >= DOS_PATHMAX - 1)
            return 0;
        out[n++] = '\\';
    }
    for (; *name; name++) {
        if (n >= DOS_PATHMAX - 1)
            return 0;
        out[n++] = *name;
    }
    for (; ext && *ext; ext++) {
        if (n >= DOS_PATHMAX - 1)
            return 0;
        out[n++] = *ext;
    }
    out[n] = 0;
    return 1;
}

static int try_dir(const char FAR *dir, u16 dlen, const char FAR *name,
                   const char *ext, char *found, u16 *fh)
{
    u16 err;
    return join(found, dir, dlen, name, ext) && f_open(found, fh, &err);
}

/* The PATH variable of KERNEL's environment, or NULL. */
static const char FAR *env_path(void)
{
    return env_var("PATH");
}

/* Open a module's file. A name with a path is opened as it is (the DOS
   error is the result: 2 or 3). Otherwise, observed on Windows 3.1 (each
   directory holding a copy, removed one by one): the current directory,
   the Windows directory, the system directory, the directory of the
   running task's program, then the PATH directories; no extension is
   added (LoadLibrary("TKNOD") did not find TKNOD.DLL). When Windows
   loads its shell there is no running task, and DLLs in the shell's own
   directory were not found.
   UNVERIFIED: network directories (the documentation lists them last). */
static int open_search(const char FAR *name, const char *ext, char *found,
                       u16 *fh, u16 *err)
{
    module FAR *t;
    const char FAR *p;
    char dir[DOS_PATHMAX];

    if (has_path(name)) {
        if (!join(found, "", 0, name, ext)) {
            *err = LE_NOPATH;
            return 0;
        }
        return f_open(found, fh, err);
    }
    *err = LE_NOTFOUND;
    if (try_dir("", 0, name, ext, found, fh) ||
        try_dir(win_dir, kstrlen(win_dir), name, ext, found, fh) ||
        try_dir(sys_dir, kstrlen(sys_dir), name, ext, found, fh))
        return 1;
    t = task_module();
    if (t) {
        dir_of(dir, t->path);
        if (try_dir(dir, kstrlen(dir), name, ext, found, fh))
            return 1;
    }
    for (p = env_path(); p && *p; ) {
        u16 n = 0;
        while (p[n] && p[n] != ';')
            n++;
        if (n && try_dir(p, n, name, ext, found, fh))
            return 1;
        p += n;
        if (*p == ';')
            p++;
    }
    return 0;
}

/* ---- relocation ---- */

/* Copy a length-prefixed string to a C string of at most max - 1 chars. */
static void pstr_to_c(const u8 FAR *p, char *out, u16 max)
{
    u16 n = p[0] < max - 1 ? p[0] : max - 1, i;
    for (i = 0; i < n; i++)
        out[i] = (char)p[1 + i];
    out[n] = 0;
}

static const u8 FAR *imported_name(module FAR *m, u16 offset)
{
    return MK_FP(m->ne_sel, NE_HDR(m)->impname_off + offset);
}

/* The name of imported module `index` (1-based), from the file's tables:
   the module table holds hModules there instead. Only while loading. */
static const u8 FAR *modref_name(module FAR *m, u16 index)
{
    const ne_header FAR *fh = MK_FP(m->ft_sel, 0);
    if (!m->ft_sel || index < 1 || index > fh->modref_count)
        return NULL;
    return imported_name(m, *(const u16 FAR *)MK_FP(m->ft_sel, fh->modref_off + 2 * (index - 1)));
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
static int target(module FAR *m, const ne_reloc FAR *rr, u16 *tsel, u16 *toff)
{
    int is_const;
    u16 segno, ord;
    const u8 FAR *modname;
    module FAR *tm;
    char name[MODNAME_MAX + 1];

    switch (rr->flags & NERF_TYPEMASK) {
    case NERF_INTERNAL:
        segno = rr->t1 & 0xFF;
        if (segno == NE_MOVABLE_SEG)
            return module_entry(m, rr->t2, tsel, toff, &is_const);
        if (segno < 1 || segno > m->seg_count)
            return 0;
        *tsel = m->seg[segno - 1].sel;
        *toff = rr->t2;
        return 1;

    case NERF_IMPORTORD:
    case NERF_IMPORTNAME:
        modname = modref_name(m, rr->t1);
        if (!modname)
            return 0;
        pstr_to_c(modname, name, sizeof name);
        tm = module_of(MOD_MODREF(m, rr->t1 - 1));
        if ((rr->flags & NERF_TYPEMASK) == NERF_IMPORTORD) {
            if (tm && module_entry(tm, rr->t2, tsel, toff, &is_const))
                return 1;
            return thunk_make(name, rr->t2, NULL, tsel, toff);
        }
        if (tm && mod_find_name(tm, imported_name(m, rr->t2), &ord) &&
            module_entry(tm, ord, tsel, toff, &is_const))
            return 1;
        return thunk_make(name, 0, imported_name(m, rr->t2), tsel, toff);

    default:
        return 0;               /* OS fixups: TODO floating-point patching */
    }
}

static int relocate_segment(module FAR *m, u16 fh, u16 i, u32 data_end)
{
    ne_reloc buf[32];
    u16 n, done = 0;

    if (!f_seek(fh, data_end) || !f_read(fh, &n, 2))
        return 0;
    while (done < n) {
        u16 k, batch = n - done > 32 ? 32 : n - done;
        if (!f_read(fh, buf, batch * sizeof(ne_reloc)))
            return 0;
        for (k = 0; k < batch; k++) {
            u16 tsel, toff;
            if (target(m, &buf[k], &tsel, &toff))
                patch(m->seg[i].sel, &buf[k], tsel, toff);
        }
        done += batch;
    }
    return 1;
}

/* Prologues of exported procedures. Observed on Windows 3.1: in a DLL,
   "mov ax, ds; nop" (8C D8 90) and "push ds; pop ax; nop" (1E 58 90) at
   an exported entry become "mov ax, DGROUP" (B8 and the DS selector); in a
   program they become three NOPs, so the AX a MakeProcInstance thunk
   loads is used. (Published descriptions, e.g. Pietrek's "Windows
   Internals", describe the same.)
   UNVERIFIED: entries that are not exported, and DLLs without DGROUP
   (left alone here). */
static void patch_prologues(module FAR *m)
{
    ne_header FAR *h = NE_HDR(m);
    u16 t = m->ne_sel, b;
    int lib = (m->flags & NEF_LIBRARY) != 0;
    u16 dgroup = h->auto_data && h->auto_data <= m->seg_count
                 ? m->seg[h->auto_data - 1].sel : 0;

    if (lib && !dgroup)
        return;
    /* the in-memory entry table (mdb.c) */
    for (b = MDB_W(t, MDB_ENTRY); b; b = MDB_W(t, b + 4)) {
        u16 k, count = MDB_W(t, b + 2) - MDB_W(t, b);
        for (k = 0; k < count; k++) {
            const u8 FAR *e = MK_FP(t, b + 6 + 5 * k);
            u16 segno = e[2], off = *(const u16 FAR *)(e + 3);
            u8 FAR *c;
            if (e[0] == NEB_CONSTANT || !(e[1] & NEE_EXPORTED) ||
                segno < 1 || segno > m->seg_count ||
                (MOD_SEG(m, segno - 1)->flags & NESEG_DATA) ||
                (u32)off + 3 > gsize_of_sel(m->seg[segno - 1].sel))
                continue;
            c = MK_FP(m->seg[segno - 1].sel, off);
            if (!((c[0] == 0x8C && c[1] == 0xD8 && c[2] == 0x90) ||
                  (c[0] == 0x1E && c[1] == 0x58 && c[2] == 0x90)))
                continue;
            if (lib) {
                c[0] = 0xB8;
                c[1] = (u8)dgroup;
                c[2] = (u8)(dgroup >> 8);
            } else {
                c[0] = c[1] = c[2] = 0x90;
            }
        }
    }
}

/* ---- loading and freeing ---- */

static void release(module FAR *m);
static u16 entry_ds(module FAR *m);

/* Free a module's memory and release the modules it imports from. */
static void destroy(module FAR *m)
{
    u16 i, n;
    mod_unlink(m);
    for (i = 0; i < m->seg_count; i++)
        if (m->seg[i].hnd)
            GlobalFree(m->seg[i].hnd);
    if (m->nonres_sel)
        GlobalFree(m->nonres_sel);
    if (m->ft_sel)
        GlobalFree(m->ft_sel);
    if (m->ne_sel) {
        n = NE_HDR(m)->modref_count;
        for (i = 0; i < n; i++) {
            module FAR *d = module_of(MOD_MODREF(m, i));
            if (d)
                release(d);
        }
        GlobalFree(m->ne_sel);
    }
    GlobalFree(m->self);
}

/* One user less. A DLL nobody uses any more gets its WEP called (observed
   on Windows 3.1: with 0, WEP_FREE_DLL) and is freed. KERNEL and
   programs stay. */
static void call_wep(module FAR *m)
{
    static const u8 wep[] = "\3WEP";
    u16 ord, sel, off;
    int is_const;

    if (mod_find_name(m, (const u8 FAR *)wep, &ord) &&
        module_entry(m, ord, &sel, &off, &is_const) && !is_const)
        mod_call(sel, off, entry_ds(m), 0, module_instance(m), 1, 0);
}

static void release(module FAR *m)
{
    if (MOD_USAGE(m))
        MOD_USAGE(m)--;
    if (MOD_USAGE(m) || m == module_first || !(m->flags & NEF_LIBRARY))
        return;
    if (NE_HDR(m)->flags & MDBF_WEP)
        call_wep(m);
    destroy(m);
}

static int is_pending(const char *name)
{
    u16 i;
    for (i = 0; pending_modules[i]; i++)
        if (kstricmp(pending_modules[i], name) == 0)
            return 1;
    return 0;
}

static module FAR *load_file(const char FAR *name, const char *ext, int program,
                             u16 *err);

/* A module another one imports from: shared if loaded, else loaded from
   NAME.DLL. *hmod is its hModule, or 0 for a module w16 does not have
   yet (pending_modules). Returns 0 with *err set on failure.
   Observed: NAME.EXE is not tried (TKUSEX.DLL, importing TKEXEM with only
   TKEXEM.EXE present, failed with 2). */
static int acquire_import(const char *name, u16 *hmod, u16 *err)
{
    module FAR *d = module_find(name);
    *hmod = 0;
    if (!d && is_pending(name))
        return 1;
    if (d)
        MOD_USAGE(d)++;
    else if ((d = load_file(name, ".DLL", 0, err)) == NULL)
        return 0;
    *hmod = d->ne_sel;
    return 1;
}

/* DS for a DLL's entry point and WEP: the DGROUP selector; for a DLL
   without DGROUP the hModule (observed at the entry point of TKNOD.DLL;
   UNVERIFIED for WEP). */
static u16 entry_ds(module FAR *m)
{
    u16 ad = NE_HDR(m)->auto_data;
    return ad && ad <= m->seg_count ? m->seg[ad - 1].sel : m->ne_sel;
}

static module FAR *load_fail(module FAR *m, const char FAR *path, const char *why)
{
    kputs("w16: cannot load ");
    kputs(path);
    kputs(": ");
    kputs(why);
    kputs("\r\n");
    if (m)
        destroy(m);
    return NULL;
}

/* Load a module from its file (name searched as in open_search, ext
   appended), with the modules it imports from, and run a DLL's
   initialisation. Programs only if `program`. */
static module FAR *load_file(const char FAR *name, const char *ext, int program,
                             u16 *err)
{
    char path[DOS_PATHMAX];
    u8 mz[64];
    ne_header h;
    u32 ne_base;
    u16 fh, i, shift, sel, ne_len;
    module FAR *m, *dup;

    if (!open_search(name, ext, path, &fh, err))
        return NULL;
    if (!f_read(fh, mz, sizeof mz) || *(u16 *)mz != MZ_MAGIC ||
        (ne_base = *(u32 *)(mz + MZ_LFANEW_OFF)) == 0 ||
        !f_seek(fh, ne_base) || !f_read(fh, &h, sizeof h) || h.magic != NE_MAGIC) {
        f_close(fh);
        *err = LE_BADFORMAT;
        return NULL;
    }
    if (!(h.flags & NEF_LIBRARY) && !program) {
        f_close(fh);
        *err = LE_PROGRAM;
        return NULL;
    }
    *err = LE_BADFORMAT;
    if (h.flags & NEF_SELFLOAD) {
        f_close(fh);
        return load_fail(NULL, path, "self-loading modules are not supported yet");
    }
    ne_len = h.entry_off + h.entry_len;
    *err = LE_NOMEM;
    m = mod_new(h.seg_count);
    if (!m) {
        f_close(fh);
        return NULL;
    }
    m->flags = h.flags;
    if (!galloc(GMEM_FIXED, ne_len, 0, &sel)) {
        f_close(fh);
        return load_fail(m, path, "out of memory");
    }
    m->ft_sel = sel;
    *err = LE_BADFORMAT;
    if (!f_seek(fh, ne_base) || !f_read(fh, MK_FP(sel, 0), ne_len)) {
        f_close(fh);
        return load_fail(m, path, "cannot read module tables");
    }
    mod_set_path(m, path);
    {
        regs16 r;                       /* the file's date and time */
        r.ax = 0x5700;
        r.bx = fh;
        r.ds = r.es = 0;
        if (dos21(&r))
            r.cx = r.dx = 0;
        m->ne_sel = mdb_build(MK_FP(m->ft_sel, 0), m->path, r.dx, r.cx, 0);
    }
    if (!m->ne_sel) {
        f_close(fh);
        *err = LE_NOMEM;
        return load_fail(m, path, "out of memory");
    }
    mod_set_name(m);

    /* One copy per module: a file whose module is already loaded (under
       another file name) gives that module.
       UNVERIFIED: Windows 3.1 compared the file's base name with the
       loaded module names before opening anything (observed); what it
       does when the module inside is called differently is not. */
    for (dup = module_first; dup; dup = dup->next)
        if (kstricmp(dup->name, m->name) == 0) {
            f_close(fh);
            destroy(m);
            MOD_USAGE(dup)++;
            return dup;
        }
    mod_link(m);

    if (h.nonres_len) {
        if (!galloc(GMEM_FIXED, h.nonres_len + 1UL, 0, &sel)) {
            f_close(fh);
            *err = LE_NOMEM;
            return load_fail(m, path, "out of memory");
        }
        m->nonres_sel = sel;
        if (!f_seek(fh, h.nonres_off) || !f_read(fh, MK_FP(sel, 0), h.nonres_len)) {
            f_close(fh);
            return load_fail(m, path, "cannot read the non-resident names");
        }
    }

    /* allocate and read every segment */
    shift = h.align_shift ? h.align_shift : 9;
    for (i = 0; i < h.seg_count; i++) {
        ne_segment FAR *s = MOD_SEG(m, i);
        u32 size = s->min_alloc ? s->min_alloc : 0x10000UL;
        u32 flen = s->sector ? (s->file_len ? s->file_len : 0x10000UL) : 0;
        if (flen > size)
            size = flen;
        if (i + 1 == h.auto_data) {
            size += (u32)h.heap_size + h.stack_size;
            if (size > 0x10000UL)
                size = 0x10000UL;
        }
        m->seg[i].hnd = galloc((s->flags & NESEG_MOVABLE) ? GMEM_MOVEABLE : GMEM_FIXED,
                               size, m->ne_sel, &sel);
        m->seg[i].sel = sel;
        if (!m->seg[i].hnd) {
            f_close(fh);
            *err = LE_NOMEM;
            return load_fail(m, path, "out of memory");
        }
        /* in the module table: the handle, and the flags of a loaded
           segment. UNVERIFIED: Windows 3.1 changed further bits (0400h,
           the top four) in ways not understood yet. */
        MOD_SEGHND(m, i) = m->seg[i].hnd;
        s->flags = (s->flags & ~NESEG_DPL) | NESEG_ALLOCED | NESEG_LOADED | NESEG_MOVABLE;
        if (flen && (!f_seek(fh, (u32)s->sector << shift) || !f_read_big(fh, sel, flen))) {
            f_close(fh);
            return load_fail(m, path, "cannot read segment");
        }
    }
    f_close(fh);
    MOD_USAGE(m) = 1;

    /* the modules it imports from (observed: loaded, and initialised,
       before the module itself; one that is missing fails the load with
       2 and nothing stays loaded); their hModules go to the module
       reference table */
    for (i = 0; i < h.modref_count; i++) {
        char dname[MODNAME_MAX + 1];
        pstr_to_c(modref_name(m, i + 1), dname, sizeof dname);
        if (!acquire_import(dname, &sel, err)) {
            destroy(m);
            return NULL;
        }
        MOD_MODREF(m, i) = sel;
    }

    /* relocations: all segments are writable data selectors until now */
    *err = LE_BADFORMAT;
    if (!f_open(path, &fh, err))
        return load_fail(m, path, "cannot reopen");
    for (i = 0; i < h.seg_count; i++) {
        const ne_segment FAR *s = MOD_SEG(m, i);
        if ((s->flags & NESEG_RELOCS) && s->sector) {
            u32 flen = s->file_len ? s->file_len : 0x10000UL;
            if (!relocate_segment(m, fh, i, ((u32)s->sector << shift) + flen)) {
                f_close(fh);
                *err = LE_BADFORMAT;
                return load_fail(m, path, "bad relocation data");
            }
        }
    }
    f_close(fh);
    GlobalFree(m->ft_sel);
    m->ft_sel = 0;
    patch_prologues(m);
    for (i = 0; i < h.seg_count; i++)
        if (!(MOD_SEG(m, i)->flags & NESEG_DATA))
            gset_code(m->seg[i].sel);

    /* A DLL's initialisation, on the caller's stack. Observed on Windows
       3.1 at the entry point: AX = 1, CX = heap size, DX = DS = the DGROUP
       selector, DI = hInstance (DS & ~1), SI = 0, ES = 0; DS:0000-000F all
       zero and no local heap yet (the DLL makes it, LocalInit); returning
       0 makes LoadLibrary return 0, and the DLL's WEP runs before it is
       freed. A DLL without DGROUP gets DS = DX = DI = its hModule.
       UNVERIFIED: WEP's argument after a failed initialisation (0 here). */
    if (h.flags & NEF_LIBRARY)
        NE_HDR(m)->flags |= MDBF_WEP;   /* observed on DLLs (table 0Ch) */
    if ((h.flags & NEF_LIBRARY) && h.cs && h.cs <= h.seg_count) {
        if (!mod_call(m->seg[h.cs - 1].sel, h.ip, entry_ds(m), h.heap_size,
                      module_instance(m), 0, 0)) {
            *err = LE_FAILED;
            call_wep(m);
            destroy(m);
            return NULL;
        }
    }
    *err = 0;
    return m;
}

/* LoadLibrary / LoadModule of a DLL. Observed on Windows 3.1: a loaded
   module is found by the base name of the argument, whatever the path
   (even C:\TKNODIR\TKDLB.DLL, a directory that does not exist), and by
   MAKELP(0, hModule); then its usage count goes up and nothing is read. */
static module FAR *acquire(const char FAR *name, u16 *err)
{
    char base[MODNAME_MAX + 1];
    module FAR *m;

    if (!FP_SEG(name)) {
        m = module_of(FP_OFF(name));
        if (!m) {
            *err = LE_NOTFOUND;         /* UNVERIFIED */
            return NULL;
        }
        MOD_USAGE(m)++;
        return m;
    }
    if (mod_base_name(name, base) && (m = module_find((const char FAR *)base)) != NULL) {
        MOD_USAGE(m)++;
        return m;
    }
    return load_file(name, NULL, 0, err);
}

module FAR *module_load_program(const char FAR *path, u16 *err)
{
    return load_file(path, NULL, 1, err);
}

/* ---- API ---- */

/* Observed: the hInstance (module_instance) or an error below 32: 2 file
   not found (also when a module it imports is missing), 3 path not
   found, 20 not an NE file (a text file, an MZ file without NE header),
   0 when its initialisation returns 0. */
u16 KAPI LoadLibrary(const char FAR *name)                     /* KERNEL.95 */
{
    u16 err;
    module FAR *m = acquire(name, &err);
    return m ? module_instance(m) : err;
}

/* Observed: one use less (hInstance or hModule); at 0 the DLL's WEP runs
   and it is unloaded (GetModuleHandle gives 0 afterwards). */
void KAPI FreeLibrary(u16 h)                                   /* KERNEL.96 */
{
    module FAR *m = module_of(h);
    if (m)
        release(m);
}

/* Observed with a DLL: the same as LoadLibrary (the parameter block is
   not used). w16 runs one task, so a program cannot be started.
   UNVERIFIED: the result for a program (LE_PROGRAM here). */
u16 KAPI LoadModule(const char FAR *name, void FAR *params)    /* KERNEL.45 */
{
    (void)params;
    return LoadLibrary(name);
}

/* Observed: the same as FreeLibrary; the value in AX afterwards is not
   meaningful (F300h on Windows 3.1). */
u16 KAPI FreeModule(u16 h)                                     /* KERNEL.46 */
{
    module FAR *m = module_of(h);
    if (m)
        release(m);
    return 0;
}

/* Observed: "C:\WINDOWS" and "C:\WINDOWS\SYSTEM" (no trailing backslash).
   UNVERIFIED: the result when the buffer is too small (the documented
   behaviour, the size needed, is used). */
static u16 copy_dir(const char *dir, char FAR *buf, u16 n)
{
    u16 len = kstrlen(dir), i;
    if (len >= n)
        return len + 1;
    for (i = 0; i <= len; i++)
        buf[i] = dir[i];
    return len;
}

u16 KAPI GetWindowsDirectory(char FAR *buf, u16 n)             /* KERNEL.134 */
{
    return copy_dir(win_dir, buf, n);
}

u16 KAPI GetSystemDirectory(char FAR *buf, u16 n)              /* KERNEL.135 */
{
    return copy_dir(sys_dir, buf, n);
}
