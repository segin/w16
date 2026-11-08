/*
 * tk7.c - floating-point arithmetic probe: results (all 80 bits) and
 * status flags of the basic x87 operations on a table of operands, run
 * with a coprocessor and with WIN87EM's emulator (tests/tk6ref.sh). The
 * floating-point instructions are inline assembly in a file built with
 * -fpi, so they carry emulator fixups like compiled code.
 *
 * Observations are named "87.*" with a coprocessor and "em.*" with the
 * emulator, so each configuration is checked against its own reference
 * (ARCHITECTURE.md 9.2 lists where the two differ).
 * All exceptions are masked (control word 037Fh, or with another
 * rounding/precision control where noted).
 */
#include <windows.h>

#define TK_REF "tk7ref.h"
#include "tklog.h"

#define STEP(s) out("-- " s "\r\n")

extern void FAR PASCAL FPMATH(void);
extern void __cdecl regcall(FARPROC fn, WORD FAR *r);

static double a_tab[] = {
    1.5, 1.0, 1e300, 1e-300, -0.0, 3.0, 123456789.123, 4.9e-324,
    -7.25, 2.0, 0.1, 1e308
};
static double b_tab[] = {
    2.25, 3.0, 1e300, 1e-300, 0.0, -0.0, -0.001, 3.0,
    0.5, 0.0, 3.0, 10.0
};
#define NV (sizeof a_tab / sizeof a_tab[0])

static int have87;
static double qa, qb;
static WORD cwv, swv;
static BYTE t80[10];
static double q64;
static float f32;
static long i32;
static short i16;

static void init_fpu(void)
{
    WORD r[10];
    WORD i;
    for (i = 0; i < 10; i++)
        r[i] = 0;
    regcall((FARPROC)FPMATH, r);        /* function 0 */
}

static void setcw(WORD cw)
{
    cwv = cw;
    _asm {
        fldcw cwv
        fclex
    }
}

/* obs with the configuration prefix */
static void obsp(const char *name, WORD v)
{
    char n[40];
    WORD k = 0;
    const char *p = have87 ? "87." : "em.";
    while (*p)
        n[k++] = *p++;
    while (*name && k < sizeof n - 1)
        n[k++] = *name++;
    n[k] = 0;
    /* the 3.1 emulator's square root of 1e308 is 1 unit in the last place
       above the correctly rounded result (which a coprocessor and w16
       give): not checked */
    if (!have87 && same(n, "em.sqrt.11.0"))
        obs_env(n, v);
    else
        obs(n, v);
}

static void report80(const char *op, WORD i)
{
    char n[32];
    WORD k = 0, j;
    const char *p = op;
    while (*p)
        n[k++] = *p++;
    n[k++] = '.';
    if (i >= 10)
        n[k++] = (char)('0' + i / 10);
    n[k++] = (char)('0' + i % 10);
    n[k++] = '.';
    n[k + 1] = 0;
    for (j = 0; j < 5; j++) {
        n[k] = (char)('0' + j);
        obsp(n, *(WORD *)(t80 + 2 * j));
    }
    n[k] = 's';
    /* C3 C2 C0 and the flags, no TOP; with a coprocessor the condition
       codes are undefined after these operations and differed between
       runs: flags only */
    obsp(n, swv & (have87 ? 0x003F : 0x473F));
}

/* ST(0) op ST(1) for each pair; result (80 bits) and flags */
static void binops(void)
{
    WORD i;
    STEP("binops");
    for (i = 0; i < NV; i++) {
        qa = a_tab[i];
        qb = b_tab[i];
        setcw(0x037F);
        _asm {
            fld qa
            fld qb
            faddp st(1), st
            fstp tbyte ptr t80
            fstsw swv
        }
        report80("add", i);
        setcw(0x037F);
        _asm {
            fld qa
            fsub qb
            fstp tbyte ptr t80
            fstsw swv
        }
        report80("sub", i);
        setcw(0x037F);
        _asm {
            fld qa
            fmul qb
            fstp tbyte ptr t80
            fstsw swv
        }
        report80("mul", i);
        setcw(0x037F);
        _asm {
            fld qa
            fdiv qb
            fstp tbyte ptr t80
            fstsw swv
        }
        report80("div", i);
        setcw(0x037F);
        _asm {
            fld qb
            fdivr qa
            fstp tbyte ptr t80
            fstsw swv
        }
        report80("divr", i);
        setcw(0x037F);
        _asm {
            fld qb
            fld qa
            fcompp
            fstsw swv
        }
        t80[0] = t80[1] = 0;
        {
            char n[16] = "cmp.";
            WORD k = 4;
            if (i >= 10)
                n[k++] = (char)('0' + i / 10);
            n[k++] = (char)('0' + i % 10);
            n[k] = 0;
            obsp(n, swv & 0x473F);
        }
    }
}

static void unops(void)
{
    WORD i;
    STEP("unops");
    for (i = 0; i < NV; i++) {
        qa = a_tab[i];
        setcw(0x037F);
        _asm {
            fld qa
            fabs
            fsqrt
            fstp tbyte ptr t80
            fstsw swv
        }
        report80("sqrt", i);
        setcw(0x037F);
        _asm {
            fld qa
            fld qa
            fmulp st(1), st
            fstp q64
            fstsw swv
        }
        *(double *)t80 = q64;
        t80[8] = t80[9] = 0;
        report80("sq64", i);
        setcw(0x037F);
        _asm {
            fld qa
            fstp f32
            fstsw swv
        }
        *(float *)t80 = f32;
        t80[4] = t80[5] = t80[6] = t80[7] = t80[8] = t80[9] = 0;
        report80("f32", i);
        setcw(0x037F);
        _asm {
            fld qa
            fistp i32
            fstsw swv
        }
        *(long *)t80 = i32;
        t80[4] = t80[5] = t80[6] = t80[7] = t80[8] = t80[9] = 0;
        report80("i32", i);
        setcw(0x037F);
        _asm {
            fld qa
            fistp i16
            fstsw swv
        }
        *(short *)t80 = i16;
        t80[2] = t80[3] = t80[4] = t80[5] = t80[6] = t80[7] = t80[8] = t80[9] = 0;
        report80("i16", i);
        setcw(0x037F);
        _asm {
            fld qa
            frndint
            fstp tbyte ptr t80
            fstsw swv
        }
        report80("rnd", i);
        setcw(0x037F);
        _asm {
            fld qa
            ftst
            fstsw swv
            fstp st(0)
        }
        t80[0] = 0;
        {
            char n[16] = "tst.";
            WORD k = 4;
            if (i >= 10)
                n[k++] = (char)('0' + i / 10);
            n[k++] = (char)('0' + i % 10);
            n[k] = 0;
            obsp(n, swv & 0x473F);
        }
    }
}

/* rounding and precision control */
static void modes(void)
{
    static const WORD cws[] = { 0x037F, 0x077F, 0x0B7F, 0x0F7F, 0x007F, 0x027F };
    WORD i;
    STEP("modes");
    for (i = 0; i < 6; i++) {
        qa = 1.0;
        qb = 3.0;
        setcw(cws[i]);
        _asm {
            fld qa
            fdiv qb
            fstp tbyte ptr t80
            fstsw swv
        }
        report80("third", i);
        qa = -2.5;
        setcw(cws[i]);
        _asm {
            fld qa
            fistp i32
            fstsw swv
        }
        *(long *)t80 = i32;
        t80[4] = t80[5] = t80[6] = t80[7] = t80[8] = t80[9] = 0;
        report80("m2.5", i);
        qa = 0.1;
        setcw(cws[i]);
        _asm {
            fld qa
            fstp f32
            fstsw swv
        }
        *(float *)t80 = f32;
        t80[4] = t80[5] = t80[6] = t80[7] = t80[8] = t80[9] = 0;
        report80("f0.1", i);
    }
    setcw(0x037F);
}

/* constants and integer loads */
static void consts(void)
{
    STEP("consts");
    setcw(0x037F);
    _asm {
        fldpi
        fstp tbyte ptr t80
        fstsw swv
    }
    report80("pi", 0);
    _asm {
        fldl2e
        fstp tbyte ptr t80
        fstsw swv
    }
    report80("l2e", 0);
    i32 = -123456789L;
    _asm {
        fild i32
        fstp tbyte ptr t80
        fstsw swv
    }
    report80("fild32", 0);
    i16 = -32768;
    _asm {
        fild i16
        fstp tbyte ptr t80
        fstsw swv
    }
    report80("fild16", 0);
    _asm {
        fld1
        fldz
        fxch st(1)
        fsubp st(1), st
        fstp tbyte ptr t80
        fstsw swv
    }
    report80("xchsub", 0);
}

int __cdecl tmain(void)
{
    out("TK7 start\r\n");
    have87 = (GetWinFlags() & WF_80x87) != 0;
    obs_env("WF_80x87", have87);
    _asm {
        fstsw swv
    }
    obsp("task.sw", swv);
    _asm {
        fstcw cwv
    }
    obsp("task.cw", cwv & ~0x40);
    init_fpu();
    _asm {
        fstsw swv
    }
    obsp("f0.sw", swv);
    binops();
    unops();
    modes();
    consts();
    out("TK7 done, failures=");
    outhex((WORD)failures);
    out("\r\n");
    return failures;
}
