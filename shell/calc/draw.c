/*
 * draw.c - w16 Calculator: painting the buttons and frames, and clicks.
 *
 * Layout measured from screenshots of Windows 3.1's Calculator at
 * 640x480 (client coordinates, VGA system font):
 *  scientific: display frame 188,8-470,34; radix and unit group frames
 *    8,44-248,72 and 260,44-470,72; frames round Inv/Hyp 174,76-328,104
 *    and round the indicators 336,76-374,104, 388,76-418,104,
 *    432,76-470,104; C, CE, Back at x 6, 62, 118 (52 x 28) y 76; the
 *    11 x 5 keys at x 6 + 43c, y 108 + 36r, 39 x 28.
 *  standard: display frame 44,8-244,34; memory indicator frame
 *    192,42-232,68; C, CE, Back at x 6, 60, 114 (50 x 28) y 40; the
 *    6 x 4 keys at x 6 + 42c, y 72 + 36r, 38 x 28.
 * Keys are rounded rectangles outlined in the key's colour (red, blue,
 * magenta, dark cyan, black) with the label in that colour, centred.
 * Labels are the string table's (strings 0-63).
 */
#include <windows.h>
#include "calc.h"
#include "ui.h"

#define C_BLACK     RGB(0, 0, 0)
#define C_RED       RGB(255, 0, 0)
#define C_BLUE      RGB(0, 0, 255)
#define C_MAGENTA   RGB(255, 0, 255)
#define C_TEAL      RGB(0, 128, 128)

/* rounded corners: the ellipse of RoundRect, UNVERIFIED (measured
   corners: 4, 3, 2, 1, 1, 1 pixels in from the straight edges) */
#define ROUND_W     12
#define ROUND_H     12

typedef struct {
    int id;             /* command */
    signed char str;    /* label: string number */
    char col;           /* colour index */
} KEY;

static const COLORREF colors[5] = { C_BLACK, C_RED, C_BLUE, C_MAGENTA, C_TEAL };
enum { K, R, B, M, T };

/* scientific keys, column by column (the string table's order) */
static const KEY sci_keys[11][5] = {
    { { IDC_STA, 0, B }, { IDC_AVE, 1, B }, { IDC_SUM, 2, B }, { IDC_DEV, 3, B }, { IDC_DAT, 4, B } },
    { { IDC_FE, 5, T }, { IDC_DMS, 6, T }, { IDC_SIN, 7, T }, { IDC_COS, 8, T }, { IDC_TAN, 9, T } },
    { { IDC_LPAREN, 10, K }, { IDC_EXP, 11, T }, { IDC_PWR, 12, T }, { IDC_CUBE, 13, T }, { IDC_SQR, 14, T } },
    { { IDC_RPAREN, 15, K }, { IDC_LN, 16, T }, { IDC_LOG, 17, T }, { IDC_FACT, 18, T }, { IDC_RECIP, 19, T } },
    { { IDC_MC, 20, M }, { IDC_MR, 21, M }, { IDC_MS, 22, M }, { IDC_MPLUS, 23, M }, { IDC_PI, 24, B } },
    { { IDC_0 + 7, 25, B }, { IDC_0 + 4, 26, B }, { IDC_0 + 1, 27, B }, { IDC_0, 28, B }, { IDC_A, 29, B } },
    { { IDC_0 + 8, 30, B }, { IDC_0 + 5, 31, B }, { IDC_0 + 2, 32, B }, { IDC_SIGN, 33, B }, { IDC_A + 1, 34, B } },
    { { IDC_0 + 9, 35, B }, { IDC_0 + 6, 36, B }, { IDC_0 + 3, 37, B }, { IDC_POINT, 38, B }, { IDC_A + 2, 39, B } },
    { { IDC_DIV, 40, R }, { IDC_MUL, 41, R }, { IDC_SUB, 42, R }, { IDC_ADD, 43, R }, { IDC_A + 3, 44, B } },
    { { IDC_MOD, 45, R }, { IDC_OR, 46, R }, { IDC_LSH, 47, R }, { IDC_EQU, 51, R }, { IDC_A + 4, 52, B } },
    { { IDC_AND, 53, R }, { IDC_XOR, 54, R }, { IDC_NOT, 55, R }, { IDC_INT, 56, R }, { IDC_A + 5, 57, B } },
};

/* standard keys, row by row */
static const KEY std_keys[4][6] = {
    { { IDC_MC, 20, M }, { IDC_0 + 7, 25, B }, { IDC_0 + 8, 30, B }, { IDC_0 + 9, 35, B }, { IDC_DIV, 40, R }, { IDC_SQRT, 48, T } },
    { { IDC_MR, 21, M }, { IDC_0 + 4, 26, B }, { IDC_0 + 5, 31, B }, { IDC_0 + 6, 36, B }, { IDC_MUL, 41, R }, { IDC_PERCENT, 49, T } },
    { { IDC_MS, 22, M }, { IDC_0 + 1, 27, B }, { IDC_0 + 2, 32, B }, { IDC_0 + 3, 37, B }, { IDC_SUB, 42, R }, { IDC_RECIP, 50, T } },
    { { IDC_MPLUS, 23, M }, { IDC_0, 28, B }, { IDC_SIGN, 33, B }, { IDC_POINT, 38, B }, { IDC_ADD, 43, R }, { IDC_EQU, 51, R } },
};

static const KEY top_keys[3] = { { IDC_CLEAR, 58, R }, { IDC_CE, 59, R }, { IDC_BACK, 60, R } };

/* the n-th key of the current view and its rectangle; FALSE past the end */
static BOOL key_at(int n, const KEY **k, RECT *r)
{
    if (n < 3) {
        int w = g.sci ? 52 : 50, step = g.sci ? 56 : 54, y = g.sci ? 76 : 40;
        *k = &top_keys[n];
        SetRect(r, 6 + step * n, y, 6 + step * n + w, y + 28);
        return TRUE;
    }
    n -= 3;
    if (g.sci) {
        int c = n / 5, row = n % 5;
        if (c >= 11)
            return FALSE;
        *k = &sci_keys[c][row];
        SetRect(r, 6 + 43 * c, 108 + 36 * row, 6 + 43 * c + 39, 108 + 36 * row + 28);
    } else {
        int row = n / 6, c = n % 6;
        if (row >= 4)
            return FALSE;
        *k = &std_keys[row][c];
        SetRect(r, 6 + 42 * c, 72 + 36 * row, 6 + 42 * c + 38, 72 + 36 * row + 28);
    }
    return TRUE;
}

static void frame(HDC dc, int l, int t, int r, int b)
{
    Rectangle(dc, l, t, r, b);
}

static void draw_key(HDC dc, const KEY *k, const RECT *r)
{
    char label[16];
    int n;
    DWORD ext;
    HPEN pen = CreatePen(PS_SOLID, 1, colors[(int)k->col]), old;
    old = SelectObject(dc, pen);
    RoundRect(dc, r->left, r->top, r->right, r->bottom, ROUND_W, ROUND_H);
    SelectObject(dc, old);
    DeleteObject(pen);
    n = LoadString(g_hinst, IDS_FIRSTKEY + k->str, label, sizeof label);
    SetTextColor(dc, colors[(int)k->col]);
    ext = GetTextExtent(dc, label, n);
    TextOut(dc, r->left + (r->right - r->left - (int)LOWORD(ext)) / 2,
            r->top + (r->bottom - r->top - (int)HIWORD(ext)) / 2, label, n);
}

void draw_paint(HWND w)
{
    PAINTSTRUCT ps;
    HDC dc = BeginPaint(w, &ps);
    const KEY *k;
    RECT r;
    int i;
    HBRUSH oldb = SelectObject(dc, GetStockObject(NULL_BRUSH));
    HPEN oldp = SelectObject(dc, GetStockObject(BLACK_PEN));
    SetBkMode(dc, TRANSPARENT);
    if (g.sci) {
        frame(dc, 188, 8, 470, 34);
        frame(dc, 8, 44, 248, 72);
        frame(dc, 260, 44, 470, 72);
        frame(dc, 174, 76, 328, 104);
        frame(dc, 336, 76, 374, 104);
        frame(dc, 388, 76, 418, 104);
        frame(dc, 432, 76, 470, 104);
    } else {
        frame(dc, 44, 8, 244, 34);
        frame(dc, 192, 42, 232, 68);
    }
    for (i = 0; key_at(i, &k, &r); i++)
        draw_key(dc, k, &r);
    SelectObject(dc, oldp);
    SelectObject(dc, oldb);
    EndPaint(w, &ps);
}

/* ---- the mouse: a key is inverted while it is held down ---- */

static int down = -1;           /* the key being pressed */
static BOOL inverted;

static int hit(int x, int y)
{
    const KEY *k;
    RECT r;
    int i;
    POINT p;
    p.x = x;
    p.y = y;
    for (i = 0; key_at(i, &k, &r); i++)
        if (PtInRect(&r, p))
            return i;
    return -1;
}

static void invert(HWND w, int n)
{
    const KEY *k;
    RECT r;
    HDC dc;
    if (n < 0 || !key_at(n, &k, &r))
        return;
    InflateRect(&r, -1, -1);
    dc = GetDC(w);
    InvertRect(dc, &r);
    ReleaseDC(w, dc);
}

void mouse_down(HWND w, int x, int y)
{
    down = hit(x, y);
    if (down < 0)
        return;
    SetCapture(w);
    invert(w, down);
    inverted = TRUE;
}

void mouse_move(HWND w, int x, int y)
{
    BOOL in;
    if (down < 0)
        return;
    in = hit(x, y) == down;
    if (in != inverted) {
        invert(w, down);
        inverted = in;
    }
}

void mouse_up(HWND w, int x, int y)
{
    const KEY *k;
    RECT r;
    int n = down;
    if (n < 0)
        return;
    down = -1;
    ReleaseCapture();
    if (inverted)
        invert(w, n);
    inverted = FALSE;
    if (hit(x, y) == n && key_at(n, &k, &r))
        do_command(k->id);
}
