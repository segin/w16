/*
 * us26.c - probe US26: the caption's buttons and the scroll bars'
 * arrows and shaft held down with real mouse input (ARCHITECTURE.md 10.3;
 * user/defwnd.c, user/scroll.c). Run as the shell under
 * tests/scripts/input/holdbtn.txt (boot-test case holdbtn).
 *
 * A window with the minimize and maximize buttons and a vertical scroll
 * bar, range 0-100 at 0, which it moves by 1 for SB_LINEUP/SB_LINEDOWN
 * and 10 for SB_PAGEUP/SB_PAGEDOWN. It logs to TK.LOG, with the time in
 * milliseconds since the last left press (GetMessageTime of the press):
 * WM_NCLBUTTONDOWN (hit-test code), WM_SYSCOMMAND (the command; not
 * carried out, so the window stays where it is), and WM_VSCROLL (code
 * and the bar's position after). The script's screens show the buttons
 * and arrows while held.
 */
#include <windows.h>

#define TK_REF "us26ref.h"
#include "tklog.h"

extern WORD FAR PASCAL WaitEvent(HTASK);
extern BOOL FAR PASCAL InitApp(HINSTANCE);

static DWORD t0;

LRESULT FAR PASCAL __export __loadds wnd_proc(HWND h, UINT m, WPARAM wp, LPARAM lp)
{
    char b[80];
    switch (m) {
    case WM_NCLBUTTONDOWN:
        t0 = GetMessageTime();
        wsprintf(b, "NCLBUTTONDOWN %d\r\n", wp);
        out(b);
        break;
    case WM_LBUTTONDOWN:
        t0 = GetMessageTime();
        break;
    case WM_SYSCOMMAND:
        wsprintf(b, "SYSCOMMAND %04X t=%ld\r\n", wp & 0xFFF0, (long)(GetTickCount() - t0));
        out(b);
        if ((wp & 0xFFF0) == SC_MINIMIZE || (wp & 0xFFF0) == SC_MAXIMIZE ||
            (wp & 0xFFF0) == SC_RESTORE || (wp & 0xFFF0) == SC_CLOSE)
            return 0;
        break;
    case WM_VSCROLL: {
        int p = GetScrollPos(h, SB_VERT);
        if (wp == SB_LINEUP) p -= 1;
        else if (wp == SB_LINEDOWN) p += 1;
        else if (wp == SB_PAGEUP) p -= 10;
        else if (wp == SB_PAGEDOWN) p += 10;
        else if (wp == SB_THUMBPOSITION) p = (int)LOWORD(lp);
        SetScrollPos(h, SB_VERT, p, TRUE);
        wsprintf(b, "VSCROLL %d pos %d t=%ld\r\n", wp, GetScrollPos(h, SB_VERT),
                 (long)(GetTickCount() - t0));
        out(b);
        return 0;
    }
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
    out("US26 start\r\n");
    wc.style = 0;
    wc.lpfnWndProc = wnd_proc;
    wc.cbClsExtra = wc.cbWndExtra = 0;
    wc.hInstance = hinst;
    wc.hIcon = 0;
    wc.hCursor = LoadCursor(NULL, IDC_ARROW);
    wc.hbrBackground = (HBRUSH)(COLOR_WINDOW + 1);
    wc.lpszMenuName = NULL;
    wc.lpszClassName = "US26";
    RegisterClass(&wc);
    hw = CreateWindow("US26", "US26", WS_OVERLAPPEDWINDOW | WS_VSCROLL | WS_VISIBLE, 40, 40, 320, 240,
                      NULL, NULL, hinst, NULL);
    SetScrollRange(hw, SB_VERT, 0, 100, FALSE);
    SetScrollPos(hw, SB_VERT, 0, TRUE);
    while (GetMessage(&msg, 0, 0, 0)) {
        TranslateMessage(&msg);
        DispatchMessage(&msg);
    }
    return 0;
}
