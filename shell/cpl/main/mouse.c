/*
 * mouse.c - the Mouse applet: tracking speed, double-click speed, button
 * swap, mouse trails.
 *
 * The mouse driver comes first: MOUSE.DRV's CplApplet (ordinal 6) is
 * called with 5, and when it returns TRUE it has shown its own dialog and
 * this one is not shown (Windows 3.1 DDK, Device Driver Adaptation Guide,
 * "CplApplet").
 *
 * Observed on Windows 3.1 (tests/scripts/cpl/ms*.txt):
 * - Tracking speed: seven settings, slow to fast, of WIN.INI [windows]
 *   MouseThreshold1, MouseThreshold2, MouseSpeed: 0 0 0, 10 0 1, 7 0 1,
 *   4 0 1, 4 12 2, 4 9 2, 4 6 2. OK always writes the three keys, in that
 *   order (the current values, from SPI_GETMOUSE, when the bar did not
 *   move). The bar starts at the slowest setting not faster than the
 *   current values (2 0 1 starts at 4 0 1, 6 10 2 at 4 12 2, 10 0 1 at
 *   10 0 1).
 * - Double-click speed: DoubleClickSpeed in milliseconds, 100 (Fast) to
 *   900 (Slow); one step of the bar adds or takes 16 (452 -> 468,
 *   450 -> 466).
 * - Both bars take Home (SB_TOP) as their Fast end and End as Slow.
 * - SwapMouseButtons=yes / no, when the box was changed.
 * - MouseTrails is written as soon as the box is clicked: 7 when checked,
 *   -7 when unchecked (the length is kept, negative when off); Cancel
 *   writes the old state back (absent and unchanged: not written; turned
 *   on, then Cancel: -7).
 * - With the buttons swapped the L and R boxes swap their labels.
 * Keys written before the tracking keys (observed: SwapMouseButtons
 * appended before them).
 */
#include "maincpl.h"

typedef BOOL (FAR PASCAL *MOUSEAPPLET)(HWND, UINT, DWORD, DWORD);

static const int accel[7][3] = {
    { 0, 0, 0 }, { 10, 0, 1 }, { 7, 0, 1 }, { 4, 0, 1 },
    { 4, 12, 2 }, { 4, 9, 2 }, { 4, 6, 2 }
};

static int cur[3], orig[3], track, track0;
static UINT dblclk, dblclk0;
static BOOL swap0, trails0;
static int trails_len;

#define DBL_MIN 100
#define DBL_MAX 900
#define DBL_STEP 16

/* the slowest setting not faster than v (see above; UNVERIFIED beyond
   the observed starting points) */
static int track_of(const int *v)
{
    int i;
    for (i = 6; i > 0; i--) {
        const int *e = accel[i];
        if (e[2] < v[2])
            return i;
        if (e[2] == v[2] && (v[2] == 2 ? e[1] >= v[1] : e[0] >= v[0]))
            return i;
    }
    return 0;
}

static void put_int(const char *key, int v)
{
    char b[8];
    wsprintf(b, "%d", v);
    WriteProfileString("windows", key, b);
}

static void put_trails(BOOL on)
{
    put_int("MouseTrails", on ? trails_len : -trails_len);
}

/* WIN.INI booleans: yes, on, true or a nonzero number */
static BOOL profile_bool(const char *key)
{
    char b[8];
    GetProfileString("windows", key, "", b, sizeof b);
    return !lstrcmpi(b, "yes") || !lstrcmpi(b, "on") || !lstrcmpi(b, "true")
           || GetProfileInt("windows", key, 0) != 0;
}

static void show_swap(HWND dlg, BOOL swapped)
{
    SetDlgItemText(dlg, IDC_MS_L, swapped ? "R" : "L");
    SetDlgItemText(dlg, IDC_MS_R, swapped ? "L" : "R");
}

static void set_dblclk_bar(HWND dlg)
{
    SetScrollPos(GetDlgItem(dlg, IDC_MS_DBLCLK), SB_CTL, (DBL_MAX - dblclk) / DBL_STEP, TRUE);
}

BOOL FAR PASCAL __loadds MouseProc(HWND dlg, UINT msg, WPARAM wp, LPARAM lp)
{
    switch (msg) {
    case WM_INITDIALOG: {
        HWND sb;
        int i;
        SystemParametersInfo(SPI_GETMOUSE, 0, cur, 0);
        for (i = 0; i < 3; i++)
            orig[i] = cur[i];
        track = track0 = track_of(cur);
        sb = GetDlgItem(dlg, IDC_MS_TRACK);
        SetScrollRange(sb, SB_CTL, 0, 6, FALSE);
        SetScrollPos(sb, SB_CTL, track, TRUE);
        dblclk = dblclk0 = GetDoubleClickTime();
        SetScrollRange(GetDlgItem(dlg, IDC_MS_DBLCLK), SB_CTL, 0, (DBL_MAX - DBL_MIN) / DBL_STEP, FALSE);
        set_dblclk_bar(dlg);
        swap0 = profile_bool("SwapMouseButtons");
        CheckDlgButton(dlg, IDC_MS_SWAP, swap0);
        show_swap(dlg, swap0);
        trails_len = GetProfileInt("windows", "MouseTrails", 0);
        trails0 = trails_len > 0;
        if (trails_len < 0)
            trails_len = -trails_len;
        if (!trails_len)
            trails_len = 7;
        CheckDlgButton(dlg, IDC_MS_TRAILS, trails0);
        return TRUE;
    }
    case WM_HSCROLL: {
        HWND sb = (HWND)HIWORD(lp);
        if (sb == GetDlgItem(dlg, IDC_MS_TRACK)) {
            switch (wp) {
            case SB_LINEUP: case SB_PAGEUP:     track--; break;
            case SB_LINEDOWN: case SB_PAGEDOWN: track++; break;
            case SB_TOP:                        track = 6; break;
            case SB_BOTTOM:                     track = 0; break;
            case SB_THUMBPOSITION:              track = LOWORD(lp); break;
            default: return TRUE;
            }
            if (track < 0) track = 0;
            if (track > 6) track = 6;
            SetScrollPos(sb, SB_CTL, track, TRUE);
            SystemParametersInfo(SPI_SETMOUSE, 0, (LPVOID)accel[track], 0);
        } else if (sb == GetDlgItem(dlg, IDC_MS_DBLCLK)) {
            long v = dblclk;
            switch (wp) {
            case SB_LINEUP:        v += DBL_STEP; break;
            case SB_LINEDOWN:      v -= DBL_STEP; break;
            case SB_PAGEUP:        v += 10 * DBL_STEP; break;   /* observed: 160 */
            case SB_PAGEDOWN:      v -= 10 * DBL_STEP; break;
            case SB_TOP:           v = DBL_MIN; break;
            case SB_BOTTOM:        v = DBL_MAX; break;
            case SB_THUMBPOSITION: v = DBL_MAX - (long)LOWORD(lp) * DBL_STEP; break;
            default: return TRUE;
            }
            if (v < DBL_MIN) v = DBL_MIN;
            if (v > DBL_MAX) v = DBL_MAX;
            dblclk = (UINT)v;
            set_dblclk_bar(dlg);
            SetDoubleClickTime(dblclk);
        }
        return TRUE;
    }
    case WM_COMMAND:
        switch (wp) {
        case IDC_MS_SWAP: {     /* a plain BS_CHECKBOX: toggled here */
            BOOL on = !IsDlgButtonChecked(dlg, IDC_MS_SWAP);
            CheckDlgButton(dlg, IDC_MS_SWAP, on);
            SwapMouseButton(on);
            show_swap(dlg, on);
            return TRUE;
        }
        case IDC_MS_TRAILS:
            put_trails(IsDlgButtonChecked(dlg, IDC_MS_TRAILS));
            return TRUE;
        case IDOK: {
            BOOL swap = IsDlgButtonChecked(dlg, IDC_MS_SWAP);
            const int *v = track != track0 ? accel[track] : cur;
            if (dblclk != dblclk0)
                put_int("DoubleClickSpeed", dblclk);
            if (swap != swap0)
                WriteProfileString("windows", "SwapMouseButtons", swap ? "yes" : "no");
            put_int("MouseThreshold1", v[0]);
            put_int("MouseThreshold2", v[1]);
            put_int("MouseSpeed", v[2]);
            EndDialog(dlg, IDOK);
            return TRUE;
        }
        case IDCANCEL:
            SystemParametersInfo(SPI_SETMOUSE, 0, orig, 0);
            SetDoubleClickTime(dblclk0);
            SwapMouseButton(swap0);
            if (IsDlgButtonChecked(dlg, IDC_MS_TRAILS) != trails0)
                put_trails(trails0);
            EndDialog(dlg, IDCANCEL);
            return TRUE;
        case IDC_HELP:
            cpl_help(dlg, AP_MOUSE);
            return TRUE;
        }
        break;
    }
    return FALSE;
}

void mouse_applet(HWND parent)
{
    HMODULE m = GetModuleHandle("MOUSE");
    if (m) {
        MOUSEAPPLET f = (MOUSEAPPLET)GetProcAddress(m, MAKEINTRESOURCE(6));
        if (f && f(parent, 5, 0, 0))
            return;
    }
    cpl_dialog(parent, DLG_MOUSE, MouseProc);
}
