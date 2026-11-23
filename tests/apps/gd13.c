/*
 * gd13.c - TrueType probe GD13 (ARCHITECTURE.md 11.12): what 3.1's GDI
 * does with a TrueType font, logged the same way on 3.1 and on w16.
 *
 * With C:\LIBSANS.TTF there (tests/fixtures/fonts/LiberationSans-
 * Regular.ttf, the free test font, copied by tests/gd13.sh):
 *   R      GetRasterizerCaps
 *   C      CreateScalableFontResource(0, "C:\LIBSANS.FOT", "C:\LIBSANS.TTF",
 *          NULL) and the .FOT it wrote (not named GD13.FOT: its module
 *          name would be the probe's, and LoadLibrary would give the
 *          probe): its FONTDIR and CCh resources'
 *          bytes (FD, TN lines) as found through its NE resource table
 *   A      AddFontResource of it
 *   M      for "Liberation Sans" and "Arial": every character height
 *          -1..-100 and cell height 1..100 and a few more, each line the
 *          face, TEXTMETRIC and an extent, as GD11's
 *   W      widths of 32..255 (GetCharWidth) at the even heights -6..-72
 *   B      GetCharABCWidths of a few characters
 *   K      GetKerningPairs: the count and the first pairs
 *   D      GetFontData: the file's size and head's first bytes
 *   P      a line of text drawn black on white into a monochrome bitmap
 *          at several sizes, its rows in hex (ROW lines) and the count of
 *          black pixels
 * then RemoveFontResource. Run as the shell (tests/gd13.sh; with -r on
 * Microsoft's GDI). Black-box: documented calls only.
 */
#include <windows.h>

extern WORD __cdecl init_regs[7];
extern void __cdecl log_write(const char FAR *buf, WORD len);
extern WORD FAR PASCAL WaitEvent(HTASK);
extern BOOL FAR PASCAL InitApp(HINSTANCE);
#define I_DI 5

static HDC hdc;
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

static void sp(long v)
{
    out(" ");
    outn(v);
}

static void hex2(BYTE v)
{
    char b[3];
    b[0] = "0123456789ABCDEF"[v >> 4];
    b[1] = "0123456789ABCDEF"[v & 15];
    b[2] = 0;
    out(b);
}

static void nl(void)
{
    out("\r\n");
    if (nbuf > sizeof buf - 600)
        flush();
}

static HFONT mkfont(const char *face, int h)
{
    LOGFONT lf;
    int i;
    for (i = 0; i < (int)sizeof lf; i++)
        ((char *)&lf)[i] = 0;
    lf.lfHeight = h;
    for (i = 0; face[i] && i < LF_FACESIZE - 1; i++)
        lf.lfFaceName[i] = face[i];
    return CreateFontIndirect(&lf);
}

static void metrics(const char *face, int h)
{
    HFONT f = mkfont(face, h), of = SelectObject(hdc, f);
    TEXTMETRIC t;
    char got[LF_FACESIZE + 1];
    DWORD e;
    GetTextFace(hdc, sizeof got, got);
    GetTextMetrics(hdc, &t);
    e = GetTextExtent(hdc, "Switch To", 9);
    out("M \"");
    out(face);
    out("\"");
    sp(h);
    out(" -> \"");
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
    sp(t.tmFirstChar);
    sp(t.tmLastChar);
    sp(t.tmDefaultChar);
    sp(t.tmBreakChar);
    sp(t.tmPitchAndFamily);
    sp(t.tmCharSet);
    sp(t.tmOverhang);
    sp(t.tmDigitizedAspectX);
    sp(t.tmDigitizedAspectY);
    out(" |");
    sp(LOWORD(e));
    sp(HIWORD(e));
    nl();
    SelectObject(hdc, of);
    DeleteObject(f);
}

static void widths(const char *face, int h)
{
    HFONT f = mkfont(face, h), of = SelectObject(hdc, f);
    static int w[224];
    static ABC abc[4];
    int i;
    out("W \"");
    out(face);
    out("\"");
    sp(h);
    out(" ->");
    if (GetCharWidth(hdc, 32, 255, w))
        for (i = 0; i < 224; i++)
            sp(w[i]);
    nl();
    out("B \"");
    out(face);
    out("\"");
    sp(h);
    out(" ->");
    sp(GetCharABCWidths(hdc, 'A', 'A', &abc[0]) + 0);
    GetCharABCWidths(hdc, 'W', 'W', &abc[1]);
    GetCharABCWidths(hdc, 'j', 'j', &abc[2]);
    GetCharABCWidths(hdc, 'f', 'f', &abc[3]);
    for (i = 0; i < 4; i++) {
        sp(abc[i].abcA);
        sp(abc[i].abcB);
        sp(abc[i].abcC);
    }
    nl();
    SelectObject(hdc, of);
    DeleteObject(f);
}

static void picture(const char *face, int h)
{
    static const char text[] = "The quick brown fox 0123 AWjf";
    HFONT f = mkfont(face, h), of;
    HDC m = CreateCompatibleDC(hdc);
    TEXTMETRIC t;
    HBITMAP bm, ob;
    int wpx = 512, ht, y, x, black = 0;
    static BYTE row[64 * 160];
    of = SelectObject(m, f);
    GetTextMetrics(m, &t);
    ht = t.tmHeight + 4;
    if (ht > 160)
        ht = 160;
    bm = CreateBitmap(wpx, ht, 1, 1, NULL);
    ob = SelectObject(m, bm);
    PatBlt(m, 0, 0, wpx, ht, WHITENESS);
    SetBkMode(m, TRANSPARENT);
    TextOut(m, 2, 2, text, sizeof text - 1);
    GetBitmapBits(bm, (DWORD)(wpx / 8) * ht, (LPSTR)row);
    for (y = 0; y < ht; y++)
        for (x = 0; x < wpx / 8; x++) {
            BYTE v = (BYTE)~row[y * (wpx / 8) + x];
            while (v) {
                black += v & 1;
                v >>= 1;
            }
        }
    out("P \"");
    out(face);
    out("\"");
    sp(h);
    sp(wpx);
    sp(ht);
    sp(black);
    nl();
    for (y = 0; y < ht; y++) {
        out("ROW");
        sp(y);
        out(" ");
        for (x = 0; x < wpx / 8; x++)
            hex2((BYTE)~row[y * (wpx / 8) + x]);
        nl();
    }
    SelectObject(m, ob);
    SelectObject(m, of);
    DeleteObject(bm);
    DeleteDC(m);
    DeleteObject(f);
}

/* the .FOT's resources, through its NE resource table */
static void dump_fot(void)
{
    static BYTE f[2048];
    OFSTRUCT of;
    int fh = OpenFile("C:\\LIBSANS.FOT", &of, OF_READ), n, i;
    WORD ne, rt, sh, p;
    if (fh == HFILE_ERROR) {
        out("C no file");
        nl();
        return;
    }
    n = _lread(fh, f, sizeof f);
    _lclose(fh);
    out("C size");
    sp(n);
    nl();
    ne = *(WORD *)(f + 0x3C);
    if (ne + 0x40 > (WORD)n || f[ne] != 'N')
        return;
    out("C ne flags");
    sp(*(WORD *)(f + ne + 0x0C));
    out(" expver");
    sp(*(WORD *)(f + ne + 0x3E));
    out(" exetype");
    sp(f[ne + 0x36]);
    nl();
    rt = ne + *(WORD *)(f + ne + 0x24);
    sh = *(WORD *)(f + rt);
    p = rt + 2;
    while (p + 8 < (WORD)n && *(WORD *)(f + p)) {
        WORD type = *(WORD *)(f + p), cnt = *(WORD *)(f + p + 2), k;
        for (k = 0; k < cnt; k++) {
            BYTE *r = f + p + 8 + 12 * k;
            DWORD off = (DWORD)*(WORD *)r << sh, len = (DWORD)*(WORD *)(r + 2) << sh;
            out(type == 0x8007 ? "FD" : "TN");
            sp(type);
            sp(*(WORD *)(r + 4));
            sp(*(WORD *)(r + 6));
            sp(len);
            out(" ");
            for (i = 0; i < (int)len && off + i < (DWORD)n && i < 200; i++)
                hex2(f[(WORD)off + i]);
            nl();
        }
        p += 8 + 12 * cnt;
    }
}

int __cdecl tmain(void)
{
    static const char *faces[2] = { "Liberation Sans", "Arial" };
    static const int pics[] = { -8, -10, -11, -12, -13, -14, -16, -18, -20, -24, -28, -32, -40, -48, -64, -72 };
    static const int big[] = { -120, -150, -200, 120, 150, 200 };
    RASTERIZER_STATUS rs;
    int i, h, k, n;
    WaitEvent(0);
    InitApp((HINSTANCE)init_regs[I_DI]);
    hdc = GetDC(NULL);
    out("GD13 start\r\n");
    rs.nSize = rs.wFlags = rs.nLanguageID = 0;
    out("R");
    sp(GetRasterizerCaps(&rs, sizeof rs));
    sp(rs.nSize);
    sp(rs.wFlags);
    sp(rs.nLanguageID);
    nl();
    out("C");
    sp(CreateScalableFontResource(0, "C:\\LIBSANS.FOT", "C:\\LIBSANS.TTF", NULL));
    nl();
    dump_fot();
    out("A");
    sp(AddFontResource("C:\\LIBSANS.FOT"));
    nl();
    flush();
    for (k = 0; k < 2; k++) {
        for (h = -100; h <= 100; h++)
            if (h)
                metrics(faces[k], h);
        for (i = 0; i < (int)(sizeof big / sizeof big[0]); i++)
            metrics(faces[k], big[i]);
        flush();
        for (h = -6; h >= -72; h -= 2)
            widths(faces[k], h);
        flush();
    }
    for (k = 0; k < 2; k++) {
        HFONT f = mkfont(faces[k], -16), of = SelectObject(hdc, f);
        static KERNINGPAIR kp[4];
        static BYTE head[16];
        DWORD sz;
        n = GetKerningPairs(hdc, 0, NULL);
        out("K \"");
        out(faces[k]);
        out("\"");
        sp(n);
        n = GetKerningPairs(hdc, 4, kp);
        for (i = 0; i < n && i < 4; i++) {
            sp(kp[i].wFirst);
            sp(kp[i].wSecond);
            sp(kp[i].iKernAmount);
        }
        nl();
        sz = GetFontData(hdc, 0, 0, NULL, 0);
        out("D \"");
        out(faces[k]);
        out("\"");
        sp((long)sz);
        sz = GetFontData(hdc, 0x64616568L, 0, head, sizeof head);     /* 'head' */
        sp((long)sz);
        out(" ");
        for (i = 0; i < 16; i++)
            hex2(head[i]);
        nl();
        SelectObject(hdc, of);
        DeleteObject(f);
        flush();
    }
    for (k = 0; k < 2; k++)
        for (i = 0; i < (int)(sizeof pics / sizeof pics[0]); i++) {
            picture(faces[k], pics[i]);
            flush();
        }
    out("RM");
    sp(RemoveFontResource("C:\\LIBSANS.FOT"));
    nl();
    ReleaseDC(NULL, hdc);
    out("GD13 done\r\n");
    flush();
    return 0;
}
