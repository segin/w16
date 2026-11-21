/*
 * tk67h.c - TK67H.EXE, the helper task of the WIN87EM audit probe's
 * two-task part (tests/apps/tk67.c, TK67P4): looks at WIN87EM's state
 * before and after its own __fpMath 0, sets its own handler and control
 * word, pushes nine values (one more than the coprocessor's eight) and
 * pops them, writes what it saw into TK67P4's tk67sh (tk67.h), and ends
 * after __fpMath 2, as a C run-time does. No log of its own. Built with
 * -fpi.
 */
#include <windows.h>
#include "tk67.h"

extern WORD __cdecl init_regs[7];    /* ax bx cx dx si di es */
extern void FAR PASCAL FPMATH(void);
extern void __cdecl regcall(FARPROC fn, WORD FAR *r);
extern void __cdecl fp_handler2(void);
extern WORD __cdecl h2_calls;

static WORD rr[10];

static WORD fpm(WORD bx, WORD ax, WORD dx)
{
    WORD i;
    for (i = 0; i < 10; i++)
        rr[i] = 0;
    rr[0] = ax;
    rr[1] = bx;
    rr[3] = dx;
    regcall((FARPROC)FPMATH, rr);
    return rr[0];
}

static WORD hex(const char FAR **p)
{
    WORD v = 0;
    const char FAR *s = *p;
    while (*s == ' ')
        s++;
    for (;;) {
        char c = *s;
        if (c >= '0' && c <= '9')
            v = v * 16 + (c - '0');
        else if (c >= 'A' && c <= 'F')
            v = v * 16 + (c - 'A' + 10);
        else
            break;
        s++;
    }
    *p = s;
    return v;
}

int __cdecl tmain(void)
{
    const char FAR *cmd = MAKELP(init_regs[6], init_regs[1]);
    WORD ds, off, mode, k, good = 0;
    tk67sh FAR *sh;
    FARPROC h = (FARPROC)fp_handler2;
    static short v;
    ds = hex(&cmd);
    off = hex(&cmd);
    mode = hex(&cmd);
    sh = (tk67sh FAR *)MAKELP(ds, off);
    sh->task = (WORD)GetCurrentTask();
    sh->cw0 = fpm(5, 0, 0);
    sh->depth0 = fpm(10, 0, 0);
    fpm(0, 0, 0);
    sh->cw1 = fpm(5, 0, 0);
    sh->depth1 = fpm(10, 0, 0);
    fpm(3, (WORD)(DWORD)h, (WORD)((DWORD)h >> 16));
    fpm(4, 0x133F, 0);
    for (k = 1; k <= 9; k++) {
        v = (short)k;
        _asm {
            fild v
        }
    }
    for (k = 9; k >= 1; k--) {
        _asm {
            fistp v
        }
        if (v == (short)k && good == 9 - k)
            good++;
    }
    sh->good = good;
    sh->depth2 = fpm(10, 0, 0);
    sh->hcalls = h2_calls;
    if (mode == 1) {
        sh->ready = 1;
        for (k = 0; k < 2000 && !sh->go; k++)
            Yield();
        sh->hcalls1 = h2_calls;
        _asm {
            fld1
            fchs
            fsqrt
            fstp st(0)
        }
        sh->hcalls2 = h2_calls;
    }
    if (mode != 2)
        fpm(2, 0, 0);
    sh->done = 1;
    return 0;
}
