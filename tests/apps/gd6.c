/*
 * gd6.c - GDI's 32-bit arithmetic helpers (gdi/i4.asm: __I4D, __U4D,
 * __I4M), linked into this probe in place of the compiler library's:
 * signed division and remainder with divisors above and below 65536,
 * every sign combination (the GDI audit's C1: __I4D kept its sign flags in
 * BP, which the long division path used for the remainder). Expected
 * values: C's truncating division (the quotient rounds toward zero, the
 * remainder has the dividend's sign). Run by make test (no Windows needed).
 */
#include <windows.h>

#define TK_REF "gd6ref.h"
#include "tklog.h"

typedef struct { long a, b, q, r; } sdiv;
static const sdiv sd[] = {
    { 100001L, 70000L, 1L, 30001L },
    { -100001L, 70000L, -1L, -30001L },
    { 100001L, -70000L, -1L, 30001L },
    { -100001L, -70000L, 1L, -30001L },
    { 2147483647L, 65536L, 32767L, 65535L },
    { -2147483647L, 65537L, -32767L, -32768L },
    { 123456789L, -99999L, -1234L, 58023L },
    { -2000000000L, 300001L, -6666L, -193334L },
    { 1999999999L, 65536L, 30517L, 37887L },
    { -7L, 74565L, 0L, -7L },
    { 5L, -131073L, 0L, 5L },
    { 1073741827L, 536870913L, 2L, 1L },
    { -1073741827L, 536870913L, -2L, -1L },
    { 1000000L, 3L, 333333L, 1L },
    { -1000000L, 7L, -142857L, -1L },
    { 65535L, -2L, -32767L, 1L },
    { -2147483647L, 2147483647L, -1L, 0L },
    { 987654321L, 123457L, 7999L, 121778L },
    { -987654321L, 123457L, -7999L, -121778L },
    { 987654321L, -123459L, -7999L, 105780L },
    { -987654322L, -123459L, 7999L, -105781L },
};

typedef struct { DWORD a, b, q, r; } udiv;
static const udiv ud[] = {
    { 0xFFFFFFFFUL, 0x10001UL, 0xFFFFUL, 0UL },
    { 0xFFFFFFFFUL, 0x12345UL, 0xE100UL, 0x5AFFUL },
    { 0x80000000UL, 0x7FFFFFFFUL, 1UL, 1UL },
    { 100001UL, 70000UL, 1UL, 30001UL },
    { 0x12345678UL, 0x10UL, 0x1234567UL, 8UL },
};

static void hex32(DWORD v)
{
    outhex(HIWORD(v));
    outhex(LOWORD(v));
}

int __cdecl tmain(void)
{
    volatile long a, b;
    volatile DWORD ua, ub;
    long q, r;
    DWORD uq, ur;
    int i;
    for (i = 0; i < sizeof sd / sizeof sd[0]; i++) {
        a = sd[i].a;
        b = sd[i].b;
        q = a / b;
        r = a % b;
        if (q != sd[i].q || r != sd[i].r) {
            out("FAIL idiv ");
            hex32(sd[i].a);
            out(" / ");
            hex32(sd[i].b);
            out(": ");
            hex32(q);
            out(" rem ");
            hex32(r);
            out("\r\n");
            failures++;
        }
    }
    check(failures == 0, "signed 32-bit division and remainder");
    for (i = 0; i < sizeof ud / sizeof ud[0]; i++) {
        ua = ud[i].a;
        ub = ud[i].b;
        uq = ua / ub;
        ur = ua % ub;
        if (uq != ud[i].q || ur != ud[i].r) {
            out("FAIL udiv ");
            hex32(ud[i].a);
            out(" / ");
            hex32(ud[i].b);
            out("\r\n");
            failures++;
        }
    }
    a = -123457L;
    b = 98765L;
    q = a * b;                          /* -12193230605, low 32 bits */
    check((DWORD)q == 0x293A10F3UL, "signed 32-bit multiply");
    return failures;
}
