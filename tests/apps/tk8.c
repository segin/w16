/*
 * tk8.c - floating-point probe, part 2: the transcendental instructions
 * (F2XM1, FYL2X, FYL2XP1, FPTAN, FPATAN), packed BCD (FBLD, FBSTP),
 * FPREM, FSCALE, FXTRACT, 64-bit integers, square roots, and the
 * contents of WIN87EM's save area, with a coprocessor and with WIN87EM's
 * emulator (tests/tk6ref.sh). Inline assembly in a file built with -fpi,
 * so every instruction carries an emulator fixup.
 *
 * Observations are named "87.*" or "em.*" by configuration. Every result
 * is the full register (80 bits) or memory operand, the exception flags
 * (and the condition codes where they mean something), and the number
 * of calls to the exception handler with the code of the last (AL).
 * Exceptions are masked (control word 037Fh).
 */
#include <windows.h>

#ifdef TK8_T
#define TK_REF "tk8tref.h"
#else
#define TK_REF "tk8ref.h"
#endif
#include "tklog.h"

#define STEP(s) out("-- " s "\r\n")

extern void FAR PASCAL FPMATH(void);
extern WORD FAR PASCAL FPSAVE(void FAR *buf, WORD n);
extern WORD FAR PASCAL FPRESTORE(void FAR *buf, WORD n);
extern void __cdecl regcall(FARPROC fn, WORD FAR *r);
extern void __cdecl fp_handler(void);
extern WORD __cdecl h_regs[12];
extern WORD __cdecl h_calls;

static int have87;
static WORD cwv, swv;
static double qa, qb;
static BYTE t80[10], t80b[10];
static BYTE bcd[10];
static long i64[2];
static WORD rr[10];

static void fpm(WORD bx, WORD ax, WORD dx)
{
    WORD i;
    for (i = 0; i < 10; i++)
        rr[i] = 0;
    rr[0] = ax;
    rr[1] = bx;
    rr[3] = dx;
    regcall((FARPROC)FPMATH, rr);
}

/* "87." / "em." + prefix + "." + n + "." + suffix */
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
    if (suffix) {
        b[k++] = '.';
        while (*suffix)
            b[k++] = *suffix++;
    }
    b[k] = 0;
    if (env)
        obs_env(b, v);
    else
        obs(b, v);
}

/* number of registers in use (__fpMath function 10) */
static WORD depth(void)
{
    fpm(10, 0, 0);
    return rr[0];
}

/* record the depth, then pop up to two registers into t80, t80b (CCh if
   not there) and start again from an empty stack (function 0) */
static void take(const char *op, WORD i)
{
    WORD n = depth(), j;
    for (j = 0; j < 10; j++)
        t80[j] = t80b[j] = 0xCC;
    ob(op, i, "depth", n, 0);
    if (n >= 1) {
        _asm {
            fstp tbyte ptr t80
        }
    }
    if (n >= 2) {
        _asm {
            fstp tbyte ptr t80b
        }
    }
    fpm(0, 0, 0);
}

static void setcw(WORD cw)
{
    cwv = cw;
    _asm {
        fldcw cwv
        fclex
    }
    h_calls = 0;
}

/* Results that the 3.1 emulator computes with its own errors (square
   roots one unit in the last place off for some inputs, transcendentals
   up to a few units, FPTAN as an unnormalised pair) are only shown
   without a coprocessor: w16 does not reproduce them bit for bit. */
static int env_vals, env_flags;

static void rep_bytes(const char *op, WORD i, const BYTE *p, WORD nwords)
{
    static const char *const n[] = { "0", "1", "2", "3", "4" };
    WORD j;
    for (j = 0; j < nwords; j++)
        ob(op, i, n[j], *(WORD *)(p + 2 * j), env_vals && !have87);
}

/* flags (and condition codes if cc), handler calls and code */
static void rep_sw(const char *op, WORD i, int cc)
{
    /* an invalid operand leaves the condition codes undefined (Intel):
       under DOSBox-X FPREM of 1.0 by 0 gave C3 now and then where 3.1's
       run had C0, and 3.1's own runs vary the same way (TK6) */
    if (cc && (swv & 0x0001))
        cc = 0;
    ob(op, i, "s", swv & (cc ? 0x473F : 0x003F), env_flags && !have87);
    ob(op, i, "h", h_calls ? (h_calls << 8) | (h_regs[0] & 0xFF) : 0, 0);
}

/* ---- transcendentals ---- */

static const double f2_x[] = { 0.0, 0.25, 0.5, -0.5, 0.1, 1e-10, -0.25, 0.375 };
static const double l2_y[] = { 1.0, 1.0, 1.0, 1.0, 3.5, 1.0, 1.0, -2.0 };
static const double l2_x[] = { 2.0, 10.0, 0.5, 3.0, 7.0, 1e-300, 1.0, 0.75 };
static const double l1_x[] = { 0.0, 0.25, -0.25, 1e-10, 0.29, 0.125 };
static const double l1_y[] = { 1.0, 1.0, 1.0, 1.0, 2.0, -3.0 };
static const double tan_x[] = { 0.0, 0.5, 0.78, 1e-10, 0.125, 0.3 };
static const double at_y[] = { 1.0, 1.0, 1e-10, 0.0, 3.0, 0.5, 1.0, -1.0 };
static const double at_x[] = { 2.0, 1.0, 1.0, 1.0, 4.0, 7.0, -2.0, 2.0 };

#define N(a) (sizeof a / sizeof a[0])

static void trans(int which)
{
    WORD i;
    env_vals = 1;
    if (which && which != 1)
        goto skip1;
    STEP("f2xm1");
    for (i = 0; i < N(f2_x); i++) {
        qa = f2_x[i];
        setcw(0x037F);
        _asm {
            fld qa
            f2xm1
            fstp tbyte ptr t80
            fstsw swv
        }
        rep_bytes("f2xm1", i, t80, 5);
        rep_sw("f2xm1", i, 0);
    }
skip1:
    if (which && which != 2)
        goto skip2;
    STEP("fyl2x");
    for (i = 0; i < N(l2_x); i++) {
        qa = l2_y[i];
        qb = l2_x[i];
        setcw(0x037F);
        _asm {
            fld qa
            fld qb
            fyl2x
            fstp tbyte ptr t80
            fstsw swv
        }
        rep_bytes("fyl2x", i, t80, 5);
        rep_sw("fyl2x", i, 0);
    }
skip2:
    if (which && which != 3)
        goto skip3;
    STEP("fyl2xp1");
    for (i = 0; i < N(l1_x); i++) {
        qa = l1_y[i];
        qb = l1_x[i];
        setcw(0x037F);
        _asm {
            fld qa
            fld qb
            fyl2xp1
            fstp tbyte ptr t80
            fstsw swv
        }
        rep_bytes("fyl2xp1", i, t80, 5);
        rep_sw("fyl2xp1", i, 0);
    }
skip3:
    if (which && which != 4)
        goto skip4;
    STEP("fptan");
    for (i = 0; i < N(tan_x); i++) {
        qa = tan_x[i];
        setcw(0x037F);
        _asm {
            fld qa
            fptan
            fstp tbyte ptr t80
            fstp tbyte ptr t80b
            fstsw swv
        }
        rep_bytes("fptan.st0", i, t80, 5);
        rep_bytes("fptan.st1", i, t80b, 5);
        rep_sw("fptan", i, 0);
    }
skip4:
    if (which && which != 5)
        goto skip5;
    STEP("fpatan");
    for (i = 0; i < N(at_x); i++) {
        qa = at_y[i];
        qb = at_x[i];
        setcw(0x037F);
        _asm {
            fld qa
            fld qb
            fpatan
            fstp tbyte ptr t80
            fstsw swv
        }
        rep_bytes("fpatan", i, t80, 5);
        rep_sw("fpatan", i, 0);
    }
skip5:
    env_vals = 0;
}

/* ---- BCD ---- */

static const BYTE bcd_in[][10] = {
    { 0x78, 0x56, 0x34, 0x12, 0x90, 0x78, 0x56, 0x34, 0x12, 0x00 },
    { 0x21, 0x43, 0x65, 0x87, 0x09, 0x00, 0x00, 0x00, 0x00, 0x80 },
    { 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 },
    { 0, 0, 0, 0, 0, 0, 0, 0, 0, 0x80 },
    { 0x99, 0x99, 0x99, 0x99, 0x99, 0x99, 0x99, 0x99, 0x99, 0x00 },
    { 0xAB, 0x00, 0, 0, 0, 0, 0, 0, 0, 0 },
    { 0x01, 0, 0, 0, 0, 0, 0, 0, 0, 0x7F },
};
static const double bcd_out[] = { 123.5, -42.0, 0.0, 1e18, 124.5, 999999999999999999.0,
                                  -0.4, 2.5 };

static void bcdops(void)
{
    WORD i, j;
    STEP("fbld");
    for (i = 0; i < N(bcd_in); i++) {
        for (j = 0; j < 10; j++)
            bcd[j] = bcd_in[i][j];
        fpm(0, 0, 0);
        setcw(0x037F);
        _asm {
            fbld tbyte ptr bcd
            fstsw swv
        }
        rep_sw("fbld", i, 0);
        take("fbld", i);
        rep_bytes("fbld", i, t80, 5);
    }
    STEP("fbstp");
    for (i = 0; i < N(bcd_out); i++) {
        qa = bcd_out[i];
        for (j = 0; j < 10; j++)
            bcd[j] = 0xCC;
        fpm(0, 0, 0);
        setcw(0x037F);
        _asm {
            fld qa
            fbstp tbyte ptr bcd
            fstsw swv
        }
        rep_bytes("fbstp", i, bcd, 5);
        rep_sw("fbstp", i, 0);
        take("fbstp", i);
    }
}

/* ---- FPREM, FSCALE, FXTRACT ---- */

static const double pr_a[] = { 5.5, -5.5, 1e30, 7.0, 1.0, 0.75, 1e300, 10.0 };
static const double pr_b[] = { 2.0, 2.0, 3.0, 0.1, 0.0, 2.0, 1e-300, -3.0 };
static const double sc_a[] = { 1.5, 1.5, 1.0, 1.0, 3.0, -2.0, 0.0 };
static const double sc_b[] = { 3.0, -3.7, 20000.0, -20000.0, 0.9, 2.5, 5.0 };
static const double xt_a[] = { 6.375, -0.1, 1e-300, 0.0, 4.9e-324, 1.0 };

static void misc(void)
{
    WORD i;
    STEP("fprem");
    for (i = 0; i < N(pr_a); i++) {
        qa = pr_a[i];
        qb = pr_b[i];
        setcw(0x037F);
        _asm {
            fld qb
            fld qa
            fprem
            fstsw swv
            fstp tbyte ptr t80
            fstp st(0)
        }
        rep_bytes("fprem", i, t80, 5);
        rep_sw("fprem", i, 1);
    }
    STEP("fscale");
    for (i = 0; i < N(sc_a); i++) {
        qa = sc_a[i];
        qb = sc_b[i];
        setcw(0x037F);
        _asm {
            fld qb
            fld qa
            fscale
            fstp tbyte ptr t80
            fstp st(0)
            fstsw swv
        }
        rep_bytes("fscale", i, t80, 5);
        rep_sw("fscale", i, 0);
    }
    STEP("fxtract");
    for (i = 0; i < N(xt_a); i++) {
        qa = xt_a[i];
        fpm(0, 0, 0);
        setcw(0x037F);
        _asm {
            fld qa
            fxtract
            fstsw swv
        }
        rep_sw("fxtract", i, 0);
        take("fxtract", i);
        rep_bytes("fxtract.st0", i, t80, 5);
        rep_bytes("fxtract.st1", i, t80b, 5);
    }
}

/* ---- 64-bit integers ---- */

static const long i64_in[][2] = {
    { 0x89ABCDEFL, 0x01234567L }, { -1L, -1L }, { 0L, (long)0x80000000L },
    { 5L, 0L }, { 0L, 0L }, { 0x7FFFFFFFL, 0x7FFFFFFFL }
};
static const double i64_out[] = { 1e18, -3.5, 2.5, 9.3e18, -9.3e18, 0.0, 12345.75 };

static void ints(void)
{
    WORD i;
    STEP("fild64");
    for (i = 0; i < N(i64_in); i++) {
        i64[0] = i64_in[i][0];
        i64[1] = i64_in[i][1];
        setcw(0x037F);
        _asm {
            fild qword ptr i64
            fstp tbyte ptr t80
            fstsw swv
        }
        rep_bytes("fild64", i, t80, 5);
        rep_sw("fild64", i, 0);
    }
    STEP("fistp64");
    for (i = 0; i < N(i64_out); i++) {
        qa = i64_out[i];
        i64[0] = i64[1] = 0x5A5A5A5AL;
        setcw(0x037F);
        _asm {
            fld qa
            fistp qword ptr i64
            fstsw swv
        }
        rep_bytes("fistp64", i, (BYTE *)i64, 4);
        rep_sw("fistp64", i, 0);
    }
}

/* ---- square roots ---- */

static const double sq_in[] = { 2.0, 3.0, 5.0, 10.0, 1e308, 1e300, 1e-300, 7e307,
                                0.5, 1.0000000001, 123456789.0, 3.14159265358979,
                                1e10, 0.1, 6.0, 7.0, 1e100, 2e308 / 3, 1e-10, 99.0,
                                1.5, 1e200, 8e307, 9e307 };

static void roots(void)
{
    WORD i;
    env_vals = env_flags = 1;   /* the 3.1 emulator's PE differs too */
    STEP("sqrt");
    for (i = 0; i < N(sq_in); i++) {
        qa = sq_in[i];
        setcw(0x037F);
        _asm {
            fld qa
            fsqrt
            fstp tbyte ptr t80
            fstsw swv
        }
        rep_bytes("sqrt", i, t80, 5);
        rep_sw("sqrt", i, 0);
    }
    /* the integers 2-61 */
    STEP("sqrtn");
    for (i = 2; i < 62; i++) {
        i64[0] = i;
        setcw(0x037F);
        _asm {
            fild dword ptr i64
            fsqrt
            fstp tbyte ptr t80
            fstsw swv
        }
        rep_bytes("sqrtn", i, t80, 4);
        rep_sw("sqrtn", i, 0);
    }
    /* extended inputs: 1, 2, 4 and 2^64 - 1 times a power of two */
    STEP("sqrt80");
    for (i = 0; i < 4; i++) {
        static const WORD m[4][5] = {
            { 0xFFFF, 0xFFFF, 0xFFFF, 0xFFFF, 0x3FFF },
            { 0x0001, 0x0000, 0x0000, 0x8000, 0x4000 },
            { 0x1234, 0x5678, 0x9ABC, 0xDEF0, 0x3FFF },
            { 0xFFFF, 0xFFFF, 0xFFFF, 0xFFFF, 0x4000 },
        };
        WORD j;
        for (j = 0; j < 5; j++)
            ((WORD *)t80)[j] = m[i][j];
        setcw(0x037F);
        _asm {
            fld tbyte ptr t80
            fsqrt
            fstp tbyte ptr t80
            fstsw swv
        }
        rep_bytes("sqrt80", i, t80, 5);
        rep_sw("sqrt80", i, 0);
    }
    env_vals = env_flags = 0;
}

/* ---- stack faults, from an empty stack ---- */

static void stack_faults(void)
{
    WORD j;
    STEP("stack");
    fpm(0, 0, 0);
    setcw(0x037F);
    _asm {
        fstp st(0)
        fstsw swv
    }
    rep_sw("uf.fstpr", 0, 0);
    ob("uf.fstpr", 0, "depth", depth(), 0);
    fpm(0, 0, 0);
    setcw(0x037F);
    _asm {
        fld1
        fadd st, st(1)
        fstsw swv
    }
    rep_sw("uf.fadd", 0, 0);
    take("uf.fadd", 0);
    rep_bytes("uf.fadd", 0, t80, 5);
    fpm(0, 0, 0);
    setcw(0x037F);
    _asm {
        fld1
        fld1
        fld1
        fld1
        fld1
        fld1
        fld1
        fld1
        fldz
        fstsw swv
    }
    rep_sw("of.fld", 0, 0);
    ob("of.fld", 0, "depth", depth(), 0);
    _asm {
        fstp tbyte ptr t80
    }
    rep_bytes("of.fld", 0, t80, 5);
    fpm(0, 0, 0);
    /* how deep the stack goes: push 1..20, then pop them all */
    {
        WORD k, ok = 1, got = 0;
        static short v;
        fpm(0, 0, 0);
        setcw(0x037F);
        for (k = 1; k <= 20; k++) {
            v = (short)k;
            _asm {
                fild v
            }
            ob("deep.calls", k, NULL, h_calls, 0);
        }
        _asm {
            fstsw swv
        }
        rep_sw("deep.push", 0, 0);
        ob("deep.push", 0, "depth", depth(), 0);
        for (k = 20; k >= 1; k--) {
            _asm {
                fistp v
            }
            if (v == (short)k && ok)
                got++;
            else
                ok = 0;
        }
        _asm {
            fstsw swv
        }
        rep_sw("deep.pop", 0, 0);
        ob("deep.pop", 0, "good", got, 0);
        ob("deep.pop", 0, "depth", depth(), 0);
        fpm(0, 0, 0);
    }
    /* last: without a coprocessor the reference run ended after this */
    fpm(0, 0, 0);
    setcw(0x037F);
    for (j = 0; j < 10; j++)
        t80[j] = 0xCC;
    _asm {
        fstp tbyte ptr t80
        fstsw swv
    }
    rep_bytes("uf.fstpm", 0, t80, 5);
    rep_sw("uf.fstpm", 0, 0);
    ob("uf.fstpm", 0, "depth", depth(), 0);
}

/* ---- the save area ---- */

static BYTE area[0x1D0];

static void save_area(void)
{
    WORD i, v;
    STEP("save");
    setcw(0x037F);
    qa = 6.375;
    qb = -1.5;
    fpm(0, 0, 0);
    fpm(4, 0x1272, 0);
    fpm(12, 0x4321, 0);
    _asm {
        fld qa
        fld qb
    }
    for (i = 0; i < sizeof area; i++)
        area[i] = 0xCC;
    v = FPSAVE(area, sizeof area);
    ob("save", 0, "ret", v, 0);
    /* the FSAVE part (94 bytes) and the rest; words that hold selectors
       or addresses differ between runs and modes */
    for (i = 0; i < 0x1CE; i += 2) {
        char n[8];
        WORD k = 0, d = i;
        n[k++] = (char)('0' + d / 100);
        n[k++] = (char)('0' + d / 10 % 10);
        n[k++] = (char)('0' + d % 10);
        n[k] = 0;
        /* checked: the FSAVE part (untouched without a coprocessor) and
           the words with a known meaning (emu.c, emu_save) */
        ob("area", 0, n, *(WORD *)(area + i),
           !(i < 94 || i == 98 || i == 100 || i == 104 || i == 256));
    }
    v = FPRESTORE(area, sizeof area);
    ob("restore", 0, "ret", v, 0);
    _asm {
        fstp tbyte ptr t80
        fstp tbyte ptr t80b
        fstsw swv
    }
    rep_bytes("restore.st0", 0, t80, 5);
    rep_bytes("restore.st1", 0, t80b, 5);
    fpm(5, 0, 0);
    ob("restore.f5", 0, NULL, rr[0], 0);
    fpm(0, 0, 0);
}

int __cdecl tmain(void)
{
    FARPROC h = (FARPROC)fp_handler;
    out("TK8 start\r\n");
    have87 = (GetWinFlags() & WF_80x87) != 0;
    obs_env("WF_80x87", have87);
    fpm(0, 0, 0);
    fpm(3, (WORD)(DWORD)h, (WORD)((DWORD)h >> 16));
#ifdef TK8_T
    /* TK8T1-5: one transcendental instruction each; without a
       coprocessor the 3.1 emulator's run ended at the first one */
    trans(TK8_T);
    if (!have87 && have87) {            /* referenced, not run */
        stack_faults();
        roots();
        ints();
        misc();
        bcdops();
        save_area();
    }
#else
    roots();
    ints();
    misc();
    bcdops();
    save_area();
    stack_faults();
    if (have87)
        trans(0);
#endif
    out("TK8 done, failures=");
    outhex((WORD)failures);
    out("\r\n");
    return failures;
}
