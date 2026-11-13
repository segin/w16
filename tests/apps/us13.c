/*
 * us13.c - probe US13: how Windows 3.1 blinks the caret. Run as the shell
 * on Windows 3.1 (tests/refrun.sh). A window with a caret pumps messages
 * for a while; every message it gets is logged (number, wParam, time
 * since the start) and the caret's pixel on the screen is sampled each
 * time (on = inverted): the blink's period, the message that drives it,
 * what SetCaretPos, HideCaret/ShowCaret and SetCaretBlinkTime do to the
 * phase.
 */
#include <windows.h>

#define TK_REF "us13ref.h"
#include "tklog.h"

extern WORD FAR PASCAL WaitEvent(HTASK);
extern BOOL FAR PASCAL InitApp(HINSTANCE);

static HWND w;
static DWORD t0;
static int lastpix = -1;

static void sample(const char *what)
{
    HDC hdc = GetDC(w);
    COLORREF c = GetPixel(hdc, 11, 11);
    int on = c == RGB(0, 0, 0);
    ReleaseDC(w, hdc);
    if (on != lastpix) {
        out("  t=");
        outhex((WORD)(GetTickCount() - t0));
        out(on ? " caret on" : " caret off");
        out(" (");
        out(what);
        out(")\r\n");
        lastpix = on;
    }
}

LRESULT CALLBACK __export WinProc(HWND h, UINT m, WPARAM wp, LPARAM lp)
{
    if (h == w && m != WM_NCHITTEST && m != WM_SETCURSOR && m != WM_MOUSEMOVE) {
        out("  t=");
        outhex((WORD)(GetTickCount() - t0));
        out(" msg ");
        outhex(m);
        out(" wp ");
        outhex(wp);
        out(" lp ");
        outhex(HIWORD(lp));
        outhex(LOWORD(lp));
        out("\r\n");
    }
    return DefWindowProc(h, m, wp, lp);
}

static void pump(DWORD ms, const char *what)
{
    DWORD t = GetTickCount();
    MSG msg;
    while (GetTickCount() - t < ms) {
        while (PeekMessage(&msg, 0, 0, 0, PM_REMOVE)) {
            DispatchMessage(&msg);
            sample(what);
        }
        sample(what);
    }
}

int __cdecl tmain(void)
{
    HINSTANCE hi = (HINSTANCE)init_regs[5];
    WNDCLASS wc;
    WaitEvent(0);
    InitApp(hi);
    out("US13 start\r\n");
    wc.style = 0;
    wc.lpfnWndProc = WinProc;
    wc.cbClsExtra = wc.cbWndExtra = 0;
    wc.hInstance = hi;
    wc.hIcon = 0;
    wc.hCursor = LoadCursor(NULL, IDC_ARROW);
    wc.hbrBackground = (HBRUSH)(COLOR_WINDOW + 1);
    wc.lpszMenuName = NULL;
    wc.lpszClassName = "US13";
    RegisterClass(&wc);
    w = CreateWindow("US13", "US13", WS_OVERLAPPEDWINDOW | WS_VISIBLE, 50, 50, 200, 150, NULL, NULL, hi, NULL);
    UpdateWindow(w);
    obs("blinktime", GetCaretBlinkTime());
    t0 = GetTickCount();
    out("create+show\r\n");
    CreateCaret(w, NULL, 4, 12);
    SetCaretPos(10, 10);
    ShowCaret(w);
    pump(2000, "plain");
    out("SetCaretPos\r\n");
    SetCaretPos(10, 10);
    pump(1000, "after pos");
    out("Hide\r\n");
    HideCaret(w);
    pump(1200, "hidden");
    out("Show\r\n");
    ShowCaret(w);
    pump(1200, "shown");
    out("SetCaretBlinkTime 200\r\n");
    SetCaretBlinkTime(200);
    obs("blinktime2", GetCaretBlinkTime());
    pump(1200, "200ms");
    out("no pump 1000\r\n");
    {
        DWORD t = GetTickCount();
        while (GetTickCount() - t < 1000)
            sample("no pump");
    }
    pump(600, "pump again");
    SetCaretBlinkTime(530);
    DestroyCaret();
    DestroyWindow(w);
    out("US13 end\r\n");
    return 0;
}
