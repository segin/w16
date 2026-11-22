/*
 * gd12.c - a few integer-scaled raster fonts drawn, for GD1's log of what
 * GDI hands the display driver (ARCHITECTURE.md 11.10): run as the shell
 * with tests/gdiboot.sh -l (-r for Microsoft's GDI), each case's
 * ExtTextOut and GetCharWidth calls show the FONTINFO and TEXTXFORM.
 * Logs its own steps to TK.LOG; checks nothing.
 */
#include <windows.h>

extern WORD __cdecl init_regs[7];
extern void __cdecl log_write(const char FAR *buf, WORD len);
extern WORD FAR PASCAL WaitEvent(HTASK);
extern BOOL FAR PASCAL InitApp(HINSTANCE);

static void out(const char FAR *s)
{
    WORD n = 0;
    while (s[n])
        n++;
    log_write(s, n);
}

static void one(HDC hscr, const char *face, int h, int w, int wt, int it, int ul, BYTE cs)
{
    char b[80];
    LOGFONT lf;
    HFONT f, of;
    HDC dc = CreateCompatibleDC(hscr);
    HBITMAP bm = CreateCompatibleBitmap(hscr, 200, 120), obm;
    int i, cw[2];
    for (i = 0; i < (int)sizeof lf; i++)
        ((char *)&lf)[i] = 0;
    lf.lfHeight = h;
    lf.lfWidth = w;
    lf.lfWeight = wt;
    lf.lfItalic = (BYTE)it;
    lf.lfUnderline = (BYTE)ul;
    lf.lfCharSet = cs;
    lstrcpy(lf.lfFaceName, face);
    wsprintf(b, "CASE %s %d %d %d %d %d\r\n", (LPSTR)face, h, w, wt, it, ul);
    out(b);
    f = CreateFontIndirect(&lf);
    of = SelectObject(dc, f);
    obm = SelectObject(dc, bm);
    TextOut(dc, 2, 2, "AW", 2);
    GetCharWidth(dc, 'W', 'W', cw);
    SelectObject(dc, obm);
    SelectObject(dc, of);
    DeleteObject(f);
    DeleteObject(bm);
    DeleteDC(dc);
}

int __cdecl tmain(void)
{
    HDC hscr;
    WaitEvent(0);
    InitApp((HINSTANCE)init_regs[5]);
    hscr = GetDC(NULL);
    out("GD12 start\r\n");
    one(hscr, "System", 16, 0, 0, 0, 0, 0);
    one(hscr, "System", 32, 0, 0, 0, 0, 0);
    one(hscr, "System", 16, 14, 0, 0, 0, 0);
    one(hscr, "MS Sans Serif", -44, 0, 0, 0, 0, 0);
    one(hscr, "MS Sans Serif", -44, 0, 700, 1, 1, 0);
    one(hscr, "MS Sans Serif", -26, 20, 0, 0, 0, 0);
    one(hscr, "Courier", 39, 0, 0, 0, 0, 0);
    one(hscr, "Terminal", 96, 0, 0, 0, 0, OEM_CHARSET);
    ReleaseDC(NULL, hscr);
    out("GD12 done\r\n");
    return 0;
}
