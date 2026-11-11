/*
 * listbox.c - the LISTBOX class (and COMBOLBOX, a combo box's list).
 *
 * The state is a block in USER's local heap, its handle in the window's
 * first extra word (Windows 3.1's class has 2 extra bytes: US2); the items
 * are another block of LBITEMs, strings in their own local blocks.
 * Items are drawn in the window's font one line apart, the text 2 pixels
 * from the left (UNVERIFIED: US4 to compare), selected items in the
 * Highlight colours, the caret item with a dotted focus rectangle.
 */
#include "user.h"

void draw_scrollbar(HDC hdc, RECT *rc, BOOL vert, void *s);

typedef struct LBITEM {
    WORD  text;                 /* local string (0 for no-string owner-draw) */
    DWORD data;
    WORD  sel;
} LBITEM;
typedef LBITEM __near *PLBITEM;

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
} LB;
typedef LB __near *PLB;

static PLB lb_of(HWND h)
{
    return (PLB)GetWindowWord(h, 0);
}

static PLBITEM item(PLB lb, int i)
{
    return (PLBITEM)(lb->items + i * sizeof(LBITEM));
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

static int visible_rows(HWND h, PLB lb)
{
    RECT r;
    GetClientRect(h, &r);
    return lb->itemh ? (r.bottom - r.top) / lb->itemh : 1;
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
    r->top = (i - lb->top) * lb->itemh;
    r->bottom = r->top + lb->itemh;
}

void sb_show_disabled(HWND hwnd, int bar);

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
    {
        int rows = visible_rows(h, lb), max = lb->count - rows;
        if (max < 0)
            max = 0;
        if (!max && (lbstyle(h) & LBS_DISABLENOSCROLL)) {
            sb_show_disabled(h, SB_VERT);
            return;
        }
        SetScrollRange(h, SB_VERT, 0, max, FALSE);
        SetScrollPos(h, SB_VERT, lb->top, TRUE);
    }
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
        di.itemAction = ODA_DRAWENTIRE;
        di.itemState = (it->sel ? ODS_SELECTED : 0) | (lb->focus && i == lb->caret ? ODS_FOCUS : 0);
        di.hwndItem = h;
        di.hDC = hdc;
        di.rcItem = r;
        di.itemData = it->data;
        send_message(lb->owner, WM_DRAWITEM, di.CtlID, (LPARAM)(DRAWITEMSTRUCT __far *)&di);
        /* the focus item again with ODA_FOCUS: File Manager draws its
           focus rectangle (3.1: round "windows" in its tree) only then */
        if ((di.itemState & ODS_FOCUS) && valid_wnd(h)) {
            di.itemAction = ODA_FOCUS;
            send_message(lb->owner, WM_DRAWITEM, di.CtlID, (LPARAM)(DRAWITEMSTRUCT __far *)&di);
        }
        return;
    }
    FillRect(hdc, &r, sysbrush[it->sel ? COLOR_HIGHLIGHT : COLOR_WINDOW]);
    SetBkMode(hdc, TRANSPARENT);
    SetTextColor(hdc, syscolor[it->sel ? COLOR_HIGHLIGHTTEXT : COLOR_WINDOWTEXT]);
    if (it->text) {
        LPCSTR s = (LPCSTR)MAKELP(get_ds(), it->text);
        if (lbstyle(h) & LBS_USETABSTOPS)
            TabbedTextOut(hdc, r.left + 2, r.top, s, lstr_len(s), 0, NULL, r.left + 2);
        else
            TextOut(hdc, r.left + 2, r.top, s, lstr_len(s));
    }
    if (lb->focus && i == lb->caret)
        DrawFocusRect(hdc, &r);
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
    if (lb->count >= lb->alloc) {
        int n = lb->alloc ? lb->alloc * 2 : 16;
        WORD blk = lb->items ? (WORD)LocalReAlloc((HLOCAL)lb->items, n * sizeof(LBITEM), LMEM_MOVEABLE)
                             : (WORD)LocalAlloc(LMEM_FIXED, n * sizeof(LBITEM));
        if (!blk)
            return LB_ERRSPACE;
        lb->items = blk;
        lb->alloc = n;
    }
    if (at < 0 || at > lb->count)
        at = lb->count;
    if (at < lb->count)
        fmemcpy((void __far *)item(lb, at + 1), (void __far *)item(lb, at),
                (lb->count - at) * sizeof(LBITEM));
    lb->count++;
    it = item(lb, at);
    it->sel = 0;
    it->data = data;
    it->text = hasstrings(h) ? local_strdup(s) : 0;
    if (!hasstrings(h))
        it->data = (DWORD)s;
    if (lb->cur >= at && lb->cur >= 0 && lb->count > 1)
        lb->cur++;
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
        if (lstrcmpi(s, (LPCSTR)MAKELP(get_ds(), item(lb, i)->text)) < 0)
            return i;
    return lb->count;
}

static void del_item(PLB lb, int i)
{
    if (item(lb, i)->text)
        LocalFree((HLOCAL)item(lb, i)->text);
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
    if (rows < 1)
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
    if (i < lb->top)
        lb->top = i;
    else if (i >= lb->top + rows)
        lb->top = i - rows + 1;
    if (lb->top < 0)
        lb->top = 0;
    update_scroll(h, lb);
}

static void set_cur(HWND h, PLB lb, int i)
{
    int k;
    for (k = 0; k < lb->count; k++)
        item(lb, k)->sel = (k == i);
    lb->cur = i;
    if (i >= 0) {
        lb->caret = i;
        make_visible(h, lb, i);
    }
    redraw(h);
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
        t = (LPCSTR)MAKELP(get_ds(), item(lb, k)->text);
        if (exact) {
            if (!lstrcmpi(t, s))
                return k;
        } else {
            lstr_cpyn(buf, t, len + 1);
            if (!lstrcmpi(buf, s))
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
    } else
        i = lb->top + y / (lb->itemh ? lb->itemh : 1);
    return i < lb->count ? i : -1;
}

/* without LBS_NOINTEGRALHEIGHT the box shrinks to whole items (US4: a
   simple combo box's list 50 pixels high, three items and its border,
   where 57 were asked for) */
static void integral(HWND h, PLB lb)
{
    RECT r;
    PWND w = valid_wnd(h);
    int extra;
    if (!w || (lbstyle(h) & LBS_NOINTEGRALHEIGHT) || lb->itemh <= 0 ||
        (lbstyle(h) & LBS_OWNERDRAWVARIABLE))
        return;
    GetClientRect(h, &r);
    extra = r.bottom % lb->itemh;
    if (extra && r.bottom > lb->itemh)
        set_window_pos(h, 0, 0, 0, w->rcWindow.right - w->rcWindow.left,
                       w->rcWindow.bottom - w->rcWindow.top - extra,
                       SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE);
}

LRESULT WINAPI ListBoxWndProc(HWND h, UINT msg, WPARAM wp, LPARAM lp)
{
    PLB lb;
    if (!valid_wnd(h))
        return 0;
    lb = lb_of(h);
    if (msg == WM_NCCREATE && (GetWindowLong(h, GWL_STYLE) & WS_BORDER)) {
        /* the border goes outside the rectangle asked for: the items get
           all of it (US4: a list box at 33,188 70x80 had its frame at
           32,187 72x82; UNVERIFIED for other parents than a dialog) */
        PWND w = valid_wnd(h);
        InflateRect(&w->rcWindow, sysmet[SM_CXBORDER], sysmet[SM_CYBORDER]);
        return DefWindowProc(h, msg, wp, lp);
    }
    if (msg == WM_CREATE) {
        lb = (PLB)LocalAlloc(LPTR, sizeof(LB));
        if (!lb)
            return -1;
        SetWindowWord(h, 0, (WORD)lb);
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
        if (lb->items)
            LocalFree((HLOCAL)lb->items);
        LocalFree((HLOCAL)lb);
        SetWindowWord(h, 0, 0);
        return 0;
    case WM_PAINT: {
        PAINTSTRUCT ps;
        BeginPaint(h, &ps);
        paint(h, lb, ps.hdc);
        EndPaint(h, &ps);
        return 0;
    }
    case WM_ERASEBKGND:
        return 1;
    case WM_SETFONT: {
        HDC hdc = GetDC(h);
        TEXTMETRIC tm;
        HFONT old = SelectObject(hdc, wp ? (HFONT)wp : GetStockObject(SYSTEM_FONT));
        GetTextMetrics(hdc, &tm);
        SelectObject(hdc, old);
        ReleaseDC(h, hdc);
        lb->font = (HFONT)wp;
        if (!(lbstyle(h) & (LBS_OWNERDRAWFIXED | LBS_OWNERDRAWVARIABLE)))
            lb->itemh = tm.tmHeight;
        if (LOWORD(lp))
            redraw(h);
        return 0;
    }
    case WM_GETFONT:
        return (LONG)(WORD)lb->font;
    case WM_SETFOCUS:
        lb->focus = 1;
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
        if (i < 0)
            return 0;
        if (multi(h)) {
            item(lb, i)->sel ^= 1;
            lb->caret = i;
            redraw(h);
        } else
            set_cur(h, lb, i);
        notify(h, lb, LBN_SELCHANGE);
        if (msg == WM_LBUTTONDBLCLK)
            notify(h, lb, LBN_DBLCLK);
        return 0;
    }
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
        case VK_UP: i--; break;
        case VK_DOWN: i++; break;
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
        int rows = visible_rows(h, lb), t = lb->top;
        switch (wp) {
        case SB_LINEUP: t--; break;
        case SB_LINEDOWN: t++; break;
        case SB_PAGEUP: t -= rows; break;
        case SB_PAGEDOWN: t += rows; break;
        case SB_THUMBTRACK:
        case SB_THUMBPOSITION: t = (int)LOWORD(lp); break;
        case SB_TOP: t = 0; break;
        case SB_BOTTOM: t = lb->count; break;
        }
        if (t > lb->count - rows)
            t = lb->count - rows;
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
        lstr_cpyn((LPSTR)lp, (LPCSTR)MAKELP(get_ds(), item(lb, wp)->text), 0x7FFF);
        return lstr_len((LPCSTR)lp);
    case LB_GETTEXTLEN:
        if ((int)wp < 0 || (int)wp >= lb->count)
            return LB_ERR;
        return item(lb, wp)->text ? lstr_len((LPCSTR)MAKELP(get_ds(), item(lb, wp)->text)) : 4;
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
        } else if ((int)LOWORD(lp) < lb->count)
            item(lb, LOWORD(lp))->sel = wp != 0;
        else
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
        lb->top = (int)wp;
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
        lb->itemh = LOWORD(lp);
        redraw(h);
        return 0;
    case LB_GETITEMHEIGHT:
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
    case LB_DIR:
        return LB_ERR;          /* UNVERIFIED: not yet */
    case LB_GETHORIZONTALEXTENT:
        return lb->hext;
    }
    return DefWindowProc(h, msg, wp, lp);
}

/* the combo box's drop-down list (class ComboLBox) */
LRESULT WINAPI ComboLBoxWndProc(HWND h, UINT msg, WPARAM wp, LPARAM lp)
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
