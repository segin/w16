/*
 * surf.c - drawing surfaces: the VGA's screen memory and GDI's memory
 * bitmaps (PBITMAP), one scan line of one plane at a time.
 *
 * The screen is mode 12h: four planes of 80 bytes a line, 480 lines, at
 * A000:0000 (__A000H). The drawing code works plane by plane: the
 * sequencer's map mask selects the one plane written, the graphics
 * controller's read map select the one read, write mode 0 with no
 * rotation, no function, no set/reset and every bit enabled, so a byte
 * written or read is that plane's eight pixels. (IBM VGA technical
 * reference; the register numbers are the standard VGA's.)
 *
 * Memory bitmaps are as the DDK's PBITMAP describes: lines top first, a
 * colour bitmap's four planes interleaved per line, split into 64K
 * segments of bmScanSegment lines (selectors __AHINCR apart) when the
 * bits are bigger than a segment.
 */
#include "vga.h"

/* the PDEVICE or PBITMAP GDI passed: the surface it describes. 0 for
   something that is neither. */
int surf_get(SURF *s, LPVOID dev)
{
    LPPBITMAP b = (LPPBITMAP)dev;
    if (!dev)
        return 0;
    if (b->bmType != 0) {
        s->planes = 4;
        s->screen = 1;
        s->width = 640;
        s->height = 480;
        s->wbytes = 80;
        s->sel = vga_sel;
        s->off = 0;
        s->scanseg = 0;
        s->fill = 0;
        return 1;
    }
    s->screen = 0;
    s->planes = b->bmPlanes == 1 && b->bmBitsPixel == 1 ? 1 : 4;
    s->width = b->bmWidth;
    s->height = b->bmHeight;
    s->wbytes = b->bmWidthBytes;
    s->sel = SELECTOROF(b->bmBits);
    s->off = OFFSETOF(b->bmBits);
    s->scanseg = b->bmScanSegment;
    s->fill = b->bmFillBytes;
    /* one segment: the lines simply follow each other */
    if ((DWORD)s->wbytes * s->planes * s->height + s->off <= 0x10000L)
        s->scanseg = 0;
    return 1;
}

/* the bytes of line y, plane p (the screen: the plane must be selected
   with scr_plane before they are used) */
BYTE __far *surf_line(SURF *s, int y, int plane)
{
    WORD line = s->wbytes * s->planes;
    if (s->screen)
        return (BYTE __far *)MK_FP(s->sel, (WORD)y * 80);
    if (!s->scanseg)
        return (BYTE __far *)MK_FP(s->sel, s->off + (WORD)y * line + plane * s->wbytes);
    return (BYTE __far *)MK_FP(s->sel + (WORD)(y / s->scanseg) * ahincr,
                               (WORD)(y % s->scanseg) * line + plane * s->wbytes);
}

/* the screen's plane for the next reads and writes */
void scr_plane(int plane)
{
    outw(0x3C4, (WORD)(0x0100 << plane) | 0x02);  /* map mask */
    outw(0x3CE, (WORD)(plane << 8) | 0x04);       /* read map select */
}

/* the graphics controller and sequencer as the drawing code expects */
void scr_reset(void)
{
    outw(0x3CE, 0x0000);                /* set/reset 0 */
    outw(0x3CE, 0x0001);                /* enable set/reset: none */
    outw(0x3CE, 0x0003);                /* rotate 0, function replace */
    outw(0x3CE, 0x0005);                /* write mode 0, read mode 0 */
    outw(0x3CE, 0xFF08);                /* bit mask: all */
    outw(0x3C4, 0x0F02);                /* map mask: all */
}

/* a drawing operation on the screen starts and ends: the cursor is kept
   off the rectangle [x0, x1) x [y0, y1) while it runs */
void scr_begin(int x0, int y0, int x1, int y1)
{
    busy = 1;
    cursor_exclude(x0, y0, x1, y1);
    scr_reset();
}

void scr_end(void)
{
    cursor_unexclude();
}

/* r cut to the clip rectangle (if any) and the surface; FALSE when
   nothing is left */
int clip_rect(RECT *r, LPRECT clip, SURF *s)
{
    if (clip) {
        if (r->left < clip->left)
            r->left = clip->left;
        if (r->top < clip->top)
            r->top = clip->top;
        if (r->right > clip->right)
            r->right = clip->right;
        if (r->bottom > clip->bottom)
            r->bottom = clip->bottom;
    }
    if (s) {
        if (r->left < 0)
            r->left = 0;
        if (r->top < 0)
            r->top = 0;
        if (r->right > s->width)
            r->right = s->width;
        if (r->bottom > s->height)
            r->bottom = s->height;
    }
    return r->left < r->right && r->top < r->bottom;
}
