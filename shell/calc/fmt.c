/*
 * fmt.c - w16 Calculator: number display and entry parsing.
 *
 * Decimal display (observed on Windows 3.1, CA1 runs in DOSBox-X with a
 * coprocessor and with Microsoft's emulator, which agree):
 *  - 13 significant digits. Let e = (int)log10|x|, the logarithm in
 *    extended precision chopped toward zero. If -13 < e < 13 the number
 *    is shown in fixed notation, otherwise (and in F-E mode) in
 *    scientific notation.
 *  - Fixed, |x| >= 1: f = 12 - e digits after the point; the number is
 *    |x| * 10^f rounded to an integer (in extended precision, nearest
 *    even), split into integer and fraction; trailing zeros of the
 *    fraction are dropped but the point stays ("123."); with f = 0 there
 *    is no point ("1234567890123", "10000000000000" for 9999999999999.9).
 *  - Fixed, |x| < 1: z = -e zeros after "0.", then 13 digits of
 *    |x| * 10^(13+z), trailing zeros dropped. If rounding carries into a
 *    fourteenth digit the original keeps "0." and the z zeros only (so
 *    0.0099999999999999 shows "0.00"); when z = 0 the carry gives "1.".
 *  - Scientific: mantissa of 13 digits, trailing zeros dropped, the point
 *    kept ("1.e+015"), 'e', the exponent's sign and three digits, except
 *    an exponent of 0, which is "e+0".
 *  - Zero is "0."; a minus sign or a blank comes first.
 * Hex, octal and binary show the number's low 32, 16 or 8 bits (Dword,
 * Word, Byte) as an unsigned integer without a point.
 *
 * Entry parsing (observed: the typed number's last bits, CA1 "x - A/10^k"
 * probes): the integer digits are accumulated as x = x*10 + d in double
 * precision; each fraction digit adds d * s to x, rounded to double, where
 * s is 0.1, 0.01, ... kept in extended precision.
 */
#include "calc.h"

static char *put(char *p, const char *s)
{
    while (*s)
        *p++ = *s++;
    return p;
}

/* fixed or scientific notation for a decimal number; out gets the sign
   character first */
static void fmt_dec(double x, char *out, int sci)
{
    char d[32];
    char *p = out;
    double a = x < 0 ? -x : x;
    int e, i, n;

    *p++ = x < 0 ? '-' : ' ';
    if (a == 0) {
        if (sci)
            put(p, "0.e+0");
        else
            put(p, "0.");
        p[sci ? 5 : 2] = 0;
        return;
    }
    if (fp_isinf(a)) {                  /* not reached: overflow is an error */
        p = put(p, "1.#INF");
        *p = 0;
        return;
    }
    e = fp_ilog10(a);
    {
        /* 16 significant digits, then 13 (nearest even) */
        int e10 = a < 1 ? e - 1 : e, e0, last, up, carry = 0;
        n = fp_digits(a, 15 - e10, d);
        if (n > 16) {
            e10++;
            n = fp_digits(a, 15 - e10, d);
        } else if (n < 16) {
            e10--;
            n = fp_digits(a, 15 - e10, d);
            if (n > 16) {
                e10++;
                n = fp_digits(a, 15 - e10, d);
            }
        }
        e0 = e10;
        /* round at 13 digits */
        if (d[13] > '5' || (d[13] == '5' && (d[14] != '0' || d[15] != '0')))
            up = 1;
        else if (d[13] == '5')
            up = (d[12] - '0') & 1;
        else
            up = 0;
        d[13] = 0;
        if (up) {
            i = 12;
            while (i >= 0 && d[i] == '9')
                d[i--] = '0';
            if (i >= 0) {
                d[i]++;
            } else {
                d[0] = '1';
                carry = 1;
                e10++;
            }
        }
        if (!sci && e > -13 && e < 13) {
            if (e0 >= 0) {
                int nint = e10 + 1;
                for (i = 0; i < nint; i++)
                    *p++ = i < 13 ? d[i] : '0';
                if (e < 12) {
                    *p++ = '.';
                    last = 13;
                    while (last > nint && d[last - 1] == '0')
                        last--;
                    for (i = nint; i < last; i++)
                        *p++ = d[i];
                }
            } else {
                int z = -e0 - 1;
                if (carry) {
                    /* the original loses the carried digit below 0.1 */
                    if (z == 0) {
                        p = put(p, "1.");
                    } else if (z == 1) {
                        p = put(p, "0.1");
                    } else {
                        p = put(p, "0.");
                        for (i = 0; i < z; i++)
                            *p++ = '0';
                    }
                } else {
                    p = put(p, "0.");
                    for (i = 0; i < z; i++)
                        *p++ = '0';
                    last = 13;
                    while (last > 1 && d[last - 1] == '0')
                        last--;
                    for (i = 0; i < last; i++)
                        *p++ = d[i];
                }
            }
            *p = 0;
            return;
        }
        *p++ = d[0];
        *p++ = '.';
        last = 13;
        while (last > 1 && d[last - 1] == '0')
            last--;
        for (i = 1; i < last; i++)
            *p++ = d[i];
        *p++ = 'e';
        if (e10 == 0) {
            p = put(p, "+0");
        } else {
            int ae = e10 < 0 ? -e10 : e10;
            *p++ = e10 < 0 ? '-' : '+';
            if (ae >= 1000)
                *p++ = (char)('0' + ae / 1000 % 10);
            *p++ = (char)('0' + ae / 100 % 10);
            *p++ = (char)('0' + ae / 10 % 10);
            *p++ = (char)('0' + ae % 10);
        }
        *p = 0;
    }
}

void fmt_number(double x, char *out, int sci, int radix, int wordsize)
{
    U32 v;
    char t[40];
    int n = 0, shift;
    if (radix == 10) {
        fmt_dec(x, out, sci);
        return;
    }
    v = fp_lowbits(x);
    if (wordsize == 1)
        v &= (U32)0xFFFF;
    else if (wordsize == 2)
        v &= (U32)0xFF;
    shift = radix == 16 ? 4 : radix == 8 ? 3 : 1;
    do {
        t[n++] = "0123456789ABCDEF"[(int)(v & (U32)(radix - 1))];
        v >>= shift;
    } while (v);
    while (n)
        *out++ = t[--n];
    *out = 0;
}

/* the value of the typed entry */
double parse_entry(void)
{
    double x = 0;
    int i;
    if (g.radix != 10) {
        for (i = 0; i < g.nidig; i++) {
            int d = g.idig[i] <= '9' ? g.idig[i] - '0' : g.idig[i] - 'A' + 10;
            x = x * g.radix + d;
        }
    } else {
        EXT sc;
        for (i = 0; i < g.nidig; i++)
            x = x * 10 + (g.idig[i] - '0');
        fp_frac_start(&sc);
        for (i = 0; i < g.nfdig; i++)
            x = fp_frac_digit(x, g.fdig[i] - '0', &sc);
    }
    return g.neg ? -x : x;
}
