/*
 * cursor.c - the mouse pointer (a software cursor), Inquire, and
 * SaveScreenBitmap.
 *
 * DDK ("Cursor"; SetCursor, MoveCursor, CheckCursor): USER sets the shape
 * (a CURSORSHAPE: AND mask then XOR mask, 32 x 32 here), calls MoveCursor
 * from the mouse interrupt and CheckCursor from the timer; the driver
 * keeps the cursor off whatever it draws (excluding it) and puts it back.
 * Here a drawing operation sets busy, takes the cursor off when its
 * rectangle meets the cursor's bytes, and draws it again when it ends;
 * MoveCursor and CheckCursor at interrupt time do nothing but note the
 * position while busy is set (CheckCursor catches up on the next tick).
 *
 * Interrupt-time code: everything here and in surf.c's register helpers
 * touches only this driver's fixed data segment (tools/irqcheck.py
 * --fixed: every segment of VGA.DRV fixed).
 */
#include "vga.h"

volatile BYTE busy;

static BYTE andm[32][4], xorm[32][4];
static short hotx, hoty, cw, ch;
static BYTE have_shape;
static volatile short curx = 320, cury = 240;
static volatile BYTE pending;
static BYTE drawn, need_redraw;
static short dx0, dy0;                  /* where it is drawn: top left */
static BYTE save[4][32][5];

extern BYTE __cdecl __far xchg_busy(void);

/* the cursor's bytes on the screen: [x0, x1) x [y0, y1) in pixels, whole
   bytes */
static void footprint(short x, short *x0, short *x1)
{
    *x0 = (short)(x >= 0 ? x & ~7 : -((-x + 7) & ~7));
    *x1 = *x0 + 40;
}

static void draw_at(short x, short y)
{
    int r, c, p, sy, col, sh, bx;
    BYTE __far *l;
    DWORD a, xr;
    BYTE ab[5], xb[5], v;
    bx = x >= 0 ? x >> 3 : -((-x + 7) >> 3);
    sh = x - bx * 8;
    scr_reset();
    for (r = 0; r < ch && r < 32; r++) {
        sy = y + r;
        if (sy < 0 || sy >= 480)
            continue;
        a = ((DWORD)andm[r][0] << 24) | ((DWORD)andm[r][1] << 16) |
            ((DWORD)andm[r][2] << 8) | andm[r][3];
        xr = ((DWORD)xorm[r][0] << 24) | ((DWORD)xorm[r][1] << 16) |
             ((DWORD)xorm[r][2] << 8) | xorm[r][3];
        /* five bytes: the row's 32 bits moved right by sh, the AND
           mask padded with 1s (screen kept), the XOR mask with 0s */
        for (c = 0; c < 5; c++) {
            int k = 24 - 8 * c + sh;
            ab[c] = (BYTE)(k >= 0 ? a >> k : a << -k);
            xb[c] = (BYTE)(k >= 0 ? xr >> k : xr << -k);
        }
        if (sh)
            ab[0] |= (BYTE)(0xFF << (8 - sh));
        ab[4] |= (BYTE)(0xFF >> sh);
        for (p = 0; p < 4; p++) {
            scr_plane(p);
            l = (BYTE __far *)MK_FP(vga_sel, (WORD)sy * 80);
            for (c = 0; c < 5; c++) {
                col = bx + c;
                if (col < 0 || col >= 80)
                    continue;
                v = l[col];
                save[p][r][c] = v;
                l[col] = (BYTE)((v & ab[c]) ^ xb[c]);
            }
        }
    }
    dx0 = x;
    dy0 = y;
    drawn = 1;
}

static void erase(void)
{
    int r, c, p, sy, col, bx;
    BYTE __far *l;
    if (!drawn)
        return;
    bx = dx0 >= 0 ? dx0 >> 3 : -((-dx0 + 7) >> 3);
    scr_reset();
    for (p = 0; p < 4; p++) {
        scr_plane(p);
        for (r = 0; r < ch && r < 32; r++) {
            sy = dy0 + r;
            if (sy < 0 || sy >= 480)
                continue;
            l = (BYTE __far *)MK_FP(vga_sel, (WORD)sy * 80);
            for (c = 0; c < 5; c++) {
                col = bx + c;
                if (col >= 0 && col < 80)
                    l[col] = save[p][r][c];
            }
        }
    }
    drawn = 0;
}

/* the cursor where it should be (busy held by the caller) */
static void redraw(void)
{
    short x, y;
    pending = 0;
    erase();
    if (!enabled || !have_shape)
        return;
    x = curx - hotx;
    y = cury - hoty;
    draw_at(x, y);
    need_redraw = 0;
}

void cursor_exclude(int x0, int y0, int x1, int y1)
{
    short fx0, fx1;
    if (!drawn)
        return;
    footprint(dx0, &fx0, &fx1);
    if (x0 < fx1 && x1 > fx0 && y0 < dy0 + ch && y1 > dy0) {
        erase();
        need_redraw = 1;
    }
}

void cursor_unexclude(void)
{
    if (need_redraw || pending)
        redraw();
    busy = 0;
}

void cursor_init(void)
{
    busy = 1;
    drawn = 0;
    need_redraw = 1;
    redraw();
    busy = 0;
}

void cursor_off(void)
{
    busy = 1;
    erase();
    busy = 0;
}

/* MoveCursor's body (entry.asm saved the registers): interrupt time */
void __cdecl __far cur_move(short x, short y)
{
    curx = x;
    cury = y;
    if (xchg_busy()) {
        pending = 1;
        return;
    }
    redraw();
    busy = 0;
}

/* CheckCursor's body: interrupt time, every timer tick */
void __cdecl __far cur_check(void)
{
    if (xchg_busy())
        return;
    if (pending || need_redraw)
        redraw();
    busy = 0;
}

/* SetCursor's body */
void __cdecl __far cur_set(LPCURSORSHAPE cs)
{
    int r, c, wb;
    while (xchg_busy())
        ;
    erase();
    if (!cs) {
        have_shape = 0;
        busy = 0;
        return;
    }
    hotx = cs->csHotX;
    hoty = cs->csHotY;
    cw = cs->csWidth > 32 ? 32 : cs->csWidth;
    ch = cs->csHeight > 32 ? 32 : cs->csHeight;
    wb = cs->csWidthBytes;
    for (r = 0; r < 32; r++)
        for (c = 0; c < 4; c++) {
            andm[r][c] = r < ch && c < wb ? cs->csBits[r * wb + c] : 0xFF;
            xorm[r][c] = r < ch && c < wb ? cs->csBits[(cs->csHeight + r) * wb + c] : 0;
        }
    have_shape = 1;
    redraw();
    busy = 0;
}

/* WORD Inquire(lpCursorInfo): the mickey-to-pixel ratios (DDK
   CURSORINFO); 4 bytes (US1: USER got AX = 4) */
WORD EXPORT Inquire(short __far *ci)
{
    ci[0] = 1;
    ci[1] = 2;
    return 4;
}

/* ---- SaveScreenBitmap: the screen under a menu, kept in the video
   memory past the visible screen (mode 12h uses 38400 bytes of each
   64K plane), copied through the VGA's latches (write mode 1) ---- */
#define SAVE_BASE 38400U
#define SAVE_SIZE (65535U - SAVE_BASE)
static BYTE saved;
static RECT saverc;

void save_discard(void)
{
    saved = 0;
}

static void latch_copy(WORD from, WORD to, WORD n)
{
    BYTE __far *s = (BYTE __far *)MK_FP(vga_sel, from);
    BYTE __far *d = (BYTE __far *)MK_FP(vga_sel, to);
    volatile BYTE t;
    WORD i;
    for (i = 0; i < n; i++) {
        t = s[i];
        d[i] = t;
    }
}

/* WORD SaveScreenBitmap(lpRect, wCommand): 0 save, 1 restore, 2 discard */
WORD EXPORT SaveScreenBitmap(LPRECT rc, WORD cmd)
{
    int x0, x1, y, nb, h;
    WORD off;
    if (cmd == 2) {
        saved = 0;
        return 1;
    }
    if (!enabled)
        return 0;
    if (cmd == 0) {
        RECT r = *rc;
        if (saved)
            return 0;
        if (!clip_rect(&r, NULL, NULL) || r.left < 0 || r.top < 0)
            return 0;
        if (r.right > 640)
            r.right = 640;
        if (r.bottom > 480)
            r.bottom = 480;
        x0 = r.left >> 3;
        x1 = (r.right - 1) >> 3;
        nb = x1 - x0 + 1;
        h = r.bottom - r.top;
        if ((DWORD)nb * h > SAVE_SIZE)
            return 0;
        scr_begin(r.left, r.top, r.right, r.bottom);
        outw(0x3CE, 0x0105);            /* write mode 1: the latches */
        outw(0x3C4, 0x0F02);
        off = SAVE_BASE;
        for (y = r.top; y < r.bottom; y++, off += nb)
            latch_copy((WORD)y * 80 + x0, off, nb);
        outw(0x3CE, 0x0005);
        scr_end();
        saverc = r;
        saved = 1;
        return 1;
    }
    if (cmd == 1) {
        if (!saved)
            return 0;
        x0 = saverc.left >> 3;
        x1 = (saverc.right - 1) >> 3;
        nb = x1 - x0 + 1;
        scr_begin(saverc.left, saverc.top, saverc.right, saverc.bottom);
        outw(0x3CE, 0x0105);
        outw(0x3C4, 0x0F02);
        off = SAVE_BASE;
        for (y = saverc.top; y < saverc.bottom; y++, off += nb)
            latch_copy(off, (WORD)y * 80 + x0, nb);
        outw(0x3CE, 0x0005);
        scr_end();
        saved = 0;
        return 1;
    }
    return 0;
}
