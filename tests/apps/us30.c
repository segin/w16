/*
 * us30.c - probe US30: where Windows 3.1's USER calls the hooks w16 did
 * not call (USER audit, item 8; docs/notes/user-audit-triage.md), with
 * which code, wParam and lParam, and what a nonzero return does.
 *
 * Run as the shell on Windows 3.1 (tests/dropin.sh -r) and with w16's
 * USER (-f USER.EXE); the OBS lines of the two logs must be identical
 * (tests/boottest.sh case uaudit). Each section installs task-local
 * hooks whose procedures record their calls as short tokens, does
 * something, and logs the record:
 *   cbt     WH_CBT around CreateWindow, ShowWindow, SetFocus, SetActiveWindow,
 *           WM_SYSCOMMAND, MoveWindow, DestroyWindow; then each refused
 *           (the procedure returns 1) once (not CREATEWND: 3.1 dies when
 *           it is refused)
 *   kbd     WH_KEYBOARD for keys from keybd_event, read with PM_NOREMOVE
 *           and PM_REMOVE; a key the hook refuses
 *   mouse   WH_MOUSE for a click from mouse_event; a refused click
 *   filter  WH_MSGFILTER in a dialog box, a message box, a popup menu, the
 *           keyboard move and size loops (each ended by an Escape, sent by
 *           a timer or, for move and size, queued before)
 *   jr      WH_JOURNALRECORD (system-wide) for a key
 * Tokens: a code with its hook's letter (C cbt, K keyboard, M mouse, F
 * msgfilter, J journal), then ':' and what was passed.
 */
#include <windows.h>

#define TK_REF "us30ref.h"
#include "tklog.h"

extern WORD FAR PASCAL WaitEvent(HTASK);
extern BOOL FAR PASCAL InitApp(HINSTANCE);
extern void __cdecl call_keybd_event(WORD ax, WORD bx);
extern void __cdecl call_mouse_event(WORD status, WORD x, WORD y, WORD buttons);

/* HCBT_ACTIVATE's lParam (3.1 SDK, CBTACTIVATESTRUCT) */
typedef struct { BOOL fMouse; HWND hWndActive; } CBTACT;

static HINSTANCE hi;
static HWND hMain, ha, hb;
static HHOOK hk;
static int deny = -1;           /* the code the procedure refuses, -1 none */
static char rec[700];

static void rec_add(const char *s)
{
    int n = lstrlen(rec);
    if (n + lstrlen(s) + 2 >= sizeof rec)
        return;
    if (n)
        rec[n++] = ' ';
    lstrcpy(rec + n, s);
}

/* the icon title window (class #32772): w16 makes it again at each
   minimise and destroys it at each restore, 3.1 keeps it until its window
   goes; its CBT calls are left out of the record */
static BOOL is_title(HWND h)
{
    char c[8];
    return h && GetClassName(h, c, sizeof c) && !lstrcmp(c, "#32772");
}

static char wname(HWND h)
{
    return h == 0 ? '0' : h == hMain ? 'm' : h == ha ? 'a' : h == hb ? 'b' : '?';
}

static void pump(DWORD ms)
{
    MSG m;
    DWORD t = GetTickCount();
    while (GetTickCount() - t < ms)
        if (PeekMessage(&m, 0, 0, 0, PM_REMOVE)) {
            TranslateMessage(&m);
            DispatchMessage(&m);
        }
}

static void drain(void)
{
    MSG m;
    pump(200);
    while (PeekMessage(&m, 0, 0, 0, PM_REMOVE))
        ;
}

/* ---- WH_CBT ---- */
LRESULT FAR PASCAL __export __loadds cbt_proc(int code, WPARAM w, LPARAM l)
{
    char t[24];
    if ((code == HCBT_DESTROYWND && is_title((HWND)w)) ||
        (code == HCBT_CREATEWND && !HIWORD((DWORD)((LPCBT_CREATEWND)l)->lpcs->lpszClass) &&
         LOWORD((DWORD)((LPCBT_CREATEWND)l)->lpcs->lpszClass) == 32772))
        return CallNextHookEx(hk, code, w, l);
    switch (code) {
    case HCBT_CREATEWND:
        /* the window is not known yet: its class's first letter */
        wsprintf(t, "C%d:%c", code,
                 (char)(HIWORD((DWORD)((LPCBT_CREATEWND)l)->lpcs->lpszClass) ?
                        ((LPCBT_CREATEWND)l)->lpcs->lpszClass[4] : '#'));
        break;
    case HCBT_MINMAX:
    case HCBT_SYSCOMMAND:
        wsprintf(t, "C%d:%c%x", code, wname((HWND)w), LOWORD(l));
        if (code == HCBT_SYSCOMMAND)
            wsprintf(t, "C%d:%x", code, w & 0xFFF0);
        break;
    case HCBT_SETFOCUS:
        wsprintf(t, "C%d:%c%c", code, wname((HWND)w), wname((HWND)LOWORD(l)));
        break;
    case HCBT_ACTIVATE:
        wsprintf(t, "C%d:%c%d%c", code, wname((HWND)w),
                 ((CBTACT FAR *)l)->fMouse, wname(((CBTACT FAR *)l)->hWndActive));
        break;
    case HCBT_MOVESIZE:
        wsprintf(t, "C%d:%c", code, wname((HWND)w));
        break;
    default:
        wsprintf(t, "C%d:%c", code, wname((HWND)w));
        break;
    }
    rec_add(t);
    if (code == deny) {
        deny = -1;
        rec_add("!");
        return 1;
    }
    return CallNextHookEx(hk, code, w, l);
}

static void sec_cbt(void)
{
    static const int codes[] = { HCBT_CREATEWND, HCBT_DESTROYWND, HCBT_ACTIVATE,
                                 HCBT_MINMAX, HCBT_SETFOCUS, HCBT_SYSCOMMAND, HCBT_MOVESIZE, -2 };
    char nm[24];
    int k;
    for (k = 0; codes[k] != -2; k++) {
        HWND r;
        RECT rc;
        rec[0] = 0;
        ha = hb = 0;
        hk = SetWindowsHookEx(WH_CBT, (HOOKPROC)cbt_proc, hi, GetCurrentTask());
        deny = k ? codes[k] : -1;
        if (k == 0) {
            /* first the plain run, recorded alone */
            ha = CreateWindow("US30A", "a", WS_OVERLAPPEDWINDOW, 10, 10, 200, 150, 0, 0, hi, NULL);
            rec_add("|show");
            ShowWindow(ha, SW_SHOWNORMAL);
            rec_add("|child");
            hb = CreateWindow("US30B", "b", WS_CHILD | WS_VISIBLE, 0, 0, 20, 20, ha, 0, hi, NULL);
            rec_add("|focus");
            SetFocus(hb);
            rec_add("|activate");
            SetActiveWindow(hMain);
            rec_add("|min");
            ShowWindow(ha, SW_MINIMIZE);
            rec_add("|restore");
            ShowWindow(ha, SW_RESTORE);
            rec_add("|max");
            ShowWindow(ha, SW_SHOWMAXIMIZED);
            rec_add("|sysmin");
            SendMessage(ha, WM_SYSCOMMAND, SC_MINIMIZE, 0);
            rec_add("|sysrestore");
            SendMessage(ha, WM_SYSCOMMAND, SC_RESTORE, 0);
            rec_add("|move");
            MoveWindow(ha, 20, 20, 210, 160, TRUE);
            rec_add("|destroy");
            DestroyWindow(ha);
            UnhookWindowsHookEx(hk);
            obs_str("cbt", rec);
            continue;
        }
        /* refused once: the same steps, briefer */
        r = CreateWindow("US30A", "a", WS_OVERLAPPEDWINDOW, 10, 10, 200, 150, 0, 0, hi, NULL);
        wsprintf(nm, "cbt.deny%d.create", codes[k]);
        obs(nm, r != 0);
        if (!r)
            r = CreateWindow("US30A", "a", WS_OVERLAPPEDWINDOW, 10, 10, 200, 150, 0, 0, hi, NULL);
        ha = r;
        ShowWindow(ha, SW_SHOWNORMAL);
        hb = CreateWindow("US30B", "b", WS_CHILD | WS_VISIBLE, 0, 0, 20, 20, ha, 0, hi, NULL);
        SetFocus(hb);
        wsprintf(nm, "cbt.deny%d.focus", codes[k]);
        obs(nm, GetFocus() == hb);
        SetActiveWindow(hMain);
        wsprintf(nm, "cbt.deny%d.active", codes[k]);
        obs(nm, GetActiveWindow() == hMain);
        SendMessage(ha, WM_SYSCOMMAND, SC_MAXIMIZE, 0);
        wsprintf(nm, "cbt.deny%d.zoomed", codes[k]);
        obs(nm, IsZoomed(ha));
        /* not of a normal window: 3.1 does not activate it then, w16
           does (left: docs/notes/user-audit-triage.md) */
        if (IsZoomed(ha))
            ShowWindow(ha, SW_RESTORE);
        MoveWindow(ha, 20, 20, 210, 160, TRUE);
        GetWindowRect(ha, &rc);
        wsprintf(nm, "cbt.deny%d.moved", codes[k]);
        obs(nm, rc.left == 20);
        DestroyWindow(ha);
        wsprintf(nm, "cbt.deny%d.alive", codes[k]);
        obs(nm, IsWindow(ha));
        UnhookWindowsHookEx(hk);
        if (IsWindow(ha))
            DestroyWindow(ha);
        wsprintf(nm, "cbt.deny%d", codes[k]);
        /* a refused SETFOCUS in an activation: 3.1 then names the
           activated window as the old focus, w16 none (left:
           docs/notes/user-audit-triage.md); not compared */
        if (codes[k] == HCBT_SETFOCUS) {
            out("OBS ~");
            out(nm);
            out("=\"");
            out(rec);
            out("\"\r\n");
        } else
            obs_str(nm, rec);
        deny = -1;
    }
}

/* ---- WH_KEYBOARD ---- */
LRESULT FAR PASCAL __export __loadds kbd_proc(int code, WPARAM w, LPARAM l)
{
    char t[24];
    wsprintf(t, "K%d:%x.%x", code, w, HIWORD(l));
    rec_add(t);
    if (deny == 1 && code == HC_ACTION && !(l & 0x80000000L)) {
        deny = -1;
        rec_add("!");
        return 1;
    }
    return CallNextHookEx(hk, code, w, l);
}

static void keys(void)
{
    call_keybd_event(0x0041, 0x001E);
    call_keybd_event(0x8041, 0x001E);
}

static void sec_kbd(void)
{
    MSG m;
    char t[16];
    SetActiveWindow(hMain);
    SetFocus(hMain);
    drain();
    rec[0] = 0;
    hk = SetWindowsHookEx(WH_KEYBOARD, (HOOKPROC)kbd_proc, hi, GetCurrentTask());
    keys();
    rec_add("|peek");
    PeekMessage(&m, 0, 0, 0, PM_NOREMOVE);
    rec_add("|peek2");
    PeekMessage(&m, 0, 0, 0, PM_NOREMOVE);
    rec_add("|get");
    while (PeekMessage(&m, 0, WM_KEYFIRST, WM_KEYLAST, PM_REMOVE)) {
        wsprintf(t, "=%x", m.message);
        rec_add(t);
    }
    obs_str("kbd", rec);
    rec[0] = 0;
    deny = 1;
    keys();
    while (PeekMessage(&m, 0, WM_KEYFIRST, WM_KEYLAST, PM_REMOVE)) {
        wsprintf(t, "=%x", m.message);
        rec_add(t);
    }
    obs_str("kbd.deny", rec);
    deny = -1;
    UnhookWindowsHookEx(hk);
}

/* ---- WH_MOUSE ---- */
LRESULT FAR PASCAL __export __loadds mouse_proc(int code, WPARAM w, LPARAM l)
{
    char t[24];
    LPMOUSEHOOKSTRUCT ms = (LPMOUSEHOOKSTRUCT)l;
    wsprintf(t, "M%d:%x.%c%d", code, w, wname(ms->hwnd), ms->wHitTestCode);
    rec_add(t);
    if (deny == 2 && w == WM_LBUTTONDOWN && code == HC_ACTION) {
        deny = -1;
        rec_add("!");
        return 1;
    }
    return CallNextHookEx(hk, code, w, l);
}

static WORD mx, my;
static void click(void)
{
    call_mouse_event(0x8002, mx, my, 0);
    call_mouse_event(0x8004, mx, my, 0);
}

static void sec_mouse(void)
{
    MSG m;
    char t[16];
    POINT p;
    p.x = 100;
    p.y = 100;
    ClientToScreen(hMain, &p);
    mx = (WORD)MulDiv(p.x, 32767, GetSystemMetrics(SM_CXSCREEN)) * 2;
    my = (WORD)MulDiv(p.y, 32767, GetSystemMetrics(SM_CYSCREEN)) * 2;
    call_mouse_event(0x8001, mx, my, 0);
    drain();
    rec[0] = 0;
    hk = SetWindowsHookEx(WH_MOUSE, (HOOKPROC)mouse_proc, hi, GetCurrentTask());
    click();
    while (PeekMessage(&m, 0, WM_MOUSEFIRST, WM_MOUSELAST, PM_REMOVE)) {
        wsprintf(t, "=%x", m.message);
        rec_add(t);
    }
    obs_str("mouse", rec);
    rec[0] = 0;
    deny = 2;
    pump(600);                          /* no double click */
    click();
    while (PeekMessage(&m, 0, WM_MOUSEFIRST, WM_MOUSELAST, PM_REMOVE)) {
        wsprintf(t, "=%x", m.message);
        rec_add(t);
    }
    obs_str("mouse.deny", rec);
    deny = -1;
    UnhookWindowsHookEx(hk);
}

/* ---- WH_MSGFILTER ---- */
static int last_code;
LRESULT FAR PASCAL __export __loadds filter_proc(int code, WPARAM w, LPARAM l)
{
    char t[24];
    MSG FAR *m = (MSG FAR *)l;
    (void)w;
    /* each new code, and the keys it sees */
    if (code != last_code || m->message == WM_KEYDOWN) {
        if (m->message == WM_KEYDOWN)
            wsprintf(t, "F%d:%x", code, m->wParam);
        else
            wsprintf(t, "F%d", code);
        rec_add(t);
        last_code = code;
    }
    return CallNextHookEx(hk, code, w, l);
}

void FAR PASCAL __export __loadds esc_timer(HWND h, UINT msg, UINT id, DWORD t)
{
    (void)h, (void)msg, (void)t;
    KillTimer(0, id);
    call_keybd_event(0x001B, 0x0001);
    call_keybd_event(0x801B, 0x0001);
}

static void esc_later(void)
{
    SetTimer(0, 0, 300, (TIMERPROC)esc_timer);
}

BOOL FAR PASCAL __export __loadds dlg_proc(HWND h, UINT m, WPARAM w, LPARAM l)
{
    (void)l;
    if (m == WM_INITDIALOG)
        return TRUE;
    if (m == WM_COMMAND && (w == IDCANCEL || w == IDOK)) {
        EndDialog(h, w);
        return TRUE;
    }
    return FALSE;
}

static void filter_run(const char *name, int what)
{
    rec[0] = 0;
    last_code = -100;
    SetActiveWindow(hMain);
    drain();
    hk = SetWindowsHookEx(WH_MSGFILTER, (HOOKPROC)filter_proc, hi, GetCurrentTask());
    /* the move and size loops dispatch no timers on 3.1: the Escape is
       queued first */
    if (what == 3 || what == 4) {
        call_keybd_event(0x001B, 0x0001);
        call_keybd_event(0x801B, 0x0001);
    } else
        esc_later();
    switch (what) {
    case 0:
        DialogBox(hi, "US30DLG", hMain, (DLGPROC)dlg_proc);
        break;
    case 1:
        MessageBox(hMain, "US30", "US30", MB_OK);
        break;
    case 2: {
        HMENU pm = CreatePopupMenu();
        AppendMenu(pm, MF_STRING, 1, "One");
        AppendMenu(pm, MF_STRING, 2, "Two");
        TrackPopupMenu(pm, 0, 50, 50, 0, hMain, NULL);
        DestroyMenu(pm);
        break;
    }
    case 3:
        SendMessage(hMain, WM_SYSCOMMAND, SC_MOVE, 0);
        break;
    case 4:
        SendMessage(ha, WM_SYSCOMMAND, SC_SIZE, 0);
        break;
    case 5:
        /* the menu bar from the keyboard: Alt, then Escape twice */
        call_keybd_event(0x0012, 0x0038);
        call_keybd_event(0x8012, 0x0038);
        pump(200);
        break;
    }
    UnhookWindowsHookEx(hk);
    pump(100);
    obs_str(name, rec);
}

static void sec_filter(void)
{
    ha = CreateWindow("US30A", "a", WS_OVERLAPPEDWINDOW | WS_VISIBLE, 10, 10, 200, 150,
                      0, 0, hi, NULL);
    filter_run("filter.dialog", 0);
    filter_run("filter.msgbox", 1);
    filter_run("filter.popup", 2);
    filter_run("filter.move", 3);
    SetActiveWindow(ha);
    filter_run("filter.size", 4);
    DestroyWindow(ha);
    ha = 0;
}

/* ---- WH_JOURNALRECORD ---- */
LRESULT FAR PASCAL __export __loadds jr_proc(int code, WPARAM w, LPARAM l)
{
    char t[24];
    if (code == HC_ACTION)
        wsprintf(t, "J%d:%x.%x", code, ((LPEVENTMSG)l)->message, LOBYTE(((LPEVENTMSG)l)->paramL));
    else
        wsprintf(t, "J%d", code);
    rec_add(t);
    return CallNextHookEx(hk, code, w, l);
}


static void sec_jr(void)
{
    MSG m;
    SetActiveWindow(hMain);
    SetFocus(hMain);
    drain();
    rec[0] = 0;
    hk = SetWindowsHookEx(WH_JOURNALRECORD, (HOOKPROC)jr_proc, hi, 0);
    obs("jr.set", hk != 0);
    keys();
    rec_add("|get");
    while (PeekMessage(&m, 0, WM_KEYFIRST, WM_KEYLAST, PM_REMOVE))
        ;
    UnhookWindowsHookEx(hk);
    obs_str("jr", rec);

}

static void reg(const char *name, WNDPROC p)
{
    WNDCLASS wc;
    wc.style = 0;
    wc.lpfnWndProc = p;
    wc.cbClsExtra = wc.cbWndExtra = 0;
    wc.hInstance = hi;
    wc.hIcon = 0;
    wc.hCursor = LoadCursor(NULL, IDC_ARROW);
    wc.hbrBackground = (HBRUSH)(COLOR_WINDOW + 1);
    wc.lpszMenuName = NULL;
    wc.lpszClassName = name;
    RegisterClass(&wc);
}

int __cdecl tmain(void)
{
    hi = (HINSTANCE)init_regs[5];
    WaitEvent(0);
    InitApp(hi);
    out("US30 start\r\n");
    reg("US30M", DefWindowProc);
    reg("US30A", DefWindowProc);
    reg("US30B", DefWindowProc);
    hMain = CreateWindow("US30M", "US30", WS_OVERLAPPEDWINDOW, 0, 0, 400, 300, 0, 0, hi, NULL);
    ShowWindow(hMain, SW_SHOWNORMAL);
    UpdateWindow(hMain);
    pump(300);
    out("cbt\r\n");
    sec_cbt();
    out("kbd\r\n");
    sec_kbd();
    out("mouse\r\n");
    sec_mouse();
    out("filter\r\n");
    sec_filter();
    out("jr\r\n");
    sec_jr();
    /* WH_DEBUG is not probed: 3.1 died at the first GetMessage with a
       task-local one set */
    out("US30 end\r\n");
    DestroyWindow(hMain);
    return 0;
}
