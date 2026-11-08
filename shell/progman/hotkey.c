/*
 * hotkey.c - the Shortcut Key field of Program Item Properties.
 *
 * Observed: a control of class "pmhotkey" (style WS_BORDER | WS_TABSTOP)
 * showing "None", or "Ctrl + Alt + Q" after Q is pressed; Tab still moves
 * on. The value is kept as .GRP stores it: low byte the virtual key, high
 * byte the modifiers (06h = Ctrl+Alt; 01h taken to be Shift, as in that
 * bit layout - UNVERIFIED). Backspace and Delete clear it (UNVERIFIED).
 */
#include "progman.h"

#define HK_SHIFT        0x0100
#define GWW_VALUE       0
#define GWW_FONT        2

static char cls_hotkey[] = "pmhotkey";

static void key_name(WORD vk, char *buf, int size)
{
    if ((vk >= '0' && vk <= '9') || (vk >= 'A' && vk <= 'Z')) {
        buf[0] = (char)vk;
        buf[1] = 0;
    } else if (vk >= VK_F1 && vk <= VK_F24) {
        buf[0] = 'F';
        s_utoa(buf + 1, vk - VK_F1 + 1);
    } else if (!GetKeyNameText((LONG)MapVirtualKey(vk, 0) << 16, buf, size)) {
        buf[0] = 0;
    }
}

static void text_of(HWND w, char *buf, int size)
{
    WORD v = GetWindowWord(w, GWW_VALUE);
    char name[32];
    if (!v) {
        LoadString(g_hinst, IDS_HOTKEYNONE, buf, size);
        return;
    }
    LoadString(g_hinst, IDS_HOTKEYPREFIX, buf, size);     /* "Ctrl + Alt + " */
    if (v & HK_SHIFT)
        s_cat(buf, "Shift + ", size);
    key_name(LOBYTE(v), name, sizeof name);
    s_cat(buf, name, size);
}

static void place_caret(HWND w)
{
    char text[64];
    HDC dc;
    HFONT f = (HFONT)GetWindowWord(w, GWW_FONT), old = NULL;
    DWORD ext;
    if (GetFocus() != w)
        return;
    text_of(w, text, sizeof text);
    dc = GetDC(w);
    if (f)
        old = SelectObject(dc, f);
    ext = GetTextExtent(dc, text, s_len(text));
    if (old)
        SelectObject(dc, old);
    ReleaseDC(w, dc);
    SetCaretPos(1 + LOWORD(ext), 1);
}

static BOOL is_hotkey(WORD vk)
{
    return (vk >= '0' && vk <= '9') || (vk >= 'A' && vk <= 'Z') ||
           (vk >= VK_F1 && vk <= VK_F24) || (vk >= VK_NUMPAD0 && vk <= VK_DIVIDE) ||
           vk == VK_INSERT || vk == VK_HOME || vk == VK_END || vk == VK_PRIOR ||
           vk == VK_NEXT || (vk >= 0xBA && vk <= 0xC0) || (vk >= 0xDB && vk <= 0xDE);
}

LRESULT CALLBACK __export HotkeyWndProc(HWND w, UINT msg, WPARAM wp, LPARAM lp)
{
    switch (msg) {
    case WM_GETDLGCODE:
        return DLGC_WANTCHARS;          /* letters are keys here, not mnemonics */
    case WM_SETFONT:
        SetWindowWord(w, GWW_FONT, wp);
        if (LOWORD(lp))
            InvalidateRect(w, NULL, TRUE);
        return 0;
    case WM_GETFONT:
        return GetWindowWord(w, GWW_FONT);
    case HKM_SETHOTKEY:
        SetWindowWord(w, GWW_VALUE, wp);
        InvalidateRect(w, NULL, TRUE);
        place_caret(w);
        return 0;
    case HKM_GETHOTKEY:
        return GetWindowWord(w, GWW_VALUE);
    case WM_KEYDOWN:
    case WM_SYSKEYDOWN:
        if (wp == VK_BACK || wp == VK_DELETE) {
            SendMessage(w, HKM_SETHOTKEY, 0, 0);
        } else if (is_hotkey(wp)) {
            WORD v = HOTKEY_CTRLALT | (BYTE)wp;
            if (GetKeyState(VK_SHIFT) < 0)
                v |= HK_SHIFT;
            SendMessage(w, HKM_SETHOTKEY, v, 0);
        } else if (msg == WM_SYSKEYDOWN) {
            break;                      /* Alt+letter: dialog mnemonics */
        }
        return 0;
    case WM_CHAR:
        return 0;
    case WM_LBUTTONDOWN:
        SetFocus(w);
        return 0;
    case WM_SETFOCUS: {
        HDC dc = GetDC(w);
        TEXTMETRIC tm;
        HFONT f = (HFONT)GetWindowWord(w, GWW_FONT), old = f ? SelectObject(dc, f) : NULL;
        GetTextMetrics(dc, &tm);
        if (old)
            SelectObject(dc, old);
        ReleaseDC(w, dc);
        CreateCaret(w, NULL, 1, tm.tmHeight);
        place_caret(w);
        ShowCaret(w);
        return 0;
    }
    case WM_KILLFOCUS:
        DestroyCaret();
        return 0;
    case WM_PAINT: {
        PAINTSTRUCT ps;
        HDC dc = BeginPaint(w, &ps);
        HFONT f = (HFONT)GetWindowWord(w, GWW_FONT), old = f ? SelectObject(dc, f) : NULL;
        char text[64];
        text_of(w, text, sizeof text);
        SetBkColor(dc, GetSysColor(COLOR_WINDOW));
        SetTextColor(dc, GetSysColor(COLOR_WINDOWTEXT));
        TextOut(dc, 1, 1, text, s_len(text));    /* observed position */
        if (old)
            SelectObject(dc, old);
        EndPaint(w, &ps);
        return 0;
    }
    }
    return DefWindowProc(w, msg, wp, lp);
}

BOOL hotkey_register(void)
{
    WNDCLASS wc;
    wc.style = 0;
    wc.lpfnWndProc = HotkeyWndProc;
    wc.cbClsExtra = 0;
    wc.cbWndExtra = 4;
    wc.hInstance = g_hinst;
    wc.hIcon = NULL;
    wc.hCursor = LoadCursor(NULL, IDC_IBEAM);
    wc.hbrBackground = (HBRUSH)(COLOR_WINDOW + 1);
    wc.lpszMenuName = NULL;
    wc.lpszClassName = cls_hotkey;
    return RegisterClass(&wc) != 0;
}
