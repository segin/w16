/*
 * ref.c - the floating-point reference for tests/ttf/ttfxtest.c: the
 * unchanged stb_truetype (gdi/ttf/stb_truetype.h), static, in a unit of
 * its own, behind small wrappers (its names would clash with nothing,
 * but its float API is kept out of the fixed-point test's sight).
 */
#define STB_TRUETYPE_IMPLEMENTATION
#define STBTT_STATIC
#include "../../gdi/ttf/stb_truetype.h"
#include "ref.h"

static stbtt_fontinfo font;

int ref_init(const unsigned char *data)
{
    return stbtt_InitFont(&font, data, stbtt_GetFontOffsetForIndex(data, 0));
}

int ref_glyph(int cp)
{
    return stbtt_FindGlyphIndex(&font, cp);
}

int ref_shape(int g, ref_vertex **out)
{
    stbtt_vertex *v;
    int n = stbtt_GetGlyphShape(&font, g, &v), i;
    *out = n > 0 ? malloc(sizeof(ref_vertex) * n) : 0;
    for (i = 0; i < n; i++) {
        (*out)[i].type = v[i].type;
        (*out)[i].x = v[i].x;
        (*out)[i].y = v[i].y;
        (*out)[i].cx = v[i].cx;
        (*out)[i].cy = v[i].cy;
    }
    if (n > 0)
        stbtt_FreeShape(&font, v);
    return n;
}

void ref_hmetrics(int g, int *adv, int *lsb)
{
    stbtt_GetGlyphHMetrics(&font, g, adv, lsb);
}

int ref_box(int g, int *x0, int *y0, int *x1, int *y1)
{
    return stbtt_GetGlyphBox(&font, g, x0, y0, x1, y1);
}

int ref_kern(int g1, int g2)
{
    return stbtt_GetGlyphKernAdvance(&font, g1, g2);
}

int ref_kern_len(void)
{
    return stbtt_GetKerningTableLength(&font);
}

void ref_vmetrics(int *a, int *d, int *l)
{
    stbtt_GetFontVMetrics(&font, a, d, l);
}

float ref_scale_ph(float px)
{
    return stbtt_ScaleForPixelHeight(&font, px);
}

float ref_scale_em(float px)
{
    return stbtt_ScaleForMappingEmToPixels(&font, px);
}

void ref_bitmap_box(int g, float s, int *x0, int *y0, int *x1, int *y1)
{
    stbtt_GetGlyphBitmapBox(&font, g, s, s, x0, y0, x1, y1);
}

unsigned char *ref_aa(int g, float s, int *w, int *h, int *xo, int *yo)
{
    return stbtt_GetGlyphBitmap(&font, s, s, g, w, h, xo, yo);
}

void ref_free_aa(unsigned char *p)
{
    stbtt_FreeBitmap(p, 0);
}

/* the outline flattened as stb_truetype's rasteriser flattens it (in font
   units, flatness 0.35 / scale), mapped to bitmap pixels as it maps
   them: x * s - xo, -y * s - yo */
int ref_flatten(int g, float s, int xo, int yo, float **px, float **py, int **counts, int *nc)
{
    stbtt_vertex *v;
    int n = stbtt_GetGlyphShape(&font, g, &v), np = 0, i, *wl = 0, wc = 0;
    stbtt__point *p;
    *nc = 0;
    if (n <= 0)
        return 0;
    p = stbtt_FlattenCurves(v, n, 0.35f / s, &wl, &wc, 0);
    stbtt_FreeShape(&font, v);
    if (!p)
        return 0;
    for (i = 0; i < wc; i++)
        np += wl[i];
    *px = malloc(sizeof(float) * (np + 1));
    *py = malloc(sizeof(float) * (np + 1));
    *counts = malloc(sizeof(int) * (wc + 1));
    for (i = 0; i < np; i++) {
        (*px)[i] = p[i].x * s - xo;
        (*py)[i] = -p[i].y * s - yo;
    }
    for (i = 0; i < wc; i++)
        (*counts)[i] = wl[i];
    *nc = wc;
    STBTT_free(p, 0);
    STBTT_free(wl, 0);
    return np;
}

int ref_name(char *buf, int size, int plat, int enc, int lang, int id)
{
    int len, i;
    const char *s = stbtt_GetFontNameString(&font, &len, plat, enc, lang, id);
    if (!s)
        return -1;
    for (i = 0; i < len && i < size; i++)
        buf[i] = s[i];
    return len;
}
