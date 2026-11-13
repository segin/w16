/*
 * us22.c - probe US22: what follows ReleaseCapture with the pointer still
 * (ARCHITECTURE.md 10.3; user/input.c). Run as the shell under real mouse
 * input (tests/scripts/input/capcursor.txt, boot-test case capcursor).
 *
 * The window captures the mouse on a left press in its client area and
 * releases it on the left release; it logs to TK.LOG every mouse message,
 * WM_NCMOUSEMOVE and WM_SETCURSOR (hit-test code and mouse message) it
 * gets, with markers around its ReleaseCapture call, so the log shows
 * whether the cursor is set again at once (inside ReleaseCapture), later
 * (a mouse message made for it), or not until the pointer moves.
 */
#include <windows.h>

#define TK_REF "us20ref.h"
#include "tklog.h"

extern WORD FAR PASCAL WaitEvent(HTASK);
extern BOOL FAR PASCAL InitApp(HINSTANCE);

static int capturing;

static void logm(const char *what, WPARAM wp, LPARAM lp)
{
    char b[80];
    wsprintf(b, "%s %04X %04X %04X\r\n", (LPSTR)what, wp, LOWORD(lp), HIWORD(lp));
    out(b);
}

LRESULT FAR PASCAL __export __loadds wnd_proc(HWND h, UINT m, WPARAM wp, LPARAM lp)
{
    switch (m) {
    case WM_SETCURSOR:
        logm("SETCURSOR", (WORD)wp == (WORD)h, lp);
        break;
    case WM_MOUSEMOVE:
        logm("MOUSEMOVE", wp, lp);
        break;
    case WM_NCMOUSEMOVE:
        logm("NCMOUSEMOVE", wp, lp);
        break;
    case WM_LBUTTONDOWN:
        logm("LBUTTONDOWN", wp, lp);
        SetCapture(h);
        capturing = 1;
        return 0;
    case WM_LBUTTONUP:
        logm("LBUTTONUP", wp, lp);
        if (capturing) {
            capturing = 0;
            out("ReleaseCapture {\r\n");
            ReleaseCapture();
            out("ReleaseCapture }\r\n");
        }
        return 0;
    case WM_NCLBUTTONDOWN:
        logm("NCLBUTTONDOWN", wp, lp);
        return 0;               /* (no sizing: the border only shows the cursor) */
    }
    return DefWindowProc(h, m, wp, lp);
}

int __cdecl tmain(void)
{
    WNDCLASS wc;
    HINSTANCE hinst = (HINSTANCE)init_regs[5];
    HWND hw;
    MSG msg;
    WaitEvent(0);
    InitApp(hinst);
    out("US22 start\r\n");
    wc.style = 0;
    wc.lpfnWndProc = wnd_proc;
    wc.cbClsExtra = wc.cbWndExtra = 0;
    wc.hInstance = hinst;
    wc.hIcon = 0;
    wc.hCursor = LoadCursor(NULL, IDC_ARROW);
    wc.hbrBackground = (HBRUSH)(COLOR_WINDOW + 1);
    wc.lpszMenuName = NULL;
    wc.lpszClassName = "US22";
    RegisterClass(&wc);
    hw = CreateWindow("US22", "US22", WS_OVERLAPPEDWINDOW | WS_VISIBLE, 40, 40, 320, 240, NULL, NULL,
                      hinst, NULL);
    (void)hw;
    while (GetMessage(&msg, 0, 0, 0)) {
        TranslateMessage(&msg);
        DispatchMessage(&msg);
    }
    out("US22 end\r\n");
    return 0;
}
