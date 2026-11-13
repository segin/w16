/*
 * tk6.c - floating-point probe: how the loader treats the floating-point
 * OS fixups of a program (the bytes of our own fixup sites after loading,
 * tk6fp.asm), compiled floating-point code (this file is built with -fpi,
 * so its floating-point instructions carry those fixups), and WIN87EM's
 * exports: __fpMath (register interface, function number in BX),
 * __Win87EmInfo, __Win87EmSave, __Win87EmRestore, and what reaches the
 * program on an unmasked floating-point exception.
 *
 * It works with a coprocessor and without one (the emulator): every
 * floating-point instruction, also in tk6fp.asm, carries an emulator
 * fixup. Observations are named "87.*" with a coprocessor and "em.*"
 * without (GetWinFlags WF_80x87), so one reference table holds both.
 *
 * Output and checks: see tklog.h; the reference values are in tk6ref.h
 * (tests/tk6ref.sh). The exit code is the number of failures. Calls that
 * might hang or fault come last.
 */
#include <windows.h>

#define TK_REF "tk6ref.h"
#include "tklog.h"

#define STEP(s) out("-- " s "\r\n")

/* tk6fp.asm */
extern BYTE FAR * __cdecl fix_site(WORD i);
extern void __cdecl regcall(FARPROC fn, WORD FAR *r);
extern WORD __cdecl fpu_cw(void);
extern WORD __cdecl fpu_sw(void);
extern void __cdecl fpu_setcw(WORD cw);
extern void __cdecl fpu_init(void);
extern void __cdecl fpu_clex(void);
extern WORD __cdecl fpu_div0(void);
extern WORD __cdecl fpu_invalid(void);
extern void __cdecl fp_ldq(void);
extern void __cdecl fp_stpq(void);
extern void __cdecl fp_fistpd(void);
extern void __cdecl fp_ld1(void);
extern void __cdecl fp_ldz(void);
extern void __cdecl fp_pop(void);
extern void __cdecl fpu_cw_code(void);
extern void __cdecl fp_handler(void);
extern void __cdecl fp_handler2(void);
extern WORD __cdecl h2_calls;
extern double __cdecl fq;
extern WORD __cdecl fw;
extern DWORD __cdecl fd;
extern WORD __cdecl h_regs[12];         /* ax bx cx dx si di bp sp ds es ss fl */
extern WORD __cdecl h_stack[8];
extern WORD __cdecl h_calls, h_sw, h_cw;

/* WIN87EM, imported by ordinal (tk6.lnk) */
extern void FAR PASCAL FPMATH(void);                    /* 1, registers */
extern WORD FAR PASCAL FPINFO(void FAR *buf, WORD n);   /* 3 */
extern WORD FAR PASCAL FPRESTORE(void FAR *buf, WORD n);/* 4 */
extern WORD FAR PASCAL FPSAVE(void FAR *buf, WORD n);   /* 5 */

enum { R_AX, R_BX, R_CX, R_DX, R_SI, R_DI, R_ES, R_FL, R_DS, R_SPD };

static WORD sel_of(const void FAR *p) { return (WORD)((DWORD)p >> 16); }
static WORD off_of(const void FAR *p) { return (WORD)(DWORD)p; }

static int have87;

/* "87." or "em." + name [+ "." + n] [+ "." + suffix] */
static void pname(char *b, const char *name, int n, const char *suffix)
{
    WORD i = 0;
    const char *m = have87 ? "87." : "em.";
    while (*m)
        b[i++] = *m++;
    while (*name && i < 40)
        b[i++] = *name++;
    if (n >= 0) {
        b[i++] = '.';
        if (n >= 100)
            b[i++] = (char)('0' + n / 100);
        if (n >= 10)
            b[i++] = (char)('0' + n / 10 % 10);
        b[i++] = (char)('0' + n % 10);
    }
    if (suffix) {
        b[i++] = '.';
        while (*suffix && i < 60)
            b[i++] = *suffix++;
    }
    b[i] = 0;
}

static void ob(const char *name, int n, const char *suffix, WORD v)
{
    char b[64];
    pname(b, name, n, suffix);
    obs(b, v);
}

static void obe(const char *name, int n, const char *suffix, WORD v)
{
    char b[64];
    pname(b, name, n, suffix);
    obs_env(b, v);
}

/* control word without bit 6 (reserved; DOSBox reads it as 0, DOSBox-X
   and a real 387 as 1) */
static WORD cwm(WORD cw)
{
    return cw & ~0x40u;
}

/* ---- the loader's treatment of OS fixups ---- */

static void probe_fixups(void)
{
    WORD i, k;
    static const char *const n[] = { "w0", "w1", "w2", "w3" };
    STEP("fixups");
    for (i = 0; i < 8; i++) {
        WORD FAR *w = (WORD FAR *)fix_site(i);
        for (k = 0; k < 4; k++)
            ob("fix", i, n[k], w[k]);
    }
}

/* the first FWAIT+ESC and NOP+FWAIT of fpu_cw, after it ran: patched back
   or left as the loader made them? */
static void probe_backpatch(void)
{
    BYTE FAR *c = (BYTE FAR *)fpu_cw_code;
    WORD i;
    static const char *const n[] = { "w0", "w1", "w2", "w3", "w4", "w5" };
    fpu_cw();
    fpu_cw();
    for (i = 0; i < 6; i++)
        ob("cwcode", -1, n[i], *(WORD FAR *)(c + 6 + 2 * i));
}

/* ---- compiled floating-point code (fixups applied by the loader) ---- */

double fa = 1.5, fb = 2.25, fc;
float ff = 3.0f;

static WORD dw(double FAR *d, WORD i)
{
    return ((WORD FAR *)d)[i];
}

static double mul_far(double FAR *p, double FAR *q)
{
    return *p * *q;
}

static void probe_compiled(void)
{
    double r, s;
    STEP("compiled");
    fc = fa * fb + ff;                  /* 6.375 = 0x4019800000000000 */
    check(dw(&fc, 3) == 0x4019 && dw(&fc, 2) == 0x8000 &&
          dw(&fc, 1) == 0 && dw(&fc, 0) == 0, "compiled fa*fb+ff");
    r = mul_far(&fa, &fb);              /* 3.375 = 0x400B000000000000 */
    check(dw(&r, 3) == 0x400B && dw(&r, 2) == 0 && dw(&r, 1) == 0 &&
          dw(&r, 0) == 0, "compiled far *p * *q");
    s = fc > fa ? fc / fb : 0.0;        /* 2.8333.. = 0x4006AAAAAAAAAAAB */
    check(dw(&s, 3) == 0x4006 && dw(&s, 0) == 0xAAAB, "compiled compare+divide");
    ob("compiled.cw", -1, NULL, cwm(fpu_cw()));
    ob("compiled.sw", -1, NULL, fpu_sw());
}

/* ---- __fpMath ---- */

static WORD r[10];

static void fpm(WORD bx, WORD ax, WORD dx)
{
    WORD i;
    for (i = 0; i < 10; i++)
        r[i] = 0;
    r[R_AX] = ax;
    r[R_BX] = bx;
    r[R_DX] = dx;
    r[R_CX] = 0x5A5A;
    r[R_SI] = 0x5151;
    r[R_DI] = 0xD1D1;
    r[R_ES] = 0;
    regcall((FARPROC)FPMATH, r);
}

/* call function n (tag t for the names) and report: AX and CF checked;
   DX checked if dx_ok; BX, CX, DX otherwise only shown; SI, DI (in:
   5151h, D1D1h), DS and SP. */
static void fpm_rep(const char *t, int dx_ok)
{
    ob(t, -1, "ax", r[R_AX]);
    ob(t, -1, "cf", r[R_FL] & 1);
    if (dx_ok)
        ob(t, -1, "dx", r[R_DX]);
    else
        obe(t, -1, "dx", r[R_DX]);
    obe(t, -1, "bx", r[R_BX]);
    obe(t, -1, "cx", r[R_CX]);
    if (have87 || t[1] != '7') {
        ob(t, -1, "si", r[R_SI]);
        ob(t, -1, "di", r[R_DI]);
    } else {
        obe(t, -1, "si", r[R_SI]);
        obe(t, -1, "di", r[R_DI]);
    }
    ob(t, -1, "ds=DS", r[R_DS] == entry_regs[E_DS]);
    ob(t, -1, "spd", r[R_SPD]);
}

/* C0, C2 and C3 (4500h) of a status word read after FDIV or FSQRT, whose
   condition codes Intel leaves undefined (FPREM's only on an invalid
   operand): under DOSBox-X they come back now 0 and now 4000h or 0100h,
   from Windows 3.1's own run too (TK6 on Microsoft's KRNL286 and WIN87EM:
   15 of 24 runs with C3 or C0 set in 87.div0.sw and the three after
   it), so they are left out of the comparison */
#define SW_DEFINED(sw) ((sw) & ~0x4500)

static void fpm_state(const char *t)
{
    char b[64];
    WORD i = 0;
    while (t[i] && i < 50) {
        b[i] = t[i];
        i++;
    }
    b[i++] = '.';
    b[i++] = 'c';
    b[i++] = 'w';
    b[i] = 0;
    ob(b, -1, NULL, cwm(fpu_cw()));
    b[i - 1] = 0;
    b[i - 2] = 's';
    b[i - 1] = 'w';
    b[i] = 0;
    ob(b, -1, NULL, SW_DEFINED(fpu_sw()));
}

static void dbl(double v)
{
    fq = v;
    fp_ldq();
}

static WORD top_of(WORD sw)
{
    return (sw >> 11) & 7;
}

static void probe_fpmath(void)
{
    static const WORD cws[] = { 0x133F, 0x0000, 0x0C3F, 0xFFFF, 0x1372, 0x037F };
    static const WORD rcs[] = { 0x0000, 0x0400, 0x0800, 0x0C00 };
    WORD i, t;
    FARPROC h = (FARPROC)fp_handler;

    STEP("fpmath");
    ob("start.cw", -1, NULL, cwm(fpu_cw()));
    ob("start.sw", -1, NULL, fpu_sw());

    /* 0: initialise, as OpenWatcom's run-time does (DX:AX = its handler) */
    fpu_setcw(0x037F);
    fpu_div0();
    fpm(0, off_of(h), sel_of(h));
    fpm_rep("f0", 0);
    fpm_state("f0");
    fpm(11, 0x1111, 0x2222);
    fpm_rep("f11", 1);

    /* 3: the exception handler */
    fpm(3, off_of(h), sel_of(h));
    fpm_rep("f3", 0);
    fpm_state("f3");

    /* 4: set the control word; 5: read it back */
    STEP("f4");
    for (i = 0; i < sizeof cws / sizeof cws[0]; i++) {
        fpm(4, cws[i], 0);
        ob("f4", i, "ax", r[R_AX]);
        ob("f4", i, "cf", r[R_FL] & 1);
        ob("f4", i, "cw", cwm(fpu_cw()));
        fpm(5, 0x5555, 0);
        ob("f5", i, "ax", r[R_AX]);
    }
    fpu_setcw(0x0C7F);                  /* behind its back */
    fpm(5, 0x5555, 0);
    ob("f5.direct", -1, "ax", r[R_AX]);
    fpm(4, 0x1332, 0);

    /* 12: AX in */
    ob("calls.before.f12", -1, NULL, h_calls);
    STEP("f12");
    fpm(12, 0x1234, 0);
    fpm_rep("f12", 0);
    fpm_state("f12");
    fpm(5, 0x5555, 0);
    ob("f12.f5", -1, "ax", r[R_AX]);
    fpm(4, 0x1332, 0);

    /* 10: stack depth */
    ob("calls.before.f10", -1, NULL, h_calls);
    STEP("f10");
    fpu_init();
    fpm(4, 0x1332, 0);
    fpm(10, 0x5555, 0);
    ob("f10.0", -1, "ax", r[R_AX]);
    fp_ld1();
    fp_ld1();
    fp_ld1();
    fpm(10, 0x5555, 0);
    fpm_rep("f10.3", 0);
    fp_pop();
    fp_pop();
    fp_pop();

    /* 6: round ST to an integer with the rounding control in AX? */
    ob("calls.before.f6", -1, NULL, h_calls);
    STEP("f6");
    for (i = 0; i < 4; i++) {
        dbl(2.5);
        fpm(6, rcs[i], 0);
        ob("f6", i, "ax", r[R_AX]);
        ob("f6", i, "cw", cwm(fpu_cw()));
        t = top_of(fpu_sw());
        fp_stpq();
        ob("f6", i, "st", dw(&fq, 3));
        ob("f6", i, "top", t);
    }
    fpm(4, 0x1332 | 0x0C00, 0);         /* CW chop, AX nearest */
    dbl(-2.75);
    fpm(6, 0, 0);
    fp_stpq();
    ob("f6.cwchop", -1, "st", dw(&fq, 3));
    fpm(4, 0x1332, 0);

    /* 7: pop ST into DX:AX as an integer? */
    ob("calls.before.f7", -1, NULL, h_calls);
    STEP("f7");
    fpu_init();
    fpm(4, 0x1332, 0);
    t = top_of(fpu_sw());
    dbl(74565.75);
    fpm(7, 0x5555, 0x5555);
    fpm_rep("f7.a", 1);     /* em: SI and DI change (00AFh, 0) */
    ob("f7.a.pop", -1, NULL, top_of(fpu_sw()) == t);
    dbl(-3.5);
    fpm(7, 0, 0);
    ob("f7.b", -1, "ax", r[R_AX]);
    ob("f7.b", -1, "dx", r[R_DX]);
    dbl(2.5);
    fpm(7, 0, 0);
    ob("f7.c", -1, "ax", r[R_AX]);
    ob("f7.c", -1, "dx", r[R_DX]);
    dbl(-3.5);
    fpm(7, 0x0C00, 0);
    ob("f7.d", -1, "ax", r[R_AX]);
    ob("f7.d", -1, "dx", r[R_DX]);
    dbl(2.25);
    fpm(7, 0x0800, 0);
    ob("f7.e", -1, "ax", r[R_AX]);
    ob("f7.e", -1, "dx", r[R_DX]);
    fpm(4, 0x133F, 0);                  /* all masked */
    fpu_clex();
    dbl(5000000000.0);
    fpm(7, 0, 0);
    ob("f7.big", -1, "ax", r[R_AX]);
    ob("f7.big", -1, "dx", r[R_DX]);
    ob("f7.big.sw", -1, NULL, fpu_sw());
    fpu_clex();
    fpm(4, 0x1332, 0);

    /* 8: status; 9: clear? */
    ob("calls.before.f8", -1, NULL, h_calls);
    STEP("f8");
    fpu_init();
    fpm(4, 0x1332 | 0x3F, 0);           /* all masked */
    fpu_div0();
    fpm(8, 0x5555, 0);
    fpm_rep("f8.z", 0);
    fpm_state("f8.z");
    fpm(8, 0x5555, 0);
    ob("f8.again", -1, "ax", r[R_AX]);
    fpu_invalid();
    fpm(8, 0x5555, 0);
    ob("f8.inv", -1, "ax", r[R_AX]);
    fpu_div0();
    fpm(9, 0x5555, 0);
    fpm_rep("f9", 0);
    fpm_state("f9");
    fpm(8, 0x5555, 0);
    ob("f9.f8", -1, "ax", r[R_AX]);

    /* 1 */
    ob("calls.before.f1", -1, NULL, h_calls);
    STEP("f1");
    fpu_div0();
    fpm(1, 0x5555, 0);
    fpm_rep("f1", 0);
    fpm_state("f1");
    ob("handler.calls", -1, NULL, h_calls);
}

/* ---- __Win87EmInfo / Save / Restore ---- */

static BYTE buf[600];

static WORD extent(void)
{
    WORD i, last = 0;
    for (i = 0; i < sizeof buf; i++)
        if (buf[i] != 0xCC)
            last = i + 1;
    return last;
}

static void fill(void)
{
    WORD i;
    for (i = 0; i < sizeof buf; i++)
        buf[i] = 0xCC;
}

static void probe_info(void)
{
    static const WORD sizes[] = { 0, 1, 11, 12, 64 };
    WORD v, i, n, need;
    STEP("info");
    for (i = 0; i < sizeof sizes / sizeof sizes[0]; i++) {
        fill();
        v = FPINFO(buf, sizes[i]);
        ob("info", sizes[i], "ret", v);
        ob("info", sizes[i], "extent", extent());
    }
    fill();
    FPINFO(buf, 64);
    n = extent();
    for (i = 0; i < n; i += 2) {
        if (i == 4 || i == 6)
            obe("info.w", i, NULL, *(WORD *)(buf + i));
        else
            ob("info.w", i, NULL, *(WORD *)(buf + i));
    }
    need = *(WORD *)(buf + 2);

    STEP("save");
    fpu_init();
    fpm(4, 0x1332, 0);
    dbl(6.375);
    fpu_setcw(0x0F7F);
    fill();
    v = FPSAVE(buf, need - 1);
    ob("save.short.ret", -1, NULL, v);
    ob("save.short.extent", -1, NULL, extent());
    fill();
    v = FPSAVE(buf, need);
    ob("save.ret", -1, NULL, v);
    ob("save.extent", -1, NULL, extent());
    ob("save.cw.after", -1, NULL, cwm(fpu_cw()));
    ob("save.sw.after", -1, NULL, fpu_sw());
    for (i = 0; i < 14; i += 2)
        ob("save.w", i, NULL, *(WORD *)(buf + i));
    n = extent();
    for (i = 14; i < n; i += 2)
        obe("save.w", i, NULL, *(WORD *)(buf + i));
    STEP("restore");
    fpu_init();
    fpu_setcw(0x037F);
    v = FPRESTORE(buf, need - 1);
    ob("restore.short.ret", -1, NULL, v);
    ob("restore.short.cw", -1, NULL, cwm(fpu_cw()));
    v = FPRESTORE(buf, need);
    ob("restore.ret", -1, NULL, v);
    ob("restore.cw", -1, NULL, cwm(fpu_cw()));
    ob("restore.top", -1, NULL, top_of(fpu_sw()));
    fpu_setcw(0x037F);
    fp_stpq();
    ob("restore.st", -1, NULL, dw(&fq, 3));
    fpu_init();
}

/* ---- exceptions ---- */

static void handler_report(const char *t)
{
    static const char *const n[] = { "ax", "bx", "cx", "dx", "si", "di",
                                     "bp", "sp", "ds", "es", "ss", "fl" };
    WORD i;
    char b[32];
    ob(t, -1, "calls", h_calls);
    if (!h_calls)
        return;
    for (i = 0; i < 12; i++) {
        WORD k = 0;
        const char *p = t;
        while (*p)
            b[k++] = *p++;
        b[k++] = '.';
        p = n[i];
        while (*p)
            b[k++] = *p++;
        b[k] = 0;
        obe(b, -1, NULL, h_regs[i]);
        if (i == 0)
            ob(t, -1, "al", h_regs[0] & 0xFF);  /* AH varies between runs */
    }
    ob(t, -1, "ds=DS", h_regs[8] == entry_regs[E_DS]);
    ob(t, -1, "sw", h_sw);
    ob(t, -1, "cw", cwm(h_cw));
    for (i = 0; i < 8; i++)
        obe(t, i, "stack", h_stack[i]);
}

static void probe_exceptions(void)
{
    FARPROC h = (FARPROC)fp_handler;
    WORD sw;
    STEP("exceptions");
    fpu_init();
    fpm(0, off_of(h), sel_of(h));
    fpm(3, off_of(h), sel_of(h));
    h_calls = 0;
    fpm(4, 0x133B, 0);                  /* ZE unmasked */
    sw = fpu_div0();
    ob("div0.sw", -1, NULL, SW_DEFINED(sw));
    ob("div0.cw", -1, NULL, cwm(fpu_cw()));
    handler_report("div0.h");
    h_calls = 0;
    fpu_clex();
    fpm(4, 0x133E, 0);                  /* IM unmasked */
    sw = fpu_invalid();
    ob("inv.sw", -1, NULL, SW_DEFINED(sw));
    handler_report("inv.h");
    fpu_clex();

    /* does function 0 set the handler too? */
    fpm(3, off_of((FARPROC)fp_handler2), sel_of((FARPROC)fp_handler2));
    fpm(0, off_of(h), sel_of(h));
    h_calls = h2_calls = 0;
    fpm(4, 0x133B, 0);
    fpu_div0();
    ob("f0handler.h", -1, "calls", h_calls);
    ob("f0handler.h2", -1, "calls", h2_calls);
    fpu_clex();
    fpm(3, off_of(h), sel_of(h));
    fpm(4, 0x1332, 0);
}

/* the emulator's status word and how the handler is called, in detail */
static void probe_details(void)
{
    FARPROC h = (FARPROC)fp_handler;
    WORD k, i;
    STEP("details");
    fpm(3, off_of(h), sel_of(h));
    fpm(4, 0x1332, 0);
    h_calls = 0;
    fpu_init();
    ob("finit.1332.calls", -1, NULL, h_calls);
    ob("finit.1332.ax", -1, NULL, h_regs[0] & 0xFF);
    fpu_init();
    ob("finit2.1332.calls", -1, NULL, h_calls);
    fpm(4, 0x133F, 0);
    h_calls = 0;
    fpu_init();
    ob("finit.133F.calls", -1, NULL, h_calls);
    fpm(4, 0x1332, 0);
    fpu_setcw(0x0F7F);
    fpu_init();
    ob("finit.0F7F.cw", -1, NULL, cwm(fpu_cw()));
    fpm(9, 0, 0);
    fpm(4, 0x133F, 0);
    fpu_init();
    ob("finit.sw", -1, NULL, fpu_sw());
    ob("finit.cw", -1, NULL, cwm(fpu_cw()));
    fpm(0, 0, 0);
    ob("f0.after.finit.sw", -1, NULL, fpu_sw());
    fp_ld1();
    ob("push1.sw", -1, NULL, fpu_sw());
    fp_ld1();
    ob("push2.sw", -1, NULL, fpu_sw());
    fp_pop();
    ob("pop1.sw", -1, NULL, fpu_sw());
    fp_pop();
    ob("pop2.sw", -1, NULL, fpu_sw());
    fp_pop();
    ob("pop.empty.sw", -1, NULL, fpu_sw());
    fpm(9, 0, 0);
    fpm(4, 0x133F, 0);
    fpu_clex();
    ob("inv.masked.sw", -1, NULL, SW_DEFINED(fpu_invalid()));
    fpu_clex();
    ob("div0.masked.sw", -1, NULL, SW_DEFINED(fpu_div0()));
    fpm(8, 0, 0);
    ob("masked.f8", -1, NULL, r[R_AX]);
    fpm(9, 0, 0);
    fpm(3, off_of(h), sel_of(h));
    for (k = 0; k < 3; k++) {
        fpu_init();
        fpm(4, 0x133B, 0);
        for (i = 0; i < k; i++)
            fp_ld1();
        h_calls = 0;
        fpu_div0();
        ob("depth.calls", k, NULL, h_calls);
        ob("depth.al", k, NULL, h_regs[0] & 0xFF);
        obe("depth.ax", k, NULL, h_regs[0]);
        fpu_clex();
    }
    /* does function 0 empty the register stack? */
    fpu_init();
    fpm(4, 0x133F, 0);
    fp_ld1();
    fp_ld1();
    fpm(0, 0, 0);
    fpm(10, 0, 0);
    ob("f0.depth", -1, NULL, r[R_AX]);
    ob("f0.depth.sw", -1, NULL, fpu_sw());
    fpu_init();
    fpm(4, 0x1332, 0);
}

int __cdecl tmain(void)
{
    out("TK6 start\r\n");
    have87 = (GetWinFlags() & WF_80x87) != 0;
    obs_env("WF_80x87", have87);
    ob("start0.cw", -1, NULL, cwm(fpu_cw()));
    probe_fixups();
    probe_compiled();
    probe_backpatch();
    probe_fpmath();
    probe_info();
    probe_exceptions();
    probe_details();
    STEP("terminate");
    fpm(2, 0, 0);
    fpm_rep("f2", 0);
    fpm_state("f2");
    out("TK6 done, failures=");
    outhex((WORD)failures);
    out("\r\n");
    return failures;
}
