/*
 * gd16.c - font substitution probe GD16 (ARCHITECTURE.md 11.12): how a
 * WIN.INI [FontSubstitutes] name whose font is TrueType answers, as a
 * program checking for a face sees it. tests/gd16.sh sets the install
 * up (the Liberation fonts installed as w16's tree has them, Arial,
 * Times New Roman and Courier New substituted to them, Microsoft's
 * own TrueType fonts there or not) and runs it as the shell, on 3.1's
 * GDI or w16's; the two logs must be the same.
 *   F    EnumFontFamilies(NULL) on the screen: the families (face:type)
 *   E    for each face asked: EnumFontFamilies and EnumFonts with the
 *        face, each entry's LOGFONT face, height, weight, italic, pitch
 *        and family, type, and (families) ENUMLOGFONT's full name and
 *        style
 *   C    CreateFont of the face at a few heights, weights and italic:
 *        GetTextFace, GetObject's face, TEXTMETRIC and an extent
 * Black-box: documented calls only.
 */
#include <windows.h>

extern WORD __cdecl init_regs[7];
extern void __cdecl log_write(const char FAR *buf, WORD len);
extern WORD FAR PASCAL WaitEvent(HTASK);
extern BOOL FAR PASCAL InitApp(HINSTANCE);
#define I_DI 5

static char buf[2048];
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
    char b[14];
    int i = 13;
    int neg = v < 0;
    DWORD u = (DWORD)(neg ? -v : v);
    b[i] = 0;
    do {
        b[--i] = (char)('0' + (int)(u % 10));
        u /= 10;
    } while (u);
    if (neg)
        b[--i] = '-';
    out(b + i);
}

static void kv(const char *k, long v)
{
    out(" ");
    out(k);
    out("=");
    outn(v);
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
    flush();
}

static int brief, n;

static void entry(const LOGFONT FAR *lf, const char FAR *full, const char FAR *style, int type)
{
    n++;
    if (brief) {
        out(" ");
        out(lf->lfFaceName);
        out(":");
        outn(type);
        return;
    }
    out("  ");
    ks("face", lf->lfFaceName);
    kv("type", type);
    kv("h", lf->lfHeight);
    kv("w", lf->lfWidth);
    kv("wt", lf->lfWeight);
    kv("it", lf->lfItalic);
    kv("cs", lf->lfCharSet);
    kv("pf", lf->lfPitchAndFamily);
    if (full) {
        ks("full", full);
        ks("style", style);
    }
    nl();
}

int FAR PASCAL __export fam_cb(const ENUMLOGFONT FAR *elf, const NEWTEXTMETRIC FAR *tm,
                               int type, LPARAM data)
{
    (void)tm;
    (void)data;
    entry(&elf->elfLogFont, (type & TRUETYPE_FONTTYPE) ? (const char FAR *)elf->elfFullName : 0,
          (const char FAR *)elf->elfStyle, type);
    return 1;
}

int FAR PASCAL __export font_cb(const LOGFONT FAR *lf, const TEXTMETRIC FAR *tm,
                                int type, LPARAM data)
{
    (void)tm;
    (void)data;
    entry(lf, 0, 0, type);
    return 1;
}

static FARPROC pfam, pfont;
static HDC scr;

static void create(const char *face, int h, int wt, int it)
{
    HFONT f = CreateFont(h, 0, 0, 0, wt, (BYTE)it, 0, 0, ANSI_CHARSET, 0, 0, 0, 0, face);
    HFONT old;
    LOGFONT lf;
    TEXTMETRIC t;
    char got[LF_FACESIZE + 8];
    int r;
    DWORD ext;
    old = SelectObject(scr, f);
    got[0] = 0;
    r = GetTextFace(scr, sizeof got, got);
    GetTextMetrics(scr, &t);
    ext = GetTextExtent(scr, "Cue Cards uses Arial", 20);
    lf.lfFaceName[0] = 0;
    GetObject(f, sizeof lf, &lf);
    out("C");
    ks("ask", face);
    kv("h", h);
    kv("wt", wt);
    kv("it", it);
    ks("textface", got);
    kv("r", r);
    ks("obj", lf.lfFaceName);
    kv("H", t.tmHeight);
    kv("A", t.tmAscent);
    kv("D", t.tmDescent);
    kv("IL", t.tmInternalLeading);
    kv("EL", t.tmExternalLeading);
    kv("AW", t.tmAveCharWidth);
    kv("MW", t.tmMaxCharWidth);
    kv("WT", t.tmWeight);
    kv("IT", t.tmItalic);
    kv("PF", t.tmPitchAndFamily);
    kv("CS", t.tmCharSet);
    kv("ext", LOWORD(ext));
    nl();
    SelectObject(scr, old);
    DeleteObject(f);
}

int __cdecl tmain(void)
{
    static const char *faces[] = {
        "Arial", "arial", "Times New Roman", "Courier New", "Helvetica", "Times",
        "Liberation Sans", "Liberation Mono", "Helv"
    };
    static const int hs[] = { -12, -16, 20, -27 };
    int i, k, r;
    WaitEvent(0);
    InitApp((HINSTANCE)init_regs[I_DI]);
    pfam = MakeProcInstance((FARPROC)fam_cb, (HINSTANCE)init_regs[I_DI]);
    pfont = MakeProcInstance((FARPROC)font_cb, (HINSTANCE)init_regs[I_DI]);
    out("GD16 start\r\n");
    scr = GetDC(NULL);
    brief = 1;
    n = 0;
    out("F");
    r = EnumFontFamilies(scr, NULL, (FONTENUMPROC)pfam, 0);
    kv("r", r);
    kv("n", n);
    nl();
    brief = 0;
    for (i = 0; i < (int)(sizeof faces / sizeof faces[0]); i++) {
        n = 0;
        out("E fam");
        ks("ask", faces[i]);
        nl();
        r = EnumFontFamilies(scr, faces[i], (FONTENUMPROC)pfam, 0);
        out("E fam");
        kv("r", r);
        kv("n", n);
        nl();
        n = 0;
        out("E fonts");
        ks("ask", faces[i]);
        nl();
        r = EnumFonts(scr, faces[i], (OLDFONTENUMPROC)pfont, 0);
        out("E fonts");
        kv("r", r);
        kv("n", n);
        nl();
    }
    for (i = 0; i < (int)(sizeof faces / sizeof faces[0]); i++)
        for (k = 0; k < (int)(sizeof hs / sizeof hs[0]); k++)
            create(faces[i], hs[k], k == 2 ? FW_BOLD : FW_NORMAL, k == 3);
    ReleaseDC(NULL, scr);
    FreeProcInstance(pfam);
    FreeProcInstance(pfont);
    out("GD16 done\r\n");
    flush();
    return 0;
}
