/*
 * vg3.c - VG3: GetNearestColor for every one of the 2^24 colours on the
 * screen DC, the display driver's ColorInfo (drivers/vga/README.md). Run
 * as the shell by tests/vgaprobe.sh -p with Windows 3.1's VGA.DRV and with
 * w16's; writes C:\NEAR.BIN, a 4-bit index per colour (two to a byte, the
 * first in the high nibble), red outermost, then green, then blue: the
 * index of the result in the 16 colours of a 4-bit DIB's colour table
 * (black, maroon, ..., white; 15 also for anything else). Then the same
 * on a memory DC holding a monochrome bitmap: C:\NEARM.BIN, a bit per
 * colour (1: white), the first in bit 7. Values only.
 */
#include <windows.h>

extern int FAR PASCAL U_INITAPP(WORD hinst);
extern WORD __cdecl init_regs[7];

static const COLORREF c16[16] = {
    0x000000L, 0x000080L, 0x008000L, 0x008080L, 0x800000L, 0x800080L, 0x808000L,
    0x808080L, 0xC0C0C0L, 0x0000FFL, 0x00FF00L, 0x00FFFFL, 0xFF0000L, 0xFF00FFL,
    0xFFFF00L, 0xFFFFFFL
};
static BYTE buf[32768];

static int index_of(COLORREF c)
{
    int i;
    for (i = 0; i < 15; i++)
        if (c16[i] == c)
            return i;
    return 15;
}

int __cdecl tmain(void)
{
    HDC dc;
    HFILE f;
    WORD r, g, b, n = 0;
    BYTE v = 0;
    U_INITAPP(init_regs[5]);
    dc = GetDC(0);
    f = _lcreat("C:\\NEAR.BIN", 0);
    if (f == HFILE_ERROR)
        return 1;
    for (r = 0; r < 256; r++)
        for (g = 0; g < 256; g++)
            for (b = 0; b < 256; b++) {
                int i = index_of(GetNearestColor(dc, RGB(r, g, b)));
                if (b & 1) {
                    buf[n++] = (BYTE)(v | i);
                    if (n == sizeof buf) {
                        _lwrite(f, buf, n);
                        n = 0;
                    }
                } else
                    v = (BYTE)(i << 4);
            }
    _lclose(f);
    /* a monochrome bitmap's DC */
    {
        HDC mem = CreateCompatibleDC(dc);
        HBITMAP bm = CreateBitmap(8, 8, 1, 1, NULL), old = SelectObject(mem, bm);
        f = _lcreat("C:\\NEARM.BIN", 0);
        n = 0;
        v = 0;
        for (r = 0; r < 256; r++)
            for (g = 0; g < 256; g++)
                for (b = 0; b < 256; b++) {
                    if (GetNearestColor(mem, RGB(r, g, b)) == 0xFFFFFFL)
                        v |= (BYTE)(0x80 >> (b & 7));
                    if ((b & 7) == 7) {
                        buf[n++] = v;
                        v = 0;
                        if (n == sizeof buf) {
                            _lwrite(f, buf, n);
                            n = 0;
                        }
                    }
                }
        _lclose(f);
        SelectObject(mem, old);
        DeleteObject(bm);
        DeleteDC(mem);
    }
    /* the end of the run: C:\\NEAR.END appears (tests/vgaprobe.sh -W) */
    _lclose(_lcreat("C:\\NEAR.END", 0));
    ReleaseDC(0, dc);
    return 0;
}
