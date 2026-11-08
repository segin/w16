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
    LPSTR line, params;
    UINT r;
    int show = minimized ? SW_SHOWMINNOACTIVE : SW_SHOWNORMAL;

    if (dir && dir[0]) {
        if (dir[1] == ':') {
            char d = dir[0];
            if (d >= 'a' && d <= 'z')
                d -= 'a' - 'A';
            dos_setdrive(d - 'A');
        }
        dos_chdir(dir);
    }
    while (*cmd == ' ')
        cmd++;
    line = s_dup(cmd);
    if (!line)
        return FALSE;
    /* ShellExecute takes the file and its parameters separately (DOS
       names have no spaces, so the first blank ends the file name) */
    for (params = line; *params && *params != ' '; params++)
        ;
    if (*params)
        *params++ = 0;
    r = (UINT)ShellExecute(owner, NULL, line, *params ? params : NULL, NULL, show);
    if (r == 31)                        /* no association: try it as a program */
        r = WinExec(cmd, show);
    if (r < 32) {
        message(owner, IDS_EXECFAIL, cmd);
        mem_free(line);
        return FALSE;
    }
    mem_free(line);
    return TRUE;
}

BOOL CALLBACK __export RunDlgProc(HWND dlg, UINT msg, WPARAM wp, LPARAM lp)
{
    LPSTR cmd;
    int n;
    (void)lp;
    switch (msg) {
    case WM_INITDIALOG:
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
            n = GetWindowTextLength(GetDlgItem(dlg, IDC_RUNCMD)) + 1;
            cmd = mem_alloc(n);
            if (!cmd)
                return TRUE;
            GetDlgItemText(dlg, IDC_RUNCMD, cmd, n);
            if (pm_exec(dlg, cmd, NULL, IsDlgButtonChecked(dlg, IDC_RUNMIN))) {
                EndDialog(dlg, IDOK);
                if (g_minonrun)
                    ShowWindow(g_frame, SW_MINIMIZE);
            }
            mem_free(cmd);
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
