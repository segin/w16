/*
 * tk6n.c - a program with floating-point OS fixups that does not import
 * WIN87EM: is WIN87EM loaded for it, and does its floating-point code run
 * (with a coprocessor NOP FWAIT becomes INT 3Dh; without one every
 * instruction becomes INT 34h-3Dh)? Built with -fpi and linked with
 * fixrqq.asm only. Companion of tk6.c (tests/tk6ref.sh).
 */
#include <windows.h>

#define TK_REF "tk6nref.h"
#include "tklog.h"

double fa = 1.5, fb = 2.25, fc;

static WORD dw(double FAR *d, WORD i)
{
    return ((WORD FAR *)d)[i];
}

int __cdecl tmain(void)
{
    int have87 = (GetWinFlags() & WF_80x87) != 0;
    out("TK6N start\r\n");
    obs_env("WF_80x87", have87);
    obs("WIN87EM loaded before", GetModuleHandle("WIN87EM") != 0);
    fc = fa * fb;                       /* 3.375 = 0x400B000000000000 */
    /* without a coprocessor nothing emulates it (observed) */
    obs(have87 ? "87.compiled fa*fb ok" : "em.compiled fa*fb ok",
        dw(&fc, 3) == 0x400B && dw(&fc, 2) == 0);
    obs("WIN87EM loaded after", GetModuleHandle("WIN87EM") != 0);
    out("TK6N done, failures=");
    outhex((WORD)failures);
    out("\r\n");
    return failures;
}
