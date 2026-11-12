/*
 * combo.c - the COMBOBOX class: simple, drop-down and drop-down list
 * combo boxes, built from an EDIT control (or a drawn box for
 * CBS_DROPDOWNLIST), a LISTBOX (a ComboLBox popup for the drop-down
 * kinds), and the drop-down button (the display's OBM_COMBO arrow).
 * UNVERIFIED: the exact layout (US4 compares it).
 */
#include "user.h"

void listbox_set_owner(HWND h, HWND owner);

typedef struct CB {
    HWND edit, list;
    HFONT font;
    WORD dropped, focus;
    int  itemh;
    RECT button;
    int  droph;                 /* the list's height when dropped */
} CB;
typedef CB __near *PCB;

static HBITMAP bmCombo;

/* the edit or selection box: the font's height plus 8 (US4: 24 with the
   system font; UNVERIFIED as the rule) */
#define EH(c) ((c)->itemh + 8)

static PCB cb_of(HWND h)
{
    return (PCB)GetWindowWord(h, 0);
}

static UINT kind(HWND h)
{
    return (UINT)(GetWindowLong(h, GWL_STYLE) & 3);
}

static void notify(HWND h, WORD code)
{
    send_message(GetParent(h), WM_COMMAND, GetWindowWord(h, GWW_ID), MAKELONG(h, code));
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
        /* its top line on the box's bottom line (US4) */
        set_window_pos(c->list, HWND_TOP, r.left, r.top + EH(c) - 1,
                       r.right - r.left, c->droph, SWP_SHOWWINDOW | SWP_NOACTIVATE);
        SetCapture(h);
    } else {
        ShowWindow(c->list, SW_HIDE);
        if (GetCapture() == h)
            ReleaseCapture();
    }
    InvalidateRect(h, NULL, FALSE);
}

static void sel_to_edit(HWND h, PCB c)
{
    int i = (int)send_message(c->list, LB_GETCURSEL, 0, 0);
    char buf[256];
    if (i < 0)
        buf[0] = 0;
    else
        send_message(c->list, LB_GETTEXT, i, (LPARAM)(LPSTR)buf);
    if (c->edit) {
        SetWindowText(c->edit, buf);
        send_message(c->edit, EM_SETSEL, 0, MAKELONG(0, -1));
    }
    InvalidateRect(h, NULL, FALSE);
}

static void paint(HWND h, PCB c, HDC hdc)
{
    RECT r, box;
    HFONT old = SelectObject(hdc, c->font ? c->font : GetStockObject(SYSTEM_FONT));
    GetClientRect(h, &r);
    if (kind(h) != CBS_SIMPLE) {
        int bw = sysmet[SM_CXVSCROLL];
        RECT in;
        SetRect(&box, 0, 0, r.right, EH(c));
        FillRect(hdc, &r, sysbrush[COLOR_WINDOW]);
        FrameRect(hdc, &box, sysbrush[COLOR_WINDOWFRAME]);
        /* the button: the box's frame around it, a raised face (white
           line left and top, two-pixel shadow right and bottom) and the
           display's OBM_COMBO arrow in the middle (US4) */
        SetRect(&c->button, r.right - bw, 0, r.right, EH(c));
        FrameRect(hdc, &c->button, sysbrush[COLOR_WINDOWFRAME]);
        SetRect(&in, c->button.left + 1, 1, c->button.right - 1, EH(c) - 1);
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
            COLORREF tc = SetTextColor(hdc, RGB(0, 0, 0)), bc = SetBkColor(hdc, syscolor[COLOR_BTNFACE]);
            GetObject(bmCombo, sizeof bm, &bm);
            ob = SelectObject(hdcMem, bmCombo);
            BitBlt(hdc, in.left + (in.right - in.left - bm.bmWidth) / 2,
                   in.top + (in.bottom - in.top - bm.bmHeight) / 2, bm.bmWidth, bm.bmHeight,
                   hdcMem, 0, 0, SRCCOPY);
            SelectObject(hdcMem, ob);
            SetTextColor(hdc, tc);
            SetBkColor(hdc, bc);
        }
        if (kind(h) == CBS_DROPDOWNLIST) {
            int i = (int)send_message(c->list, LB_GETCURSEL, 0, 0);
            char buf[256];
            RECT t;
            SetRect(&t, 1, 1, c->button.left - 1, c->itemh + 3);
            buf[0] = 0;
            if (i >= 0)
                send_message(c->list, LB_GETTEXT, i, (LPARAM)(LPSTR)buf);
            FillRect(hdc, &t, sysbrush[c->focus ? COLOR_HIGHLIGHT : COLOR_WINDOW]);
            SetBkMode(hdc, TRANSPARENT);
            SetTextColor(hdc, syscolor[c->focus ? COLOR_HIGHLIGHTTEXT : COLOR_WINDOWTEXT]);
            TextOut(hdc, 3, (EH(c) - c->itemh) / 2 - 1, buf, lstr_len(buf));  /* US4 */
            if (c->focus)
                DrawFocusRect(hdc, &t);
        }
    } else
        FillRect(hdc, &r, sysbrush[COLOR_WINDOW]);
    SelectObject(hdc, old);
}

static void layout(HWND h, PCB c)
{
    RECT r;
    int eh = EH(c);
    GetClientRect(h, &r);
    if (kind(h) == CBS_SIMPLE) {
        if (c->edit)
            MoveWindow(c->edit, 0, 0, r.right, eh, TRUE);
        if (c->list)
            /* indented 8, its top line on the edit's bottom line (US4) */
            MoveWindow(c->list, 8, eh - 1, r.right - 8, r.bottom - eh + 2, TRUE);
    } else if (c->edit)
        MoveWindow(c->edit, 0, 0, r.right - sysmet[SM_CXVSCROLL], eh, TRUE);
}

LRESULT WINAPI __loadds ComboBoxWndProc(HWND h, UINT msg, WPARAM wp, LPARAM lp)
{
    PCB c;
    if (!valid_wnd(h))
        return 0;
    c = cb_of(h);
    if (msg == WM_CREATE) {
        CREATESTRUCT __far *cs = (CREATESTRUCT __far *)lp;
        DWORD s = cs->style;
        HINSTANCE hi = cs->hInstance;
        c = (PCB)LocalAlloc(LPTR, sizeof(CB));
        if (!c)
            return -1;
        SetWindowWord(h, 0, (WORD)c);
        /* the scroll bar styles are the list's, not the box's */
        {
            PWND w = valid_wnd(h);
            w->style &= ~(WS_VSCROLL | WS_HSCROLL);
            calc_client(w);
        }
        c->itemh = cySysChar;
        c->droph = cs->cy - (EH(c));
        if (c->droph < c->itemh * 2)
            c->droph = c->itemh * 4 + 2;
        if (kind(h) != CBS_DROPDOWNLIST)
            c->edit = create_window(0, "Edit", NULL, WS_CHILD | WS_VISIBLE | WS_BORDER |
                                    ((s & CBS_AUTOHSCROLL) ? ES_AUTOHSCROLL : 0),
                                    0, 0, 0, 0, h, (HMENU)1001, hi, NULL);
        if (kind(h) == CBS_SIMPLE)
            c->list = create_window(0, "ListBox", NULL, WS_CHILD | WS_VISIBLE | WS_BORDER |
                                    WS_VSCROLL | LBS_NOTIFY | ((s & CBS_SORT) ? LBS_SORT : 0),
                                    0, 0, 0, 0, h, (HMENU)1000, hi, NULL);
        else {
            RECT r;
            GetWindowRect(h, &r);
            c->list = create_window(0, "ComboLBox", NULL, WS_POPUP | WS_BORDER | WS_VSCROLL |
                                    LBS_NOTIFY | ((s & CBS_SORT) ? LBS_SORT : 0),
                                    r.left, r.bottom, r.right - r.left, c->droph, h, 0, hi, NULL);
        }
        if (c->list)
            listbox_set_owner(c->list, h);
        /* the height of a drop-down combo box is its edit's (3.1 SDK) */
        if (kind(h) != CBS_SIMPLE) {
            PWND w = valid_wnd(h);
            w->rcWindow.bottom = w->rcWindow.top + EH(c);
            calc_client(w);
        }
        layout(h, c);
        return 0;
    }
    if (!c)
        return DefWindowProc(h, msg, wp, lp);
    switch (msg) {
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
        layout(h, c);
        return 0;
    case WM_SETFONT:
        c->font = (HFONT)wp;
        if (c->edit)
            send_message(c->edit, WM_SETFONT, wp, lp);
        if (c->list)
            send_message(c->list, WM_SETFONT, wp, lp);
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
                sel_to_edit(h, c);
                notify(h, CBN_SELCHANGE);
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
            char buf[256];
            buf[0] = 0;
            if (i >= 0)
                send_message(c->list, LB_GETTEXT, i, (LPARAM)(LPSTR)buf);
            if (msg == WM_GETTEXTLENGTH)
                return lstr_len(buf);
            lstr_cpyn((LPSTR)lp, buf, wp);
            return lstr_len((LPSTR)lp);
        }
        return 0;
    /* ---- CB_ messages: most are the list's ---- */
    case CB_ADDSTRING: return send_message(c->list, LB_ADDSTRING, wp, lp);
    case CB_INSERTSTRING: return send_message(c->list, LB_INSERTSTRING, wp, lp);
    case CB_DELETESTRING: return send_message(c->list, LB_DELETESTRING, wp, lp);
    case CB_GETCOUNT: return send_message(c->list, LB_GETCOUNT, wp, lp);
    case CB_GETLBTEXT: return send_message(c->list, LB_GETTEXT, wp, lp);
    case CB_GETLBTEXTLEN: return send_message(c->list, LB_GETTEXTLEN, wp, lp);
    case CB_FINDSTRING: return send_message(c->list, LB_FINDSTRING, wp, lp);
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
    case CB_SETITEMHEIGHT:
    case CB_GETITEMHEIGHT:
        return c->itemh;
    case CB_GETDROPPEDCONTROLRECT:
        GetWindowRect(h, (RECT __far *)lp);
        ((RECT __far *)lp)->bottom += c->droph;
        return 1;
    }
    return DefWindowProc(h, msg, wp, lp);
}
