/*
 * file.c - file I/O: OpenFile, _lopen, _lcreat, _lclose, _lread, _lwrite,
 * _llseek, _hread, _hwrite, GetTempFileName, GetTempDrive, GetDriveType,
 * SetHandleCount.
 *
 * Everything goes to DOS through INT 21h (translated by the DOS extender),
 * as on Windows 3.1, so DOS decides sharing, attributes and seek limits.
 * Behaviour observed black-box on Windows 3.1 with tests/apps/tk14.c
 * (Standard and 386 Enhanced mode agree); ARCHITECTURE.md 9.2 lists the
 * findings.
 */
#include "kfile.h"

/* OpenFile flags (OpenWatcom h/win/win16.h) */
#define OF_PARSE        0x0100
#define OF_DELETE       0x0200
#define OF_VERIFY       0x0400
#define OF_CREATE       0x1000
#define OF_EXIST        0x4000
#define OF_REOPEN       0x8000
#define TF_FORCEDRIVE   0x80

#define OFS_PATHMAX     128     /* OFSTRUCT.szPathName (format bound) */

typedef struct ofstruct {
    u8   cBytes;
    u8   fFixedDisk;
    u16  nErrCode;
    u16  date, time;            /* "reserved": observed date, then time */
    char szPathName[OFS_PATHMAX];
} ofstruct;

/* ---- DOS ---- */

int dos_call(regs16 *r)
{
    return !dos21(r);
}

static void rinit(regs16 *r, u16 ax)
{
    r->ax = ax;
    r->bx = r->cx = r->dx = r->si = r->di = 0;
    r->ds = r->es = 0;
}

u16 dos_open(const char FAR *path, u16 mode, u16 *err)
{
    regs16 r;
    rinit(&r, 0x3D00 | (mode & 0xFF));
    r.dx = FP_OFF(path);
    r.ds = FP_SEG(path);
    if (!dos_call(&r)) {
        *err = r.ax;
        return HFILE_ERROR;
    }
    return r.ax;
}

u16 dos_create(const char FAR *path, u16 attr, u16 *err)
{
    regs16 r;
    rinit(&r, 0x3C00);
    r.cx = attr;
    r.dx = FP_OFF(path);
    r.ds = FP_SEG(path);
    if (!dos_call(&r)) {
        *err = r.ax;
        return HFILE_ERROR;
    }
    return r.ax;
}

int dos_close(u16 fh)
{
    regs16 r;
    rinit(&r, 0x3E00);
    r.bx = fh;
    return dos_call(&r);
}

u32 dos_seek(u16 fh, u32 pos, u16 how)
{
    regs16 r;
    rinit(&r, 0x4200 | (how & 0xFF));
    r.bx = fh;
    r.cx = (u16)(pos >> 16);
    r.dx = (u16)pos;
    if (!dos_call(&r))
        return 0xFFFFFFFFUL;
    return ((u32)r.dx << 16) | r.ax;
}

/* Read (ah = 3Fh) or write (40h) n bytes at sel:off, a huge pointer
   (tiles sel_inc apart), in pieces that do not cross a 64K boundary.
   Returns the count transferred, FFFFFFFFh if the first call fails.
   A write of 0 bytes is passed on (DOS truncates the file there). */
u32 dos_rw(u16 ah, u16 fh, u16 sel, u16 off, u32 n)
{
    regs16 r;
    u32 done = 0;
    do {
        u32 room = 0x10000UL - off;
        u16 chunk = (u16)(n - done > 0x8000UL ? 0x8000UL : n - done);
        if (chunk > room)
            chunk = (u16)room;
        rinit(&r, ah << 8);
        r.bx = fh;
        r.cx = chunk;
        r.dx = off;
        r.ds = sel;
        if (!dos_call(&r))
            return done ? done : 0xFFFFFFFFUL;
        done += r.ax;
        if (r.ax < chunk)
            break;
        off += chunk;
        if (!off)
            sel += sel_inc;
    } while (done < n);
    return done;
}

u8 FAR *huge_at(u16 sel, u32 off)
{
    return MK_FP(sel + (u16)(off >> 16) * sel_inc, (u16)off);
}

/* ---- names and paths ---- */

static char upc(char c)
{
    return (c >= 'a' && c <= 'z') ? (char)(c - 32) : c;
}

int file_has_path(const char FAR *name)
{
    for (; *name; name++)
        if (*name == '\\' || *name == '/' || *name == ':')
            return 1;
    return 0;
}

/* The task's environment variable `name` (exact case), or NULL. w16's
   task shares KERNEL's PSP and environment. */
const char FAR *file_env(const char *name)
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

static u16 cur_drive(void)
{
    regs16 r;
    rinit(&r, 0x1900);
    dos_call(&r);
    return r.ax & 0xFF;                 /* 0 = A */
}

/* characters DOS does not take in a name (UNVERIFIED: the list) */
static int bad_char(char c)
{
    static const char bad[] = " \"+,;=[]|<>";
    const char *b;
    if ((u8)c < 0x20)
        return 1;
    for (b = bad; *b; b++)
        if (c == *b)
            return 1;
    return 0;
}

/* The full upper-case path of `name` as OpenFile's OF_PARSE gives it:
   drive, current directory, "." and ".." resolved, '/' as '\'. Each
   component must be a DOS 8.3 name (wildcards pass). Returns 1, or 0
   with *err = 2 (as observed for every bad name: no name, a name too
   long, blanks, a drive that is not ready) and a partial path in out. */
int file_canon(const char FAR *name, char *out, u16 *err)
{
    regs16 r;
    char cwd[64];
    u16 n, drive, i, root, start;
    const char FAR *p = name;

    *err = DOSE_NOFILE;
    if (p[0] && p[1] == ':') {
        drive = (u16)(upc(p[0]) - 'A');
        p += 2;
    } else
        drive = cur_drive();
    out[0] = (char)('A' + drive);
    out[1] = ':';
    out[2] = '\\';
    out[3] = 0;
    root = n = 3;
    if (*p != '\\' && *p != '/') {
        rinit(&r, 0x4700);
        r.dx = drive + 1;
        r.si = FP_OFF((char FAR *)cwd);
        r.ds = FP_SEG((char FAR *)cwd);
        if (!dos_call(&r))
            return 0;
        for (i = 0; cwd[i] && n < OFS_PATHMAX - 2; i++)
            out[n++] = upc(cwd[i]);
        if (n > root)
            out[n++] = '\\';
        out[n] = 0;
    }
    for (;;) {
        u16 base = 0, ext = 0, dot = 0;
        while (*p == '\\' || *p == '/')
            p++;
        if (!*p)
            break;
        start = n;
        if (p[0] == '.' && (!p[1] || p[1] == '\\' || p[1] == '/')) {
            p++;
            continue;
        }
        if (p[0] == '.' && p[1] == '.' && (!p[2] || p[2] == '\\' || p[2] == '/')) {
            p += 2;
            if (n > root) {             /* drop the last component */
                n--;
                while (n > root && out[n - 1] != '\\')
                    n--;
                out[n] = 0;
            }
            continue;
        }
        for (; *p && *p != '\\' && *p != '/'; p++) {
            if (bad_char(*p) || n >= OFS_PATHMAX - 2) {
                out[n] = 0;
                return 0;
            }
            if (*p == '.') {
                if (dot++ || !base) {
                    out[n] = 0;
                    return 0;
                }
            } else if (dot ? ++ext > 3 : ++base > 8) {
                out[n] = 0;
                return 0;
            }
            out[n++] = upc(*p);
        }
        (void)start;
        if (*p)
            out[n++] = '\\';
        out[n] = 0;
    }
    /* no name at the end ("C:\", "", "dir\") */
    if (n == root || out[n - 1] == '\\')
        return 0;
    return 1;
}

static u16 join_canon(const char FAR *dir, const char FAR *name, char *out, u16 *err)
{
    char tmp[OFS_PATHMAX + 16];
    u16 n = 0;
    while (*dir && n < sizeof tmp - 2)
        tmp[n++] = *dir++;
    if (n && tmp[n - 1] != '\\')
        tmp[n++] = '\\';
    while (*name && n < sizeof tmp - 1)
        tmp[n++] = *name++;
    tmp[n] = 0;
    return file_canon(tmp, out, err);
}

/* ---- OpenFile ---- */

static int fixed_disk(char letter)
{
    regs16 r;
    rinit(&r, 0x4408);
    r.bx = (u16)(upc(letter) - 'A' + 1);
    return dos_call(&r) && r.ax == 1;
}

static void stamp_of(u16 fh, u16 *date, u16 *time)
{
    regs16 r;
    rinit(&r, 0x5700);
    r.bx = fh;
    if (dos_call(&r)) {
        *date = r.dx;
        *time = r.cx;
    } else
        *date = *time = 0;
}

static void str_to(char FAR *d, const char *s)
{
    while ((*d++ = *s++) != 0)
        ;
}

/* Try to open `name` (no path) in the directories OpenFile searches,
   observed on Windows 3.1 (a copy in each, removed one by one): the
   current directory, the Windows directory, the system directory, the
   directory of the running task's program, the PATH directories. */
static u16 search_open(const char FAR *name, u16 mode, char *found)
{
    char dir[OFS_PATHMAX];
    const char FAR *p;
    module FAR *t;
    u16 fh, err, i, k;

    for (k = 0; k < 4; k++) {
        switch (k) {
        case 0: dir[0] = '.'; dir[1] = 0; break;
        case 1: GetWindowsDirectory(dir, sizeof dir); break;
        case 2: GetSystemDirectory(dir, sizeof dir); break;
        case 3:
            t = task_module();
            if (!t)
                continue;
            for (i = 0; t->path[i] && i < sizeof dir - 1; i++)
                dir[i] = t->path[i];
            while (i && dir[i - 1] != '\\')
                i--;
            dir[i] = 0;
            break;
        }
        if (join_canon(dir, name, found, &err) &&
            (fh = dos_open(found, mode, &err)) != HFILE_ERROR)
            return fh;
    }
    for (p = file_env("PATH"); p && *p; ) {
        u16 n = 0;
        while (p[n] && p[n] != ';')
            n++;
        if (n && n < sizeof dir) {
            for (i = 0; i < n; i++)
                dir[i] = p[i];
            dir[n] = 0;
            if (join_canon(dir, name, found, &err) &&
                (fh = dos_open(found, mode, &err)) != HFILE_ERROR)
                return fh;
        }
        p += n;
        if (*p == ';')
            p++;
    }
    return HFILE_ERROR;
}

u16 KAPI OpenFile(const char FAR *name, ofstruct FAR *of, u16 flags)  /* KERNEL.74 */
{
    char path[OFS_PATHMAX], given[OFS_PATHMAX];
    u16 fh, err, date, time, i;
    u16 mode = flags & 0xFF;

    if (flags & OF_REOPEN) {
        for (i = 0; i < OFS_PATHMAX - 1 && of->szPathName[i]; i++)
            given[i] = of->szPathName[i];
        given[i] = 0;
        name = given;
    }
    if (!file_canon(name, path, &err)) {
        /* observed: nErrCode 2 and the path so far; nothing else set */
        of->nErrCode = err;
        str_to(of->szPathName, path);
        return HFILE_ERROR;
    }
    if (flags & OF_PARSE) {
        of->cBytes = (u8)(8 + kstrlen(path));
        of->fFixedDisk = (u8)fixed_disk(path[0]);
        of->nErrCode = 0;
        str_to(of->szPathName, path);
        return 0;
    }
    if (flags & OF_CREATE) {
        fh = dos_create(path, 0, &err);
    } else if (file_has_path(name)) {
        fh = dos_open(path, (flags & (OF_EXIST | OF_DELETE)) ? 0 : mode, &err);
    } else {
        char found[OFS_PATHMAX];
        fh = search_open(name, (flags & (OF_EXIST | OF_DELETE)) ? 0 : mode, found);
        if (fh != HFILE_ERROR)
            kmemcpy(path, found, sizeof path);
        else
            err = DOSE_NOFILE;
    }
    if (fh == HFILE_ERROR) {
        /* observed: cBytes and fFixedDisk are left alone */
        of->nErrCode = err;
        str_to(of->szPathName, path);
        return HFILE_ERROR;
    }
    stamp_of(fh, &date, &time);
    if ((flags & (OF_REOPEN | OF_VERIFY)) == (OF_REOPEN | OF_VERIFY) &&
        (of->date != date || of->time != time)) {
        /* observed: fails; UNVERIFIED: nErrCode (3.1 left 0 there) */
        dos_close(fh);
        return HFILE_ERROR;
    }
    /* observed: cBytes = 8 + the length of the path (not the structure
       size), date before time in the reserved bytes */
    of->cBytes = (u8)(8 + kstrlen(path));
    of->fFixedDisk = (u8)fixed_disk(path[0]);
    of->nErrCode = 0;
    of->date = date;
    of->time = time;
    str_to(of->szPathName, path);
    if (flags & OF_DELETE) {
        regs16 r;
        dos_close(fh);
        rinit(&r, 0x4100);
        r.dx = FP_OFF((char FAR *)path);
        r.ds = FP_SEG((char FAR *)path);
        if (!dos_call(&r)) {
            of->nErrCode = r.ax;
            return HFILE_ERROR;
        }
        return fh;                      /* observed: the handle it used */
    }
    if (flags & OF_EXIST)
        dos_close(fh);
    return fh;
}

/* ---- the _l and _h functions: DOS calls, FFFFh on error ---- */

u16 KAPI _lopen(const char FAR *path, u16 mode)                 /* KERNEL.85 */
{
    u16 err;
    return dos_open(path, mode, &err);  /* observed: no search */
}

u16 KAPI _lcreat(const char FAR *path, u16 attr)                /* KERNEL.83 */
{
    u16 err;
    return dos_create(path, attr, &err);
}

u16 KAPI _lclose(u16 fh)                                        /* KERNEL.81 */
{
    return dos_close(fh) ? 0 : HFILE_ERROR;
}

u16 KAPI _lread(u16 fh, void FAR *buf, u16 n)                   /* KERNEL.82 */
{
    return (u16)dos_rw(0x3F, fh, FP_SEG(buf), FP_OFF(buf), n);
}

u16 KAPI _lwrite(u16 fh, const void FAR *buf, u16 n)            /* KERNEL.86 */
{
    return (u16)dos_rw(0x40, fh, FP_SEG(buf), FP_OFF(buf), n);
}

u32 KAPI _llseek(u16 fh, u32 off, u16 how)                      /* KERNEL.84 */
{
    return dos_seek(fh, off, how);
}

u32 KAPI _hread(u16 fh, void FAR *buf, u32 n)                   /* KERNEL.349 */
{
    return dos_rw(0x3F, fh, FP_SEG(buf), FP_OFF(buf), n);
}

u32 KAPI _hwrite(u16 fh, const void FAR *buf, u32 n)            /* KERNEL.350 */
{
    return dos_rw(0x40, fh, FP_SEG(buf), FP_OFF(buf), n);
}

/* ---- temporary files ---- */

static int dir_exists(const char FAR *d)
{
    regs16 r;
    rinit(&r, 0x4300);
    r.dx = FP_OFF(d);
    r.ds = FP_SEG(d);
    return dos_call(&r) && (r.cx & 0x10);
}

/* The directory for temporary files: TEMP if it names a directory, else
   the Windows directory (observed). UNVERIFIED: Windows 3.1 seems to
   check TEMP once, at start: a TEMP directory made after Windows started
   was not used. w16 checks on every call. */
static void temp_dir(char *d, u16 max)
{
    const char FAR *t = file_env("TEMP");
    u16 n = 0;
    if (t && *t) {
        while (t[n] && n < max - 1) {
            d[n] = t[n];
            n++;
        }
        d[n] = 0;
        if (n > 3 && d[n - 1] == '\\')
            d[--n] = 0;
        if (dir_exists(d) || (n == 3 && d[1] == ':'))
            return;
    }
    GetWindowsDirectory(d, max);
}

/* observed: AL = the drive letter, AH = ':' ('C:' with and without TEMP).
   UNVERIFIED: w16 takes the drive of the temporary directory. */
u16 KAPI GetTempDrive(u8 drive)                                 /* KERNEL.92 */
{
    char d[OFS_PATHMAX];
    (void)drive;
    temp_dir(d, sizeof d);
    return ((u16)':' << 8) | (u8)upc(d[0]);
}

static u16 temp_name(char FAR *out, const char *dir, const char FAR *prefix, u16 u)
{
    u16 n = 0, i;
    for (i = 0; dir[i]; i++)
        out[n++] = dir[i];
    out[n++] = '~';
    for (i = 0; i < 3 && prefix && prefix[i]; i++)  /* observed: 3, case kept */
        out[n++] = prefix[i];
    for (i = 0; i < 4; i++)
        out[n++] = "0123456789ABCDEF"[(u >> (12 - 4 * i)) & 15];
    out[n++] = '.';
    out[n++] = 'T';
    out[n++] = 'M';
    out[n++] = 'P';
    out[n] = 0;
    return n;
}

u16 KAPI GetTempFileName(u8 drive, const char FAR *prefix, u16 unique,
                         char FAR *out)                         /* KERNEL.97 */
{
    char dir[OFS_PATHMAX];
    regs16 r;
    u16 n, fh, err, tries;

    if (drive & TF_FORCEDRIVE) {
        /* observed: "C:~TKP12AB.TMP", the drive's current directory */
        dir[0] = upc((char)(drive & 0x7F));
        dir[1] = ':';
        dir[2] = 0;
    } else {
        temp_dir(dir, sizeof dir - 1);
        n = kstrlen(dir);
        if (n && dir[n - 1] != '\\') {
            dir[n] = '\\';
            dir[n + 1] = 0;
        }
    }
    if (unique) {                       /* observed: no file is made */
        temp_name(out, dir, prefix, unique);
        return unique;
    }
    /* UNVERIFIED: the number Windows starts from (it differed between
       runs); w16 takes it from the time of day */
    rinit(&r, 0x2C00);
    dos_call(&r);
    unique = r.cx ^ r.dx;
    for (tries = 0; tries < 0xFFFF; tries++, unique++) {
        if (!unique)
            continue;
        temp_name(out, dir, prefix, unique);
        fh = dos_open(out, 0, &err);
        if (fh != HFILE_ERROR) {        /* exists: try the next number */
            dos_close(fh);
            continue;
        }
        fh = dos_create(out, 0, &err);
        if (fh == HFILE_ERROR)
            return 0;
        dos_close(fh);
        return unique;
    }
    return 0;
}

/* ---- drives and handles ---- */

#define DRIVE_REMOVABLE 2
#define DRIVE_FIXED     3
#define DRIVE_REMOTE    4

/* observed on 3.1 in DOSBox: A: 2 (removable), B: 0, C: 3, D: 0, Z: (the
   emulator's own drive) 3, 26 and up 0. UNVERIFIED: w16 asks DOS (IOCTL
   4409h remote, 4408h removable) and the BIOS equipment word's floppy
   count for A: and B: (which fits what 3.1 gave). */
u16 KAPI_ES GetDriveType(u16 drive)                                /* KERNEL.136 */
{
    regs16 r;
    if (drive >= 26)
        return 0;
    if (drive < 2) {
        u16 eq = *(u16 FAR *)MK_FP(dpmi_seg_to_desc(0x40), 0x10);
        /* bits 6-7 = floppy drives - 1; bit 0 (any floppy) is clear in
           DOSBox, where 3.1 still gave A: as removable */
        u16 floppies = ((eq >> 6) & 3) + 1;
        return drive < floppies ? DRIVE_REMOVABLE : 0;
    }
    rinit(&r, 0x4409);
    r.bx = drive + 1;
    if (!dos_call(&r))
        return 0;
    if (r.dx & 0x1000)
        return DRIVE_REMOTE;
    rinit(&r, 0x4408);
    r.bx = drive + 1;
    if (!dos_call(&r))
        return 0;
    return r.ax == 0 ? DRIVE_REMOVABLE : DRIVE_FIXED;
}

/* observed: the count only grows, from 20 up to 255; it is the task's
   own (tests/apps/tk16.c "hc": a task started after another raised its
   count to 40 starts from 20). It is kept per task (task.c); DOS's 6700h
   makes the handle table of the task's PDB at least that big. w16 had one count for all tasks (KERNEL audit #10) */
u16 task_handles(u16 n);                /* task.c */

u16 KAPI_ES SetHandleCount(u16 n)                                  /* KERNEL.199 */
{
    regs16 r;
    if (n > 255)
        n = 255;
    if (n > task_handles(0)) {
        rinit(&r, 0x6700);
        r.bx = n;
        if (dos_call(&r))       /* DOS may round the table up (DOSBox: even) */
            task_handles(n);
    }
    return task_handles(0);
}
