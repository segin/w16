/*
 * hotkey.c - the Advanced dialog's Application Shortcut Key box, window
 * class "PifHKy" (ARCHITECTURE.md 15.11). As observed (PE2, PE12): a
 * bordered box in the system font showing "None", or the key pressed in it
 * with the modifiers held ("Alt+Ctrl+P", "Ctrl+F1", "Ctrl+Shift+K": Alt,
 * Ctrl, Shift in that order, the key's name from its scan code), a caret
 * after the text while it has the focus. Backspace empties it, which
 * leaves the key as it was; a key without Alt or Ctrl ("Q") shows but is
 * refused when OK is chosen ("Invalid shortcut key."). Tab and Enter work
 * as in any dialog.
 */
#include "pifedit.h"

/* window words: the state, the scan code, the modifiers */
#define HKW_STATE   0
#define HKW_SCAN    2
#define HKW_MODS    4
#define HK_EXTRA    6

#define ST_NONE     0   /* "None" */
#define ST_SET      1   /* a valid key */
#define ST_BAD      2   /* shown, refused at OK */
#define ST_BLANK    3   /* emptied: the key is left as it was */

/* the text's place in the box (PE2: "None" 5 pixels in, its capitals
   3 down) and its font, the dialog's (MS Sans Serif 8 bold) although the
   box itself answers WM_GETFONT with none */
#define HK_X        4
#define HK_Y        1

static HFONT hk_font(HWND w)
{
    return (HFONT)SendMessage(GetParent(w), WM_GETFONT, 0, 0);
}

static void hk_text(HWND w, LPSTR buf)
{
    WORD st = GetWindowWord(w, HKW_STATE);
    WORD mods = GetWindowWord(w, HKW_MODS);
    char name[32];
    buf[0] = 0;
    if (st == ST_NONE) {
        rstr(IDS_NONE, buf, 32);
        return;
    }
    if (st == ST_BLANK)
        return;
    if (mods & HK_ALT)
        rstr(IDS_ALT, buf + lstrlen(buf), 16);
    if (mods & HK_CTRL)
        rstr(IDS_CTRL, buf + lstrlen(buf), 16);
    if (mods & HK_SHIFT)
        rstr(IDS_SHIFT, buf + lstrlen(buf), 16);
    name[0] = 0;
    GetKeyNameText((LONG)GetWindowWord(w, HKW_SCAN) << 16, name, sizeof name);
    lstrcat(buf, name);
}

static void hk_caret(HWND w)
{
    char buf[80];
    HDC dc;
    DWORD ext;
    if (GetFocus() != w)
        return;
    hk_text(w, buf);
    dc = GetDC(w);
    SelectObject(dc, hk_font(w));
    ext = GetTextExtent(dc, buf, lstrlen(buf));
    ReleaseDC(w, dc);
    SetCaretPos(HK_X + LOWORD(ext), HK_Y);
}

static void hk_paint(HWND w)
{
    PAINTSTRUCT ps;
    char buf[80];
    HDC dc = BeginPaint(w, &ps);
    HFONT of = SelectObject(dc, hk_font(w));
    hk_text(w, buf);
    SetBkColor(dc, GetSysColor(COLOR_WINDOW));
    SetTextColor(dc, GetSysColor(COLOR_WINDOWTEXT));
    TextOut(dc, HK_X, HK_Y, buf, lstrlen(buf));
    SelectObject(dc, of);
    EndPaint(w, &ps);
}

static LRESULT CALLBACK hk_proc(HWND w, UINT msg, WPARAM wp, LPARAM lp)
{
    switch (msg) {
    case WM_GETDLGCODE:
        return DLGC_WANTCHARS | DLGC_WANTARROWS;
    case WM_ERASEBKGND: {
        RECT r;
        HBRUSH b = CreateSolidBrush(GetSysColor(COLOR_WINDOW));
        GetClientRect(w, &r);
        FillRect((HDC)wp, &r, b);
        DeleteObject(b);
        return 1;
    }
    case WM_PAINT:
        hk_paint(w);
        return 0;
    case WM_SETFOCUS: {
        TEXTMETRIC tm;
        HDC dc = GetDC(w);
        SelectObject(dc, hk_font(w));
        GetTextMetrics(dc, &tm);
        ReleaseDC(w, dc);
        CreateCaret(w, NULL, 1, tm.tmHeight);
        hk_caret(w);
        ShowCaret(w);
        status_set(GetParent(w), status_topic(w));
        return 0;
    }
    case WM_KILLFOCUS:
        DestroyCaret();
        return 0;
    case WM_LBUTTONDOWN:
        SetFocus(w);
        return 0;
    case WM_KEYDOWN:
    case WM_SYSKEYDOWN: {
        WORD mods = 0;
        if (wp == VK_SHIFT || wp == VK_CONTROL || wp == VK_MENU)
            return 0;
        if ((wp == VK_F1 && GetKeyState(VK_CONTROL) >= 0 && GetKeyState(VK_MENU) >= 0) ||
            wp == VK_TAB || wp == VK_RETURN || wp == VK_ESCAPE)
            break;
        if (GetKeyState(VK_MENU) < 0)
            mods |= HK_ALT;
        if (GetKeyState(VK_CONTROL) < 0)
            mods |= HK_CTRL;
        if (GetKeyState(VK_SHIFT) < 0)
            mods |= HK_SHIFT;
        if (wp == VK_BACK && !mods) {
            SetWindowWord(w, HKW_STATE, ST_BLANK);
        } else {
            SetWindowWord(w, HKW_SCAN, (HIWORD(lp) & 0xFF));
            SetWindowWord(w, HKW_MODS, mods);
            SetWindowWord(w, HKW_STATE, (mods & (HK_ALT | HK_CTRL)) && (HIWORD(lp) & 0xFF) ? ST_SET : ST_BAD);
        }
        InvalidateRect(w, NULL, TRUE);
        UpdateWindow(w);
        hk_caret(w);
        return 0;
    }
    case WM_CHAR:
    case WM_SYSCHAR:
        return 0;
    }
    return DefWindowProc(w, msg, wp, lp);
}

BOOL hotkey_register(HINSTANCE inst)
{
    WNDCLASS wc;
    char cls[16];
    rstr(IDS_HKCLASS, cls, sizeof cls);
    wc.style = 0;
    wc.lpfnWndProc = hk_proc;
    wc.cbClsExtra = 0;
    wc.cbWndExtra = HK_EXTRA;
    wc.hInstance = inst;
    wc.hIcon = NULL;
    wc.hCursor = LoadCursor(NULL, IDC_IBEAM);
    wc.hbrBackground = NULL;
    wc.lpszMenuName = NULL;
    wc.lpszClassName = cls;
    return RegisterClass(&wc) != 0;
}

void hotkey_set(HWND w, WORD scan, WORD mods, BOOL on)
{
    SetWindowWord(w, HKW_SCAN, scan);
    SetWindowWord(w, HKW_MODS, mods);
    SetWindowWord(w, HKW_STATE, on ? ST_SET : ST_NONE);
    InvalidateRect(w, NULL, TRUE);
}

/* 0: refused; 1: a key (or none) to keep; 2: nothing to change */
BOOL hotkey_get(HWND w, WORD FAR *scan, WORD FAR *mods, BOOL FAR *on)
{
    switch (GetWindowWord(w, HKW_STATE)) {
    case ST_BAD:
        return 0;
    case ST_SET:
        *scan = GetWindowWord(w, HKW_SCAN);
        *mods = GetWindowWord(w, HKW_MODS);
        *on = TRUE;
        return 1;
    default:
        return 2;
    }
}
