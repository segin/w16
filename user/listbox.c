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

static void notify(HWND h, PLB lb, WORD code)
{
    if ((lbstyle(h) & LBS_NOTIFY) || GetParent(h) != lb->owner)
        send_message(lb->owner, WM_COMMAND, GetWindowWord(h, GWW_ID), MAKELONG(h, code));
}

static void update_scroll(HWND h, PLB lb)
{
    if (!(lbstyle(h) & WS_VSCROLL))
        return;
    {
        int rows = visible_rows(h, lb), max = lb->count - rows;
        if (max < 0)
            max = 0;
        SetScrollRange(h, SB_VERT, 0, max, FALSE);
        SetScrollPos(h, SB_VERT, lb->top, TRUE);
    }
}

static void draw_item(HWND h, PLB lb, HDC hdc, int i)
{
    RECT r;
    PLBITEM it;
    int y = (i - lb->top) * lb->itemh;
    GetClientRect(h, &r);
    if (y < 0 || y >= r.bottom)
        return;
    r.top = y;
    r.bottom = y + lb->itemh;
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

static int find(PLB lb, int start, LPCSTR s, BOOL exact)
{
    int i, n = lb->count, len = lstr_len(s);
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

static int hit(HWND h, PLB lb, int y)
{
    int i = lb->top + y / (lb->itemh ? lb->itemh : 1);
    (void)h;
    return i < lb->count ? i : -1;
}

LRESULT WINAPI ListBoxWndProc(HWND h, UINT msg, WPARAM wp, LPARAM lp)
{
    PLB lb;
    if (!valid_wnd(h))
        return 0;
    lb = lb_of(h);
    if (msg == WM_CREATE) {
        lb = (PLB)LocalAlloc(LPTR, sizeof(LB));
        if (!lb)
            return -1;
        SetWindowWord(h, 0, (WORD)lb);
        lb->cur = -1;
        lb->itemh = cySysChar;
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
        update_scroll(h, lb);
        return 0;
    case WM_GETDLGCODE:
        return DLGC_WANTARROWS | DLGC_WANTCHARS;
    case WM_LBUTTONDOWN:
    case WM_LBUTTONDBLCLK: {
        int i = hit(h, lb, (int)HIWORD(lp));
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
        switch (wp) {
        case VK_UP: i--; break;
        case VK_DOWN: i++; break;
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
        s[0] = (char)wp;
        s[1] = 0;
        i = find(lb, lb->caret, s, FALSE);
        if (i >= 0) {
            set_cur(h, lb, i);
            notify(h, lb, LBN_SELCHANGE);
        }
        return 0;
    }
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
        return find(lb, (int)wp, (LPCSTR)lp, FALSE);
    case LB_FINDSTRINGEXACT:
        return find(lb, (int)wp, (LPCSTR)lp, TRUE);
    case LB_SELECTSTRING: {
        int i = find(lb, (int)wp, (LPCSTR)lp, FALSE);
        if (i >= 0)
            set_cur(h, lb, i);
        return i;
    }
    case LB_SETTOPINDEX:
        lb->top = (int)wp;
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
        GetClientRect(h, &c);
        SetRect(r, 0, ((int)wp - lb->top) * lb->itemh, c.right, ((int)wp - lb->top + 1) * lb->itemh);
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
    case LB_SETHORIZONTALEXTENT:
    case LB_SETTABSTOPS:
    case LB_SETCOLUMNWIDTH:
        return 1;
    case LB_DIR:
        return LB_ERR;          /* UNVERIFIED: not yet */
    case LB_GETHORIZONTALEXTENT:
        return 0;
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
