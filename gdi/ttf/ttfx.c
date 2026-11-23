/*
 * ttfx.c - TrueType in fixed point (ttfx.h): adapted from stb_truetype
 * v1.28 (stb_truetype.h in this directory) by Sean Barrett / RAD Game
 * Tools and contributors, used under its public-domain dedication.
 *
 * The parsing (tables, cmap formats 0, 4, 6, 12 and 13, simple and
 * compound glyphs, hmtx, kern format 0, name) follows stb_truetype
 * function for function; what differs:
 *
 * - Data: stb_truetype reads a font that is one block in memory. Here
 *   every read goes through ttfx__at, a cache of 512-byte pages (each with
 *   16 bytes of the next, so that reads of up to 16 bytes never straddle)
 *   filled by the program's callback; a glyph's outline is read in one
 *   piece into a buffer of its own and parsed there as stb_truetype does.
 * - Integers: OpenWatcom's 16-bit int. Offsets, counts that can pass
 *   32767 and products are ttfx_i32; loop indexes into blocks under 64K
 *   are unsigned.
 * - Numbers: no floating point. Scales are 8.24 fixed point; scaled
 *   coordinates are 26.6 (1/64 pixel), the TrueType rasteriser's F26Dot6,
 *   rounded to nearest; the compound-glyph matrix is the file's 2.14
 *   values, applied in integers with the truncation toward zero that
 *   stb_truetype's float-to-short conversion does; the scaled component
 *   offset uses an integer square root.
 * - Curves: quadratic Beziers are flattened in pixel space by the same
 *   midpoint subdivision and the same 0.35-pixel flatness test as
 *   stb_truetype (which tests in font units against 0.35 / scale), in
 *   16.16 arithmetic; the points come out 26.6.
 * - The rasteriser: monochrome, one sample at each pixel's centre, the
 *   TrueType specification's scan-conversion rules 1 and 2 (a pixel is
 *   on when its centre is inside the outline, or on it), non-zero
 *   winding; no anti-aliasing. Edges are stepped from one centre row to
 *   the next exactly (an integer part and a remainder, as a DDA), so
 *   there is no rounding drift. Rule 3 (dropout control) and hinting are
 *   not done (stage 1).
 * - Left out: CFF/Type 2 outlines, SVG, GPOS kerning, TrueType
 *   collections, SDF, the packing and baking helpers, the 3D sample code.
 */
#include "ttfx.h"

/* ---- arithmetic ---- */

static ttfx_i32 ttfx__abs(ttfx_i32 v)
{
    return v < 0 ? -v : v;
}

/* a * b as an unsigned 64-bit value */
static void ttfx__mul64(ttfx_u32 a, ttfx_u32 b, ttfx_u32 *hi, ttfx_u32 *lo)
{
    ttfx_u32 al = a & 0xFFFF, ah = a >> 16, bl = b & 0xFFFF, bh = b >> 16;
    ttfx_u32 ll = al * bl, lh = al * bh, hl = ah * bl, hh = ah * bh;
    ttfx_u32 mid = (ll >> 16) + (lh & 0xFFFF) + (hl & 0xFFFF);
    *lo = (ll & 0xFFFF) | (mid << 16);
    *hi = hh + (lh >> 16) + (hl >> 16) + (mid >> 16);
}

/* (hi:lo) / c, the quotient less than 2^32 (hi < c) */
static ttfx_u32 ttfx__div64(ttfx_u32 hi, ttfx_u32 lo, ttfx_u32 c, ttfx_u32 *rem)
{
    ttfx_u32 q = 0;
    int i;
    if (!hi) {
        *rem = lo % c;
        return lo / c;
    }
    for (i = 0; i < 32; i++) {
        ttfx_u32 carry = hi >> 31;
        hi = (hi << 1) | (lo >> 31);
        lo <<= 1;
        q <<= 1;
        if (carry || hi >= c) {
            hi -= c;
            q |= 1;
        }
    }
    *rem = hi;
    return q;
}

/* floor(a * b / c) and the remainder (0 <= rem < c), c > 0 */
static ttfx_i32 ttfx__muldiv(ttfx_i32 a, ttfx_i32 b, ttfx_i32 c, ttfx_i32 *prem)
{
    ttfx_u32 ua = (ttfx_u32)ttfx__abs(a), ub = (ttfx_u32)ttfx__abs(b), hi, lo, q, r;
    int neg = (a < 0) != (b < 0);
    if (ua < 0x8000L && ub < 0x10000L) {
        lo = ua * ub;
        hi = 0;
    } else
        ttfx__mul64(ua, ub, &hi, &lo);
    q = ttfx__div64(hi, lo, (ttfx_u32)c, &r);
    if (neg && r) {
        q++;
        r = (ttfx_u32)c - r;
    }
    if (prem)
        *prem = (ttfx_i32)r;
    return neg ? -(ttfx_i32)q : (ttfx_i32)q;
}

/* the integer square root of v */
static ttfx_u32 ttfx__isqrt(ttfx_u32 v)
{
    ttfx_u32 r = 0, b = 0x40000000UL;
    while (b > v)
        b >>= 2;
    while (b) {
        if (v >= r + b) {
            v -= r + b;
            r = (r >> 1) + b;
        } else
            r >>= 1;
        b >>= 2;
    }
    return r;
}

static void ttfx__copy(void TTFX_FAR *dst, const void TTFX_FAR *src, ttfx_u32 n)
{
    ttfx_u8 TTFX_FAR *d = (ttfx_u8 TTFX_FAR *)dst;
    const ttfx_u8 TTFX_FAR *s = (const ttfx_u8 TTFX_FAR *)src;
    while (n--)
        *d++ = *s++;
}

/* ---- reading the file: the page cache ---- */

static const ttfx_u8 ttfx__zero[TTFX_SLACK];

#define SLOT_PAGE(p) (*(ttfx_i32 TTFX_FAR *)(p))
#define SLOT_AGE(p) (*(ttfx_u16 TTFX_FAR *)((p) + 4))
#define SLOT_DATA(p) ((p) + 8)

static const ttfx_u8 TTFX_FAR *ttfx__at(const ttfx_font *cf, ttfx_i32 off)
{
    ttfx_font *f = (ttfx_font *)cf;
    ttfx_i32 page;
    ttfx_u8 TTFX_FAR *s, TTFX_FAR *victim;
    unsigned i, n;
    ttfx_u16 oldest = 0xFFFF;
    if (off < 0)
        return ttfx__zero;
    page = off >> 9;
    if (page == f->lastpage)
        return f->lastdata + (unsigned)(off & (TTFX_PAGE - 1));
    if (!++f->clock) {                  /* (the ages start again) */
        for (i = 0; i < f->nslots; i++)
            SLOT_AGE(f->cache + i * TTFX_SLOT) = 0;
        f->clock = 1;
    }
    victim = f->cache;
    for (i = 0; i < f->nslots; i++) {
        s = f->cache + i * TTFX_SLOT;
        if (SLOT_PAGE(s) == page) {
            SLOT_AGE(s) = f->clock;
            f->lastpage = page;
            f->lastdata = SLOT_DATA(s);
            return f->lastdata + (unsigned)(off & (TTFX_PAGE - 1));
        }
        if (SLOT_AGE(s) < oldest) {
            oldest = SLOT_AGE(s);
            victim = s;
        }
    }
    n = f->read(f->user, (ttfx_u32)page << 9, SLOT_DATA(victim), TTFX_PAGE + TTFX_SLACK);
    for (i = n; i < TTFX_PAGE + TTFX_SLACK; i++)
        SLOT_DATA(victim)[i] = 0;
    SLOT_PAGE(victim) = page;
    SLOT_AGE(victim) = f->clock;
    f->lastpage = page;
    f->lastdata = SLOT_DATA(victim);
    return f->lastdata + (unsigned)(off & (TTFX_PAGE - 1));
}

ttfx_u8 ttfx_Byte(const ttfx_font *f, ttfx_i32 off)
{
    return ttfx__at(f, off)[0];
}

ttfx_u16 ttfx_U16(const ttfx_font *f, ttfx_i32 off)
{
    const ttfx_u8 TTFX_FAR *p = ttfx__at(f, off);
    return (ttfx_u16)(((ttfx_u16)p[0] << 8) | p[1]);
}

ttfx_i16 ttfx_I16(const ttfx_font *f, ttfx_i32 off)
{
    return (ttfx_i16)ttfx_U16(f, off);
}

ttfx_u32 ttfx_U32(const ttfx_font *f, ttfx_i32 off)
{
    const ttfx_u8 TTFX_FAR *p = ttfx__at(f, off);
    return ((ttfx_u32)p[0] << 24) | ((ttfx_u32)p[1] << 16) | ((ttfx_u32)p[2] << 8) | p[3];
}

#define ttBYTE(o) ttfx_Byte(f, (o))
#define ttCHAR(o) ((ttfx_i8)ttfx_Byte(f, (o)))
#define ttUSHORT(o) ttfx_U16(f, (o))
#define ttSHORT(o) ttfx_I16(f, (o))
#define ttULONG(o) ttfx_U32(f, (o))

void ttfx_Read(const ttfx_font *f, ttfx_i32 off, void TTFX_FAR *dst, unsigned n)
{
    ttfx_u8 TTFX_FAR *d = (ttfx_u8 TTFX_FAR *)dst;
    unsigned got = n > 0 && off >= 0 ? f->read(f->user, (ttfx_u32)off, d, n) : 0;
    for (; got < n; got++)
        d[got] = 0;
}

static int ttfx__tag(const ttfx_u8 TTFX_FAR *p, const char *t)
{
    return p[0] == (ttfx_u8)t[0] && p[1] == (ttfx_u8)t[1] && p[2] == (ttfx_u8)t[2] &&
           p[3] == (ttfx_u8)t[3];
}

static ttfx_i32 ttfx__find_table(const ttfx_font *f, ttfx_i32 fontstart, const char *tag, ttfx_u32 *len)
{
    ttfx_u16 num_tables = ttUSHORT(fontstart + 4), i;
    ttfx_i32 tabledir = fontstart + 12;
    for (i = 0; i < num_tables; ++i) {
        ttfx_i32 loc = tabledir + 16L * i;
        if (ttfx__tag(ttfx__at(f, loc), tag)) {
            if (len)
                *len = ttULONG(loc + 12);
            return (ttfx_i32)ttULONG(loc + 8);
        }
    }
    if (len)
        *len = 0;
    return 0;
}

ttfx_i32 ttfx_FindTable(const ttfx_font *f, const char *tag, ttfx_u32 *len)
{
    return ttfx__find_table(f, f->fontstart, tag, len);
}

/* ---- loading ---- */

int ttfx_InitFont(ttfx_font *f, ttfx_readfn read, void *user, void *alloc,
                  void TTFX_FAR *cache, unsigned cache_bytes, ttfx_i32 fontstart)
{
    ttfx_i32 cmap, t, symbol_map = 0;
    ttfx_u16 numTables, i;
    const ttfx_u8 TTFX_FAR *v;
    ttfx_u16 sym_plat = 0, sym_enc = 0;
    f->read = read;
    f->user = user;
    f->alloc = alloc;
    f->cache = (ttfx_u8 TTFX_FAR *)cache;
    f->nslots = cache_bytes / TTFX_SLOT;
    f->clock = 0;
    f->lastpage = -1;
    f->lastdata = 0;
    if (f->nslots < 2)
        return 0;
    for (i = 0; i < f->nslots; i++) {
        SLOT_PAGE(f->cache + i * TTFX_SLOT) = -1;
        SLOT_AGE(f->cache + i * TTFX_SLOT) = 0;
    }
    f->fontstart = fontstart;
    /* TrueType outlines only (stb_truetype's isfont, less "typ1" and
       "OTTO") */
    v = ttfx__at(f, fontstart);
    if (!(v[0] == '1' && !v[1] && !v[2] && !v[3]) && !(!v[0] && v[1] == 1 && !v[2] && !v[3]) &&
        !ttfx__tag(v, "true"))
        return 0;

    cmap = ttfx__find_table(f, fontstart, "cmap", 0);
    f->loca = ttfx__find_table(f, fontstart, "loca", 0);
    f->head = ttfx__find_table(f, fontstart, "head", 0);
    f->glyf = ttfx__find_table(f, fontstart, "glyf", 0);
    f->hhea = ttfx__find_table(f, fontstart, "hhea", 0);
    f->hmtx = ttfx__find_table(f, fontstart, "hmtx", 0);
    f->kern = ttfx__find_table(f, fontstart, "kern", 0);
    f->os2 = ttfx__find_table(f, fontstart, "OS/2", 0);
    f->post = ttfx__find_table(f, fontstart, "post", 0);
    f->name = ttfx__find_table(f, fontstart, "name", 0);
    f->maxp = ttfx__find_table(f, fontstart, "maxp", 0);
    if (!cmap || !f->head || !f->hhea || !f->hmtx || !f->glyf || !f->loca)
        return 0;

    t = f->maxp;
    f->numGlyphs = t ? ttUSHORT(t + 4) : 0xFFFF;

    /* the cmap subtable, as stb_truetype chooses it: Microsoft Unicode
       (BMP or full) or any Unicode-platform one, the last listed; else
       Microsoft Symbol */
    numTables = ttUSHORT(cmap + 2);
    f->index_map = 0;
    for (i = 0; i < numTables; ++i) {
        ttfx_i32 rec = cmap + 4 + 8L * i;
        ttfx_u16 plat = ttUSHORT(rec), enc = ttUSHORT(rec + 2);
        switch (plat) {
        case TTFX_PLATFORM_ID_MICROSOFT:
            switch (enc) {
            case TTFX_MS_EID_UNICODE_BMP:
            case TTFX_MS_EID_UNICODE_FULL:
                f->index_map = cmap + (ttfx_i32)ttULONG(rec + 4);
                f->index_map_platform = plat;
                f->index_map_encoding = enc;
                break;
            case TTFX_MS_EID_SYMBOL:
                symbol_map = cmap + (ttfx_i32)ttULONG(rec + 4);
                sym_plat = plat;
                sym_enc = enc;
                break;
            }
            break;
        case TTFX_PLATFORM_ID_UNICODE:
            f->index_map = cmap + (ttfx_i32)ttULONG(rec + 4);
            f->index_map_platform = plat;
            f->index_map_encoding = enc;
            break;
        }
    }
    if (!f->index_map) {
        f->index_map = symbol_map;
        f->index_map_platform = sym_plat;
        f->index_map_encoding = sym_enc;
    }
    if (!f->index_map)
        return 0;
    f->indexToLocFormat = ttUSHORT(f->head + 50);
    f->unitsPerEm = ttUSHORT(f->head + 18);
    if (!f->unitsPerEm)
        return 0;
    f->hint = TTFX_HINT_DEFAULT;
    f->hint_done = 0;
    return 1;
}

void ttfx_SetHinting(ttfx_font *f, int flags)
{
    f->hint = flags & TTFX_HINT_DEFAULT;
}

/* ---- characters to glyphs ---- */

ttfx_u16 ttfx_FindGlyphIndex(const ttfx_font *f, ttfx_i32 c)
{
    ttfx_i32 index_map = f->index_map;
    ttfx_u16 format = ttUSHORT(index_map);
    if (c < 0)
        return 0;
    if (format == 0) {                  /* Apple byte encoding */
        ttfx_i32 bytes = ttUSHORT(index_map + 2);
        if (c < bytes - 6)
            return ttBYTE(index_map + 6 + c);
        return 0;
    } else if (format == 6) {
        ttfx_i32 first = ttUSHORT(index_map + 6), count = ttUSHORT(index_map + 8);
        if (c >= first && c < first + count)
            return ttUSHORT(index_map + 10 + (c - first) * 2);
        return 0;
    } else if (format == 4) {           /* binary search of the segments */
        ttfx_u16 segcount = ttUSHORT(index_map + 6) >> 1;
        ttfx_u16 searchRange = ttUSHORT(index_map + 8) >> 1;
        ttfx_u16 entrySelector = ttUSHORT(index_map + 10);
        ttfx_u16 rangeShift = ttUSHORT(index_map + 12) >> 1;
        ttfx_i32 endCount = index_map + 14, search = endCount;
        if (c > 0xFFFF)
            return 0;
        if (c >= ttUSHORT(search + (ttfx_i32)rangeShift * 2))
            search += (ttfx_i32)rangeShift * 2;
        search -= 2;
        while (entrySelector) {
            ttfx_u16 end;
            searchRange >>= 1;
            end = ttUSHORT(search + (ttfx_i32)searchRange * 2);
            if (c > end)
                search += (ttfx_i32)searchRange * 2;
            --entrySelector;
        }
        search += 2;
        {
            ttfx_u16 offset, start, last, index;
            ttfx_i16 delta;
            ttfx_i32 item = (search - endCount) >> 1;
            start = ttUSHORT(index_map + 14 + (ttfx_i32)segcount * 2 + 2 + 2 * item);
            last = ttUSHORT(endCount + 2 * item);
            if (c < start || c > last)
                return 0;
            offset = ttUSHORT(index_map + 14 + (ttfx_i32)segcount * 6 + 2 + 2 * item);
            delta = ttSHORT(index_map + 14 + (ttfx_i32)segcount * 4 + 2 + 2 * item);
            if (offset == 0)
                return (ttfx_u16)(c + delta);
            index = ttUSHORT(offset + (c - start) * 2 + index_map + 14 + (ttfx_i32)segcount * 6 + 2 + 2 * item);
            if (index != 0)
                index = (ttfx_u16)(index + delta);
            return index;
        }
    } else if (format == 12 || format == 13) {
        ttfx_i32 low = 0, high = (ttfx_i32)ttULONG(index_map + 12);
        while (low < high) {
            ttfx_i32 mid = low + ((high - low) >> 1);
            ttfx_u32 start_char = ttULONG(index_map + 16 + mid * 12);
            ttfx_u32 end_char = ttULONG(index_map + 16 + mid * 12 + 4);
            if ((ttfx_u32)c < start_char)
                high = mid;
            else if ((ttfx_u32)c > end_char)
                low = mid + 1;
            else {
                ttfx_u32 start_glyph = ttULONG(index_map + 16 + mid * 12 + 8);
                if (format == 12)
                    return (ttfx_u16)(start_glyph + (ttfx_u32)c - start_char);
                return (ttfx_u16)start_glyph;
            }
        }
        return 0;
    }
    return 0;                           /* format 2 and others: not done */
}

/* ---- glyphs ---- */

static ttfx_i32 ttfx__glyf_offset(const ttfx_font *f, ttfx_u16 glyph, ttfx_u32 *len)
{
    ttfx_i32 g1, g2;
    if (glyph >= f->numGlyphs || f->indexToLocFormat >= 2)
        return -1;
    if (f->indexToLocFormat == 0) {
        g1 = f->glyf + (ttfx_i32)ttUSHORT(f->loca + (ttfx_i32)glyph * 2) * 2;
        g2 = f->glyf + (ttfx_i32)ttUSHORT(f->loca + (ttfx_i32)glyph * 2 + 2) * 2;
    } else {
        g1 = f->glyf + (ttfx_i32)ttULONG(f->loca + (ttfx_i32)glyph * 4);
        g2 = f->glyf + (ttfx_i32)ttULONG(f->loca + (ttfx_i32)glyph * 4 + 4);
    }
    if (len)
        *len = g2 > g1 ? (ttfx_u32)(g2 - g1) : 0;
    return g1 == g2 ? -1 : g1;
}

int ttfx_GetGlyphBox(const ttfx_font *f, ttfx_u16 glyph, int *x0, int *y0, int *x1, int *y1)
{
    ttfx_i32 g = ttfx__glyf_offset(f, glyph, 0);
    if (g < 0)
        return 0;
    if (x0) *x0 = ttSHORT(g + 2);
    if (y0) *y0 = ttSHORT(g + 4);
    if (x1) *x1 = ttSHORT(g + 6);
    if (y1) *y1 = ttSHORT(g + 8);
    return 1;
}

int ttfx_IsGlyphEmpty(const ttfx_font *f, ttfx_u16 glyph)
{
    ttfx_i32 g = ttfx__glyf_offset(f, glyph, 0);
    if (g < 0)
        return 1;
    return ttSHORT(g) == 0;
}

static void ttfx__setvertex(ttfx_vertex TTFX_FAR *v, ttfx_u8 type, ttfx_i32 x, ttfx_i32 y, ttfx_i32 cx, ttfx_i32 cy)
{
    v->type = type;
    v->x = (ttfx_i16)x;
    v->y = (ttfx_i16)y;
    v->cx = (ttfx_i16)cx;
    v->cy = (ttfx_i16)cy;
}

static unsigned ttfx__close_shape(ttfx_vertex TTFX_FAR *vertices, unsigned num_vertices, int was_off, int start_off,
                                  ttfx_i32 sx, ttfx_i32 sy, ttfx_i32 scx, ttfx_i32 scy, ttfx_i32 cx, ttfx_i32 cy)
{
    if (start_off) {
        if (was_off)
            ttfx__setvertex(&vertices[num_vertices++], TTFX_vcurve, (cx + scx) >> 1, (cy + scy) >> 1, cx, cy);
        ttfx__setvertex(&vertices[num_vertices++], TTFX_vcurve, sx, sy, scx, scy);
    } else {
        if (was_off)
            ttfx__setvertex(&vertices[num_vertices++], TTFX_vcurve, sx, sy, cx, cy);
        else
            ttfx__setvertex(&vertices[num_vertices++], TTFX_vline, sx, sy, 0, 0);
    }
    return num_vertices;
}

/* x * m / 16384 + e, then truncated toward zero, as stb_truetype's
   (short)(float) conversion of mtx[0] * x + mtx[2] * y + mtx[4] does:
   num = a * x + c * y, e14 the offset times 16384 */
static ttfx_i32 ttfx__xform(ttfx_i32 num, ttfx_i32 e14)
{
    ttfx_i32 q = num / 16384, r = num % 16384;  /* (truncated) */
    ttfx_i32 eq = e14 / 16384, er = e14 % 16384;
    ttfx_i32 whole = q + eq, frac = r + er;     /* value = whole + frac / 16384 */
    whole += frac / 16384;
    frac %= 16384;
    if (whole > 0 && frac < 0)
        return whole - 1;
    if (whole < 0 && frac > 0)
        return whole + 1;
    return whole;
}

#define TTFX_MAXVERTS 6000              /* 60000 bytes of vertices */

static int ttfx__shape(const ttfx_font *f, ttfx_u16 glyph, ttfx_vertex TTFX_FAR **pvertices, int depth)
{
    ttfx_i16 numberOfContours;
    ttfx_u8 TTFX_FAR *data, TTFX_FAR *end;
    ttfx_vertex TTFX_FAR *vertices = 0;
    unsigned num_vertices = 0;
    ttfx_u32 glen;
    ttfx_i32 g = ttfx__glyf_offset(f, glyph, &glen);

    *pvertices = 0;
    if (g < 0 || glen < 10 || glen > 65000L || depth > 8)
        return 0;
    data = (ttfx_u8 TTFX_FAR *)ttfx_alloc(f->alloc, glen + 2);
    if (!data)
        return 0;
    ttfx_Read(f, g, data, (unsigned)glen);
    data[glen] = data[glen + 1] = 0;
    end = data + (unsigned)glen;
#define DSHORT(p) ((ttfx_i16)(((ttfx_u16)(p)[0] << 8) | (p)[1]))
#define DUSHORT(p) ((ttfx_u16)(((ttfx_u16)(p)[0] << 8) | (p)[1]))
#define NEXT() (points < end ? *points++ : 0)
    numberOfContours = DSHORT(data);

    if (numberOfContours > 0) {
        ttfx_u8 flags = 0, flagcount;
        ttfx_u8 TTFX_FAR *endPtsOfContours = data + 10, TTFX_FAR *points;
        unsigned ins, i, j = 0, m, n, next_move, off;
        int was_off = 0, start_off = 0;
        ttfx_i32 x, y, cx, cy, sx, sy, scx, scy;
        if (10 + (ttfx_u32)numberOfContours * 2 + 2 > glen)
            goto fail;
        ins = DUSHORT(data + 10 + numberOfContours * 2);
        points = data + 10 + numberOfContours * 2 + 2 + ins;
        n = 1 + DUSHORT(endPtsOfContours + numberOfContours * 2 - 2);
        if ((ttfx_u32)n + 2L * numberOfContours > TTFX_MAXVERTS)
            goto fail;
        m = n + 2 * numberOfContours;   /* a loose bound on the vertices needed */
        vertices = (ttfx_vertex TTFX_FAR *)ttfx_alloc(f->alloc, (ttfx_u32)m * sizeof(vertices[0]));
        if (!vertices)
            goto fail;
        next_move = 0;
        flagcount = 0;
        /* the points unconverted at the array's end, the result from its
           start */
        off = m - n;
        for (i = 0; i < n; ++i) {
            if (flagcount == 0) {
                flags = NEXT();
                if (flags & 8)
                    flagcount = NEXT();
            } else
                --flagcount;
            vertices[off + i].type = flags;
        }
        x = 0;
        for (i = 0; i < n; ++i) {
            flags = vertices[off + i].type;
            if (flags & 2) {
                ttfx_i16 dx = NEXT();
                x += (flags & 16) ? dx : -dx;
            } else if (!(flags & 16)) {
                ttfx_u8 hb = NEXT();
                x = x + (ttfx_i16)(((ttfx_u16)hb << 8) | NEXT());
            }
            vertices[off + i].x = (ttfx_i16)x;
        }
        y = 0;
        for (i = 0; i < n; ++i) {
            flags = vertices[off + i].type;
            if (flags & 4) {
                ttfx_i16 dy = NEXT();
                y += (flags & 32) ? dy : -dy;
            } else if (!(flags & 32)) {
                ttfx_u8 hb = NEXT();
                y = y + (ttfx_i16)(((ttfx_u16)hb << 8) | NEXT());
            }
            vertices[off + i].y = (ttfx_i16)y;
        }
        num_vertices = 0;
        sx = sy = cx = cy = scx = scy = 0;
        for (i = 0; i < n; ++i) {
            flags = vertices[off + i].type;
            x = (ttfx_i16)vertices[off + i].x;
            y = (ttfx_i16)vertices[off + i].y;
            if (next_move == i) {
                if (i != 0)
                    num_vertices = ttfx__close_shape(vertices, num_vertices, was_off, start_off, sx, sy, scx, scy, cx, cy);
                start_off = !(flags & 1) && i < DUSHORT(endPtsOfContours + j * 2);
                if (start_off) {
                    /* a contour starting off the curve: start on it */
                    scx = x;
                    scy = y;
                    if (!(vertices[off + i + 1].type & 1)) {
                        sx = (x + (ttfx_i32)vertices[off + i + 1].x) >> 1;
                        sy = (y + (ttfx_i32)vertices[off + i + 1].y) >> 1;
                    } else {
                        sx = (ttfx_i32)vertices[off + i + 1].x;
                        sy = (ttfx_i32)vertices[off + i + 1].y;
                        ++i;
                    }
                } else {
                    sx = x;
                    sy = y;
                }
                ttfx__setvertex(&vertices[num_vertices++], TTFX_vmove, sx, sy, 0, 0);
                was_off = 0;
                next_move = 1 + DUSHORT(endPtsOfContours + j * 2);
                ++j;
            } else {
                if (!(flags & 1)) {
                    if (was_off)        /* two off points: an on point between */
                        ttfx__setvertex(&vertices[num_vertices++], TTFX_vcurve, (cx + x) >> 1, (cy + y) >> 1, cx, cy);
                    cx = x;
                    cy = y;
                    was_off = 1;
                } else {
                    if (was_off)
                        ttfx__setvertex(&vertices[num_vertices++], TTFX_vcurve, x, y, cx, cy);
                    else
                        ttfx__setvertex(&vertices[num_vertices++], TTFX_vline, x, y, 0, 0);
                    was_off = 0;
                }
            }
        }
        num_vertices = ttfx__close_shape(vertices, num_vertices, was_off, start_off, sx, sy, scx, scy, cx, cy);
    } else if (numberOfContours < 0) {
        /* compound glyphs */
        int more = 1;
        ttfx_u8 TTFX_FAR *comp = data + 10;
        while (more) {
            ttfx_u16 flags, gidx;
            int comp_num_verts, i;
            ttfx_vertex TTFX_FAR *comp_verts = 0, TTFX_FAR *tmp;
            /* the 2.14 matrix (16384 = 1) and the offset */
            ttfx_i32 ma = 16384, mb = 0, mc = 0, md = 16384, e14, f14;
            ttfx_i16 e = 0, ff = 0;
            if (comp + 4 > end)
                break;
            flags = DUSHORT(comp);
            comp += 2;
            gidx = DUSHORT(comp);
            comp += 2;
            if (flags & 2) {            /* x, y values */
                if (flags & 1) {
                    e = DSHORT(comp);
                    ff = DSHORT(comp + 2);
                    comp += 4;
                } else {
                    e = (ttfx_i8)comp[0];
                    ff = (ttfx_i8)comp[1];
                    comp += 2;
                }
            }
            /* (matching points: not done, as in stb_truetype) */
            if (flags & (1 << 3)) {     /* WE_HAVE_A_SCALE */
                ma = md = DSHORT(comp);
                comp += 2;
            } else if (flags & (1 << 6)) {      /* WE_HAVE_AN_X_AND_YSCALE */
                ma = DSHORT(comp);
                md = DSHORT(comp + 2);
                comp += 4;
            } else if (flags & (1 << 7)) {      /* WE_HAVE_A_TWO_BY_TWO */
                ma = DSHORT(comp);
                mb = DSHORT(comp + 2);
                mc = DSHORT(comp + 4);
                md = DSHORT(comp + 6);
                comp += 8;
            }
            e14 = (ttfx_i32)e * 16384;
            f14 = (ttfx_i32)ff * 16384;
            /* SCALED_COMPONENT_OFFSET without UNSCALED_COMPONENT_OFFSET:
               the offset scaled by each column's length */
            if ((flags & (1 << 11)) && !(flags & (1 << 12))) {
                ttfx_u32 m = ttfx__isqrt((ttfx_u32)(ma * ma) + (ttfx_u32)(mb * mb));
                ttfx_u32 nn = ttfx__isqrt((ttfx_u32)(mc * mc) + (ttfx_u32)(md * md));
                e14 = (ttfx_i32)e * (ttfx_i32)m;
                f14 = (ttfx_i32)ff * (ttfx_i32)nn;
            }
            comp_num_verts = ttfx__shape(f, gidx, &comp_verts, depth + 1);
            if (comp_num_verts > 0) {
                for (i = 0; i < comp_num_verts; ++i) {
                    ttfx_vertex TTFX_FAR *v = &comp_verts[i];
                    ttfx_i32 x = v->x, y = v->y;
                    v->x = (ttfx_i16)ttfx__xform(ma * x + mc * y, e14);
                    v->y = (ttfx_i16)ttfx__xform(mb * x + md * y, f14);
                    x = v->cx;
                    y = v->cy;
                    v->cx = (ttfx_i16)ttfx__xform(ma * x + mc * y, e14);
                    v->cy = (ttfx_i16)ttfx__xform(mb * x + md * y, f14);
                }
                if ((ttfx_u32)num_vertices + comp_num_verts > TTFX_MAXVERTS)
                    tmp = 0;
                else
                    tmp = (ttfx_vertex TTFX_FAR *)ttfx_alloc(f->alloc, ((ttfx_u32)num_vertices + comp_num_verts) * sizeof(ttfx_vertex));
                if (!tmp) {
                    ttfx_free(f->alloc, comp_verts);
                    goto fail;
                }
                if (num_vertices > 0 && vertices)
                    ttfx__copy(tmp, vertices, (ttfx_u32)num_vertices * sizeof(ttfx_vertex));
                ttfx__copy(tmp + num_vertices, comp_verts, (ttfx_u32)comp_num_verts * sizeof(ttfx_vertex));
                if (vertices)
                    ttfx_free(f->alloc, vertices);
                vertices = tmp;
                ttfx_free(f->alloc, comp_verts);
                num_vertices += comp_num_verts;
            }
            more = flags & (1 << 5);
        }
    }
    ttfx_free(f->alloc, data);
    *pvertices = vertices;
    return (int)num_vertices;
fail:
    if (vertices)
        ttfx_free(f->alloc, vertices);
    ttfx_free(f->alloc, data);
    return 0;
#undef NEXT
#undef DSHORT
#undef DUSHORT
}

int ttfx_GetGlyphShape(const ttfx_font *f, ttfx_u16 glyph, ttfx_vertex TTFX_FAR **pv)
{
    return ttfx__shape(f, glyph, pv, 0);
}

void ttfx_FreeShape(const ttfx_font *f, ttfx_vertex TTFX_FAR *v)
{
    if (v)
        ttfx_free(f->alloc, v);
}

/* ---- metrics ---- */

void ttfx_GetGlyphHMetrics(const ttfx_font *f, ttfx_u16 glyph, ttfx_u16 *advanceWidth, int *leftSideBearing)
{
    ttfx_u16 n = ttUSHORT(f->hhea + 34);
    if (glyph < n) {
        if (advanceWidth) *advanceWidth = ttUSHORT(f->hmtx + 4L * glyph);
        if (leftSideBearing) *leftSideBearing = ttSHORT(f->hmtx + 4L * glyph + 2);
    } else {
        if (advanceWidth) *advanceWidth = ttUSHORT(f->hmtx + 4L * (n - 1));
        if (leftSideBearing) *leftSideBearing = ttSHORT(f->hmtx + 4L * n + 2L * (glyph - n));
    }
}

void ttfx_GetFontVMetrics(const ttfx_font *f, int *ascent, int *descent, int *lineGap)
{
    if (ascent) *ascent = ttSHORT(f->hhea + 4);
    if (descent) *descent = ttSHORT(f->hhea + 6);
    if (lineGap) *lineGap = ttSHORT(f->hhea + 8);
}

void ttfx_GetFontBoundingBox(const ttfx_font *f, int *x0, int *y0, int *x1, int *y1)
{
    *x0 = ttSHORT(f->head + 36);
    *y0 = ttSHORT(f->head + 38);
    *x1 = ttSHORT(f->head + 40);
    *y1 = ttSHORT(f->head + 42);
}

/* the first kern subtable, if it is horizontal and format 0 */
static int ttfx__kern_ok(const ttfx_font *f)
{
    return f->kern && ttUSHORT(f->kern + 2) >= 1 && ttUSHORT(f->kern + 8) == 1;
}

int ttfx_GetKerningTableLength(const ttfx_font *f)
{
    return ttfx__kern_ok(f) ? ttUSHORT(f->kern + 10) : 0;
}

int ttfx_GetKerningEntry(const ttfx_font *f, int i, ttfx_u16 *g1, ttfx_u16 *g2, int *advance)
{
    ttfx_i32 p = f->kern + 18 + 6L * (ttfx_u16)i;
    if (!ttfx__kern_ok(f) || (ttfx_u16)i >= ttUSHORT(f->kern + 10))
        return 0;
    *g1 = ttUSHORT(p);
    *g2 = ttUSHORT(p + 2);
    *advance = ttSHORT(p + 4);
    return 1;
}

int ttfx_GetGlyphKernAdvance(const ttfx_font *f, ttfx_u16 glyph1, ttfx_u16 glyph2)
{
    ttfx_u32 needle, straw;
    ttfx_i32 l, r, m;
    if (!ttfx__kern_ok(f))
        return 0;
    l = 0;
    r = (ttfx_i32)ttUSHORT(f->kern + 10) - 1;
    needle = ((ttfx_u32)glyph1 << 16) | glyph2;
    while (l <= r) {
        m = (l + r) >> 1;
        straw = ttULONG(f->kern + 18 + m * 6);
        if (needle < straw)
            r = m - 1;
        else if (needle > straw)
            l = m + 1;
        else
            return ttSHORT(f->kern + 22 + m * 6);
    }
    return 0;
}

/* ---- scales ---- */

static ttfx_scale ttfx__scale(ttfx_i32 pixels, ttfx_i32 units)
{
    ttfx_i32 rem, q;
    if (units <= 0 || pixels <= 0)
        return 0;
    /* pixels * 2^24 / units, to nearest */
    q = ttfx__muldiv(pixels, 0x01000000L, units, &rem);
    if (rem * 2 >= units)
        q++;
    return q;
}

ttfx_scale ttfx_ScaleForPixelHeight(const ttfx_font *f, ttfx_i32 pixels)
{
    ttfx_i32 fheight = (ttfx_i32)ttSHORT(f->hhea + 4) - ttSHORT(f->hhea + 6);
    return ttfx__scale(pixels, fheight);
}

ttfx_scale ttfx_ScaleForMappingEmToPixels(const ttfx_font *f, ttfx_i32 pixels)
{
    return ttfx__scale(pixels, f->unitsPerEm);
}

/* v * s / 2^18 to nearest (half away from zero): font units to 26.6 */
ttfx_i32 ttfx_Scale26(ttfx_i32 v, ttfx_scale s)
{
    ttfx_u32 av = (ttfx_u32)ttfx__abs(v), r;
    if (av < 0x8000L) {
        ttfx_u32 plo = av * (ttfx_u16)s, phi = av * (ttfx_u32)((ttfx_u32)s >> 16);
        r = (phi >> 2) + ((((phi & 3) << 16) + plo + 0x20000UL) >> 18);
    } else {
        ttfx_u32 hi, lo;
        ttfx__mul64(av, (ttfx_u32)s, &hi, &lo);
        lo += 0x20000UL;
        if (lo < 0x20000UL)
            hi++;
        r = (hi << 14) | (lo >> 18);
    }
    return v < 0 ? -(ttfx_i32)r : (ttfx_i32)r;
}

/* floor(v * s / 2^(24 - frac)) exactly: font units to whole pixels
   (frac 0) or 26.6 (frac 6); up rounds up instead */
static ttfx_i32 ttfx__scaled(ttfx_i32 v, ttfx_scale s, int frac, int up)
{
    ttfx_u32 av = (ttfx_u32)ttfx__abs(v), hi, lo, q, mask, sh = 24 - frac;
    ttfx__mul64(av, (ttfx_u32)s, &hi, &lo);
    mask = ((ttfx_u32)1 << sh) - 1;
    q = (hi << (32 - sh)) | (lo >> sh);
    /* toward minus infinity for floor, plus infinity for ceiling */
    if ((lo & mask) && ((v < 0) != up))
        q++;
    return v < 0 ? -(ttfx_i32)q : (ttfx_i32)q;
}

ttfx_i32 ttfx_MulDivRound(ttfx_i32 v, ttfx_i32 num, ttfx_i32 den)
{
    ttfx_i32 rem, q;
    if (den <= 0)
        return 0;
    q = ttfx__muldiv(v, num, den, &rem);       /* floor, 0 <= rem < den */
    /* to nearest, a half away from zero */
    if (rem * 2 > den || (rem * 2 == den && q >= 0))
        q++;
    return q;
}

ttfx_i32 ttfx_ScaleRound(ttfx_i32 v, ttfx_scale s)
{
    ttfx_u32 av = (ttfx_u32)ttfx__abs(v), hi, lo, q;
    ttfx__mul64(av, (ttfx_u32)s, &hi, &lo);
    lo += 0x800000UL;
    if (lo < 0x800000UL)
        hi++;
    q = (hi << 8) | (lo >> 24);
    return v < 0 ? -(ttfx_i32)q : (ttfx_i32)q;
}

/* floor and ceiling of a 26.6 value in whole pixels */
static int ttfx__floor6(ttfx_i32 v)
{
    return (int)(v >= 0 ? v >> 6 : -((-v + 63) >> 6));
}

static int ttfx__ceil6(ttfx_i32 v)
{
    return (int)(v >= 0 ? (v + 63) >> 6 : -((-v) >> 6));
}

void ttfx_GetGlyphBitmapBox(const ttfx_font *f, ttfx_u16 glyph, ttfx_scale sx, ttfx_scale sy,
                            ttfx_i32 shift_x, ttfx_i32 shift_y, int *ix0, int *iy0, int *ix1, int *iy1)
{
    int x0 = 0, y0 = 0, x1, y1;
    if (!ttfx_GetGlyphBox(f, glyph, &x0, &y0, &x1, &y1)) {
        if (ix0) *ix0 = 0;
        if (iy0) *iy0 = 0;
        if (ix1) *ix1 = 0;
        if (iy1) *iy1 = 0;
    } else {
        /* exactly, from the unrounded values (stb_truetype's floor and
           ceiling of x * scale + shift; the outline's points, rounded to
           26.6, cannot reach a pixel centre outside it) */
        if (shift_x & 63 || shift_y & 63) {
            if (ix0) *ix0 = ttfx__floor6(ttfx__scaled(x0, sx, 6, 0) + shift_x);
            if (iy0) *iy0 = ttfx__floor6(ttfx__scaled(-(ttfx_i32)y1, sy, 6, 0) + shift_y);
            if (ix1) *ix1 = ttfx__ceil6(ttfx__scaled(x1, sx, 6, 1) + shift_x);
            if (iy1) *iy1 = ttfx__ceil6(ttfx__scaled(-(ttfx_i32)y0, sy, 6, 1) + shift_y);
        } else {
            if (ix0) *ix0 = (int)(ttfx__scaled(x0, sx, 0, 0) + (shift_x >> 6));
            if (iy0) *iy0 = (int)(ttfx__scaled(-(ttfx_i32)y1, sy, 0, 0) + (shift_y >> 6));
            if (ix1) *ix1 = (int)(ttfx__scaled(x1, sx, 0, 1) + (shift_x >> 6));
            if (iy1) *iy1 = (int)(ttfx__scaled(-(ttfx_i32)y0, sy, 0, 1) + (shift_y >> 6));
        }
    }
}

/* ---- flattening ---- */

#define TTFX_MAXPOINTS 8000             /* 64000 bytes of points */

typedef struct {
    ttfx_point TTFX_FAR *points;        /* 0 in the counting pass */
    unsigned n;
    int overflow;
} ttfx__flat;

static void ttfx__add_point(ttfx__flat *fl, ttfx_i32 x, ttfx_i32 y)
{
    if (fl->n >= TTFX_MAXPOINTS) {
        fl->overflow = 1;
        return;
    }
    if (fl->points) {
        fl->points[fl->n].x = x;
        fl->points[fl->n].y = y;
    }
    fl->n++;
}

/* stb_truetype's midpoint subdivision of a quadratic, in 16.16 pixels:
   split while the curve's midpoint is more than 0.35 pixel from the
   chord's. Compared squared, halved to stay in 32 bits: dx and dy under
   a pixel (2^16), (0.35 * 65536)^2 / 2 */
#define TTFX_FLAT2H 263066746UL

static void ttfx__tesselate(ttfx__flat *fl, ttfx_i32 x0, ttfx_i32 y0, ttfx_i32 x1, ttfx_i32 y1,
                            ttfx_i32 x2, ttfx_i32 y2, int n)
{
    ttfx_i32 mx = (x0 + 2 * x1 + x2) >> 2, my = (y0 + 2 * y1 + y2) >> 2;
    ttfx_i32 dx = ((x0 + x2) >> 1) - mx, dy = ((y0 + y2) >> 1) - my;
    ttfx_u32 adx = (ttfx_u32)ttfx__abs(dx), ady = (ttfx_u32)ttfx__abs(dy);
    if (n > 16)
        return;
    if (adx >= 0x10000UL || ady >= 0x10000UL || ((adx * adx) >> 1) + ((ady * ady) >> 1) > TTFX_FLAT2H) {
        ttfx__tesselate(fl, x0, y0, (x0 + x1) >> 1, (y0 + y1) >> 1, mx, my, n + 1);
        ttfx__tesselate(fl, mx, my, (x1 + x2) >> 1, (y1 + y2) >> 1, x2, y2, n + 1);
    } else
        ttfx__add_point(fl, (x2 + 512) >> 10, (y2 + 512) >> 10);
}

int ttfx_Flatten(const ttfx_font *f, const ttfx_vertex TTFX_FAR *v, int nv, ttfx_scale sx, ttfx_scale sy,
                 ttfx_i32 shift_x, ttfx_i32 shift_y, int off_x, int off_y,
                 ttfx_point TTFX_FAR **pts, int TTFX_FAR **counts, int *ncontours)
{
    ttfx__flat fl;
    int i, n = 0, pass;
    unsigned start = 0;
    ttfx_i32 ox = shift_x - (ttfx_i32)off_x * 64, oy = shift_y - (ttfx_i32)off_y * 64;
    ttfx_point TTFX_FAR *hp = 0;        /* the points grid-fitted (ttfhint.c) */
    *pts = 0;
    *counts = 0;
    *ncontours = 0;
    for (i = 0; i < nv; ++i)
        if (v[i].type == TTFX_vmove)
            ++n;
    if (!n)
        return 1;
    if (f->hint)
        hp = ttfx__HintPoints(f, v, nv, sx, sy, ox, oy);
    *counts = (int TTFX_FAR *)ttfx_alloc(f->alloc, (ttfx_u32)n * sizeof(int));
    if (!*counts) {
        if (hp)
            ttfx_free(f->alloc, hp);
        return 0;
    }
    fl.points = 0;
    for (pass = 0; pass < 2; ++pass) {
        ttfx_i32 x = 0, y = 0;          /* 26.6 */
        int c = -1;
        if (pass == 1) {
            fl.points = (ttfx_point TTFX_FAR *)ttfx_alloc(f->alloc, (ttfx_u32)fl.n * sizeof(ttfx_point) + 1);
            if (!fl.points)
                goto error;
        }
        fl.n = 0;
        fl.overflow = 0;
        for (i = 0; i < nv; ++i) {
            ttfx_i32 X, Y;
            if (hp) {
                X = hp[2 * i].x;
                Y = hp[2 * i].y;
            } else {
                X = ttfx_Scale26(v[i].x, sx) + ox;
                Y = -ttfx_Scale26(v[i].y, sy) + oy;
            }
            switch (v[i].type) {
            case TTFX_vmove:
                if (c >= 0)
                    (*counts)[c] = (int)(fl.n - start);
                ++c;
                start = fl.n;
                ttfx__add_point(&fl, X, Y);
                break;
            case TTFX_vline:
                ttfx__add_point(&fl, X, Y);
                break;
            case TTFX_vcurve: {
                ttfx_i32 CX, CY;
                if (hp) {
                    CX = hp[2 * i + 1].x;
                    CY = hp[2 * i + 1].y;
                } else {
                    CX = ttfx_Scale26(v[i].cx, sx) + ox;
                    CY = -ttfx_Scale26(v[i].cy, sy) + oy;
                }
                ttfx__tesselate(&fl, x << 10, y << 10, CX << 10, CY << 10, X << 10, Y << 10, 0);
                break;
            }
            }
            x = X;
            y = Y;
        }
        if (c >= 0)
            (*counts)[c] = (int)(fl.n - start);
        if (fl.overflow || c < 0)
            goto error;
    }
    if (hp)
        ttfx_free(f->alloc, hp);
    *pts = fl.points;
    *ncontours = n;
    return 1;
error:
    if (fl.points)
        ttfx_free(f->alloc, fl.points);
    if (hp)
        ttfx_free(f->alloc, hp);
    ttfx_free(f->alloc, *counts);
    *counts = 0;
    return 0;
}

/* ---- the scan converter ---- */

typedef struct {
    ttfx_i32 x0, y0, x1, y1;            /* 26.6, y0 < y1 */
    ttfx_i32 x, rem, q, r;              /* at the current centre row: x + rem / (y1 - y0) */
    ttfx_i16 dir;                       /* +1 down, -1 up */
} ttfx__edge;

#define TTFX_MAXEDGES 1800              /* 61200 bytes of edges */

/* insertion sort of edge indexes by y0 (stb_truetype sorts the edges by
   their top with a quicksort finished by an insertion sort; the order
   among equal tops does not matter here) */
static void ttfx__sort_by_top(ttfx_u16 TTFX_FAR *idx, unsigned n, const ttfx__edge TTFX_FAR *e)
{
    unsigned i, j;
    /* a shell sort's gaps, then insertion */
    static const unsigned gaps[] = { 701, 301, 132, 57, 23, 10, 4, 1 };
    unsigned g;
    for (g = 0; g < sizeof gaps / sizeof gaps[0]; g++) {
        unsigned gap = gaps[g];
        for (i = gap; i < n; i++) {
            ttfx_u16 t = idx[i];
            ttfx_i32 ty = e[t].y0;
            for (j = i; j >= gap && e[idx[j - gap]].y0 > ty; j -= gap)
                idx[j] = idx[j - gap];
            idx[j] = t;
        }
    }
}

/* floor division by d > 0 */
static ttfx_i32 ttfx__fdiv(ttfx_i32 n, ttfx_i32 d, ttfx_i32 *rem)
{
    ttfx_i32 q = n / d, r = n % d;
    if (r < 0) {
        q--;
        r += d;
    }
    *rem = r;
    return q;
}

/* set bits x0..x1 (inclusive) of a row */
static void ttfx__span(ttfx_u8 TTFX_FAR *row, int x0, int x1)
{
    int b0 = x0 >> 3, b1 = x1 >> 3;
    ttfx_u8 m0 = (ttfx_u8)(0xFF >> (x0 & 7)), m1 = (ttfx_u8)(0xFF << (7 - (x1 & 7)));
    if (b0 == b1) {
        row[b0] |= m0 & m1;
        return;
    }
    row[b0] |= m0;
    while (++b0 < b1)
        row[b0] = 0xFF;
    row[b1] |= m1;
}

static int ttfx__scan(const ttfx_font *f, ttfx_bitmap *bm, const ttfx_point TTFX_FAR *p,
                      const int TTFX_FAR *counts, int nc)
{
    ttfx__edge TTFX_FAR *e;
    ttfx_u16 TTFX_FAR *idx, TTFX_FAR *act;
    unsigned ne = 0, i, k, nact = 0, next = 0, m = 0;
    int c, row;
    for (c = 0; c < nc; c++)
        ne += counts[c];
    if (ne > TTFX_MAXEDGES)
        return 0;
    if (!ne)
        return 1;
    e = (ttfx__edge TTFX_FAR *)ttfx_alloc(f->alloc, (ttfx_u32)ne * sizeof(ttfx__edge));
    idx = (ttfx_u16 TTFX_FAR *)ttfx_alloc(f->alloc, (ttfx_u32)ne * 4);
    if (!e || !idx) {
        if (e) ttfx_free(f->alloc, e);
        if (idx) ttfx_free(f->alloc, idx);
        return 0;
    }
    act = idx + ne;
    ne = 0;
    for (c = 0; c < nc; c++) {
        const ttfx_point TTFX_FAR *q = p + m;
        unsigned cnt = counts[c], j = cnt - 1;
        m += cnt;
        for (k = 0; k < cnt; j = k++) {
            ttfx__edge TTFX_FAR *d = &e[ne];
            if (q[j].y == q[k].y)
                continue;               /* horizontal */
            if (q[j].y < q[k].y) {
                d->x0 = q[j].x; d->y0 = q[j].y; d->x1 = q[k].x; d->y1 = q[k].y; d->dir = 1;
            } else {
                d->x0 = q[k].x; d->y0 = q[k].y; d->x1 = q[j].x; d->y1 = q[j].y; d->dir = -1;
            }
            idx[ne] = (ttfx_u16)ne;
            ne++;
        }
    }
    ttfx__sort_by_top(idx, ne, e);

    for (row = 0; row < bm->h; row++) {
        ttfx_i32 ys = (ttfx_i32)row * 64 + 32;
        ttfx_u8 TTFX_FAR *line = bm->pixels + (ttfx_u32)row * bm->stride;
        int w, xs = 0;
        /* step the active edges to this row; drop those ended */
        for (i = k = 0; i < nact; i++) {
            ttfx__edge TTFX_FAR *d = &e[act[i]];
            if (d->y1 <= ys)
                continue;
            d->x += d->q;
            d->rem += d->r;
            if (d->rem >= d->y1 - d->y0) {
                d->rem -= d->y1 - d->y0;
                d->x++;
            }
            act[k++] = act[i];
        }
        nact = k;
        /* add the edges reaching this row's centres: x exactly there */
        while (next < ne && e[idx[next]].y0 <= ys) {
            ttfx__edge TTFX_FAR *d = &e[idx[next]];
            if (d->y1 > ys) {
                ttfx_i32 den = d->y1 - d->y0, num = d->x1 - d->x0, t = ys - d->y0, rem, r;
                if (t < 0x8000L && ttfx__abs(num) < 0x10000L)
                    d->x = d->x0 + ttfx__fdiv(t * num, den, &rem);
                else
                    d->x = d->x0 + ttfx__muldiv(t, num, den, &rem);
                if (ttfx__abs(num) < 0x1000000L)
                    d->q = ttfx__fdiv(num * 64, den, &r);
                else
                    d->q = ttfx__muldiv(num, 64, den, &r);
                d->rem = rem;
                d->r = r;
                act[nact++] = idx[next];
            }
            next++;
        }
        if (!nact)
            continue;
        /* in order of x (an insertion sort: the order changes little from
           row to row); a crossing with a remainder is a little right */
        for (i = 1; i < nact; i++) {
            ttfx_u16 t = act[i];
            ttfx_i32 tx = e[t].x;
            int tr = e[t].rem != 0;
            for (k = i; k > 0; k--) {
                ttfx__edge TTFX_FAR *d = &e[act[k - 1]];
                if (d->x < tx || (d->x == tx && (d->rem != 0) <= tr))
                    break;
                act[k] = act[k - 1];
            }
            act[k] = t;
        }
        /* non-zero winding: a span from where the winding leaves 0 to
           where it comes back; the pixels whose centres (64 i + 32) are
           inside it or on its ends */
        w = 0;
        for (i = 0; i < nact; i++) {
            ttfx__edge TTFX_FAR *d = &e[act[i]];
            if (!w) {
                /* the first centre at or right of the crossing (the
                   crossing's ceiling in 26.6 is x, plus 1 with a
                   remainder) */
                xs = ttfx__ceil6(d->x + (d->rem != 0) - 32);
            }
            w += d->dir;
            if (!w) {
                int xe = ttfx__floor6(d->x - 32);
                if (xs < 0)
                    xs = 0;
                if (xe >= bm->w)
                    xe = bm->w - 1;
                if (xs <= xe)
                    ttfx__span(line, xs, xe);
            }
        }
    }
    ttfx_free(f->alloc, idx);
    ttfx_free(f->alloc, e);
    return 1;
}

int ttfx_Rasterize(const ttfx_font *f, ttfx_bitmap *bm, const ttfx_vertex TTFX_FAR *v, int nv,
                   ttfx_scale sx, ttfx_scale sy, ttfx_i32 shift_x, ttfx_i32 shift_y,
                   int off_x, int off_y)
{
    ttfx_point TTFX_FAR *pts;
    int TTFX_FAR *counts;
    int nc, ok;
    if (!ttfx_Flatten(f, v, nv, sx, sy, shift_x, shift_y, off_x, off_y, &pts, &counts, &nc))
        return 0;
    if (!nc)
        return 1;
    ok = ttfx__scan(f, bm, pts, counts, nc);
    ttfx_free(f->alloc, pts);
    ttfx_free(f->alloc, counts);
    return ok;
}

int ttfx_RenderGlyph(const ttfx_font *f, ttfx_bitmap *bm, ttfx_u16 glyph, ttfx_scale sx, ttfx_scale sy,
                     ttfx_i32 shift_x, ttfx_i32 shift_y, int ix0, int iy0)
{
    ttfx_vertex TTFX_FAR *v;
    int nv = ttfx_GetGlyphShape(f, glyph, &v), ok;
    if (!nv)
        return 1;                       /* (empty, e.g. the space) */
    ok = ttfx_Rasterize(f, bm, v, nv, sx, sy, shift_x, shift_y, ix0, iy0);
    ttfx_FreeShape(f, v);
    return ok;
}

/* ---- names ---- */

int ttfx_GetFontNameString(const ttfx_font *f, void TTFX_FAR *buf, int size, int platformID,
                           int encodingID, int languageID, int nameID)
{
    ttfx_i32 nm = f->name, stringOffset;
    ttfx_u16 count, i;
    if (!nm)
        return -1;
    count = ttUSHORT(nm + 2);
    stringOffset = nm + ttUSHORT(nm + 4);
    for (i = 0; i < count; ++i) {
        ttfx_i32 loc = nm + 6 + 12L * i;
        if ((ttfx_u16)platformID == ttUSHORT(loc) && (ttfx_u16)encodingID == ttUSHORT(loc + 2) &&
            (ttfx_u16)languageID == ttUSHORT(loc + 4) && (ttfx_u16)nameID == ttUSHORT(loc + 6)) {
            int len = (int)ttUSHORT(loc + 8);
            if (buf && size > 0)
                ttfx_Read(f, stringOffset + ttUSHORT(loc + 10), buf, (unsigned)(len < size ? len : size));
            return len;
        }
    }
    return -1;
}
