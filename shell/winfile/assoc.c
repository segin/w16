/*
 * assoc.c - w16 File Manager: File > Associate.
 *
 * Observed (obs WF8): "Files with Extension:" the extension of the
 * selected file in capitals; "Associate With:" the description of its
 * class in the registration database, or "(None)"; the list: "(None)"
 * and every class with an open command, "Description (program.exe)",
 * sorted, the extension's own selected. OK sets the extension's class
 * (REG.DAT) and WIN.INI [Extensions] "program ^.EXT" (as 3.1's SHELL and
 * programs expect); a program typed in or browsed for goes to WIN.INI
 * alone; "(None)" removes both. Browse... is COMMDLG's Open box.
 */
#include "winfile.h"

#define IDD_EXT         112
#define IDD_ASSOC       113
#define IDD_CLASSES     111
#define IDD_BROWSE      232
#define IDD_HELP        254

static char assoc_ext[8];
static HGLOBAL assoc_from;

/* the program file of a class's open command, "notepad.exe" */
static BOOL class_program(LPCSTR cls, LPSTR out, int n)
{
    char key[96], cmd[PATHLEN];
    LONG cb = sizeof cmd;
    int k, s;
    s_cpy(key, cls, sizeof key);
    s_cat(key, "\\shell\\open\\command", sizeof key);
    if (RegQueryValue(HKEY_CLASSES_ROOT, key, cmd, &cb) != ERROR_SUCCESS || !cmd[0])
        return FALSE;
    for (k = 0; cmd[k] && cmd[k] != ' '; k++)
        ;
    cmd[k] = 0;
    for (s = k; s > 0 && cmd[s - 1] != '\\' && cmd[s - 1] != ':'; s--)
        ;
    s_cpy(out, cmd + s, n);
    return TRUE;
}

static void fill_classes(HWND h)
{
    HWND lb = GetDlgItem(h, IDD_CLASSES);
    char cls[80], desc[80], prog[64], t[160];
    DWORD i;
    LONG cb;
    SendMessage(lb, LB_ADDSTRING, 0, (LPARAM)(LPSTR)load_str(IDS_NONE));
    for (i = 0; RegEnumKey(HKEY_CLASSES_ROOT, i, cls, sizeof cls) == ERROR_SUCCESS; i++) {
        if (cls[0] == '.')
            continue;
        cb = sizeof desc;
        if (RegQueryValue(HKEY_CLASSES_ROOT, cls, desc, &cb) != ERROR_SUCCESS || !desc[0])
            continue;
        if (!class_program(cls, prog, sizeof prog))
            continue;
        wsprintf(t, "%s (%s)", (LPSTR)desc, (LPSTR)prog);
        SendMessage(lb, LB_ADDSTRING, 0, (LPARAM)(LPSTR)t);
    }
}

static void ext_class(LPCSTR ext, LPSTR cls, int n)
{
    char key[8];
    LONG cb = n;
    key[0] = '.';
    s_cpy(key + 1, ext, sizeof key - 1);
    if (RegQueryValue(HKEY_CLASSES_ROOT, key, cls, &cb) != ERROR_SUCCESS)
        cls[0] = 0;
}

static void show_assoc(HWND h)
{
    char cls[80], desc[80], prog[64], t[160];
    LONG cb = sizeof desc;
    int i;
    GetDlgItemText(h, IDD_EXT, assoc_ext, sizeof assoc_ext);
    cls[0] = 0;
    if (assoc_ext[0])
        ext_class(assoc_ext, cls, sizeof cls);
    if (cls[0] && RegQueryValue(HKEY_CLASSES_ROOT, cls, desc, &cb) == ERROR_SUCCESS && desc[0]) {
        SetDlgItemText(h, IDD_ASSOC, desc);
        class_program(cls, prog, sizeof prog);
        wsprintf(t, "%s (%s)", (LPSTR)desc, (LPSTR)prog);
        i = (int)SendDlgItemMessage(h, IDD_CLASSES, LB_FINDSTRINGEXACT, (WPARAM)-1,
                                    (LPARAM)(LPSTR)t);
    } else if (assoc_ext[0] && GetProfileString("Extensions", assoc_ext, "", t, sizeof t)) {
        /* only WIN.INI knows it: its command without " ^.EXT" */
        int k;
        for (k = 0; t[k] && t[k] != '^'; k++)
            ;
        while (k > 0 && t[k - 1] == ' ')
            k--;
        t[k] = 0;
        SetDlgItemText(h, IDD_ASSOC, t);
        i = -1;
    } else {
        SetDlgItemText(h, IDD_ASSOC, load_str(IDS_NONE));
        i = 0;
    }
    SendDlgItemMessage(h, IDD_CLASSES, LB_SETCURSEL, i, 0);
}

static void set_assoc(HWND h)
{
    char want[PATHLEN], cls[80], desc[80], key[8], t[PATHLEN + 16];
    DWORD i;
    LONG cb;
    GetDlgItemText(h, IDD_EXT, assoc_ext, sizeof assoc_ext);
    GetDlgItemText(h, IDD_ASSOC, want, sizeof want);
    if (!assoc_ext[0])
        return;
    key[0] = '.';
    s_cpy(key + 1, assoc_ext, sizeof key - 1);
    if (!want[0] || !lstrcmpi(want, load_str(IDS_NONE))) {
        RegDeleteKey(HKEY_CLASSES_ROOT, key);
        WriteProfileString("Extensions", assoc_ext, NULL);
        return;
    }
    for (i = 0; RegEnumKey(HKEY_CLASSES_ROOT, i, cls, sizeof cls) == ERROR_SUCCESS; i++) {
        if (cls[0] == '.')
            continue;
        cb = sizeof desc;
        if (RegQueryValue(HKEY_CLASSES_ROOT, cls, desc, &cb) == ERROR_SUCCESS &&
            !lstrcmpi(desc, want)) {
            char kc[96], cmd[PATHLEN];
            int k;
            RegSetValue(HKEY_CLASSES_ROOT, key, REG_SZ, cls, s_len(cls));
            s_cpy(kc, cls, sizeof kc);
            s_cat(kc, "\\shell\\open\\command", sizeof kc);
            cb = sizeof cmd;
            if (RegQueryValue(HKEY_CLASSES_ROOT, kc, cmd, &cb) == ERROR_SUCCESS) {
                for (k = 0; cmd[k] && cmd[k] != ' '; k++)
                    ;
                cmd[k] = 0;
                wsprintf(t, "%s ^.%s", (LPSTR)cmd, (LPSTR)assoc_ext);
                WriteProfileString("Extensions", assoc_ext, t);
            }
            return;
        }
    }
    RegDeleteKey(HKEY_CLASSES_ROOT, key);
    wsprintf(t, "%s ^.%s", (LPSTR)want, (LPSTR)assoc_ext);
    WriteProfileString("Extensions", assoc_ext, t);
}

static void browse(HWND h)
{
    OPENFILENAME of;
    char file[PATHLEN];
    static char filt[96];
    LPBYTE b = (LPBYTE)&of;
    LPSTR q;
    int i;
    for (i = 0; i < (int)sizeof of; i++)
        b[i] = 0;
    s_cpy(filt, load_str(IDS_PROGFILTER), sizeof filt);
    for (q = filt; *q; q++)
        if (*q == '|')
            *q = 0;
    file[0] = 0;
    of.lStructSize = sizeof of;
    of.hwndOwner = h;
    of.lpstrFilter = filt;
    of.lpstrFile = file;
    of.nMaxFile = sizeof file;
    of.Flags = OFN_HIDEREADONLY | OFN_FILEMUSTEXIST;
    if (GetOpenFileName(&of)) {
        s_lower(file);
        SetDlgItemText(h, IDD_ASSOC, file);
    }
}

BOOL CALLBACK _export AssociateDlgProc(HWND h, UINT msg, WPARAM wp, LPARAM lp)
{
    switch (msg) {
    case WM_INITDIALOG: {
        LPSTR from = (LPSTR)GlobalLock(assoc_from);
        char name[PATHLEN];
        LPCSTR p = from;
        if (!next_name(&p, name, sizeof name))
            name[0] = 0;
        GlobalUnlock(assoc_from);
        s_cpy(assoc_ext, s_ext(name), sizeof assoc_ext);
        s_upper(assoc_ext);
        SendDlgItemMessage(h, IDD_EXT, EM_LIMITTEXT, 3, 0);
        fill_classes(h);
        SetDlgItemText(h, IDD_EXT, assoc_ext);
        show_assoc(h);
        return TRUE;
    }
    case WM_COMMAND:
        switch (wp) {
        case IDD_CLASSES:
            if (HIWORD(lp) == LBN_SELCHANGE) {
                char t[160];
                int i = (int)SendDlgItemMessage(h, IDD_CLASSES, LB_GETCURSEL, 0, 0), k;
                SendDlgItemMessage(h, IDD_CLASSES, LB_GETTEXT, i, (LPARAM)(LPSTR)t);
                k = s_len(t);
                if (i > 0) {
                    while (k > 0 && t[k - 1] != '(')
                        k--;
                    if (k > 1)
                        t[k - 2] = 0;
                }
                SetDlgItemText(h, IDD_ASSOC, t);
            }
            return TRUE;
        case IDD_EXT:
            if (HIWORD(lp) == EN_CHANGE)
                show_assoc(h);
            return TRUE;
        case IDD_BROWSE:
            browse(h);
            return TRUE;
        case IDOK:
            set_assoc(h);
            EndDialog(h, 1);
            refresh_all();
            return TRUE;
        case IDCANCEL:
            EndDialog(h, 0);
            return TRUE;
        case IDD_HELP:
            WinHelp(g_frame, load_str(IDS_HELPFILE), HELP_CONTEXT, 5000L + IDM_ASSOCIATE);
            return TRUE;
        }
        break;
    }
    return FALSE;
}

void file_associate(LPDIRWIN d)
{
    assoc_from = selected_text(d, FALSE);
    run_dialog("DLG_ASSOCIATE", (FARPROC)AssociateDlgProc);
    if (assoc_from)
        GlobalFree(assoc_from);
    assoc_from = NULL;
}
