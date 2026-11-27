/*
 * dosst.c - DOSST.EXE: the real-mode machine state DOS is left with, for
 * comparing it before Windows starts and after it ends (tests/dosstrun.sh,
 * boot-test case win386exit). OpenWatcom 16-bit DOS, small model.
 * DOSST TAG writes DS<TAG>.LOG (DS<TAG>.TMP until it is complete):
 *   VEC nn sss:ooo   the 256 interrupt vectors, with the owner of the
 *                    memory they point at: the MCB's owner (a PSP or 8
 *                    for DOS), "free" for a free block, "bios" above the
 *                    upper memory blocks, "dos" below the first MCB
 *   MCB ...          the memory arena walked from the first MCB (INT 21h
 *                    52h), the upper memory blocks linked for the walk
 *                    (5803h BX=1, the link put back after)
 *   STRAT, LINK      the allocation strategy and UMB link (5800h, 5802h)
 *   PIC              the two interrupt controllers' masks
 *   BDA              40:00-40:FF less the timer count and keyboard buffer
 *                    head/tail (they change on their own)
 *   MOUSE            the INT 33h driver's user event handler (0014h swaps
 *                    it out and back)
 *   XMS             the driver's version, A20 state, free memory, and
 *                    whether the HMA can be taken (and then gives it back)
 * Black box (ARCHITECTURE.md 3): the published MS-DOS 5.0 functions, the
 * MCB layout ('M'/'Z', owner at +1, size at +3) and the XMS 2.0 calls.
 */
#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include <dos.h>
#include <i86.h>
#include <conio.h>

static FILE *logf;

static void out(const char *fmt, ...)
{
    char b[200];
    va_list a;
    va_start(a, fmt);
    vsprintf(b, fmt, a);
    va_end(a);
    if (logf) fputs(b, logf);
}

static unsigned char peekb_(unsigned seg, unsigned off) { return *(unsigned char __far *)MK_FP(seg, off); }
static unsigned peekw_(unsigned seg, unsigned off) { return *(unsigned __far *)MK_FP(seg, off); }

static unsigned dos(unsigned ax, unsigned bx, int *cf)
{
    union REGS r;
    r.x.ax = ax;
    r.x.bx = bx;
    intdos(&r, &r);
    if (cf) *cf = r.x.cflag ? 1 : 0;
    return r.x.ax;
}

static unsigned first_mcb(void)
{
    union REGS r;
    struct SREGS s;
    segread(&s);
    r.h.ah = 0x52;
    int86x(0x21, &r, &r, &s);
    return *(unsigned __far *)MK_FP(s.es, r.x.bx - 2);
}

#define MAXB 128
static unsigned bseg[MAXB], bsize[MAXB], bown[MAXB];
static char btype[MAXB];
static int nb;

static void walk(void)
{
    unsigned s = first_mcb(), n = 0;
    char t;
    nb = 0;
    for (;;) {
        t = peekb_(s, 0);
        if (t != 'M' && t != 'Z') { out("MCB %04X bad type %02X\n", s, (unsigned char)t); break; }
        if (nb < MAXB) {
            bseg[nb] = s; bsize[nb] = peekw_(s, 3); bown[nb] = peekw_(s, 1); btype[nb] = t; nb++;
        }
        if (t == 'Z' || ++n > 400) break;
        s += peekw_(s, 3) + 1;
    }
}

static const char *owner_of(unsigned long lin, char *b)
{
    int i;
    unsigned seg = (unsigned)(lin >> 4);
    if (nb == 0 || seg < bseg[0]) return "dos";
    for (i = 0; i < nb; i++) {
        if (seg > bseg[i] && seg <= bseg[i] + bsize[i]) {
            if (bown[i] == 0) return "free";
            sprintf(b, "%04X", bown[i]);
            return b;
        }
    }
    return "bios";
}

static unsigned long xms_entry;
static unsigned xr_ax, xr_bx, xr_dx;

/* the XMS driver: AX, DX in; AX, BX, DX out */
static unsigned xms(unsigned ax, unsigned dx, unsigned *bx, unsigned *dxo)
{
    xr_ax = ax; xr_dx = dx; xr_bx = 0;
    _asm {
        push bp
        push si
        push di
        mov ax, xr_ax
        mov dx, xr_dx
        mov bx, 0
        call dword ptr xms_entry
        mov xr_ax, ax
        mov xr_bx, bx
        mov xr_dx, dx
        pop di
        pop si
        pop bp
    }
    *bx = xr_bx; *dxo = xr_dx;
    return xr_ax;
}

static void dump_xms(void)
{
    union REGS r;
    struct SREGS s;
    unsigned bx = 0, dx = 0, v, a;
    r.x.ax = 0x4300;
    int86(0x2F, &r, &r);
    if (r.h.al != 0x80) { out("XMS none\n"); return; }
    segread(&s);
    r.x.ax = 0x4310;
    int86x(0x2F, &r, &r, &s);
    xms_entry = ((unsigned long)s.es << 16) | r.x.bx;
    v = xms(0x0000, 0, &bx, &dx);
    out("XMS version %04X hma-exists %u\n", v, dx);
    a = xms(0x0700, 0, &bx, &dx);
    out("XMS a20 %u\n", a);
    v = xms(0x0800, 0, &bx, &dx);
    out("XMS free largest %u total %u\n", v, dx);
    v = xms(0x0100, 0xFFFF, &bx, &dx);
    out("XMS hma-request %u err %02X\n", v, bx & 0xFF);
    if (v == 1) xms(0x0200, 0, &bx, &dx);
}

/* the INT 33h driver's user event handler (AX=0014h swaps it: swapped
 * out with mask 0 and back), when INT 33h has a handler */
static void dump_mouse(void)
{
    union REGS r;
    struct SREGS s;
    unsigned mask, seg, off;
    if (!peekw_(0, 0x33 * 4) && !peekw_(0, 0x33 * 4 + 2)) { out("MOUSE none\n"); return; }
    segread(&s);
    s.es = 0;
    r.x.ax = 0x0014; r.x.cx = 0; r.x.dx = 0;
    int86x(0x33, &r, &r, &s);
    mask = r.x.cx; seg = s.es; off = r.x.dx;
    s.es = seg;
    r.x.ax = 0x0014; r.x.cx = mask; r.x.dx = off;
    int86x(0x33, &r, &r, &s);
    if (mask) out("MOUSE handler mask %04X at %04X:%04X\n", mask, seg, off);
    else out("MOUSE handler none\n");
}

int main(int argc, char **argv)
{
    char name[16], tmp[16], b[8];
    int i, cf;
    unsigned link, strat;
    char tag = argc > 1 ? argv[1][0] : 'A';
    sprintf(tmp, "DS%c.TMP", tag);
    sprintf(name, "DS%c.LOG", tag);
    logf = fopen(tmp, "w");
    if (!logf) return 1;
    strat = dos(0x5800, 0, &cf);
    out("STRAT %04X\n", cf ? 0xFFFF : strat);
    link = dos(0x5802, 0, &cf) & 0xFF;
    out("LINK %02X\n", cf ? 0xFF : link);
    if (!cf) dos(0x5803, 1, &cf);
    walk();
    if (link != 0xFF) dos(0x5803, link & 1, &cf);
    for (i = 0; i < nb; i++) {
        char n[9];
        int k;
        for (k = 0; k < 8; k++) {
            n[k] = peekb_(bseg[i], 8 + k);
            if (n[k] < ' ' || n[k] > '~') n[k] = '.';
        }
        n[8] = 0;
        out("MCB %04X %c owner %04X size %04X %s\n", bseg[i], btype[i], bown[i], bsize[i],
            bown[i] == 0 ? "" : n);
    }
    for (i = 0; i < 256; i++) {
        unsigned o = peekw_(0, i * 4), s = peekw_(0, i * 4 + 2);
        out("VEC %02X %04X:%04X %s\n", i, s, o, owner_of(((unsigned long)s << 4) + o, b));
    }
    out("PIC %02X %02X\n", inp(0x21), inp(0xA1));
    for (i = 0; i < 256; i += 16) {
        int k;
        out("BDA %02X", i);
        for (k = 0; k < 16; k++) {
            int j = i + k;
            /* timer count (6Ch-70h) and the keyboard buffer's head and
             * tail (1Ah-1Dh) and its contents (1Eh-3Dh) change by
             * themselves */
            if ((j >= 0x6C && j <= 0x70) || (j >= 0x1A && j <= 0x3D)) out(" --");
            else out(" %02X", peekb_(0x40, j));
        }
        out("\n");
    }
    dump_mouse();
    dump_xms();
    out("END\n");
    fclose(logf);
    remove(name);
    rename(tmp, name);
    return 0;
}
