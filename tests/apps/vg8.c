/*
 * vg8.c - VG8: the display driver's enumerations and escapes, and flood
 * fills (drivers/vga/README.md): EnumObjects for pens and brushes (the
 * driver's EnumObj: every object, in order), Escape(QUERYESCSUPPORT) for
 * escape numbers 0-4100 and Escape(GETCOLORTABLE) for indexes -2..20,
 * EnumFonts (the driver's EnumDFonts adds nothing), and FloodFill and
 * ExtFloodFill on a drawn figure (GDI's ScanLR calls) with the result's
 * pixels. Run as the shell by tests/vgaprobe.sh -p with Windows 3.1's
 * VGA.DRV and with w16's; the TK.LOGs must be identical. Values only.
 */
#include <windows.h>

#define TK_REF "vg1ref.h"
#include "tklog.h"

extern int FAR PASCAL U_INITAPP(WORD hinst);
extern WORD __cdecl init_regs[7];

static char buf[700];

static void num(const char *name, int v)
{
    char s[12];
    int i = 11;
    unsigned u = v < 0 ? -v : v;
    s[11] = 0;
    do {
        s[--i] = (char)('0' + u % 10);
        u /= 10;
    } while (u);
    if (v < 0)
        s[--i] = '-';
    out(" ");
    out(name);
    out("=");
    out(s + i);
}

static void hex(const char *name, DWORD v)
{
    char s[9];
    int i;
    for (i = 7; i >= 0; i--) {
        s[i] = "0123456789ABCDEF"[(WORD)v & 15];
        v >>= 4;
    }
    s[8] = 0;
    out(" ");
    out(name);
    out("=");
    out(s);
}

static int count;

int FAR PASCAL __export enum_pen(LPLOGPEN p, LPSTR data)
{
    (void)data;
    out("pen");
    num("n", count++);
    num("style", p->lopnStyle);
    num("w", p->lopnWidth.x);
    hex("color", p->lopnColor);
    out("\r\n");
    return count < 400;
}

int FAR PASCAL __export enum_brush(LPLOGBRUSH b, LPSTR data)
{
    (void)data;
    out("brush");
    num("n", count++);
    num("style", b->lbStyle);
    hex("color", b->lbColor);
    num("hatch", b->lbHatch);
    out("\r\n");
    return count < 400;
}

int FAR PASCAL __export enum_font(LPLOGFONT lf, LPTEXTMETRIC tm, int type, LPSTR data)
{
    (void)tm;
    (void)data;
    out("font ");
    out(lf->lfFaceName);
    num("h", lf->lfHeight);
    num("type", type);
    out("\r\n");
    return 1;
}

static void box(HDC dc, int x0, int y0, int x1, int y1)
{
    int x, y, n;
    for (y = y0; y < y1; y++) {
        n = 0;
        for (x = x0; x < x1 && n < (int)sizeof buf - 3; x++) {
            COLORREF c = GetPixel(dc, x, y);
            buf[n++] = c == 0 ? '#' : c == 0xFFFFFFL ? '-' : c == 0x0000FFL ? 'r' : c == 0x808080L ? '.' : '?';
        }
        buf[n++] = '\r';
        buf[n++] = '\n';
        buf[n] = 0;
        out(buf);
    }
}

int __cdecl tmain(void)
{
    HDC dc;
    HINSTANCE hi = init_regs[5];
    FARPROC p;
    int i;
    WORD esc;
    U_INITAPP(hi);
    dc = GetDC(0);

    p = MakeProcInstance((FARPROC)enum_pen, hi);
    count = 0;
    num("enumpens", EnumObjects(dc, OBJ_PEN, (GOBJENUMPROC)p, NULL));
    out("\r\n");
    p = MakeProcInstance((FARPROC)enum_brush, hi);
    count = 0;
    num("enumbrushes", EnumObjects(dc, OBJ_BRUSH, (GOBJENUMPROC)p, NULL));
    out("\r\n");
    p = MakeProcInstance((FARPROC)enum_font, hi);
    num("enumfonts", EnumFonts(dc, NULL, (FONTENUMPROC)p, NULL));
    out("\r\n");

    out("escapes");
    for (esc = 0; esc <= 4100; esc++) {
        WORD q = esc;
        int r = Escape(dc, QUERYESCSUPPORT, sizeof q, (LPSTR)&q, NULL);
        if (r)
            num("e", esc), num("r", r);
    }
    out("\r\n");
    for (i = -2; i <= 20; i++) {
        DWORD c = 0x5A5A5A5AL;
        int r = Escape(dc, GETCOLORTABLE, sizeof i, (LPSTR)&i, (LPSTR)&c);
        out("colortable");
        num("i", i);
        num("r", r);
        hex("c", c);
        out("\r\n");
    }

    /* flood fills: a gray area, a black outline with a gap, filled red */
    {
        HBRUSH g = CreateSolidBrush(0x808080L), r = CreateSolidBrush(0x0000FFL), ob;
        RECT rc;
        rc.left = 0;
        rc.top = 0;
        rc.right = 120;
        rc.bottom = 60;
        FillRect(dc, &rc, g);
        MoveTo(dc, 10, 10);
        LineTo(dc, 100, 10);
        LineTo(dc, 100, 50);
        LineTo(dc, 10, 50);
        LineTo(dc, 10, 10);
        MoveTo(dc, 30, 10);
        LineTo(dc, 60, 40);
        ob = SelectObject(dc, r);
        out("floodfill");
        num("r", FloodFill(dc, 20, 30, 0));
        out("\r\n");
        box(dc, 0, 0, 120, 60);
        FillRect(dc, &rc, g);
        MoveTo(dc, 10, 10);
        LineTo(dc, 100, 10);
        LineTo(dc, 100, 50);
        LineTo(dc, 10, 50);
        LineTo(dc, 10, 10);
        out("extfloodfill");
        num("r", ExtFloodFill(dc, 50, 30, 0x808080L, FLOODFILLSURFACE));
        out("\r\n");
        box(dc, 0, 0, 120, 60);
        SelectObject(dc, ob);
        DeleteObject(g);
        DeleteObject(r);
    }
    out("end\r\n");
    ReleaseDC(0, dc);
    return 0;
}
