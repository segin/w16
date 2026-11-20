/*
 * bitmap.c - w16 File Manager: the strip of small pictures (resource 100,
 * w16's own art from tools/mkwfbmp.py; layout there).
 *
 * The pictures stand on pure blue (rows 0-15, for selected lines) and pure
 * magenta (rows 16-31); those two colours of the bitmap's colour table are
 * replaced, when it is loaded, by the nearest solid colours to the
 * highlight and window colours (observed: the pictures take those colours
 * on 3.1, WF1; the original imports GetNearestColor and CreateDIBitmap).
 * It is loaded again when the system colours change.
 */
#include "winfile.h"

HBITMAP g_strip;
static HDC strip_dc;
static HBITMAP strip_old;

static DWORD rgbq(COLORREF c)
{
    /* COLORREF 00BBGGRR to RGBQUAD bytes B, G, R, 0 */
    return ((DWORD)GetRValue(c) << 16) | ((DWORD)GetGValue(c) << 8) | GetBValue(c);
}

void bm_load(void)
{
    HRSRC r;
    HGLOBAL h;
    LPBITMAPINFOHEADER src;
    HGLOBAL copy;
    LPBITMAPINFOHEADER bi;
    DWORD FAR *pal;
    DWORD size;
    int ncol, i;
    HDC dc;
    COLORREF hi, win;

    bm_free();
    r = FindResource(g_hinst, MAKEINTRESOURCE(IDB_STRIP), RT_BITMAP);
    if (!r)
        return;
    h = LoadResource(g_hinst, r);
    src = (LPBITMAPINFOHEADER)LockResource(h);
    if (!src)
        return;
    size = SizeofResource(g_hinst, r);
    copy = GlobalAlloc(GMEM_MOVEABLE, size);
    bi = (LPBITMAPINFOHEADER)GlobalLock(copy);
    hmemcpy(bi, src, size);
    UnlockResource(h);
    FreeResource(h);

    dc = GetDC(NULL);
    hi = GetNearestColor(dc, GetSysColor(COLOR_HIGHLIGHT));
    win = GetNearestColor(dc, GetSysColor(COLOR_WINDOW));
    ncol = bi->biClrUsed ? (int)bi->biClrUsed : 1 << bi->biBitCount;
    pal = (DWORD FAR *)((LPBYTE)bi + bi->biSize);
    for (i = 0; i < ncol; i++) {
        DWORD c = pal[i] & 0x00FFFFFFL;
        if (c == 0x000000FFL)               /* pure blue */
            pal[i] = rgbq(hi);
        else if (c == 0x00FF00FFL)          /* pure magenta */
            pal[i] = rgbq(win);
    }
    g_strip = CreateDIBitmap(dc, bi, CBM_INIT, (LPBYTE)(pal + ncol),
                             (LPBITMAPINFO)bi, DIB_RGB_COLORS);
    strip_dc = CreateCompatibleDC(dc);
    if (g_strip)
        strip_old = SelectObject(strip_dc, g_strip);
    ReleaseDC(NULL, dc);
    GlobalUnlock(copy);
    GlobalFree(copy);
}

void bm_free(void)
{
    if (strip_dc) {
        SelectObject(strip_dc, strip_old);
        DeleteDC(strip_dc);
        strip_dc = NULL;
    }
    if (g_strip) {
        DeleteObject(g_strip);
        g_strip = NULL;
    }
}

void bm_draw(HDC dc, int x, int y, int pic, BOOL hilite)
{
    if (strip_dc)
        BitBlt(dc, x, y, CX_BM, CY_BM, strip_dc, pic * CX_BM, hilite ? 0 : CY_BM, SRCCOPY);
}

void bm_draw_drive(HDC dc, int x, int y, int pic)
{
    if (strip_dc)
        BitBlt(dc, x, y, CX_DRIVEBM, CY_DRIVEBM, strip_dc, pic * CX_DRIVEBM, 2 * CY_BM, SRCCOPY);
}
