/*
 * ttfx.h - TrueType fonts in fixed-point arithmetic, for 16-bit code
 * without a coprocessor: loading, cmap lookup, metrics, kerning, glyph
 * outlines, name strings, and a monochrome scan converter.
 *
 * Adapted from stb_truetype v1.28 (gdi/ttf/stb_truetype.h), by Sean
 * Barrett / RAD Game Tools, 2009-2021, with its contributors (Mikko
 * Mononen: compound shapes, cmap formats; Tor Andersson: kerning; and the
 * others its header lists). stb_truetype is dual-licensed public domain
 * (Unlicense) or MIT; w16 uses it under the public-domain dedication, and
 * this adaptation is likewise in the public domain (LICENSE).
 *
 * What changed from stb_truetype (see ttfx.c for the details):
 * - no floating point: scale factors are 8.24 fixed point, outline
 *   coordinates on the pixel grid 26.6 (the TrueType rasteriser's own
 *   F26Dot6), curve flattening and edge stepping exact integer arithmetic;
 * - the font is not one block in memory: every read goes through a small
 *   page cache filled by a callback (a file, or a huge block), so a .TTF
 *   over 64K needs only the cache's few K of near or far memory;
 * - 16-bit int: everything that can pass 32767 is ttfx_i32;
 * - the rasteriser is monochrome with the TrueType specification's rules
 *   1 and 2 (a pixel is on when its centre is inside the outline or on
 *   it; non-zero winding), no anti-aliasing and (stage 1) no dropout
 *   control and no TrueType instructions; instead an autohinter of w16's
 *   own (ttfhint.c) grid-fits the outline at render time;
 * - TrueType outlines only: no CFF/Type 2, SVG, GPOS, SDF, packing or
 *   baking.
 */
#ifndef TTFX_H
#define TTFX_H

#if defined(__WATCOMC__) && !defined(__386__)
#define TTFX_FAR __far
typedef unsigned char ttfx_u8;
typedef signed char ttfx_i8;
typedef unsigned short ttfx_u16;
typedef short ttfx_i16;
typedef unsigned long ttfx_u32;
typedef long ttfx_i32;
#else
#include <stdint.h>
#define TTFX_FAR
typedef uint8_t ttfx_u8;
typedef int8_t ttfx_i8;
typedef uint16_t ttfx_u16;
typedef int16_t ttfx_i16;
typedef uint32_t ttfx_u32;
typedef int32_t ttfx_i32;
#endif

/* reads len bytes at offset off of the font file into dst; returns the
   bytes read (fewer at the end of the file: the rest are taken as 0) */
typedef unsigned (*ttfx_readfn)(void *user, ttfx_u32 off, void TTFX_FAR *dst, unsigned len);

/* the page cache: TTFX_PAGE bytes a page, each slot TTFX_SLACK bytes
   longer so that any read of up to TTFX_SLACK bytes is contiguous */
#define TTFX_PAGE 512
#define TTFX_SLACK 16
#define TTFX_SLOT (TTFX_PAGE + TTFX_SLACK + 8)
/* bytes of cache memory for n pages (at least 2) */
#define TTFX_CACHE_BYTES(n) ((unsigned)(n) * TTFX_SLOT)

/* the autohinter's flags (ttfx_font.hint, ttfx_SetHinting) */
#define TTFX_HINT_Y 1                   /* vertical: blue zones, horizontal stems */
#define TTFX_HINT_X 2                   /* horizontal: vertical stems */
#define TTFX_HINT_DROPOUT 4             /* the scan converter's rule 3, along rows */
#define TTFX_HINT_DEFAULT (TTFX_HINT_Y | TTFX_HINT_X | TTFX_HINT_DROPOUT)
#define TTFX_HINT_BYTECODE 8            /* the font's own instructions (ttfbc.c), where it has them */
#define TTFX_HINT_ALL (TTFX_HINT_DEFAULT | TTFX_HINT_BYTECODE)
#define TTFX_HINT_ZONES 5

typedef struct ttfx_font {
    ttfx_readfn read;
    void *user;                     /* the callback's */
    void *alloc;                    /* TTFX_malloc's userdata */
    ttfx_u8 TTFX_FAR *cache;
    unsigned nslots;
    ttfx_u16 clock;
    ttfx_i32 lastpage;              /* the most recent page and its data */
    ttfx_u8 TTFX_FAR *lastdata;

    ttfx_i32 fontstart;
    ttfx_u16 numGlyphs;
    /* table offsets in the file */
    ttfx_i32 loca, head, glyf, hhea, hmtx, kern, os2, post, name, maxp;
    ttfx_i32 index_map;             /* the cmap subtable used */
    ttfx_u16 index_map_platform, index_map_encoding;
    int indexToLocFormat;
    ttfx_u16 unitsPerEm;

    /* the autohinter (ttfhint.c): TTFX_HINT_* flags, TTFX_HINT_DEFAULT
       from ttfx_InitFont; then its analysis of the font, made at the
       first hinted glyph (font units, y up) */
    int hint;
    ttfx_u8 hint_done, nzones, hint_up;  /* hint_up: a stem is rounded up from it (1/64 px) */
    ttfx_i16 zone[TTFX_HINT_ZONES][3];  /* reference, overshoot, 1 top 0 bottom */
    ttfx_i16 stdw[2];                   /* standard stem widths across x, y (0: none) */

    /* the bytecode interpreter (ttfbc.c): the size ttfx_bcPrepare made
       ready, 0: none */
    void TTFX_FAR *bc;
} ttfx_font;

/* 8.24 fixed-point scale: pixels per font unit */
typedef ttfx_i32 ttfx_scale;
#define TTFX_ONE_SCALE 0x01000000L

typedef struct {
    ttfx_i16 x, y, cx, cy;
    ttfx_u8 type, padding;
} ttfx_vertex;
enum { TTFX_vmove = 1, TTFX_vline, TTFX_vcurve };

/* a 1-bit bitmap, rows top first, the most significant bit leftmost;
   glyph pixels are set (OR), others left as they are */
typedef struct {
    int w, h;
    unsigned stride;                /* bytes a row */
    ttfx_u8 TTFX_FAR *pixels;
} ttfx_bitmap;

/* memory, supplied by the program (alloc is ttfx_font's): blocks of
   up to 65000 bytes; ttfx_alloc returns 0 for more or when there is none */
void TTFX_FAR *ttfx_alloc(void *alloc, ttfx_u32 n);
void ttfx_free(void *alloc, void TTFX_FAR *p);

/* loading: the font at fontstart (0 for a .TTF) of the file read() reads;
   cache is the cache's memory, cache_bytes long (TTFX_CACHE_BYTES).
   Returns 0 if it is not a TrueType font w16 can use */
int ttfx_InitFont(ttfx_font *f, ttfx_readfn read, void *user, void *alloc,
                  void TTFX_FAR *cache, unsigned cache_bytes, ttfx_i32 fontstart);

/* raw reads of the file (big-endian), through the cache */
ttfx_u8 ttfx_Byte(const ttfx_font *f, ttfx_i32 off);
ttfx_u16 ttfx_U16(const ttfx_font *f, ttfx_i32 off);
ttfx_i16 ttfx_I16(const ttfx_font *f, ttfx_i32 off);
ttfx_u32 ttfx_U32(const ttfx_font *f, ttfx_i32 off);
/* a table's offset in the file, 0 if absent; its length in *len */
ttfx_i32 ttfx_FindTable(const ttfx_font *f, const char *tag, ttfx_u32 *len);
/* copy n bytes of the file at off to dst */
void ttfx_Read(const ttfx_font *f, ttfx_i32 off, void TTFX_FAR *dst, unsigned n);

/* the glyph index of a character (0: missing) */
ttfx_u16 ttfx_FindGlyphIndex(const ttfx_font *f, ttfx_i32 codepoint);

/* metrics in font units */
void ttfx_GetFontVMetrics(const ttfx_font *f, int *ascent, int *descent, int *lineGap);
void ttfx_GetFontBoundingBox(const ttfx_font *f, int *x0, int *y0, int *x1, int *y1);
void ttfx_GetGlyphHMetrics(const ttfx_font *f, ttfx_u16 glyph, ttfx_u16 *advanceWidth, int *leftSideBearing);
int ttfx_GetGlyphBox(const ttfx_font *f, ttfx_u16 glyph, int *x0, int *y0, int *x1, int *y1);
int ttfx_IsGlyphEmpty(const ttfx_font *f, ttfx_u16 glyph);
int ttfx_GetGlyphKernAdvance(const ttfx_font *f, ttfx_u16 g1, ttfx_u16 g2);
int ttfx_GetKerningTableLength(const ttfx_font *f);
/* entry i of the kerning table (format 0) */
int ttfx_GetKerningEntry(const ttfx_font *f, int i, ttfx_u16 *g1, ttfx_u16 *g2, int *advance);

/* scales (8.24): the ascent-to-descent height, or the em, in pixels */
ttfx_scale ttfx_ScaleForPixelHeight(const ttfx_font *f, ttfx_i32 pixels);
ttfx_scale ttfx_ScaleForMappingEmToPixels(const ttfx_font *f, ttfx_i32 pixels);
/* v font units at scale s, in 26.6 pixels (rounded to nearest) */
ttfx_i32 ttfx_Scale26(ttfx_i32 v, ttfx_scale s);
/* ... in whole pixels (rounded to nearest, half away from zero) */
ttfx_i32 ttfx_ScaleRound(ttfx_i32 v, ttfx_scale s);
/* v * num / den exactly, rounded to nearest (half away from zero): for
   metrics, where the 8.24 scale's last bit can matter at a tie */
ttfx_i32 ttfx_MulDivRound(ttfx_i32 v, ttfx_i32 num, ttfx_i32 den);

/* the outline: *pv is allocated (TTFX_malloc), freed by ttfx_FreeShape;
   returns the vertex count (0: empty or failed) */
int ttfx_GetGlyphShape(const ttfx_font *f, ttfx_u16 glyph, ttfx_vertex TTFX_FAR **pv);
void ttfx_FreeShape(const ttfx_font *f, ttfx_vertex TTFX_FAR *v);

/* render-time hinting (ttfhint.c): flags TTFX_HINT_* (0: off). Hinting
   moves outline points only, within the pixel box ttfx_GetGlyphBitmapBox
   gives; metrics (advance widths, boxes) never change. X and Y apply to
   ttfx_Flatten, ttfx_Rasterize and ttfx_RenderGlyph; DROPOUT to the last
   two: a run of a row between two pixel centres, inside the outline but
   holding no centre (a stroke thinner than a pixel), turns on the pixel
   its middle is in (the TrueType specification's rule 3 along rows, as a
   font's instructions would ask for at small sizes) */
void ttfx_SetHinting(ttfx_font *f, int flags);

/* the pixel box a glyph's outline can touch, at scale sx, sy and shift
   (26.6), y down (stb_truetype's GetGlyphBitmapBoxSubpixel) */
void ttfx_GetGlyphBitmapBox(const ttfx_font *f, ttfx_u16 glyph, ttfx_scale sx, ttfx_scale sy,
                            ttfx_i32 shift_x, ttfx_i32 shift_y, int *ix0, int *iy0, int *ix1, int *iy1);

/* scan-convert vertices into bm; the outline's pixel (x, y) (y down) goes
   to bm's (x - off_x, y - off_y). Returns 0 if out of memory */
int ttfx_Rasterize(const ttfx_font *f, ttfx_bitmap *bm, const ttfx_vertex TTFX_FAR *v, int nv,
                   ttfx_scale sx, ttfx_scale sy, ttfx_i32 shift_x, ttfx_i32 shift_y,
                   int off_x, int off_y);
/* the same for a glyph, at its bitmap box's top left: bm->w, h and stride
   must be set (from the box) and pixels cleared by the caller */
int ttfx_RenderGlyph(const ttfx_font *f, ttfx_bitmap *bm, ttfx_u16 glyph, ttfx_scale sx, ttfx_scale sy,
                     ttfx_i32 shift_x, ttfx_i32 shift_y, int ix0, int iy0);

/* the outline flattened to 26.6 points (y down, bitmap coordinates): the
   points and each contour's count, allocated (TTFX_malloc); for tests */
typedef struct { ttfx_i32 x, y; } ttfx_point;
int ttfx_Flatten(const ttfx_font *f, const ttfx_vertex TTFX_FAR *v, int nv, ttfx_scale sx, ttfx_scale sy,
                 ttfx_i32 shift_x, ttfx_i32 shift_y, int off_x, int off_y,
                 ttfx_point TTFX_FAR **pts, int TTFX_FAR **counts, int *ncontours);

/* (ttfhint.c, for ttfx_Flatten) the outline's points scaled to 26.6 (y
   down, +ox, +oy): [2i] vertex i's point, [2i+1] its control point; grid-
   fitted as f->hint asks. Allocated (TTFX_malloc); 0 if out of memory */
ttfx_point TTFX_FAR *ttfx__HintPoints(const ttfx_font *f, const ttfx_vertex TTFX_FAR *v, int nv,
                                      ttfx_scale sx, ttfx_scale sy, ttfx_i32 ox, ttfx_i32 oy);

/* ---- the bytecode interpreter (ttfbc.c) ----
   A size's state: the font program and the control value program run,
   the CVT scaled, the functions defined. ttfx_bcBytes gives the memory a
   size needs (0: the font has no maxp 1.0 or needs over 64K);
   ttfx_bcPrepare fills mem with it for the scales sx, sy (ppem_x, ppem_y
   pixels to the em) and attaches it to f (f->bc), returning 0 if fpgm or
   prep fails (f->bc 0: the autohinter then). The block holds no pointers
   to itself: a program may move it between uses and re-attach it with
   ttfx_bcUse (0 detaches). */
ttfx_u32 ttfx_bcBytes(const ttfx_font *f);
int ttfx_bcPrepare(ttfx_font *f, void TTFX_FAR *mem, ttfx_u32 bytes, ttfx_scale sx, ttfx_scale sy,
                   int ppem_x, int ppem_y);
void ttfx_bcUse(ttfx_font *f, void TTFX_FAR *mem);

/* a glyph grid-fitted by its instructions: the outline's TrueType points
   (26.6, y down, the origin at the first phantom point), on-curve flags
   (bit 0), each contour's last point; the hinted advance (26.6, the
   phantom points' distance); the scan converter's rules its scan control
   asks for (TTFX_DROP_*). ttfx_LoadOutline returns 0 when the bytecode is
   not used for it (TTFX_HINT_BYTECODE off, no size prepared at sx, sy,
   an error in the glyph's program, no memory): the caller then draws it
   as before (the autohinter, ttfx_RenderGlyph). */
#define TTFX_DROP_NONE 0                /* rules 1 and 2 */
#define TTFX_DROP_SIMPLE 1              /* and rule 3: dropouts, stubs included (SCANTYPE 0) */
#define TTFX_DROP_NOSTUBS 2             /* and rule 4: dropouts but not stubs (SCANTYPE 1) */
typedef struct {
    ttfx_point TTFX_FAR *p;
    ttfx_u8 TTFX_FAR *on;
    ttfx_u16 TTFX_FAR *ends;
    unsigned n, nc;
    ttfx_i32 adv;
    int dropout;
    void TTFX_FAR *mem;
} ttfx_outline;
int ttfx_LoadOutline(const ttfx_font *f, ttfx_u16 glyph, ttfx_scale sx, ttfx_scale sy, ttfx_outline *o);
void ttfx_FreeOutline(const ttfx_font *f, ttfx_outline *o);
/* the pixel box the outline (and its dropout pixels) can touch, y down */
void ttfx_OutlineBox(const ttfx_outline *o, int *x0, int *y0, int *x1, int *y1);
/* scan-convert it: the outline's pixel (x, y) to bm's (x - off_x, y -
   off_y), with the rules o->dropout names. 0: out of memory */
int ttfx_RenderOutline(const ttfx_font *f, ttfx_bitmap *bm, const ttfx_outline *o, int off_x, int off_y);
/* (ttfx.c, for ttfbc.c) a * b / c, truncated (round 0) or to nearest */
ttfx_i32 ttfx__MulDiv(ttfx_i32 a, ttfx_i32 b, ttfx_i32 c, int round);
/* (ttfx.c, for ttfbc.c) a glyph's data in the glyf table, -1 if none */
ttfx_i32 ttfx__GlyfOffset(const ttfx_font *f, ttfx_u16 glyph, ttfx_u32 *len);

/* a name record (name table): copies at most size bytes of the string
   (UTF-16BE for platform 3 and 0) to buf; returns its whole length, -1 if
   there is none */
int ttfx_GetFontNameString(const ttfx_font *f, void TTFX_FAR *buf, int size, int platformID,
                           int encodingID, int languageID, int nameID);

enum { TTFX_PLATFORM_ID_UNICODE = 0, TTFX_PLATFORM_ID_MAC = 1, TTFX_PLATFORM_ID_ISO = 2,
       TTFX_PLATFORM_ID_MICROSOFT = 3 };
enum { TTFX_MS_EID_SYMBOL = 0, TTFX_MS_EID_UNICODE_BMP = 1, TTFX_MS_EID_SHIFTJIS = 2,
       TTFX_MS_EID_UNICODE_FULL = 10 };

#endif
