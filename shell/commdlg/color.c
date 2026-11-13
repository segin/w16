/*
 * color.c - w16 COMMDLG: ChooseColor, the Color dialog (template
 * "CHOOSECOLOR", ColorDlgProc) and WantArrows, the procedure of its two
 * colour boxes (observed: the basic and custom colour controls, 2D0h and
 * 2D1h, have it).
 *
 * Observed on Windows 3.1 (tests/scripts/commdlg/color*.txt,
 * ARCHITECTURE.md 17):
 *  - Checks: a wrong lStructSize fails without touching the extended error;
 *    CC_ENABLEHOOK without a hook CDERR_NOHOOK (Bh); CC_ENABLETEMPLATE
 *    without a name CDERR_NOTEMPLATE (3). A call that succeeds leaves the
 *    error as it was.
 *  - The dialog is made full width and moved to the screen's top left
 *    corner (observed with the owner there; UNVERIFIED elsewhere); without
 *    CC_FULLOPEN it is cut to the left part (its right edge 4 dialog units
 *    left of the colour field) and the right-hand controls are disabled;
 *    "Define Custom Colors..." widens it, fills the edits (Red Green Blue
 *    Hue Sat Lum) and gives Hue the focus. CC_PREVENTFULLOPEN disables that
 *    button, CC_SHOWHELP shows Help. The hidden "&o" button (Alt+O, the
 *    "S&olid" label) is no tab stop; it takes the nearest solid colour.
 *  - The hook sees every message, the set-up's EN_UPDATE/EN_CHANGE pairs
 *    before WM_INITDIALOG (full: the edits filled twice, Red..Blue then
 *    Hue..Lum), not WM_DESTROY. Without a hook WM_INITDIALOG returns TRUE
 *    (the basic colours get the focus); with one, the hook's answer (CD1's
 *    FALSE: no control has the focus).
 *  - Colours are HLS with 240 steps (hue 0-239, saturation and luminosity
 *    0-240): the textbook model, its rounding fitted to the original by
 *    observation (tests/scripts/commdlg/hls1.txt and hls1.table; see the
 *    HLS section below).
 *  - Edits: a number takes effect as typed (Red/Green/Blue update
 *    Hue/Sat/Lum, and the other way round), above the maximum it becomes
 *    the maximum; a non-digit puts the old value back at once, an empty edit
 *    when it loses the focus. A chosen box or Solid updates Hue Sat Lum, then
 *    Red Green Blue. Three characters at most.
 *  - The colour field: blocks of 4 hue by 8 saturation steps (luminosity
 *    120), each block the colour of its top left corner - found pixel for
 *    pixel against the display driver's dithering of every field colour.
 *    A colour's cross is at x = hue * 205 / 239, y = (240 - sat) * 186 /
 *    240 from the field's inner top left corner (205 x 187 here): black,
 *    arms 5 long and 3 wide around a 9-pixel gap, clipped to the field.
 *  - The luminosity bar: 23 pixels wide from its static control's left
 *    edge, its height, framed; blocks of 8 luminosity steps (see
 *    draw_bar). The arrow's tip: bar bottom - 1 - ceil(lum * (height - 1) /
 *    240), two pixels right of the bar.
 *  - The sample: its left half the colour (dithered), its right half the
 *    nearest solid colour.
 *  - Basic colours (observed: each chosen and returned) 8 x 6 boxes, custom
 *    8 x 2, 24 x 17 pixels each, 29 and 22 apart, 3 pixels into their
 *    control (UNVERIFIED: how the original derives these for other dialog
 *    fonts); the chosen box one pixel bigger and framed twice, the focus
 *    box (while its control has the focus) in a focus rectangle 3 pixels
 *    out. Arrow keys move the focus box, Space chooses it. A colour that is
 *    no box's frames the first basic box but stays as it is.
 *  - Add to Custom Colors: into the custom focus box and at once into the
 *    caller's lpCustColors (kept on Cancel); the focus box then moves down,
 *    then to the next column (0, 8, 1, 9, ...).
 *  - OK: COLOROKSTRING to the hook (wParam 0, lParam the CHOOSECOLOR),
 *    which can refuse it; rgbResult the colour as it is. Help:
 *    HELPMSGSTRING to the owner (wParam the dialog).
 * UNVERIFIED: mouse clicks in the field and the bar (the inverse of the
 * mappings above), SETRGBSTRING (handled as a chosen colour), the arrow
 * keys at a box's edge (they stop).
 */
#include "commdlg.h"


#define HLSMAX          240
#define RGBMAX          255
#define HUE_UNDEFINED   (HLSMAX * 2 / 3)

#define NBASIC          48
#define NCUSTOM         16              /* format-defined: lpCustColors */
#define COLS            8

/* box geometry (pixels, observed with the dialog's 8-point Helv) */
#define CELL_X0         3
#define CELL_Y0         3
#define CELL_DX         29
#define CELL_DY         22
#define CELL_W          24
#define CELL_H          17

/* the 48 basic colours, row by row (observed: each chosen and returned) */
static const COLORREF basic[NBASIC] = {
    0x8080FFL, 0xE8FFFFL, 0x80FF80L, 0x80FF00L, 0xFFFF80L, 0xFF8000L, 0xC080FFL, 0xFF80FFL,
    0x0000FFL, 0x80FFFFL, 0x00FF80L, 0x40FF00L, 0xFFFF00L, 0xC08000L, 0xC08080L, 0xFF00FFL,
    0x404080L, 0x00FFFFL, 0x00FF00L, 0x808000L, 0x804000L, 0xFF8080L, 0x400080L, 0x8000FFL,
    0x000080L, 0x0080FFL, 0x008000L, 0x408000L, 0xFF0000L, 0xA00000L, 0x800080L, 0xFF0080L,
    0x000040L, 0x004080L, 0x004000L, 0x404000L, 0x800000L, 0x400000L, 0x400040L, 0x800040L,
    0x000000L, 0x008080L, 0x408080L, 0x808080L, 0x808040L, 0xC0C0C0L, 0x828282L, 0xFFFFFFL,
};

typedef struct {
    LPCHOOSECOLOR cc;
    HWND dlg;
    cd_hook hook;
    BOOL full;
    COLORREF rgb;                       /* the current colour */
    int h, s, l;
    COLORREF cust[NCUSTOM];
    int sel_box, sel;                   /* the chosen box: IDC_BASIC/CUSTOM, index */
    int foc_basic, foc_cust;            /* the focus box in each */
    BOOL updating;                      /* setting the edits ourselves */
    HBITMAP field;                      /* the colour field, drawn once */
    RECT rfield, rbar, rsample;         /* inner rectangles, dialog client */
    int small_w, full_w;
} cstate;

/* ---- HLS ----
 * The conversion is the textbook HLS model (lightness the mean of the
 * largest and smallest channel, saturation the channels' spread relative
 * to it, hue the position on the six-sector colour circle), on Windows'
 * scales: hue 0-239 (40 per sector), saturation and lightness 0-240.
 * Its integer rounding was fitted to the original dialog by observation
 * alone: tests/scripts/commdlg/hls1.txt types 6597 values into the 3.1
 * dialog (CD1's colour probe) and hls1.table records what it showed; the
 * rules below reproduce every entry. Observed rules: lightness, saturation
 * and channels round to nearest; the saturation's divisor follows the
 * rounded lightness (up to 120: max + min, above: 510 - max - min); the
 * hue is the difference of the two other channels' distances from the
 * largest, each scaled to 40 and rounded on its own; a gray has hue 160
 * and its channels are truncated (240ths to 255ths); in HLS to RGB the
 * colour's top and bottom channel values (in 240ths) are rounded first,
 * then each sector's rising or falling channel along that range. */

/* (a * b + k) / d in 32 bits, the quotient toward zero (no C runtime:
   the processor's IMUL and IDIV); every use here fits in 16 bits */
static int mad(int a, int b, int k, int d)
{
    int q = 0;
    _asm {
        mov ax, a
        imul word ptr b             ; dx:ax = a * b
        mov bx, ax
        mov cx, dx
        mov ax, k
        cwd                         ; + k, sign-extended
        add ax, bx
        adc dx, cx
        idiv word ptr d
        mov q, ax
    }
    return q;
}

/* a * b / d to the nearest integer (a, b >= 0, d > 0) */
static int scale_round(int a, int b, int d)
{
    return mad(a, b, d / 2, d);
}

static void rgb_to_hls(COLORREF c, int FAR *ph, int FAR *pl, int FAR *ps)
{
    int ch[3], hi = 0, lo = 0, i, spread, l, s, h;
    ch[0] = GetRValue(c);
    ch[1] = GetGValue(c);
    ch[2] = GetBValue(c);
    for (i = 1; i < 3; i++) {
        if (ch[i] > ch[hi])
            hi = i;
        if (ch[i] < ch[lo])
            lo = i;
    }
    spread = ch[hi] - ch[lo];
    l = scale_round(ch[hi] + ch[lo], HLSMAX, 2 * RGBMAX);
    if (spread == 0) {
        *ph = HUE_UNDEFINED;
        *ps = 0;
        *pl = l;
        return;
    }
    s = scale_round(spread, HLSMAX,
                    l <= HLSMAX / 2 ? ch[hi] + ch[lo] : 2 * RGBMAX - ch[hi] - ch[lo]);
    {
        /* the sector starts at 0 (red), 80 (green) or 160 (blue); the two
           other channels' distances from the top pull the hue either way */
        int next = (hi + 1) % 3, prev = (hi + 2) % 3;
        int dn = scale_round(ch[hi] - ch[next], HLSMAX / 6, spread);
        int dp = scale_round(ch[hi] - ch[prev], HLSMAX / 6, spread);
        h = hi * (HLSMAX / 3) + dp - dn;
    }
    if (h < 0)
        h += HLSMAX;
    if (h >= HLSMAX)
        h -= HLSMAX;
    *ph = h;
    *pl = l;
    *ps = s;
}

static COLORREF hls_to_rgb(int h, int l, int s)
{
    int top, bot, span, sector, pos, up, down, v[3], i;
    if (s == 0) {
        int g = (int)(((unsigned)l * RGBMAX) / HLSMAX);
        return RGB(g, g, g);
    }
    /* the largest and smallest channel, in 240ths */
    if (l <= HLSMAX / 2)
        top = scale_round(l, HLSMAX + s, HLSMAX);
    else
        top = l + s - scale_round(l, s, HLSMAX);
    bot = 2 * l - top;
    span = top - bot;
    h %= HLSMAX;
    sector = h / (HLSMAX / 6);
    pos = h % (HLSMAX / 6);
    up = bot + scale_round(span, pos, HLSMAX / 6);
    down = bot + scale_round(span, HLSMAX / 6 - pos, HLSMAX / 6);
    switch (sector) {
    case 0: v[0] = top;  v[1] = up;   v[2] = bot;  break;    /* red to yellow */
    case 1: v[0] = down; v[1] = top;  v[2] = bot;  break;    /* to green */
    case 2: v[0] = bot;  v[1] = top;  v[2] = up;   break;    /* to cyan */
    case 3: v[0] = bot;  v[1] = down; v[2] = top;  break;    /* to blue */
    case 4: v[0] = up;   v[1] = bot;  v[2] = top;  break;    /* to magenta */
    default: v[0] = top; v[1] = bot;  v[2] = down; break;    /* back to red */
    }
    for (i = 0; i < 3; i++)
        v[i] = scale_round(v[i], RGBMAX, HLSMAX);
    return RGB(v[0], v[1], v[2]);
}

/* ---- geometry ---- */

static void ctl_rect(HWND dlg, int id, RECT FAR *r)
{
    HWND w = GetDlgItem(dlg, id);
    GetWindowRect(w, r);
    ScreenToClient(dlg, (LPPOINT)&r->left);
    ScreenToClient(dlg, (LPPOINT)&r->right);
}

static void cell_rect(cstate FAR *st, int box, int i, RECT FAR *r)
{
    RECT b;
    ctl_rect(st->dlg, box, &b);
    r->left = b.left + CELL_X0 + (i % COLS) * CELL_DX;
    r->top = b.top + CELL_Y0 + (i / COLS) * CELL_DY;
    r->right = r->left + CELL_W;
    r->bottom = r->top + CELL_H;
}

static int field_x(cstate FAR *st, int h)
{
    return st->rfield.left + mad(h, st->rfield.right - st->rfield.left, 0, HLSMAX - 1);
}

static int field_y(cstate FAR *st, int v)
{
    return st->rfield.top + mad(HLSMAX - v, st->rfield.bottom - st->rfield.top - 1, 0, HLSMAX);
}

/* ---- drawing ---- */

static void fill(HDC dc, const RECT FAR *r, COLORREF c)
{
    HBRUSH b = CreateSolidBrush(c);
    FillRect(dc, r, b);
    DeleteObject(b);
}

static void frame(HDC dc, const RECT FAR *r)
{
    HBRUSH b = GetStockObject(BLACK_BRUSH);
    FrameRect(dc, r, b);
}

/* one box: framed in black; the chosen one one pixel bigger, framed
   twice; the focus one with a focus rectangle around */
static void draw_cell(cstate FAR *st, HDC dc, int box, int i, BOOL erase)
{
    RECT r, o;
    COLORREF c = box == IDC_BASIC ? basic[i] : st->cust[i];
    BOOL chosen = st->sel_box == box && st->sel == i;
    BOOL focus = GetFocus() == GetDlgItem(st->dlg, box) &&
                 (box == IDC_BASIC ? st->foc_basic : st->foc_cust) == i;
    cell_rect(st, box, i, &r);
    if (erase) {
        o = r;
        InflateRect(&o, 3, 3);
        fill(dc, &o, GetSysColor(COLOR_WINDOW));
    }
    if (chosen) {
        o = r;
        InflateRect(&o, 1, 1);
        frame(dc, &o);
        InflateRect(&o, -1, -1);
        frame(dc, &o);
        InflateRect(&o, -1, -1);
        fill(dc, &o, c);
    } else {
        frame(dc, &r);
        InflateRect(&r, -1, -1);
        fill(dc, &r, c);
        InflateRect(&r, 1, 1);
    }
    if (focus) {
        o = r;
        InflateRect(&o, 3, 3);
        DrawFocusRect(dc, &o);
    }
}

static void draw_box(cstate FAR *st, HDC dc, int box)
{
    int i, n = box == IDC_BASIC ? NBASIC : NCUSTOM;
    for (i = 0; i < n; i++)
        draw_cell(st, dc, box, i, FALSE);
}

static void redraw_cell(cstate FAR *st, int box, int i)
{
    HDC dc;
    if (box != IDC_BASIC && box != IDC_CUSTOM)
        return;
    dc = GetDC(st->dlg);
    draw_cell(st, dc, box, i, TRUE);
    ReleaseDC(st->dlg, dc);
}

/* The colour field: blocks of 4 hue by 8 saturation steps, each in the
   colour of its top left corner (luminosity 120), from saturation 240 down
   and hue 0 right (observed: pixel for pixel, the display driver's
   dithering of each block's brush); drawn once into a bitmap. */
#define SAT_STEP        8
#define HUE_STEP        4

static void make_field(cstate FAR *st, HDC dc)
{
    int w = st->rfield.right - st->rfield.left, h = st->rfield.bottom - st->rfield.top;
    int hue, sat;
    HDC mem;
    HBITMAP old;
    RECT r;
    if (st->field)
        return;
    st->field = CreateCompatibleBitmap(dc, w, h);
    mem = CreateCompatibleDC(dc);
    if (!st->field || !mem) {
        if (mem)
            DeleteDC(mem);
        return;
    }
    old = SelectObject(mem, st->field);
    for (sat = HLSMAX; sat > 0; sat -= SAT_STEP) {
        r.top = mad(HLSMAX - sat, h, 0, HLSMAX);
        r.bottom = mad(HLSMAX - sat + SAT_STEP, h, 0, HLSMAX);
        for (hue = 0; hue < HLSMAX; hue += HUE_STEP) {
            r.left = mad(hue, w, 0, HLSMAX);
            r.right = mad(hue + HUE_STEP, w, 0, HLSMAX);
            fill(mem, &r, hls_to_rgb(hue, HLSMAX / 2, sat));
        }
    }
    SelectObject(mem, old);
    DeleteDC(mem);
}

static void draw_cross(cstate FAR *st, HDC dc)
{
    int cx = field_x(st, st->h), cy = field_y(st, st->s), k;
    RECT r;
    HRGN clip = CreateRectRgn(st->rfield.left, st->rfield.top, st->rfield.right,
                             st->rfield.bottom);
    SelectClipRgn(dc, clip);
    for (k = 0; k < 4; k++) {
        static const int dx[4] = { -9, 5, -1, -1 }, dy[4] = { -1, -1, -9, 5 };
        static const int w[4] = { 5, 5, 3, 3 }, hh[4] = { 3, 3, 5, 5 };
        r.left = cx + dx[k];
        r.top = cy + dy[k];
        r.right = r.left + w[k];
        r.bottom = r.top + hh[k];
        FillRect(dc, &r, GetStockObject(BLACK_BRUSH));
    }
    SelectClipRgn(dc, NULL);
    DeleteObject(clip);
}

static void draw_field(cstate FAR *st, HDC dc)
{
    HDC mem;
    HBITMAP old;
    make_field(st, dc);
    mem = CreateCompatibleDC(dc);
    if (st->field && mem) {
        old = SelectObject(mem, st->field);
        BitBlt(dc, st->rfield.left, st->rfield.top, st->rfield.right - st->rfield.left,
               st->rfield.bottom - st->rfield.top, mem, 0, 0, SRCCOPY);
        SelectObject(mem, old);
    }
    if (mem)
        DeleteDC(mem);
    draw_cross(st, dc);
}

/* The luminosity bar: blocks of 8 luminosity steps from 240 (top) down
   to 0, each in its value's colour. Observed (each row's colour against
   the display driver's dithering of every luminosity, for two colours):
   block l starts n - 3 - (l * n + 60) / 240 rows below the bar's top (n:
   the bar's height, 189 rows here), the topmost cut at the top; reproduced
   as found (UNVERIFIED: the original's own formula for other heights). */
#define LUM_STEP        8

static int bar_row(int l, int n)
{
    int y = n - 3 - mad(l, n, 60, HLSMAX);
    return y < 0 ? 0 : y > n ? n : y;
}

static void draw_bar(cstate FAR *st, HDC dc)
{
    RECT r, o;
    int lum, n = st->rbar.bottom - st->rbar.top;
    o = st->rbar;
    InflateRect(&o, 1, 1);
    frame(dc, &o);
    r = st->rbar;
    for (lum = HLSMAX; lum >= 0; lum -= LUM_STEP) {
        r.top = st->rbar.top + bar_row(lum, n);
        r.bottom = lum ? st->rbar.top + bar_row(lum - LUM_STEP, n) : st->rbar.bottom;
        fill(dc, &r, hls_to_rgb(st->h, lum, st->s));
    }
}

static void draw_arrow(cstate FAR *st, HDC dc, BOOL erase)
{
    POINT p[3];
    RECT r;
    /* observed: the tip at bar bottom - 1 - ceil(lum * (n - 1) / 240) */
    int n1 = st->rbar.bottom - st->rbar.top - 1;
    int x = st->rbar.right + 2, y = st->rbar.top + n1 - mad(st->l, n1, HLSMAX - 1, HLSMAX);
    HPEN op;
    HBRUSH ob;
    if (erase) {
        r.left = x;
        r.top = st->rbar.top - 6;
        r.right = x + 8;
        r.bottom = st->rbar.bottom + 6;
        fill(dc, &r, GetSysColor(COLOR_WINDOW));
    }
    p[0].x = x + 6;
    p[0].y = y - 5;
    p[1].x = x;
    p[1].y = y;
    p[2].x = x + 6;
    p[2].y = y + 5;
    op = SelectObject(dc, GetStockObject(BLACK_PEN));
    ob = SelectObject(dc, GetStockObject(WHITE_BRUSH));
    Polygon(dc, p, 3);
    SelectObject(dc, op);
    SelectObject(dc, ob);
}

static void draw_sample(cstate FAR *st, HDC dc)
{
    RECT r = st->rsample;
    int mid = r.left + (r.right - r.left) / 2;
    r.right = mid;
    fill(dc, &r, st->rgb);
    r.left = mid;
    r.right = st->rsample.right;
    fill(dc, &r, GetNearestColor(dc, st->rgb));
}

/* ---- the current colour ---- */

static void set_edit(cstate FAR *st, int id, int v)
{
    char b[8];
    s_itoa(v, b);
    SetDlgItemText(st->dlg, id, b);
}

/* the edits, in the orders observed (each SetDlgItemText is an
   EN_UPDATE/EN_CHANGE pair the hook sees); only in the full dialog */
static void show_rgb(cstate FAR *st)
{
    if (!st->full)
        return;
    st->updating = TRUE;
    set_edit(st, IDC_RED, GetRValue(st->rgb));
    set_edit(st, IDC_GREEN, GetGValue(st->rgb));
    set_edit(st, IDC_BLUE, GetBValue(st->rgb));
    st->updating = FALSE;
}

static void show_hls(cstate FAR *st)
{
    if (!st->full)
        return;
    st->updating = TRUE;
    set_edit(st, IDC_HUE, st->h);
    set_edit(st, IDC_SAT, st->s);
    set_edit(st, IDC_LUM, st->l);
    st->updating = FALSE;
}

/* a colour chosen (a box, Solid): Hue Sat Lum, then Red Green Blue */
static void show_values(cstate FAR *st)
{
    show_hls(st);
    show_rgb(st);
}

static void repaint_right(cstate FAR *st, BOOL field, BOOL bar)
{
    HDC dc;
    if (!st->full)
        return;
    dc = GetDC(st->dlg);
    if (field)
        draw_field(st, dc);
    if (bar) {
        draw_bar(st, dc);
        draw_arrow(st, dc, TRUE);
    }
    draw_sample(st, dc);
    ReleaseDC(st->dlg, dc);
}

/* a new colour from RGB (h, l, s recomputed) */
static void set_rgb(cstate FAR *st, COLORREF c, BOOL edits)
{
    int oh = st->h, os = st->s;
    st->rgb = c;
    rgb_to_hls(c, &st->h, &st->l, &st->s);
    if (edits)
        show_values(st);
    repaint_right(st, oh != st->h || os != st->s, TRUE);
}

/* a new colour from HLS */
static void set_hls(cstate FAR *st, int h, int l, int s, BOOL edits)
{
    BOOL hs = h != st->h || s != st->s;
    st->h = h;
    st->l = l;
    st->s = s;
    st->rgb = hls_to_rgb(h, l, s);
    if (edits)
        show_values(st);
    repaint_right(st, hs, TRUE);
}

static void choose_cell(cstate FAR *st, int box, int i)
{
    int ob = st->sel_box, oi = st->sel;
    st->sel_box = box;
    st->sel = i;
    redraw_cell(st, ob, oi);
    redraw_cell(st, box, i);
    set_rgb(st, box == IDC_BASIC ? basic[i] : st->cust[i], TRUE);
}

/* ---- the colour boxes' procedure ---- */

LRESULT CDPROC WantArrows(HWND w, UINT msg, WPARAM wp, LPARAM lp)
{
    cstate FAR *st = (cstate FAR *)state_get(GetParent(w));
    int box = GetWindowWord(w, GWW_ID);
    if (msg == WM_GETDLGCODE)
        return DLGC_WANTARROWS;
    if (st && (msg == WM_SETFOCUS || msg == WM_KILLFOCUS)) {
        LRESULT r = call_old(w, msg, wp, lp);
        redraw_cell(st, box, box == IDC_BASIC ? st->foc_basic : st->foc_cust);
        return r;
    }
    if (st && msg == WM_KEYDOWN) {
        int FAR *f = box == IDC_BASIC ? &st->foc_basic : &st->foc_cust;
        int n = box == IDC_BASIC ? NBASIC : NCUSTOM, o = *f, k = o;
        switch (wp) {
        case VK_LEFT: if (k % COLS) k--; break;
        case VK_RIGHT: if (k % COLS < COLS - 1) k++; break;
        case VK_UP: if (k >= COLS) k -= COLS; break;
        case VK_DOWN: if (k + COLS < n) k += COLS; break;
        case VK_SPACE:
            choose_cell(st, box, k);
            return 0;
        default:
            return call_old(w, msg, wp, lp);
        }
        if (k != o) {
            *f = k;
            redraw_cell(st, box, o);
            redraw_cell(st, box, k);
        }
        return 0;
    }
    if (st && msg == WM_PAINT) {
        PAINTSTRUCT ps;
        HDC dc;
        BeginPaint(w, &ps);
        EndPaint(w, &ps);
        dc = GetDC(st->dlg);
        draw_box(st, dc, box);
        ReleaseDC(st->dlg, dc);
        return 0;
    }
    return call_old(w, msg, wp, lp);
}

/* ---- the dialog ---- */

static void show_full(cstate FAR *st, BOOL full)
{
    static const int ids[] = { IDC_HUE, IDC_SAT, IDC_LUM, IDC_RED, IDC_GREEN, IDC_BLUE,
        IDC_HUELBL, IDC_SATLBL, IDC_LUMLBL, IDC_REDLBL, IDC_GREENLBL, IDC_BLUELBL,
        IDC_ADDCUSTOM, IDC_SOLIDBTN, 0 };
    RECT r;
    int i;
    st->full = full;
    for (i = 0; ids[i]; i++)
        EnableWindow(GetDlgItem(st->dlg, ids[i]), full);
    GetWindowRect(st->dlg, &r);
    SetWindowPos(st->dlg, NULL, 0, 0, full ? st->full_w : st->small_w, r.bottom - r.top,
                 SWP_NOZORDER | SWP_NOACTIVATE);
    if (full) {
        EnableWindow(GetDlgItem(st->dlg, IDC_CUSTDEFINE), FALSE);
        show_rgb(st);
        show_hls(st);
    }
}

static void init(cstate FAR *st)
{
    LPCHOOSECOLOR cc = st->cc;
    HWND dlg = st->dlg;
    RECT r, d;
    int i;
    for (i = 0; i < NCUSTOM; i++)
        st->cust[i] = cc->lpCustColors ? cc->lpCustColors[i] : RGB(255, 255, 255);
    ctl_rect(dlg, IDC_SPECTRUM, &st->rfield);
    InflateRect(&st->rfield, -1, -1);
    ctl_rect(dlg, IDC_LUMBAR, &r);
    st->rbar.left = r.left;
    st->rbar.right = r.left + 23;
    st->rbar.top = r.top;
    st->rbar.bottom = r.bottom;
    ctl_rect(dlg, IDC_SAMPLE, &st->rsample);
    InflateRect(&st->rsample, -1, -1);
    GetWindowRect(dlg, &d);
    GetWindowRect(GetDlgItem(dlg, IDC_SPECTRUM), &r);
    st->full_w = d.right - d.left;
    {   /* 4 dialog units: the left margin of the controls */
        RECT m;
        m.left = m.top = 0;
        m.right = 4;
        m.bottom = 0;
        MapDialogRect(dlg, &m);
        st->small_w = r.left - d.left - m.right;
    }
    st->rgb = (cc->Flags & CC_RGBINIT) ? cc->rgbResult : RGB(0, 0, 0);
    rgb_to_hls(st->rgb, &st->h, &st->l, &st->s);
    st->sel_box = 0;
    st->sel = -1;
    for (i = 0; i < NBASIC; i++)
        if (basic[i] == st->rgb) {
            st->sel_box = IDC_BASIC;
            st->sel = i;
            break;
        }
    if (st->sel < 0)
        for (i = 0; i < NCUSTOM; i++)
            if (st->cust[i] == st->rgb) {
                st->sel_box = IDC_CUSTOM;
                st->sel = i;
                break;
            }
    if (st->sel < 0) {                  /* observed: the first box framed,
                                           the colour kept */
        st->sel_box = IDC_BASIC;
        st->sel = 0;
    }
    st->foc_basic = st->sel_box == IDC_BASIC ? st->sel : 0;
    st->foc_cust = st->sel_box == IDC_CUSTOM ? st->sel : 0;
    if (!(cc->Flags & CC_SHOWHELP)) {
        ShowWindow(GetDlgItem(dlg, pshHelp), SW_HIDE);
        EnableWindow(GetDlgItem(dlg, pshHelp), FALSE);
    }
    if (cc->Flags & CC_PREVENTFULLOPEN)
        EnableWindow(GetDlgItem(dlg, IDC_CUSTDEFINE), FALSE);
    for (i = 0; i < 6; i++)
        SendDlgItemMessage(dlg, IDC_HUE + i, EM_LIMITTEXT, 3, 0);
    subclass(GetDlgItem(dlg, IDC_BASIC), (FARPROC)WantArrows);
    subclass(GetDlgItem(dlg, IDC_CUSTOM), (FARPROC)WantArrows);
    {   /* observed: the hidden Solid button is not a tab stop */
        HWND o = GetDlgItem(dlg, IDC_SOLIDBTN);
        SetWindowLong(o, GWL_STYLE, GetWindowLong(o, GWL_STYLE) & ~WS_TABSTOP);
    }
    show_full(st, (cc->Flags & CC_FULLOPEN) && !(cc->Flags & CC_PREVENTFULLOPEN));
    if (st->full) {                     /* observed: the edits filled twice */
        show_rgb(st);
        show_hls(st);
    }
    /* observed: without a hook the dialog manager gives the first tab stop
       (the basic colours) the focus; a hook's FALSE leaves none */
}

/* an edit changed: a number takes effect (above its maximum, the
   maximum, written back); anything else gives the old value back */
static void edit_changed(cstate FAR *st, int id)
{
    char b[8];
    BOOL ok;
    int v, max = id == IDC_HUE ? HLSMAX - 1 : id <= IDC_LUM ? HLSMAX : RGBMAX;
    int old = id == IDC_HUE ? st->h : id == IDC_SAT ? st->s : id == IDC_LUM ? st->l :
              id == IDC_RED ? GetRValue(st->rgb) : id == IDC_GREEN ? GetGValue(st->rgb) :
              GetBValue(st->rgb);
    if (st->updating)
        return;
    GetDlgItemText(st->dlg, id, b, sizeof b);
    if (!b[0])                          /* observed: put back when it loses */
        return;                         /* the focus (edit_left) */
    v = s_atoi(b, &ok);
    if (!ok || v > max) {
        st->updating = TRUE;
        set_edit(st, id, ok ? max : old);
        st->updating = FALSE;
        if (!ok)
            return;
        v = max;
    }
    switch (id) {
    case IDC_HUE:
        set_hls(st, v, st->l, st->s, FALSE);
        show_rgb(st);
        break;
    case IDC_SAT:
        set_hls(st, st->h, st->l, v, FALSE);
        show_rgb(st);
        break;
    case IDC_LUM:
        set_hls(st, st->h, v, st->s, FALSE);
        show_rgb(st);
        break;
    default:
        set_rgb(st, RGB(id == IDC_RED ? v : GetRValue(st->rgb),
                        id == IDC_GREEN ? v : GetGValue(st->rgb),
                        id == IDC_BLUE ? v : GetBValue(st->rgb)), FALSE);
        show_hls(st);
        break;
    }
}

/* an edit left empty gets its value back */
static void edit_left(cstate FAR *st, int id)
{
    int v = id == IDC_HUE ? st->h : id == IDC_SAT ? st->s : id == IDC_LUM ? st->l :
            id == IDC_RED ? GetRValue(st->rgb) : id == IDC_GREEN ? GetGValue(st->rgb) :
            GetBValue(st->rgb);
    if (GetWindowTextLength(GetDlgItem(st->dlg, id)) == 0) {
        st->updating = TRUE;
        set_edit(st, id, v);
        st->updating = FALSE;
    }
}

static BOOL in(const RECT FAR *r, int x, int y)
{
    return x >= r->left && x < r->right && y >= r->top && y < r->bottom;
}

static void click(cstate FAR *st, int x, int y)
{
    int box, i, n;
    RECT r;
    for (box = IDC_CUSTDEFINE + 1; box <= IDC_CUSTOM; box++) {
        n = box == IDC_BASIC ? NBASIC : NCUSTOM;
        for (i = 0; i < n; i++) {
            cell_rect(st, box, i, &r);
            InflateRect(&r, 2, 2);
            if (in(&r, x, y)) {
                if (box == IDC_BASIC)
                    st->foc_basic = i;
                else
                    st->foc_cust = i;
                SetFocus(GetDlgItem(st->dlg, box));
                choose_cell(st, box, i);
                return;
            }
        }
    }
    if (!st->full)
        return;
    if (in(&st->rfield, x, y)) {
        int h = mad(x - st->rfield.left, HLSMAX - 1, 0, st->rfield.right - st->rfield.left);
        int s = HLSMAX - mad(y - st->rfield.top, HLSMAX, 0, st->rfield.bottom - st->rfield.top - 1);
        HDC dc = GetDC(st->dlg);
        st->h = h;              /* the old cross goes with the field */
        st->s = s;
        draw_field(st, dc);
        ReleaseDC(st->dlg, dc);
        set_hls(st, h, st->l, s, TRUE);
    } else {
        RECT b = st->rbar;
        b.right += 10;
        if (in(&b, x, y)) {
            int l = HLSMAX - mad(y - st->rbar.top, HLSMAX, 0, st->rbar.bottom - st->rbar.top - 1);
            set_hls(st, st->h, l, st->s, TRUE);
        }
    }
}

static BOOL color_proc(HWND dlg, UINT msg, WPARAM wp, LPARAM lp)
{
    cstate FAR *st;
    if (msg == WM_INITDIALOG) {
        st = (cstate FAR *)lp;
        st->dlg = dlg;
        state_set(dlg, st);
        init(st);
        if (st->hook)
            return (BOOL)call_hook(st->hook, dlg, msg, wp, (LPARAM)st->cc);
        return TRUE;
    }
    st = (cstate FAR *)state_get(dlg);
    if (!st)
        return FALSE;
    if (msg == WM_DESTROY) {            /* observed: not to the hook */
        if (st->field)
            DeleteObject(st->field);
        st->field = NULL;
        state_remove(dlg);
        return FALSE;
    }
    if (st->hook) {
        UINT r = call_hook(st->hook, dlg, msg, wp, lp);
        if (r)
            return (BOOL)r;
    }
    if (msg == msg_setrgb) {
        set_rgb(st, (COLORREF)lp, TRUE);
        return TRUE;
    }
    switch (msg) {
    case WM_PAINT:
        if (st->full) {
            PAINTSTRUCT ps;
            BeginPaint(dlg, &ps);
            draw_field(st, ps.hdc);
            draw_bar(st, ps.hdc);
            draw_arrow(st, ps.hdc, FALSE);
            draw_sample(st, ps.hdc);
            EndPaint(dlg, &ps);
            return TRUE;
        }
        break;
    case WM_LBUTTONDOWN:
        click(st, LOWORD(lp), HIWORD(lp));
        return TRUE;
    case WM_COMMAND:
        switch (wp) {
        case IDOK: {
            LPCHOOSECOLOR cc = st->cc;
            COLORREF old = cc->rgbResult;
            cc->rgbResult = st->rgb;
            if (st->hook && call_hook(st->hook, dlg, msg_colorok, 0, (LPARAM)cc)) {
                cc->rgbResult = old;
                return TRUE;
            }
            EndDialog(dlg, TRUE);
            return TRUE;
        }
        case IDCANCEL:
            EndDialog(dlg, FALSE);
            return TRUE;
        case pshHelp:
            send_help(dlg, st->cc->hwndOwner, (LPARAM)st->cc);
            return TRUE;
        case IDC_CUSTDEFINE:
            show_full(st, TRUE);
            InvalidateRect(dlg, NULL, FALSE);
            SetFocus(GetDlgItem(dlg, IDC_HUE));
            return TRUE;
        case IDC_SOLIDBTN:
            if (st->full) {
                HDC dc = GetDC(dlg);
                COLORREF c = GetNearestColor(dc, st->rgb);
                ReleaseDC(dlg, dc);
                set_rgb(st, c, TRUE);
            }
            return TRUE;
        case IDC_ADDCUSTOM: {
            /* observed: into the custom box with the focus box, and
               into the caller's array at once (kept on Cancel); the
               focus box moves below it, then to the next column (0, 8,
               1, 9, ...) */
            int i = st->foc_cust;
            st->cust[i] = st->rgb;
            if (st->cc->lpCustColors)
                st->cc->lpCustColors[i] = st->rgb;
            st->foc_cust = i < COLS ? i + COLS : (i - COLS + 1) % COLS;
            redraw_cell(st, IDC_CUSTOM, i);
            redraw_cell(st, IDC_CUSTOM, st->foc_cust);
            return TRUE;
        }
        case IDC_HUE: case IDC_SAT: case IDC_LUM:
        case IDC_RED: case IDC_GREEN: case IDC_BLUE:
            if (HIWORD(lp) == EN_CHANGE)
                edit_changed(st, wp);
            else if (HIWORD(lp) == EN_KILLFOCUS)
                edit_left(st, wp);
            return TRUE;
        }
        break;
    }
    return FALSE;
}

BOOL CDPROC ColorDlgProc(HWND dlg, UINT msg, WPARAM wp, LPARAM lp)
{
    return color_proc(dlg, msg, wp, lp);
}

BOOL CDAPI ChooseColor(LPCHOOSECOLOR cc)
{
    cstate FAR *st;
    cd_template t;
    int r;
    if (!cc || cc->lStructSize != sizeof(CHOOSECOLOR))
        return FALSE;                   /* observed: the error untouched */
    if ((cc->Flags & CC_ENABLEHOOK) && !cc->lpfnHook) {
        cd_seterr(CDERR_NOHOOK);
        return FALSE;
    }
    if ((cc->Flags & CC_ENABLETEMPLATE) && !(cc->Flags & CC_ENABLETEMPLATEHANDLE) &&
        !cc->lpTemplateName) {
        cd_seterr(CDERR_NOTEMPLATE);
        return FALSE;
    }
    st = mem_alloc(sizeof(cstate));
    if (!st) {
        cd_seterr(CDERR_MEMALLOCFAILURE);
        return FALSE;
    }
    st->cc = cc;
    st->hook = (cc->Flags & CC_ENABLEHOOK) ? (cd_hook)cc->lpfnHook : NULL;
    t.app = (HINSTANCE)cc->hInstance;
    t.use_handle = (cc->Flags & CC_ENABLETEMPLATEHANDLE) != 0;
    t.use_name = !t.use_handle && (cc->Flags & CC_ENABLETEMPLATE);
    t.name = cc->lpTemplateName;
    t.ours = DLG_COLOR;
    r = cd_modal(&t, cc->hwndOwner, (FARPROC)ColorDlgProc, (LPARAM)(LPVOID)st);
    mem_free(st);
    return r == 1;
}
