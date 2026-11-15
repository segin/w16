/*
 * cursor.c - the mouse pointer (a software cursor) and its trail, Inquire,
 * and SaveScreenBitmap.
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
 * Mouse trails (DDK "Mouse Trails", the MOUSETRAILS escape: vga.c
 * Control), as Windows 3.1's VGA.DRV draws them (VG9, tests/apps/vg9.c:
 * its MoveCursor and CheckCursor called directly, the screen read back
 * from the VGA's memory; drivers/vga/README.md):
 * - the trail is the cursor's earlier positions: every MoveCursor adds
 *   the position the cursor had (also when it does not change), at most
 *   trail size - 1 of them (the oldest goes); every CheckCursor takes the
 *   oldest away, so the trail runs down one image a tick once the pointer
 *   stops;
 * - the images are drawn oldest first, each over the ones before it, all
 *   of the current shape;
 * - no trail for a cursor whose AND mask has no 0 bit (XOR only: the
 *   I-beam), as the DDK says;
 * - SetCursor (a new shape, or none: ShowCursor) ends the trail: the new
 *   shape is drawn alone at once;
 * - a drawing operation that meets any image takes the whole trail and
 *   the cursor off, and they stay off (the trail forgotten) until the next
 *   CheckCursor or MoveCursor draws the cursor alone; drawing that meets
 *   none leaves them;
 * - an escape that changes the trail (cursor_trail) takes everything off
 *   the same way.
 * With trails off (or a trail size of 1) the cursor is excluded and drawn
 * again exactly as before.
 *
 * Interrupt-time code: everything here and in surf.c's register helpers
 * touches only this driver's fixed data segment (tools/irqcheck.py
 * --fixed: every segment of VGA.DRV fixed).
 */
#include "vga.h"

volatile BYTE busy;

static BYTE andm[32][4], xorm[32][4];
static short hotx, hoty, cw, ch;
static BYTE have_shape, xor_only;
static volatile short curx = 320, cury = 240;
static volatile BYTE pending;
static BYTE need_redraw;

/* the trail's settings (cursor_trail; vga.c keeps WIN.INI) */
static BYTE trail_on;
static BYTE trail_size = 1;
/* the earlier positions (hot spots), oldest first; px, py: where the
   cursor itself was last drawn */
static short qx[TRAIL_MAX - 1], qy[TRAIL_MAX - 1];
static BYTE qn;
static short px = 320, py = 240;
/* off after a drawing operation met the trail, or an escape: drawn alone
   by the next CheckCursor or MoveCursor */
static BYTE held;

/* the images on the screen, oldest first: top left, and the screen bytes
   under each (four planes, 32 rows, 5 bytes) */
static BYTE nd;
static short ix[TRAIL_MAX], iy[TRAIL_MAX];
static BYTE save[TRAIL_MAX][4][32][5];

extern BYTE __cdecl __far xchg_busy(void);

/* the cursor's bytes on the screen: [x0, x1) x [y0, y1) in pixels, whole
   bytes */
static void footprint(short x, short *x0, short *x1)
{
    *x0 = (short)(x >= 0 ? x & ~7 : -((-x + 7) & ~7));
    *x1 = *x0 + 40;
}

static int trail_active(void)
{
    return trail_on && trail_size > 1 && have_shape && !xor_only;
}

static void draw_at(short x, short y)
{
    int r, c, p, sy, col, sh, bx;
    BYTE __far *l;
    DWORD a, xr;
    BYTE ab[5], xb[5], v;
    BYTE (*sv)[32][5] = save[nd];
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
                sv[p][r][c] = v;
                l[col] = (BYTE)((v & ab[c]) ^ xb[c]);
            }
        }
    }
    ix[nd] = x;
    iy[nd] = y;
    nd++;
}

/* every image off, the newest first */
static void erase(void)
{
    int r, c, p, sy, col, bx;
    BYTE __far *l;
    if (!nd)
        return;
    scr_reset();
    while (nd) {
        BYTE (*sv)[32][5];
        nd--;
        sv = save[nd];
        bx = ix[nd] >= 0 ? ix[nd] >> 3 : -((-ix[nd] + 7) >> 3);
        for (p = 0; p < 4; p++) {
            scr_plane(p);
            for (r = 0; r < ch && r < 32; r++) {
                sy = iy[nd] + r;
                if (sy < 0 || sy >= 480)
                    continue;
                l = (BYTE __far *)MK_FP(vga_sel, (WORD)sy * 80);
                for (c = 0; c < 5; c++) {
                    col = bx + c;
                    if (col >= 0 && col < 80)
                        l[col] = sv[p][r][c];
                }
            }
        }
    }
}

/* the cursor and its trail where they should be (busy held by the
   caller) */
static void redraw(void)
{
    int i;
    pending = 0;
    need_redraw = 0;
    held = 0;
    erase();
    if (!enabled || !have_shape)
        return;
    for (i = 0; i < qn; i++)
        draw_at(qx[i] - hotx, qy[i] - hoty);
    px = curx;
    py = cury;
    draw_at(px - hotx, py - hoty);
}

/* the oldest earlier position dropped */
static void trail_pop(void)
{
    int i;
    if (!qn)
        return;
    qn--;
    for (i = 0; i < qn; i++) {
        qx[i] = qx[i + 1];
        qy[i] = qy[i + 1];
    }
}

/* the cursor moved (MoveCursor, now or noted while busy): the position
   it was drawn at joins the trail, unless the cursor is off (held) */
static void trail_push(void)
{
    if (trail_active() && !held && nd) {
        if (qn >= trail_size - 1)
            trail_pop();
        qx[qn] = px;
        qy[qn] = py;
        qn++;
    }
}

static void moved(void)
{
    trail_push();
    redraw();
}

/* everything off, the trail forgotten; drawn alone by the next
   CheckCursor or MoveCursor */
static void hold(void)
{
    erase();
    qn = 0;
    held = 1;
}

void cursor_exclude(int x0, int y0, int x1, int y1)
{
    short fx0, fx1;
    int i;
    for (i = 0; i < nd; i++) {
        footprint(ix[i], &fx0, &fx1);
        if (x0 < fx1 && x1 > fx0 && y0 < iy[i] + ch && y1 > iy[i])
            break;
    }
    if (i == nd)
        return;
    if (trail_active())
        hold();
    else {
        erase();
        need_redraw = 1;
    }
}

void cursor_unexclude(void)
{
    if (pending)
        moved();
    else if (need_redraw)
        redraw();
    busy = 0;
}

void cursor_init(void)
{
    busy = 1;
    nd = 0;
    qn = 0;
    redraw();
    busy = 0;
}

void cursor_off(void)
{
    busy = 1;
    erase();
    qn = 0;
    busy = 0;
}

/* the trail turned on or off, or its size set (Control, MOUSETRAILS):
   everything off until the next CheckCursor or MoveCursor */
void cursor_trail(int on, int size)
{
    while (xchg_busy())
        ;
    hold();
    trail_on = (BYTE)(on != 0);
    trail_size = (BYTE)size;
    busy = 0;
}

/* MoveCursor's body (entry.asm saved the registers): interrupt time */
void __cdecl __far cur_move(short x, short y)
{
    /* 3.1's driver draws a cursor moved to row 0 as on row 1, whatever
       its hot spot (VG9: MoveCursor(x, 0) and (x, 1) give the same screen,
       the arrow's tip never on the top row; x 0 is not moved) */
    if (y == 0)
        y = 1;
    curx = x;
    cury = y;
    if (xchg_busy()) {
        pending = 1;
        return;
    }
    moved();
    busy = 0;
}

/* CheckCursor's body: interrupt time, every timer tick */
void __cdecl __far cur_check(void)
{
    BYTE again = 0;
    if (xchg_busy())
        return;
    if (pending) {
        trail_push();
        again = 1;
    }
    if (held || need_redraw)
        again = 1;
    else if (qn) {
        trail_pop();
        again = 1;
    }
    if (again)
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
    qn = 0;
    held = 0;
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
    xor_only = 1;
    for (r = 0; r < 32; r++)
        for (c = 0; c < 4; c++) {
            andm[r][c] = r < ch && c < wb ? cs->csBits[r * wb + c] : 0xFF;
            xorm[r][c] = r < ch && c < wb ? cs->csBits[(cs->csHeight + r) * wb + c] : 0;
            if (andm[r][c] != 0xFF && c * 8 < cw)
                xor_only = 0;
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
