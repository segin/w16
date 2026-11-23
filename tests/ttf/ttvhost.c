/*
 * ttvhost.c - TTVIEW's page (tests/apps/ttvpage.c) drawn on the host,
 * from the same code as TTVIEW.EXE, for tests/ttview.sh to compare with
 * what TTVIEW drew on Windows (tests/ttf/ttvcmp.py).
 *
 *   usage: ttvhost [-u] [-2] FONT.TTF PAGE.PG
 *     -u  unhinted (ttfx_SetHinting 0; TTVIEW /U)
 *     -2  page 2, the specimen at 8-24 pixels to the em (TTVIEW /P2)
 *
 * PAGE.PG: 640 x 480 bits, rows of 80 bytes, set bits the glyphs' pixels
 * (TTVIEW writes its TTVIEW.PG the same way). Prints the font's name, the
 * glyphs drawn and the arena's peak.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "../apps/ttvpage.h"

static unsigned char *data;
static long datalen;

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
    static unsigned char cache[TTFX_CACHE_BYTES(8)], page[TTV_STRIDE * TTV_H];
    ttfx_font f;
    FILE *fp;
    char name[64];
    int n, failed, unhinted = 0, page2 = 0;
    while (argc > 1 && argv[1][0] == '-') {
        if (!strcmp(argv[1], "-u"))
            unhinted = 1;
        else if (!strcmp(argv[1], "-2"))
            page2 = 1;
        else
            break;
        argc--;
        argv++;
    }
    if (argc != 3) {
        fprintf(stderr, "usage: ttvhost [-u] [-2] FONT.TTF PAGE.PG\n");
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
    ttv_arena = malloc(TTV_ARENA);
    if (!ttfx_InitFont(&f, readfn, 0, 0, cache, sizeof cache, 0)) {
        fprintf(stderr, "ttvhost: %s is not a TrueType font\n", argv[1]);
        return 1;
    }
    if (unhinted)
        ttfx_SetHinting(&f, 0);
    n = page2 ? ttv_sizes(&f, page, &failed) : ttv_page(&f, page, &failed);
    ttv_name(&f, name, sizeof name);
    printf("ttvhost: %s: %d glyphs, %d failed, arena peak %u\n", name, n, failed, ttv_arena_peak);
    fp = fopen(argv[2], "wb");
    if (!fp || fwrite(page, 1, sizeof page, fp) != sizeof page)
        return 1;
    fclose(fp);
    return 0;
}
