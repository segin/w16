/*
 * fpu.c - w16 Calculator: floating-point helpers (x87).
 *
 * The arithmetic itself is C (double, which the compiler evaluates on the
 * x87 in extended precision and stores as double). The few steps whose
 * last bits show on the display are done here in extended precision with
 * the x87's own operations (Intel 287/387 programmer's reference): FYL2X
 * for logarithms, F2XM1 and FSCALE for powers, FPTAN after an FPREM
 * reduction for the trigonometric functions, FPATAN, FSQRT, FRNDINT.
 * Built with emulator fixups (-fpi), so the code runs on a coprocessor or
 * WIN87EM's emulator; fp_init initialises WIN87EM (__fpMath function 0,
 * ARCHITECTURE.md 9.2 floating-point findings) and masks every exception:
 * the calculator checks results itself.
 *
 * tools/calchost/fpu_host.c is the same set of helpers for the host (GCC,
 * x87 long double), used to test the engine against the original's logs.
 */
#include "calc.h"

/* the inline assembly assigns the results */
#pragma disable_message(200)

extern void far pascal FPMATH(void);    /* WIN87EM.1, register interface */

static const short ten = 10;
static const double quarter_ulp = 1.3552527156068805425e-20;     /* 2^-66 */
static short cw_save, cw_tmp;

void fp_init(void)
{
    _asm {
        push si
        push di
        xor bx, bx
        call FPMATH                     /* function 0: initialise */
        pop di
        pop si
    }
    cw_tmp = 0x133F;                    /* extended precision, nearest, all masked */
    _asm {
        fldcw cw_tmp
    }
}

/* s = 0.1 in extended precision */
void fp_frac_start(EXT *sc)
{
    EXT far *p = sc;
    _asm {
        les bx, p
        fld1
        fidiv ten
        fstp tbyte ptr es:[bx]
        fwait
    }
}

/* x + d*s rounded to double; s /= 10 */
double fp_frac_digit(double x, int d, EXT *sc)
{
    EXT far *p = sc;
    double r;
    short dd = (short)d;
    _asm {
        les bx, p
        fld tbyte ptr es:[bx]
        fild dd
        fmul st, st(1)
        fadd x
        fstp r
        fidiv ten
        fstp tbyte ptr es:[bx]
        fwait
    }
    return r;
}

/* chop toward zero (rounding control 11) */
#define CHOP_ON  _asm { fstcw cw_save } cw_tmp = cw_save | 0x0C00; _asm { fldcw cw_tmp }
#define CHOP_OFF _asm { fldcw cw_save }

/* (int)log10(a), a > 0: the logarithm rounded to double, then chopped
   (observed: 1e-13 is shown in scientific notation) */
int fp_ilog10(double a)
{
    short r;
    double l;
    _asm {
        fldlg2
        fld a
        fyl2x
        fstp l
        fwait
    }
    CHOP_ON
    _asm {
        fld l
        fistp r
        fwait
    }
    CHOP_OFF
    return r;
}

double fp_trunc(double x)
{
    double r;
    CHOP_ON
    _asm {
        fld x
        frndint
        fstp r
        fwait
    }
    CHOP_OFF
    return r;
}

double fp_floor(double x)
{
    double r;
    _asm { fstcw cw_save }
    cw_tmp = (cw_save & ~0x0C00) | 0x0400;
    _asm {
        fldcw cw_tmp
        fld x
        frndint
        fstp r
        fwait
        fldcw cw_save
    }
    return r;
}

/* the decimal digits of round(a * 10^n) (a >= 0, nearest even, extended
   precision); returns the number of digits (at least 1) */
int fp_digits(double a, int n, char *out)
{
    unsigned short q[4];
    unsigned short far *pq = q;
    short nn = (short)(n < 0 ? -n : n);
    char t[24];
    int k = 0, i;
    _asm {
        fld1
        mov cx, nn
        jcxz done10
    loop10:
        fimul ten
        loop loop10
    done10:
        fld a
        cmp nn, 0
        je nomul
        mov ax, n
        or ax, ax
        js divide
        fmul st, st(1)
        jmp nomul
    divide:
        fdiv st, st(1)
    nomul:
        frndint
        les bx, pq
        fistp qword ptr es:[bx]
        fstp st(0)
        fwait
    }
    /* 64-bit integer to decimal: repeated division by 10 on words */
    for (;;) {
        unsigned long rem = 0;
        int z = 1;
        for (i = 3; i >= 0; i--) {
            unsigned long cur = (rem << 16) | q[i];
            q[i] = (unsigned short)(cur / 10);
            rem = cur % 10;
            if (q[i])
                z = 0;
        }
        t[k++] = (char)('0' + (int)rem);
        if (z)
            break;
    }
    for (i = 0; i < k; i++)
        out[i] = t[k - 1 - i];
    out[k] = 0;
    return k;
}

double fp_sqrt(double x)
{
    double r;
    _asm {
        fld x
        fsqrt
        fstp r
        fwait
    }
    return r;
}

double fp_ln(double x)
{
    double r;
    _asm {
        fldln2
        fld x
        fyl2x
        fstp r
        fwait
    }
    return r;
}

double fp_log10(double x)
{
    double r;
    _asm {
        fldlg2
        fld x
        fyl2x
        fstp r
        fwait
    }
    return r;
}

/* 2^ST(0) in place (extended): split into integer and fraction (nearest),
   F2XM1 on |f| <= 0.5 (a negative fraction through 1/2^-f), FSCALE */
#define POW2_ST                                                          \
    _asm { fld st(0) }                                                   \
    _asm { frndint }                    /* i z */                        \
    _asm { fsub st(1), st }             /* i f */                        \
    _asm { fxch st(1) }                 /* f i */                        \
    _asm { ftst }                                                        \
    _asm { fstsw sw }                                                    \
    _asm { fwait }                                                       \
    if (sw & 0x0100) {                  /* f < 0 */                      \
        _asm { fchs }                                                    \
        _asm { f2xm1 }                                                   \
        _asm { fld1 }                                                    \
        _asm { fadd st(1), st }                                          \
        _asm { fdivrp st(1), st }                                        \
    } else {                                                             \
        _asm { f2xm1 }                                                   \
        _asm { fld1 }                                                    \
        _asm { faddp st(1), st }                                         \
    }                                                                    \
    _asm { fscale }                                                      \
    _asm { fstp st(1) }

double fp_exp(double x)
{
    double r;
    unsigned short sw;
    _asm {
        fldl2e
        fmul x
    }
    POW2_ST
    _asm {
        fstp r
        fwait
    }
    return r;
}

double fp_exp10(double x)
{
    double r;
    unsigned short sw;
    _asm {
        fldl2t
        fmul x
    }
    POW2_ST
    _asm {
        fstp r
        fwait
    }
    return r;
}

/* x^y for x > 0 */
double fp_pow(double x, double y)
{
    double r;
    unsigned short sw;
    _asm {
        fld y
        fld x
        fyl2x
    }
    POW2_ST
    _asm {
        fstp r
        fwait
    }
    return r;
}

/* m * 10^e for the exponent of a typed number */
double fp_pow10i(double m, int e)
{
    double r;
    short ae = (short)(e < 0 ? -e : e);
    _asm {
        fld1
        mov cx, ae
        jcxz pdone
    ploop:
        fimul ten
        loop ploop
    pdone:
        fld m
        mov ax, e
        or ax, ax
        js pdiv
        fmulp st(1), st
        jmp pend
    pdiv:
        fdivrp st(1), st
    pend:
        fstp r
        fwait
    }
    return r;
}

double fp_pi(void)
{
    double r;
    _asm {
        fldpi
        fstp r
        fwait
    }
    return r;
}

/* sin (0), cos (1), tan (2) of x in radians: FPREM by pi/4 in extended
   precision, FPTAN on the reduced argument (0 <= r <= pi/4, the 287's
   domain), then the octant's identities */
static double trig(double x, int fn)
{
    double r;
    unsigned short sw;
    short oct;
    int neg = 0;
    double t, c, s;
    if (x < 0) {
        x = -x;
        neg = fn != 1;
    }
    _asm {
        fldpi
        fld1
        fadd st, st
        fadd st, st                     /* 4 pi */
        fdivp st(1), st                 /* pi/4 */
        fld x
    rem:
        fprem
        fstsw sw
        fwait
        mov ax, sw
        test ax, 0400h                  /* C2: incomplete */
        jnz rem
        /* quotient bits: C0 = q2, C3 = q1, C1 = q0 */
        xor dx, dx
        test ax, 0200h
        jz noq0
        or dx, 1
    noq0:
        test ax, 4000h
        jz noq1
        or dx, 2
    noq1:
        test ax, 0100h
        jz noq2
        or dx, 4
    noq2:
        mov oct, dx
        /* the 387 reduces with a 66-bit pi, 2^-64 below FLDPI's: add
           q * 2^-66 where q is the whole quotient */
        fld x
        fsub st, st(1)                  /* x - r */
        fdiv st, st(2)
        frndint                         /* q */
        fmul quarter_ulp
        faddp st(1), st                 /* r corrected */
        test dx, 1                      /* odd octant: pi/4 - r */
        jz even
        fld quarter_ulp
        fsubp st(2), st                 /* pi/4 - 2^-66 */
        fsubp st(1), st
        jmp red
    even:
        fstp st(1)
    red:
        fptan                           /* x y: tan = y/x */
        fdivp st(1), st
        fstp r
        fwait
    }
    t = r;                              /* tan of the reduced angle */
    /* sin and cos of the reduced angle */
    c = 1 / fp_sqrt(1 + t * t);
    s = t * c;
    switch (oct & 7) {
    case 0: r = fn == 0 ? s : fn == 1 ? c : s / c; break;
    case 1: r = fn == 0 ? c : fn == 1 ? s : c / s; break;
    case 2: r = fn == 0 ? c : fn == 1 ? -s : -c / s; break;
    case 3: r = fn == 0 ? s : fn == 1 ? -c : -s / c; break;
    case 4: r = fn == 0 ? -s : fn == 1 ? -c : s / c; break;
    case 5: r = fn == 0 ? -c : fn == 1 ? -s : c / s; break;
    case 6: r = fn == 0 ? -c : fn == 1 ? s : -c / s; break;
    default: r = fn == 0 ? -s : fn == 1 ? c : -s / c; break;
    }
    return neg ? -r : r;
}

double fp_sin(double x) { return trig(x, 0); }
double fp_cos(double x) { return trig(x, 1); }
double fp_tan(double x) { return trig(x, 2); }

/* atan2 through FPATAN with 0 <= y <= x (the 287's domain) */
double fp_atan2(double y, double x)
{
    double r, ay = y < 0 ? -y : y, ax = x < 0 ? -x : x;
    int swap = ay > ax;
    double a = swap ? ax : ay, b = swap ? ay : ax;
    if (b == 0)
        return 0;
    _asm {
        fld a
        fld b
        fpatan
        fstp r
        fwait
    }
    if (swap)
        r = fp_pi() / 2 - r;
    if (x < 0)
        r = fp_pi() - r;
    return y < 0 ? -r : r;
}

int fp_isinf(double x)
{
    unsigned short far *w = (unsigned short far *)&x;
    return (w[3] & 0x7FF0) == 0x7FF0;
}

/* x - y * trunc(x / y), exact (FPREM) */
double fp_fmod(double x, double y)
{
    double r;
    unsigned short sw;
    _asm {
        fld y
        fld x
    mrem:
        fprem
        fstsw sw
        fwait
        test sw, 0400h
        jnz mrem
        fstp r
        fstp st(0)
        fwait
    }
    return r;
}

/* the low 32 bits of trunc(x) as two's complement (x within 2^63) */
U32 fp_lowbits(double x)
{
    unsigned short q[4];
    unsigned short far *pq = q;
    CHOP_ON
    _asm {
        fld x
        les bx, pq
        fistp qword ptr es:[bx]
        fwait
    }
    CHOP_OFF
    return ((unsigned long)q[1] << 16) | q[0];
}
