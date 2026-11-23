/*
 * ttvpage.h - TTVIEW's page (tests/apps/ttvpage.c): text in a TrueType
 * font rasterised with gdi/ttf/ttfx.c into a 1-bit page, the same on
 * Windows (TTVIEW.EXE) and on the host (tests/ttf/ttvhost.c), so the
 * two can be compared bit for bit.
 */
#include "../../gdi/ttf/ttfx.h"

#define TTV_W 640                       /* the page: 640 x 480, 1 bit, */
#define TTV_H 480                       /* rows of 80 bytes (a word multiple), */
#define TTV_STRIDE 80                   /* set bits are the glyphs' pixels */
#define TTV_ARENA 49152U                /* scratch memory for one glyph */

/* the arena ttfx_alloc takes its blocks from (the program supplies the
   memory; one glyph's blocks at a time) */
extern ttfx_u8 TTFX_FAR *ttv_arena;
extern unsigned ttv_arena_used, ttv_arena_peak;

/* the font's name (name table: the full name, else the family) as ASCII */
void ttv_name(const ttfx_font *f, char *out, int size);

/* draw the page into bits (cleared first): sample lines at 11 sizes
   (7 to 32 points at 96 dpi), then characters 20h-FFh (Windows' ANSI
   set) in a grid of 32 columns at 16 pixels to the em. Returns the
   glyphs drawn; *failed counts those that ran out of memory */
int ttv_page(const ttfx_font *f, ttfx_u8 TTFX_FAR *bits, int *failed);
