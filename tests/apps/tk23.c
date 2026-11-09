/*
 * tk23.c - which step of a program's set-up stops SYSTEM.DRV's timer
 * procedures under w16 (TK20 versus TK21). Run as the shell with the
 * number of steps as its command tail (tests/dropin.sh -c 'WIN /S /B N'),
 * on w16 and with -r on Windows 3.1. Steps, cumulative:
 *   1 WaitEvent(0) + InitApp   2 RegisterClass + CreateWindow
 *   (9: InitApp, then wait for ever, for w16's watchdog and trace)
 *   3 ShowWindow               4 UpdateWindow     5 SetTimer
 * After the steps it counts calls of a CreateSystemTimer procedure of
 * its own (55 ms) for 18 BIOS ticks, busy-waiting without calling USER,
 * then once more while it peeks messages.
 */
#include <windows.h>

#define TK_REF "tk23ref.h"
#include "tklog.h"

extern WORD FAR PASCAL K_WAITEVENT(WORD htask);
extern int FAR PASCAL U_INITAPP(WORD hinst);
extern WORD __cdecl get_0040h(void);
extern WORD FAR PASCAL S_CREATESYSTEMTIMER(WORD ms, FARPROC proc);
extern WORD FAR PASCAL S_KILLSYSTEMTIMER(WORD id);

static volatile WORD calls;

void FAR __loadds sys_timer(void)
{
    calls++;
}

static WORD get_flags(void);
#pragma aux get_flags = "pushf" "pop ax" value [ax];

LRESULT FAR PASCAL __export __loadds WndProc(HWND h, UINT msg, WPARAM wp, LPARAM lp)
{
    return DefWindowProc(h, msg, wp, lp);
}

static WORD count(int peek)
{
    volatile DWORD FAR *ticks = (volatile DWORD FAR *)MAKELP(get_0040h(), 0x6C);
    DWORD t0;
    WORD id;
    MSG m;
    calls = 0;
    id = S_CREATESYSTEMTIMER(55, (FARPROC)sys_timer);
    t0 = *ticks;
    while (*ticks - t0 < 18)
        if (peek && PeekMessage(&m, 0, 0, 0, PM_REMOVE))
            DispatchMessage(&m);
    S_KILLSYSTEMTIMER(id);
    return calls;
}

int __cdecl tmain(void)
{
    WORD hinst = init_regs[5];          /* DI from InitTask */
    LPSTR tail = (LPSTR)MAKELP(init_regs[6], init_regs[1]);   /* ES:BX */
    WORD steps = 0, n;
    WNDCLASS wc;
    HWND h = 0;

    for (n = 0; n < 8 && tail[n] != '\r' && tail[n]; n++)
        if (tail[n] >= '0' && tail[n] <= '9') {
            steps = tail[n] - '0';
            break;
        }
    out("TK23 start\r\n");
    obs("steps", steps);
    obs("IF at start", (get_flags() & 0x200) != 0);
    if (steps >= 1) {
        K_WAITEVENT(0);
        obs("InitApp != 0", U_INITAPP(hinst) != 0);
        obs("IF after InitApp", (get_flags() & 0x200) != 0);
        if (steps == 9)                 /* stop here, for KERNEL's watchdog */
            for (;;)
                K_WAITEVENT(0);
    }
    if (steps >= 2) {
        wc.style = 0;
        wc.lpfnWndProc = WndProc;
        wc.cbClsExtra = wc.cbWndExtra = 0;
        wc.hInstance = hinst;
        wc.hIcon = 0;
        wc.hCursor = LoadCursor(0, IDC_ARROW);
        wc.hbrBackground = GetStockObject(WHITE_BRUSH);
        wc.lpszMenuName = NULL;
        wc.lpszClassName = "TK23";
        RegisterClass(&wc);
        h = CreateWindow("TK23", "TK23", WS_OVERLAPPEDWINDOW, 40, 40, 200, 120,
                         0, 0, hinst, NULL);
        obs("CreateWindow != 0", h != 0);
    }
    if (steps >= 3)
        ShowWindow(h, SW_SHOWNORMAL);
    if (steps >= 4)
        UpdateWindow(h);
    if (steps >= 5)
        SetTimer(h, 1, 100, NULL);
    obs("IF before counting", (get_flags() & 0x200) != 0);
    n = count(0);
    obs("timer calls busy >= 10", n >= 10);
    obs_env("timer calls busy", n);
    n = count(1);
    obs("timer calls peeking >= 10", n >= 10);
    obs_env("timer calls peeking", n);
    out("TK23 done, failures=");
    outhex((WORD)failures);
    out("\r\n");
    return failures;
}
