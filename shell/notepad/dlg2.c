/*
 * dlg2.c - w16 Notepad, Windows 2.x build (W16_WIN2): what Windows 2.x
 * lacks and the 3.x build takes from COMMDLG and SHELL.
 *
 * Open and Save As use the layouts of the original's own "File Open" and
 * "File Save As" dialogs (templates 1 and 2 of the 3.1 NOTEPAD.EXE, read
 * as data; COMMDLG replaced them on 3.1): a file name, the current
 * directory, the text files and the directories and drives, filled by
 * DlgDirList. Find uses the original's template 9 (Find What, Match
 * Upper/Lowercase, Forward/Backward). About is our own.
 *
 * Windows 2.x cannot load the 3.x menu resource format wrc writes (its
 * 4-byte header): the menu is built with CreateMenu and ChangeMenu from
 * the same resource (as w16's Program Manager does).
 */
#include "notepad.h"

#ifdef W16_WIN2

void find_set(const char FAR *w, BOOL d, BOOL mc);
void find_get(char FAR *w, int size, BOOL *d, BOOL *mc);
void find_search2(HWND owner);

/* ---- the menu ---- */

static const BYTE FAR *build_items(HMENU m, const BYTE FAR *p)
{
    for (;;) {
        WORD flags = p[0] | (p[1] << 8), id = 0;
        const char FAR *text;
        p += 2;
        if (!(flags & MF_POPUP)) {
            id = p[0] | (p[1] << 8);
            p += 2;
        }
        text = (const char FAR *)p;
        p += s_len(text) + 1;
        if (flags & MF_POPUP) {
            HMENU sub = CreateMenu();
            p = build_items(sub, p);
            ChangeMenu(m, 0, text, (UINT)sub, MF_APPEND | MF_POPUP);
        } else if (!text[0] && !id) {
            ChangeMenu(m, 0, NULL, 0, MF_APPEND | MF_SEPARATOR);
        } else {
            ChangeMenu(m, 0, text, id,
                       MF_APPEND | MF_STRING | (flags & (MF_GRAYED | MF_DISABLED | MF_CHECKED)));
        }
        if (flags & MF_END)
            return p;
    }
}

HMENU load_menu2(void)
{
    HRSRC r = FindResource(g_hinst, MAKEINTRESOURCE(IDM_MENU), RT_MENU);
    HGLOBAL h = r ? LoadResource(g_hinst, r) : NULL;
    const BYTE FAR *p = h ? (const BYTE FAR *)LockResource(h) : NULL;
    HMENU m = NULL;
    if (p) {
        m = CreateMenu();
        build_items(m, p + 4 + (p[2] | (p[3] << 8)));
        UnlockResource(h);
    }
    if (h)
        FreeResource(h);
    return m;
}

/* ---- Open and Save As ---- */

static char *dlg_file;                  /* in: the current name; out: the choice */
static BOOL dlg_save;
static char spec[16] = "*.TXT";

static void fill_lists(HWND dlg)
{
    char s[16];
    if (!dlg_save) {
        s_cpy(s, spec, sizeof s);
        DlgDirList(dlg, s, IDC_FILES, IDC_PATH, 0);
    }
    s_cpy(s, "*.*", sizeof s);
    DlgDirList(dlg, s, IDC_DIRS, dlg_save ? IDC_PATH : 0, DDL_DIRECTORY | DDL_DRIVES | DDL_EXCLUSIVE);
}

static BOOL has_wild(const char *s)
{
    for (; *s; s++)
        if (*s == '*' || *s == '?')
            return TRUE;
    return FALSE;
}

BOOL FAR PASCAL __export FileDlgProc2(HWND dlg, UINT msg, WPARAM wp, LPARAM lp)
{
    char buf[DOS_PATH];
    switch (msg) {
    case WM_INITDIALOG:
        fill_lists(dlg);
        SetDlgItemText(dlg, IDC_FNAME, dlg_save && dlg_file[0] ? dlg_file : spec);
        SendDlgItemMessage(dlg, IDC_FNAME, EM_SETSEL, 0, MAKELONG(0, 0x7FFF));
        return TRUE;
    case WM_COMMAND:
        switch (wp) {
        case IDC_FILES:
            if (HIWORD(lp) == LBN_SELCHANGE || HIWORD(lp) == LBN_DBLCLK) {
                DlgDirSelect(dlg, buf, IDC_FILES);
                SetDlgItemText(dlg, IDC_FNAME, buf);
                if (HIWORD(lp) == LBN_DBLCLK)
                    PostMessage(dlg, WM_COMMAND, IDOK, 0);
            }
            return TRUE;
        case IDC_DIRS:
            if (HIWORD(lp) == LBN_DBLCLK) {
                DlgDirSelect(dlg, buf, IDC_DIRS);
                s_cat(buf, dlg_save ? "*.*" : spec, sizeof buf);
                DlgDirList(dlg, buf, dlg_save ? IDC_DIRS : IDC_FILES, IDC_PATH,
                           dlg_save ? DDL_DIRECTORY | DDL_DRIVES | DDL_EXCLUSIVE : 0);
                fill_lists(dlg);
            }
            return TRUE;
        case IDOK:
            GetDlgItemText(dlg, IDC_FNAME, buf, sizeof buf);
            if (!buf[0])
                return TRUE;
            if (has_wild(buf)) {        /* a new pattern: list it */
                DlgDirList(dlg, buf, dlg_save ? IDC_DIRS : IDC_FILES, IDC_PATH,
                           dlg_save ? DDL_DIRECTORY | DDL_DRIVES | DDL_EXCLUSIVE : 0);
                if (!dlg_save)
                    s_cpy(spec, buf, sizeof spec);
                fill_lists(dlg);
                SetDlgItemText(dlg, IDC_FNAME, buf);
                return TRUE;
            }
            {
                OFSTRUCT of;
                if (OpenFile(buf, &of, OF_PARSE) == HFILE_ERROR) {
                    msg_box(dlg, IDS_BADNAME, NULL, MB_OK | MB_ICONEXCLAMATION);
                    return TRUE;
                }
                if (dlg_save && OpenFile(buf, &of, OF_EXIST) != HFILE_ERROR &&
                    msg_box(dlg, IDS_REPLACE, of.szPathName, MB_YESNO | MB_ICONEXCLAMATION) != IDYES)
                    return TRUE;
                if (!dlg_save && OpenFile(buf, &of, OF_EXIST) == HFILE_ERROR) {
                    msg_box(dlg, IDS_CANTOPEN, buf, MB_OK | MB_ICONEXCLAMATION);
                    return TRUE;
                }
                s_cpy(dlg_file, of.szPathName, DOS_PATH);
            }
            EndDialog(dlg, TRUE);
            return TRUE;
        case IDCANCEL:
            EndDialog(dlg, FALSE);
            return TRUE;
        }
        break;
    }
    return FALSE;
}

BOOL open_dialog2(char *file, BOOL save)
{
    FARPROC p = MakeProcInstance((FARPROC)FileDlgProc2, g_hinst);
    BOOL r;
    dlg_file = file;
    dlg_save = save;
    r = DialogBox(g_hinst, MAKEINTRESOURCE(save ? IDD_SAVEAS2 : IDD_OPEN2), g_main, (DLGPROC)p) == TRUE;
    FreeProcInstance(p);
    return r;
}

/* ---- Find ---- */

BOOL FAR PASCAL __export FindDlgProc2(HWND dlg, UINT msg, WPARAM wp, LPARAM lp)
{
    char w[128];
    BOOL down, mc;
    (void)lp;
    switch (msg) {
    case WM_INITDIALOG:
        find_get(w, sizeof w, &down, &mc);
        SetDlgItemText(dlg, IDC_FNAME, w);
        CheckDlgButton(dlg, IDC_CASE, mc);
        CheckRadioButton(dlg, IDC_FWD, IDC_BACK, down ? IDC_FWD : IDC_BACK);
        return TRUE;
    case WM_COMMAND:
        switch (wp) {
        case IDC_CASE:
            CheckDlgButton(dlg, IDC_CASE, !IsDlgButtonChecked(dlg, IDC_CASE));
            return TRUE;
        case IDC_FWD:
        case IDC_BACK:
            CheckRadioButton(dlg, IDC_FWD, IDC_BACK, wp);
            return TRUE;
        case IDOK:
            GetDlgItemText(dlg, IDC_FNAME, w, sizeof w);
            find_set(w, IsDlgButtonChecked(dlg, IDC_FWD), IsDlgButtonChecked(dlg, IDC_CASE));
            EndDialog(dlg, TRUE);
            return TRUE;
        case IDCANCEL:
            EndDialog(dlg, FALSE);
            return TRUE;
        }
        break;
    }
    return FALSE;
}

void find_dialog2(void)
{
    FARPROC p = MakeProcInstance((FARPROC)FindDlgProc2, g_hinst);
    BOOL r = DialogBox(g_hinst, MAKEINTRESOURCE(IDD_FIND2), g_main, (DLGPROC)p) == TRUE;
    FreeProcInstance(p);
    if (r)
        find_search2(g_main);
}

/* ---- About ---- */

BOOL FAR PASCAL __export AboutDlgProc2(HWND dlg, UINT msg, WPARAM wp, LPARAM lp)
{
    (void)lp;
    if (msg == WM_INITDIALOG)
        return TRUE;
    if (msg == WM_COMMAND && (wp == IDOK || wp == IDCANCEL)) {
        EndDialog(dlg, TRUE);
        return TRUE;
    }
    return FALSE;
}

void about_dialog2(void)
{
    FARPROC p = MakeProcInstance((FARPROC)AboutDlgProc2, g_hinst);
    DialogBox(g_hinst, MAKEINTRESOURCE(IDD_ABOUT2), g_main, (DLGPROC)p);
    FreeProcInstance(p);
}

#endif
