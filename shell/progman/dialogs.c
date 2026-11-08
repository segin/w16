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

/* ---- File > New ---- */

/* New Program Object: IDOK -> IDC_NEWGROUP or IDC_NEWITEM chosen. */
static int new_choice;

BOOL CALLBACK __export NewDlgProc(HWND dlg, UINT msg, WPARAM wp, LPARAM lp)
{
    (void)lp;
    switch (msg) {
    case WM_INITDIALOG:
        /* observed: Program Item is preselected when there is a group to
           put it in. With no group at all, Program Item is disabled and
           Program Group chosen (the user's specification). */
        new_choice = SendMessage(g_client, WM_MDIGETACTIVE, 0, 0) ? IDC_NEWITEM : IDC_NEWGROUP;
        EnableWindow(GetDlgItem(dlg, IDC_NEWITEM), new_choice == IDC_NEWITEM);
        CheckRadioButton(dlg, IDC_NEWGROUP, IDC_NEWITEM, new_choice);
        SetFocus(GetDlgItem(dlg, new_choice));
        return FALSE;
    case WM_COMMAND:
        switch (wp) {
        case IDOK:
            new_choice = IsDlgButtonChecked(dlg, IDC_NEWGROUP) ? IDC_NEWGROUP : IDC_NEWITEM;
            EndDialog(dlg, IDOK);
            return TRUE;
        case IDCANCEL:
            EndDialog(dlg, IDCANCEL);
            return TRUE;
        case IDC_HELPBTN:
            message(dlg, IDS_NOTYET, NULL);     /* TODO: help */
            return TRUE;
        }
        break;
    }
    return FALSE;
}

/* Text of a dialog control in a new global block. */
static LPSTR dlg_text(HWND dlg, int id)
{
    int n = GetWindowTextLength(GetDlgItem(dlg, id)) + 1;
    LPSTR s = mem_alloc(n);
    if (s)
        GetDlgItemText(dlg, id, s, n);
    return s;
}

static BOOL is_blank(const char FAR *s)
{
    while (*s == ' ')
        s++;
    return !*s;
}

/* The file for a new group: as typed (default directory: Windows', and
   extension .GRP), or made from the description: blanks dropped, first
   eight characters, upper case (observed: "Test Group" -> TESTGROU.GRP).
   UNVERIFIED: how the original avoids a name already in use; here digits
   replace the end of the name. */
static LPSTR group_file_name(const char FAR *desc, const char FAR *typed)
{
    char windir[144];                   /* DOS path bound (OFSTRUCT) */
    char base[9];
    LPSTR path = NULL;
    OFSTRUCT of;
    unsigned tries;
    int n = 0;

    GetWindowsDirectory(windir, sizeof windir);
    if (typed && !is_blank(typed)) {
        BOOL dirpart = FALSE, ext = FALSE;
        const char FAR *c;
        while (*typed == ' ')
            typed++;
        for (c = typed; *c; c++) {
            if (*c == '\\' || *c == ':') {
                dirpart = TRUE;
                ext = FALSE;
            } else if (*c == '.') {
                ext = TRUE;
            }
        }
        if (!dirpart) {
            path = s_dup(windir);
            if (path && path[s_len(path) - 1] != '\\')
                s_append(&path, "\\");
            if (path)
                s_append(&path, typed);
        } else {
            path = s_dup(typed);
        }
        if (path && !ext)
            s_append(&path, ".GRP");
        return path;
    }
    for (; *desc && n < 8; desc++)
        if (*desc != ' ') {
            char ch = *desc;
            if (ch >= 'a' && ch <= 'z')
                ch -= 'a' - 'A';
            base[n++] = ch;
        }
    if (!n)
        base[n++] = 'G';
    base[n] = 0;
    for (tries = 0;; tries++) {
        char name[16], digits[6];
        mem_free(path);
        s_cpy(name, base, sizeof name);
        if (tries) {
            int d;
            s_utoa(digits, tries);
            d = s_len(digits);
            s_cpy(name + (n + d > 8 ? 8 - d : n), digits, sizeof name - 8 + d);
        }
        path = s_dup(windir);
        if (!path)
            return NULL;
        if (path[s_len(path) - 1] != '\\')
            s_append(&path, "\\");
        s_append(&path, name);
        s_append(&path, ".GRP");
        if (!path || OpenFile(path, &of, OF_EXIST) == HFILE_ERROR || tries == 0xFFFFu)
            return path;
    }
}

BOOL CALLBACK __export GroupPropsDlgProc(HWND dlg, UINT msg, WPARAM wp, LPARAM lp)
{
    LPSTR desc, file, path;
    (void)lp;
    switch (msg) {
    case WM_INITDIALOG:
        EnableWindow(GetDlgItem(dlg, IDOK), FALSE);   /* observed */
        return TRUE;
    case WM_COMMAND:
        switch (wp) {
        case IDC_DESC:
            if (HIWORD(lp) == EN_CHANGE)
                EnableWindow(GetDlgItem(dlg, IDOK),
                             GetWindowTextLength(GetDlgItem(dlg, IDC_DESC)) > 0);
            return TRUE;
        case IDOK:
            desc = dlg_text(dlg, IDC_DESC);
            file = dlg_text(dlg, IDC_GROUPFILE);
            path = desc ? group_file_name(desc, file) : NULL;
            if (path) {
                int n = ini_add_group(path);
                if (group_create(desc, path, n)) {
                    ini_groups_changed();
                    EndDialog(dlg, IDOK);
                } else {
                    ini_remove_group(n);
                    message(dlg, IDS_CANTWRITE, path);
                }
            }
            mem_free(desc);
            mem_free(file);
            mem_free(path);
            return TRUE;
        case IDCANCEL:
            EndDialog(dlg, IDCANCEL);
            return TRUE;
        case IDC_HELPBTN:
            message(dlg, IDS_NOTYET, NULL);
            return TRUE;
        }
        break;
    }
    return FALSE;
}

BOOL CALLBACK __export ItemPropsDlgProc(HWND dlg, UINT msg, WPARAM wp, LPARAM lp)
{
    LPSTR desc, cmd, dir;
    HWND group;
    (void)lp;
    switch (msg) {
    case WM_INITDIALOG:
        /* observed for a new item: OK and Change Icon start disabled, and
           no icon is shown yet */
        EnableWindow(GetDlgItem(dlg, IDOK), FALSE);
        EnableWindow(GetDlgItem(dlg, IDC_CHANGEICON), FALSE);
        return TRUE;
    case WM_COMMAND:
        switch (wp) {
        case IDC_CMDLINE:
            /* UNVERIFIED: OK becomes available once there is a command */
            if (HIWORD(lp) == EN_CHANGE)
                EnableWindow(GetDlgItem(dlg, IDOK),
                             GetWindowTextLength(GetDlgItem(dlg, IDC_CMDLINE)) > 0);
            return TRUE;
        case IDOK:
            group = (HWND)(WORD)SendMessage(g_client, WM_MDIGETACTIVE, 0, 0);
            desc = dlg_text(dlg, IDC_DESC);
            cmd = dlg_text(dlg, IDC_CMDLINE);
            dir = dlg_text(dlg, IDC_WORKDIR);
            if (desc && cmd && dir && group &&
                group_add_item(group, desc, cmd, dir,
                               (WORD)SendDlgItemMessage(dlg, IDC_HOTKEY, HKM_GETHOTKEY, 0, 0),
                               IsDlgButtonChecked(dlg, IDC_ITEMMIN)))
                EndDialog(dlg, IDOK);
            else if (group)
                message(dlg, IDS_CANTWRITE, group_path(group));
            mem_free(desc);
            mem_free(cmd);
            mem_free(dir);
            return TRUE;
        case IDCANCEL:
            EndDialog(dlg, IDCANCEL);
            return TRUE;
        case IDC_BROWSE:
        case IDC_CHANGEICON:
        case IDC_HELPBTN:
            message(dlg, IDS_NOTYET, NULL);     /* TODO */
            return TRUE;
        }
        break;
    }
    return FALSE;
}

void cmd_new(HWND frame)
{
    if (run_dialog(frame, (FARPROC)NewDlgProc, IDD_NEW) != IDOK)
        return;
    if (new_choice == IDC_NEWGROUP)
        run_dialog(frame, (FARPROC)GroupPropsDlgProc, IDD_GROUPPROPS);
    else if (SendMessage(g_client, WM_MDIGETACTIVE, 0, 0))
        run_dialog(frame, (FARPROC)ItemPropsDlgProc, IDD_ITEMPROPS);
    else
        message(frame, IDS_NOGROUP, NULL);
}

/* ---- File > Delete ---- */

/* Observed: in an open group with a selected item, Delete asks "Are you
   sure you want to delete the item '<name>'?"; in an empty or minimized
   group, "...the group '<name>'?" (box title "Delete", Yes/No,
   exclamation icon). Deleting a group removes its file and its
   PROGMAN.INI entry. */
void cmd_delete(HWND frame)
{
    HWND g = (HWND)(WORD)SendMessage(g_client, WM_MDIGETACTIVE, 0, 0);
    LPCSTR name;
    if (!g)
        return;
    if (!IsIconic(g) && (name = group_selected_name(g)) != NULL) {
        if (ask(frame, IDS_DELETE, IDS_DELITEM, name, MB_YESNO | MB_ICONEXCLAMATION) == IDYES &&
            !group_delete_selected(g))
            message(frame, IDS_CANTWRITE, group_path(g));
        return;
    }
    if (ask(frame, IDS_DELETE, IDS_DELGROUP, group_name(g), MB_YESNO | MB_ICONEXCLAMATION) == IDYES) {
        LPSTR path = s_dup(group_path(g));
        int n = group_ininum(g);
        OFSTRUCT of;
        SendMessage(g_client, WM_MDIDESTROY, (WPARAM)g, 0);
        if (path) {
            OpenFile(path, &of, OF_DELETE);
            mem_free(path);
        }
        ini_remove_group(n);
    }
}
