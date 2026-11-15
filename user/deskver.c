/*
 * deskver.c - the version stamp on the desktop (a w16 extension).
 *
 * In the style of Windows 3.0's debug pre-release builds (a screenshot the
 * user supplied: white bold italic text in the desktop's bottom right
 * corner, "... Mode Windows v3.0 Debug Release 1.33"), the desktop shows
 * "<Mode> Mode w16 v<version> Debug Release <build>": the mode from
 * GetWinFlags, w16's version from the VERSION file and the build's git
 * commit (build/w16ver.h, made by the Makefile). Windows 3.1 has no such
 * text: [w16] DesktopVersion=0 in SYSTEM.INI (read at start-up) turns it
 * off, and none of this runs (ARCHITECTURE.md 6.8).
 *
 * The drawing is w16's own: the System font (bold), slanted one pixel for
 * every DV_SLANT rows above the cell's bottom row (italic made here, as
 * neither GDI need simulate it for a raster font), a black shadow one
 * pixel right and down, then the text in white, both transparent. The
 * slanted text is made once, as a monochrome bitmap; each desktop paint
 * (WM_ERASEBKGND, after the background, so under every window and clipped
 * to the region being painted) blits it twice. tools/deskstamp.py draws
 * the same for tests/boottest.sh's case deskver: keep the constants equal.
 */
#include "user.h"
#include "w16ver.h"

#define DV_SLANT    3           /* rows per pixel of slant */
#define DV_MARGINX  4           /* the box (text and shadow) from the right */
#define DV_MARGINY  2           /* and from the bottom of the screen */

WORD deskver_on = 1;            /* [w16] DesktopVersion */
static HBITMAP dv_bm;           /* the slanted text, 1 = ink */
static int dv_cx, dv_cy;
static BOOL dv_failed;

void deskver_init(void)
{
    deskver_on = GetPrivateProfileInt("w16", "DesktopVersion", 1, "SYSTEM.INI") != 0;
}

static void dv_cat(char *d, const char __far *s)
{
    while (*d)
        d++;
    while ((*d++ = *s++) != 0)
        ;
}

/* the text, the slanted bitmap of it */
static BOOL dv_make(HDC hdc)
{
    char text[80];
    DWORD fl = GetWinFlags();
    HDC m1, m2;
    HBITMAP b1, o1, o2;
    HFONT of;
    DWORD ext;
    int cx, y, top, sh;
    BOOL ok = FALSE;

    text[0] = 0;
    dv_cat(text, !(fl & WF_PMODE) ? "Real" : (fl & WF_ENHANCED) ? "386 Enhanced" : "Standard");
    dv_cat(text, " Mode w16 v" W16_VERSION " Debug Release " W16_BUILD);

    m1 = CreateCompatibleDC(hdc);
    m2 = CreateCompatibleDC(hdc);
    if (!m1 || !m2)
        goto out;
    of = SelectObject(m1, GetStockObject(SYSTEM_FONT));
    ext = GetTextExtent(m1, text, lstr_len(text));
    cx = LOWORD(ext);
    dv_cy = HIWORD(ext);
    dv_cx = cx + (dv_cy - 1) / DV_SLANT;
    b1 = CreateBitmap(cx, dv_cy, 1, 1, NULL);
    dv_bm = CreateBitmap(dv_cx, dv_cy, 1, 1, NULL);
    if (!b1 || !dv_bm) {
        SelectObject(m1, of);
        if (b1)
            DeleteObject(b1);
        goto out;
    }
    /* the text, ink 1 on 0 */
    o1 = SelectObject(m1, b1);
    SetTextColor(m1, RGB(255, 255, 255));
    SetBkColor(m1, RGB(0, 0, 0));
    SetBkMode(m1, OPAQUE);
    TextOut(m1, 0, 0, text, lstr_len(text));
    /* slanted: the rows that move by as much, one blit each */
    o2 = SelectObject(m2, dv_bm);
    PatBlt(m2, 0, 0, dv_cx, dv_cy, BLACKNESS);
    for (top = 0; top < dv_cy; top = y) {
        sh = (dv_cy - 1 - top) / DV_SLANT;
        for (y = top; y < dv_cy && (dv_cy - 1 - y) / DV_SLANT == sh; y++)
            ;
        BitBlt(m2, sh, top, cx, y - top, m1, 0, top, SRCCOPY);
    }
    SelectObject(m2, o2);
    SelectObject(m1, o1);
    SelectObject(m1, of);
    DeleteObject(b1);
    /* USER's, not the task's that happened to paint the desktop first */
    G_MAKEOBJECTPRIVATE(dv_bm, TRUE);
    ok = TRUE;
out:
    if (!ok && dv_bm) {
        DeleteObject(dv_bm);
        dv_bm = 0;
    }
    if (m2)
        DeleteDC(m2);
    if (m1)
        DeleteDC(m1);
    return ok;
}

/* from the desktop's WM_ERASEBKGND, after the background: the desktop
   DC, rc its client rectangle (the screen) */
void deskver_paint(HDC hdc, const RECT *rc)
{
    HDC m;
    HBITMAP o;
    COLORREF tc, bc;
    int x, y;
    if (!deskver_on || dv_failed)
        return;
    if (!dv_bm && !dv_make(hdc)) {
        dv_failed = TRUE;
        return;
    }
    m = CreateCompatibleDC(hdc);
    if (!m)
        return;
    o = SelectObject(m, dv_bm);
    x = rc->right - DV_MARGINX - (dv_cx + 1);
    y = rc->bottom - DV_MARGINY - (dv_cy + 1);
    /* a monochrome source's 1 bits take the background colour, its 0
       bits the text colour: the shadow ANDs black where the ink is and
       white elsewhere, the text ORs white where the ink is, black
       elsewhere */
    tc = SetTextColor(hdc, RGB(255, 255, 255));
    bc = SetBkColor(hdc, RGB(0, 0, 0));
    BitBlt(hdc, x + 1, y + 1, dv_cx, dv_cy, m, 0, 0, SRCAND);
    SetTextColor(hdc, RGB(0, 0, 0));
    SetBkColor(hdc, RGB(255, 255, 255));
    BitBlt(hdc, x, y, dv_cx, dv_cy, m, 0, 0, SRCPAINT);
    SetTextColor(hdc, tc);
    SetBkColor(hdc, bc);
    SelectObject(m, o);
    DeleteDC(m);
}
