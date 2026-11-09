/*
 * gd3.c - GD3, the drawing probe for w16's GDI (ARCHITECTURE.md 11).
 *
 * Runs as the Windows shell (tests/gdiboot.sh -f build/dos/GD3.EXE=GD3.EXE
 * -i 'boot:shell=C:\GD3.EXE'), draws every kind of GDI output on the
 * screen DC (lines, shapes with pens, brushes and ROPs, regions, text,
 * mapping modes) at fixed places, then waits. A screenshot of the result
 * under Microsoft's GDI and under w16's, and the GD1 log of the driver
 * calls each made (tests/gdiboot.sh -l, tools/gdtrace.py), show where w16
 * differs. Only documented APIs are used.
 */
#include <windows.h>

extern WORD __cdecl init_regs[7];
extern WORD FAR PASCAL WaitEvent(HTASK);
extern BOOL FAR PASCAL InitApp(HINSTANCE);

static void shapes(HDC dc, int x, int y)
{
    POINT star[5];
    Rectangle(dc, x, y, x + 40, y + 30);
    Ellipse(dc, x + 50, y, x + 110, y + 30);
    Ellipse(dc, x + 120, y, x + 127, y + 5);
    Ellipse(dc, x + 120, y + 10, x + 140, y + 30);
    RoundRect(dc, x + 150, y, x + 200, y + 30, 12, 10);
    Pie(dc, x + 210, y, x + 260, y + 30, x + 260, y, x + 210, y + 30);
    Chord(dc, x + 270, y, x + 320, y + 30, x + 320, y + 15, x + 270, y);
    Arc(dc, x + 330, y, x + 380, y + 30, x + 380, y + 30, x + 330, y);
    star[0].x = x + 410; star[0].y = y;
    star[1].x = x + 420; star[1].y = y + 30;
    star[2].x = x + 395; star[2].y = y + 10;
    star[3].x = x + 425; star[3].y = y + 10;
    star[4].x = x + 400; star[4].y = y + 30;
    Polygon(dc, star, 5);
    SetPolyFillMode(dc, WINDING);
    OffsetViewportOrg(dc, 40, 0);
    Polygon(dc, star, 5);
    OffsetViewportOrg(dc, -40, 0);
    SetPolyFillMode(dc, ALTERNATE);
    MoveTo(dc, x + 480, y);
    LineTo(dc, x + 520, y + 30);
    LineTo(dc, x + 530, y);
    LineTo(dc, x + 490, y + 7);
    LineTo(dc, x + 480, y + 29);
    LineTo(dc, x + 560, y + 15);
}

int __cdecl tmain(void)
{
    WORD hinst = init_regs[5];
    HDC dc;
    HPEN wide, dash, dot, red;
    HBRUSH gray, hatch, hatch2;
    HFONT f;
    POINT pl[4];
    int i;
    MSG m;

    WaitEvent(0);
    InitApp(hinst);
    dc = GetDC(0);
    PatBlt(dc, 0, 0, 640, 480, WHITENESS);
    /* row 1: defaults (black pen, white brush) */
    shapes(dc, 10, 10);
    /* row 2: gray brush, no pen */
    gray = CreateSolidBrush(RGB(128, 128, 128));
    SelectObject(dc, gray);
    SelectObject(dc, GetStockObject(NULL_PEN));
    shapes(dc, 10, 50);
    /* row 3: wide red pen, hatched brush */
    wide = CreatePen(PS_SOLID, 5, RGB(255, 0, 0));
    hatch = CreateHatchBrush(HS_DIAGCROSS, RGB(0, 0, 255));
    SelectObject(dc, wide);
    SelectObject(dc, hatch);
    shapes(dc, 10, 95);
    /* row 4: dashed pen, null brush, opaque background yellow */
    dash = CreatePen(PS_DASH, 1, RGB(0, 128, 0));
    SelectObject(dc, dash);
    SelectObject(dc, GetStockObject(NULL_BRUSH));
    SetBkColor(dc, RGB(255, 255, 0));
    shapes(dc, 10, 140);
    /* row 5: dotted pen, transparent, ROP2 XOR, horizontal hatch */
    dot = CreatePen(PS_DOT, 1, RGB(0, 0, 0));
    hatch2 = CreateHatchBrush(HS_HORIZONTAL, RGB(255, 0, 255));
    SelectObject(dc, dot);
    SelectObject(dc, hatch2);
    SetBkMode(dc, TRANSPARENT);
    SetROP2(dc, R2_NOTXORPEN);
    shapes(dc, 10, 180);
    SetROP2(dc, R2_COPYPEN);
    SetBkMode(dc, OPAQUE);
    SetBkColor(dc, RGB(255, 255, 255));
    /* row 6: inside-frame pen, light gray brush; polyline; pixels */
    red = CreatePen(PS_INSIDEFRAME, 3, RGB(128, 0, 0));
    SelectObject(dc, red);
    SelectObject(dc, GetStockObject(LTGRAY_BRUSH));
    shapes(dc, 10, 220);
    SelectObject(dc, GetStockObject(BLACK_PEN));
    pl[0].x = 10; pl[0].y = 300; pl[1].x = 60; pl[1].y = 260;
    pl[2].x = 110; pl[2].y = 300; pl[3].x = 160; pl[3].y = 262;
    Polyline(dc, pl, 4);
    for (i = 0; i < 20; i++)
        SetPixel(dc, 180 + i * 3, 280 + (i & 3), RGB(i * 12, 0, 255 - i * 12));
    /* row 7: text: system, a raster face, an aligned and an opaque one */
    TextOut(dc, 10, 320, "System 0123 AaBbGgYy", 20);
    f = CreateFont(-11, 0, 0, 0, 400, 0, 0, 0, 0, 0, 0, 0, 0, "MS Sans Serif");
    SelectObject(dc, f);
    SetTextColor(dc, RGB(0, 0, 255));
    TextOut(dc, 220, 320, "MS Sans Serif 8", 15);
    SetTextAlign(dc, TA_RIGHT | TA_BASELINE);
    TextOut(dc, 630, 340, "right/baseline", 14);
    SetTextAlign(dc, TA_LEFT | TA_TOP);
    {
        RECT r;
        SetRect(&r, 10, 350, 200, 370);
        SetBkColor(dc, RGB(0, 255, 255));
        ExtTextOut(dc, 15, 352, ETO_OPAQUE | ETO_CLIPPED, &r, "ExtTextOut opaque clipped text", 30, NULL);
    }
    SelectObject(dc, GetStockObject(SYSTEM_FONT));
    /* row 8: mapping modes */
    SelectObject(dc, GetStockObject(GRAY_BRUSH));
    SetMapMode(dc, MM_LOMETRIC);
    SetViewportOrg(dc, 10, 380);
    Rectangle(dc, 0, 0, 200, -150);
    Ellipse(dc, 250, 0, 500, -150);
    SetMapMode(dc, MM_TEXT);
    SetViewportOrg(dc, 0, 0);
    /* InvertRect, FrameRect, region painting */
    {
        RECT r;
        HRGN rg = CreateEllipticRgn(400, 380, 500, 460), r2 = CreateRectRgn(450, 400, 560, 470);
        CombineRgn(rg, rg, r2, RGN_XOR);
        FillRgn(dc, rg, gray);
        FrameRgn(dc, rg, GetStockObject(BLACK_BRUSH), 2, 1);
        SetRect(&r, 580, 380, 630, 470);
        InvertRect(dc, &r);
        DeleteObject(rg);
        DeleteObject(r2);
    }
    ReleaseDC(0, dc);
    while (GetMessage(&m, 0, 0, 0))
        DispatchMessage(&m);
    return 0;
}
