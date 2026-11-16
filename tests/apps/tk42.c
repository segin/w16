/*
 * tk42.c - per-task interrupt vectors (Pietrek, Windows Internals ch. 1,
 * BootStrap: TDB 32h-4Dh hold INT 00h, 02h, 04h, 06h, 07h, 3Eh, 75h,
 * "managed on a per-task basis"). Logs, as the shell: the TDB's seven
 * dwords against what INT 21h AH=35h returns for each; then sets INT 00h
 * and INT 06h with INT 21h AH=25h and logs the TDB and AH=35h again,
 * and once more after yielding to other tasks for 3 seconds.
 * Run as the shell (tests/dropin.sh, and -r for 3.1).
 */
#include <windows.h>

#define TK_REF "tk42ref.h"
#include "tklog.h"

static const BYTE ints[7] = { 0x00, 0x02, 0x04, 0x06, 0x07, 0x3E, 0x75 };

static DWORD getvec(BYTE n);
#pragma aux getvec = "mov ah, 35h" "int 21h" "mov dx, es" "mov ax, bx" \
    parm [al] value [dx ax] modify [bx es];
static void setvec(BYTE n, DWORD v);
#pragma aux setvec = "push ds" "mov ds, cx" "mov ah, 25h" "int 21h" "pop ds" \
    parm [al] [cx dx] modify [ah];

static WORD hsel;

static void vdump(const char *who)
{
    DWORD FAR *t = (DWORD FAR *)MAKELP(GetCurrentTask(), 0x32);
    char n[40];
    WORD i;
    out(who);
    out("\r\n");
    for (i = 0; i < 7; i++) {
        DWORD v = getvec(ints[i]);
        wsprintf(n, "  INT %02X: TDB = INT 21h 35h", ints[i]);
        obs(n, t[i] == v);
        wsprintf(n, "  INT %02X: TDB is ours", ints[i]);
        obs(n, HIWORD(t[i]) == hsel);
        wsprintf(n, "  INT %02X: 35h is ours", ints[i]);
        obs(n, HIWORD(v) == hsel);
        wsprintf(n, "  INT %02X: TDB 0", ints[i]);
        obs(n, t[i] == 0);
    }
}

static void __far handler(void)
{
}

int __cdecl tmain(void)
{
    MSG m;
    DWORD end;
    hsel = SELECTOROF((void __far *)handler);
    out("TK42 start\r\n");
    vdump("at start");
    setvec(0x00, (DWORD)(void __far *)handler);
    setvec(0x06, (DWORD)(void __far *)handler);
    vdump("after setting 00h, 06h");
    end = GetTickCount() + 3000;
    while (GetTickCount() < end)
        if (PeekMessage(&m, 0, 0, 0, PM_REMOVE))
            DispatchMessage(&m);
    vdump("after yielding");
    out("TK42 done\r\n");
    return 0;
}
