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
    if (name[3] == 's' && (name[4] == 't' || name[4] == 'f')) {
        /* the stars: one Polyline call, one Output */
        POINT pp[8];
        for (i = 0; i < n && i < 8; i++) {
            pp[i].x = pts[2 * i];
            pp[i].y = pts[2 * i + 1];
        }
        Polyline(dc, pp, n);
    } else {
        MoveTo(dc, pts[0], pts[1]);
        for (i = 1; i < n; i++)
            LineTo(dc, pts[2 * i], pts[2 * i + 1]);
    }
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
    /* the same line from other starting places: does the style's phase
       follow the screen position? */
    static const int t_x1[] = { 11, 10, 91, 10 }, t_x2[] = { 12, 10, 92, 10 };
    static const int t_x3[] = { 13, 10, 93, 10 }, t_x4[] = { 14, 10, 94, 10 };
    static const int t_x5[] = { 15, 10, 95, 10 }, t_x6[] = { 16, 10, 96, 10 };
    static const int t_x7[] = { 17, 10, 97, 10 }, t_y1[] = { 10, 11, 10, 91 };
    static const int t_y2[] = { 10, 12, 10, 92 }, t_y3[] = { 10, 13, 10, 93 };
    static const int t_y4[] = { 10, 14, 10, 94 }, t_y5[] = { 10, 15, 10, 95 };
    static const int t_y6[] = { 10, 16, 10, 96 }, t_y7[] = { 10, 17, 10, 97 };
    static const int t_st[] = { 420, 140, 430, 170, 405, 150, 435, 150, 410, 170, 420, 140 };
    static const int t_st2[] = { 30, 10, 40, 40, 15, 20, 45, 20, 20, 40, 30, 10 };
    static const int t_z1[] = { 13, 11, 23, 41 }, t_z2[] = { 14, 12, 24, 42 };
    /* polylines that do not cross themselves (one Polyline call each) */
    static const int t_st3[] = { 10, 10, 50, 30, 90, 10, 130, 30 };
    static const int t_st4[] = { 10, 10, 20, 40, 30, 10, 40, 40 };
    static const int t_st5[] = { 10, 10, 40, 40, 70, 10 };
    static const int t_st6[] = { 10, 10, 60, 10, 60, 15, 110, 15 };
    static const int t_st7[] = { 10, 30, 40, 40, 70, 40, 80, 10 };
    static const int t_st8[] = { 10, 10, 30, 13, 50, 20, 60, 40, 61, 70 };
    static const int t_st9[] = { 10, 10, 50, 10, 10, 20 };
    static const int t_sta[] = { 50, 10, 10, 10, 50, 20 };
    static const int t_stb[] = { 50, 10, 10, 30, 60, 40 };
    static const int t_stc[] = { 10, 10, 40, 10, 40, 40, 10, 30 };
    static const int t_std[] = { 40, 10, 10, 30, 10, 60 };
    static const int t_ste[] = { 60, 40, 10, 10, 60, 12 };
    /* polylines turning in every direction, one Polyline call each */
    static const int t_sf1[] = { 10, 10, 40, 10, 40, 40, 10, 40, 10, 12 };
    static const int t_sf2[] = { 60, 10, 30, 30, 10, 10 };
    static const int t_sf3[] = { 10, 40, 30, 10, 60, 30 };
    static const int t_sf4[] = { 10, 10, 30, 40, 50, 35, 60, 10 };
    static const int t_sf5[] = { 40, 40, 10, 30, 40, 10 };
    static const int t_sf6[] = { 10, 30, 20, 10, 30, 30, 40, 10 };
    static const int t_sf7[] = { 30, 10, 10, 25, 18, 45, 42, 45, 50, 25, 30, 10 };
    static const int t_sf8[] = { 30, 10, 50, 25, 42, 45, 18, 45, 10, 25, 30, 10 };
    /* a turn from y to x (and x to y) after 1-8 pixels */
    static const int t_v1[] = { 10, 10, 10, 11, 60, 11 };
    static const int t_w1[] = { 10, 10, 11, 10, 11, 60 };
    static const int t_u1[] = { 10, 10, 10, 11, 60, 31 };
    static const int t_v2[] = { 10, 10, 10, 12, 60, 12 };
    static const int t_w2[] = { 10, 10, 12, 10, 12, 60 };
    static const int t_u2[] = { 10, 10, 10, 12, 60, 32 };
    static const int t_v3[] = { 10, 10, 10, 13, 60, 13 };
    static const int t_w3[] = { 10, 10, 13, 10, 13, 60 };
    static const int t_u3[] = { 10, 10, 10, 13, 60, 33 };
    static const int t_v4[] = { 10, 10, 10, 14, 60, 14 };
    static const int t_w4[] = { 10, 10, 14, 10, 14, 60 };
    static const int t_u4[] = { 10, 10, 10, 14, 60, 34 };
    static const int t_v5[] = { 10, 10, 10, 15, 60, 15 };
    static const int t_w5[] = { 10, 10, 15, 10, 15, 60 };
    static const int t_u5[] = { 10, 10, 10, 15, 60, 35 };
    static const int t_v6[] = { 10, 10, 10, 16, 60, 16 };
    static const int t_w6[] = { 10, 10, 16, 10, 16, 60 };
    static const int t_u6[] = { 10, 10, 10, 16, 60, 36 };
    static const int t_v7[] = { 10, 10, 10, 17, 60, 17 };
    static const int t_w7[] = { 10, 10, 17, 10, 17, 60 };
    static const int t_u7[] = { 10, 10, 10, 17, 60, 37 };
    static const int t_v8[] = { 10, 10, 10, 18, 60, 18 };
    static const int t_w8[] = { 10, 10, 18, 10, 18, 60 };
    static const int t_u8[] = { 10, 10, 10, 18, 60, 38 };

    struct { const char *n; const int *p; int c; } T[] = {
        { "h", t_h, 2 }, { "hl", t_hl, 2 }, { "v", t_v, 2 }, { "vu", t_vu, 2 },
        { "d", t_d, 2 }, { "du", t_du, 2 }, { "d2", t_d2, 2 }, { "s1", t_s1, 2 },
        { "s2", t_s2, 2 }, { "s3", t_s3, 2 }, { "s4", t_s4, 2 }, { "s5", t_s5, 2 },
        { "rect", t_p, 5 }, { "poly", t_q, 5 }, { "short", t_short, 6 },
        { "x1", t_x1, 2 }, { "x2", t_x2, 2 }, { "x3", t_x3, 2 }, { "x4", t_x4, 2 },
        { "x5", t_x5, 2 }, { "x6", t_x6, 2 }, { "x7", t_x7, 2 }, { "y1", t_y1, 2 },
        { "y2", t_y2, 2 }, { "y3", t_y3, 2 }, { "y4", t_y4, 2 }, { "y5", t_y5, 2 },
        { "y6", t_y6, 2 }, { "y7", t_y7, 2 }, { "star", t_st, 6 }, { "star2", t_st2, 6 },
        { "z1", t_z1, 2 }, { "z2", t_z2, 2 }, { "st3", t_st3, 4 }, { "st4", t_st4, 4 },
        { "st5", t_st5, 3 }, { "st6", t_st6, 4 }, { "st7", t_st7, 4 }, { "st8", t_st8, 5 },
        { "st9", t_st9, 3 }, { "sta", t_sta, 3 }, { "stb", t_stb, 3 }, { "stc", t_stc, 4 },
        { "std", t_std, 3 }, { "ste", t_ste, 3 }, { "sf1", t_sf1, 5 }, { "sf2", t_sf2, 3 }, { "sf3", t_sf3, 3 }, { "sf4", t_sf4, 4 }, { "sf5", t_sf5, 3 }, { "sf6", t_sf6, 4 }, { "sf7", t_sf7, 6 }, { "sf8", t_sf8, 6 }, { "stv1", t_v1, 3 }, { "stw1", t_w1, 3 }, { "stu1", t_u1, 3 }, { "stv2", t_v2, 3 }, { "stw2", t_w2, 3 }, { "stu2", t_u2, 3 }, { "stv3", t_v3, 3 }, { "stw3", t_w3, 3 }, { "stu3", t_u3, 3 }, { "stv4", t_v4, 3 }, { "stw4", t_w4, 3 }, { "stu4", t_u4, 3 }, { "stv5", t_v5, 3 }, { "stw5", t_w5, 3 }, { "stu5", t_u5, 3 }, { "stv6", t_v6, 3 }, { "stw6", t_w6, 3 }, { "stu6", t_u6, 3 }, { "stv7", t_v7, 3 }, { "stw7", t_w7, 3 }, { "stu7", t_u7, 3 }, { "stv8", t_v8, 3 }, { "stw8", t_w8, 3 }, { "stu8", t_u8, 3 }
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
