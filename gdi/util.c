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

/* mem_copy and mem_set: forward string moves, words then the odd byte
   (a byte loop with two far pointers reloads ES twice a byte; REP MOVSW
   moves a word in 4 clocks on a 286). DS, the direction flag aside, is
   kept; n = 0 does nothing (CX = 0). */
void far_copy(void __far *d, const void __far *s, WORD n);
#pragma aux far_copy = \
    "push ds" "mov ds, dx" "cld" "shr cx, 1" "rep movsw" "adc cx, cx" \
    "rep movsb" "pop ds" \
    parm [es di] [dx si] [cx] modify exact [si di cx];

void far_fill(void __far *d, BYTE v, WORD n);
#pragma aux far_fill = \
    "cld" "mov ah, al" "shr cx, 1" "rep stosw" "adc cx, cx" "rep stosb" \
    parm [es di] [al] [cx] modify exact [di cx ah];

void mem_copy(void __far *d, const void __far *s, WORD n)
{
    far_copy(d, s, n);
}

void mem_set(void __far *d, BYTE v, WORD n)
{
    far_fill(d, v, n);
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
