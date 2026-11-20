/*
 * us29d.c - US29D.DLL, the DLL of probe US29 (tests/apps/us29.c): it
 * registers US29DG (CS_GLOBALCLASS) and US29DL (local) when it loads, and
 * sets WH_GETMESSAGE hooks that count their calls.
 */
#include <windows.h>

extern WORD __cdecl lib_regs[12];

static HINSTANCE hinst;
static HHOOK hA, hB;
static WORD cntA, cntB;

LRESULT FAR PASCAL __loadds US29D_HOOKA(int c, WPARAM w, LPARAM l)
{
    cntA++;
    return CallNextHookEx(hA, c, w, l);
}

LRESULT FAR PASCAL __loadds US29D_HOOKB(int c, WPARAM w, LPARAM l)
{
    cntB++;
    return CallNextHookEx(hB, c, w, l);
}

int __cdecl libmain(void)
{
    WNDCLASS wc;
    hinst = (HINSTANCE)lib_regs[5];
    wc.style = CS_GLOBALCLASS;
    wc.lpfnWndProc = DefWindowProc;
    wc.cbClsExtra = wc.cbWndExtra = 0;
    wc.hInstance = hinst;
    wc.hIcon = 0;
    wc.hCursor = 0;
    wc.hbrBackground = 0;
    wc.lpszMenuName = NULL;
    wc.lpszClassName = "US29DG";
    RegisterClass(&wc);
    wc.style = 0;
    wc.lpszClassName = "US29DL";
    RegisterClass(&wc);
    return 1;
}

WORD FAR PASCAL __loadds US29D_SETA(WORD task)
{
    hA = SetWindowsHookEx(WH_GETMESSAGE, (HOOKPROC)US29D_HOOKA, hinst, (HTASK)task);
    return hA != 0;
}

WORD FAR PASCAL __loadds US29D_SETB(WORD task)
{
    hB = SetWindowsHookEx(WH_GETMESSAGE, (HOOKPROC)US29D_HOOKB, hinst, (HTASK)task);
    return hB != 0;
}

DWORD FAR PASCAL __loadds US29D_COUNTS(void)
{
    return MAKELONG(cntA, cntB);
}

DWORD FAR PASCAL __loadds US29D_RESET(void)
{
    cntA = cntB = 0;
    return 0;
}

WORD FAR PASCAL __loadds US29D_UNHOOK(WORD x)
{
    (void)x;
    if (hA)
        UnhookWindowsHookEx(hA);
    if (hB)
        UnhookWindowsHookEx(hB);
    hA = hB = 0;
    return 0;
}
