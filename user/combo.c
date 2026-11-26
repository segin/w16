/*
 * combo.c - the COMBOBOX class: simple, drop-down and drop-down list
 * combo boxes, built from an EDIT control (or a drawn box for
 * CBS_DROPDOWNLIST), a LISTBOX (a ComboLBox popup for the drop-down
 * kinds), and the drop-down button (the display's OBM_COMBO arrow).
 *
 * The layout, as probe US34 measured it on Windows 3.1 (tests/apps/us34.c,
 * boot-test case combo; f the font's height, s the system font's):
 * - the selection field's text height T is f + min(f, s) / 4 - 2, from the
 *   font of the moment (the system font when made, then WM_SETFONT's: 14
 *   in MS Sans Serif 8, 18 in the system font); an owner-drawn box asks
 *   WM_MEASUREITEM (item -1, ODT_COMBOBOX, offered T) once, when made, and
 *   keeps the answer whatever font it is given;
 * - the box's height H is T + 6: a closed drop-down box's window is H high
 *   whatever height it was made with (20 in MS Sans Serif 8, as a 12 dialog
 *   unit edit field; 24 in the system font), and CB_GETITEMHEIGHT(-1) is
 *   H; the height it was made with is the dropped control's
 *   (CB_GETDROPPEDCONTROLRECT);
 * - the list hangs from the box's bottom line (top at H - 1), 8 in from
 *   the left under a CBS_DROPDOWN or CBS_SIMPLE (as wide as the box under
 *   a CBS_DROPDOWNLIST), made cy - H high (a simple box's cy - H - 1) and
 *   so whole items and its border (listbox.c's rounding; an
 *   LBS_OWNERDRAWVARIABLE list as it is); a simple box keeps its height;
 * - the button: SM_CXVSCROLL wide at the right, H high, framed; inside a
 *   raised face (a white line left and top, two lines of shadow right and
 *   bottom) and the OBM_COMBO arrow in the middle;
 * - CBS_DROPDOWNLIST: the box framed whole, the button's frame its right
 *   part; the selection field (2, 2) to (button - 1, H - 2), the
 *   highlight's when the box has the focus, with the focus rectangle on
 *   it; the item drawn at (3, 3) (an owner-drawn one in (3, 3, button - 2,
 *   3 + T));
 * - CBS_DROPDOWN: the edit W - 24 wide and H high, the button standing 7
 *   pixels to its right; CBS_SIMPLE: the edit as wide as the box. The edit
 *   has no WS_BORDER but draws a frame of its own (edit.c), and is the
 *   same as an edit field of that size inside.
 */
#include "user.h"

void listbox_set_owner(HWND h, HWND owner);
LRESULT WINAPI __loadds ComboBoxWndProc(HWND h, UINT msg, WPARAM wp, LPARAM lp);

typedef struct CB {
    HWND edit, list;
    HFONT font;
    WORD dropped, focus;
    int  T;                     /* the selection field's text height */
    int  H;                     /* the closed box's height (T + 6) */
    int  HL;                    /* the H the list's height is figured from */
    int  cy;                    /* the dropped control's height (as made) */
    RECT button;
    WORD od;                    /* CBS_OWNERDRAWFIXED/VARIABLE */
    WORD sizing;                /* the box resizing itself */
} CB;
typedef CB __near *PCB;

static HBITMAP bmCombo;

static PCB cb_of(HWND h)
{
    return (PCB)GetWindowWord(h, 0);
}

static UINT kind(HWND h)
{
    return (UINT)(GetWindowLong(h, GWL_STYLE) & 3);
}

/* is h a combo box? (edit.c: a combo box's edit draws its own frame) */
BOOL is_combo(HWND h)
{
    PWND w = valid_wnd(h);
    return w && w->lpfnWndProc == (WNDPROC)ComboBoxWndProc;
}

static void notify(HWND h, WORD code)
{
    send_message(GetParent(h), WM_COMMAND, GetWindowWord(h, GWW_ID), MAKELONG(h, code));
}

/* T from the font (US34) */
static int text_h(HWND h, HFONT f)
{
    HDC hdc = ctl_dc(h);
    TEXTMETRIC tm;
    HFONT old = SelectObject(hdc, f ? f : GetStockObject(SYSTEM_FONT));
    int s;
    GetTextMetrics(hdc, &tm);
    SelectObject(hdc, old);
    ReleaseDC(h, hdc);
    s = tm.tmHeight < cySysChar ? tm.tmHeight : cySysChar;
    return tm.tmHeight + s / 4 - 2;
}

/* the edit, the list's size and the button from H, cy and the width; a
   drop-down box made H high */
static void layout(HWND h, PCB c)
{
    PWND w = valid_wnd(h);
    int W, n;
    if (!w)
        return;
    W = w->rcWindow.right - w->rcWindow.left;
    if (kind(h) == CBS_SIMPLE) {
        c->cy = w->rcWindow.bottom - w->rcWindow.top;
        if (c->edit)
            MoveWindow(c->edit, 0, 0, W, c->H, TRUE);
        n = c->cy - c->HL - 1;
        if (c->list)
            MoveWindow(c->list, 8, c->HL - 1, W - 8, n > 0 ? n : 0, TRUE);
        return;
    }
    if (w->rcWindow.bottom - w->rcWindow.top != c->H) {
        c->sizing = 1;
        set_window_pos(h, 0, 0, 0, W, c->H, SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE);
        if (!valid_wnd(h) || cb_of(h) != c)
            return;
        c->sizing = 0;
    }
    SetRect(&c->button, W - sysmet[SM_CXVSCROLL], 0, W, c->H);
    if (c->edit)
        MoveWindow(c->edit, 0, 0, W - sysmet[SM_CXVSCROLL] - 7, c->H, TRUE);
    n = c->cy - c->HL;
    if (c->list)
        set_window_pos(c->list, 0, 0, 0, kind(h) == CBS_DROPDOWN ? W - 8 : W, n > 0 ? n : 0,
                       SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE);
}

static void show_list(HWND h, PCB c, BOOL show)
{
    RECT r;
    if (kind(h) == CBS_SIMPLE || !c->list || show == (c->dropped != 0))
        return;
    c->dropped = show;
    if (show) {
        GetWindowRect(h, &r);
        notify(h, CBN_DROPDOWN);
        /* its top line on the box's bottom line, sized by layout (US34:
           CB_SETITEMHEIGHT for an item leaves the list as it was) */
        if (kind(h) == CBS_DROPDOWN)
            r.left += 8;
        set_window_pos(c->list, HWND_TOP, r.left, r.top + c->H - 1, 0, 0,
                       SWP_NOSIZE | SWP_SHOWWINDOW | SWP_NOACTIVATE);
        if (c->edit)
            edit_show_sel(c->edit);
        SetCapture(h);
    } else {
        ShowWindow(c->list, SW_HIDE);
        if (GetCapture() == h)
            ReleaseCapture();
    }
    InvalidateRect(h, NULL, FALSE);
}

/* item i's text in a block of its own, NULL if no memory; "" for no
   item. An item can be 7FFFh characters long: copied into a 256-byte
   stack buffer, as it was, a long one ran over the stack (USER audit,
   item 7; US29 cb: 300 characters). item_free gives it back. */
static LPSTR item_text(PCB c, int i)
{
    LONG n = i < 0 ? 0 : send_message(c->list, LB_GETTEXTLEN, i, 0);
    HGLOBAL g;
    LPSTR p;
    if (n < 4)
        n = 4;                  /* an owner-drawn item's data: 4 bytes */
    g = GlobalAlloc(GMEM_MOVEABLE | GMEM_ZEROINIT, n + 1);
    if (!g)
        return NULL;
    p = (LPSTR)GlobalLock(g);
    if (i >= 0)
        send_message(c->list, LB_GETTEXT, i, (LPARAM)p);
    p[n] = 0;
    return p;
}

static void item_free(LPSTR p)
{
    HGLOBAL g;
    if (!p)
        return;
    g = (HGLOBAL)LOWORD(GlobalHandle(SELECTOROF(p)));
    GlobalUnlock(g);
    GlobalFree(g);
}

static void sel_to_edit(HWND h, PCB c)
{
    int i = (int)send_message(c->list, LB_GETCURSEL, 0, 0);
    LPSTR t = item_text(c, i);
    if (c->edit) {
        SetWindowText(c->edit, t ? t : "");
        send_message(c->edit, EM_SETSEL, 0, MAKELONG(0, -1));
    }
    item_free(t);
    InvalidateRect(h, NULL, FALSE);
}

static void paint(HWND h, PCB c, HDC hdc)
{
    RECT r, box;
    HFONT old = SelectObject(hdc, c->font ? c->font : GetStockObject(SYSTEM_FONT));
    GetClientRect(h, &r);
    /* not over the edit and a simple box's list (US34 on 3.1: a simple
       box given a font twice keeps its list's pixels) */
    {
        HWND k[2];
        int i;
        k[0] = c->edit;
        k[1] = kind(h) == CBS_SIMPLE ? c->list : 0;
        for (i = 0; i < 2; i++)
            if (k[i] && IsWindowVisible(k[i])) {
                RECT cr;
                GetWindowRect(k[i], &cr);
                ScreenToClient(h, (POINT FAR *)&cr.left);
                ScreenToClient(h, (POINT FAR *)&cr.right);
                ExcludeClipRect(hdc, cr.left, cr.top, cr.right, cr.bottom);
            }
    }
    FillRect(hdc, &r, sysbrush[COLOR_WINDOW]);
    if (kind(h) != CBS_SIMPLE) {
        RECT in, f;
        int H = c->H;
        /* the field highlighted with the focus, not while the list is
           down (US34 on 3.1) */
        BOOL hl = c->focus && !c->dropped;
        if (kind(h) == CBS_DROPDOWNLIST) {
            SetRect(&box, 0, 0, r.right, H);
            FrameRect(hdc, &box, sysbrush[COLOR_WINDOWFRAME]);
        }
        /* the button (US34) */
        FrameRect(hdc, &c->button, sysbrush[COLOR_WINDOWFRAME]);
        SetRect(&in, c->button.left + 1, 1, c->button.right - 1, H - 1);
        FillRect(hdc, &in, sysbrush[COLOR_BTNFACE]);
        {
            HBRUSH ob = SelectObject(hdc, sysbrush[COLOR_BTNHIGHLIGHT]);
            PatBlt(hdc, in.left, in.top, 1, in.bottom - in.top - 1, PATCOPY);
            PatBlt(hdc, in.left, in.top, in.right - in.left - 1, 1, PATCOPY);
            SelectObject(hdc, sysbrush[COLOR_BTNSHADOW]);
            PatBlt(hdc, in.right - 1, in.top, 1, in.bottom - in.top, PATCOPY);
            PatBlt(hdc, in.right - 2, in.top + 1, 1, in.bottom - in.top - 1, PATCOPY);
            PatBlt(hdc, in.left, in.bottom - 1, in.right - in.left, 1, PATCOPY);
            PatBlt(hdc, in.left + 1, in.bottom - 2, in.right - in.left - 1, 1, PATCOPY);
            SelectObject(hdc, ob);
        }
        if (!bmCombo)
            bmCombo = LoadBitmap(0, MAKEINTRESOURCE(OBM_COMBO));
        {
            BITMAP bm;
            HBITMAP ob;
            /* only the arrow's black: the face, highlight and shadow
               stay where it lies over them (US34 on 3.1: in an 11-high
               box the arrow's top over the white line, its underline
               over the shadow) */
            COLORREF tc = SetTextColor(hdc, RGB(0, 0, 0)), bc = SetBkColor(hdc, RGB(255, 255, 255));
            GetObject(bmCombo, sizeof bm, &bm);
            ob = SelectObject(hdcMem, bmCombo);
            BitBlt(hdc, in.left + (in.right - in.left - bm.bmWidth) / 2,
                   in.top + (in.bottom - in.top - bm.bmHeight) / 2, bm.bmWidth, bm.bmHeight,
                   hdcMem, 0, 0, bm.bmBitsPixel == 1 && bm.bmPlanes == 1 ? SRCAND : SRCCOPY);
            SelectObject(hdcMem, ob);
            SetTextColor(hdc, tc);
            SetBkColor(hdc, bc);
        }
        /* the selection field (US34: 2 in from the frame, a pixel short
           of the button) */
        SetRect(&f, 2, 2, c->button.left - 1, H - 2);
        if (kind(h) == CBS_DROPDOWNLIST && c->od) {
            /* an owner-drawn selection field: the owner draws it (observed
               on 3.1 with Control Panel's Color dialog: one WM_DRAWITEM,
               ODA_DRAWENTIRE, the item 3 pixels in, T high, ending 2
               pixels before the button; with the focus ODS_SELECTED |
               ODS_FOCUS, without it neither) */
            DRAWITEMSTRUCT di;
            /* the field filled first and the DC's colours set, as for a
               plain item (US34 on 3.1: an owner whose FillRect does
               nothing gets the highlight all the same, and its focus
               rectangle's dots white and highlight) */
            FillRect(hdc, &f, sysbrush[hl ? COLOR_HIGHLIGHT : COLOR_WINDOW]);
            SetTextColor(hdc, syscolor[hl ? COLOR_HIGHLIGHTTEXT : COLOR_WINDOWTEXT]);
            SetBkColor(hdc, syscolor[hl ? COLOR_HIGHLIGHT : COLOR_WINDOW]);
            di.CtlType = ODT_COMBOBOX;
            di.CtlID = GetWindowWord(h, GWW_ID);
            di.itemID = (UINT)(int)send_message(c->list, LB_GETCURSEL, 0, 0);
            di.itemAction = ODA_DRAWENTIRE;
            di.itemState = hl ? ODS_SELECTED | ODS_FOCUS : 0;
            di.hwndItem = h;
            di.hDC = hdc;
            SetRect(&di.rcItem, 3, 3, c->button.left - 2, 3 + c->T);
            di.itemData = di.itemID == (UINT)-1 ? 0 :
                          send_message(c->list, LB_GETITEMDATA, di.itemID, 0);
            send_message(GetParent(h), WM_DRAWITEM, di.CtlID, (LPARAM)(DRAWITEMSTRUCT __far *)&di);
        } else if (kind(h) == CBS_DROPDOWNLIST) {
            int i = (int)send_message(c->list, LB_GETCURSEL, 0, 0);
            LPSTR buf = item_text(c, i);
            FillRect(hdc, &f, sysbrush[hl ? COLOR_HIGHLIGHT : COLOR_WINDOW]);
            SetBkMode(hdc, TRANSPARENT);
            /* the colours the focus rectangle's dots take too (US34 on
               3.1: its dots black and yellow over the highlight, every
               other one, not every one inverted) */
            SetTextColor(hdc, syscolor[hl ? COLOR_HIGHLIGHTTEXT : COLOR_WINDOWTEXT]);
            SetBkColor(hdc, syscolor[hl ? COLOR_HIGHLIGHT : COLOR_WINDOW]);
            if (buf)
                ExtTextOut(hdc, 3, 3, ETO_CLIPPED, &f, buf, lstr_len(buf), NULL);
            item_free(buf);
            if (hl)
                DrawFocusRect(hdc, &f);
        }
    }
    SelectObject(hdc, old);
}

LRESULT WINAPI __loadds ComboBoxWndProc(HWND h, UINT msg, WPARAM wp, LPARAM lp)
{
    PCB c;
    if (!valid_wnd(h))
        return 0;
    c = cb_of(h);
    if (msg == WM_CREATE) {
        CREATESTRUCT __far *cs = (CREATESTRUCT __far *)lp;
        DWORD s = cs->style, ls;
        HINSTANCE hi = cs->hInstance;
        c = (PCB)LocalAlloc(LPTR, sizeof(CB));
        if (!c)
            return -1;
        SetWindowWord(h, 0, (WORD)c);
        /* the scroll bar styles are the list's, not the box's (US34: a
           simple box without WS_VSCROLL has a list without one) */
        {
            PWND w = valid_wnd(h);
            w->style &= ~(WS_VSCROLL | WS_HSCROLL);
            calc_client(w);
        }
        c->od = (WORD)(s & (CBS_OWNERDRAWFIXED | CBS_OWNERDRAWVARIABLE));
        c->T = text_h(h, 0);
        if (c->od) {
            /* asked once, before the list is made (US34: item -1 offered
               18, then the list's items) */
            MEASUREITEMSTRUCT mi;
            mi.CtlType = ODT_COMBOBOX;
            mi.CtlID = GetWindowWord(h, GWW_ID);
            mi.itemID = (UINT)-1;
            mi.itemWidth = 0;
            mi.itemHeight = c->T;
            mi.itemData = 0;
            send_message(GetParent(h), WM_MEASUREITEM, mi.CtlID, (LPARAM)(MEASUREITEMSTRUCT __far *)&mi);
            if (!valid_wnd(h) || cb_of(h) != c)
                return -1;
            c->T = mi.itemHeight;
        }
        c->H = c->HL = c->T + 6;
        c->cy = cs->cy;
        if (kind(h) != CBS_DROPDOWNLIST)
            c->edit = create_window(0, "Edit", NULL, WS_CHILD | WS_VISIBLE |
                                    ((s & CBS_AUTOHSCROLL) ? ES_AUTOHSCROLL : 0) |
                                    ((s & CBS_OEMCONVERT) ? ES_OEMCONVERT : 0),
                                    0, 0, 0, 0, h, (HMENU)1001, hi, NULL);
        ls = LBS_NOTIFY | (s & (WS_VSCROLL | WS_HSCROLL)) | c->od |
             ((s & CBS_SORT) ? LBS_SORT : 0) | ((s & CBS_HASSTRINGS) ? LBS_HASSTRINGS : 0) |
             ((s & CBS_NOINTEGRALHEIGHT) ? LBS_NOINTEGRALHEIGHT : 0) |
             ((s & CBS_DISABLENOSCROLL) ? LBS_DISABLENOSCROLL : 0);
        if (kind(h) == CBS_SIMPLE)
            c->list = create_window(0, "ListBox", NULL, WS_CHILD | WS_VISIBLE | WS_BORDER | ls,
                                    0, 0, 0, 0, h, (HMENU)1000, hi, NULL);
        else {
            RECT r;
            GetWindowRect(h, &r);
            c->list = create_window(0, "ComboLBox", NULL, WS_POPUP | WS_BORDER | ls,
                                    r.left, r.bottom, r.right - r.left, 0, h, 0, hi, NULL);
        }
        if (c->list)
            listbox_set_owner(c->list, h);
        /* a drop-down box is H high, whatever it was made (US34) */
        if (kind(h) != CBS_SIMPLE) {
            PWND w = valid_wnd(h);
            w->rcWindow.bottom = w->rcWindow.top + c->H;
            calc_client(w);
        }
        layout(h, c);
        return 0;
    }
    if (!c)
        return DefWindowProc(h, msg, wp, lp);
    switch (msg) {
    case WM_MEASUREITEM:
    case WM_DRAWITEM:
    case WM_COMPAREITEM:
    case WM_DELETEITEM:
        /* the list's, to the owner as the combo box's (3.1 SDK: CtlType
           ODT_COMBOBOX, the box's id and handle; US34: CtlType 3) */
        if (msg == WM_MEASUREITEM) {
            MEASUREITEMSTRUCT __far *mi = (MEASUREITEMSTRUCT __far *)lp;
            mi->CtlType = ODT_COMBOBOX;
            mi->CtlID = GetWindowWord(h, GWW_ID);
        } else if (msg == WM_DRAWITEM) {
            DRAWITEMSTRUCT __far *di = (DRAWITEMSTRUCT __far *)lp;
            di->CtlType = ODT_COMBOBOX;
            di->CtlID = GetWindowWord(h, GWW_ID);
            di->hwndItem = h;
        } else if (msg == WM_COMPAREITEM) {
            COMPAREITEMSTRUCT __far *ci = (COMPAREITEMSTRUCT __far *)lp;
            ci->CtlType = ODT_COMBOBOX;
            ci->CtlID = GetWindowWord(h, GWW_ID);
            ci->hwndItem = h;
        } else {
            DELETEITEMSTRUCT __far *d = (DELETEITEMSTRUCT __far *)lp;
            d->CtlType = ODT_COMBOBOX;
            d->CtlID = GetWindowWord(h, GWW_ID);
            d->hwndItem = h;
        }
        return send_message(GetParent(h), msg, GetWindowWord(h, GWW_ID), lp);
    case WM_DESTROY:
        if (c->list && kind(h) != CBS_SIMPLE)
            DestroyWindow(c->list);
        return 0;
    case WM_NCDESTROY:
        LocalFree((HLOCAL)c);
        SetWindowWord(h, 0, 0);
        return 0;
    case WM_PAINT: {
        PAINTSTRUCT ps;
        BeginPaint(h, &ps);
        paint(h, c, ps.hdc);
        EndPaint(h, &ps);
        return 0;
    }
    case WM_ERASEBKGND:
        return 1;
    case WM_SIZE:
        /* a drop-down box sized: the new height is the dropped control's
           (UNVERIFIED) */
        if (!c->sizing && kind(h) != CBS_SIMPLE && (int)HIWORD(lp) != c->H)
            c->cy = (int)HIWORD(lp);
        if (!c->sizing)
            layout(h, c);
        return 0;
    case WM_SETFONT:
        c->font = (HFONT)wp;
        if (c->edit)
            send_message(c->edit, WM_SETFONT, wp, lp);
        if (c->list)
            send_message(c->list, WM_SETFONT, wp, lp);
        /* the height from the new font; an owner-drawn box keeps its own
           (US34) */
        if (!c->od) {
            c->T = text_h(h, c->font);
            c->H = c->HL = c->T + 6;
        }
        layout(h, c);
        if (LOWORD(lp))
            InvalidateRect(h, NULL, TRUE);
        return 0;
    case WM_GETFONT:
        return (LONG)(WORD)c->font;
    case WM_SETFOCUS:
        if (c->edit)
            SetFocus(c->edit);
        c->focus = 1;
        InvalidateRect(h, NULL, FALSE);
        notify(h, CBN_SETFOCUS);
        return 0;
    case WM_KILLFOCUS:
        c->focus = 0;
        show_list(h, c, FALSE);
        InvalidateRect(h, NULL, FALSE);
        notify(h, CBN_KILLFOCUS);
        return 0;
    case WM_GETDLGCODE:
        return DLGC_WANTARROWS | DLGC_WANTCHARS;
    case WM_LBUTTONDOWN:
        if (c->dropped) {
            POINT pt;
            RECT lr;
            pt.x = (int)LOWORD(lp);
            pt.y = (int)HIWORD(lp);
            ClientToScreen(h, &pt);
            GetWindowRect(c->list, &lr);
            if (PtInRect(&lr, pt)) {
                ScreenToClient(c->list, &pt);
                send_message(c->list, WM_LBUTTONDOWN, wp, MAKELONG(pt.x, pt.y));
                /* the parent may have destroyed the combo box on the
                   list's notification (USER audit, sweep) */
                if (!valid_wnd(h) || cb_of(h) != c)
                    return 0;
                sel_to_edit(h, c);
                notify(h, CBN_SELCHANGE);
                if (!valid_wnd(h) || cb_of(h) != c)
                    return 0;
            }
            show_list(h, c, FALSE);
            notify(h, CBN_CLOSEUP);
        } else {
            SetFocus(h);
            show_list(h, c, TRUE);
        }
        return 0;
    case WM_KEYDOWN:
        if (wp == VK_F4 || ((wp == VK_DOWN || wp == VK_UP) && (GetKeyState(VK_MENU) & 0x8000))) {
            show_list(h, c, !c->dropped);
            return 0;
        }
        if ((wp == VK_RETURN || wp == VK_ESCAPE) && c->dropped) {
            show_list(h, c, FALSE);
            return 0;
        }
        if (wp == VK_UP || wp == VK_DOWN || wp == VK_HOME || wp == VK_END ||
            wp == VK_PRIOR || wp == VK_NEXT) {
            send_message(c->list, WM_KEYDOWN, wp, lp);
            sel_to_edit(h, c);
            notify(h, CBN_SELCHANGE);
        }
        return 0;
    case WM_CHAR:
        if (c->edit)
            return send_message(c->edit, msg, wp, lp);
        send_message(c->list, WM_CHAR, wp, lp);
        sel_to_edit(h, c);
        return 0;
    case WM_COMMAND:
        if (LOWORD((DWORD)lp) == (WORD)c->list && HIWORD(lp) == LBN_SELCHANGE) {
            sel_to_edit(h, c);
            notify(h, CBN_SELCHANGE);
        } else if (LOWORD((DWORD)lp) == (WORD)c->list && HIWORD(lp) == LBN_DBLCLK)
            notify(h, CBN_DBLCLK);
        else if (c->edit && LOWORD((DWORD)lp) == (WORD)c->edit && HIWORD(lp) == EN_CHANGE)
            notify(h, CBN_EDITCHANGE);
        return 0;
    case WM_SETTEXT:
    case WM_GETTEXT:
    case WM_GETTEXTLENGTH:
        if (c->edit)
            return send_message(c->edit, msg, wp, lp);
        if (msg == WM_GETTEXT || msg == WM_GETTEXTLENGTH) {
            int i = (int)send_message(c->list, LB_GETCURSEL, 0, 0);
            LPSTR buf;
            LONG n;
            /* a CBS_DROPDOWNLIST's (US29 cb on 3.1): WM_GETTEXTLENGTH is
               -1 whatever is selected; WM_GETTEXT copies what fits and
               returns the text's length, or the buffer's size when it
               was cut (20 for 19 characters of 300). The size is
               compared as an int: one of 32768 or more counts as cut,
               wp - 1 bytes copied, a NUL at wp - 1, wp returned (US36 on
               3.1: 3.1 copies what follows the item's text in its heap,
               w16 zeros) */
            if (msg == WM_GETTEXTLENGTH)
                return -1;
            if (!wp || GP_BAD((LPSTR)lp))
                return 0;
            if ((buf = item_text(c, i)) == NULL)
                return 0;
            n = lstr_len(buf);
            if (n < (LONG)(int)wp) {
                lstr_cpyn((LPSTR)lp, buf, wp);
                item_free(buf);
                return n;
            }
            lstr_cpyn((LPSTR)lp, buf, (WORD)n < wp ? (WORD)n + 1 : wp);
            item_free(buf);
            if ((WORD)n + 1 < wp)
                fmemset((LPSTR)lp + (WORD)n + 1, 0, wp - (WORD)n - 1);
            return (LONG)(int)wp;
        }
        return 0;
    /* ---- CB_ messages: most are the list's ---- */
    case CB_ADDSTRING: return send_message(c->list, LB_ADDSTRING, wp, lp);
    case CB_DIR: return send_message(c->list, LB_DIR, wp, lp);
    case CB_INSERTSTRING: return send_message(c->list, LB_INSERTSTRING, wp, lp);
    case CB_DELETESTRING: return send_message(c->list, LB_DELETESTRING, wp, lp);
    case CB_GETCOUNT: return send_message(c->list, LB_GETCOUNT, wp, lp);
    case CB_GETLBTEXT: return send_message(c->list, LB_GETTEXT, wp, lp);
    case CB_GETLBTEXTLEN: return send_message(c->list, LB_GETTEXTLEN, wp, lp);
    case CB_FINDSTRING: return send_message(c->list, LB_FINDSTRING, wp, lp);
    /* (w16 left it to DefWindowProc, 0: COMMDLG's Font dialog, which
       looks each face up before adding it, found every face at item 0
       of its empty list and showed none) */
    case CB_FINDSTRINGEXACT: return send_message(c->list, LB_FINDSTRINGEXACT, wp, lp);
    case CB_GETITEMDATA: return send_message(c->list, LB_GETITEMDATA, wp, lp);
    case CB_SETITEMDATA: return send_message(c->list, LB_SETITEMDATA, wp, lp);
    case CB_GETCURSEL: return send_message(c->list, LB_GETCURSEL, wp, lp);
    case CB_RESETCONTENT:
        send_message(c->list, LB_RESETCONTENT, 0, 0);
        if (c->edit)
            SetWindowText(c->edit, "");
        InvalidateRect(h, NULL, FALSE);
        return 0;
    case CB_SETCURSEL: {
        LONG r = send_message(c->list, LB_SETCURSEL, wp, lp);
        /* the item chosen comes to the list's top (US34 on 3.1: item 1
           of 5 at the top of a simple box's list two items high) */
        if ((int)r >= 0)
            send_message(c->list, LB_SETTOPINDEX, (WPARAM)r, 0);
        sel_to_edit(h, c);
        return r;
    }
    case CB_SELECTSTRING: {
        LONG r = send_message(c->list, LB_SELECTSTRING, wp, lp);
        sel_to_edit(h, c);
        return r;
    }
    case CB_SHOWDROPDOWN:
        show_list(h, c, wp != 0);
        return 1;
    case CB_GETDROPPEDSTATE:
        return c->dropped;
    case CB_GETEDITSEL:
        return c->edit ? send_message(c->edit, EM_GETSEL, 0, 0) : CB_ERR;
    case CB_SETEDITSEL:
        return c->edit ? send_message(c->edit, EM_SETSEL, 0, lp) : CB_ERR;
    case CB_LIMITTEXT:
        return c->edit ? send_message(c->edit, EM_LIMITTEXT, wp, 0) : CB_ERR;
    case CB_GETITEMHEIGHT:
        /* -1: the box's height (US34: an owner-drawn box answering 10
           to WM_MEASUREITEM is 16 high and says 16) */
        if ((int)wp == -1)
            return c->H;
        return send_message(c->list, LB_GETITEMHEIGHT, wp, 0);
    case CB_SETITEMHEIGHT:
        if ((int)wp == -1) {
            if (c->od) {
                c->T = LOWORD(lp);
                c->H = c->HL = c->T + 6;
            } else if (kind(h) == CBS_SIMPLE) {
                /* US34 on 3.1, a box of MS Sans Serif 8 given 20: a
                   simple one shrinks to its list's bottom, edit and list
                   as they were; a drop-down one stays 20 high and says
                   20, but its list is figured from 26. UNVERIFIED beyond
                   that one case */
                RECT lr, wr;
                if (c->list) {
                    GetWindowRect(c->list, &lr);
                    GetWindowRect(h, &wr);
                    c->sizing = 1;
                    set_window_pos(h, 0, 0, 0, wr.right - wr.left, lr.bottom - wr.top + 1,
                                   SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE);
                    if (!valid_wnd(h) || cb_of(h) != c)
                        return CB_OKAY;
                    c->sizing = 0;
                    c->cy = lr.bottom - wr.top + 1;
                }
                return CB_OKAY;
            } else
                c->HL = LOWORD(lp) + 6;
            layout(h, c);
            InvalidateRect(h, NULL, TRUE);
            return CB_OKAY;
        }
        /* an item's: the list's, not its size (US34) */
        send_message(c->list, LB_SETITEMHEIGHT, wp, lp);
        return CB_OKAY;
    case CB_GETDROPPEDCONTROLRECT:
        /* the box as made (US34) */
        GetWindowRect(h, (RECT __far *)lp);
        ((RECT __far *)lp)->bottom = ((RECT __far *)lp)->top + c->cy;
        return 1;
    }
    return DefWindowProc(h, msg, wp, lp);
}
