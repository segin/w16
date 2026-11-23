/*
 * ttfont.c - TrueType fonts in GDI: .FOT entries in the font table, sizes
 * realized as 3.x bitmap fonts the display driver draws (the handover the
 * integer-scaled raster fonts use, font.c font_scale), glyphs rasterised
 * by gdi/ttf/ttfx.c when a character is first drawn.
 *
 * A .FOT (3.1's, as Setup and CreateScalableFontResource leave them; read
 * as data, their published NE and font-directory structure) is a module
 * with no code: a FONTDIR whose one entry has the font's metrics in font
 * units (dfPoints the units per em, dfPixHeight the ascent plus descent),
 * and a resource of type CCh, number 1, holding the .TTF's file name.
 *
 * The metrics are 3.1's as probe GD11 shows them (tests/gdi/gd11ref.txt,
 * every Arial, Times New Roman and Courier New result, and GD13;
 * ARCHITECTURE.md 11.12), with the rules of the TrueType/OpenType specification's tables:
 * - the size: a character height (negative lfHeight) is the pixels per
 *   em; a cell height the smallest ppem that gives the greatest cell
 *   height not over it;
 * - tmAscent, tmDescent: the VDMX table's yMax and -yMin at that ppem
 *   (the 1:1 ratio), else usWinAscent and usWinDescent scaled, rounded;
 *   tmHeight their sum, tmInternalLeading tmHeight less the ppem;
 * - tmExternalLeading: hhea's line gap less what usWin* add over hhea's
 *   ascender and descender (at least 0), scaled, rounded;
 * - tmAveCharWidth: OS/2 xAvgCharWidth scaled, rounded; tmMaxCharWidth:
 *   head's xMax - xMin scaled, rounded;
 * - advance widths: hdmx's at that ppem, else the advance scaled and
 *   rounded. 3.1's hinting moves some widths off that (Arial's W is 79
 *   at 79 ppem, not 75): w16 does not hint yet (stage 4).
 */
#include "gdi.h"
#include "ttf/ttfx.h"

BYTE tt_enabled = 1;

long WINAPI _llseek(int, long, int);
int WINAPI _lcreat(LPCSTR, int);
WORD WINAPI _lwrite(int, const void __far *, WORD);
int WINAPI lstrlen(LPCSTR);

/* what a .TTF's tables say once, kept with its base entry */
typedef struct {
    char path[128];
    BYTE loaded, bad;
    WORD upem;
    short winA, winD, gap, xavg, bboxw;
    DWORD vdmx;                         /* the 1:1 VDMX group, 0: none */
    DWORD hdmx;                         /* the hdmx table, 0: none */
    WORD hdmxn;
    DWORD hdmxsize;
    BYTE symbol;                        /* a Microsoft Symbol cmap */
    BYTE hint;                          /* hint its glyphs (the hinter to come: always 0 for now) */
} TTFILE;
typedef TTFILE __far *LPTTFILE;

#define RT_TTNAME 204                   /* the .FOT's resource naming the .TTF */

/* ---- reading the file: one open file at a time, through ttfx's cache ---- */

static ttfx_font ttf;
static int tt_fh = -1;
static HGLOBAL tt_cache_h;
static BYTE __far *tt_cache;
#define TT_CACHE TTFX_CACHE_BYTES(8)

static unsigned tt_read(void *user, ttfx_u32 off, void TTFX_FAR *dst, unsigned len)
{
    WORD n;
    (void)user;
    if (_llseek(tt_fh, (long)off, 0) != (long)off)
        return 0;
    n = _lread(tt_fh, dst, len);
    return n == 0xFFFF ? 0 : n;
}

static void tt_close(void)
{
    if (tt_fh >= 0)
        _lclose(tt_fh);
    tt_fh = -1;
    if (tt_cache)
        GlobalUnlock(tt_cache_h);
    tt_cache = 0;
}

/* open the file (file handles belong to the calling task: each use opens
   and closes it) */
static int tt_open(LPTTFILE t)
{
    BYTE of[136];
    WORD em;
    if (t->bad)
        return 0;
    if (!tt_cache_h)
        tt_cache_h = GlobalAlloc(GMEM_MOVEABLE | GMEM_SHARE, TT_CACHE);
    tt_cache = tt_cache_h ? GlobalLock(tt_cache_h) : 0;
    if (!tt_cache)
        return 0;
    em = SetErrorMode(0x8000);
    tt_fh = OpenFile(t->path, of, 0x0040);      /* OF_READ | OF_SHARE_DENY_NONE */
    SetErrorMode(em);
    if (tt_fh < 0 || !ttfx_InitFont(&ttf, tt_read, 0, 0, tt_cache, TT_CACHE, 0)) {
        tt_close();
        t->bad = 1;
        return 0;
    }
    return 1;
}

/* ---- ttfx's memory: one block, a glyph's blocks at a time ---- */

static HGLOBAL tt_arena_h;
static BYTE __far *tt_arena;
static WORD tt_used;
#define TT_ARENA 64000U

void TTFX_FAR *ttfx_alloc(void *alloc, ttfx_u32 n)
{
    BYTE __far *p;
    (void)alloc;
    n = (n + 3) & ~1UL;
    if (!tt_arena || n > (ttfx_u32)(TT_ARENA - tt_used))
        return 0;
    p = tt_arena + tt_used;
    *(WORD __far *)p = (WORD)n;
    tt_used += (WORD)n;
    return p + 2;
}

void ttfx_free(void *alloc, void TTFX_FAR *q)
{
    BYTE __far *p = (BYTE __far *)q - 2;
    (void)alloc;
    if (p + *(WORD __far *)p == tt_arena + tt_used)
        tt_used -= *(WORD __far *)p;
}

static int arena_get(void)
{
    if (!tt_arena_h)
        tt_arena_h = GlobalAlloc(GMEM_MOVEABLE, TT_ARENA);
    tt_arena = tt_arena_h ? GlobalLock(tt_arena_h) : 0;
    tt_used = 0;
    return tt_arena != 0;
}

static void arena_put(void)
{
    if (tt_arena_h) {
        GlobalUnlock(tt_arena_h);
        GlobalFree(tt_arena_h);         /* (not kept: 64000 bytes) */
    }
    tt_arena_h = 0;
    tt_arena = 0;
}

/* ---- the font table ---- */

/* a .FOT's FONTDIR entry: TrueType if the module has the resource naming
   its .TTF and that file is found (OpenFile's search: the current
   directory, the Windows and System directories, the PATH) */
int tt_dirent(XP xf, HINSTANCE h)
{
    HRSRC r = FindResource(h, MAKEINTRESOURCE(1), MAKEINTRESOURCE(RT_TTNAME));
    HGLOBAL g;
    const char __far *p;
    char name[128];
    BYTE of[136];
    WORD em;
    int i, fh;
    XP xt;
    LPTTFILE t;
    LPFONT f;
    if (!r || !(g = LoadResource(h, r)))
        return 0;
    p = LockResource(g);
    for (i = 0; p && i < (int)sizeof name - 1 && p[i]; i++)
        name[i] = p[i];
    name[i] = 0;
    GlobalUnlock(g);
    FreeResource(g);
    em = SetErrorMode(0x8000);
    fh = OpenFile(name, of, 0x4000);    /* OF_EXIST: the full path */
    SetErrorMode(em);
    xt = xalloc(sizeof(TTFILE));
    if (!xt)
        return 0;
    t = xptr(xt);
    if (fh < 0)
        t->bad = 1;                     /* (kept: 3.1 counts it; never chosen) */
    for (i = 0; i < 127 && of[8 + i]; i++)
        t->path[i] = of[8 + i];
    f = xptr(xf);
    f->tt = 1;
    f->ttf = xt;
    return 1;
}

int tt_usable(LPFONT f)
{
    return tt_enabled && f->ttf && !((LPTTFILE)xptr(f->ttf))->bad;
}

void tt_free(LPFONT f)
{
    if (f->tt && !f->base && f->ttf)
        xfree(f->ttf);
}

/* the tables' values, the first time (the file open) */
static void tt_load(LPTTFILE t)
{
    ttfx_i32 o;
    ttfx_u32 len;
    if (t->loaded)
        return;
    t->loaded = 1;
    t->upem = ttf.unitsPerEm;
    o = ttf.os2;
    if (o) {
        t->winA = (short)ttfx_U16(&ttf, o + 74);
        t->winD = (short)ttfx_U16(&ttf, o + 76);
        t->xavg = ttfx_I16(&ttf, o + 2);
    }
    {
        int a, d, l, x0, y0, x1, y1;
        ttfx_GetFontVMetrics(&ttf, &a, &d, &l);
        if (!o) {                       /* (no OS/2: hhea's) */
            t->winA = a;
            t->winD = -d;
        }
        /* the OpenType specification's external leading for Windows */
        t->gap = l - ((t->winA + t->winD) - (a - d));
        if (t->gap < 0)
            t->gap = 0;
        ttfx_GetFontBoundingBox(&ttf, &x0, &y0, &x1, &y1);
        t->bboxw = x1 - x0;
    }
    t->symbol = ttf.index_map_platform == TTFX_PLATFORM_ID_MICROSOFT &&
                ttf.index_map_encoding == TTFX_MS_EID_SYMBOL;
    /* VDMX: the group of the first ratio that is 1:1 or any ratio */
    o = ttfx_FindTable(&ttf, "VDMX", &len);
    t->vdmx = 0;
    if (o) {
        WORD nrat = ttfx_U16(&ttf, o + 4), i;
        for (i = 0; i < nrat; i++) {
            ttfx_i32 rr = o + 6 + 4L * i;
            BYTE xr = ttfx_Byte(&ttf, rr + 1), y0 = ttfx_Byte(&ttf, rr + 2), y1 = ttfx_Byte(&ttf, rr + 3);
            if (!xr || (xr == 1 && y0 <= 1 && y1 >= 1)) {
                t->vdmx = o + ttfx_U16(&ttf, o + 6 + 4L * nrat + 2L * i);
                break;
            }
        }
    }
    o = ttfx_FindTable(&ttf, "hdmx", &len);
    t->hdmx = o;
    if (o) {
        t->hdmxn = ttfx_U16(&ttf, o + 2);
        t->hdmxsize = ttfx_U32(&ttf, o + 4);
    }
}

static int rnd(long v, WORD ppem, WORD upem)
{
    return (int)ttfx_MulDivRound(v, ppem, upem);
}

/* tmAscent and tmDescent at a size */
static void tt_vert(LPTTFILE t, WORD ppem, int *a, int *d)
{
    if (t->vdmx) {
        WORD n = ttfx_U16(&ttf, t->vdmx), i;
        for (i = 0; i < n; i++) {
            ttfx_i32 e = t->vdmx + 4 + 6L * i;
            WORD yp = ttfx_U16(&ttf, e);
            if (yp == ppem) {
                *a = ttfx_I16(&ttf, e + 2);
                *d = -ttfx_I16(&ttf, e + 4);
                return;
            }
            if (yp > ppem)
                break;
        }
    }
    *a = rnd(t->winA, ppem, t->upem);
    *d = rnd(t->winD, ppem, t->upem);
}

/* The size (pixels per em) a request height h asks, and the size across
   for a width w (*xppem; 0: the same).
   - h < 0: a character height, the ppem itself.
   - h > 0: a cell height. Fitted to GD11 on 3.1 (217 of 227 cell
     heights of Arial, Times New Roman, Courier New and Symbol; the rest
     within a ppem: very small sizes, and Times 12, which 3.1 makes 9
     where this gives 10): h x upem / (usWinAscent + usWinDescent + the
     external leading), rounded down; down while the cell is taller than
     h; then up while the cell is shorter than h and the next is not
     taller. (Arial 60 gives 52 although 53 has the same 60-pixel cell;
     Symbol 47 gives 38 where 37 has the same cell.)
   - w: GD11 on 3.1: tmAveCharWidth comes out as w, so the size across is
     w x upem / xAvgCharWidth, rounded. UNVERIFIED: tmMaxCharWidth and
     the widths then differ from 3.1's by a pixel or two (Arial 16 x 200:
     3.1 476, this 474). */
int tt_ppem(XP xf, int h, int w, WORD __far *xppem)
{
    LPFONT f = xptr(xf);
    LPTTFILE t = xptr(f->ttf);
    long p;
    int a, d, a1, d1;
    if (w < 0)
        w = -w;
    *xppem = 0;
    if (!tt_open(t))
        return h < 0 ? -h : h ? h : 1;
    tt_load(t);
    if (h < 0)
        p = -h;
    else {
        p = (long)h * t->upem / ((long)t->winA + t->winD + t->gap);
        if (p < 1)
            p = 1;
        for (;;) {
            tt_vert(t, (WORD)p, &a, &d);
            if (p <= 1 || a + d <= h)
                break;
            p--;
        }
        for (;;) {
            tt_vert(t, (WORD)p, &a, &d);
            tt_vert(t, (WORD)p + 1, &a1, &d1);
            if (a + d >= h || a1 + d1 > h)
                break;
            p++;
        }
    }
    /* GD13 on 3.1: no size below 2 (Arial asked 1 high has 2's metrics) */
    if (p < 2)
        p = 2;
    if (w && t->xavg > 0)
        *xppem = (WORD)ttfx_MulDivRound(w, t->upem, t->xavg);
    tt_close();
    if (*xppem == (WORD)p)
        *xppem = 0;
    return (int)p;
}

/* Windows' ANSI codes 80h-9Fh in Unicode (code page 1252; 0: none) */
static const WORD cp1252[32] = {
    0x20AC, 0, 0x201A, 0x0192, 0x201E, 0x2026, 0x2020, 0x2021,
    0x02C6, 0x2030, 0x0160, 0x2039, 0x0152, 0, 0x017D, 0,
    0, 0x2018, 0x2019, 0x201C, 0x201D, 0x2022, 0x2013, 0x2014,
    0x02DC, 0x2122, 0x0161, 0x203A, 0x0153, 0, 0x017E, 0x0178
};

static WORD tt_glyph(LPTTFILE t, BYTE c)
{
    long u = c;
    if (t->symbol)
        u = 0xF000 + c;
    else if (c >= 0x80 && c < 0xA0 && cp1252[c - 0x80])
        u = cp1252[c - 0x80];
    return ttfx_FindGlyphIndex(&ttf, u);
}

static int tt_width(LPTTFILE t, WORD g, WORD ppem, int square)
{
    ttfx_u16 adv;
    int lsb;
    if (t->hdmx && square) {
        WORD i;
        for (i = 0; i < t->hdmxn; i++) {
            ttfx_i32 r = t->hdmx + 8 + (ttfx_i32)i * t->hdmxsize;
            if (ttfx_Byte(&ttf, r) == ppem)
                return ttfx_Byte(&ttf, r + 2 + g);
        }
    }
    ttfx_GetGlyphHMetrics(&ttf, g, &adv, &lsb);
    return rnd(adv, ppem, t->upem);
}

/* A size as the driver gets it: a 3.x font in a global block of its own,
   as font_scale's scaled copies (font.c): FONTINFO at 42h, the character
   table at 94h (width, 32-bit offset), the face, the bits at an even
   offset after it, each character as many 8-pixel columns as its width
   needs, each column the cell's height. The base entry's FONTDIR values
   give the rest (weight, italic, character set, pitch and family, the
   first, last, default and break characters). The bits are made when
   the font is first drawn, the glyphs as their characters are. */
LPFONTINFO tt_info(XP xs)
{
    LPFONT s = xptr(xs), b = xptr(s->base);
    LPTTFILE t = xptr(b->ttf);
    WORD ppem = s->ppem, xp = s->xppem ? s->xppem : s->ppem, n, i, flen, first, last;
    int a, d;
    DWORD off, size, bits;
    BYTE __far *q;
    LPFONTINFO fi;
    BYTE dfbreak;
    if (!tt_open(t))
        return 0;
    tt_load(t);
    tt_vert(t, ppem, &a, &d);
    first = s->first;
    last = s->last;
    dfbreak = s->brk;
    n = last - first + 2;
    for (flen = 0; s->face[flen]; flen++)
        ;
    bits = (0x94 + (DWORD)n * 6 + flen + 2) & ~1UL;
    s->hMem = GlobalAlloc(GMEM_MOVEABLE | GMEM_ZEROINIT | GMEM_SHARE, bits);
    q = s->hMem ? GlobalLock(s->hMem) : 0;
    if (!q) {
        if (s->hMem)
            GlobalFree(s->hMem);
        s->hMem = 0;
        tt_close();
        return 0;
    }
    fi = (LPFONTINFO)(q + 0x42);
    fi->dfType = 0;                     /* a raster font, to the driver */
    fi->dfPoints = mul_div(ppem, 72, 96);
    fi->dfVertRes = fi->dfHorizRes = 96;
    fi->dfAscent = a;
    fi->dfPixHeight = a + d;
    fi->dfInternalLeading = a + d - ppem;
    fi->dfExternalLeading = rnd(t->gap, ppem, t->upem);
    fi->dfItalic = s->sx == 2 ? (char)0xFF : s->italic;      /* (sx 2: italic simulated) */
    fi->dfWeight = s->weight;
    fi->dfCharSet = s->charset;
    fi->dfPitchAndFamily = s->pitch;
    fi->dfAvgWidth = rnd(t->xavg, xp, t->upem);
    fi->dfMaxWidth = rnd(t->bboxw, xp, t->upem);
    fi->dfFirstChar = (BYTE)first;
    fi->dfLastChar = (BYTE)last;
    fi->dfDefaultChar = s->dflt;
    fi->dfBreakChar = dfbreak;
    fi->dfFace = 0x94 + (DWORD)n * 6;
    *(WORD __far *)q = 0x300;
    *(DWORD __far *)(q + 0x76) = (s->pitch & 1) ? 2 : 1;       /* dfFlags */
    mem_copy(q + (WORD)fi->dfFace, s->face, flen + 1);
    size = off = bits;
    for (i = 0; i < n; i++) {
        WORD c = i < n - 1 ? first + i : first + dfbreak;      /* (the last: the break) */
        int w = tt_width(t, tt_glyph(t, (BYTE)c), xp, xp == ppem);
        if (w < 0)
            w = 0;
        *(WORD __far *)(q + 0x94 + i * 6) = (WORD)w;
        *(DWORD __far *)(q + 0x94 + i * 6 + 2) = off;
        off += (DWORD)((w + 7) >> 3) * (WORD)(a + d);
    }
    size = off;
    *(DWORD __far *)(q + 2) = size;
    fi->dfBitsOffset = bits;
    fi->dfBitsPointer = MAKELP(SELECTOROF(q), OFFSETOF(q) + (WORD)bits);
    tt_close();
    s = xptr(xs);
    s->bits = 0;
    mem_set(s->done, 0, sizeof s->done);
    s->fi = fi;
    return fi;
}

/* rasterise character index i of size s into its cell */
static void tt_draw(LPFONT s, LPTTFILE t, WORD i, ttfx_scale sc, ttfx_scale sx)
{
    BYTE __far *base = (BYTE __far *)s->fi - 0x42;
    WORD w = *(WORD __far *)(base + 0x94 + i * 6), h = s->fi->dfPixHeight, stride, r, c;
    DWORD off = *(DWORD __far *)(base + 0x94 + i * 6 + 2);
    ttfx_vertex TTFX_FAR *v;
    ttfx_bitmap bm;
    int nv;
    BYTE c8 = (BYTE)(i < (WORD)(s->fi->dfLastChar - s->fi->dfFirstChar + 1) ? s->fi->dfFirstChar + i
                                                                          : s->fi->dfFirstChar + s->fi->dfBreakChar);
    if (!w || !h)
        return;
    stride = (w + 7) >> 3;
    if ((DWORD)stride * h > 30000L)
        return;                         /* (a glyph over 30000 bytes: blank) */
    tt_used = 0;
    bm.pixels = ttfx_alloc(0, (DWORD)stride * h);
    if (!bm.pixels)
        return;
    mem_set(bm.pixels, 0, stride * h);
    bm.w = w;
    bm.h = h;
    bm.stride = stride;
    nv = ttfx_GetGlyphShape(&ttf, tt_glyph(t, c8), &v);
    if (nv && s->sx == 2) {
        /* simulated italic: slanted by a quarter of the height (in font
           units, before scaling). UNVERIFIED: 3.1's slant and whether it
           keeps the glyph inside its advance */
        int k;
        for (k = 0; k < nv; k++) {
            v[k].x += v[k].y / 4;
            v[k].cx += v[k].cy / 4;
        }
    }
    if (nv) {
        /* the origin at the cell's left, the baseline at row dfAscent */
        ttfx_Rasterize(&ttf, &bm, v, nv, sx, sc, 0, 0, 0, -s->fi->dfAscent);
        ttfx_FreeShape(&ttf, v);
    }
    for (c = 0; c < stride; c++)
        for (r = 0; r < h; r++) {
            BYTE v8 = bm.pixels[r * stride + c];
            if (v8)
                *font_huge(base, off + (DWORD)c * h + r) = v8;
        }
}

void tt_glyphs(XP xs, LPCSTR str, int n)
{
    LPFONT s = xptr(xs), b;
    LPTTFILE t;
    LPFONTINFO fi;
    WORD k, nc;
    int opened = 0;
    ttfx_scale sc = 0, sx = 0;
    if (!s->tt || !s->base || !s->fi || n <= 0 || !str)
        return;
    b = xptr(s->base);
    t = xptr(b->ttf);
    if (!s->bits) {                     /* the block grown to the bits */
        DWORD size = *(DWORD __far *)((BYTE __far *)s->fi - 0x42 + 2);
        HGLOBAL hn;
        BYTE __far *q;
        GlobalUnlock(s->hMem);
        hn = GlobalReAlloc(s->hMem, size, GMEM_MOVEABLE | GMEM_ZEROINIT);
        if (hn)
            s->hMem = hn;
        q = GlobalLock(s->hMem);
        s->fi = fi = (LPFONTINFO)(q + 0x42);
        fi->dfBitsPointer = MAKELP(SELECTOROF(q), OFFSETOF(q) + (WORD)fi->dfBitsOffset);
        if (!hn)
            return;                     /* no room: drawn blank */
        s->bits = 1;
    }
    fi = s->fi;
    nc = fi->dfLastChar - fi->dfFirstChar + 1;
    for (k = 0; k < (WORD)n; k++) {
        BYTE ch = (BYTE)str[k];
        WORD i = ch >= fi->dfFirstChar && ch <= fi->dfLastChar ? ch - fi->dfFirstChar : fi->dfDefaultChar;
        if (i >= nc)
            continue;
        if (s->done[i >> 3] & (1 << (i & 7)))
            continue;
        if (!opened) {
            if (!tt_open(t) || !arena_get()) {
                tt_close();
                return;
            }
            sc = ttfx_ScaleForMappingEmToPixels(&ttf, s->ppem);
            sx = s->xppem ? ttfx_ScaleForMappingEmToPixels(&ttf, s->xppem) : sc;
            opened = 1;
        }
        tt_draw(s, t, i, sc, sx);
        s->done[i >> 3] |= (BYTE)(1 << (i & 7));
    }
    if (opened) {
        arena_put();
        tt_close();
    }
}

LPFONT tt_dc_font(LPDC dc)
{
    LPFONT f = dc_font(dc);
    return f && f->tt ? f : 0;
}

/* ---- the API ---- */

/* the DC's font, realized, if TrueType, with its base entry and file */
static LPFONT tt_hdc(HDC hdc, LPTTFILE __far *pt)
{
    LPDC dc = dc_get(hdc);
    LPFONT s, b;
    if (!dc || !font_realize(dc))
        return 0;
    dc = dc_get(hdc);
    s = dc_font(dc);
    if (!s || !s->tt || !s->base)
        return 0;
    b = xptr(s->base);
    *pt = xptr(b->ttf);
    return s;
}

/* GetFontData: bytes of the DC's TrueType font's file: of table t
   (its tag as four bytes in a DWORD, the first in the low byte; 0: the
   whole file) from offset off; buf NULL: the bytes there are (SDK) */
DWORD GDIAPI GetFontData(HDC hdc, DWORD table, DWORD off, void __far *buf, DWORD n)  /* GDI.311 */
{
    LPTTFILE t;
    LPFONT s = tt_hdc(hdc, &t);
    char tag[5];
    ttfx_i32 start = 0;
    ttfx_u32 len = 0;
    DWORD done = 0;
    if (!s || !tt_open(t))
        return 0xFFFFFFFFUL;
    if (table) {
        tag[0] = (char)table;
        tag[1] = (char)(table >> 8);
        tag[2] = (char)(table >> 16);
        tag[3] = (char)(table >> 24);
        tag[4] = 0;
        start = ttfx_FindTable(&ttf, tag, &len);
        if (!start) {
            tt_close();
            return 0xFFFFFFFFUL;
        }
    } else {
        len = (ttfx_u32)_llseek(tt_fh, 0, 2);
    }
    if (off > len) {
        tt_close();
        return 0xFFFFFFFFUL;
    }
    len -= off;
    if (!buf) {
        tt_close();
        return len;
    }
    if (n > len)
        n = len;
    while (done < n) {
        WORD k = n - done > 0x8000UL ? 0x8000 : (WORD)(n - done);
        ttfx_Read(&ttf, start + (ttfx_i32)(off + done), font_huge(buf, done), k);
        done += k;
    }
    tt_close();
    return done;
}

/* GetCharABCWidths: TrueType only (SDK; FALSE for other fonts). A is
   where the glyph's pixels start, B their width, C the rest of the
   advance; a blank glyph A 0, B 0, C the advance. UNVERIFIED against
   3.1 (hinted extents; B for a blank glyph), and in device units */
BOOL GDIAPI GetCharABCWidths(HDC hdc, WORD first, WORD last, void __far *p) /* GDI.307 */
{
    LPTTFILE t;
    LPFONT s = tt_hdc(hdc, &t);
    short __far *abc = p;
    ttfx_scale sy, sx;
    WORD c;
    if (!s || !p || last < first || !tt_open(t))
        return FALSE;
    sy = ttfx_ScaleForMappingEmToPixels(&ttf, s->ppem);
    sx = s->xppem ? ttfx_ScaleForMappingEmToPixels(&ttf, s->xppem) : sy;
    for (c = first; c <= last; c++) {
        LPFONTINFO fi = s->fi;
        WORD i = c >= fi->dfFirstChar && c <= fi->dfLastChar ? c - fi->dfFirstChar : fi->dfDefaultChar;
        int w = *(WORD __far *)((BYTE __far *)fi - 0x42 + 0x94 + i * 6), x0, y0, x1, y1;
        ttfx_GetGlyphBitmapBox(&ttf, tt_glyph(t, (BYTE)(fi->dfFirstChar + i)), sx, sy, 0, 0, &x0, &y0, &x1, &y1);
        if (x1 <= x0)
            x0 = x1 = 0;
        abc[0] = x0;
        abc[1] = x1 - x0;
        abc[2] = w - x1;
        abc += 3;
        if (c == 0xFFFF)
            break;
    }
    tt_close();
    return TRUE;
}

/* GetKerningPairs: every pair of the kern table (GD13 on 3.1: 908 for
   Liberation Sans, most of whose glyphs no ANSI character has), each
   glyph named by its last character, the amounts at the font's size,
   rounded; 0 for other fonts */
int GDIAPI GetKerningPairs(HDC hdc, int n, void __far *p)          /* GDI.332 */
{
    LPTTFILE t;
    LPFONT s = tt_hdc(hdc, &t);
    WORD __far *kp = p;
    int k, nk, out = 0;
    WORD xp, c, first, last;
    WORD gl[256];                       /* each character's glyph */
    if (!s || !tt_open(t))
        return 0;
    nk = ttfx_GetKerningTableLength(&ttf);
    if (!p) {
        tt_close();
        return nk;
    }
    xp = s->xppem ? s->xppem : s->ppem;
    first = s->fi->dfFirstChar;
    last = s->fi->dfLastChar;
    for (c = first; c <= last; c++)
        gl[c] = tt_glyph(t, (BYTE)c);
    /* GD13 on 3.1: of the first n entries, those whose glyphs both have
       a character (Liberation Sans, n 4: 3 pairs) */
    for (k = 0; k < nk && k < n; k++) {
        ttfx_u16 g1, g2;
        int adv;
        WORD c1 = 0, c2 = 0;
        ttfx_GetKerningEntry(&ttf, k, &g1, &g2, &adv);
        /* the characters with those glyphs: the last of each (GD13 on
           3.1: Arial's space glyph is A0h's, not 20h's) */
        for (c = first; c <= last; c++) {
            if (gl[c] == g1)
                c1 = c;
            if (gl[c] == g2)
                c2 = c;
        }
        if (!c1 || !c2)
            continue;
        kp[0] = c1;
        kp[1] = c2;
        kp[2] = (WORD)rnd(adv, xp, t->upem);
        kp += 3;
        out++;
    }
    tt_close();
    return out;
}

/* ---- CreateScalableFontResource: writing a .FOT ---- */

/* a name record as ANSI (platform 3 Unicode, US English; else Macintosh
   Roman), at most n - 1 characters */
static int tt_name(int id, char __far *out, int n)
{
    BYTE buf[128];
    int len = ttfx_GetFontNameString(&ttf, buf, sizeof buf, 3, 1, 0x409, id), i, k = 0;
    if (len < 0)
        len = ttfx_GetFontNameString(&ttf, buf, sizeof buf, 3, 0, 0x409, id);
    if (len >= 0) {
        for (i = 0; i + 1 < len && i + 1 < (int)sizeof buf && k < n - 1; i += 2)
            out[k++] = buf[i] ? '?' : (char)buf[i + 1];
    } else {
        len = ttfx_GetFontNameString(&ttf, buf, sizeof buf, 1, 0, 0, id);
        for (i = 0; i < len && i < (int)sizeof buf && k < n - 1; i++)
            out[k++] = (char)buf[i];
    }
    out[k] = 0;
    return k;
}

static void put16(BYTE __far *p, WORD v)
{
    p[0] = (BYTE)v;
    p[1] = (BYTE)(v >> 8);
}

static void put32(BYTE __far *p, DWORD v)
{
    put16(p, (WORD)v);
    put16(p + 2, (WORD)(v >> 16));
}

/* w16's own DOS stub: prints a line and ends */
static const BYTE fot_stub[] = {
    0x0E, 0x1F, 0xBA, 0x0E, 0x00, 0xB4, 0x09, 0xCD, 0x21, 0xB8, 0x01, 0x4C, 0xCD, 0x21
};
static const char fot_msg[] = "w16 TrueType font resource, not a program.\r\n$";

#define FOT_NE 0x80
#define FOT_SIZE 0x600

/* The .FOT as 3.1's (the reference install's 14 .FOT files read as data:
   their NE header, resource table and resources; the FONTDIR entry's
   every field derived from the .TTF matches 3.1's for all of them): a
   module without code or data (flags 8000h, expected version 3.00),
   module name the .FOT's file name, description "FONTRES:" and the
   face; a FONTDIR resource (its name "FONTDIR") of one entry and a
   resource of type CCh, number 1, holding lpszFontFile as given, 128
   bytes (GD13: 3.1's for Liberation Sans byte for byte but this). The
   entry, in font units: dfVersion 200h, dfSize 95h, dfCopyright the
   marker 3.1's have ("Windows! Windows! Windows!", then 10h 03h 01h 01h;
   UNVERIFIED whether 3.1 checks it), dfType 3 with OS/2 fsSelection's
   low byte above, dfPoints the units per em, resolutions 72, dfAscent
   usWinAscent, dfInternalLeading usWinAscent + usWinDescent - em,
   dfExternalLeading as tmExternalLeading's, dfItalic FFh for an italic
   font, dfWeight usWeightClass, dfCharSet 2 for a symbol cmap, else 0,
   dfPixHeight usWinAscent + usWinDescent, dfPitchAndFamily 6 (vector,
   TrueType) | 1 unless post says fixed pitch, with the family from
   PANOSE (fixed pitch or monospaced proportion: FF_MODERN; serif style
   11-13: FF_SWISS; 0 or 1: FF_DONTCARE; else FF_ROMAN), dfAvgWidth
   xAvgCharWidth, dfMaxWidth head's xMax - xMin, characters 1Eh-FFh
   (default 1, break 2 after the first), dfFace 76h, dfReserved head's
   lowestRecPPEM, F0000000h added for a symbol font (Arial 11, Wingdings
   12, Liberation Sans 8 on 3.1: GD13); then an
   empty device name, the family name, the full name and the style
   (name records 1, 4 and 2). w16's DOS stub is its own. UNVERIFIED:
   fHidden (not stored), and the header fields no program reads. */
BOOL GDIAPI CreateScalableFontResource(WORD hidden, LPCSTR fot, LPCSTR ttfile, LPCSTR curpath)  /* GDI.310 */
{
    TTFILE t;
    HGLOBAL hb;
    BYTE __far *b, __far *ne, __far *rt, __far *e, __far *q;
    char family[64], full[64], style[64], module[9];
    WORD i, k, off, fdir, fdlen, ttlen, nr;
    int fh, panose1, panose3, fixed;
    long o;
    LPCSTR base;
    (void)hidden;
    if (!fot || !ttfile)
        return FALSE;
    mem_set(&t, 0, sizeof t);
    k = 0;
    if (curpath)
        for (; curpath[k] && k < 120; k++)
            t.path[k] = curpath[k];
    if (k && t.path[k - 1] != '\\' && t.path[k - 1] != ':')
        t.path[k++] = '\\';
    for (i = 0; ttfile[i] && k < 127; i++)
        t.path[k++] = ttfile[i];
    if (!tt_open(&t))
        return FALSE;
    tt_load(&t);
    hb = GlobalAlloc(GMEM_MOVEABLE | GMEM_ZEROINIT, FOT_SIZE);
    b = hb ? GlobalLock(hb) : 0;
    if (!b) {
        if (hb)
            GlobalFree(hb);
        tt_close();
        return FALSE;
    }
    tt_name(1, family, sizeof family);
    tt_name(4, full, sizeof full);
    tt_name(2, style, sizeof style);
    /* the module name: the .FOT's file name, without extension */
    for (base = fot, i = 0; fot[i]; i++)
        if (fot[i] == '\\' || fot[i] == '/' || fot[i] == ':')
            base = fot + i + 1;
    for (i = 0; i < 8 && base[i] && base[i] != '.'; i++)
        module[i] = base[i] >= 'a' && base[i] <= 'z' ? base[i] - 32 : base[i];
    module[i] = 0;

    /* MZ header and stub */
    b[0] = 'M';
    b[1] = 'Z';
    put16(b + 2, FOT_SIZE % 512);
    put16(b + 4, (FOT_SIZE + 511) / 512);
    put16(b + 8, 4);                    /* header paragraphs */
    put16(b + 0x0C, 0xFFFF);
    put16(b + 0x10, 0xB8);              /* SP */
    put16(b + 0x18, 0x40);              /* relocations at */
    put32(b + 0x3C, FOT_NE);
    mem_copy(b + 0x40, fot_stub, sizeof fot_stub);
    mem_copy(b + 0x40 + sizeof fot_stub, fot_msg, sizeof fot_msg);

    /* NE header */
    ne = b + FOT_NE;
    ne[0] = 'N';
    ne[1] = 'E';
    ne[2] = 5;
    ne[3] = 10;
    put16(ne + 0x0C, 0x8000);           /* a library without data */
    put16(ne + 0x22, 0x40);             /* segment table (none) */
    put16(ne + 0x24, 0x40);             /* resource table */
    put16(ne + 0x32, 4);
    put16(ne + 0x34, 2);                /* resources */
    ne[0x36] = 2;                       /* Windows */
    put16(ne + 0x3E, 0x0300);
    /* the resource table: FONTDIR (named), CCh number 1, the names */
    rt = ne + 0x40;
    put16(rt, 4);
    ttlen = (WORD)((lstrlen(ttfile) + 1 + 15) & ~15);
    if (ttlen < 128)
        ttlen = 128;
    off = 0x400;                        /* the resources, from 400h */
    put16(rt + 2, 0x8007);
    put16(rt + 4, 1);
    put16(rt + 10, (WORD)((off + ttlen) >> 4));
    fdir = off + ttlen;
    put16(rt + 14, 0x0C50);
    /* (the FONTDIR's length and name offset: below) */
    put16(rt + 22, 0x80CC);
    put16(rt + 24, 1);
    put16(rt + 30, off >> 4);
    put16(rt + 32, ttlen >> 4);
    put16(rt + 34, 0x0C50);
    put16(rt + 36, 0x8001);
    put16(rt + 42, 0);                  /* end of types */
    put16(rt + 16, 44);                 /* the FONTDIR's name, at 44 */
    rt[44] = 7;
    mem_copy(rt + 45, "FONTDIR", 7);
    rt[52] = 0;                         /* end of names */
    /* resident names: the module */
    q = rt + 53;
    put16(ne + 0x26, (WORD)(q - ne));
    nr = lstrlen(module);
    q[0] = (BYTE)nr;
    mem_copy(q + 1, module, nr);
    q += 1 + nr + 2;
    *q++ = 0;
    put16(ne + 0x28, (WORD)(q - ne));   /* module references: none */
    put16(ne + 0x2A, (WORD)(q - ne));   /* imported names */
    *q++ = 0;
    put16(ne + 0x04, (WORD)(q - ne));   /* entry table: empty */
    put16(ne + 0x06, 2);
    q += 2;
    /* non-resident names: the description */
    put32(ne + 0x2C, (DWORD)(q - b));
    nr = 8 + lstrlen(family);
    q[0] = (BYTE)nr;
    mem_copy(q + 1, "FONTRES:", 8);
    mem_copy(q + 9, family, nr - 8);
    q += 1 + nr + 2;
    *q++ = 0;
    put16(ne + 0x20, (WORD)(1 + nr + 2 + 1));

    /* the CCh resource: the file name as given */
    mem_copy(b + off, ttfile, lstrlen(ttfile));
    /* the FONTDIR */
    q = b + fdir;
    put16(q, 1);
    put16(q + 2, 0);
    e = q + 4;
    put16(e, 0x200);
    put32(e + 2, 0x95);
    mem_copy(e + 6, "Windows! Windows! Windows!", 26);
    e[33] = 0x10;
    e[34] = 0x03;
    e[35] = 0x01;
    e[36] = 0x01;
    o = ttf.os2;
    put16(e + 66, (WORD)((o ? ttfx_Byte(&ttf, o + 63) : 0) << 8 | 3));
    put16(e + 68, t.upem);
    put16(e + 70, 72);
    put16(e + 72, 72);
    put16(e + 74, t.winA);
    put16(e + 76, t.winA + t.winD - t.upem);
    put16(e + 78, t.gap);
    e[80] = o && (ttfx_Byte(&ttf, o + 63) & 1) ? 0xFF : 0;
    put16(e + 83, o ? ttfx_U16(&ttf, o + 4) : 400);
    e[85] = t.symbol ? 2 : 0;
    put16(e + 88, t.winA + t.winD);
    panose1 = o ? ttfx_Byte(&ttf, o + 33) : 0;
    panose3 = o ? ttfx_Byte(&ttf, o + 35) : 0;
    fixed = ttf.post && ttfx_U32(&ttf, ttf.post + 12) != 0;
    e[90] = (BYTE)((fixed || panose3 == 9 ? 0x30 : panose1 >= 11 && panose1 <= 13 ? 0x20 :
                    panose1 <= 1 ? 0x00 : 0x10) | (fixed ? 0 : 1) | 6);
    put16(e + 91, t.xavg);
    put16(e + 93, t.bboxw);
    e[95] = 0x1E;
    e[96] = 0xFF;
    e[97] = 1;
    e[98] = 2;
    put32(e + 105, 0x76);
    put32(e + 109, (t.symbol ? 0xF0000000UL : 0) | ttfx_U16(&ttf, ttf.head + 46));
    q = e + 113;
    *q++ = 0;                           /* the device: none */
    for (i = 0; family[i]; i++)
        *q++ = family[i];
    *q++ = 0;
    for (i = 0; full[i]; i++)
        *q++ = full[i];
    *q++ = 0;
    for (i = 0; style[i]; i++)
        *q++ = style[i];
    *q++ = 0;
    fdlen = (WORD)((q - (b + fdir) + 15) & ~15);
    put16(rt + 12, fdlen >> 4);
    tt_close();
    fh = _lcreat(fot, 0);
    k = fh >= 0 && _lwrite(fh, b, fdir + fdlen) == fdir + fdlen;
    if (fh >= 0)
        _lclose(fh);
    GlobalUnlock(hb);
    GlobalFree(hb);
    return k;
}
