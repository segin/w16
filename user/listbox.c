/*
 * listbox.c - the LISTBOX class (and COMBOLBOX, a combo box's list).
 *
 * The state is a block in USER's local heap, its handle in the window's
 * first extra word (Windows 3.1's class has 2 extra bytes: US2); the items
 * are another block of LBITEMs, strings blocks of their own, both in the
 * object heap (uheap.c: 3.1's list box strings cost DGROUP nothing, US15),
 * reached through item() and STR() each time.
 * Items are drawn in the window's font one line apart, the text 2 pixels
 * from the left (UNVERIFIED: US4 to compare), selected items in the
 * Highlight colours, the caret item with a dotted focus rectangle.
 *
 * Probe US32 (tests/apps/us32.c; boot-test case lbclip) measured on 3.1:
 * - the height (without LBS_NOINTEGRALHEIGHT or LBS_OWNERDRAWVARIABLE):
 *   the window, H high, is made floor(H / item height) items + 2 high,
 *   whatever its border (none, WS_BORDER, WS_DLGFRAME: the client area is
 *   then smaller), 0 items too; when made (the item height then the
 *   system font's, or WM_MEASUREITEM's) and whenever it is sized, from the
 *   new height. WM_SETFONT changes the item height but not the window
 *   (a dialog's list box is sized again after: dlg.c). Made, every list
 *   box is a pixel bigger each side than asked, border or not;
 * - the vertical scroll bar's range is always 0-100 (hidden too): the
 *   position is top * 100 / max rounded, max the last top that shows the
 *   last item whole; SB_THUMBPOSITION/THUMBTRACK n goes to
 *   n * (max + 1) / 100 rounded; a page is the whole items shown less one;
 *   LB_SETTOPINDEX stops at max;
 * - a click on the item the bottom edge cuts selects it without
 *   scrolling; LB_SETCURSEL and the keys scroll it into view;
 * - the focus rectangle (the caret) is not drawn on a hidden box nor
 *   before its first paint (caret_on);
 * - how it draws (probe US33, case lbdraw): only what changes, the caret
 *   taken away and put back around each change, scrolls by moving the
 *   pixels ("drawing" below);
 * - LBS_OWNERDRAWVARIABLE: WM_MEASUREITEM for each item as it is added
 *   (itemHeight the font's height), each item its own height; scrolling,
 *   the range and the clicks by those heights.
 */
#include "user.h"

void draw_scrollbar(HDC hdc, RECT *rc, BOOL vert, void *s);

typedef struct LBITEM {
    WORD  text;                 /* local string (0 for no-string owner-draw) */
    DWORD data;
    WORD  sel;
    WORD  ht;                   /* LBS_OWNERDRAWVARIABLE: its height */
} LBITEM;
typedef LBITEM __far *PLBITEM;
#define STR(t)  ((LPCSTR)UH_PTR(&uhObj, (t)))

typedef struct LB {
    WORD   items;               /* local block of LBITEMs */
    int    count, alloc;
    int    top, cur, caret;
    int    itemh;
    HFONT  font;
    WORD   focus;
    WORD   anchor;
    HWND   owner;               /* parent, or the combo box */
    WORD   nredraw;             /* WM_SETREDRAW FALSE: no drawing */
    int    colw;                /* LBS_MULTICOLUMN: a column's width */
    DWORD  bars;                /* WS_VSCROLL/WS_HSCROLL it was made with */
    int    hext;                /* LB_SETHORIZONTALEXTENT */
    BYTE   sizing;              /* integral() resizing it */
    BYTE   caretOn;             /* the focus rectangle is drawn (caret_on) */
    BYTE   painted;             /* WM_PAINT seen */
    BYTE   latesize;            /* sized again by the dialog manager: 1 a
                                   WM_SIZE to post after the first paint,
                                   2 posted (late_size) */
    WORD   tabs;                /* LB_SETTABSTOPS: object-heap block of
                                   ntabs stops in pixels (0: the default) */
    int    ntabs;
} LB;
typedef LB __near *PLB;

static PLB lb_of(HWND h)
{
    return (PLB)GetWindowWord(h, 0);
}

static BOOL lb_shown(HWND h);

static PLBITEM item(PLB lb, int i)
{
    return (PLBITEM)UH_PTR(&uhObj, lb->items + i * sizeof(LBITEM));
}

static DWORD lbstyle(HWND h)
{
    return GetWindowLong(h, GWL_STYLE);
}

static BOOL multi(HWND h)
{
    return (lbstyle(h) & (LBS_MULTIPLESEL | LBS_EXTENDEDSEL)) != 0;
}

static BOOL hasstrings(HWND h)
{
    DWORD s = lbstyle(h);
    return !(s & (LBS_OWNERDRAWFIXED | LBS_OWNERDRAWVARIABLE)) || (s & LBS_HASSTRINGS);
}

static int client_h(HWND h)
{
    RECT r;
    GetClientRect(h, &r);
    return r.bottom;
}

/* LBS_OWNERDRAWVARIABLE (not with LBS_MULTICOLUMN): each item its own
   height */
static BOOL varh(HWND h)
{
    return (lbstyle(h) & (LBS_OWNERDRAWVARIABLE | LBS_MULTICOLUMN)) == LBS_OWNERDRAWVARIABLE;
}

static int item_h(HWND h, PLB lb, int i)
{
    if (varh(h) && i >= 0 && i < lb->count)
        return item(lb, i)->ht;
    return lb->itemh;
}

/* the whole items shown from the top one */
static int visible_rows(HWND h, PLB lb)
{
    int ch = client_h(h), y = 0, n = 0;
    if (!varh(h))
        return lb->itemh ? ch / lb->itemh : 1;
    while (lb->top + n < lb->count && y + item(lb, lb->top + n)->ht <= ch)
        y += item(lb, lb->top + n++)->ht;
    return n;
}

/* the last top item: the one that shows the last item whole */
static int max_top(HWND h, PLB lb)
{
    int t, y = 0, ch;
    if (!varh(h)) {
        t = lb->count - (lb->itemh ? client_h(h) / lb->itemh : 1);
        return t > 0 ? t : 0;
    }
    ch = client_h(h);
    for (t = lb->count; t > 0 && y + item(lb, t - 1)->ht <= ch; t--)
        y += item(lb, t - 1)->ht;
    if (t >= lb->count)
        t = lb->count - 1;
    return t > 0 ? t : 0;
}

/* v * u / d rounded to the nearest, as MulDiv */
static int mul_div(int v, int u, int d)
{
    long p = (long)v * u;
    if (!d)
        return 0;
    return (int)(p >= 0 ? (p + d / 2) / d : -((-p + d / 2) / d));
}

/* LBS_MULTICOLUMN (File Manager's file list): items run down a column,
   then on in the next one to the right, each column colw wide
   (LB_SETCOLUMNWIDTH); lb->top is the first item of the leftmost column
   shown, and a horizontal scroll bar moves by columns (3.1 SDK).
   UNVERIFIED: the default column width (here 15 average characters) */
static BOOL multicol(HWND h)
{
    return (lbstyle(h) & LBS_MULTICOLUMN) != 0;
}

static int col_width(PLB lb)
{
    return lb->colw > 0 ? lb->colw : 15 * cxSysChar;
}

static int visible_cols(HWND h, PLB lb)
{
    RECT r;
    int n;
    GetClientRect(h, &r);
    n = (r.right - r.left) / col_width(lb);
    return n > 0 ? n : 1;
}

/* the item's rectangle in the client area (it may lie outside it) */
static void item_rect(HWND h, PLB lb, int i, RECT *r)
{
    RECT c;
    GetClientRect(h, &c);
    if (multicol(h)) {
        int rows = visible_rows(h, lb), k = i - lb->top;
        int col, row;
        if (rows < 1)
            rows = 1;
        col = k >= 0 ? k / rows : -((-k + rows - 1) / rows);
        row = k - col * rows;
        r->left = col * col_width(lb);
        r->right = r->left + col_width(lb);
        r->top = row * lb->itemh;
        r->bottom = r->top + lb->itemh;
        return;
    }
    r->left = 0;
    r->right = c.right;
    if (varh(h)) {
        int k, y = 0;
        if (i >= lb->top)
            for (k = lb->top; k < i; k++)
                y += item_h(h, lb, k);
        else
            for (k = i; k < lb->top; k++)
                y -= item_h(h, lb, k);
        r->top = y;
        r->bottom = y + item_h(h, lb, i);
        return;
    }
    r->top = (i - lb->top) * lb->itemh;
    r->bottom = r->top + lb->itemh;
}

void sb_show_disabled(HWND hwnd, int bar);
void sb_range_quiet(HWND hwnd, int bar, int min, int max, int pos);

static void notify(HWND h, PLB lb, WORD code)
{
    if ((lbstyle(h) & LBS_NOTIFY) || GetParent(h) != lb->owner)
        send_message(lb->owner, WM_COMMAND, GetWindowWord(h, GWW_ID), MAKELONG(h, code));
}

static void update_scroll(HWND h, PLB lb)
{
    /* not while its redraw is off (US33 rd on 3.1: 37 items added, the
       bar not shown; WM_SETREDRAW TRUE shows it) */
    if (lb->nredraw)
        return;
    if (multicol(h)) {
        int rows = visible_rows(h, lb), cols, max;
        if (!(lb->bars & WS_HSCROLL))
            return;
        if (rows < 1)
            rows = 1;
        cols = (lb->count + rows - 1) / rows;
        max = cols - visible_cols(h, lb);
        if (max < 0)
            max = 0;
        if (!max && (lbstyle(h) & LBS_DISABLENOSCROLL)) {
            sb_show_disabled(h, SB_HORZ);
            return;
        }
        /* 0-100 as the vertical bar, the column as a share of the last
           (US33: column 1 of 0-2 at 50, of 0-3 at 33; hidden, the
           position kept) */
        if (!max) {
            int pos = GetScrollPos(h, SB_HORZ);
            SetScrollRange(h, SB_HORZ, 0, 0, FALSE);        /* hides it */
            if (valid_wnd(h))
                sb_range_quiet(h, SB_HORZ, 0, 100, pos);
            return;
        }
        SetScrollRange(h, SB_HORZ, 0, 100, FALSE);
        SetScrollPos(h, SB_HORZ, mul_div(lb->top / rows < max ? lb->top / rows : max, 100, max), TRUE);
        return;
    }
    /* a single-column box's horizontal bar: LB_SETHORIZONTALEXTENT's
       width less the box's (UNVERIFIED: no horizontal scrolling yet) */
    if (lb->bars & WS_HSCROLL) {
        RECT c;
        int max;
        GetClientRect(h, &c);
        max = lb->hext - c.right;
        if (max <= 0 && (lbstyle(h) & LBS_DISABLENOSCROLL))
            sb_show_disabled(h, SB_HORZ);
        else
            SetScrollRange(h, SB_HORZ, 0, max > 0 ? max : 0, TRUE);
    }
    /* the bars it was made with come and go as needed (an empty range
       hides one, and takes its style away: File Manager's file list, empty
       when made, then never showed its horizontal bar) */
    if (!(lb->bars & WS_VSCROLL))
        return;
    /* 0-100 whatever the count, hidden too (US32) */
    {
        int max = max_top(h, lb);
        /* none when no whole item shows (US34 on 3.1: a combo box's list
           2 high, five items, has no bar; 8 high, items of 6, has one) */
        if (!varh(h) && lb->itemh > 0 && client_h(h) < lb->itemh)
            max = 0;
        if (!max && (lbstyle(h) & LBS_DISABLENOSCROLL)) {
            sb_show_disabled(h, SB_VERT);
            return;
        }
        if (!max) {
            /* hidden, its position kept (US33: emptied at 40, 0-100 at
               40) */
            int pos = GetScrollPos(h, SB_VERT);
            SetScrollRange(h, SB_VERT, 0, 0, FALSE);        /* hides it */
            if (valid_wnd(h))
                sb_range_quiet(h, SB_VERT, 0, 100, pos);
            return;
        }
        SetScrollRange(h, SB_VERT, 0, 100, FALSE);
        SetScrollPos(h, SB_VERT, mul_div(lb->top < max ? lb->top : max, 100, max), TRUE);
    }
}

/* ---- drawing (probe US33 on 3.1, owner-drawn list boxes: every
   WM_DRAWITEM recorded with its action, state, rectangle and order) ----
   3.1 draws what changes, not the whole list: the focus rectangle (the
   caret) is taken away and put back with ODA_FOCUS around each change, a
   selection change is ODA_SELECT for the items that changed and show, and
   a scroll moves the pixels (ScrollWindow) and has the items that come in
   painted at once, the caret away meanwhile. WM_PAINT draws the items of
   the update region whole (ODA_DRAWENTIRE, without ODS_FOCUS), between an
   ODA_FOCUS that takes the caret away and one that puts it back when it
   is shown. An ODA_FOCUS goes to the owner wherever the caret item is,
   shown or not (its rectangle off the client area). A list box that is
   not owner-drawn does the same with its own drawing: the item filled and
   its text, the focus rectangle an XOR. Until 2026-11-20 w16 drew every
   item shown again for any change, with ODA_DRAWENTIRE: an owner whose
   focus rectangle is an XOR (QuickTime's, US32's) showed other pixels. */

static BOOL ownerdraw(HWND h)
{
    return (lbstyle(h) & (LBS_OWNERDRAWFIXED | LBS_OWNERDRAWVARIABLE)) != 0;
}

/* item i shows (a selection change is drawn only then): its rectangle
   meets the client area; in columns, the items from the top one through
   one column more than fit (US33: 3.1 draws the ODA_SELECT of an item in
   the column past the right edge, then scrolls it in) */
static BOOL shows(HWND h, PLB lb, int i)
{
    RECT r, c, x;
    if (i < 0 || i >= lb->count)
        return FALSE;
    if (multicol(h)) {
        int rows = visible_rows(h, lb);
        if (rows < 1)
            rows = 1;
        return i >= lb->top && i < lb->top + rows * (visible_cols(h, lb) + 1);
    }
    GetClientRect(h, &c);
    item_rect(h, lb, i, &r);
    return IntersectRect(&x, &r, &c);
}

/* the item's own drawing (not owner-drawn): filled and its text */
static void plain_item(HWND h, PLB lb, HDC hdc, int i, RECT *r)
{
    PLBITEM it = item(lb, i);
    FillRect(hdc, r, sysbrush[it->sel ? COLOR_HIGHLIGHT : COLOR_WINDOW]);
    SetBkMode(hdc, TRANSPARENT);
    SetTextColor(hdc, syscolor[it->sel ? COLOR_HIGHLIGHTTEXT : COLOR_WINDOWTEXT]);
    if (it->text) {
        LPCSTR s = STR(it->text);
        /* LBS_USETABSTOPS: the stops counted from the item's left edge, the
           text 2 pixels in (US33 tab: a stop of 20 units in the system
           font, 40 pixels, puts the next word at 40) */
        if (lbstyle(h) & LBS_USETABSTOPS)
            TabbedTextOut(hdc, r->left + 2, r->top, s, lstr_len(s), lb->ntabs,
                          lb->tabs ? (int __far *)UH_PTR(&uhObj, lb->tabs) : NULL, r->left);
        else
            TextOut(hdc, r->left + 2, r->top, s, lstr_len(s));
    }
}

/* one WM_DRAWITEM (or its equivalent), on hdc; i -1: the empty list's
   focus rectangle on the first line */
static void draw_one(HWND h, PLB lb, HDC hdc, int i, UINT action, UINT state)
{
    RECT r;
    if (i < 0 || i >= lb->count) {
        GetClientRect(h, &r);
        r.top = 0;
        /* (variable heights: none, US33) */
        r.bottom = varh(h) ? 0 : item_h(h, lb, 0);
        if (multicol(h))
            r.right = col_width(lb);
        i = -1;
    } else
        item_rect(h, lb, i, &r);
    if (ownerdraw(h)) {
        DRAWITEMSTRUCT di;
        di.CtlType = ODT_LISTBOX;
        di.CtlID = GetWindowWord(h, GWW_ID);
        di.itemID = i;
        di.itemAction = action;
        di.itemState = state;
        di.hwndItem = h;
        di.hDC = hdc;
        di.rcItem = r;
        di.itemData = i >= 0 ? item(lb, i)->data : 0;
        send_message(lb->owner, WM_DRAWITEM, di.CtlID, (LPARAM)(DRAWITEMSTRUCT __far *)&di);
        return;
    }
    if (action == ODA_FOCUS) {
        /* black and white: the dots invert the item and the rest is left
           (3.1: the Task List's selected item, white on blue, has a dotted
           yellow and blue rectangle: boot-test case taskman; on an
           unselected item too, US32) */
        COLORREF tc = SetTextColor(hdc, RGB(0, 0, 0)), bc = SetBkColor(hdc, RGB(255, 255, 255));
        DrawFocusRect(hdc, &r);
        SetTextColor(hdc, tc);
        SetBkColor(hdc, bc);
        return;
    }
    if (i >= 0)
        plain_item(h, lb, hdc, i, &r);
}

static UINT item_state(PLB lb, int i)
{
    return i >= 0 && i < lb->count && item(lb, i)->sel ? ODS_SELECTED : 0;
}

/* a DC for drawing outside WM_PAINT: the list box's own client area, its
   font, and the owner told (WM_CTLCOLOR) as for a paint */
static HDC draw_dc(HWND h, PLB lb, HFONT *old)
{
    HDC hdc = ctl_dc(h);
    *old = lb->font ? SelectObject(hdc, lb->font) : 0;
    if (!ownerdraw(h))
        send_message(lb->owner, WM_CTLCOLOR, (WPARAM)hdc, MAKELONG(h, CTLCOLOR_LISTBOX));
    return hdc;
}

static void end_dc(HWND h, HDC hdc, HFONT old)
{
    if (old)
        SelectObject(hdc, old);
    ReleaseDC(h, hdc);
}

/* the caret item lies in what is waiting to be painted: its paint will
   show the caret (US33 on 3.1: LB_SETSEL of a multiple-selection box
   moves the caret to the item it invalidates, and only that item's
   WM_PAINT draws the caret there) */
static BOOL caret_pending(HWND h, PLB lb)
{
    RECT r;
    BOOL in;
    HRGN u;
    if (!GetUpdateRect(h, &r, FALSE) || (u = CreateRectRgn(0, 0, 0, 0)) == 0)
        return FALSE;
    GetUpdateRgn(h, u, FALSE);
    item_rect(h, lb, lb->caret, &r);
    /* (the regions' intersection: RectInRegion counts a rectangle that
       only touches the region's edge) */
    SetRectRgn(hrgnTmp2, r.left, r.top, r.right, r.bottom);
    in = CombineRgn(u, u, hrgnTmp2, RGN_AND) > NULLREGION;
    DeleteObject(u);
    return in;
}

static void caret_draw(HWND h, PLB lb, BOOL on)
{
    HFONT old;
    HDC hdc;
    UINT st;
    if (caret_pending(h, lb))
        return;
    hdc = draw_dc(h, lb, &old);
    st = lb->count ? item_state(lb, lb->caret) : ODS_SELECTED;
    draw_one(h, lb, hdc, lb->count ? lb->caret : -1, ODA_FOCUS, st | (on ? ODS_FOCUS : 0));
    end_dc(h, hdc, old);
}

/* the caret shown: only with the focus and while the box is visible (a
   box given the focus while hidden, as a dialog's first one is, shows no
   caret until it next gets the focus: US32, US33) */
static void caret_on(HWND h, PLB lb)
{
    if (!valid_wnd(h) || lb_of(h) != lb || !lb->focus || lb->caretOn || !lb_shown(h))
        return;
    lb->caretOn = 1;
    caret_draw(h, lb, TRUE);
}

static void caret_off(HWND h, PLB lb)
{
    if (!valid_wnd(h) || lb_of(h) != lb || !lb->caretOn)
        return;
    lb->caretOn = 0;
    /* (not while hidden: a dialog destroyed with its list box focused
       draws nothing, US32) */
    if (lb_shown(h))
        caret_draw(h, lb, FALSE);
}

/* the caret to item i: away from the old one, shown on the new one */
static void caret_move(HWND h, PLB lb, int i)
{
    caret_off(h, lb);
    lb->caret = i;
    caret_on(h, lb);
}

/* item i's selection drawn as it now is (ODA_SELECT), if it shows */
static void sel_draw(HWND h, PLB lb, int i)
{
    HFONT old;
    HDC hdc;
    if (!shows(h, lb, i) || !lb_shown(h))
        return;
    hdc = draw_dc(h, lb, &old);
    draw_one(h, lb, hdc, i, ODA_SELECT, item_state(lb, i) |
             (lb->caretOn && i == lb->caret ? ODS_FOCUS : 0));
    end_dc(h, hdc, old);
}

/* item i's selection set; drawn if it changed (always: draw_always) */
static void set_sel(HWND h, PLB lb, int i, BOOL on, BOOL draw_always)
{
    PLBITEM it;
    if (i < 0 || i >= lb->count)
        return;
    it = item(lb, i);
    if ((it->sel != 0) == (on != 0) && !draw_always)
        return;
    it->sel = on != 0;
    sel_draw(h, lb, i);
}

static void invalidate_item(HWND h, PLB lb, int i, BOOL erase)
{
    RECT r;
    if (lb->nredraw)
        return;
    item_rect(h, lb, i, &r);
    InvalidateRect(h, &r, erase);
}

static void paint(HWND h, PLB lb, HDC hdc, RECT *upd)
{
    RECT r, x;
    int i, last, bm;
    COLORREF tc, bc;
    HFONT old = lb->font ? SelectObject(hdc, lb->font) : 0;
    GetClientRect(h, &r);
    send_message(lb->owner, WM_CTLCOLOR, (WPARAM)hdc, MAKELONG(h, CTLCOLOR_LISTBOX));
    /* the background where no item is (the items' own area is theirs to
       draw, erased only by WM_ERASEBKGND: US33 on 3.1, a caret item
       painted again without erasing keeps its pixels where the owner
       draws nothing) */
    {
        HRGN bg = CreateRectRgnIndirect(&r);
        int k, n = visible_rows(h, lb) + 1;
        if (multicol(h))
            n = (n > 1 ? n - 1 : 1) * (visible_cols(h, lb) + 1);
        else if (varh(h))
            n = lb->count;
        for (k = lb->top; bg && k < lb->count && k < lb->top + n; k++) {
            RECT ir;
            item_rect(h, lb, k, &ir);
            if (!multicol(h) && ir.top >= r.bottom)
                break;
            SetRectRgn(hrgnTmp2, ir.left, ir.top, ir.right, ir.bottom);
            CombineRgn(bg, bg, hrgnTmp2, RGN_DIFF);
        }
        if (bg) {
            FillRgn(hdc, bg, sysbrush[COLOR_WINDOW]);
            DeleteObject(bg);
        }
    }
    tc = GetTextColor(hdc);
    bc = GetBkColor(hdc);
    bm = GetBkMode(hdc);
    if (lb->caretOn)
        draw_one(h, lb, hdc, lb->count ? lb->caret : -1, ODA_FOCUS, item_state(lb, lb->caret));
    /* the items whose rectangles meet the update region */
    if (multicol(h)) {
        int rows = visible_rows(h, lb);
        last = lb->top + (rows > 0 ? rows : 1) * (visible_cols(h, lb) + 1);
    } else
        last = lb->count;
    for (i = lb->top; i < lb->count && i < last; i++) {
        RECT ir;
        item_rect(h, lb, i, &ir);
        if (!multicol(h) && ir.top >= r.bottom)
            break;
        if (IntersectRect(&x, &ir, upd) && IntersectRect(&x, &ir, &r))
            draw_one(h, lb, hdc, i, ODA_DRAWENTIRE, item_state(lb, i));
        if (!valid_wnd(h) || lb_of(h) != lb)
            return;
    }
    if (lb->caretOn) {
        /* the DC's colours and background mode as they were before the
           items (US33 on 3.1: the last ODA_FOCUS of a paint finds OPAQUE
           whatever the owner left; its XOR then undoes the next one) */
        SetTextColor(hdc, tc);
        SetBkColor(hdc, bc);
        SetBkMode(hdc, bm);
        draw_one(h, lb, hdc, lb->count ? lb->caret : -1, ODA_FOCUS,
                 item_state(lb, lb->caret) | ODS_FOCUS);
    }
    if (old)
        SelectObject(hdc, old);
}

static void redraw(HWND h)
{
    PLB lb = lb_of(h);
    if (lb && lb->nredraw)
        return;
    InvalidateRect(h, NULL, FALSE);
}

/* drawn now: visible, and its redraw not turned off */
static BOOL lb_shown(HWND h)
{
    PLB lb = lb_of(h);
    return IsWindowVisible(h) && !(lb && lb->nredraw);
}

/* the client area's pixels moved by dx, dy, what comes in invalidated
   (and what was waiting to be painted moved with them), as ScrollWindow;
   the whole client area when the box is not wholly visible */
static void scroll_bits(HWND h, int dx, int dy)
{
    RECT c, b;
    HDC hdc = ctl_dc(h);
    HRGN upd = CreateRectRgn(0, 0, 0, 0), old = CreateRectRgn(0, 0, 0, 0);
    GetClientRect(h, &c);
    if (GetClipBox(hdc, &b) != SIMPLEREGION || !EqualRect(&b, &c) || !upd || !old ||
        (dx < 0 ? -dx : dx) >= c.right || (dy < 0 ? -dy : dy) >= c.bottom) {
        ReleaseDC(h, hdc);
        InvalidateRect(h, NULL, TRUE);
    } else {
        int had = GetUpdateRgn(h, old, FALSE);
        ScrollDC(hdc, dx, dy, &c, &c, upd, NULL);
        ReleaseDC(h, hdc);
        if (had != NULLREGION && had != ERROR) {
            OffsetRgn(old, dx, dy);
            CombineRgn(upd, upd, old, RGN_OR);
        }
        InvalidateRgn(h, upd, TRUE);
    }
    if (upd)
        DeleteObject(upd);
    if (old)
        DeleteObject(old);
}

/* the top item made t (the caret already taken away by the caller): the
   pixels scrolled and what comes in painted at once (US33: one item of
   variable height at a time is scrolled, more are painted whole) */
static void scroll_to(HWND h, PLB lb, int t)
{
    int old = lb->top, d = 0, k;
    if (t == old)
        return;
    if (multicol(h)) {
        int rows = visible_rows(h, lb);
        if (rows < 1)
            rows = 1;
        lb->top = t;
        update_scroll(h, lb);
        if (lb_shown(h))
            scroll_bits(h, (old - t) / rows * col_width(lb), 0);
    } else {
        if (varh(h)) {
            if (t == old + 1 || t == old - 1)
                for (k = t < old ? t : old; k < (t < old ? old : t); k++)
                    d += item_h(h, lb, k);
            else
                d = 0x7FFF;
            if (t > old)
                d = -d;
        } else
            d = (old - t) * lb->itemh;
        lb->top = t;
        update_scroll(h, lb);
        if (lb_shown(h)) {
            if (d == 0x7FFF || d == -0x7FFF)
                InvalidateRect(h, NULL, TRUE);
            else
                scroll_bits(h, 0, d);
        }
    }
    if (valid_wnd(h) && lb_of(h) == lb)
        UpdateWindow(h);
}

static int add_item(HWND h, PLB lb, int at, LPCSTR s, DWORD data)
{
    PLBITEM it;
    WORD text;
    if (lb->count >= lb->alloc) {
        int n = lb->alloc ? lb->alloc * 2 : 16;
        WORD blk = uh_realloc(&uhObj, lb->items, n * sizeof(LBITEM));
        if (!blk)
            return LB_ERRSPACE;
        lb->items = blk;
        lb->alloc = n;
    }
    text = hasstrings(h) ? uh_strdup_in(&uhObj, s) : 0;    /* before item(): it allocates */
    /* no room for the string: no item (it was added with no text, and
       LB_GETTEXT then copied from the heap's header: USER audit, sweep) */
    if (hasstrings(h) && !text)
        return LB_ERRSPACE;
    if (at < 0 || at > lb->count)
        at = lb->count;
    if (at < lb->count)
        fmemcpy((void __far *)item(lb, at + 1), (void __far *)item(lb, at),
                (lb->count - at) * sizeof(LBITEM));
    lb->count++;
    it = item(lb, at);
    it->sel = 0;
    it->data = data;
    it->text = text;
    it->ht = lb->itemh;
    if (!hasstrings(h))
        it->data = (DWORD)s;
    /* a single-selection box keeps the selection's index, the item now
       there selected (US33 on 3.1: an item inserted at the selection's
       place shows selected); a multiple-selection box's flags go with
       their items */
    if (!multi(h) && lb->cur >= at && lb->cur < lb->count) {
        if (lb->cur + 1 < lb->count)
            item(lb, lb->cur + 1)->sel = 0;
        item(lb, lb->cur)->sel = 1;
    }
    if (lbstyle(h) & LBS_OWNERDRAWVARIABLE) {
        /* each item measured as it comes, the font's height offered (US32:
           3.1 asks with 16, the system font's, in a dialog of MS Sans
           Serif too) */
        MEASUREITEMSTRUCT mi;
        mi.CtlType = ODT_LISTBOX;
        mi.CtlID = GetWindowWord(h, GWW_ID);
        mi.itemID = at;
        mi.itemWidth = 0;
        mi.itemHeight = lb->itemh;
        mi.itemData = (DWORD)s;
        send_message(lb->owner, WM_MEASUREITEM, mi.CtlID, (LPARAM)(MEASUREITEMSTRUCT __far *)&mi);
        if (valid_wnd(h) && lb_of(h) == lb && at < lb->count)
            item(lb, at)->ht = mi.itemHeight;
    }
    return at;
}

/* The order of a sorted list box: lstrcmpi's, except that an item that
   starts with '[' (LB_DIR's directories and drives) comes after every
   other. Observed on 3.1 (US40): "_x", "1a", "mixed.txt", "zzz", "[-b-]",
   "[..]", "[aaa]", "[subdir.x]", although lstrcmpi puts "[a]" before "z"
   (w16 sorted the bracketed names first). */
static int sort_cmp(LPCSTR a, LPCSTR b)
{
    BOOL ba = *a == '[', bb = *b == '[';
    if (ba != bb)
        return ba ? 1 : -1;
    return lstrcmpi(a, b);
}

static int sorted_pos(HWND h, PLB lb, LPCSTR s)
{
    int lo = 0, hi = lb->count;
    if (!hasstrings(h)) {
        UINT id = GetWindowWord(h, GWW_ID);
        while (lo < hi) {
            int mid = lo + (hi - lo) / 2;
            COMPAREITEMSTRUCT ci;
            ci.CtlType = ODT_LISTBOX;
            ci.CtlID = id;
            ci.hwndItem = h;
            ci.itemID1 = (UINT)-1;
            ci.itemData1 = (DWORD)s;
            ci.itemID2 = mid;
            ci.itemData2 = item(lb, mid)->data;
            if ((int)send_message(lb->owner, WM_COMPAREITEM, ci.CtlID,
                                  (LPARAM)(COMPAREITEMSTRUCT __far *)&ci) < 0)
                hi = mid;
            else
                lo = mid + 1;
            if (hi > lb->count)
                hi = lb->count;
            if (lo > hi)
                lo = hi;
        }
        return lo;
    }
    while (lo < hi) {
        int mid = lo + (hi - lo) / 2;
        if (sort_cmp(s, STR(item(lb, mid)->text)) < 0)
            hi = mid;
        else
            lo = mid + 1;
    }
    return lo;
}

static void del_item(PLB lb, int i)
{
    uh_free(&uhObj, item(lb, i)->text);
    if (i + 1 < lb->count)
        fmemcpy((void __far *)item(lb, i), (void __far *)item(lb, i + 1),
                (lb->count - i - 1) * sizeof(LBITEM));
    lb->count--;
    if (lb->cur == i)
        lb->cur = -1;
    else if (lb->cur > i)
        lb->cur--;
}

/* the top item that brings item i into view (the top as it is if it
   shows) */
static int top_for(HWND h, PLB lb, int i)
{
    int rows = visible_rows(h, lb), t = lb->top;
    /* not while it has no height yet (File Manager's file list, filled
       while 0 high: its top went to the selected item, and ".." at the
       head of the list was never shown) */
    if (i < 0 || (varh(h) ? client_h(h) <= 0 : rows < 1))
        return t;
    if (multicol(h)) {
        int cols = visible_cols(h, lb);
        if (i < t)
            t = i / rows * rows;
        else if (i >= t + rows * cols)
            t = (i / rows - cols + 1) * rows;
        return t < 0 ? 0 : t;
    }
    if (varh(h)) {
        RECT r;
        int ch = client_h(h);
        if (i < t)
            return i;
        item_rect(h, lb, i, &r);
        while (t < i && r.bottom > ch) {
            r.bottom -= item_h(h, lb, t);
            t++;
        }
        return t;
    }
    if (i < t)
        t = i;
    else if (i >= t + rows)
        t = i - rows + 1;
    return t < 0 ? 0 : t;
}

/* item i into view (the caret already away) */
static void bring(HWND h, PLB lb, int i)
{
    scroll_to(h, lb, top_for(h, lb, i));
}

/* the box hidden or not yet painted: no drawing, as 3.1 draws nothing
   on a hidden window; the state alone, then a paint */
static BOOL quiet(HWND h)
{
    return !lb_shown(h);
}

/* LB_SETCURSEL, a single-selection box (US33 on 3.1): the caret away, the
   item scrolled into view, the old selection and the new drawn, the caret
   on the item */
static void set_cur(HWND h, PLB lb, int i)
{
    int old = lb->cur;
    if (quiet(h)) {
        if (old >= 0 && old < lb->count)
            item(lb, old)->sel = 0;
        lb->cur = i;
        if (i >= 0) {
            item(lb, i)->sel = 1;
            lb->caret = i;
            lb->top = top_for(h, lb, i);
        }
        update_scroll(h, lb);
        redraw(h);
        return;
    }
    caret_off(h, lb);
    if (i >= 0)
        bring(h, lb, i);
    if (old >= 0 && old != i)
        set_sel(h, lb, old, FALSE, FALSE);
    lb->cur = i;
    if (i >= 0) {
        set_sel(h, lb, i, TRUE, FALSE);
        lb->caret = i;
    }
    caret_on(h, lb);
}

/* the selection to item i the keyboard's way (US33): the caret moved
   there, then away while the selection changes (single: the old one off,
   the new one on; extended: every other item off, the new one drawn
   whether it changed or not, or with Shift the items from the anchor),
   the item scrolled into view, the caret back */
static void key_select(HWND h, PLB lb, int i, BOOL shift)
{
    DWORD st = lbstyle(h);
    int k;
    if (quiet(h)) {
        lb->caret = i;
        if (!(st & LBS_MULTIPLESEL)) {
            for (k = 0; k < lb->count; k++)
                item(lb, k)->sel = (k == i);
            lb->cur = i;
            lb->anchor = i;
        }
        lb->top = top_for(h, lb, i);
        update_scroll(h, lb);
        redraw(h);
        return;
    }
    caret_move(h, lb, i);
    caret_off(h, lb);
    if (st & LBS_MULTIPLESEL)
        ;
    else if (st & LBS_EXTENDEDSEL) {
        int a = lb->anchor;
        if (shift) {
            for (k = 0; k < lb->count; k++)
                set_sel(h, lb, k, (k >= a && k <= i) || (k <= a && k >= i), FALSE);
        } else {
            for (k = 0; k < lb->count; k++)
                if (k != i)
                    set_sel(h, lb, k, FALSE, FALSE);
            set_sel(h, lb, i, TRUE, TRUE);
            lb->anchor = i;
        }
        lb->cur = i;
    } else {
        if (lb->cur >= 0 && lb->cur != i)
            set_sel(h, lb, lb->cur, FALSE, FALSE);
        set_sel(h, lb, i, TRUE, FALSE);
        lb->cur = i;
    }
    bring(h, lb, i);
    caret_on(h, lb);
}

static int find(HWND h, PLB lb, int start, LPCSTR s, BOOL exact)
{
    int i, n = lb->count, len;
    /* owner-drawn without LBS_HASSTRINGS: lParam is item data, equal to
       an item's (or, sorted, WM_COMPAREITEM says 0) - 3.1 SDK. w16 found
       nothing, so File Manager's tree (LB_SELECTSTRING by its item data)
       had no selection, and it showed "\*.*", the root, unanswering */
    if (!hasstrings(h)) {
        for (i = 1; i <= n; i++) {
            int k = (start + i) % n;
            if (lbstyle(h) & LBS_SORT) {
                COMPAREITEMSTRUCT ci;
                ci.CtlType = ODT_LISTBOX;
                ci.CtlID = GetWindowWord(h, GWW_ID);
                ci.hwndItem = h;
                ci.itemID1 = (UINT)-1;
                ci.itemData1 = (DWORD)s;
                ci.itemID2 = k;
                ci.itemData2 = item(lb, k)->data;
                if (!(int)send_message(lb->owner, WM_COMPAREITEM, ci.CtlID,
                                       (LPARAM)(COMPAREITEMSTRUCT __far *)&ci))
                    return k;
            } else if (item(lb, k)->data == (DWORD)s)
                return k;
        }
        return LB_ERR;
    }
    len = lstr_len(s);
    for (i = 1; i <= n; i++) {
        int k = (start + i) % n;
        LPCSTR t;
        char buf[256];
        if (!item(lb, k)->text)
            continue;
        t = STR(item(lb, k)->text);
        if (exact) {
            if (!lstrcmpi(t, s))
                return k;
        } else if (len < sizeof buf) {
            lstr_cpyn(buf, t, len + 1);
            if (!lstrcmpi(buf, s))
                return k;
        } else {
            /* a prefix longer than the buffer: copied into a block of its
               own (into buf it ran over the stack: USER audit, sweep) */
            HGLOBAL g = GlobalAlloc(GMEM_MOVEABLE, (DWORD)len + 1);
            LPSTR p = g ? (LPSTR)GlobalLock(g) : 0;
            BOOL same = FALSE;
            if (p) {
                lstr_cpyn(p, t, len + 1);
                same = !lstrcmpi(p, s);
                GlobalUnlock(g);
            }
            if (g)
                GlobalFree(g);
            if (same)
                return k;
        }
    }
    return LB_ERR;
}

static int hit(HWND h, PLB lb, int x, int y)
{
    int i;
    if (multicol(h)) {
        int rows = visible_rows(h, lb), row = y / (lb->itemh ? lb->itemh : 1);
        if (rows < 1)
            rows = 1;
        if (row >= rows || x < 0 || y < 0)
            return -1;
        i = lb->top + x / col_width(lb) * rows + row;
    } else if (varh(h)) {
        int y0 = 0;
        if (y < 0)
            return -1;
        for (i = lb->top; i < lb->count; i++) {
            y0 += item(lb, i)->ht;
            if (y < y0)
                break;
        }
    } else
        i = lb->top + y / (lb->itemh ? lb->itemh : 1);
    return i < lb->count ? i : -1;
}

/* items from i on changed (added, inserted, deleted): the list painted
   again when they show or lie above (US33: an item added past the last
   one shown draws nothing; in columns the whole box is painted) */
static void changed_from(HWND h, PLB lb, int i)
{
    if (lb->nredraw)                    /* (US33 rd: nothing invalidated) */
        return;
    if (multicol(h) || i <= lb->top + visible_rows(h, lb))
        InvalidateRect(h, NULL, TRUE);
}

/* a click on item i (US33 on 3.1): the caret away, the selection
   changed (single: the old item off, this one on; multiple: this one
   turned over; extended: every other item off and this one on, or with
   Ctrl this one turned over and the caret moved after, or with Shift
   first every item off, then the items from the anchor on, the anchor
   keeping its own state), the caret back on the item. The item the
   bottom edge cuts is not scrolled into view (US32) */
static void click_select(HWND h, PLB lb, int i)
{
    DWORD st = lbstyle(h);
    int k;
    BOOL ctrl = (GetKeyState(VK_CONTROL) & 0x8000) != 0, shift = (GetKeyState(VK_SHIFT) & 0x8000) != 0;
    /* single selection: a click on the item selected draws nothing (US32
       on 3.1: the selected item the bottom edge cuts, clicked) */
    if (!(st & (LBS_MULTIPLESEL | LBS_EXTENDEDSEL)) && i == lb->cur && i == lb->caret)
        return;
    if (quiet(h)) {
        if (st & LBS_MULTIPLESEL)
            item(lb, i)->sel ^= 1;
        else if (!(st & LBS_EXTENDEDSEL) || !ctrl)
            for (k = 0; k < lb->count; k++)
                item(lb, k)->sel = (k == i);
        else
            item(lb, i)->sel ^= 1;
        lb->caret = lb->cur = lb->anchor = i;
        redraw(h);
        return;
    }
    if (st & LBS_MULTIPLESEL) {
        caret_off(h, lb);
        set_sel(h, lb, i, !item(lb, i)->sel, TRUE);
        lb->caret = lb->anchor = i;
        caret_on(h, lb);
        return;
    }
    if (st & LBS_EXTENDEDSEL) {
        if (ctrl) {
            caret_off(h, lb);
            set_sel(h, lb, i, !item(lb, i)->sel, TRUE);
            caret_on(h, lb);
            caret_move(h, lb, i);
            lb->cur = lb->anchor = i;
            return;
        }
        if (shift) {
            int a = lb->anchor;
            caret_off(h, lb);
            for (k = 0; k < lb->count; k++)
                set_sel(h, lb, k, FALSE, FALSE);
            caret_on(h, lb);
            caret_off(h, lb);
            for (k = 0; k < lb->count; k++)
                if (k != a && ((k >= a && k <= i) || (k <= a && k >= i)))
                    set_sel(h, lb, k, TRUE, FALSE);
            caret_on(h, lb);
            caret_move(h, lb, i);
            lb->cur = i;
            return;
        }
        caret_off(h, lb);
        for (k = 0; k < lb->count; k++)
            if (k != i)
                set_sel(h, lb, k, FALSE, FALSE);
        set_sel(h, lb, i, TRUE, FALSE);
        lb->caret = lb->cur = lb->anchor = i;
        caret_on(h, lb);
        return;
    }
    caret_off(h, lb);
    if (lb->cur >= 0 && lb->cur != i)
        set_sel(h, lb, lb->cur, FALSE, FALSE);
    set_sel(h, lb, i, TRUE, FALSE);
    lb->caret = lb->cur = i;
    caret_on(h, lb);
}

/* without LBS_NOINTEGRALHEIGHT (or LBS_OWNERDRAWVARIABLE) the window,
   H high, becomes floor(H / item height) items + 2 high, whatever its
   frame (US32; US4: a simple combo box's list 50 pixels high, three
   items and its border, where 57 were asked for). From the window's height
   now: a dialog's list box made with the system font's items and given
   the dialog's font after is sized again from the first rounding (3.1:
   QuickTime's Control Panel's Files list, 120 asked, 114 with items of
   16, then 106 with items of 13; w16 kept the height asked, 9 items of
   13 where 3.1 has 8, and the list ran over the static below it) */
static void integral(HWND h, PLB lb)
{
    PWND w = valid_wnd(h);
    int H, want;
    if (!w || (lbstyle(h) & LBS_NOINTEGRALHEIGHT) || lb->itemh <= 0 ||
        (lbstyle(h) & LBS_OWNERDRAWVARIABLE))
        return;
    H = w->rcWindow.bottom - w->rcWindow.top;
    want = H / lb->itemh * lb->itemh + 2 * sysmet[SM_CYBORDER];
    if (want != H) {
        lb->sizing = 1;
        set_window_pos(h, 0, 0, 0, w->rcWindow.right - w->rcWindow.left, want,
                       SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE);
        if (valid_wnd(h) && lb_of(h) == lb)
            lb->sizing = 0;
    }
}

/* the dialog manager, after giving a list box the dialog's font: its
   height again from the window's (dlg.c) */
void listbox_refit(HWND h)
{
    PLB lb = lb_of(h);
    if (!lb)
        return;
    integral(h, lb);
    /* 3.1 sends such a list box a WM_SIZE after its first paint (US33: a
       dialog's list box of whole items, not one of LBS_NOINTEGRALHEIGHT
       or LBS_OWNERDRAWVARIABLE), and the list box then shows its caret
       (on, off and on again) and paints the caret item again; in columns
       it paints itself whole. w16 posts it then (late_size). UNVERIFIED
       what 3.1 sends it from */
    if (!(lbstyle(h) & (LBS_NOINTEGRALHEIGHT | LBS_OWNERDRAWVARIABLE)))
        lb->latesize = 1;
}

static void late_size(HWND h, PLB lb)
{
    lb->latesize = 0;
    if (multicol(h)) {
        InvalidateRect(h, NULL, TRUE);
        return;
    }
    caret_on(h, lb);
    caret_off(h, lb);
    caret_on(h, lb);
    if (valid_wnd(h) && lb_of(h) == lb)
        invalidate_item(h, lb, lb->caret, FALSE);
}

LRESULT WINAPI __loadds ListBoxWndProc(HWND h, UINT msg, WPARAM wp, LPARAM lp)
{
    PLB lb;
    if (!valid_wnd(h))
        return 0;
    lb = lb_of(h);
    if (msg == WM_NCCREATE) {
        /* a pixel more each side than asked for (US4: a list box at
           33,188 70x80 had its frame at 32,187 72x82; US32: in any window,
           with a border, WS_DLGFRAME or none) */
        PWND w = valid_wnd(h);
        InflateRect(&w->rcWindow, sysmet[SM_CXBORDER], sysmet[SM_CYBORDER]);
        return DefWindowProc(h, msg, wp, lp);
    }
    if (msg == WM_CREATE) {
        lb = (PLB)LocalAlloc(LPTR, sizeof(LB));
        if (!lb)
            return -1;
        SetWindowWord(h, 0, (WORD)lb);
        PW(h)->flags |= WF_LISTBOX;
        lb->cur = -1;
        lb->itemh = cySysChar;
        lb->bars = lbstyle(h) & (WS_VSCROLL | WS_HSCROLL);
        lb->owner = GetParent(h);
        /* a combo box's popup list: the box, from the start (its
           WM_MEASUREITEM goes to the box's owner as the box's: US34) */
        if (is_combo(((CREATESTRUCT __far *)lp)->hwndParent))
            lb->owner = ((CREATESTRUCT __far *)lp)->hwndParent;
        if (lbstyle(h) & LBS_OWNERDRAWFIXED) {
            MEASUREITEMSTRUCT mi;
            mi.CtlType = ODT_LISTBOX;
            mi.CtlID = GetWindowWord(h, GWW_ID);
            mi.itemID = 0;
            mi.itemWidth = 0;
            mi.itemHeight = lb->itemh;
            mi.itemData = 0;
            send_message(lb->owner, WM_MEASUREITEM, mi.CtlID, (LPARAM)(MEASUREITEMSTRUCT __far *)&mi);
            lb->itemh = mi.itemHeight;
        }
        integral(h, lb);
        update_scroll(h, lb);
        return 0;
    }
    if (!lb)
        return DefWindowProc(h, msg, wp, lp);
    switch (msg) {
    case WM_DESTROY:
        uh_free(&uhObj, lb->tabs);
        while (lb->count)
            del_item(lb, lb->count - 1);
        uh_free(&uhObj, lb->items);
        LocalFree((HLOCAL)lb);
        SetWindowWord(h, 0, 0);
        return 0;
    case WM_PAINT: {
        PAINTSTRUCT ps;
        BeginPaint(h, &ps);
        lb->painted = 1;
        paint(h, lb, ps.hdc, &ps.rcPaint);
        EndPaint(h, &ps);
        if (valid_wnd(h) && lb_of(h) == lb && lb->latesize == 1) {
            RECT c;
            GetClientRect(h, &c);
            lb->latesize = 2;
            PostMessage(h, WM_SIZE, SIZE_RESTORED, MAKELONG(c.right, c.bottom));
        }
        return 0;
    }
    case WM_ERASEBKGND: {
        /* the whole client area (US33: an erased paint loses the caret's
           pixels, a paint without erasing keeps them) */
        RECT c;
        GetClientRect(h, &c);
        FillRect((HDC)wp, &c, sysbrush[COLOR_WINDOW]);
        return 1;
    }
    case WM_SETFONT: {
        HDC hdc = ctl_dc(h);
        TEXTMETRIC tm;
        HFONT old = SelectObject(hdc, wp ? (HFONT)wp : GetStockObject(SYSTEM_FONT));
        GetTextMetrics(hdc, &tm);
        SelectObject(hdc, old);
        ReleaseDC(h, hdc);
        lb->font = (HFONT)wp;
        /* the item height, not the window's (US32: 3.1 leaves it, until
           it is next sized) */
        if (!(lbstyle(h) & (LBS_OWNERDRAWFIXED | LBS_OWNERDRAWVARIABLE))) {
            lb->itemh = tm.tmHeight;
            update_scroll(h, lb);
        }
        if (LOWORD(lp))
            redraw(h);
        return 0;
    }
    case WM_GETFONT:
        return (LONG)(WORD)lb->font;
    case WM_SETFOCUS:
        /* no caret before the box's first paint (US32, US33: 3.1 gives a
           dialog's first list box the focus while the dialog is hidden;
           w16's dialog manager as it is shown) */
        lb->focus = 1;
        if (lb->painted)
            caret_on(h, lb);
        if (valid_wnd(h) && lb_of(h) == lb)
            notify(h, lb, LBN_SETFOCUS);
        return 0;
    case WM_KILLFOCUS:
        caret_off(h, lb);
        if (!valid_wnd(h) || lb_of(h) != lb)
            return 0;
        lb->focus = 0;
        notify(h, lb, LBN_KILLFOCUS);
        return 0;
    case WM_SIZE:
        if (lb->latesize == 2) {
            late_size(h, lb);
            return 0;
        }
        /* columns start on a whole column again (a top worked out while
           the box was one row high is any item) */
        if (multicol(h) && visible_rows(h, lb) > 0)
            lb->top = lb->top / visible_rows(h, lb) * visible_rows(h, lb);
        /* sized: whole items again, from the new height (a scroll bar
           come or gone leaves the window's height, and so the items) */
        if (!lb->sizing)
            integral(h, lb);
        update_scroll(h, lb);
        return 0;
    case WM_GETDLGCODE:
        return DLGC_WANTARROWS | DLGC_WANTCHARS;
    case WM_LBUTTONDOWN:
    case WM_LBUTTONDBLCLK: {
        int i = hit(h, lb, (int)LOWORD(lp), (int)HIWORD(lp));
        if (GetFocus() != h && GetParent(h) == lb->owner)
            SetFocus(h);
        /* WM_LBTRACKPOINT (131h, Schulman ch. 7): the item and the point
           to the owner first; a non-zero answer means it does the rest
           (3.1: File Manager reads Shift and Ctrl here, selects, and
           starts its drag itself, with the capture on its directory
           window). UNVERIFIED: sent, not posted, and the answer's use
           (Wine's list box does so) */
        if (msg == WM_LBUTTONDOWN && GetParent(h) == lb->owner &&
            send_message(lb->owner, 0x0131, (WPARAM)i, lp))
            return 0;
        if (!valid_wnd(h))
            return 0;
        /* the box takes the mouse while the button is down, before it
           tells its owner of the change, the selection following the
           pointer (3.1: File Manager's LBN_SELCHANGE starts its drag
           only while its file list has the capture) */
        if (msg == WM_LBUTTONDOWN && GetParent(h) == lb->owner && valid_wnd(h))
            SetCapture(h);
        if (i < 0)
            return 0;
        click_select(h, lb, i);
        if (!valid_wnd(h) || lb_of(h) != lb)
            return 0;
        notify(h, lb, LBN_SELCHANGE);
        if (!valid_wnd(h) || lb_of(h) != lb)
            return 0;           /* the owner destroyed it (USER audit, sweep) */
        if (msg == WM_LBUTTONDBLCLK)
            notify(h, lb, LBN_DBLCLK);
        /* extended style 2 (Schulman: WES_NOTIFYDRAG; Wine calls it
           WS_EX_DRAGDETECT): a press that turns into a drag sends the
           owner WM_BEGINDRAG (22Ch), 0 and 0 (File Manager does not use it).
           UNVERIFIED: the parameters */
        if (msg == WM_LBUTTONDOWN && (GetWindowLong(h, GWL_EXSTYLE) & 2) && valid_wnd(h)) {
            extern BOOL WINAPI U_DRAGDETECT(HWND h, POINT __far *lp);
            POINT pt;
            pt.x = (int)LOWORD(lp);
            pt.y = (int)HIWORD(lp);
            ClientToScreen(h, &pt);
            if (U_DRAGDETECT(h, &pt) && valid_wnd(h))
                send_message(lb->owner, 0x022C, 0, 0);
        }
        return 0;
    }
    case WM_MOUSEMOVE:
        if (GetCapture() == h && (wp & MK_LBUTTON)) {
            RECT c;
            POINT p;
            int i;
            GetClientRect(h, &c);
            p.x = (int)LOWORD(lp);
            p.y = (int)HIWORD(lp);
            if (!PtInRect(&c, p))
                return 0;       /* UNVERIFIED: 3.1 scrolls here */
            i = hit(h, lb, p.x, p.y);
            if (i < 0 || i == lb->caret)
                return 0;
            if (lbstyle(h) & LBS_EXTENDEDSEL) {
                int k, a = lb->anchor;
                caret_off(h, lb);
                for (k = 0; k < lb->count; k++)
                    set_sel(h, lb, k, (k >= a && k <= i) || (k <= a && k >= i), FALSE);
                lb->caret = lb->cur = i;
                caret_on(h, lb);
            } else if (multi(h))
                return 0;
            else {
                caret_off(h, lb);
                if (lb->cur >= 0 && lb->cur != i)
                    set_sel(h, lb, lb->cur, FALSE, FALSE);
                set_sel(h, lb, i, TRUE, FALSE);
                lb->cur = lb->caret = i;
                caret_on(h, lb);
            }
            notify(h, lb, LBN_SELCHANGE);
        }
        return 0;
    case WM_LBUTTONUP:
        if (GetCapture() == h)
            ReleaseCapture();
        /* extended selection: the caret away and back (US33) */
        if ((lbstyle(h) & LBS_EXTENDEDSEL) && valid_wnd(h) && lb_of(h) == lb) {
            caret_off(h, lb);
            caret_on(h, lb);
        }
        return 0;
    case WM_KEYDOWN: {
        int i = lb->caret, rows = visible_rows(h, lb);
        /* LBS_WANTKEYBOARDINPUT: the owner first (WM_VKEYTOITEM: -2
           done, -1 the list box's own handling, else the item to go to) */
        if (lbstyle(h) & LBS_WANTKEYBOARDINPUT) {
            int r = (int)send_message(lb->owner, WM_VKEYTOITEM, wp, MAKELONG(h, lb->caret));
            if (!valid_wnd(h) || r == -2)
                return 0;
            if (r >= 0) {
                if (r < lb->count) {
                    set_cur(h, lb, r);
                    notify(h, lb, LBN_SELCHANGE);
                }
                return 0;
            }
        }
        if (rows < 1)
            rows = 1;
        switch (wp) {
        /* nothing selected yet: Down or Up selects the caret's item, the
           first (3.1: COMMDLG's file list, Down twice from no selection
           went to the second file; w16 went to the third) */
        case VK_UP: if (multi(h) || lb->cur >= 0) i--; else if (i < 0) i = 0; break;
        case VK_DOWN: if (multi(h) || lb->cur >= 0) i++; else if (i < 0) i = 0; break;
        case VK_LEFT: if (multicol(h)) i -= rows; else i--; break;
        case VK_RIGHT: if (multicol(h)) i += rows; else i++; break;
        case VK_PRIOR:
            if (varh(h)) {
                /* back while the items from there to the caret fit */
                int y = 0, ch = client_h(h);
                while (i > 0 && y + item_h(h, lb, i - 1) <= ch)
                    y += item_h(h, lb, --i);
            } else
                i -= (multicol(h) ? rows * visible_cols(h, lb) : rows) - 1;
            break;
        case VK_NEXT:
            /* variable heights: the caret stays (US33 on 3.1: Page Down
               does nothing in such a box, single selection; extended,
               the caret item is selected again) */
            if (!varh(h))
                i += (multicol(h) ? rows * visible_cols(h, lb) : rows) - 1;
            break;
        case VK_HOME: i = 0; break;
        case VK_END: i = lb->count - 1; break;
        case VK_SPACE:
            /* the caret item's selection turned over and brought into
               view (US33: the caret moved to itself, then away while
               the selection changes and the list scrolls) */
            /* extended: the caret item alone selected, as by the keys */
            if ((lbstyle(h) & LBS_EXTENDEDSEL) && i >= 0 && i < lb->count) {
                key_select(h, lb, i, (GetKeyState(VK_SHIFT) & 0x8000) != 0);
                if (valid_wnd(h) && lb_of(h) == lb)
                    notify(h, lb, LBN_SELCHANGE);
            } else if (multi(h) && i >= 0 && i < lb->count) {
                if (quiet(h)) {
                    item(lb, i)->sel ^= 1;
                    lb->top = top_for(h, lb, i);
                    update_scroll(h, lb);
                    redraw(h);
                } else {
                    caret_move(h, lb, i);
                    caret_off(h, lb);
                    set_sel(h, lb, i, !item(lb, i)->sel, FALSE);
                    bring(h, lb, i);
                    caret_on(h, lb);
                }
                if (valid_wnd(h) && lb_of(h) == lb)
                    notify(h, lb, LBN_SELCHANGE);
            }
            return 0;
        default:
            return 0;
        }
        if (i < 0)
            i = 0;
        if (i >= lb->count)
            i = lb->count - 1;
        if (i < 0)
            return 0;
        /* single selection: nothing when the item is the one selected
           (US33: Page Down in a box of variable heights draws nothing) */
        if (!multi(h) && i == lb->cur && i == lb->caret)
            return 0;
        key_select(h, lb, i, (GetKeyState(VK_SHIFT) & 0x8000) != 0);
        if (valid_wnd(h) && lb_of(h) == lb)
            notify(h, lb, LBN_SELCHANGE);
        return 0;
    }
    case WM_CHAR: {
        char s[2];
        int i;
        /* owner-drawn without strings and LBS_WANTKEYBOARDINPUT: the
           owner says (WM_CHARTOITEM; File Manager's Ctrl+letter drives) */
        if ((lbstyle(h) & LBS_WANTKEYBOARDINPUT) && !hasstrings(h)) {
            int r = (int)send_message(lb->owner, WM_CHARTOITEM, wp, MAKELONG(h, lb->caret));
            if (valid_wnd(h) && r >= 0 && r < lb->count) {
                set_cur(h, lb, r);
                notify(h, lb, LBN_SELCHANGE);
            }
            return 0;
        }
        s[0] = (char)wp;
        s[1] = 0;
        i = find(h, lb, lb->caret, s, FALSE);
        if (i >= 0) {
            set_cur(h, lb, i);
            notify(h, lb, LBN_SELCHANGE);
        }
        return 0;
    }
    case WM_HSCROLL:
        if (multicol(h)) {
            int rows = visible_rows(h, lb), c, cols, t;
            if (rows < 1)
                rows = 1;
            c = lb->top / rows;
            cols = visible_cols(h, lb);
            switch (wp) {
            case SB_LINEUP: c--; break;
            case SB_LINEDOWN: c++; break;
            case SB_PAGEUP: c -= cols; break;
            case SB_PAGEDOWN: c += cols; break;
            case SB_THUMBTRACK:
            case SB_THUMBPOSITION:
                /* (0-100: as the vertical bar's thumb) */
                t = (lb->count + rows - 1) / rows - cols;
                c = mul_div((int)LOWORD(lp), (t > 0 ? t : 0) + 1, 100);
                break;
            case SB_TOP: c = 0; break;
            case SB_BOTTOM: c = lb->count; break;
            }
            t = (lb->count + rows - 1) / rows - cols;
            if (c > t)
                c = t;
            if (c < 0)
                c = 0;
            /* the caret away and back whatever the code (US33) */
            caret_off(h, lb);
            scroll_to(h, lb, c * rows);
            caret_on(h, lb);
        }
        return 0;
    case WM_VSCROLL: {
        /* a page is the whole items shown less one; the thumb's 0-100 is
           n * (max + 1) / 100 rounded (US32) */
        int rows = visible_rows(h, lb), t = lb->top, max = max_top(h, lb);
        if (--rows < 1)
            rows = 1;
        switch (wp) {
        case SB_LINEUP: t--; break;
        case SB_LINEDOWN: t++; break;
        case SB_PAGEUP: t -= rows; break;
        case SB_PAGEDOWN: t += rows; break;
        case SB_THUMBTRACK:
        case SB_THUMBPOSITION: t = mul_div((int)LOWORD(lp), max + 1, 100); break;
        case SB_TOP: t = 0; break;
        case SB_BOTTOM: t = lb->count; break;
        }
        if (t > max)
            t = max;
        if (t < 0)
            t = 0;
        /* the caret away and back for every code, SB_ENDSCROLL and a
           thumb that did not move too (US33 on 3.1) */
        caret_off(h, lb);
        scroll_to(h, lb, t);
        caret_on(h, lb);
        return 0;
    }
    /* ---- LB_ messages ---- */
    case LB_ADDSTRING: {
        int at = (lbstyle(h) & LBS_SORT) ? sorted_pos(h, lb, (LPCSTR)lp) : lb->count;
        at = add_item(h, lb, at, (LPCSTR)lp, 0);
        if (!valid_wnd(h) || lb_of(h) != lb)
            return at;
        update_scroll(h, lb);
        changed_from(h, lb, at);
        return at;
    }
    case LB_INSERTSTRING: {
        int at = add_item(h, lb, (int)wp, (LPCSTR)lp, 0);
        if (!valid_wnd(h) || lb_of(h) != lb)
            return at;
        update_scroll(h, lb);
        changed_from(h, lb, at);
        return at;
    }
    case LB_DELETESTRING:
        if ((int)wp < 0 || (int)wp >= lb->count)
            return LB_ERR;
        del_item(lb, wp);
        if (lb->caret >= lb->count && lb->count)
            lb->caret = lb->count - 1;
        update_scroll(h, lb);
        changed_from(h, lb, (int)wp);
        /* the caret brought into view, the list painted at once (US33 on
           3.1: an item deleted with the caret above the top item) */
        if (!quiet(h) && lb->count && top_for(h, lb, lb->caret) != lb->top) {
            caret_off(h, lb);
            bring(h, lb, lb->caret);
            caret_on(h, lb);
        }
        return lb->count;
    case LB_RESETCONTENT:
        while (lb->count)
            del_item(lb, lb->count - 1);
        lb->top = 0;
        lb->cur = -1;
        lb->caret = 0;
        update_scroll(h, lb);
        /* invalidated even with its redraw off (US33 rd on 3.1), the
           scroll bar left as it was then */
        InvalidateRect(h, NULL, FALSE);
        return 0;
    case WM_SETREDRAW:
        /* its own: WS_VISIBLE kept (US33 rd on 3.1); off, nothing drawn,
           invalidated or scrolled; on again, the scroll bar brought up
           to date and the whole box invalidated */
        if (!wp)
            lb->nredraw = 1;
        else if (lb->nredraw) {
            lb->nredraw = 0;
            update_scroll(h, lb);
            /* UNVERIFIED: the pixels before the next paint (3.1 has
               drawn something of the frame by then; US33 rd logs them
               as ~rd.lb.on.pixels, not compared) */
            if (valid_wnd(h) && lb_of(h) == lb)
                InvalidateRect(h, NULL, TRUE);
        }
        return 0;
    case LB_GETCOUNT:
        return lb->count;
    case LB_GETTEXT:
        if ((int)wp < 0 || (int)wp >= lb->count)
            return LB_ERR;
        if (!hasstrings(h)) {
            *(DWORD __far *)lp = item(lb, wp)->data;
            return 4;
        }
        if (!item(lb, wp)->text) {
            *(LPSTR)lp = 0;
            return 0;
        }
        lstr_cpyn((LPSTR)lp, STR(item(lb, wp)->text), 0x7FFF);
        return lstr_len((LPCSTR)lp);
    case LB_GETTEXTLEN:
        if ((int)wp < 0 || (int)wp >= lb->count)
            return LB_ERR;
        if (!hasstrings(h))
            return 4;
        return item(lb, wp)->text ? lstr_len(STR(item(lb, wp)->text)) : 0;
    case LB_SETCURSEL:
        if ((int)wp >= lb->count)
            return LB_ERR;
        set_cur(h, lb, (int)wp);
        return (int)wp < 0 ? LB_ERR : wp;
    case LB_GETCURSEL:
        return multi(h) ? lb->caret : lb->cur;
    case LB_SETSEL: {
        /* the caret away; the items that change invalidated, not drawn;
           selecting an item moves the caret (the focus rectangle) to it
           (3.1: File Manager selects the file it was pressed on with
           LB_SETSEL alone, and the rectangle is round it); the caret back
           (US33: drawn by the item's paint when that is to come) */
        int i, n = (int)LOWORD(lp);
        if (n != -1 && (n < -1 || n >= lb->count))
            return LB_ERR;
        caret_off(h, lb);
        for (i = n == -1 ? 0 : n; i < lb->count && (n == -1 || i == n); i++)
            if ((item(lb, i)->sel != 0) != (wp != 0)) {
                item(lb, i)->sel = wp != 0;
                if (shows(h, lb, i))
                    invalidate_item(h, lb, i, TRUE);
            }
        if (n != -1 && wp)
            lb->caret = n;
        caret_on(h, lb);
        return 0;
    }
    case LB_GETSEL:
        /* (LB_ERR as a long: w16 gave FFFFh, a selected item, for one
           past the end) */
        return (int)wp >= 0 && (int)wp < lb->count ? (LRESULT)item(lb, wp)->sel : LB_ERR;
    case LB_GETSELCOUNT: {
        int i, n = 0;
        for (i = 0; i < lb->count; i++)
            n += item(lb, i)->sel != 0;
        return n;
    }
    case LB_GETSELITEMS: {
        int i, n = 0;
        for (i = 0; i < lb->count && n < (int)wp; i++)
            if (item(lb, i)->sel)
                ((int __far *)lp)[n++] = i;
        return n;
    }
    case LB_FINDSTRING:
        return find(h, lb, (int)wp, (LPCSTR)lp, FALSE);
    case LB_FINDSTRINGEXACT:
        return find(h, lb, (int)wp, (LPCSTR)lp, TRUE);
    case LB_SELECTSTRING: {
        int i = find(h, lb, (int)wp, (LPCSTR)lp, FALSE);
        if (i >= 0)
            set_cur(h, lb, i);
        return i;
    }
    case LB_SETTOPINDEX: {
        /* no further than the last top (US32); multi-column: the column
           holding it (US33: 5 in columns of 4 is 4) */
        int t = (int)wp;
        if (!multicol(h) && t > max_top(h, lb))
            t = max_top(h, lb);
        if (multicol(h) && visible_rows(h, lb) > 0)
            t = t / visible_rows(h, lb) * visible_rows(h, lb);
        if (t < 0)
            t = 0;
        if (t == lb->top)
            return 0;
        if (quiet(h)) {
            lb->top = t;
            update_scroll(h, lb);
            redraw(h);
            return 0;
        }
        caret_off(h, lb);
        scroll_to(h, lb, t);
        caret_on(h, lb);
        return 0;
    }
    case LB_GETTOPINDEX:
        return lb->top;
    case LB_GETITEMDATA:
        return (int)wp >= 0 && (int)wp < lb->count ? item(lb, wp)->data : LB_ERR;
    case LB_SETITEMDATA:
        if ((int)wp < 0 || (int)wp >= lb->count)
            return LB_ERR;
        item(lb, wp)->data = lp;
        return 0;
    case LB_GETITEMRECT: {
        RECT __far *r = (RECT __far *)lp;
        RECT c;
        item_rect(h, lb, (int)wp, &c);
        *r = c;
        return 0;
    }
    case LB_SETITEMHEIGHT:
        if (varh(h)) {
            if ((int)wp < 0 || (int)wp >= lb->count)
                return LB_ERR;
            item(lb, wp)->ht = LOWORD(lp);
        } else
            lb->itemh = LOWORD(lp);
        update_scroll(h, lb);
        redraw(h);
        return 0;
    case LB_GETITEMHEIGHT:
        /* LBS_OWNERDRAWVARIABLE: that item's (US32) */
        if (varh(h))
            return (int)wp >= 0 && (int)wp < lb->count ? item(lb, wp)->ht : LB_ERR;
        return lb->itemh;
    case LB_SETCARETINDEX:
        if (quiet(h)) {
            lb->caret = (int)wp;
            lb->top = top_for(h, lb, (int)wp);
            update_scroll(h, lb);
            redraw(h);
            return 0;
        }
        caret_off(h, lb);
        lb->caret = (int)wp;
        bring(h, lb, (int)wp);
        caret_on(h, lb);
        return 0;
    case LB_GETCARETINDEX:
        return lb->caret;
    case LB_SETCOLUMNWIDTH:
        /* File Manager's: 0 returned on 3.1 (US-trace) */
        lb->colw = (int)wp;
        update_scroll(h, lb);
        redraw(h);
        return 0;
    case LB_SETHORIZONTALEXTENT:
        lb->hext = (int)wp;
        update_scroll(h, lb);
        return 0;
    case LB_SETTABSTOPS: {
        /* LBS_USETABSTOPS only (US33: 0 without); none: the default,
           TabbedTextOut's 8 average characters, the same as the SDK's 32
           dialog units; one: a stop every that many; the stops are dialog
           units of the box's font as it is now, rounded (US33 tab: 13 units
           of MS Sans Serif, base 7, 23 pixels; of ANSI_VAR_FONT, base 6,
           20; a font given after leaves them) */
        int n = (int)wp, i, bx, by;
        int __far *t;
        if (!(lbstyle(h) & LBS_USETABSTOPS))
            return FALSE;
        uh_free(&uhObj, lb->tabs);
        lb->tabs = 0;
        lb->ntabs = 0;
        if (n > 0 && lp) {
            if (n > 0x7FF0 / sizeof(int))
                return FALSE;
            lb->tabs = uh_alloc(&uhObj, LMEM_FIXED | LMEM_ZEROINIT, n * sizeof(int));
            if (!lb->tabs)
                return FALSE;
            font_base(lb->font, &bx, &by);
            t = (int __far *)UH_PTR(&uhObj, lb->tabs);
            for (i = 0; i < n; i++)
                t[i] = mul_div(((int __far *)lp)[i], bx, 4);
            lb->ntabs = n;
        }
        redraw(h);
        return TRUE;
    }
    case LB_DIR: {
        extern LRESULT lb_dir(HWND lb, UINT attr, LPCSTR spec);   /* dirlist.c */
        return lb_dir(h, (UINT)wp, (LPCSTR)lp);
    }
    case LB_GETHORIZONTALEXTENT:
        return lb->hext;
    }
    return DefWindowProc(h, msg, wp, lp);
}

/* the combo box's drop-down list (class ComboLBox) */
LRESULT WINAPI __loadds ComboLBoxWndProc(HWND h, UINT msg, WPARAM wp, LPARAM lp)
{
    if (msg == WM_MOUSEACTIVATE)
        return MA_NOACTIVATE;
    return ListBoxWndProc(h, msg, wp, lp);
}

/* the combo box sets the list's owner (where notifications go) */
void listbox_set_owner(HWND h, HWND owner)
{
    PLB lb = lb_of(h);
    if (lb)
        lb->owner = owner;
}
