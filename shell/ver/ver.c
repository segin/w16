/*
 * ver.c - w16's VER.DLL: version resources and version-checked file
 * installation (ARCHITECTURE.md, VER).
 *
 * Sources: Windows 3.1 SDK, Programmer's Reference Vol. 2 (GetFileResource,
 * GetFileResourceSize, GetFileVersionInfo, GetFileVersionInfoSize,
 * VerFindFile, VerInstallFile, VerLanguageName, VerQueryValue), Vol. 3
 * (VS_FIXEDFILEINFO), Vol. 4 (the NE resource table; the VERSIONINFO
 * block: nodes of wLength, wValueLength, a key, the value and the
 * children, each on a 4-byte boundary), Vol. 1 ch. 9 (installation),
 * Pietrek, Windows Internals ch. 3 (the module table's link to the next
 * module, offset 6). Everything the SDK does not say was observed
 * black-box on Windows 3.1 with tests/ver/vrp.c: which resources are
 * found, what makes a version resource unreadable, how VerQueryValue
 * walks a block (also a broken one), VerFindFile's search order and
 * buffer rules, VerInstallFile's temporary names, comparisons, result
 * bits and second call, and the language names (ARCHITECTURE.md 20).
 */
#include "ver.h"

extern void FAR PASCAL DOS3Call(void);          /* KERNEL.102: INT 21h */
extern WORD __cdecl ver_ahincr;                 /* entry.asm: KERNEL's __AHINCR */

/* ---- strings and paths (no C runtime) ---- */

/* dir and name as one path: the name alone for an empty dir (observed:
   VerInstallFile's source is then found by OpenFile's search), no second
   backslash after one that ends dir */
static void join(LPSTR out, LPCSTR dir, LPCSTR name)
{
    int n = 0;
    while (dir && *dir && n < PATHMAX - 2)
        out[n++] = *dir++;
    if (n && out[n - 1] != '\\')
        out[n++] = '\\';
    while (*name && n < PATHMAX - 1)
        out[n++] = *name++;
    out[n] = 0;
}

static void copyn(LPSTR d, LPCSTR s, int max)
{
    while (*s && max-- > 1)
        *d++ = *s++;
    *d = 0;
}

/* ---- MS-DOS calls (DOS3Call, on OEM names); the DOS error or 0 ---- */
static WORD dos_call(WORD func, LPCSTR path, LPCSTR path2)
{
    char a[PATHMAX], b[PATHMAX];
    WORD aseg, aoff, bseg, boff, err = 0;
    LPSTR pa = a, pb = b;
    copyn(a, path, PATHMAX);
    AnsiToOem(a, a);
    copyn(b, path2 ? path2 : "", PATHMAX);
    AnsiToOem(b, b);
    aseg = SELECTOROF(pa);
    aoff = OFFSETOF(pa);
    bseg = SELECTOROF(pb);
    boff = OFFSETOF(pb);
    _asm {
        push ds
        push es
        push di
        mov es, bseg
        mov di, boff
        mov dx, aoff
        mov cx, 0
        mov ax, func
        mov ds, aseg
        call DOS3Call
        pop di
        pop es
        pop ds
        jnc ok
        mov err, ax
    ok:
    }
    return err;
}

#define dos_delete(p)     dos_call(0x4100, p, 0)
#define dos_attr(p)       dos_call(0x4300, p, 0)       /* 0: it is there */

/* a file's attributes, 0 if it is not there */
static WORD dos_getattr(LPCSTR path)
{
    char a[PATHMAX];
    WORD pseg, poff, attr = 0;
    LPSTR pa = a;
    copyn(a, path, PATHMAX);
    AnsiToOem(a, a);
    pseg = SELECTOROF(pa);
    poff = OFFSETOF(pa);
    _asm {
        push ds
        mov dx, poff
        mov ax, 4300h
        mov ds, pseg
        call DOS3Call
        pop ds
        jc no
        mov attr, cx
    no:
    }
    return attr;
}
#define dos_rename(p, q)  dos_call(0x5600, p, q)

/* an MS-DOS error as VerInstallFile's bits (SDK: access and sharing
   violations) */
static DWORD errbits(WORD e)
{
    if (e == 5)
        return VIF_ACCESSVIOLATION;
    if (e == 32)
        return VIF_SHARINGVIOLATION;
    return 0;
}

static BOOL exists(LPCSTR path)
{
    OFSTRUCT of;
    char p[PATHMAX];
    copyn(p, path, PATHMAX);
    return OpenFile(p, &of, OF_EXIST) != HFILE_ERROR;
}

/* ---- reading a file (through LZEXPAND: a compressed file is read as
   it expands, observed) ---- */
static BOOL rd(HFILE h, DWORD off, void FAR *buf, DWORD n)
{
    WORD sel = SELECTOROF(buf), o = OFFSETOF(buf);
    if (LZSeek(h, (LONG)off, 0) < 0)
        return FALSE;
    while (n) {
        /* at most 16K, and not across a 64K boundary of a huge block */
        WORD k = n > 0x4000 ? 0x4000 : (WORD)n;
        if (o && k > (WORD)(0 - o))
            k = (WORD)(0 - o);
        if (LZRead(h, (LPSTR)MAKELP(sel, o), k) != (int)k)
            return FALSE;
        o += k;
        if (!o)
            sel += ver_ahincr;
        n -= k;
    }
    return TRUE;
}

static HFILE vopen(LPCSTR name)
{
    OFSTRUCT of;
    char p[PATHMAX];
    copyn(p, name, PATHMAX);
    return LZOpenFile(p, &of, OF_READ);
}

/* the resource (type, id) of an NE file: its offset and length. Only
   integer types and names are found (observed: a named type or name, or
   a "#n" string, never is); the file must start with "MZ" and its new
   header with "NE" (observed); a file whose resource table is where its
   resident names start has none (SDK Vol. 4) */
static BOOL find_res(HFILE h, LPCSTR type, LPCSTR id, DWORD *off, DWORD *len)
{
    BYTE hdr[64];
    DWORD ne, p;
    WORD shift, t, cnt, i, guard;
    BYTE ent[12];
    if (SELECTOROF(type) || SELECTOROF(id))
        return FALSE;
    if (!rd(h, 0, hdr, 64) || hdr[0] != 'M' || hdr[1] != 'Z')
        return FALSE;
    ne = *(DWORD *)(hdr + 0x3C);
    if (!rd(h, ne, hdr, 64) || hdr[0] != 'N' || hdr[1] != 'E')
        return FALSE;
    if (*(WORD *)(hdr + 0x24) == *(WORD *)(hdr + 0x26))
        return FALSE;
    p = ne + *(WORD *)(hdr + 0x24);
    if (!rd(h, p, &shift, 2))
        return FALSE;
    p += 2;
    for (guard = 0; guard < 1000; guard++) {
        if (!rd(h, p, ent, 8))
            return FALSE;
        t = *(WORD *)ent;
        cnt = *(WORD *)(ent + 2);
        p += 8;
        if (!t)
            return FALSE;
        if (t == (0x8000 | OFFSETOF(type))) {
            for (i = 0; i < cnt; i++, p += 12) {
                if (!rd(h, p, ent, 12))
                    return FALSE;
                if (*(WORD *)(ent + 6) == (0x8000 | OFFSETOF(id))) {
                    *off = (DWORD)*(WORD *)ent << shift;
                    *len = (DWORD)*(WORD *)(ent + 2) << shift;
                    return TRUE;
                }
            }
            return FALSE;
        }
        p += ((DWORD)cnt << 3) + ((DWORD)cnt << 2);    /* 12 bytes each */
    }
    return FALSE;
}

/* GetFileResourceSize (VER.2): the length in the resource table, the
   resource's file offset to *lpdwFileOffset; 0 if not found */
DWORD VAPI GetFileResourceSize(LPCSTR lpszFileName, LPCSTR lpszResType, LPCSTR lpszResID,
                               DWORD FAR *lpdwFileOffset)
{
    HFILE h = vopen(lpszFileName);
    DWORD off, len;
    BOOL ok;
    if (h < 0)
        return 0;
    ok = find_res(h, lpszResType, lpszResID, &off, &len);
    LZClose(h);
    if (!ok)
        return 0;
    *lpdwFileOffset = off;
    return len;
}

/* GetFileResource (VER.3). With an offset, dwResLen bytes are read from
   there whatever the file is (observed: a text file's bytes too); with 0
   the resource is looked for and at most its length is read. TRUE only if
   every byte asked for was read. */
BOOL VAPI GetFileResource(LPCSTR lpszFileName, LPCSTR lpszResType, LPCSTR lpszResID,
                          DWORD dwFileOffset, DWORD dwResLen, void FAR *lpvData)
{
    HFILE h = vopen(lpszFileName);
    DWORD off = dwFileOffset, len;
    BOOL ok = TRUE;
    if (h < 0)
        return FALSE;
    if (!off) {
        ok = find_res(h, lpszResType, lpszResID, &off, &len);
        if (ok && len < dwResLen)
            dwResLen = len;
    }
    if (ok)
        ok = rd(h, off, lpvData, dwResLen);
    LZClose(h);
    return ok;
}

/* GetFileVersionInfoSize (VER.6): the root's wLength, if it is not 0 and
   not more than the resource table's length, and the dword at offset 20
   is the fixed part's signature (observed: nothing else is checked, not
   the key, not wValueLength); the handle is the resource's offset, set
   once the resource is found even if 0 is returned */
DWORD VAPI GetFileVersionInfoSize(LPCSTR lpszFileName, DWORD FAR *lpdwHandle)
{
    HFILE h = vopen(lpszFileName);
    DWORD off, len;
    BYTE b[VS_SIGOFF + 4];
    DWORD r = 0;
    if (h < 0)
        return 0;
    if (find_res(h, MAKEINTRESOURCE(RT_VER), MAKEINTRESOURCE(VER_ID), &off, &len)) {
        *lpdwHandle = off;
        if (rd(h, off, b, sizeof b) && *(WORD *)b && *(WORD *)b <= len &&
            *(DWORD *)(b + VS_SIGOFF) == VS_SIG)
            r = *(WORD *)b;
    }
    LZClose(h);
    return r;
}

/* GetFileVersionInfo (VER.7): the handle is the file offset, read from
   as GetFileResource does (observed: no check of what is there) */
BOOL VAPI GetFileVersionInfo(LPCSTR lpszFileName, DWORD handle, DWORD cbBuf, void FAR *lpvData)
{
    return GetFileResource(lpszFileName, MAKEINTRESOURCE(RT_VER), MAKEINTRESOURCE(VER_ID),
                           handle, cbBuf, lpvData);
}

/* ---- VerQueryValue ---- */
#define W(o) (*(const WORD FAR *)(b + (o)))

/* where a node's value starts: after its key, on a 4-byte boundary from
   the block's start (observed: the same offsets at any address). The key
   is read no further than lim, the end of the node's parent (for the
   root, of the root itself: observed, a root length of 5 or 8 puts its
   value at 8) */
static DWORD valoff(const BYTE FAR *b, DWORD node, DWORD lim)
{
    DWORD k = node + 4;
    while (k < lim && b[k])
        k++;
    if (k < lim)
        k++;
    return (k + 3) & ~3L;
}

/* VerQueryValue (VER.11): the path's names, separated by backslashes
   (empty ones skipped, so "\\" and "" are the root), are looked for
   among the children of each node, regardless of case; a child counts
   only if it ends within its parent (observed: a block whose lengths are
   too long loses its later children). A node of 4 bytes or less ends the
   search (observed for the root: a length of 0 or 4 finds nothing, 5
   finds the root). The value is the node's: after its key, wValueLength
   bytes. Nothing is written when the path is not found. */
BOOL VAPI VerQueryValue(const void FAR *lpvBlock, LPCSTR lpszSubBlock, void FAR * FAR *lplpBuffer,
                        UINT FAR *lpcb)
{
    const BYTE FAR *b = lpvBlock;
    LPCSTR s = lpszSubBlock;
    DWORD node = 0, lim = W(0), end, c;
    char name[256];
    int n;
    if (W(0) <= 4)
        return FALSE;
    for (;;) {
        while (*s == '\\')
            s++;
        if (!*s) {
            *lplpBuffer = (void FAR *)(b + valoff(b, node, lim));
            *lpcb = W(node + 2);
            return TRUE;
        }
        for (n = 0; *s && *s != '\\'; s++)
            if (n < (int)sizeof name - 1)
                name[n++] = *s;
        name[n] = 0;
        end = node + W(node);
        c = (valoff(b, node, lim) + W(node + 2) + 3) & ~3L;
        for (;;) {
            if (c + 4 > end || W(c) <= 4 || c + W(c) > end)
                return FALSE;
            if (!lstrcmpi((LPCSTR)(b + c + 4), name))
                break;
            c = (c + W(c) + 3) & ~3L;
        }
        node = c;
        lim = end;
    }
}

/* ---- VerLanguageName ---- */

/* the names Windows 3.1's VER.DLL gives (observed for every identifier
   0-FFFFh with tests/ver/vrp.c; ARCHITECTURE.md 20.2) */
static const struct {
    WORD id;
    const char *name;
} langs[] = {
    { 0x0401, "Arabic" }, { 0x0402, "Bulgarian" }, { 0x0403, "Catalan" },
    { 0x0404, "Traditional Chinese" }, { 0x0405, "Czech" }, { 0x0406, "Danish" },
    { 0x0407, "German" }, { 0x0408, "Greek" }, { 0x0409, "U.S. English" },
    { 0x040A, "Castilian Spanish" }, { 0x040B, "Finnish" }, { 0x040C, "French" },
    { 0x040D, "Hebrew" }, { 0x040E, "Hungarian" }, { 0x040F, "Icelandic" },
    { 0x0410, "Italian" }, { 0x0411, "Japanese" }, { 0x0412, "Korean" },
    { 0x0413, "Dutch" }, { 0x0414, "Norwegian - Bokmal" }, { 0x0415, "Polish" },
    { 0x0416, "Brazilian Portuguese" }, { 0x0417, "Rhaeto-Romanic" },
    { 0x0418, "Romanian" }, { 0x0419, "Russian" }, { 0x041A, "Croato-Serbian (latin)" },
    { 0x041B, "Slovak" }, { 0x041C, "Albanian" }, { 0x041D, "Swedish" },
    { 0x041E, "Thai" }, { 0x041F, "Turkish" }, { 0x0420, "Urdu" }, { 0x0421, "Bahasa" },
    { 0x0804, "Simplified Chinese" }, { 0x0807, "Swiss German" },
    { 0x0809, "U.K. English" }, { 0x080A, "Mexican Spanish" },
    { 0x080C, "Belgian French" }, { 0x0810, "Swiss Italian" },
    { 0x0813, "Belgian Dutch" }, { 0x0814, "Norwegian - Nynorsk" },
    { 0x0816, "Portuguese" }, { 0x081A, "Serbo-Croatian (cyrillic)" },
    { 0x0C0C, "Canadian French" }, { 0x100C, "Swiss French" },
};

/* VerLanguageName (VER.10): the whole name's length is returned; the
   buffer gets at most cbLang - 1 characters and a NUL, and with cbLang 0
   the whole name (observed) */
UINT VAPI VerLanguageName(UINT uLang, LPSTR lpszLang, UINT cbLang)
{
    const char *s = "Unknown Language";
    UINT i, n;
    for (i = 0; i < sizeof langs / sizeof langs[0]; i++)
        if (langs[i].id == uLang)
            s = langs[i].name;
    n = lstrlen(s);
    for (i = 0; s[i] && i + 1 < (cbLang ? cbLang : 0xFFFFU); i++)
        lpszLang[i] = s[i];
    lpszLang[i] = 0;
    return n;
}

/* ---- files in use: a loaded module's file. The modules are walked from
   KERNEL's along the link at offset 6 of each module table (Pietrek ch. 3)
   and their file names compared (observed: a module counts whatever its
   name, a module of that name loaded from elsewhere does not) ---- */
static BOOL in_use(LPCSTR path)
{
    char p[PATHMAX], m[PATHMAX];
    HMODULE h = GetModuleHandle("KERNEL");
    int guard;
    OFSTRUCT of;
    copyn(p, path, PATHMAX);
    if (OpenFile(p, &of, OF_PARSE) != HFILE_ERROR) {
        copyn(p, of.szPathName, PATHMAX);
        OemToAnsi(p, p);
    }
    for (guard = 0; h && guard < 2000; guard++) {
        if (GetModuleFileName(h, m, sizeof m) && !lstrcmpi(m, p))
            return TRUE;
        h = *(HMODULE FAR *)MAKELP(h, 6);
    }
    return FALSE;
}

/* ---- VerFindFile ---- */

/* the directory part of a full path; the root keeps its backslash */
static void dirpart(LPSTR d, LPCSTR full)
{
    int n = lstrlen(full);
    while (n && full[n - 1] != '\\')
        n--;
    if (n > 1 && full[n - 2] != ':')
        n--;
    copyn(d, full, n + 1);
}

static BOOL try_dir(LPCSTR dir, LPCSTR name, LPSTR full)
{
    char p[PATHMAX];
    OFSTRUCT of;
    join(p, dir, name);
    if (OpenFile(p, &of, OF_EXIST) == HFILE_ERROR)
        return FALSE;
    copyn(full, of.szPathName, PATHMAX);
    OemToAnsi(full, full);
    return TRUE;
}

/* UNVERIFIED: how 3.1 knows it runs as a shared copy (SDK: shared files
   then go to the Windows directory); here, when the system directory is
   not inside the Windows directory, as a network setup leaves them */
static BOOL shared_windows(LPCSTR win, LPCSTR sys)
{
    int n = lstrlen(win);
    char s[PATHMAX];
    copyn(s, sys, n + 1);
    return lstrcmpi(s, win) || sys[n] != '\\';
}

/* VerFindFile (VER.8). The current copy is looked for in the
   application's directory, then the Windows and system directories, for
   a shared file in the Windows and system directories and then the
   application's, then by OpenFile's search (observed; lpszWinDir is not
   used: SDK). The recommended directory: the system directory for a
   shared file, else lpszAppDir as given less one backslash at its end,
   its length counted with it (observed). A buffer too small is left
   alone; its length is set to what is needed either way; a destination
   that does not fit also leaves VFF_CURNEDEST out (observed). */
UINT VAPI VerFindFile(UINT flags, LPCSTR lpszFilename, LPCSTR lpszWinDir, LPCSTR lpszAppDir,
                      LPSTR lpszCurDir, UINT FAR *lpuCurDirLen, LPSTR lpszDestDir,
                      UINT FAR *lpuDestDirLen)
{
    char win[PATHMAX], sys[PATHMAX], full[PATHMAX], cur[PATHMAX], dst[PATHMAX];
    BOOL shared = flags & VFFF_ISSHAREDFILE, found;
    UINT r = 0, need, n;
    (void)lpszWinDir;
    if (!lpszAppDir)
        lpszAppDir = "";
    GetWindowsDirectory(win, sizeof win);
    GetSystemDirectory(sys, sizeof sys);
    full[0] = 0;
    if (shared)
        found = try_dir(win, lpszFilename, full) || try_dir(sys, lpszFilename, full) ||
                try_dir(lpszAppDir, lpszFilename, full);
    else
        found = try_dir(lpszAppDir, lpszFilename, full) || try_dir(win, lpszFilename, full) ||
                try_dir(sys, lpszFilename, full);
    if (!found)
        found = try_dir("", lpszFilename, full);
    cur[0] = 0;
    if (found)
        dirpart(cur, full);
    if (found && in_use(full))
        r |= VFF_FILEINUSE;
    if (shared)
        copyn(dst, shared_windows(win, sys) ? win : sys, PATHMAX);
    else
        copyn(dst, lpszAppDir, PATHMAX);
    need = lstrlen(cur) + 1;
    if (*lpuCurDirLen >= need)
        lstrcpy(lpszCurDir, cur);
    else
        r |= VFF_BUFFTOOSMALL;
    *lpuCurDirLen = need;
    need = lstrlen(dst) + 1;
    if (*lpuDestDirLen >= need) {
        lstrcpy(lpszDestDir, dst);
        n = need - 1;
        if (n && lpszDestDir[n - 1] == '\\')
            lpszDestDir[n - 1] = 0;
        if (lstrcmpi(cur, lpszDestDir))
            r |= VFF_CURNEDEST;
    } else
        r |= VFF_BUFFTOOSMALL;
    *lpuDestDirLen = need;
    return r;
}

/* ---- VerInstallFile ---- */

/* a file's version block, in a global block (0 if it has none) */
static HGLOBAL getver(LPCSTR path)
{
    DWORD h, n = GetFileVersionInfoSize(path, &h);
    HGLOBAL g;
    if (!n)
        return 0;
    g = GlobalAlloc(GMEM_MOVEABLE, n);
    if (!g)
        return 0;
    if (!GetFileVersionInfo(path, h, n, GlobalLock(g))) {
        GlobalUnlock(g);
        GlobalFree(g);
        return 0;
    }
    GlobalUnlock(g);
    return g;
}

/* the new file against the one it replaces (observed): nothing if that
   has no version; the new one older (file version only) or without a
   version: VIF_SRCOLD; another file type or subtype (not OS):
   VIF_DIFFTYPE; a language and code page of the old one's Translation
   missing from the new one's (both having one): VIF_DIFFLANG (any code
   page, but code page 0 in the old file's matches any; VIF_DIFFCODEPG
   was never seen); VIF_MISMATCH with any of them */
static DWORD compare(LPCSTR newf, LPCSTR oldf)
{
    HGLOBAL go = getver(oldf), gn;
    const void FAR *bo, *bn;
    FIXEDINFO FAR *fo, FAR *fn;
    DWORD FAR *to, FAR *tn;
    UINT lo, ln, i, j;
    DWORD r = 0;
    if (!go)
        return 0;
    gn = getver(newf);
    if (!gn) {
        GlobalFree(go);
        return VIF_SRCOLD | VIF_MISMATCH;
    }
    bo = GlobalLock(go);
    bn = GlobalLock(gn);
    if (VerQueryValue(bo, "\\", (void FAR * FAR *)&fo, &lo) &&
        VerQueryValue(bn, "\\", (void FAR * FAR *)&fn, &ln)) {
        if (fn->dwFileVersionMS < fo->dwFileVersionMS ||
            (fn->dwFileVersionMS == fo->dwFileVersionMS &&
             fn->dwFileVersionLS < fo->dwFileVersionLS))
            r |= VIF_SRCOLD;
        if (fn->dwFileType != fo->dwFileType || fn->dwFileSubtype != fo->dwFileSubtype)
            r |= VIF_DIFFTYPE;
    }
    if (VerQueryValue(bo, "\\VarFileInfo\\Translation", (void FAR * FAR *)&to, &lo) &&
        VerQueryValue(bn, "\\VarFileInfo\\Translation", (void FAR * FAR *)&tn, &ln) &&
        lo >= 4 && ln >= 4) {
        for (i = 0; i < lo / 4; i++) {
            /* code page 0 in the old file's entry matches any (observed) */
            for (j = 0; j < ln / 4; j++)
                if (LOWORD(tn[j]) == LOWORD(to[i]) &&
                    (!HIWORD(to[i]) || HIWORD(tn[j]) == HIWORD(to[i])))
                    break;
            if (j == ln / 4)
                r |= VIF_DIFFLANG;
        }
    }
    GlobalUnlock(go);
    GlobalUnlock(gn);
    GlobalFree(go);
    GlobalFree(gn);
    if (r)
        r |= VIF_MISMATCH;
    return r;
}

/* the same file, by full names (observed: a current directory spelt
   otherwise, relative or with a backslash at its end, is the
   destination) */
static BOOL samefile(LPCSTR a, LPCSTR b)
{
    char pa[PATHMAX], pb[PATHMAX];
    OFSTRUCT of;
    copyn(pa, a, PATHMAX);
    copyn(pb, b, PATHMAX);
    if (OpenFile(pa, &of, OF_PARSE) != HFILE_ERROR)
        copyn(pa, of.szPathName, PATHMAX);
    if (OpenFile(pb, &of, OF_PARSE) != HFILE_ERROR)
        copyn(pb, of.szPathName, PATHMAX);
    return !lstrcmpi(pa, pb);
}

/* VerInstallFile (VER.9), as observed:
   - forced, with a name in lpszTmpFile that is there in the destination
     directory: that file is installed (the second call; the source is not
     read, nothing compared);
   - else the source (lpszSrcDir and lpszSrcFilename, through LZOpenFile)
     is expanded into "temp.NNN" in the destination directory, NNN the
     first unused of 000, 001, ... in hexadecimal (cannot open the source,
     or it does not expand: VIF_CANNOTREADSRC; cannot create the
     temporary file: VIF_CANNOTCREATE) and, unless forced, compared with
     the destination file;
   - the destination file in use: VIF_FILEINUSE alone, the temporary file
     made here deleted;
   - differences: the bits with VIF_TEMPFILE and the temporary name (its
     length to *lpwTmpFileLen); a buffer too small: VIF_BUFFTOOSMALL
     instead of VIF_TEMPFILE, the length needed, the file deleted;
   - else the destination is deleted (failing: VIF_CANNOTDELETE and the
     error's bit, the temporary file deleted) and the temporary file
     renamed to it; then, unless VIFF_DONTDELETEOLD, the copy in
     lpszCurDir if that is another file (not in use and deleted, else
     VIF_CANNOTDELETECUR). Nothing is written to lpszTmpFile then. */
DWORD VAPI VerInstallFile(UINT flags, LPCSTR lpszSrcFilename, LPCSTR lpszDestFilename,
                          LPCSTR lpszSrcDir, LPCSTR lpszDestDir, LPCSTR lpszCurDir,
                          LPSTR lpszTmpFile, UINT FAR *lpwTmpFileLen)
{
    char dpath[PATHMAX], tpath[PATHMAX], spath[PATHMAX], tname[16];
    BOOL made = FALSE;
    DWORD r = 0;
    WORD e, k;
    join(dpath, lpszDestDir, lpszDestFilename);
    tpath[0] = 0;
    if ((flags & VIFF_FORCEINSTALL) && lpszTmpFile[0]) {
        join(tpath, lpszDestDir, lpszTmpFile);
        if (!exists(tpath))
            tpath[0] = 0;
    }
    if (!tpath[0]) {
        OFSTRUCT of;
        HFILE hs, ht;
        LONG c;
        join(spath, lpszSrcDir, lpszSrcFilename);
        hs = LZOpenFile(spath, &of, OF_READ);
        if (hs < 0)
            return VIF_CANNOTREADSRC;
        for (k = 0; k < 0x1000; k++) {
            wsprintf(tname, "temp.%03X", k);
            join(tpath, lpszDestDir, tname);
            if (!exists(tpath))
                break;
        }
        ht = OpenFile(tpath, &of, OF_CREATE | OF_WRITE);
        if (ht == HFILE_ERROR) {
            LZClose(hs);
            return VIF_CANNOTCREATE;
        }
        c = LZCopy(hs, ht);
        LZClose(hs);
        _lclose(ht);
        made = TRUE;
        if (c < 0) {
            dos_delete(tpath);
            if (c == LZERROR_GLOBALLOC || c == LZERROR_GLOBLOCK)
                return VIF_OUTOFMEMORY;
            if (c == LZERROR_BADOUTHANDLE || c == LZERROR_WRITE)
                return VIF_OUTOFSPACE;         /* UNVERIFIED: a full disk */
            return VIF_CANNOTREADSRC;
        }
        if (!(flags & VIFF_FORCEINSTALL))
            r = compare(tpath, dpath);
    }
    /* a read-only destination: VIF_WRITEPROT alone, before anything else
       is looked at, forced or not (observed in DOSBox-X, which keeps the
       attribute; DOSBox does not) */
    if (dos_getattr(dpath) & 1) {
        if (made)
            dos_delete(tpath);
        return VIF_WRITEPROT;
    }
    if (in_use(dpath)) {
        if (made)
            dos_delete(tpath);
        return VIF_FILEINUSE;
    }
    if (r) {
        UINT need = lstrlen(tname) + 1;
        if (*lpwTmpFileLen < need) {
            *lpwTmpFileLen = need;
            dos_delete(tpath);
            return r | VIF_BUFFTOOSMALL;
        }
        lstrcpy(lpszTmpFile, tname);
        *lpwTmpFileLen = need;
        return r | VIF_TEMPFILE;
    }
    /* a destination that is there (a directory too, observed) and cannot
       be deleted: an access violation unless a sharing one */
    if (!dos_attr(dpath) && (e = dos_delete(dpath)) != 0) {
        if (made)
            dos_delete(tpath);
        return VIF_CANNOTDELETE | (e == 32 ? VIF_SHARINGVIOLATION : VIF_ACCESSVIOLATION);
    }
    e = dos_rename(tpath, dpath);
    if (e)
        return VIF_CANNOTRENAME | errbits(e);   /* UNVERIFIED: not provoked */
    if (lpszCurDir && lpszCurDir[0] && !(flags & VIFF_DONTDELETEOLD)) {
        join(spath, lpszCurDir, lpszDestFilename);
        if (!samefile(spath, dpath) && !dos_attr(spath) &&
            (in_use(spath) || dos_delete(spath)))
            r |= VIF_CANNOTDELETECUR;
    }
    return r;
}
