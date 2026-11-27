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
/* with TTFX_HINT_BYTECODE, the memory for a size's state (ttfx_bcBytes):
   each size's fpgm and prep run in it before its glyphs (0: none, the
   autohinter then) */
extern void TTFX_FAR *ttv_bcmem;
extern ttfx_u32 ttv_bcbytes;

/* the font's name (name table: the full name, else the family) as ASCII */
void ttv_name(const ttfx_font *f, char *out, int size);
/* name record id (1 the family, 4 the full name) as ASCII, "" if none */
void ttv_nameid(const ttfx_font *f, int id, char *out, int size);

/* draw the page into bits (cleared first): sample lines at 11 sizes
   (7 to 32 points at 96 dpi), then characters 20h-FFh (Windows' ANSI
   set) in a grid of 32 columns at 16 pixels to the em. Returns the
   glyphs drawn; *failed counts those that ran out of memory */
int ttv_page(const ttfx_font *f, ttfx_u8 TTFX_FAR *bits, int *failed);

/* page 2, the specimen: the font's name, then a sample line at every size
   from 8 to 24 pixels to the em, then the alphabets at 11 and 13; drawn
   into bits as ttv_page does. ttv_line gives its line i (0 the name):
   the em, the baseline and the text (from x 2); 0 past the last */
#define TTV_LINES 20
int ttv_line(const ttfx_font *f, int i, int *em, int *y, char *text, int size);
int ttv_sizes(const ttfx_font *f, ttfx_u8 TTFX_FAR *bits, int *failed);
