/*
 * dialogs.c - Run and About dialogs, program execution.
 */
#include "progman.h"

#ifdef PM_DEBUGLOG
void dbg(const char FAR *s, int v)
{
    OFSTRUCT of;
    HFILE f = OpenFile("C:\\PMDEBUG.LOG", &of, OF_WRITE);
    char n[8];
    if (f == HFILE_ERROR)
        f = OpenFile("C:\\PMDEBUG.LOG", &of, OF_CREATE | OF_WRITE);
    if (f == HFILE_ERROR)
        return;
    _llseek(f, 0, 2);
    _lwrite(f, s, s_len(s));
    s_itoa(n, v);
    _lwrite(f, " ", 1);
    _lwrite(f, n, s_len(n));
    _lwrite(f, "\r\n", 2);
    _lclose(f);
}
#endif

/* Windows 2.x: plain radio buttons and check boxes (PM_RADIO, PM_CHECK)
   do not check themselves; do it on BN_CLICKED. TRUE if it was one. */
static BOOL manual_button(HWND dlg, WPARAM id, LPARAM lp)
{
#ifdef W16_WIN2
    if (HIWORD(lp) != BN_CLICKED)
        return FALSE;
    if (id == IDC_NEWGROUP || id == IDC_NEWITEM) {
        if (IsWindowEnabled(GetDlgItem(dlg, id)))
            CheckRadioButton(dlg, IDC_NEWGROUP, IDC_NEWITEM, id);
        return TRUE;
    }
    if (id == IDC_RUNMIN || id == IDC_ITEMMIN) {
        CheckDlgButton(dlg, id, !IsDlgButtonChecked(dlg, id));
        return TRUE;
    }
#else
    (void)dlg;
    (void)id;
    (void)lp;
#endif
    return FALSE;
}

int run_dialog(HWND owner, FARPROC proc, int id)
{
    FARPROC thunk = MakeProcInstance(proc, g_hinst);
    int r;
#ifdef PM_DEBUGLOG
    dbg("DialogBox id", id);
#endif
    r = DialogBox(g_hinst, MAKEINTRESOURCE(id), owner, (DLGPROC)thunk);
#ifdef PM_DEBUGLOG
    dbg("DialogBox returned", r);
#endif
    FreeProcInstance(thunk);
    if (r == -1)                        /* could not be created */
        message(owner, IDS_DLGFAIL, NULL);
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
        if (manual_button(dlg, wp, lp))
            return TRUE;
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
        if (manual_button(dlg, wp, lp))
            return TRUE;
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

/* The group being edited by Properties, or NULL for File > New. */
static HWND props_group;

/* A new group (New Program Group, DDE CreateGroup): its file (as typed,
   or made from the name), its PROGMAN.INI entry, its window. */
HWND create_group_named(const char FAR *name, const char FAR *file)
{
    LPSTR path = group_file_name(name, file);
    HWND w = NULL;
    int n;
    if (!path)
        return NULL;
    n = ini_add_group(path);
    w = group_create(name, path, n);
    if (w)
        ini_groups_changed();
    else
        ini_remove_group(n, NULL);
    mem_free(path);
    return w;
}

/* Delete a group: its window, its file, its PROGMAN.INI entry. */
void delete_group(HWND g)
{
    LPSTR path = s_dup(group_path(g));
    LPSTR order = order_string(FALSE);  /* before the window goes */
    int n = group_ininum(g);
    OFSTRUCT of;
    SendMessage(g_client, WM_MDIDESTROY, (WPARAM)g, 0);
    if (path) {
        OpenFile(path, &of, OF_DELETE);
        mem_free(path);
    }
    ini_remove_group(n, order);
    mem_free(order);
}

BOOL CALLBACK __export GroupPropsDlgProc(HWND dlg, UINT msg, WPARAM wp, LPARAM lp)
{
    LPSTR desc, file, path;
    (void)lp;
    switch (msg) {
    case WM_INITDIALOG:
        if (props_group) {              /* observed: name and file shown */
            SetDlgItemText(dlg, IDC_DESC, group_name(props_group));
            SetDlgItemText(dlg, IDC_GROUPFILE, group_path(props_group));
        } else {
            EnableWindow(GetDlgItem(dlg, IDOK), FALSE);   /* observed */
        }
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
            if (props_group) {
                file = dlg_text(dlg, IDC_GROUPFILE);
                path = file && desc ? group_file_name(desc, file) : NULL;
                if (!desc || !path || !group_rename(props_group, desc) ||
                    !group_set_file(props_group, path))
                    message(dlg, IDS_CANTWRITE, path ? path : group_path(props_group));
                else
                    EndDialog(dlg, IDOK);
                mem_free(desc);
                mem_free(file);
                mem_free(path);
                return TRUE;
            }
            file = dlg_text(dlg, IDC_GROUPFILE);
            if (desc && file) {
                if (create_group_named(desc, file))
                    EndDialog(dlg, IDOK);
                else
                    message(dlg, IDS_CANTWRITE, file);
            }
            mem_free(desc);
            mem_free(file);
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
    itemprops ip;
    HWND group;
    BOOL ok;
    (void)lp;
    switch (msg) {
    case WM_INITDIALOG:
        if (props_group && group_get_props(props_group, &ip)) {
            /* observed: the fields filled in, the item's icon shown */
            SetDlgItemText(dlg, IDC_DESC, ip.desc);
            SetDlgItemText(dlg, IDC_CMDLINE, ip.cmd);
            SetDlgItemText(dlg, IDC_WORKDIR, ip.dir);
            SendDlgItemMessage(dlg, IDC_HOTKEY, HKM_SETHOTKEY, ip.hotkey, 0);
            CheckDlgButton(dlg, IDC_ITEMMIN, ip.minimized);
#ifndef W16_WIN2
            SendDlgItemMessage(dlg, IDC_ITEMICON, STM_SETICON, (WPARAM)ip.icon, 0);
#endif
            itemprops_free(&ip);
            /* select the description, as a dialog does for its first edit */
            SendDlgItemMessage(dlg, IDC_DESC, EM_SETSEL, 0, MAKELPARAM(0, 0x7FFF));
            return TRUE;
        }
        /* observed for a new item: OK and Change Icon start disabled, and
           no icon is shown yet */
        EnableWindow(GetDlgItem(dlg, IDOK), FALSE);
        EnableWindow(GetDlgItem(dlg, IDC_CHANGEICON), FALSE);
        return TRUE;
    case WM_COMMAND:
        if (manual_button(dlg, wp, lp))
            return TRUE;
        switch (wp) {
        case IDC_CMDLINE:
            /* UNVERIFIED: OK becomes available once there is a command */
            if (HIWORD(lp) == EN_CHANGE)
                EnableWindow(GetDlgItem(dlg, IDOK),
                             GetWindowTextLength(GetDlgItem(dlg, IDC_CMDLINE)) > 0);
            return TRUE;
        case IDOK:
            group = props_group ? props_group
                                : (HWND)(WORD)SendMessage(g_client, WM_MDIGETACTIVE, 0, 0);
            ip.desc = dlg_text(dlg, IDC_DESC);
            ip.cmd = dlg_text(dlg, IDC_CMDLINE);
            ip.dir = dlg_text(dlg, IDC_WORKDIR);
            ip.hotkey = (WORD)SendDlgItemMessage(dlg, IDC_HOTKEY, HKM_GETHOTKEY, 0, 0);
            ip.minimized = IsDlgButtonChecked(dlg, IDC_ITEMMIN);
            ip.iconpath = NULL;
            ip.has_pos = FALSE;
            ip.dde = FALSE;
            ok = ip.desc && ip.cmd && ip.dir && group &&
                 (props_group ? group_edit_selected(group, &ip) : group_add_item(group, &ip));
            if (ok)
                EndDialog(dlg, IDOK);
            else if (group)
                message(dlg, IDS_CANTWRITE, group_path(group));
            itemprops_free(&ip);
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
    props_group = NULL;
    if (run_dialog(frame, (FARPROC)NewDlgProc, IDD_NEW) != IDOK)
        return;
    if (new_choice == IDC_NEWGROUP)
        run_dialog(frame, (FARPROC)GroupPropsDlgProc, IDD_GROUPPROPS);
    else if (SendMessage(g_client, WM_MDIGETACTIVE, 0, 0))
        run_dialog(frame, (FARPROC)ItemPropsDlgProc, IDD_ITEMPROPS);
    else
        message(frame, IDS_NOGROUP, NULL);
}

/* ---- File > Properties ---- */

/* Observed: on a selected item in an open group, Program Item Properties;
   on a minimized group, Program Group Properties. UNVERIFIED: an open
   group without items (taken to be the group's properties here). */
void cmd_properties(HWND frame)
{
    HWND g = (HWND)(WORD)SendMessage(g_client, WM_MDIGETACTIVE, 0, 0);
    if (!g)
        return;
    props_group = g;
    if (!IsIconic(g) && group_selected_name(g))
        run_dialog(frame, (FARPROC)ItemPropsDlgProc, IDD_ITEMPROPS);
    else
        run_dialog(frame, (FARPROC)GroupPropsDlgProc, IDD_GROUPPROPS);
    props_group = NULL;
}

/* ---- File > Move, File > Copy ---- */

/* Windows 2.x has no combo box control (3.0 and later): its dialog has a
   sorted list box instead. */
#ifdef W16_WIN2
#define TO_ADD      LB_ADDSTRING
#define TO_SETSEL   LB_SETCURSEL
#define TO_GETSEL   LB_GETCURSEL
#define TO_GETTEXT  LB_GETTEXT
#define TO_TEXTLEN  LB_GETTEXTLEN
#else
#define TO_ADD      CB_ADDSTRING
#define TO_SETSEL   CB_SETCURSEL
#define TO_GETSEL   CB_GETCURSEL
#define TO_GETTEXT  CB_GETLBTEXT
#define TO_TEXTLEN  CB_GETLBTEXTLEN
#endif

static HWND mc_from;
static BOOL mc_copy;

/* Observed: "Move Program Item" lists every group but the item's own,
   "Copy Program Item:" lists all of them; both sorted, the first chosen. */
BOOL CALLBACK __export MoveCopyDlgProc(HWND dlg, UINT msg, WPARAM wp, LPARAM lp)
{
    char text[64];                      /* our own resource strings */
    HWND w;
    (void)lp;
    switch (msg) {
    case WM_INITDIALOG:
        LoadString(g_hinst, mc_copy ? IDS_COPYTITLE : IDS_MOVETITLE, text, sizeof text);
        SetWindowText(dlg, text);
        LoadString(g_hinst, mc_copy ? IDS_COPYLABEL : IDS_MOVELABEL, text, sizeof text);
        SetDlgItemText(dlg, IDC_MC_LABEL, text);
        SetDlgItemText(dlg, IDC_MC_ITEM, group_selected_name(mc_from));
        SetDlgItemText(dlg, IDC_MC_GROUP, group_name(mc_from));
        for (w = GetWindow(g_client, GW_CHILD); w; w = GetWindow(w, GW_HWNDNEXT))
            if (group_name(w) && (mc_copy || w != mc_from))
                SendDlgItemMessage(dlg, IDC_MC_TO, TO_ADD, 0, (LPARAM)group_name(w));
        SendDlgItemMessage(dlg, IDC_MC_TO, TO_SETSEL, 0, 0);
        return TRUE;
    case WM_COMMAND:
        switch (wp) {
        case IDOK: {
            LONG i = SendDlgItemMessage(dlg, IDC_MC_TO, TO_GETSEL, 0, 0);
            LONG n = i < 0 ? -1 : SendDlgItemMessage(dlg, IDC_MC_TO, TO_TEXTLEN, (WPARAM)i, 0);
            LPSTR name = n >= 0 ? mem_alloc(n + 1) : NULL;
            HWND to = NULL;
            if (name) {
                SendDlgItemMessage(dlg, IDC_MC_TO, TO_GETTEXT, (WPARAM)i, (LPARAM)name);
                /* UNVERIFIED: groups with the same name; the first wins */
                for (w = GetWindow(g_client, GW_CHILD); w && !to; w = GetWindow(w, GW_HWNDNEXT))
                    if (group_name(w) && (mc_copy || w != mc_from) && !s_icmp(group_name(w), name))
                        to = w;
                mem_free(name);
            }
            if (to && !group_transfer_selected(mc_from, to, mc_copy))
                message(dlg, IDS_CANTWRITE, group_path(to));
            EndDialog(dlg, IDOK);
            return TRUE;
        }
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

/* UNVERIFIED: with no item selected (a minimized or empty group) the
   original greys these commands; here they do nothing. */
void cmd_movecopy(HWND frame, BOOL copy_only)
{
    HWND g = (HWND)(WORD)SendMessage(g_client, WM_MDIGETACTIVE, 0, 0);
    if (!g || IsIconic(g) || !group_selected_name(g))
        return;
    mc_from = g;
    mc_copy = copy_only;
    run_dialog(frame, (FARPROC)MoveCopyDlgProc, IDD_MOVECOPY);
}

/* ---- Exit Windows (as the shell) ---- */

BOOL CALLBACK __export ExitWinDlgProc(HWND dlg, UINT msg, WPARAM wp, LPARAM lp)
{
    RECT r;
    (void)lp;
    switch (msg) {
    case WM_INITDIALOG:
        /* observed: centred on the screen */
        GetWindowRect(dlg, &r);
        SetWindowPos(dlg, NULL,
                     (GetSystemMetrics(SM_CXSCREEN) - (r.right - r.left)) / 2,
                     (GetSystemMetrics(SM_CYSCREEN) - (r.bottom - r.top)) / 2,
                     0, 0, SWP_NOSIZE | SWP_NOZORDER);
#ifndef W16_WIN2
        SendDlgItemMessage(dlg, IDC_EXITICON, STM_SETICON,
                           (WPARAM)LoadIcon(NULL, IDI_ASTERISK), 0);
#endif
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
    if (ask(frame, IDS_DELETE, IDS_DELGROUP, group_name(g), MB_YESNO | MB_ICONEXCLAMATION) == IDYES)
        delete_group(g);
}
