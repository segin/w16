/*
 * gd4.c - probe GD4: what each GDI object costs in GDI's heaps, how many
 * of each fit, and the "GDI resources" percentage along the way
 * (ARCHITECTURE.md 11.7). Run as the shell on Windows 3.1
 * (tests/gdiheap.sh -r) and with w16's GDI (tests/gdiheap.sh).
 *
 * Observation only, through documented interfaces: TOOLHELP's
 * SystemHeapInfo (hGDISegment), GlobalFirst/GlobalNext (GDI-owned blocks
 * with a local heap), LocalFirst/LocalNext (block sizes, flags and the
 * wType TOOLHELP gives GDI's blocks), KERNEL's GetHeapSpaces and USER's
 * GetFreeSystemResources (as US14 did for USER).
 *
 * For each kind of object: one is made and the bytes each GDI heap gained
 * are printed with the object's own block (its size, flags, type); then
 * objects are made until GDI refuses, printing every 32nd the percentage
 * GFSR_GDIRESOURCES gives and GetHeapSpaces(GDI); all are deleted and the
 * percentage must come back. Counts depend on what ran before, so they
 * are OBS ~ and compared by tools in tests/gdiheap.sh.
 */
#include <windows.h>
#include <toolhelp.h>

#define TK_REF "gd4ref.h"
#include "tklog.h"

extern WORD FAR PASCAL WaitEvent(HTASK);
extern BOOL FAR PASCAL InitApp(HINSTANCE);

typedef BOOL (FAR PASCAL *P1)(void FAR *);
typedef BOOL (FAR PASCAL *P2)(void FAR *, WORD);
typedef UINT (FAR PASCAL *PGFSR)(UINT);
typedef DWORD (FAR PASCAL *PGHS)(HMODULE);

static P1 sysheapinfo, lnext;
static P2 gfirst, gnext, lfirst;
static PGFSR gfsr;
static PGHS getheapspaces;
static HMODULE hgdi;
static WORD gdids;          /* GDI's DGROUP (global handle) */
static HINSTANCE hinst;

#define MAXH 8
typedef struct {
    HGLOBAL h;
    DWORD size;
    WORD freeb, usedb, nused;
    char name;              /* D: DGROUP, 1.. other GDI-owned heaps */
} HEAP;
static HEAP heaps[MAXH], prev[MAXH];
static int nheaps, nprev;
static char name[80];

static void nm(const char *a, const char *b)
{
    int i = 0;
    while (*a && i < 40)
        name[i++] = *a++;
    while (*b && i < 78)
        name[i++] = *b++;
    name[i] = 0;
}

static void nmc(const char *a, char c, const char *b)
{
    char t[2];
    t[0] = c, t[1] = 0;
    nm(a, t);
    {
        int i = 0;
        while (name[i])
            i++;
        while (*b && i < 78)
            name[i++] = *b++;
        name[i] = 0;
    }
}

static void scan(void)
{
    GLOBALENTRY ge;
    LOCALENTRY le;
    BOOL more, lm;
    int i;
    char next = '1';
    for (i = 0; i < nheaps; i++)
        prev[i] = heaps[i];
    nprev = nheaps;
    nheaps = 0;
    ge.dwSize = sizeof ge;
    for (more = gfirst(&ge, GLOBAL_ALL); more; more = gnext(&ge, GLOBAL_ALL)) {
        HEAP *hp;
        if (!ge.wHeapPresent || nheaps >= MAXH)
            continue;
        if (ge.hOwner != hgdi && (WORD)ge.hBlock != gdids)
            continue;
        hp = &heaps[nheaps++];
        hp->h = ge.hBlock;
        hp->size = ge.dwBlockSize;
        hp->freeb = hp->usedb = hp->nused = 0;
        hp->name = (WORD)ge.hBlock == gdids ? 'D' : next++;
        le.dwSize = sizeof le;
        for (lm = lfirst(&le, (WORD)ge.hBlock); lm; lm = lnext(&le)) {
            if (le.wFlags & LF_FREE)
                hp->freeb += le.wSize;
            else {
                hp->usedb += le.wSize;
                hp->nused++;
            }
        }
    }
}

static int find_block(WORD handle, LOCALENTRY *out_le)
{
    LOCALENTRY le;
    BOOL more;
    int i;
    for (i = 0; i < nheaps; i++) {
        le.dwSize = sizeof le;
        for (more = lfirst(&le, (WORD)heaps[i].h); more; more = lnext(&le))
            if (!(le.wFlags & LF_FREE) && (WORD)le.hHandle == handle) {
                if (out_le)
                    *out_le = le;
                return i;
            }
    }
    return -1;
}

static int delta(int i)
{
    int j;
    for (j = 0; j < nprev; j++)
        if (prev[j].h == heaps[i].h)
            return (int)heaps[i].usedb - (int)prev[j].usedb;
    return (int)heaps[i].usedb;
}

static WORD pct(HMODULE h)
{
    DWORD g = getheapspaces(h);
    WORD t = HIWORD(g) >> 10;
    return t ? (LOWORD(g) >> 10) * 100 / t : 0;
}

static WORD fits, misfits;

static void model(void)
{
    fits++;
    misfits += gfsr(1) != pct(hgdi);
}

static void heaps_report(const char *tag)
{
    int i;
    DWORD g = getheapspaces(hgdi);
    scan();
    out("OBS ~");
    out(tag);
    out(": GFSR(1)=");
    outhex(gfsr(1));
    out(" ghs=");
    outhex(LOWORD(g));
    out("/");
    outhex(HIWORD(g));
    for (i = 0; i < nheaps; i++) {
        char c[3];
        c[0] = ' ', c[1] = heaps[i].name, c[2] = 0;
        out(c);
        out("=");
        outhex((WORD)(heaps[i].size >> 4));
        out("/");
        outhex(heaps[i].freeb);
        out("/");
        outhex(heaps[i].usedb);
        out("/");
        outhex(heaps[i].nused);
    }
    out("\r\n");
    model();
}

/* DGROUP's blocks by TOOLHELP type: count and bytes */
static void histogram(const char *tag)
{
    LOCALENTRY le;
    BOOL more;
    WORD cnt[12], by[12];
    int i, t;
    for (i = 0; i < 12; i++)
        cnt[i] = by[i] = 0;
    le.dwSize = sizeof le;
    for (more = lfirst(&le, gdids); more; more = lnext(&le)) {
        if (le.wFlags & LF_FREE)
            continue;
        t = le.wType <= 10 ? le.wType : 11;
        cnt[t]++;
        by[t] += le.wSize;
    }
    for (i = 0; i < 12; i++)
        if (cnt[i]) {
            out("OBS ~");
            out(tag);
            out(" type ");
            outhex(i);
            out(": blocks=");
            outhex(cnt[i]);
            out(" bytes=");
            outhex(by[i]);
            out("\r\n");
        }
}

/* after scan() before and after making something: the bytes each heap
   gained (D=dgroup, 1.. others) */
static void deltas(const char *tag)
{
    int i;
    for (i = 0; i < nheaps; i++) {
        nmc(tag, heaps[i].name, " bytes");
        obs_env(name, (WORD)delta(i));
    }
}

/* the object's block: which heap, its size, flags and TOOLHELP type */
static void block_of(const char *tag, WORD h)
{
    LOCALENTRY le;
    int i = find_block(h, &le);
    nm(tag, " handle");
    obs_env(name, h);
    nm(tag, " heap");
    obs_env(name, i >= 0 ? heaps[i].name : 0);
    if (i < 0)
        return;
    nm(tag, " block size");
    obs_env(name, le.wSize);
    nm(tag, " block flags");
    obs_env(name, le.wFlags);
    nm(tag, " block type");
    obs_env(name, le.wType);
    nm(tag, " handle==address");
    obs_env(name, (WORD)le.hHandle == le.wAddress);
}

/* ---- the objects ---- */

static HDC hdcScreen;
static HBITMAP hbmPat;
static int seq;

enum { K_PEN, K_WPEN, K_SBRUSH, K_HBRUSH, K_PBRUSH, K_FONT, K_FONTLONG, K_FONTNOFACE,
       K_RGN, K_RGNNULL, K_ERGN, K_BMP, K_BMPBIG, K_CBMP, K_PAL16, K_PAL256,
       K_CDC, K_DC, K_IC, K_N };
static const char *const kname[K_N] = {
    "pen", "wide pen", "solid brush", "hatch brush", "pattern brush", "font",
    "font (long face)", "font (no face)", "rect rgn", "empty rgn", "elliptic rgn",
    "bitmap 8x8x1", "bitmap 64x64x1", "compatible bitmap 16x16", "palette 16",
    "palette 256", "compatible DC", "display DC", "display IC"
};

static WORD make(int k)
{
    int n = ++seq;
    switch (k) {
    case K_PEN: return (WORD)CreatePen(PS_SOLID, 1, RGB(n, n >> 8, 1));
    case K_WPEN: return (WORD)CreatePen(PS_SOLID, 5, RGB(n, n >> 8, 2));
    case K_SBRUSH: return (WORD)CreateSolidBrush(RGB(n, n >> 8, 3));
    case K_HBRUSH: return (WORD)CreateHatchBrush(HS_CROSS, RGB(n, n >> 8, 4));
    case K_PBRUSH: return (WORD)CreatePatternBrush(hbmPat);
    case K_FONT: return (WORD)CreateFont(-(n % 40) - 8, 0, 0, 0, 400, 0, 0, 0, ANSI_CHARSET,
                                         0, 0, 0, 0, "MS Sans Serif");
    case K_FONTLONG: return (WORD)CreateFont(-(n % 40) - 8, 0, 0, 0, 400, 0, 0, 0, ANSI_CHARSET,
                                             0, 0, 0, 0, "Abcdefghijklmnopqrstuvwxyz01234");
    case K_FONTNOFACE: return (WORD)CreateFont(-(n % 40) - 8, 0, 0, 0, 400, 0, 0, 0,
                                               ANSI_CHARSET, 0, 0, 0, 0, NULL);
    case K_RGN: return (WORD)CreateRectRgn(n, 0, n + 10, 10);
    case K_RGNNULL: return (WORD)CreateRectRgn(0, 0, 0, 0);
    case K_ERGN: return (WORD)CreateEllipticRgn(0, 0, 40, 40);
    case K_BMP: return (WORD)CreateBitmap(8, 8, 1, 1, NULL);
    case K_BMPBIG: return (WORD)CreateBitmap(64, 64, 1, 1, NULL);
    case K_CBMP: return (WORD)CreateCompatibleBitmap(hdcScreen, 16, 16);
    case K_PAL16:
    case K_PAL256: {
        static struct { WORD v, n; PALETTEENTRY e[256]; } lp;
        int i;
        lp.v = 0x300;
        lp.n = k == K_PAL16 ? 16 : 256;
        for (i = 0; i < lp.n; i++) {
            lp.e[i].peRed = (BYTE)i;
            lp.e[i].peGreen = (BYTE)n;
            lp.e[i].peBlue = 0;
            lp.e[i].peFlags = 0;
        }
        return (WORD)CreatePalette((LOGPALETTE FAR *)&lp);
    }
    case K_CDC: return (WORD)CreateCompatibleDC(NULL);
    case K_DC: return (WORD)CreateDC("DISPLAY", NULL, NULL, NULL);
    case K_IC: return (WORD)CreateIC("DISPLAY", NULL, NULL, NULL);
    }
    return 0;
}

static BOOL unmake(int k, WORD h)
{
    if (k >= K_CDC)
        return DeleteDC((HDC)h);
    return DeleteObject((HGDIOBJ)h);
}

static void one(int k)
{
    WORD h;
    scan();
    h = make(k);
    scan();
    nm(kname[k], " made");
    obs(name, h != 0);
    deltas(kname[k]);
    block_of(kname[k], h);
    if (h) {
        if (k < K_CDC) {
            nm(kname[k], " IsGDIObject");
            {
                typedef int (FAR PASCAL *PIGO)(HANDLE);
                PIGO igo = (PIGO)GetProcAddress(hgdi, MAKEINTRESOURCE(462));
                obs(name, igo ? igo((HANDLE)h) : 0xFFFF);
            }
        }
        unmake(k, h);
        scan();
        nm(kname[k], " deleted, back");
        {
            int i, d = 0;
            for (i = 0; i < nheaps; i++)
                d += delta(i);
            obs_env(name, (WORD)-d);
        }
    }
}

/* ---- costs inside a DC ---- */

static void dc_costs(void)
{
    HDC dc = CreateCompatibleDC(NULL);
    HBITMAP bm = CreateCompatibleBitmap(hdcScreen, 64, 64), obm;
    HPEN pen = CreatePen(PS_SOLID, 1, RGB(255, 0, 0)), open;
    HBRUSH br = CreateSolidBrush(RGB(0, 0, 255)), obr;
    HFONT f = CreateFont(-20, 0, 0, 0, 700, 1, 0, 0, ANSI_CHARSET, 0, 0, 0, 0, "Courier"), of;
    HRGN r = CreateRectRgn(1, 1, 30, 30), r2;
    int lvl;
    obm = SelectObject(dc, bm);
    scan();
    open = SelectObject(dc, pen);
    MoveTo(dc, 0, 0);
    LineTo(dc, 20, 20);
    scan();
    deltas("select+draw pen");
    obr = SelectObject(dc, br);
    PatBlt(dc, 0, 0, 10, 10, PATCOPY);
    scan();
    deltas("select+draw brush");
    of = SelectObject(dc, f);
    TextOut(dc, 0, 0, "Hello", 5);
    scan();
    deltas("select+draw font");
    SelectClipRgn(dc, r);
    scan();
    deltas("SelectClipRgn");
    lvl = SaveDC(dc);
    scan();
    deltas("SaveDC");
    nm("SaveDC level", "");
    obs(name, lvl);
    RestoreDC(dc, -1);
    scan();
    deltas("RestoreDC");
    /* complex regions: bytes per rectangle in one band and per band */
    r2 = CreateRectRgn(0, 0, 2, 2);
    scan();
    {
        HRGN t = CreateRectRgn(0, 0, 0, 0);
        int i;
        for (i = 1; i < 16; i++) {
            SetRectRgn(t, i * 4, 0, i * 4 + 2, 2);
            CombineRgn(r2, r2, t, RGN_OR);
        }
        scan();
        deltas("16 rects in a band, more than 1");
        SetRectRgn(r2, 0, 0, 2, 2);
        scan();
        for (i = 1; i < 16; i++) {
            SetRectRgn(t, 0, i * 4, 2, i * 4 + 2);
            CombineRgn(r2, r2, t, RGN_OR);
        }
        scan();
        deltas("16 bands of 1 rect, more than 1");
        DeleteObject(t);
    }
    DeleteObject(r2);
    SelectObject(dc, open);
    SelectObject(dc, obr);
    SelectObject(dc, of);
    SelectObject(dc, obm);
    DeleteDC(dc);
    DeleteObject(bm);
    DeleteObject(pen);
    DeleteObject(br);
    DeleteObject(f);
    DeleteObject(r);
}

/* ---- limits ---- */

#define NMAX 16000
static WORD FAR *hs;

static void fill(int k)
{
    WORD n, start = gfsr(1);
    char tag[48];
    nm("curve ", kname[k]);
    lstrcpy(tag, name);
    for (n = 0; n < NMAX; n++) {
        hs[n] = make(k);
        if (!hs[n])
            break;
        if (n % 32 == 31) {
            DWORD g = getheapspaces(hgdi);
            out("OBS ~");
            out(tag);
            out(" ");
            outhex(n + 1);
            out(": GFSR(1)=");
            outhex(gfsr(1));
            out(" ghs=");
            outhex(LOWORD(g));
            out("/");
            outhex(HIWORD(g));
            out("\r\n");
            model();
        }
    }
    nm("limit ", kname[k]);
    obs_env(name, n);
    nm("full ", kname[k]);
    heaps_report(name);
    while (n)
        unmake(k, hs[--n]);
    nm("freed: GFSR(1) back ", kname[k]);
    obs(name, gfsr(1) == start);
}

/* a region made of rectangles in rows of 128 (one band each), grown a row
   at a time and then by halves of a row, until CombineRgn fails */
static void fill_complex(void)
{
    HRGN r = CreateRectRgn(0, 0, 0, 0), row[8], t;
    WORD count = 0, y = 0, ok = 1;
    int i, k;
    for (k = 0; k < 8; k++) {       /* row[k]: 1 << k rects of 2x2, 4 apart */
        row[k] = CreateRectRgn(0, 0, 2, 2);
        for (i = 0; i < k; i++) {
            t = CreateRectRgn(0, 0, 0, 0);
            CombineRgn(t, row[k], 0, RGN_COPY);
            OffsetRgn(t, 4 << i, 0);
            CombineRgn(row[k], row[k], t, RGN_OR);
            DeleteObject(t);
        }
    }
    t = CreateRectRgn(0, 0, 0, 0);
    for (k = 7; k >= 0; k--) {
        for (;;) {
            CombineRgn(t, row[k], 0, RGN_COPY);
            OffsetRgn(t, 0, y);
            if (CombineRgn(r, r, t, RGN_OR) == ERROR) {
                ok = 0;
                break;
            }
            count += 1 << k;
            y += 4;
            if (count % 1024 == 0) {
                out("OBS ~curve complex region rects ");
                outhex(count);
                out(": GFSR(1)=");
                outhex(gfsr(1));
                out("\r\n");
            }
            if (k < 7 || count >= 30000)
                break;
        }
        if (count >= 30000)
            break;
    }
    (void)ok;
    obs_env("limit complex region rects (rows of 128)", count);
    heaps_report("full complex region");
    DeleteObject(t);
    for (k = 0; k < 8; k++)
        DeleteObject(row[k]);
    DeleteObject(r);
}

#ifndef GD4_STAIRS_MS
#define GD4_STAIRS_MS 60000L
#endif
/* one rectangle per band, added one at a time (at most a minute) */
static void fill_stairs(void)
{
    HRGN r = CreateRectRgn(0, 0, 0, 0), t = CreateRectRgn(0, 0, 0, 0);
    WORD n;
    DWORD t0 = GetTickCount();
    for (n = 0; n < 8000; n++) {
        SetRectRgn(t, n % 200, n * 2, n % 200 + 3, n * 2 + 1);
        if (CombineRgn(r, r, t, RGN_OR) == ERROR)
            break;
        if (n % 256 == 255) {
            out("OBS ~curve region bands ");
            outhex(n + 1);
            out(": GFSR(1)=");
            outhex(gfsr(1));
            out("\r\n");
            if (GetTickCount() - t0 > GD4_STAIRS_MS) {
                out("OBS ~region bands: stopped at a minute\r\n");
                n++;
                break;
            }
        }
    }
    obs_env("limit region bands of 1 rect", n);
    heaps_report("full region bands");
    DeleteObject(t);
    DeleteObject(r);
}

static void fill_savedc(void)
{
    HDC dc = CreateCompatibleDC(NULL);
    WORD n = 0;
    while (n < NMAX && SaveDC(dc))
        n++;
    obs_env("limit SaveDC depth", n);
    heaps_report("full SaveDC");
    DeleteDC(dc);
}

/* GetDC without ReleaseDC (USER's cache), then CS_OWNDC windows */
static void fill_getdc(void)
{
    WNDCLASS wc;
    HWND top, w;
    WORD n;
    wc.style = 0;
    wc.lpfnWndProc = DefWindowProc;
    wc.cbClsExtra = wc.cbWndExtra = 0;
    wc.hInstance = hinst;
    wc.hIcon = 0;
    wc.hCursor = 0;
    wc.hbrBackground = 0;
    wc.lpszMenuName = NULL;
    wc.lpszClassName = "GD4";
    RegisterClass(&wc);
    wc.style = CS_OWNDC;
    wc.lpszClassName = "GD4OWN";
    RegisterClass(&wc);
    top = CreateWindow("GD4", "GD4", WS_OVERLAPPED, 0, 0, 100, 100, NULL, NULL, hinst, NULL);
    for (n = 0; n < 2000; n++) {
        hs[n] = (WORD)GetDC(top);
        if (!hs[n])
            break;
    }
    obs_env("limit GetDC without ReleaseDC", n);
    heaps_report("full GetDC");
    while (n)
        ReleaseDC(top, (HDC)hs[--n]);
    for (n = 0; n < 4000; n++) {
        w = CreateWindow("GD4OWN", "", WS_CHILD, 0, 0, 10, 10, top, (HMENU)n, hinst, NULL);
        if (!w || !GetDC(w))
            break;
        if (n % 32 == 31) {
            DWORD g = getheapspaces(hgdi);
            out("OBS ~curve CS_OWNDC windows ");
            outhex(n + 1);
            out(": GFSR(1)=");
            outhex(gfsr(1));
            out(" GFSR(2)=");
            outhex(gfsr(2));
            out(" ghs=");
            outhex(LOWORD(g));
            out("/");
            outhex(HIWORD(g));
            out("\r\n");
        }
    }
    obs_env("limit CS_OWNDC windows", n);
    heaps_report("full CS_OWNDC");
    DestroyWindow(top);
    heaps_report("CS_OWNDC freed");
}

int __cdecl tmain(void)
{
    HINSTANCE th;
    SYSHEAPINFO si;
    HGLOBAL g;
    int k;
    hinst = (HINSTANCE)init_regs[5];
    WaitEvent(0);
    InitApp(hinst);
    out("GD4 start\r\n");
    hgdi = GetModuleHandle("GDI");
    gfsr = (PGFSR)GetProcAddress(GetModuleHandle("USER"), MAKEINTRESOURCE(284));
    getheapspaces = (PGHS)GetProcAddress(GetModuleHandle("KERNEL"), MAKEINTRESOURCE(138));
    th = LoadLibrary("TOOLHELP.DLL");
    obs("TOOLHELP loaded", th > HINSTANCE_ERROR);
    if (th <= HINSTANCE_ERROR || !gfsr || !getheapspaces)
        return 1;
    sysheapinfo = (P1)GetProcAddress(th, "SystemHeapInfo");
    gfirst = (P2)GetProcAddress(th, "GlobalFirst");
    gnext = (P2)GetProcAddress(th, "GlobalNext");
    lfirst = (P2)GetProcAddress(th, "LocalFirst");
    lnext = (P1)GetProcAddress(th, "LocalNext");
    si.dwSize = sizeof si;
    obs("SystemHeapInfo", sysheapinfo(&si));
    gdids = LOWORD(GlobalHandle((WORD)si.hGDISegment));
    obs_env("hGDISegment", (WORD)si.hGDISegment);
    obs_env("GDI DGROUP handle", gdids);
    obs("SystemHeapInfo wGDIFreePercent==GFSR(1)", si.wGDIFreePercent == gfsr(1));
    hdcScreen = GetDC(NULL);
    hbmPat = CreateBitmap(8, 8, 1, 1, NULL);
    heaps_report("start");
    histogram("start DGROUP");
    {
        int n = 0, i;
        for (i = 0; i < nheaps; i++)
            n += heaps[i].name != 'D';
        obs_env("GDI heaps besides DGROUP", n);
    }

    for (k = 0; k < K_N; k++)
        one(k);
    dc_costs();
    {
        HBRUSH b = CreatePatternBrush(hbmPat);
        LOGBRUSH lb;
        obs("GetObject(pattern brush)", GetObject(b, sizeof lb, &lb));
        obs("pattern brush lbStyle", lb.lbStyle);
        obs("pattern brush lbHatch==bitmap", (HBITMAP)lb.lbHatch == hbmPat);
        obs_env("pattern brush lbHatch", lb.lbHatch);
        obs_env("pattern brush lbColor lo", LOWORD(lb.lbColor));
        obs_env("pattern brush lbColor hi", HIWORD(lb.lbColor));
        DeleteObject(b);
    }
    heaps_report("after singles");

    g = GlobalAlloc(GMEM_MOVEABLE, NMAX * sizeof(WORD));
    hs = (WORD FAR *)GlobalLock(g);
#ifndef GD4_STAIRS_ONLY
    fill(K_PEN);
    fill(K_SBRUSH);
    fill(K_FONT);
    fill(K_RGN);
    fill(K_PAL16);
    fill(K_CDC);
    fill(K_DC);
    fill(K_BMP);
    fill_savedc();
    fill_complex();
#endif
    fill_stairs();
    ReleaseDC(NULL, hdcScreen);
    fill_getdc();
    GlobalUnlock(g);
    GlobalFree(g);
    heaps_report("end");
    obs("GFSR(1) checked against the 1K rule", fits > 50);
    obs("GFSR(1) misfits", misfits);
    out("GD4 end\r\n");
    return 0;
}
