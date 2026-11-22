/*
 * search.c - w16 Windows Help: the Search dialog and keyword jumps.
 *
 * Observed on 3.1 (WH2, NOTEPAD.HLP): "Search", a modal dialog over the
 * main window; the keywords (|KWBTREE, in its order) in a list with the
 * first selected; typing selects the first keyword that starts with what
 * was typed and makes Show Topics the default button; Show Topics fills
 * the lower list with the keyword's topics' titles (|KWDATA's offsets,
 * the titles from the topics' headers) and enables Go To; Go To (or a
 * double click) shows the selected topic and closes the dialog.
 * A WinHelp HELP_KEY whose keyword has one topic shows it at once;
 * otherwise (several topics, or no such keyword) the dialog comes up
 * with the key typed in (w16: UNVERIFIED which of these 3.1 does for an
 * unknown keyword).
 */
#include "winhelp.h"

static char initial[80];
static HLP FAR *sh;             /* the file being searched */
static DWORD fkw, fkd;          /* the keyword table: |KWBTREE, |KWDATA */
static DWORD topics[64];
static int ntopics;

static BOOL fill_kw(void FAR *c, const char FAR *kw, WORD count, DWORD dataoff)
{
    HWND lb = (HWND)LOWORD((DWORD)c);
    (void)count;
    (void)dataoff;
    SendMessage(lb, LB_ADDSTRING, 0, (LPARAM)kw);
    return TRUE;
}

static void use_table(BOOL on, DWORD bt, DWORD dt, DWORD *sb, DWORD *sd)
{
    if (on) {
        *sb = sh->f_kwbtree;
        *sd = sh->f_kwdata;
        sh->f_kwbtree = bt;
        sh->f_kwdata = dt;
    } else {
        sh->f_kwbtree = *sb;
        sh->f_kwdata = *sd;
    }
}

static int kw_topics(LPCSTR kw, BOOL partial)
{
    WORD count, i;
    DWORD dataoff, sb, sd;
    BOOL found;
    use_table(TRUE, fkw, fkd, &sb, &sd);
    found = hlp_kwfind(sh, kw, partial, &count, &dataoff);
    ntopics = 0;
    if (found)
        for (i = 0; i < count && ntopics < 64; i++)
            if (hlp_kwtopic(sh, dataoff, i, &topics[ntopics]))
                ntopics++;
    use_table(FALSE, 0, 0, &sb, &sd);
    return found ? ntopics : -1;
}

static void show_topics(HWND d)
{
    char kw[80];
    HWND lb = GetDlgItem(d, IDC_KEYWORDS), tl = GetDlgItem(d, IDC_TOPICS);
    int i = (int)SendMessage(lb, LB_GETCURSEL, 0, 0), k;
    if (i < 0)
        return;
    SendMessage(lb, LB_GETTEXT, i, (LPARAM)(LPSTR)kw);
    SetDlgItemText(d, IDC_SEARCHEDIT, kw);
    kw_topics(kw, FALSE);
    SendMessage(tl, LB_RESETCONTENT, 0, 0);
    for (k = 0; k < ntopics; k++) {
        char t[128];
        int ti = hlp_topic_at(sh, topics[k]);
        t[0] = 0;
        hlp_title(sh, ti, t, sizeof t);
        SendMessage(tl, LB_INSERTSTRING, (WPARAM)-1, (LPARAM)(LPSTR)t);
        SendMessage(tl, LB_SETITEMDATA, k, k);
    }
    SendMessage(tl, LB_SETCURSEL, 0, 0);
    EnableWindow(tl, ntopics > 0);
    /* Cancel becomes Close once topics are shown (WS01) */
    SetDlgItemText(d, IDCANCEL, "Close");
    EnableWindow(GetDlgItem(d, IDC_GOTO), ntopics > 0);
    if (ntopics) {
        SendMessage(GetDlgItem(d, IDC_SHOW), BM_SETSTYLE, BS_PUSHBUTTON, TRUE);
        SendMessage(GetDlgItem(d, IDC_GOTO), BM_SETSTYLE, BS_DEFPUSHBUTTON, TRUE);
        SendMessage(d, DM_SETDEFID, IDC_GOTO, 0);
        /* the focus goes to the topics (WS01) */
        SetFocus(tl);
    }
}

static void edit_changed(HWND d)
{
    char t[80];
    HWND lb = GetDlgItem(d, IDC_KEYWORDS);
    int i;
    int n, k;
    GetDlgItemText(d, IDC_SEARCHEDIT, t, sizeof t);
    if (!t[0])
        return;
    /* the first keyword that starts with the text; when none does, the
       last one before it (WH2: "find" selects "files, including time and
       date", the item above shown at the top) */
    i = (int)SendMessage(lb, LB_FINDSTRING, (WPARAM)-1, (LPARAM)(LPSTR)t);
    if (i < 0) {
        char kw[80];
        n = (int)SendMessage(lb, LB_GETCOUNT, 0, 0);
        for (k = 0; k < n; k++) {
            SendMessage(lb, LB_GETTEXT, k, (LPARAM)(LPSTR)kw);
            if (lstrcmpi(kw, t) > 0)
                break;
        }
        i = k > 0 ? k - 1 : (n ? 0 : -1);
    }
    if (i >= 0) {
        SendMessage(lb, LB_SETCURSEL, i, 0);
        SendMessage(lb, LB_SETTOPINDEX, i > 0 ? i - 1 : 0, 0);
    }
    EnableWindow(GetDlgItem(d, IDC_SHOW), i >= 0);
    if (i >= 0) {
        SendMessage(GetDlgItem(d, IDC_SHOW), BM_SETSTYLE, BS_DEFPUSHBUTTON, TRUE);
        SendMessage(d, DM_SETDEFID, IDC_SHOW, 0);
    }
}

BOOL CALLBACK __export SearchProc(HWND d, UINT msg, WPARAM wp, LPARAM lp)
{
    switch (msg) {
    case WM_INITDIALOG: {
        HWND lb = GetDlgItem(d, IDC_KEYWORDS);
        DWORD sb, sd;
        /* the lists and the edit where 3.1 has them, in pixels (WH2; not
           whole dialog units) */
        MoveWindow(GetDlgItem(d, IDC_SEARCHEDIT), 7, 46, 259, 23, FALSE);
        MoveWindow(lb, 25, 68, 357, 81, FALSE);
        MoveWindow(GetDlgItem(d, IDC_TOPICS), 6, 201, 377, 93, FALSE);
        use_table(TRUE, fkw, fkd, &sb, &sd);
        hlp_keywords(sh, fill_kw, (void FAR *)MAKELONG(lb, 0));
        use_table(FALSE, 0, 0, &sb, &sd);
        SendMessage(lb, LB_SETCURSEL, 0, 0);
        SendDlgItemMessage(d, IDC_SEARCHEDIT, EM_LIMITTEXT, 79, 0);
        if (initial[0]) {
            SetDlgItemText(d, IDC_SEARCHEDIT, initial);
            edit_changed(d);
            if (kw_topics(initial, FALSE) >= 0)
                show_topics(d);
        }
        SetFocus(GetDlgItem(d, IDC_SEARCHEDIT));
        if (!IsWindowVisible(g_frame) && g_owner && IsWindow(g_owner)) {
            /* the Help window hidden: over the application's window,
               its dialog window at 33, 5 on VGA (WS05, the Registration Info Editor's
               Search for Help On; UNVERIFIED for other places) */
            RECT r;
            HWND o = g_owner;
            while (GetParent(o))
                o = GetParent(o);
            GetWindowRect(o, &r);
            SetWindowPos(d, NULL, r.left + 32, r.top + 4, 0, 0, SWP_NOSIZE | SWP_NOZORDER);
        }
        return FALSE;
    }
    case WM_COMMAND:
        switch (wp) {
        case IDCANCEL:
            EndDialog(d, -1);
            return TRUE;
        case IDOK:
            /* Enter: the default button */
            if (IsWindowEnabled(GetDlgItem(d, IDC_GOTO)) &&
                LOWORD(SendMessage(d, DM_GETDEFID, 0, 0)) == IDC_GOTO)
                PostMessage(d, WM_COMMAND, IDC_GOTO, 0);
            else if (IsWindowEnabled(GetDlgItem(d, IDC_SHOW)))
                show_topics(d);
            return TRUE;
        case IDC_SEARCHEDIT:
            if (HIWORD(lp) == EN_CHANGE)
                edit_changed(d);
            return TRUE;
        case IDC_KEYWORDS:
            if (HIWORD(lp) == LBN_SELCHANGE) {
                char kw[80];
                int i = (int)SendDlgItemMessage(d, IDC_KEYWORDS, LB_GETCURSEL, 0, 0);
                if (i >= 0) {
                    SendDlgItemMessage(d, IDC_KEYWORDS, LB_GETTEXT, i, (LPARAM)(LPSTR)kw);
                    SetDlgItemText(d, IDC_SEARCHEDIT, kw);
                }
            } else if (HIWORD(lp) == LBN_DBLCLK) {
                show_topics(d);
            }
            return TRUE;
        case IDC_SHOW:
            show_topics(d);
            return TRUE;
        case IDC_TOPICS:
            if (HIWORD(lp) != LBN_DBLCLK)
                return TRUE;
            /* fall through */
        case IDC_GOTO: {
            int i = (int)SendDlgItemMessage(d, IDC_TOPICS, LB_GETCURSEL, 0, 0);
            if (i >= 0 && i < ntopics)
                EndDialog(d, i);
            return TRUE;
        }
        }
        break;
    }
    return FALSE;
}

static void run_dialog(void)
{
    FARPROC p;
    int r;
    p = MakeProcInstance((FARPROC)SearchProc, g_inst);
    r = DialogBox(g_inst, MAKEINTRESOURCE(DLG_SEARCH), g_frame, (DLGPROC)p);
    FreeProcInstance(p);
    if (r >= 0 && r < ntopics) {
        if (sh != g_hlp)
            open_file(g_file, TRUE);
        show_main();
        show_offset(topics[r], TRUE, 0);
    }
}

void search_dialog(LPCSTR init)
{
    if (!g_hlp)
        return;
    sh = g_hlp;
    fkw = g_hlp->f_kwbtree;
    fkd = g_hlp->f_kwdata;
    if (!fkw)
        return;
    hlp_strcpyn(initial, init ? init : "", sizeof initial);
    run_dialog();
}

static BOOL jump_with(DWORD bt, DWORD dt, LPCSTR key, BOOL partial)
{
    int n;
    sh = g_hlp;
    fkw = bt;
    fkd = dt;
    if (!g_hlp || !bt)
        return FALSE;
    n = kw_topics(key, partial);
    if (n == 1) {
        show_main();
        return show_offset(topics[0], TRUE, 0);
    }
    hlp_strcpyn(initial, key, sizeof initial);
    run_dialog();
    return n > 0;
}

BOOL key_jump(LPCSTR key, BOOL partial)
{
    if (!g_hlp)
        return FALSE;
    return jump_with(g_hlp->f_kwbtree, g_hlp->f_kwdata, key, partial);
}

/* HELP_MULTIKEY: the keyword table of footnote letter k (|kKWBTREE,
   |kKWDATA) */
BOOL multikey_jump(char k, LPCSTR key)
{
    char n1[16], n2[16];
    DWORD bt, dt;
    if (!g_hlp)
        return FALSE;
    wsprintf(n1, "|%cKWBTREE", k);
    wsprintf(n2, "|%cKWDATA", k);
    bt = hlp_file(g_hlp, n1);
    dt = hlp_file(g_hlp, n2);
    if (!bt || !dt) {
        errorbox(IDS_NOKEYWORD, NULL);
        return FALSE;
    }
    return jump_with(bt, dt, key, FALSE);
}
