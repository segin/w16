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
 * - Mouse Trails goes through the display driver's MOUSETRAILS escape
 *   (Windows 3.1 DDK, "Mouse Trails": the driver draws the trail and keeps
 *   WIN.INI MouseTrails, positive on, negative off): -1 turns it on, 0 off,
 *   as the box is clicked; Cancel puts back the old state. The box is
 *   checked when QUERYESCSUPPORT(MOUSETRAILS) answers a positive trail
 *   length, and disabled when it answers 0 (no trails). Observed in
 *   WIN.INI with Microsoft's VGA.DRV: 7 when checked, -7 when unchecked,
 *   absent and unchanged not written, turned on then Cancel -7.
 * - A mouse button held down over the dialog (not on a control) shows in
 *   the picture: the left (right) button's box inverted while it is down;
 *   double-clicking the TEST box inverts it, at the double-click speed the
 *   bar has just set (observed on 3.1 with real input).
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

#define MOUSETRAILS 39             /* the display driver escape (DDK) */
#define ABS(v) ((v) < 0 ? -(v) : (v))

static int trails_query(void)
{
    HDC dc = GetDC(NULL);
    int n = MOUSETRAILS, r = Escape(dc, QUERYESCSUPPORT, sizeof n, (LPCSTR)&n, NULL);
    ReleaseDC(NULL, dc);
    return r;
}

static void put_trails(BOOL on)
{
    HDC dc = GetDC(NULL);
    int n = on ? -1 : 0;
    Escape(dc, MOUSETRAILS, sizeof n, (LPCSTR)&n, NULL);
    ReleaseDC(NULL, dc);
}

/* the picture: a box of the dialog inverted inside its frame */
static void invert_ctl(HWND dlg, int id)
{
    RECT r;
    HDC dc;
    GetWindowRect(GetDlgItem(dlg, id), &r);
    ScreenToClient(dlg, (POINT FAR *)&r.left);
    ScreenToClient(dlg, (POINT FAR *)&r.right);
    InflateRect(&r, -1, -1);
    dc = GetDC(dlg);
    InvertRect(dc, &r);
    ReleaseDC(dlg, dc);
}

static BOOL down_l, down_r, test_on;
static DWORD last_click;
static POINT last_pt;

static BOOL in_ctl(HWND dlg, int id, LPARAM lp)
{
    RECT r;
    POINT pt;
    GetWindowRect(GetDlgItem(dlg, id), &r);
    ScreenToClient(dlg, (POINT FAR *)&r.left);
    ScreenToClient(dlg, (POINT FAR *)&r.right);
    pt.x = (int)LOWORD(lp);
    pt.y = (int)HIWORD(lp);
    return PtInRect(&r, pt);
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

/* a time outside 100..900 (WIN.INI, SetDoubleClickTime) at the bar's
   nearer end: observed, 1000 at Slow, 60 at Fast (msdbl, msdbl2) */
static void set_dblclk_bar(HWND dlg)
{
    UINT v = dblclk < DBL_MIN ? DBL_MIN : dblclk > DBL_MAX ? DBL_MAX : dblclk;
    SetScrollPos(GetDlgItem(dlg, IDC_MS_DBLCLK), SB_CTL, (DBL_MAX - v) / DBL_STEP, TRUE);
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
        trails_len = trails_query();
        trails0 = trails_len > 0;
        CheckDlgButton(dlg, IDC_MS_TRAILS, trails0);
        EnableWindow(GetDlgItem(dlg, IDC_MS_TRAILS), trails_len != 0);
        down_l = down_r = test_on = FALSE;
        last_click = 0;
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
    case WM_LBUTTONDOWN:
    case WM_LBUTTONDBLCLK:
        if (!down_l) {
            down_l = TRUE;
            invert_ctl(dlg, IDC_MS_LBOX);
        }
        if (in_ctl(dlg, IDC_MS_TEST, lp)) {
            /* a double click: the dialog's class may give WM_LBUTTONDBLCLK,
               else two downs within the double-click time and size */
            DWORD t = GetMessageTime();
            BOOL dbl = msg == WM_LBUTTONDBLCLK ||
                       (last_click && t - last_click <= GetDoubleClickTime() &&
                        ABS((int)LOWORD(lp) - last_pt.x) <= GetSystemMetrics(SM_CXDOUBLECLK) / 2 &&
                        ABS((int)HIWORD(lp) - last_pt.y) <= GetSystemMetrics(SM_CYDOUBLECLK) / 2);
            if (dbl) {
                test_on = !test_on;
                invert_ctl(dlg, IDC_MS_TEST);
                last_click = 0;
            } else {
                last_click = t;
                last_pt.x = (int)LOWORD(lp);
                last_pt.y = (int)HIWORD(lp);
            }
        } else
            last_click = 0;
        return TRUE;
    case WM_LBUTTONUP:
        if (down_l) {
            down_l = FALSE;
            invert_ctl(dlg, IDC_MS_LBOX);
        }
        return TRUE;
    case WM_RBUTTONDOWN:
    case WM_RBUTTONDBLCLK:
        if (!down_r) {
            down_r = TRUE;
            invert_ctl(dlg, IDC_MS_RBOX);
        }
        return TRUE;
    case WM_RBUTTONUP:
        if (down_r) {
            down_r = FALSE;
            invert_ctl(dlg, IDC_MS_RBOX);
        }
        return TRUE;
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
            /* observed (wnchange): "Windows", capital W; Cancel sends none */
            SendMessage(HWND_BROADCAST, WM_WININICHANGE, 0, (LPARAM)(LPSTR)"Windows");
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
