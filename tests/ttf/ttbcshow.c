/*
 * ttbcshow.c - a glyph or a line of text in a TrueType font, rasterised
 * by gdi/ttf/ttfx.c at one size and printed as text (# a pixel), with the
 * font's own instructions (gdi/ttf/ttfbc.c), the autohinter or neither:
 * for looking at the interpreter's work on the host.
 *
 *   usage: ttbcshow [-b|-a|-u] [-g] FONT.TTF PPEM TEXT
 * Built with -DBC_TRACE (and gdi/ttf/ttfbc.c so too), BCTRACE=1 in the
 * environment prints each instruction of the glyph programs.
 *     -b  the font's instructions (default), -a the autohinter, -u neither
 *     -g  TEXT is glyph indexes (comma-separated), not characters
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "../../gdi/ttf/ttfx.h"
#include "../../gdi/ttf/ttfbc.h"

static unsigned char *data;
static long datalen;

#ifdef BC_TRACE
/* (built with -DBC_TRACE, BCTRACE set) each instruction the glyph
   programs run: program and offset, opcode, the stack's top, the state */
void bc_trace(ttfx__bc *bc, int prog, unsigned ip, int op)
{
    int i;
    printf("%d:%04u %02X  [", prog, ip, op);
    for (i = bc->sp > 14 ? bc->sp - 14 : 0; i < (int)bc->sp; i++)
        printf(" %ld", (long)bc->stack[i]);
    printf(" ]  pv %d,%d fv %d,%d rp %u %u %u zp %u%u%u rs %d\n", bc->gs.pv[0], bc->gs.pv[1], bc->gs.fv[0],
           bc->gs.fv[1], bc->gs.rp[0], bc->gs.rp[1], bc->gs.rp[2], bc->gs.zp[0], bc->gs.zp[1], bc->gs.zp[2],
           bc->gs.round_state);
}
#endif

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

int main(int argc, char **argv)
{
    static unsigned char cache[TTFX_CACHE_BYTES(8)];
    static unsigned char pix[200 * 1024];
    ttfx_font f;
    FILE *fp;
    int mode = 'b', glyphs = 0, ppem, x = 4, n = 0, gl[256], i, w = 0, asc, dsc, gap;
    ttfx_scale sc;
    void *mem;
    ttfx_bitmap bm;
    extern int ttfx__bcTrace;
    while (argc > 1 && argv[1][0] == '-' && argv[1][1]) {
        if (argv[1][1] == 'g')
            glyphs = 1;
        else
            mode = argv[1][1];
        argc--;
        argv++;
    }
    if (argc != 4) {
        fprintf(stderr, "usage: ttbcshow [-b|-a|-u] [-g] FONT.TTF PPEM TEXT\n");
        return 2;
    }
    fp = fopen(argv[1], "rb");
    if (!fp)
        return 1;
    fseek(fp, 0, SEEK_END);
    datalen = ftell(fp);
    fseek(fp, 0, SEEK_SET);
    data = malloc(datalen);
    if (fread(data, 1, datalen, fp) != (size_t)datalen)
        return 1;
    fclose(fp);
    if (!ttfx_InitFont(&f, readfn, 0, 0, cache, sizeof cache, 0))
        return 1;
    ppem = atoi(argv[2]);
    sc = ttfx_ScaleForMappingEmToPixels(&f, ppem);
    ttfx_SetHinting(&f, mode == 'b' ? TTFX_HINT_ALL : mode == 'a' ? TTFX_HINT_DEFAULT : 0);
    if (mode == 'b') {
        ttfx_u32 bytes = ttfx_bcBytes(&f);
        mem = bytes ? malloc(bytes) : 0;
        if (!mem || !ttfx_bcPrepare(&f, mem, bytes, sc, sc, ppem, ppem))
            printf("prepare failed: error %d in program %d at %u\n", ttfx__bcLastError, ttfx__bcLastProg,
                   ttfx__bcLastIp);
    }
    if (getenv("BCTRACE"))
        ttfx__bcTrace = 1;
    if (glyphs) {
        char *s = argv[3];
        while (*s && n < 256) {
            gl[n++] = (int)strtol(s, &s, 0);
            if (*s == ',')
                s++;
        }
    } else
        for (i = 0; argv[3][i] && n < 256; i++)
            gl[n++] = ttfx_FindGlyphIndex(&f, (unsigned char)argv[3][i]);
    ttfx_GetFontVMetrics(&f, &asc, &dsc, &gap);
    bm.w = 1024;
    bm.h = ppem * 2 + 8;
    bm.stride = 128;
    bm.pixels = pix;
    memset(pix, 0, sizeof pix);
    for (i = 0; i < n; i++) {
        ttfx_outline o;
        int adv;
        if (mode == 'b' && f.bc && ttfx_LoadOutline(&f, (ttfx_u16)gl[i], sc, sc, &o)) {
            adv = (int)((o.adv + 32) >> 6);
            if (!ttfx_RenderOutline(&f, &bm, &o, -x, -(ppem + 2)))
                printf("glyph %d: no memory\n", gl[i]);
            printf("glyph %d: %u points, advance %d (%ld/64), dropout %d\n", gl[i], o.n, adv, (long)o.adv, o.dropout);
            ttfx_FreeOutline(&f, &o);
        } else {
            ttfx_u16 a;
            int lsb, x0, y0, x1, y1;
            if (mode == 'b')
                printf("glyph %d: not by bytecode (error %d in program %d at %u)\n", gl[i], ttfx__bcLastError,
                       ttfx__bcLastProg, ttfx__bcLastIp);
            ttfx_GetGlyphHMetrics(&f, (ttfx_u16)gl[i], &a, &lsb);
            adv = (int)ttfx_MulDivRound(a, ppem, f.unitsPerEm);
            ttfx_GetGlyphBitmapBox(&f, (ttfx_u16)gl[i], sc, sc, 0, 0, &x0, &y0, &x1, &y1);
            {
                ttfx_bitmap b2 = bm;
                b2.pixels = pix;
                ttfx_RenderGlyph(&f, &b2, (ttfx_u16)gl[i], sc, sc, 0, 0, -x, -(ppem + 2));
            }
        }
        x += adv;
    }
    w = x + 4;
    for (i = 0; i < bm.h; i++) {
        int k, any = 0;
        for (k = 0; k < w; k++)
            any |= pix[i * 128 + (k >> 3)] & (0x80 >> (k & 7));
        if (!any)
            continue;
        for (k = 0; k < w; k++)
            putchar(pix[i * 128 + (k >> 3)] & (0x80 >> (k & 7)) ? '#' : '.');
        putchar('\n');
    }
    return 0;
}
