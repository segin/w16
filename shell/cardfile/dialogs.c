/*
 * dialogs.c - w16 Cardfile: Index, Add, Go To, Find, Autodial, the file
 * dialogs and About.
 *
 * The index field (40 bytes in the file) is written the way the original
 * writes it (CF4, CF5): Index puts the line and its NUL at the start of
 * the card's own field and leaves the rest; Add starts the new card's
 * field as a copy of the front card's and does the same. The line is at
 * most 40 characters (EM_LIMITTEXT) and must fit the edit box, which
 * does not scroll (39 digits, 40 i's, 23 W's; CF3, CF4).
 */
#include "cardfile.h"

UINT g_findmsg;
HWND g_finddlg;

static char find_text[256];             /* what Find looks for */
static BOOL find_case, find_up;

/* ---- Index, Add, Go To ---- */

static int dlg_kind;                    /* IDD_INDEX, IDD_ADD, IDD_GOTO */
static char dlg_buf[INDEX_BYTES + 1];   /* the field being edited */
static char goto_text[INDEX_BYTES + 1];

BOOL CALLBACK __export DlgProc(HWND d, UINT msg, WPARAM wp, LPARAM lp)
{
    (void)lp;
    switch (msg) {
    case WM_INITDIALOG:
        SendDlgItemMessage(d, IDC_EDIT, EM_LIMITTEXT, INDEX_BYTES, 0);
        if (dlg_kind == IDD_INDEX)
            SetDlgItemText(d, IDC_EDIT, dlg_buf);
        return TRUE;
    case WM_COMMAND:
        if (wp == IDOK) {
            if (dlg_kind == IDD_GOTO)
                GetDlgItemText(d, IDC_EDIT, goto_text, sizeof goto_text);
            else
                GetDlgItemText(d, IDC_EDIT, dlg_buf, INDEX_BYTES + 1);
            EndDialog(d, TRUE);
            return TRUE;
        }
        if (wp == IDCANCEL) {
            EndDialog(d, FALSE);
            return TRUE;
        }
        break;
    }
    return FALSE;
}

static BOOL run_dlg(int id)
{
    FARPROC p = MakeProcInstance((FARPROC)DlgProc, g_hinst);
    BOOL r;
    dlg_kind = id;
    r = DialogBox(g_hinst, MAKEINTRESOURCE(id), g_frame, (DLGPROC)p);
    FreeProcInstance(p);
    return r == TRUE;
}

BOOL dlg_index(void)
{
    LPCARD c = card_hdr(g_cur);
    mem_cpy(dlg_buf, c->index, INDEX_BYTES);
    dlg_buf[INDEX_BYTES] = 0;
    if (!run_dlg(IDD_INDEX))
        return FALSE;
    c = card_hdr(g_cur);
    mem_cpy(c->index, dlg_buf, INDEX_BYTES);
    g_cur = card_resort(g_cur);
    g_dirty = TRUE;
    card_changed();
    return TRUE;
}

BOOL dlg_add(void)
{
    WORD at;
    mem_cpy(dlg_buf, card_hdr(g_cur)->index, INDEX_BYTES);
    dlg_buf[INDEX_BYTES] = 0;
    if (!run_dlg(IDD_ADD))
        return FALSE;
    at = card_insert(dlg_buf);
    if (at == 0xFFFF) {
        msg_box(IDS_NOMEM, NULL, MB_OK | MB_ICONEXCLAMATION);
        return FALSE;
    }
    undo_forget();
    pic_free(&g_pic);
    g_cur = at;
    body_load(at);
    g_dirty = TRUE;
    card_changed();
    if (!g_listview)
        SetFocus(g_edit);
    return TRUE;
}

/* does the index line contain s (ignoring case)? */
static BOOL contains(LPCSTR line, LPCSTR s)
{
    char a[INDEX_BYTES + 1], b[INDEX_BYTES + 1];
    int n, i;
    s_cpy(a, line, sizeof a);
    s_cpy(b, s, sizeof b);
    AnsiUpper(a);
    AnsiUpper(b);
    n = s_len(b);
    for (i = 0; a[i]; i++)
        if (!s_cmpn(a + i, b, n))
            return TRUE;
    return n == 0;
}

/* Go To: the next card (after the front one, wrapping) whose index line
   contains the text. UNVERIFIED: substring or prefix. */
void dlg_goto(void)
{
    WORD k;
    if (!run_dlg(IDD_GOTO))
        return;
    for (k = 1; k <= g_ncards; k++) {
        WORD i = (WORD)(((DWORD)g_cur + k) % g_ncards);
        if (contains(card_hdr(i)->index, goto_text)) {
            if (i != g_cur)
                go_card(i);
            return;
        }
    }
    msg_box(IDS_NOTFOUND, goto_text, MB_OK | MB_ICONEXCLAMATION);
}

/* ---- Find ---- */

/* Search card texts from the caret on, then the following cards. */
static BOOL search_text(LPCSTR text, WORD start, WORD *at)
{
    int n = s_len(find_text), len = s_len(text);
    int i;
    if (!n)
        return FALSE;
    for (i = start; i + n <= len; i++) {
        BOOL eq = TRUE;
        int k;
        for (k = 0; k < n && eq; k++) {
            char a[2], b[2];
            a[0] = text[i + k];
            b[0] = find_text[k];
            a[1] = b[1] = 0;
            if (!find_case) {
                AnsiUpper(a);
                AnsiUpper(b);
            }
            eq = a[0] == b[0];
        }
        if (eq) {
            *at = (WORD)i;
            return TRUE;
        }
    }
    return FALSE;
}

static BOOL search_text_up(LPCSTR text, int before, WORD *at)
{
    int n = s_len(find_text), i;
    for (i = before - n; i >= 0; i--) {
        WORD w;
        if (search_text(text + i, 0, &w) && w == 0) {
            *at = (WORD)i;
            return TRUE;
        }
    }
    return FALSE;
}

void find_next(void)
{
    HLOCAL h;
    LPSTR t;
    WORD at, k, n;
    DWORD sel;
    if (!find_text[0]) {
        dlg_find();
        return;
    }
    if (g_listview)
        return;
    n = GetWindowTextLength(g_edit);
    h = LocalAlloc(LMEM_MOVEABLE, n + 1);
    if (!h)
        return;
    t = LocalLock(h);
    GetWindowText(g_edit, t, n + 1);
    sel = SendMessage(g_edit, EM_GETSEL, 0, 0);
    if (find_up ? search_text_up(t, LOWORD(sel), &at) : search_text(t, HIWORD(sel), &at)) {
        SendMessage(g_edit, EM_SETSEL, 0, MAKELPARAM(at, at + s_len(find_text)));
        LocalUnlock(h);
        LocalFree(h);
        return;
    }
    LocalUnlock(h);
    LocalFree(h);
    /* the other cards, in order, back to this one */
    for (k = 1; k <= g_ncards; k++) {
        WORD i = find_up ? (WORD)(((DWORD)g_cur + g_ncards - k) % g_ncards)
                         : (WORD)(((DWORD)g_cur + k) % g_ncards);
        go_card(i);
        n = GetWindowTextLength(g_edit);
        h = LocalAlloc(LMEM_MOVEABLE, n + 1);
        if (!h)
            return;
        t = LocalLock(h);
        GetWindowText(g_edit, t, n + 1);
        if (find_up ? search_text_up(t, n, &at) : search_text(t, 0, &at)) {
            SendMessage(g_edit, EM_SETSEL, 0, MAKELPARAM(at, at + s_len(find_text)));
            LocalUnlock(h);
            LocalFree(h);
            return;
        }
        LocalUnlock(h);
        LocalFree(h);
    }
    msg_box(IDS_NOTFOUND, find_text, MB_OK | MB_ICONEXCLAMATION);
}

#ifndef W16_WIN2
static FINDREPLACE fr;

void dlg_find(void)
{
    if (g_finddlg) {
        SetFocus(g_finddlg);
        return;
    }
    mem_set(&fr, 0, sizeof fr);
    fr.lStructSize = sizeof fr;
    fr.hwndOwner = g_frame;
    fr.hInstance = g_hinst;
    fr.Flags = FR_ENABLETEMPLATE | FR_HIDEWHOLEWORD | FR_DOWN |
               (find_case ? FR_MATCHCASE : 0) | (find_up ? 0 : FR_DOWN);
    if (find_up)
        fr.Flags &= ~FR_DOWN;
    fr.lpstrFindWhat = find_text;
    fr.wFindWhatLen = sizeof find_text;
    fr.lpTemplateName = MAKEINTRESOURCE(IDD_FIND2);
    g_finddlg = FindText(&fr);
}

/* COMMDLG's message from the Find box */
BOOL find_message(UINT msg, LPARAM lp)
{
    LPFINDREPLACE f = (LPFINDREPLACE)lp;
    (void)msg;
    if (f->Flags & FR_DIALOGTERM) {
        g_finddlg = NULL;
        return 0;
    }
    find_case = (f->Flags & FR_MATCHCASE) != 0;
    find_up = !(f->Flags & FR_DOWN);
    if (f->Flags & FR_FINDNEXT)
        find_next();
    return 0;
}
#else
BOOL CALLBACK __export Find2DlgProc(HWND d, UINT msg, WPARAM wp, LPARAM lp)
{
    (void)lp;
    switch (msg) {
    case WM_INITDIALOG:
        SetDlgItemText(d, IDC_FINDTEXT, find_text);
        CheckDlgButton(d, IDC_MATCHCASE, find_case);
        CheckRadioButton(d, IDC_FINDUP, IDC_FINDDOWN, find_up ? IDC_FINDUP : IDC_FINDDOWN);
        return TRUE;
    case WM_COMMAND:
        switch (wp) {
        case IDOK:
            GetDlgItemText(d, IDC_FINDTEXT, find_text, sizeof find_text);
            find_case = IsDlgButtonChecked(d, IDC_MATCHCASE);
            find_up = IsDlgButtonChecked(d, IDC_FINDUP);
            find_next();
            return TRUE;
        case IDCANCEL:
            DestroyWindow(d);
            g_finddlg = NULL;
            return TRUE;
        case IDC_MATCHCASE:
            CheckDlgButton(d, wp, !IsDlgButtonChecked(d, wp));
            return TRUE;
        case IDC_FINDUP:
        case IDC_FINDDOWN:
            CheckRadioButton(d, IDC_FINDUP, IDC_FINDDOWN, wp);
            return TRUE;
        }
        break;
    }
    return FALSE;
}

void dlg_find(void)
{
    if (g_finddlg) {
        SetFocus(g_finddlg);
        return;
    }
    g_finddlg = CreateDialog(g_hinst, MAKEINTRESOURCE(IDD_FIND2), g_frame,
                             (DLGPROC)MakeProcInstance((FARPROC)Find2DlgProc, g_hinst));
}

BOOL find_message(UINT msg, LPARAM lp)
{
    (void)msg;
    (void)lp;
    return 0;
}
#endif

/* ---- Autodial ----
   The settings live in WIN.INI [windows] (CF10). UNVERIFIED: that the
   third Modem= field is the baud button's index (only 2, for 1200, was
   seen). */

static char s_section[] = "windows";
static char dial_prefix[32];
static BOOL dial_useprefix, dial_pulse, dial_expanded;
static int dial_port, dial_baud;         /* 0..3, 0..6 */
static const WORD bauds[] = { 110, 300, 1200, 2400, 4800, 9600, 19200 };
static char dial_number[64];

static void dial_load(void)
{
    static BOOL loaded;
    char b[24];
    int i;
    if (loaded)
        return;
    loaded = TRUE;
    /* Modem=COMn,T|P,baud (the index of the baud rate button: 2 is
       1200, the default) */
    GetProfileString(s_section, "Modem", "COM1,T,2", b, sizeof b);
    dial_port = (b[3] >= '1' && b[3] <= '4') ? b[3] - '1' : 0;
    for (i = 0; b[i] && b[i] != ','; i++)
        ;
    dial_pulse = b[i] == ',' && (b[i + 1] == 'P' || b[i + 1] == 'p');
    if (b[i])
        for (i++; b[i] && b[i] != ','; i++)
            ;
    dial_baud = (b[i] == ',' && b[i + 1] >= '0' && b[i + 1] <= '6') ? b[i + 1] - '0' : 2;
    GetProfileString(s_section, "Prefix", "9-", dial_prefix, sizeof dial_prefix);
    dial_useprefix = GetProfileInt(s_section, "UsePrefix", 0) != 0;
}

/* Observed (CF10): after dialing, WIN.INI [windows] gained, in this
   order, Modem=COM1,T,2, Prefix=9- and UsePrefix=0. */
static void dial_save(void)
{
    char b[16];
    s_cpy(b, "COM1,T,2", sizeof b);
    b[3] = (char)('1' + dial_port);
    b[5] = dial_pulse ? 'P' : 'T';
    b[7] = (char)('0' + dial_baud);
    WriteProfileString(s_section, "Modem", b);
    WriteProfileString(s_section, "Prefix", dial_prefix);
    WriteProfileString(s_section, "UsePrefix", dial_useprefix ? "1" : "0");
}

/* the number Autodial offers: the selection, else the first run of
   digits (with - ( ) and blanks inside) on the card */
static void dial_guess(void)
{
    WORD n = GetWindowTextLength(g_edit), i, j = 0;
    DWORD sel = SendMessage(g_edit, EM_GETSEL, 0, 0);
    HLOCAL h = LocalAlloc(LMEM_MOVEABLE, n + 1);
    LPSTR t;
    dial_number[0] = 0;
    if (!h)
        return;
    t = LocalLock(h);
    GetWindowText(g_edit, t, n + 1);
    if (HIWORD(sel) > LOWORD(sel)) {
        for (i = LOWORD(sel); i < HIWORD(sel) && j < sizeof dial_number - 1; i++)
            dial_number[j++] = t[i];
    } else {
        for (i = 0; t[i] && !(t[i] >= '0' && t[i] <= '9'); i++)
            ;
        for (; t[i] && j < sizeof dial_number - 1; i++) {
            char c = t[i];
            if ((c >= '0' && c <= '9') || c == '-' || c == '(' || c == ')' || c == ' ' ||
                c == ',')
                dial_number[j++] = c;
            else
                break;
        }
        while (j && dial_number[j - 1] == ' ')
            j--;
    }
    dial_number[j] = 0;
    LocalUnlock(h);
    LocalFree(h);
}

/* the dialog's height without and with the Setup part (the separator
   statics 308 and 309 mark where each ends) */
static void dial_size(HWND d, BOOL expanded)
{
    RECT r, s;
    HWND sep = GetDlgItem(d, expanded ? IDC_SEP2 : IDC_SEP1);
    int id;
    GetWindowRect(d, &r);
    GetWindowRect(sep, &s);
    SetWindowPos(d, NULL, 0, 0, r.right - r.left, s.bottom - r.top, SWP_NOMOVE | SWP_NOZORDER);
    for (id = IDC_TONE; id <= IDC_PULSE; id++)
        EnableWindow(GetDlgItem(d, id), expanded);
    for (id = IDC_BAUD110; id <= IDC_BAUD110 + 6; id++)
        EnableWindow(GetDlgItem(d, id), expanded);
    for (id = IDC_COM1; id <= IDC_COM1 + 3; id++)
        EnableWindow(GetDlgItem(d, id), expanded);
    if (expanded)
        EnableWindow(GetDlgItem(d, IDC_SETUP), FALSE);
}

/* As observed (CF9): only the checked button of each group keeps
   WS_TABSTOP. */
static void radio(HWND d, int first, int last, int on)
{
    int id;
    CheckRadioButton(d, first, last, on);
    for (id = first; id <= last; id++) {
        HWND b = GetDlgItem(d, id);
        LONG st = GetWindowLong(b, GWL_STYLE);
        SetWindowLong(b, GWL_STYLE, id == on ? st | WS_TABSTOP : st & ~WS_TABSTOP);
    }
}

BOOL CALLBACK __export DialDlgProc(HWND d, UINT msg, WPARAM wp, LPARAM lp)
{
    (void)lp;
    switch (msg) {
    case WM_INITDIALOG:
        SetDlgItemText(d, IDC_DIALNUM, dial_number);
        SetDlgItemText(d, IDC_PREFIX, dial_prefix);
        CheckDlgButton(d, IDC_USEPREFIX, dial_useprefix);
        radio(d, IDC_TONE, IDC_PULSE, dial_pulse ? IDC_PULSE : IDC_TONE);
        radio(d, IDC_COM1, IDC_COM1 + 3, IDC_COM1 + dial_port);
        radio(d, IDC_BAUD110, IDC_BAUD110 + 6, IDC_BAUD110 + dial_baud);
        dial_size(d, dial_expanded);
        return TRUE;
    case WM_COMMAND:
        switch (wp) {
        case IDC_SETUP:
            dial_expanded = TRUE;
            dial_size(d, TRUE);
            return TRUE;
        case IDC_TONE:
        case IDC_PULSE:
            radio(d, IDC_TONE, IDC_PULSE, wp);
            return TRUE;
        case IDC_USEPREFIX:
            CheckDlgButton(d, wp, !IsDlgButtonChecked(d, wp));
            return TRUE;
        case IDOK:
            {
                int i;
                GetDlgItemText(d, IDC_DIALNUM, dial_number, sizeof dial_number);
                GetDlgItemText(d, IDC_PREFIX, dial_prefix, sizeof dial_prefix);
                dial_useprefix = IsDlgButtonChecked(d, IDC_USEPREFIX);
                dial_pulse = IsDlgButtonChecked(d, IDC_PULSE);
                for (i = 0; i < 4; i++)
                    if (IsDlgButtonChecked(d, IDC_COM1 + i))
                        dial_port = i;
                for (i = 0; i < 7; i++)
                    if (IsDlgButtonChecked(d, IDC_BAUD110 + i))
                        dial_baud = i;
                EndDialog(d, TRUE);
            }
            return TRUE;
        case IDCANCEL:
            EndDialog(d, FALSE);
            return TRUE;
        default:
            if (wp >= IDC_COM1 && wp <= IDC_COM1 + 3) {
                radio(d, IDC_COM1, IDC_COM1 + 3, wp);
                return TRUE;
            }
            if (wp >= IDC_BAUD110 && wp <= IDC_BAUD110 + 6) {
                radio(d, IDC_BAUD110, IDC_BAUD110 + 6, wp);
                return TRUE;
            }
        }
        break;
    }
    return FALSE;
}

/* dial through the modem: Hayes commands on the chosen port */
static void dial(void)
{
    char port[6], cmd[128];
    int c;
    DCB dcb;
    s_cpy(port, "COM1", sizeof port);
    port[3] = (char)('1' + dial_port);
    c = OpenComm(port, 256, 256);
    if (c < 0) {
        msg_box(IDS_CANTDIAL, NULL, MB_OK | MB_ICONEXCLAMATION);
        return;
    }
    if (GetCommState(c, &dcb) == 0) {
        dcb.BaudRate = bauds[dial_baud];
        dcb.ByteSize = 8;
        dcb.Parity = NOPARITY;
        dcb.StopBits = ONESTOPBIT;
        SetCommState(&dcb);
    }
    s_cpy(cmd, dial_pulse ? "ATDP" : "ATDT", sizeof cmd);
    if (dial_useprefix)
        s_cat(cmd, dial_prefix, sizeof cmd);
    s_cat(cmd, dial_number, sizeof cmd);
    s_cat(cmd, "\r", sizeof cmd);
    if (WriteComm(c, cmd, s_len(cmd)) < 0) {
        COMSTAT st;
        GetCommError(c, &st);
        CloseComm(c);
        msg_box(IDS_CANTDIAL, NULL, MB_OK | MB_ICONEXCLAMATION);
        return;
    }
    msg_box(IDS_PICKUP, NULL, MB_OK);
    WriteComm(c, "ATH0\r", 5);
    FlushComm(c, 0);
    FlushComm(c, 1);
    CloseComm(c);
}

void dlg_autodial(void)
{
    FARPROC p;
    int r;
    dial_load();
    dial_guess();
    p = MakeProcInstance((FARPROC)DialDlgProc, g_hinst);
    r = DialogBox(g_hinst, MAKEINTRESOURCE(IDD_AUTODIAL), g_frame, (DLGPROC)p);
    FreeProcInstance(p);
    if (r != TRUE)
        return;
    dial_save();
    dial();
}

/* ---- About ---- */

void about_box(void)
{
#ifndef W16_WIN2
    char t[40];
    LoadString(g_hinst, IDS_APPNAME, t, sizeof t);
    ShellAbout(g_frame, t, "", LoadIcon(g_hinst, MAKEINTRESOURCE(IDI_CARDFILE)));
#else
    FARPROC p = MakeProcInstance((FARPROC)DlgProc, g_hinst);
    dlg_kind = IDD_ABOUT2;
    DialogBox(g_hinst, MAKEINTRESOURCE(IDD_ABOUT2), g_frame, (DLGPROC)p);
    FreeProcInstance(p);
#endif
}
