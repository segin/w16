/*
 * mcbchk.c - MCBCHK.EXE: the DOS memory arena as programs that measure
 * memory use it, run in a DOS box under Windows 3.1's WIN386 and w16's
 * (ARCHITECTURE.md 12.10; tests/mcbrun.sh). OpenWatcom 16-bit DOS, small
 * model. MCBCHK [TAG]:
 *   the chain walked from the first MCB (INT 21h 52h) before anything
 *   changes, and what follows the last conventional block (the MCB that
 *   links the upper memory blocks, where DOS has UMBs); the allocation
 *   strategy and UMB link state (5800h, 5802h); the UMBs linked (5803h
 *   BX=1), the chain walked again; a block taken high first (strategy
 *   80h) and freed; the largest block taken low first, shrunk (4Ah),
 *   freed; the link and strategy put back (5803h BX=0, 5801h), the chain
 *   walked once more. Every walk checks that each MCB is 'M' or 'Z' and
 *   that the chain ends with 'Z'.
 * Output: one line per result to the screen and to MC<TAG>.TMP, renamed
 * MC<TAG>.LOG at the end; lines starting "INFO" (segments, sizes, owners:
 * they differ with what runs before the program) are not compared.
 * Black box (ARCHITECTURE.md 3): the published MS-DOS 5.0 functions
 * (INT 21h 48h, 49h, 4Ah, 52h, 58h) and the MCB layout ('M'/'Z', owner at
 * +1, size in paragraphs at +3, name at +8).
 */
#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include <dos.h>
#include <i86.h>

static FILE *logf;
static int fails;
static char tag = 'A';

static void out(const char *fmt, ...)
{
    char b[200];
    va_list a;
    va_start(a, fmt);
    vsprintf(b, fmt, a);
    va_end(a);
    fputs(b, stdout);
    if (logf) { fputs(b, logf); fflush(logf); }
}

static void check(int ok, const char *what)
{
    out("%s %s\n", ok ? "PASS" : "FAIL", what);
    if (!ok) fails++;
}

static unsigned char peekb_(unsigned seg, unsigned off) { return *(unsigned char __far *)MK_FP(seg, off); }
static unsigned peekw_(unsigned seg, unsigned off) { return *(unsigned __far *)MK_FP(seg, off); }

static unsigned first_mcb(void)
{
    union REGS r;
    struct SREGS s;
    segread(&s);
    r.h.ah = 0x52;
    int86x(0x21, &r, &r, &s);
    return *(unsigned __far *)MK_FP(s.es, r.x.bx - 2);
}

/* INT 21h AX=ax BX=bx: AX back, *cf the carry flag, *bxo BX back */
static unsigned dos(unsigned ax, unsigned bx, unsigned es, int *cf, unsigned *bxo)
{
    union REGS r;
    struct SREGS s;
    segread(&s);
    s.es = es;
    r.x.ax = ax;
    r.x.bx = bx;
    int86x(0x21, &r, &r, &s);
    if (cf) *cf = r.x.cflag ? 1 : 0;
    if (bxo) *bxo = r.x.bx;
    return r.x.ax;
}

static void name8(unsigned seg, char *n)
{
    int i;
    for (i = 0; i < 8; i++) {
        unsigned char c = peekb_(seg, 8 + i);
        n[i] = c >= 0x20 && c < 0x7F ? (char)c : (c ? '?' : 0);
        if (!c) break;
    }
    n[i] = 0;
}

static unsigned conv_end;       /* where the unlinked chain's last block ends */

/* the chain from the first MCB: each MCB 'M' or 'Z', the last 'Z' */
static int walk(const char *label, int *umbs)
{
    unsigned seg = first_mcb(), n = 0;
    char nm[9];
    int ok = 0;
    *umbs = 0;
    for (n = 0; n < 400; n++) {
        unsigned char t = peekb_(seg, 0);
        unsigned own = peekw_(seg, 1), sz = peekw_(seg, 3);
        name8(seg, nm);
        out("INFO %s %04X %c %04X %04X %s\n", label, seg, t >= 0x20 && t < 0x7F ? t : '?', own, sz, nm);
        if (t != 'M' && t != 'Z') {
            out("INFO %s: MCB %04X has type %02X\n", label, seg, t);
            break;
        }
        if (seg >= 0xA000) *umbs = 1;
        if (t == 'Z') {
            ok = 1;
            conv_end = seg + sz + 1;
            break;
        }
        if ((unsigned long)seg + sz + 1 > 0xFFFFUL) break;
        seg += sz + 1;
    }
    return ok;
}

int main(int argc, char **argv)
{
    char t[40], done[40];
    int cf, umbs, ok;
    unsigned strat, link, ax, bx, top, seg, max;

    if (argc > 1) tag = argv[1][0];
    sprintf(t, "MC%c.TMP", tag);
    sprintf(done, "MC%c.LOG", tag);
    logf = fopen(t, "w");
    out("MCBCHK %c\n", tag);
    top = peekw_(0x40, 0x13);
    out("INFO BIOS memory %u KB\n", top);

    /* 1. the chain as the box starts it */
    ok = walk("start", &umbs);
    check(ok, "start: the chain ends with Z");
    out("start: UMBs in the chain %s\n", umbs ? "yes" : "no");
    if (ok && conv_end < 0xA000) {
        /* what follows the last conventional block: the UMB link's MCB */
        unsigned char ty = peekb_(conv_end, 0);
        char nm[9];
        name8(conv_end, nm);
        out("INFO after the chain %04X: %02X %04X %04X %s\n", conv_end, ty,
            peekw_(conv_end, 1), peekw_(conv_end, 3), nm);
        out("after the chain: type %c owner %04X name %s, %s\n",
            ty >= 0x20 && ty < 0x7F ? ty : '?', peekw_(conv_end, 1), nm,
            (unsigned long)conv_end + peekw_(conv_end, 3) + 1 > 0xA000UL ? "reaches past A000h" : "below A000h");
    } else if (ok) {
        out("after the chain: the top of conventional memory\n");
    }

    /* 2. strategy and link state */
    strat = dos(0x5800, 0, 0, &cf, NULL);
    out("5800h: CF %d strategy %04X\n", cf, strat);
    link = dos(0x5802, 0, 0, &cf, NULL);
    out("5802h: CF %d link %02X\n", cf, link & 0xFF);

    /* 3. the UMBs linked */
    ax = dos(0x5803, 1, 0, &cf, NULL);
    out("5803h BX=1: CF %d%s\n", cf, cf ? (ax == 1 ? " AX 1" : ax == 7 ? " AX 7" : " AX other") : "");
    ok = walk("linked", &umbs);
    check(ok, "linked: the chain ends with Z");
    out("linked: UMBs in the chain %s\n", umbs ? "yes" : "no");
    ax = dos(0x5802, 0, 0, &cf, NULL);
    out("5802h: CF %d link %02X\n", cf, ax & 0xFF);

    /* 4. high first: a block in upper memory when there is one */
    dos(0x5801, 0x0080, 0, &cf, NULL);
    out("5801h BX=80h: CF %d\n", cf);
    seg = dos(0x4800, 0x0100, 0, &cf, NULL);
    out("INFO 48h 100h (high first): CF %d seg %04X\n", cf, seg);
    out("48h 100h (high first): CF %d%s\n", cf, cf ? "" : seg >= 0xA000 ? " upper memory" : " conventional");
    if (!cf) {
        dos(0x4900, 0, seg, &cf, NULL);
        out("49h: CF %d\n", cf);
    }
    ok = walk("high", &umbs);
    check(ok, "high first, freed: the chain ends with Z");

    /* 5. low first: the largest block, shrunk, freed */
    dos(0x5801, 0x0000, 0, &cf, NULL);
    out("5801h BX=0: CF %d\n", cf);
    ax = dos(0x4800, 0xFFFF, 0, &cf, &max);
    out("48h FFFFh: CF %d AX %04X\n", cf, ax);
    out("INFO largest %04X\n", max);
    seg = dos(0x4800, max, 0, &cf, NULL);
    out("48h largest: CF %d\n", cf);
    if (!cf) {
        ok = walk("taken", &umbs);
        check(ok, "the largest taken: the chain ends with Z");
        dos(0x4A00, max / 2, seg, &cf, NULL);
        out("4Ah half: CF %d\n", cf);
        ok = walk("shrunk", &umbs);
        check(ok, "shrunk: the chain ends with Z");
        dos(0x4900, 0, seg, &cf, NULL);
        out("49h: CF %d\n", cf);
    }
    ok = walk("freed", &umbs);
    check(ok, "freed: the chain ends with Z");

    /* 6. as it was */
    ax = dos(0x5803, link & 1, 0, &cf, NULL);
    out("5803h BX=%u: CF %d\n", link & 1, cf);
    dos(0x5801, strat, 0, &cf, NULL);
    out("5801h back: CF %d\n", cf);
    ok = walk("end", &umbs);
    check(ok, "end: the chain ends with Z");
    out("end: UMBs in the chain %s\n", umbs ? "yes" : "no");

    out("MCBCHK end, %d failure%s\n", fails, fails == 1 ? "" : "s");
    if (logf) {
        fclose(logf);
        remove(done);
        rename(t, done);
    }
    return fails ? 1 : 0;
}
