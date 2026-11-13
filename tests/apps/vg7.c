/*
 * vg7.c - VG7: bitmaps as the display driver makes them (drivers/vga/
 * README.md): every one of the 256 ternary raster operations with a
 * source, a pattern brush and a destination on colour and on monochrome
 * bitmaps; monochrome <-> colour BitBlts with several text and background
 * colours; DIBs of 1, 4, 8 and 24 bits set into and read back from colour
 * and monochrome bitmaps; the hatched brushes. Logged: the bitmaps' bits
 * (GetBitmapBits) and the DIBs (GetDIBits), as hex. Run as the shell by
 * tests/vgaprobe.sh -p with Windows 3.1's VGA.DRV and with w16's; the
 * TK.LOGs must be identical. Values only.
 */
#include <windows.h>

#define TK_REF "vg1ref.h"
#include "tklog.h"

extern int FAR PASCAL U_INITAPP(WORD hinst);
extern WORD __cdecl init_regs[7];

/* the low words of the 256 ROP codes (the Windows 3.1 SDK's and
   DDK's raster-operation table: interface constants) */
static const WORD rop_low[256] = {
    0x0042, 0x0289, 0x0C89, 0x00AA, 0x0C88, 0x00A9, 0x0865, 0x02C5, 0x0F08, 0x0245, 0x0329,
    0x0B2A, 0x0324, 0x0B25, 0x08A5, 0x0001, 0x0C85, 0x00A6, 0x0868, 0x02C8, 0x0869, 0x02C9,
    0x5CCA, 0x1D54, 0x0D59, 0x1CC8, 0x06C5, 0x0768, 0x06CA, 0x0766, 0x01A5, 0x0385, 0x0F09,
    0x0248, 0x0326, 0x0B24, 0x0D55, 0x1CC5, 0x06C8, 0x1868, 0x0369, 0x16CA, 0x0CC9, 0x1D58,
    0x0784, 0x060A, 0x064A, 0x0E2A, 0x032A, 0x0B28, 0x0688, 0x0008, 0x06C4, 0x1864, 0x01A8,
    0x0388, 0x078A, 0x0604, 0x0644, 0x0E24, 0x004A, 0x18A4, 0x1B24, 0x00EA, 0x0F0A, 0x0249,
    0x0D5D, 0x1CC4, 0x0328, 0x0B29, 0x06C6, 0x076A, 0x0368, 0x16C5, 0x0789, 0x0605, 0x0CC8,
    0x1954, 0x0645, 0x0E25, 0x0325, 0x0B26, 0x06C9, 0x0764, 0x08A9, 0x0009, 0x01A9, 0x0389,
    0x0785, 0x0609, 0x0049, 0x18A9, 0x0649, 0x0E29, 0x1B29, 0x00E9, 0x0365, 0x16C6, 0x0786,
    0x0608, 0x0788, 0x0606, 0x0046, 0x18A8, 0x58A6, 0x0145, 0x01E9, 0x178A, 0x01E8, 0x1785,
    0x1E28, 0x0C65, 0x0CC5, 0x1D5C, 0x0648, 0x0E28, 0x0646, 0x0E26, 0x1B28, 0x00E6, 0x01E5,
    0x1786, 0x1E29, 0x0C68, 0x1E24, 0x0C69, 0x0955, 0x03C9, 0x03E9, 0x0975, 0x0C49, 0x1E04,
    0x0C48, 0x1E05, 0x17A6, 0x01C5, 0x00C6, 0x1B08, 0x0E06, 0x0666, 0x0E08, 0x0668, 0x1D7C,
    0x0CE5, 0x0C45, 0x1E08, 0x17A9, 0x01C4, 0x17AA, 0x01C9, 0x0169, 0x588A, 0x1888, 0x0066,
    0x0709, 0x07A8, 0x0704, 0x07A6, 0x16E6, 0x0345, 0x00C9, 0x1B05, 0x0E09, 0x0669, 0x1885,
    0x0065, 0x0706, 0x07A5, 0x03A9, 0x0189, 0x0029, 0x0889, 0x0744, 0x06E9, 0x0B06, 0x0229,
    0x0E05, 0x0665, 0x1974, 0x0CE8, 0x070A, 0x07A9, 0x16E9, 0x0348, 0x074A, 0x06E6, 0x0B09,
    0x0226, 0x1CE4, 0x0D7D, 0x0269, 0x08C9, 0x00CA, 0x1B04, 0x1884, 0x006A, 0x0E04, 0x0664,
    0x0708, 0x07AA, 0x03A8, 0x0184, 0x0749, 0x06E4, 0x0020, 0x0888, 0x0B08, 0x0224, 0x0E0A,
    0x066A, 0x0705, 0x07A4, 0x1D78, 0x0CE9, 0x16EA, 0x0349, 0x0745, 0x06E8, 0x1CE9, 0x0D75,
    0x0B04, 0x0228, 0x0268, 0x08C8, 0x03A5, 0x0185, 0x0746, 0x06EA, 0x0748, 0x06E5, 0x1CE8,
    0x0D79, 0x1D74, 0x5CE6, 0x02E9, 0x0849, 0x02E8, 0x0848, 0x0086, 0x0A08, 0x0021, 0x0885,
    0x0B05, 0x022A, 0x0B0A, 0x0225, 0x0265, 0x08C5, 0x02E5, 0x0845, 0x0089, 0x0A09, 0x008A,
    0x0A0A, 0x02A9, 0x0062
};

static char line[1200];
static int lp;
static BYTE bits[1024];

static void put(const char *s)
{
    while (*s && lp < (int)sizeof line - 3)
        line[lp++] = *s++;
}

static void hexb(BYTE v)
{
    char b[3];
    b[0] = "0123456789ABCDEF"[v >> 4];
    b[1] = "0123456789ABCDEF"[v & 15];
    b[2] = 0;
    put(b);
}

static void flush(void)
{
    line[lp++] = '\r';
    line[lp++] = '\n';
    line[lp] = 0;
    out(line);
    lp = 0;
}

static void dumpbm(const char *name, HBITMAP bm, int n)
{
    int i;
    GetBitmapBits(bm, n, bits);
    put(name);
    put(" ");
    for (i = 0; i < n; i++)
        hexb(bits[i]);
    flush();
}

/* a 16 x 8 colour bitmap with all 16 colours in stripes, and the same
   shape in monochrome */
static void fill_src(HDC dc, int colour)
{
    int i;
    for (i = 0; i < 16; i++) {
        static const COLORREF c16[16] = {
            0x000000L, 0x000080L, 0x008000L, 0x008080L, 0x800000L, 0x800080L, 0x808000L,
            0x808080L, 0xC0C0C0L, 0x0000FFL, 0x00FF00L, 0x00FFFFL, 0xFF0000L, 0xFF00FFL,
            0xFFFF00L, 0xFFFFFFL
        };
        HBRUSH b = CreateSolidBrush(colour ? c16[i] : (i & 1 ? 0xFFFFFFL : 0));
        HBRUSH ob = SelectObject(dc, b);
        PatBlt(dc, i, 0, 1, 8, PATCOPY);
        PatBlt(dc, 0, i & 7, i, 1, PATCOPY);
        SelectObject(dc, ob);
        DeleteObject(b);
    }
}

int __cdecl tmain(void)
{
    HDC dc, s, d;
    HBITMAP cs, cd, ms, md, os, od, pat;
    HBRUSH pb, ob;
    int rop, k, bpp;
    static const WORD patbits[8] = { 0x55, 0x33, 0x0F, 0xF0, 0xCC, 0xAA, 0x81, 0x7E };
    static struct { BITMAPINFOHEADER h; RGBQUAD q[256]; } bi;
    U_INITAPP(init_regs[5]);
    dc = GetDC(0);
    s = CreateCompatibleDC(dc);
    d = CreateCompatibleDC(dc);
    cs = CreateCompatibleBitmap(dc, 16, 8);
    cd = CreateCompatibleBitmap(dc, 16, 8);
    ms = CreateBitmap(16, 8, 1, 1, NULL);
    md = CreateBitmap(16, 8, 1, 1, NULL);
    pat = CreateBitmap(8, 8, 1, 1, patbits);
    pb = CreatePatternBrush(pat);
    os = SelectObject(s, cs);
    od = SelectObject(d, cd);

    /* every ROP3 on colour: source stripes, destination the stripes
       turned, pattern from a mono bitmap in the DC's colours */
    SetTextColor(d, 0x0000FFL);
    SetBkColor(d, 0x00FF00L);
    for (rop = 0; rop < 256; rop++) {
        DWORD r = ((DWORD)rop << 16) | rop_low[rop];
        fill_src(s, 1);
        fill_src(d, 1);
        BitBlt(d, 0, 0, 16, 8, d, 3, 0, SRCCOPY);
        ob = SelectObject(d, pb);
        BitBlt(d, 0, 0, 16, 8, s, 0, 0, r);
        SelectObject(d, ob);
        put("rop ");
        hexb((BYTE)rop);
        put(" ");
        flush();
        dumpbm("  c", cd, 64);
    }
    /* every ROP3 on monochrome */
    SelectObject(s, ms);
    SelectObject(d, md);
    for (rop = 0; rop < 256; rop++) {
        DWORD r = ((DWORD)rop << 16) | rop_low[rop];
        fill_src(s, 0);
        fill_src(d, 0);
        BitBlt(d, 0, 0, 16, 8, d, 5, 0, SRCCOPY);
        ob = SelectObject(d, pb);
        BitBlt(d, 0, 0, 16, 8, s, 0, 0, r);
        SelectObject(d, ob);
        put("mrop ");
        hexb((BYTE)rop);
        flush();
        dumpbm("  m", md, 16);
    }
    /* mono -> colour and colour -> mono with several colours */
    for (k = 0; k < 6; k++) {
        static const COLORREF fg[6] = { 0, 0xFFFFFFL, 0x0000FFL, 0x808080L, 0xC0C0C0L, 0x123456L };
        static const COLORREF bk[6] = { 0xFFFFFFL, 0, 0x00FF00L, 0xC0C0C0L, 0x808080L, 0x654321L };
        SelectObject(s, ms);
        SelectObject(d, cd);
        fill_src(s, 0);
        SetTextColor(d, fg[k]);
        SetBkColor(d, bk[k]);
        BitBlt(d, 0, 0, 16, 8, s, 0, 0, SRCCOPY);
        put("m2c ");
        hexb((BYTE)k);
        flush();
        dumpbm("  c", cd, 64);
        SelectObject(s, cs);
        SelectObject(d, md);
        fill_src(s, 1);
        SetTextColor(d, fg[k]);
        SetBkColor(d, bk[k]);
        SetTextColor(s, bk[k]);
        SetBkColor(s, fg[k]);
        BitBlt(d, 0, 0, 16, 8, s, 0, 0, SRCCOPY);
        put("c2m ");
        hexb((BYTE)k);
        flush();
        dumpbm("  m", md, 16);
    }
    /* DIBs: 1, 4, 8, 24 bits into colour and mono, and back */
    SelectObject(s, os);
    SelectObject(d, od);
    for (k = 0; k < 4; k++) {
        static const int bpps[4] = { 1, 4, 8, 24 };
        int i, n, stride, kk;
        static BYTE dib[16 * 8 * 3];
        bpp = bpps[k];
        n = bpp == 24 ? 0 : 1 << bpp;
        stride = ((16 * bpp + 31) / 32) * 4;
        for (i = 0; i < (int)sizeof bi; i++)
            ((BYTE *)&bi)[i] = 0;
        bi.h.biSize = sizeof(BITMAPINFOHEADER);
        bi.h.biWidth = 16;
        bi.h.biHeight = 8;
        bi.h.biPlanes = 1;
        bi.h.biBitCount = bpp;
        for (i = 0; i < n; i++) {
            bi.q[i].rgbRed = (BYTE)(i * 37);
            bi.q[i].rgbGreen = (BYTE)(i * 91);
            bi.q[i].rgbBlue = (BYTE)(255 - i * 13);
        }
        for (i = 0; i < stride * 8; i++)
            dib[i] = (BYTE)(i * 29 + 7);
        SetDIBits(dc, cd, 0, 8, dib, (BITMAPINFO FAR *)&bi, DIB_RGB_COLORS);
        SetDIBits(dc, md, 0, 8, dib, (BITMAPINFO FAR *)&bi, DIB_RGB_COLORS);
        put("dib ");
        hexb((BYTE)bpp);
        flush();
        dumpbm("  c", cd, 64);
        dumpbm("  m", md, 16);
        for (kk = 0; kk < 2; kk++) {
            for (i = 0; i < (int)sizeof dib; i++)
                dib[i] = 0;
            for (i = 0; i < 256 * 4; i++)
                ((BYTE *)bi.q)[i] = 0xEE;
            bi.h.biClrUsed = 0;
            bi.h.biSizeImage = 0;
            GetDIBits(dc, kk ? md : cd, 0, 8, dib, (BITMAPINFO FAR *)&bi, DIB_RGB_COLORS);
            put(kk ? "  getm " : "  getc ");
            for (i = 0; i < stride * 8; i++)
                hexb(dib[i]);
            put(" t ");
            for (i = 0; i < (n < 20 ? n : 20) * 4; i++)
                hexb(((BYTE *)bi.q)[i]);
            flush();
        }
    }
    /* hatched brushes, opaque and transparent, on colour */
    SelectObject(d, cd);
    for (k = 0; k < 6; k++) {
        HBRUSH h = CreateHatchBrush(k, 0x0000FFL);
        int m;
        for (m = 1; m <= 2; m++) {
            fill_src(d, 1);
            SetBkColor(d, 0xFFFF00L);
            SetBkMode(d, m);
            ob = SelectObject(d, h);
            PatBlt(d, 0, 0, 16, 8, PATCOPY);
            SelectObject(d, ob);
            put("hatch ");
            hexb((BYTE)k);
            hexb((BYTE)m);
            flush();
            dumpbm("  c", cd, 64);
        }
        DeleteObject(h);
    }
    out("end\r\n");
    SelectObject(s, os);
    SelectObject(d, od);
    DeleteDC(s);
    DeleteDC(d);
    DeleteObject(cs);
    DeleteObject(cd);
    DeleteObject(ms);
    DeleteObject(md);
    DeleteObject(pb);
    DeleteObject(pat);
    ReleaseDC(0, dc);
    return 0;
}
