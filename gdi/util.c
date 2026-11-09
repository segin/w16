/*
 * util.c - small helpers of w16's GDI: the debug log (C:\W16GDI.LOG, only
 * when SYSTEM.INI [w16] gdilog=1), memory and string helpers.
 */
#include "gdi.h"

extern void __cdecl __far gdi_append(const char __far *path, const char __far *buf, WORD len);

static int log_on = -1;
static WORD unimpl_seen[32];           /* bit per ordinal (0-511) */

static int logging(void)
{
    if (log_on < 0)
        log_on = GetPrivateProfileInt("w16", "gdilog", 0, "SYSTEM.INI");
    return log_on;
}

void gdi_log(const char __far *s)
{
    if (logging())
        gdi_append("C:\\W16GDI.LOG", s, lstrlen(s));
}

void gdi_loghex(const char __far *what, WORD v)
{
    char b[48];
    int i, n = 0;
    while (what[n] && n < 36) {
        b[n] = what[n];
        n++;
    }
    b[n++] = ' ';
    for (i = 3; i >= 0; i--)
        b[n++] = "0123456789ABCDEF"[(v >> (i * 4)) & 15];
    b[n++] = '\r';
    b[n++] = '\n';
    b[n] = 0;
    gdi_log(b);
}

/* an export w16 does not implement yet was called (gdi/stubs.asm) */
void __cdecl __far gdi_unimpl(WORD ord)
{
    if (ord < 512 && !(unimpl_seen[ord >> 4] & (1 << (ord & 15)))) {
        unimpl_seen[ord >> 4] |= 1 << (ord & 15);
        gdi_loghex("unimplemented GDI ordinal (hex)", ord);
    }
}

void mem_copy(void __far *d, const void __far *s, WORD n)
{
    char __far *dp = d;
    const char __far *sp = s;
    while (n--)
        *dp++ = *sp++;
}

void mem_set(void __far *d, BYTE v, WORD n)
{
    char __far *dp = d;
    while (n--)
        *dp++ = v;
}

static int lower(int c)
{
    return c >= 'A' && c <= 'Z' ? c + 32 : c;
}

int str_icmp(const char __far *a, const char __far *b)
{
    while (*a && lower(*a) == lower(*b))
        a++, b++;
    return lower(*a) - lower(*b);
}

/* a * b / c, rounded to nearest (halves away from zero) */
int mul_div(int a, int b, int c)
{
    long p = (long)a * b;
    if (!c)
        return -1;
    if ((p < 0) != (c < 0))
        return (int)((p - c / 2) / c);
    return (int)((p + c / 2) / c);
}

void __far *mk_fp(WORD sel, WORD off)
{
    union { void __far *p; struct { WORD off, sel; } w; } u;
    u.w.off = off;
    u.w.sel = sel;
    return u.p;
}
