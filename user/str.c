/*
 * str.c - USER's string functions: AnsiUpper/Lower/Next/Prev, IsChar*,
 * lstrcmp/lstrcmpi, wsprintf, LoadString.
 *
 * The character tables and the sort order are Windows 3.1's, observed with
 * probe US2 (tests/apps/us2.c; reference in tests/apps/us2ref.h and
 * ARCHITECTURE.md 10): the ANSI character set's letters are A-Z, a-z,
 * 8Ah 8Ch 9Ah 9Ch 9Fh and C0h-FFh except D7h and F7h; upper case pairs with
 * lower case 20h above it (8Ah/9Ah, 8Ch/9Ch, 9Fh/FFh); DFh and FFh have no
 * upper case. lstrcmp sorts by a weight per character (below); lstrcmpi
 * also folds case first.
 */
#include "user.h"

/* overlapping blocks are fine (memmove): a list box makes room for an
   item by moving those after it up one place in the same block, which a
   forward copy smeared over the rest (File Manager's sorted owner-drawn
   file list: every item after the insertion point became the same one,
   its WM_COMPAREITEM then got stale data, and on w16's KERNEL its lstrcat
   faulted, back to DOS) */
void fmemcpy(void __far *d, const void __far *s, WORD n)
{
    BYTE __far *dd = (BYTE __far *)d;
    const BYTE __far *ss = (const BYTE __far *)s;
    if (FP_SEG(dd) == FP_SEG(ss) && FP_OFF(dd) > FP_OFF(ss) && FP_OFF(dd) - FP_OFF(ss) < n) {
        dd += n;
        ss += n;
        while (n--)
            *--dd = *--ss;
        return;
    }
    while (n--)
        *dd++ = *ss++;
}

void fmemset(void __far *d, BYTE v, WORD n)
{
    BYTE __far *dd = (BYTE __far *)d;
    while (n--)
        *dd++ = v;
}

WORD lstr_len(LPCSTR s)
{
    WORD n = 0;
    if (!s)
        return 0;
    while (s[n])
        n++;
    return n;
}

void lstr_cpyn(LPSTR d, LPCSTR s, WORD n)
{
    if (!n)
        return;
    while (--n && *s)
        *d++ = *s++;
    *d = 0;
}

WORD local_strdup(LPCSTR s)
{
    WORD n;
    char __near *p;
    if (!s)
        return 0;
    n = lstr_len(s);
    p = (char __near *)LocalAlloc(LMEM_FIXED, n + 1);
    if (p)
        fmemcpy((char __far *)p, s, n + 1);
    return (WORD)p;
}

/* ---- character classes ---- */
static BOOL is_upper(BYTE c)
{
    return (c >= 'A' && c <= 'Z') || c == 0x8A || c == 0x8C || c == 0x9F ||
           (c >= 0xC0 && c <= 0xDE && c != 0xD7);
}

static BOOL is_lower(BYTE c)
{
    return (c >= 'a' && c <= 'z') || c == 0x9A || c == 0x9C || c == 0xFF ||
           (c >= 0xDF && c != 0xF7);
}

static BYTE to_upper(BYTE c)
{
    if ((c >= 'a' && c <= 'z') || (c >= 0xE0 && c <= 0xFE && c != 0xF7))
        return c - 0x20;
    if (c == 0x9A || c == 0x9C)
        return c - 0x10;
    if (c == 0xFF)
        return 0x9F;
    return c;
}

static BYTE to_lower(BYTE c)
{
    if ((c >= 'A' && c <= 'Z') || (c >= 0xC0 && c <= 0xDE && c != 0xD7))
        return c + 0x20;
    if (c == 0x8A || c == 0x8C)
        return c + 0x10;
    if (c == 0x9F)
        return 0xFF;
    return c;
}

BOOL WINAPI IsCharAlpha(char c)        { return is_upper(c) || is_lower(c); }
BOOL WINAPI IsCharUpper(char c)        { return is_upper(c); }
BOOL WINAPI IsCharLower(char c)        { return is_lower(c); }
BOOL WINAPI IsCharAlphaNumeric(char c) { return IsCharAlpha(c) || ((BYTE)c >= '0' && (BYTE)c <= '9'); }

/* AnsiUpper/AnsiLower: with a selector of 0 the argument is a character */
LPSTR WINAPI AnsiUpper(LPSTR s)
{
    LPSTR p;
    if (!HIWORD((DWORD)s))
        return (LPSTR)MAKELP(0, to_upper((BYTE)LOWORD((DWORD)s)));
    for (p = s; *p; p++)
        *p = to_upper(*p);
    return s;
}

LPSTR WINAPI AnsiLower(LPSTR s)
{
    LPSTR p;
    if (!HIWORD((DWORD)s))
        return (LPSTR)MAKELP(0, to_lower((BYTE)LOWORD((DWORD)s)));
    for (p = s; *p; p++)
        *p = to_lower(*p);
    return s;
}

UINT WINAPI AnsiUpperBuff(LPSTR s, UINT n)
{
    UINT i;
    if (!n)
        n = 0x10000L > 0xFFFF ? 0xFFFF : 0;     /* 0 means 64K (3.1 SDK) */
    for (i = 0; i < n; i++)
        s[i] = to_upper(s[i]);
    return n;
}

UINT WINAPI AnsiLowerBuff(LPSTR s, UINT n)
{
    UINT i;
    if (!n)
        n = 0xFFFF;
    for (i = 0; i < n; i++)
        s[i] = to_lower(s[i]);
    return n;
}

LPSTR WINAPI AnsiNext(LPCSTR s)
{
    return (LPSTR)(*s ? s + 1 : s);
}

LPSTR WINAPI AnsiPrev(LPCSTR start, LPCSTR s)
{
    return (LPSTR)(s > start ? s - 1 : s);
}

/* ---- sorting ----
   The weight of each character; equal weights sort equal (lstrcmpi) or
   by code (lstrcmp's tie break: lower case before upper case, as US2
   shows "a" < "A"). The order (US2's lstrcmp.0-7): control characters
   01h-1Fh, then 20h-2Fh, 3Ah-40h, 5Bh-60h, 7Bh-BFh in code order, D7h,
   DFh, the accented letters C0h/E0h ... (each upper case before its lower
   case for lstrcmp), F7h after F6h, then digits 0-9, then A a B b ... Z z. */
/* (far: out of DGROUP, whose room is windows' and classes') */
static BYTE __far weight[256];
static BYTE __far tiebreak[256];

static void make_weights(void)
{
    WORD c, w = 1;
    static const BYTE punct_ranges[][2] = {
        { 0x01, 0x2F }, { 0x3A, 0x40 }, { 0x5B, 0x60 }, { 0x7B, 0xBF }, { 0, 0 }
    };
    int i;
    if (weight['a'])
        return;
    for (i = 0; punct_ranges[i][0]; i++)
        for (c = punct_ranges[i][0]; c <= punct_ranges[i][1]; c++) {
            if (c == 0x8A || c == 0x8C || c == 0x9A || c == 0x9C || c == 0x9F)
                ;               /* letters by class, but sorted here (US2) */
            weight[c] = (BYTE)w++;
        }
    weight[0xD7] = (BYTE)w++;
    weight[0xDF] = (BYTE)w++;
    for (c = 0xC0; c <= 0xDE; c++) {
        if (c == 0xD7) {
            continue;
        }
        weight[c] = weight[c + 0x20] = (BYTE)w++;
        tiebreak[c] = 1;            /* upper case after lower case? see below */
        if (c == 0xD6)
            weight[0xF7] = (BYTE)w++;
    }
    weight[0xFF] = (BYTE)w++;
    for (c = '0'; c <= '9'; c++)
        weight[c] = (BYTE)w++;
    for (c = 'A'; c <= 'Z'; c++) {
        weight[c] = weight[c + 0x20] = (BYTE)w++;
        tiebreak[c] = 1;
    }
}

/* US2: lstrcmp's order lists C0h before E0h but "A" before "a"... the
   observed sequence is "41 61" (A then a) and "C0 E0": upper case first;
   yet pair6 ("A" vs "a") gives -1 (A < a). Both agree: upper first. */
static int cmp(LPCSTR a, LPCSTR b, BOOL icase)
{
    const BYTE __far *p = (const BYTE __far *)a, __far *q = (const BYTE __far *)b;
    int tie = 0;
    make_weights();
    for (;; p++, q++) {
        BYTE x = *p, y = *q;
        if (!x || !y) {
            if (x != y)
                return x ? 1 : -1;
            return icase ? 0 : tie;
        }
        if (weight[x] != weight[y])
            return weight[x] < weight[y] ? -1 : 1;
        if (!tie && x != y)
            tie = is_upper(x) ? -1 : 1;
    }
}

int WINAPI lstrcmp(LPCSTR a, LPCSTR b)
{
    return cmp(a, b, FALSE);
}

int WINAPI lstrcmpi(LPCSTR a, LPCSTR b)
{
    return cmp(a, b, TRUE);
}

int lstr_cmpi(LPCSTR a, LPCSTR b)
{
    return cmp(a, b, TRUE);
}

/* ---- wsprintf (the 3.1 SDK's format: %[-][#][0][width][.precision]
   [l]{d,i,u,x,X,c,s,%}; '+' and ' ' are not flags: US2 printed "+d") ---- */
int WINAPI wvsprintf(LPSTR out, LPCSTR fmt, const void __far *args)
{
    const BYTE __far *ap = (const BYTE __far *)args;
    LPSTR o = out;
    while (*fmt) {
        char buf[16], *b;
        LPCSTR s;
        int left = 0, zero = 0, alt = 0, width = 0, prec = -1, lng = 0, len, neg;
        DWORD v;
        if (*fmt != '%') {
            *o++ = *fmt++;
            continue;
        }
        fmt++;
        for (;; fmt++) {
            if (*fmt == '-') left = 1;
            else if (*fmt == '#') alt = 1;
            else if (*fmt == '0') zero = 1;
            else break;
        }
        while (*fmt >= '0' && *fmt <= '9')
            width = width * 10 + *fmt++ - '0';
        if (*fmt == '.') {
            prec = 0;
            fmt++;
            while (*fmt >= '0' && *fmt <= '9')
                prec = prec * 10 + *fmt++ - '0';
        }
        if (*fmt == 'l' || *fmt == 'L') {
            lng = 1;
            fmt++;
        }
        neg = 0;
        b = buf + sizeof buf;
        *--b = 0;
        switch (*fmt) {
        case 'd':
        case 'i':
        case 'u':
            if (lng) {
                v = *(const DWORD __far *)ap;
                ap += 4;
                if (*fmt != 'u' && (long)v < 0) {
                    neg = 1;
                    v = (DWORD)-(long)v;
                }
            } else {
                v = *(const WORD __far *)ap;
                ap += 2;
                if (*fmt != 'u' && (int)v < 0) {
                    neg = 1;
                    v = (WORD)-(int)(WORD)v;
                }
            }
            do {
                *--b = (char)('0' + v % 10);
                v /= 10;
            } while (v);
            s = b;
            break;
        case 'x':
        case 'X':
            if (lng) {
                v = *(const DWORD __far *)ap;
                ap += 4;
            } else {
                v = *(const WORD __far *)ap;
                ap += 2;
            }
            do {
                *--b = (*fmt == 'x' ? "0123456789abcdef" : "0123456789ABCDEF")[v & 15];
                v >>= 4;
            } while (v);
            if (alt) {
                *--b = *fmt;
                *--b = '0';
            }
            s = b;
            break;
        case 'c':
            buf[0] = (char)*(const WORD __far *)ap;
            buf[1] = 0;
            ap += 2;
            s = buf;
            break;
        case 's':
            s = *(LPCSTR __far *)ap;
            ap += 4;
            if (!s)
                s = "(null)";       /* UNVERIFIED: what 3.1 prints for NULL */
            break;
        case 0:
            continue;
        default:
            *o++ = *fmt++;
            continue;
        }
        fmt++;
        len = lstr_len(s);
        if (prec >= 0 && *(fmt - 1) == 's' && len > prec)
            len = prec;
        if (!left) {
            if (zero && neg) {
                *o++ = '-';
                neg = 0;
                width--;
            }
            while (width > len + neg) {
                *o++ = zero ? '0' : ' ';
                width--;
            }
        }
        if (neg) {
            *o++ = '-';
            width--;
        }
        {
            int i;
            for (i = 0; i < len; i++)
                *o++ = s[i];
        }
        while (left && width > len) {
            *o++ = ' ';
            width--;
        }
    }
    *o = 0;
    return (int)(o - out);
}

int __far __cdecl wsprintf(LPSTR out, LPCSTR fmt, ...)
{
    return wvsprintf(out, fmt, (const void __far *)(&fmt + 1));
}

/* ---- LoadString: string tables of 16 (3.1 SDK resource format) ---- */
int WINAPI LoadString(HINSTANCE hinst, UINT id, LPSTR buf, int n)
{
    HRSRC r;
    HGLOBAL g;
    const BYTE __far *p;
    UINT i;
    int len = 0;
    if (n <= 0 || !buf)
        return 0;
    r = FindResource(hinst, MAKEINTRESOURCE((id >> 4) + 1), RT_STRING);
    if (!r) {
        buf[0] = 0;
        return 0;
    }
    g = LoadResource(hinst, r);
    p = (const BYTE __far *)LockResource(g);
    if (p) {
        for (i = 0; i < (id & 15); i++)
            p += 1 + *p;
        len = *p;
        if (len >= n)
            len = n - 1;
        fmemcpy(buf, p + 1, len);
        GlobalUnlock(g);
    }
    buf[len] = 0;
    FreeResource(g);
    return len;
}
