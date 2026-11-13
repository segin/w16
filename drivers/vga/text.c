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

static BYTE tbuf[TROWB];

typedef struct {
    LPFONTINFO f;
    WORD base;                          /* the font's offset in its segment */
    BYTE first, last, dflt, brk;
    short height;
    BYTE bold;
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
    /* emboldening (dpText TC_EA_DOUBLE): the glyph again one pixel
       right when GDI asks for more weight than the font has
       (UNVERIFIED against 3.1's rule) */
    c->bold = x && x->txfWeight > f->dfWeight + 100;
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

static WORD char_width(FONTCTX *c, BYTE ch)
{
    return *(WORD __far *)char_entry(c, ch);
}

/* the advance of each character (into adv), with the DRAWMODE's
   character extra and justification (DDK, ExtTextOut's algorithm; the
   running error in BreakErr is updated, the caller restores it when
   drawing); the total width returned */
static int advances(FONTCTX *c, LPCSTR s, int n, LPSHORT widths, LPDRAWMODE dm, short *adv)
{
    int i, w, total = 0;
    for (i = 0; i < n; i++) {
        BYTE ch = (BYTE)s[i];
        w = widths ? widths[i] : (int)char_width(c, ch);
        if (!widths && dm) {
            if (dm->TBreakExtra && ch == (BYTE)(c->first + c->brk)) {
                w += dm->BreakExtra;
                dm->BreakErr -= dm->BreakRem;
                if (dm->BreakErr <= 0) {
                    w++;
                    dm->BreakErr += dm->BreakCount;
                }
            }
            w += dm->CharExtra;
        }
        if (adv)
            adv[i] = (short)w;
        total += w;
    }
    return total;
}

static short advbuf[512];

/* the glyphs' bits of row r (0 = the text's top) into tbuf, which holds
   pixels [bx0, bx0 + 8 * nbuf) */
static void build_row(FONTCTX *c, LPCSTR s, int n, int x, int r, int bx0, int nbuf)
{
    int i, k, cols, px, rel, idx, sh, w;
    BYTE __far *e;
    BYTE __far *g;
    BYTE gb;
    for (i = 0; i < nbuf; i++)
        tbuf[i] = 0;
    px = x;
    for (i = 0; i < n; i++) {
        e = char_entry(c, (BYTE)s[i]);
        w = *(WORD __far *)e;
        g = (BYTE __far *)MK_FP(SELECTOROF(c->f), c->base + *(WORD __far *)(e + 2));
        cols = (w + 7) >> 3;
        for (k = 0; k < cols; k++) {
            gb = g[k * c->height + r];
            if (k == cols - 1 && (w & 7))
                gb &= (BYTE)(0xFF << (8 - (w & 7)));
            if (!gb)
                continue;
            rel = px + 8 * k - bx0;
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
        px += advbuf[i];
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
    RECT cr, box;
    int width, fg, bg, r, k;
    short berr;
    if (!f)
        return 0x80000000L;
    if (n < 0) {
        font_ctx(&c, f, xf);
        width = advances(&c, s, -n, widths, dm, NULL);
        return MAKELONG(width, c.height);
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
        RECT o = *opaque;
        if (clip_rect(&o, &cr, &D)) {
            if (D.screen)
                scr_begin(o.left, o.top, o.right, o.bottom);
            fill_solid(&D, o.left, o.top, o.right, o.bottom, 0xF0, bg, bg != 0);
            if (D.screen)
                scr_end();
        }
    }
    if (n == 0)
        return 1;
    if (n > 512)
        n = 512;
    font_ctx(&c, f, xf);
    berr = dm->BreakErr;
    width = advances(&c, s, n, widths, dm, advbuf);
    dm->BreakErr = berr;
    box.left = x;
    box.top = y;
    box.right = x + width + c.bold;
    box.bottom = y + c.height;
    if (!clip_rect(&box, &cr, &D))
        return MAKELONG(width, c.height);
    if (D.screen)
        scr_begin(box.left, box.top, box.right, box.bottom);
    if (dm->bkMode == OPAQUE) {
        RECT o;
        o.left = x;
        o.top = y;
        o.right = x + width;
        o.bottom = y + c.height;
        if (clip_rect(&o, &cr, &D))
            fill_solid(&D, o.left, o.top, o.right, o.bottom, 0xF0, bg, bg != 0);
    }
    for (k = box.left; k < box.right; k += (TROWB - 1) * 8) {
        int x1 = k + (TROWB - 1) * 8, bx0 = k & ~7, nbuf;
        if (x1 > box.right)
            x1 = box.right;
        nbuf = ((x1 - 1) >> 3) - (bx0 >> 3) + 1;
        for (r = box.top; r < box.bottom; r++) {
            build_row(&c, s, n, x, r - y, bx0, nbuf);
            put_row(&D, r, k, x1, bx0, fg);
        }
    }
    if (D.screen)
        scr_end();
    return MAKELONG(width, c.height);
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
        *buf++ = (short)(char_width(&c, (BYTE)ch) + c.bold);
    return 1;
}
