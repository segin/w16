/*
 * engine.c - w16 Calculator: what the keys do.
 *
 * Observed on Windows 3.1 (CA1 runs, ARCHITECTURE.md 15.3):
 *  - The current number x is typed digit by digit or is the last result.
 *    A binary operator shows its left operand (or the result of what it
 *    completed), and x becomes 0 until a number is entered; Edit > Copy
 *    shows x again afterwards (so "2+" then Copy shows 0).
 *  - Standard view: operators are applied left to right. Scientific view:
 *    precedence x^y > * / Mod Lsh > + - > And > Or Xor, left to right
 *    within a level. A new operator completes the pending ones only when
 *    its precedence is not above any of them; then all of them are
 *    evaluated. Otherwise it waits (the display keeps the typed number).
 *  - An operator right after another replaces it.
 *  - =: evaluates the innermost parenthesis level; with nothing entered
 *    after the operator, the operand is that level's first value
 *    ("2+=" is 4). Repeated = repeats the last operation performed, until
 *    a new number is typed (CE does not count).
 *  - A function with nothing entered after an operator works on the
 *    displayed operand ("2+@" in the standard view is sqrt 2).
 *  - Back removes the last fraction digit while one is typed; otherwise
 *    it divides x by the radix and chops (1x34 Back is 1.e+033), and
 *    digits typed next append to that.
 *  - +/- on a result makes it a number being typed again ("5=N3" is -53).
 *  - In an error state only C and CE do anything.
 */
#include "calc.h"

CALC g;

static double lastres;

/* ---- precedence ---- */
static int prec(int op)
{
    if (!g.sci)
        return 1;
    switch (op) {
    case IDC_PWR: case IDC_PWR + 1000: return 6;
    case IDC_MUL: case IDC_DIV: case IDC_MOD: case IDC_LSH: case IDC_LSH + 1000: return 5;
    case IDC_ADD: case IDC_SUB: return 4;
    case IDC_AND: return 3;
    default: return 2;                  /* Or, Xor */
    }
}

static void set_error(int e)
{
    g.err = e;
}

/* integer operand for the bitwise operators: the low 32 bits */
static double from_bits(U32 v)
{
    return (v & (U32)0x80000000UL) ? -(double)((U32)0 - v) : (double)v;
}

/* x within the 32-bit range the bitwise operators take */
static int bits_ok(double x)
{
    return x > -2147483649.0 && x < 4294967296.0;
}

static double apply(double a, int op, double b)
{
    switch (op) {
    case IDC_ADD: return a + b;
    case IDC_SUB: return a - b;
    case IDC_MUL: return a * b;
    case IDC_DIV:
        if (b == 0) {
            set_error(ERR_DIV0);
            return 0;
        }
        return a / b;
    case IDC_MOD:
        if (b == 0) {
            set_error(ERR_DIV0);
            return 0;
        }
        return fp_fmod(a, b);
    case IDC_PWR:
        /* Inv counts when the operation is done (observed: Inv used by
           a function in between gives the plain power) */
        if (g.inv) {
            g.inv = 0;
            return apply(a, IDC_PWR + 1000, b);
        }
        return calc_pow(a, b);
    case IDC_PWR + 1000:                /* Inv x^y: the b-th root */
        if (b == 0 || a < 0) {
            set_error(ERR_INVALID);
            return 0;
        }
        return calc_pow(a, 1 / b);
    case IDC_LSH:
        if (g.inv) {
            g.inv = 0;
            return apply(a, IDC_LSH + 1000, b);
        }
        /* fall through */
    case IDC_AND: case IDC_OR: case IDC_XOR: case IDC_LSH + 1000: {
        U32 u, v, r;
        int n;
        if (!bits_ok(a) || !bits_ok(b)) {
            set_error(ERR_BIG);
            return 0;
        }
        u = fp_lowbits(a);
        v = fp_lowbits(b);
        if (op == IDC_AND)
            r = u & v;
        else if (op == IDC_OR)
            r = u | v;
        else if (op == IDC_XOR)
            r = u ^ v;
        else {
            /* the count's low byte, one bit at a time (observed: 7 Lsh
               15625 is 7 Lsh 9; 1 Lsh 32 is 0); Inv Lsh keeps the sign */
            n = (int)(v & 0xFF);
            r = u;
            while (n--)
                r = op == IDC_LSH ? r << 1 : (r >> 1) | (r & (U32)0x80000000UL);
        }
        return from_bits(r);            /* signed in every radix (observed) */
    }
    }
    return b;
}

/* evaluate the pending operators of a level with x as the last operand:
   leftmost highest precedence first */
static double eval_frame(FRAME *f, double x)
{
    double v[MAXOPS + 1];
    int op[MAXOPS];
    int n = f->n, i, j, best;
    for (i = 0; i < n; i++) {
        v[i] = f->v[i];
        op[i] = f->op[i];
    }
    v[n] = x;
    while (n > 0) {
        best = 0;
        for (i = 1; i < n; i++)
            if (prec(op[i]) > prec(op[best]))
                best = i;
        g.lastop = op[best];
        g.lastopnd = v[best + 1];
        v[best] = apply(v[best], op[best], v[best + 1]);
        if (g.err)
            return 0;
        for (j = best + 1; j < n; j++) {
            v[j] = v[j + 1];
            op[j - 1] = op[j];
        }
        n--;
    }
    g.havelast = 1;
    return v[0];
}

/* ---- display ---- */

/* the text of x as the display shows it; the typed-zero rule: when the
   decimal point was typed and the last digit typed was 0, one 0 more
   ("1.50", "1.50e+030") */
static void text_of(double x, char *out)
{
    fmt_number(x, out, g.fe, g.radix, g.wordsize);
    if (g.radix == 10 && g.point && g.lastzero) {
        char *e = out;
        while (*e && *e != 'e')
            e++;
        if (*e) {
            char *q = e;
            while (*q)
                q++;
            while (q >= e) {
                q[1] = q[0];
                q--;
            }
            *e = '0';
        } else {
            e[0] = '0';
            e[1] = 0;
        }
    }
}

static void copy(char *d, const char *s)
{
    while ((*d++ = *s++) != 0)
        ;
}

/* show a text; in the standard view a text wider than the display is not
   shown and is an error (observed: the display keeps its old text) */
static int show_text(const char *t)
{
    g.showparen = 0;
    if (!g.sci && ui_toowide(t)) {
        set_error(ERR_SILENT);
        return 0;
    }
    copy(g.disp, t);
    return 1;
}

static void show_x(void)
{
    char t[64];
    if (g.err)
        return;
    if (fp_isinf(g.x) || g.x != g.x) {
        set_error(ERR_BIG);
        return;
    }
    text_of(g.x, t);
    show_text(t);
}

/* the entry while an exponent is typed: "1.50e+002", "1.e+0" */
static void show_exp_entry(void)
{
    char t[64];
    char *p;
    int lz = g.lastzero, fe = g.fe;
    g.lastzero = g.mantzero;
    g.fe = 0;
    text_of(g.mant, t);
    g.fe = fe;
    g.lastzero = lz;
    p = t;
    while (*p)
        p++;
    *p++ = 'e';
    *p++ = g.expneg ? '-' : '+';
    if (g.expval == 0) {
        *p++ = '0';
    } else {
        int v = g.expval;
        if (v >= 1000)
            *p++ = (char)('0' + v / 1000 % 10);
        *p++ = (char)('0' + v / 100 % 10);
        *p++ = (char)('0' + v / 10 % 10);
        *p++ = (char)('0' + v % 10);
    }
    *p = 0;
    show_text(t);
}

void calc_display(void)
{
    show_x();
}

/* the text Copy puts on the clipboard: the display without the point the
   display adds (kept when the point was typed) */
int calc_copytext(char *buf, int n)
{
    int len = 0;
    if (g.err)
        return 0;
    while (g.disp[len] && len < n - 1) {
        buf[len] = g.disp[len];
        len++;
    }
    if (len > 1 && buf[len - 1] == '.' && !g.point)
        len--;
    buf[len] = 0;
    return len;
}

/* ---- entry ---- */

static void start_entry(void)
{
    g.entering = 1;
    g.newnum = 1;
    g.havelast = 0;
    g.neg = 0;
    g.nidig = 0;
    g.nfdig = 0;
    g.point = 0;
    g.lastzero = 0;
    g.expmode = 0;
    g.x = 0;
}

/* make x a number being typed (after +/-, Back, or before a parenthesis):
   its digits are the display's */
static void entry_from_x(void)
{
    char t[64];
    const char *p;
    double x = g.x;
    start_entry();
    if (g.radix != 10) {
        U32 v = fp_lowbits(x);
        int shift = g.radix == 16 ? 4 : g.radix == 8 ? 3 : 1, n = 0, i;
        char d[40];
        if (x < 0) {
            g.neg = 1;
            v = (U32)0 - v;
        }
        do {
            d[n++] = "0123456789ABCDEF"[(int)(v & (U32)(g.radix - 1))];
            v >>= shift;
        } while (v);
        for (i = 0; i < n; i++)
            g.idig[i] = d[n - 1 - i];
        g.nidig = n;
        g.x = x;
        return;
    }
    fmt_number(x, t, 0, 10, 0);
    p = t;
    if (*p == '-')
        g.neg = 1;
    p++;
    for (; *p && *p != '.' && *p != 'e'; p++)
        g.idig[g.nidig++] = *p;
    if (*p == '.') {
        for (p++; *p && *p != 'e'; p++)
            g.fdig[g.nfdig++] = *p;
    }
    if (*p == 'e' || g.nidig == 0) {    /* scientific: keep the value */
        g.x = x;
        g.nidig = 0;
        g.nfdig = 0;
        g.idig[g.nidig++] = '0';
        g.xfixed = 1;
        return;
    }
    if (g.nidig == 1 && g.idig[0] == '0' && g.nfdig == 0)
        g.nidig = 1;
    g.x = x;
}

static void entry_value(void)
{
    if (g.xfixed)
        return;
    if (g.expmode) {
        int e = g.expneg ? -g.expval : g.expval;
        g.x = fp_pow10i(g.mant, e);
    } else {
        g.x = parse_entry();
    }
}

static void digit(int d)
{
    if (d >= g.radix)
        return;
    if (!g.entering)
        start_entry();
    g.newnum = 1;
    if (g.xfixed) {
        g.xfixed = 0;
        g.nidig = 0;
    }
    if (g.expmode) {
        if (g.expdigits >= 4)
            return;
        g.expval = g.expval * 10 + d;
        g.expdigits++;
        g.lastzero = d == 0;
        if (g.expval > 307) {
            set_error(g.expneg ? ERR_SMALL : ERR_BIG);
            return;
        }
        entry_value();
        show_exp_entry();
        return;
    }
    if (g.radix != 10) {
        /* the number is a Dword whatever the word size (which only
           masks the display): a digit that takes it past 32 bits is an
           error (observed: FFFFFFFF1 in Byte mode) */
        char c = (char)(d < 10 ? '0' + d : 'A' + d - 10);
        double v = (g.neg ? -g.x : g.x) * g.radix + d;
        if (g.nidig == 1 && g.idig[0] == '0')
            g.nidig = 0;
        if (v > 4294967295.0 || g.nidig >= 40) {
            set_error(ERR_BIG);
            return;
        }
        g.idig[g.nidig++] = c;
        g.x = parse_entry();
        g.lastzero = d == 0;
        show_x();
        return;
    }
    {
        int ni = g.nidig, nf = g.nfdig, lz = g.lastzero;
        double old = g.x;
        if (g.point) {
            if (g.nfdig >= (int)sizeof g.fdig)
                return;
            g.fdig[g.nfdig++] = (char)('0' + d);
        } else {
            if (g.nidig >= (int)sizeof g.idig)
                return;
            if (g.nidig == 1 && g.idig[0] == '0')
                g.nidig = 0;
            g.idig[g.nidig++] = (char)('0' + d);
        }
        g.x = parse_entry();
        g.lastzero = d == 0;
        if (!g.sci) {
            char t[64];
            text_of(g.x, t);
            if (ui_toowide(t)) {        /* the digit does not fit: ignored */
                g.nidig = ni;
                g.nfdig = nf;
                g.lastzero = lz;
                g.x = old;
                return;
            }
        }
        show_x();
    }
}

static void point(void)
{
    if (g.radix != 10)
        return;
    if (!g.entering) {
        start_entry();
        g.idig[0] = '0';
        g.nidig = 1;
    }
    if (g.expmode || g.point)
        return;
    if (g.xfixed)
        return;
    g.point = 1;
    g.lastzero = 0;
    show_x();
}

static void exponent(void)
{
    if (g.radix != 10 || g.fe) {
        /* observed: in hex Exp ends the number and drops the pending
           operators ("1x308*1x308=" shows 308) */
        int i;
        if (g.radix != 10) {
            for (i = 0; i <= MAXPAREN; i++)
                g.fr[i].n = 0;
            g.depth = 0;
        }
        g.entering = 0;
        return;
    }
    if (!g.entering)
        start_entry();
    if (g.expmode)
        return;
    g.mant = g.x;
    g.mantzero = g.prevzero;
    if (g.mant == 0) {
        g.mant = 1;
        g.mantzero = 0;
    }
    g.x = g.mant;
    g.expmode = 1;
    g.expval = 0;
    g.expdigits = 0;
    g.expneg = 0;
    g.lastzero = 0;
    show_exp_entry();
}

static void sign(void)
{
    if (g.entering && g.expmode) {
        g.expneg ^= 1;
        entry_value();
        show_exp_entry();
        return;
    }
    if (!g.entering) {
        int pt = g.point;
        entry_from_x();
        g.point = pt && g.nfdig;
    }
    g.neg ^= 1;
    if (g.xfixed)
        g.x = -g.x;
    else
        entry_value();
    show_x();
}

static void back(void)
{
    if (g.entering && !g.expmode && !g.xfixed && g.radix == 10 && g.nfdig > 0) {
        g.nfdig--;
        g.lastzero = g.nfdig > 0 && g.fdig[g.nfdig - 1] == '0';
        g.x = parse_entry();
        show_x();
        return;
    }
    if (g.entering && !g.expmode && !g.xfixed && g.radix != 10) {
        if (g.nidig > 0)
            g.nidig--;
        g.x = parse_entry();
        show_x();
        return;
    }
    g.x = fp_trunc(g.x / g.radix);
    g.point = 0;
    entry_from_x();
    show_x();
}

/* the operand a function works on */
static double operand(void)
{
    FRAME *f = &g.fr[g.depth];
    if (g.newnum || f->n == 0)
        return g.x;
    return f->v[f->n - 1];
}

/* a function's result becomes x */
static void result(double r)
{
    if (g.err)
        return;
    g.x = r;
    g.entering = 0;
    g.expmode = 0;
    g.xfixed = 0;
    g.newnum = 1;
    g.opjust = 0;
    g.lastzero = 0;
    if (g.radix != 10)
        g.x = int_result(g.x);
    show_x();
}

static void binop(int op)
{
    FRAME *f = &g.fr[g.depth];
    g.lastzero = 0;
    g.entering = 0;
    g.expmode = 0;
    g.xfixed = 0;
    /* observed: an operator leaves Inv and Hyp set ("8.8659iy47hs=" is 8.8659^asinh 47) */
    if (g.opjust && f->n > 0) {
        f->op[f->n - 1] = op;
        return;
    }
    if (f->n > 0) {
        int i, low = 99;
        for (i = 0; i < f->n; i++)
            if (prec(f->op[i]) < low)
                low = prec(f->op[i]);
        if (prec(op) <= low || f->n >= MAXOPS) {
            double v = eval_frame(f, g.x);
            if (g.err)
                return;
            if (g.radix != 10)
                v = int_result(v);
            if (g.err)
                return;
            f->n = 0;
            g.x = v;
            show_x();
        }
    }
    f->v[f->n] = g.x;
    f->op[f->n] = op;
    f->n++;
    g.x = 0;
    g.newnum = 0;
    g.opjust = 1;
}

static void equals(void)
{
    FRAME *f = &g.fr[g.depth];
    double v;
    g.lastzero = 0;
    g.entering = 0;
    g.expmode = 0;
    g.xfixed = 0;
    if (f->n > 0) {
        /* nothing entered: the level's first value with its operator
           ("2+3*=" is 4) */
        if (!g.newnum)
            f->n = 1;
        v = eval_frame(f, g.newnum ? g.x : f->v[0]);
        f->n = 0;
    } else if (g.havelast && !g.newnum) {
        v = apply(lastres, g.lastop, g.lastopnd);
    } else if (g.havelast && g.ceafter) {
        v = apply(lastres, g.lastop, g.lastopnd);
    } else {
        g.opjust = 0;
        if (g.showparen)
            show_x();
        return;
    }
    if (g.err)
        return;
    if (g.radix != 10)
        v = int_result(v);
    if (g.err)
        return;
    g.x = v;
    lastres = v;
    g.havelast = 1;
    g.newnum = 0;
    g.opjust = 0;
    g.ceafter = 0;
    show_x();
}

static void lparen(void)
{
    char t[MAXPAREN + 1];
    int i;
    if (g.depth >= MAXPAREN)
        return;
    if (!g.entering) {
        int pt = g.point;
        entry_from_x();
        g.point = pt && g.nfdig;
    }
    g.depth++;
    g.fr[g.depth].n = 0;
    g.opjust = 0;
    g.parens++;
    for (i = 0; i < g.parens && i < MAXPAREN; i++)
        t[i] = '(';
    t[i] = 0;
    g.showparen = g.parens;
}

static void rparen(void)
{
    FRAME *f = &g.fr[g.depth];
    double v;
    g.entering = 0;
    g.expmode = 0;
    g.xfixed = 0;
    if (g.depth == 0) {
        g.opjust = 0;
        return;
    }
    v = f->n ? eval_frame(f, g.newnum || g.opjust == 0 ? g.x : f->v[0]) : g.x;
    if (g.err)
        return;
    f->n = 0;
    g.depth--;
    g.x = v;
    g.newnum = 1;
    g.opjust = 0;
    show_x();
}

/* ---- functions ---- */

/* degrees and grads to radians: divided by 180/pi and 200/pi written
   with 15 digits (observed: sin 180 deg is -1.209806994168e-015, which
   this conversion gives exactly, as it does sin 30, cos 60 and tan 45's
   last bits; the grad constant is UNVERIFIED by the same method) */
#define DEG_PER_RAD     57.2957795130823
#define GRAD_PER_RAD    63.6619772367581

static double to_rad(double x)
{
    if (g.angle == 0)
        return x / DEG_PER_RAD;
    if (g.angle == 2)
        return x / GRAD_PER_RAD;
    return x;
}

static double from_rad(double x)
{
    if (g.angle == 0)
        return x * DEG_PER_RAD;
    if (g.angle == 2)
        return x * GRAD_PER_RAD;
    return x;
}

double calc_pow(double a, double b)
{
    double r, t;
    if (a == 0) {
        if (b <= 0) {
            set_error(ERR_INVALID);
            return 0;
        }
        return 0;
    }
    if (a < 0) {
        t = fp_trunc(b);
        if (t != b) {
            set_error(ERR_INVALID);
            return 0;
        }
        r = fp_pow(-a, b);
        if (fp_fmod(t, 2) != 0)
            r = -r;
    } else {
        r = fp_pow(a, b);
    }
    if (fp_isinf(r) || r != r) {
        set_error(ERR_BIG);
        return 0;
    }
    return r;
}

static void trig(int id)
{
    double v = operand(), r;
    if (g.hyp) {
        if (!g.inv) {
            double e = fp_exp(v), ei;
            if (fp_isinf(e) && id != IDC_TAN) {
                /* sinh, cosh near the top of the range: e^(x/2) squared */
                double h = fp_exp((v < 0 ? -v : v) / 2);
                r = h * (h / 2);
                if (!fp_isinf(r)) {
                    result(id == IDC_SIN && v < 0 ? -r : r);
                    return;
                }
                set_error(ERR_BIG);
                return;
            }
            ei = 1 / e;
            if (id == IDC_SIN)
                r = (e - ei) / 2;
            else if (id == IDC_COS)
                r = (e + ei) / 2;
            else
                r = fp_isinf(e) ? 1 : (e - ei) / (e + ei);
        } else {
            if (id == IDC_SIN) {
                r = fp_ln(v + fp_sqrt(v * v + 1));
                if (v < 0)
                    r = -fp_ln(-v + fp_sqrt(v * v + 1));
            } else if (id == IDC_COS) {
                if (v < 1) {
                    set_error(ERR_INVALID);
                    return;
                }
                r = fp_ln(v + fp_sqrt(v * v - 1));
            } else {
                if (v <= -1 || v >= 1) {
                    set_error(ERR_INVALID);
                    return;
                }
                r = fp_ln((1 + v) / (1 - v)) / 2;
            }
        }
    } else if (!g.inv) {
        double a = to_rad(v);
        double aa = a < 0 ? -a : a;
        if (aa >= 134217728.0) {        /* UNVERIFIED: the limit is between 1e8 and 1.57e8 */
            set_error(ERR_UNDEF);
            return;
        }
        /* a cosine below 2^-48 is 0, and its tangent undefined (observed:
           cos 90 deg is 0. and tan(pi/2) in radians is undefined; the
           limit is UNVERIFIED) */
        if (id == IDC_SIN) {
            r = fp_sin(a);
        } else {
            double c = fp_cos(a);
            if (c < 3.5527136788005009e-15 && c > -3.5527136788005009e-15) {
                if (id == IDC_TAN) {
                    set_error(ERR_UNDEF);
                    return;
                }
                c = 0;
            }
            r = id == IDC_COS ? c : fp_tan(a);
        }
    } else {
        if (id == IDC_TAN) {
            r = fp_atan2(v, 1);
        } else {
            if (v > 1 || v < -1) {
                set_error(ERR_INVALID);
                return;
            }
            r = fp_atan2(v, fp_sqrt((1 - v) * (1 + v)));
            if (id == IDC_COS)
                r = fp_pi() / 2 - r;
        }
        r = from_rad(r);
    }
    result(r);
}

static void factorial(void)
{
    double v = operand(), r = 1;
    if (v < 0) {
        set_error(ERR_INVALID);
        return;
    }
    if (fp_trunc(v) != v || v > 170) {
        set_error(ERR_BIG);
        return;
    }
    {
        double k;
        for (k = 2; k <= v; k += 1)
            r *= k;
    }
    result(r);
}

/* dms and Inv dms, in the order of operations of Microsoft's Calculator
   engine (src/CalcManager/CEngine/scifunc.cpp, IDC_DMS), whose scifunc.c
   names "SCICALC Scientific Calculator for Windows 3.00.12".
   Adapted from: Copyright (c) Microsoft Corporation. All rights reserved.
   Licensed under the MIT License (shell/calc/LICENSE-mscalc.txt). Checked on 3.1 with CA1 (1.3 Inv
   dms - 1.5 = 0, 2.45 dms - 2.27 = 0, 701.8 Inv dms). */
static void dms(void)
{
    double v = operand(), shft = g.inv ? 100 : 60, deg, minute, second;
    deg = fp_trunc(v);
    minute = (v - deg) * shft;
    {
        /* UNVERIFIED how: the original's minutes come out whole where the
           product is a hair below a whole number (2.45 dms is exactly 2.27,
           1.3 Inv dms exactly 1.5) while other results keep their last
           bits; snapping to a whole number within 1e-9 does both */
        double w = fp_floor(minute + 0.5), dlt = minute - w;
        if (w != 0 && dlt < 1e-9 && dlt > -1e-9)
            minute = w;
    }
    second = minute;
    minute = fp_trunc(minute);
    second = (second - minute) * shft;
    shft = g.inv ? 60 : 100;
    second /= shft;
    minute = (minute + second) / shft;
    result(deg + minute);
}

static void function(int id)
{
    double v = operand(), r = 0;
    int inv = g.inv;
    g.inv = 0;
    switch (id) {
    case IDC_SIN: case IDC_COS: case IDC_TAN:
        g.inv = inv;
        trig(id);
        g.inv = g.hyp = 0;
        return;
    case IDC_LN:
        if (inv) {
            r = fp_exp(v);
            if (fp_isinf(r)) {
                set_error(ERR_BIG);
                return;
            }
        } else {
            if (v <= 0) {
                set_error(ERR_UNDEF);
                return;
            }
            r = fp_ln(v);
        }
        break;
    case IDC_LOG:
        if (inv) {
            r = fp_exp10(v);
            if (fp_isinf(r)) {
                set_error(ERR_BIG);
                return;
            }
        } else {
            if (v <= 0) {
                set_error(ERR_UNDEF);
                return;
            }
            r = fp_log10(v);
        }
        break;
    case IDC_SQR:
        if (inv || !g.sci) {
            if (v < 0) {
                set_error(ERR_INVALID);
                return;
            }
            r = fp_sqrt(v);
        } else {
            r = v * v;
        }
        break;
    case IDC_SQRT:
        if (v < 0) {
            set_error(ERR_INVALID);
            return;
        }
        r = fp_sqrt(v);
        break;
    case IDC_CUBE:
        if (inv) {
            r = v < 0 ? -fp_pow(-v, 1.0 / 3) : v == 0 ? 0 : fp_pow(v, 1.0 / 3);
        } else {
            r = v * v * v;
        }
        break;
    case IDC_RECIP:
        if (v == 0) {
            set_error(ERR_DIV0);
            return;
        }
        r = 1 / v;
        break;
    case IDC_FACT:
        g.inv = inv;                    /* n! has no Inv: it stays set */
        factorial();
        return;
    case IDC_DMS:
        g.inv = inv;
        if (g.radix != 10) {
            g.inv = 0;
            result(v);
            return;
        }
        dms();
        g.inv = 0;
        return;
    case IDC_INT:
        r = fp_trunc(v);
        if (inv)
            r = v - r;
        break;
    case IDC_NOT:
        if (!bits_ok(v)) {
            set_error(ERR_BIG);
            return;
        }
        r = from_bits(~fp_lowbits(v));
        break;
    case IDC_PI:
        if (g.radix != 10)
            return;
        r = fp_pi();
        if (inv)
            r *= 2;
        break;
    case IDC_PERCENT: {
        FRAME *f = &g.fr[g.depth];
        double acc = f->n ? f->v[0] : 0;
        r = acc * operand() / 100;
        break;
    }
    }
    /* observed (CA1): only functions with an Inv meaning use it up; after
       1/x, Not, sqrt and % it is still set; Hyp only goes with sin, cos,
       tan */
    if (id == IDC_RECIP || id == IDC_NOT || id == IDC_SQRT || id == IDC_PERCENT)
        g.inv = inv;
    if (fp_isinf(r) || r != r) {
        set_error(ERR_BIG);
        return;
    }
    result(r);
}

/* ---- radix ---- */

/* an integer result in hex, octal or binary: chopped; outside the 32-bit
   range an error (observed: 4294967296 Hex is too large) */
double int_result(double v)
{
    double t = fp_trunc(v);
    if (!(t > -2147483649.0 && t < 4294967296.0)) {
        set_error(ERR_BIG);
        return 0;
    }
    return t;
}

static void set_radix(int r)
{
    if (r == g.radix)
        return;
    if (r != 10) {
        double t = fp_trunc(g.x);
        if (!(t > -2147483649.0 && t < 4294967296.0)) {
            set_error(ERR_BIG);
            g.radix = r;
            return;
        }
        g.x = t;
    }
    g.radix = r;
    g.entering = 0;
    g.expmode = 0;
    g.xfixed = 0;
    g.point = 0;
    g.lastzero = 0;
    if (g.fe && r != 10)
        g.fe = 0;
    show_x();
}

static void set_size(int i)
{
    if (g.radix == 10) {
        g.angle = i;
        return;
    }
    g.wordsize = i;
    show_x();
}

/* ---- memory and statistics ---- */

static void stat_fn(int id)
{
    int n = ui_stat_count(), i;
    double s = 0, s2 = 0, v, r;
    int inv = g.inv;
    if (!g.stat)
        return;
    g.inv = 0;
    for (i = 0; i < n; i++) {
        v = ui_stat_get(i);
        s += v;
        s2 += v * v;
    }
    switch (id) {
    case IDC_SUM:
        r = inv ? s2 : s;
        break;
    case IDC_AVE:
        if (n == 0) {
            set_error(ERR_DIV0);
            return;
        }
        r = (inv ? s2 : s) / n;
        break;
    default:                            /* s */
        /* observed: 0 for one value or none; two equal values are
           "Invalid value" (the variance is not above 0) */
        if (n <= 1) {
            r = 0;
            break;
        }
        v = (s2 - s * s / n) / (n - (inv ? 0 : 1));
        if (v <= 0) {
            set_error(ERR_INVALID);
            return;
        }
        r = fp_sqrt(v);
        break;
    }
    result(r);
}

/* Statistics Box LOAD: the value becomes the current number */
void calc_load(double v)
{
    if (g.err)
        return;
    g.x = v;
    g.entering = 0;
    g.expmode = 0;
    g.xfixed = 0;
    g.newnum = 1;
    g.opjust = 0;
    g.lastzero = 0;
    show_x();
}

/* ---- the commands ---- */

void calc_clear(void)
{
    int i;
    for (i = 0; i <= MAXPAREN; i++)
        g.fr[i].n = 0;
    g.depth = 0;
    g.parens = 0;
    g.x = 0;
    g.entering = 0;
    g.expmode = 0;
    g.xfixed = 0;
    g.newnum = 0;
    g.opjust = 0;
    g.havelast = 0;
    g.ceafter = 0;
    g.point = 0;
    g.lastzero = 0;
    g.err = 0;
    g.inv = g.hyp = 0;
    g.fe = 0;
    show_x();
}

void calc_init(void)
{
    g.sci = 0;
    g.radix = 10;
    g.wordsize = 0;
    g.angle = 0;
    g.inv = g.hyp = 0;
    g.fe = 0;
    g.mem = 0;
    g.stat = 0;
    calc_clear();
}

/* the standard view is decimal (observed: switching from octal 10 shows
   8) */
void calc_set_view(int sci)
{
    g.sci = sci;
    if (!sci && g.radix != 10 && !g.err)
        set_radix(10);
}

void calc_command(int id)
{
    if (id != IDC_LPAREN)
        g.parens = 0;
    /* the typed-zero rule holds only while the last key was the digit 0 */
    g.prevzero = g.lastzero;
    if (id != IDC_0)
        g.lastzero = 0;
    if (g.err) {
        if (id == IDC_CLEAR) {
            calc_clear();
        } else if (id == IDC_CE) {
            g.err = 0;
            g.x = 0;
            start_entry();
            g.newnum = 0;
            show_x();
        }
        return;
    }
    if ((id >= IDC_0 && id <= IDC_0 + 9) || (id >= IDC_A && id <= IDC_A + 5)) {
        digit(id <= IDC_0 + 9 ? id - IDC_0 : id - IDC_A + 10);
        g.opjust = 0;
        return;
    }
    switch (id) {
    case IDC_POINT:
        point();
        g.opjust = 0;
        break;
    case IDC_EXP:
        exponent();
        g.opjust = 0;
        break;
    case IDC_SIGN:
        sign();
        g.opjust = 0;
        break;
    case IDC_BACK:
        back();
        g.opjust = 0;
        break;
    case IDC_CE:
        {
            int hl = g.havelast;
            g.x = 0;
            start_entry();
            g.havelast = hl;
            g.ceafter = 1;
            g.inv = 0;                  /* observed: CE clears Inv */
            show_x();
        }
        break;
    case IDC_CLEAR:
        calc_clear();
        break;
    case IDC_ADD: case IDC_SUB: case IDC_MUL: case IDC_DIV: case IDC_MOD:
    case IDC_PWR: case IDC_AND: case IDC_OR: case IDC_XOR: case IDC_LSH:
        binop(id);
        break;
    case IDC_EQU:
        equals();
        break;
    case IDC_LPAREN:
        lparen();
        break;
    case IDC_RPAREN:
        rparen();
        break;
    case IDC_SIN: case IDC_COS: case IDC_TAN: case IDC_LN: case IDC_LOG:
    case IDC_SQR: case IDC_CUBE: case IDC_RECIP: case IDC_FACT: case IDC_DMS:
    case IDC_INT: case IDC_NOT: case IDC_PI: case IDC_PERCENT: case IDC_SQRT:
        function(id);
        break;
    case IDC_FE:
        if (g.radix == 10) {
            g.fe ^= 1;
            if (g.entering && !g.expmode)
                show_x();
            else if (!g.entering)
                show_x();
        }
        break;
    case IDC_INV:
        g.inv ^= 1;
        break;
    case IDC_HYP:
        g.hyp ^= 1;
        break;
    case IDC_MC:
        g.mem = 0;
        break;
    case IDC_MR:
        g.x = g.mem;
        g.entering = 0;
        g.expmode = 0;
        g.xfixed = 0;
        g.newnum = 1;
        g.opjust = 0;
        if (g.radix != 10)
            g.x = int_result(g.x);
        show_x();
        break;
    case IDC_MS:
        g.mem = g.x;
        g.entering = 0;
        break;
    case IDC_MPLUS:
        g.mem += g.x;
        g.entering = 0;
        break;
    case IDC_HEX: set_radix(16); break;
    case IDC_DEC: set_radix(10); break;
    case IDC_OCT: set_radix(8); break;
    case IDC_BIN: set_radix(2); break;
    case IDC_SIZE1: case IDC_SIZE2: case IDC_SIZE3:
        set_size(id - IDC_SIZE1);
        break;
    case IDC_AVE: case IDC_SUM: case IDC_DEV:
        stat_fn(id);
        break;
    case IDC_DAT:
        if (g.stat) {
            ui_stat_add(g.x, g.disp);
            g.entering = 0;
            g.lastzero = 0;
            show_x();
        }
        break;
    }
}
