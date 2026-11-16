/*
 * us10.c - probe US10: USER's DisableOEMLayer (USER.4) and EnableOEMLayer
 * (USER.3), which WINOLDAP uses around a DOS program (Schulman ch. 6).
 * Run as the shell on Windows 3.1 (tests/refrun.sh; tests/ustrace.sh for
 * the calls USER makes into GDI and the drivers): the BIOS video mode
 * (INT 10h AH=0Fh) before, after DisableOEMLayer and after
 * EnableOEMLayer, and the messages a window gets meanwhile.
 */
#include <windows.h>

#define TK_REF "us10ref.h"
#include "tklog.h"

extern WORD FAR PASCAL WaitEvent(HTASK);
extern BOOL FAR PASCAL InitApp(HINSTANCE);
extern void FAR PASCAL EnableOEMLayer(void);
extern void FAR PASCAL DisableOEMLayer(void);

static WORD vmode(void)
{
    WORD m = 0;
    __asm {
        mov ah, 0Fh
        int 10h
        xor ah, ah
        mov m, ax
    }
    return m;
}

LRESULT CALLBACK __export WinProc(HWND w, UINT m, WPARAM wp, LPARAM lp)
{
    if (m == WM_PAINT || m == WM_ERASEBKGND || m == WM_NCPAINT || m == WM_ACTIVATEAPP ||
        m == WM_ACTIVATE || m == WM_SETFOCUS || m == WM_KILLFOCUS || m == WM_SYSCOLORCHANGE ||
        m == WM_WININICHANGE || m == WM_DEVMODECHANGE) {
        out("  msg ");
        outhex(m);
        out("\r\n");
    }
    return DefWindowProc(w, m, wp, lp);
}

int __cdecl tmain(void)
{
    HINSTANCE hi = (HINSTANCE)init_regs[5];
    WNDCLASS wc;
    HWND w;
    MSG msg;
    DWORD t;
    WaitEvent(0);
    InitApp(hi);
    out("US10 start\r\n");
    wc.style = 0;
    wc.lpfnWndProc = WinProc;
    wc.cbClsExtra = wc.cbWndExtra = 0;
    wc.hInstance = hi;
    wc.hIcon = 0;
    wc.hCursor = LoadCursor(NULL, IDC_ARROW);
    wc.hbrBackground = (HBRUSH)(COLOR_WINDOW + 1);
    wc.lpszMenuName = NULL;
    wc.lpszClassName = "US10";
    RegisterClass(&wc);
    w = CreateWindow("US10", "US10", WS_OVERLAPPEDWINDOW | WS_VISIBLE, 50, 50, 200, 150, NULL, NULL, hi, NULL);
    UpdateWindow(w);
    while (PeekMessage(&msg, 0, 0, 0, PM_REMOVE))
        DispatchMessage(&msg);
    obs("mode-before", vmode());
    out("DisableOEMLayer\r\n");
    DisableOEMLayer();
    obs("mode-disabled", vmode());
    out("EnableOEMLayer\r\n");
    EnableOEMLayer();
    obs("mode-enabled", vmode());
    t = GetTickCount();
    while (GetTickCount() - t < 1500)
        while (PeekMessage(&msg, 0, 0, 0, PM_REMOVE))
            DispatchMessage(&msg);
    obs("update-empty", GetUpdateRect(w, NULL, FALSE) == 0);
    DestroyWindow(w);
    out("US10 end\r\n");
    return 0;
}
