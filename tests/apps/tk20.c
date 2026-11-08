/*
 * tk20.c - the first window: a minimal Windows program run as the shell
 * (tests/dropin.sh -i 'boot:shell=C:\TK20.EXE' -f build/dos/TK20.EXE=TK20.EXE;
 * with -r on Windows 3.1's own KERNEL). It logs each step of a plain
 * Windows program's life, to compare w16's KERNEL with Windows' under
 * Microsoft's USER: InitApp, the message queue, RegisterClass,
 * CreateWindow, ShowWindow/UpdateWindow, a timer, the message loop,
 * DestroyWindow and WM_QUIT. The messages the window gets are counted.
 * Exits with the number of failures.
 */
#include <windows.h>

#define TK_REF "tk20ref.h"
#include "tklog.h"

extern WORD FAR PASCAL K_WAITEVENT(WORD htask);
extern WORD FAR PASCAL K_GETTASKQUEUE(WORD htask);
extern int FAR PASCAL U_INITAPP(WORD hinst);
extern WORD __cdecl get_0040h(void);
extern WORD FAR PASCAL S_CREATESYSTEMTIMER(WORD ms, FARPROC proc);
extern WORD FAR PASCAL S_KILLSYSTEMTIMER(WORD id);

static volatile WORD sys_calls;

void FAR __loadds sys_timer(void)
{
    sys_calls++;
}

static WORD get_flags(void);
#pragma aux get_flags = "pushf" "pop ax" value [ax];

static WORD n_create, n_paint, n_timer, n_destroy, n_size, n_show, n_other;
static HWND hwnd_created;

LRESULT FAR PASCAL __export __loadds WndProc(HWND h, UINT msg, WPARAM wp, LPARAM lp)
{
    switch (msg) {
    case WM_CREATE:
        n_create++;
        hwnd_created = h;
        break;
    case WM_PAINT: {
        PAINTSTRUCT ps;
        n_paint++;
        BeginPaint(h, &ps);
        TextOut(ps.hdc, 4, 4, "w16", 3);
        EndPaint(h, &ps);
        return 0;
    }
    case WM_TIMER:
        n_timer++;
        if (n_timer == 4) {
            out("DestroyWindow\r\n");
            DestroyWindow(h);
            out("DestroyWindow returned\r\n");
        }
        return 0;
    case WM_SIZE:
        n_size++;
        break;
    case WM_SHOWWINDOW:
        n_show++;
        break;
    case WM_DESTROY:
        n_destroy++;
        out("WM_DESTROY\r\n");
        PostQuitMessage(7);
        return 0;
    default:
        n_other++;
        break;
    }
    return DefWindowProc(h, msg, wp, lp);
}

int __cdecl tmain(void)
{
    WORD hinst = init_regs[5];          /* DI from InitTask */
    WNDCLASS wc;
    HWND h;
    MSG m;
    WORD n = 0, r;

    out("TK20 start\r\n");
    K_WAITEVENT(0);
    r = U_INITAPP(hinst);
    obs("InitApp != 0", r != 0);
    obs("queue != 0", K_GETTASKQUEUE(0) != 0);
    obs("GetDesktopWindow != 0", GetDesktopWindow() != 0);
    wc.style = CS_HREDRAW | CS_VREDRAW;
    wc.lpfnWndProc = WndProc;
    wc.cbClsExtra = 0;
    wc.cbWndExtra = 0;
    wc.hInstance = hinst;
    wc.hIcon = 0;
    wc.hCursor = LoadCursor(0, IDC_ARROW);
    wc.hbrBackground = GetStockObject(WHITE_BRUSH);
    wc.lpszMenuName = NULL;
    wc.lpszClassName = "TK20";
    obs("RegisterClass != 0", RegisterClass(&wc) != 0);
    h = CreateWindow("TK20", "TK20", WS_OVERLAPPEDWINDOW, 40, 40, 200, 120,
                     0, 0, hinst, NULL);
    obs("CreateWindow != 0", h != 0);
    obs("WM_CREATE", n_create);
    obs("hwnd in WM_CREATE == result", hwnd_created == h);
    obs("IsWindow", IsWindow(h) != 0);
    ShowWindow(h, SW_SHOWNORMAL);
    obs("WM_SHOWWINDOW after ShowWindow", n_show);
    UpdateWindow(h);
    obs("WM_PAINT after UpdateWindow", n_paint);
    obs("SetTimer != 0", SetTimer(h, 1, 100, NULL) != 0);
    {
        /* PeekMessage for about a second first (BIOS ticks) */
        volatile DWORD FAR *ticks = (volatile DWORD FAR *)MAKELP(get_0040h(), 0x6C);
        DWORD t0 = *ticks;
        WORD peeked = 0, id, ifl = 0x200;
        id = S_CREATESYSTEMTIMER(55, (FARPROC)sys_timer);
        while (*ticks - t0 < 18 && n_timer < 2) {
            if (PeekMessage(&m, 0, 0, 0, PM_REMOVE)) {
                peeked++;
                DispatchMessage(&m);
            }
            ifl &= get_flags();
        }
        S_KILLSYSTEMTIMER(id);
        obs("WM_TIMER while peeking > 0", n_timer > 0);
        obs_env("messages peeked", peeked);
        obs_env("own system timer calls", sys_calls);
        obs("IF always set after PeekMessage", (ifl & 0x200) != 0);
    }
    out("loop\r\n");
    while (GetMessage(&m, 0, 0, 0)) {
        TranslateMessage(&m);
        DispatchMessage(&m);
        if (++n > 2000)
            break;
    }
    obs("WM_QUIT wParam", m.wParam);
    obs("WM_TIMER", n_timer);
    obs("WM_DESTROY", n_destroy);
    obs("IsWindow after", IsWindow(h) != 0);
    obs_env("messages dispatched", n);
    out("TK20 done, failures=");
    outhex((WORD)failures);
    out("\r\n");
    return failures;
}
