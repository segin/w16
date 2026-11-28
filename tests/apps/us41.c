/*
 * us41.c - probe US41: what DefWindowProc's WM_NCACTIVATE repaints of a
 * window's non-client area. Found with Win32s's Freecell, which writes
 * "Cards Left: N" into its own menu bar: on w16 the text went when a
 * message box took the activation (w16 painted the whole non-client
 * area, the menu bar too). Run as the shell (on 3.1 and with w16's USER),
 * its log compared with 3.1's (boot-test case rsrchandler, part user).
 * An overlapped window with a menu; magenta pixels are put through its
 * window DC in the caption, the menu bar and the left frame, then
 * WM_NCACTIVATE is sent (FALSE, then TRUE):
 *   OBS ncaN.caption, .menu, .frame = 1 the pixel was repainted, 0 kept
 * N: 0 for FALSE, 1 for TRUE.
 */
#include <windows.h>

#define TK_REF "us41ref.h"
#include "tklog.h"

extern BOOL FAR PASCAL InitApp(HINSTANCE);

#define MAGENTA RGB(255, 0, 255)

LRESULT FAR PASCAL __export __loadds main_proc(HWND h, UINT m, WPARAM w, LPARAM l)
{
    return DefWindowProc(h, m, w, l);
}

static void pump(int n)
{
    MSG m;
    while (n-- > 0) {
        while (PeekMessage(&m, 0, 0, 0, PM_REMOVE)) {
            TranslateMessage(&m);
            DispatchMessage(&m);
        }
        Yield();
    }
}

int __cdecl tmain(void)
{
    HINSTANCE hi = (HINSTANCE)init_regs[5];
    WNDCLASS wc;
    HWND h;
    HMENU mb;
    RECT wr;
    POINT c;
    int pt[3][2], k, n;
    static const char *what[3] = { "caption", "menu", "frame" };
    char name[32];

    InitApp(hi);
    out("US41 start\r\n");
    wc.style = 0;
    wc.lpfnWndProc = main_proc;
    wc.cbClsExtra = wc.cbWndExtra = 0;
    wc.hInstance = hi;
    wc.hIcon = 0;
    wc.hCursor = LoadCursor(NULL, IDC_ARROW);
    wc.hbrBackground = (HBRUSH)(COLOR_WINDOW + 1);
    wc.lpszMenuName = NULL;
    wc.lpszClassName = "US41";
    RegisterClass(&wc);
    mb = CreateMenu();
    AppendMenu(mb, MF_STRING, 100, "&Game");
    h = CreateWindow("US41", "U", WS_OVERLAPPEDWINDOW | WS_VISIBLE, 0, 0, 400, 300,
                     0, mb, hi, NULL);
    pump(30);
    GetWindowRect(h, &wr);
    c.x = c.y = 0;
    ClientToScreen(h, &c);
    c.x -= wr.left;
    c.y -= wr.top;
    pt[0][0] = 300;                     /* the caption, right of the title */
    pt[0][1] = GetSystemMetrics(SM_CYFRAME) + 4;
    pt[1][0] = 300;                     /* the menu bar, right of its item */
    pt[1][1] = c.y - 5;
    pt[2][0] = 2;                       /* the left frame */
    pt[2][1] = 150;
    for (n = 0; n < 2; n++) {
        HDC dc = GetWindowDC(h);
        for (k = 0; k < 3; k++)
            SetPixel(dc, pt[k][0], pt[k][1], MAGENTA);
        ReleaseDC(h, dc);
        SendMessage(h, WM_NCACTIVATE, n, 0);
        dc = GetWindowDC(h);
        for (k = 0; k < 3; k++) {
            wsprintf(name, "nca%d.%s", n, (LPSTR)what[k]);
            obs(name, (GetPixel(dc, pt[k][0], pt[k][1]) & 0xFFFFFFUL) != MAGENTA);
        }
        ReleaseDC(h, dc);
    }
    DestroyWindow(h);
    out("US41 end\r\n");
    return failures;
}
