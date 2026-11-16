/*
 * rop.c - raster operations on bytes (eight pixels of one plane).
 *
 * A ternary raster operation (DDK, "Raster Operations"; the SDK's BitBlt)
 * is the truth table of the result over pattern P, source S and
 * destination D: bit (P*4 + S*2 + D) of the ROP code's byte 2 is the
 * result for those inputs. A binary one (ROP2, 1-16, the DRAWMODE's Rop2)
 * combines pen P and D: bit (P*2 + D) of Rop2 - 1. Every operation is
 * computed from its truth table; the common ones have their own loops.
 */
#include "vga.h"

/* the result of ROP3 rop on bytes p, s, d */
BYTE rop3_apply(BYTE rop, BYTE p, BYTE s, BYTE d)
{
    BYTE r = 0;
    switch (rop) {
    case 0x00: return 0;
    case 0xFF: return 0xFF;
    case 0xCC: return s;
    case 0xF0: return p;
    case 0xAA: return d;
    case 0x55: return (BYTE)~d;
    case 0x33: return (BYTE)~s;
    case 0x0F: return (BYTE)~p;
    case 0x66: return s ^ d;
    case 0x5A: return p ^ d;
    case 0x88: return s & d;
    case 0xEE: return s | d;
    case 0xC0: return p & s;
    case 0xBB: return (BYTE)(~s | d);
    case 0x44: return s & (BYTE)~d;
    case 0x11: return (BYTE)~(s | d);
    case 0x22: return (BYTE)(~s & d);
    case 0xA0: return p & d;
    case 0xFA: return p | d;
    case 0xFB: return (BYTE)(p | ~s | d);
    case 0xE2: return (BYTE)((s & p) | (~s & d));   /* DSPDxax */
    case 0xB8: return (BYTE)((s & d) | (~s & p));   /* PSDPxax */
    }
    if (rop & 0x01) r |= (BYTE)(~p & ~s & ~d);
    if (rop & 0x02) r |= (BYTE)(~p & ~s & d);
    if (rop & 0x04) r |= (BYTE)(~p & s & ~d);
    if (rop & 0x08) r |= (BYTE)(~p & s & d);
    if (rop & 0x10) r |= (BYTE)(p & ~s & ~d);
    if (rop & 0x20) r |= (BYTE)(p & ~s & d);
    if (rop & 0x40) r |= (BYTE)(p & s & ~d);
    if (rop & 0x80) r |= (BYTE)(p & s & d);
    return r;
}

/* the ROP3 that does what ROP2 r2 does with the pen as the pattern */
BYTE rop2_to_rop3(int r2)
{
    BYTE t = (BYTE)(r2 - 1) & 15, r = 0;
    int i;
    for (i = 0; i < 8; i++) {
        int p = (i >> 2) & 1, d = i & 1;
        if (t & (1 << (p * 2 + d)))
            r |= (BYTE)(1 << i);
    }
    return r;
}

/* n bytes of one plane: d[i] = rop(pat, src[i], d[i]) within the masks
   (lmask the first byte's pixels, rmask the last's); src may be NULL when
   the operation has no source */
void rop_run(BYTE rop, BYTE __far *d, const BYTE *src, BYTE pat, WORD n, BYTE lmask, BYTE rmask)
{
    WORD i;
    BYTE m, v, s;
    if (n == 1)
        lmask &= rmask;
    for (i = 0; i < n; i++) {
        m = i == 0 ? lmask : i == n - 1 ? rmask : 0xFF;
        s = src ? src[i] : 0;
        if (m == 0xFF) {
            switch (rop) {
            case 0xCC: d[i] = s; continue;
            case 0xF0: d[i] = pat; continue;
            case 0x00: d[i] = 0; continue;
            case 0xFF: d[i] = 0xFF; continue;
            }
            d[i] = rop3_apply(rop, pat, s, d[i]);
        } else {
            v = d[i];
            d[i] = (BYTE)((rop3_apply(rop, pat, s, v) & m) | (v & ~m));
        }
    }
}
