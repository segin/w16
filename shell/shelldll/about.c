/*
 * about.c - w16 SHELL.DLL: ShellAbout (SHELL.22) and its dialog
 * procedure AboutDlgProc (SHELL.33).
 *
 * Observed on Windows 3.1 (probe SH8, which dumps every control of the
 * running dialog and takes screenshots; SH10 for USER's strings):
 * - szApp "Title#Text": the caption is "Title" and the first line ends
 *   with "Text" (only the first '#' counts); without '#', the caption is
 *   "About szApp" and the first line ends with szApp.
 * - szOtherStuff (or nothing) is the fourth line; the version line is
 *   "Version %s %s" with USER's string 516 ("3.1") and "(Debug)" on a
 *   debugging version; the user's and organisation's names and the
 *   serial-number line are USER's strings 514, 515 and 517, which Setup
 *   fills in.
 * - The mode line, "Memory:" GetFreeSpace(0) / 1024 KB with thousands
 *   separated by commas, "System Resources:" GetFreeSystemResources(0)
 *   percent; the SMARTDrive line's label is hidden (UNVERIFIED: whether
 *   3.1 shows it when SMARTDrive is loaded).
 * - hIcon is shown at the top left; with no icon that control is hidden
 *   and a 64x64 picture is drawn instead (Windows' logo there; w16 draws
 *   its own artwork, tools/mkaboutlogo.py).
 * - The return value is 1 (the dialog's result).
 * The template (shell.rc) reproduces the dialog's layout; the strings
 * that are SHELL's own and name Microsoft's product are replaced by w16's
 * (ARCHITECTURE.md 13: awaiting a decision).
 */
#include "shell.h"

#define STM_SETICON_31  (WM_USER + 0)   /* SDK 3.1 */
#define LOGO_X  10                      /* UNVERIFIED: the picture's place */
#define LOGO_Y  10

typedef struct {
    LPCSTR app, other;
    HICON icon;
} ABOUT;

static void user_string(HWND dlg, int id, WORD sid)
{
    char t[200];
    if (LoadString(GetModuleHandle("USER"), sid, t, sizeof t))
        SetDlgItemText(dlg, id, t);
}

static void init(HWND dlg, ABOUT __far *a)
{
    char fmt[200], t[400], ver[40], num[20];
    const char __far *hash = NULL, *s;
    HINSTANCE user = GetModuleHandle("USER");
    DWORD flags = GetWinFlags();
    WORD sid;

    for (s = a->app; s && *s; s++)
        if (*s == '#') {
            hash = s;
            break;
        }
    if (hash) {
        WORD n = (WORD)(hash - a->app);
        if (n > sizeof t - 1)
            n = sizeof t - 1;
        sh_move(t, a->app, n);
        t[n] = 0;
        SetWindowText(dlg, t);
        s = hash + 1;
    } else {
        GetWindowText(dlg, fmt, sizeof fmt);
        wsprintf(t, fmt, a->app ? a->app : (LPCSTR)"");
        SetWindowText(dlg, t);
        s = a->app ? a->app : (LPCSTR)"";
    }
    GetDlgItemText(dlg, IDC_LINE1, fmt, sizeof fmt);
    wsprintf(t, fmt, s);
    SetDlgItemText(dlg, IDC_LINE1, t);

    if (!LoadString(user, 516, ver, sizeof ver))
        ver[0] = 0;
    LoadString(g_inst, IDS_VERSION, fmt, sizeof fmt);
    if (GetSystemMetrics(SM_DEBUG))
        LoadString(g_inst, IDS_DEBUG, num, sizeof num);
    else
        num[0] = 0;
    wsprintf(t, fmt, (LPSTR)ver, (LPSTR)num);
    SetDlgItemText(dlg, IDC_VERSION, t);
    SetDlgItemText(dlg, IDC_OTHER, a->other ? a->other : (LPCSTR)"");
    user_string(dlg, IDC_USER, 514);
    user_string(dlg, IDC_ORG, 515);
    user_string(dlg, IDC_SERIAL, 517);

    sid = flags & WF_ENHANCED ? IDS_ENHANCED : flags & WF_STANDARD ? IDS_STANDARD : IDS_REALMODE;
    LoadString(g_inst, sid, t, sizeof t);
    SetDlgItemText(dlg, IDC_MODE, t);
    sh_commas(num, GetFreeSpace(0) / 1024);
    LoadString(g_inst, IDS_KBFREE, fmt, sizeof fmt);
    wsprintf(t, fmt, (LPSTR)num);
    SetDlgItemText(dlg, IDC_MEM, t);
    LoadString(g_inst, IDS_RESOURCES, t, sizeof t);
    SetDlgItemText(dlg, IDC_RESLABEL, t);
    LoadString(g_inst, IDS_PCTFREE, fmt, sizeof fmt);
    wsprintf(t, fmt, GetFreeSystemResources(0));
    SetDlgItemText(dlg, IDC_RES, t);
    ShowWindow(GetDlgItem(dlg, IDC_SMARTLABEL), SW_HIDE);

    /* the icon control is icon-sized (SH8: 32x32 even when hidden; the
       template's units cannot give 32 pixels high) */
    SetWindowPos(GetDlgItem(dlg, IDC_ABICON), NULL, 0, 0, GetSystemMetrics(SM_CXICON),
                 GetSystemMetrics(SM_CYICON), SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE);
    if (a->icon)
        SendDlgItemMessage(dlg, IDC_ABICON, STM_SETICON_31, (WPARAM)a->icon, 0);
    else
        ShowWindow(GetDlgItem(dlg, IDC_ABICON), SW_HIDE);
}

static void paint_logo(HWND dlg)
{
    PAINTSTRUCT ps;
    HDC dc = BeginPaint(dlg, &ps);
    HBITMAP b = LoadBitmap(g_inst, MAKEINTRESOURCE(BMP_LOGO));
    if (b) {
        HDC m = CreateCompatibleDC(dc);
        HBITMAP old = SelectObject(m, b);
        BitBlt(dc, LOGO_X, LOGO_Y, 64, 64, m, 0, 0, SRCCOPY);
        SelectObject(m, old);
        DeleteDC(m);
        DeleteObject(b);
    }
    EndPaint(dlg, &ps);
}

BOOL SHAPI AboutDlgProc(HWND dlg, UINT msg, WPARAM wp, LPARAM lp)
{
    switch (msg) {
    case WM_INITDIALOG:
        init(dlg, (ABOUT __far *)lp);
        return TRUE;
    case WM_PAINT:
        if (!IsWindowVisible(GetDlgItem(dlg, IDC_ABICON))) {
            paint_logo(dlg);
            return TRUE;
        }
        break;
    case WM_COMMAND:
        if (wp == IDOK || wp == IDCANCEL) {
            EndDialog(dlg, TRUE);
            return TRUE;
        }
        break;
    }
    return FALSE;
}

int SHAPI ShellAbout(HWND owner, LPCSTR app, LPCSTR other, HICON icon)
{
    ABOUT a;
    a.app = app;
    a.other = other;
    a.icon = icon;
    return DialogBoxParam(g_inst, MAKEINTRESOURCE(DLG_ABOUT), owner, (DLGPROC)AboutDlgProc,
                          (LPARAM)(ABOUT __far *)&a);
}
