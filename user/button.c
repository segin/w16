/*
 * button.c - the BUTTON and STATIC classes.
 *
 * Push buttons as Windows 3.1 draws them on VGA (measured, probe US4: the
 * OK button of a message box): a black border with its four corner
 * pixels left out (two pixels thick, only the outermost corners left out,
 * for the default button), inside it two pixels of ButtonHighlight on the
 * top and left and two of ButtonShadow on the bottom and right, meeting
 * diagonally, the face ButtonFace, the text centred, and with the focus a
 * dotted rectangle around the text. Check boxes and radio buttons use the
 * display driver's OBM_CHECKBOXES bitmap (14x13 cells).
 *
 * Window extra bytes (Windows 3.1's class has 3, US2): byte 0 the state
 * (BST_* check bits 0-1, 4 pushed, 8 focus), bytes 1-2 the font.
 */
#include "user.h"

HBRUSH sys_brush(HBRUSH b);

#define ST_CHECK    3
#define ST_PUSHED   4
#define ST_FOCUS    8

static HBITMAP bmChecks;

static BYTE state(HWND h)
{
    return (BYTE)(GetWindowWord(h, 0) & 0xFF);
}

static void set_state(HWND h, BYTE s)
{
    PWND w = valid_wnd(h);
    if (w)
        WND_EXTRA(w)[0] = s;
}

static HFONT btn_font(HWND h)
{
    PWND w = valid_wnd(h);
    return w ? *(HFONT __far *)(WND_EXTRA(w) + 1) : 0;
}

static UINT btype(HWND h)
{
    return (UINT)(GetWindowLong(h, GWL_STYLE) & 0x0F);
}

static HBRUSH ctl_brush(HWND h, HDC hdc, int type)
{
    HWND p = GetParent(h);
    HBRUSH b = 0;
    if (p)
        b = (HBRUSH)(WORD)send_message(p, WM_CTLCOLOR, (WPARAM)hdc, MAKELONG(h, type));
    if (!b)
        b = (HBRUSH)(WORD)DefWindowProc(h, WM_CTLCOLOR, (WPARAM)hdc, MAKELONG(h, type));
    return b;
}

static void fillc(HDC hdc, int x, int y, int cx, int cy, int color)
{
    HBRUSH old = SelectObject(hdc, sysbrush[color]);
    PatBlt(hdc, x, y, cx, cy, PATCOPY);
    SelectObject(hdc, old);
}

/* the text of a button or static with its mnemonic, in rc */
static HFONT gray_font;

/* GrayString's output function for a label: DrawText in the bitmap */
static BOOL CALLBACK __loadds gray_out(HDC hdc, LPARAM data, int n)
{
    RECT r;
    SetRect(&r, 0, 0, LOWORD(n), 32767);
    SelectObject(hdc, gray_font);
    DrawText(hdc, (LPCSTR)data, -1, &r, DT_SINGLELINE | DT_LEFT | DT_TOP);
    return TRUE;
}

/* a disabled control's text is grayed with GrayString's checkerboard,
   not drawn in COLOR_GRAYTEXT (US4: "Radio 3", the "Gray" button) */
static void draw_label(HWND h, HDC hdc, RECT *rc, UINT fmt, BOOL gray)
{
    char text[256];
    GetWindowText(h, text, sizeof text);
    if (gray) {
        RECT c;
        int x, y;
        SetRect(&c, 0, 0, 0, 0);
        DrawText(hdc, text, -1, &c, DT_CALCRECT | DT_SINGLELINE);
        x = (fmt & DT_CENTER) ? (rc->left + rc->right - c.right) / 2 :
            (fmt & DT_RIGHT) ? rc->right - c.right : rc->left;
        y = (fmt & DT_VCENTER) ? (rc->top + rc->bottom - c.bottom) / 2 : rc->top;
        gray_font = (HFONT)SelectObject(hdc, GetStockObject(SYSTEM_FONT));
        SelectObject(hdc, gray_font);
        GrayString(hdc, NULL, (GRAYSTRINGPROC)gray_out, (LPARAM)(LPSTR)text, c.right, x, y,
                   c.right, c.bottom);
        return;
    }
    DrawText(hdc, text, -1, rc, fmt);
}

static void dotted_rect(HDC hdc, RECT *r)
{
    DrawFocusRect(hdc, r);
}

static void paint_push(HWND h, HDC hdc)
{
    RECT r, t;
    BYTE s = state(h);
    /* a push button with the focus has the default button's border too
       (US4: Push focused, OK still bold) */
    BOOL def = btype(h) == BS_DEFPUSHBUTTON || (state(h) & ST_FOCUS);
    BOOL pushed = (s & ST_PUSHED) != 0;
    int b = def ? 2 : 1, x0, y0, x1, y1, i;
    char text[256];
    GetClientRect(h, &r);
    ctl_brush(h, hdc, CTLCOLOR_BTN);
    /* the border, corners left as they are */
    fillc(hdc, r.left + 1, r.top, r.right - r.left - 2, 1, COLOR_WINDOWFRAME);
    fillc(hdc, r.left + 1, r.bottom - 1, r.right - r.left - 2, 1, COLOR_WINDOWFRAME);
    fillc(hdc, r.left, r.top + 1, 1, r.bottom - r.top - 2, COLOR_WINDOWFRAME);
    fillc(hdc, r.right - 1, r.top + 1, 1, r.bottom - r.top - 2, COLOR_WINDOWFRAME);
    if (def) {
        fillc(hdc, r.left, r.top + 1, r.right - r.left, 1, COLOR_WINDOWFRAME);
        fillc(hdc, r.left, r.bottom - 2, r.right - r.left, 1, COLOR_WINDOWFRAME);
        fillc(hdc, r.left + 1, r.top, 1, r.bottom - r.top, COLOR_WINDOWFRAME);
        fillc(hdc, r.right - 2, r.top, 1, r.bottom - r.top, COLOR_WINDOWFRAME);
    }
    x0 = r.left + b;
    y0 = r.top + b;
    x1 = r.right - b;
    y1 = r.bottom - b;
    fillc(hdc, x0, y0, x1 - x0, y1 - y0, COLOR_BTNFACE);
    if (pushed) {
        /* UNVERIFIED: the pushed look (a shadow line top and left) */
        fillc(hdc, x0, y0, x1 - x0, 1, COLOR_BTNSHADOW);
        fillc(hdc, x0, y0, 1, y1 - y0, COLOR_BTNSHADOW);
    } else {
        for (i = 0; i < 2; i++) {
            fillc(hdc, x0, y0 + i, x1 - x0 - 1 - i, 1, COLOR_BTNHIGHLIGHT);
            fillc(hdc, x0 + i, y0, 1, y1 - y0 - 1 - i, COLOR_BTNHIGHLIGHT);
            fillc(hdc, x1 - 1 - i, y0 + i, 1, y1 - y0 - i, COLOR_BTNSHADOW);
            fillc(hdc, x0 + i, y1 - 1 - i, x1 - x0 - i, 1, COLOR_BTNSHADOW);
        }
    }
    GetWindowText(h, text, sizeof text);
    SetBkMode(hdc, TRANSPARENT);
    SetTextColor(hdc, syscolor[COLOR_BTNTEXT]);
    t = r;
    t.right -= 2;               /* centred left of the shadow (US4) */
    if (pushed)
        OffsetRect(&t, 1, 1);
    draw_label(h, hdc, &t, DT_CENTER | DT_VCENTER | DT_SINGLELINE, !IsWindowEnabled(h));
    if (s & ST_FOCUS) {
        RECT c;
        int w, hh;
        SetRect(&c, 0, 0, 0, 0);
        DrawText(hdc, text, -1, &c, DT_CALCRECT | DT_SINGLELINE);
        w = c.right;
        hh = c.bottom;
        /* UNVERIFIED: the focus rectangle 2 pixels around the text */
        SetRect(&c, t.left + (t.right - t.left - w) / 2 - 2, t.top + (t.bottom - t.top - hh) / 2 - 1,
                0, 0);
        c.right = c.left + w + 4;
        c.bottom = c.top + hh + 3;          /* US4: 19 high with the system font */
        dotted_rect(hdc, &c);
    }
}

static void paint_check(HWND h, HDC hdc)
{
    RECT r, t;
    BYTE s = state(h);
    UINT type = btype(h);
    HBRUSH bg;
    int row, col, y;
    GetClientRect(h, &r);
    bg = ctl_brush(h, hdc, CTLCOLOR_BTN);
    FillRect(hdc, &r, bg);
    if (!bmChecks)
        bmChecks = LoadBitmap(0, MAKEINTRESOURCE(OBM_CHECKBOXES));
    /* OBM_CHECKBOXES: 4 columns, 3 rows of 14x13 cells: rows check box,
       radio button, three-state; columns unchecked, checked, pushed,
       pushed checked (UNVERIFIED: the cell meanings) */
    row = (type == BS_RADIOBUTTON || type == BS_AUTORADIOBUTTON) ? 1 :
          (type == BS_3STATE || type == BS_AUTO3STATE) && (s & ST_CHECK) == 2 ? 2 : 0;
    col = ((s & ST_CHECK) ? 1 : 0) + ((s & ST_PUSHED) ? 2 : 0);
    if (row == 2)
        col = 1;
    y = r.top + (r.bottom - r.top - 13) / 2;
    {
        HBITMAP old = SelectObject(hdcMem, bmChecks);
        COLORREF tc = SetTextColor(hdc, RGB(0, 0, 0)), bc = SetBkColor(hdc, RGB(255, 255, 255));
        BitBlt(hdc, r.left, y, 13, 13, hdcMem, col * 14, row * 13, SRCCOPY);
        SetTextColor(hdc, tc);
        SetBkColor(hdc, bc);
        SelectObject(hdcMem, old);
    }
    t = r;
    /* the text one pixel lower and 5 right of the box (US4: a 20-pixel
       high check box, system font; UNVERIFIED as a rule) */
    t.left += 13 + cxSysChar / 2 + 1;
    t.top += 2;
    SetBkMode(hdc, TRANSPARENT);
    draw_label(h, hdc, &t, DT_VCENTER | DT_SINGLELINE | DT_LEFT, !IsWindowEnabled(h));
    if (s & ST_FOCUS) {
        RECT c;
        char text[256];
        GetWindowText(h, text, sizeof text);
        SetRect(&c, 0, 0, 0, 0);
        DrawText(hdc, text, -1, &c, DT_CALCRECT | DT_SINGLELINE);
        OffsetRect(&c, t.left - 1, t.top + (t.bottom - t.top - c.bottom) / 2 - 1);
        c.right += 2;
        c.bottom += 2;
        dotted_rect(hdc, &c);
    }
}

static void paint_group(HWND h, HDC hdc)
{
    RECT r, t;
    char text[256];
    int fh = cySysChar;
    HBRUSH bg;
    GetClientRect(h, &r);
    bg = ctl_brush(h, hdc, CTLCOLOR_BTN);
    {
        HBRUSH ob = SelectObject(hdc, sysbrush[COLOR_WINDOWFRAME]);
        int y = r.top + fh / 2;
        PatBlt(hdc, r.left, y, r.right - r.left, 1, PATCOPY);
        PatBlt(hdc, r.left, r.bottom - 1, r.right - r.left, 1, PATCOPY);
        PatBlt(hdc, r.left, y, 1, r.bottom - y, PATCOPY);
        PatBlt(hdc, r.right - 1, y, 1, r.bottom - y, PATCOPY);
        SelectObject(hdc, ob);
    }
    GetWindowText(h, text, sizeof text);
    if (text[0]) {
        SetRect(&t, 0, 0, 0, 0);
        DrawText(hdc, text, -1, &t, DT_CALCRECT | DT_SINGLELINE);
        /* the text 9 pixels in and 3 down, the line cleared 2 pixels
           before and after it (US4, system font; UNVERIFIED for others) */
        OffsetRect(&t, r.left + 9, r.top + 3);
        {
            RECT e = t;
            e.left -= 2;
            e.right += 2;
            e.top = r.top;
            FillRect(hdc, &e, bg);
        }
        SetBkMode(hdc, TRANSPARENT);
        draw_label(h, hdc, &t, DT_SINGLELINE | DT_LEFT, !IsWindowEnabled(h));
    }
}

static void paint_button(HWND h, HDC hdc)
{
    HFONT f = btn_font(h), old = 0;
    if (f)
        old = SelectObject(hdc, f);
    switch (btype(h)) {
    case BS_PUSHBUTTON:
    case BS_DEFPUSHBUTTON:
        paint_push(h, hdc);
        break;
    case BS_GROUPBOX:
        paint_group(h, hdc);
        break;
    case BS_OWNERDRAW: {
        DRAWITEMSTRUCT di;
        BYTE s = state(h);
        di.CtlType = ODT_BUTTON;
        di.CtlID = GetWindowWord(h, GWW_ID);
        di.itemID = 0;
        di.itemAction = ODA_DRAWENTIRE;
        di.itemState = ((s & ST_PUSHED) ? ODS_SELECTED : 0) | ((s & ST_FOCUS) ? ODS_FOCUS : 0) |
                       (IsWindowEnabled(h) ? 0 : ODS_DISABLED);
        di.hwndItem = h;
        di.hDC = hdc;
        GetClientRect(h, &di.rcItem);
        di.itemData = 0;
        send_message(GetParent(h), WM_DRAWITEM, di.CtlID, (LPARAM)(DRAWITEMSTRUCT __far *)&di);
        break;
    }
    default:
        paint_check(h, hdc);
    }
    if (f)
        SelectObject(hdc, old);
}

static void redraw(HWND h)
{
    if (IsWindowVisible(h)) {
        HDC hdc = GetDC(h);
        paint_button(h, hdc);
        ReleaseDC(h, hdc);
    }
}

static void notify(HWND h, WORD code)
{
    send_message(GetParent(h), WM_COMMAND, GetWindowWord(h, GWW_ID), MAKELONG(h, code));
}

void button_click(HWND h);
static void clicked(HWND h)
{
    UINT t = btype(h);
    BYTE s = state(h);
    if (t == BS_AUTOCHECKBOX)
        set_state(h, (BYTE)((s & ~ST_CHECK) | ((s & ST_CHECK) ? 0 : 1)));
    else if (t == BS_AUTO3STATE)
        set_state(h, (BYTE)((s & ~ST_CHECK) | (((s & ST_CHECK) + 1) % 3)));
    else if (t == BS_AUTORADIOBUTTON) {
        HWND p = GetParent(h), c = h;
        /* uncheck the others of the group */
        do {
            c = GetNextDlgGroupItem(p, c, FALSE);
            if (c != h && btype(c) == BS_AUTORADIOBUTTON)
                send_message(c, BM_SETCHECK, 0, 0);
        } while (c && c != h);
        set_state(h, (BYTE)((s & ~ST_CHECK) | 1));
    }
    redraw(h);
    notify(h, BN_CLICKED);
}

LRESULT WINAPI __loadds ButtonWndProc(HWND h, UINT msg, WPARAM wp, LPARAM lp)
{
    BYTE s;
    if (!valid_wnd(h))
        return 0;
    s = state(h);
    switch (msg) {
    case WM_CREATE: {
        /* BS_USERBUTTON in a program marked for 3.10 is a push button
           (US18 on 3.1: made by such a program, by CreateWindow or a
           dialog template, it reads back 0; by one marked 3.00 it stays
           8. Terminal's status bar, Stop and Pause, are such buttons) */
        PWND w = valid_wnd(h);
        if (w && (w->style & 0x0F) == BS_USERBUTTON &&
            K_GETEXPWINVER(K_GETEXEPTR(w->hInstance)) >= 0x030A)
            w->style &= ~0x0FL;
        set_state(h, 0);
        return 0;
    }
    case WM_PAINT: {
        PAINTSTRUCT ps;
        BeginPaint(h, &ps);
        paint_button(h, ps.hdc);
        EndPaint(h, &ps);
        return 0;
    }
    case WM_ERASEBKGND:
        return 1;
    case WM_ENABLE:
    case WM_SETTEXT: {
        LONG r = msg == WM_SETTEXT ? DefWindowProc(h, msg, wp, lp) : 0;
        redraw(h);
        return r;
    }
    case WM_SETFONT: {
        PWND w = valid_wnd(h);
        *(HFONT __far *)(WND_EXTRA(w) + 1) = (HFONT)wp;
        if (LOWORD(lp))
            redraw(h);
        return 0;
    }
    case WM_GETFONT:
        return (LONG)(WORD)btn_font(h);
    case WM_SETFOCUS:
        set_state(h, (BYTE)(s | ST_FOCUS));
        redraw(h);
        return 0;
    case WM_KILLFOCUS:
        set_state(h, (BYTE)(s & ~(ST_FOCUS | ST_PUSHED)));
        if (GetCapture() == h)
            ReleaseCapture();
        redraw(h);
        return 0;
    case WM_GETDLGCODE: {
        UINT t = btype(h);
        if (t == BS_DEFPUSHBUTTON)
            return DLGC_BUTTON | DLGC_DEFPUSHBUTTON;
        if (t == BS_PUSHBUTTON)
            return DLGC_BUTTON | DLGC_UNDEFPUSHBUTTON;
        if (t == BS_RADIOBUTTON || t == BS_AUTORADIOBUTTON)
            return DLGC_BUTTON | DLGC_RADIOBUTTON;
        if (t == BS_GROUPBOX)
            return DLGC_STATIC;
        return DLGC_BUTTON;
    }
    case WM_LBUTTONDOWN:
    case WM_LBUTTONDBLCLK:
        if (btype(h) == BS_GROUPBOX)
            return 0;
        SetFocus(h);
        SetCapture(h);
        set_state(h, (BYTE)(state(h) | ST_PUSHED));
        redraw(h);
        return 0;
    case WM_MOUSEMOVE:
        if (GetCapture() == h) {
            RECT r;
            POINT pt;
            BOOL in;
            GetClientRect(h, &r);
            pt.x = (int)LOWORD(lp);
            pt.y = (int)HIWORD(lp);
            in = PtInRect(&r, pt);
            if (in != ((s & ST_PUSHED) != 0)) {
                set_state(h, (BYTE)(in ? s | ST_PUSHED : s & ~ST_PUSHED));
                redraw(h);
            }
        }
        return 0;
    case WM_LBUTTONUP:
        if (GetCapture() == h) {
            BOOL was = (s & ST_PUSHED) != 0;
            ReleaseCapture();
            set_state(h, (BYTE)(s & ~ST_PUSHED));
            redraw(h);
            if (was)
                clicked(h);
        }
        return 0;
    case WM_KEYDOWN:
        if (wp == VK_SPACE && !(s & ST_PUSHED)) {
            set_state(h, (BYTE)(s | ST_PUSHED));
            redraw(h);
        }
        return 0;
    case WM_KEYUP:
        if (wp == VK_SPACE && (s & ST_PUSHED)) {
            set_state(h, (BYTE)(s & ~ST_PUSHED));
            redraw(h);
            clicked(h);
        }
        return 0;
    case WM_CHAR:
        if (wp == '+' || wp == '=' || wp == '-') {
            UINT t = btype(h);
            if (t == BS_CHECKBOX || t == BS_AUTOCHECKBOX || t == BS_3STATE || t == BS_AUTO3STATE) {
                send_message(h, BM_SETCHECK, wp == '-' ? 0 : 1, 0);
                notify(h, BN_CLICKED);
            }
        }
        return 0;
    case BM_GETCHECK:
        return s & ST_CHECK;
    case BM_SETCHECK:
        set_state(h, (BYTE)((s & ~ST_CHECK) | (wp & ST_CHECK)));
        if ((wp & ST_CHECK) != (s & ST_CHECK))
            redraw(h);
        return 0;
    case BM_GETSTATE:
        return s;
    case BM_SETSTATE:
        set_state(h, (BYTE)(wp ? s | ST_PUSHED : s & ~ST_PUSHED));
        redraw(h);
        return 0;
    case BM_SETSTYLE: {
        PWND w = valid_wnd(h);
        w->style = (w->style & ~0x0FL) | (wp & 0x0F);
        if (LOWORD(lp))
            redraw(h);
        return 0;
    }
    }
    return DefWindowProc(h, msg, wp, lp);
}

/* ---- STATIC: extra bytes 0-1 the font, 2-3 the icon (US2: 6 bytes) ---- */
LRESULT WINAPI __loadds StaticWndProc(HWND h, UINT msg, WPARAM wp, LPARAM lp)
{
    PWND w = valid_wnd(h);
    UINT type;
    if (!w)
        return 0;
    type = (UINT)(w->style & 0x0F);
    switch (msg) {
    case WM_CREATE:
        if (type == SS_ICON) {
            CREATESTRUCT __far *cs = (CREATESTRUCT __far *)lp;
            HICON ic = cs->lpszName ? LoadIcon(cs->hInstance, cs->lpszName) : 0;
            /* an icon not the program's own is a system one (3.1: Program
               Manager's Exit dialog names icon 32516 and shows
               IDI_ASTERISK); "#n" is n */
            if (!ic && cs->lpszName && !HIWORD((DWORD)cs->lpszName))
                ic = LoadIcon(0, cs->lpszName);
            else if (!ic && cs->lpszName) {
                LPCSTR s = cs->lpszName;
                if (s[0] == '#') {
                    WORD n = 0;
                    for (s++; *s >= '0' && *s <= '9'; s++)
                        n = n * 10 + (*s - '0');
                    ic = LoadIcon(cs->hInstance, MAKEINTRESOURCE(n));
                    if (!ic)
                        ic = LoadIcon(0, MAKEINTRESOURCE(n));
                } else
                    ic = LoadIcon(0, cs->lpszName);
            }
            *(WORD __far *)(WND_EXTRA(w) + 2) = (WORD)ic;
            if (ic)
                SetWindowPos(h, 0, 0, 0, sysmet[SM_CXICON], sysmet[SM_CYICON],
                             SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE);
        }
        return 0;
    case WM_PAINT: {
        PAINTSTRUCT ps;
        RECT r;
        HDC hdc = BeginPaint(h, &ps);
        HFONT f = *(HFONT __far *)WND_EXTRA(w), old = 0;
        GetClientRect(h, &r);
        if (f)
            old = SelectObject(hdc, f);
        switch (type) {
        case SS_LEFT:
        case SS_CENTER:
        case SS_RIGHT:
        case SS_LEFTNOWORDWRAP:
        case SS_SIMPLE: {
            HBRUSH bg = ctl_brush(h, hdc, CTLCOLOR_STATIC);
            UINT fmt = type == SS_CENTER ? DT_CENTER | DT_WORDBREAK :
                       type == SS_RIGHT ? DT_RIGHT | DT_WORDBREAK :
                       type == SS_LEFT ? DT_LEFT | DT_WORDBREAK : DT_LEFT | DT_SINGLELINE;
            if (w->style & SS_NOPREFIX)
                fmt |= DT_NOPREFIX;
            if (type != SS_SIMPLE)
                FillRect(hdc, &r, bg);
            SetBkMode(hdc, TRANSPARENT);
            draw_label(h, hdc, &r, fmt | DT_EXPANDTABS, !IsWindowEnabled(h));
            break;
        }
        case SS_ICON: {
            HICON ic = (HICON)*(WORD __far *)(WND_EXTRA(w) + 2);
            FillRect(hdc, &r, ctl_brush(h, hdc, CTLCOLOR_STATIC));
            if (ic)
                DrawIcon(hdc, 0, 0, ic);
            break;
        }
        case SS_BLACKRECT: FillRect(hdc, &r, sysbrush[COLOR_WINDOWFRAME]); break;
        case SS_GRAYRECT: FillRect(hdc, &r, sysbrush[COLOR_BACKGROUND]); break;
        case SS_WHITERECT: FillRect(hdc, &r, sysbrush[COLOR_WINDOW]); break;
        case SS_BLACKFRAME: FrameRect(hdc, &r, sysbrush[COLOR_WINDOWFRAME]); break;
        case SS_GRAYFRAME: FrameRect(hdc, &r, sysbrush[COLOR_BACKGROUND]); break;
        case SS_WHITEFRAME: FrameRect(hdc, &r, sysbrush[COLOR_WINDOW]); break;
        }
        if (f)
            SelectObject(hdc, old);
        EndPaint(h, &ps);
        return 0;
    }
    case WM_ERASEBKGND:
        return 1;
    case WM_SETTEXT: {
        LONG r = DefWindowProc(h, msg, wp, lp);
        InvalidateRect(h, NULL, TRUE);
        return r;
    }
    case WM_ENABLE:
        InvalidateRect(h, NULL, TRUE);
        return 0;
    case WM_SETFONT:
        *(HFONT __far *)WND_EXTRA(w) = (HFONT)wp;
        if (LOWORD(lp))
            InvalidateRect(h, NULL, TRUE);
        return 0;
    case WM_GETFONT:
        return *(WORD __far *)WND_EXTRA(w);
    case STM_SETICON: {
        WORD old = *(WORD __far *)(WND_EXTRA(w) + 2);
        *(WORD __far *)(WND_EXTRA(w) + 2) = wp;
        InvalidateRect(h, NULL, TRUE);
        return old;
    }
    case STM_GETICON:
        return *(WORD __far *)(WND_EXTRA(w) + 2);
    case WM_NCHITTEST:
        return HTTRANSPARENT;
    case WM_GETDLGCODE:
        return DLGC_STATIC;
    }
    return DefWindowProc(h, msg, wp, lp);
}

/* a click from the dialog manager (a mnemonic, an arrow key) */
void button_click(HWND h)
{
    char cls[16];
    GetClassName(h, cls, sizeof cls);
    if (!lstr_cmpi(cls, "Button"))
        clicked(h);
}
