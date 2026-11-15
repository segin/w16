/*
 * fpu_host.c - shell/calc/fpu.c's helpers for the host (GCC on x86,
 * long double = the x87's extended format), the same operations in the
 * same order, for tools/calchost.
 */
#include <string.h>
#include "../../shell/calc/calc.h"

typedef long double LD;

static unsigned short cw_get(void)
{
    unsigned short cw;
    __asm__ volatile ("fnstcw %0" : "=m"(cw));
    return cw;
}

static void cw_set(unsigned short cw)
{
    __asm__ volatile ("fldcw %0" : : "m"(cw));
}

static LD rnd(LD x)
{
    __asm__ ("frndint" : "+t"(x));
    return x;
}

static LD chop(LD x)
{
    unsigned short cw = cw_get();
    cw_set(cw | 0x0C00);
    x = rnd(x);
    cw_set(cw);
    return x;
}

void fp_init(void)
{
    cw_set(0x133F);
}

void fp_frac_start(EXT *sc)
{
    LD v = (LD)1 / 10;
    memcpy(sc, &v, 10);
}

double fp_frac_digit(double x, int d, EXT *sc)
{
    LD s, r;
    memcpy(&s, sc, 10);
    r = (LD)d * s + x;
    s = s / 10;
    memcpy(sc, &s, 10);
    return (double)r;
}

static LD yl2x(LD y, LD x)
{
    LD r;
    __asm__ ("fyl2x" : "=t"(r) : "0"(x), "u"(y) : "st(1)");
    return r;
}

static LD lg2(void)
{
    LD r;
    __asm__ ("fldlg2" : "=t"(r));
    return r;
}

static LD ln2(void)
{
    LD r;
    __asm__ ("fldln2" : "=t"(r));
    return r;
}

int fp_ilog10(double a)
{
    volatile double l = (double)yl2x(lg2(), (LD)a);
    return (int)chop(l);
}

double fp_trunc(double x)
{
    return (double)chop(x);
}

double fp_floor(double x)
{
    unsigned short cw = cw_get();
    LD r;
    cw_set((cw & ~0x0C00) | 0x0400);
    r = rnd(x);
    cw_set(cw);
    return (double)r;
}

int fp_digits(double a, int n, char *out)
{
    LD p = 1, v;
    int i, k = 0;
    unsigned long long q;
    char t[24];
    for (i = 0; i < (n < 0 ? -n : n); i++)
        p = p * 10;
    v = n < 0 ? a / p : a * p;
    if (n == 0)
        v = a;
    v = rnd(v);
    q = (unsigned long long)v;
    do {
        t[k++] = (char)('0' + q % 10);
        q /= 10;
    } while (q);
    for (i = 0; i < k; i++)
        out[i] = t[k - 1 - i];
    out[k] = 0;
    return k;
}

double fp_sqrt(double x)
{
    LD r = x;
    __asm__ ("fsqrt" : "+t"(r));
    return (double)r;
}

double fp_ln(double x) { return (double)yl2x(ln2(), x); }
double fp_log10(double x) { return (double)yl2x(lg2(), x); }

static LD f2xm1(LD x)
{
    __asm__ ("f2xm1" : "+t"(x));
    return x;
}

static LD scale(LD x, LD n)
{
    LD r;
    __asm__ ("fscale" : "=t"(r) : "0"(x), "u"(n));
    return r;
}

static LD pow2(LD z)
{
    LD i = rnd(z), f = z - i, r;
    if (f < 0)
        r = 1 / (f2xm1(-f) + 1);
    else
        r = f2xm1(f) + 1;
    return scale(r, i);
}

static LD l2e(void) { LD r; __asm__ ("fldl2e" : "=t"(r)); return r; }
static LD l2t(void) { LD r; __asm__ ("fldl2t" : "=t"(r)); return r; }
static LD pi(void) { LD r; __asm__ ("fldpi" : "=t"(r)); return r; }

double fp_exp(double x) { return (double)pow2(l2e() * x); }
double fp_exp10(double x) { return (double)pow2(l2t() * x); }
double fp_pow(double x, double y) { return (double)pow2(yl2x(y, x)); }

double fp_pow10i(double m, int e)
{
    LD p = 1;
    int i;
    for (i = 0; i < (e < 0 ? -e : e); i++)
        p = p * 10;
    return (double)(e < 0 ? (LD)m / p : (LD)m * p);
}

double fp_pi(void) { return (double)pi(); }

static LD prem(LD x, LD y, int *q)
{
    unsigned short sw;
    do {
        __asm__ ("fprem; fnstsw %0" : "=m"(sw), "+t"(x) : "u"(y));
    } while (sw & 0x0400);
    *q = ((sw >> 9) & 1) | ((sw >> 13) & 2) | ((sw >> 6) & 4);
    return x;
}

static double trig(double x, int fn)
{
    int neg = 0, oct;
    LD r, p4 = pi() / 4, y, xx;
    double t, c, s, res;
    if (x < 0) {
        x = -x;
        neg = fn != 1;
    }
    r = prem(x, p4, &oct);
    {
        /* reduce with the 387's 66-bit pi: pi/4 is 2^-66 below p4 */
        LD q = rnd(((LD)x - r) / p4), u = (LD)1 / 18446744073709551616.0L / 4;
        r = r + q * u;
        if (oct & 1)
            r = (p4 - u) - r;
    }
    __asm__ ("fptan" : "=t"(xx), "=u"(y) : "0"(r));
    t = (double)(y / xx);
    c = 1 / fp_sqrt(1 + t * t);
    s = t * c;
    switch (oct & 7) {
    case 0: res = fn == 0 ? s : fn == 1 ? c : s / c; break;
    case 1: res = fn == 0 ? c : fn == 1 ? s : c / s; break;
    case 2: res = fn == 0 ? c : fn == 1 ? -s : -c / s; break;
    case 3: res = fn == 0 ? s : fn == 1 ? -c : -s / c; break;
    case 4: res = fn == 0 ? -s : fn == 1 ? -c : s / c; break;
    case 5: res = fn == 0 ? -c : fn == 1 ? -s : c / s; break;
    case 6: res = fn == 0 ? -c : fn == 1 ? s : -c / s; break;
    default: res = fn == 0 ? -s : fn == 1 ? c : -s / c; break;
    }
    return neg ? -res : res;
}

double fp_sin(double x) { return trig(x, 0); }
double fp_cos(double x) { return trig(x, 1); }
double fp_tan(double x) { return trig(x, 2); }

double fp_atan2(double y, double x)
{
    double r, ay = y < 0 ? -y : y, ax = x < 0 ? -x : x;
    int swap = ay > ax;
    LD a = swap ? ax : ay, b = swap ? ay : ax, q;
    if (b == 0)
        return 0;
    __asm__ ("fpatan" : "=t"(q) : "0"(b), "u"(a) : "st(1)");
    r = (double)q;
    if (swap)
        r = fp_pi() / 2 - r;
    if (x < 0)
        r = fp_pi() - r;
    return y < 0 ? -r : r;
}

int fp_isinf(double x)
{
    unsigned long long b;
    memcpy(&b, &x, 8);
    return ((b >> 52) & 0x7FF) == 0x7FF;
}

double fp_fmod(double x, double y)
{
    int q;
    return (double)prem(x, y, &q);
}

U32 fp_lowbits(double x)
{
    long long v = (long long)chop(x);
    return (unsigned long)(unsigned int)v;
}
