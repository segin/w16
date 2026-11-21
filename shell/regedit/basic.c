/*
 * basic.c - w16 REGEDIT's basic view: the registered file types
 * (ARCHITECTURE.md 15.8; observed on Windows 3.1, probes RE1, RE3, RE5,
 * RE9, RE12).
 *
 * The view lists every key under HKEY_CLASSES_ROOT whose name does not
 * start with '.' by its value, sorted ("" for a key without one, which then
 * sorts first); a hidden list (ID 431h, off the window) keeps the
 * identifiers in the same order. The first entry is selected; Enter and a
 * double-click are Modify File Type.
 *
 * Modify, Add and Copy File Type are one dialog (Modify's Identifier is a
 * static): File Type, the Open and Print actions, each with its Command and
 * DDE settings (Uses DDE, DDE Message, the optional message for an
 * application not running, Application, Topic), kept apart while the
 * dialog is up. Add starts with Uses DDE checked for both actions and
 * nothing filled in; Copy with the selected type's settings and an empty
 * Identifier. The DDE fields are enabled only while Uses DDE is checked,
 * the not-running message only while the DDE Message is not empty.
 * Checking Uses DDE with a Command fills an empty Application with the
 * program's name (its first letter capital, the rest small: "pbrush.exe
 * %1" gives "Pbrush") and an empty Topic with "System".
 *
 * OK checks, in this order: a File Type (else "There is no File Type
 * specified."), an identifier SHELL accepts (1-63 characters from 21h-7Fh,
 * no backslash; else "The specified identifier is not valid."), and for
 * Add and Copy one that does not exist yet (any case; else "The identifier
 * ... already exists."); the box stays up. Then it writes, at once (no key
 * stays open): the identifier's value (the File Type), and for each action
 * shell\<action>\command (or the key alone without a command) and, with Uses
 * DDE and a DDE Message, shell\<action>\ddeexec with ifexec, application and
 * topic below it.
 */
#include "regedit.h"
#include <shellapi.h>
#include <commdlg.h>

/* one action's settings while the dialog is up */
typedef struct {
    BOOL dde;
    char cmd[PATHMAX], msg[PATHMAX], notrun[PATHMAX], app[256], topic[256];
} ACTION;

static ACTION act[2];           /* 0 open, 1 print */
static int cur;                 /* the action shown */
static int dlg_kind;            /* IDM_ADD, IDM_COPY, IDM_MODIFY */
static char ident[PATHMAX], ftype[PATHMAX];
static const char *verbs[2] = { "open", "print" };
static LPSTR default_app(LPCSTR cmd);

static HWND types(HWND v)  { return GetDlgItem(v, IDC_TYPES); }
static HWND idents(HWND v) { return GetDlgItem(v, IDC_IDENTS); }

void basic_refresh(HWND v)
{
    HKEY root;
    DWORD i;
    static char name[PATHMAX], val[PATHMAX];
    int sel = (int)SendMessage(types(v), LB_GETCURSEL, 0, 0);
    SendMessage(types(v), WM_SETREDRAW, FALSE, 0);
    SendMessage(types(v), LB_RESETCONTENT, 0, 0);
    SendMessage(idents(v), LB_RESETCONTENT, 0, 0);
    if (!RegCreateKey(HKEY_CLASSES_ROOT, "", &root)) {
        for (i = 0; !RegEnumKey(root, i, name, sizeof name); i++) {
            int at;
            if (name[0] == '.')
                continue;
            reg_get(name, val, sizeof val);
            at = (int)SendMessage(types(v), LB_ADDSTRING, 0, (LPARAM)(LPSTR)val);
            /* among equal names the key read first (the newer) stays first
               (RE5: Delete after Copy deleted the copy) */
            {
                static char t[PATHMAX];
                int n = (int)SendMessage(types(v), LB_GETCOUNT, 0, 0);
                while (at + 1 < n && SendMessage(types(v), LB_GETTEXTLEN, at + 1, 0) < PATHMAX) {
                    SendMessage(types(v), LB_GETTEXT, at + 1, (LPARAM)(LPSTR)t);
                    if (lstrcmp(t, val))
                        break;
                    at++;
                }
            }
            SendMessage(idents(v), LB_INSERTSTRING, at, (LPARAM)(LPSTR)name);
        }
        RegCloseKey(root);
    }
    if (sel < 0)
        sel = 0;
    if (sel >= (int)SendMessage(types(v), LB_GETCOUNT, 0, 0))
        sel = (int)SendMessage(types(v), LB_GETCOUNT, 0, 0) - 1;
    SendMessage(types(v), LB_SETCURSEL, sel, 0);
    SendMessage(types(v), WM_SETREDRAW, TRUE, 0);
    InvalidateRect(types(v), NULL, TRUE);
}

/* the selected entry's identifier ("" for none) */
static void selected(HWND v, LPSTR id)
{
    int s = (int)SendMessage(types(v), LB_GETCURSEL, 0, 0);
    id[0] = 0;
    if (s >= 0 && SendMessage(idents(v), LB_GETTEXTLEN, s, 0) < PATHMAX)
        SendMessage(idents(v), LB_GETTEXT, s, (LPARAM)id);
}

/* select the entry of identifier id (any case) */
static void select_ident(HWND v, LPCSTR id)
{
    static char s[PATHMAX];
    int n = (int)SendMessage(idents(v), LB_GETCOUNT, 0, 0), i;
    for (i = 0; i < n; i++) {
        if (SendMessage(idents(v), LB_GETTEXTLEN, i, 0) >= PATHMAX)
            continue;
        SendMessage(idents(v), LB_GETTEXT, i, (LPARAM)(LPSTR)s);
        if (!lstrcmpi(s, id)) {
            SendMessage(types(v), LB_SETCURSEL, i, 0);
            return;
        }
    }
}

/* ---- the File Type dialog ---- */


/* a value below key k ("" if none) */
static void qv(HKEY k, LPCSTR sub, LPSTR out, LONG cb)
{
    out[0] = 0;
    if (RegQueryValue(k, (LPSTR)sub, out, &cb))
        out[0] = 0;
}

/* the type's actions, read as 3.1 reads them (RE17): id opened, then
   shell, shell\open and shell\print with RegCreateKey, which makes them
   when missing (so opening Modify or Copy on a type adds them) */
static void load_actions(LPCSTR id)
{
    HKEY k, s, a, d;
    int i;
    _fmemset(act, 0, sizeof act);
    if (!id) {
        act[0].dde = act[1].dde = TRUE;
        return;
    }
    if (RegOpenKey(HKEY_CLASSES_ROOT, (LPSTR)id, &k))
        return;
    if (!RegCreateKey(k, "shell", &s)) {
        for (i = 0; i < 2; i++) {
            ACTION *x = &act[i];
            if (RegCreateKey(s, (LPSTR)verbs[i], &a))
                continue;
            qv(a, "command", x->cmd, sizeof x->cmd);
            if (!RegOpenKey(a, "ddeexec", &d)) {
                RegCloseKey(d);
                x->dde = TRUE;
                qv(a, "ddeexec", x->msg, sizeof x->msg);
                qv(a, "ddeexec\\ifexec", x->notrun, sizeof x->notrun);
                qv(a, "ddeexec\\application", x->app, sizeof x->app);
                qv(a, "ddeexec\\topic", x->topic, sizeof x->topic);
                /* UNVERIFIED: missing ones shown as the defaults */
                if (!x->app[0])
                    lstrcpy(x->app, default_app(x->cmd));
                if (!x->topic[0])
                    lstrcpy(x->topic, "System");
            }
            RegCloseKey(a);
        }
        RegCloseKey(s);
    }
    RegCloseKey(k);
}

static void enable_dde(HWND d)
{
    static const int ids[] = { IDC_DDEGROUP, IDC_DDEMSG, IDC_DDEAPP, IDC_DDETOPIC };
    BOOL on = IsDlgButtonChecked(d, IDC_USESDDE);
    BOOL nr = on && GetWindowTextLength(GetDlgItem(d, IDC_DDEMSG)) > 0;
    HWND c;
    int i;
    for (i = 0; i < 4; i++)
        EnableWindow(GetDlgItem(d, ids[i]), on);
    /* the statics before each edit */
    for (c = GetWindow(d, GW_CHILD); c; c = GetWindow(c, GW_HWNDNEXT)) {
        HWND nx = GetWindow(c, GW_HWNDNEXT);
        if (GetDlgCtrlID(c) != -1 || !nx)
            continue;
        switch (GetDlgCtrlID(nx)) {
        case IDC_DDEMSG: case IDC_DDEAPP: case IDC_DDETOPIC:
            EnableWindow(c, on); break;
        case IDC_DDENOTRUN:
            EnableWindow(c, nr); break;
        }
    }
    EnableWindow(GetDlgItem(d, IDC_DDENOTRUN), nr);
}

static void show_action(HWND d, int a)
{
    cur = a;
    CheckRadioButton(d, IDC_OPEN, IDC_PRINT, a ? IDC_PRINT : IDC_OPEN);
    SetDlgItemText(d, IDC_COMMAND, act[a].cmd);
    CheckDlgButton(d, IDC_USESDDE, act[a].dde);
    SetDlgItemText(d, IDC_DDEMSG, act[a].msg);
    SetDlgItemText(d, IDC_DDENOTRUN, act[a].notrun);
    SetDlgItemText(d, IDC_DDEAPP, act[a].app);
    SetDlgItemText(d, IDC_DDETOPIC, act[a].topic);
    enable_dde(d);
}

static void keep_action(HWND d)
{
    ACTION *p = &act[cur];
    GetDlgItemText(d, IDC_COMMAND, p->cmd, sizeof p->cmd);
    p->dde = IsDlgButtonChecked(d, IDC_USESDDE);
    GetDlgItemText(d, IDC_DDEMSG, p->msg, sizeof p->msg);
    GetDlgItemText(d, IDC_DDENOTRUN, p->notrun, sizeof p->notrun);
    GetDlgItemText(d, IDC_DDEAPP, p->app, sizeof p->app);
    GetDlgItemText(d, IDC_DDETOPIC, p->topic, sizeof p->topic);
}

/* the DDE application a command implies: its program's name, the first
   letter capital, the rest small ("pbrush.exe %1": "Pbrush"); "" none */
static LPSTR default_app(LPCSTR cmd)
{
    static char c[PATHMAX];
    LPSTR s, e, b;
    lstrcpy(c, cmd);
    for (s = c; *s == ' '; s++)
        ;
    for (e = s; *e && *e != ' '; e++)
        ;
    *e = 0;
    for (b = s, e = s; *e; e = AnsiNext(e))
        if (*e == '\\' || *e == ':')
            b = e + 1;
    for (e = b; *e && *e != '.'; e++)
        ;
    *e = 0;
    if (b[0]) {
        AnsiLower(b);
        b[0] = (char)LOWORD((DWORD)AnsiUpper((LPSTR)MAKELONG((BYTE)b[0], 0)));
    }
    return b;
}

/* Uses DDE checked: the program's name and "System" as defaults */
static void dde_defaults(HWND d)
{
    static char c[PATHMAX];
    LPSTR b;
    GetDlgItemText(d, IDC_COMMAND, c, sizeof c);
    b = default_app(c);
    if (!b[0])
        return;
    if (!GetWindowTextLength(GetDlgItem(d, IDC_DDEAPP)))
        SetDlgItemText(d, IDC_DDEAPP, b);
    if (!GetWindowTextLength(GetDlgItem(d, IDC_DDETOPIC)))
        SetDlgItemText(d, IDC_DDETOPIC, "System");
}

static BOOL valid_ident(LPCSTR s)
{
    int n = 0;
    for (; *s; s++, n++)
        if ((BYTE)*s <= 0x20 || (BYTE)*s >= 0x80 || *s == '\\')
            return FALSE;
    return n > 0 && n <= 63;
}

/* a value set, or the key deleted when it is empty */
static void set_or_delete(HKEY k, LPCSTR sub, LPCSTR v)
{
    if (v[0])
        RegSetValue(k, (LPSTR)sub, REG_SZ, (LPSTR)v, 0);
    else
        RegDeleteKey(k, (LPSTR)sub);
}

/* the type written in 3.1's order of calls (RE17): a new identifier made
   first (RegCreateKey, closed), then opened; shell made; the file type
   set; for open, then print: the verb made, its command set (deleted when
   empty), and with Uses DDE and a DDE Message ddeexec, ifexec,
   application and topic (an empty one deleted), else those four deleted */
static void write_type(LPCSTR id, BOOL is_new)
{
    HKEY k, s, a;
    int i;
    if (is_new) {
        if (RegCreateKey(HKEY_CLASSES_ROOT, (LPSTR)id, &k))
            return;
        RegCloseKey(k);
    }
    if (RegOpenKey(HKEY_CLASSES_ROOT, (LPSTR)id, &k))
        return;
    if (!RegCreateKey(k, "shell", &s)) {
        RegSetValue(k, "", REG_SZ, ftype, 0);
        for (i = 0; i < 2; i++) {
            ACTION *x = &act[i];
            if (RegCreateKey(s, (LPSTR)verbs[i], &a))
                continue;
            set_or_delete(a, "command", x->cmd);
            if (x->dde && x->msg[0]) {
                RegSetValue(a, "ddeexec", REG_SZ, x->msg, 0);
                set_or_delete(a, "ddeexec\\ifexec", x->notrun);
                /* the defaults are not written: an application named as
                   the command's program (any case) and the topic System
                   are deleted (RE17: "NOTEPAD" for notepad.exe) */
                set_or_delete(a, "ddeexec\\application",
                              lstrcmpi(x->app, default_app(x->cmd)) ? x->app : "");
                set_or_delete(a, "ddeexec\\topic", lstrcmpi(x->topic, "System") ? x->topic : "");
            } else {
                RegDeleteKey(a, "ddeexec");
                RegDeleteKey(a, "ddeexec\\ifexec");
                RegDeleteKey(a, "ddeexec\\application");
                RegDeleteKey(a, "ddeexec\\topic");
            }
            RegCloseKey(a);
        }
        RegCloseKey(s);
    }
    RegCloseKey(k);
}

BOOL FAR PASCAL _export TypeDlgProc(HWND d, UINT msg, WPARAM wp, LPARAM lp)
{
    switch (msg) {
    case WM_INITDIALOG: {
        char t[64];
        if (dlg_kind == IDM_COPY) {
            rstr(IDS_COPY, t, sizeof t);
            SetWindowText(d, t);
        }
        SetDlgItemText(d, IDC_IDENT, dlg_kind == IDM_MODIFY ? ident : "");
        SetDlgItemText(d, IDC_FILETYPE, dlg_kind == IDM_ADD ? "" : ftype);
        show_action(d, 0);
        /* the first edit gets the focus, its caret at the start, nothing
           selected (RE3) */
        SetFocus(GetDlgItem(d, dlg_kind == IDM_MODIFY ? IDC_FILETYPE : IDC_IDENT));
        return FALSE;
    }
    case WM_COMMAND:
        switch (wp) {
        case IDC_OPEN:
        case IDC_PRINT:
            if (HIWORD(lp) == BN_CLICKED && cur != (wp == IDC_PRINT)) {
                keep_action(d);
                show_action(d, wp == IDC_PRINT);
            }
            return TRUE;
        case IDC_USESDDE:
            if (HIWORD(lp) == BN_CLICKED) {
                CheckDlgButton(d, IDC_USESDDE, !IsDlgButtonChecked(d, IDC_USESDDE));
                if (IsDlgButtonChecked(d, IDC_USESDDE))
                    dde_defaults(d);
                enable_dde(d);
            }
            return TRUE;
        case IDC_DDEMSG:
            if (HIWORD(lp) == EN_CHANGE)
                enable_dde(d);
            return TRUE;
        case IDC_BROWSE: {
            static char f[PATHMAX];
            lstrcpy(f, "*.com;*.exe;*.bat;*.pif");
            if (file_dialog(d, FALSE, IDS_BROWSETITLE, IDS_PROGFILTER, NULL, f, sizeof f))
                SetDlgItemText(d, IDC_COMMAND, f);
            return TRUE;
        }
        case IDC_HELP:
            help(HELP_CONTEXT, dlg_kind == IDM_MODIFY ? 0 : 0);
            return TRUE;
        case IDOK: {
            static char id[PATHMAX];
            keep_action(d);
            GetDlgItemText(d, IDC_FILETYPE, ftype, sizeof ftype);
            if (dlg_kind == IDM_MODIFY)
                lstrcpy(id, ident);
            else
                GetDlgItemText(d, IDC_IDENT, id, sizeof id);
            if (!ftype[0]) {
                msgbox(d, IDS_NOFILETYPE, NULL, MB_OK);
                SetFocus(GetDlgItem(d, IDC_FILETYPE));
                return TRUE;
            }
            if (!valid_ident(id)) {
                msgbox(d, IDS_NOIDENT, NULL, MB_OK);
                SetFocus(GetDlgItem(d, IDC_IDENT));
                return TRUE;
            }
            if (dlg_kind != IDM_MODIFY && reg_exists(id)) {
                msgbox(d, IDS_IDEXISTS, id, MB_OK);
                SetFocus(GetDlgItem(d, IDC_IDENT));
                return TRUE;
            }
            lstrcpy(ident, id);
            write_type(id, dlg_kind != IDM_MODIFY);
            EndDialog(d, IDOK);
            return TRUE;
        }
        case IDCANCEL:
            EndDialog(d, IDCANCEL);
            return TRUE;
        }
        break;
    }
    return FALSE;
}

void basic_command(HWND v, UINT id)
{
    static char f[PATHMAX];
    FARPROC fp;
    switch (id) {
    case IDM_ADD:
    case IDM_COPY:
    case IDM_MODIFY:
        selected(v, ident);
        if (id != IDM_ADD && !ident[0] && SendMessage(types(v), LB_GETCOUNT, 0, 0) == 0)
            return;
        dlg_kind = id;
        if (id == IDM_ADD) {
            load_actions(NULL);
        } else {
            reg_get(ident, ftype, sizeof ftype);
            load_actions(ident);
        }
        fp = MakeProcInstance((FARPROC)TypeDlgProc, g_inst);
        if (DialogBox(g_inst, MAKEINTRESOURCE(id == IDM_MODIFY ? DLG_MODIFY : DLG_FILETYPE),
                      g_frame, (DLGPROC)fp) == IDOK) {
            basic_refresh(v);
            /* the type selected by its name, the first of equal names, scrolled
               to from the top (RE5) */
            SendMessage(types(v), LB_SETTOPINDEX, 0, 0);
            SendMessage(types(v), LB_SETCURSEL,
                        (WPARAM)SendMessage(types(v), LB_FINDSTRINGEXACT, (WPARAM)-1, (LPARAM)(LPSTR)ftype), 0);
        }
        FreeProcInstance(fp);
        SetFocus(types(v));
        return;
    case IDM_DELETE: {
        static char name[PATHMAX];
        int s = (int)SendMessage(types(v), LB_GETCURSEL, 0, 0);
        if (s < 0)
            return;
        selected(v, ident);
        SendMessage(types(v), LB_GETTEXT, s, (LPARAM)(LPSTR)name);
        if (msgbox(g_frame, IDS_DELCONFIRM, name, MB_YESNO | MB_ICONEXCLAMATION) != IDYES)
            return;
        reg_delete(ident);
        basic_refresh(v);
        return;
    }
    case IDM_MERGE:
        lstrcpy(f, "*.reg");
        if (file_dialog(g_frame, FALSE, IDS_MERGETITLE, IDS_REGFILTER, "reg", f, sizeof f)) {
            switch (merge_file(f, NULL)) {
            case MERGE_OK:
                msgbox(g_frame, IDS_MERGED, f, MB_OK | MB_ICONEXCLAMATION);
                break;
            case MERGE_NOTREG:
                msgbox(g_frame, IDS_NOTREG, NULL, MB_OK | MB_ICONEXCLAMATION);
                break;
            default:
                msgbox(g_frame, IDS_CANTOPEN, f, MB_OK | MB_ICONEXCLAMATION);
            }
        }
        /* read again, the first type selected, also after Cancel (RE3) */
        SendMessage(types(v), LB_SETCURSEL, 0, 0);
        basic_refresh(v);
        SendMessage(types(v), LB_SETCURSEL, 0, 0);
        return;
    }
}

/* the list fills the view below its label: one pixel past each side and
   the bottom, its height whole lines (RE1, RE10) */
static int list_top;

static void place_list(HWND v, int cx, int cy)
{
    MoveWindow(types(v), -1, list_top, cx + 2, cy + 1 - list_top, TRUE);
}

BOOL FAR PASCAL _export BasicDlgProc(HWND v, UINT msg, WPARAM wp, LPARAM lp)
{
    switch (msg) {
    case WM_INITDIALOG: {
        RECT r;
        GetWindowRect(types(v), &r);
        ScreenToClient(v, (POINT FAR *)&r);
        list_top = 25;      /* measured (RE1): 2 below the label; not a whole dialog unit */
        /* the hidden list of identifiers, off the window (RE1) */
        CreateWindow("ListBox", "", WS_CHILD | WS_VISIBLE | WS_BORDER | LBS_NOTIFY,
                     -175, -163, 20, 15, v, (HMENU)IDC_IDENTS, g_inst, NULL);
        basic_refresh(v);
        if (g_sel[0]) {
            static char s[PATHMAX];
            LPSTR p;
            lstrcpy(s, g_sel);
            for (p = s; *p && *p != '\\'; p++)
                ;
            *p = 0;
            select_ident(v, s);
        }
        return TRUE;
    }
    case WM_SIZE:
        place_list(v, LOWORD(lp), HIWORD(lp));
        return TRUE;
    case WM_COMMAND:
        if (wp == IDC_TYPES && HIWORD(lp) == LBN_DBLCLK)
            basic_command(v, IDM_MODIFY);
        else if (wp == IDOK)
            basic_command(v, IDM_MODIFY);
        else if (wp == IDC_TYPES && HIWORD(lp) == LBN_SELCHANGE)
            selected(v, g_sel);
        return TRUE;
    case WM_DESTROY:
        selected(v, g_sel);
        return FALSE;
    }
    return FALSE;
}
