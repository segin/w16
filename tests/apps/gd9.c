/*
 * gd9.c - metafiles (ARCHITECTURE.md 11.6, 11.8): what Windows 3.1's GDI
 * records, plays and returns.
 *
 * - the metafile DC: its object type, what the query functions
 *   (GetDeviceCaps, GetTextMetrics, GetMapMode, ...) and the recording
 *   functions return on it;
 * - every recordable call in a metafile of its own: the bytes of the
 *   memory metafile (GetMetaFileBits) word by word (OBS name@off, for
 *   reading), their checksum and length, each record's function number
 *   and size (checked);
 * - objects: the object table (CreatePen/Brush/Font/Palette/Region
 *   records, SelectObject by index, reuse of a slot, DeleteObject
 *   records, stock objects), palettes changed while selected;
 * - disk metafiles (CreateMetaFile with a name, CopyMetaFile, GetMetaFile):
 *   the file's bytes and the handle's memory; placeable (Aldus) files;
 * - GetMetaFileBits, SetMetaFileBits(Better): handles and their owners;
 * - playback into memory bitmaps (pixels hashed and compared with the
 *   same calls made directly), under the destination's mapping, with
 *   SetWindowOrg/Ext records, nested and unbalanced SaveDC/RestoreDC, and
 *   the destination DC's state afterwards;
 * - EnumMetaFile record by record and PlayMetaFileRecord's handle table;
 * - errors: bad handles, an unknown function number, bad object indexes,
 *   bad headers;
 * - CF_METAFILEPICT through USER's clipboard (who frees what).
 *
 * Run as the shell of Windows 3.1 with Microsoft's GDI (the reference:
 * tools/obsref.py of its log is gd9ref.h) and with w16's
 * (tests/gdiheap.sh -p build/dos/GD9.EXE [-r]; the boot-test case
 * gdimeta). Sources: the SDK (Programmer's Reference vol. 1 2.15, vol. 2
 * the metafile functions, vol. 4 ch. 3) for what the calls are; the
 * values are what 3.1 gives.
 */
#include <windows.h>
#include <toolhelp.h>

#define TK_REF "gd9ref.h"
#include "tklog.h"

extern WORD FAR PASCAL WaitEvent(HTASK);
extern BOOL FAR PASCAL InitApp(HINSTANCE);
extern int WINAPI SetRelAbs(HDC, int);

typedef BOOL (FAR PASCAL *PISVALIDMF)(HANDLE);
typedef BOOL (FAR PASCAL *PGEH)(GLOBALENTRY FAR *, HGLOBAL);
static PISVALIDMF isvalidmf;
static PGEH gentryhandle;
static HINSTANCE hinst;
static HDC hscr;
static HGLOBAL hmine;                   /* a block of the probe's own, for its owner */

#define PW 64                           /* the bitmaps metafiles are played into */
#define PH 48

/* ---- names ---- */

static char nbuf[96];

static const char *nm(const char *a, const char *b)
{
    int i = 0;
    while (*a && i < 60)
        nbuf[i++] = *a++;
    if (*b && i < 60)
        nbuf[i++] = ' ';
    while (*b && i < 92)
        nbuf[i++] = *b++;
    nbuf[i] = 0;
    return nbuf;
}

static int unchecked;                   /* the bytes vary on 3.1 (garbage, pointers) */

static void obs2(const char *a, const char *b, WORD v)
{
    if (unchecked)
        obs_env(nm(a, b), v);
    else
        obs(nm(a, b), v);
}

static void obs_dw(const char *a, const char *b, DWORD v)
{
    char n[96];
    lstrcpy(n, nm(a, b));
    lstrcat(n, " lo");
    if (unchecked)
        obs_env(n, LOWORD(v));
    else
        obs(n, LOWORD(v));
    n[lstrlen(n) - 2] = 'h';
    n[lstrlen(n) - 1] = 'i';
    if (unchecked)
        obs_env(n, HIWORD(v));
    else
        obs(n, HIWORD(v));
}

static void numname(char *n, const char *a, const char *b, WORD i, const char *c)
{
    lstrcpy(n, nm(a, b));
    n += lstrlen(n);
    *n++ = '0' + i / 100 % 10;
    *n++ = '0' + i / 10 % 10;
    *n++ = '0' + i % 10;
    lstrcpy(n, c);
}

/* ---- the probe's own block owner, through TOOLHELP ---- */

static HANDLE owner(HGLOBAL h)
{
    GLOBALENTRY ge;
    if (!gentryhandle || !h)
        return 0;
    ge.dwSize = sizeof ge;
    if (!gentryhandle(&ge, h))
        return 0;
    return ge.hOwner;
}

/* who owns h: 1 the probe, 2 GDI, 3 someone else, 0 no such block */
static WORD whose(HGLOBAL h)
{
    HANDLE o = owner(h);
    if (!o)
        return 0;
    if (o == owner(hmine))
        return 1;
    if (o == GetModuleHandle("GDI"))
        return 2;
    return 3;
}

/* ---- a metafile's bits ---- */

static WORD sum16(BYTE FAR *p, DWORD n)
{
    WORD h = 0;
    while (n--)
        h = (WORD)((h << 3) | (h >> 13)) + *p++ + 1;
    return h;
}

/* the metafile in memory at p (n bytes there): the header's fields,
   each record's function and size, the checksum of mtSize words */
static void show_bits(const char *name, BYTE FAR *p, DWORD n)
{
    METAHEADER FAR *mh = (METAHEADER FAR *)p;
    WORD FAR *w = (WORD FAR *)p;
    DWORD words, at;
    WORD i;
    char rn[100];
    if (n < 18) {
        obs2(name, "short", (WORD)n);
        return;
    }
    /* (checked: the size, the objects and the checksum of every word;
       the rest is there to read, as OBS ~: the table must fit in the
       probe's data segment) */
    obs_env(nm(name, "mtType"), mh->mtType);
    obs_env(nm(name, "mtHeaderSize"), mh->mtHeaderSize);
    obs_env(nm(name, "mtVersion"), mh->mtVersion);
    obs2(name, "mtSize", (WORD)mh->mtSize);
    obs2(name, "mtNoObjects", mh->mtNoObjects);
    obs_env(nm(name, "mtMaxRecord"), (WORD)mh->mtMaxRecord);
    obs_env(nm(name, "mtNoParameters"), mh->mtNoParameters);
    words = mh->mtSize;
    if (words * 2 > n)
        words = n / 2;
    at = mh->mtHeaderSize;
    for (i = 0; at + 3 <= words && i < 200; i++) {
        DWORD sz = MAKELONG(w[(WORD)at], w[(WORD)at + 1]);
        numname(rn, name, "r", i, " fn");
        obs_env(rn, w[(WORD)at + 2]);
        numname(rn, name, "r", i, " size");
        obs_env(rn, (WORD)sz);
        if (sz < 3)
            break;
        at += sz;
    }
    obs2(name, "records", i);
    /* a region record's ObjectCount (its third and fourth words) is the
       region object's sequence count on 3.1: not compared */
    {
        DWORD a = mh->mtHeaderSize;
        WORD k = 0;
        while (a + 3 <= words && k++ < 200) {
            DWORD sz = MAKELONG(w[(WORD)a], w[(WORD)a + 1]);
            if (sz < 3)
                break;
            if (w[(WORD)a + 2] == META_CREATEREGION && sz >= 7) {
                obs_env(nm(name, "region count"), w[(WORD)a + 5]);
                w[(WORD)a + 5] = w[(WORD)a + 6] = 0;
            }
            a += sz;
        }
    }
    if (unchecked)
        obs_env(nm(name, "sum"), sum16(p, words * 2));
    else
        obs2(name, "sum", sum16(p, words * 2));
    {
        WORD nw = (WORD)(words > 600 ? 600 : words);
        WORD k;
        for (k = 0; k < nw; k++) {
            if ((k & 7) == 0) {
                if (k)
                    out("\r\n");
                out("OBS ");
                out(name);
                out("@");
                outhex(2 * k);
                out("=");
            } else {
                out(" ");
            }
            outhex(w[k]);
        }
        out("\r\n");
    }
}

/* a memory metafile's handle: its block (3.1: a global handle) */
static void show_mf(const char *name, HMETAFILE h)
{
    BYTE FAR *p;
    DWORD sz;
    if (!h) {
        obs2(name, "handle", 0);
        return;
    }
    sz = GlobalSize((HGLOBAL)h);
    obs2(name, "IsValidMetaFile", isvalidmf ? isvalidmf((HANDLE)h) : 0xEEEE);
    obs2(name, "GlobalSize", (WORD)sz);
    p = (BYTE FAR *)GlobalLock((HGLOBAL)h);
    if (!p) {
        obs2(name, "locked", 0);
        return;
    }
    show_bits(name, p, sz);
    GlobalUnlock((HGLOBAL)h);
}

/* ---- recording one call at a time ---- */

static HDC rec;
static char cname[64];

static void rb(const char *name)
{
    lstrcpy(cname, name);
    rec = CreateMetaFile(NULL);
    if (!rec)
        obs2(name, "CreateMetaFile", 0);
}

static void rr(DWORD v)
{
    obs_dw(cname, "ret", v);
}

static void re(void)
{
    HMETAFILE h = CloseMetaFile(rec);
    show_mf(cname, h);
    obs2(cname, "DeleteMetaFile", DeleteMetaFile(h));
    rec = 0;
}

/* a 16 x 8 bitmap with a pattern, colour (the screen's format) or mono */
static HBITMAP pattern_bitmap(int mono)
{
    /* 32 wide: a DIB of it has no padding (3.1 leaves the padding as the
       memory was) */
    HBITMAP b = mono ? CreateBitmap(32, 8, 1, 1, NULL) : CreateCompatibleBitmap(hscr, 16, 8);
    HDC d = CreateCompatibleDC(hscr);
    HBITMAP ob = SelectObject(d, b);
    int x, y;
    PatBlt(d, 0, 0, 32, 8, WHITENESS);
    for (y = 0; y < 8; y++)
        for (x = 0; x < (mono ? 32 : 16); x++)
            if ((x + y) % 3 == 0)
                SetPixel(d, x, y, mono ? RGB(0, 0, 0) : RGB(255, 0, 0));
            else if (!mono && (x * y) % 5 == 1)
                SetPixel(d, x, y, RGB(0, 0, 255));
    SelectObject(d, ob);
    DeleteDC(d);
    return b;
}

/* a 4-bit DIB, 8 x 4 */
static struct {
    BITMAPINFOHEADER h;
    RGBQUAD c[16];
    BYTE bits[16];
} dib4;

static void make_dib(void)
{
    static const BYTE vga[16][3] = {
        {0, 0, 0}, {128, 0, 0}, {0, 128, 0}, {128, 128, 0}, {0, 0, 128}, {128, 0, 128},
        {0, 128, 128}, {192, 192, 192}, {128, 128, 128}, {255, 0, 0}, {0, 255, 0},
        {255, 255, 0}, {0, 0, 255}, {255, 0, 255}, {0, 255, 255}, {255, 255, 255} };
    int i;
    dib4.h.biSize = sizeof(BITMAPINFOHEADER);
    dib4.h.biWidth = 8;
    dib4.h.biHeight = 4;
    dib4.h.biPlanes = 1;
    dib4.h.biBitCount = 4;
    dib4.h.biCompression = 0;
    dib4.h.biSizeImage = 0;
    dib4.h.biXPelsPerMeter = dib4.h.biYPelsPerMeter = 0;
    dib4.h.biClrUsed = dib4.h.biClrImportant = 0;
    for (i = 0; i < 16; i++) {
        dib4.c[i].rgbRed = vga[i][0];
        dib4.c[i].rgbGreen = vga[i][1];
        dib4.c[i].rgbBlue = vga[i][2];
        dib4.c[i].rgbReserved = 0;
    }
    for (i = 0; i < 16; i++)
        dib4.bits[i] = (BYTE)(i * 0x11 + 0x01);
}

/* ---- the metafile DC itself ---- */

static void sec_dc(void)
{
    HDC m = CreateMetaFile(NULL);
    TEXTMETRIC tm;
    RECT rc;
    POINT pt;
    HMETAFILE h;
    int i;
    char n[40];
    obs("CreateMetaFile(NULL) nonzero", m != 0);
    obs("meta DC IsGDIObject", IsGDIObject(m));
    for (i = 0; i <= 0x6C; i += 2) {
        lstrcpy(n, "meta GetDeviceCaps ");
        n[19] = "0123456789ABCDEF"[i >> 4];
        n[20] = "0123456789ABCDEF"[i & 15];
        n[21] = 0;
        obs(n, GetDeviceCaps(m, i));
    }
    tm.tmHeight = 0x5555;
    obs("meta GetTextMetrics", GetTextMetrics(m, &tm));
    obs("meta GetTextMetrics tmHeight", tm.tmHeight);
    obs_dw("meta GetTextExtent", "", GetTextExtent(m, "abc", 3));
    obs("meta GetTextFace", GetTextFace(m, 40, n));
    obs("meta GetMapMode", GetMapMode(m));
    obs("meta GetBkMode", GetBkMode(m));
    obs("meta GetROP2", GetROP2(m));
    obs("meta GetTextAlign", GetTextAlign(m));
    obs_dw("meta GetBkColor", "", GetBkColor(m));
    obs_dw("meta GetTextColor", "", GetTextColor(m));
    obs_dw("meta GetWindowExt", "", GetWindowExt(m));
    obs_dw("meta GetViewportExt", "", GetViewportExt(m));
    obs_dw("meta GetWindowOrg", "", GetWindowOrg(m));
    obs_dw("meta GetCurrentPosition", "", GetCurrentPosition(m));
    obs_dw("meta GetBrushOrg", "", GetBrushOrg(m));
    obs_dw("meta GetDCOrg", "", GetDCOrg(m));
    obs_dw("meta GetNearestColor", "", GetNearestColor(m, RGB(1, 2, 3)));
    obs_dw("meta GetPixel", "", GetPixel(m, 1, 1));
    SetRect(&rc, 1, 2, 3, 4);
    obs("meta GetClipBox", GetClipBox(m, &rc));
    obs("meta GetClipBox left", rc.left);
    obs("meta GetClipBox right", rc.right);
    obs("meta PtVisible", PtVisible(m, 1, 1));
    SetRect(&rc, 0, 0, 10, 10);
    obs("meta RectVisible", RectVisible(m, &rc));
    pt.x = 5;
    pt.y = 6;
    obs("meta LPtoDP", LPtoDP(m, &pt, 1));
    obs("meta LPtoDP x", pt.x);
    {
        HDC c = CreateCompatibleDC(m);
        HBITMAP b = CreateCompatibleBitmap(m, 8, 8);
        obs("meta CreateCompatibleDC nonzero", c != 0);
        obs("meta CreateCompatibleBitmap nonzero", b != 0);
        if (c)
            DeleteDC(c);
        if (b)
            DeleteObject(b);
    }
    obs("meta GetTextCharacterExtra", GetTextCharacterExtra(m));
    obs("meta GetPolyFillMode", GetPolyFillMode(m));
    obs("meta GetStretchBltMode", GetStretchBltMode(m));
    /* what the recording calls return */
    obs_dw("meta SetBkColor", "", SetBkColor(m, RGB(9, 9, 9)));
    obs_dw("meta SetBkColor again", "", SetBkColor(m, RGB(8, 8, 8)));
    obs("meta SetMapMode", SetMapMode(m, MM_TWIPS));
    obs("meta SetMapMode again", SetMapMode(m, MM_TEXT));
    obs("meta GetMapMode after", GetMapMode(m));
    obs_dw("meta MoveTo", "", MoveTo(m, 5, 6));
    obs_dw("meta MoveTo again", "", MoveTo(m, 7, 8));
    obs_dw("meta GetCurrentPosition after", "", GetCurrentPosition(m));
    obs("meta SaveDC", SaveDC(m));
    obs("meta SaveDC again", SaveDC(m));
    obs("meta RestoreDC", RestoreDC(m, -1));
    {
        HPEN p = CreatePen(PS_SOLID, 1, RGB(1, 2, 3));
        HPEN p2 = CreatePen(PS_SOLID, 1, RGB(4, 5, 6));
        WORD r1 = (WORD)SelectObject(m, p), r2 = (WORD)SelectObject(m, p2);
        obs("meta SelectObject pen ret is 1", r1 == 1);
        obs("meta SelectObject pen ret is pen", r1 == (WORD)p);
        obs("meta SelectObject pen ret zero", r1 == 0);
        obs("meta SelectObject pen ret BLACK_PEN", r1 == (WORD)GetStockObject(BLACK_PEN));
        obs("meta SelectObject brush ret WHITE_BRUSH",
            (WORD)SelectObject(m, GetStockObject(GRAY_BRUSH)) == (WORD)GetStockObject(WHITE_BRUSH));
        {
            WORD f = (WORD)SelectObject(m, GetStockObject(ANSI_VAR_FONT));
            WORD q = (WORD)SelectPalette(m, GetStockObject(DEFAULT_PALETTE), 0);
            obs("meta SelectObject font ret SYSTEM_FONT", f == (WORD)GetStockObject(SYSTEM_FONT));
            obs("meta SelectObject font ret 0", f == 0);
            obs("meta SelectObject font ret 1", f == 1);
            obs("meta SelectObject font ret ANSI_VAR_FONT", f == (WORD)GetStockObject(ANSI_VAR_FONT));
            obs_env("meta SelectObject font ret", f);
            obs("meta SelectPalette ret DEFAULT_PALETTE", q == (WORD)GetStockObject(DEFAULT_PALETTE));
            obs("meta SelectPalette ret 0", q == 0);
            obs("meta SelectPalette ret 1", q == 1);
            obs_env("meta SelectPalette ret", q);
            q = (WORD)SelectPalette(m, GetStockObject(DEFAULT_PALETTE), 0);
            obs("meta SelectPalette again ret DEFAULT_PALETTE", q == (WORD)GetStockObject(DEFAULT_PALETTE));
            obs_env("meta SelectPalette again ret", q);
        }
        obs("meta SelectObject 2nd ret is 1st", r2 == (WORD)p);
        obs("meta SelectObject 2nd ret is 1", r2 == 1);
        obs("meta DeleteObject selected pen", DeleteObject(p2));
        obs("meta pen after DeleteObject", IsGDIObject(p2));
        DeleteObject(p);
    }
    obs("meta SetBrushOrg is 0", GetBrushOrg(m) == 0 && SetBrushOrg(m, 3, 3) == 0);
    obs("meta GetBrushOrg after", LOWORD(GetBrushOrg(m)));
    h = CloseMetaFile(m);
    obs("CloseMetaFile nonzero", h != 0);
    obs_env("CloseMetaFile handle IsGDIObject", IsGDIObject((HGDIOBJ)h));
    obs("closed meta DC IsGDIObject", IsGDIObject(m));
    obs("CloseMetaFile again", CloseMetaFile(m) != 0);
    obs("CloseMetaFile screen DC", CloseMetaFile(hscr) != 0);
    obs("metafile GlobalFlags", GlobalFlags((HGLOBAL)h) & 0xFF00);
    obs("metafile whose", whose((HGLOBAL)h));
    show_mf("dc case", h);
    obs("DeleteMetaFile", DeleteMetaFile(h));
    obs("DeleteMetaFile again", DeleteMetaFile(h));
    obs("DeleteMetaFile 0", DeleteMetaFile(0));
    /* DeleteDC on an open metafile DC */
    m = CreateMetaFile(NULL);
    Rectangle(m, 1, 2, 3, 4);
    obs("DeleteDC meta DC", DeleteDC(m));
    obs("meta DC after DeleteDC IsGDIObject", IsGDIObject(m));
    obs("CloseMetaFile after DeleteDC", CloseMetaFile(m) != 0);
}

/* ---- every recordable call ---- */

static char abcz[] = "abcZ";
static char abcdz[] = "abcdZ";

static void sec_calls(void)
{
    RECT rc;
    POINT pts[6];
    int cnt[2];
    int dx[4];
    SetRect(&rc, 1, 2, 30, 20);
    pts[0].x = 1; pts[0].y = 2; pts[1].x = 30; pts[1].y = 3; pts[2].x = 15; pts[2].y = 25;
    pts[3].x = 40; pts[3].y = 5; pts[4].x = 45; pts[4].y = 20; pts[5].x = 35; pts[5].y = 20;
    cnt[0] = 3; cnt[1] = 3;
    dx[0] = 7; dx[1] = 8; dx[2] = 9; dx[3] = 10;

    rb("empty"); re();
    rb("SetBkColor"); rr(SetBkColor(rec, RGB(1, 2, 3))); re();
    rb("SetBkMode"); rr(SetBkMode(rec, TRANSPARENT)); re();
    rb("SetMapMode"); rr(SetMapMode(rec, MM_ANISOTROPIC)); re();
    rb("SetROP2"); rr(SetROP2(rec, R2_XORPEN)); re();
    rb("SetRelAbs"); rr(SetRelAbs(rec, 2)); re();
    rb("SetPolyFillMode"); rr(SetPolyFillMode(rec, WINDING)); re();
    rb("SetStretchBltMode"); rr(SetStretchBltMode(rec, COLORONCOLOR)); re();
    rb("SetTextCharacterExtra"); rr(SetTextCharacterExtra(rec, 3)); re();
    rb("SetTextColor"); rr(SetTextColor(rec, RGB(4, 5, 6))); re();
    rb("SetTextJustification"); rr(SetTextJustification(rec, 7, 2)); re();
    rb("SetWindowOrg"); rr(SetWindowOrg(rec, 11, 12)); re();
    rb("SetWindowExt"); rr(SetWindowExt(rec, 13, 14)); re();
    rb("SetViewportOrg"); rr(SetViewportOrg(rec, 15, 16)); re();
    rb("SetViewportExt"); rr(SetViewportExt(rec, 17, 18)); re();
    rb("OffsetWindowOrg"); rr(OffsetWindowOrg(rec, 19, 20)); re();
    rb("ScaleWindowExt"); rr(ScaleWindowExt(rec, 1, 2, 3, 4)); re();
    rb("OffsetViewportOrg"); rr(OffsetViewportOrg(rec, 21, 22)); re();
    rb("ScaleViewportExt"); rr(ScaleViewportExt(rec, 5, 6, 7, 8)); re();
    rb("LineTo"); rr(LineTo(rec, 23, 24)); re();
    rb("MoveTo"); rr(MoveTo(rec, 25, 26)); re();
    rb("ExcludeClipRect"); rr(ExcludeClipRect(rec, 1, 2, 3, 4)); re();
    rb("IntersectClipRect"); rr(IntersectClipRect(rec, 5, 6, 7, 8)); re();
    rb("Arc"); rr(Arc(rec, 1, 2, 3, 4, 5, 6, 7, 8)); re();
    rb("Ellipse"); rr(Ellipse(rec, 9, 10, 11, 12)); re();
    rb("FloodFill"); rr(FloodFill(rec, 5, 6, RGB(7, 8, 9))); re();
    rb("ExtFloodFill"); rr(ExtFloodFill(rec, 5, 6, RGB(7, 8, 9), FLOODFILLSURFACE)); re();
    rb("Pie"); rr(Pie(rec, 11, 12, 13, 14, 15, 16, 17, 18)); re();
    rb("Chord"); rr(Chord(rec, 21, 22, 23, 24, 25, 26, 27, 28)); re();
    rb("Rectangle"); rr(Rectangle(rec, 31, 32, 33, 34)); re();
    rb("RoundRect"); rr(RoundRect(rec, 1, 2, 3, 4, 5, 6)); re();
    rb("PatBlt"); rr(PatBlt(rec, 1, 2, 3, 4, PATINVERT)); re();
    rb("SaveDC"); rr(SaveDC(rec)); re();
    rb("RestoreDC -1"); rr(RestoreDC(rec, -1)); re();
    rb("RestoreDC 1"); rr(RestoreDC(rec, 1)); re();
    rb("SaveDC RestoreDC"); SaveDC(rec); SaveDC(rec); rr(RestoreDC(rec, -2)); re();
    rb("SetPixel"); rr(SetPixel(rec, 3, 4, RGB(5, 6, 7))); re();
    rb("OffsetClipRgn"); rr(OffsetClipRgn(rec, 5, 6)); re();
    rb("TextOut 3"); rr(TextOut(rec, 1, 2, abcz, 3)); re();
    rb("TextOut 4"); rr(TextOut(rec, 1, 2, abcdz, 4)); re();
    rb("TextOut 0"); rr(TextOut(rec, 1, 2, abcz, 0)); re();
    rb("ExtTextOut plain"); rr(ExtTextOut(rec, 1, 2, 0, NULL, abcz, 3, NULL)); re();
    rb("ExtTextOut rect opt 0"); rr(ExtTextOut(rec, 1, 2, 0, &rc, abcz, 3, NULL)); re();
    rb("ExtTextOut opaque"); rr(ExtTextOut(rec, 1, 2, ETO_OPAQUE, &rc, abcdz, 4, NULL)); re();
    rb("ExtTextOut clipped dx"); rr(ExtTextOut(rec, 1, 2, ETO_CLIPPED, &rc, abcz, 3, dx)); re();
    rb("ExtTextOut dx"); rr(ExtTextOut(rec, 1, 2, 0, NULL, abcz, 3, dx)); re();
    unchecked = 1;                      /* 3.1 reads a rectangle at NULL */
    rb("ExtTextOut opt no rect"); rr(ExtTextOut(rec, 1, 2, ETO_OPAQUE, NULL, abcz, 3, NULL)); re();
    unchecked = 0;
    rb("ExtTextOut 0"); rr(ExtTextOut(rec, 1, 2, ETO_OPAQUE, &rc, abcz, 0, NULL)); re();
    rb("Polygon"); rr(Polygon(rec, pts, 3)); re();
    rb("Polyline"); rr(Polyline(rec, pts, 4)); re();
    rb("PolyPolygon"); rr(PolyPolygon(rec, pts, cnt, 2)); re();
    rb("SetTextAlign"); rr(SetTextAlign(rec, TA_BOTTOM | TA_CENTER)); re();
    rb("SetMapperFlags"); rr(SetMapperFlags(rec, 1)); re();
    rb("SetBrushOrg"); rr(SetBrushOrg(rec, 3, 4)); re();
    {
        static char cmt[] = "w16Z";
        int q = MFCOMMENT;
        rb("Escape MFCOMMENT"); rr(Escape(rec, MFCOMMENT, 3, cmt, NULL)); re();
        rb("Escape QUERYESCSUPPORT"); rr(Escape(rec, QUERYESCSUPPORT, 2, (LPCSTR)&q, NULL)); re();
        rb("Escape NEWFRAME"); rr(Escape(rec, NEWFRAME, 0, NULL, NULL)); re();
        rb("Escape 0 bytes"); rr(Escape(rec, MFCOMMENT, 0, NULL, NULL)); re();
    }
    {
        HBITMAP mono = pattern_bitmap(1), col = pattern_bitmap(0);
        HDC s = CreateCompatibleDC(hscr);
        HBITMAP os = SelectObject(s, mono);
        rb("BitBlt mono"); rr(BitBlt(rec, 1, 2, 32, 8, s, 0, 0, SRCCOPY)); re();
        rb("BitBlt mono part"); rr(BitBlt(rec, 1, 2, 5, 3, s, 2, 1, SRCAND)); re();
        rb("StretchBlt mono"); rr(StretchBlt(rec, 1, 2, 32, 16, s, 0, 0, 16, 8, SRCCOPY)); re();
        SelectObject(s, col);
        rb("BitBlt colour"); rr(BitBlt(rec, 1, 2, 16, 8, s, 0, 0, SRCCOPY)); re();
        rb("StretchBlt colour"); rr(StretchBlt(rec, 1, 2, 8, 4, s, 1, 1, 15, 7, SRCPAINT)); re();
        SelectObject(s, os);
        rb("BitBlt no source"); rr(BitBlt(rec, 1, 2, 3, 4, NULL, 0, 0, PATCOPY)); re();
        rb("StretchBlt no source"); rr(StretchBlt(rec, 1, 2, 3, 4, NULL, 0, 0, 5, 6, DSTINVERT)); re();
        rb("BitBlt from screen"); rr(BitBlt(rec, 0, 0, 4, 2, hscr, 0, 0, SRCCOPY)); re();
        DeleteDC(s);
        DeleteObject(mono);
        DeleteObject(col);
    }
    rb("SetDIBitsToDevice"); rr(SetDIBitsToDevice(rec, 1, 2, 8, 4, 0, 0, 0, 4, dib4.bits,
                                (BITMAPINFO FAR *)&dib4, DIB_RGB_COLORS)); re();
    rb("SetDIBitsToDevice part"); rr(SetDIBitsToDevice(rec, 1, 2, 5, 2, 1, 1, 1, 2, dib4.bits + 4,
                                     (BITMAPINFO FAR *)&dib4, DIB_RGB_COLORS)); re();
    rb("StretchDIBits"); rr(StretchDIBits(rec, 1, 2, 16, 8, 0, 0, 8, 4, dib4.bits,
                            (BITMAPINFO FAR *)&dib4, DIB_RGB_COLORS, SRCCOPY)); re();
    rb("StretchDIBits pal"); rr(StretchDIBits(rec, 1, 2, 16, 8, 0, 0, 8, 4, dib4.bits,
                                (BITMAPINFO FAR *)&dib4, DIB_PAL_COLORS, SRCINVERT)); re();
    /* USER's drawing on a metafile DC */
    rb("FillRect"); FillRect(rec, &rc, GetStockObject(GRAY_BRUSH)); re();
    /* (DrawText's height comes from GetTextMetrics, which fails here) */
    rb("DrawText"); obs_env("DrawText ret", DrawText(rec, "ab", 2, &rc, DT_LEFT | DT_SINGLELINE | DT_NOCLIP)); re();
}

/* ---- objects ---- */

static void sec_objects(void)
{
    HPEN p1 = CreatePen(PS_DASH, 1, RGB(1, 2, 3)), p2 = CreatePen(PS_SOLID, 3, RGB(4, 5, 6));
    HBRUSH b1 = CreateSolidBrush(RGB(7, 8, 9)), bh = CreateHatchBrush(HS_CROSS, RGB(10, 11, 12));
    HBITMAP pat = pattern_bitmap(1), cpat = pattern_bitmap(0);
    HBRUSH bp = CreatePatternBrush(pat), bcp = CreatePatternBrush(cpat);
    HGLOBAL hdib;
    HBRUSH bd = 0;
    HRGN r1 = CreateRectRgn(1, 2, 30, 40), r2 = CreateRectRgn(0, 0, 10, 10), rc2 = CreateRectRgn(0, 0, 1, 1);
    LOGFONT lf;
    HFONT f;
    int i;
    BYTE FAR *q;
    CombineRgn(rc2, r1, r2, RGN_XOR);   /* a complex one */
    /* 3.1 copies the block's GlobalSize: a size it keeps, zeroed */
    hdib = GlobalAlloc(GMEM_MOVEABLE | GMEM_ZEROINIT, 128);
    q = GlobalLock(hdib);
    for (i = 0; i < sizeof dib4; i++)
        q[i] = ((BYTE *)&dib4)[i];
    GlobalUnlock(hdib);
    bd = CreateDIBPatternBrush(hdib, DIB_RGB_COLORS);

    rb("select pen"); rr((WORD)SelectObject(rec, p1) == (WORD)p1); re();
    rb("select pen twice"); SelectObject(rec, p1); SelectObject(rec, p1); re();
    rb("select pen p1 p2 p1"); SelectObject(rec, p1); SelectObject(rec, p2); SelectObject(rec, p1); re();
    rb("select and delete");
    SelectObject(rec, p1);
    SelectObject(rec, p2);
    obs("select and delete DeleteObject p1", DeleteObject(p1));
    SelectObject(rec, b1);
    re();
    p1 = CreatePen(PS_DASH, 1, RGB(1, 2, 3));
    rb("delete selected");
    SelectObject(rec, p1);
    obs("delete selected DeleteObject", DeleteObject(p1));
    SelectObject(rec, b1);
    re();
    p1 = CreatePen(PS_DASH, 1, RGB(1, 2, 3));
    rb("stock pen");
    rr((WORD)SelectObject(rec, GetStockObject(BLACK_PEN)) == (WORD)GetStockObject(BLACK_PEN));
    SelectObject(rec, GetStockObject(BLACK_PEN));
    re();
    unchecked = 1;                      /* System's face: a byte of 3.1's after the NUL */
    rb("stock objects");
    SelectObject(rec, GetStockObject(NULL_PEN));
    SelectObject(rec, GetStockObject(WHITE_BRUSH));
    SelectObject(rec, GetStockObject(HOLLOW_BRUSH));
    SelectObject(rec, GetStockObject(SYSTEM_FONT));
    SelectObject(rec, GetStockObject(ANSI_VAR_FONT));
    re();
    unchecked = 0;
    rb("stock objects 2");
    SelectObject(rec, GetStockObject(NULL_PEN));
    SelectObject(rec, GetStockObject(WHITE_PEN));
    SelectObject(rec, GetStockObject(WHITE_BRUSH));
    SelectObject(rec, GetStockObject(LTGRAY_BRUSH));
    SelectObject(rec, GetStockObject(HOLLOW_BRUSH));
    SelectObject(rec, GetStockObject(ANSI_VAR_FONT));
    re();
    unchecked = 1;                      /* the bytes after the faces are 3.1's own */
    rb("stock fonts");
    SelectObject(rec, GetStockObject(ANSI_FIXED_FONT));
    SelectObject(rec, GetStockObject(OEM_FIXED_FONT));
    re();
    unchecked = 0;
    rb("solid brush"); SelectObject(rec, b1); re();
    rb("hatch brush"); SelectObject(rec, bh); re();
    rb("pattern brush"); rr((WORD)SelectObject(rec, bp) != 0); re();
    rb("colour pattern brush"); SelectObject(rec, bcp); re();
    rb("DIB pattern brush"); rr((WORD)SelectObject(rec, bd) != 0); re();
    /* a LOGFONT with bytes after the face's end */
    for (i = 0; i < sizeof lf; i++)
        ((BYTE *)&lf)[i] = 0x5A;
    lf.lfHeight = -13;
    lf.lfWidth = 0;
    lf.lfEscapement = lf.lfOrientation = 0;
    lf.lfWeight = 700;
    lf.lfItalic = 1;
    lf.lfUnderline = lf.lfStrikeOut = 0;
    lf.lfCharSet = 0;
    lf.lfOutPrecision = lf.lfClipPrecision = lf.lfQuality = 0;
    lf.lfPitchAndFamily = 0x22;
    lstrcpy(lf.lfFaceName, "Helv");
    f = CreateFontIndirect(&lf);
    rb("font"); SelectObject(rec, f); re();
    rb("region"); rr(SelectObject(rec, r1)); re();
    rb("SelectClipRgn"); rr(SelectClipRgn(rec, r1)); re();
    rb("SelectClipRgn complex"); rr(SelectClipRgn(rec, rc2)); re();
    rb("SelectClipRgn NULL"); rr(SelectClipRgn(rec, NULL)); re();
    rb("SelectClipRgn twice"); SelectClipRgn(rec, r1); SelectClipRgn(rec, r1); re();
    rb("FillRgn"); rr(FillRgn(rec, r1, b1)); re();
    rb("FrameRgn"); rr(FrameRgn(rec, rc2, bh, 2, 3)); re();
    rb("InvertRgn"); rr(InvertRgn(rec, r1)); re();
    rb("PaintRgn"); rr(PaintRgn(rec, r1)); re();
    rb("FillRgn twice"); FillRgn(rec, r1, b1); FillRgn(rec, r1, b1); re();
    {
        HBITMAP bm = CreateCompatibleBitmap(hscr, 8, 8);
        rb("select bitmap"); rr((WORD)SelectObject(rec, bm)); re();
        DeleteObject(bm);
    }
    /* object slots: more at once */
    rb("many objects");
    SelectObject(rec, p1);
    SelectObject(rec, b1);
    SelectObject(rec, f);
    SelectObject(rec, p2);
    SelectObject(rec, bh);
    SelectClipRgn(rec, r2);
    re();
    {
        /* a pen made for the metafile, deleted after it is selected
           elsewhere: the slot reused */
        HPEN t1 = CreatePen(PS_SOLID, 0, RGB(1, 1, 1)), t2 = CreatePen(PS_SOLID, 0, RGB(2, 2, 2));
        HPEN t3 = CreatePen(PS_SOLID, 0, RGB(3, 3, 3));
        rb("slot reuse");
        SelectObject(rec, t1);
        SelectObject(rec, b1);
        SelectObject(rec, t2);
        DeleteObject(t1);
        SelectObject(rec, t3);
        DeleteObject(t2);
        DeleteObject(t3);
        re();
    }
    /* palettes */
    {
        struct { WORD v, n; PALETTEENTRY e[4]; } lp;
        HPALETTE pal;
        PALETTEENTRY ne[2];
        lp.v = 0x300;
        lp.n = 4;
        for (i = 0; i < 4; i++) {
            lp.e[i].peRed = (BYTE)(i * 10);
            lp.e[i].peGreen = (BYTE)(i * 20);
            lp.e[i].peBlue = (BYTE)(i * 30);
            lp.e[i].peFlags = (BYTE)(i == 2 ? PC_RESERVED : 0);
        }
        ne[0].peRed = 1; ne[0].peGreen = 2; ne[0].peBlue = 3; ne[0].peFlags = 0;
        ne[1].peRed = 4; ne[1].peGreen = 5; ne[1].peBlue = 6; ne[1].peFlags = PC_RESERVED;
        pal = CreatePalette((LOGPALETTE FAR *)&lp);
        rb("SelectPalette"); rr((WORD)SelectPalette(rec, pal, FALSE) != 0); re();
        rb("RealizePalette"); rr(RealizePalette(rec)); re();
        /* 3.1 writes a change once for every metafile DC the palette was
           ever selected in (handles reused), AnimatePalette's record
           holds its arguments, not the entries: see the dumps */
        unchecked = 1;
        rb("palette changes");
        SelectPalette(rec, pal, FALSE);
        RealizePalette(rec);
        obs("palette changes SetPaletteEntries", SetPaletteEntries(pal, 1, 2, ne));
        AnimatePalette(pal, 2, 1, ne + 1);
        obs("palette changes ResizePalette", ResizePalette(pal, 6));
        SelectPalette(rec, GetStockObject(DEFAULT_PALETTE), FALSE);
        obs("palette changes SetPaletteEntries after", SetPaletteEntries(pal, 0, 1, ne));
        re();
        rb("palette SetPaletteEntries");
        SelectPalette(rec, pal, FALSE);
        obs("palette SetPaletteEntries only", SetPaletteEntries(pal, 1, 2, ne));
        re();
        rb("palette AnimatePalette");
        SelectPalette(rec, pal, FALSE);
        AnimatePalette(pal, 1, 2, ne);
        re();
        rb("palette AnimatePalette realized");
        SelectPalette(rec, pal, FALSE);
        RealizePalette(rec);
        AnimatePalette(pal, 3, 1, ne + 1);
        re();
        rb("palette ResizePalette");
        SelectPalette(rec, pal, FALSE);
        obs("palette ResizePalette only", ResizePalette(pal, 3));
        re();
        rb("palette ResizePalette two");
        SelectPalette(rec, pal, FALSE);
        SelectPalette(rec, GetStockObject(DEFAULT_PALETTE), FALSE);
        ResizePalette(pal, 5);
        re();
        unchecked = 0;
        rb("palette unselected changes");
        obs("palette unselected SetPaletteEntries", SetPaletteEntries(pal, 0, 1, ne));
        re();
        DeleteObject(pal);
    }
    DeleteObject(p1);
    DeleteObject(p2);
    DeleteObject(b1);
    DeleteObject(bh);
    DeleteObject(bp);
    DeleteObject(bcp);
    if (bd)
        DeleteObject(bd);
    GlobalFree(hdib);
    DeleteObject(pat);
    DeleteObject(cpat);
    DeleteObject(r1);
    DeleteObject(r2);
    DeleteObject(rc2);
    DeleteObject(f);
}

/* ---- a scene: drawn directly or recorded and played ---- */

static void scene(HDC d, int kind)
{
    HPEN p = CreatePen(PS_SOLID, 1, RGB(255, 0, 0)), op;
    HBRUSH b = CreateSolidBrush(RGB(0, 0, 255)), ob;
    POINT pts[4];
    pts[0].x = 40; pts[0].y = 4; pts[1].x = 60; pts[1].y = 10; pts[2].x = 50; pts[2].y = 30;
    switch (kind) {
    case 0:                             /* plain */
        op = SelectObject(d, p);
        ob = SelectObject(d, b);
        Rectangle(d, 2, 2, 30, 20);
        MoveTo(d, 0, 40);
        LineTo(d, 60, 44);
        /* (a Polygon's right edge: w16's fill differs from 3.1's, 11.6) */
        Polyline(d, pts, 3);
        SetPixel(d, 62, 46, RGB(0, 255, 0));
        PatBlt(d, 33, 33, 9, 6, PATINVERT);
        SetBkColor(d, RGB(255, 255, 0));
        TextOut(d, 4, 22, "Hi", 2);
        SelectObject(d, op);
        SelectObject(d, ob);
        break;
    case 1:                             /* the window mapped (MM_ANISOTROPIC set outside) */
        SetWindowOrg(d, 10, 10);
        SetWindowExt(d, 200, 100);
        op = SelectObject(d, p);
        ob = SelectObject(d, b);
        Rectangle(d, 20, 20, 120, 80);
        MoveTo(d, 10, 10);
        LineTo(d, 200, 100);
        SelectObject(d, op);
        SelectObject(d, ob);
        break;
    case 2:                             /* its own mapping mode */
        SetMapMode(d, MM_ANISOTROPIC);
        SetWindowExt(d, 100, 100);
        SetViewportExt(d, 50, 40);
        SetViewportOrg(d, 4, 4);
        op = SelectObject(d, p);
        Rectangle(d, 0, 0, 100, 100);
        SelectObject(d, op);
        break;
    case 3:                             /* nested SaveDC/RestoreDC */
        SaveDC(d);
        SelectObject(d, b);
        SaveDC(d);
        SelectObject(d, p);
        SetROP2(d, R2_NOT);
        Rectangle(d, 2, 2, 20, 20);
        RestoreDC(d, -1);
        Rectangle(d, 22, 2, 40, 20);
        RestoreDC(d, -1);
        Rectangle(d, 42, 2, 60, 20);
        break;
    case 4:                             /* clipping */
        IntersectClipRect(d, 5, 5, 40, 30);
        ExcludeClipRect(d, 10, 10, 20, 20);
        PatBlt(d, 0, 0, PW, PH, BLACKNESS);
        break;
    case 5:                             /* a DIB and a bitmap */
        StretchDIBits(d, 2, 2, 16, 8, 0, 0, 8, 4, dib4.bits, (BITMAPINFO FAR *)&dib4,
                      DIB_RGB_COLORS, SRCCOPY);
        SetDIBitsToDevice(d, 30, 2, 8, 4, 0, 0, 0, 4, dib4.bits, (BITMAPINFO FAR *)&dib4,
                          DIB_RGB_COLORS);
        {
            HBITMAP col = pattern_bitmap(0), mono = pattern_bitmap(1);
            HDC s = CreateCompatibleDC(hscr);
            HBITMAP os = SelectObject(s, col);
            BitBlt(d, 2, 20, 16, 8, s, 0, 0, SRCCOPY);
            SelectObject(s, mono);
            SetTextColor(d, RGB(255, 0, 0));
            SetBkColor(d, RGB(0, 255, 0));
            BitBlt(d, 30, 20, 16, 8, s, 0, 0, SRCCOPY);
            SelectObject(s, os);
            DeleteDC(s);
            DeleteObject(col);
            DeleteObject(mono);
        }
        break;
    case 6:                             /* the pen and brush left selected */
        SelectObject(d, p);
        SelectObject(d, b);
        SetBkColor(d, RGB(0, 255, 255));
        SetTextColor(d, RGB(255, 0, 255));
        SetMapMode(d, MM_ANISOTROPIC);
        SetWindowExt(d, 2, 2);
        SetViewportExt(d, 1, 1);
        Rectangle(d, 4, 4, 40, 40);
        MoveTo(d, 7, 9);
        break;
    }
    if (kind != 6) {
        DeleteObject(p);
        DeleteObject(b);
    }
}

static HDC pdc, pdc2;
static HBITMAP pbm, pbm2, pold, pold2;

/* the VGA's 16 colours as letters (anything else '?') */
static char letter(DWORD c)
{
    static const DWORD vga[16] = {
        0x000000L, 0x000080L, 0x008000L, 0x008080L, 0x800000L, 0x800080L, 0x808000L, 0xC0C0C0L,
        0x808080L, 0x0000FFL, 0x00FF00L, 0x00FFFFL, 0xFF0000L, 0xFF00FFL, 0xFFFF00L, 0xFFFFFFL };
    int i;
    for (i = 0; i < 16; i++)
        if (vga[i] == c)
            return "0123456789ABCDEF"[i];
    return c == 0xFFFFFFFFL ? '-' : '?';
}

static int rows;                        /* print the pixels as ROW lines */

static WORD hash(HDC d)
{
    WORD h = 0, x, y;
    char row[PW + 1];
    SetMapMode(d, MM_TEXT);
    SetWindowOrg(d, 0, 0);
    SetViewportOrg(d, 0, 0);
    if (rows)
        for (y = 0; y < PH; y++) {
            for (x = 0; x < PW; x++)
                row[x] = letter(GetPixel(d, x, y));
            row[PW] = 0;
            out("ROW ");
            out(row);
            out("\r\n");
        }
    for (y = 0; y < PH; y++)
        for (x = 0; x < PW; x++) {
            DWORD c = GetPixel(d, x, y);
            h = (WORD)((h << 5) | (h >> 11)) + LOWORD(c) + HIWORD(c) * 3 + 1;
        }
    return h;
}

static void white(HDC d)
{
    SetMapMode(d, MM_TEXT);
    SetWindowOrg(d, 0, 0);
    SetViewportOrg(d, 0, 0);
    PatBlt(d, 0, 0, PW, PH, WHITENESS);
}

static HMETAFILE record(int kind)
{
    HDC m = CreateMetaFile(NULL);
    scene(m, kind);
    return CloseMetaFile(m);
}

/* the scene played and drawn directly: the hash (checked) and whether
   they agree */
static void play_case(const char *name, int kind, int dmap)
{
    HMETAFILE h = record(kind);
    WORD a, b;
    int i;
    for (i = 0; i < 2; i++) {
        HDC d = i ? pdc2 : pdc;
        white(d);
        if (dmap) {
            SetMapMode(d, MM_ANISOTROPIC);
            SetViewportExt(d, PW, PH);
            SetWindowExt(d, PW * 2, PH * 2);
            SetViewportOrg(d, 2, 3);
        }
        if (i == 0)
            obs2(name, "PlayMetaFile", PlayMetaFile(d, h));
        else {
            SaveDC(d);                  /* the direct drawing's own changes go */
            scene(d, kind);
            RestoreDC(d, -1);
        }
    }
    SetMapMode(pdc, MM_TEXT);
    SetMapMode(pdc2, MM_TEXT);
    {
        RECT rc;
        GetClipBox(pdc, &rc);
        obs2(name, "clip box right after play", rc.right);
        GetClipBox(pdc2, &rc);
        obs2(name, "clip box right after direct", rc.right);
    }
    out("ROWS played\r\n");
    rows = 1;
    a = hash(pdc);
    out("ROWS direct\r\n");
    b = hash(pdc2);
    rows = 0;
    /* bitmaps shrunk by the mapping: w16 stretches by its own rule
       (ARCHITECTURE.md 11.6), so only that playback and direct drawing
       agree is checked there */
    unchecked = kind == 5 && dmap;
    obs2(name, "played", a);
    obs2(name, "direct", b);
    unchecked = 0;
    obs2(name, "same", a == b);
    DeleteMetaFile(h);
}

static void sec_play(void)
{
    HMETAFILE h;
    pbm = CreateCompatibleBitmap(hscr, PW, PH);
    pbm2 = CreateCompatibleBitmap(hscr, PW, PH);
    pdc = CreateCompatibleDC(hscr);
    pdc2 = CreateCompatibleDC(hscr);
    pold = SelectObject(pdc, pbm);
    pold2 = SelectObject(pdc2, pbm2);
    play_case("play plain", 0, 0);
    play_case("play plain mapped", 0, 1);
    play_case("play window", 1, 1);
    play_case("play own mapping", 2, 0);
    play_case("play own mapping mapped", 2, 1);
    play_case("play SaveDC", 3, 0);
    play_case("play clip", 4, 0);
    play_case("play clip mapped", 4, 1);
    play_case("play bitmaps", 5, 0);
    play_case("play bitmaps mapped", 5, 1);
    {
        HMETAFILE w = record(1);
        HMETAFILE s = record(3);
        show_mf("scene window", w);
        show_mf("scene SaveDC", s);
        show_mf("scene bitmaps", h = record(5));
        DeleteMetaFile(h);
        DeleteMetaFile(w);
        DeleteMetaFile(s);
    }
    /* the destination's state after PlayMetaFile */
    {
        HPEN p0 = CreatePen(PS_SOLID, 1, RGB(0, 128, 0));
        HBRUSH b0 = CreateSolidBrush(RGB(128, 0, 0));
        HPEN cur;
        HBRUSH curb;
        int lvl;
        white(pdc);
        SelectObject(pdc, p0);
        SelectObject(pdc, b0);
        SetBkColor(pdc, RGB(1, 1, 1));
        SetTextColor(pdc, RGB(2, 2, 2));
        MoveTo(pdc, 3, 3);
        h = record(6);
        obs("state PlayMetaFile", PlayMetaFile(pdc, h));
        cur = SelectObject(pdc, GetStockObject(BLACK_PEN));
        curb = SelectObject(pdc, GetStockObject(BLACK_BRUSH));
        obs("state pen is the original", cur == p0);
        obs("state pen IsGDIObject", IsGDIObject(cur));
        obs("state brush is the original", curb == b0);
        obs("state brush IsGDIObject", IsGDIObject(curb));
        obs_dw("state bk colour", "", GetBkColor(pdc));
        obs_dw("state text colour", "", GetTextColor(pdc));
        obs("state map mode", GetMapMode(pdc));
        obs_dw("state window ext", "", GetWindowExt(pdc));
        obs_dw("state position", "", GetCurrentPosition(pdc));
        lvl = SaveDC(pdc);
        obs("state save level", lvl);
        RestoreDC(pdc, -1);
        obs("state hash", hash(pdc));
        DeleteMetaFile(h);
        DeleteObject(p0);
        DeleteObject(b0);
    }
    /* unbalanced SaveDC / RestoreDC in a metafile */
    {
        HDC m = CreateMetaFile(NULL);
        int lvl;
        SaveDC(m);
        SetBkColor(m, RGB(0, 0, 255));
        h = CloseMetaFile(m);
        white(pdc);
        obs("SaveDC alone PlayMetaFile", PlayMetaFile(pdc, h));
        lvl = SaveDC(pdc);
        obs("SaveDC alone save level", lvl);
        RestoreDC(pdc, -1);
        obs_dw("SaveDC alone bk colour", "", GetBkColor(pdc));
        DeleteMetaFile(h);
        m = CreateMetaFile(NULL);
        RestoreDC(m, -1);
        SetBkColor(m, RGB(0, 255, 0));
        h = CloseMetaFile(m);
        SetBkColor(pdc, RGB(255, 0, 0));
        SaveDC(pdc);
        SetBkColor(pdc, RGB(255, 255, 255));
        obs("RestoreDC alone PlayMetaFile", PlayMetaFile(pdc, h));
        lvl = SaveDC(pdc);
        obs("RestoreDC alone save level", lvl);
        RestoreDC(pdc, -1);
        obs_dw("RestoreDC alone bk colour", "", GetBkColor(pdc));
        RestoreDC(pdc, -1);
        obs_dw("RestoreDC alone bk colour popped", "", GetBkColor(pdc));
        DeleteMetaFile(h);
        m = CreateMetaFile(NULL);
        SaveDC(m);
        SaveDC(m);
        RestoreDC(m, 1);
        SetBkColor(m, RGB(0, 255, 0));
        h = CloseMetaFile(m);
        SetBkColor(pdc, RGB(255, 0, 0));
        SaveDC(pdc);
        SetBkColor(pdc, RGB(255, 255, 255));
        obs("RestoreDC 1 PlayMetaFile", PlayMetaFile(pdc, h));
        lvl = SaveDC(pdc);
        obs("RestoreDC 1 save level", lvl);
        RestoreDC(pdc, -1);
        obs_dw("RestoreDC 1 bk colour", "", GetBkColor(pdc));
        RestoreDC(pdc, -1);
        DeleteMetaFile(h);
    }
    /* a metafile played into a metafile */
    {
        HDC m = CreateMetaFile(NULL);
        HMETAFILE inner = record(0), outer;
        SetBkMode(m, OPAQUE);
        obs("into meta PlayMetaFile", PlayMetaFile(m, inner));
        outer = CloseMetaFile(m);
        show_mf("into meta", outer);
        DeleteMetaFile(outer);
        DeleteMetaFile(inner);
    }
    /* the same metafile played twice; and into the screen DC's mapping */
    h = record(0);
    white(pdc);
    PlayMetaFile(pdc, h);
    obs("twice PlayMetaFile", PlayMetaFile(pdc, h));
    obs("twice hash", hash(pdc));
    DeleteMetaFile(h);
}

/* ---- EnumMetaFile, PlayMetaFileRecord ---- */

static WORD enum_n, enum_stop;
static HDC enum_dc;
static WORD enum_sel;
static HGDIOBJ enum_objs[8];

int FAR PASCAL __export enum_proc(HDC dc, HANDLETABLE FAR *ht, METARECORD FAR *mr, int nobj, LPARAM lp)
{
    char n[60];
    int i, used = 0;
    numname(n, "enum", "", enum_n, " fn");
    obs(n, mr->rdFunction);
    numname(n, "enum", "", enum_n, " size");
    obs(n, LOWORD(mr->rdSize));
    numname(n, "enum", "", enum_n, " cObj");
    obs(n, nobj);
    numname(n, "enum", "", enum_n, " hdc same");
    obs(n, dc == enum_dc);
    numname(n, "enum", "", enum_n, " lParam");
    obs(n, LOWORD(lp));
    numname(n, "enum", "", enum_n, " in the bits");
    obs(n, SELECTOROF(mr) == enum_sel);
    for (i = 0; i < nobj; i++)
        used += ht->objectHandle[i] != 0;
    numname(n, "enum", "", enum_n, " slots before");
    obs(n, used);
    PlayMetaFileRecord(dc, ht, mr, nobj);
    used = 0;
    for (i = 0; i < nobj; i++) {
        used += ht->objectHandle[i] != 0;
        if (i < 8 && ht->objectHandle[i])
            enum_objs[i] = ht->objectHandle[i];
    }
    numname(n, "enum", "", enum_n, " slots after");
    obs(n, used);
    if (nobj > 0) {
        numname(n, "enum", "", enum_n, " slot0 type");
        obs(n, ht->objectHandle[0] ? IsGDIObject(ht->objectHandle[0]) : 0);
    }
    enum_n++;
    return enum_n != enum_stop;
}

static void sec_enum(void)
{
    FARPROC fp = MakeProcInstance((FARPROC)enum_proc, hinst);
    HMETAFILE h = record(0);
    int i;
    white(pdc);
    enum_dc = pdc;
    enum_sel = SELECTOROF(GlobalLock((HGLOBAL)h));
    GlobalUnlock((HGLOBAL)h);
    enum_n = 0;
    enum_stop = 0xFFFF;
    for (i = 0; i < 8; i++)
        enum_objs[i] = 0;
    obs("EnumMetaFile", EnumMetaFile(pdc, h, (MFENUMPROC)fp, 0x1234));
    obs("EnumMetaFile records", enum_n);
    obs("EnumMetaFile hash", hash(pdc));
    for (i = 0; i < 3; i++) {
        char n[40];
        numname(n, "EnumMetaFile obj", "", i, " after");
        obs(n, enum_objs[i] ? IsGDIObject(enum_objs[i]) : 0xEEEE);
    }
    /* stopped at the third record */
    enum_n = 0;
    enum_stop = 3;
    for (i = 0; i < 8; i++)
        enum_objs[i] = 0;
    out("EnumMetaFile stop\r\n");
    obs("EnumMetaFile stopped", EnumMetaFile(pdc, h, (MFENUMPROC)fp, 0x1234));
    obs("EnumMetaFile stopped records", enum_n);
    for (i = 0; i < 3; i++) {
        char n[40];
        numname(n, "EnumMetaFile stopped obj", "", i, " after");
        obs(n, enum_objs[i] ? IsGDIObject(enum_objs[i]) : 0xEEEE);
    }
    DeleteMetaFile(h);
    FreeProcInstance(fp);
    /* PlayMetaFileRecord with a table of the probe's own */
    {
        struct { HGDIOBJ h[4]; } t;
        struct { DWORD sz; WORD fn; WORD p[8]; } r;
        HGDIOBJ cur;
        for (i = 0; i < 4; i++)
            t.h[i] = 0;
        t.h[0] = GetStockObject(BLACK_BRUSH);
        r.sz = 8;
        r.fn = META_CREATEPENINDIRECT;
        r.p[0] = PS_SOLID; r.p[1] = 1; r.p[2] = 0; r.p[3] = 0x00FF; r.p[4] = 0x0000;
        PlayMetaFileRecord(pdc, (HANDLETABLE FAR *)&t, (METARECORD FAR *)&r, 4);
        obs("PMFR create slot0 kept", t.h[0] == GetStockObject(BLACK_BRUSH));
        obs("PMFR create slot1 type", t.h[1] ? IsGDIObject(t.h[1]) : 0);
        obs("PMFR create slot2", t.h[2] != 0);
        r.sz = 4;
        r.fn = META_SELECTOBJECT;
        r.p[0] = 1;
        PlayMetaFileRecord(pdc, (HANDLETABLE FAR *)&t, (METARECORD FAR *)&r, 4);
        cur = SelectObject(pdc, GetStockObject(BLACK_PEN));
        obs("PMFR select selected it", cur == t.h[1] && cur != 0);
        SelectObject(pdc, cur);
        r.fn = META_DELETEOBJECT;
        cur = t.h[1];
        PlayMetaFileRecord(pdc, (HANDLETABLE FAR *)&t, (METARECORD FAR *)&r, 4);
        obs("PMFR delete slot1", t.h[1] != 0);
        obs("PMFR delete object", IsGDIObject(cur));
        SelectObject(pdc, GetStockObject(BLACK_PEN));
        obs("PMFR delete object after deselect", IsGDIObject(cur));
        if (IsGDIObject(cur))
            DeleteObject(cur);
        /* a record with a slot out of the table */
        r.fn = META_SELECTOBJECT;
        r.p[0] = 9;
        PlayMetaFileRecord(pdc, (HANDLETABLE FAR *)&t, (METARECORD FAR *)&r, 4);
        obs("PMFR bad index survived", 1);
        r.sz = 7;
        r.fn = META_CREATEBRUSHINDIRECT;
        r.p[0] = BS_SOLID; r.p[1] = 0x00FF; r.p[2] = 0; r.p[3] = 0;
        t.h[1] = t.h[2] = t.h[3] = 0;
        t.h[0] = 0;
        PlayMetaFileRecord(pdc, (HANDLETABLE FAR *)&t, (METARECORD FAR *)&r, 1);
        obs("PMFR brush slot0 type", t.h[0] ? IsGDIObject(t.h[0]) : 0);
        if (t.h[0])
            DeleteObject(t.h[0]);
        /* (a create record with no free slot: 3.1 hangs) */
    }
}

/* ---- disk metafiles ---- */

static BYTE fbuf[2048];

static void show_file(const char *name, const char *file)
{
    OFSTRUCT of;
    int fh = OpenFile(file, &of, OF_READ);
    WORD n;
    obs2(name, "file opened", fh != HFILE_ERROR);
    if (fh == HFILE_ERROR)
        return;
    n = _lread(fh, fbuf, sizeof fbuf);
    _lclose(fh);
    obs2(name, "file bytes", n);
    show_bits(name, fbuf, n);
}

static void file_size(const char *name, const char *file)
{
    OFSTRUCT of;
    int fh = OpenFile(file, &of, OF_READ | OF_SHARE_DENY_NONE);
    obs2(name, "exists", fh != HFILE_ERROR);
    if (fh != HFILE_ERROR) {
        obs2(name, "size", (WORD)_llseek(fh, 0, 2));
        _lclose(fh);
    }
}

/* a disk metafile's handle: what its memory holds */
static void show_handle(const char *name, HMETAFILE h)
{
    BYTE FAR *p;
    WORD sz, k;
    obs2(name, "handle nonzero", h != 0);
    if (!h)
        return;
    sz = (WORD)GlobalSize((HGLOBAL)h);
    obs2(name, "GlobalSize", sz);
    obs2(name, "IsValidMetaFile", isvalidmf ? isvalidmf((HANDLE)h) : 0xEEEE);
    obs2(name, "whose", whose((HGLOBAL)h));
    p = GlobalLock((HGLOBAL)h);
    if (!p)
        return;
    obs2(name, "mtType", ((METAHEADER FAR *)p)->mtType);
    obs2(name, "mtHeaderSize", ((METAHEADER FAR *)p)->mtHeaderSize);
    obs_dw(name, "mtSize", ((METAHEADER FAR *)p)->mtSize);
    obs2(name, "mtNoObjects", ((METAHEADER FAR *)p)->mtNoObjects);
    out("MEM ");
    out(name);
    out(" ");
    for (k = 0; k < sz && k < 64; k++) {
        char t[4];
        t[0] = "0123456789ABCDEF"[p[k] >> 4];
        t[1] = "0123456789ABCDEF"[p[k] & 15];
        t[2] = ' ';
        t[3] = 0;
        out(t);
    }
    out("\r\n");
    if (sz > 18 && sz <= 64) {
        /* text after the header (the file's name?) */
        char s[48];
        WORD i, j = 0;
        for (i = 18; i < sz && j < 47; i++)
            s[j++] = p[i] >= 32 && p[i] < 127 ? p[i] : '.';
        s[j] = 0;
        obs_str(nm(name, "text"), s);
    }
    GlobalUnlock((HGLOBAL)h);
}

static void sec_disk(void)
{
    OFSTRUCT of;
    HDC m;
    HMETAFILE h, h2, h3;
    HPEN p = CreatePen(PS_SOLID, 0, RGB(0, 255, 0));
    HBRUSH b = CreateSolidBrush(RGB(255, 0, 255));
    OpenFile("C:\\GD9A.WMF", &of, OF_DELETE);
    OpenFile("C:\\GD9B.WMF", &of, OF_DELETE);
    OpenFile("C:\\GD9C.WMF", &of, OF_DELETE);
    m = CreateMetaFile("C:\\GD9A.WMF");
    obs("disk CreateMetaFile nonzero", m != 0);
    obs("disk meta DC IsGDIObject", IsGDIObject(m));
    file_size("disk while open", "C:\\GD9A.WMF");
    SelectObject(m, p);
    SelectObject(m, b);
    Rectangle(m, 0, 0, 150, 70);
    TextOut(m, 10, 10, "Hello People", 12);
    file_size("disk while recording", "C:\\GD9A.WMF");
    obs("disk GetDeviceCaps TECHNOLOGY", GetDeviceCaps(m, TECHNOLOGY));
    h = CloseMetaFile(m);
    show_handle("disk handle", h);
    show_file("disk file", "C:\\GD9A.WMF");
    obs("disk PlayMetaFile", PlayMetaFile(pdc, h));
    h2 = GetMetaFileBits(h);
    obs("disk GetMetaFileBits same", h2 == h);
    obs("disk GetMetaFileBits nonzero", h2 != 0);
    if (h2)
        show_handle("disk bits", h2);
    h = h2 ? SetMetaFileBits(h2) : 0;
    obs("disk SetMetaFileBits same", h == h2);
    h2 = CopyMetaFile(h, NULL);
    obs("disk CopyMetaFile NULL nonzero", h2 != 0);
    unchecked = 1;                      /* 3.1: 3 words of garbage at the end */
    show_mf("disk copied to memory", h2);
    unchecked = 0;
    h3 = CopyMetaFile(h2, "C:\\GD9B.WMF");
    show_handle("memory copied to disk handle", h3);
    show_file("memory copied to disk", "C:\\GD9B.WMF");
    obs("memory copied to disk DeleteMetaFile", DeleteMetaFile(h3));
    file_size("memory copied to disk after delete", "C:\\GD9B.WMF");
    obs("disk DeleteMetaFile", DeleteMetaFile(h));
    file_size("disk after DeleteMetaFile", "C:\\GD9A.WMF");
    h = GetMetaFile("C:\\GD9A.WMF");
    show_handle("GetMetaFile", h);
    obs("GetMetaFile PlayMetaFile", PlayMetaFile(pdc, h));
    h3 = CopyMetaFile(h, "C:\\GD9C.WMF");
    show_handle("disk copied to disk handle", h3);
    show_file("disk copied to disk", "C:\\GD9C.WMF");
    DeleteMetaFile(h3);
    h3 = CopyMetaFile(h, NULL);
    show_mf("GetMetaFile copied", h3);
    DeleteMetaFile(h3);
    DeleteMetaFile(h);
    DeleteMetaFile(h2);
    /* a fresh memory metafile copied: to memory, to a file */
    h = record(0);
    h2 = CopyMetaFile(h, NULL);
    show_mf("memory copied to memory", h2);
    obs("memory copied to memory whose", whose((HGLOBAL)h2));
    DeleteMetaFile(h2);
    h2 = CopyMetaFile(h, "C:\\GD9D.WMF");
    show_handle("fresh copied to disk handle", h2);
    show_file("fresh copied to disk", "C:\\GD9D.WMF");
    DeleteMetaFile(h2);
    DeleteMetaFile(h);
    DeleteObject(p);
    DeleteObject(b);
}

/* (last: with a bad name or a placeable file 3.1 may not come back) */
static void sec_disk2(void)
{
    OFSTRUCT of;
    HDC m;
    HMETAFILE h;
    /* an empty disk metafile */
    m = CreateMetaFile("C:\\GD9C.WMF");
    h = CloseMetaFile(m);
    show_file("disk empty", "C:\\GD9C.WMF");
    DeleteMetaFile(h);
    /* a placeable (Aldus) file: GetMetaFile, and its bits */
    {
        HDC mm = CreateMetaFile(NULL);
        HMETAFILE mh;
        BYTE FAR *src;
        WORD len, i, ck = 0;
        WORD hdr[11];
        int fh;
        HGLOBAL g;
        Rectangle(mm, 0, 0, 10, 10);
        mh = CloseMetaFile(mm);
        src = GlobalLock((HGLOBAL)mh);
        len = (WORD)((METAHEADER FAR *)src)->mtSize * 2;
        hdr[0] = 0xCDD7; hdr[1] = 0x9AC6; hdr[2] = 0;
        hdr[3] = 0; hdr[4] = 0; hdr[5] = 10; hdr[6] = 10; hdr[7] = 1440;
        hdr[8] = 0; hdr[9] = 0;
        for (i = 0; i < 10; i++)
            ck ^= hdr[i];
        hdr[10] = ck;
        fh = OpenFile("C:\\GD9P.WMF", &of, OF_CREATE | OF_WRITE);
        _lwrite(fh, (LPCSTR)hdr, 22);
        _lwrite(fh, (LPCSTR)src, len);
        _lclose(fh);
        h = GetMetaFile("C:\\GD9P.WMF");
        obs("placeable GetMetaFile", h != 0);
        if (h) {
            show_handle("placeable", h);
            white(pdc);
            obs("placeable PlayMetaFile", PlayMetaFile(pdc, h));
            obs("placeable hash", hash(pdc));
            DeleteMetaFile(h);
        }
        g = GlobalAlloc(GMEM_MOVEABLE, len + 22);
        {
            BYTE FAR *d = GlobalLock(g);
            for (i = 0; i < 22; i++)
                d[i] = ((BYTE *)hdr)[i];
            for (i = 0; i < len; i++)
                d[22 + i] = src[i];
            GlobalUnlock(g);
        }
        GlobalUnlock((HGLOBAL)mh);
        obs("placeable bits IsValidMetaFile", isvalidmf ? isvalidmf(g) : 0xEEEE);
        h = SetMetaFileBits(g);
        obs("placeable SetMetaFileBits", h != 0);
        if (h) {
            white(pdc);
            obs("placeable bits PlayMetaFile", PlayMetaFile(pdc, h));
            obs("placeable bits hash", hash(pdc));
            DeleteMetaFile(h);
        } else
            GlobalFree(g);
        DeleteMetaFile(mh);
    }
    /* (CreateMetaFile with a directory that is not there, or GetMetaFile
       of a file that is not there: 3.1 did not come back from one of
       them; not probed) */
}

/* ---- GetMetaFileBits, SetMetaFileBits(Better) ---- */

static void sec_bits(void)
{
    HMETAFILE h = record(0), h2;
    HGLOBAL g;
    obs("bits whose", whose((HGLOBAL)h));
    obs("bits GlobalFlags", GlobalFlags((HGLOBAL)h) & 0xFF00);
    obs("bits GlobalSize", (WORD)GlobalSize((HGLOBAL)h));
    g = GetMetaFileBits(h);
    obs("GetMetaFileBits same handle", g == (HGLOBAL)h);
    obs("GetMetaFileBits whose", whose(g));
    obs("GetMetaFileBits IsValidMetaFile", isvalidmf ? isvalidmf(g) : 0xEEEE);
    h2 = SetMetaFileBits(g);
    obs("SetMetaFileBits same handle", h2 == (HMETAFILE)g);
    obs("SetMetaFileBits whose", whose((HGLOBAL)h2));
    g = GetMetaFileBits(h2);
    h2 = SetMetaFileBitsBetter(g);
    obs("SetMetaFileBitsBetter same handle", h2 == (HMETAFILE)g);
    obs("SetMetaFileBitsBetter whose", whose((HGLOBAL)h2));
    obs("SetMetaFileBitsBetter GlobalFlags", GlobalFlags((HGLOBAL)h2) & 0xFF00);
    white(pdc);
    obs("SetMetaFileBitsBetter PlayMetaFile", PlayMetaFile(pdc, h2));
    obs("SetMetaFileBitsBetter DeleteMetaFile", DeleteMetaFile(h2));
    /* bits of the program's own making */
    {
        static const WORD mf[] = {
            1, 9, 0x300, 0x1A, 0, 0, 7, 0, 0,
            7, 0, META_RECTANGLE, 30, 20, 2, 2,
            5, 0, 0x0201, 0x00FF, 0,       /* SetBkColor */
            3, 0, 0 };
        WORD i;
        WORD FAR *p;
        g = GlobalAlloc(GMEM_MOVEABLE, sizeof mf);
        p = (WORD FAR *)GlobalLock(g);
        for (i = 0; i < sizeof mf / 2; i++)
            p[i] = mf[i];
        GlobalUnlock(g);
        obs("own bits IsValidMetaFile", isvalidmf ? isvalidmf(g) : 0xEEEE);
        obs("own bits whose", whose(g));
        h2 = SetMetaFileBits(g);
        obs("own bits SetMetaFileBits same", h2 == (HMETAFILE)g);
        obs("own bits SetMetaFileBits whose", whose(g));
        white(pdc);
        obs("own bits PlayMetaFile", PlayMetaFile(pdc, h2));
        obs("own bits hash", hash(pdc));
        obs_dw("own bits bk colour after", "", GetBkColor(pdc));
        DeleteMetaFile(h2);
        obs("own bits after DeleteMetaFile GlobalFlags", GlobalFlags(g));
    }
    obs("GetMetaFileBits 0", GetMetaFileBits(0) != 0);
    obs("SetMetaFileBits 0", SetMetaFileBits(0) != 0);
}

/* ---- errors ---- */

static HMETAFILE own_mf(const WORD *w, WORD n)
{
    HGLOBAL g = GlobalAlloc(GMEM_MOVEABLE, n * 2);
    WORD FAR *p = (WORD FAR *)GlobalLock(g);
    WORD i;
    for (i = 0; i < n; i++)
        p[i] = w[i];
    GlobalUnlock(g);
    return (HMETAFILE)g;
}

static void try_mf(const char *name, const WORD *w, WORD n)
{
    HMETAFILE h = own_mf(w, n);
    white(pdc);
    obs2(name, "IsValidMetaFile", isvalidmf ? isvalidmf((HANDLE)h) : 0xEEEE);
    obs2(name, "PlayMetaFile", PlayMetaFile(pdc, h));
    obs2(name, "hash", hash(pdc));
    obs2(name, "DeleteMetaFile", DeleteMetaFile(h));
    if (GlobalSize((HGLOBAL)h))
        GlobalFree((HGLOBAL)h);
}

static void sec_errors(void)
{
    static const WORD unk[] = {
        1, 9, 0x300, 0x17, 0, 0, 7, 0, 0,
        4, 0, 0x0999, 1,
        7, 0, META_RECTANGLE, 30, 20, 2, 2,
        3, 0, 0 };
    static const WORD eofmid[] = {
        1, 9, 0x300, 0x1D, 0, 0, 7, 0, 0,
        7, 0, META_RECTANGLE, 30, 20, 2, 2,
        4, 0, 0x0000, 1,
        7, 0, META_RECTANGLE, 40, 40, 32, 32,
        3, 0, 0 };
    static const WORD shortrec[] = {
        1, 9, 0x300, 0x18, 0, 0, 7, 0, 0,
        7, 0, META_RECTANGLE, 30, 20, 2, 2,
        5, 0, META_SETPIXEL, 0, 0, 5, 5,       /* (rdSize 5 short of 7) */
        3, 0, 0 };
    static const WORD badsel[] = {
        1, 9, 0x300, 0x1A, 0, 1, 7, 0, 0,
        4, 0, META_SELECTOBJECT, 3,
        7, 0, META_RECTANGLE, 30, 20, 2, 2,
        3, 0, 0 };
    static const WORD baddel[] = {
        1, 9, 0x300, 0x1A, 0, 1, 7, 0, 0,
        4, 0, META_DELETEOBJECT, 5,
        7, 0, META_RECTANGLE, 30, 20, 2, 2,
        3, 0, 0 };
    static const WORD v100[] = {
        1, 9, 0x100, 0x13, 0, 0, 7, 0, 0,
        7, 0, META_RECTANGLE, 30, 20, 2, 2,
        3, 0, 0 };
    static const WORD v200[] = {
        1, 9, 0x200, 0x13, 0, 0, 7, 0, 0,
        7, 0, META_RECTANGLE, 30, 20, 2, 2,
        3, 0, 0 };
    static const WORD t2[] = {
        2, 9, 0x300, 0x13, 0, 0, 7, 0, 0,
        7, 0, META_RECTANGLE, 30, 20, 2, 2,
        3, 0, 0 };
    static const WORD t0[] = {
        0, 9, 0x300, 0x13, 0, 0, 7, 0, 0,
        7, 0, META_RECTANGLE, 30, 20, 2, 2,
        3, 0, 0 };
    static const WORD t3[] = {
        3, 9, 0x300, 0x13, 0, 0, 7, 0, 0,
        7, 0, META_RECTANGLE, 30, 20, 2, 2,
        3, 0, 0 };
    static const WORD hs8[] = {
        1, 8, 0x300, 0x13, 0, 0, 7, 0, 0,
        7, 0, META_RECTANGLE, 30, 20, 2, 2,
        3, 0, 0 };
    static const WORD noeof[] = {
        1, 9, 0x300, 0x10, 0, 0, 7, 0, 0,
        7, 0, META_RECTANGLE, 30, 20, 2, 2,
        3, 0, 0 };
    static const WORD earlyeof[] = {
        1, 9, 0x300, 0x16, 0, 0, 7, 0, 0,
        3, 0, 0,
        7, 0, META_RECTANGLE, 30, 20, 2, 2,
        3, 0, 0 };
    static const WORD bigsize[] = {
        1, 9, 0x300, 0x80, 0, 0, 7, 0, 0,
        7, 0, META_RECTANGLE, 30, 20, 2, 2,
        3, 0, 0 };
    HGLOBAL g;
    obs("PlayMetaFile 0", PlayMetaFile(pdc, 0));
    obs("IsValidMetaFile 0", isvalidmf ? isvalidmf(0) : 0xEEEE);
    g = GlobalAlloc(GMEM_MOVEABLE | GMEM_ZEROINIT, 64);
    obs("PlayMetaFile zeros", PlayMetaFile(pdc, (HMETAFILE)g));
    obs("IsValidMetaFile zeros", isvalidmf ? isvalidmf(g) : 0xEEEE);
    obs("DeleteMetaFile zeros", DeleteMetaFile((HMETAFILE)g));
    obs("zeros after DeleteMetaFile GlobalSize", GlobalSize(g) != 0);
    if (GlobalSize(g))
        GlobalFree(g);
    obs("PlayMetaFile pen", PlayMetaFile(pdc, (HMETAFILE)GetStockObject(BLACK_PEN)));
    obs("PlayMetaFile into closed", PlayMetaFile(0, 0));
    try_mf("eof in the middle", eofmid, sizeof eofmid / 2);
    try_mf("bad select", badsel, sizeof badsel / 2);
    try_mf("bad delete", baddel, sizeof baddel / 2);
    try_mf("version 100", v100, sizeof v100 / 2);
    try_mf("version 200", v200, sizeof v200 / 2);
    try_mf("type 2", t2, sizeof t2 / 2);
    try_mf("type 0", t0, sizeof t0 / 2);
    try_mf("type 3", t3, sizeof t3 / 2);
    try_mf("header 8", hs8, sizeof hs8 / 2);
    try_mf("no eof", noeof, sizeof noeof / 2);
    try_mf("early eof", earlyeof, sizeof earlyeof / 2);
    out("GD9 disk2\r\n");
    sec_disk2();
    /* (not probed: 3.1 did not come back from a metafile holding an
       unknown function, a record shorter than its function's parameters
       and an mtSize past the block, all in one; the arrays are kept for
       a probe of them one at a time) */
    (void)unk;
    (void)shortrec;
    (void)bigsize;
}

/* ---- the clipboard ---- */

static void sec_clip(void)
{
    HWND w = CreateWindow("STATIC", "", WS_POPUP, 0, 0, 1, 1, NULL, NULL, hinst, NULL);
    HMETAFILE h = record(0);
    HGLOBAL mp = GlobalAlloc(GMEM_MOVEABLE | GMEM_DDESHARE, sizeof(METAFILEPICT));
    METAFILEPICT FAR *p = (METAFILEPICT FAR *)GlobalLock(mp);
    HANDLE got;
    p->mm = MM_ANISOTROPIC;
    p->xExt = 1000;
    p->yExt = 500;
    p->hMF = h;
    GlobalUnlock(mp);
    obs("clip OpenClipboard", OpenClipboard(w));
    obs("clip EmptyClipboard", EmptyClipboard());
    obs("clip SetClipboardData same", SetClipboardData(CF_METAFILEPICT, mp) == mp);
    obs("clip pict whose", whose(mp));
    obs("clip metafile whose", whose((HGLOBAL)h));
    obs("clip pict USER's", owner(mp) == GetModuleHandle("USER"));
    obs("clip metafile USER's", owner((HGLOBAL)h) == GetModuleHandle("USER"));
    obs("clip pict GlobalFlags", GlobalFlags(mp) & 0xFF00);
    obs("clip metafile GlobalFlags", GlobalFlags((HGLOBAL)h) & 0xFF00);
    CloseClipboard();
    obs("clip available", IsClipboardFormatAvailable(CF_METAFILEPICT));
    OpenClipboard(w);
    got = GetClipboardData(CF_METAFILEPICT);
    obs("clip GetClipboardData same", got == mp);
    p = (METAFILEPICT FAR *)GlobalLock(got);
    if (p) {
        obs("clip hMF same", p->hMF == h);
        obs("clip mm", p->mm);
        obs("clip xExt", p->xExt);
        GlobalUnlock(got);
    }
    white(pdc);
    obs("clip PlayMetaFile", PlayMetaFile(pdc, h));
    obs("clip EmptyClipboard again", EmptyClipboard());
    obs("clip pict after empty GlobalSize", GlobalSize(mp) != 0);
    obs("clip metafile after empty GlobalSize", GlobalSize((HGLOBAL)h) != 0);
    obs("clip metafile after empty IsValidMetaFile", isvalidmf ? isvalidmf((HANDLE)h) : 0xEEEE);
    CloseClipboard();
    DestroyWindow(w);
}

int __cdecl tmain(void)
{
    HMODULE gdi;
    HINSTANCE th;
    hinst = (HINSTANCE)init_regs[5];
    WaitEvent(0);
    InitApp(hinst);
    out("GD9 start\r\n");
    gdi = GetModuleHandle("GDI");
    isvalidmf = (PISVALIDMF)GetProcAddress(gdi, MAKEINTRESOURCE(410));
    th = LoadLibrary("TOOLHELP.DLL");
    if (th > HINSTANCE_ERROR)
        gentryhandle = (PGEH)GetProcAddress(th, "GlobalEntryHandle");
    hmine = GlobalAlloc(GMEM_MOVEABLE, 16);
    hscr = GetDC(NULL);
    make_dib();
    out("GD9 dc\r\n");
    sec_dc();
    out("GD9 calls\r\n");
    sec_calls();
    out("GD9 objects\r\n");
    sec_objects();
    out("GD9 play\r\n");
    sec_play();
    out("GD9 enum\r\n");
    sec_enum();
    out("GD9 disk\r\n");
    sec_disk();
    out("GD9 bits\r\n");
    sec_bits();
    out("GD9 clip\r\n");
    sec_clip();
    out("GD9 errors\r\n");
    sec_errors();
    ReleaseDC(NULL, hscr);
    out("GD9 done\r\n");
    return failures;
}
