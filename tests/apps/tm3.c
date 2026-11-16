/*
 * tm3.c - probe TM3: what TileChildWindows, CascadeChildWindows and
 * ArrangeIconicWindows do to the desktop's windows, the Task List's Tile,
 * Cascade and Arrange Icons (ARCHITECTURE.md 15.5).
 *
 * Run as the shell (tests/refrun.sh on Windows 3.1; tests/userboot.sh -s
 * on w16's USER), so that only its own windows are on the desktop. For
 * each case it makes top-level windows (each 200x150 at 10 + 15i, 10 +
 * 10i, WS_OVERLAPPEDWINDOW unless the case says otherwise, made in order
 * so that the last is on top), calls the function and logs to TK.LOG
 * every visible window of its own, in the order they were made:
 *     <case> <i>:x,y,cx,cy[,I icon][,Z zoomed]
 * and the function's AX. Cases:
 *   n1..n10   N plain windows: tile 0, tile 1 (MDITILE_HORIZONTAL),
 *             cascade 0
 *   ic        5 windows, the top one (active) minimised: tile 0, tile 1,
 *             cascade 0 (a window that is not active minimised: see
 *             ARCHITECTURE.md 15.5)
 *   mx        4 windows, the top one maximised: tile 0; again, cascade 0
 *   mix       5 windows: the second WS_POPUP | WS_CAPTION | WS_SYSMENU
 *             (not sizable), the fourth disabled: tile 0, tile 2
 *             (MDITILE_SKIPDISABLED), cascade 0, cascade 2
 *   arK       K = 1, 3, 7, 12 minimised windows, their icons moved to
 *             scattered places: ArrangeIconicWindows(desktop)
 */
#include <windows.h>

#define TK_REF "tm3ref.h"
#include "tklog.h"

extern WORD FAR PASCAL WaitEvent(HTASK);
extern BOOL FAR PASCAL InitApp(HINSTANCE);
extern WORD FAR PASCAL TileChildWindows(HWND, WORD);         /* USER.199 */
extern WORD FAR PASCAL CascadeChildWindows(HWND, WORD);      /* USER.198 */

#define NW 14
static HWND w[NW];
static int nw;
static HINSTANCE hinst;

LRESULT FAR PASCAL __export __loadds tm3_proc(HWND h, UINT m, WPARAM wp, LPARAM lp)
{
    return DefWindowProc(h, m, wp, lp);
}

static void pump(void)
{
    MSG m;
    while (PeekMessage(&m, 0, 0, 0, PM_REMOVE))
        DispatchMessage(&m);
}

static void make(int n, DWORD style)
{
    int i;
    char t[8];
    for (i = 0; i < n && nw < NW; i++, nw++) {
        wsprintf(t, "W%d", nw);
        w[nw] = CreateWindow("TM3", t, style, 10 + 15 * nw, 10 + 10 * nw, 200, 150, 0, 0, hinst, 0);
        ShowWindow(w[nw], SW_SHOWNORMAL);
    }
    pump();
}

static void clear(void)
{
    while (nw)
        DestroyWindow(w[--nw]);
    pump();
}

static void wdump(const char *label, WORD ax)
{
    HWND h;
    RECT r;
    int i;
    char b[64];
    pump();
    wsprintf(b, "%s ax=%04X", (LPSTR)label, ax);
    out(b);
    for (i = 0; i < nw; i++) {
        h = w[i];
        if (!IsWindowVisible(h))
            continue;
        GetWindowRect(h, &r);
        wsprintf(b, " %d:%d,%d,%d,%d%s%s", i, r.left, r.top, r.right - r.left, r.bottom - r.top,
                 IsIconic(h) ? (LPSTR)",I" : (LPSTR)"", IsZoomed(h) ? (LPSTR)",Z" : (LPSTR)"");
        out(b);
    }
    out("\r\n");
}

static void ops(const char *c)
{
    char b[24];
    HWND d = GetDesktopWindow();
    wsprintf(b, "%s tile0", (LPSTR)c);
    wdump(b, TileChildWindows(d, 0));
    wsprintf(b, "%s tile1", (LPSTR)c);
    wdump(b, TileChildWindows(d, MDITILE_HORIZONTAL));
    wsprintf(b, "%s cascade0", (LPSTR)c);
    wdump(b, CascadeChildWindows(d, 0));
}

int __cdecl tmain(void)
{
    WNDCLASS wc;
    HWND d = GetDesktopWindow();
    int n, k;
    char b[16];
    static const int ark[4] = { 1, 3, 7, 12 };
    hinst = (HINSTANCE)init_regs[5];
    WaitEvent(0);
    InitApp(hinst);
    out("TM3 start\r\n");
    wc.style = 0;
    wc.lpfnWndProc = tm3_proc;
    wc.cbClsExtra = wc.cbWndExtra = 0;
    wc.hInstance = hinst;
    wc.hIcon = LoadIcon(0, IDI_APPLICATION);
    wc.hCursor = LoadCursor(0, IDC_ARROW);
    wc.hbrBackground = (HBRUSH)(COLOR_WINDOW + 1);
    wc.lpszMenuName = 0;
    wc.lpszClassName = "TM3";
    if (!RegisterClass(&wc))
        return 1;

    for (n = 1; n <= 10; n++) {
        make(n, WS_OVERLAPPEDWINDOW);
        wsprintf(b, "n%d", n);
        ops(b);
        clear();
    }

    make(5, WS_OVERLAPPEDWINDOW);
    ShowWindow(w[4], SW_MINIMIZE);
    ops("ic");
    clear();

    make(4, WS_OVERLAPPEDWINDOW);
    ShowWindow(w[3], SW_MAXIMIZE);
    wdump("mx before", 0);
    wdump("mx tile0", TileChildWindows(d, 0));
    ShowWindow(w[3], SW_MAXIMIZE);
    wdump("mx cascade0", CascadeChildWindows(d, 0));
    clear();

    make(1, WS_OVERLAPPEDWINDOW);
    make(1, WS_POPUP | WS_CAPTION | WS_SYSMENU);
    make(3, WS_OVERLAPPEDWINDOW);
    EnableWindow(w[3], FALSE);
    wdump("mix tile0", TileChildWindows(d, 0));
    wdump("mix tile2", TileChildWindows(d, 2));
    wdump("mix cascade0", CascadeChildWindows(d, 0));
    wdump("mix cascade2", CascadeChildWindows(d, 2));
    clear();

    for (k = 0; k < 4; k++) {
        make(ark[k], WS_OVERLAPPEDWINDOW);
        for (n = 0; n < nw; n++) {
            ShowWindow(w[n], SW_MINIMIZE);
            SetWindowPos(w[n], 0, 37 * n % 500 + 50, 23 * n % 300 + 20, 0, 0,
                         SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE);
        }
        wsprintf(b, "ar%d before", ark[k]);
        wdump(b, 0);
        wsprintf(b, "ar%d", ark[k]);
        wdump(b, ArrangeIconicWindows(d));
        clear();
    }
    out("done\r\n");
    return 0;
}
