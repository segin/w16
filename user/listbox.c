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
 * - a single-selection list box with nothing selected draws its focus
 *   rectangle only in some cases (focus_shown);
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
    WORD   nredraw;
    int    colw;                /* LBS_MULTICOLUMN: a column's width */
    DWORD  bars;                /* WS_VSCROLL/WS_HSCROLL it was made with */
    int    hext;                /* LB_SETHORIZONTALEXTENT */
    BYTE   sizing;              /* integral() resizing it */
    BYTE   caretshown;          /* the focus rectangle shown with nothing
                                   selected (focus_shown) */
    BYTE   painted;             /* WM_PAINT seen */
} LB;
typedef LB __near *PLB;

static PLB lb_of(HWND h)
{
    return (PLB)GetWindowWord(h, 0);
}

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
void sb_range_quiet(HWND hwnd, int bar, int min, int max);

static void notify(HWND h, PLB lb, WORD code)
{
    if ((lbstyle(h) & LBS_NOTIFY) || GetParent(h) != lb->owner)
        send_message(lb->owner, WM_COMMAND, GetWindowWord(h, GWW_ID), MAKELONG(h, code));
}

static void update_scroll(HWND h, PLB lb)
{
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
        SetScrollRange(h, SB_HORZ, 0, max, FALSE);
        SetScrollPos(h, SB_HORZ, lb->top / rows, TRUE);
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
        if (!max && (lbstyle(h) & LBS_DISABLENOSCROLL)) {
            sb_show_disabled(h, SB_VERT);
            return;
        }
        if (!max) {
            SetScrollRange(h, SB_VERT, 0, 0, FALSE);        /* hides it */
            if (valid_wnd(h))
                sb_range_quiet(h, SB_VERT, 0, 100);
            return;
        }
        SetScrollRange(h, SB_VERT, 0, 100, FALSE);
        SetScrollPos(h, SB_VERT, mul_div(lb->top < max ? lb->top : max, 100, max), TRUE);
    }
}

/* The focus rectangle of a single-selection box with nothing selected
   (US32 on 3.1, dialogs of 12 items, the first a tab stop): none when the
   dialog shows with the list box given its height (LBS_NOINTEGRALHEIGHT),
   on the first item when the box was sized to whole items as it was
   made; and it comes when the box, shown and painted, gets the focus
   (SetFocus elsewhere and back), and stays when the selection is taken
   away (LB_SETCURSEL -1). 3.1 gives the focus inside CreateDialog, before
   anything is painted; w16 as the dialog is shown. UNVERIFIED: what 3.1
   keeps that makes the two dialogs differ; this is the rule that gives
   what was seen. */
static BOOL focus_shown(HWND h, PLB lb)
{
    return multi(h) || lb->cur >= 0 || lb->caretshown;
}

static void draw_item(HWND h, PLB lb, HDC hdc, int i)
{
    RECT r, c;
    PLBITEM it;
    GetClientRect(h, &c);
    item_rect(h, lb, i, &r);
    if (r.top < 0 || r.top >= c.bottom || r.left >= c.right || r.right <= 0)
        return;
    if (i >= lb->count) {
        return;
    }
    it = item(lb, i);
    if (lbstyle(h) & (LBS_OWNERDRAWFIXED | LBS_OWNERDRAWVARIABLE)) {
        DRAWITEMSTRUCT di;
        di.CtlType = ODT_LISTBOX;
        di.CtlID = GetWindowWord(h, GWW_ID);
        di.itemID = i;
        /* the item drawn whole without ODS_FOCUS, then the focus item
           again with ODA_FOCUS and ODS_FOCUS (3.1: File Manager's tree
           draws its focus rectangle only then, and its file list draws
           it once - both match only so) */
        di.itemAction = ODA_DRAWENTIRE;
        di.itemState = it->sel ? ODS_SELECTED : 0;
        di.hwndItem = h;
        di.hDC = hdc;
        di.rcItem = r;
        di.itemData = it->data;
        send_message(lb->owner, WM_DRAWITEM, di.CtlID, (LPARAM)(DRAWITEMSTRUCT __far *)&di);
        if (lb->focus && i == lb->caret && focus_shown(h, lb) && valid_wnd(h)) {
            di.itemState |= ODS_FOCUS;
            di.itemAction = ODA_FOCUS;
            send_message(lb->owner, WM_DRAWITEM, di.CtlID, (LPARAM)(DRAWITEMSTRUCT __far *)&di);
        }
        return;
    }
    FillRect(hdc, &r, sysbrush[it->sel ? COLOR_HIGHLIGHT : COLOR_WINDOW]);
    SetBkMode(hdc, TRANSPARENT);
    SetTextColor(hdc, syscolor[it->sel ? COLOR_HIGHLIGHTTEXT : COLOR_WINDOWTEXT]);
    if (it->text) {
        LPCSTR s = STR(it->text);
        if (lbstyle(h) & LBS_USETABSTOPS)
            TabbedTextOut(hdc, r.left + 2, r.top, s, lstr_len(s), 0, NULL, r.left + 2);
        else
            TextOut(hdc, r.left + 2, r.top, s, lstr_len(s));
    }
    if (lb->focus && i == lb->caret && focus_shown(h, lb)) {
        /* the dots invert the item where the pattern is white and leave
           it where it is black (3.1: the Task List's selected item, white
           on blue, has a dotted yellow and blue rectangle; drawn in the
           item's white text and the list's white background it was a
           solid line: boot-test case taskman); on an unselected item too
           (US32: the dots on white in 3.1's phase only so) */
        SetTextColor(hdc, RGB(255, 255, 255));
        SetBkColor(hdc, RGB(0, 0, 0));
        DrawFocusRect(hdc, &r);
    }
}

static void paint(HWND h, PLB lb, HDC hdc)
{
    RECT r;
    int i, rows;
    HFONT old = lb->font ? SelectObject(hdc, lb->font) : 0;
    GetClientRect(h, &r);
    send_message(lb->owner, WM_CTLCOLOR, (WPARAM)hdc, MAKELONG(h, CTLCOLOR_LISTBOX));
    FillRect(hdc, &r, sysbrush[COLOR_WINDOW]);
    rows = visible_rows(h, lb) + 1;
    if (multicol(h))
        rows = (rows - 1 > 0 ? rows - 1 : 1) * (visible_cols(h, lb) + 1);
    else if (varh(h)) {
        /* to the first item that starts below the bottom */
        int y = 0, ch = r.bottom;
        for (rows = 0; lb->top + rows < lb->count && y < ch; rows++)
            y += item(lb, lb->top + rows)->ht;
    }
    for (i = lb->top; i < lb->count && i < lb->top + rows; i++)
        draw_item(h, lb, hdc, i);
    if (old)
        SelectObject(hdc, old);
}

static void redraw(HWND h)
{
    InvalidateRect(h, NULL, FALSE);
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
    if (lb->cur >= at && lb->cur >= 0 && lb->count > 1)
        lb->cur++;
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

static int sorted_pos(HWND h, PLB lb, LPCSTR s)
{
    int i;
    if (!hasstrings(h)) {
        for (i = 0; i < lb->count; i++) {
            COMPAREITEMSTRUCT ci;
            ci.CtlType = ODT_LISTBOX;
            ci.CtlID = GetWindowWord(h, GWW_ID);
            ci.hwndItem = h;
            ci.itemID1 = (UINT)-1;
            ci.itemData1 = (DWORD)s;
            ci.itemID2 = i;
            ci.itemData2 = item(lb, i)->data;
            if ((int)send_message(lb->owner, WM_COMPAREITEM, ci.CtlID,
                                  (LPARAM)(COMPAREITEMSTRUCT __far *)&ci) < 0)
                return i;
        }
        return lb->count;
    }
    for (i = 0; i < lb->count; i++)
        if (lstrcmpi(s, STR(item(lb, i)->text)) < 0)
            return i;
    return lb->count;
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

static void make_visible(HWND h, PLB lb, int i)
{
    int rows = visible_rows(h, lb);
    /* not while it has no height yet (File Manager's file list, filled
       while 0 high: its top went to the selected item, and ".." at the
       head of the list was never shown) */
    if (varh(h) ? client_h(h) <= 0 : rows < 1)
        return;
    if (multicol(h)) {
        int cols = visible_cols(h, lb);
        if (i < lb->top)
            lb->top = i / rows * rows;
        else if (i >= lb->top + rows * cols)
            lb->top = (i / rows - cols + 1) * rows;
        if (lb->top < 0)
            lb->top = 0;
        update_scroll(h, lb);
        return;
    }
    if (varh(h)) {
        RECT r;
        int ch = client_h(h);
        if (i < lb->top)
            lb->top = i;
        else {
            item_rect(h, lb, i, &r);
            while (lb->top < i && r.bottom > ch) {
                r.bottom -= item_h(h, lb, lb->top);
                lb->top++;
            }
        }
        update_scroll(h, lb);
        return;
    }
    if (i < lb->top)
        lb->top = i;
    else if (i >= lb->top + rows)
        lb->top = i - rows + 1;
    if (lb->top < 0)
        lb->top = 0;
    update_scroll(h, lb);
}

/* scroll: brought into view (LB_SETCURSEL, the keys); a click selects the
   item the bottom edge cuts without scrolling (US32) */
static void set_cur_at(HWND h, PLB lb, int i, BOOL scroll)
{
    int k;
    for (k = 0; k < lb->count; k++)
        item(lb, k)->sel = (k == i);
    lb->cur = i;
    if (i >= 0) {
        lb->caret = i;
        if (scroll)
            make_visible(h, lb, i);
    }
    redraw(h);
}

static void set_cur(HWND h, PLB lb, int i)
{
    set_cur_at(h, lb, i, TRUE);
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
        lb->caretshown = 1;     /* (focus_shown) */
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
    if (lb)
        integral(h, lb);
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
        paint(h, lb, ps.hdc);
        EndPaint(h, &ps);
        return 0;
    }
    case WM_ERASEBKGND:
        return 1;
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
        lb->focus = 1;
        if (lb->painted && IsWindowVisible(h))
            lb->caretshown = 1;
        redraw(h);
        notify(h, lb, LBN_SETFOCUS);
        return 0;
    case WM_KILLFOCUS:
        lb->focus = 0;
        redraw(h);
        notify(h, lb, LBN_KILLFOCUS);
        return 0;
    case WM_SIZE:
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
        if ((lbstyle(h) & LBS_EXTENDEDSEL) && !(GetKeyState(VK_CONTROL) & 0x8000)) {
            /* extended selection (3.1 SDK): a click selects that item
               alone, Shift+click the range from the anchor */
            int k, a = (GetKeyState(VK_SHIFT) & 0x8000) ? lb->anchor : i;
            for (k = 0; k < lb->count; k++)
                item(lb, k)->sel = (k >= a && k <= i) || (k <= a && k >= i);
            if (!(GetKeyState(VK_SHIFT) & 0x8000))
                lb->anchor = i;
            lb->caret = i;
            lb->cur = i;
            redraw(h);
        } else if (multi(h)) {
            item(lb, i)->sel ^= 1;
            lb->caret = i;
            lb->anchor = i;
            redraw(h);
        } else
            set_cur_at(h, lb, i, FALSE);
        notify(h, lb, LBN_SELCHANGE);
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
                for (k = 0; k < lb->count; k++)
                    item(lb, k)->sel = (k >= a && k <= i) || (k <= a && k >= i);
                lb->caret = lb->cur = i;
                redraw(h);
            } else if (multi(h))
                return 0;
            else
                set_cur_at(h, lb, i, FALSE);
            notify(h, lb, LBN_SELCHANGE);
        }
        return 0;
    case WM_LBUTTONUP:
        if (GetCapture() == h)
            ReleaseCapture();
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
        case VK_PRIOR: i -= rows - 1; break;
        case VK_NEXT: i += rows - 1; break;
        case VK_HOME: i = 0; break;
        case VK_END: i = lb->count - 1; break;
        case VK_SPACE:
            if (multi(h) && i >= 0) {
                item(lb, i)->sel ^= 1;
                redraw(h);
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
        if (multi(h)) {
            lb->caret = i;
            make_visible(h, lb, i);
            redraw(h);
        } else
            set_cur(h, lb, i);
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
            case SB_THUMBPOSITION: c = (int)LOWORD(lp); break;
            case SB_TOP: c = 0; break;
            case SB_BOTTOM: c = lb->count; break;
            }
            t = (lb->count + rows - 1) / rows - cols;
            if (c > t)
                c = t;
            if (c < 0)
                c = 0;
            if (c * rows != lb->top) {
                lb->top = c * rows;
                update_scroll(h, lb);
                redraw(h);
            }
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
        if (t != lb->top) {
            lb->top = t;
            update_scroll(h, lb);
            redraw(h);
        }
        return 0;
    }
    /* ---- LB_ messages ---- */
    case LB_ADDSTRING: {
        int at = (lbstyle(h) & LBS_SORT) ? sorted_pos(h, lb, (LPCSTR)lp) : lb->count;
        at = add_item(h, lb, at, (LPCSTR)lp, 0);
        update_scroll(h, lb);
        redraw(h);
        return at;
    }
    case LB_INSERTSTRING: {
        int at = add_item(h, lb, (int)wp, (LPCSTR)lp, 0);
        update_scroll(h, lb);
        redraw(h);
        return at;
    }
    case LB_DELETESTRING:
        if ((int)wp < 0 || (int)wp >= lb->count)
            return LB_ERR;
        del_item(lb, wp);
        update_scroll(h, lb);
        redraw(h);
        return lb->count;
    case LB_RESETCONTENT:
        while (lb->count)
            del_item(lb, lb->count - 1);
        lb->top = 0;
        lb->cur = -1;
        lb->caret = 0;
        update_scroll(h, lb);
        redraw(h);
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
        lstr_cpyn((LPSTR)lp, STR(item(lb, wp)->text), 0x7FFF);
        return lstr_len((LPCSTR)lp);
    case LB_GETTEXTLEN:
        if ((int)wp < 0 || (int)wp >= lb->count)
            return LB_ERR;
        return item(lb, wp)->text ? lstr_len(STR(item(lb, wp)->text)) : 4;
    case LB_SETCURSEL:
        if ((int)wp >= lb->count)
            return LB_ERR;
        set_cur(h, lb, (int)wp);
        return (int)wp < 0 ? LB_ERR : wp;
    case LB_GETCURSEL:
        return multi(h) ? lb->caret : lb->cur;
    case LB_SETSEL: {
        int i;
        if ((int)LOWORD(lp) == -1) {
            for (i = 0; i < lb->count; i++)
                item(lb, i)->sel = wp != 0;
        } else if ((int)LOWORD(lp) < lb->count) {
            item(lb, LOWORD(lp))->sel = wp != 0;
            /* selecting an item moves the caret (the focus rectangle)
               to it (3.1: File Manager selects the file it was pressed
               on with LB_SETSEL alone, and the rectangle is round it) */
            if (wp)
                lb->caret = (int)LOWORD(lp);
        } else
            return LB_ERR;
        redraw(h);
        return 0;
    }
    case LB_GETSEL:
        return (int)wp >= 0 && (int)wp < lb->count ? item(lb, wp)->sel : LB_ERR;
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
    case LB_SETTOPINDEX:
        /* no further than the last top (US32) */
        lb->top = (int)wp;
        if (!multicol(h) && lb->top > max_top(h, lb))
            lb->top = max_top(h, lb);
        /* multi-column: the column holding it (UNVERIFIED) */
        if (multicol(h) && visible_rows(h, lb) > 0)
            lb->top = lb->top / visible_rows(h, lb) * visible_rows(h, lb);
        update_scroll(h, lb);
        redraw(h);
        return 0;
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
        lb->caret = (int)wp;
        make_visible(h, lb, (int)wp);
        redraw(h);
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
    case LB_SETTABSTOPS:
        return 1;
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
