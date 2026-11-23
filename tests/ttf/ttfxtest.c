/*
 * ttfxtest.c - the fixed-point TrueType code (gdi/ttf/ttfx.c) against the
 * floating-point stb_truetype it was adapted from (ref.c), on the host.
 *
 *   usage: ttfxtest [-v] FONT.TTF ...
 *
 * Exact (any difference fails): the cmap for every code 0-FFFFh, every
 * glyph's horizontal metrics, box and outline (font units), the kerning
 * table and pairs, the name strings; at each size, the rounded advance
 * widths (to the nearest pixel), and each glyph's bitmap against a brute
 * force evaluation, pixel by pixel in 64-bit integers, of the rules the
 * scan converter implements (a pixel centre inside the non-zero winding
 * of ttfx's own flattened outline, or on it).
 *
 * Within tolerance (counted, and failing past a bound): the scale factor
 * against float's, the bitmap boxes, and the bitmaps against the same
 * centre-sampling rule evaluated in floating point on stb_truetype's own
 * flattened outline: a pixel may differ only where its centre lies
 * within TOL_PX of that outline (26.6 rounding of the points, and curves
 * split differently near the flatness threshold). Also reported, not
 * checked: how far the monochrome bitmaps are from stb_truetype's
 * anti-aliased ones thresholded at half coverage (a different rule).
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <time.h>
#include "../../gdi/ttf/ttfx.h"
#include "ref.h"

#define TOL_PX 0.15
#define TOL_SPLIT 0.36                  /* the flatness, 0.35, and rounding */

static long long floordiv(long long n, long long d)
{
    long long q = n / d;
    return (n % d && (n < 0) != (d < 0)) ? q - 1 : q;
}

static unsigned char *data;
static long datalen;
static int verbose, fails;

void TTFX_FAR *ttfx_alloc(void *alloc, ttfx_u32 n)
{
    (void)alloc;
    return n > 65000 ? 0 : malloc(n ? n : 1);
}

void ttfx_free(void *alloc, void TTFX_FAR *p)
{
    (void)alloc;
    free(p);
}

static unsigned readfn(void *user, ttfx_u32 off, void *dst, unsigned len)
{
    (void)user;
    if ((long)off >= datalen)
        return 0;
    if ((long)(off + len) > datalen)
        len = (unsigned)(datalen - (long)off);
    memcpy(dst, data + off, len);
    return len;
}

static void fail(const char *what, int a, int b, int c)
{
    if (fails < 40)
        printf("FAIL %s (%d %d %d)\n", what, a, b, c);
    fails++;
}

/* the exact rule on ttfx's points (26.6, bitmap coordinates) */
static int exact_on(const ttfx_point *p, const int *cnt, int nc, long long cx, long long cy)
{
    int c, k, m = 0, w = 0, at = 0;
    for (c = 0; c < nc; c++) {
        const ttfx_point *q = p + m;
        int n = cnt[c], j = n - 1;
        m += n;
        for (k = 0; k < n; j = k++) {
            long long x0, y0, x1, y1, v;
            int dir;
            if (q[j].y == q[k].y)
                continue;
            if (q[j].y < q[k].y) {
                x0 = q[j].x; y0 = q[j].y; x1 = q[k].x; y1 = q[k].y; dir = 1;
            } else {
                x0 = q[k].x; y0 = q[k].y; x1 = q[j].x; y1 = q[j].y; dir = -1;
            }
            if (!(y0 <= cy && cy < y1))
                continue;
            v = (x0 - cx) * (y1 - y0) + (cy - y0) * (x1 - x0);
            if (v < 0)
                w += dir;
            else if (v == 0)
                at = 1;
        }
    }
    return w != 0 || at;
}

/* the same rule in floating point on stb_truetype's points (pixels) */
static int float_on(const float *px, const float *py, const int *cnt, int nc, double cx, double cy, double *dist)
{
    int c, k, m = 0, w = 0;
    double best = 1e9;
    for (c = 0; c < nc; c++) {
        int n = cnt[c], j = n - 1;
        for (k = 0; k < n; j = k++) {
            double x0 = px[m + j], y0 = py[m + j], x1 = px[m + k], y1 = py[m + k];
            double dx = x1 - x0, dy = y1 - y0, t, d, ex, ey;
            double l2 = dx * dx + dy * dy;
            t = l2 > 0 ? ((cx - x0) * dx + (cy - y0) * dy) / l2 : 0;
            if (t < 0) t = 0;
            if (t > 1) t = 1;
            ex = x0 + t * dx - cx;
            ey = y0 + t * dy - cy;
            d = sqrt(ex * ex + ey * ey);
            if (d < best)
                best = d;
            if (y0 == y1)
                continue;
            if (y0 < y1 ? (y0 <= cy && cy < y1) : (y1 <= cy && cy < y0)) {
                double x = x0 + (cy - y0) * dx / dy;
                if (x <= cx)
                    w += y0 < y1 ? 1 : -1;
            }
        }
        m += n;
    }
    *dist = best;
    return w != 0;
}

struct sums {
    long glyphs, px, exact_bad, float_bad, float_tol, aa_bad, box_bad, box_out, fbox_bad, adv_bad, fadv_bad, flat_bad;
    double float_maxdist, scale_maxrel, pt_maxdev;
};

static void size_test(ttfx_font *f, int px, const int *glyphs, int ng, struct sums *S)
{
    ttfx_scale s = ttfx_ScaleForPixelHeight(f, px);
    float fs = ref_scale_ph((float)px);
    int gi, a, dsc, fh;
    ttfx_GetFontVMetrics(f, &a, &dsc, 0);
    fh = a - dsc;
    double rel = fabs((double)s / 16777216.0 - fs) / fs;
    if (rel > S->scale_maxrel)
        S->scale_maxrel = rel;
    for (gi = 0; gi < ng; gi++) {
        int g = glyphs[gi], ix0, iy0, ix1, iy1, rx0, ry0, rx1, ry1, w, h, x, y, nv, nc, rnc;
        ttfx_u16 adv;
        int radv, rlsb, lsb, fa, ra, same_split;
        ttfx_bitmap bm;
        ttfx_vertex *v;
        ttfx_point *pts;
        int *cnt, *rcnt;
        float *rx, *ry;
        unsigned char *aa;
        int aw, ah, axo, ayo;

        ttfx_GetGlyphHMetrics(f, (ttfx_u16)g, &adv, &lsb);
        ref_hmetrics(g, &radv, &rlsb);
        /* the width to the nearest pixel: exactly (a half up) */
        {
            long long num = (long long)adv * px * 2 + fh;
            int exact = (int)(num / (2LL * fh));
            fa = (int)ttfx_MulDivRound(adv, px, fh);
            ra = (int)floor(radv * fs + 0.5f);
            if (fa != exact) {
                S->adv_bad++;
                if (verbose)
                    printf("  adv %d px %d: %d, exactly %d\n", g, px, fa, exact);
            }
            if (ra != exact)
                S->fadv_bad++;
        }
        /* the box: exactly floor and ceiling of v * px / fh; ttfx's may be
           a pixel bigger (its 8.24 scale is rounded), never smaller */
        ttfx_GetGlyphBitmapBox(f, (ttfx_u16)g, s, s, 0, 0, &ix0, &iy0, &ix1, &iy1);
        ref_bitmap_box(g, fs, &rx0, &ry0, &rx1, &ry1);
        {
            int b[4], e0, e1, e2, e3;
            if (ttfx_GetGlyphBox(f, (ttfx_u16)g, &b[0], &b[1], &b[2], &b[3])) {
                e0 = (int)floordiv((long long)b[0] * px, fh);
                e1 = (int)floordiv(-(long long)b[3] * px, fh);
                e2 = (int)-floordiv(-(long long)b[2] * px, fh);
                e3 = (int)-floordiv((long long)b[1] * px, fh);
            } else
                e0 = e1 = e2 = e3 = 0;
            if (ix0 != e0 || iy0 != e1 || ix1 != e2 || iy1 != e3)
                S->box_bad++;
            if (ix0 > e0 || iy0 > e1 || ix1 < e2 || iy1 < e3 || ix0 < e0 - 1 || iy0 < e1 - 1 ||
                ix1 > e2 + 1 || iy1 > e3 + 1)
                S->box_out++;
            if (rx0 != e0 || ry0 != e1 || rx1 != e2 || ry1 != e3)
                S->fbox_bad++;
        }
        w = ix1 - ix0;
        h = iy1 - iy0;
        S->glyphs++;
        if (w <= 0 || h <= 0)
            continue;
        bm.w = w;
        bm.h = h;
        bm.stride = (unsigned)(w + 7) / 8;
        bm.pixels = calloc(bm.stride, h);
        if (!ttfx_RenderGlyph(f, &bm, (ttfx_u16)g, s, s, 0, 0, ix0, iy0)) {
            fail("render", g, px, 0);
            free(bm.pixels);
            continue;
        }
        nv = ttfx_GetGlyphShape(f, (ttfx_u16)g, &v);
        if (!ttfx_Flatten(f, v, nv, s, s, 0, 0, ix0, iy0, &pts, &cnt, &nc))
            fail("flatten", g, px, 0);
        ttfx_FreeShape(f, v);
        if (!ref_flatten(g, fs, ix0, iy0, &rx, &ry, &rcnt, &rnc))
            rnc = 0;
        /* the flattened outlines: same contours and points? */
        same_split = 0;
        if (rnc != nc)
            S->flat_bad++;
        else {
            int c, m = 0, same = 1;
            for (c = 0; c < nc; c++) {
                if (cnt[c] != rcnt[c])
                    same = 0;
                m += cnt[c];
            }
            /* the same counts, and every point within rounding of
               float's (equal counts can also come from one curve split
               more and another less) */
            if (same) {
                double md = 0;
                for (c = 0; c < m; c++) {
                    double d = fabs(pts[c].x / 64.0 - rx[c]) + fabs(pts[c].y / 64.0 - ry[c]);
                    if (d > md)
                        md = d;
                }
                if (md > 0.05)
                    same = 0;
                else if (md > S->pt_maxdev)
                    S->pt_maxdev = md;
            }
            same_split = same;
            if (!same)
                S->flat_bad++;
        }
        aa = ref_aa(g, fs, &aw, &ah, &axo, &ayo);
        for (y = 0; y < h; y++)
            for (x = 0; x < w; x++) {
                int on = (bm.pixels[y * bm.stride + (x >> 3)] >> (7 - (x & 7))) & 1;
                int ex = exact_on(pts, cnt, nc, (long long)x * 64 + 32, (long long)y * 64 + 32);
                double d = 0;
                int fo = rnc ? float_on(rx, ry, rcnt, rnc, x + 0.5, y + 0.5, &d) : 0;
                int ax = x + ix0 - axo, ay = y + iy0 - ayo, ao = 0;
                S->px++;
                if (on != ex) {
                    S->exact_bad++;
                    if (S->exact_bad <= 10)
                        printf("FAIL exact: glyph %d px %d at %d,%d: %d, the rule says %d\n", g, px, x, y, on, ex);
                }
                if (on != fo) {
                    S->float_bad++;
                    if (d > S->float_maxdist)
                        S->float_maxdist = d;
                    if (d > (same_split ? TOL_PX : TOL_SPLIT)) {
                        S->float_tol++;
                        if (verbose)
                            printf("  float: glyph %d px %d at %d,%d: %d vs %d, %.4f px from the outline\n",
                                   g, px, x, y, on, fo, d);
                    }
                }
                if (aa && ax >= 0 && ay >= 0 && ax < aw && ay < ah)
                    ao = aa[ay * aw + ax] >= 128;
                if (ao != on)
                    S->aa_bad++;
            }
        if (aa)
            ref_free_aa(aa);
        free(pts);
        free(cnt);
        if (rnc) {
            free(rx);
            free(ry);
            free(rcnt);
        }
        free(bm.pixels);
    }
}

/* the autohinter (ttfhint.c), at 6-72 pixels to the em: each hinted
   bitmap exactly the scan-conversion rule on the hinted outline (ttfx's
   own flattening of it), every hinted point inside the glyph's bitmap box
   (hinting moves points within it only); and the time, hinted against
   unhinted */
static void hint_test(ttfx_font *f, const int *glyphs, int ng)
{
    static const int ems[] = { 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16, 17, 18, 19, 20, 21, 22, 24, 28, 32,
                               36, 48, 72 };
    long px = 0, bad = 0, outside = 0, changed = 0, n = 0, dif = 0;
    int i, gi, x, y, round;
    clock_t t[2] = { 0, 0 };
    for (i = 0; i < (int)(sizeof ems / sizeof ems[0]); i++) {
        ttfx_scale s = ttfx_ScaleForMappingEmToPixels(f, ems[i]);
        for (gi = 0; gi < ng; gi++) {
            int g = glyphs[gi], ix0, iy0, ix1, iy1, w, h, nv, nc, k, m;
            ttfx_bitmap bm, ub;
            ttfx_vertex *v;
            ttfx_point *pts;
            int *cnt;
            ttfx_GetGlyphBitmapBox(f, (ttfx_u16)g, s, s, 0, 0, &ix0, &iy0, &ix1, &iy1);
            w = ix1 - ix0;
            h = iy1 - iy0;
            if (w <= 0 || h <= 0)
                continue;
            bm.w = ub.w = w;
            bm.h = ub.h = h;
            bm.stride = ub.stride = (unsigned)(w + 7) / 8;
            bm.pixels = calloc(bm.stride, h);
            ub.pixels = calloc(bm.stride, h);
            for (round = 0; round < 2; round++) {
                clock_t c0 = clock();
                ttfx_SetHinting(f, round ? TTFX_HINT_DEFAULT : 0);
                if (!ttfx_RenderGlyph(f, round ? &bm : &ub, (ttfx_u16)g, s, s, 0, 0, ix0, iy0))
                    fail("hinted render", g, ems[i], round);
                t[round] += clock() - c0;
            }
            n++;
            if (memcmp(bm.pixels, ub.pixels, bm.stride * h))
                changed++;
            for (k = 0; k < (int)bm.stride * h; k++)
                dif += __builtin_popcount(bm.pixels[k] ^ ub.pixels[k]);
            nv = ttfx_GetGlyphShape(f, (ttfx_u16)g, &v);
            if (!ttfx_Flatten(f, v, nv, s, s, 0, 0, ix0, iy0, &pts, &cnt, &nc))
                fail("hinted flatten", g, ems[i], 0);
            ttfx_FreeShape(f, v);
            for (k = m = 0; k < nc; k++)
                m += cnt[k];
            for (k = 0; k < m; k++)
                if (pts[k].x < 0 || pts[k].y < 0 || pts[k].x > w * 64 || pts[k].y > h * 64) {
                    outside++;
                    if (verbose)
                        printf("  hinted point outside: glyph %d em %d: %d,%d in %dx%d\n", g, ems[i],
                               (int)pts[k].x, (int)pts[k].y, w, h);
                }
            for (y = 0; y < h; y++)
                for (x = 0; x < w; x++) {
                    int on = (bm.pixels[y * bm.stride + (x >> 3)] >> (7 - (x & 7))) & 1;
                    px++;
                    if (on != exact_on(pts, cnt, nc, (long long)x * 64 + 32, (long long)y * 64 + 32))
                        bad++;
                }
            free(pts);
            free(cnt);
            free(bm.pixels);
            free(ub.pixels);
        }
    }
    ttfx_SetHinting(f, 0);
    printf("  hinted, %d sizes 6-72 px to the em x %d characters: %ld of %ld glyphs differ from unhinted (%ld pixels);\n"
           "    against the exact rule on the hinted outline: %ld of %ld pixels different; points outside the box: %ld;\n"
           "    time hinted %.0f ms, unhinted %.0f ms\n",
           (int)(sizeof ems / sizeof ems[0]), ng, changed, n, dif, bad, px, outside,
           t[1] * 1000.0 / CLOCKS_PER_SEC, t[0] * 1000.0 / CLOCKS_PER_SEC);
    if (bad)
        fail("hinted bitmaps against the exact rule", (int)bad, 0, 0);
    if (outside)
        fail("hinted points outside the box", (int)outside, 0, 0);
    if (!changed)
        fail("hinting changed nothing", 0, 0, 0);
}

static int test_font(const char *path)
{
    FILE *fp = fopen(path, "rb");
    ttfx_font f;
    static unsigned char cache[TTFX_CACHE_BYTES(4)];
    int g, cp, n, i, k, ng = 0, *glyphs;
    long checked = 0;
    struct sums S;
    static const int sizes[] = { 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16, 17, 18, 19, 20, 21, 22, 24, 26,
                                 28, 30, 32, 36, 40, 48, 56, 64, 72, 96, 128, 200, 300 };
    int f0 = fails;
    if (!fp) {
        printf("FAIL %s: cannot open\n", path);
        return 1;
    }
    fseek(fp, 0, SEEK_END);
    datalen = ftell(fp);
    fseek(fp, 0, SEEK_SET);
    data = malloc(datalen);
    if (fread(data, 1, datalen, fp) != (size_t)datalen) {
        fclose(fp);
        return 1;
    }
    fclose(fp);
    memset(&S, 0, sizeof S);
    if (!ttfx_InitFont(&f, readfn, 0, 0, cache, sizeof cache, 0) || !ref_init(data)) {
        printf("FAIL %s: not loaded\n", path);
        return 1;
    }
    if (f.hint != TTFX_HINT_DEFAULT)
        fail("hinting not on by default", f.hint, 0, 0);
    ttfx_SetHinting(&f, 0);             /* the comparisons with stb_truetype: unhinted */
    printf("%s: %ld bytes, %u glyphs, %u units per em, cmap %u/%u\n", path, datalen, f.numGlyphs,
           f.unitsPerEm, f.index_map_platform, f.index_map_encoding);

    /* the cmap */
    for (cp = 0; cp <= 0x10FFFF; cp = cp < 0xFFFF ? cp + 1 : cp + 97) {
        if (ttfx_FindGlyphIndex(&f, cp) != (ttfx_u16)ref_glyph(cp))
            fail("cmap", cp, ttfx_FindGlyphIndex(&f, cp), ref_glyph(cp));
        checked++;
    }
    /* metrics, boxes and outlines of every glyph */
    {
        int a, d, l, ra, rd, rl;
        ttfx_GetFontVMetrics(&f, &a, &d, &l);
        ref_vmetrics(&ra, &rd, &rl);
        if (a != ra || d != rd || l != rl)
            fail("vmetrics", a, d, l);
    }
    for (g = 0; g < f.numGlyphs; g++) {
        ttfx_u16 adv;
        int lsb, radv, rlsb, b[4], rb[4], hb, rhb;
        ttfx_vertex *v;
        ref_vertex *rv;
        ttfx_GetGlyphHMetrics(&f, (ttfx_u16)g, &adv, &lsb);
        ref_hmetrics(g, &radv, &rlsb);
        if ((ttfx_i16)adv != radv || lsb != rlsb)
            fail("hmetrics", g, adv, radv);
        hb = ttfx_GetGlyphBox(&f, (ttfx_u16)g, &b[0], &b[1], &b[2], &b[3]);
        rhb = ref_box(g, &rb[0], &rb[1], &rb[2], &rb[3]);
        if (hb != rhb || (hb && memcmp(b, rb, sizeof b)))
            fail("box", g, hb, rhb);
        n = ttfx_GetGlyphShape(&f, (ttfx_u16)g, &v);
        k = ref_shape(g, &rv);
        if (n != k)
            fail("shape count", g, n, k);
        else
            for (i = 0; i < n; i++)
                if (v[i].type != rv[i].type || v[i].x != rv[i].x || v[i].y != rv[i].y ||
                    (v[i].type == TTFX_vcurve && (v[i].cx != rv[i].cx || v[i].cy != rv[i].cy))) {
                    fail("shape vertex", g, i, v[i].type);
                    break;
                }
        ttfx_FreeShape(&f, v);
        free(rv);
    }
    /* kerning */
    n = ttfx_GetKerningTableLength(&f);
    if (n != ref_kern_len())
        fail("kern length", n, ref_kern_len(), 0);
    for (i = 0; i < n; i++) {
        ttfx_u16 g1, g2;
        int adv;
        ttfx_GetKerningEntry(&f, i, &g1, &g2, &adv);
        if (ttfx_GetGlyphKernAdvance(&f, g1, g2) != adv || ref_kern(g1, g2) != adv)
            fail("kern pair", g1, g2, adv);
    }
    for (i = 0; i < 20000; i++) {
        int g1 = rand() % f.numGlyphs, g2 = rand() % f.numGlyphs;
        if (ttfx_GetGlyphKernAdvance(&f, (ttfx_u16)g1, (ttfx_u16)g2) != ref_kern(g1, g2))
            fail("kern", g1, g2, 0);
    }
    /* names */
    {
        static const int ids[][3] = { { 3, 1, 0x409 }, { 1, 0, 0 }, { 3, 0, 0x409 } };
        char a[512], b[512];
        int id, t;
        for (t = 0; t < 3; t++)
            for (id = 0; id < 26; id++) {
                int la = ttfx_GetFontNameString(&f, a, sizeof a, ids[t][0], ids[t][1], ids[t][2], id);
                int lb = ref_name(b, sizeof b, ids[t][0], ids[t][1], ids[t][2], id);
                if (la != lb || (la > 0 && memcmp(a, b, la < 512 ? la : 512)))
                    fail("name", ids[t][0], id, la);
            }
    }
    printf("  exact: cmap (%ld codes), metrics, boxes, outlines (%u glyphs), kerning (%d pairs), names: %s\n",
           checked, f.numGlyphs, n, fails == f0 ? "same" : "DIFFERENT");

    /* bitmaps: the characters 20h-7Eh and A0h-FFh (Windows' ANSI set,
       Latin-1 there), at every size listed */
    glyphs = malloc(sizeof(int) * 200);
    for (cp = 0x20; cp <= 0xFF; cp++)
        if (cp < 0x7F || cp >= 0xA0)
            glyphs[ng++] = ttfx_FindGlyphIndex(&f, cp);
    for (i = 0; i < (int)(sizeof sizes / sizeof sizes[0]); i++)
        size_test(&f, sizes[i], glyphs, ng, &S);
    hint_test(&f, glyphs, ng);
    free(glyphs);
    printf("  sizes 6-300 px (%d sizes x %d characters, %ld pixels):\n", (int)(sizeof sizes / sizeof sizes[0]),
           ng, S.px);
    printf("    scale: largest relative difference from float %.2e\n", S.scale_maxrel);
    printf("    advance widths to the nearest pixel: %ld of %ld different from exact (float: %ld)\n",
           S.adv_bad, S.glyphs, S.fadv_bad);
    printf("    bitmap boxes: %ld of %ld a pixel bigger than exact, %ld wrong (float: %ld different)\n",
           S.box_bad, S.glyphs, S.box_out, S.fbox_bad);
    printf("    flattened outlines: %ld glyphs split differently; points within %.4f px of float's\n",
           S.flat_bad, S.pt_maxdev);
    printf("    bitmaps against the exact rule: %ld pixels different\n", S.exact_bad);
    printf("    bitmaps against the rule on float's outline: %ld pixels different (%.4f%%), the farthest %.4f px from the outline, %ld past %.2f px (%.2f where split differently)\n",
           S.float_bad, 100.0 * S.float_bad / (S.px ? S.px : 1), S.float_maxdist, S.float_tol, TOL_PX, TOL_SPLIT);
    printf("    (not checked) against float's anti-aliased bitmaps at half coverage: %ld pixels different (%.2f%%)\n",
           S.aa_bad, 100.0 * S.aa_bad / (S.px ? S.px : 1));
    if (S.exact_bad)
        fail("bitmaps against the exact rule", (int)S.exact_bad, 0, 0);
    if (S.float_tol)
        fail("bitmaps against float's outline", (int)S.float_tol, 0, 0);
    if (S.adv_bad)
        fail("advance widths", (int)S.adv_bad, 0, 0);
    if (S.box_out)
        fail("bitmap boxes", (int)S.box_out, 0, 0);
    free(data);
    return fails != f0;
}

int main(int argc, char **argv)
{
    int i, bad = 0;
    for (i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "-v")) {
            verbose = 1;
            continue;
        }
        bad |= test_font(argv[i]);
    }
    printf("ttfxtest: %s\n", bad || fails ? "FAILED" : "ok");
    return bad || fails;
}
