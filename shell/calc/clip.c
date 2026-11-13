/*
 * clip.c - w16 Calculator: Edit > Copy and Edit > Paste.
 *
 * Copy (observed): the display's text, less the point the display adds
 * (kept when one was typed), goes onto the clipboard through the hidden
 * Edit control (Windows 3.1's Calculator imports no SetClipboardData);
 * nothing happens in an error state. Afterwards the display shows the
 * current number again (calc_display: "2+" then Copy shows 0.).
 * Paste: the CF_TEXT block, all GlobalSize bytes of it, as keys
 * (paste.c), then the current number is shown again.
 */
#include <windows.h>
#include "calc.h"
#include "ui.h"

void clip_copy(void)
{
    char t[80];
    HWND e = GetDlgItem(g_wnd, IDD_EDIT);
    if (calc_copytext(t, sizeof t) && e) {
        SetWindowText(e, t);
        SendMessage(e, EM_SETSEL, 0, MAKELPARAM(0, 0x7FFF));
        SendMessage(e, WM_COPY, 0, 0);
    }
    calc_display();
}

void clip_paste(void)
{
    HANDLE h;
    char FAR *p;
    char msg[160], title[32];
    if (!OpenClipboard(g_wnd)) {
        LoadString(g_hinst, IDS_CLIPBUSY, msg, sizeof msg);
        LoadString(g_hinst, IDS_CALC, title, sizeof title);
        MessageBox(g_wnd, msg, title, MB_OK | MB_ICONEXCLAMATION);
        return;
    }
    h = GetClipboardData(CF_TEXT);
    if (h && (p = GlobalLock(h)) != NULL) {
        DWORD n = GlobalSize(h);
        calc_paste(p, n > 0xFFF0UL ? 0xFFF0U : (unsigned)n, paste_command);
        GlobalUnlock(h);
    }
    CloseClipboard();
    calc_display();
}
