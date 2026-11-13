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
/* KERNEL's messages go to its own standard output: handle 1 of its PSP,
   not of the running task's (where it may be a file the task opened;
   KERNEL audit, low) */
void kputs(const char FAR *s)
{
    regs16 r;
    kernel_psp(1);
    r.ax = 0x4000;
    r.bx = 1;
    r.cx = kstrlen(s);
    r.dx = FP_OFF(s);
    r.ds = FP_SEG(s);
    r.es = 0;
    dos21(&r);
    kernel_psp(0);
}

/* ---- w16's own diagnostics (kdbg) ----
   Windows 3.1 writes nothing of its own to standard output while it
   runs (its OutputDebugString goes to handle 3, AUX: task.c). w16's
   diagnostics (unimplemented calls, WinOldApCall notes, [w16] schedlog
   and the like) went to handle 1, which a user's session has on the
   console: painted over Windows' screen (KERNEL audit, finding 9). They
   go to the file [w16] debuglog= names, appended, when it is set; else
   to KERNEL's standard output only while no Windows screen is up (make
   test, the boot before USER); else nowhere. Reports that end Windows
   (faults, the watchdog, "Error loading") switch to text mode and still
   use kputs. */
static char dbg_path[80];
u16 win_screen;                 /* USER loaded, Windows' screen up */

void kdbg_init(const char FAR *path)
{
    u16 i;
    for (i = 0; path[i] && i < sizeof dbg_path - 1; i++)
        dbg_path[i] = path[i];
    dbg_path[i] = 0;
}

/* to the [w16] debuglog file only (nothing without it) */
void kdbg_file(const char FAR *s)
{
    regs16 r;
    u16 h;
    if (!dbg_path[0])
        return;
    kernel_psp(1);
    r.ax = 0x3D01;                      /* open for writing */
    r.dx = FP_OFF((char FAR *)dbg_path);
    r.ds = FP_SEG((char FAR *)dbg_path);
    r.es = 0;
    if (dos21(&r)) {
        r.ax = 0x3C00;                  /* or create it */
        r.cx = 0;
        r.dx = FP_OFF((char FAR *)dbg_path);
        r.ds = FP_SEG((char FAR *)dbg_path);
        r.es = 0;
        if (dos21(&r)) {
            kernel_psp(0);
            return;
        }
    }
    h = r.ax;
    r.ax = 0x4202;                      /* to the end */
    r.bx = h;
    r.cx = r.dx = 0;
    r.ds = r.es = 0;
    dos21(&r);
    r.ax = 0x4000;
    r.bx = h;
    r.cx = kstrlen(s);
    r.dx = FP_OFF(s);
    r.ds = FP_SEG(s);
    r.es = 0;
    dos21(&r);
    r.ax = 0x3E00;
    r.bx = h;
    r.ds = r.es = 0;
    dos21(&r);
    kernel_psp(0);
}

void kdbg(const char FAR *s)
{
    if (dbg_path[0])
        kdbg_file(s);
    else if (!win_screen)
        kputs(s);
}

void kdbghex(u16 v)
{
    char b[5];
    int i;
    for (i = 3; i >= 0; i--) {
        u16 d = v & 15;
        b[i] = (char)(d < 10 ? '0' + d : 'A' + d - 10);
        v >>= 4;
    }
    b[4] = 0;
    kdbg(b);
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
