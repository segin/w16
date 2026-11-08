/*
 * tk9.c - WIN87EM probe, part 3: __fpMath functions above 12 and the
 * unnamed export ordinal 2, called black-box with known registers; what
 * comes back in the registers, the stack pointer, and the coprocessor's
 * (or emulator's) control and status words and stack depth. Run with
 * and without a coprocessor (tests/tk6ref.sh). Each call is logged
 * before it is made, so a run that dies shows where.
 */
#include <windows.h>

#define TK_REF "tk9ref.h"
#include "tklog.h"

#define STEP(s) out("-- " s "\r\n")

extern void FAR PASCAL FPMATH(void);
extern void __cdecl regcall(FARPROC fn, WORD FAR *r);
extern void __cdecl regcall3(FARPROC fn, WORD FAR *r, WORD a1, WORD a2, WORD a3);
extern WORD __cdecl fpu_cw(void);
extern WORD __cdecl fpu_sw(void);
extern void __cdecl fp_ld1(void);

enum { R_AX, R_BX, R_CX, R_DX, R_SI, R_DI, R_ES, R_FL, R_DS, R_SPD };

static int have87;
static WORD r[10];
static BYTE buf[512];
#ifndef ARG2
#define ARG2 sizeof buf
#endif

static void ob(const char *prefix, WORD n, const char *suffix, WORD v, int env)
{
    char b[48];
    WORD k = 0;
    const char *p = have87 ? "87." : "em.";
    while (*p)
        b[k++] = *p++;
    while (*prefix)
        b[k++] = *prefix++;
    b[k++] = '.';
    if (n >= 10)
        b[k++] = (char)('0' + n / 10);
    b[k++] = (char)('0' + n % 10);
    b[k++] = '.';
    while (*suffix)
        b[k++] = *suffix++;
    b[k] = 0;
    if (env)
        obs_env(b, v);
    else
        obs(b, v);
}

static WORD depth(void)
{
    WORD q[10], i;
    for (i = 0; i < 10; i++)
        q[i] = 0;
    q[R_BX] = 10;
    regcall((FARPROC)FPMATH, q);
    return q[R_AX];
}

static void call(const char *t, WORD n, FARPROC fn, WORD ax, WORD bx, WORD cx,
                 WORD dx, WORD es)
{
    WORD i;
    int env = t[0] == 'i';              /* function 0 between calls: shown only */
    for (i = 0; i < 10; i++)
        r[i] = 0;
    r[R_AX] = ax;
    r[R_BX] = bx;
    r[R_CX] = cx;
    r[R_DX] = dx;
    r[R_SI] = 0x5151;
    r[R_DI] = 0xD1D1;
    r[R_ES] = es;
    regcall(fn, r);
    ob(t, n, "ax", r[R_AX], env);
    ob(t, n, "bx", r[R_BX], env);
    ob(t, n, "cx", r[R_CX], env);
    ob(t, n, "dx", r[R_DX], env);
    ob(t, n, "si", r[R_SI], env);
    ob(t, n, "di", r[R_DI], env);
    ob(t, n, "es=in", r[R_ES] == es, env);
    ob(t, n, "cf", r[R_FL] & 1, env);
    ob(t, n, "ds=DS", r[R_DS] == entry_regs[E_DS], env);
    ob(t, n, "spd", r[R_SPD], env);
    ob(t, n, "cw", fpu_cw() & ~0x40u, env);
    ob(t, n, "sw", fpu_sw(), env);
    ob(t, n, "depth", depth(), env);
}

int __cdecl tmain(void)
{
    WORD n;
    FARPROC o2;
    out("TK9 start\r\n");
    have87 = (GetWinFlags() & WF_80x87) != 0;
    obs_env("WF_80x87", have87);
    call("init", 0, (FARPROC)FPMATH, 0, 0, 0, 0, 0);
    STEP("fpmath 13-24");
    for (n = 13; n <= 24; n++) {
        out("-- fpm ");
        outhex(n);
        out("\r\n");
        fp_ld1();
        call("fpm", n, (FARPROC)FPMATH, 0x1234, n, 0x5A5A, 0x5678, 0);
        call("init", n, (FARPROC)FPMATH, 0, 0, 0, 0, 0);
    }
    STEP("ordinal 2");
    o2 = GetProcAddress(GetModuleHandle("WIN87EM"), MAKEINTRESOURCE(2));
    obs("ord2 found", o2 != NULL);
    /* With all registers 0 (and no arguments) the reference run ended at
       the call. Here every pointer register points to a zeroed buffer
       and three words are pushed: the buffer's selector and offset and
       its size, as for __Win87EmSave. */
    if (o2) {
        WORD i, last = 0, sel = (WORD)((DWORD)(void FAR *)buf >> 16);
        WORD off = (WORD)(DWORD)(void FAR *)buf;
        for (i = 0; i < sizeof buf; i++)
            buf[i] = 0;
        for (i = 0; i < 10; i++)
            r[i] = 0;
        r[R_BX] = r[R_SI] = r[R_DI] = off;
        r[R_ES] = sel;
        out("-- ord2 with pointers\r\n");
        /* one word argument (observed: it pops 2 bytes) */
        fp_ld1();
        fp_ld1();
        regcall3(o2, r, sel, off, ARG2);
        /* BX, SI and DI came back as 01A6h on Windows 3.1 in every run
           (an internal value, it seems): shown only */
        ob("ord2", 0, "ax", r[R_AX], 0);
        ob("ord2", 0, "bx", r[R_BX], 1);
        ob("ord2", 0, "cx", r[R_CX], 0);
        ob("ord2", 0, "dx", r[R_DX], 0);
        ob("ord2", 0, "si", r[R_SI], 1);
        ob("ord2", 0, "di", r[R_DI], 1);
        ob("ord2", 0, "cf", r[R_FL] & 1, 0);
        ob("ord2", 0, "spd", r[R_SPD], 0);
        ob("ord2", 0, "cw", fpu_cw() & ~0x40u, 0);
        ob("ord2", 0, "sw", fpu_sw(), 0);
        ob("ord2", 0, "depth", depth(), 0);
        for (i = 0; i < sizeof buf; i++)
            if (buf[i])
                last = i + 1;
        ob("ord2", 0, "extent", last, 0);
        for (i = 0; i < last && i < 64; i += 2)
            ob("ord2.buf", i, "w", *(WORD *)(buf + i), 0);
    }
    out("TK9 done, failures=");
    outhex((WORD)failures);
    out("\r\n");
    return failures;
}
