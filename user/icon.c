/*
 * icon.c - icons, cursors and bitmaps from resources.
 *
 * An HICON or HCURSOR is a global block holding a 12-byte header (hot spot
 * x, y, width, height, bytes per row, planes, bits per pixel), the AND mask
 * and then the XOR image in the display driver's format: the layout
 * Program Manager copies into .GRP files (ARCHITECTURE.md 6.11) and the one
 * DISPLAY's SetCursor takes (CURSORSHAPE, Windows 3.1 DDK). USER makes the
 * device bits through GDI, as US1 saw Windows 3.1's do: SetDIBits into a
 * bitmap, then GetBitmapBits. The system cursors and icons (IDC_*, IDI_*)
 * come from the display driver's resources; LoadIcon/LoadCursor give the
 * same handle for the same resource (a cache, per module and name).
 */
#include "user.h"

extern HMODULE hModDisplay;

typedef struct ICONHDR {
    int  hotx, hoty, cx, cy, wbytes;
    BYTE planes, bpp;
} ICONHDR;

typedef struct CACHE {
    WORD next;
    HMODULE mod;
    WORD id;                    /* integer name, or an atom of a string name */
    WORD cursor;
    HGLOBAL h;
} CACHE;
typedef CACHE __near *PCACHE;
static WORD cache;

static int dib_colors(const BITMAPINFOHEADER __far *bi)
{
    if (bi->biClrUsed)
        return (int)bi->biClrUsed;
    return bi->biBitCount <= 8 ? 1 << bi->biBitCount : 0;
}

/* make an icon/cursor block from an RT_ICON/RT_CURSOR resource (DIB) */
static HGLOBAL make_block(const BYTE __far *res, BOOL cursor)
{
    const BITMAPINFOHEADER __far *bi;
    int hotx = 0, hoty = 0, cx, cy, wb, nc, planes, bpp;
    const BYTE __far *xorbits, *andbits;
    HGLOBAL g;
    BYTE __far *p;
    DWORD xorsize, andsize;
    HBITMAP bm;
    static struct { BITMAPINFOHEADER h; RGBQUAD c[2]; } mono = {
        { sizeof(BITMAPINFOHEADER), 0, 0, 1, 1, 0, 0, 0, 0, 2, 0 },
        { { 0, 0, 0, 0 }, { 255, 255, 255, 0 } }
    };
    if (cursor) {
        hotx = ((const WORD __far *)res)[0];
        hoty = ((const WORD __far *)res)[1];
        res += 4;
    }
    bi = (const BITMAPINFOHEADER __far *)res;
    if (bi->biSize != sizeof(BITMAPINFOHEADER))
        return 0;                       /* UNVERIFIED: 2.x-format resources */
    cx = (int)bi->biWidth;
    cy = (int)bi->biHeight / 2;
    nc = dib_colors(bi);
    xorbits = res + bi->biSize + nc * sizeof(RGBQUAD);
    {
        DWORD xrow = ((cx * bi->biBitCount + 31) / 32) * 4;
        andbits = xorbits + xrow * cy;
    }
    wb = ((cx + 15) / 16) * 2;
    if (cursor || bi->biBitCount == 1) {
        planes = bpp = 1;
        if (!cursor) {
            planes = GetDeviceCaps(hdcScreen, PLANES);
            bpp = GetDeviceCaps(hdcScreen, BITSPIXEL);
        }
    } else {
        planes = GetDeviceCaps(hdcScreen, PLANES);
        bpp = GetDeviceCaps(hdcScreen, BITSPIXEL);
    }
    andsize = (DWORD)wb * cy;
    xorsize = (DWORD)(((cx * bpp + 15) / 16) * 2) * cy * planes;
    g = GlobalAlloc(GMEM_MOVEABLE | GMEM_SHARE, sizeof(ICONHDR) + andsize + xorsize);
    if (!g)
        return 0;
    p = (BYTE __far *)GlobalLock(g);
    {
        ICONHDR __far *h = (ICONHDR __far *)p;
        h->hotx = hotx;
        h->hoty = hoty;
        h->cx = cx;
        h->cy = cy;
        h->wbytes = wb;
        h->planes = (BYTE)planes;
        h->bpp = (BYTE)bpp;
    }
    /* the AND mask: a mono bitmap from the DIB's mask */
    mono.h.biWidth = cx;
    mono.h.biHeight = cy;
    bm = CreateBitmap(cx, cy, 1, 1, NULL);
    SetDIBits(hdcScreen, bm, 0, cy, andbits, (BITMAPINFO __far *)&mono, DIB_RGB_COLORS);
    GetBitmapBits(bm, andsize, p + sizeof(ICONHDR));
    DeleteObject(bm);
    /* the XOR image, in the device's format */
    {
        BITMAPINFOHEADER __far *xb;
        HGLOBAL hx = GlobalAlloc(GMEM_MOVEABLE, bi->biSize + nc * sizeof(RGBQUAD));
        xb = (BITMAPINFOHEADER __far *)GlobalLock(hx);
        fmemcpy(xb, bi, (WORD)(bi->biSize + nc * sizeof(RGBQUAD)));
        xb->biHeight = cy;
        if (planes == 1 && bpp == 1)
            bm = CreateBitmap(cx, cy, 1, 1, NULL);
        else
            bm = CreateCompatibleBitmap(hdcScreen, cx, cy);
        SetDIBits(hdcScreen, bm, 0, cy, xorbits, (BITMAPINFO __far *)xb, DIB_RGB_COLORS);
        GetBitmapBits(bm, xorsize, p + sizeof(ICONHDR) + andsize);
        DeleteObject(bm);
        GlobalUnlock(hx);
        GlobalFree(hx);
    }
    GlobalUnlock(g);
    return g;
}

/* pick the image of an icon/cursor directory that suits the display:
   32x32, the most colours the display has (UNVERIFIED: Windows' exact
   choice among several) */
static WORD pick(const BYTE __far *dir, BOOL cursor)
{
    WORD n = ((const WORD __far *)dir)[2], i, best = 0;
    int bestbits = -1;
    int maxbits = GetDeviceCaps(hdcScreen, PLANES) * GetDeviceCaps(hdcScreen, BITSPIXEL);
    for (i = 0; i < n; i++) {
        const BYTE __far *e = dir + 6 + 14 * i;
        int w, bits;
        if (cursor) {
            w = ((const WORD __far *)e)[0];
            bits = ((const WORD __far *)e)[3];
        } else {
            w = e[0];
            bits = ((const WORD __far *)e)[3];
        }
        if (w != 32 && n > 1)
            continue;
        if (bits > maxbits)
            continue;
        if (bits > bestbits) {
            bestbits = bits;
            best = ((const WORD __far *)e)[6];
        }
    }
    if (!best && n)
        best = ((const WORD __far *)(dir + 6))[6];
    return best;
}

HICON load_icon_cursor(HINSTANCE hinst, LPCSTR name, BOOL cursor)
{
    HMODULE mod;
    HRSRC r;
    HGLOBAL gd, gi, h;
    const BYTE __far *dir;
    WORD id, key;
    PCACHE c;
    if (!hinst) {
        WORD n = LOWORD((DWORD)name);
        /* the display's system cursors and icons; USER's own others */
        mod = (ISATOM(name) && n >= 32512 && n < 32646) ? hModDisplay : hModUser;
    } else
        mod = K_GETEXEPTR(hinst);
    key = ISATOM(name) ? LOWORD((DWORD)name) : AddAtom(name);
    for (c = (PCACHE)cache; c; c = (PCACHE)c->next)
        if (c->mod == mod && c->id == key && c->cursor == (WORD)cursor)
            return (HICON)c->h;
    r = FindResource(mod ? mod : hinst, name, cursor ? RT_GROUP_CURSOR : RT_GROUP_ICON);
    if (!r)
        return 0;
    gd = LoadResource(mod, r);
    dir = (const BYTE __far *)LockResource(gd);
    if (!dir)
        return 0;
    id = pick(dir, cursor);
    GlobalUnlock(gd);
    FreeResource(gd);
    r = FindResource(mod, MAKEINTRESOURCE(id), cursor ? RT_CURSOR : RT_ICON);
    if (!r)
        return 0;
    gi = LoadResource(mod, r);
    h = make_block((const BYTE __far *)LockResource(gi), cursor);
    GlobalUnlock(gi);
    FreeResource(gi);
    if (!h)
        return 0;
    c = (PCACHE)LocalAlloc(LPTR, sizeof(CACHE));
    if (c) {
        c->mod = mod;
        c->id = key;
        c->cursor = cursor;
        c->h = h;
        c->next = cache;
        cache = (WORD)c;
    }
    return (HICON)h;
}

HICON WINAPI LoadIcon(HINSTANCE hinst, LPCSTR name)
{
    return load_icon_cursor(hinst, name, FALSE);
}

HCURSOR WINAPI LoadCursor(HINSTANCE hinst, LPCSTR name)
{
    return (HCURSOR)load_icon_cursor(hinst, name, TRUE);
}

static HGLOBAL create_icon(int hotx, int hoty, int cx, int cy, BYTE planes, BYTE bpp,
                           const void __far *andb, const void __far *xorb)
{
    WORD wb = ((cx + 15) / 16) * 2;
    DWORD andsize = (DWORD)wb * cy;
    DWORD xorsize = (DWORD)(((cx * bpp + 15) / 16) * 2) * cy * planes;
    HGLOBAL g = GlobalAlloc(GMEM_MOVEABLE | GMEM_SHARE, sizeof(ICONHDR) + andsize + xorsize);
    BYTE __far *p;
    ICONHDR __far *h;
    if (!g)
        return 0;
    p = (BYTE __far *)GlobalLock(g);
    h = (ICONHDR __far *)p;
    h->hotx = hotx;
    h->hoty = hoty;
    h->cx = cx;
    h->cy = cy;
    h->wbytes = wb;
    h->planes = planes;
    h->bpp = bpp;
    fmemcpy(p + sizeof(ICONHDR), andb, (WORD)andsize);
    fmemcpy(p + sizeof(ICONHDR) + andsize, xorb, (WORD)xorsize);
    GlobalUnlock(g);
    return g;
}

HICON WINAPI CreateIcon(HINSTANCE hinst, int cx, int cy, BYTE planes, BYTE bpp,
                        const void __far *andb, const void __far *xorb)
{
    (void)hinst;
    return (HICON)create_icon(cx / 2, cy / 2, cx, cy, planes, bpp, andb, xorb);
}

HCURSOR WINAPI CreateCursor(HINSTANCE hinst, int hx, int hy, int cx, int cy,
                            const void __far *andb, const void __far *xorb)
{
    (void)hinst;
    return (HCURSOR)create_icon(hx, hy, cx, cy, 1, 1, andb, xorb);
}

static BOOL is_cached(HGLOBAL h)
{
    PCACHE c;
    for (c = (PCACHE)cache; c; c = (PCACHE)c->next)
        if (c->h == h)
            return TRUE;
    return FALSE;
}

BOOL WINAPI DestroyIcon(HICON h)
{
    if (!is_cached((HGLOBAL)h))
        GlobalFree((HGLOBAL)h);
    return TRUE;
}

BOOL WINAPI DestroyCursor(HCURSOR h)
{
    return DestroyIcon((HICON)h);
}

HICON WINAPI CopyIcon(HINSTANCE hinst, HICON h)
{
    DWORD n = GlobalSize((HGLOBAL)h);
    HGLOBAL g = GlobalAlloc(GMEM_MOVEABLE | GMEM_SHARE, n);
    (void)hinst;
    if (!g)
        return 0;
    fmemcpy(GlobalLock(g), GlobalLock((HGLOBAL)h), (WORD)n);
    GlobalUnlock(g);
    GlobalUnlock((HGLOBAL)h);
    return (HICON)g;
}

HCURSOR WINAPI CopyCursor(HINSTANCE hinst, HCURSOR h)
{
    return (HCURSOR)CopyIcon(hinst, (HICON)h);
}

void draw_icon(HDC hdc, int x, int y, HICON hi)
{
    BYTE __far *p = (BYTE __far *)GlobalLock((HGLOBAL)hi);
    ICONHDR __far *h = (ICONHDR __far *)p;
    HBITMAP band, bxor, old;
    DWORD andsize;
    if (!p)
        return;
    andsize = (DWORD)h->wbytes * h->cy;
    band = CreateBitmap(h->cx, h->cy, 1, 1, p + sizeof(ICONHDR));
    bxor = CreateBitmap(h->cx, h->cy, h->planes, h->bpp, p + sizeof(ICONHDR) + andsize);
    old = SelectObject(hdcMem, band);
    BitBlt(hdc, x, y, h->cx, h->cy, hdcMem, 0, 0, SRCAND);
    SelectObject(hdcMem, bxor);
    BitBlt(hdc, x, y, h->cx, h->cy, hdcMem, 0, 0, SRCINVERT);
    SelectObject(hdcMem, old);
    DeleteObject(band);
    DeleteObject(bxor);
    GlobalUnlock((HGLOBAL)hi);
}

BOOL WINAPI DrawIcon(HDC hdc, int x, int y, HICON h)
{
    if (!h)
        return FALSE;
    draw_icon(hdc, x, y, h);
    return TRUE;
}

/* LoadBitmap: a DIB resource made into a device bitmap; hinst NULL means
   the display driver's OBM_* bitmaps */
HBITMAP WINAPI LoadBitmap(HINSTANCE hinst, LPCSTR name)
{
    HMODULE mod = hinst ? K_GETEXEPTR(hinst) : hModDisplay;
    HRSRC r = FindResource(mod, name, RT_BITMAP);
    HGLOBAL g;
    const BITMAPINFOHEADER __far *bi;
    HBITMAP bm = 0;
    int cx, cy, nc, mono;
    const BYTE __far *bits;
    if (!r)
        return 0;
    g = LoadResource(mod, r);
    bi = (const BITMAPINFOHEADER __far *)LockResource(g);
    if (!bi) {
        FreeResource(g);
        return 0;
    }
    if (bi->biSize == sizeof(BITMAPCOREHEADER)) {
        const BITMAPCOREHEADER __far *bc = (const BITMAPCOREHEADER __far *)bi;
        cx = bc->bcWidth;
        cy = bc->bcHeight;
        mono = bc->bcBitCount == 1 && bc->bcPlanes == 1;
        nc = bc->bcBitCount <= 8 ? 1 << bc->bcBitCount : 0;
        bits = (const BYTE __far *)bi + bc->bcSize + nc * sizeof(RGBTRIPLE);
    } else {
        cx = (int)bi->biWidth;
        cy = (int)bi->biHeight;
        mono = bi->biBitCount == 1 && bi->biPlanes == 1;
        nc = dib_colors(bi);
        bits = (const BYTE __far *)bi + bi->biSize + nc * sizeof(RGBQUAD);
    }
    bm = mono ? CreateBitmap(cx, cy, 1, 1, NULL) : CreateCompatibleBitmap(hdcScreen, cx, cy);
    if (bm)
        SetDIBits(hdcScreen, bm, 0, cy, bits, (BITMAPINFO __far *)bi, DIB_RGB_COLORS);
    GlobalUnlock(g);
    FreeResource(g);
    return bm;
}
