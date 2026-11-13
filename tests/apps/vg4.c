/*
 * vg4.c - VG4: the solid-brush patterns (dithering) of the display driver
 * for every colour of a range of reds, all greens and blues
 * (drivers/vga/README.md). Run as the shell by tests/vgacolor.sh with
 * Windows 3.1's VGA.DRV and with w16's; reads the range from C:\VG4.IN
 * ("first last", decimal reds) and writes C:\VG4.BIN: for each colour, red
 * outermost, a 32-bit checksum (CRC-32) of the brush's 8 x 8 pattern on a
 * colour bitmap (4 planes x 8 rows) followed by its pattern on a
 * monochrome bitmap (8 rows). Then C:\VG4.END. Values only: the two runs'
 * files must be identical.
 */
#include <windows.h>

extern int FAR PASCAL U_INITAPP(WORD hinst);
extern WORD __cdecl init_regs[7];

static BYTE out[16384];
static DWORD crctab[256];

static void crc_init(void)
{
    int i, k;
    for (i = 0; i < 256; i++) {
        DWORD c = i;
        for (k = 0; k < 8; k++)
            c = c & 1 ? 0xEDB88320L ^ (c >> 1) : c >> 1;
        crctab[i] = c;
    }
}

static DWORD crc(const BYTE FAR *p, int n, DWORD c)
{
    while (n--)
        c = crctab[(BYTE)(c ^ *p++)] ^ (c >> 8);
    return c;
}

static int readnum(HFILE f, int *v)
{
    char ch;
    int n = 0;
    *v = 0;
    while (_lread(f, &ch, 1) == 1) {
        if (ch >= '0' && ch <= '9') {
            *v = *v * 10 + ch - '0';
            n++;
        } else if (n)
            return 1;
    }
    return n > 0;
}

int __cdecl tmain(void)
{
    HDC dc, mem;
    HBITMAP cbm, mbm, old;
    HFILE f;
    int r0, r1, r, g, b, k;
    WORD n = 0;
    static BYTE bits[64], pat[40];
    U_INITAPP(init_regs[5]);
    crc_init();
    f = _lopen("C:\\VG4.IN", 0);
    if (f == HFILE_ERROR || !readnum(f, &r0) || !readnum(f, &r1))
        return 1;
    _lclose(f);
    dc = GetDC(0);
    mem = CreateCompatibleDC(dc);
    cbm = CreateCompatibleBitmap(dc, 8, 8);
    mbm = CreateBitmap(8, 8, 1, 1, NULL);
    old = SelectObject(mem, cbm);
    f = _lcreat("C:\\VG4.BIN", 0);
    for (r = r0; r <= r1; r++)
        for (g = 0; g < 256; g++)
            for (b = 0; b < 256; b++) {
                HBRUSH br = CreateSolidBrush(RGB(r, g, b)), ob;
                DWORD c;
                SelectObject(mem, cbm);
                ob = SelectObject(mem, br);
                PatBlt(mem, 0, 0, 8, 8, PATCOPY);
                SelectObject(mem, ob);
                GetBitmapBits(cbm, 64, bits);
                for (k = 0; k < 32; k++)
                    pat[k] = bits[2 * k];
                SelectObject(mem, mbm);
                ob = SelectObject(mem, br);
                PatBlt(mem, 0, 0, 8, 8, PATCOPY);
                SelectObject(mem, ob);
                GetBitmapBits(mbm, 16, bits);
                for (k = 0; k < 8; k++)
                    pat[32 + k] = bits[2 * k];
                DeleteObject(br);
                c = crc(pat, 40, 0xFFFFFFFFL) ^ 0xFFFFFFFFL;
                out[n++] = (BYTE)c;
                out[n++] = (BYTE)(c >> 8);
                out[n++] = (BYTE)(c >> 16);
                out[n++] = (BYTE)(c >> 24);
                if (n == sizeof out) {
                    _lwrite(f, out, n);
                    n = 0;
                }
            }
    if (n)
        _lwrite(f, out, n);
    _lclose(f);
    _lclose(_lcreat("C:\\VG4.END", 0));
    SelectObject(mem, old);
    DeleteObject(cbm);
    DeleteObject(mbm);
    DeleteDC(mem);
    ReleaseDC(0, dc);
    return 0;
}
