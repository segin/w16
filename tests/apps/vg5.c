/*
 * vg5.c - VG5: lines as the display driver draws them (drivers/vga/
 * README.md): every pen style, solid and styled, on the screen, in
 * directions and slopes of every kind, single lines and polylines made
 * of several LineTo calls, with an opaque and a transparent background.
 * Run as the shell by tests/vgaprobe.sh -p with Windows 3.1's VGA.DRV and
 * with w16's; the TK.LOGs must be identical.
 *
 * Each line is drawn on a gray area with a black pen, white background
 * colour; the log has, for each, the pixels of its bounding box (one
 * character a pixel: # black, - white, . gray), row by row. Values only.
 */
#include <windows.h>

#define TK_REF "vg1ref.h"
#include "tklog.h"

extern int FAR PASCAL U_INITAPP(WORD hinst);
extern WORD __cdecl init_regs[7];

static char line[700];

static void dumpbox(HDC dc, const char *name, int x0, int y0, int x1, int y1)
{
    int x, y, n;
    out("line ");
    out(name);
    out("\r\n");
    for (y = y0; y <= y1; y++) {
        n = 0;
        for (x = x0; x <= x1 && n < (int)sizeof line - 3; x++) {
            COLORREF c = GetPixel(dc, x, y);
            line[n++] = c == 0 ? '#' : c == 0xFFFFFFL ? '-' : c == 0x808080L ? '.' : '?';
        }
        line[n++] = '\r';
        line[n++] = '\n';
        line[n] = 0;
        out(line);
    }
}

/* a test: the area (x0, y0) - (x1, y1) cleared to gray, then the points
   joined by LineTo from the first, dumped */
static void test(HDC dc, const char *name, int bk, const int *pts, int n)
{
    int i, x0 = 9999, y0 = 9999, x1 = -1, y1 = -1;
    RECT r;
    HBRUSH g = CreateSolidBrush(0x808080L);
    for (i = 0; i < n; i++) {
        if (pts[2 * i] < x0) x0 = pts[2 * i];
        if (pts[2 * i] > x1) x1 = pts[2 * i];
        if (pts[2 * i + 1] < y0) y0 = pts[2 * i + 1];
        if (pts[2 * i + 1] > y1) y1 = pts[2 * i + 1];
    }
    x0--; y0--; x1++; y1++;
    r.left = x0;
    r.top = y0;
    r.right = x1 + 1;
    r.bottom = y1 + 1;
    FillRect(dc, &r, g);
    DeleteObject(g);
    SetBkMode(dc, bk);
    MoveTo(dc, pts[0], pts[1]);
    for (i = 1; i < n; i++)
        LineTo(dc, pts[2 * i], pts[2 * i + 1]);
    dumpbox(dc, name, x0, y0, x1, y1);
}

int __cdecl tmain(void)
{
    HDC dc;
    int s, bk, k;
    static char name[40];
    static const int t_h[] = { 10, 10, 250, 10 };
    static const int t_hl[] = { 250, 10, 10, 10 };
    static const int t_v[] = { 10, 10, 10, 250 };
    static const int t_vu[] = { 10, 250, 10, 10 };
    static const int t_d[] = { 10, 10, 110, 110 };
    static const int t_du[] = { 110, 110, 10, 10 };
    static const int t_d2[] = { 10, 110, 110, 10 };
    static const int t_s1[] = { 10, 10, 210, 110 };
    static const int t_s2[] = { 10, 10, 110, 210 };
    static const int t_s3[] = { 10, 10, 210, 37 };
    static const int t_s4[] = { 210, 47, 10, 10 };
    static const int t_s5[] = { 10, 10, 47, 210 };
    static const int t_p[] = { 10, 10, 90, 10, 90, 90, 10, 90, 10, 10 };
    static const int t_q[] = { 10, 10, 50, 30, 90, 10, 70, 90, 30, 60 };
    static const int t_short[] = { 10, 10, 13, 10, 13, 14, 20, 14, 21, 15, 30, 15 };
    struct { const char *n; const int *p; int c; } T[] = {
        { "h", t_h, 2 }, { "hl", t_hl, 2 }, { "v", t_v, 2 }, { "vu", t_vu, 2 },
        { "d", t_d, 2 }, { "du", t_du, 2 }, { "d2", t_d2, 2 }, { "s1", t_s1, 2 },
        { "s2", t_s2, 2 }, { "s3", t_s3, 2 }, { "s4", t_s4, 2 }, { "s5", t_s5, 2 },
        { "rect", t_p, 5 }, { "poly", t_q, 5 }, { "short", t_short, 6 }
    };
    U_INITAPP(init_regs[5]);
    dc = GetDC(0);
    SetBkColor(dc, 0xFFFFFFL);
    for (s = 0; s <= 4; s++)
        for (bk = 1; bk <= 2; bk++) {
            HPEN p = CreatePen(s, 1, 0), op;
            if (s == 0 && bk == 1)
                continue;
            op = SelectObject(dc, p);
            for (k = 0; k < (int)(sizeof T / sizeof T[0]); k++) {
                int i = 0;
                const char *q = T[k].n;
                name[i++] = (char)('0' + s);
                name[i++] = bk == 2 ? 'o' : 't';
                name[i++] = ' ';
                while (*q)
                    name[i++] = *q++;
                name[i] = 0;
                test(dc, name, bk, T[k].p, T[k].c);
            }
            SelectObject(dc, op);
            DeleteObject(p);
        }
    out("end\r\n");
    ReleaseDC(0, dc);
    return 0;
}
