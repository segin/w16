/*
 * ruler.c - w16 Write: the ruler (Document > Ruler On/Off).
 *
 * Observed on 3.1 (probes WR24-WR27, ARCHITECTURE.md 15.6): the ruler is
 * the child window MSWRITE_RULER, made by Ruler On and destroyed by Ruler
 * Off, first in the frame's window list; it is the text window's width
 * and 39 pixels tall, and the text window starts 36 pixels down, so the
 * ruler covers its top 3 rows. In ruler pixels (x 19 is indent 0, a
 * pixel left of where text starts):
 *   rows 1-15   nine buttons, 22 x 15, at x 91 and 125 (left and decimal
 *               tab: the kind a click on the marks' row sets), 195, 229,
 *               263 (single, 1 1/2, double spacing), 333, 367, 401, 435
 *               (left, centered, right, justified); the current ones down
 *   rows 17-25  the scale: inches in eighths (ticks 9, 7, 5 and 3 rows
 *               tall for inches, halves, quarters, eighths), the inch's
 *               number in MS Sans Serif 8 a pixel right of its tick (the
 *               ANSI_VAR_FONT face); in cm, halves of a centimetre (9 and
 *               5 rows)
 *   row 26      a line from x 19 to the right edge
 *   rows 27-37  the marks of the paragraph with the caret: first-line
 *               indent, left and right indent, tab stops, each a 12 x 11
 *               cell whose column 6 is the position, inverted onto the
 *               ruler (a first-line indent over the left one shows white)
 *   row 38      a line from x 19 to the right edge
 * Mouse: a button sets its format on the selected paragraphs (tab kind:
 * the ruler's own); in the marks' row a mark is dragged, its cell
 * inverted while the button is down, the text laid out when it is
 * released; elsewhere in that row a click sets a tab of the current kind.
 * Positions go to the nearest 1/16 inch (WR26: 100, 205, 262 pixels give
 * 1530, 3060, 3960 twips). A tab dragged off the ruler is gone at once
 * (WR25 D7). Tabs belong to every paragraph (the last one gets its
 * paragraph mark, its PAP byte 0 is kept; WR27); indents and the
 * buttons' formats to the selected ones. When the first-line and left
 * indents coincide a press there takes the first-line one (WR26). Moving
 * the left indent keeps the first line where it was (WR25: left 1440,
 * first line -720).
 *
 * Art: the buttons and marks are w16's own bitmaps (tools/mkwriteruler.py)
 * with the cell sizes of the original's resources 102 and 202.
 */
#include "write.h"

#define RL_H        39
#define RL_X0       19
#define RL_BTNY     1
#define RL_BTNW     22
#define RL_BTNH     15
#define RL_SCALE    17                  /* the scale's top row */
#define RL_BASE     26
#define RL_MARKY    27
#define RL_MARKH    11
#define RL_MARKW    12
#define RL_BOTTOM   38
#define IDB_BUTTONS 102
#define IDB_MARKS   202

/* the cells of the marks' bitmap, in the original's order (found with
   tests/wrmsres.sh against WR24) */
enum { MK_FIRST, MK_LEFT, MK_RIGHT, MK_TAB, MK_DECTAB };

HWND g_ruler;
BOOL g_cm;                              /* Page Layout: measurements in cm */
static int tabJc;                       /* 0 left tab, 3 decimal: the button down */
static HBITMAP bmButtons, bmMarks;
static const int btnX[9] = { 91, 125, 195, 229, 263, 333, 367, 401, 435 };

/* what the ruler shows: the caret's paragraph and the page's text width */
typedef struct {
    int left, first, right;             /* positions, twips from indent 0 */
    int jc, dyaLine;
    int ntabs;
    struct { int dxa; BYTE jc; } tab[NTABS];
    BOOL cm;
    int tabJc;
} RSTATE;
static RSTATE shown;

/* the mark being dragged: kind, tab index, position in twips; gone: a
   tab pulled off the ruler */
static BOOL drag;
static int dragKind, dragTab, dragPos;
static BOOL dragGone;
static int pressX, pressY;              /* where the button went down */

static int px(long tw)
{
    /* on the cm scale a centimetre is its 38 pixels (two of 19), so the
       marks sit on it (WR38: the right indent at 8644 twips 579 pixels in,
       not 576) */
    if (g_cm)
        return RL_X0 + MulDiv((int)tw, 2 * MulDiv(96, 50, 254), 567);
    return RL_X0 + (int)((tw * 96 + (tw >= 0 ? 720 : -720)) / 1440);
}

static void state_now(RSTATE *s)
{
    DWORD f, l;
    ATOM32 a;
    PAPX p;
    SEPX sep;
    int i;
    m_set(s, 0, sizeof *s);
    para_at(&g_doc, g_selFirst, &f, &l, &a);
    pap_decode(&g_doc, a, &p);
    sep_decode(&g_doc, &sep);
    /* a running head's indents count from the page's edges: the ruler
       shows them from the margins, as its text is laid out (WR38) */
    if (p.rhc & 6) {
        p.dxaLeft -= sep.xaLeft;
        p.dxaRight -= (int)sep.xaMac - (int)sep.xaLeft - (int)sep.dxaText;
    }
    s->left = p.dxaLeft;
    s->first = p.dxaLeft + p.dxaLeft1;
    s->right = (int)sep.dxaText - p.dxaRight;
    s->jc = p.jc;
    s->dyaLine = p.dyaLine;
    s->ntabs = p.ntabs;
    for (i = 0; i < p.ntabs; i++) {
        s->tab[i].dxa = p.tab[i].dxa;
        s->tab[i].jc = p.tab[i].jc;
    }
    s->cm = g_cm;
    s->tabJc = tabJc;
}

/* ---- drawing ---- */

static void blit(HDC dc, HBITMAP bm, int sx, int w, int h, int x, int y, DWORD rop)
{
    HDC mdc = CreateCompatibleDC(dc);
    HBITMAP old = SelectObject(mdc, bm);
    BitBlt(dc, x, y, w, h, mdc, sx, 0, rop);
    SelectObject(mdc, old);
    DeleteDC(mdc);
}

/* a mark: its cell's ink inverts the ruler (DSxn: white source keeps) */
static void mark(HDC dc, int kind, int x)
{
    blit(dc, bmMarks, kind * RL_MARKW, RL_MARKW, RL_MARKH, x - 6, RL_MARKY, 0x00990066L);
}

static void vline(HDC dc, int x, int y1, int y2)
{
    PatBlt(dc, x, y1, 1, y2 - y1, BLACKNESS);
}

static void scale(HDC dc, int right)
{
    HFONT old = SelectObject(dc, GetStockObject(ANSI_VAR_FONT));
    long k;
    int x;
    SetBkMode(dc, TRANSPARENT);
    SetTextColor(dc, RGB(0, 0, 0));
    for (k = 0;; k++) {
        int top;
        if (g_cm) {
            /* halves of a centimetre, each a whole number of pixels (WR26
               S4: 19 at 96 dpi, the 4 cm tick 152 pixels in) */
            x = RL_X0 + (int)(k * MulDiv(96, 50, 254));
            top = k % 2 ? RL_SCALE + 4 : RL_SCALE;
        } else {
            x = px(k * 180);
            top = k % 8 == 0 ? RL_SCALE : k % 4 == 0 ? RL_SCALE + 2 :
                  k % 2 == 0 ? RL_SCALE + 4 : RL_SCALE + 6;
        }
        if (x >= right)
            break;
        vline(dc, x, top, RL_BASE);
        if (top == RL_SCALE) {
            char s[8];
            s_utoa(s, g_cm ? k / 2 : k / 8);
            TextOut(dc, x + 1, RL_SCALE - 2, s, s_len(s));
        }
    }
    SelectObject(dc, old);
}

static void paint(HWND w)
{
    PAINTSTRUCT ps;
    HDC dc = BeginPaint(w, &ps);
    RECT r;
    int i, kind;
    RSTATE s;
    state_now(&s);
    shown = s;
    GetClientRect(w, &r);
    /* the marks invert what is under them: the ruler is drawn from a
       clean background each time */
    {
        HBRUSH b = CreateSolidBrush(GetSysColor(COLOR_WINDOW));
        FillRect(dc, &r, b);
        DeleteObject(b);
    }
    for (i = 0; i < 9; i++) {
        BOOL down;
        switch (i) {
        case 0: down = tabJc == 0; break;
        case 1: down = tabJc == 3; break;
        case 2: down = s.dyaLine <= 240; break;
        case 3: down = s.dyaLine == 360; break;
        case 4: down = s.dyaLine == 480; break;
        default: down = s.jc == i - 5; break;
        }
        blit(dc, bmButtons, (i + (down ? 9 : 0)) * RL_BTNW, RL_BTNW, RL_BTNH, btnX[i], RL_BTNY, SRCCOPY);
    }
    scale(dc, r.right);
    PatBlt(dc, RL_X0, RL_BASE, r.right - RL_X0, 1, BLACKNESS);
    PatBlt(dc, RL_X0, RL_BOTTOM, r.right - RL_X0, 1, BLACKNESS);
    /* the marks; the one being dragged at its new place, inverted */
    for (i = 0; i < s.ntabs; i++)
        if (!(drag && dragKind >= MK_TAB && dragTab == i))
            mark(dc, s.tab[i].jc == 3 ? MK_DECTAB : MK_TAB, px(s.tab[i].dxa));
    if (!(drag && dragKind == MK_RIGHT))
        mark(dc, MK_RIGHT, px(s.right));
    if (!(drag && dragKind == MK_LEFT))
        mark(dc, MK_LEFT, px(s.left));
    if (!(drag && dragKind == MK_FIRST))
        mark(dc, MK_FIRST, px(s.first));
    if (drag && !dragGone) {
        kind = dragKind;
        mark(dc, kind, px(dragPos));
        PatBlt(dc, px(dragPos) - 6, RL_MARKY, RL_MARKW, RL_MARKH, DSTINVERT);
    }
    EndPaint(w, &ps);
}

/* the caret's paragraph or the measurement unit changed: show it */
void ruler_update(void)
{
    RSTATE s;
    if (!g_ruler || drag)
        return;
    state_now(&s);
    if (m_cmp(&s, &shown, sizeof s))
        InvalidateRect(g_ruler, NULL, FALSE);
}

/* ---- changes ---- */

static int snap(int x)
{
    long tw = g_cm ? (long)(x - RL_X0) * 567 / (2 * MulDiv(96, 50, 254))
                   : (long)(x - RL_X0) * 1440 / 96;
    if (tw < 0)
        tw = 0;
    /* to the nearest 1/16 inch (WR26), or 1/8 cm, the twips cut (WR38:
       50 and 157 pixels give 779 and 2338) */
    if (g_cm)
        return (int)((tw * 8 + 283) / 567 * 567 / 8);
    return (int)((tw + 45) / 90 * 90);
}

static RSTATE target;                   /* the values to set */

static void pap_ruler(PAPX *p, WORD what)
{
    int i, ml = 0, mr = 0;
    if (p->rhc & 6) {                   /* running heads: from the page's edges */
        SEPX sep;
        sep_decode(&g_doc, &sep);
        ml = sep.xaLeft;
        mr = (int)sep.xaMac - (int)sep.xaLeft - (int)sep.dxaText;
    }
    switch (what) {
    case MK_LEFT:
        p->dxaLeft1 = p->dxaLeft - ml + p->dxaLeft1 - target.left;
        p->dxaLeft = target.left + ml;
        break;
    case MK_FIRST:
        p->dxaLeft1 = target.first - (p->dxaLeft - ml);
        break;
    case MK_RIGHT: {
        SEPX sep;
        sep_decode(&g_doc, &sep);
        p->dxaRight = (int)sep.dxaText - target.right + mr;
        break;
    }
    default:
        p->ntabs = target.ntabs;
        for (i = 0; i < target.ntabs; i++) {
            p->tab[i].dxa = target.tab[i].dxa;
            p->tab[i].jc = target.tab[i].jc;
        }
        break;
    }
}

/* the tab list with tab i moved to pos (i < 0: a new one of the current
   kind), or removed: sorted, no two at one place (UNVERIFIED) */
static void tabs_set(int i, int pos, BOOL remove)
{
    int k, j, n = 0;
    BYTE jc = (BYTE)tabJc;
    struct { int dxa; BYTE jc; } t[NTABS + 1];
    for (k = 0; k < shown.ntabs; k++) {
        if (k == i) {
            jc = shown.tab[k].jc;
            continue;
        }
        t[n].dxa = shown.tab[k].dxa;
        t[n++].jc = shown.tab[k].jc;
    }
    if (!remove) {
        for (k = 0; k < n && t[k].dxa < pos; k++)
            ;
        if (!(k < n && t[k].dxa == pos) && n < NTABS) {
            for (j = n; j > k; j--)
                t[j] = t[j - 1];
            t[k].dxa = pos;
            t[k].jc = jc;
            n++;
        }
    }
    target.ntabs = n;
    for (k = 0; k < n; k++) {
        target.tab[k].dxa = t[k].dxa;
        target.tab[k].jc = t[k].jc;
    }
    /* in the Header or Footer view the tabs go to the head and, back in
       the document, to all of it (WR38: a tab set in the Header view is
       in the body's paragraphs too); the head's text is not ended */
    if (hf_mode()) {
        PAPX t;
        pap_default(&t);
        t.ntabs = target.ntabs;
        for (k = 0; k < target.ntabs; k++) {
            t.tab[k].dxa = target.tab[k].dxa;
            t.tab[k].jc = target.tab[k].jc;
        }
        hf_tabs(&t);
        pap_apply(pap_ruler, MK_TAB, PA_ALL | PA_KEEPB0);
    } else {
        /* the document's one paragraph gets its mark (WR27); with more
           paragraphs the last is left as it is (WR38; UNVERIFIED rule) */
        DWORD f, l;
        ATOM32 a;
        BOOL one = g_doc.nparas <= 1 || (para_get(&g_doc, 0, &f, &l, &a), l >= g_doc.cpMac);
        pap_apply(pap_ruler, MK_TAB, PA_ALL | PA_KEEPB0 | (one ? PA_TERMINATE : 0));
    }
}

static void drop(void)
{
    target = shown;
    switch (dragKind) {
    case MK_LEFT:
        if (dragPos == shown.left)
            return;
        target.left = dragPos;
        pap_apply(pap_ruler, MK_LEFT, 0);
        break;
    case MK_FIRST:
        if (dragPos == shown.first)
            return;
        target.first = dragPos;
        pap_apply(pap_ruler, MK_FIRST, 0);
        break;
    case MK_RIGHT:
        if (dragPos == shown.right)
            return;
        target.right = dragPos;
        pap_apply(pap_ruler, MK_RIGHT, 0);
        break;
    default:
        if (dragTab >= 0 && !dragGone && dragPos == shown.tab[dragTab].dxa)
            return;
        tabs_set(dragTab, dragPos, dragGone);
        break;
    }
    view_caret_now();                   /* shown after the change (WR26 S1) */
}

static BOOL hit(int x, int pos)
{
    int m = px(pos);
    return x >= m - 6 && x < m + 6;
}

static void press(HWND w, int x, int y)
{
    int i;
    if (y >= RL_BTNY && y < RL_BTNY + RL_BTNH) {
        for (i = 0; i < 9; i++)
            if (x >= btnX[i] && x < btnX[i] + RL_BTNW)
                break;
        if (i == 0 || i == 1) {
            tabJc = i ? 3 : 0;
            InvalidateRect(w, NULL, FALSE);
        } else if (i < 9) {
            static const WORD ids[7] = { IDM_SINGLE, IDM_ONEHALF, IDM_DOUBLE,
                                         IDM_LEFT, IDM_CENTER, IDM_RIGHT, IDM_JUSTIFY };
            cmd_para_how(ids[i - 2], PA_TERMINATE);
        }
        return;
    }
    if (y < RL_MARKY || y >= RL_MARKY + RL_MARKH)
        return;
    dragTab = -1;
    if (hit(x, shown.first)) {
        dragKind = MK_FIRST;
        dragPos = shown.first;
    } else if (hit(x, shown.left)) {
        dragKind = MK_LEFT;
        dragPos = shown.left;
    } else if (hit(x, shown.right)) {
        dragKind = MK_RIGHT;
        dragPos = shown.right;
    } else {
        dragKind = MK_TAB;
        dragPos = snap(x);
        for (i = 0; i < shown.ntabs; i++)
            if (hit(x, shown.tab[i].dxa)) {
                dragTab = i;
                dragPos = shown.tab[i].dxa;
                break;
            }
    }
    drag = TRUE;
    dragGone = FALSE;
    pressX = x;
    pressY = y;
    SetCapture(w);
    InvalidateRect(w, NULL, FALSE);
    UpdateWindow(w);
}

static void move(HWND w, int x, int y)
{
    RECT r;
    int pos;
    BOOL gone;
    /* a mark pressed and let go where it was stays where it was (WR38:
       a tab at 1530 clicked on the cm ruler) */
    if (x == pressX && y == pressY)
        return;
    pressX = -32767;
    pos = snap(x);
    GetClientRect(w, &r);
    /* a tab pulled off the ruler is gone (WR25 D7: 98 pixels below) */
    gone = dragKind >= MK_TAB && (y < 0 || y >= r.bottom);
    if (pos == dragPos && gone == dragGone)
        return;
    dragPos = pos;
    dragGone = gone;
    InvalidateRect(w, NULL, FALSE);
    UpdateWindow(w);
}

LRESULT FAR PASCAL __export RulerWndProc(HWND w, UINT msg, WPARAM wp, LPARAM lp)
{
    switch (msg) {
    case WM_PAINT:
        paint(w);
        return 0;
    case WM_LBUTTONDOWN:
        press(w, (int)LOWORD(lp), (int)HIWORD(lp));
        return 0;
    case WM_MOUSEMOVE:
        if (drag)
            move(w, (int)LOWORD(lp), (int)HIWORD(lp));
        return 0;
    case WM_LBUTTONUP:
        if (drag) {
            move(w, (int)LOWORD(lp), (int)HIWORD(lp));
            ReleaseCapture();
            drag = FALSE;
            drop();
            InvalidateRect(w, NULL, FALSE);
        }
        return 0;
    case WM_CANCELMODE:
        if (drag) {
            ReleaseCapture();
            drag = FALSE;
            InvalidateRect(w, NULL, FALSE);
        }
        return 0;
    }
    return DefWindowProc(w, msg, wp, lp);
}

BOOL ruler_register(HINSTANCE hi)
{
    WNDCLASS wc;
    m_set(&wc, 0, sizeof wc);
    wc.lpfnWndProc = RulerWndProc;
    wc.hInstance = hi;
    wc.hCursor = LoadCursor(NULL, IDC_ARROW);
    wc.hbrBackground = (HBRUSH)(COLOR_WINDOW + 1);
    wc.lpszClassName = "MSWRITE_RULER";
    return RegisterClass(&wc);
}

int ruler_height(void)
{
    return g_ruler ? RL_H - 3 : 0;
}

/* Ruler On / Off: made or destroyed, the frame's children placed again */
void ruler_toggle(void)
{
    RECT r;
    if (g_ruler) {
        DestroyWindow(g_ruler);
        g_ruler = NULL;
    } else {
        if (!bmButtons)
            bmButtons = LoadBitmap(g_hinst, MAKEINTRESOURCE(IDB_BUTTONS));
        if (!bmMarks)
            bmMarks = LoadBitmap(g_hinst, MAKEINTRESOURCE(IDB_MARKS));
        m_set(&shown, 0xFF, sizeof shown);
        g_ruler = CreateWindow("MSWRITE_RULER", NULL, WS_CHILD | WS_CLIPSIBLINGS,
                               0, 0, 0, RL_H, g_frame, NULL, g_hinst, NULL);
        if (!g_ruler)
            return;
        /* first in the window list (WR24) */
        SetWindowPos(g_ruler, (HWND)0, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
    }
    GetClientRect(g_frame, &r);
    frame_layout(r.right, r.bottom);
    if (g_ruler)
        ShowWindow(g_ruler, SW_SHOWNA);
    /* the text window repainted whole, the caret with it (WR24 R1: shown) */
    InvalidateRect(g_docwnd, NULL, TRUE);
    UpdateWindow(g_docwnd);
    view_caret_now();
}

void ruler_done(void)
{
    if (bmButtons)
        DeleteObject(bmButtons);
    if (bmMarks)
        DeleteObject(bmMarks);
    bmButtons = bmMarks = NULL;
}
