/*
 * vg1.c - VG1, the display driver's values as programs see them
 * (drivers/vga/README.md). Run as the shell by tests/vgaprobe.sh -p,
 * once with Windows 3.1's VGA.DRV and once with w16's; the two TK.LOGs
 * must be identical.
 *
 * Logged (values only, through documented APIs and the driver's own
 * exports): every GetDeviceCaps index; the system metrics and colours
 * USER takes from the driver's configuration resource; the stock fonts'
 * LOGFONTs and metrics (from the driver's FONTS resource); GetNearestColor
 * over a grid of colours; the planes of each solid colour in a colour
 * bitmap and its colour table through GetDIBits; the 8 x 8 pattern of
 * solid brushes (the dithering) on a colour and on a monochrome bitmap;
 * Inquire's CURSORINFO and the bytes it pops; the VGA's DAC and attribute
 * palette as the driver left them (DAC 0-63: the attribute registers
 * point into all of them).
 */
#include <windows.h>

#define TK_REF "vg1ref.h"
#include "tklog.h"

extern DWORD __cdecl __far md_call(FARPROC f, WORD FAR *args, WORD FAR *out);
extern int FAR PASCAL U_INITAPP(WORD hinst);
extern WORD __cdecl init_regs[7];

static char line[600];
static int lp;

static void put(const char *s)
{
    while (*s && lp < (int)sizeof line - 2)
        line[lp++] = *s++;
}

static void hexn(DWORD v, int n)
{
    char b[9];
    int i;
    for (i = n - 1; i >= 0; i--) {
        b[i] = "0123456789ABCDEF"[(WORD)v & 15];
        v >>= 4;
    }
    b[n] = 0;
    put(b);
}

static void dec(int v)
{
    char b[8];
    int i = 7;
    unsigned u = v < 0 ? -v : v;
    b[7] = 0;
    do {
        b[--i] = (char)('0' + u % 10);
        u /= 10;
    } while (u);
    if (v < 0)
        b[--i] = '-';
    put(b + i);
}

static void flush(void)
{
    line[lp++] = '\r';
    line[lp++] = '\n';
    line[lp] = 0;
    out(line);
    lp = 0;
}

void vga_out(WORD port, BYTE v);
#pragma aux vga_out = "out dx, al" parm [dx] [al] modify exact [];
BYTE vga_in(WORD port);
#pragma aux vga_in = "in al, dx" parm [dx] value [al] modify exact [al];

/* the grid of colours (VG1_STEP: 16 by default, 0 ... 240 and 255) */
#ifndef VG1_STEP
#define VG1_STEP 16
#endif
#define NV (256 / VG1_STEP + 1)
static int vals[NV];

static void one_color(HDC dc, HDC mem, HBITMAP cbm, HBITMAP mbm, COLORREF c)
{
    static BYTE bits[64];
    HBRUSH br, ob;
    int k;
    put("c ");
    hexn(c, 6);
    put(" near=");
    hexn(GetNearestColor(dc, c), 6);
    br = CreateSolidBrush(c);
    SelectObject(mem, cbm);
    ob = SelectObject(mem, br);
    PatBlt(mem, 0, 0, 8, 8, PATCOPY);
    SelectObject(mem, ob);
    GetBitmapBits(cbm, 64, bits);
    put(" col=");
    for (k = 0; k < 64; k += 2)
        hexn(bits[k], 2);
    SelectObject(mem, mbm);
    ob = SelectObject(mem, br);
    PatBlt(mem, 0, 0, 8, 8, PATCOPY);
    SelectObject(mem, ob);
    GetBitmapBits(mbm, 16, bits);
    put(" mono=");
    for (k = 0; k < 16; k += 2)
        hexn(bits[k], 2);
    DeleteObject(br);
    flush();
}

/* the next colour of the input file: hex digits up to the line's end */
static int read_color(HFILE f, COLORREF *c)
{
    char ch;
    int n = 0;
    DWORD v = 0;
    for (;;) {
        if (_lread(f, &ch, 1) != 1)
            return n > 0 ? (*c = v, 1) : 0;
        if (ch == '\n') {
            if (n) {
                *c = v;
                return 1;
            }
            continue;
        }
        if (ch >= '0' && ch <= '9')
            v = v * 16 + (ch - '0'), n++;
        else if (ch >= 'A' && ch <= 'F')
            v = v * 16 + (ch - 'A' + 10), n++;
        else if (ch >= 'a' && ch <= 'f')
            v = v * 16 + (ch - 'a' + 10), n++;
    }
}

int __cdecl tmain(void)
{
    HDC dc, mem;
    HBITMAP cbm, mbm, old;
    HBRUSH br, ob;
    int i, r, g, b;
    BYTE bits[64];
    static struct { BITMAPINFOHEADER h; RGBQUAD q[256]; } bi;
    static BYTE dib[64];
    LOGFONT lf;
    TEXTMETRIC tm;
    HMODULE hd;
    FARPROC f;
    static WORD args[16], res[2];
    static short ci[8];
    DWORD rv;
    HFILE infile;
    COLORREF cin;

    U_INITAPP(init_regs[5]);
    for (i = 0; i < NV - 1; i++)
        vals[i] = i * VG1_STEP;
    vals[NV - 1] = 255;
    dc = GetDC(0);

    put("caps");
    for (i = 0; i <= 0x6C; i += 2) {
        put(" ");
        dec(GetDeviceCaps(dc, i));
    }
    flush();
    put("metrics");
    for (i = 0; i < 38; i++) {
        put(" ");
        dec(GetSystemMetrics(i));
    }
    flush();
    put("syscolors");
    for (i = 0; i <= 20; i++) {
        put(" ");
        hexn(GetSysColor(i), 6);
    }
    flush();

    /* the stock fonts: 10 OEM_FIXED, 11 ANSI_FIXED, 12 ANSI_VAR, 13
       SYSTEM, 14 DEVICE_DEFAULT, 16 SYSTEM_FIXED */
    for (i = 10; i <= 16; i++) {
        HFONT h = GetStockObject(i), of;
        int n;
        if (i == 15)
            continue;
        n = GetObject(h, sizeof lf, &lf);
        put("font ");
        dec(i);
        put(" n=");
        dec(n);
        put(" lf=");
        dec(lf.lfHeight); put(","); dec(lf.lfWidth); put(","); dec(lf.lfEscapement); put(",");
        dec(lf.lfOrientation); put(","); dec(lf.lfWeight); put(","); dec(lf.lfItalic); put(",");
        dec(lf.lfUnderline); put(","); dec(lf.lfStrikeOut); put(","); dec(lf.lfCharSet); put(",");
        dec(lf.lfOutPrecision); put(","); dec(lf.lfClipPrecision); put(",");
        dec(lf.lfQuality); put(","); dec(lf.lfPitchAndFamily);
        put(" face=");
        put(lf.lfFaceName);
        of = SelectObject(dc, h);
        GetTextMetrics(dc, &tm);
        put(" tm=");
        dec(tm.tmHeight); put(","); dec(tm.tmAscent); put(","); dec(tm.tmAveCharWidth); put(",");
        dec(tm.tmMaxCharWidth); put(","); dec(tm.tmWeight); put(","); dec(tm.tmCharSet); put(",");
        dec(tm.tmPitchAndFamily);
        GetTextFace(dc, sizeof lf.lfFaceName, lf.lfFaceName);
        put(" face=");
        put(lf.lfFaceName);
        SelectObject(dc, of);
        flush();
    }

    mem = CreateCompatibleDC(dc);
    cbm = CreateCompatibleBitmap(dc, 8, 8);
    mbm = CreateBitmap(8, 8, 1, 1, NULL);

    /* the 16 colours' planes and their DIB colour table */
    old = SelectObject(mem, cbm);
    for (i = 0; i < 16; i++) {
        static const COLORREF c16[16] = {
            0x000000L, 0x000080L, 0x008000L, 0x008080L, 0x800000L, 0x800080L, 0x808000L,
            0x808080L, 0xC0C0C0L, 0x0000FFL, 0x00FF00L, 0x00FFFFL, 0xFF0000L, 0xFF00FFL,
            0xFFFF00L, 0xFFFFFFL
        };
        br = CreateSolidBrush(c16[i]);
        ob = SelectObject(mem, br);
        PatBlt(mem, 0, 0, 8, 8, PATCOPY);
        SelectObject(mem, ob);
        DeleteObject(br);
        GetBitmapBits(cbm, 64, bits);
        put("color ");
        hexn(c16[i], 6);
        put(" planes");
        for (r = 0; r < 4; r++) {
            put(" ");
            hexn(bits[2 * r], 2);       /* 8 pixels: 2 bytes a plane's line */
        }
        {
            BYTE FAR *z = (BYTE FAR *)&bi;
            for (r = 0; r < (int)sizeof bi; r++)
                z[r] = 0;
        }
        bi.h.biSize = sizeof(BITMAPINFOHEADER);
        bi.h.biWidth = 8;
        bi.h.biHeight = 8;
        bi.h.biPlanes = 1;
        bi.h.biBitCount = 4;
        SelectObject(mem, old);
        r = GetDIBits(mem, cbm, 0, 8, dib, (BITMAPINFO FAR *)&bi, DIB_RGB_COLORS);
        SelectObject(mem, cbm);
        put(" dib=");
        dec(r);
        put(" pix=");
        hexn(dib[0], 2);
        put(" used=");
        dec((int)bi.h.biClrUsed);
        put(" img=");
        dec((int)bi.h.biSizeImage);
        if (i == 0) {
            put(" table");
            for (r = 0; r < 16; r++) {
                put(" ");
                hexn(*(DWORD FAR *)&bi.q[r], 8);
            }
        }
        flush();
    }

    /* each colour of the grid (or of C:\\VG1.IN, one COLORREF in hex a
       line, when it exists): GetNearestColor, and a solid brush's pattern
       (the dithering) on a colour and on a monochrome bitmap */
    infile = _lopen("C:\\VG1.IN", 0);
    if (infile != HFILE_ERROR) {
        while (read_color(infile, &cin))
            one_color(dc, mem, cbm, mbm, cin);
        _lclose(infile);
    } else
        for (r = 0; r < NV; r++)
            for (g = 0; g < NV; g++)
                for (b = 0; b < NV; b++)
                    one_color(dc, mem, cbm, mbm, RGB(vals[r], vals[g], vals[b]));
    SelectObject(mem, old);

    /* Inquire (DISPLAY.101): the CURSORINFO, AX, the bytes popped */
    hd = GetModuleHandle("DISPLAY");
    f = GetProcAddress(hd, MAKEINTRESOURCE(101));
    for (i = 0; i < 8; i++)
        ci[i] = 0x5555;
    for (i = 0; i < 16; i++)
        args[i] = 0;
    args[14] = SELECTOROF((void FAR *)ci);
    args[15] = OFFSETOF((void FAR *)ci);
    rv = md_call(f, args, res);
    put("inquire ax=");
    hexn(LOWORD(rv), 4);
    put(" pop=");
    dec(res[0]);
    put(" ci");
    for (i = 0; i < 4; i++) {
        put(" ");
        hexn(ci[i], 4);
    }
    flush();

    /* the VGA's palette: attribute controller registers 0-15 and 10h-14h,
       DAC 0-15 */
    put("attr");
    for (i = 0; i < 0x15; i++) {
        vga_in(0x3DA);
        vga_out(0x3C0, (BYTE)(i | 0x20));
        put(" ");
        hexn(vga_in(0x3C1), 2);
    }
    vga_in(0x3DA);
    vga_out(0x3C0, 0x20);
    flush();
    put("dac");
    vga_out(0x3C7, 0);
    for (i = 0; i < 64; i++) {
        put(" ");
        hexn(vga_in(0x3C9), 2);
        hexn(vga_in(0x3C9), 2);
        hexn(vga_in(0x3C9), 2);
    }
    flush();
    put("end");
    flush();

    DeleteDC(mem);
    DeleteObject(cbm);
    DeleteObject(mbm);
    ReleaseDC(0, dc);
    return 0;
}
