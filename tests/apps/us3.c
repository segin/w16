/*
 * us3.c - probe US3: DrawText's layout (ARCHITECTURE.md 10).
 *
 * For a set of strings, fonts, widths and flags: the rectangle DT_CALCRECT
 * returns, the height DrawText returns, and where the text lands - the
 * first and last column with ink in each line, found by drawing into a
 * monochrome bitmap and scanning it. Run as the shell on Windows 3.1
 * (tests/refrun.sh build/dos/US3.EXE) and on w16's USER
 * (tests/userboot.sh -s build/dos/US3.EXE); us3ref.h holds 3.1's values.
 */
#include <windows.h>

#define TK_REF "us3ref.h"
#include "tklog.h"

#define BW 160
#define BH 64

static BYTE bits[BH * BW / 8];

static const char *texts[] = {
    "Clipboard Viewer", "MS-DOS Prompt", "Windows Setup", "File Manager", "Accessories",
    "Print Manager", "a b c d e f g h", "Supercalifragilistic", "x", "&File &&More", 0
};
static const int widths[] = { 72, 73, 74, 75, 40, 0 };
static const UINT flagsets[] = {
    DT_CENTER | DT_WORDBREAK | DT_NOPREFIX,
    DT_LEFT | DT_WORDBREAK | DT_NOPREFIX,
    DT_RIGHT | DT_WORDBREAK,
    DT_CENTER | DT_SINGLELINE | DT_VCENTER,
    0xFFFF
};

static void name3(char *b, int t, int w, int f, const char *what)
{
    wsprintf(b, "t%d.w%d.f%d.%s", t, w, f, (LPSTR)what);
}

int __cdecl tmain(void)
{
    HDC scr = GetDC(NULL);
    HDC mem = CreateCompatibleDC(scr);
    HBITMAP bm = CreateBitmap(BW, BH, 1, 1, NULL), oldbm;
    HFONT fonts[2], oldf;
    int fi, t, wi, f;
    char nm[64];
    LOGFONT lf;
    oldbm = SelectObject(mem, bm);
    fonts[0] = GetStockObject(SYSTEM_FONT);
    {
        int i;
        for (i = 0; i < (int)sizeof lf; i++)
            ((BYTE *)&lf)[i] = 0;
        lf.lfHeight = -8 * GetDeviceCaps(scr, LOGPIXELSY) / 72;
        lf.lfWeight = FW_NORMAL;
        lstrcpy(lf.lfFaceName, "MS Sans Serif");
        fonts[1] = CreateFontIndirect(&lf);
    }
    out("US3 start\r\n");
    for (fi = 0; fi < 2; fi++) {
        TEXTMETRIC tm;
        oldf = SelectObject(mem, fonts[fi]);
        GetTextMetrics(mem, &tm);
        wsprintf(nm, "font%d.height", fi);
        obs(nm, tm.tmHeight);
        for (t = 0; texts[t]; t++) {
            wsprintf(nm, "font%d.t%d.extent", fi, t);
            obs(nm, LOWORD(GetTextExtent(mem, texts[t], lstrlen(texts[t]))));
            for (wi = 0; widths[wi]; wi++)
                for (f = 0; flagsets[f] != 0xFFFF; f++) {
                    RECT r, c;
                    int h, y, line;
                    SetRect(&c, 0, 0, widths[wi], 0);
                    h = DrawText(mem, texts[t], -1, &c, flagsets[f] | DT_CALCRECT);
                    name3(nm, t, widths[wi], f, "");
                    {
                        char n2[64];
                        wsprintf(n2, "font%d.%scalc.h", fi, (LPSTR)nm);
                        obs(n2, h);
                        wsprintf(n2, "font%d.%scalc.right", fi, (LPSTR)nm);
                        obs(n2, c.right);
                        wsprintf(n2, "font%d.%scalc.bottom", fi, (LPSTR)nm);
                        obs(n2, c.bottom);
                    }
                    PatBlt(mem, 0, 0, BW, BH, WHITENESS);
                    SetTextColor(mem, RGB(0, 0, 0));
                    SetBkColor(mem, RGB(255, 255, 255));
                    SetBkMode(mem, TRANSPARENT);
                    SetRect(&r, 0, 0, widths[wi], (flagsets[f] & DT_SINGLELINE) ? 40 : BH);
                    h = DrawText(mem, texts[t], -1, &r, flagsets[f]);
                    {
                        char n2[64];
                        wsprintf(n2, "font%d.%sdraw.h", fi, (LPSTR)nm);
                        obs(n2, h);
                    }
                    GetBitmapBits(bm, sizeof bits, bits);
                    /* per text line: first and last inked column, and the
                       first inked row */
                    for (line = 0; line * tm.tmHeight < BH && line < 4; line++) {
                        int x0 = -1, x1 = -1, y0 = -1, x;
                        for (y = line * tm.tmHeight; y < (line + 1) * tm.tmHeight && y < BH; y++)
                            for (x = 0; x < BW; x++)
                                if (!(bits[y * (BW / 8) + x / 8] & (0x80 >> (x & 7)))) {
                                    if (x0 < 0 || x < x0) x0 = x;
                                    if (x > x1) x1 = x;
                                    if (y0 < 0) y0 = y;
                                }
                        if (x0 < 0)
                            break;
                        {
                            char n2[64];
                            wsprintf(n2, "font%d.%sline%d.x0", fi, (LPSTR)nm, line);
                            obs(n2, x0);
                            wsprintf(n2, "font%d.%sline%d.x1", fi, (LPSTR)nm, line);
                            obs(n2, x1);
                            wsprintf(n2, "font%d.%sline%d.y0", fi, (LPSTR)nm, line);
                            obs(n2, y0);
                        }
                    }
                }
        }
        SelectObject(mem, oldf);
    }
    SelectObject(mem, oldbm);
    DeleteObject(bm);
    DeleteDC(mem);
    ReleaseDC(NULL, scr);
    out("US3 done, failures=");
    outhex((WORD)failures);
    out("\r\n");
    return failures;
}
