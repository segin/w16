/*
 * fn4.c - the TrueType probe (ARCHITECTURE.md 6.9.3): what a program can
 * observe about Windows 3.1's TrueType core set (Arial, Times New Roman,
 * Courier New, four styles each) through the API, as the reference for
 * w16's own metric-compatible TrueType fonts; the same dump for w16's
 * fonts once installed; the .FOT files CreateScalableFontResource writes
 * for them; and which Unicode character 3.1 looks up for each ANSI code.
 *
 * Runs as the shell (tests/refrun.sh, tests/ttfonts.sh) and writes TK.LOG.
 * Only documented GDI calls are used; no font file is opened or read
 * (GetFontData is never called). GetGlyphOutline is asked for metrics only
 * (GGO_METRICS: the black box and origin), never for outlines.
 *
 * Sections:
 *   FACE "name" wt=W it=I   (Microsoft's faces, then w16's)
 *     each realised at a height of -em (em from GetOutlineTextMetrics'
 *     otmEMSquare), so that every value is in font units:
 *     face, TEXTMETRIC, OUTLINETEXTMETRIC (every field and its four
 *     names), GetCharWidth 0-255 (w lines), GetCharABCWidths 0-255 (abc
 *     lines), GGO_METRICS of a few letters (gm lines: black box, origin,
 *     advance), GetKerningPairs (kp lines); ABC widths and GGO_METRICS
 *     again at 128, 256, 512 and 1024 pixels per em (abcN, gmN lines: at
 *     2048 3.1's ABC widths are not right);
 *     then for every pixel size (ppem, lfHeight -ppem) 6-255: TEXTMETRIC
 *     and GetCharWidth 0-255 (s lines), the hinted device widths
 *   MAP: for each test font present (tools/ttmap.py: C:\TTMAP.TTF, whose
 *     glyph widths name their Unicode character; TTMAPA/TTMAPB.TTF, the
 *     same with an hdmx table, head flags bit 4 clear/set): GetCharWidth
 *     0-255 at 2048, 20 and 12 pixels per em
 *   FOT: if w16's .TTF files are in SYSTEM, CreateScalableFontResource for
 *     each, the portable way (file name and path apart: SYSTEM\X.FOT) and
 *     with a full path (C:\FOTB\X.FOT), and again the portable way into
 *     C:\FOTC (to see what varies), then AddFontResource of SYSTEM\X.FOT
 *   FAMILIES: EnumFontFamilies of every family, and of w16's families
 *     (full name and style of each member)
 *
 * With the command tail "show N" it is instead a window for screenshots
 * (see show()).
 */
#include <windows.h>

extern WORD __cdecl init_regs[7];      /* ax bx cx dx si di es */
extern void __cdecl log_write(const char FAR *buf, WORD len);
extern WORD FAR PASCAL WaitEvent(HTASK);
extern BOOL FAR PASCAL InitApp(HINSTANCE);

#define I_DI 5

static HDC hdc;

static void zero(void FAR *p, WORD n)
{
    BYTE FAR *b = (BYTE FAR *)p;
    while (n--)
        *b++ = 0;
}
static char sysdir[144];

static void out(const char FAR *s)
{
    WORD n = 0;
    while (s[n])
        n++;
    log_write(s, n);
}

static void outl(long v)
{
    /* no long division (no C library): subtract powers of ten */
    static const unsigned long pw[] = {
        1000000000UL, 100000000UL, 10000000UL, 1000000UL, 100000UL,
        10000UL, 1000UL, 100UL, 10UL, 1UL
    };
    char b[14];
    int i, n = 0;
    unsigned long u = v < 0 ? -(unsigned long)v : (unsigned long)v;
    if (v < 0)
        b[n++] = '-';
    for (i = 0; i < 10; i++) {
        char d = '0';
        while (u >= pw[i]) {
            u -= pw[i];
            d++;
        }
        if (d != '0' || n > (v < 0) || i == 9)
            b[n++] = d;
    }
    b[n] = 0;
    out(b);
}

static void kv(const char *k, long v)
{
    out(" ");
    out(k);
    out("=");
    outl(v);
}

static void ks(const char *k, const char FAR *s)
{
    out(" ");
    out(k);
    out("=\"");
    out(s);
    out("\"");
}

static void nl(void)
{
    out("\r\n");
}

static void tm_line(const TEXTMETRIC FAR *t)
{
    kv("h", t->tmHeight);
    kv("a", t->tmAscent);
    kv("d", t->tmDescent);
    kv("il", t->tmInternalLeading);
    kv("el", t->tmExternalLeading);
    kv("aw", t->tmAveCharWidth);
    kv("mw", t->tmMaxCharWidth);
    kv("wt", t->tmWeight);
    kv("it", t->tmItalic);
    kv("ul", t->tmUnderlined);
    kv("so", t->tmStruckOut);
    kv("fc", t->tmFirstChar);
    kv("lc", t->tmLastChar);
    kv("dc", t->tmDefaultChar);
    kv("bc", t->tmBreakChar);
    kv("pf", t->tmPitchAndFamily);
    kv("cs", t->tmCharSet);
    kv("oh", t->tmOverhang);
    kv("ax", t->tmDigitizedAspectX);
    kv("ay", t->tmDigitizedAspectY);
}

static HFONT mkfont(int h, const char *face, int wt, int it)
{
    return CreateFont(h, 0, 0, 0, wt, it, 0, 0, ANSI_CHARSET, OUT_TT_PRECIS,
                      CLIP_DEFAULT_PRECIS, DEFAULT_QUALITY, DEFAULT_PITCH | FF_DONTCARE,
                      face);
}

static char otmbuf[600];

static void otm_lines(void)
{
    OUTLINETEXTMETRIC FAR *o = (OUTLINETEXTMETRIC FAR *)otmbuf;
    WORD n = GetOutlineTextMetrics(hdc, sizeof otmbuf, o);
    int i;
    out(" OTM");
    kv("ret", n);
    if (!n) {
        nl();
        return;
    }
    kv("size", o->otmSize);
    kv("sel", o->otmfsSelection);
    kv("fstype", o->otmfsType);
    kv("rise", o->otmsCharSlopeRise);
    kv("run", o->otmsCharSlopeRun);
    kv("angle", o->otmItalicAngle);
    kv("em", o->otmEMSquare);
    kv("asc", o->otmAscent);
    kv("desc", o->otmDescent);
    kv("gap", o->otmLineGap);
    kv("cap", o->otmsCapEmHeight);
    kv("xh", o->otmsXHeight);
    nl();
    out(" OTM2");
    kv("box.l", o->otmrcFontBox.left);
    kv("box.t", o->otmrcFontBox.top);
    kv("box.r", o->otmrcFontBox.right);
    kv("box.b", o->otmrcFontBox.bottom);
    kv("macasc", o->otmMacAscent);
    kv("macdesc", o->otmMacDescent);
    kv("macgap", o->otmMacLineGap);
    kv("minppem", o->otmusMinimumPPEM);
    nl();
    out(" OTM3");
    kv("subx", o->otmptSubscriptSize.x);
    kv("suby", o->otmptSubscriptSize.y);
    kv("subox", o->otmptSubscriptOffset.x);
    kv("suboy", o->otmptSubscriptOffset.y);
    kv("supx", o->otmptSuperscriptSize.x);
    kv("supy", o->otmptSuperscriptSize.y);
    kv("supox", o->otmptSuperscriptOffset.x);
    kv("supoy", o->otmptSuperscriptOffset.y);
    kv("strsize", o->otmsStrikeoutSize);
    kv("strpos", o->otmsStrikeoutPosition);
    kv("ulpos", o->otmsUnderscorePosition);
    kv("ulsize", o->otmsUnderscoreSize);
    nl();
    out(" OTM4 panose=");
    {
        BYTE FAR *p = (BYTE FAR *)&o->otmPanoseNumber;
        for (i = 0; i < 10; i++) {
            if (i)
                out(",");
            outl(p[i]);
        }
    }
    kv("filler", o->otmFiller);
    nl();
    out(" OTM5");
    ks("family", otmbuf + (WORD)o->otmpFamilyName);
    ks("face", otmbuf + (WORD)o->otmpFaceName);
    ks("style", otmbuf + (WORD)o->otmpStyleName);
    ks("full", otmbuf + (WORD)o->otmpFullName);
    nl();
    out(" OTMTM");
    tm_line(&o->otmTextMetrics);
    nl();
}

static int widths[256];

static void width_lines(const char *tag)
{
    int i, j;
    for (i = 0; i < 256; i++)
        widths[i] = -1;
    if (!GetCharWidth(hdc, 0, 255, widths)) {
        out(" GetCharWidth failed\r\n");
        return;
    }
    for (i = 0; i < 256; i += 16) {
        out(" ");
        out(tag);
        outl(i);
        out("=");
        for (j = 0; j < 16; j++) {
            if (j)
                out(",");
            outl(widths[i + j]);
        }
        nl();
    }
}

static ABC abcs[256];

static void abc_lines(int ppem)
{
    int i;
    if (!GetCharABCWidths(hdc, 0, 255, abcs)) {
        out(" GetCharABCWidths failed\r\n");
        return;
    }
    for (i = 0; i < 256; i++) {
        out(" abc");
        outl(ppem);
        out(" ");
        outl(i);
        out(" ");
        outl(abcs[i].abcA);
        out(" ");
        outl(abcs[i].abcB);
        out(" ");
        outl(abcs[i].abcC);
        nl();
    }
}

static void gm_lines(int ppem)
{
    static const char probe[] = "xHdpoOn";
    GLYPHMETRICS gm;
    MAT2 m;
    int i;
    DWORD r;
    zero(&m, sizeof m);
    m.eM11.value = 1;
    m.eM22.value = 1;
    for (i = 0; probe[i]; i++) {
        r = GetGlyphOutline(hdc, (BYTE)probe[i], GGO_METRICS, &gm, 0, NULL, &m);
        out(" gm");
        outl(ppem);
        out(" ");
        outl((BYTE)probe[i]);
        kv("ret", (long)r);
        kv("bx", gm.gmBlackBoxX);
        kv("by", gm.gmBlackBoxY);
        kv("ox", gm.gmptGlyphOrigin.x);
        kv("oy", gm.gmptGlyphOrigin.y);
        kv("incx", gm.gmCellIncX);
        kv("incy", gm.gmCellIncY);
        nl();
    }
}

static void kern_lines(void)
{
    int n = GetKerningPairs(hdc, 0, NULL), i;
    HGLOBAL g;
    KERNINGPAIR FAR *k;
    out(" KERN");
    kv("n", n);
    nl();
    if (n <= 0)
        return;
    g = GlobalAlloc(GMEM_MOVEABLE, (DWORD)n << 2)    /* sizeof(KERNINGPAIR) */;
    if (!g)
        return;
    k = (KERNINGPAIR FAR *)GlobalLock(g);
    i = GetKerningPairs(hdc, n, k);
    kv(" got", i);
    nl();
    for (n = 0; n < i; n++) {
        out(" kp ");
        outl(k[n].wFirst);
        out(" ");
        outl(k[n].wSecond);
        out(" ");
        outl(k[n].iKernAmount);
        nl();
    }
    GlobalUnlock(g);
    GlobalFree(g);
}

/* sizes whose ABC widths and black boxes are logged (at 2048 3.1's values
   are not the font's: GetCharABCWidths gives -96 1664 -89 for Arial's H) */
static const int abcsizes[] = { 128, 256, 512, 1024, 0 };

static void dump_face(const char *face, int wt, int it, int sizes)
{
    HFONT f, old;
    OUTLINETEXTMETRIC FAR *o = (OUTLINETEXTMETRIC FAR *)otmbuf;
    TEXTMETRIC t;
    char got[LF_FACESIZE + 1];
    int em = 0, i;

    out("FACE");
    ks("name", face);
    kv("wt", wt);
    kv("it", it);
    nl();
    f = mkfont(-20, face, wt, it);
    old = SelectObject(hdc, f);
    if (GetOutlineTextMetrics(hdc, sizeof otmbuf, o))
        em = o->otmEMSquare;
    SelectObject(hdc, old);
    DeleteObject(f);
    if (!em) {
        out(" not TrueType\r\n");
        return;
    }
    f = mkfont(-em, face, wt, it);
    old = SelectObject(hdc, f);
    got[0] = 0;
    GetTextFace(hdc, sizeof got, got);
    out(" ");
    ks("got", got);
    kv("em", em);
    GetTextMetrics(hdc, &t);
    tm_line(&t);
    nl();
    otm_lines();
    width_lines("w");
    abc_lines(em);
    gm_lines(em);
    kern_lines();
    SelectObject(hdc, old);
    DeleteObject(f);
    for (i = 0; abcsizes[i]; i++) {
        f = mkfont(-abcsizes[i], face, wt, it);
        old = SelectObject(hdc, f);
        abc_lines(abcsizes[i]);
        gm_lines(abcsizes[i]);
        SelectObject(hdc, old);
        DeleteObject(f);
    }
    if (!sizes)
        return;
    for (i = 6; i <= 255; i++) {
        f = mkfont(-i, face, wt, it);
        old = SelectObject(hdc, f);
        GetTextMetrics(hdc, &t);
        out(" SIZE");
        kv("ppem", i);
        tm_line(&t);
        nl();
        width_lines("s");
        SelectObject(hdc, old);
        DeleteObject(f);
    }
}

typedef struct {
    const char *face;
    const char *file;           /* w16's: the file's base name */
    int wt, it;
} facespec;

static const facespec msfaces[] = {
    { "Arial", 0, 400, 0 }, { "Arial", 0, 700, 0 }, { "Arial", 0, 400, 1 }, { "Arial", 0, 700, 1 },
    { "Times New Roman", 0, 400, 0 }, { "Times New Roman", 0, 700, 0 },
    { "Times New Roman", 0, 400, 1 }, { "Times New Roman", 0, 700, 1 },
    { "Courier New", 0, 400, 0 }, { "Courier New", 0, 700, 0 },
    { "Courier New", 0, 400, 1 }, { "Courier New", 0, 700, 1 },
    { 0 }
};

static const facespec ours[] = {
    { "w16 Sans", "W16SANS", 400, 0 }, { "w16 Sans", "W16SANBD", 700, 0 },
    { "w16 Sans", "W16SANIT", 400, 1 }, { "w16 Sans", "W16SANBI", 700, 1 },
    { "w16 Serif", "W16SERIF", 400, 0 }, { "w16 Serif", "W16SERBD", 700, 0 },
    { "w16 Serif", "W16SERIT", 400, 1 }, { "w16 Serif", "W16SERBI", 700, 1 },
    { "w16 Mono", "W16MONO", 400, 0 }, { "w16 Mono", "W16MONBD", 700, 0 },
    { "w16 Mono", "W16MONIT", 400, 1 }, { "w16 Mono", "W16MONBI", 700, 1 },
    { 0 }
};

static BOOL exists(const char *path)
{
    OFSTRUCT of;
    return OpenFile(path, &of, OF_EXIST) != HFILE_ERROR;
}

static void cat(char *d, const char *a, const char *b, const char *c)
{
    lstrcpy(d, a);
    lstrcat(d, b);
    lstrcat(d, c);
}

static void map_one(const char *ttf, const char *fot, const char *face)
{
    HFONT f, old;
    TEXTMETRIC t;
    char got[LF_FACESIZE + 1];
    static const int sz[] = { -2048, -20, -12, 0 };
    int i;
    if (!exists(ttf))
        return;
    out("MAP");
    ks("file", ttf);
    kv("csfr", CreateScalableFontResource(0, fot, ttf, NULL));
    kv("add", AddFontResource(fot));
    nl();
    for (i = 0; sz[i]; i++) {
        f = mkfont(sz[i], face, 400, 0);
        old = SelectObject(hdc, f);
        got[0] = 0;
        GetTextFace(hdc, sizeof got, got);
        out(" MAPSIZE");
        kv("lh", sz[i]);
        ks("got", got);
        GetTextMetrics(hdc, &t);
        tm_line(&t);
        nl();
        width_lines("w");
        SelectObject(hdc, old);
        DeleteObject(f);
    }
    RemoveFontResource(fot);
}

static void map_section(void)
{
    map_one("C:\\TTMAP.TTF", "C:\\TTMAP.FOT", "w16 Map Test");
    map_one("C:\\TTMAPA.TTF", "C:\\TTMAPA.FOT", "w16 Map A");
    map_one("C:\\TTMAPB.TTF", "C:\\TTMAPB.FOT", "w16 Map B");
}

static int fot_section(void)
{
    char ttf[160], fot[160], name[16];
    int i, n = 0;
    for (i = 0; ours[i].face; i++) {
        cat(ttf, sysdir, "\\", ours[i].file);
        lstrcat(ttf, ".TTF");
        if (!exists(ttf))
            continue;
        if (!n++)
            out("FOT\r\n");
        cat(name, ours[i].file, ".TTF", "");
        out(" ");
        out(name);
        cat(fot, sysdir, "\\", ours[i].file);
        lstrcat(fot, ".FOT");
        kv("portable", CreateScalableFontResource(0, fot, name, sysdir));
        cat(fot, "C:\\FOTB\\", ours[i].file, ".FOB");
        kv("fullpath", CreateScalableFontResource(0, fot, ttf, NULL));
        cat(fot, "C:\\FOTC\\", ours[i].file, ".FOC");
        kv("again", CreateScalableFontResource(0, fot, name, sysdir));
        cat(fot, sysdir, "\\", ours[i].file);
        lstrcat(fot, ".FOT");
        kv("add", AddFontResource(fot));
        nl();
    }
    return n;
}

static int CALLBACK fam_cb(const LOGFONT FAR *lf, const TEXTMETRIC FAR *tm, int type, LPARAM l)
{
    const ENUMLOGFONT FAR *e = (const ENUMLOGFONT FAR *)lf;
    out(" fam");
    ks("face", lf->lfFaceName);
    kv("type", type);
    if (type & TRUETYPE_FONTTYPE) {
        ks("full", e->elfFullName);
        ks("style", e->elfStyle);
    }
    kv("lh", lf->lfHeight);
    kv("lwt", lf->lfWeight);
    kv("lit", lf->lfItalic);
    kv("lpf", lf->lfPitchAndFamily);
    tm_line(tm);
    nl();
    (void)l;
    return 1;
}

static void families(void)
{
    FARPROC p = MakeProcInstance((FARPROC)fam_cb, (HINSTANCE)init_regs[I_DI]);
    static const char *const fams[] = { "w16 Sans", "w16 Serif", "w16 Mono", 0 };
    int i;
    out("FAMILIES\r\n");
    EnumFontFamilies(hdc, NULL, (FONTENUMPROC)p, 0);
    for (i = 0; fams[i]; i++) {
        out("FAMILY");
        ks("name", fams[i]);
        nl();
        EnumFontFamilies(hdc, fams[i], (FONTENUMPROC)p, 0);
    }
    FreeProcInstance(p);
}

/* ---- show mode (command tail "show N"): a window setting the same text
   in Microsoft's face (left) and w16's (right), for screenshots (SNAP);
   N 1 Arial / w16 Sans, 2 Times New Roman / w16 Serif, 3 Courier New /
   w16 Mono. w16's fonts are made and added first (as in the FOT section).
   The window fills the screen and closes on WM_CLOSE or after 60 s. */

static int showpage;

static const char sample[] =
    "Windows 3.1 sets this paragraph in TrueType. The quick brown fox "
    "jumps over the lazy dog: 0123456789 (+-*/=) \"quotes\" & 'marks'. "
    "Zw\xf6lf Boxk\xe4mpfer jagen Viktor quer \xfc" "ber den gro\xdf" "en Deich; "
    "\xe9t\xe9, gar\xe7on, \xc0 bient\xf4t, \xa3" "5, \xa9 1992.";

static void show_col(HDC dc, const char *face, int x, int w, int top, int bottom)
{
    static const int pts[] = { 10, 12, 14, 18, 0 };
    static const int wts[] = { 400, 700, 400, 700 };
    static const int its[] = { 0, 0, 1, 1 };
    int y = top, i;
    RECT r;
    HFONT f, old;
    SetBkMode(dc, TRANSPARENT);
    f = mkfont(-16, "System", 700, 0);
    old = SelectObject(dc, f);
    TextOut(dc, x, y, face, lstrlen(face));
    SelectObject(dc, old);
    DeleteObject(f);
    y += 20;
    for (i = 0; pts[i] && y < bottom; i++) {
        int k = i < 4 ? i : 0;
        f = mkfont(-MulDiv(pts[i], 96, 72), face, wts[k], its[k]);
        old = SelectObject(dc, f);
        SetRect(&r, x, y, x + w, bottom);
        y += DrawText(dc, sample, -1, &r, DT_WORDBREAK | DT_NOPREFIX) + 6;
        SelectObject(dc, old);
        DeleteObject(f);
    }
}

LRESULT CALLBACK _export show_proc(HWND h, UINT msg, WPARAM wp, LPARAM lp)
{
    static const char *const ms[] = { "Arial", "Times New Roman", "Courier New" };
    static const char *const w16[] = { "w16 Sans", "w16 Serif", "w16 Mono" };
    if (msg == WM_PAINT) {
        PAINTSTRUCT ps;
        RECT rc;
        HDC dc = BeginPaint(h, &ps);
        int half;
        GetClientRect(h, &rc);
        half = rc.right / 2;
        show_col(dc, ms[showpage], 6, half - 12, 4, rc.bottom);
        show_col(dc, w16[showpage], half + 6, half - 12, 4, rc.bottom);
        EndPaint(h, &ps);
        return 0;
    }
    if (msg == WM_TIMER || msg == WM_CLOSE) {
        DestroyWindow(h);
        return 0;
    }
    if (msg == WM_DESTROY) {
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProc(h, msg, wp, lp);
}

static int show(int page)
{
    HINSTANCE hi = (HINSTANCE)init_regs[I_DI];
    WNDCLASS wc;
    HWND h;
    MSG m;
    showpage = page;
    fot_section();
    wc.style = CS_HREDRAW | CS_VREDRAW;
    wc.lpfnWndProc = show_proc;
    wc.cbClsExtra = wc.cbWndExtra = 0;
    wc.hInstance = hi;
    wc.hIcon = 0;
    wc.hCursor = LoadCursor(0, IDC_ARROW);
    wc.hbrBackground = GetStockObject(WHITE_BRUSH);
    wc.lpszMenuName = 0;
    wc.lpszClassName = "W16TTShow";
    RegisterClass(&wc);
    h = CreateWindow("W16TTShow", "FN4: Microsoft's face | w16's", WS_OVERLAPPEDWINDOW | WS_MAXIMIZE,
                     0, 0, 640, 480, 0, 0, hi, 0);
    ShowWindow(h, SW_SHOWMAXIMIZED);
    UpdateWindow(h);
    SetTimer(h, 1, 60000, 0);
    while (GetMessage(&m, 0, 0, 0)) {
        TranslateMessage(&m);
        DispatchMessage(&m);
    }
    return 0;
}

int __cdecl tmain(void)
{
    RASTERIZER_STATUS rs;
    int i;
    LPSTR tail = (LPSTR)MAKELP(init_regs[6], init_regs[1]);   /* ES:BX */

    WaitEvent(0);
    InitApp((HINSTANCE)init_regs[I_DI]);
    hdc = GetDC(NULL);
    GetSystemDirectory(sysdir, sizeof sysdir);
    while (*tail == ' ')
        tail++;
    if (tail[0] == 's' && tail[1] == 'h' && tail[2] == 'o' && tail[3] == 'w') {
        int page = 0;
        tail += 4;
        while (*tail == ' ')
            tail++;
        if (*tail >= '1' && *tail <= '3')
            page = *tail - '1';
        out("FN4 show\r\n");
        i = show(page);
        ReleaseDC(NULL, hdc);
        return i;
    }
    out("FN4 TrueType probe\r\n");
    zero(&rs, sizeof rs);
    out("RAST");
    kv("ret", GetRasterizerCaps(&rs, sizeof rs));
    kv("size", rs.nSize);
    kv("flags", rs.wFlags);
    kv("lang", rs.nLanguageID);
    nl();

    if (!exists("C:\\FN4NOREF"))
        for (i = 0; msfaces[i].face; i++)
            dump_face(msfaces[i].face, msfaces[i].wt, msfaces[i].it, 1);
    map_section();
    if (fot_section()) {
        /* C:\FN4SET1, 2 or 3: only w16 Sans, Serif or Mono (3.1 did not
           live through all twelve faces in one session; tests/ttfonts.sh
           runs the three families apart) */
        int set = exists("C:\\FN4SET1") ? 1 : exists("C:\\FN4SET2") ? 2 : exists("C:\\FN4SET3") ? 3 : 0;
        for (i = 0; ours[i].face; i++)
            if (!set || i / 4 == set - 1)
                dump_face(ours[i].face, ours[i].wt, ours[i].it, !exists("C:\\FN4QUICK"));
    }
    families();

    ReleaseDC(NULL, hdc);
    out("FN4 done\r\n");
    return 0;
}
