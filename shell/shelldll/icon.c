/*
 * icon.c - w16 SHELL.DLL: ExtractIcon (SHELL.34), ExtractAssociatedIcon
 * (SHELL.36) and InternalExtractIcon (SHELL.39).
 *
 * Icons come from a Windows program or library (the NE resource table:
 * the Windows 3.1 SDK's executable-file format) or an .ICO file (the
 * SDK's icon file format). Observed (probe SH6): an .ICO file holds one
 * icon whatever its number of images; a file that is neither (a text
 * file, a DOS program) gives the count 0 and the "icon" 1 at any index; a
 * missing file, a directory, an index past the end give NULL; no
 * extension is added to the name, which is searched for as OpenFile does.
 * The icon made is the one LoadIcon would make: the image is chosen by
 * USER's GetIconID (USER.455) from the group and made a device icon by
 * LoadIconHandler (USER.456) in a block from DirectResAlloc (KERNEL.168),
 * as SHELL.DLL's imports suggest (Schulman et al., Undocumented Windows,
 * ch. 5-6); the icon blocks matched Windows 3.1's byte for byte (SH6).
 */
#include "shell.h"

#define RT_ICON_N        3
#define RT_GROUP_ICON_N  14

typedef struct {
    HFILE f;
    int kind;                   /* 0 none, 1 not icons (gives 1), 2 NE, 3 ICO */
    WORD count;                 /* icons */
    DWORD restab;               /* NE: the resource table's file offset */
    WORD shift;
} ICONFILE;

static int rd(HFILE f, DWORD at, void __far *p, WORD n)
{
    if (_llseek(f, at, 0) != (LONG)at)
        return 0;
    return _lread(f, (LPSTR)p, n) == n;
}

/* the n-th resource of a type in the NE table: its offset and length in
   bytes and its id; returns the number of resources of the type */
static WORD ne_find(ICONFILE *x, WORD type, WORD n, DWORD *off, DWORD *len, WORD *id)
{
    DWORD p = x->restab + 2;
    WORD t[4], r[6];
    for (;;) {
        if (!rd(x->f, p, t, 8) || !t[0])
            return 0;
        p += 8;
        if (t[0] == (0x8000 | type)) {
            if (off && n < t[1] && rd(x->f, p + (DWORD)n * 12, r, 12)) {
                *off = (DWORD)r[0] << x->shift;
                *len = (DWORD)r[1] << x->shift;
                *id = r[3];
            }
            return t[1];
        }
        p += (DWORD)t[1] * 12;
    }
}

/* the resource of a type with the given integer id */
static int ne_by_id(ICONFILE *x, WORD type, WORD id, DWORD *off, DWORD *len)
{
    DWORD p = x->restab + 2;
    WORD t[4], r[6], i;
    for (;;) {
        if (!rd(x->f, p, t, 8) || !t[0])
            return 0;
        p += 8;
        if (t[0] == (0x8000 | type)) {
            for (i = 0; i < t[1]; i++) {
                if (!rd(x->f, p + (DWORD)i * 12, r, 12))
                    return 0;
                if (r[3] == (0x8000 | id)) {
                    *off = (DWORD)r[0] << x->shift;
                    *len = (DWORD)r[1] << x->shift;
                    return 1;
                }
            }
            return 0;
        }
        p += (DWORD)t[1] * 12;
    }
}

static void open_icons(ICONFILE *x, LPCSTR file)
{
    OFSTRUCT of;
    WORD mz[32], ne[32];
    x->kind = 0;
    x->count = 0;
    if (!file || !*file)
        return;
    x->f = OpenFile(file, &of, OF_READ | OF_SHARE_DENY_NONE);
    if (x->f == HFILE_ERROR)
        x->f = OpenFile(file, &of, OF_READ);
    if (x->f == HFILE_ERROR)
        return;
    x->kind = 1;
    if (!rd(x->f, 0, mz, 6))
        return;
    if (mz[0] == 0 && mz[1] == 1) {             /* .ICO */
        if (mz[2]) {
            x->kind = 3;
            x->count = 1;
        }
        return;
    }
    if (mz[0] != 0x5A4D || !rd(x->f, 0, mz, 64))
        return;
    if (!rd(x->f, *(DWORD *)&mz[30], ne, 64) || ne[0] != 0x454E)
        return;
    x->kind = 2;
    x->restab = *(DWORD *)&mz[30] + ne[0x24 / 2];
    if (ne[0x24 / 2] == ne[0x26 / 2])           /* no resource table */
        return;
    {
        WORD s;
        if (!rd(x->f, x->restab, &s, 2))
            return;
        x->shift = s;
    }
    x->count = ne_find(x, RT_GROUP_ICON_N, 0, NULL, NULL, NULL);
}

static HGLOBAL read_block(HINSTANCE inst, HFILE f, WORD type, DWORD off, DWORD len)
{
    HGLOBAL h;
    void __far *p;
    if (!len || len > 0xFFF0L)
        return NULL;
    h = DirectResAlloc(inst, type, (WORD)len);
    if (!h)
        return NULL;
    p = GlobalLock(h);
    if (!p || !rd(f, off, p, (WORD)len)) {
        if (p)
            GlobalUnlock(h);
        GlobalFree(h);
        return NULL;
    }
    GlobalUnlock(h);
    return h;
}

/* which image of a group directory (NEWHEADER, then 14-byte entries with
   the id in the last WORD): USER's choice, or, where USER does not offer
   GetIconID (w16's own USER, so far), the 32x32 image with the most
   colours up to 16 (UNVERIFIED: USER's exact rule) */
static WORD pick(HGLOBAL group)
{
    const BYTE __far *g;
    WORD id = GetIconID(group, RT_ICON_N), n, i, best = 0, bestscore = 0;
    if (id)
        return id;
    g = (const BYTE __far *)GlobalLock(group);
    if (!g)
        return 0;
    n = *(const WORD __far *)(g + 4);
    for (i = 0; i < n; i++) {
        const BYTE __far *e = g + 6 + i * 14;
        WORD colors = e[2] ? e[2] : 256, score;
        score = (e[0] == 32 && e[1] == 32 ? 0x1000 : 0) + (colors <= 16 ? colors : 1);
        if (score > bestscore) {
            bestscore = score;
            best = *(const WORD __far *)(e + 12);
        }
    }
    GlobalUnlock(group);
    return best;
}

static HICON make_icon(HGLOBAL h)
{
    HICON i = LoadIconHandler(h, TRUE);
    if (!i)
        GlobalFree(h);
    return i;
}

static HICON icon_at(HINSTANCE inst, ICONFILE *x, WORD index)
{
    HGLOBAL g, img;
    WORD id;
    DWORD off, len;
    if (x->kind == 2) {
        WORD gid;
        if (!ne_find(x, RT_GROUP_ICON_N, index, &off, &len, &gid))
            return NULL;
        g = read_block(inst, x->f, RT_GROUP_ICON_N, off, len);
        if (!g)
            return NULL;
        id = pick(g);
        GlobalFree(g);
        if (!id || !ne_by_id(x, RT_ICON_N, id, &off, &len))
            return NULL;
        img = read_block(inst, x->f, RT_ICON_N, off, len);
        return img ? make_icon(img) : NULL;
    }
    if (x->kind == 3) {
        /* the file's directory as a group directory (ids 1..n) */
        WORD n, i;
        BYTE e[16];
        BYTE __far *p;
        if (index || !rd(x->f, 4, &n, 2) || !n)
            return NULL;
        g = GlobalAlloc(GMEM_MOVEABLE, 6 + (DWORD)n * 14);
        p = g ? (BYTE __far *)GlobalLock(g) : NULL;
        if (!p)
            return NULL;
        ((WORD __far *)p)[0] = 0;
        ((WORD __far *)p)[1] = 1;
        ((WORD __far *)p)[2] = n;
        for (i = 0; i < n; i++) {
            if (!rd(x->f, 6 + (DWORD)i * 16, e, 16))
                break;
            sh_move(p + 6 + i * 14, e, 12);
            *(WORD __far *)(p + 6 + i * 14 + 12) = i + 1;
        }
        GlobalUnlock(g);
        id = pick(g);
        GlobalFree(g);
        if (!id || id > n || !rd(x->f, 6 + (DWORD)(id - 1) * 16, e, 16))
            return NULL;
        img = read_block(inst, x->f, RT_ICON_N, *(DWORD *)(e + 12), *(DWORD *)(e + 8));
        return img ? make_icon(img) : NULL;
    }
    return NULL;
}

HICON extract_icon(HINSTANCE inst, LPCSTR file, UINT index)
{
    ICONFILE x;
    HICON h = NULL;
    open_icons(&x, file);
    if (!x.kind)
        return NULL;
    if (index == (UINT)-1)
        h = (HICON)x.count;
    else if (x.kind == 1)
        h = (HICON)1;
    else if (index < x.count)
        h = icon_at(inst, &x, index);
    _lclose(x.f);
    return h;
}

HICON SHAPI ExtractIcon(HINSTANCE inst, LPCSTR file, UINT index)
{
    return extract_icon(inst, file, index);
}

/* An array of nIcons icon handles in a moveable block (NULL past the
   last icon; 1 first for a file without icons), or NULL for no file or
   nIcons = 0 (SH6). */
HGLOBAL SHAPI InternalExtractIcon(HINSTANCE inst, LPCSTR file, UINT first, UINT n)
{
    ICONFILE x;
    HGLOBAL h;
    HICON __far *a;
    UINT i;
    if (!n || n > 0x7FF0)
        return NULL;
    open_icons(&x, file);
    if (!x.kind)
        return NULL;
    h = GlobalAlloc(GMEM_MOVEABLE | GMEM_ZEROINIT, (DWORD)n * 2);
    a = h ? (HICON __far *)GlobalLock(h) : NULL;
    if (a) {
        if (x.kind == 1)
            a[0] = (HICON)1;
        else
            for (i = 0; i < n && first + i < x.count; i++)
                a[i] = icon_at(inst, &x, first + i);
        GlobalUnlock(h);
    }
    _lclose(x.f);
    return h;
}

/* The file's own icon; for a file without icons (ExtractIcon gives 1)
   the icon of its associated program, path becoming that program's name
   and *index 0; failing both, icon 1 (a DOS program by its extension) or
   6 (anything else) of the calling program, path becoming its file name
   (SH6: those are Program Manager's stock icons when Program Manager
   asks). */
HICON SHAPI ExtractAssociatedIcon(HINSTANCE inst, LPSTR path, WORD __far *index)
{
    HICON h = extract_icon(inst, path, *index);
    char exe[160];
    int dos = 0;
    if ((UINT)h > 1)
        return h;
    if ((UINT)h == 1) {
        dos = sh_is_program(path);
        if (!dos && (UINT)find_exec(path, NULL, exe, 0) > 32) {
            lstrcpy(path, exe);
            *index = 0;
            h = extract_icon(inst, path, 0);
            if ((UINT)h > 1)
                return h;
        }
    }
    *index = dos ? 1 : 6;
    GetModuleFileName(inst, path, 128);
    h = extract_icon(inst, path, *index);
    return (UINT)h > 1 ? h : NULL;
}
