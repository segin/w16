/*
 * fn1.c - the font metrics probe (ARCHITECTURE.md 6.9).
 *
 * Runs as the shell (tests/refrun.sh, tests/fonts.sh) and writes to TK.LOG
 * everything a program can observe about the raster fonts, so that w16's
 * own fonts can be checked against Windows 3.1's black-box: run it once
 * with the original fonts and once with w16's, and the two logs must be
 * identical. Only documented GDI/USER calls are used; no font file is
 * read.
 *
 * Per font (stock objects, and CreateFont requests by face and height):
 * the realised face name (GetTextFace), every TEXTMETRIC field, the
 * aspect-ratio filter, GetCharWidth for codes 0-255, GetTextExtent of
 * test strings. Then EnumFonts and EnumFontFamilies (LOGFONT and
 * TEXTMETRIC of each font, ENUMLOGFONT names, NEWTEXTMETRIC extras), and
 * how a grid of CreateFont requests (heights, widths, weights, pitch,
 * family, character set, faces) is mapped. Also GetDialogBaseUnits and
 * the system metrics that follow the System font.
 *
 * Output format: one item per line,
 *   FONT <label>                      a font follows
 *   face=<name> h=.. a=.. d=.. il=.. el=.. aw=.. mw=.. wt=.. it=.. ul=..
 *     so=.. fc=.. lc=.. dc=.. bc=.. pf=.. cs=.. oh=.. ax=.. ay=..
 *   w<row>=<16 widths>                GetCharWidth, 16 codes per row
 *   ext "<text>"=<cx>,<cy>
 *   ENUM / FAM lines, MAP lines
 * All numbers decimal.
 */
#include <windows.h>

extern WORD __cdecl init_regs[7];      /* ax bx cx dx si di es */
extern void __cdecl log_write(const char FAR *buf, WORD len);
extern WORD FAR PASCAL WaitEvent(HTASK);
extern BOOL FAR PASCAL InitApp(HINSTANCE);

#define I_DI 5

static void out(const char FAR *s)
{
    WORD n = 0;
    while (s[n])
        n++;
    log_write(s, n);
}

static void outn(long v)
{
    char b[12];
    int i = 11;
    int neg = v < 0;
    WORD u = (WORD)(neg ? -v : v);    /* every value printed fits 16 bits */
    b[i] = 0;
    do {
        b[--i] = (char)('0' + u % 10);
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

static void lf_line(const LOGFONT FAR *l)
{
    out(" face=\"");
    out(l->lfFaceName);
    out("\"");
    kv("lh", l->lfHeight);
    kv("lw", l->lfWidth);
    kv("esc", l->lfEscapement);
    kv("or", l->lfOrientation);
    kv("lwt", l->lfWeight);
    kv("lit", l->lfItalic);
    kv("lul", l->lfUnderline);
    kv("lso", l->lfStrikeOut);
    kv("lcs", l->lfCharSet);
    kv("op", l->lfOutPrecision);
    kv("cp", l->lfClipPrecision);
    kv("q", l->lfQuality);
    kv("lpf", l->lfPitchAndFamily);
}

static const char *const texts[] = {
    "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz",
    "The quick brown fox jumps over the lazy dog.",
    "Program Manager",
    "&File  &Options  &Window  &Help",
    "0123456789 !\"#$%&'()*+,-./:;<=>?@[\\]^_`{|}~",
    " ",
    "",
    0
};

static HDC hdc;

/* everything about the font selected into hdc */
static void dump_selected(const char *label)
{
    TEXTMETRIC t;
    char face[LF_FACESIZE + 1];
    int w[256];
    int i, j;
    DWORD e;

    out("FONT ");
    out(label);
    nl();
    face[0] = 0;
    GetTextFace(hdc, sizeof face, face);
    GetTextMetrics(hdc, &t);
    out(" face=\"");
    out(face);
    out("\"");
    tm_line(&t);
    e = GetAspectRatioFilter(hdc);
    kv("arx", LOWORD(e));
    kv("ary", HIWORD(e));
    nl();
    for (i = 0; i < 256; i++)
        w[i] = -1;
    if (!GetCharWidth(hdc, 0, 255, w))
        out(" GetCharWidth failed\r\n");
    for (i = 0; i < 256; i += 16) {
        out(" w");
        outn(i);
        out("=");
        for (j = 0; j < 16; j++) {
            if (j)
                out(",");
            outn(w[i + j]);
        }
        nl();
    }
    for (i = 0; texts[i]; i++) {
        WORD n = 0;
        while (texts[i][n])
            n++;
        e = GetTextExtent(hdc, texts[i], n);
        out(" ext \"");
        out(texts[i]);
        out("\"=");
        outn(LOWORD(e));
        out(",");
        outn(HIWORD(e));
        nl();
    }
}

static void dump_font(const char *label, HFONT f)
{
    HFONT old;
    LOGFONT lf;
    if (!f) {
        out("FONT ");
        out(label);
        out(" none\r\n");
        return;
    }
    old = SelectObject(hdc, f);
    dump_selected(label);
    if (GetObject(f, sizeof lf, &lf)) {
        out(" LOGFONT");
        lf_line(&lf);
        nl();
    }
    SelectObject(hdc, old);
}

static HFONT mk(int h, int w, int wt, BYTE cs, BYTE pf, const char *face)
{
    return CreateFont(h, w, 0, 0, wt, 0, 0, 0, cs, OUT_DEFAULT_PRECIS,
                      CLIP_DEFAULT_PRECIS, DEFAULT_QUALITY, pf, face);
}

static void request(const char *face, int h)
{
    HFONT f = mk(h, 0, 0, DEFAULT_CHARSET, DEFAULT_PITCH, face);
    char label[64];
    char *p = label;
    const char *s = face;
    while (*s)
        *p++ = *s++;
    *p++ = ' ';
    if (h < 0) {
        *p++ = '-';
        h = -h;
    }
    if (h >= 10)
        *p++ = (char)('0' + h / 10);
    *p++ = (char)('0' + h % 10);
    *p = 0;
    dump_font(label, f);
    DeleteObject(f);
}

/* ---- enumeration ---- */
/* The faces are collected first and each is enumerated afterwards (GDI is
   not asked to enumerate from inside its own callback). */
#define MAXFACES 48
static char facelist[MAXFACES][LF_FACESIZE];
static int nfaces;

static void keep_face(const char FAR *f)
{
    int i;
    if (nfaces >= MAXFACES)
        return;
    for (i = 0; i < LF_FACESIZE - 1 && f[i]; i++)
        facelist[nfaces][i] = f[i];
    facelist[nfaces][i] = 0;
    nfaces++;
}

int FAR PASCAL __export enum_font(const LOGFONT FAR *lf, const TEXTMETRIC FAR *tm,
                                           int type, LPARAM data)
{
    out(data ? "ENUM" : "ENUMFACE");
    kv("type", type);
    lf_line(lf);
    nl();
    out(" ");
    tm_line(tm);
    nl();
    if (!data)
        keep_face(lf->lfFaceName);
    return 1;
}

int FAR PASCAL __export enum_fam(const ENUMLOGFONT FAR *elf,
                                          const NEWTEXTMETRIC FAR *tm, int type, LPARAM data)
{
    out(data ? "FAM" : "FAMILY");
    kv("type", type);
    lf_line(&elf->elfLogFont);
    if (type & TRUETYPE_FONTTYPE) {
        /* (for raster fonts these are not filled in: whatever memory held) */
        out(" full=\"");
        out(elf->elfFullName);
        out("\" style=\"");
        out(elf->elfStyle);
        out("\"");
    }
    nl();
    out(" ");
    tm_line((const TEXTMETRIC FAR *)tm);
    if (type & TRUETYPE_FONTTYPE) {
        kv("ntmf", (long)tm->ntmFlags);
        kv("em", tm->ntmSizeEM);
        kv("ch", tm->ntmCellHeight);
        kv("avg", tm->ntmAvgWidth);
    }
    nl();
    if (!data)
        keep_face(elf->elfLogFont.lfFaceName);
    return 1;
}

/* ---- the CreateFont mapping grid: realised face, height, width ---- */
static void map_one(int h, int w, int wt, BYTE cs, BYTE pf, const char *face)
{
    HFONT f = mk(h, w, wt, cs, pf, face), old;
    TEXTMETRIC t;
    char got[LF_FACESIZE + 1];
    out("MAP \"");
    out(face);
    out("\"");
    kv("h", h);
    kv("w", w);
    kv("wt", wt);
    kv("cs", cs);
    kv("pf", pf);
    out(" ->");
    old = SelectObject(hdc, f);
    got[0] = 0;
    GetTextFace(hdc, sizeof got, got);
    GetTextMetrics(hdc, &t);
    out(" \"");
    out(got);
    out("\"");
    kv("H", t.tmHeight);
    kv("A", t.tmAscent);
    kv("AW", t.tmAveCharWidth);
    kv("MW", t.tmMaxCharWidth);
    kv("WT", t.tmWeight);
    kv("IT", t.tmItalic);
    kv("CS", t.tmCharSet);
    kv("PF", t.tmPitchAndFamily);
    kv("OH", t.tmOverhang);
    nl();
    SelectObject(hdc, old);
    DeleteObject(f);
}

static const char *const faces[] = {
    "", "System", "Fixedsys", "Terminal", "MS Sans Serif", "MS Serif",
    "Courier", "Small Fonts", "Helv", "Tms Rmn", "Arial", "Courier New",
    "Nonexistent", 0
};

int __cdecl tmain(void)
{
    int i, h;
    char buf[8];

    WaitEvent(0);
    InitApp((HINSTANCE)init_regs[I_DI]);
    hdc = GetDC(NULL);
    out("FN1 font probe\r\n");
    kv("dbux", LOWORD(GetDialogBaseUnits()));
    kv("dbuy", HIWORD(GetDialogBaseUnits()));
    kv("cycaption", GetSystemMetrics(SM_CYCAPTION));
    kv("cymenu", GetSystemMetrics(SM_CYMENU));
    kv("logpx", GetDeviceCaps(hdc, LOGPIXELSX));
    kv("logpy", GetDeviceCaps(hdc, LOGPIXELSY));
    kv("aspx", GetDeviceCaps(hdc, ASPECTX));
    kv("aspy", GetDeviceCaps(hdc, ASPECTY));
    kv("aspxy", GetDeviceCaps(hdc, ASPECTXY));
    kv("textcaps", GetDeviceCaps(hdc, TEXTCAPS));
    kv("extra", GetTextCharacterExtra(hdc));
    nl();

    dump_selected("dc-default");
    dump_font("SYSTEM_FONT", GetStockObject(SYSTEM_FONT));
    dump_font("SYSTEM_FIXED_FONT", GetStockObject(SYSTEM_FIXED_FONT));
    dump_font("OEM_FIXED_FONT", GetStockObject(OEM_FIXED_FONT));
    dump_font("ANSI_FIXED_FONT", GetStockObject(ANSI_FIXED_FONT));
    dump_font("ANSI_VAR_FONT", GetStockObject(ANSI_VAR_FONT));
    dump_font("DEVICE_DEFAULT_FONT", GetStockObject(DEVICE_DEFAULT_FONT));

    request("System", 0);
    request("System", 16);
    request("Fixedsys", 0);
    request("Fixedsys", 15);
    request("Terminal", 0);
    request("Terminal", 12);
    request("MS Sans Serif", 13);
    request("MS Sans Serif", -8);
    request("MS Sans Serif", -10);
    request("MS Sans Serif", -11);
    request("MS Sans Serif", -13);
    request("MS Sans Serif", 16);
    request("MS Sans Serif", -16);
    request("MS Sans Serif", 20);
    request("MS Sans Serif", -19);
    request("MS Sans Serif", 24);
    request("MS Sans Serif", 29);
    request("MS Sans Serif", -24);
    request("MS Sans Serif", 37);
    request("MS Sans Serif", -32);
    /* a bold System request, and System via the system-font paths */
    {
        HFONT f = mk(16, 0, FW_NORMAL, ANSI_CHARSET, VARIABLE_PITCH | FF_SWISS, "System");
        dump_font("System normal swiss", f);
        DeleteObject(f);
        f = mk(0, 0, 0, OEM_CHARSET, FIXED_PITCH | FF_MODERN, "");
        dump_font("oem fixed modern", f);
        DeleteObject(f);
        f = mk(0, 0, 0, ANSI_CHARSET, FIXED_PITCH, "");
        dump_font("ansi fixed", f);
        DeleteObject(f);
    }

    {
        /* callbacks through MakeProcInstance thunks (DS = our data) */
        FARPROC pf = MakeProcInstance((FARPROC)enum_font, (HINSTANCE)init_regs[I_DI]);
        FARPROC pm = MakeProcInstance((FARPROC)enum_fam, (HINSTANCE)init_regs[I_DI]);
        out("ENUMFONTS\r\n");
        nfaces = 0;
        EnumFonts(hdc, NULL, (OLDFONTENUMPROC)pf, (LPSTR)0);
        for (i = 0, h = nfaces; i < h; i++)
            EnumFonts(hdc, facelist[i], (OLDFONTENUMPROC)pf, (LPSTR)1L);
        out("ENUMFONTFAMILIES\r\n");
        nfaces = 0;
        EnumFontFamilies(hdc, NULL, (FONTENUMPROC)pm, (LPSTR)0);
        for (i = 0, h = nfaces; i < h; i++)
            EnumFontFamilies(hdc, facelist[i], (FONTENUMPROC)pm, (LPSTR)1L);
        FreeProcInstance(pf);
        FreeProcInstance(pm);
    }
    out("MAPPING\r\n");

    for (i = 0; faces[i]; i++) {
        for (h = -40; h <= 40; h += (h >= -20 && h < 20) ? 1 : 4)
            map_one(h, 0, 0, DEFAULT_CHARSET, DEFAULT_PITCH, faces[i]);
        map_one(0, 0, FW_BOLD, DEFAULT_CHARSET, DEFAULT_PITCH, faces[i]);
        map_one(0, 0, FW_NORMAL, ANSI_CHARSET, DEFAULT_PITCH, faces[i]);
        map_one(0, 0, 0, OEM_CHARSET, DEFAULT_PITCH, faces[i]);
        map_one(0, 0, 0, SYMBOL_CHARSET, DEFAULT_PITCH, faces[i]);
        map_one(0, 0, 0, ANSI_CHARSET, FIXED_PITCH, faces[i]);
        map_one(0, 0, 0, ANSI_CHARSET, VARIABLE_PITCH, faces[i]);
        map_one(16, 4, 0, DEFAULT_CHARSET, DEFAULT_PITCH, faces[i]);
        map_one(16, 12, 0, DEFAULT_CHARSET, DEFAULT_PITCH, faces[i]);
        map_one(-8, 0, FW_BOLD, DEFAULT_CHARSET, DEFAULT_PITCH, faces[i]);
    }
    for (i = 0; i < 6; i++) {
        static const BYTE fam[] = { FF_DONTCARE, FF_ROMAN, FF_SWISS, FF_MODERN, FF_SCRIPT, FF_DECORATIVE };
        for (h = 0; h <= 24; h += 6) {
            map_one(h, 0, 0, DEFAULT_CHARSET, fam[i] | VARIABLE_PITCH, "");
            map_one(h, 0, 0, DEFAULT_CHARSET, fam[i] | FIXED_PITCH, "");
            map_one(h, 0, 0, OEM_CHARSET, fam[i], "");
        }
    }
    (void)buf;
    ReleaseDC(NULL, hdc);
    out("FN1 done\r\n");
    return 0;
}
