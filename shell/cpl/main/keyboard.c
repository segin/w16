/*
 * keyboard.c - the Keyboard applet: key repeat delay and rate.
 *
 * Observed on Windows 3.1 (tests/scripts/cpl/kb*.txt): WIN.INI [windows]
 * KeyboardDelay (0..3) and KeyboardSpeed (0..31). The delay scroll bar
 * runs Long to Short, so its position is 3 - KeyboardDelay; the rate
 * scroll bar's position is KeyboardSpeed. OK writes both; Cancel writes
 * nothing. The settings reach the keyboard driver through
 * SystemParametersInfo as the bars move, so the Test box shows them.
 * The calls, as KEYBOARD.SetSpeed sees them through USER (KBREC.DRV,
 * tests/scripts/cpl/kbspeed*.txt): every message of the delay bar
 * (SB_TOP, then SB_ENDSCROLL ...) sets the delay the bar had before it,
 * so the new one arrives with SB_ENDSCROLL; the rate bar's set the new
 * rate each time; OK sets both again; Cancel puts back the rate, then the
 * delay.
 */
#include "maincpl.h"

static int delay0, speed0, delay, speed;

static void setpos(HWND dlg, int id, int pos, int max)
{
    HWND sb = GetDlgItem(dlg, id);
    SetScrollRange(sb, SB_CTL, 0, max, FALSE);
    SetScrollPos(sb, SB_CTL, pos, TRUE);
}

static int scroll(int pos, int max, WPARAM code, int thumb)
{
    switch (code) {
    case SB_LINEUP:        pos--; break;
    case SB_LINEDOWN:      pos++; break;
    case SB_PAGEUP:        pos -= (max + 1) / 4; break;  /* observed: rate 8, delay 1 */
    case SB_PAGEDOWN:      pos += (max + 1) / 4; break;
    case SB_TOP:           pos = 0; break;
    case SB_BOTTOM:        pos = max; break;
    case SB_THUMBTRACK:
    case SB_THUMBPOSITION: pos = thumb; break;
    }
    return pos < 0 ? 0 : pos > max ? max : pos;
}

BOOL FAR PASCAL __loadds KeyboardProc(HWND dlg, UINT msg, WPARAM wp, LPARAM lp)
{
    switch (msg) {
    case WM_INITDIALOG:
        delay = delay0 = GetProfileInt("windows", "KeyboardDelay", 2);
        speed = speed0 = GetProfileInt("windows", "KeyboardSpeed", 31);
        if (delay < 0) delay = 0;
        if (delay > 3) delay = 3;
        if (speed < 0) speed = 0;
        if (speed > 31) speed = 31;
        setpos(dlg, IDC_KB_DELAY, 3 - delay, 3);
        setpos(dlg, IDC_KB_SPEED, speed, 31);
        return TRUE;
    case WM_HSCROLL: {
        HWND sb = (HWND)HIWORD(lp);
        if (sb == GetDlgItem(dlg, IDC_KB_DELAY)) {
            SystemParametersInfo(SPI_SETKEYBOARDDELAY, delay, NULL, 0);
            delay = 3 - scroll(3 - delay, 3, wp, LOWORD(lp));
            SetScrollPos(sb, SB_CTL, 3 - delay, TRUE);
        } else if (sb == GetDlgItem(dlg, IDC_KB_SPEED)) {
            speed = scroll(speed, 31, wp, LOWORD(lp));
            SetScrollPos(sb, SB_CTL, speed, TRUE);
            SystemParametersInfo(SPI_SETKEYBOARDSPEED, speed, NULL, 0);
        }
        return TRUE;
    }
    case WM_COMMAND:
        switch (wp) {
        case IDOK: {
            char b[8];
            wsprintf(b, "%d", delay);
            WriteProfileString("windows", "KeyboardDelay", b);
            wsprintf(b, "%d", speed);
            WriteProfileString("windows", "KeyboardSpeed", b);
            SystemParametersInfo(SPI_SETKEYBOARDDELAY, delay, NULL, 0);
            SystemParametersInfo(SPI_SETKEYBOARDSPEED, speed, NULL, 0);
            /* observed (wnchange): "windows", lower case; Cancel sends none */
            SendMessage(HWND_BROADCAST, WM_WININICHANGE, 0, (LPARAM)(LPSTR)"windows");
            EndDialog(dlg, IDOK);
            return TRUE;
        }
        case IDCANCEL:
            SystemParametersInfo(SPI_SETKEYBOARDSPEED, speed0, NULL, 0);
            SystemParametersInfo(SPI_SETKEYBOARDDELAY, delay0, NULL, 0);
            EndDialog(dlg, IDCANCEL);
            return TRUE;
        case IDC_HELP:
            cpl_help(dlg, AP_KEYBOARD);
            return TRUE;
        }
        break;
    }
    return FALSE;
}

void keyboard_applet(HWND parent)
{
    cpl_dialog(parent, DLG_KEYBOARD, KeyboardProc);
}
