/*
 * vg6.c - VG6: text as the display driver draws and measures it
 * (drivers/vga/README.md): raster fonts with the attributes GDI leaves to
 * the driver or simulates (bold, italic, underline, strike-out and their
 * mixtures), opaque and transparent, clipped, with ExtTextOut's widths and
 * opaque rectangle, justification (SetTextJustification) and character
 * extra (SetTextCharacterExtra). Logged: GetTextExtent, GetTextMetrics'
 * sizes and overhang, GetCharWidth, and the pixels of each string's box
 * (# text colour, - background colour, . untouched gray). Run as the
 * shell by tests/vgaprobe.sh -p with Windows 3.1's VGA.DRV and with
 * w16's; the TK.LOGs must be identical. Values only.
 */
#include <windows.h>

#define TK_REF "vg1ref.h"
#include "tklog.h"

extern int FAR PASCAL U_INITAPP(WORD hinst);
extern WORD __cdecl init_regs[7];

static char buf[700];

static void num(const char *name, int v)
{
    char s[8];
    int i = 7;
    unsigned u = v < 0 ? -v : v;
    s[7] = 0;
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

static void box(HDC dc, int x0, int y0, int x1, int y1)
{
    int x, y, n;
    for (y = y0; y < y1; y++) {
        n = 0;
        for (x = x0; x < x1 && n < (int)sizeof buf - 3; x++) {
            COLORREF c = GetPixel(dc, x, y);
            buf[n++] = c == 0 ? '#' : c == 0xFFFFFFL ? '-' : c == 0x808080L ? '.' : '?';
        }
        buf[n++] = '\r';
        buf[n++] = '\n';
        buf[n] = 0;
        out(buf);
    }
}

static void clear(HDC dc)
{
    RECT r;
    HBRUSH g = CreateSolidBrush(0x808080L);
    r.left = 0;
    r.top = 0;
    r.right = 640;
    r.bottom = 120;
    FillRect(dc, &r, g);
    DeleteObject(g);
}

static const char text[] = "Switch To AWgjy|";

static void one(HDC dc, const char *face, int h, int weight, int ital, int ul, int so, int mode)
{
    HFONT f, of;
    LOGFONT lf;
    TEXTMETRIC tm;
    DWORD ext;
    int i, w[8];
    static int dx[16] = { 9, 8, 7, 6, 5, 4, 3, 10, 11, 12, 6, 6, 6, 6, 6, 6 };
    RECT r;
    for (i = 0; i < (int)sizeof lf; i++)
        ((char *)&lf)[i] = 0;
    lf.lfHeight = h;
    lf.lfWeight = weight;
    lf.lfItalic = (BYTE)ital;
    lf.lfUnderline = (BYTE)ul;
    lf.lfStrikeOut = (BYTE)so;
    lstrcpy(lf.lfFaceName, face);
    f = CreateFontIndirect(&lf);
    of = SelectObject(dc, f);
    clear(dc);
    out("text ");
    out(face);
    num("h", h);
    num("weight", weight);
    num("ital", ital);
    num("ul", ul);
    num("so", so);
    num("mode", mode);
    ext = GetTextExtent(dc, text, sizeof text - 1);
    num("ext", LOWORD(ext));
    num("exth", HIWORD(ext));
    GetTextMetrics(dc, &tm);
    num("tmh", tm.tmHeight);
    num("tmasc", tm.tmAscent);
    num("tmave", tm.tmAveCharWidth);
    num("tmmax", tm.tmMaxCharWidth);
    num("tmwt", tm.tmWeight);
    num("tmovh", tm.tmOverhang);
    GetCharWidth(dc, 'A', 'H', w);
    for (i = 0; i < 8; i++)
        num("cw", w[i]);
    out("\r\n");
    SetBkMode(dc, mode == 2 ? OPAQUE : TRANSPARENT);
    TextOut(dc, 10, 10, text, sizeof text - 1);
    /* ExtTextOut: clipped and opaque rectangle, own widths */
    r.left = 12;
    r.top = 42;
    r.right = 90;
    r.bottom = 52;
    ExtTextOut(dc, 10, 40, ETO_CLIPPED | ETO_OPAQUE, &r, text, sizeof text - 1, NULL);
    ExtTextOut(dc, 10, 60, 0, NULL, text, sizeof text - 1, dx);
    /* justification and character extra */
    SetTextJustification(dc, 13, 2);
    TextOut(dc, 10, 80, text, sizeof text - 1);
    SetTextJustification(dc, 0, 0);
    SetTextCharacterExtra(dc, 2);
    TextOut(dc, 200, 80, text, sizeof text - 1);
    SetTextCharacterExtra(dc, 0);
    box(dc, 8, 8, 260, 100);
    SelectObject(dc, of);
    DeleteObject(f);
}

int __cdecl tmain(void)
{
    HDC dc;
    int mode, k;
    static const struct { const char *face; int h, weight, ital, ul, so; } T[] = {
        { "MS Sans Serif", -11, 400, 0, 0, 0 }, { "MS Sans Serif", -11, 700, 0, 0, 0 },
        { "MS Sans Serif", -11, 400, 1, 0, 0 }, { "MS Sans Serif", -11, 400, 0, 1, 0 },
        { "MS Sans Serif", -11, 400, 0, 0, 1 }, { "MS Sans Serif", -11, 700, 1, 1, 1 },
        { "MS Sans Serif", -13, 700, 0, 0, 0 }, { "System", 16, 700, 0, 0, 0 },
        { "System", 16, 900, 0, 0, 0 }, { "System", 16, 400, 1, 1, 0 },
        { "Courier", -13, 700, 0, 0, 0 }, { "Courier", -13, 400, 1, 0, 1 },
        { "MS Serif", -11, 700, 1, 0, 0 }, { "Terminal", 12, 700, 0, 1, 0 },
        { "Small Fonts", -9, 700, 1, 0, 0 }, { "Fixedsys", 15, 700, 0, 0, 0 }
    };
    U_INITAPP(init_regs[5]);
    dc = GetDC(0);
    SetBkColor(dc, 0xFFFFFFL);
    SetTextColor(dc, 0);
    for (mode = 1; mode <= 2; mode++)
        for (k = 0; k < (int)(sizeof T / sizeof T[0]); k++)
            one(dc, T[k].face, T[k].h, T[k].weight, T[k].ital, T[k].ul, T[k].so, mode);
    out("end\r\n");
    ReleaseDC(0, dc);
    return 0;
}
