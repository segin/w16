/*
 * util.c - small freestanding helpers (no C runtime in KERNEL).
 */
#include "kernel.h"

void kmemset(void FAR *d, u8 v, u16 n)
{
    u8 FAR *p = d;
    while (n--)
        *p++ = v;
}

void kmemcpy(void FAR *d, const void FAR *s, u16 n)
{
    u8 FAR *dp = d;
    const u8 FAR *sp = s;
    while (n--)
        *dp++ = *sp++;
}

u16 kstrlen(const char FAR *s)
{
    u16 n = 0;
    while (s[n])
        n++;
    return n;
}

static char upper(char c)
{
    return (c >= 'a' && c <= 'z') ? (char)(c - 'a' + 'A') : c;
}

int kstricmp(const char FAR *a, const char FAR *b)
{
    while (*a && upper(*a) == upper(*b)) {
        a++;
        b++;
    }
    return upper(*a) - upper(*b);
}

/* Compare a NUL-terminated string with a length-prefixed one; 0 if equal. */
int kstrnicmp_p(const char FAR *a, const u8 FAR *pstr)
{
    u8 n = pstr[0], i;
    for (i = 0; i < n; i++)
        if (!a[i] || upper(a[i]) != upper((char)pstr[1 + i]))
            return 1;
    return a[n] != 0;
}

/* Write to DOS standard output. */
void kputs(const char FAR *s)
{
    regs16 r;
    r.ax = 0x4000;
    r.bx = 1;
    r.cx = kstrlen(s);
    r.dx = FP_OFF(s);
    r.ds = FP_SEG(s);
    r.es = 0;
    dos21(&r);
}

void kputhex(u16 v)
{
    char b[5];
    int i;
    for (i = 3; i >= 0; i--) {
        u16 d = v & 15;
        b[i] = (char)(d < 10 ? '0' + d : 'A' + d - 10);
        v >>= 4;
    }
    b[4] = 0;
    kputs(b);
}

void kputdec(u16 v)
{
    char b[6];
    int i = 5;
    b[5] = 0;
    do {
        b[--i] = (char)('0' + v % 10);
        v /= 10;
    } while (v);
    kputs(b + i);
}

/* [w16] exitmsg=1 (winboot.c; tests/dropin.sh sets it): "back to DOS"
   is printed when Windows ends. 3.1 prints nothing on a normal exit,
   and the screen may still be in graphics mode then. */
u16 exit_msg;

/* Restore the INT 21h vector and terminate the DOS process. */
void kexit_to_dos(u8 code)
{
    regs16 r;
    if (winboot_active && exit_msg) {   /* a debugging aid */
        kputs("w16: back to DOS, code ");
        kputhex(code);
        kputs("\r\n");
        trace_print();
    }
    r.ds = r.es = 0;
    if (old21) {
        r.ax = 0x0205;
        r.bx = 0x21;
        r.cx = (u16)(old21 >> 16);
        r.dx = (u16)old21;
        dpmi31(&r);
    }
    r.ax = 0x4C00 | code;
    r.ds = r.es = 0;
    dos21(&r);
}
