/*
 * wrmet.c - probe WR9: the text metrics of the fonts Write uses (black
 * box, documented calls only). For each face named in C:\WRMET.TXT (one
 * per line, default Arial) and each lfHeight from -6 to -48, logs
 *     MET <face> <lfHeight> h=<tmHeight> a=<tmAscent> d=<tmDescent>
 *         il=<tmInternalLeading> el=<tmExternalLeading> w=<tmAveCharWidth>
 * to TK.LOG, for w16 Write's line layout.
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

static void outint(int v)
{
    char b[8];
    int i = 7;
    WORD u = v < 0 ? -v : v;
    b[7] = 0;
    do {
        b[--i] = (char)('0' + u % 10);
        u /= 10;
    } while (u);
    if (v < 0)
        b[--i] = '-';
    out(b + i);
}

static void probe(HDC dc, const char *face)
{
    int h;
    for (h = -6; h >= -48; h--) {
        LOGFONT lf;
        TEXTMETRIC tm;
        HFONT f, old;
        int i;
        for (i = 0; i < sizeof lf; i++)
            ((char *)&lf)[i] = 0;
        lf.lfHeight = h;
        lf.lfWeight = FW_NORMAL;
        lstrcpy(lf.lfFaceName, face);
        f = CreateFontIndirect(&lf);
        old = SelectObject(dc, f);
        GetTextMetrics(dc, &tm);
        SelectObject(dc, old);
        DeleteObject(f);
        out("MET ");
        out(face);
        out(" ");
        outint(h);
        out(" h=");
        outint(tm.tmHeight);
        out(" a=");
        outint(tm.tmAscent);
        out(" d=");
        outint(tm.tmDescent);
        out(" il=");
        outint(tm.tmInternalLeading);
        out(" el=");
        outint(tm.tmExternalLeading);
        out(" w=");
        outint(tm.tmAveCharWidth);
        out("\r\n");
    }
}

int __cdecl tmain(void)
{
    static char faces[512];
    HFILE f;
    int n = 0;
    char *p, *e;
    HDC dc;
    WaitEvent(0);
    InitApp((HINSTANCE)init_regs[5]);
    f = _lopen("C:\\WRMET.TXT", OF_READ);
    if (f != HFILE_ERROR) {
        n = _lread(f, faces, sizeof faces - 1);
        _lclose(f);
    }
    if (n <= 0) {
        lstrcpy(faces, "Arial");
        n = 5;
    }
    faces[n] = 0;
    dc = GetDC(NULL);
    for (p = faces; *p; p = e) {
        for (e = p; *e && *e != '\r' && *e != '\n'; e++)
            ;
        if (*e)
            *e++ = 0;
        if (*p)
            probe(dc, p);
    }
    ReleaseDC(NULL, dc);
    out("WRMET done\r\n");
    return 0;
}
