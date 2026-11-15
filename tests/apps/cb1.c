/*
 * cb1.c - CB1: a clipboard viewer that records every clipboard change
 * (black-box probe for the display grabbers, tests/grabber.sh,
 * ARCHITECTURE.md 9.11).
 *
 * Started from WIN.INI load=, it joins the clipboard-viewer chain with a
 * hidden window. At each WM_DRAWCLIPBOARD it appends to C:\CB.LOG
 *     == change n
 *     fmt <id> <name> size <GlobalSize>
 * and for each format:
 *     CF_TEXT, CF_OEMTEXT: the text, line by line ("  | ..."), bytes below
 *         20h other than CR LF as \xx;
 *     CF_BITMAP: "  bitmap <w>x<h> planes <p> bpp <b> widthbytes <wb>"
 *         and the bits' size and checksum; the bits go to C:\CBn.BIN;
 *     CF_DIB: the BITMAPINFOHEADER's fields, the colour table, the bits'
 *         checksum; the whole block to C:\CBn.DIB;
 *     CF_PALETTE: the entries;
 *     others: size and checksum.
 * "Delayed" formats are rendered by GetClipboardData as for any viewer.
 */
#include <windows.h>

extern WORD __cdecl init_regs[7];
extern WORD FAR PASCAL K_WAITEVENT(HTASK);
extern BOOL FAR PASCAL U_INITAPP(HINSTANCE);
extern WORD __cdecl __far get_ahincr(void);

static HWND next_viewer;
static int changes;
static HFILE lf = HFILE_ERROR;
static char line[200];

static void lopen(void)
{
    OFSTRUCT of;
    lf = OpenFile("C:\\CB.LOG", &of, OF_WRITE);
    if (lf == HFILE_ERROR)
        lf = OpenFile("C:\\CB.LOG", &of, OF_CREATE | OF_WRITE);
    if (lf != HFILE_ERROR)
        _llseek(lf, 0, 2);
}

static void out(const char *s)
{
    if (lf != HFILE_ERROR)
        _lwrite(lf, s, lstrlen(s));
}

static void lclose(void)
{
    if (lf != HFILE_ERROR)
        _lclose(lf);
    lf = HFILE_ERROR;
}

/* 16-bit sum and rotate-xor of n bytes from sel:0 on (64 KB pieces) */
static void checksum(WORD sel, DWORD n, WORD *sum, WORD *rx)
{
    WORD s = 0, x = 0, k, i;
    const BYTE FAR *p;
    while (n) {
        p = (const BYTE FAR *)MAKELP(sel, 0);
        k = n > 32768L ? 32768U : (WORD)n;
        for (i = 0; i < k; i++) {
            s += p[i];
            x = (WORD)((x << 1) | (x >> 15));
            x ^= p[i];
        }
        n -= k;
        if (n) {
            p = (const BYTE FAR *)MAKELP(sel, 32768U);
            k = n > 32768L ? 32768U : (WORD)n;
            for (i = 0; i < k; i++) {
                s += p[i];
                x = (WORD)((x << 1) | (x >> 15));
                x ^= p[i];
            }
            n -= k;
        }
        sel += get_ahincr();
    }
    *sum = s;
    *rx = x;
}

static void dumpfile(const char *ext, WORD sel, DWORD n)
{
    char name[20];
    OFSTRUCT of;
    HFILE f;
    wsprintf(name, "C:\\CB%d.%s", changes, (LPSTR)ext);
    f = OpenFile(name, &of, OF_CREATE | OF_WRITE);
    if (f == HFILE_ERROR)
        return;
    while (n) {
        WORD k = n > 32768L ? 32768U : (WORD)n;
        _lwrite(f, (LPCSTR)MAKELP(sel, 0), k);
        n -= k;
        if (n) {
            k = n > 32768L ? 32768U : (WORD)n;
            _lwrite(f, (LPCSTR)MAKELP(sel, 32768U), k);
            n -= k;
        }
        sel += get_ahincr();
    }
    _lclose(f);
}

static void text(HGLOBAL h)
{
    LPSTR p = GlobalLock(h);
    DWORD n = GlobalSize(h), i;
    int k = 0;
    if (!p)
        return;
    out("  | ");
    for (i = 0; i < n && p[(WORD)i]; i++) {
        BYTE c = p[(WORD)i];
        if (c == '\n') {
            out("\\n\r\n  | ");
            continue;
        }
        if (c < 0x20 || c >= 0x7F) {
            wsprintf(line, "\\%02X", c);
            out(line);
        } else {
            line[0] = c;
            line[1] = 0;
            out(line);
        }
        k++;
    }
    wsprintf(line, "\r\n  (%lu bytes to the NUL)\r\n", i);
    out(line);
    GlobalUnlock(h);
}

static void bitmap(HBITMAP hb)
{
    BITMAP bm;
    HGLOBAL hm;
    WORD sel;
    DWORD n = 0, got;
    WORD s, x, i;
    if (!GetObject(hb, sizeof bm, &bm)) {
        out("  bitmap: GetObject failed\r\n");
        return;
    }
    wsprintf(line, "  bitmap %dx%d planes %d bpp %d widthbytes %d\r\n",
             bm.bmWidth, bm.bmHeight, bm.bmPlanes, bm.bmBitsPixel, bm.bmWidthBytes);
    out(line);
    for (i = 0; i < (WORD)(bm.bmHeight * bm.bmPlanes); i++)
        n += bm.bmWidthBytes;
    hm = GlobalAlloc(GMEM_MOVEABLE | GMEM_ZEROINIT, n + 16);
    if (!hm)
        return;
    sel = SELECTOROF(GlobalLock(hm));
    got = GetBitmapBits(hb, n, (LPSTR)MAKELP(sel, 0));
    checksum(sel, n, &s, &x);
    wsprintf(line, "  bits %lu got %lu sum %04X rx %04X\r\n", n, got, s, x);
    out(line);
    dumpfile("BIN", sel, n);
    GlobalUnlock(hm);
    GlobalFree(hm);
}

static void dib(HGLOBAL h)
{
    BYTE FAR *p = (BYTE FAR *)GlobalLock(h);
    BITMAPINFOHEADER FAR *bi = (BITMAPINFOHEADER FAR *)p;
    DWORD n = GlobalSize(h);
    WORD s, x;
    int i, nc;
    if (!p)
        return;
    wsprintf(line, "  dib size %lu %ldx%ld planes %u bpp %u compr %lu image %lu clrused %lu\r\n",
             bi->biSize, bi->biWidth, bi->biHeight, bi->biPlanes, bi->biBitCount,
             bi->biCompression, bi->biSizeImage, bi->biClrUsed);
    out(line);
    nc = bi->biClrUsed ? (int)bi->biClrUsed : (bi->biBitCount <= 8 ? 1 << bi->biBitCount : 0);
    for (i = 0; i < nc && i < 256; i++) {
        RGBQUAD FAR *q = (RGBQUAD FAR *)(p + bi->biSize) + i;
        wsprintf(line, " %02X%02X%02X", q->rgbRed, q->rgbGreen, q->rgbBlue);
        out(line);
        if (i % 16 == 15)
            out("\r\n");
    }
    checksum(SELECTOROF(p), n, &s, &x);
    wsprintf(line, "  block %lu sum %04X rx %04X\r\n", n, s, x);
    out(line);
    dumpfile("DIB", SELECTOROF(p), n);
    GlobalUnlock(h);
}

static void palette(HPALETTE hp)
{
    PALETTEENTRY pe[16];
    WORD n = 0, i, k;
    GetObject(hp, sizeof n, &n);
    wsprintf(line, "  palette %u entries\r\n", n);
    out(line);
    for (i = 0; i < n; i += 16) {
        k = GetPaletteEntries(hp, i, 16, pe);
        while (k--) {
            wsprintf(line, " %02X%02X%02X%02X", pe[15 - k].peRed, pe[15 - k].peGreen,
                     pe[15 - k].peBlue, pe[15 - k].peFlags);
            out(line);
        }
        out("\r\n");
    }
}

static void record(HWND hwnd)
{
    UINT f = 0;
    char name[64];
    changes++;
    lopen();
    wsprintf(line, "== change %d owner %04X\r\n", changes, (WORD)GetClipboardOwner());
    out(line);
    if (!OpenClipboard(hwnd)) {
        out("  OpenClipboard failed\r\n");
        lclose();
        return;
    }
    while ((f = EnumClipboardFormats(f)) != 0) {
        HANDLE h;
        name[0] = 0;
        if (f >= 0xC000)
            GetClipboardFormatName(f, name, sizeof name);
        h = GetClipboardData(f);
        wsprintf(line, "fmt %04X %s handle %s size %lu\r\n", f, (LPSTR)name,
                 (LPSTR)(h ? "yes" : "none"),
                 (h && f != CF_BITMAP && f != CF_PALETTE) ? GlobalSize(h) : 0L);
        out(line);
        if (!h)
            continue;
        switch (f) {
        case CF_TEXT:
        case CF_OEMTEXT:
            text(h);
            break;
        case CF_BITMAP:
            bitmap((HBITMAP)h);
            break;
        case CF_DIB:
            dib(h);
            break;
        case CF_PALETTE:
            palette((HPALETTE)h);
            break;
        default: {
            BYTE FAR *p = (BYTE FAR *)GlobalLock(h);
            WORD s, x;
            if (p) {
                checksum(SELECTOROF(p), GlobalSize(h), &s, &x);
                wsprintf(line, "  sum %04X rx %04X\r\n", s, x);
                out(line);
                GlobalUnlock(h);
            }
        }
        }
    }
    CloseClipboard();
    lclose();
}

LRESULT FAR PASCAL __export CbWndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp)
{
    switch (msg) {
    case WM_CREATE:
        next_viewer = SetClipboardViewer(hwnd);
        return 0;
    case WM_DRAWCLIPBOARD:
        record(hwnd);
        SetTimer(hwnd, 1, 2000, NULL);  /* and once more a little later */
        if (next_viewer)
            SendMessage(next_viewer, msg, wp, lp);
        return 0;
    case WM_TIMER:
        KillTimer(hwnd, 1);
        lopen();
        out("(2 s later)\r\n");
        lclose();
        record(hwnd);
        return 0;
    case WM_CHANGECBCHAIN:
        if ((HWND)wp == next_viewer)
            next_viewer = (HWND)LOWORD(lp);
        else if (next_viewer)
            SendMessage(next_viewer, msg, wp, lp);
        return 0;
    case WM_DESTROY:
        ChangeClipboardChain(hwnd, next_viewer);
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProc(hwnd, msg, wp, lp);
}

void __cdecl __far tmain(void)
{
    HINSTANCE hinst = (HINSTANCE)init_regs[5];
    WNDCLASS wc;
    MSG msg;

    K_WAITEVENT(0);
    U_INITAPP(hinst);
    lopen();
    out("CB1 started\r\n");
    lclose();
    {
        BYTE *z = (BYTE *)&wc;
        int i;
        for (i = 0; i < sizeof wc; i++)
            z[i] = 0;
    }
    wc.lpfnWndProc = CbWndProc;
    wc.hInstance = hinst;
    wc.lpszClassName = "CB1";
    if (!RegisterClass(&wc))
        return;
    CreateWindow("CB1", "CB1", WS_OVERLAPPED, 0, 0, 10, 10, NULL, NULL, hinst, NULL);
    while (GetMessage(&msg, NULL, 0, 0))
        DispatchMessage(&msg);
}
