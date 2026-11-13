/*
 * tk48.c - what INT 15h C2xxh (the PS/2 pointing device) and INT 33h
 * functions 0 and 21h return to a protected-mode caller, to compare
 * Microsoft's DOSX with w16's (MOUSE.DRV chooses between the BIOS
 * interface and an INT 33h driver). C2xx calls that would change the
 * device (enable, reset) are only made on request (tail "c"). Run as the
 * shell (tests/dropin.sh, -x for w16's DOSX).
 */
#include <windows.h>

#define TK_REF "tk48ref.h"
#include "tklog.h"

static DWORD i15(WORD ax, WORD bx);
#pragma aux i15 = "int 15h" "mov dx, ax" "sbb ax, ax" "xchg ax, dx" \
    parm [ax] [bx] value [dx ax] modify [bx cx es];
static WORD i33(WORD ax);
#pragma aux i33 = "int 33h" parm [ax] value [ax] modify [bx cx dx es];

static void c2(const char *n, WORD ax, WORD bx)
{
    DWORD r = i15(ax, bx);
    char t[32];
    wsprintf(t, "%s AH", (LPSTR)n);
    obs(t, LOWORD(r) >> 8);
    wsprintf(t, "%s CF", (LPSTR)n);
    obs(t, HIWORD(r) & 1);
}

int __cdecl tmain(void)
{
    LPSTR tail = (LPSTR)MAKELP(init_regs[6], init_regs[1]);
    out("TK48 start\r\n");
    c2("C204 (type)", 0xC204, 0);
    c2("C206 00 (status)", 0xC206, 0x0000);
    c2("C203 (resolution 2)", 0xC203, 0x0200);
    if (tail[0] == 'c' || tail[1] == 'c') {
        c2("C205 (init, 3 bytes)", 0xC205, 0x0300);
        c2("C201 (reset)", 0xC201, 0);
        c2("C200 (enable)", 0xC200, 0x0100);
    }
    obs("INT 33h 0 AX", i33(0));
    obs("INT 33h 21h AX", i33(0x21));
    out("TK48 done\r\n");
    return 0;
}
