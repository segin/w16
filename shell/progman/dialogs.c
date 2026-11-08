/*
 * dialogs.c - Run and About dialogs, program execution.
 */
#include "progman.h"

int run_dialog(HWND owner, FARPROC proc, int id)
{
    FARPROC thunk = MakeProcInstance(proc, g_hinst);
    int r = DialogBox(g_hinst, MAKEINTRESOURCE(id), owner, (DLGPROC)thunk);
    FreeProcInstance(thunk);
    return r;
}

/* Start a program. cmd is a command line; a document name is opened with
   the program associated with its extension (WinExec does not do that,
   ShellExecute does, Windows 3.1). dir, if given, becomes the current
   directory first. */
BOOL pm_exec(HWND owner, const char FAR *cmd, const char FAR *dir, BOOL minimized)
{
    char line[160];
    UINT r;

    if (dir && dir[0]) {
        if (dir[1] == ':') {
            char d = dir[0];
            if (d >= 'a' && d <= 'z')
                d -= 'a' - 'A';
            dos_setdrive(d - 'A');
        }
        dos_chdir(dir);
    }
    s_cpy(line, cmd, sizeof line);
    r = (UINT)ShellExecute(owner, NULL, line, NULL, NULL,
                           minimized ? SW_SHOWMINNOACTIVE : SW_SHOWNORMAL);
    if (r == 31) {                      /* no association: try it as a program */
        r = WinExec(line, minimized ? SW_SHOWMINNOACTIVE : SW_SHOWNORMAL);
    }
    if (r < 32) {
        message(owner, IDS_EXECFAIL, line);
        return FALSE;
    }
    return TRUE;
}

BOOL CALLBACK __export RunDlgProc(HWND dlg, UINT msg, WPARAM wp, LPARAM lp)
{
    char cmd[128];
    (void)lp;
    switch (msg) {
    case WM_INITDIALOG:
        SendDlgItemMessage(dlg, IDC_RUNCMD, EM_LIMITTEXT, sizeof cmd - 1, 0);
        EnableWindow(GetDlgItem(dlg, IDOK), FALSE);
        return TRUE;
    case WM_COMMAND:
        switch (wp) {
        case IDC_RUNCMD:
            if (HIWORD(lp) == EN_CHANGE)
                EnableWindow(GetDlgItem(dlg, IDOK),
                             GetWindowTextLength(GetDlgItem(dlg, IDC_RUNCMD)) > 0);
            return TRUE;
        case IDOK:
            GetDlgItemText(dlg, IDC_RUNCMD, cmd, sizeof cmd);
            if (pm_exec(dlg, cmd, NULL, IsDlgButtonChecked(dlg, IDC_RUNMIN))) {
                EndDialog(dlg, IDOK);
                if (g_minonrun)
                    ShowWindow(g_frame, SW_MINIMIZE);
            }
            return TRUE;
        case IDCANCEL:
            EndDialog(dlg, IDCANCEL);
            return TRUE;
        case IDC_BROWSE:
        case IDC_HELPBTN:
            message(dlg, IDS_NOTYET, NULL);     /* TODO */
            return TRUE;
        }
        break;
    }
    return FALSE;
}

BOOL CALLBACK __export AboutDlgProc(HWND dlg, UINT msg, WPARAM wp, LPARAM lp)
{
    (void)lp;
    switch (msg) {
    case WM_INITDIALOG:
        return TRUE;
    case WM_COMMAND:
        if (wp == IDOK || wp == IDCANCEL) {
            EndDialog(dlg, wp);
            return TRUE;
        }
        break;
    }
    return FALSE;
}
