/*
 * loader.c - the NE loader: finding a module's file, loading it and the
 * modules it imports from, relocating, initialising and freeing it;
 * LoadLibrary, FreeLibrary, LoadModule, FreeModule, GetWindowsDirectory,
 * GetSystemDirectory.
 *
 * Behaviour observed black-box on Windows 3.1 (Standard and 386 Enhanced
 * mode agree) with tests/apps/tk5.c and its DLLs; ARCHITECTURE.md 9.2
 * lists the findings. The module table (hModule) is built by mdb.c.
 * Starting programs (LoadModule, WinExec; tests/apps/tk15.c) makes tasks
 * through task.c. Not yet: demand loading and discarding, self-loading
 * modules.
 *
 * NE format source: see include/w16/ne.h.
 */
#include "kernel.h"

/* LoadLibrary / LoadModule results below 32 (observed on Windows 3.1) */
#define LE_FAILED       0       /* the DLL's initialisation returned 0 */
#define LE_NOTFOUND     2       /* also: a module it imports is missing */
#define LE_NOPATH       3
#define LE_BADFORMAT    20      /* not MZ, or MZ without an NE header */
#define LE_PROGRAM      5       /* LoadLibrary of a program (tk15.c) */

/* UNVERIFIED: these are w16's own choices or documented values not yet
   seen on Windows. */
#define LE_NOMEM        8       /* out of memory */
#define LE_MULTIDATA    16      /* a second instance of a program with
                                   more than one writable data segment
                                   (the SDK's value) */

/* While a program is loaded for a new task (not the first one), the DLLs
   loaded with it are not initialised then but listed here, for InitTask
   to initialise them on the task's stack: observed on Windows 3.1 (tk15.c,
   and TK5 started from WIN.INI's load=), as Pietrek describes. A block:
   a count word, then hModules. */
static u16 defer_sel;
static u16 last_libinit;        /* the list the last program load made */

/* Windows' own modules that w16 does not provide yet. A program that
   imports from one of them still loads: the imports go to the
   "unimplemented" thunks (thunk.c). Any other missing module fails the
   load, as on Windows. */
static const char *const pending_modules[] = {
    "USER", "GDI", "KEYBOARD", "SOUND", "SYSTEM", "DISPLAY", "MOUSE",
    "COMM", NULL
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
            if (tm && module_entry(tm, rr->t2, tsel, toff, &is_const)) {
                if (!is_const && trace_wanted(name, rr->t2))
                    thunk_trace(name, rr->t2, tsel, toff);
                return 1;
            }
            return thunk_make(name, rr->t2, NULL, tsel, toff);
        }
        if (tm && mod_find_name(tm, imported_name(m, rr->t2), &ord) &&
            module_entry(tm, ord, tsel, toff, &is_const))
            return 1;
        return thunk_make(name, 0, imported_name(m, rr->t2), tsel, toff);

    default:
        return 0;               /* OS fixups: os_fixup() */
    }
}

/* Floating-point OS fixups (NERF_OSFIXUP; include/w16/ne.h). Observed on
   Windows 3.1 (tests/apps/tk6.c; Standard and 386 Enhanced mode agree):
   - with a coprocessor (WF_80x87), the FWAIT at a type 1-5 site becomes a
     NOP (9B D9 C0 -> 90 D9 C0, 9B 26 D9 07 -> 90 26 D9 07), so the
     coprocessor runs the instruction itself; a type 6 site (NOP FWAIT)
     becomes INT 3Dh (CD 3D), which KERNEL handles even when WIN87EM is
     not loaded (int3d_handler);
   - without one, FIxRQQ is added to the word at the site and, for types
     1-3, FJxRQQ to the word one byte further: 9B D9 -> CD 35 (INT 34h +
     escape - D8h), 9B 3E D9 -> CD 3C 19 (INT 3Ch; the escape byte keeps
     its low 3 bits and gets the segment in its top 2: DS 00, SS 01, CS
     10, ES 11), 90 9B -> CD 3D. WIN87EM's emulator handles these; a
     program that does not import WIN87EM gets no emulator.
   Code is never patched back afterwards (observed in both cases).
   The record's additive flag does not matter and there are no chains
   (observed with tests/apps/tk10.c: a record without the flag whose site
   held the offset of another site patched only its own site, as a
   number: 04BCh became 04B1h with a coprocessor, 60EEh without). With a
   coprocessor the first byte is lowered by 0Bh (9Bh -> 90h). */
static void os_fixup(u16 sel, const ne_reloc FAR *rr, int have87)
{
    static const u16 fi[] = { NEFIX_FIARQQ, NEFIX_FISRQQ, NEFIX_FICRQQ,
                              NEFIX_FIERQQ, NEFIX_FIDRQQ, NEFIX_FIWRQQ };
    static const u16 fj[] = { NEFIX_FJARQQ, NEFIX_FJSRQQ, NEFIX_FJCRQQ };
    u8 FAR *p = MK_FP(sel, 0);
    u16 type = rr->t1, off = rr->offset;

    if (type < NEOSFIX_DS || type > NEOSFIX_W ||
        (u32)off + 3 > gsize_of_sel(sel))
        return;
    if (have87 && type != NEOSFIX_W) {
        p[off] -= 0x0B;
    } else {
        *(u16 FAR *)(p + off) += fi[type - 1];
        if (type <= NEOSFIX_CS)
            *(u16 FAR *)(p + off + 1) += fj[type - 1];
    }
}

static int relocate_segment(module FAR *m, u16 fh, u16 i, u32 data_end)
{
    ne_reloc buf[32];
    u16 n, done = 0;
    int have87 = (get_winflags() & WF_80x87) != 0;

    if (!f_seek(fh, data_end) || !f_read(fh, &n, 2))
        return 0;
    while (done < n) {
        u16 k, batch = n - done > 32 ? 32 : n - done;
        if (!f_read(fh, buf, batch * sizeof(ne_reloc)))
            return 0;
        for (k = 0; k < batch; k++) {
            u16 tsel, toff;
            if ((buf[k].flags & NERF_TYPEMASK) == NERF_OSFIXUP)
                os_fixup(m->seg[i].sel, &buf[k], have87);
            else if (target(m, &buf[k], &tsel, &toff))
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

/* Usage counts (observed on Windows 3.1, tk5.c and tk15.c): loading a
   module, or another instance of a program, or LoadLibrary of a loaded
   DLL, adds one use to the module and to every DLL in its import
   closure, each once however many paths lead to it (a program importing
   TKDLL and TKDLB, where TKDLL imports TKDLB, gave 1 for both; a second
   instance 2 for both). Freeing takes one use from each, and the DLLs
   that reach 0 are freed (WEP first). Pietrek describes the same
   (IncExeUsage, DecExeUsage). KERNEL is counted too (observed under a
   booted 3.1, TK29: 11 at the shell, one more with every DLL loaded,
   whatever it imports), but never freed. */
static u16 walk_gen;            /* marks modules already counted */
static u16 load_depth;          /* nested load_file calls */

static void closure_walk(module FAR *m, int delta)
{
    u16 i, n;
    module FAR *d;
    if (m->walk == walk_gen)
        return;
    m->walk = walk_gen;
    if (delta > 0)
        MOD_USAGE(m)++;
    else if (MOD_USAGE(m))
        MOD_USAGE(m)--;
    n = NE_HDR(m)->modref_count;
    for (i = 0; i < n; i++) {
        d = module_of(MOD_MODREF(m, i));
        if (d && (d->flags & NEF_LIBRARY))
            closure_walk(d, delta);
    }
}

static void uses(module FAR *m, int delta)
{
    if (!++walk_gen) {                  /* wrapped: clear every mark */
        module FAR *x;
        for (x = module_first; x; x = x->next)
            x->walk = 0;
        walk_gen = 1;
    }
    closure_walk(m, delta);
}

static void destroy(module FAR *m);
static void call_wep(module FAR *m);

/* Free the DLLs nobody uses any more, importers first (the module list
   has a module before the modules it imports). Not while loading. */
static void sweep(void)
{
    module FAR *m;
    if (load_depth)
        return;
    for (m = module_first; m; ) {
        if (m != module_first && (m->flags & NEF_LIBRARY) && !MOD_USAGE(m)) {
            if (NE_HDR(m)->flags & MDBF_WEP)
                call_wep(m);
            module_signal(m->ne_sel, 0x80);     /* Pietrek: after WEP */
            destroy(m);
            m = module_first;
            continue;
        }
        m = m->next;
    }
}

/* Free a module's memory (its imports: uses() and sweep()). */
static void destroy(module FAR *m)
{
    u16 i;
    th_delmodule(m);                    /* toolhelp.c: 65h, 52h each */
    mod_unlink(m);
    for (i = 0; i < m->seg_count; i++)
        if (m->seg[i].hnd)
            GlobalFree(m->seg[i].hnd);
    if (m->nonres_sel)
        GlobalFree(m->nonres_sel);
    if (m->ft_sel)
        GlobalFree(m->ft_sel);
    if (m->dg_snap)
        GlobalFree(m->dg_snap);
    if (m->ne_sel) {
        rsrc_free(m);
        GlobalFree(m->ne_sel);
    }
    GlobalFree(m->self);
}

/* A DLL nobody uses any more gets its WEP called (observed on Windows
   3.1: with 0, WEP_FREE_DLL) before it is freed. */
static void call_wep(module FAR *m)
{
    static const u8 wep[] = "\3WEP";
    u16 ord, sel, off;
    int is_const;

    if (mod_find_name(m, (const u8 FAR *)wep, &ord) &&
        module_entry(m, ord, &sel, &off, &is_const) && !is_const)
        mod_call(sel, off, entry_ds(m), 0, module_instance(m), 1, 0);
}

/* One use less of a DLL (FreeLibrary, FreeModule); KERNEL and programs
   stay (a program goes with its last instance, module_end_instance). */
static void release(module FAR *m)
{
    if (m == module_first || !(m->flags & NEF_LIBRARY))
        return;
    uses(m, -1);
    sweep();
}

static int is_pending(const char *name)
{
    u16 i;
    if (winboot_active)
        return 0;                       /* Windows' own modules are there */
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
    if (!d) {
        char logname[MODNAME_MAX + 5];
        u16 i;
        for (i = 0; name[i] && i < MODNAME_MAX; i++)
            logname[i] = name[i];
        kmemcpy(logname + i, ".DLL", 5);
        diag_load_start(logname);       /* observed: "SHELL.DLL" */
        d = load_file(name, ".DLL", 0, err);
        diag_load_end(logname, d ? 32 : *err);
        if (!d)
            return 0;
    }
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

/* Append hmod to the deferred list; 0 if out of memory. */
static int defer_add(u16 hmod)
{
    u16 n = *(u16 FAR *)MK_FP(defer_sel, 0);
    if ((u32)(n + 2) * 2 > gsize_of_sel(defer_sel) &&
        !GlobalReAlloc(defer_sel, (u32)(n + 2) * 2 + 64, 0))
        return 0;
    *(u16 FAR *)MK_FP(defer_sel, 2 + 2 * n) = hmod;
    *(u16 FAR *)MK_FP(defer_sel, 0) = n + 1;
    return 1;
}

/* Copy the DGROUP of a program just loaded, for its later instances
   (UNVERIFIED: Windows reads the segment from the file again; a far
   pointer to DGROUP in the data would point to the first instance's
   here). */
static int snapshot(module FAR *m)
{
    u16 ad = NE_HDR(m)->auto_data, sel;
    u32 n;
    if (!ad || ad > m->seg_count)
        return 1;
    /* KRNL286 sizes are odd multiples of 10h (arena and block together
       rounded to 20h): asking for the size rounded down to 20h gives the
       same size back; at most 64K is data */
    n = gsize_of_sel(m->seg[ad - 1].sel);
    if (!galloc(GMEM_FIXED, n & ~0x1FUL, m->ne_sel, &sel))
        return 0;
    if (n > 0x10000UL)
        n = 0x10000UL;
    m->dg_snap = sel;
    if (n > 0x8000UL) {
        kmemcpy(MK_FP(sel, 0), MK_FP(m->seg[ad - 1].sel, 0), 0x8000);
        kmemcpy(MK_FP(sel, 0x8000), MK_FP(m->seg[ad - 1].sel, 0x8000), (u16)(n - 0x8000UL));
    } else {
        kmemcpy(MK_FP(sel, 0), MK_FP(m->seg[ad - 1].sel, 0), (u16)n);
    }
    return 1;
}

static module FAR *load_body(const char FAR *name, const char *ext, int program,
                             u16 *err);

/* Load a module from its file (name searched as in open_search, ext
   appended), with the modules it imports from, and run a DLL's
   initialisation (or, for a program loaded for a new task, list them:
   last_libinit). Programs only if `program`. */
static module FAR *load_file(const char FAR *name, const char *ext, int program,
                             u16 *err)
{
    u16 saved = defer_sel;
    module FAR *m;
    load_depth++;
    m = load_body(name, ext, program, err);
    load_depth--;
    if (defer_sel != saved) {
        if (m && *(u16 FAR *)MK_FP(defer_sel, 0))
            last_libinit = defer_sel;
        else
            GlobalFree(defer_sel);
        defer_sel = saved;
    }
    return m;
}

static module FAR *load_body(const char FAR *name, const char *ext, int program,
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
    if (!(h.flags & NEF_LIBRARY) && cur_task) {
        u16 dsel;
        if (!galloc(GMEM_FIXED, 64, 0, &dsel)) {
            f_close(fh);
            *err = LE_NOMEM;
            return NULL;
        }
        defer_sel = dsel;
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
            /* DGROUP stays one selector, below 64K (Pietrek: the local
               heap keeps "the DGROUP below 64K"; local.c SEG_MAX). A full
               64K would be two selector tiles, and a local heap grown in
               it would move to new selectors. UNVERIFIED: 3.1's size for
               data, heap and stack that add up to 64K or more */
            size += (u32)h.heap_size + h.stack_size;
            if (size > 0xFFF0UL)
                size = 0xFFF0UL;
        }
        /* observed (KRNL286, tests/apps/tk11.c): a program's fixed
           segments are loaded moveable (even handles, hInstance = DS &
           ~1). UNVERIFIED: a DLL's fixed segments stay fixed. */
        m->seg[i].hnd = galloc(((s->flags & NESEG_MOVABLE) || !(h.flags & NEF_LIBRARY) ?
                                GMEM_MOVEABLE : GMEM_FIXED) |
                               (i + 1 == h.auto_data ? GA_REQ_DGROUP : 0),
                               size, m->ne_sel, &sel);
        m->seg[i].sel = sel;
        if (!m->seg[i].hnd) {
            f_close(fh);
            *err = LE_NOMEM;
            return load_fail(m, path, "out of memory");
        }
        /* in the module table: the handle, and the flags of a loaded
           segment. Observed with GetCodeInfo (TK25): a DLL's fixed code
           segment stays fixed (USER's first: file 0D40h, loaded C146h);
           the DPL bits go; a DLL's segments get 8000h, code segments
           4000h, a program's code 0400h too (program code 0D40h ->
           4556h, program data 0D41h -> 0157h, SHELL's code 1D50h ->
           D156h, SHELL's data -> 8077h). UNVERIFIED: what those bits
           mean, other kinds of segments. */
        MOD_SEGHND(m, i) = m->seg[i].hnd;
        s->flags = (s->flags & ~NESEG_DPL) | NESEG_ALLOCED | NESEG_LOADED |
                   (((s->flags & NESEG_MOVABLE) || !(h.flags & NEF_LIBRARY)) ? NESEG_MOVABLE : 0);
        if (h.flags & NEF_LIBRARY)
            s->flags |= 0x8000;
        if (!(s->flags & NESEG_DATA))
            s->flags |= (h.flags & NEF_LIBRARY) ? 0x4000 : 0x4400;
        /* observed (TK25): the automatic data segment's minimum
           allocation includes the local heap and the stack */
        if (i + 1 == h.auto_data) {
            m->dg_min = s->min_alloc;
            if (size < 0x10000UL)
                s->min_alloc = (u16)size;
        }
        if (flen && (!f_seek(fh, (u32)s->sector << shift) || !f_read_big(fh, sel, flen))) {
            f_close(fh);
            return load_fail(m, path, "cannot read segment");
        }
    }
    f_close(fh);

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
    if (!(h.flags & NEF_LIBRARY) && !snapshot(m)) {
        *err = LE_NOMEM;
        return load_fail(m, path, "out of memory");
    }
    patch_prologues(m);
    for (i = 0; i < h.seg_count; i++)
        if (!(MOD_SEG(m, i)->flags & NESEG_DATA))
            gset_code(m->seg[i].sel);
    th_loadsegs(m);                     /* toolhelp.c: 50h each */
    rsrc_init(m);

    /* A DLL's initialisation, on the caller's stack. Observed on Windows
       3.1 at the entry point: AX = 1, CX = heap size, DX = DS = the DGROUP
       selector, DI = hInstance (DS & ~1), SI = 0, ES = 0; DS:0000-000F all
       zero and no local heap yet (the DLL makes it, LocalInit); returning
       0 makes LoadLibrary return 0, and the DLL's WEP runs before it is
       freed. A DLL without DGROUP gets DS = DX = DI = its hModule.
       UNVERIFIED: WEP's argument after a failed initialisation (0 here). */
    if (h.flags & NEF_LIBRARY) {
        NE_HDR(m)->flags |= MDBF_WEP;   /* observed on DLLs (table 0Ch) */
        module_signal(m->ne_sel, 0x40); /* Pietrek: before its init */
    }
    if ((h.flags & NEF_LIBRARY) && !(h.cs && h.cs <= h.seg_count && defer_sel))
        th_startdll(m, h.cs && h.cs <= h.seg_count ? m->seg[h.cs - 1].sel : 0,
                    h.cs && h.cs <= h.seg_count ? h.ip : 0);   /* 64h */
    if ((h.flags & NEF_LIBRARY) && h.cs && h.cs <= h.seg_count && defer_sel) {
        if (!defer_add(m->ne_sel)) {
            *err = LE_NOMEM;
            destroy(m);
            return NULL;
        }
    } else if ((h.flags & NEF_LIBRARY) && h.cs && h.cs <= h.seg_count) {
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
static module FAR *find_loaded(const char FAR *name)
{
    char base[MODNAME_MAX + 1];
    if (!FP_SEG(name))
        return module_of(FP_OFF(name));
    if (mod_base_name(name, base))
        return module_find((const char FAR *)base);
    return NULL;
}

static module FAR *acquire(const char FAR *name, u16 *err)
{
    module FAR *m;

    m = find_loaded(name);
    if (m) {
        if (!(m->flags & NEF_LIBRARY) && m != module_first) {
            *err = LE_PROGRAM;          /* observed, also while it runs */
            return NULL;
        }
        uses(m, 1);
        return m;
    }
    if (!FP_SEG(name)) {
        *err = LE_NOTFOUND;             /* UNVERIFIED */
        return NULL;
    }
    m = load_file(name, NULL, 0, err);
    if (m)
        uses(m, 1);
    sweep();                            /* what a failed load left */
    return m;
}

/* the same, while a task is current (the boot task): *libinit gets the
   list of DLLs the program's InitTask is to initialise (for task_create) */
module FAR *module_load_program_init(const char FAR *path, u16 *err, u16 *libinit)
{
    module FAR *m;
    last_libinit = 0;
    m = load_file(path, NULL, 1, err);
    *libinit = last_libinit;
    last_libinit = 0;
    if (m)
        uses(m, 1);
    sweep();
    return m;
}

module FAR *module_load_program(const char FAR *path, u16 *err)
{
    module FAR *m = load_file(path, NULL, 1, err);
    if (m)
        uses(m, 1);
    sweep();
    return m;
}

/* ---- API ---- */

/* Observed: the hInstance (module_instance) or an error below 32: 2 file
   not found (also when a module it imports is missing), 3 path not
   found, 20 not an NE file (a text file, an MZ file without NE header),
   0 when its initialisation returns 0. */
u16 KAPI LoadLibrary(const char FAR *name)                     /* KERNEL.95 */
{
    u16 err, r;
    module FAR *m;
    if (FP_SEG(name))
        diag_load_start(name);
    m = acquire(name, &err);
    r = m ? module_instance(m) : err;
    if (FP_SEG(name))
        diag_load_end(name, r < 32 ? r : 32);
    return r;
}

/* Observed: one use less (hInstance or hModule); at 0 the DLL's WEP runs
   and it is unloaded (GetModuleHandle gives 0 afterwards). */
void KAPI FreeLibrary(u16 h)                                   /* KERNEL.96 */
{
    module FAR *m = module_of(h);
    if (m)
        release(m);
}

/* LoadModule's parameter block */
typedef struct loadparms {
    u16 env;                    /* observed: 0 shares KERNEL's (one
                                   selector for all tasks); else a copy */
    const u8 FAR *cmd;          /* length byte, text, CR */
    const u16 FAR *show;        /* 2, nCmdShow */
    u32 reserved;
} loadparms;

/* Start a task for an instance of program m whose DGROUP is dg. */
static u16 start_task(module FAR *m, u16 dg_sel, u16 dg_hnd, u16 hprev,
                      const loadparms FAR *lp, u16 libinit)
{
    u16 t = task_create(m, dg_sel, dg_hnd, hprev, lp ? lp->cmd : NULL,
                        lp ? lp->show : NULL, libinit);
    if (!t)
        return 0;
    if (lp && lp->env)
        task_set_env(t, lp->env);
    task_start(t);
    return dg_hnd;
}

/* Another instance of a loaded program: a new DGROUP (the module's as
   loaded), one more use of the module and of the DLLs it imports
   (import_uses; the DLLs are not initialised again); hPrevInstance is
   the first instance (observed with two). */
static u16 new_instance(module FAR *m, const loadparms FAR *lp)
{
    u16 i, n = 0, sel, hnd, hprev, r;
    u32 size;

    for (i = 0; i < m->seg_count; i++)
        if ((MOD_SEG(m, i)->flags & NESEG_DATA) && !(MOD_SEG(m, i)->flags & NESEG_RDONLY))
            n++;
    if (n > 1)
        return LE_MULTIDATA;
    if (!m->dg_snap)
        return LE_NOMEM;
    size = gsize_of_sel(m->dg_snap);
    if (size > 0xFFF0UL)                /* one selector, as above */
        size = 0xFFF0UL;
    hnd = galloc(GMEM_MOVEABLE | GA_REQ_DGROUP, size & ~0x1FUL, m->ne_sel, &sel);
    if (!hnd)
        return LE_NOMEM;
    if (size > 0x10000UL)
        size = 0x10000UL;
    if (size > 0x8000UL) {
        kmemcpy(MK_FP(sel, 0), MK_FP(m->dg_snap, 0), 0x8000);
        kmemcpy(MK_FP(sel, 0x8000), MK_FP(m->dg_snap, 0x8000), (u16)(size - 0x8000UL));
    } else {
        kmemcpy(MK_FP(sel, 0), MK_FP(m->dg_snap, 0), (u16)size);
    }
    hprev = task_prev_instance(m);
    uses(m, 1);
    r = start_task(m, sel, hnd, hprev, lp, 0);
    if (!r) {
        module_end_instance(m, hnd, sel);
        return LE_NOMEM;
    }
    return r;
}

/* One instance of program m has ended (task.c): its DGROUP goes, and one
   use of the module and the modules it imports; with the last one the
   module is freed (observed: GetModuleHandle gives 0 afterwards, and the
   implicitly loaded DLLs are gone). UNVERIFIED: which DGROUP the
   module's segment table holds when the first instance ends before
   another. */
void module_end_instance(module FAR *m, u16 dg_hnd, u16 dg_sel)
{
    u16 ad = NE_HDR(m)->auto_data;
    (void)dg_sel;
    uses(m, -1);
    if (MOD_USAGE(m)) {
        if (ad && ad <= m->seg_count && m->seg[ad - 1].hnd == dg_hnd) {
            u16 sel = 0, h = task_other_dgroup(m, dg_hnd, &sel);
            m->seg[ad - 1].hnd = h;
            m->seg[ad - 1].sel = sel;
            MOD_SEGHND(m, ad - 1) = h;
        }
        GlobalFree(dg_hnd);
        sweep();
        return;
    }
    if (ad && ad <= m->seg_count && m->seg[ad - 1].hnd != dg_hnd)
        GlobalFree(dg_hnd);
    destroy(m);
    sweep();
}

/* Initialise a DLL whose initialisation was deferred (InitTask, on the
   task's stack); returns its result. */
int module_init_dll(u16 hmod)
{
    module FAR *m = module_of(hmod);
    ne_header FAR *h;
    if (!m)
        return 1;
    h = NE_HDR(m);
    if (!h->cs || h->cs > m->seg_count)
        return 1;
    th_startdll(m, m->seg[h->cs - 1].sel, h->ip);     /* toolhelp.c: 64h */
    return mod_call(m->seg[h->cs - 1].sel, h->ip, entry_ds(m), h->heap_size,
                    module_instance(m), 0, 0) != 0;
}

/* A DOS program (a file that is not a Windows module): Windows starts
   WINOLDAP.MOD from the system directory in its place (Standard mode;
   WINOA386.MOD in 386 Enhanced mode; Pietrek). Observed on 3.1 (TK33,
   in WINOLDAP.MOD's place, for WinExec("COMMAND.COM /C ECHO HI")): its
   command tail is the DOS program's tail, a NUL, then the program's name
   as given ("/C ECHO HI\0COMMAND.COM", length byte 16h, followed by
   0Ah), nCmdShow as given, hPrevInstance 0. UNVERIFIED: the 0Ah, a name
   with a path, a .PIF, tails longer than 127 bytes (Pietrek: the PDB
   then grows); the TDB's WINOLDAP flag (Pietrek: TDB 16h bit 0) is not
   set yet. */
u16 KAPI LoadModule(const char FAR *name, void FAR *params);

static u16 start_oldapp(const char FAR *name, const loadparms FAR *lp)
{
    static char woa[DOS_PATHMAX + 16];
    static u8 tail[0x100];
    loadparms np;
    const u8 FAR *t = lp->cmd;
    u16 n = 0, i, k;

    k = t ? t[0] : 0;
    for (i = 0; i < k && n < sizeof tail - 3; i++)
        tail[1 + n++] = t[1 + i];
    tail[1 + n++] = 0;
    for (i = 0; name[i] && n < sizeof tail - 3; i++)
        tail[1 + n++] = name[i];
    tail[0] = (u8)(n > 0xFF ? 0xFF : n);
    tail[1 + n] = '\n';
    n = kstrlen(sys_dir);
    kmemcpy(woa, sys_dir, n);
    if (n && woa[n - 1] != '\\')
        woa[n++] = '\\';
    kmemcpy(woa + n, "WINOLDAP.MOD", 13);
    np.env = lp->env;
    np.cmd = tail;
    np.show = lp->show;
    np.reserved = 0;
    return LoadModule(woa, &np);
}

/* Observed on Windows 3.1 (tk5.c, tk15.c): for a DLL the same as
   LoadLibrary; with the parameter block -1 (what LoadLibrary passes,
   Pietrek) a program gives 5. A program starts a task: a new module,
   whose DLLs are initialised in the task's InitTask, or another instance
   of a loaded one; the result is the hInstance, after the new task has
   run until it yielded. Errors as LoadLibrary's. */
u16 KAPI LoadModule(const char FAR *name, void FAR *params)    /* KERNEL.45 */
{
    const loadparms FAR *lp = params;
    module FAR *m;
    u16 err, libinit, ad, r;

    if (params == (void FAR *)-1L)
        return LoadLibrary(name);
    m = find_loaded(name);
    if (m && ((m->flags & NEF_LIBRARY) || m == module_first)) {
        uses(m, 1);
        return module_instance(m);
    }
    if (m)
        return new_instance(m, lp);
    if (!FP_SEG(name))
        return LE_NOTFOUND;             /* UNVERIFIED */
    last_libinit = 0;
    m = load_file(name, NULL, 1, &err);
    libinit = last_libinit;
    last_libinit = 0;
    if (m)
        uses(m, 1);
    sweep();
    if (!m && err == LE_BADFORMAT && winboot_active && lp)
        return start_oldapp(name, lp);
    if (!m)
        return err;
    if (m->flags & NEF_LIBRARY)
        return module_instance(m);
    ad = NE_HDR(m)->auto_data;
    r = ad && ad <= m->seg_count
        ? start_task(m, m->seg[ad - 1].sel, m->seg[ad - 1].hnd, 0, lp, libinit) : 0;
    if (!r) {
        if (libinit)
            GlobalFree(libinit);
        uses(m, -1);
        destroy(m);
        sweep();
        return LE_NOMEM;
    }
    return r;
}

/* WinExec (KERNEL.166): LoadModule with the command line split into the
   program and its tail. Observed on Windows 3.1 (tk15.c, tk16.c): one
   blank after the program's name is not part of the tail (with three,
   the tail starts with two); a name without extension gets ".EXE"; the
   name is searched as LoadLibrary's (TK16A was found in the directory of
   the caller's program); nCmdShow reaches InitTask; a missing file gives
   2, a missing directory 3.
   UNVERIFIED: tabs, quotes, a name ending in a dot. */
/* a dot in the last part of a path */
static int has_ext(const char *name)
{
    int dot = 0;
    for (; *name; name++)
        if (*name == '.')
            dot = 1;
        else if (*name == '\\' || *name == '/' || *name == ':')
            dot = 0;
    return dot;
}

u16 KAPI WinExec(const char FAR *cmd, u16 show)                /* KERNEL.166 */
{
    char name[DOS_PATHMAX];
    u8 tail[0x80];
    u16 shw[2];
    loadparms lp;
    u16 i = 0, n = 0;

    while (cmd[i] == ' ')
        i++;
    while (cmd[i] && cmd[i] != ' ') {
        if (n >= sizeof name - 1)
            return LE_NOTFOUND;
        name[n++] = cmd[i++];
    }
    name[n] = 0;
    if (!has_ext(name)) {
        if (n + 4 >= sizeof name)
            return LE_NOTFOUND;
        name[n++] = '.';
        name[n++] = 'E';
        name[n++] = 'X';
        name[n++] = 'E';
        name[n] = 0;
    }
    if (cmd[i] == ' ')
        i++;
    for (n = 0; cmd[i] && n < 0x7E; n++)
        tail[1 + n] = (u8)cmd[i++];
    tail[0] = (u8)n;
    tail[1 + n] = 13;
    shw[0] = 2;
    shw[1] = show;
    lp.env = 0;
    lp.cmd = tail;
    lp.show = shw;
    lp.reserved = 0;
    return LoadModule(name, &lp);
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

u16 KAPI_ES GetWindowsDirectory(char FAR *buf, u16 n)             /* KERNEL.134 */
{
    return copy_dir(win_dir, buf, n);
}

u16 KAPI_ES GetSystemDirectory(char FAR *buf, u16 n)              /* KERNEL.135 */
{
    return copy_dir(sys_dir, buf, n);
}
