/*
 * us25.c - probe US25: mouse moves made while a program is busy - one
 * message each, or merged? (ARCHITECTURE.md 10.3; user/input.c). Run as
 * the shell under real mouse input (tests/scripts/input/mmerge.txt;
 * boot-test case mmerge).
 *
 * First, without real input: moves, presses and releases fed to USER's
 * mouse_event (USER.299) by a SYSTEM.DRV timer procedure (at interrupt
 * time, as MOUSE.DRV does: tests/apps/mevent.asm) while the probe is busy
 * for 1.5 seconds, one event per timer tick or several in one tick; then
 * the mouse messages it gets are logged ("S<part> ..."). Then:
 * A right click on the window's client area makes it busy for 4 seconds
 * (a loop that reads no messages) while the script moves the pointer,
 * presses and releases the left button and moves again; then every mouse
 * message the window gets is logged to TK.LOG with its point and key
 * state, in order, until the next right click, which ends the probe's
 * log.
 */
#include <windows.h>

#define TK_REF "us25ref.h"
#include "tklog.h"

extern WORD FAR PASCAL WaitEvent(HTASK);
extern BOOL FAR PASCAL InitApp(HINSTANCE);
extern WORD FAR PASCAL S_CREATESYSTEMTIMER(WORD ms, FARPROC proc);
extern WORD FAR PASCAL S_KILLSYSTEMTIMER(WORD id);
extern void __cdecl call_mouse_event(WORD status, WORD x, WORD y, WORD buttons);

/* the synthetic script: status (1 move, 2 left down, 4 left up, 0 the end
   of this tick's events), screen x, y */
typedef struct { WORD st; int x, y; } MSTEP;
static MSTEP mscript[40];
static volatile int mstep, msteps;

void FAR __loadds mtimer(void)
{
    while (mstep < msteps) {
        MSTEP *s = &mscript[mstep++];
        if (!s->st)
            break;              /* the rest next tick */
        /* absolute, normalised 0-65535 (as us17.c) */
        call_mouse_event(s->st | 0x8000, (WORD)(s->x * 102 + s->x * 2 / 5 + 1),
                         (WORD)(s->y * 136 + s->y * 8 / 15 + 1), 2);
    }
}

static void add(WORD st, int x, int y)
{
    mscript[msteps].st = st;
    mscript[msteps].x = x;
    mscript[msteps].y = y;
    msteps++;
}

static const char *spart;

static int phase;               /* 0 before the first right click, 1 after */

static void logm(const char *what, WPARAM wp, LPARAM lp)
{
    char b[80];
    if (spart)
        wsprintf(b, "%s %s %04X %d,%d\r\n", (LPSTR)spart, (LPSTR)what, wp, (int)LOWORD(lp),
                 (int)HIWORD(lp));
    else
        wsprintf(b, "%s %04X %d,%d\r\n", (LPSTR)what, wp, (int)LOWORD(lp), (int)HIWORD(lp));
    out(b);
}

LRESULT FAR PASCAL __export __loadds wnd_proc(HWND h, UINT m, WPARAM wp, LPARAM lp)
{
    switch (m) {
    case WM_MOUSEMOVE:
        if (phase == 1 || spart)
            logm("MOUSEMOVE", wp, lp);
        return 0;
    case WM_LBUTTONDOWN:
        if (phase == 1 || spart)
            logm("LBUTTONDOWN", wp, lp);
        return 0;
    case WM_LBUTTONUP:
        if (phase == 1 || spart)
            logm("LBUTTONUP", wp, lp);
        return 0;
    case WM_RBUTTONDOWN:
        return 0;
    case WM_RBUTTONUP:
        if (phase == 0) {
            DWORD end = GetTickCount() + 4000;
            out("busy\r\n");
            SetWindowText(h, "US25 busy");
            while (GetTickCount() < end)
                ;
            out("reading\r\n");
            phase = 1;
            SetWindowText(h, "US25 reading");
        } else if (phase == 1) {
            out("US25 end\r\n");
            phase = 2;
            SetWindowText(h, "US25 done");
        }
        return 0;
    }
    return DefWindowProc(h, m, wp, lp);
}

static void pump(DWORD ms)
{
    DWORD end = GetTickCount() + ms;
    MSG m;
    while (GetTickCount() < end) {
        while (PeekMessage(&m, 0, 0, 0, PM_REMOVE)) {
            TranslateMessage(&m);
            DispatchMessage(&m);
        }
    }
}

/* one synthetic part: the script played while busy for 1.5 s, then the
   messages read and logged */
static void spart_run(const char *tag)
{
    DWORD end;
    pump(300);
    spart = tag;
    mstep = 0;
    end = GetTickCount() + 1500;
    while (GetTickCount() < end)
        ;
    pump(500);
    spart = 0;
    msteps = 0;
}

int __cdecl tmain(void)
{
    WNDCLASS wc;
    HINSTANCE hinst = (HINSTANCE)init_regs[5];
    MSG msg;
    WaitEvent(0);
    InitApp(hinst);
    out("US25 start\r\n");
    wc.style = 0;
    wc.lpfnWndProc = wnd_proc;
    wc.cbClsExtra = wc.cbWndExtra = 0;
    wc.hInstance = hinst;
    wc.hIcon = 0;
    wc.hCursor = LoadCursor(NULL, IDC_ARROW);
    wc.hbrBackground = (HBRUSH)(COLOR_WINDOW + 1);
    wc.lpszMenuName = NULL;
    wc.lpszClassName = "US25";
    RegisterClass(&wc);
    CreateWindow("US25", "US25", WS_OVERLAPPEDWINDOW | WS_VISIBLE, 40, 40, 400, 300, NULL, NULL,
                 hinst, NULL);
    {
        int i;
        WORD id;
        pump(1000);
        msteps = 0;
        mstep = 40;             /* nothing to play yet */
        id = S_CREATESYSTEMTIMER(55, (FARPROC)mtimer);
        /* S1: eight moves, one a tick */
        msteps = 0;
        for (i = 0; i < 8; i++) {
            add(1, 100 + 10 * i, 150);
            add(0, 0, 0);
        }
        mstep = 40;
        spart_run("S1");
        /* S2: eight moves in one tick */
        msteps = 0;
        for (i = 0; i < 8; i++)
            add(1, 100 + 10 * i, 170);
        spart_run("S2");
        /* S3: moves, the left button pressed, moves, released, moves, one
           event a tick */
        msteps = 0;
        for (i = 0; i < 3; i++)
            add(1, 100 + 10 * i, 190), add(0, 0, 0);
        add(2, 120, 190), add(0, 0, 0);
        for (i = 0; i < 3; i++)
            add(1, 130 + 10 * i, 190), add(0, 0, 0);
        add(4, 150, 190), add(0, 0, 0);
        for (i = 0; i < 3; i++)
            add(1, 160 + 10 * i, 190), add(0, 0, 0);
        spart_run("S3");
        /* S4: the same in one tick */
        msteps = 0;
        for (i = 0; i < 3; i++)
            add(1, 100 + 10 * i, 210);
        add(2, 120, 210);
        for (i = 0; i < 3; i++)
            add(1, 130 + 10 * i, 210);
        add(4, 150, 210);
        for (i = 0; i < 3; i++)
            add(1, 160 + 10 * i, 210);
        spart_run("S4");
        S_KILLSYSTEMTIMER(id);
        out("synthetic end\r\n");
    }
    while (GetMessage(&msg, 0, 0, 0)) {
        TranslateMessage(&msg);
        DispatchMessage(&msg);
    }
    return 0;
}
