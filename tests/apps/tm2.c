/*
 * tm2.c - probe TM2: which windows the Task List lists, and what its
 * buttons send them (ARCHITECTURE.md 15.5).
 *
 * Makes top-level windows of one class, each with a title that says what
 * it is, and logs to C:\TM2.LOG the messages each gets that tell what
 * the Task List did to it (activation, WM_CLOSE, WM_SYSCOMMAND, showing,
 * moving and sizing, WM_QUERYOPEN). The windows (x,y w x h on 640x480):
 *   A "TM2 Plain"     overlapped, visible
 *   B "TM2 Hidden"    overlapped, never shown
 *   C "TM2 Owned"     overlapped, visible, owned by A
 *   D ""              overlapped, visible, no title
 *   E "TM2 Popup"     WS_POPUP | WS_BORDER, visible, no caption
 *   F "TM2 Disabled"  overlapped, visible, disabled
 *   G "TM2 Iconic"    overlapped, minimised
 *   H "TM2 Modal"     overlapped, visible; owns "TM2 Modal Child", a
 *                     visible popup with a caption, and is disabled while
 *                     it is up (as a modal dialog does)
 *   K "TM2 Keep"      overlapped, visible; answers WM_CLOSE without
 *                     closing
 * With "/1" on the command line only A, G and K are made. Every second,
 * when any visible top-level window has moved, sized or changed Z order,
 * all of them are logged ("windows:", then handle, rectangle, iconic,
 * owner, title in Z order): Cascade, Tile and Arrange Icons' results.
 * WM_CLOSE (but K's) goes to DefWindowProc (DestroyWindow); the probe ends
 * when A is destroyed.
 */
#include <windows.h>

static HFILE logf = HFILE_ERROR;
static HWND wa, wk;

static void out(const char FAR *s)
{
    logf = _lopen("C:\\TM2.LOG", OF_WRITE);
    if (logf == HFILE_ERROR)
        logf = _lcreat("C:\\TM2.LOG", 0);
    else
        _llseek(logf, 0, 2);
    if (logf != HFILE_ERROR) {
        _lwrite(logf, s, lstrlen(s));
        _lclose(logf);
    }
}

static const char *msgname(UINT m)
{
    switch (m) {
    case WM_ACTIVATE: return "ACTIVATE";
    case WM_ACTIVATEAPP: return "ACTIVATEAPP";
    case WM_SETFOCUS: return "SETFOCUS";
    case WM_KILLFOCUS: return "KILLFOCUS";
    case WM_CLOSE: return "CLOSE";
    case WM_SYSCOMMAND: return "SYSCOMMAND";
    case WM_COMMAND: return "COMMAND";
    case WM_SHOWWINDOW: return "SHOWWINDOW";
    case WM_QUERYOPEN: return "QUERYOPEN";
    case WM_WINDOWPOSCHANGED: return "POSCHANGED";
    case WM_DESTROY: return "DESTROY";
    case WM_QUERYENDSESSION: return "QUERYENDSESSION";
    case WM_ENDSESSION: return "ENDSESSION";
    case WM_NCACTIVATE: return "NCACTIVATE";
    case WM_QUIT: return "QUIT";
    case WM_USER: return "USER";
    }
    return 0;
}

/* every second: when the top-level windows' places have changed, log
   them all (Z order, visible ones: handle, rectangle, iconic, title) */
static DWORD lastsum;
static void dump(void)
{
    HWND h;
    RECT r;
    DWORD sum = 0;
    char b[120], t[40];
    for (h = GetWindow(GetDesktopWindow(), GW_CHILD); h; h = GetWindow(h, GW_HWNDNEXT))
        if (IsWindowVisible(h)) {
            GetWindowRect(h, &r);
            sum = sum * 31 + (WORD)h + r.left * 7 + r.top * 131 + r.right * 1031 + r.bottom * 8191;
        }
    if (sum == lastsum)
        return;
    lastsum = sum;
    out("windows:\r\n");
    for (h = GetWindow(GetDesktopWindow(), GW_CHILD); h; h = GetWindow(h, GW_HWNDNEXT))
        if (IsWindowVisible(h)) {
            GetWindowRect(h, &r);
            t[0] = 0;
            GetWindowText(h, t, sizeof t);
            wsprintf(b, "  %04X %d,%d %dx%d ico=%d own=%04X \"%s\"\r\n", h, r.left, r.top,
                     r.right - r.left, r.bottom - r.top, IsIconic(h) ? 1 : 0, GetWindow(h, GW_OWNER), (LPSTR)t);
            out(b);
        }
}

LRESULT FAR PASCAL __export __loadds tm2_proc(HWND h, UINT m, WPARAM wp, LPARAM lp)
{
    const char *n = msgname(m);
    if (n) {
        char b[160], t[40];
        t[0] = 0;
        GetWindowText(h, t, sizeof t);
        if (m == WM_WINDOWPOSCHANGED) {
            WINDOWPOS FAR *p = (WINDOWPOS FAR *)lp;
            wsprintf(b, "%08lX \"%s\" %s after=%04X %d,%d %dx%d fl=%04X\r\n", GetTickCount(), (LPSTR)t,
                     (LPSTR)n, p->hwndInsertAfter, p->x, p->y, p->cx, p->cy, p->flags);
        } else
            wsprintf(b, "%08lX \"%s\" %s %04X %08lX\r\n", GetTickCount(), (LPSTR)t, (LPSTR)n, wp, lp);
        out(b);
    }
    switch (m) {
    case WM_TIMER:
        dump();
        return 0;
    case WM_CLOSE:
        if (h == wk)
            return 0;
        break;
    case WM_DESTROY:
        if (h == wa)
            PostQuitMessage(0);
        break;
    }
    return DefWindowProc(h, m, wp, lp);
}

static HWND mk(LPCSTR t, DWORD st, int x, int y, int cx, int cy, HWND owner, HINSTANCE hi)
{
    return CreateWindow("TM2", t, st, x, y, cx, cy, owner, 0, hi, 0);
}

int PASCAL WinMain(HINSTANCE hi, HINSTANCE hp, LPSTR cmd, int show)
{
    WNDCLASS wc;
    MSG m;
    HWND h, c;
    BOOL one = cmd[0] == '/' && cmd[1] == '1';
    (void)hp, (void)show;
    out("TM2 start\r\n");
    wc.style = 0;
    wc.lpfnWndProc = tm2_proc;
    wc.cbClsExtra = wc.cbWndExtra = 0;
    wc.hInstance = hi;
    wc.hIcon = LoadIcon(0, IDI_APPLICATION);
    wc.hCursor = LoadCursor(0, IDC_ARROW);
    wc.hbrBackground = (HBRUSH)(COLOR_WINDOW + 1);
    wc.lpszMenuName = 0;
    wc.lpszClassName = "TM2";
    if (!RegisterClass(&wc))
        return 1;
    wa = mk("TM2 Plain", WS_OVERLAPPEDWINDOW | WS_VISIBLE, 10, 10, 200, 100, 0, hi);
    if (!one) {
        mk("TM2 Hidden", WS_OVERLAPPEDWINDOW, 220, 10, 200, 100, 0, hi);
        mk("TM2 Owned", WS_OVERLAPPEDWINDOW | WS_VISIBLE, 430, 10, 200, 100, wa, hi);
        mk("", WS_OVERLAPPEDWINDOW | WS_VISIBLE, 10, 120, 200, 100, 0, hi);
        mk("TM2 Popup", WS_POPUP | WS_BORDER | WS_VISIBLE, 220, 120, 200, 100, 0, hi);
        h = mk("TM2 Disabled", WS_OVERLAPPEDWINDOW | WS_VISIBLE, 430, 120, 200, 100, 0, hi);
        EnableWindow(h, FALSE);
    }
    h = mk("TM2 Iconic", WS_OVERLAPPEDWINDOW, 10, 230, 200, 100, 0, hi);
    ShowWindow(h, SW_SHOWMINNOACTIVE);
    if (!one) {
        h = mk("TM2 Modal", WS_OVERLAPPEDWINDOW | WS_VISIBLE, 220, 230, 200, 100, 0, hi);
        c = mk("TM2 Modal Child", WS_POPUP | WS_CAPTION | WS_SYSMENU | WS_VISIBLE, 260, 260, 150, 60, h, hi);
        EnableWindow(h, FALSE);
        (void)c;
    }
    wk = mk("TM2 Keep", WS_OVERLAPPEDWINDOW | WS_VISIBLE, 430, 230, 200, 100, 0, hi);
    out("TM2 ready\r\n");
    SetTimer(wa, 1, 1000, 0);
    while (GetMessage(&m, 0, 0, 0)) {
        TranslateMessage(&m);
        DispatchMessage(&m);
    }
    out("TM2 end\r\n");
    return 0;
}
