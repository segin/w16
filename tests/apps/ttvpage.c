/*
 * ttvpage.c - TTVIEW's page (ttvpage.h): sample lines and a character
 * grid in a TrueType font, rasterised by gdi/ttf/ttfx.c into a 1-bit
 * page. Compiled for Windows into TTVIEW.EXE (OpenWatcom, 286, no
 * floating point) and on the host into tests/ttf/ttvhost.c's tool: the
 * same page from the same code, compared bit for bit.
 */
#include "ttvpage.h"

ttfx_u8 TTFX_FAR *ttv_arena;
unsigned ttv_arena_used, ttv_arena_peak;

/* ttfx's memory: blocks from the arena, each after a word holding its
   size; freeing the last block gives its room back (ttfx frees mostly in
   the reverse order), the rest goes when the next glyph starts */
void TTFX_FAR *ttfx_alloc(void *alloc, ttfx_u32 n)
{
    ttfx_u8 TTFX_FAR *p;
    (void)alloc;
    n = (n + 3) & ~1UL;                 /* the size word, word aligned */
    if (n > TTV_ARENA - ttv_arena_used)
        return 0;
    p = ttv_arena + ttv_arena_used;
    *(ttfx_u16 TTFX_FAR *)p = (ttfx_u16)n;
    ttv_arena_used += (unsigned)n;
    if (ttv_arena_used > ttv_arena_peak)
        ttv_arena_peak = ttv_arena_used;
    return p + 2;
}

void ttfx_free(void *alloc, void TTFX_FAR *q)
{
    ttfx_u8 TTFX_FAR *p = (ttfx_u8 TTFX_FAR *)q - 2;
    (void)alloc;
    if (p + *(ttfx_u16 TTFX_FAR *)p == ttv_arena + ttv_arena_used)
        ttv_arena_used -= *(ttfx_u16 TTFX_FAR *)p;
}

/* Windows' ANSI codes 80h-9Fh in Unicode (code page 1252; 0: none) */
static const ttfx_u16 cp1252[32] = {
    0x20AC, 0, 0x201A, 0x0192, 0x201E, 0x2026, 0x2020, 0x2021,
    0x02C6, 0x2030, 0x0160, 0x2039, 0x0152, 0, 0x017D, 0,
    0, 0x2018, 0x2019, 0x201C, 0x201D, 0x2022, 0x2013, 0x2014,
    0x02DC, 0x2122, 0x0161, 0x203A, 0x0153, 0, 0x017E, 0x0178
};

static ttfx_u16 glyph_of(const ttfx_font *f, int c)
{
    ttfx_i32 u = c;
    if (f->index_map_platform == TTFX_PLATFORM_ID_MICROSOFT &&
        f->index_map_encoding == TTFX_MS_EID_SYMBOL)
        u = 0xF000 + c;                 /* a symbol font's codes */
    else if (c >= 0x80 && c < 0xA0 && cp1252[c - 0x80])
        u = cp1252[c - 0x80];
    return ttfx_FindGlyphIndex(f, u);
}

/* one glyph with its origin at page pixel (px, py) (py: the baseline) */
static int draw_glyph(const ttfx_font *f, ttfx_u8 TTFX_FAR *bits, ttfx_u16 g, ttfx_scale sc,
                      int px, int py, int *failed)
{
    int ix0, iy0, ix1, iy1, top, h;
    ttfx_bitmap bm;
    ttfx_GetGlyphBitmapBox(f, g, sc, sc, 0, 0, &ix0, &iy0, &ix1, &iy1);
    if (ix1 <= ix0 || iy1 <= iy0)
        return 0;
    top = py + iy0;
    h = iy1 - iy0;
    if (top < 0) {                      /* cut to the page */
        h += top;
        iy0 -= top;
        top = 0;
    }
    if (top + h > TTV_H)
        h = TTV_H - top;
    if (h <= 0 || px + ix0 >= TTV_W || px + ix1 <= 0)
        return 0;
    bm.w = TTV_W;
    bm.h = h;
    bm.stride = TTV_STRIDE;
    bm.pixels = bits + (ttfx_u32)top * TTV_STRIDE;
    ttv_arena_used = 0;
    /* the outline's (x, y) to the page's (px + x, py + y) */
    if (!ttfx_RenderGlyph(f, &bm, g, sc, sc, 0, 0, -px, iy0)) {
        (*failed)++;
        return 0;
    }
    return 1;
}

/* a string from pen position x on baseline y, em pixels to the em; each
   advance rounded to whole pixels (as a font realized at a size has) */
static int draw_text(const ttfx_font *f, ttfx_u8 TTFX_FAR *bits, int x, int y, int em, const char *s,
                     int *failed)
{
    ttfx_scale sc = ttfx_ScaleForMappingEmToPixels(f, em);
    int n = 0;
    for (; *s && x < TTV_W; s++) {
        ttfx_u16 g = glyph_of(f, (unsigned char)*s), adv;
        int lsb;
        ttfx_GetGlyphHMetrics(f, g, &adv, &lsb);
        n += draw_glyph(f, bits, g, sc, x, y, failed);
        x += (int)ttfx_MulDivRound(adv, em, f->unitsPerEm);
    }
    return n;
}

static void hline(ttfx_u8 TTFX_FAR *bits, int x0, int x1, int y)
{
    for (; x0 <= x1; x0++)
        bits[(ttfx_u32)y * TTV_STRIDE + (x0 >> 3)] |= (ttfx_u8)(0x80 >> (x0 & 7));
}

static void vline(ttfx_u8 TTFX_FAR *bits, int x, int y0, int y1)
{
    for (; y0 <= y1; y0++)
        bits[(ttfx_u32)y0 * TTV_STRIDE + (x >> 3)] |= (ttfx_u8)(0x80 >> (x & 7));
}

void ttv_name(const ttfx_font *f, char *out, int size)
{
    static const int ids[] = { 4, 1 };
    ttfx_u8 buf[128];
    int k, i, n = 0, len;
    out[0] = 0;
    for (k = 0; k < 2 && !out[0]; k++) {
        len = ttfx_GetFontNameString(f, buf, sizeof buf, TTFX_PLATFORM_ID_MICROSOFT, 1, 0x409, ids[k]);
        if (len < 0)
            len = ttfx_GetFontNameString(f, buf, sizeof buf, TTFX_PLATFORM_ID_MICROSOFT, 0, 0x409, ids[k]);
        if (len > 0) {                  /* UTF-16BE */
            for (i = 0; i + 1 < len && i + 1 < (int)sizeof buf && n < size - 1; i += 2)
                out[n++] = buf[i] || buf[i + 1] >= 0x80 ? '?' : (char)buf[i + 1];
        } else {
            len = ttfx_GetFontNameString(f, buf, sizeof buf, TTFX_PLATFORM_ID_MAC, 0, 0, ids[k]);
            for (i = 0; i < len && i < (int)sizeof buf && n < size - 1; i++)
                out[n++] = (char)buf[i];
        }
        out[n] = 0;
    }
}

int ttv_page(const ttfx_font *f, ttfx_u8 TTFX_FAR *bits, int *failed)
{
    /* 7, 8, 9, 10, 11, 12, 14, 16, 18, 24 and 32 points at 96 dpi */
    static const int ems[] = { 9, 11, 12, 13, 15, 16, 19, 21, 24, 32, 43 };
    static const char sample[] = "The quick brown fox jumps over the lazy dog. 0123456789 &$?!";
    char line[80], name[64];
    int asc, dsc, gap, y = 0, i, n = 0, c;
    ttfx_u32 k;
    for (k = 0; k < (ttfx_u32)TTV_STRIDE * TTV_H; k++)
        bits[k] = 0;
    *failed = 0;
    ttfx_GetFontVMetrics(f, &asc, &dsc, &gap);
    ttv_name(f, name, sizeof name);
    for (i = 0; i < (int)(sizeof ems / sizeof ems[0]) + 1; i++) {
        int em = i ? ems[i - 1] : 16, j = 0;
        const char *s;
        if (!i)
            s = name;
        else {
            /* "NN: " and the sample */
            if (em >= 10)
                line[j++] = (char)('0' + em / 10);
            line[j++] = (char)('0' + em % 10);
            line[j++] = ':';
            line[j++] = ' ';
            for (s = sample; *s; s++)
                line[j++] = *s;
            line[j] = 0;
            s = line;
        }
        y += (int)ttfx_MulDivRound(asc, em, f->unitsPerEm);
        n += draw_text(f, bits, 2, y, em, s, failed);
        y += (int)ttfx_MulDivRound((ttfx_i32)gap - dsc, em, f->unitsPerEm);
    }
    /* the character grid: 32 x 7 cells of 20 x 22, 16 pixels to the em */
    y += 2;
    for (c = 0x20; c <= 0xFF; c++) {
        int col = (c - 0x20) % 32, row = (c - 0x20) / 32, cx = col * 20, cy = y + row * 22;
        ttfx_u16 g = glyph_of(f, c), adv;
        int lsb, w;
        if (cy + 22 > TTV_H)
            break;
        ttfx_GetGlyphHMetrics(f, g, &adv, &lsb);
        w = (int)ttfx_MulDivRound(adv, 16, f->unitsPerEm);
        n += draw_glyph(f, bits, g, ttfx_ScaleForMappingEmToPixels(f, 16), cx + (20 - w) / 2,
                        cy + 1 + (int)ttfx_MulDivRound(asc, 16, f->unitsPerEm), failed);
    }
    for (i = 0; i <= 7 && y + i * 22 < TTV_H; i++)
        hline(bits, 0, 32 * 20 - 1, y + i * 22 < TTV_H - 1 ? y + i * 22 : TTV_H - 1);
    for (i = 0; i <= 32; i++)
        vline(bits, i < 32 ? i * 20 : TTV_W - 1, y, y + 7 * 22 < TTV_H ? y + 7 * 22 : TTV_H - 1);
    return n;
}
