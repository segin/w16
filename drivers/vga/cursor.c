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
 * MoveCursor and CheckCursor at interrupt time queue their position or
 * tick, and whoever holds busy (they, or a drawing operation ending)
 * takes the whole queue before letting go.
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
static BYTE need_redraw;

/* What MoveCursor and CheckCursor bring, queued (with interrupts off for
   the few instructions it takes: irq_off) whether or not the cursor can
   be drawn now: the positions moved to, in order (the last TRAIL_MAX
   kept: older ones would have left the trail anyway), and the ticks.
   Whoever holds busy next takes them all: every position joins the trail
   and every tick takes one image off, as if each had been drawn when it
   came. Without the queue, a move that came while the cursor was being
   drawn (CheckCursor runs with interrupts on, and a mouse interrupt comes
   in the middle) kept only its last position, and a tick that came while
   MoveCursor drew was lost: the trail had gaps and doubled images and
   ran down unevenly (tests/currec.sh, the tree under real motion). */
#define MQ (TRAIL_MAX + 1)
static volatile short mqx[MQ], mqy[MQ];
static volatile BYTE mq_n, age_n;
extern WORD __cdecl __far irq_off(void);
extern void __cdecl __far irq_restore(WORD f);

/* the trail's settings (cursor_trail; vga.c keeps WIN.INI) */
static BYTE trail_on;
static BYTE trail_size = 1;
/* the earlier positions (hot spots), oldest first; px, py: where the
   cursor itself is */
static short qx[TRAIL_MAX - 1], qy[TRAIL_MAX - 1];
static BYTE qn;
static short px = 320, py = 240;
/* off after a drawing operation met the trail, or an escape: drawn alone
   by the next CheckCursor or MoveCursor */
static BYTE held;
/* how a still trail runs down: an image every decay ticks, the first on
   the first tick after a move (decay 2: seven images gone in 11 ticks,
   604 ms; cursor_decay, [w16] MouseTrailDecay) */
static BYTE decay = 2, phase;

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

/* the shape's masks moved right by 0-7 bits, five bytes a row (the AND
   mask padded with 1s: the screen kept; the XOR mask with 0s), made when
   first needed after SetCursor */
static BYTE mab[8][32][5], mxb[8][32][5];
static BYTE mvalid;                     /* bit n: shift n made */

static void masks(int sh)
{
    int r, c, k;
    DWORD a, xr;
    for (r = 0; r < 32; r++) {
        a = ((DWORD)andm[r][0] << 24) | ((DWORD)andm[r][1] << 16) |
            ((DWORD)andm[r][2] << 8) | andm[r][3];
        xr = ((DWORD)xorm[r][0] << 24) | ((DWORD)xorm[r][1] << 16) |
             ((DWORD)xorm[r][2] << 8) | xorm[r][3];
        for (c = 0; c < 5; c++) {
            k = 24 - 8 * c + sh;
            mab[sh][r][c] = (BYTE)(k >= 0 ? a >> k : a << -k);
            mxb[sh][r][c] = (BYTE)(k >= 0 ? xr >> k : xr << -k);
        }
        if (sh)
            mab[sh][r][0] |= (BYTE)(0xFF << (8 - sh));
        mab[sh][r][4] |= (BYTE)(0xFF >> sh);
    }
    mvalid |= (BYTE)(1 << sh);
}

/* an image of the shape at x, y (top left), on top of what is there, the
   bytes under it kept in save[nd]. Plane by plane: the map mask and read
   map are set four times an image, not four times a row (port writes are
   what drawing a trail of seven images at every mouse interrupt costs) */
static void draw_at(short x, short y)
{
    int r, c, p, sy, sh, bx, c0, c1, r0, r1;
    BYTE __far *l;
    BYTE v;
    BYTE (*sv)[32][5] = save[nd];
    bx = x >= 0 ? x >> 3 : -((-x + 7) >> 3);
    sh = x - bx * 8;
    if (!(mvalid & (1 << sh)))
        masks(sh);
    c0 = bx < 0 ? -bx : 0;
    c1 = bx + 5 > 80 ? 80 - bx : 5;
    r0 = y < 0 ? -y : 0;
    r1 = ch < 32 ? ch : 32;
    if (y + r1 > 480)
        r1 = 480 - y;
    scr_reset();
    for (p = 0; p < 4; p++) {
        scr_plane(p);
        for (r = r0; r < r1; r++) {
            sy = y + r;
            l = (BYTE __far *)MK_FP(vga_sel, (WORD)sy * 80 + bx);
            for (c = c0; c < c1; c++) {
                v = l[c];
                sv[p][r][c] = v;
                l[c] = (BYTE)((v & mab[sh][r][c]) ^ mxb[sh][r][c]);
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
    need_redraw = 0;
    held = 0;
    erase();
    if (!enabled || !have_shape)
        return;
    for (i = 0; i < qn; i++)
        draw_at(qx[i] - hotx, qy[i] - hoty);
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

/* the cursor moved to x, y: where it was joins the trail, unless the
   cursor is off (held, hidden) */
static void move_to(short x, short y)
{
    if (trail_active() && !held && enabled) {
        if (qn >= trail_size - 1)
            trail_pop();
        qx[qn] = px;
        qy[qn] = py;
        qn++;
    }
    px = x;
    py = y;
}

/* what the queue holds, taken (busy held by the caller): the moves, and
   with ticks set the ticks too (a drawing operation ending takes only the
   moves: after one that met the trail, the cursor stays off until a tick
   or a move); then the cursor drawn again if anything changed */
static void serve_queue(BYTE ticks)
{
    short x[MQ], y[MQ];
    BYTE n, a = 0, i, again = need_redraw;
    WORD f = irq_off();
    n = mq_n;
    for (i = 0; i < n; i++) {
        x[i] = mqx[i];
        y[i] = mqy[i];
    }
    mq_n = 0;
    if (ticks) {
        a = age_n;
        age_n = 0;
    }
    irq_restore(f);
    for (i = 0; i < n; i++) {
        move_to(x[i], y[i]);
        held = 0;
        again = 1;
    }
    if (n)
        phase = (BYTE)(decay - 1);
    if (a) {
        if (held)
            again = 1;
        while (a--) {
            if (++phase < decay)
                continue;
            phase = 0;
            if (qn) {
                trail_pop();
                again = 1;
            }
        }
    }
    if (again)
        redraw();
}

/* the queue served by whoever gets busy, until nothing is left in it */
static void serve(void)
{
    for (;;) {
        if (xchg_busy())
            return;
        serve_queue(1);
        busy = 0;
        if (!mq_n && !age_n)
            return;
    }
}

/* the queue emptied without drawing (a new shape, the screen given up):
   the cursor goes to the last position moved to */
static void drop_queue(void)
{
    WORD f = irq_off();
    if (mq_n) {
        px = mqx[mq_n - 1];
        py = mqy[mq_n - 1];
    }
    mq_n = 0;
    age_n = 0;
    irq_restore(f);
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
    serve_queue(0);
    busy = 0;
    if (mq_n)
        serve();
}

void cursor_init(void)
{
    busy = 1;
    nd = 0;
    qn = 0;
    drop_queue();
    redraw();
    busy = 0;
}

void cursor_off(void)
{
    busy = 1;
    erase();
    qn = 0;
    drop_queue();
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

/* ticks per image of a still trail's run-down (vga.c, at Enable) */
void cursor_decay(int ticks)
{
    decay = (BYTE)(ticks < 1 ? 1 : ticks > 255 ? 255 : ticks);
}

/* MoveCursor's body (entry.asm saved the registers): interrupt time */
void __cdecl __far cur_move(short x, short y)
{
    /* 3.1's driver draws a cursor moved to row 0 as on row 1, whatever
       its hot spot (VG9: MoveCursor(x, 0) and (x, 1) give the same screen,
       the arrow's tip never on the top row; x 0 is not moved) */
    WORD f;
    if (y == 0)
        y = 1;
    f = irq_off();
    if (mq_n == MQ) {
        BYTE i;
        for (i = 1; i < MQ; i++) {
            mqx[i - 1] = mqx[i];
            mqy[i - 1] = mqy[i];
        }
        mq_n--;
    }
    mqx[mq_n] = x;
    mqy[mq_n] = y;
    mq_n++;
    irq_restore(f);
    serve();
}

/* CheckCursor's body: interrupt time, every timer tick */
void __cdecl __far cur_check(void)
{
    WORD f = irq_off();
    if (age_n < 255)
        age_n++;
    irq_restore(f);
    serve();
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
    drop_queue();
    if (!cs) {
        have_shape = 0;
        busy = 0;
        return;
    }
    mvalid = 0;
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
