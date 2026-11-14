/*
 * us24.c - probe US24: a top-level window created with WS_MINIMIZE (as
 * WINOLDAP's icon window: WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU |
 * WS_MINIMIZEBOX | WS_CLIPSIBLINGS | WS_MINIMIZE, CW_USEDEFAULT, then
 * ShowWindow(SW_SHOWMINNOACTIVE)), and the same with WS_VISIBLE: where
 * the window is, its size, IsIconic and the placement after CreateWindow
 * and after ShowWindow, and the messages it gets (ARCHITECTURE.md 10.3,
 * user/win.c). Then, with an icon and a main window: GetNextQueueWindow's
 * ring from the icon (its title is no stop on it) and which SetWindowPos
 * flags activate the main window. Run as the shell on Windows 3.1 and on
 * w16's USER (tests/refrun.sh build/dos/US24.EXE; REFRUN_FILES=
 * build/dos/USER.EXE=WINDOWS/SYSTEM/USER.EXE) and compare the OBS lines:
 * equal but for window B (3.1 leaves a window made with WS_MINIMIZE |
 * WS_VISIBLE hidden; w16 shows it) and so the third icon's place.
 */
#include <windows.h>

#define TK_REF "us20ref.h"
#include "tklog.h"

extern WORD FAR PASCAL WaitEvent(HTASK);
extern BOOL FAR PASCAL InitApp(HINSTANCE);

static int logging;

static void logm(const char *what, WPARAM wp, LPARAM lp)
{
    char b[80];
    if (!logging)
        return;
    wsprintf(b, "MSG %s %04X %04X %04X\r\n", (LPSTR)what, wp, LOWORD(lp), HIWORD(lp));
    out(b);
}

LRESULT FAR PASCAL __export __loadds wnd_proc(HWND h, UINT m, WPARAM wp, LPARAM lp)
{
    switch (m) {
    case WM_NCCREATE:       logm("NCCREATE", wp, 0); break;
    case WM_CREATE:         logm("CREATE", wp, 0); break;
    case WM_GETMINMAXINFO:  logm("GETMINMAXINFO", wp, 0); break;
    case WM_NCCALCSIZE:     logm("NCCALCSIZE", wp, 0); break;
    case WM_SIZE:           logm("SIZE", wp, lp); break;
    case WM_MOVE:           logm("MOVE", wp, lp); break;
    case WM_SHOWWINDOW:     logm("SHOWWINDOW", wp, lp); break;
    case WM_WINDOWPOSCHANGING: {
        WINDOWPOS FAR *p = (WINDOWPOS FAR *)lp;
        char b[80];
        if (logging) {
            wsprintf(b, "MSG POSCHANGING %d %d %d %d %04X\r\n", p->x, p->y, p->cx, p->cy, p->flags);
            out(b);
        }
        break;
    }
    case WM_WINDOWPOSCHANGED: {
        WINDOWPOS FAR *p = (WINDOWPOS FAR *)lp;
        char b[80];
        if (logging) {
            wsprintf(b, "MSG POSCHANGED %d %d %d %d %04X\r\n", p->x, p->y, p->cx, p->cy, p->flags);
            out(b);
        }
        break;
    }
    case WM_QUERYOPEN:      logm("QUERYOPEN", wp, lp); break;
    case WM_ACTIVATE:       logm("ACTIVATE", wp, lp); break;
    }
    return DefWindowProc(h, m, wp, lp);
}

static void state(const char *tag, HWND w)
{
    RECT r;
    WINDOWPLACEMENT pl;
    char b[160];
    GetWindowRect(w, &r);
    pl.length = sizeof pl;
    GetWindowPlacement(w, &pl);
    wsprintf(b, "OBS %s rect=%d,%d,%d,%d iconic=%d visible=%d style=%08lX show=%d min=%d,%d normal=%d,%d,%d,%d\r\n",
             (LPSTR)tag, r.left, r.top, r.right, r.bottom, IsIconic(w) != 0, IsWindowVisible(w) != 0,
             GetWindowLong(w, GWL_STYLE), pl.showCmd, pl.ptMinPosition.x, pl.ptMinPosition.y,
             pl.rcNormalPosition.left, pl.rcNormalPosition.top,
             pl.rcNormalPosition.right, pl.rcNormalPosition.bottom);
    out(b);
}

int __cdecl tmain(void)
{
    HINSTANCE hi = (HINSTANCE)init_regs[5];
    WNDCLASS wc;
    HWND w, v, m;
    MSG msg;
    WaitEvent(0);
    InitApp(hi);
    out("US24 start\r\n");
    wc.style = 0;
    wc.lpfnWndProc = wnd_proc;
    wc.cbClsExtra = wc.cbWndExtra = 0;
    wc.hInstance = hi;
    wc.hIcon = LoadIcon(NULL, IDI_APPLICATION);
    wc.hCursor = LoadCursor(NULL, IDC_ARROW);
    wc.hbrBackground = (HBRUSH)(COLOR_WINDOW + 1);
    wc.lpszMenuName = NULL;
    wc.lpszClassName = "US24";
    RegisterClass(&wc);
    /* a main window, so that the icons are not the only windows */
    m = CreateWindow("US24", "US24 main", WS_OVERLAPPEDWINDOW | WS_VISIBLE, 100, 50, 300, 200,
                     NULL, NULL, hi, NULL);
    state("main", m);
    out("A: WS_MINIMIZE, hidden\r\n");
    logging = 1;
    w = CreateWindow("US24", "A", WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX |
                     WS_CLIPSIBLINGS | WS_MINIMIZE, CW_USEDEFAULT, 0, CW_USEDEFAULT, 0,
                     NULL, NULL, hi, NULL);
    logging = 0;
    state("A-created", w);
    logging = 1;
    ShowWindow(w, SW_SHOWMINNOACTIVE);
    logging = 0;
    state("A-shown", w);
    out("B: WS_MINIMIZE | WS_VISIBLE\r\n");
    logging = 1;
    v = CreateWindow("US24", "B", WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX |
                     WS_CLIPSIBLINGS | WS_MINIMIZE | WS_VISIBLE, CW_USEDEFAULT, 0, CW_USEDEFAULT, 0,
                     NULL, NULL, hi, NULL);
    logging = 0;
    state("B-created", v);
    out("C: WS_MINIMIZE, hidden, fixed position\r\n");
    {
        HWND c;
        logging = 1;
        c = CreateWindow("US24", "C", WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX |
                         WS_CLIPSIBLINGS | WS_MINIMIZE, 40, 30, 200, 100,
                         NULL, NULL, hi, NULL);
        logging = 0;
        state("C-created", c);
        logging = 1;
        ShowWindow(c, SW_SHOWMINNOACTIVE);
        logging = 0;
        state("C-shown", c);
        logging = 1;
        ShowWindow(c, SW_RESTORE);
        logging = 0;
        state("C-restored", c);
        DestroyWindow(c);
    }
    /* D: with A an icon (its title under it) and the main window:
       GetNextQueueWindow's ring from A (an icon's title is no stop on it?),
       and SetWindowPos on the inactive main window without
       SWP_NOACTIVATE (Microsoft's WINOLDAP comes back to Windows that
       way: SWP_SHOWWINDOW | SWP_NOMOVE | SWP_NOSIZE on a window shown) */
    DestroyWindow(v);
    v = 0;
    {
        typedef HWND (FAR PASCAL *GNQ)(HWND, int);
        GNQ gnq = (GNQ)GetProcAddress(GetModuleHandle("USER"), MAKEINTRESOURCE(274));
        HWND t = w;
        int i;
        char b[80], cn[20];
        for (i = 0; i < 4; i++) {
            t = gnq(t, 1);
            cn[0] = 0;
            if (t)
                GetClassName(t, cn, sizeof cn);
            wsprintf(b, "OBS gnq-%d=%s\r\n", i, (LPSTR)(t == w ? "A" : t == m ? "main" : t ? (LPSTR)cn : "0"));
            out(b);
        }
        SetActiveWindow(w);
        SetWindowPos(m, 0, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_SHOWWINDOW);
        obs("swp-show-activates", GetActiveWindow() == m);
        SetActiveWindow(w);
        SetWindowPos(m, 0, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER);
        obs("swp-nozorder-activates", GetActiveWindow() == m);
        SetActiveWindow(w);
        SetWindowPos(m, 0, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE);
        obs("swp-plain-activates", GetActiveWindow() == m);
        SetActiveWindow(w);
        SetWindowPos(m, 0, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
        obs("swp-noactivate-activates", GetActiveWindow() == m);
        SetActiveWindow(w);
        obs("icon-active", GetActiveWindow() == w);
    }
    logging = 1;
    ShowWindow(w, SW_RESTORE);
    logging = 0;
    state("A-restored", w);
    while (PeekMessage(&msg, NULL, 0, 0, PM_REMOVE))
        DispatchMessage(&msg);
    DestroyWindow(w);
    DestroyWindow(v);
    DestroyWindow(m);
    out("US24 end\r\n");
    return 0;
}
