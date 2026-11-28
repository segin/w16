/*
 * us43.c - probe US43: task switching from the keyboard (Alt+Tab with and
 * without WIN.INI [windows] CoolSwitch, Alt+Shift+Tab, Alt+Esc,
 * Alt+Shift+Esc). Run as the shell, it is instance A; it starts four more
 * instances of itself (B to E, the letter in the command line), each a
 * task with a top-level window "US43 X" of its own class US43X and icon:
 *   B also has a hidden top-level window (US43Bh) and a visible disabled
 *     one (US43Bd), C a visible popup it owns (US43Cp), D starts
 *     minimised and, with WIN.INI [US43] close=1, destroys its window once
 *     the switch box has been up 3 seconds (the window it shows closing
 *     meanwhile).
 * Each instance is a module of its own (US43B.EXE ... US43E.EXE, renamed
 * copies: a second instance's __loadds callbacks would run on the first
 * instance's data). Every message the windows get goes to C:\US43.LOG
 * (all instances append to it), with who got it (the instance's letter,
 * 's' when sent from another task), the window's class and the parameters,
 * hwnds as class names; so do each task's WH_CBT and WH_MSGFILTER calls.
 * Instance A's timer logs, when it changes, the active, focus, capture and
 * system-modal windows and the visible top-level windows in Z order with
 * their rectangles (so the switch box shows up if it is a window).
 * Keys (WM_CHAR in any window): 'm' a modal dialog owned by that window,
 * 's' a system-modal MessageBox, 'n' a MessageBox (modal, task-modal),
 * 'z' the window minimised, 'c' and 'u' CoolSwitch turned over through
 * SystemParametersInfo (flags 0, and 3). B's title is long (how the box
 * fits it) and its background gray; each window writes "paint X" in its
 * client area (what the switching shows of a window).
 * Real keys come from tests/scripts/input/us43*.txt (tests/inputrun.sh;
 * boot-test cases alttab and alttabdos): the log, without the lines that
 * depend on timing (paint messages, the timer's states), is compared with
 * Windows 3.1's.
 */
#include <windows.h>

extern WORD __cdecl init_regs[7];    /* ax bx cx dx si di es */
extern void __cdecl log_write(const char FAR *buf, WORD len);
extern WORD FAR PASCAL WaitEvent(HTASK);
extern BOOL FAR PASCAL InitApp(HINSTANCE);

#define NI 5
static struct inst {
    HTASK task;
    HINSTANCE hi;
    HWND main;
    HHOOK cbt, flt;
    BOOL gone;
    int up;
} in[NI];
static char laststate[600];
static BOOL begun;

static void __loadds lg(const char FAR *s)
{
    HFILE f;
    int n = lstrlen(s);
    f = _lopen("C:\\US43.LOG", OF_READWRITE | OF_SHARE_DENY_NONE);
    if (f == HFILE_ERROR)
        f = _lcreat("C:\\US43.LOG", 0);
    if (f == HFILE_ERROR)
        return;
    _llseek(f, 0, 2);
    _lwrite(f, s, n);
    _lwrite(f, "\r\n", 2);
    _lclose(f);
}

static int me(void)
{
    HTASK t = GetCurrentTask();
    int i;
    for (i = 0; i < NI; i++)
        if (in[i].task == t)
            return i;
    return -1;
}

static char letter(void)
{
    int i = me();
    return i < 0 ? '?' : (char)('A' + i);
}

/* a window as its class name (0 for none, "-" for an invalid handle) */
static void wname(HWND h, char FAR *b)
{
    if (!h) {
        lstrcpy(b, "0");
        return;
    }
    if (!IsWindow(h)) {
        lstrcpy(b, "-");
        return;
    }
    GetClassName(h, b, 20);
}

static const struct { UINT m; const char *n; } names[] = {
    { WM_CREATE, "CREATE" }, { WM_DESTROY, "DESTROY" }, { WM_MOVE, "MOVE" },
    { WM_SIZE, "SIZE" }, { WM_ACTIVATE, "ACTIVATE" }, { WM_SETFOCUS, "SETFOCUS" },
    { WM_KILLFOCUS, "KILLFOCUS" }, { WM_ENABLE, "ENABLE" }, { WM_SETTEXT, "SETTEXT" },
    { WM_GETTEXT, "GETTEXT" }, { WM_GETTEXTLENGTH, "GETTEXTLENGTH" }, { WM_PAINT, "PAINT" },
    { WM_CLOSE, "CLOSE" }, { WM_QUERYOPEN, "QUERYOPEN" }, { WM_ERASEBKGND, "ERASEBKGND" },
    { WM_SHOWWINDOW, "SHOWWINDOW" }, { WM_ACTIVATEAPP, "ACTIVATEAPP" },
    { WM_CANCELMODE, "CANCELMODE" }, { WM_CHILDACTIVATE, "CHILDACTIVATE" },
    { WM_GETMINMAXINFO, "GETMINMAXINFO" }, { WM_PAINTICON, "PAINTICON" },
    { WM_ICONERASEBKGND, "ICONERASEBKGND" }, { WM_QUERYDRAGICON, "QUERYDRAGICON" },
    { WM_WINDOWPOSCHANGING, "POSCHANGING" }, { WM_WINDOWPOSCHANGED, "POSCHANGED" },
    { WM_NCCREATE, "NCCREATE" }, { WM_NCDESTROY, "NCDESTROY" }, { WM_NCCALCSIZE, "NCCALCSIZE" },
    { WM_NCPAINT, "NCPAINT" }, { WM_NCACTIVATE, "NCACTIVATE" }, { WM_GETDLGCODE, "GETDLGCODE" },
    { WM_KEYDOWN, "KEYDOWN" }, { WM_KEYUP, "KEYUP" }, { WM_CHAR, "CHAR" },
    { WM_SYSKEYDOWN, "SYSKEYDOWN" }, { WM_SYSKEYUP, "SYSKEYUP" }, { WM_SYSCHAR, "SYSCHAR" },
    { WM_SYSDEADCHAR, "SYSDEADCHAR" }, { WM_DEADCHAR, "DEADCHAR" },
    { WM_INITDIALOG, "INITDIALOG" }, { WM_COMMAND, "COMMAND" }, { WM_SYSCOMMAND, "SYSCOMMAND" },
    { WM_INITMENU, "INITMENU" }, { WM_INITMENUPOPUP, "INITMENUPOPUP" },
    { WM_MENUSELECT, "MENUSELECT" }, { WM_MENUCHAR, "MENUCHAR" }, { WM_ENTERIDLE, "ENTERIDLE" },
    { WM_LBUTTONDOWN, "LBUTTONDOWN" }, { WM_LBUTTONUP, "LBUTTONUP" },
    { WM_MOUSEACTIVATE, "MOUSEACTIVATE" }, { WM_NCLBUTTONDOWN, "NCLBUTTONDOWN" },
    { WM_CTLCOLOR, "CTLCOLOR" }, { WM_SETFONT, "SETFONT" }, { WM_GETFONT, "GETFONT" },
    { WM_QUERYNEWPALETTE, "QUERYNEWPALETTE" }, { WM_PALETTECHANGED, "PALETTECHANGED" },
    { WM_SYSCOLORCHANGE, "SYSCOLORCHANGE" }, { WM_ENDSESSION, "ENDSESSION" },
    { WM_QUERYENDSESSION, "QUERYENDSESSION" }, { WM_NEXTDLGCTL, "NEXTDLGCTL" },
    { WM_SETREDRAW, "SETREDRAW" }, { WM_TIMER, "TIMER" }, { 0x0026, "PAINTICON26" },
    { 0x0003, "MOVE" }, { 0x0017, "SYSTEMERROR" }, { 0x0018, "SHOWWINDOW" },
    { 0x0088, "SYSTIMER?" }, { 0x0118, "SYSTIMER" }, { 0x003F, "MSG3F" },
    { 0x0035, "ISACTIVEICON" }, { 0x0036, "QUERYPARKICON" }, { 0x0037, "QUERYSAVESTATE?" },
    { 0x0040, "MSG40" }, { 0x0233, "DROPFILES" }, { 0x0231, "ENTERSIZEMOVE" },
    { 0x0232, "EXITSIZEMOVE" }, { 0x0009, "SETVISIBLE" }, { 0x0004, "MSG04" },
    { WM_WININICHANGE, "WININICHANGE" },
    { 0, 0 }
};

static void msgname(UINT m, char FAR *b)
{
    int i;
    for (i = 0; names[i].n; i++)
        if (names[i].m == m) {
            lstrcpy(b, names[i].n);
            return;
        }
    wsprintf(b, "M%04X", m);
}

/* the messages left out: the mouse's, the hit test, timers */
static BOOL quiet(UINT m)
{
    return m == WM_NCHITTEST || m == WM_SETCURSOR || m == WM_MOUSEMOVE || m == WM_NCMOUSEMOVE ||
           m == WM_TIMER || m == WM_ENTERIDLE || m == WM_CTLCOLOR;
}

static void logmsg(const char *src, HWND h, UINT m, WPARAM wp, LPARAM lp)
{
    char b[200], cn[24], mn[24], p1[24], p2[24];
    if (quiet(m))
        return;
    wname(h, cn);
    msgname(m, mn);
    switch (m) {
    case WM_ACTIVATE:
        wname((HWND)LOWORD(lp), p1);
        wsprintf(b, "%c%s %s %s %04X %s %04X", letter(), (LPSTR)src, (LPSTR)cn, (LPSTR)mn, wp,
                 (LPSTR)p1, HIWORD(lp));
        break;
    case WM_SETFOCUS:
    case WM_KILLFOCUS:
        wname((HWND)wp, p1);
        wsprintf(b, "%c%s %s %s %s", letter(), (LPSTR)src, (LPSTR)cn, (LPSTR)mn, (LPSTR)p1);
        break;
    case WM_ACTIVATEAPP:
        wsprintf(b, "%c%s %s %s %04X %s", letter(), (LPSTR)src, (LPSTR)cn, (LPSTR)mn, wp,
                 (LPSTR)(LOWORD(lp) == 0 ? "0" : LOWORD(lp) == (WORD)GetCurrentTask() ? "self" : "task"));
        break;
    case WM_WINDOWPOSCHANGING:
    case WM_WINDOWPOSCHANGED: {
        WINDOWPOS FAR *p = (WINDOWPOS FAR *)lp;
        if (p->hwndInsertAfter == HWND_BOTTOM)
            lstrcpy(p1, "BOTTOM");
        else
            wname(p->hwndInsertAfter, p1);
        wsprintf(b, "%c%s %s %s after=%s %d,%d %dx%d %04X", letter(), (LPSTR)src, (LPSTR)cn,
                 (LPSTR)mn, (LPSTR)p1, p->x, p->y, p->cx, p->cy, p->flags);
        break;
    }
    case WM_WININICHANGE:
    case WM_SETTEXT:
        wsprintf(b, "%c%s %s %s \"%s\"", letter(), (LPSTR)src, (LPSTR)cn, (LPSTR)mn,
                 lp ? (LPSTR)lp : (LPSTR)"");
        break;
    default:
        p2[0] = 0;
        wsprintf(b, "%c%s %s %s %04X %04X %04X", letter(), (LPSTR)src, (LPSTR)cn, (LPSTR)mn, wp,
                 LOWORD(lp), HIWORD(lp));
    }
    lg(b);
}

BOOL CALLBACK __export __loadds dlg_proc(HWND h, UINT m, WPARAM wp, LPARAM lp)
{
    logmsg(InSendMessage() ? "s" : "", h, m, wp, lp);
    if (m == WM_INITDIALOG)
        return TRUE;
    if (m == WM_COMMAND && (wp == IDOK || wp == IDCANCEL)) {
        EndDialog(h, wp);
        return TRUE;
    }
    return FALSE;
}

LRESULT FAR PASCAL __export __loadds wnd_proc(HWND h, UINT m, WPARAM wp, LPARAM lp)
{
    int i = me();
    LRESULT r;
    logmsg(InSendMessage() ? "s" : "", h, m, wp, lp);
    if (m == WM_CHAR && i >= 0) {
        if (wp == 'm') {
            DialogBox(in[i].hi, "US43DLG", h, (DLGPROC)dlg_proc);
            return 0;
        }
        if (wp == 's') {
            MessageBox(h, "system modal", "US43 SM", MB_OK | MB_SYSTEMMODAL);
            return 0;
        }
        if (wp == 'c' || wp == 'u') {
            /* CoolSwitch through SystemParametersInfo: 'c' flags 0, 'u'
               SPIF_UPDATEINIFILE | SPIF_SENDWININICHANGE */
            char b[100];
            BOOL v = 7, v2 = 7, r;
            SystemParametersInfo(SPI_GETFASTTASKSWITCH, 0, &v, 0);
            r = SystemParametersInfo(SPI_SETFASTTASKSWITCH, !v, 0, wp == 'u' ? 3 : 0);
            SystemParametersInfo(SPI_GETFASTTASKSWITCH, 0, &v2, 0);
            wsprintf(b, "%c SPI fast %d set %d -> %d, now %d, WIN.INI %d", letter(), v, !v, r, v2,
                     GetProfileInt("windows", "CoolSwitch", 9));
            lg(b);
            return 0;
        }
        if (wp == 'z') {
            ShowWindow(h, SW_MINIMIZE);
            return 0;
        }
        if (wp == 'n') {
            MessageBox(h, "task modal", "US43 MB", MB_OK);
            return 0;
        }
    }
    if (m == WM_PAINT) {
        /* the client's own drawing (what the switch shows of it) */
        PAINTSTRUCT ps;
        char t[16];
        HDC dc = BeginPaint(h, &ps);
        wsprintf(t, "paint %c", letter());
        TextOut(dc, 4, 4, t, lstrlen(t));
        EndPaint(h, &ps);
        return 0;
    }
    if (m == WM_TIMER && wp == 1 && i == 0) {
        /* A: the state, when it changes */
        char b[600], t[80], c[24];
        HWND w;
        int n = 0;
        if (!begun) {
            if (!FindWindow("US43B", 0) || !FindWindow("US43C", 0) || !FindWindow("US43D", 0) ||
                !FindWindow("US43E", 0))
                return 0;
            begun = TRUE;
            lg("BEGIN");
            {
                BOOL v = 7;
                char b[40];
                SystemParametersInfo(SPI_GETFASTTASKSWITCH, 0, &v, 0);
                wsprintf(b, "A SPI fast %d", v);
                lg(b);
            }
            SetCursorPos(639, 479);
        }
        wname(GetActiveWindow(), c);
        wsprintf(b, "T act=%s", (LPSTR)c);
        wname(GetFocus(), c);
        wsprintf(t, " foc=%s", (LPSTR)c);
        lstrcat(b, t);
        wname(GetCapture(), c);
        wsprintf(t, " cap=%s", (LPSTR)c);
        lstrcat(b, t);
        wname(GetSysModalWindow(), c);
        wsprintf(t, " smod=%s z:", (LPSTR)c);
        lstrcat(b, t);
        for (w = GetWindow(GetDesktopWindow(), GW_CHILD); w && n < 14; w = GetWindow(w, GW_HWNDNEXT)) {
            RECT rc;
            if (!IsWindowVisible(w))
                continue;
            GetWindowRect(w, &rc);
            wname(w, c);
            wsprintf(t, " %s%s(%d,%d,%d,%d)", (LPSTR)c, (LPSTR)(IsIconic(w) ? "*" : ""),
                     rc.left, rc.top, rc.right, rc.bottom);
            if (lstrlen(b) + lstrlen(t) < sizeof b - 1)
                lstrcat(b, t);
            n++;
        }
        if (lstrcmp(b, laststate) != 0) {
            lstrcpy(laststate, b);
            lg(b);
        }
        return 0;
    }
    if (m == WM_TIMER && wp == 2) {
        /* D with WIN.INI [US43] close=1: gone once the switch window has
           been up for 3 seconds (tests/scripts/input/us43close.txt) */
        HWND sw = FindWindow("#32771", 0);
        if (sw && IsWindowVisible(sw)) {
            if (++in[i].up == 30 && !in[i].gone) {
                in[i].gone = TRUE;
                lg("D destroys its window");
                DestroyWindow(h);
            }
        } else
            in[i].up = 0;
        return 0;
    }
    if (m == WM_DESTROY && i >= 0 && h == in[i].main) {
        in[i].main = (HWND)1;
        PostQuitMessage(0);
    }
    r = DefWindowProc(h, m, wp, lp);
    if (m == WM_SYSCOMMAND || m == WM_SYSKEYDOWN || m == WM_SYSKEYUP || m == WM_KEYDOWN ||
        m == WM_SYSCHAR) {
        char b[60], mn[24];
        msgname(m, mn);
        wsprintf(b, "%c %s %04X done", letter(), (LPSTR)mn, wp);
        lg(b);
    }
    return r;
}

LRESULT CALLBACK __export __loadds cbt_proc(int code, WPARAM wp, LPARAM lp)
{
    char b[120], c[24], d[24];
    int i = me();
    switch (code) {
    case HCBT_ACTIVATE: {
        CBTACTIVATESTRUCT FAR *a = (CBTACTIVATESTRUCT FAR *)lp;
        wname((HWND)wp, c);
        wname(a->hWndActive, d);
        wsprintf(b, "%c CBT ACTIVATE %s mouse=%d act=%s", letter(), (LPSTR)c, a->fMouse, (LPSTR)d);
        break;
    }
    case HCBT_SETFOCUS:
        wname((HWND)wp, c);
        wname((HWND)LOWORD(lp), d);
        wsprintf(b, "%c CBT SETFOCUS %s old=%s", letter(), (LPSTR)c, (LPSTR)d);
        break;
    case HCBT_SYSCOMMAND:
        wsprintf(b, "%c CBT SYSCOMMAND %04X %04X %04X", letter(), wp, LOWORD(lp), HIWORD(lp));
        break;
    case HCBT_MINMAX:
        wname((HWND)wp, c);
        wsprintf(b, "%c CBT MINMAX %s %d", letter(), (LPSTR)c, LOWORD(lp));
        break;
    case HCBT_MOVESIZE:
        wname((HWND)wp, c);
        wsprintf(b, "%c CBT MOVESIZE %s", letter(), (LPSTR)c);
        break;
    case HCBT_QS:
        b[0] = 0;
        break;
    default:
        wsprintf(b, "%c CBT %d %04X", letter(), code, wp);
    }
    if (b[0])
        lg(b);
    return i >= 0 ? CallNextHookEx(in[i].cbt, code, wp, lp) : 0;
}

LRESULT CALLBACK __export __loadds flt_proc(int code, WPARAM wp, LPARAM lp)
{
    MSG FAR *m = (MSG FAR *)lp;
    int i = me();
    if (code >= 0 && !quiet(m->message)) {
        char b[120], c[24], mn[24];
        wname(m->hwnd, c);
        msgname(m->message, mn);
        wsprintf(b, "%c FILTER %d %s %s %04X %04X %04X", letter(), code, (LPSTR)c, (LPSTR)mn,
                 m->wParam, LOWORD(m->lParam), HIWORD(m->lParam));
        lg(b);
    }
    return i >= 0 ? CallNextHookEx(in[i].flt, code, wp, lp) : 0;
}

static void __loadds inst_main(int i, HINSTANCE hi, int show)
{
    WNDCLASS wc;
    char cls[8], title[80], cmd[24];
    HWND h;
    MSG m;
    in[i].task = GetCurrentTask();
    in[i].hi = hi;
    wsprintf(cls, "US43%c", 'A' + i);
    wsprintf(title, "US43 %c", 'A' + i);
    if (i == 1)
        lstrcpy(title, "US43 B, whose title is too long to fit the switch box's width");
    wc.style = 0;
    wc.lpfnWndProc = wnd_proc;
    wc.cbClsExtra = wc.cbWndExtra = 0;
    wc.hInstance = hi;
    wc.hIcon = LoadIcon(hi, MAKEINTRESOURCE(i + 1));
    wc.hCursor = LoadCursor(0, IDC_ARROW);
    wc.hbrBackground = (HBRUSH)(COLOR_WINDOW + 1);
    wc.lpszMenuName = 0;
    wc.lpszClassName = cls;
    if (i == 1)
        wc.hbrBackground = (HBRUSH)(COLOR_BTNSHADOW + 1);
    RegisterClass(&wc);
    wc.hbrBackground = (HBRUSH)(COLOR_WINDOW + 1);
    in[i].cbt = SetWindowsHookEx(WH_CBT, (HOOKPROC)cbt_proc, hi, GetCurrentTask());
    in[i].flt = SetWindowsHookEx(WH_MSGFILTER, (HOOKPROC)flt_proc, hi, GetCurrentTask());
    h = CreateWindow(cls, title, WS_OVERLAPPEDWINDOW, 10 + 70 * i, 10 + 60 * i, 260, 160, 0, 0, hi, 0);
    if (i == 1) {
        wc.lpszClassName = "US43Bh";
        RegisterClass(&wc);
        CreateWindow("US43Bh", "US43 Bh", WS_OVERLAPPEDWINDOW, 400, 300, 200, 100, 0, 0, hi, 0);
        wc.lpszClassName = "US43Bd";
        RegisterClass(&wc);
        CreateWindow("US43Bd", "US43 Bd", WS_OVERLAPPEDWINDOW | WS_VISIBLE | WS_DISABLED, 420, 20, 200, 100,
                     0, 0, hi, 0);
    }
    ShowWindow(h, show);
    UpdateWindow(h);
    if (i == 2) {
        wc.lpszClassName = "US43Cp";
        RegisterClass(&wc);
        CreateWindow("US43Cp", "US43 Cp", WS_POPUP | WS_CAPTION | WS_SYSMENU | WS_VISIBLE, 440, 160, 180, 80,
                     h, 0, hi, 0);
    }
    in[i].main = h;
    if (i == 0) {
        int k;
        SetTimer(h, 1, 200, 0);
        for (k = 1; k < NI; k++) {
            wsprintf(cmd, "US43%c.EXE %c", 'A' + k, 'A' + k);
            WinExec(cmd, k == 3 ? SW_SHOWMINIMIZED : SW_SHOWNORMAL);
        }
    }
    if (i == 3 && GetProfileInt("US43", "close", 0))
        SetTimer(h, 2, 100, 0);
    while (GetMessage(&m, 0, 0, 0)) {
        TranslateMessage(&m);
        DispatchMessage(&m);
    }
    if (i == 0) {
        /* A stays the shell: Windows ends only by its own Exit */
        for (;;)
            WaitMessage();
    }
}

int __cdecl tmain(void)
{
    LPSTR c = MAKELP(init_regs[6], init_regs[1]);
    int i = 0;
    while (*c == ' ')
        c++;
    if (*c >= 'B' && *c <= 'E')
        i = *c - 'A';
    WaitEvent(0);
    InitApp((HINSTANCE)init_regs[5]);
    inst_main(i, (HINSTANCE)init_regs[5], init_regs[3]);
    return 0;
}
