/*
 * taskman.c - w16 Task List (TASKMAN.EXE), the dialog USER starts on
 * Ctrl+Esc and on a double-click on the desktop (ARCHITECTURE.md 15.5).
 *
 * Observed on Windows 3.1 (black box: screens, probes TM1 and TM2, and
 * TASKMAN's calls into USER and KERNEL recorded by USTRACE forwarders,
 * tests/taskman.sh):
 *
 * Start. USER runs SYSTEM.INI [boot] taskman.exe= (default TASKMAN.EXE)
 * with WinExec, SW_SHOWNORMAL; the command line is "X Y" in decimal: the
 * double-click's screen point, "0 0" for Ctrl+Esc and the system menu's
 * Switch To; WINOLDAP's Ctrl+Esc from a DOS program, "0 0" as well. Each
 * start is a new instance: there is no single-instance check, but a Task
 * List that loses activation ends (below), so only one is ever up.
 *
 * The dialog is template 10, a modal dialog box without owner. At
 * WM_INITDIALOG it is moved, size unchanged: for "0 0" (or no numbers)
 * centred on the screen, ((screen - size) / 2); otherwise with its corner
 * at (X - 101, Y - 40) on Windows 3.1's VGA screen, kept on the screen
 * (see place()). The numbers are read digit by digit, each character
 * taken as a digit up to a blank (3.1: "300,200" put the dialog at 0,0).
 * Then the list box gets the windows: the desktop's children in Z order
 * from the top, less the Task List itself, the hidden ones, the owned ones
 * and those without a title (GetWindowText 0), each added as its title
 * (LB_ADDSTRING, unsorted) with its handle as item data; the first is
 * selected and the dialog made topmost (SetWindowPos HWND_TOPMOST).
 * Focus goes to the list (the first tab stop).
 *
 * Switch To (also Enter, and a double-click on an item): the dialog ends,
 * then the selected window's last active popup, if it is a window and is
 * enabled, is activated by SwitchToThisWindow(popup, TRUE) (an icon is
 * restored); a disabled one gets MessageBeep(0) instead.
 * End Task: a window that is disabled (it has a modal dialog up) is not
 * closed: the dialog ends, MessageBeep(0), and the switch above (to its
 * dialog). Otherwise the dialog ends, the window's task is checked with
 * IsWinOldApTask; a DOS program's window ... (see end_task()); any other
 * window is switched to as above and posted WM_CLOSE (if it is still a
 * window).
 * Cascade, Tile, Arrange Icons: the dialog hides itself, then
 * CascadeChildWindows(desktop, 0), TileChildWindows(desktop, Shift held ?
 * MDITILE_HORIZONTAL : MDITILE_VERTICAL), ArrangeIconicWindows(desktop),
 * and ends. Cancel (Escape, Alt+F4) ends it.
 * Losing activation (WM_ACTIVATE inactive) posts the dialog WM_USER+100,
 * which ends it: a click on another window takes the Task List away.
 * Pen Windows: RegisterPenApp from the module GetSystemMetrics
 * (SM_PENWINDOWS) gives, when there is one.
 */
#include <windows.h>
#include "taskman.h"

extern BOOL FAR PASCAL IsWinOldApTask(HTASK);                 /* KERNEL.158 */
extern void FAR PASCAL SwitchToThisWindow(HWND, BOOL);        /* USER.172 */
extern void FAR PASCAL CascadeChildWindows(HWND, WORD);       /* USER.198 */
extern void FAR PASCAL TileChildWindows(HWND, WORD);          /* USER.199 */

#define WM_TASKLIST_END (WM_USER + 100)
/* where the point given on the command line lands in the dialog
   (measured on Windows 3.1, VGA: the dialog's corner at X - 101, Y - 40,
   for X from 0 to 600 and Y from 0 to 300. UNVERIFIED how 3.1 derives
   them: it asks only GetWindowRect (48,12-338,215 as the dialog is made)
   and the screen's size; 101 is the dialog's height / 2) */
#define PT_DX 101
#define PT_DY 40

typedef void (FAR PASCAL *PENAPP)(WORD, BOOL);

static int ptx, pty;

/* the selected window's last active popup: activate it, or beep when it
   is disabled */
static void switch_to(HWND dlg)
{
    HWND list = GetDlgItem(dlg, IDC_LIST);
    int i = (int)SendMessage(list, LB_GETCURSEL, 0, 0);
    HWND h = (HWND)(WORD)SendMessage(list, LB_GETITEMDATA, i, 0);
    HWND p;
    if (!IsWindow(h))
        return;
    p = GetLastActivePopup(h);
    if (!IsWindow(p))
        return;
    if (GetWindowLong(p, GWL_STYLE) & WS_DISABLED)
        MessageBeep(0);
    else
        SwitchToThisWindow(p, TRUE);
}

static void end_task(HWND dlg)
{
    HWND list = GetDlgItem(dlg, IDC_LIST);
    int i = (int)SendMessage(list, LB_GETCURSEL, 0, 0);
    HWND h = (HWND)(WORD)SendMessage(list, LB_GETITEMDATA, i, 0);
    if (!IsWindow(h))
        return;
    if (GetWindowLong(h, GWL_STYLE) & WS_DISABLED) {
        EndDialog(dlg, 0);
        MessageBeep(0);
        switch_to(dlg);
        return;
    }
    EndDialog(dlg, 1);
    if (IsWinOldApTask(GetWindowTask(h))) {
        /* a DOS program: see the header */
        switch_to(dlg);
        return;
    }
    switch_to(dlg);
    if (IsWindow(h) && !(GetWindowLong(h, GWL_STYLE) & WS_DISABLED))
        PostMessage(h, WM_CLOSE, 0, 0);
}

/* the dialog's place: centred for 0,0, else around the point, on the
   screen */
static void place(HWND dlg)
{
    RECT r;
    int cx, cy, sx, sy, x, y;
    GetWindowRect(dlg, &r);
    cx = r.right - r.left;
    cy = r.bottom - r.top;
    sx = GetSystemMetrics(SM_CXSCREEN);
    sy = GetSystemMetrics(SM_CYSCREEN);
    if (!ptx && !pty) {
        x = (sx - cx) / 2;
        y = (sy - cy) / 2;
    } else {
        x = ptx - PT_DX;
        y = pty - PT_DY;
        if (x + cx > sx)
            x = sx - cx;
        if (y + cy > sy)
            y = sy - cy;
        if (x < 0)
            x = 0;
        if (y < 0)
            y = 0;
    }
    MoveWindow(dlg, x, y, cx, cy, FALSE);
}

static void fill(HWND dlg)
{
    HWND list = GetDlgItem(dlg, IDC_LIST), h;
    char title[80];
    int i;
    for (h = GetWindow(dlg, GW_HWNDFIRST); h; h = GetWindow(h, GW_HWNDNEXT)) {
        if (h == dlg || !IsWindowVisible(h) || GetWindow(h, GW_OWNER))
            continue;
        if (!GetWindowText(h, title, sizeof title))
            continue;
        i = (int)SendMessage(list, LB_ADDSTRING, 0, (LPARAM)(LPSTR)title);
        SendMessage(list, LB_SETITEMDATA, i, MAKELPARAM(h, 0));
    }
    SendMessage(list, LB_SETCURSEL, 0, 0);
}

BOOL FAR PASCAL __export TaskManDlgProc(HWND dlg, UINT msg, WPARAM wp, LPARAM lp)
{
    switch (msg) {
    case WM_INITDIALOG:
        place(dlg);
        fill(dlg);
        SetWindowPos(dlg, HWND_TOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE);
        return TRUE;
    case WM_ACTIVATE:
        if (wp == WA_INACTIVE)
            PostMessage(dlg, WM_TASKLIST_END, 0, 0);
        return FALSE;
    case WM_TASKLIST_END:
        EndDialog(dlg, 0);
        return TRUE;
    case WM_COMMAND:
        switch (wp) {
        case IDC_LIST:
            if (HIWORD(lp) != LBN_DBLCLK)
                return FALSE;
            /* fall through: a double-click is Switch To */
        case IDOK:
            EndDialog(dlg, 0);
            switch_to(dlg);
            return TRUE;
        case IDC_ENDTASK:
            end_task(dlg);
            return TRUE;
        case IDCANCEL:
            EndDialog(dlg, 0);
            return TRUE;
        case IDC_CASCADE:
            ShowWindow(dlg, SW_HIDE);
            CascadeChildWindows(GetDesktopWindow(), 0);
            EndDialog(dlg, 1);
            return TRUE;
        case IDC_TILE:
            ShowWindow(dlg, SW_HIDE);
            TileChildWindows(GetDesktopWindow(),
                             GetKeyState(VK_SHIFT) < 0 ? MDITILE_HORIZONTAL : MDITILE_VERTICAL);
            EndDialog(dlg, 1);
            return TRUE;
        case IDC_ARRANGE:
            ShowWindow(dlg, SW_HIDE);
            ArrangeIconicWindows(GetDesktopWindow());
            EndDialog(dlg, 1);
            return TRUE;
        }
        return FALSE;
    }
    (void)lp;
    return FALSE;
}

/* "X Y": each character up to a blank taken as a digit */
static LPSTR number(LPSTR s, int *v)
{
    int n = 0;
    while (*s == ' ')
        s++;
    while (*s && *s != ' ')
        n = n * 10 + (*s++ - '0');
    *v = n;
    return s;
}

int PASCAL WinMain(HINSTANCE hi, HINSTANCE hprev, LPSTR cmd, int show)
{
    HMODULE pen = (HMODULE)GetSystemMetrics(SM_PENWINDOWS);
    PENAPP penapp = pen ? (PENAPP)GetProcAddress(pen, "RegisterPenApp") : 0;
    DLGPROC proc;
    (void)hprev, (void)show;
    cmd = number(cmd, &ptx);
    number(cmd, &pty);
    if (penapp)
        penapp(1, TRUE);
    proc = (DLGPROC)MakeProcInstance((FARPROC)TaskManDlgProc, hi);
    DialogBox(hi, MAKEINTRESOURCE(IDD_TASKLIST), 0, proc);
    FreeProcInstance((FARPROC)proc);
    if (penapp)
        penapp(1, FALSE);
    return 0;
}
