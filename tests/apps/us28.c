/*
 * us28.c - probe US28: where WM_MDICREATE puts a child given
 * CW_USEDEFAULT (user/mdi.c, mdi_create), as children come and go.
 *
 * Found with QuickTime's Movie Player (docs/notes/quicktime-handover.md):
 * a movie closed and opened again came back 20 pixels lower on 3.1, at
 * the same place on w16. An MDI frame of fixed size (0,0, 600 x 400) and
 * its MDICLIENT; each step makes or destroys children (WM_MDICREATE with
 * x, y, cx, cy all CW_USEDEFAULT unless the step says otherwise;
 * WM_MDIDESTROY) and logs the new child's rectangle in the client's
 * coordinates, OBS NAME="x y cx cy":
 *   a b c       three children
 *   d           after b was destroyed (a and c left)
 *   e           after every child was destroyed
 *   f           after e was destroyed again
 *   g h         g at an explicit place (10, 10, 200 x 100), then h
 *   wrap1..8    eight more, a, c, d, e, ... destroyed first (the cascade
 *               wraps after the client's height in steps)
 *   max after   a child made maximised (WS_MAXIMIZE), then another
 *   last        after every child was destroyed
 * Run as the shell: tests/dropin.sh -r ... -i boot:shell=C:\US28.EXE
 * (Windows 3.1), with -f w16's USER.EXE (w16's USER); compare the OBS
 * lines, which must be identical (tests/boottest.sh case mdimax).
 */
#include <windows.h>

#define TK_REF "us28ref.h"
#include "tklog.h"

extern WORD FAR PASCAL WaitEvent(HTASK);
extern BOOL FAR PASCAL InitApp(HINSTANCE);

static HINSTANCE hi;
static HWND hFrame, hClient;

LRESULT FAR PASCAL __export __loadds frame_proc(HWND h, UINT m, WPARAM w, LPARAM l)
{
    return DefFrameProc(h, hClient, m, w, l);
}

LRESULT FAR PASCAL __export __loadds child_proc(HWND h, UINT m, WPARAM w, LPARAM l)
{
    return DefMDIChildProc(h, m, w, l);
}

static HWND make(const char *name, int x, int y, int cx, int cy, DWORD style)
{
    MDICREATESTRUCT mc;
    HWND c;
    RECT r;
    char b[80];
    mc.szClass = "US28C";
    mc.szTitle = name;
    mc.hOwner = hi;
    mc.x = x;
    mc.y = y;
    mc.cx = cx;
    mc.cy = cy;
    mc.style = style;
    mc.lParam = 0;
    c = (HWND)LOWORD(SendMessage(hClient, WM_MDICREATE, 0, (LPARAM)(LPVOID)&mc));
    if (!c) {
        wsprintf(b, "OBS %s=none\r\n", (LPSTR)name);
        out(b);
        return 0;
    }
    GetWindowRect(c, &r);
    ScreenToClient(hClient, (POINT FAR *)&r.left);
    ScreenToClient(hClient, (POINT FAR *)&r.right);
    wsprintf(b, "OBS %s=\"%d %d %d %d\"\r\n", (LPSTR)name, r.left, r.top,
             r.right - r.left, r.bottom - r.top);
    out(b);
    return c;
}

static void destroy(HWND c)
{
    if (c)
        SendMessage(hClient, WM_MDIDESTROY, (WPARAM)c, 0);
}

#define D CW_USEDEFAULT

int __cdecl tmain(void)
{
    WNDCLASS wc;
    CLIENTCREATESTRUCT cc;
    HWND a, b, c, d, e, f, g, h, w[8], m;
    RECT r;
    int i;
    char nm[8];

    hi = (HINSTANCE)init_regs[5];
    WaitEvent(0);
    InitApp(hi);
    out("US28 start\r\n");

    wc.style = 0;
    wc.lpfnWndProc = frame_proc;
    wc.cbClsExtra = wc.cbWndExtra = 0;
    wc.hInstance = hi;
    wc.hIcon = LoadIcon(NULL, IDI_APPLICATION);
    wc.hCursor = LoadCursor(NULL, IDC_ARROW);
    wc.hbrBackground = (HBRUSH)(COLOR_APPWORKSPACE + 1);
    wc.lpszMenuName = NULL;
    wc.lpszClassName = "US28F";
    RegisterClass(&wc);
    wc.lpfnWndProc = child_proc;
    wc.hbrBackground = (HBRUSH)(COLOR_WINDOW + 1);
    wc.lpszClassName = "US28C";
    RegisterClass(&wc);

    hFrame = CreateWindow("US28F", "US28", WS_OVERLAPPEDWINDOW | WS_CLIPCHILDREN, 0, 0, 600, 400,
                          0, 0, hi, NULL);
    cc.hWindowMenu = 0;
    cc.idFirstChild = 500;
    GetClientRect(hFrame, &r);
    hClient = CreateWindow("MDICLIENT", NULL, WS_CHILD | WS_CLIPCHILDREN | WS_VISIBLE, 0, 0,
                           r.right, r.bottom, hFrame, (HMENU)1, hi, (LPVOID)&cc);
    ShowWindow(hFrame, SW_SHOWNORMAL);
    UpdateWindow(hFrame);
    GetClientRect(hClient, &r);
    obs("client.cx", r.right);
    obs("client.cy", r.bottom);

    a = make("a", D, D, D, D, 0);
    b = make("b", D, D, D, D, 0);
    c = make("c", D, D, D, D, 0);
    destroy(b);
    d = make("d", D, D, D, D, 0);
    destroy(a);
    destroy(c);
    destroy(d);
    e = make("e", D, D, D, D, 0);
    destroy(e);
    f = make("f", D, D, D, D, 0);
    g = make("g", 10, 10, 200, 100, 0);
    h = make("h", D, D, D, D, 0);
    destroy(f);
    destroy(g);
    destroy(h);
    for (i = 0; i < 8; i++) {
        wsprintf(nm, "wrap%d", i + 1);
        w[i] = make(nm, D, D, D, D, 0);
    }
    for (i = 0; i < 8; i++)
        destroy(w[i]);
    m = make("max", D, D, D, D, WS_MAXIMIZE);
    a = make("after", D, D, D, D, 0);
    destroy(a);
    destroy(m);
    a = make("last", D, D, D, D, 0);
    destroy(a);
    out("US28 done\r\n");
    DestroyWindow(hFrame);
    return 0;
}
