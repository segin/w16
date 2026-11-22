/*
 * fn3.c - the font probe for the rest of the VGA raster set
 * (ARCHITECTURE.md 6.9): Courier, MS Serif, Small Fonts and Symbol
 * (COURE/SERIFE/SMALLE/SYMBOLE.FON, which WIN.INI's [fonts] loads), the
 * DOS-box fonts (DOSAPP.FON and the grabber's EGA80WOA/EGA40WOA/CGA80WOA/
 * CGA40WOA.FON, which only WINOLDAP loads) and the files' module facts.
 *
 * Runs as the shell (tests/refrun.sh, tests/fonts.sh) and writes to TK.LOG.
 * FN1's companion: it uses only documented GDI/KERNEL/TOOLHELP calls; no
 * font file is opened or read (a .FON is loaded as a library and only
 * asked for its module name and which resources it has, by ID and size).
 *
 * Sections:
 *   FACE <name>: every font EnumFonts gives for the face, each realised
 *     again with CreateFontIndirect from its LOGFONT and dumped as in FN1
 *     (TEXTMETRIC, GetCharWidth 0-255, extents); then a CreateFont mapping
 *     grid for the face (MAP lines as FN1's)
 *   FILE <name>: AddFontResource's count, the fonts it added (every face
 *     enumerated before and after; a font not in the earlier list, or one
 *     more of a kind, is "new"), each dumped; RemoveFontResource
 *   DOSMAP: with all five DOS-box files added, Terminal/OEM requests by
 *     cell (WINOLDAP's CreateFont(h, w, ..., OEM_CHARSET, FIXED_PITCH |
 *     FF_MODERN, "Terminal")) and by height
 *   MODULE <file>: LoadLibrary, TOOLHELP's module name, every RT_FONT
 *     resource ID 1-1023 with SizeofResource, FONTDIR's and RT_VERSION's
 *     sizes
 */
#include <windows.h>
#include <toolhelp.h>

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
    "0123456789 !\"#$%&'()*+,-./:;<=>?@[\\]^_`{|}~",
    " ",
    0
};

static HDC hdc;

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

static void dump_lf(const char *label, const LOGFONT FAR *lf)
{
    LOGFONT l = *lf;
    HFONT f = CreateFontIndirect(&l), old;
    if (!f) {
        out("FONT ");
        out(label);
        out(" none\r\n");
        return;
    }
    old = SelectObject(hdc, f);
    dump_selected(label);
    SelectObject(hdc, old);
    DeleteObject(f);
}

static HFONT mk(int h, int w, int wt, BYTE cs, BYTE pf, const char *face)
{
    return CreateFont(h, w, 0, 0, wt, 0, 0, 0, cs, OUT_DEFAULT_PRECIS,
                      CLIP_DEFAULT_PRECIS, DEFAULT_QUALITY, pf, face);
}

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

/* ---- enumeration into a list ---- */
typedef struct {
    LOGFONT lf;
    TEXTMETRIC tm;
    int type;
} EF;

#define MAXF 120
static EF far *cur;                    /* the list being filled */
static int ncur;
static EF base[MAXF], now[MAXF];
static int nbase, nnow;

#define MAXFACES 48
static char facelist[MAXFACES][LF_FACESIZE];
static int nfaces;

int FAR PASCAL __export enum_face(const LOGFONT FAR *lf, const TEXTMETRIC FAR *tm,
                                  int type, LPARAM data)
{
    int i;
    (void)tm; (void)type; (void)data;
    if (nfaces >= MAXFACES)
        return 1;
    for (i = 0; i < LF_FACESIZE - 1 && lf->lfFaceName[i]; i++)
        facelist[nfaces][i] = lf->lfFaceName[i];
    facelist[nfaces][i] = 0;
    nfaces++;
    return 1;
}

int FAR PASCAL __export enum_font(const LOGFONT FAR *lf, const TEXTMETRIC FAR *tm,
                                  int type, LPARAM data)
{
    (void)data;
    if (ncur >= MAXF)
        return 1;
    cur[ncur].lf = *lf;
    cur[ncur].tm = *tm;
    cur[ncur].type = type;
    ncur++;
    return 1;
}

static FARPROC pface, pfont;

/* every font of every face, in enumeration order */
static int enum_all(EF far *list)
{
    int i;
    nfaces = 0;
    EnumFonts(hdc, NULL, (OLDFONTENUMPROC)pface, 0);
    cur = list;
    ncur = 0;
    for (i = 0; i < nfaces; i++)
        EnumFonts(hdc, facelist[i], (OLDFONTENUMPROC)pfont, 0);
    return ncur;
}

static int enum_face_list(const char *face, EF far *list)
{
    cur = list;
    ncur = 0;
    EnumFonts(hdc, face, (OLDFONTENUMPROC)pfont, 0);
    return ncur;
}

/* the same font: LOGFONT up to the face name, the face name itself (the
   bytes after its terminator are whatever memory held), TEXTMETRIC, type */
static BOOL same(const EF far *a, const EF far *b)
{
    const BYTE far *p = (const BYTE far *)&a->lf;
    const BYTE far *q = (const BYTE far *)&b->lf;
    int i;
    for (i = 0; i < (int)((BYTE *)&a->lf.lfFaceName - (BYTE *)&a->lf); i++)
        if (p[i] != q[i])
            return FALSE;
    if (lstrcmp(a->lf.lfFaceName, b->lf.lfFaceName))
        return FALSE;
    p = (const BYTE far *)&a->tm;
    q = (const BYTE far *)&b->tm;
    for (i = 0; i < (int)sizeof(TEXTMETRIC); i++)
        if (p[i] != q[i])
            return FALSE;
    return a->type == b->type;
}

static void ef_lines(const char *tag, const EF far *e)
{
    out(tag);
    kv("type", e->type);
    lf_line(&e->lf);
    nl();
    out(" ");
    tm_line(&e->tm);
    nl();
}

static void label_of(char *buf, const EF far *e)
{
    char *p = buf;
    const char far *s = e->lf.lfFaceName;
    int v, k;
    while (*s)
        *p++ = *s++;
    for (k = 0; k < 2; k++) {
        v = k ? e->tm.tmAveCharWidth : e->tm.tmHeight;
        *p++ = k ? 'x' : ' ';
        if (v >= 10)
            *p++ = (char)('0' + v / 10);
        *p++ = (char)('0' + v % 10);
    }
    *p = 0;
}

static void face_section(const char *face, BOOL grid)
{
    int i, n, h;
    char label[64];
    out("FACE ");
    out(face);
    nl();
    n = enum_face_list(face, now);
    for (i = 0; i < n; i++)
        ef_lines("ENUM", &now[i]);
    for (i = 0; i < n; i++) {
        label_of(label, &now[i]);
        dump_lf(label, &now[i].lf);
    }
    if (!grid)
        return;
    for (h = -40; h <= 40; h += (h >= -20 && h < 20) ? 1 : 4)
        map_one(h, 0, 0, DEFAULT_CHARSET, DEFAULT_PITCH, face);
    map_one(0, 0, FW_BOLD, DEFAULT_CHARSET, DEFAULT_PITCH, face);
    map_one(0, 0, 0, ANSI_CHARSET, DEFAULT_PITCH, face);
    map_one(0, 0, 0, OEM_CHARSET, DEFAULT_PITCH, face);
    map_one(0, 0, 0, SYMBOL_CHARSET, DEFAULT_PITCH, face);
    map_one(0, 0, 0, SYMBOL_CHARSET, VARIABLE_PITCH | FF_DECORATIVE, face);
    map_one(0, 0, 0, ANSI_CHARSET, FIXED_PITCH, face);
    map_one(-11, 0, 0, DEFAULT_CHARSET, DEFAULT_PITCH, face);
    map_one(13, 8, 0, DEFAULT_CHARSET, DEFAULT_PITCH, face);
    map_one(16, 4, 0, DEFAULT_CHARSET, DEFAULT_PITCH, face);
}

static char sysdir[128];

static void path_of(char *buf, const char *file)
{
    char *p = buf;
    const char *s = sysdir;
    while (*s)
        *p++ = *s++;
    if (p > buf && p[-1] != '\\')
        *p++ = '\\';
    s = file;
    while (*s)
        *p++ = *s++;
    *p = 0;
}

static const char *const dosfiles[] = {
    "DOSAPP.FON", "EGA80WOA.FON", "EGA40WOA.FON", "CGA80WOA.FON", "CGA40WOA.FON", 0
};

static void file_section(const char *file)
{
    char path[160], label[64];
    int i, j, n, added;
    static char used[MAXF];
    path_of(path, file);
    out("FILE ");
    out(file);
    added = AddFontResource(path);
    kv("added", added);
    nl();
    n = enum_all(now);
    nnow = n;
    for (j = 0; j < nbase; j++)
        used[j] = 0;
    for (i = 0; i < nnow; i++) {
        for (j = 0; j < nbase; j++)
            if (!used[j] && same(&now[i], &base[j]))
                break;
        if (j < nbase) {
            used[j] = 1;
            continue;
        }
        ef_lines("NEW", &now[i]);
        label_of(label, &now[i]);
        dump_lf(label, &now[i].lf);
    }
    /* where the new ones sit: the whole Terminal enumeration */
    n = enum_face_list("Terminal", now);
    for (i = 0; i < n; i++) {
        out("TERMINAL");
        kv("h", now[i].tm.tmHeight);
        kv("aw", now[i].tm.tmAveCharWidth);
        kv("lh", now[i].lf.lfHeight);
        kv("lw", now[i].lf.lfWidth);
        nl();
    }
    kv("removed", RemoveFontResource(path));
    nl();
}

/* what a WIN.INI [fonts] file brings: removed, the fonts no longer
   enumerated are listed, then it is added again */
static const char *const inifiles[] = {
    "COURE.FON", "SERIFE.FON", "SMALLE.FON", "SYMBOLE.FON", 0
};

static void gone_section(const char *file)
{
    char path[160];
    int i, j, n;
    static char used[MAXF];
    path_of(path, file);
    out("GONE ");
    out(file);
    kv("removed", RemoveFontResource(path));
    nl();
    n = enum_all(now);
    for (j = 0; j < n; j++)
        used[j] = 0;
    for (i = 0; i < nbase; i++) {
        for (j = 0; j < n; j++)
            if (!used[j] && same(&base[i], &now[j]))
                break;
        if (j < n) {
            used[j] = 1;
            continue;
        }
        ef_lines("OUT", &base[i]);
    }
    kv("added", AddFontResource(path));
    nl();
}

static void dos_map(void)
{
    static const int cell[][2] = {
        { 12, 8 }, { 12, 16 }, { 8, 8 }, { 8, 16 }, { 6, 4 }, { 8, 6 },
        { 12, 5 }, { 12, 7 }, { 12, 10 }, { 16, 12 }, { 18, 10 }, { 16, 8 },
        { 0, 0 }
    };
    char path[160];
    int i, h;
    out("DOSMAP\r\n");
    for (i = 0; dosfiles[i]; i++) {
        path_of(path, dosfiles[i]);
        AddFontResource(path);
    }
    for (i = 0; cell[i][0]; i++) {
        map_one(cell[i][0], cell[i][1], FW_NORMAL, OEM_CHARSET, FIXED_PITCH | FF_MODERN, "Terminal");
        map_one(cell[i][0], cell[i][1], 0, OEM_CHARSET, FIXED_PITCH | FF_MODERN, "");
    }
    for (h = 1; h <= 30; h++)
        map_one(h, 0, 0, OEM_CHARSET, DEFAULT_PITCH, "Terminal");
    for (i = 0; dosfiles[i]; i++) {
        path_of(path, dosfiles[i]);
        RemoveFontResource(path);
    }
}

typedef MODULEENTRY MODENT;
typedef BOOL (FAR PASCAL *MFH)(MODENT FAR *);
static MFH mfirst, mnext;

static const char *const allfiles[] = {
    "VGASYS.FON", "VGAFIX.FON", "VGAOEM.FON", "SSERIFE.FON",
    "COURE.FON", "SERIFE.FON", "SMALLE.FON", "SYMBOLE.FON",
    "DOSAPP.FON", "EGA80WOA.FON", "EGA40WOA.FON", "CGA80WOA.FON", "CGA40WOA.FON", 0
};

static void module_section(void)
{
    char path[160];
    int i, id;
    for (i = 0; allfiles[i]; i++) {
        HINSTANCE h;
        HRSRC r;
        MODENT me;
        path_of(path, allfiles[i]);
        out("MODULE ");
        out(allfiles[i]);
        h = LoadLibrary(path);
        if ((UINT)h < 32) {
            kv("error", (UINT)h);
            nl();
            continue;
        }
        if (mfirst && mnext) {
            BOOL ok;
            int k = 0;
            me.dwSize = sizeof me;
            for (ok = mfirst(&me); ok; ok = mnext(&me), k++)
                if (me.hModule == GetModuleHandle(path) || !lstrcmpi(me.szExePath, path)) {
                    out(" name=");
                    out(me.szModule);
                    break;
                }
            if (!ok)
                kv("modules", k);
        }
        r = FindResource(h, "FONTDIR", RT_FONTDIR);
        kv("fontdir", r ? (long)SizeofResource(h, r) : -1L);
        r = FindResource(h, MAKEINTRESOURCE(1), MAKEINTRESOURCE(16));
        kv("version", r ? (long)SizeofResource(h, r) : -1L);
        nl();
        for (id = 1; id < 1024; id++) {
            r = FindResource(h, MAKEINTRESOURCE(id), RT_FONT);
            if (r) {
                out(" font");
                kv("id", id);
                kv("size", (long)SizeofResource(h, r));
                nl();
            }
        }
        FreeLibrary(h);
    }
}

int __cdecl tmain(void)
{
    HINSTANCE th;
    static const char *const faces[] = { "Courier", "MS Serif", "Small Fonts", "Symbol", 0 };
    int i;

    WaitEvent(0);
    InitApp((HINSTANCE)init_regs[I_DI]);
    hdc = GetDC(NULL);
    GetSystemDirectory(sysdir, sizeof sysdir);
    out("FN3 font probe\r\n");
    pface = MakeProcInstance((FARPROC)enum_face, (HINSTANCE)init_regs[I_DI]);
    pfont = MakeProcInstance((FARPROC)enum_font, (HINSTANCE)init_regs[I_DI]);

    for (i = 0; faces[i]; i++)
        face_section(faces[i], TRUE);

    nbase = enum_all(base);
    out("BASE");
    kv("fonts", nbase);
    nl();
    for (i = 0; inifiles[i]; i++) {
        gone_section(inifiles[i]);
        nbase = enum_all(base);
    }
    for (i = 0; dosfiles[i]; i++)
        file_section(dosfiles[i]);
    dos_map();

    th = LoadLibrary("TOOLHELP.DLL");
    if ((UINT)th >= 32)
    {
        mfirst = (MFH)GetProcAddress(th, "ModuleFirst");
        mnext = (MFH)GetProcAddress(th, "ModuleNext");
    }
    module_section();
    if ((UINT)th >= 32)
        FreeLibrary(th);

    FreeProcInstance(pface);
    FreeProcInstance(pfont);
    ReleaseDC(NULL, hdc);
    out("FN3 done\r\n");
    return 0;
}
