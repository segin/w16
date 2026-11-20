/*
 * us31.c - probe US31: what a window's owner gets, and what other tasks
 * do, while the window is dragged by its caption (DefWindowProc's
 * SC_MOVE, user/track.c) under real mouse input.
 *
 * Found with QuickTime's Movie Player (docs/notes/quicktime-handover.md,
 * "Dragging the movie"): QTIM.DLL keeps a system-wide WH_GETMESSAGE hook
 * that, every time it is called, takes its own registered message out of
 * the queue (PeekMessage of that message only, PM_REMOVE | PM_NOYIELD)
 * and posts it again to the player's task (PostAppMessage, hwnd 0), so
 * the player's queue is never without a posted message. On w16 the drag
 * then never saw the mouse again and never let another task run.
 *
 * The probe (run as the shell: tests/dropin.sh ... -i boot:shell=C:\US31.EXE,
 * driven by tests/scripts/input/us31.txt) starts C:\US31B.EXE, this source
 * built with US31_OTHER (the "other task": a small window with a 55 ms
 * timer counting its ticks; a second module, not a second instance, whose
 * __loadds callbacks would use the first instance's data)
 * and shows its own window at 40,40, 240 x 160, with a 100 ms timer that
 * invalidates the window and posts WM_USER+5 to it. The script drags the
 * window by its caption three times: the first plainly; during the second
 * the probe runs QTIM's pump: a task WH_GETMESSAGE hook doing the same
 * Peek and PostAppMessage (message WM_USER+0x300); the third is cancelled
 * with Esc. For each drag it logs:
 *   OBS dN.x / dN.y            the window's place after the drop
 *   OBS dN.<what>              0, 1 (once) or 2 (more than once): how
 *       often during the drag the window procedure got WM_TIMER, WM_PAINT,
 *       WM_USER+5, WM_MOUSEMOVE, WM_NCMOUSEMOVE, WM_MOVE, WM_GETMINMAXINFO,
 *       WM_ENTERIDLE, and the hook saw WM_MOUSEMOVE, WM_TIMER, WM_PAINT,
 *       WM_USER+5 and the pump's message; the other task's timer ticks;
 *       WH_MSGFILTER and WH_SYSMSGFILTER calls; messages 231h and 232h
 *       (Win32's WM_ENTERSIZEMOVE and WM_EXITSIZEMOVE; 3.1 sends them,
 *       undocumented) and their wParam and lParam (the lParam's words
 *       ORed)
 *   dN other ticks / ms       the other task's timer ticks during the
 *       drag and its length, not compared
 *   dN wnd: / dN hook: / dN filter:   the window procedure's and the
 *       hook's messages in order (runs of one message counted), not
 *       compared
 * then ends Windows (the other copy is closed first).
 */
#include <windows.h>

#define TK_REF "us31ref.h"
#include "tklog.h"

extern WORD FAR PASCAL WaitEvent(HTASK);
extern BOOL FAR PASCAL InitApp(HINSTANCE);

#define WM_PUMP (WM_USER + 0x300)
#define WM_MINE (WM_USER + 5)
#define WM_TICKS (WM_USER + 9)
#define WM_PAINTS (WM_USER + 10)

static HINSTANCE hi;
static HWND hMain, hOther;
static HHOOK hGet, hFilt, hSysFilt;
static int pump, indrag, drags, inhook;
static WPARAM sizemove_wp[2];
static LPARAM sizemove_lp[2];

/* runs of one message, in order */
typedef struct { WORD msg; WORD n; } run;
#define NRUN 48
static run wnd[NRUN], hook[NRUN], filt[NRUN];
static int nwnd, nhook, nfilt;

static void add(run *r, int *n, WORD msg)
{
    if (*n && r[*n - 1].msg == msg) {
        r[*n - 1].n++;
        return;
    }
    if (*n < NRUN) {
        r[*n].msg = msg;
        r[*n].n = 1;
        (*n)++;
    } else
        r[NRUN - 1].msg = 0xFFFF;       /* more than fits */
}

static WORD count(run *r, int n, WORD msg)
{
    WORD c = 0;
    int i;
    for (i = 0; i < n; i++)
        if (r[i].msg == msg)
            c += r[i].n;
    return c;
}

static void runs(const char *name, run *r, int n)
{
    char b[24];
    int i;
    out(name);
    for (i = 0; i < n; i++) {
        wsprintf(b, " %X", r[i].msg);
        out(b);
        if (r[i].n > 1) {
            wsprintf(b, "x%u", r[i].n);
            out(b);
        }
    }
    out("\r\n");
}

static void obs3(int d, const char *what, DWORD n)
{
    char b[40];
    wsprintf(b, "d%d.%s", d, (LPSTR)what);
    obs(b, n == 0 ? 0 : n == 1 ? 1 : 2);
}

LRESULT FAR PASCAL __export __loadds get_hook(int code, WPARAM wp, LPARAM lp)
{
    if (code >= 0) {
        MSG FAR *m = (MSG FAR *)lp;
        MSG pm;
        if (indrag)
            add(hook, &nhook, m->message);
        if (indrag && m->message == WM_MOUSEMOVE) {
            /* the dragging task drawing meanwhile (GetDC): shown? */
            static WORD n;
            char b[16];
            HDC dc = GetDC(hMain);
            wsprintf(b, "%5u", ++n);
            TextOut(dc, 4, 24, b, lstrlen(b));
            ReleaseDC(hMain, dc);
        }
        if (pump && !inhook) {
            /* QTIM's pump, as seen from outside (trace=): its own message
               taken out, then posted again to the task (the Peek calls
               the hook again: not pumped there) */
            inhook = 1;
            PeekMessage(&pm, 0, WM_PUMP, WM_PUMP, PM_REMOVE | PM_NOYIELD);
            PostAppMessage(GetCurrentTask(), WM_PUMP, 0, GetTickCount());
            inhook = 0;
        }
    }
    return CallNextHookEx(hGet, code, wp, lp);
}

LRESULT FAR PASCAL __export __loadds filt_hook(int code, WPARAM wp, LPARAM lp)
{
    if (indrag && code >= 0)
        add(filt, &nfilt, (WORD)code);
    return CallNextHookEx(hFilt, code, wp, lp);
}

LRESULT FAR PASCAL __export __loadds sysfilt_hook(int code, WPARAM wp, LPARAM lp)
{
    if (indrag && code >= 0)
        add(filt, &nfilt, 0x8000 | (WORD)code);
    return CallNextHookEx(hSysFilt, code, wp, lp);
}

static void report(int d, DWORD t0, DWORD t1, DWORD ms)
{
    RECT r;
    char b[80];
    GetWindowRect(hMain, &r);
    wsprintf(b, "d%d.x", d);
    obs(b, r.left);
    wsprintf(b, "d%d.y", d);
    obs(b, r.top);
    obs3(d, "wnd.timer", count(wnd, nwnd, WM_TIMER));
    obs3(d, "wnd.paint", count(wnd, nwnd, WM_PAINT));
    obs3(d, "wnd.mine", count(wnd, nwnd, WM_MINE));
    obs3(d, "wnd.mousemove", count(wnd, nwnd, WM_MOUSEMOVE));
    obs3(d, "wnd.ncmousemove", count(wnd, nwnd, WM_NCMOUSEMOVE));
    obs3(d, "wnd.move", count(wnd, nwnd, WM_MOVE));
    obs3(d, "wnd.getminmaxinfo", count(wnd, nwnd, WM_GETMINMAXINFO));
    obs3(d, "wnd.enteridle", count(wnd, nwnd, WM_ENTERIDLE));
    obs3(d, "hook.mousemove", count(hook, nhook, WM_MOUSEMOVE));
    obs3(d, "hook.timer", count(hook, nhook, WM_TIMER));
    obs3(d, "hook.paint", count(hook, nhook, WM_PAINT));
    obs3(d, "hook.mine", count(hook, nhook, WM_MINE));
    obs3(d, "hook.pump", count(hook, nhook, WM_PUMP));
    obs3(d, "filter", nfilt);
    obs3(d, "other", t1 - t0);
    obs3(d, "wnd.231", count(wnd, nwnd, 0x231));
    obs3(d, "wnd.232", count(wnd, nwnd, 0x232));
    wsprintf(b, "d%d.231.wp", d);
    obs(b, sizemove_wp[0]);
    wsprintf(b, "d%d.231.lp", d);
    obs(b, LOWORD(sizemove_lp[0]) | HIWORD(sizemove_lp[0]));
    wsprintf(b, "d%d.232.wp", d);
    obs(b, sizemove_wp[1]);
    wsprintf(b, "d%d.232.lp", d);
    obs(b, LOWORD(sizemove_lp[1]) | HIWORD(sizemove_lp[1]));
    sizemove_wp[0] = sizemove_wp[1] = 0;
    sizemove_lp[0] = sizemove_lp[1] = 0;
    /* not compared: timing */
    wsprintf(b, "d%d other ticks %lu in %lu ms\r\n", d, t1 - t0, ms);
    out(b);
    wsprintf(b, "d%d wnd:", d);
    runs(b, wnd, nwnd);
    wsprintf(b, "d%d hook:", d);
    runs(b, hook, nhook);
    wsprintf(b, "d%d filter:", d);
    runs(b, filt, nfilt);
    nwnd = nhook = nfilt = 0;
}

LRESULT FAR PASCAL __export __loadds main_proc(HWND h, UINT m, WPARAM w, LPARAM l)
{
    if (indrag)
        add(wnd, &nwnd, m);
    if (m == 0x231 || m == 0x232) {
        sizemove_wp[m - 0x231] = w;
        sizemove_lp[m - 0x231] = l;
    }
    switch (m) {
    case WM_TIMER:
        if (w == 2) {
            KillTimer(h, 2);
            PostQuitMessage(0);
            return 0;
        }
        InvalidateRect(h, NULL, FALSE);
        PostMessage(h, WM_MINE, 0, 0);
        return 0;
    case WM_PAINT: {
        PAINTSTRUCT ps;
        static WORD n;
        char b[16];
        BeginPaint(h, &ps);
        wsprintf(b, "%5u", ++n);
        TextOut(ps.hdc, 4, 4, b, lstrlen(b));
        EndPaint(h, &ps);
        return 0;
    }
    case WM_SYSCOMMAND:
        if ((w & 0xFFF0) == SC_MOVE) {
            DWORD t0, t1, p0, p1, ms;
            LRESULT r;
            MSG pm;
            int d = ++drags;
            nwnd = nhook = nfilt = 0;
            t0 = SendMessage(hOther, WM_TICKS, 0, 0);
            p0 = SendMessage(hOther, WM_PAINTS, 0, 0);
            if (d == 2) {
                /* the second drag with QTIM's pump going (only during the
                   drag: it keeps any message loop from idling) */
                pump = 1;
                PostAppMessage(GetCurrentTask(), WM_PUMP, 0, 0);
            }
            indrag = 1;
            ms = GetTickCount();
            r = DefWindowProc(h, m, w, l);
            ms = GetTickCount() - ms;
            indrag = 0;
            pump = 0;
            while (PeekMessage(&pm, 0, WM_PUMP, WM_PUMP, PM_REMOVE | PM_NOYIELD))
                ;
            t1 = SendMessage(hOther, WM_TICKS, 0, 0);
            p1 = SendMessage(hOther, WM_PAINTS, 0, 0);
            report(d, t0, t1, ms);
            {
                char b[48];
                wsprintf(b, "d%d other paints %lu\r\n", d, p1 - p0);
                out(b);
            }
            if (d == 3)
                SetTimer(h, 2, 3000, NULL);
            return r;
        }
        break;
    }
    return DefWindowProc(h, m, w, l);
}

#ifdef US31_OTHER
static DWORD ticks, paints;

LRESULT FAR PASCAL __export __loadds other_proc(HWND h, UINT m, WPARAM w, LPARAM l)
{
    switch (m) {
    case WM_TIMER: {
        /* the count drawn at once (GetDC) on the second line, and again
           on the first when WM_PAINT comes */
        HDC dc = GetDC(h);
        char b[16];
        ticks++;
        wsprintf(b, "%5lu", ticks);
        TextOut(dc, 4, 24, b, lstrlen(b));
        ReleaseDC(h, dc);
        /* and on the screen under it (the desktop's DC) */
        dc = GetDC(0);
        TextOut(dc, 424, 440, b, lstrlen(b));
        ReleaseDC(0, dc);
        InvalidateRect(h, NULL, FALSE);
        return 0;
    }
    case WM_PAINT: {
        PAINTSTRUCT ps;
        char b[16];
        paints++;
        BeginPaint(h, &ps);
        wsprintf(b, "%5lu", ticks);
        TextOut(ps.hdc, 4, 4, b, lstrlen(b));
        EndPaint(h, &ps);
        return 0;
    }
    case WM_TICKS:
        return ticks;
    case WM_PAINTS:
        return paints;
    case WM_DESTROY:
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProc(h, m, w, l);
}

static int other(void)
{
    MSG m;
    WNDCLASS wc;
    wc.style = 0;
    wc.lpfnWndProc = other_proc;
    wc.cbClsExtra = wc.cbWndExtra = 0;
    wc.hInstance = hi;
    wc.hIcon = LoadIcon(NULL, IDI_APPLICATION);
    wc.hCursor = LoadCursor(NULL, IDC_ARROW);
    wc.hbrBackground = (HBRUSH)(COLOR_WINDOW + 1);
    wc.lpszMenuName = NULL;
    wc.lpszClassName = "US31K";
    RegisterClass(&wc);
    hOther = CreateWindow("US31K", "other", WS_OVERLAPPED | WS_CAPTION, 420, 320, 160, 100,
                          0, 0, hi, NULL);
    ShowWindow(hOther, SW_SHOWNOACTIVATE);
    SetTimer(hOther, 1, 55, NULL);
    while (GetMessage(&m, 0, 0, 0)) {
        TranslateMessage(&m);
        DispatchMessage(&m);
    }
    return 0;
}
#endif

#ifdef US31_OTHER
int __cdecl tmain(void)
{
    hi = (HINSTANCE)init_regs[5];
    WaitEvent(0);
    InitApp(hi);
    return other();
}
#else
int __cdecl tmain(void)
{
    WNDCLASS wc;
    MSG m;
    DWORD t;

    hi = (HINSTANCE)init_regs[5];
    WaitEvent(0);
    InitApp(hi);
    out("US31 start\r\n");
    WinExec("C:\\US31B.EXE", SW_SHOWNOACTIVATE);
    for (t = GetTickCount(); !(hOther = FindWindow("US31K", NULL)) && GetTickCount() - t < 10000;)
        if (PeekMessage(&m, 0, 0, 0, PM_REMOVE))
            DispatchMessage(&m);
    obs("other", hOther != 0);

    wc.style = 0;
    wc.lpfnWndProc = main_proc;
    wc.cbClsExtra = wc.cbWndExtra = 0;
    wc.hInstance = hi;
    wc.hIcon = LoadIcon(NULL, IDI_APPLICATION);
    wc.hCursor = LoadCursor(NULL, IDC_ARROW);
    wc.hbrBackground = (HBRUSH)(COLOR_WINDOW + 1);
    wc.lpszMenuName = NULL;
    wc.lpszClassName = "US31";
    RegisterClass(&wc);
    hMain = CreateWindow("US31", "US31", WS_OVERLAPPEDWINDOW, 40, 40, 240, 160, 0, 0, hi, NULL);
    hGet = SetWindowsHookEx(WH_GETMESSAGE, (HOOKPROC)get_hook, hi, GetCurrentTask());
    hFilt = SetWindowsHookEx(WH_MSGFILTER, (HOOKPROC)filt_hook, hi, GetCurrentTask());
    hSysFilt = SetWindowsHookEx(WH_SYSMSGFILTER, (HOOKPROC)sysfilt_hook, hi, 0);
    ShowWindow(hMain, SW_SHOWNORMAL);
    UpdateWindow(hMain);
    SetTimer(hMain, 1, 100, NULL);
    while (GetMessage(&m, 0, 0, 0)) {
        TranslateMessage(&m);
        DispatchMessage(&m);
    }
    UnhookWindowsHookEx(hGet);
    UnhookWindowsHookEx(hFilt);
    UnhookWindowsHookEx(hSysFilt);
    obs("drags", drags);
    if (hOther)
        SendMessage(hOther, WM_CLOSE, 0, 0);
    out("US31 done\r\n");
    DestroyWindow(hMain);
    return 0;
}
#endif
