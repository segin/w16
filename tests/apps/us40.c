/*
 * us40.c - probe US40: LoadBitmap of 1-bit DIB resources whose colour
 * table is not black and white. Found with Win32s's Freecell: CARDS.DLL's
 * cards of hearts and diamonds are 1-bit bitmaps with red and white in
 * their colour table; w16's USER made every 1-bit DIB a monochrome
 * bitmap, and the red pips came out black. Run as the shell (on 3.1 and
 * with w16's USER), its log compared with 3.1's (boot-test case
 * rsrchandler). For each bitmap (us40.rc, made by us40bmp.py: 8 x 8,
 * columns 0-3 colour 0, 4-7 colour 1):
 *   OBS N.planes, N.bits      GetObject's bmPlanes and bmBitsPixel
 *   OBS N.c0=..., N.c1=...    GetPixel of a pixel of colour 0 and of 1
 *                             with the bitmap in a memory DC (a colour
 *                             name, or the RGB value)
 * N: 1 black/white, 2 white/black, 3 red/white, 4 red/green (all
 * BITMAPINFOHEADER), 5 red/white, 6 black/white (BITMAPCOREHEADER); and
 * "cdib", CreateDIBitmap(screen DC, CBM_INIT) of bitmap 3's resource.
 */
#include <windows.h>

#define TK_REF "us40ref.h"
#include "tklog.h"

static void colour(const char *name, COLORREF c)
{
    out("OBS ");
    out(name);
    out("=");
    c &= 0xFFFFFFUL;
    if (c == 0)
        out("black");
    else if (c == 0xFFFFFFUL)
        out("white");
    else if (c == 0x0000FFUL)
        out("red");
    else if (c == 0x00FF00UL)
        out("green");
    else {
        outhex((WORD)(c >> 16));
        outhex((WORD)c);
    }
    out("\r\n");
}

static void report(const char *n, HBITMAP hb)
{
    BITMAP bm;
    HDC dc, mdc;
    HBITMAP old;
    char name[24];
    WORD i;
    for (i = 0; n[i] && i < 12; i++)
        name[i] = n[i];
    name[i] = 0;
    out("OBS ");
    out(name);
    out(".loaded=");
    out(hb ? "1" : "0");
    out("\r\n");
    if (!hb)
        return;
    GetObject(hb, sizeof bm, &bm);
    lstrcpy(name + i, ".planes");
    obs(name, bm.bmPlanes);
    lstrcpy(name + i, ".bits");
    obs(name, bm.bmBitsPixel);
    dc = GetDC(NULL);
    mdc = CreateCompatibleDC(dc);
    ReleaseDC(NULL, dc);
    old = SelectObject(mdc, hb);
    lstrcpy(name + i, ".c0");
    colour(name, GetPixel(mdc, 0, 0));
    lstrcpy(name + i, ".c1");
    colour(name, GetPixel(mdc, 7, 0));
    SelectObject(mdc, old);
    DeleteDC(mdc);
    DeleteObject(hb);
}

int __cdecl tmain(void)
{
    HINSTANCE hi = (HINSTANCE)init_regs[5];     /* DI at the start */
    static const char *names[] = { "1", "2", "3", "4", "5", "6" };
    WORD i;
    HRSRC r;
    out("US40 start\r\n");
    for (i = 0; i < 6; i++)
        report(names[i], LoadBitmap(hi, MAKEINTRESOURCE(i + 1)));
    r = FindResource(hi, MAKEINTRESOURCE(3), RT_BITMAP);
    if (r) {
        HGLOBAL g = LoadResource(hi, r);
        BITMAPINFO FAR *bi = (BITMAPINFO FAR *)LockResource(g);
        if (bi) {
            HDC dc = GetDC(NULL);
            HBITMAP hb = CreateDIBitmap(dc, &bi->bmiHeader, CBM_INIT,
                                        (BYTE FAR *)bi + bi->bmiHeader.biSize + 2 * sizeof(RGBQUAD),
                                        bi, DIB_RGB_COLORS);
            ReleaseDC(NULL, dc);
            report("cdib", hb);
            GlobalUnlock(g);
        }
        FreeResource(g);
    }
    out("US40 end\r\n");
    return failures;
}
