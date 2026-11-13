/*
 * us19.c - probe US19: dragging a scroll bar's thumb with real mouse
 * input (tests/inputrun.sh drives the pointer; boot-test case sbdrag).
 *
 * A window with both scroll bars and two SCROLLBAR controls, all with
 * the range 0-100 at 50. Two of the four act on SB_THUMBTRACK (the
 * window's horizontal bar and the vertical control call SetScrollPos with
 * each tracked position); the other two move only on SB_THUMBPOSITION,
 * so that the screenshots taken mid-drag show both kinds of program. The
 * window logs to TK.LOG every WM_VSCROLL/WM_HSCROLL it gets (as a window
 * or as the controls' parent): "MSG <bar> <code> <pos> <bar's pos before>"
 * with the bar w (the window's), cv or ch, and every left press and
 * release it sees ("CLICK x,y"), so that a run's log is the drag's
 * message sequence; tests/boottest.sh sbdrag compares its MSG lines with
 * Windows 3.1's. The window's vertical bar's last SB_THUMBTRACK position
 * is drawn in the client area ("track N") by WM_PAINT, the handler only
 * invalidating it: whether the window repaints while the thumb is
 * dragged shows in the screens.
 *
 * Run as the shell: -i 'boot:shell=C:\US19.EXE' (tests/dropin.sh).
 */
#include <windows.h>

#define TK_REF "us19ref.h"
#include "tklog.h"

extern WORD FAR PASCAL WaitEvent(HTASK);
extern BOOL FAR PASCAL InitApp(HINSTANCE);

static HWND hMain, hCV, hCH;
static int vtrack = 50;         /* the window's vertical bar's last
                                   SB_THUMBTRACK, shown in the client area */
static RECT rcShow = { 100, 100, 200, 120 };

static const char *barname(WORD ctl)
{
    if (!ctl)
        return "w";
    if ((HWND)ctl == hCV)
        return "cv";
    if ((HWND)ctl == hCH)
        return "ch";
    return "?";
}

LRESULT FAR PASCAL __export __loadds wnd_proc(HWND h, UINT m, WPARAM wp, LPARAM lp)
{
    char b[64];
    switch (m) {
    case WM_VSCROLL:
    case WM_HSCROLL: {
        HWND ctl = (HWND)HIWORD(lp);
        int bar = ctl ? SB_CTL : m == WM_VSCROLL ? SB_VERT : SB_HORZ;
        HWND bw = ctl ? ctl : h;
        /* which bars move with the thumb: the window's horizontal one
           and the vertical control */
        BOOL tracks = ctl ? ctl == hCV : m == WM_HSCROLL;
        int pos = (int)LOWORD(lp);
        POINT pt;
        GetCursorPos(&pt);
        wsprintf(b, "MSG %c%s %d %d %d @%d,%d\r\n", m == WM_VSCROLL ? 'V' : 'H',
                 (LPSTR)barname(HIWORD(lp)), wp, wp == SB_THUMBTRACK || wp == SB_THUMBPOSITION ? pos : 0,
                 GetScrollPos(bw, bar), pt.x, pt.y);
        out(b);
        /* the window's vertical bar: the tracked position drawn by
           WM_PAINT, the area only invalidated here (does a drag let the
           window paint?) */
        if (!ctl && m == WM_VSCROLL && wp == SB_THUMBTRACK) {
            vtrack = pos;
            InvalidateRect(h, &rcShow, TRUE);
        }
        if (wp == SB_THUMBPOSITION || (wp == SB_THUMBTRACK && tracks))
            SetScrollPos(bw, bar, pos, TRUE);
        else if (wp == SB_LINEUP || wp == SB_LINEDOWN || wp == SB_PAGEUP || wp == SB_PAGEDOWN) {
            int p = GetScrollPos(bw, bar);
            p += wp == SB_LINEUP ? -1 : wp == SB_LINEDOWN ? 1 : wp == SB_PAGEUP ? -10 : 10;
            SetScrollPos(bw, bar, p, TRUE);
        }
        return 0;
    }
    case WM_PAINT: {
        PAINTSTRUCT ps;
        HDC hdc = BeginPaint(h, &ps);
        wsprintf(b, "track %d", vtrack);
        TextOut(hdc, rcShow.left, rcShow.top, b, lstrlen(b));
        EndPaint(h, &ps);
        return 0;
    }
    case WM_LBUTTONDOWN:
    case WM_LBUTTONUP:
    case WM_NCLBUTTONDOWN:
    case WM_NCLBUTTONUP: {
        POINT p;
        GetCursorPos(&p);
        wsprintf(b, "CLICK %04X %d,%d\r\n", m, p.x, p.y);
        out(b);
        break;
    }
    }
    return DefWindowProc(h, m, wp, lp);
}

int __cdecl tmain(void)
{
    WNDCLASS wc;
    HINSTANCE tk_hinst = (HINSTANCE)init_regs[5];      /* DI at start-up */
    MSG m;
    RECT r;
    char b[80];
    WaitEvent(0);
    InitApp(tk_hinst);
    out("US19 start\r\n");
    wc.style = 0;
    wc.lpfnWndProc = wnd_proc;
    wc.cbClsExtra = wc.cbWndExtra = 0;
    wc.hInstance = tk_hinst;
    wc.hIcon = 0;
    wc.hCursor = LoadCursor(NULL, IDC_ARROW);
    wc.hbrBackground = (HBRUSH)(COLOR_WINDOW + 1);
    wc.lpszMenuName = NULL;
    wc.lpszClassName = "US19";
    RegisterClass(&wc);
    hMain = CreateWindow("US19", "US19", WS_OVERLAPPEDWINDOW | WS_VSCROLL | WS_HSCROLL | WS_VISIBLE,
                         40, 40, 320, 240, NULL, NULL, tk_hinst, NULL);
    SetScrollRange(hMain, SB_VERT, 0, 100, FALSE);
    SetScrollRange(hMain, SB_HORZ, 0, 100, FALSE);
    SetScrollPos(hMain, SB_VERT, 50, TRUE);
    SetScrollPos(hMain, SB_HORZ, 50, TRUE);
    hCV = CreateWindow("ScrollBar", "", WS_CHILD | WS_VISIBLE | SBS_VERT, 20, 10, 17, 120, hMain,
                       (HMENU)1, tk_hinst, NULL);
    hCH = CreateWindow("ScrollBar", "", WS_CHILD | WS_VISIBLE | SBS_HORZ, 60, 10, 150, 17, hMain,
                       (HMENU)2, tk_hinst, NULL);
    SetScrollRange(hCV, SB_CTL, 0, 100, FALSE);
    SetScrollPos(hCV, SB_CTL, 50, FALSE);
    SetScrollRange(hCH, SB_CTL, 0, 100, FALSE);
    SetScrollPos(hCH, SB_CTL, 50, FALSE);
    UpdateWindow(hMain);
    GetWindowRect(hMain, &r);
    wsprintf(b, "OBS ~window=%d,%d,%d,%d\r\n", r.left, r.top, r.right, r.bottom);
    out(b);
    GetWindowRect(hCV, &r);
    wsprintf(b, "OBS ~cv=%d,%d,%d,%d\r\n", r.left, r.top, r.right, r.bottom);
    out(b);
    GetWindowRect(hCH, &r);
    wsprintf(b, "OBS ~ch=%d,%d,%d,%d\r\n", r.left, r.top, r.right, r.bottom);
    out(b);
    while (GetMessage(&m, 0, 0, 0)) {
        TranslateMessage(&m);
        DispatchMessage(&m);
    }
    out("US19 end\r\n");
    return 0;
}
