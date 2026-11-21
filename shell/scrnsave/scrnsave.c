/*
 * scrnsave.c - w16's screen saver library (scrnsave.h): WinMain, the
 * full-screen window's default behaviour, the password dialogs and the
 * spin control the Setup dialogs use. Written from the Windows 3.1 SDK's
 * description of SCRNSAVE.LIB (Programmer's Reference vol. 1, ch. 14)
 * and black-box observation of Windows 3.1's SSSTARS.SCR (ARCHITECTURE.md
 * 15.8; tests/scripts/ss):
 *
 * - /s (or -s) runs the saver: a WS_POPUP window over the whole screen,
 *   topmost, of class "WindowsScreenSaverClass" (black background, no
 *   cursor). A second /s while one runs does nothing.
 * - A key, a mouse button or the mouse moving ends it (DefScreenSaverProc
 *   posts WM_CLOSE). Password protected (CONTROL.INI [Screen Saver.X]
 *   PWProtected=1), it asks instead: the dialog captioned "X" (the part of
 *   idsAppName after the dot), centred on the screen, with the
 *   exclamation icon; Cancel goes back to the saver; a wrong password
 *   shows "Incorrect password" (the stop icon) and then goes back to the
 *   saver too; the right one ends it.
 * - The password is CONTROL.INI [ScreenSaver] Password, shared by every
 *   saver, written by the Change Password dialog as soon as it is OK'd:
 *   uppercased and encoded (ss_encode_password), empty for none. Its
 *   edits take up to 20 characters. Old Password is disabled when there
 *   is none. OK starts disabled with Cancel the default button; text in
 *   any field enables it and makes it the default. A wrong old password:
 *   "The value for Old Password is not correct..." and that field
 *   cleared; New and Retype differing: "The values for New Password and
 *   Retype New Password do not match..." and both cleared (message boxes
 *   captioned "Change Password", the stop icon).
 * - Help: CONTROL.HLP, topic 9000 for the password dialog (its Help
 *   button and F1); HELP_QUIT when a dialog closes.
 * UNVERIFIED: how far the mouse must move to end the saver (w16: any
 * move from where the pointer was at the start), whether a key's release
 * ends it (w16: no, so that the key that started a test does not end it),
 * which check comes first when both the old and the new passwords are
 * wrong.
 */
#include "scrnsave.h"

void pw_encode(char *s);                /* pwenc.c */

HINSTANCE hMainInstance;
HWND hMainWindow;
UINT MyHelpMessage;
char szAppName[64];
char szIniFile[] = "CONTROL.INI";

static char s_class[] = "WindowsScreenSaverClass";
static char s_ss[] = "ScreenSaver";
static char s_pw[] = "Password";
static char s_protected[] = "PWProtected";

static BOOL pw_protected;       /* this run's */
static BOOL in_dialog;          /* the password dialog is up */
static POINT pt_start;          /* where the pointer was */
static BOOL have_start;
static HHOOK help_hook;
static unsigned long rnd;

#define PW_MAX 20               /* the edits' limit (observed) */

/* ---- small helpers ---- */

int ss_get_int(const char *key, int def)
{
    return GetPrivateProfileInt(szAppName, key, def, szIniFile);
}

void ss_put_int(const char *key, int v)
{
    char b[8];
    wsprintf(b, "%d", v);
    WritePrivateProfileString(szAppName, key, b, szIniFile);
}

unsigned ss_rand(void)
{
    rnd = rnd * 1103515245UL + 12345;
    return (unsigned)(rnd >> 16) & 0x7FFF;
}

/* the stored password */
static void get_password(char *buf, int size)
{
    GetPrivateProfileString(s_ss, s_pw, "", buf, size, szIniFile);
}

/* what the user typed, as stored */
static void typed_password(HWND dlg, int id, char *buf, int size)
{
    GetDlgItemText(dlg, id, buf, size);
    ss_encode_password(buf);
}

static void center(HWND w)
{
    RECT r;
    GetWindowRect(w, &r);
    SetWindowPos(w, NULL,
                 (GetSystemMetrics(SM_CXSCREEN) - (r.right - r.left) + 1) / 2,
                 (GetSystemMetrics(SM_CYSCREEN) - (r.bottom - r.top) + 1) / 2,
                 0, 0, SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE);
}

static int message(HWND owner, UINT ids)
{
    char t[64], m[256];
    LoadString(hMainInstance, IDS_PWCAPTION, t, sizeof t);
    LoadString(hMainInstance, ids, m, sizeof m);
    return MessageBox(owner, m, t, MB_OK | MB_ICONSTOP);
}

static void help(HWND w, DWORD topic)
{
    WinHelp(w, HELP_FILE, HELP_CONTEXT, topic);
}

/* ---- F1 in the dialogs: MyHelpMessage to the dialog (SDK) ---- */

DWORD FAR PASCAL __export HelpMessageFilterHookFunction(int code, WORD wp, LPMSG m)
{
    if (code == MSGF_DIALOGBOX && m->message == WM_KEYDOWN && m->wParam == VK_F1) {
        HWND d = m->hwnd;
        while (d && (GetWindowLong(d, GWL_STYLE) & WS_CHILD))
            d = GetParent(d);
        if (d) {
            PostMessage(d, MyHelpMessage, 0, 0);
            return TRUE;
        }
    }
    return CallNextHookEx(help_hook, code, wp, (LPARAM)m);
}

/* ---- the password ----
   ss_encode_password: the stored form of a password, as Windows 3.1
   writes it (observed: CONTROL.INI after setting passwords). */
void ss_encode_password(char *s)
{
    AnsiUpper(s);
    pw_encode(s);
}

/* the password dialogs' caption: idsAppName after the dot ("Stars") */
static void caption(HWND dlg)
{
    const char *p = szAppName;
    while (*p && *p != '.')
        p++;
    SetWindowText(dlg, *p ? p + 1 : szAppName);
}

BOOL FAR PASCAL __export DlgInvalidPassword(HWND dlg, UINT msg, WPARAM wp, LPARAM lp)
{
    (void)lp;
    switch (msg) {
    case WM_INITDIALOG:
        caption(dlg);
        SendDlgItemMessage(dlg, ID_ICON, STM_SETICON, (WPARAM)LoadIcon(NULL, IDI_HAND), 0);
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

BOOL FAR PASCAL __export DlgGetPassword(HWND dlg, UINT msg, WPARAM wp, LPARAM lp)
{
    (void)lp;
    switch (msg) {
    case WM_INITDIALOG: {
        caption(dlg);
        SendDlgItemMessage(dlg, ID_ICON, STM_SETICON, (WPARAM)LoadIcon(NULL, IDI_EXCLAMATION), 0);
        SendDlgItemMessage(dlg, ID_ETPASSWORD, EM_LIMITTEXT, PW_MAX, 0);
        center(dlg);
        SetFocus(GetDlgItem(dlg, ID_ETPASSWORD));
        return FALSE;
    }
    case WM_COMMAND:
        switch (wp) {
        case IDOK: {
            char typed[PW_MAX + 1], stored[PW_MAX * 2 + 2];
            typed_password(dlg, ID_ETPASSWORD, typed, sizeof typed);
            get_password(stored, sizeof stored);
            if (!lstrcmp(typed, stored)) {
                EndDialog(dlg, TRUE);
            } else {
                FARPROC f = MakeProcInstance((FARPROC)DlgInvalidPassword, hMainInstance);
                DialogBox(hMainInstance, MAKEINTRESOURCE(DLG_INVALIDPASSWORD), dlg, (DLGPROC)f);
                FreeProcInstance(f);
                EndDialog(dlg, FALSE);
            }
            return TRUE;
        }
        case IDCANCEL:
            EndDialog(dlg, FALSE);
            return TRUE;
        }
        break;
    }
    return FALSE;
}

static void enable(HWND dlg, int id, BOOL on)
{
    EnableWindow(GetDlgItem(dlg, id), on);
}

/* OK the default button or not */
static void ok_default(HWND dlg, BOOL ok, BOOL redraw)
{
    SendMessage(dlg, DM_SETDEFID, ok ? IDOK : IDCANCEL, 0);
    SendMessage(GetDlgItem(dlg, ok ? IDCANCEL : IDOK), BM_SETSTYLE, BS_PUSHBUTTON, redraw);
    SendMessage(GetDlgItem(dlg, ok ? IDOK : IDCANCEL), BM_SETSTYLE, BS_DEFPUSHBUTTON, redraw);
}

/* the fields' states (observed): with a password set, New and Retype
   (and their labels) only while Old has text, OK always; with none, Old
   disabled, and OK, the default button, only once New or Retype has
   text (Cancel the default meanwhile) */
static void cp_update(HWND dlg, BOOL redraw)
{
    HWND ok = GetDlgItem(dlg, IDOK);
    if (IsWindowEnabled(GetDlgItem(dlg, ID_ETOLD))) {
        BOOL on = GetWindowTextLength(GetDlgItem(dlg, ID_ETOLD)) != 0;
        enable(dlg, ID_NEWTEXT, on);
        enable(dlg, ID_ETNEW, on);
        enable(dlg, ID_AGAIN, on);
        enable(dlg, ID_ETAGAIN, on);
    } else {
        BOOL any = GetWindowTextLength(GetDlgItem(dlg, ID_ETNEW)) ||
                   GetWindowTextLength(GetDlgItem(dlg, ID_ETAGAIN));
        if (any == IsWindowEnabled(ok))
            return;
        EnableWindow(ok, any);
        ok_default(dlg, any, redraw);
    }
}

BOOL FAR PASCAL __export DlgChangePassword(HWND dlg, UINT msg, WPARAM wp, LPARAM lp)
{
    if (msg == MyHelpMessage && msg) {
        help(dlg, HELP_PASSWORD);
        return TRUE;
    }
    switch (msg) {
    case WM_INITDIALOG: {
        char stored[PW_MAX * 2 + 2];
        int id;
        for (id = ID_ETOLD; id <= ID_ETAGAIN; id++)
            SendDlgItemMessage(dlg, id, EM_LIMITTEXT, PW_MAX, 0);
        get_password(stored, sizeof stored);
        if (!stored[0]) {
            enable(dlg, ID_OLDTEXT, FALSE);
            enable(dlg, ID_ETOLD, FALSE);
        }
        cp_update(dlg, FALSE);
        SetFocus(GetDlgItem(dlg, stored[0] ? ID_ETOLD : ID_ETNEW));
        return FALSE;
    }
    case WM_COMMAND:
        switch (wp) {
        case ID_ETOLD:
        case ID_ETNEW:
        case ID_ETAGAIN:
            if (HIWORD(lp) == EN_CHANGE)
                cp_update(dlg, TRUE);
            return TRUE;
        case IDOK: {
            char a[PW_MAX + 1], b[PW_MAX + 1], stored[PW_MAX * 2 + 2];
            get_password(stored, sizeof stored);
            if (IsWindowEnabled(GetDlgItem(dlg, ID_ETOLD))) {
                typed_password(dlg, ID_ETOLD, a, sizeof a);
                if (lstrcmp(a, stored)) {
                    message(dlg, IDS_PWBADOLD);
                    SetDlgItemText(dlg, ID_ETOLD, "");
                    SetFocus(GetDlgItem(dlg, ID_ETOLD));
                    return TRUE;
                }
            }
            typed_password(dlg, ID_ETNEW, a, sizeof a);
            typed_password(dlg, ID_ETAGAIN, b, sizeof b);
            if (lstrcmp(a, b)) {
                message(dlg, IDS_PWMISMATCH);
                SetDlgItemText(dlg, ID_ETNEW, "");
                SetDlgItemText(dlg, ID_ETAGAIN, "");
                SetFocus(GetDlgItem(dlg, ID_ETNEW));
                return TRUE;
            }
            WritePrivateProfileString(s_ss, s_pw, a, szIniFile);
            WinHelp(dlg, HELP_FILE, HELP_QUIT, 0);
            EndDialog(dlg, TRUE);
            return TRUE;
        }
        case IDCANCEL:
            WinHelp(dlg, HELP_FILE, HELP_QUIT, 0);
            EndDialog(dlg, FALSE);
            return TRUE;
        case ID_PASSWORDHELP:
            help(dlg, HELP_PASSWORD);
            return TRUE;
        }
        break;
    }
    return FALSE;
}

/* the Setup dialog's Set Password... button */
void ss_set_password(HWND dlg)
{
    FARPROC f = MakeProcInstance((FARPROC)DlgChangePassword, hMainInstance);
    DialogBox(hMainInstance, MAKEINTRESOURCE(DLG_CHANGEPASSWORD), dlg, (DLGPROC)f);
    FreeProcInstance(f);
    SendMessage(dlg, WM_NEXTDLGCTL, (WPARAM)GetDlgItem(dlg, IDOK), 1L);
}

/* ---- the spin control: class "cpArrow" (as Control Panel's) ----
   Observed (Windows 3.1's SSSTARS.SCR, a 14 x 20 control with WS_BORDER):
   the face colour, a black line across 9/20 of the client height down,
   an up arrow whose base is 2 rows above the line and a down arrow from
   3 rows below it, each 6 rows (a press on the line counts as up); the rows' spans around the middle
   column c are c, c-1..c+1, c-2..c+1, c-2..c+2, c-3..c+2, c-4..c+3. A
   press sends the parent WM_VSCROLL SB_LINEUP or SB_LINEDOWN with the
   control in the high word of lParam. */
static const signed char arrow_span[6][2] = {
    { 0, 0 }, { -1, 1 }, { -2, 1 }, { -2, 2 }, { -3, 2 }, { -4, 3 }
};

static void arrow_paint(HWND w, HDC dc)
{
    RECT r, t;
    HBRUSH face, black;
    int mid, cx, k;
    GetClientRect(w, &r);
    face = CreateSolidBrush(GetSysColor(COLOR_BTNFACE));
    black = GetStockObject(BLACK_BRUSH);
    FillRect(dc, &r, face);
    mid = r.top + 9 * (r.bottom - r.top) / 20;
    SetRect(&t, r.left, mid, r.right, mid + 1);
    FillRect(dc, &t, black);
    cx = (r.right - r.left) / 2;
    for (k = 0; k < 6; k++) {
        SetRect(&t, cx + arrow_span[k][0], mid - 7 + k, cx + arrow_span[k][1] + 1, mid - 6 + k);
        FillRect(dc, &t, black);
        SetRect(&t, cx + arrow_span[k][0], mid + 8 - k, cx + arrow_span[k][1] + 1, mid + 9 - k);
        FillRect(dc, &t, black);
    }
    DeleteObject(face);
}

LRESULT FAR PASCAL __export ArrowProc(HWND w, UINT msg, WPARAM wp, LPARAM lp)
{
    switch (msg) {
    case WM_PAINT: {
        PAINTSTRUCT ps;
        BeginPaint(w, &ps);
        arrow_paint(w, ps.hdc);
        EndPaint(w, &ps);
        return 0;
    }
    case WM_LBUTTONDOWN:
    case WM_LBUTTONDBLCLK: {
        RECT r;
        GetClientRect(w, &r);
        SendMessage(GetParent(w), WM_VSCROLL,
                    (int)HIWORD(lp) <= r.bottom * 9 / 20 ? SB_LINEUP : SB_LINEDOWN,
                    MAKELONG(0, w));
        return 0;
    }
    }
    return DefWindowProc(w, msg, wp, lp);
}

void ss_register_arrow(HINSTANCE h)
{
    WNDCLASS wc;
    wc.style = CS_DBLCLKS;
    wc.lpfnWndProc = ArrowProc;
    wc.cbClsExtra = wc.cbWndExtra = 0;
    wc.hInstance = h;
    wc.hIcon = NULL;
    wc.hCursor = LoadCursor(NULL, IDC_ARROW);
    wc.hbrBackground = NULL;
    wc.lpszMenuName = NULL;
    wc.lpszClassName = "cpArrow";
    RegisterClass(&wc);
}

/* ---- the saver's window ---- */

/* input: end the saver, or ask for the password first (WM_CLOSE) */
static void input(HWND h)
{
    if (!in_dialog)
        PostMessage(h, WM_CLOSE, 0, 0);
}

static BOOL ask_password(HWND h)
{
    FARPROC f;
    int r;
    in_dialog = TRUE;
    f = MakeProcInstance((FARPROC)DlgGetPassword, hMainInstance);
    r = DialogBox(hMainInstance, MAKEINTRESOURCE(DLG_ENTERPASSWORD), h, (DLGPROC)f);
    FreeProcInstance(f);
    in_dialog = FALSE;
    GetCursorPos(&pt_start);    /* moves from here on end it again */
    return r == TRUE;
}

LRESULT WINAPI DefScreenSaverProc(HWND h, UINT msg, WPARAM wp, LPARAM lp)
{
    switch (msg) {
    case WM_CREATE:
        GetCursorPos(&pt_start);
        have_start = TRUE;
        break;
    case WM_SETCURSOR:
        if (!in_dialog) {
            SetCursor(NULL);
            return TRUE;
        }
        break;
    case WM_MOUSEMOVE: {
        POINT pt;
        GetCursorPos(&pt);
        if (!have_start) {
            pt_start = pt;
            have_start = TRUE;
        } else if (pt.x != pt_start.x || pt.y != pt_start.y)
            input(h);
        return 0;
    }
    case WM_LBUTTONDOWN:
    case WM_RBUTTONDOWN:
    case WM_MBUTTONDOWN:
    case WM_KEYDOWN:
    case WM_SYSKEYDOWN:
        input(h);
        return 0;
    case WM_ACTIVATEAPP:
    case WM_ACTIVATE:
    case WM_NCACTIVATE:
        if (!wp && !pw_protected && !in_dialog)
            PostMessage(h, WM_CLOSE, 0, 0);
        break;
    case WM_SYSCOMMAND:
        if ((wp & 0xFFF0) == SC_SCREENSAVE || (wp & 0xFFF0) == SC_CLOSE)
            return FALSE;
        break;
    case WM_CLOSE:
        if (in_dialog)
            return 0;
        if (pw_protected && !ask_password(h))
            return 0;
        DestroyWindow(h);
        return 0;
    case WM_DESTROY:
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProc(h, msg, wp, lp);
}

static int run_saver(HINSTANCE inst, HINSTANCE prev)
{
    WNDCLASS wc;
    MSG m;
    if (FindWindow(s_class, NULL))
        return 0;
    pw_protected = ss_get_int(s_protected, 0) != 0;
    if (!prev) {
        wc.style = CS_VREDRAW | CS_HREDRAW | CS_SAVEBITS | CS_DBLCLKS;
        wc.lpfnWndProc = ScreenSaverProc;
        wc.cbClsExtra = wc.cbWndExtra = 0;
        wc.hInstance = inst;
        wc.hIcon = LoadIcon(inst, MAKEINTRESOURCE(ID_APP));
        wc.hCursor = NULL;
        wc.hbrBackground = GetStockObject(BLACK_BRUSH);
        wc.lpszMenuName = NULL;
        wc.lpszClassName = s_class;
        if (!RegisterClass(&wc))
            return 0;
    }
    hMainWindow = CreateWindowEx(WS_EX_TOPMOST, s_class, "",
                                 WS_POPUP | WS_VISIBLE | WS_CLIPSIBLINGS, 0, 0,
                                 GetSystemMetrics(SM_CXSCREEN), GetSystemMetrics(SM_CYSCREEN),
                                 NULL, NULL, inst, NULL);
    if (!hMainWindow)
        return 0;
    while (GetMessage(&m, NULL, 0, 0)) {
        TranslateMessage(&m);
        DispatchMessage(&m);
    }
    return m.wParam;
}

static int configure(HINSTANCE inst)
{
    FARPROC f, hook;
    int r;
    if (!RegisterDialogClasses(inst))
        return 0;
    hook = MakeProcInstance((FARPROC)HelpMessageFilterHookFunction, inst);
    help_hook = SetWindowsHookEx(WH_MSGFILTER, (HOOKPROC)hook, inst, GetCurrentTask());
    f = MakeProcInstance((FARPROC)ScreenSaverConfigureDialog, inst);
    /* owned by the window that was active (Control Panel's Desktop
       dialog for its Setup...: 3.1's comes up placed from it, that
       dialog disabled; run alone, from nothing) */
    r = DialogBox(inst, MAKEINTRESOURCE(DLG_SCRNSAVECONFIGURE), GetActiveWindow(), (DLGPROC)f);
    FreeProcInstance(f);
    if (help_hook)
        UnhookWindowsHookEx(help_hook);
    FreeProcInstance(hook);
    return r;
}

int PASCAL WinMain(HINSTANCE inst, HINSTANCE prev, LPSTR cmd, int show)
{
    (void)show;
    hMainInstance = inst;
    rnd = GetTickCount();
    LoadString(inst, idsAppName, szAppName, sizeof szAppName);
    MyHelpMessage = RegisterWindowMessage("SCRNSAVE_HELP");
    while (*cmd == ' ' || *cmd == '\t')
        cmd++;
    if ((cmd[0] == '/' || cmd[0] == '-') && (cmd[1] == 's' || cmd[1] == 'S'))
        return run_saver(inst, prev);
    return configure(inst);
}
