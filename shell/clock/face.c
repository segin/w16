/*
 * face.c - the analog clock.
 *
 * Geometry observed on Windows 3.1 (the GDI calls of the original at a
 * dozen window sizes, tests/clockscenarios.sh; ARCHITECTURE.md 15.x):
 * - The face fits the client area: m = the smaller of its width and
 *   height, centred, its centre at (cx - m)/2 + m/2 - 1 (same for y).
 * - Hour marks are s x s squares, s = MulDiv(25, m, 640); the radius of
 *   the marks is r = m/2 - s/2 - 4. A mark is a Rectangle in the black
 *   pen filled teal, its top and left edges redrawn cyan; its top left
 *   corner is s/2 up and left of the point on the circle.
 * - The 48 other minutes are dots: 2 x 2 white at the point, 2 x 2
 *   button shadow one up and left of it, the point itself button face.
 * - The hour and minute hands are four lines each, from the two side
 *   points (the centre plus and minus the direction 90 degrees on, scaled
 *   to 7 and 5 per cent of r) to the tip (65 and 80 per cent of r) and to
 *   the tail (15 and 20 per cent, the opposite way). They are drawn in
 *   the window-text pen and erased in the button-face pen, both with
 *   R2_COPYPEN, whenever the minute changes. The hour hand points at
 *   hour * 5 + minute / 12.
 * - The second hand is one line, 80 per cent of r, drawn with R2_NOT, so
 *   drawing it again erases it.
 * - Positions on the circle come from a table of 60 points on a circle of
 *   radius 8000 (sin and cos at 6-degree steps, as integers), y scaled by
 *   the screen's aspect, then MulDiv(v, r, 8000).
 * - Minimized (36 x 36): the 12 hour marks as 2 x 2 black with the top
 *   left pixel white, the hour and minute hands as single lines (65 and
 *   80 per cent), no second hand, then the frame (util.c).
 * Colours: the face is COLOR_BTNFACE, the dots' shadow COLOR_BTNSHADOW,
 * the hands COLOR_WINDOWTEXT; teal, cyan, white and black are fixed
 * (observed with another colour scheme).
 */
#include "clock.h"

/* 60 points on a circle of radius 8000, clockwise from 12: x, y. Made by
   tools/clocktable.py: sin and cos at 6-degree steps, rounded, with the
   entries the original uses one smaller in magnitude corrected as
   observed (its MulDiv calls when it draws the face; ARCHITECTURE.md) */
static int circ[120] = {
#include "circle.h"
};
#define CX(i) circ[2 * (i)]
#define CY(i) circ[2 * (i) + 1]

static int cxc, cyc;                    /* the centre */
static int rad;                         /* radius of the marks */
static int msize, msizev;               /* hour mark size, across and down */
static int fmin;                        /* the smaller client dimension */

void face_init(void)
{
    int i;
    for (i = 0; i < 60; i++)
        CY(i) = MulDiv(CY(i), g_aspv, g_asph);
}

void face_size(int cx, int cy)
{
    int m = cx < cy ? cx : cy;
    fmin = m;
    cxc = (cx - m) / 2 + m / 2 - 1;
    cyc = (cy - m) / 2 + m / 2 - 1;
}

static void sizes(HDC dc)
{
    msize = MulDiv(25, fmin, GetDeviceCaps(dc, HORZRES));
    msizev = MulDiv(msize, g_aspv, g_asph);
    rad = fmin / 2 - msize / 2 - 4;
    if (msize < 2)                      /* observed: 2 across, 1 down */
        msize = 2;
}

/* a point at index i on a circle of radius r round the centre */
static void at(int i, int r, int *x, int *y)
{
    *y = cyc + MulDiv(CY(i), r, 8000);
    *x = cxc + MulDiv(CX(i), r, 8000);
}

static void fill(HDC dc, int x, int y, int w, int h, HBRUSH b)
{
    RECT rc;
    rc.left = x;
    rc.top = y;
    rc.right = x + w;
    rc.bottom = y + h;
    FillRect(dc, &rc, b);
}

static void marks(HDC dc)
{
    int i, x, y;
    for (i = 0; i < 60; i++) {
        at(i, rad, &x, &y);
        if (i % 5 == 0) {
            int l = x - msize / 2, t = y - msizev / 2;
            SelectObject(dc, GetStockObject(BLACK_PEN));
            SelectObject(dc, g_brteal);
            Rectangle(dc, l, t, l + msize, t + msizev);
            SelectObject(dc, g_pncyan);
            MoveTo(dc, l, t + msizev - 1);
            LineTo(dc, l, t);
            LineTo(dc, l + msize - 1, t);
        } else if (msize > 2) {         /* no dots on small faces (observed:
                                           none at s = 2, dots at s = 3) */
            fill(dc, x, y, 2, 2, GetStockObject(WHITE_BRUSH));
            fill(dc, x - 1, y - 1, 2, 2, g_brshadow);
            fill(dc, x, y, 1, 1, g_brface);
        }
    }
}

/* a hand: four lines from the side points to the tip and to the tail */
static void hand(HDC dc, int i, int wpct, int lpct, int tpct, HPEN pen)
{
    int w, l, sx, sy, tx, ty, bx, by;
    SetROP2(dc, R2_COPYPEN);
    SelectObject(dc, pen);
    w = MulDiv(rad, wpct, 100);
    sy = MulDiv(CY((i + 15) % 60), w, 8000);
    sx = MulDiv(CX((i + 15) % 60), w, 8000);
    l = MulDiv(rad, lpct, 100);
    ty = MulDiv(CY(i), l, 8000);
    tx = MulDiv(CX(i), MulDiv(rad, lpct, 100), 8000);
    MoveTo(dc, cxc + sx, cyc + sy);
    LineTo(dc, cxc + tx, cyc + ty);
    MoveTo(dc, cxc - sx, cyc - sy);
    LineTo(dc, cxc + tx, cyc + ty);
    l = MulDiv(rad, tpct, 100);
    by = MulDiv(CY((i + 30) % 60), l, 8000);
    bx = MulDiv(CX((i + 30) % 60), l, 8000);
    MoveTo(dc, cxc + sx, cyc + sy);
    LineTo(dc, cxc + bx, cyc + by);
    MoveTo(dc, cxc - sx, cyc - sy);
    LineTo(dc, cxc + bx, cyc + by);
}

static int hour_pos(const CLOCKTIME *t)
{
    return (t->hour % 12) * 5 + t->minute / 12;
}

static void hands(HDC dc, const CLOCKTIME *t, HPEN pen)
{
    hand(dc, hour_pos(t), 7, 65, 15, pen);
    hand(dc, t->minute, 5, 80, 20, pen);
}

static void second_hand(HDC dc, int s)
{
    int l, x, y;
    MoveTo(dc, cxc, cyc);
    l = MulDiv(rad, 80, 100);
    SetROP2(dc, R2_NOT);
    SelectObject(dc, g_pnface);
    y = MulDiv(CY(s), l, 8000);
    x = MulDiv(CX(s), l, 8000);
    LineTo(dc, cxc + x, cyc + y);
}

void face_paint(HDC dc, const CLOCKTIME *t)
{
    sizes(dc);
    marks(dc);
    hands(dc, t, g_pnhand);
    if (!g_opt.noseconds)
        second_hand(dc, t->second);
}

/* once a second: the second hand moves; when the minute changes the old
   hands are erased (minute hand first) and the new ones drawn */
void face_tick(HDC dc, const CLOCKTIME *old, const CLOCKTIME *t)
{
    sizes(dc);
    if (!g_opt.noseconds)
        second_hand(dc, old->second);
    if (old->minute != t->minute || old->hour != t->hour) {
        hand(dc, old->minute, 5, 80, 20, g_pnface);
        hand(dc, hour_pos(old), 7, 65, 15, g_pnface);
        hand(dc, t->minute, 5, 80, 20, g_pnhand);
        hand(dc, hour_pos(t), 7, 65, 15, g_pnhand);
    }
    if (!g_opt.noseconds)
        second_hand(dc, t->second);
}

static void icon_hand(HDC dc, int i, int lpct)
{
    int l, x, y;
    MoveTo(dc, cxc, cyc);
    l = MulDiv(rad, lpct, 100);
    SetROP2(dc, R2_COPYPEN);
    SelectObject(dc, g_pnhand);
    x = MulDiv(CX(i), l, 8000);
    y = MulDiv(CY(i), l, 8000);
    LineTo(dc, cxc + x, cyc + y);
}

void face_icon(HDC dc, const CLOCKTIME *t)
{
    int i, x, y;
    sizes(dc);
    for (i = 0; i < 60; i += 5) {
        at(i, rad, &x, &y);
        PatBlt(dc, x, y, 2, 2, BLACKNESS);
        PatBlt(dc, x, y, 1, 1, WHITENESS);
    }
    icon_hand(dc, hour_pos(t), 65);
    icon_hand(dc, t->minute, 80);
    icon_frame(dc);
}
