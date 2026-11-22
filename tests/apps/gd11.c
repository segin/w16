/*
 * gd11.c - the font mapper and raster font integer scaling
 * (ARCHITECTURE.md 11.9, 11.10): what Windows 3.1's GDI picks for a grid
 * of CreateFont requests, raster faces above all, and the metrics of
 * the (scaled) font it realizes.
 *
 * Sections:
 *   ENUM   EnumFonts of each raster face: the real sizes (LOGFONT and
 *          TEXTMETRIC), the base a scaled font is made from
 *   H      every height -130..130 of each raster face (its own character
 *          set), and a few far bigger ones (the largest multiple)
 *   W      widths 1..255 with several heights: separate horizontal and
 *          vertical factors
 *   A      weight, italic, underline, strike-out, pitch, family and
 *          character set with heights that need scaling
 *   weights 0..1000 for a 400 and a 700 font, scaled and not (where
 *          emboldening starts)
 *   N      no face: heights by pitch and family
 *   X      GetCharWidth of 32..126 and extents of scaled fonts
 * One line a request:
 *   M "face" h w wt it ul so cs pf -> "face" H A D IL EL AW MW WT IT UL SO
 *     PF CS OH AX AY | cwW cwi ext extH
 * (all decimal). tools/gd11.py compares two logs (Microsoft's GDI with
 * w16's) and names the scale factor of each result from the ENUM lines.
 *
 * Test fonts: with C:\GDT.FON there (tools/gdtfont.py; the specs
 * tests/gdi/gdt-*.txt), its faces T1, T2, ... in their own character
 * sets are asked instead: every height -70..70 and widths 1..60 at six
 * heights, which is how the rules in ARCHITECTURE.md 11.10 were pinned
 * down (fonts of chosen sizes and metrics, nothing else competing).
 *
 * Run as the shell of Windows 3.1 (tests/gdiheap.sh -p build/dos/GD11.EXE
 * [-r] [-v build/dos/VGA.DRV]; REFRUN_FILES=DIR/GDT.FON for the test
 * fonts); the reference is 3.1's own log, tests/gdi/gd11ref.txt (the
 * boot-test case gdifont). Black-box: only documented calls; nothing of
 * Microsoft's is copied.
 */
#include <windows.h>

extern WORD __cdecl init_regs[7];      /* ax bx cx dx si di es */
extern void __cdecl log_write(const char FAR *buf, WORD len);
extern WORD FAR PASCAL WaitEvent(HTASK);
extern BOOL FAR PASCAL InitApp(HINSTANCE);

#define I_DI 5

static HDC hdc;
static BYTE enumcs;

/* the log, written a few lines at a time (each write is committed) */
static char buf[3072];
static WORD nbuf;

static void flush(void)
{
    if (nbuf)
        log_write(buf, nbuf);
    nbuf = 0;
}

static void out(const char FAR *s)
{
    while (*s) {
        if (nbuf >= sizeof buf)
            flush();
        buf[nbuf++] = *s++;
    }
}

static void outn(long v)
{
    char b[12];
    int i = 11;
    int neg = v < 0;
    WORD u = (WORD)(neg ? -v : v);      /* every value fits 16 bits */
    b[i] = 0;
    do {
        b[--i] = (char)('0' + u % 10);
        u /= 10;
    } while (u);
    if (neg)
        b[--i] = '-';
    out(b + i);
}

static void sp(long v)
{
    out(" ");
    outn(v);
}

static void nl(void)
{
    out("\r\n");
    if (nbuf > sizeof buf - 400)
        flush();
}

int FAR PASCAL __export enum_font(const LOGFONT FAR *lf, const TEXTMETRIC FAR *t, int type,
                                  LPARAM data)
{
    (void)data;
    enumcs = lf->lfCharSet;
    out("E \"");
    out(lf->lfFaceName);
    out("\"");
    sp(type);
    sp(lf->lfHeight);
    sp(lf->lfWidth);
    sp(lf->lfWeight);
    sp(lf->lfCharSet);
    sp(lf->lfPitchAndFamily);
    out(" ->");
    sp(t->tmHeight);
    sp(t->tmAscent);
    sp(t->tmInternalLeading);
    sp(t->tmExternalLeading);
    sp(t->tmAveCharWidth);
    sp(t->tmMaxCharWidth);
    sp(t->tmWeight);
    sp(t->tmPitchAndFamily);
    sp(t->tmCharSet);
    nl();
    return 1;
}

static void req(const char *face, int h, int w, int wt, int it, int ul, int so, BYTE cs, BYTE pf)
{
    LOGFONT lf;
    HFONT f, of;
    TEXTMETRIC t;
    char got[LF_FACESIZE + 1];
    int i, cw;
    DWORD e;
    for (i = 0; i < (int)sizeof lf; i++)
        ((char *)&lf)[i] = 0;
    lf.lfHeight = h;
    lf.lfWidth = w;
    lf.lfWeight = wt;
    lf.lfItalic = (BYTE)it;
    lf.lfUnderline = (BYTE)ul;
    lf.lfStrikeOut = (BYTE)so;
    lf.lfCharSet = cs;
    lf.lfPitchAndFamily = pf;
    lstrcpy(lf.lfFaceName, face);
    out("M \"");
    out(face);
    out("\"");
    sp(h);
    sp(w);
    sp(wt);
    sp(it);
    sp(ul);
    sp(so);
    sp(cs);
    sp(pf);
    out(" ->");
    f = CreateFontIndirect(&lf);
    if (!f) {
        out(" none");
        nl();
        return;
    }
    of = SelectObject(hdc, f);
    got[0] = 0;
    GetTextFace(hdc, sizeof got, got);
    for (i = 0; i < (int)sizeof t; i++)
        ((char *)&t)[i] = 0;
    GetTextMetrics(hdc, &t);
    out(" \"");
    out(got);
    out("\"");
    sp(t.tmHeight);
    sp(t.tmAscent);
    sp(t.tmDescent);
    sp(t.tmInternalLeading);
    sp(t.tmExternalLeading);
    sp(t.tmAveCharWidth);
    sp(t.tmMaxCharWidth);
    sp(t.tmWeight);
    sp(t.tmItalic);
    sp(t.tmUnderlined);
    sp(t.tmStruckOut);
    sp(t.tmPitchAndFamily);
    sp(t.tmCharSet);
    sp(t.tmOverhang);
    sp(t.tmDigitizedAspectX);
    sp(t.tmDigitizedAspectY);
    out(" |");
    cw = 0;
    GetCharWidth(hdc, 'W', 'W', &cw);
    sp(cw);
    cw = 0;
    GetCharWidth(hdc, 'i', 'i', &cw);
    sp(cw);
    e = GetTextExtent(hdc, "Switch To", 9);
    sp(LOWORD(e));
    sp(HIWORD(e));
    nl();
    SelectObject(hdc, of);
    DeleteObject(f);
}

/* is face a font's face, and its character set */
static int test_face(const char *face, BYTE *cs)
{
    LOGFONT lf;
    HFONT f, of;
    TEXTMETRIC t;
    char got[LF_FACESIZE + 1];
    int i;
    for (i = 0; i < (int)sizeof lf; i++)
        ((char *)&lf)[i] = 0;
    lf.lfCharSet = DEFAULT_CHARSET;
    lstrcpy(lf.lfFaceName, face);
    f = CreateFontIndirect(&lf);
    of = SelectObject(hdc, f);
    got[0] = 0;
    GetTextFace(hdc, sizeof got, got);
    GetTextMetrics(hdc, &t);
    *cs = t.tmCharSet;
    SelectObject(hdc, of);
    DeleteObject(f);
    return !lstrcmp(got, face);
}

/* the full widths and extents of one font */
static void widths(const char *face, int h, int w, BYTE cs)
{
    LOGFONT lf;
    HFONT f, of;
    int i, cw[95];
    DWORD e;
    static const char *const s[] = { "Switch To AWgjy|", "iiiiii", "W", " ", "MMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMM" };
    for (i = 0; i < (int)sizeof lf; i++)
        ((char *)&lf)[i] = 0;
    lf.lfHeight = h;
    lf.lfWidth = w;
    lf.lfCharSet = cs;
    lstrcpy(lf.lfFaceName, face);
    f = CreateFontIndirect(&lf);
    of = SelectObject(hdc, f);
    out("X \"");
    out(face);
    out("\"");
    sp(h);
    sp(w);
    out(" ->");
    for (i = 0; i < 95; i++)
        cw[i] = 0;
    GetCharWidth(hdc, 32, 126, cw);
    for (i = 0; i < 95; i++)
        sp(cw[i]);
    out(" |");
    for (i = 0; i < (int)(sizeof s / sizeof s[0]); i++) {
        e = GetTextExtent(hdc, s[i], lstrlen(s[i]));
        sp(LOWORD(e));
        sp(HIWORD(e));
    }
    nl();
    SelectObject(hdc, of);
    DeleteObject(f);
}

static const struct { const char *face; BYTE cs; } faces[] = {
    { "System", ANSI_CHARSET }, { "Fixedsys", ANSI_CHARSET }, { "Terminal", OEM_CHARSET },
    { "MS Sans Serif", ANSI_CHARSET }, { "MS Serif", ANSI_CHARSET },
    { "Courier", ANSI_CHARSET }, { "Small Fonts", ANSI_CHARSET }, { "Symbol", SYMBOL_CHARSET }
};
#define NFACES (int)(sizeof faces / sizeof faces[0])

static const int wheights[] = { 0, 16, -11, 26, -26, 48 };
static const int wwidths[] = { 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16, 17, 18,
                               19, 20, 21, 22, 23, 24, 25, 26, 27, 28, 30, 32, 36, 40, 44,
                               48, 56, 64, 80, 100, 128, 200, 255 };
static const int aheights[] = { -26, 32, 39, -44, 48 };
static const int theights[] = { -11, 16, -22, 32, -26, -20 };
static const int big[] = { 150, -150, 200, 255, 256, -256, 300, 400, 500, -500, 640, 1000 };

int __cdecl tmain(void)
{
    int i, k, h, j;
    FARPROC ef;
    WaitEvent(0);
    InitApp((HINSTANCE)init_regs[I_DI]);
    hdc = GetDC(NULL);
    out("GD11 start\r\n");
    ef = MakeProcInstance((FARPROC)enum_font, (HINSTANCE)init_regs[I_DI]);
    /* test fonts: C:\GDT.FON (faces T1, T2, ...) in place of the grid */
    if (AddFontResource("C:\\GDT.FON") > 0) {
        char face[8];
        for (i = 1; i < 60; i++) {
            wsprintf(face, "T%d", i);
            EnumFonts(hdc, face, (OLDFONTENUMPROC)ef, 0);
            /* there, and its character set (by mapping, not EnumFonts,
               which w16's GDI lacks) */
            if (!test_face(face, &enumcs))
                continue;
            for (h = -70; h <= 70; h++)
                req(face, h, 0, 0, 0, 0, 0, enumcs, 0);
            for (k = 0; k < (int)(sizeof theights / sizeof theights[0]); k++)
                for (j = 1; j <= 60; j++)
                    req(face, theights[k], j, 0, 0, 0, 0, enumcs, 0);
            flush();
        }
        RemoveFontResource("C:\\GDT.FON");
        ReleaseDC(NULL, hdc);
        out("GD11 done\r\n");
        flush();
        return 0;
    }
    for (i = 0; i < NFACES; i++)
        EnumFonts(hdc, faces[i].face, (OLDFONTENUMPROC)ef, 0);
    FreeProcInstance(ef);
    out("GD11 heights\r\n");
    for (i = 0; i < NFACES; i++)
        for (h = -130; h <= 130; h++)
            req(faces[i].face, h, 0, 0, 0, 0, 0, faces[i].cs, 0);
    out("GD11 widths\r\n");
    for (i = 0; i < NFACES; i++)
        for (k = 0; k < (int)(sizeof wheights / sizeof wheights[0]); k++)
            for (j = 0; j < (int)(sizeof wwidths / sizeof wwidths[0]); j++)
                req(faces[i].face, wheights[k], wwidths[j], 0, 0, 0, 0, faces[i].cs, 0);
    out("GD11 attributes\r\n");
    for (i = 0; i < NFACES; i++)
        for (k = 0; k < (int)(sizeof aheights / sizeof aheights[0]); k++) {
            const char *f = faces[i].face;
            BYTE cs = faces[i].cs;
            h = aheights[k];
            req(f, h, 0, 100, 0, 0, 0, cs, 0);
            req(f, h, 0, 700, 0, 0, 0, cs, 0);
            req(f, h, 0, 900, 0, 0, 0, cs, 0);
            req(f, h, 0, 400, 1, 0, 0, cs, 0);
            req(f, h, 0, 700, 1, 1, 1, cs, 0);
            req(f, h, 0, 400, 0, 0, 0, cs, FIXED_PITCH);
            req(f, h, 0, 400, 0, 0, 0, cs, VARIABLE_PITCH);
            req(f, h, 0, 400, 0, 0, 0, cs, FF_ROMAN);
            req(f, h, 0, 400, 0, 0, 0, cs, FF_MODERN | FIXED_PITCH);
            req(f, h, 0, 400, 0, 0, 0, DEFAULT_CHARSET, 0);
            req(f, h, 0, 400, 0, 0, 0, ANSI_CHARSET, 0);
            req(f, h, 0, 400, 0, 0, 0, OEM_CHARSET, 0);
            req(f, h, 12, 400, 0, 0, 0, cs, 0);
        }
    out("GD11 weights\r\n");
    /* where emboldening starts, unscaled and scaled, for a 400 and a 700
       font */
    for (j = 0; j <= 1000; j += 50) {
        req("MS Sans Serif", -11, 0, j, 0, 0, 0, ANSI_CHARSET, 0);
        req("MS Sans Serif", -26, 0, j, 0, 0, 0, ANSI_CHARSET, 0);
        req("System", 16, 0, j, 0, 0, 0, ANSI_CHARSET, 0);
        req("System", 32, 0, j, 0, 0, 0, ANSI_CHARSET, 0);
    }
    out("GD11 noface\r\n");
    {
        static const BYTE pf[] = { 0, FIXED_PITCH, VARIABLE_PITCH, FF_ROMAN | VARIABLE_PITCH,
                                   FF_SWISS | VARIABLE_PITCH, FF_MODERN | FIXED_PITCH };
        for (k = 0; k < (int)sizeof pf; k++)
            for (h = -60; h <= 60; h += 2)
                req("", h, 0, 0, 0, 0, 0, DEFAULT_CHARSET, pf[k]);
    }
    out("GD11 extents\r\n");
    widths("System", 32, 0, ANSI_CHARSET);
    widths("System", 16, 14, ANSI_CHARSET);
    widths("MS Sans Serif", -44, 0, ANSI_CHARSET);
    widths("MS Sans Serif", 48, 0, ANSI_CHARSET);
    widths("MS Sans Serif", -26, 20, ANSI_CHARSET);
    widths("Courier", 39, 0, ANSI_CHARSET);
    widths("MS Serif", -27, 0, ANSI_CHARSET);
    widths("Small Fonts", -11, 0, ANSI_CHARSET);
    widths("Terminal", 24, 0, OEM_CHARSET);
    out("GD11 big\r\n");
    flush();
    /* the largest multiple (each line written before the next request,
       in case a size is too much) */
    for (i = 0; i < NFACES; i++)
        for (k = 0; k < (int)(sizeof big / sizeof big[0]); k++) {
            req(faces[i].face, big[k], 0, 0, 0, 0, 0, faces[i].cs, 0);
            flush();
        }
    for (i = 0; i < NFACES; i++) {
        req(faces[i].face, 16, 300, 0, 0, 0, 0, faces[i].cs, 0);
        req(faces[i].face, 300, 4, 0, 0, 0, 0, faces[i].cs, 0);
        flush();
    }
    ReleaseDC(NULL, hdc);
    out("GD11 done\r\n");
    flush();
    return 0;
}
