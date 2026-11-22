/*
 * gd10.c - raster font simulations (ARCHITECTURE.md 11.6 item 3, 11.9):
 * what Windows 3.1's GDI draws and reports for a raster font asked with
 * attributes its face does not have: italic, underline, strike-out,
 * emboldening, and their mixtures, and sizes the font file does not have
 * (integer scaling).
 *
 * For each face, size and set of attributes: GetTextMetrics (height,
 * ascent, leading, widths, weight, italic, underlined, struck-out,
 * overhang), GetTextExtent, GetCharWidth and GetTextFace (checked against
 * 3.1's values); then the text is drawn into a 4-plane memory bitmap
 * filled with gray: TextOut transparent, TextOut opaque, ExtTextOut with
 * ETO_OPAQUE | ETO_CLIPPED and a rectangle cutting the text, ExtTextOut
 * with the caller's widths, TextOut with TA_RIGHT. The bitmap is read
 * back with GetDIBits (4-bit) and printed (ROW lines: # text colour,
 * - background, . untouched gray) and hashed (checked).
 *
 * Run as the shell of Windows 3.1 with Microsoft's GDI (the reference:
 * tools/obsref.py of its log is gd10ref.h) and with w16's
 * (tests/gdiheap.sh -p build/dos/GD10.EXE [-r] [-v build/dos/VGA.DRV];
 * the boot-test case gdifont). For the driver calls behind each picture,
 * tests/gdiboot.sh -l with GD10 as the shell. Values only: nothing of
 * Microsoft's is copied.
 */
#include <windows.h>

#define TK_REF "gd10ref.h"
#include "tklog.h"

extern WORD FAR PASCAL WaitEvent(HTASK);
extern BOOL FAR PASCAL InitApp(HINSTANCE);

#define W 320
#define MAXH 300

static HINSTANCE hinst;
static HDC hscr;
static HGLOBAL hbits;

static struct {
    BITMAPINFOHEADER h;
    RGBQUAD c[16];
} bi;

static char nmbuf[96];

static const char *nm(const char *a, const char *b)
{
    int i = 0;
    while (*a && i < 70)
        nmbuf[i++] = *a++;
    nmbuf[i++] = ' ';
    while (*b && i < 94)
        nmbuf[i++] = *b++;
    nmbuf[i] = 0;
    return nmbuf;
}

static char letter(BYTE v)
{
    return v == 0 ? '#' : v == 15 ? '-' : v == 8 ? '.' : "0123456789ABCDEF"[v];
}

static char row[W + 8];

/* the bitmap's pixels: printed (rows) and hashed */
static WORD pixels(HDC dc, HBITMAP bm, HBITMAP old, int h)
{
    BYTE FAR *p = GlobalLock(hbits);
    WORD hash = 0;
    int x, y, n, stride = W / 2;
    SelectObject(dc, old);
    bi.h.biSize = sizeof(BITMAPINFOHEADER);
    bi.h.biWidth = W;
    bi.h.biHeight = h;
    bi.h.biPlanes = 1;
    bi.h.biBitCount = 4;
    bi.h.biCompression = 0;
    bi.h.biSizeImage = 0;
    bi.h.biClrUsed = bi.h.biClrImportant = 0;
    GetDIBits(dc, bm, 0, h, p, (BITMAPINFO FAR *)&bi, DIB_RGB_COLORS);
    SelectObject(dc, bm);
    for (y = 0; y < h; y++) {
        BYTE FAR *l = p + (long)(h - 1 - y) * stride;
        n = 0;
        for (x = 0; x < W; x++) {
            BYTE v = (BYTE)(x & 1 ? l[x >> 1] & 15 : l[x >> 1] >> 4);
            row[x] = letter(v);
            if (v != 8)
                n = x + 1;
            hash = (WORD)((hash << 5) | (hash >> 11)) + v + 1;
        }
        row[n] = 0;
        out("ROW ");
        out(row);
        out("\r\n");
    }
    GlobalUnlock(hbits);
    return hash;
}

static const char text[] = "Switch To AWgjy|";

/* kind: 0 checked; 1 checked, with a blue text colour, R2_XORPEN and
   TA_UPDATECP in place of TA_RIGHT (the current position logged); 2
   integer scaling (ARCHITECTURE.md 11.10): the pixels, extents, widths
   and the metrics that differ from the unscaled font's checked, the rest
   logged (GD11 checks every metric; the table must fit the data
   segment) */
static int kind;

/* lfWidth of the next case */
static int width;

static void ob(const char *name, WORD v)
{
    if (kind == 2)
        obs_env(name, v);
    else
        obs(name, v);
}

/* in the table for kind 2 too */
static void ob2(const char *name, WORD v)
{
    obs(name, v);
}

/* the face's short name in the observations' names (the reference table
   must fit the probe's data segment) */
static const char *short_face(const char *face)
{
    static const char *const f[][2] = {
        { "System", "Sys" }, { "MS Sans Serif", "SS" }, { "MS Serif", "Sf" },
        { "Courier", "Cour" }, { "Terminal", "Term" }, { "Small Fonts", "Sm" } };
    int i;
    for (i = 0; i < (int)(sizeof f / sizeof f[0]); i++)
        if (!lstrcmp(face, f[i][0]))
            return f[i][1];
    return face;
}

static void one(const char *face, int ht, BYTE cs, int wt, int it, int ul, int so)
{
    char name[48], b[40];
    LOGFONT lf;
    HFONT f, of;
    HDC dc;
    HBITMAP bm, obm;
    HBRUSH gray;
    TEXTMETRIC tm;
    RECT r;
    DWORD e;
    int i, lh, h, y, cw[2];
    int dx[sizeof text];
    wsprintf(name, "%s %d %s%s%s%s", (LPSTR)short_face(face), ht, (LPSTR)(wt > 400 ? "b" : ""),
             (LPSTR)(it ? "i" : ""), (LPSTR)(ul ? "u" : ""), (LPSTR)(so ? "s" : ""));
    if (wt <= 400 && !it && !ul && !so)
        lstrcat(name, "r");
    if (width)
        wsprintf(name + lstrlen(name), " w%d", width);
    if (kind == 1)
        lstrcat(name, " xor");
    for (i = 0; i < (int)sizeof lf; i++)
        ((char *)&lf)[i] = 0;
    lf.lfHeight = ht;
    lf.lfWidth = width;
    lf.lfWeight = wt;
    lf.lfItalic = (BYTE)it;
    lf.lfUnderline = (BYTE)ul;
    lf.lfStrikeOut = (BYTE)so;
    lf.lfCharSet = cs;
    lstrcpy(lf.lfFaceName, face);
    f = CreateFontIndirect(&lf);
    dc = CreateCompatibleDC(hscr);
    of = SelectObject(dc, f);
    out("CASE ");
    out(name);
    out("\r\n");
    GetTextMetrics(dc, &tm);
    ob2(nm(name, "tmH"), tm.tmHeight);
    ob(nm(name, "tmA"), tm.tmAscent);
    ob(nm(name, "tmD"), tm.tmDescent);
    ob(nm(name, "tmIL"), tm.tmInternalLeading);
    ob(nm(name, "tmEL"), tm.tmExternalLeading);
    ob2(nm(name, "tmAve"), tm.tmAveCharWidth);
    ob(nm(name, "tmMax"), tm.tmMaxCharWidth);
    ob2(nm(name, "tmW"), tm.tmWeight);
    ob(nm(name, "tmI"), tm.tmItalic);
    ob(nm(name, "tmU"), tm.tmUnderlined);
    ob(nm(name, "tmS"), tm.tmStruckOut);
    ob2(nm(name, "tmOvh"), tm.tmOverhang);
    ob(nm(name, "tmPF"), tm.tmPitchAndFamily);
    ob(nm(name, "tmCS"), tm.tmCharSet);
    ob2(nm(name, "tmAsp"), tm.tmDigitizedAspectX);
    e = GetTextExtent(dc, text, sizeof text - 1);
    ob2(nm(name, "ext"), LOWORD(e));
    ob(nm(name, "exth"), HIWORD(e));
    e = GetTextExtent(dc, "W", 1);
    ob(nm(name, "extW"), LOWORD(e));
    GetCharWidth(dc, 'W', 'W', cw);
    ob2(nm(name, "cwW"), cw[0]);
    GetCharWidth(dc, 'i', 'i', cw);
    ob2(nm(name, "cwi"), cw[0]);
    GetTextFace(dc, sizeof b, b);
    out("OBS ");
    out(nm(name, "face=\""));
    out(b);
    out("\"\r\n");
    /* the pictures */
    lh = tm.tmHeight + 6;
    h = 5 * lh + 4;
    if (h > MAXH)
        h = MAXH;
    bm = CreateCompatibleBitmap(hscr, W, h);
    obm = SelectObject(dc, bm);
    gray = CreateSolidBrush(RGB(128, 128, 128));
    r.left = 0;
    r.top = 0;
    r.right = W;
    r.bottom = h;
    FillRect(dc, &r, gray);
    DeleteObject(gray);
    SetTextColor(dc, kind == 1 ? RGB(0, 0, 255) : RGB(0, 0, 0));
    if (kind == 1)
        SetROP2(dc, R2_XORPEN);
    SetBkColor(dc, RGB(255, 255, 255));
    y = 3;
    SetBkMode(dc, TRANSPARENT);
    TextOut(dc, 4, y, text, sizeof text - 1);
    y += lh;
    SetBkMode(dc, OPAQUE);
    TextOut(dc, 4, y, text, sizeof text - 1);
    y += lh;
    r.left = 9;
    r.top = y + 2;
    r.right = 90;
    r.bottom = y + tm.tmHeight - 2;
    ExtTextOut(dc, 4, y, ETO_OPAQUE | ETO_CLIPPED, &r, text, sizeof text - 1, NULL);
    y += lh;
    SetBkMode(dc, TRANSPARENT);
    for (i = 0; i < (int)sizeof text - 1; i++)
        dx[i] = (int)(6 + (i * 5) % 7 + tm.tmAveCharWidth / 2);
    ExtTextOut(dc, 4, y, 0, NULL, text, sizeof text - 1, dx);
    y += lh;
    if (kind == 1) {
        DWORD cp;
        MoveTo(dc, 4, y);
        SetTextAlign(dc, TA_UPDATECP);
        TextOut(dc, 0, 0, text, 6);
        TextOut(dc, 0, 0, text + 6, 4);
        cp = GetCurrentPosition(dc);
        ob(nm(name, "cp x"), LOWORD(cp));
        ob(nm(name, "cp y"), HIWORD(cp));
    } else {
        SetTextAlign(dc, TA_RIGHT);
        TextOut(dc, W - 4, y, text, 6);
    }
    SetTextAlign(dc, TA_LEFT);
    if (y + lh <= h)
        ob2(nm(name, "hash"), pixels(dc, bm, obm, h));
    else
        obs_env(nm(name, "hash (cut)"), pixels(dc, bm, obm, h));
    SelectObject(dc, obm);
    DeleteObject(bm);
    SelectObject(dc, of);
    DeleteDC(dc);
    DeleteObject(f);
}

/* a copy over 64K (System 8 down and 5 across: some 200K; Terminal 8 by
   5, 122K, italic): two characters drawn and hashed */
static void big(const char *face, int ht, BYTE cs, int it)
{
    char name[40];
    LOGFONT lf;
    HFONT f, of;
    HDC dc = CreateCompatibleDC(hscr);
    HBITMAP bm = CreateCompatibleBitmap(hscr, W, 140), obm;
    HBRUSH gray = CreateSolidBrush(RGB(128, 128, 128));
    RECT r;
    int i;
    wsprintf(name, "%s %d big", (LPSTR)short_face(face), ht);
    out("CASE ");
    out(name);
    out("\r\n");
    for (i = 0; i < (int)sizeof lf; i++)
        ((char *)&lf)[i] = 0;
    lf.lfHeight = ht;
    lf.lfItalic = (BYTE)it;
    lf.lfCharSet = cs;
    lstrcpy(lf.lfFaceName, face);
    f = CreateFontIndirect(&lf);
    of = SelectObject(dc, f);
    obm = SelectObject(dc, bm);
    r.left = r.top = 0;
    r.right = W;
    r.bottom = 140;
    FillRect(dc, &r, gray);
    DeleteObject(gray);
    SetBkColor(dc, RGB(255, 255, 255));
    TextOut(dc, 2, 2, "Wg", 2);
    obs(nm(name, "hash"), pixels(dc, bm, obm, 140));
    SelectObject(dc, obm);
    DeleteObject(bm);
    SelectObject(dc, of);
    DeleteDC(dc);
    DeleteObject(f);
}

static const struct { const char *face; int h; BYTE cs; } fonts[] = {
    { "System", 16, 0 }, { "MS Sans Serif", -11, 0 }, { "MS Sans Serif", -13, 0 },
    { "MS Sans Serif", -16, 0 }, { "MS Sans Serif", -24, 0 }, { "Courier", -13, 0 },
    { "Terminal", 12, OEM_CHARSET }, { "MS Serif", -16, 0 }, { "Small Fonts", -9, 0 }
};

static const struct { int wt, it, ul, so; } attrs[] = {
    { 400, 0, 0, 0 }, { 700, 0, 0, 0 }, { 400, 1, 0, 0 }, { 400, 0, 1, 0 },
    { 400, 0, 0, 1 }, { 700, 1, 0, 0 }, { 400, 0, 1, 1 }, { 400, 1, 1, 0 },
    { 700, 1, 1, 1 }
};

/* more sizes, for the lines' position and thickness */
static const struct { const char *face; int h; } sizes[] = {
    { "MS Sans Serif", -19 }, { "MS Sans Serif", -32 }, { "MS Serif", -11 },
    { "MS Serif", -13 }, { "MS Serif", -19 }, { "MS Serif", -21 }, { "MS Serif", -32 },
    { "Courier", -16 }, { "Courier", -20 }, { "Small Fonts", -5 }, { "Small Fonts", -7 },
    { "System", 0 }
};

/* sizes the files do not have (integer scaling), by height and width */
static const struct { const char *face; int h, w; BYTE cs; } scaled[] = {
    { "System", 32, 0, 0 }, { "MS Sans Serif", -44, 0, 0 }, { "Courier", -26, 0, 0 },
    { "Terminal", 24, 0, OEM_CHARSET }, { "MS Sans Serif", 48, 0, 0 }, { "Courier", 39, 0, 0 },
    { "MS Serif", -27, 0, 0 }, { "Small Fonts", -11, 0, 0 }, { "System", 16, 14, 0 },
    { "MS Sans Serif", -26, 20, 0 }, { "Terminal", 48, 4, OEM_CHARSET },
    { "Small Fonts", 40, 0, 0 }
};

int __cdecl tmain(void)
{
    int k, a;
    hinst = (HINSTANCE)init_regs[5];
    WaitEvent(0);
    InitApp(hinst);
    out("GD10 start\r\n");
    hscr = GetDC(NULL);
    hbits = GlobalAlloc(GMEM_MOVEABLE, (DWORD)W / 2 * MAXH);
    obs("caps TEXTCAPS", GetDeviceCaps(hscr, TEXTCAPS));
    for (k = 0; k < (int)(sizeof fonts / sizeof fonts[0]); k++)
        for (a = 0; a < (int)(sizeof attrs / sizeof attrs[0]); a++)
            one(fonts[k].face, fonts[k].h, fonts[k].cs, attrs[a].wt, attrs[a].it, attrs[a].ul,
                attrs[a].so);
    out("GD10 sizes\r\n");
    for (k = 0; k < (int)(sizeof sizes / sizeof sizes[0]); k++) {
        one(sizes[k].face, sizes[k].h, 0, 400, 0, 1, 1);
        one(sizes[k].face, sizes[k].h, 0, 400, 1, 1, 1);
    }
    out("GD10 colour\r\n");
    kind = 1;
    one("MS Sans Serif", -13, 0, 700, 1, 1, 1);
    one("Courier", -13, 0, 400, 0, 1, 1);
    out("GD10 scaled\r\n");
    kind = 2;
    for (k = 0; k < (int)(sizeof scaled / sizeof scaled[0]); k++) {
        width = scaled[k].w;
        one(scaled[k].face, scaled[k].h, scaled[k].cs, 400, 0, 0, 0);
        one(scaled[k].face, scaled[k].h, scaled[k].cs, 400, 1, 1, 1);
        one(scaled[k].face, scaled[k].h, scaled[k].cs, 700, 0, 0, 0);
    }
    width = 0;
    out("GD10 big\r\n");
    kind = 0;
    big("System", 128, 0, 0);
    big("Terminal", 96, OEM_CHARSET, 1);
    GlobalFree(hbits);
    ReleaseDC(NULL, hscr);
    out("GD10 done\r\n");
    return failures;
}
