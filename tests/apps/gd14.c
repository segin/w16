/*
 * gd14.c - font enumeration probe GD14 (ARCHITECTURE.md 11.12): what
 * EnumFonts and EnumFontFamilies return and give beyond FN1's list of
 * the screen's fonts (tests/apps/fn1.c), logged the same way on 3.1 and
 * on w16. Each line names the call, the DC and the face asked, then
 * "r=" the function's return, "n=" the callbacks made, "d=" whether
 * every callback got the caller's data, and the faces and types the
 * callbacks got (with the height and width of the first entry):
 *   F   EnumFontFamilies, NULL face, the screen DC (GetDC(NULL)), the
 *       callback returning 1: the families
 *   S   the same with a callback returning 0 (the first only), and one
 *       returning 7
 *   X   faces: "NoSuchFace", "", "arial" (case), "Helv" (a
 *       [FontSubstitutes] name), "MS Sans Serif", "Arial"; EnumFonts
 *       likewise
 *   D   other DCs: a memory DC, CreateIC("DISPLAY"), CreateDC("DISPLAY"),
 *       a metafile DC, DC 0
 *   M   the screen DC in MM_LOMETRIC and in MM_ANISOTROPIC (2:1): the
 *       first entries' LOGFONT heights and widths, logical or device
 * Run as the shell (tests/gdiheap.sh -p build/dos/GD14.EXE [-r]).
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

static void nl(void)
{
    out("\r\n");
    flush();
}

/* what the callbacks saw */
static int ncalls, ret, baddata, nlisted;
static char list[1200];

static void addn(int v)
{
    char b[8];
    int i = 7;
    unsigned u = v < 0 ? -v : v;
    b[i] = 0;
    do {
        b[--i] = (char)('0' + u % 10);
        u /= 10;
    } while (u);
    if (v < 0)
        b[--i] = '-';
    while (b[i])
        list[nlisted++] = b[i++];
}

static void note(const LOGFONT FAR *lf, int type, LPARAM data)
{
    int i;
    if (data != 0x12345678L)
        baddata = 1;
    if (ncalls == 0) {
        /* the first entry's size */
        list[nlisted++] = ' ';
        list[nlisted++] = '[';
        addn(lf->lfHeight);
        list[nlisted++] = ',';
        addn(lf->lfWidth);
        list[nlisted++] = ']';
    }
    ncalls++;
    if (nlisted < (int)sizeof list - 40) {
        list[nlisted++] = ' ';
        for (i = 0; lf->lfFaceName[i] && i < LF_FACESIZE; i++)
            list[nlisted++] = lf->lfFaceName[i];
        list[nlisted++] = ':';
        list[nlisted++] = (char)('0' + type);
    }
    list[nlisted] = 0;
}

int FAR PASCAL __export fam_cb(const ENUMLOGFONT FAR *elf, const NEWTEXTMETRIC FAR *tm,
                               int type, LPARAM data)
{
    (void)tm;
    note(&elf->elfLogFont, type, data);
    return ret;
}

int FAR PASCAL __export font_cb(const LOGFONT FAR *lf, const TEXTMETRIC FAR *tm,
                                int type, LPARAM data)
{
    (void)tm;
    note(lf, type, data);
    return ret;
}

static FARPROC pfam, pfont;

static void one(const char *tag, const char *dcname, HDC dc, const char *face, BOOL fam, int r)
{
    int got;
    ncalls = baddata = nlisted = 0;
    list[0] = 0;
    ret = r;
    if (fam)
        got = EnumFontFamilies(dc, face, (FONTENUMPROC)pfam, (LPSTR)0x12345678L);
    else
        got = EnumFonts(dc, face, (OLDFONTENUMPROC)pfont, (LPSTR)0x12345678L);
    out(tag);
    out(fam ? " fam " : " fonts ");
    out(dcname);
    out(" \"");
    out(face ? face : "(null)");
    out("\"");
    kv("cb", r);
    kv("r", got);
    kv("n", ncalls);
    kv("d", !baddata);
    out(list);
    nl();
}

int __cdecl tmain(void)
{
    static const char *faces[] = { "NoSuchFace", "", "arial", "Helv", "MS Sans Serif", "Arial", "Symbol" };
    HDC scr, mem, ic, dc, mf;
    HMETAFILE hmf;
    int i, k;
    WaitEvent(0);
    InitApp((HINSTANCE)init_regs[I_DI]);
    pfam = MakeProcInstance((FARPROC)fam_cb, (HINSTANCE)init_regs[I_DI]);
    pfont = MakeProcInstance((FARPROC)font_cb, (HINSTANCE)init_regs[I_DI]);
    out("GD14 start\r\n");
    scr = GetDC(NULL);
    one("F", "screen", scr, NULL, TRUE, 1);
    one("F", "screen", scr, NULL, FALSE, 1);
    one("S", "screen", scr, NULL, TRUE, 0);
    one("S", "screen", scr, NULL, TRUE, 7);
    one("S", "screen", scr, "Arial", TRUE, 0);
    one("S", "screen", scr, "MS Sans Serif", FALSE, 0);
    one("S", "screen", scr, "MS Sans Serif", FALSE, -3);
    for (k = 0; k < 2; k++)
        for (i = 0; i < (int)(sizeof faces / sizeof faces[0]); i++)
            one("X", "screen", scr, faces[i], k == 0, 1);
    mem = CreateCompatibleDC(scr);
    ic = CreateIC("DISPLAY", NULL, NULL, NULL);
    dc = CreateDC("DISPLAY", NULL, NULL, NULL);
    mf = CreateMetaFile(NULL);
    for (k = 0; k < 2; k++) {
        one("D", "mem", mem, NULL, k == 0, 1);
        one("D", "ic", ic, NULL, k == 0, 1);
        one("D", "dc", dc, NULL, k == 0, 1);
        one("D", "meta", mf, NULL, k == 0, 1);
        one("D", "zero", 0, NULL, k == 0, 1);
        one("D", "mem", mem, "Arial", k == 0, 1);
        one("D", "ic", ic, "MS Serif", k == 0, 1);
    }
    hmf = CloseMetaFile(mf);
    DeleteMetaFile(hmf);
    SetMapMode(dc, MM_LOMETRIC);
    one("M", "lometric", dc, "MS Sans Serif", TRUE, 1);
    one("M", "lometric", dc, "Arial", FALSE, 1);
    SetMapMode(dc, MM_ANISOTROPIC);
    SetWindowExt(dc, 2, 1);
    SetViewportExt(dc, 1, 1);
    one("M", "aniso", dc, "MS Sans Serif", FALSE, 1);
    one("M", "aniso", dc, "Arial", TRUE, 1);
    DeleteDC(dc);
    DeleteDC(ic);
    DeleteDC(mem);
    ReleaseDC(NULL, scr);
    FreeProcInstance(pfam);
    FreeProcInstance(pfont);
    out("GD14 done\r\n");
    flush();
    return 0;
}
