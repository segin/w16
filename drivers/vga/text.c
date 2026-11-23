/*
 * text.c - ExtTextOut, StrBlt and GetCharWidth for raster fonts.
 *
 * GDI realizes the fonts itself (RealizeObject(OBJ_FONT) returns 0) and
 * hands the driver the font in memory: FONTINFO (DDK) at offset 42h of
 * the font's segment, a Windows 3.x font (GDI converts 2.x ones because
 * the GDIINFO has RC_BIGFONT), its character table of 6-byte entries
 * (width, 32-bit offset of the glyph from the font's start) at offset
 * 94h. A glyph is stored a column of 8 pixels at a time, each column
 * dfPixHeight bytes, top first, bit 7 the leftmost pixel (the font file
 * format: DDK "Font File Format").
 *
 * With wCount < 0 ExtTextOut only measures (GDI's GetTextExtent: VGREC,
 * the width in AX and the height in DX).
 */
#include "vga.h"

#define TROWB 132
#define MAX_GRUN 256

static BYTE tbuf[TROWB];

typedef struct {
    DWORD goff;                         /* c->base + glyph offset in font */
    WORD sel, off;                      /* pre-resolved pointer when fast != 0 */
    short rel;                          /* px - bx0 */
    short w;                            /* glyph width in pixels */
    BYTE cols;                          /* (w + 7) >> 3 */
    BYTE fast;                          /* 1 if entire glyph fits in one 64K seg */
} GLYPH_RUN;

static GLYPH_RUN grun[MAX_GRUN];

typedef struct {
    LPFONTINFO f;
    WORD base;                          /* the font's offset in its segment */
    BYTE first, last, dflt, brk;
    short height;
    BYTE bold;                          /* emboldened: 1 */
} FONTCTX;

static void font_ctx(FONTCTX *c, LPFONTINFO f, LPTEXTXFORM x)
{
    c->f = f;
    c->base = OFFSETOF(f) - FONTINFO_OFFSET;
    c->first = f->dfFirstChar;
    c->last = f->dfLastChar;
    c->dflt = f->dfDefaultChar;
    c->brk = f->dfBreakChar;
    c->height = f->dfPixHeight;
    /* Emboldening (dpText TC_EA_DOUBLE), when TEXTXFORM's txfAccelerator
       has 200h (VG6, VGREC), as 3.1's driver does it: each glyph again one
       pixel to the right, every character one pixel wider, the extent one
       more (the overhang); not for a font that is bold already (System
       asked for 900: GDI sets the bit, 3.1's driver draws it plain;
       UNVERIFIED where between 400 and 700 the line is). The other bits
       are GDI's own simulations:
       italic (400h: GDI calls ExtTextOut a band of two rows at a time,
       each moved right), underline (800h) and strike-out (GDI's
       polylines after the text). */
    c->bold = x && (x->txfAccelerator & 0x200) && f->dfWeight < 700 ? 1 : 0;
}

/* the character table entry of character ch */
static BYTE __far *char_entry(FONTCTX *c, BYTE ch)
{
    WORD i;
    if (ch < c->first || ch > c->last)
        i = c->dflt;
    else
        i = ch - c->first;
    return (BYTE __far *)MK_FP(SELECTOROF(c->f), c->base + 0x94 + i * 6);
}

/* a byte of the font at offset o from its segment's start: GDI's
   integer-scaled copies can be over 64K (selectors __AHINCR apart; the
   3.x character table's offsets are 32-bit) */
static BYTE __far *glyph_byte(FONTCTX *c, DWORD o)
{
    return (BYTE __far *)MK_FP(SELECTOROF(c->f) + (WORD)(o >> 16) * ahincr, (WORD)o);
}

static WORD char_width(FONTCTX *c, BYTE ch)
{
    return *(WORD __far *)char_entry(c, ch);
}

/* the advance of one character, updating *berr for justification */
static int char_advance(FONTCTX *c, BYTE ch, int gw, LPSHORT widths, WORD i, LPDRAWMODE dm,
                        short *berr)
{
    int w;
    if (widths)
        return widths[i];
    w = gw + c->bold;
    if (dm) {
        if (dm->TBreakExtra && ch == (BYTE)(c->first + c->brk)) {
            w += dm->BreakExtra;
            *berr -= dm->BreakRem;
            if (*berr <= 0) {
                w++;
                *berr += dm->BreakCount;
            }
        }
        w += dm->CharExtra;
    }
    return w;
}

/* the glyphs' bits of row r (0 = the text's top) into tbuf, which holds
   pixels [bx0, bx0 + 8 * nbuf) */
static void build_row(FONTCTX *c, LPCSTR s, int n, int x, int r, int bx0, int nbuf,
                      LPSHORT widths, LPDRAWMODE dm)
{
    int i, k, cols, rel, idx, sh, w;
    long px = x, bx1 = (long)bx0 + (long)nbuf * 8;
    short berr = dm ? dm->BreakErr : 0;
    BYTE __far *e;
    DWORD g;
    BYTE gb;
    for (i = 0; i < nbuf; i++)
        tbuf[i] = 0;
    for (i = 0; i < n; i++) {
        BYTE ch = (BYTE)s[i];
        e = char_entry(c, ch);
        w = *(WORD __far *)e;
        if (px + w + c->bold > bx0 && px < bx1) {
            g = c->base + *(DWORD __far *)(e + 2);
            cols = (w + 7) >> 3;
            for (k = 0; k < cols; k++) {
                gb = *glyph_byte(c, g + (DWORD)k * c->height + r);
                if (k == cols - 1 && (w & 7))
                    gb &= (BYTE)(0xFF << (8 - (w & 7)));
                if (!gb)
                    continue;
                rel = (int)(px + 8 * k - bx0);
                idx = rel >= 0 ? rel >> 3 : -((-rel + 7) >> 3);
                sh = rel - idx * 8;
                if (idx >= 0 && idx < nbuf)
                    tbuf[idx] |= (BYTE)(gb >> sh);
                if (sh && idx + 1 >= 0 && idx + 1 < nbuf)
                    tbuf[idx + 1] |= (BYTE)(gb << (8 - sh));
                if (c->bold) {
                    /* again, one pixel right */
                    int sh2 = sh + 1, idx2 = idx;
                    if (sh2 == 8) {
                        sh2 = 0;
                        idx2++;
                    }
                    if (idx2 >= 0 && idx2 < nbuf)
                        tbuf[idx2] |= (BYTE)(gb >> sh2);
                    if (sh2 && idx2 + 1 >= 0 && idx2 + 1 < nbuf)
                        tbuf[idx2 + 1] |= (BYTE)(gb << (8 - sh2));
                }
            }
        }
        px += char_advance(c, ch, w, widths, (WORD)i, dm, &berr);
    }
}

/* pre-resolve visible glyphs in chunk [bx0, bx0 + 8 * nbuf); returns count
   or -1 if more than MAX_GRUN visible glyphs */
static int prep_chunk(FONTCTX *c, LPCSTR s, int n, int x, int bx0, int nbuf,
                      LPSHORT widths, LPDRAWMODE dm)
{
    int i, ng = 0, w, cols;
    long px = x, bx1 = (long)bx0 + (long)nbuf * 8;
    short berr = dm ? dm->BreakErr : 0;
    BYTE __far *e;
    DWORD goff;
    WORD fsel = SELECTOROF(c->f);
    for (i = 0; i < n; i++) {
        BYTE ch = (BYTE)s[i];
        e = char_entry(c, ch);
        w = *(WORD __far *)e;
        if (w > 0 && px + w + c->bold > bx0 && px < bx1) {
            if (ng >= MAX_GRUN)
                return -1;
            goff = c->base + *(DWORD __far *)(e + 2);
            cols = (w + 7) >> 3;
            grun[ng].goff = goff;
            grun[ng].rel = (short)(px - bx0);
            grun[ng].w = (short)w;
            grun[ng].cols = (BYTE)cols;
            if ((goff & 0xFFFFL) + (DWORD)cols * c->height <= 0x10000L) {
                grun[ng].sel = fsel + (WORD)(goff >> 16) * ahincr;
                grun[ng].off = (WORD)goff;
                grun[ng].fast = 1;
            } else {
                grun[ng].fast = 0;
            }
            ng++;
        }
        px += char_advance(c, ch, w, widths, (WORD)i, dm, &berr);
    }
    return ng;
}

/* build one scanline r from pre-resolved grun[0..ng-1] into tbuf */
static void build_row_fast(FONTCTX *c, int ng, int r, int nbuf)
{
    int g, k, rel, idx, sh, h = c->height, bold = c->bold;
    BYTE gb;
    memsetf(tbuf, 0, (WORD)nbuf);
    for (g = 0; g < ng; g++) {
        GLYPH_RUN *gr = &grun[g];
        int cols = gr->cols, w = gr->w;
        BYTE __far *gp = gr->fast ? (BYTE __far *)MK_FP(gr->sel, gr->off + (WORD)r) : NULL;
        rel = gr->rel;
        for (k = 0; k < cols; k++, rel += 8) {
            if (gp) {
                gb = *gp;
                gp += h;
            } else {
                gb = *glyph_byte(c, gr->goff + (DWORD)k * h + r);
            }
            if (k == cols - 1 && (w & 7))
                gb &= (BYTE)(0xFF << (8 - (w & 7)));
            if (!gb)
                continue;
            idx = rel >= 0 ? rel >> 3 : -((-rel + 7) >> 3);
            sh = rel - idx * 8;
            if (idx >= 0 && idx < nbuf)
                tbuf[idx] |= (BYTE)(gb >> sh);
            if (sh && idx + 1 >= 0 && idx + 1 < nbuf)
                tbuf[idx + 1] |= (BYTE)(gb << (8 - sh));
            if (bold) {
                int sh2 = sh + 1, idx2 = idx;
                if (sh2 == 8) {
                    sh2 = 0;
                    idx2++;
                }
                if (idx2 >= 0 && idx2 < nbuf)
                    tbuf[idx2] |= (BYTE)(gb >> sh2);
                if (sh2 && idx2 + 1 >= 0 && idx2 + 1 < nbuf)
                    tbuf[idx2 + 1] |= (BYTE)(gb << (8 - sh2));
            }
        }
    }
}

/* the text colour where tbuf has 1 bits, within [x0, x1) of line y on screen
   using VGA Write Mode 3 (already configured by caller) */
static void put_row_scr(int y, int x0, int x1, int bx0)
{
    int i, b0 = x0 >> 3, b1 = (x1 - 1) >> 3, o = b0 - (bx0 >> 3);
    BYTE lm = (BYTE)(0xFF >> (x0 & 7)), rm = (BYTE)(0xFF << (7 - ((x1 - 1) & 7)));
    volatile BYTE __far *l = (volatile BYTE __far *)MK_FP(vga_sel, (WORD)y * 80);
    BYTE m;
    for (i = b0; i <= b1; i++) {
        m = tbuf[o + i - b0];
        if (i == b0)
            m &= lm;
        if (i == b1)
            m &= rm;
        if (m) {
            vga_latch(l + i);
            l[i] = m;
        }
    }
}

/* the text colour where tbuf has 1 bits, within [x0, x1) of line y */
static void put_row(SURF *D, int y, int x0, int x1, int bx0, int fg)
{
    int p, i, b0 = x0 >> 3, b1 = (x1 - 1) >> 3, o = b0 - (bx0 >> 3);
    BYTE lm = (BYTE)(0xFF >> (x0 & 7)), rm = (BYTE)(0xFF << (7 - ((x1 - 1) & 7)));
    BYTE __far *l;
    BYTE m, set;
    for (p = 0; p < D->planes; p++) {
        if (D->screen)
            scr_plane(p);
        l = surf_line(D, y, p);
        set = (BYTE)((fg >> p) & 1 ? 0xFF : 0);
        for (i = b0; i <= b1; i++) {
            m = tbuf[o + i - b0];
            if (i == b0)
                m &= lm;
            if (i == b1)
                m &= rm;
            if (m)
                l[i] = (BYTE)((l[i] & ~m) | (set & m));
        }
    }
}

static DWORD text_out(LPVOID lpDev, short x, short y, LPRECT clip, LPCSTR s, short n,
                      LPFONTINFO f, LPDRAWMODE dm, LPTEXTXFORM xf, LPSHORT widths,
                      LPRECT opaque, WORD opts)
{
    SURF D;
    FONTCTX c;
    RECT cr, box, op_rc;
    int fg, bg, r, k, has_op = 0, has_text = 0;
    if (!f)
        return 0x80000000L;
    if (n < 0) {
        /* the extent: as the box below (GDI's underline and strike-out
           take their length from it) */
        WORD cnt = (WORD)(-(long)n), i;
        short berr = dm ? dm->BreakErr : 0;
        long total = 0;
        font_ctx(&c, f, xf);
        for (i = 0; i < cnt; i++) {
            BYTE ch = (BYTE)s[i];
            int gw = (int)char_width(&c, ch);
            total += char_advance(&c, ch, gw, widths, i, dm, &berr);
        }
        if (dm)
            dm->BreakErr = berr;
        if (widths)
            total = total - widths[cnt - 1] + (int)char_width(&c, (BYTE)s[cnt - 1]);
        else
            total += c.bold;
        return MAKELONG((WORD)total, c.height);
    }
    if (!surf_get(&D, lpDev))
        return 0x80000000L;
    if (D.planes == 1) {
        fg = PC_MONO(dm->TextColor) * 15;
        bg = PC_MONO(dm->bkColor) * 15;
    } else {
        fg = PC_INDEX(dm->TextColor);
        bg = PC_INDEX(dm->bkColor);
    }
    cr.left = 0;
    cr.top = 0;
    cr.right = D.width;
    cr.bottom = D.height;
    if (clip)
        clip_rect(&cr, clip, &D);
    if ((opts & ETO_CLIPPED) && opaque)
        clip_rect(&cr, opaque, NULL);
    if ((opts & ETO_OPAQUE) && opaque) {
        op_rc = *opaque;
        has_op = clip_rect(&op_rc, &cr, &D);
    }
    if (n > 0) {
        short berr = dm->BreakErr;
        long cur = x, min_x = x, max_x = x;
        int i;
        font_ctx(&c, f, xf);
        /* the text's box, which the background fills (OPAQUE) and the glyphs
           are cut to: the extent, or with the caller's widths the bounding
           box of each character's own width (VG6: its bold second strike
           falls outside and is cut), supporting negative lpCharWidths */
        for (i = 0; i < n; i++) {
            BYTE ch = (BYTE)s[i];
            int gw = (int)char_width(&c, ch);
            long right_i = cur + gw + (widths ? 0 : c.bold);
            if (cur < min_x)
                min_x = cur;
            if (right_i > max_x)
                max_x = right_i;
            cur += char_advance(&c, ch, gw, widths, (WORD)i, dm, &berr);
            if (!widths) {
                if (cur < min_x)
                    min_x = cur;
                if (cur + c.bold > max_x)
                    max_x = cur + c.bold;
            }
        }
        if (min_x < cr.left)
            min_x = cr.left;
        if (max_x > cr.right)
            max_x = cr.right;
        if (min_x < max_x) {
            box.left = (short)min_x;
            box.right = (short)max_x;
            box.top = y;
            box.bottom = y + c.height;
            has_text = clip_rect(&box, &cr, &D);
        }
    }
    if (!has_op && !has_text)
        return n == 0 ? 1 : 0;
    if (D.screen) {
        RECT u = has_op ? op_rc : box;
        if (has_op && has_text) {
            if (box.left < u.left)
                u.left = box.left;
            if (box.top < u.top)
                u.top = box.top;
            if (box.right > u.right)
                u.right = box.right;
            if (box.bottom > u.bottom)
                u.bottom = box.bottom;
        }
        scr_begin(u.left, u.top, u.right, u.bottom);
    }
    if (has_op)
        fill_solid(&D, op_rc.left, op_rc.top, op_rc.right, op_rc.bottom, 0xF0, bg, bg != 0);
    if (has_text) {
        if (dm->bkMode == OPAQUE)
            fill_solid(&D, box.left, box.top, box.right, box.bottom, 0xF0, bg, bg != 0);
        if (D.screen) {
            outw(0x3C4, 0x0F02);                        /* map mask: all 4 planes */
            outw(0x3CE, (WORD)((fg & 15) << 8) | 0x00); /* set/reset: fg */
            outw(0x3CE, 0x0003);                        /* rotate 0, replace */
            outw(0x3CE, 0x0305);                        /* write mode 3, read mode 0 */
            outw(0x3CE, 0xFF08);                        /* bit mask: all */
        }
        for (k = box.left; k < box.right; k += (TROWB - 1) * 8) {
            int x1 = k + (TROWB - 1) * 8, bx0 = k & ~7, nbuf, ng;
            if (x1 > box.right)
                x1 = box.right;
            nbuf = ((x1 - 1) >> 3) - (bx0 >> 3) + 1;
            ng = prep_chunk(&c, s, n, x, bx0, nbuf, widths, dm);
            for (r = box.top; r < box.bottom; r++) {
                if (ng >= 0)
                    build_row_fast(&c, ng, r - y, nbuf);
                else
                    build_row(&c, s, n, x, r - y, bx0, nbuf, widths, dm);
                if (D.screen)
                    put_row_scr(r, k, x1, bx0);
                else
                    put_row(&D, r, k, x1, bx0, fg);
            }
        }
        if (D.screen)
            scr_reset();
    }
    if (D.screen)
        scr_end();
    return n == 0 ? 1 : 0;              /* VGREC: 3.1's driver returns 0 when it draws */
}

/* DWORD ExtTextOut(lpDestDev, wDestXOrg, wDestYOrg, lpClipRect, lpString,
   wCount, lpFontInfo, lpDrawMode, lpTextXForm, lpCharWidths,
   lpOpaqueRect, wOptions) - VGREC: pops 40 bytes */
DWORD EXPORT ExtTextOut(LPVOID lpDev, short x, short y, LPRECT clip, LPCSTR s, short n,
                        LPFONTINFO f, LPDRAWMODE dm, LPTEXTXFORM xf, LPSHORT widths,
                        LPRECT opaque, WORD opts)
{
    return text_out(lpDev, x, y, clip, s, n, f, dm, xf, widths, opaque, opts);
}

/* DWORD StrBlt(...): ExtTextOut without widths or an opaque rectangle
   (DDK) */
DWORD EXPORT StrBlt(LPVOID lpDev, short x, short y, LPRECT clip, LPCSTR s, short n,
                    LPFONTINFO f, LPDRAWMODE dm, LPTEXTXFORM xf)
{
    return text_out(lpDev, x, y, clip, s, n, f, dm, xf, NULL, NULL, 0);
}

/* WORD GetCharWidth(lpDestDev, lpBuffer, wFirstChar, wLastChar,
   lpFontInfo, lpDrawMode, lpFontTrans) */
WORD EXPORT GetCharWidth(LPVOID lpDev, LPSHORT buf, WORD first, WORD last, LPFONTINFO f,
                         LPDRAWMODE dm, LPTEXTXFORM xf)
{
    FONTCTX c;
    WORD ch;
    (void)lpDev;
    (void)dm;
    if (!f || !buf)
        return 0;
    font_ctx(&c, f, xf);
    for (ch = first; ch <= last && ch < 256; ch++)
        *buf++ = (short)(char_width(&c, (BYTE)ch) + 2 * c.bold);
    return 1;
}
