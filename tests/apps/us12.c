/*
 * us12.c - probe US12: what GetFocus returns inside WM_KILLFOCUS and
 * WM_SETFOCUS when the focus moves from one child window to another
 * (SetFocus). Run as the shell on Windows 3.1 (tests/refrun.sh).
 */
#include <windows.h>

#define TK_REF "us12ref.h"
#include "tklog.h"

extern WORD FAR PASCAL WaitEvent(HTASK);
extern BOOL FAR PASCAL InitApp(HINSTANCE);

static HWND a, b, top;

static const char *nm(HWND h)
{
    return h == a ? "A" : h == b ? "B" : h == top ? "top" : h ? "other" : "0";
}

LRESULT CALLBACK __export WinProc(HWND w, UINT m, WPARAM wp, LPARAM lp)
{
    if ((m == WM_KILLFOCUS || m == WM_SETFOCUS) && (w == a || w == b)) {
        out("OBS ");
        out(nm(w));
        out(m == WM_KILLFOCUS ? "-kill" : "-set");
        out("-getfocus=");
        out(nm(GetFocus()));
        out("\r\nOBS ");
        out(nm(w));
        out(m == WM_KILLFOCUS ? "-kill" : "-set");
        out("-wparam=");
        out(nm((HWND)wp));
        out("\r\n");
    }
    return DefWindowProc(w, m, wp, lp);
}

int __cdecl tmain(void)
{
    HINSTANCE hi = (HINSTANCE)init_regs[5];
    WNDCLASS wc;
    MSG msg;
    WaitEvent(0);
    InitApp(hi);
    out("US12 start\r\n");
    wc.style = 0;
    wc.lpfnWndProc = WinProc;
    wc.cbClsExtra = wc.cbWndExtra = 0;
    wc.hInstance = hi;
    wc.hIcon = 0;
    wc.hCursor = LoadCursor(NULL, IDC_ARROW);
    wc.hbrBackground = (HBRUSH)(COLOR_WINDOW + 1);
    wc.lpszMenuName = NULL;
    wc.lpszClassName = "US12";
    RegisterClass(&wc);
    top = CreateWindow("US12", "US12", WS_OVERLAPPEDWINDOW | WS_VISIBLE, 50, 50, 300, 200, NULL, NULL, hi, NULL);
    a = CreateWindow("US12", "", WS_CHILD | WS_VISIBLE, 10, 10, 50, 50, top, (HMENU)1, hi, NULL);
    b = CreateWindow("US12", "", WS_CHILD | WS_VISIBLE, 70, 10, 50, 50, top, (HMENU)2, hi, NULL);
    while (PeekMessage(&msg, 0, 0, 0, PM_REMOVE))
        DispatchMessage(&msg);
    out("SetFocus(A)\r\n");
    SetFocus(a);
    out("SetFocus(B)\r\n");
    SetFocus(b);
    out("SetFocus(NULL)\r\n");
    SetFocus(NULL);
    DestroyWindow(top);
    out("US12 end\r\n");
    return 0;
}
