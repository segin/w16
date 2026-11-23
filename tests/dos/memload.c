/*
 * memload.c - MEMLOAD.EXE: DOS programs that use XMS and EMS the way real
 * applications do, run in DOS boxes under Windows 3.1's WIN386 and w16's
 * (V86MMGR, ARCHITECTURE.md 12.13; tests/memloadrun.sh). OpenWatcom 16-bit
 * DOS, large model. MEMLOAD MODE TAG [ROUNDS]:
 *   X  the XMS workload: as much memory as the driver gives in several
 *      blocks, each filled with a pattern through moves from conventional
 *      memory; block-to-block moves (an odd length, an odd offset, an
 *      overlapping move inside a block both ways); everything read back
 *      and checked; all freed and one block taken again; the HMA (02h,
 *      01h, A20 on with 05h, a pattern at FFFF:0010-FFFF:FFFF, checked,
 *      wrap tested with A20 off again).
 *   E  the EMS workload: pages over three handles, each filled through
 *      the frame, mapped again in a pseudo-random order and checked;
 *      47h/48h and 4E00h/4E01h save and restore the map; 50h maps four
 *      pages at once; 57h moves conventional memory to a page and back
 *      and exchanges two pages' contents; all freed.
 *   3  XMS 3.0 and UMBs: 88h, 89h, 8Eh, 8Fh, 10h, 11h, 12h; the answers.
 *   C  both workloads, ROUNDS times (default 3), with a seed from TAG, so
 *      that boxes running at once use different data (the harness waits
 *      for every box's log).
 * Output: one line per result to the screen and to ML<TAG>.TMP, renamed
 * ML<TAG>.LOG at the end (the harness waits for that name):
 *   results that must equal Windows 3.1's (PASS/FAIL lines, counts,
 *   error codes); lines starting "RATE" (KB/s by the BIOS tick count, the
 *   VM's own time) and "INFO" are not compared.
 * Black box (ARCHITECTURE.md 3): the published XMS 2.0/3.0 and LIM EMS 4.0
 * specifications' functions.
 */
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <dos.h>
#include <i86.h>

struct xr { unsigned ax, bx, dx, si, ds; };
struct xr32 { unsigned long eax, ebx, ecx, edx; };
extern void __cdecl __far xms_call(struct xr __far *r);
extern void __cdecl __far xms_call32(struct xr32 __far *r);
extern unsigned long __far xms_entry;
#pragma aux xms_entry "_*"

#pragma pack(1)
struct xmove { unsigned long len; unsigned sh; unsigned long so; unsigned dh; unsigned long doff; };
#pragma pack()

static FILE *logf;
static unsigned seed;
static int fails;
static char tag = 'A';
static int conc;                        /* C: boxes running together */

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

static unsigned long ticks(void)
{
    return *(unsigned long __far *)MK_FP(0x40, 0x6C);
}

static void rate(const char *what, unsigned long kb, unsigned long t0)
{
    unsigned long t = ticks() - t0;
    if (!t) t = 1;
    out("RATE %s %lu KB/s\n", what, kb * 182UL / (t * 10UL));
}

/* the pattern word at byte offset off of block b */
static unsigned pat(unsigned b, unsigned long off)
{
    unsigned long w = off >> 1;
    return (unsigned)((w * 40503UL) ^ (w >> 7) ^ (b * 9973U) ^ seed);
}

static unsigned char buf[4096], buf2[4096];

static void fill(unsigned b, unsigned long off, unsigned n)
{
    unsigned i;
    for (i = 0; i < n; i += 2) *(unsigned *)(buf + i) = pat(b, off + i);
}

static int same(unsigned b, unsigned long off, unsigned n)
{
    unsigned i;
    for (i = 0; i < n; i += 2)
        if (*(unsigned *)(buf + i) != pat(b, off + i)) return 0;
    return 1;
}

/* ---------------------------------------------------------------- XMS */
static int xms_ok;

static int xms_init(void)
{
    union REGS r;
    struct SREGS s;
    if (xms_ok) return 1;
    r.x.ax = 0x4300;
    int86(0x2F, &r, &r);
    if (r.h.al != 0x80) return 0;
    r.x.ax = 0x4310;
    segread(&s);
    int86x(0x2F, &r, &r, &s);
    xms_entry = ((unsigned long)s.es << 16) | r.x.bx;
    xms_ok = 1;
    return 1;
}

static struct xr x;

static unsigned xcall(unsigned ax, unsigned bx, unsigned dx)
{
    x.ax = ax; x.bx = bx; x.dx = dx; x.si = 0; x.ds = 0;
    xms_call(&x);
    return x.ax;
}

static unsigned xmove(unsigned long len, unsigned sh, unsigned long so, unsigned dh, unsigned long doff)
{
    static struct xmove m;
    m.len = len; m.sh = sh; m.so = so; m.dh = dh; m.doff = doff;
    x.ax = 0x0B00; x.bx = 0; x.dx = 0;
    x.si = FP_OFF(&m); x.ds = FP_SEG(&m);
    xms_call(&x);
    return x.ax;
}

static unsigned long far_addr(void __far *p)
{
    return ((unsigned long)FP_SEG(p) << 16) | FP_OFF(p);
}

#define NBLK 8

static void xms_workload(void)
{
    unsigned h[NBLK], kb[NBLK], n = 0, i, big, total, quarter;
    unsigned long off, t0, moved = 0;
    int ok;

    if (!xms_init()) { out("XMS none\n"); return; }
    xcall(0x0800, 0, 0);
    big = x.ax; total = x.dx;
    out("XMS free largest=%u total=%u\n", big, total);
    quarter = total / 4 ? total / 4 : total;
    /* several blocks: each a quarter of the total, or the largest left */
    while (n < NBLK) {
        xcall(0x0800, 0, 0);
        big = x.ax;
        if (!big) break;
        kb[n] = big < quarter ? big : quarter;
        if (xcall(0x0900, 0, kb[n]) != 1) { out("XMS alloc %u KB: error %02X\n", kb[n], x.bx & 0xFF); break; }
        h[n++] = x.dx;
    }
    out("XMS blocks=%u", n);
    for (i = 0; i < n; i++) out(" %u", kb[i]);
    out("\n");
    /* fill every block through moves from conventional memory */
    t0 = ticks();
    ok = 1;
    for (i = 0; i < n; i++)
        for (off = 0; off < (unsigned long)kb[i] * 1024; off += sizeof(buf)) {
            fill(i, off, sizeof(buf));
            if (xmove(sizeof(buf), 0, far_addr(buf), h[i], off) != 1) ok = 0;
            moved += sizeof(buf);
        }
    rate("xms-fill", moved / 1024, t0);
    check(ok, "XMS fill");
    /* block to block: 4097 bytes (odd) from block 0 at 100h to block 1 at
     * 3 (odd); inside block 0: 8000h bytes from 2 down to 0 (overlapping,
     * source above destination) */
    if (n >= 2 && kb[0] >= 64 && kb[1] >= 64) {
        xmove(4097, h[0], 0x100, h[1], 3);
        out("XMS b2b ax=%u bl=%02X\n", x.ax, x.bx & 0xFF);
        /* block 1 from 0 to 4100: 0-2 as before, 3-4099 block 0's 100h-1100h */
        ok = xmove(4100, h[1], 0, 0, far_addr(buf)) == 1;
        for (i = 0; i < 4100 && ok; i++) {
            unsigned char e;
            if (i >= 3 && i < 3 + 4097) { unsigned long o = 0x100 + i - 3; e = (unsigned char)(pat(0, o & ~1UL) >> ((o & 1) * 8)); }
            else e = (unsigned char)(pat(1, i & ~1U) >> ((i & 1) * 8));
            if (buf[i] != e) ok = 0;
        }
        check(ok, "XMS block-to-block odd move");
        out("XMS overlap-down ax=%u\n", xmove(0x8000, h[0], 2, h[0], 0));
        ok = 1;
        for (off = 0; off < 0x8000 && ok; off += sizeof(buf)) {
            unsigned j;
            xmove(sizeof(buf), h[0], off, 0, far_addr(buf));
            for (j = 0; j < sizeof(buf); j += 2)
                if (*(unsigned *)(buf + j) != pat(0, off + j + 2)) { ok = 0; break; }
        }
        check(ok, "XMS overlapping move down");
        /* the other way (destination above source): what the driver does */
        out("XMS overlap-up ax=%u\n", xmove(0x1000, h[0], 0x9000, h[0], 0x9002));
        xmove(16, h[0], 0x9000, 0, far_addr(buf2));
        out("XMS overlap-up first words %04X %04X %04X %04X\n",
            ((unsigned *)buf2)[0], ((unsigned *)buf2)[1], ((unsigned *)buf2)[2], ((unsigned *)buf2)[3]);
    }
    /* read everything back (block 1's first 4100 and block 0's first 9004h
     * bytes changed above) */
    t0 = ticks();
    moved = 0;
    ok = 1;
    for (i = 0; i < n && ok; i++)
        for (off = 0; off < (unsigned long)kb[i] * 1024; off += sizeof(buf)) {
            unsigned long lo = i == 0 ? 0xB000 : i == 1 ? 0x2000 : 0;
            if (xmove(sizeof(buf), h[i], off, 0, far_addr(buf)) != 1) { ok = 0; break; }
            moved += sizeof(buf);
            if (off >= lo && !same(i, off, sizeof(buf))) { out("XMS block %u differs at %lX\n", i, off); ok = 0; break; }
        }
    rate("xms-read", moved / 1024, t0);
    check(ok, "XMS read back");
    /* free and take one block of the total again */
    for (i = 0; i < n; i++) xcall(0x0A00, 0, h[i]);
    xcall(0x0800, 0, 0);
    out("XMS after free largest=%u total=%u\n", x.ax, x.dx);
    if (x.ax && xcall(0x0900, 0, x.ax) == 1) {
        unsigned hh = x.dx;
        unsigned long last;
        xcall(0x0E00, 0, hh);
        last = (unsigned long)x.dx * 1024 - sizeof(buf);
        fill(7, last, sizeof(buf));
        xmove(sizeof(buf), 0, far_addr(buf), hh, last);
        memset(buf, 0, sizeof(buf));
        xmove(sizeof(buf), hh, last, 0, far_addr(buf));
        check(same(7, last, sizeof(buf)), "XMS reallocated block's end");
        xcall(0x0A00, 0, hh);
    } else
        out("XMS realloc error %02X\n", x.bx & 0xFF);
    /* the HMA: only when A20 is off at first (with it on, as in DOSBox-X,
     * DOS itself is in the HMA: a global user, whose code this would
     * overwrite) */
    xcall(0x0700, 0, 0);
    if (x.ax) { out("XMS A20 on at first: the HMA test left out\n"); return; }
    xcall(0x0200, 0, 0);
    /* (with boxes started together, which one finds the HMA in use
     * depends on which VM was made first: not compared) */
    out("%sXMS 02 ax=%u bl=%02X\n", conc ? "INFO " : "", x.ax, x.bx & 0xFF);
    xcall(0x0100, 0, 0xFFFF);
    out("XMS 01 ax=%u bl=%02X\n", x.ax, x.bx & 0xFF);
    if (x.ax == 1) {
        /* 8 KB at its end (FFFF:E010-FFFF:FFFF), saved first and put
         * back after: what is in the HMA (DOSBox keeps data of its own
         * there: the HMA was in use) stays as it was; no DOS call while
         * it is changed */
        static unsigned char keep[0x1FF0];
        unsigned char __far *hma = MK_FP(0xFFFF, 0xE010);
        unsigned char __far *low = MK_FP(0x0E00, 0);    /* the same address with A20 off */
        unsigned k, a20, q, wraps, differ;
        int okw;
        xcall(0x0500, 0, 0);
        a20 = x.ax;
        _fmemcpy(keep, hma, sizeof(keep));
        for (k = 0; k < sizeof(keep); k++) hma[k] = (unsigned char)(k * 7 + seed);
        okw = 1;
        for (k = 0; k < sizeof(keep); k++) if (hma[k] != (unsigned char)(k * 7 + seed)) { okw = 0; break; }
        differ = low[0] != hma[0] || low[1] != hma[1] || low[2] != hma[2];
        xcall(0x0600, 0, 0);
        q = xcall(0x0700, 0, 0);
        wraps = low[0] == hma[0] && low[1] == hma[1] && low[5] == hma[5];
        xcall(0x0500, 0, 0);
        _fmemcpy(hma, keep, sizeof(keep));
        xcall(0x0600, 0, 0);
        out("XMS 05 ax=%u\n", a20);
        check(okw, "XMS HMA written and read");
        check(differ, "XMS HMA is not the first 64 KB");
        out("XMS after 06: A20 %u, wraps=%u\n", q, wraps);
        out("XMS 02 ax=%u\n", xcall(0x0200, 0, 0));
    }
}

/* ---------------------------------------------------------------- XMS 3.0 */
static void xms3(void)
{
    struct xr32 y;
    unsigned h = 0;
    if (!xms_init()) { out("XMS none\n"); return; }
    xcall(0x0000, 0, 0);
    out("XMS3 00 ax=%04X bx=%04X dx=%04X\n", x.ax, x.bx, x.dx);
    y.eax = 0x8800; y.ebx = 0; y.ecx = 0; y.edx = 0;
    xms_call32(&y);
    out("XMS3 88 ax=%04X bl=%02X\n", (unsigned)y.eax, (unsigned)y.ebx & 0xFF);
    y.eax = 0x8900; y.ebx = 0; y.edx = 64;
    xms_call32(&y);
    out("XMS3 89 ax=%04X bl=%02X\n", (unsigned)y.eax, (unsigned)y.ebx & 0xFF);
    if ((unsigned)y.eax == 1) h = (unsigned)y.edx;
    y.eax = 0x8E00; y.ebx = 0; y.edx = h;
    xms_call32(&y);
    out("XMS3 8E ax=%04X bl=%02X\n", (unsigned)y.eax, (unsigned)y.ebx & 0xFF);
    y.eax = 0x8F00; y.ebx = 128; y.edx = h;
    xms_call32(&y);
    out("XMS3 8F ax=%04X bl=%02X\n", (unsigned)y.eax, (unsigned)y.ebx & 0xFF);
    if (h) xcall(0x0A00, 0, h);
    xcall(0x1000, 0, 0xFFFF);
    out("XMS3 10 ax=%04X bl=%02X dx=%04X\n", x.ax, x.bx & 0xFF, x.dx);
    xcall(0x1100, 0, 0xD000);
    out("XMS3 11 ax=%04X bl=%02X\n", x.ax, x.bx & 0xFF);
    xcall(0x1200, 0x10, 0xD000);
    out("XMS3 12 ax=%04X bl=%02X\n", x.ax, x.bx & 0xFF);
}

/* ---------------------------------------------------------------- EMS */
static unsigned frame;

static int ems_init(void)
{
    union REGS r;
    struct SREGS s;
    char __far *name;
    r.x.ax = 0x3567;
    segread(&s);
    intdosx(&r, &r, &s);
    name = MK_FP(s.es, 0x0A);
    if (_fmemcmp(name, "EMMXXXX0", 8)) return 0;
    r.h.ah = 0x41;
    int86(0x67, &r, &r);
    if (r.h.ah) return 0;
    frame = r.x.bx;
    return 1;
}

static union REGS er;
static struct SREGS es_;

static unsigned ecall(unsigned ax, unsigned bx, unsigned dx)
{
    er.x.ax = ax; er.x.bx = bx; er.x.dx = dx;
    int86(0x67, &er, &er);
    return er.h.ah;
}

static unsigned ecall_x(unsigned ax, unsigned cx, unsigned dx, void __far *si, void __far *di)
{
    segread(&es_);
    er.x.ax = ax; er.x.cx = cx; er.x.dx = dx;
    er.x.si = FP_OFF(si); es_.ds = FP_SEG(si);
    er.x.di = FP_OFF(di); es_.es = FP_SEG(di);
    int86x(0x67, &er, &er, &es_);
    return er.h.ah;
}

static unsigned __far *page(unsigned p)
{
    return MK_FP(frame + p * 0x400, 0);
}

static void epfill(unsigned p, unsigned b, unsigned long base)
{
    unsigned __far *w = page(p);
    unsigned i;
    for (i = 0; i < 8192; i++) w[i] = pat(b, base + i * 2UL);
}

static int epsame(unsigned p, unsigned b, unsigned long base)
{
    unsigned __far *w = page(p);
    unsigned i;
    for (i = 0; i < 8192; i++) if (w[i] != pat(b, base + i * 2UL)) return 0;
    return 1;
}

#define NH 3

static void ems_workload(void)
{
    unsigned h[NH], np[NH], i, k, free0, total, err;
    unsigned long t0, kb = 0, rnd;
    int ok;
    static unsigned char save[256], map2[256];
    static unsigned mlist[8];
    static unsigned char mreg[18];

    if (!ems_init()) { out("EMS none\n"); return; }
    ecall(0x4200, 0, 0);
    free0 = er.x.bx; total = er.x.dx;
    out("EMS frame=%04X free=%u total=%u\n", frame, free0, total);
    /* three handles sharing what is free (up to 48 pages) */
    k = free0 < 48 ? free0 : 48;
    for (i = 0; i < NH; i++) {
        np[i] = k / NH + (i < k % NH);
        if (!np[i]) { h[i] = 0xFFFF; continue; }
        err = ecall(0x4300, np[i], 0);
        if (err) { out("EMS 43 %u pages: %02X\n", np[i], err); h[i] = 0xFFFF; np[i] = 0; continue; }
        h[i] = er.x.dx;
    }
    out("EMS handles pages %u %u %u\n", np[0], np[1], np[2]);
    ecall(0x4200, 0, 0);
    out("EMS free now %u\n", er.x.bx);
    /* fill each page through physical page (logical % 4) */
    t0 = ticks();
    ok = 1;
    for (i = 0; i < NH; i++)
        for (k = 0; k < np[i]; k++) {
            if (ecall(0x4400 | (k & 3), k, h[i])) ok = 0;
            epfill(k & 3, i, (unsigned long)k << 14);
            kb += 16;
        }
    rate("ems-fill", kb, t0);
    check(ok, "EMS map and fill");
    /* every page again, in a pseudo-random order, each through a
     * physical page of its own choosing */
    t0 = ticks();
    kb = 0;
    rnd = seed | 1;
    ok = 1;
    for (k = 0; k < np[0] + np[1] + np[2]; k++) {
        unsigned hi, pg, ph;
        rnd = rnd * 1103515245UL + 12345;
        hi = (unsigned)(rnd >> 16) % NH;
        if (!np[hi]) continue;
        pg = (unsigned)(rnd >> 8) % np[hi];
        ph = (unsigned)(rnd >> 24) & 3;
        if (ecall(0x4400 | ph, pg, h[hi]) || !epsame(ph, hi, (unsigned long)pg << 14)) {
            out("EMS handle %u page %u via %u differs\n", hi, pg, ph);
            ok = 0;
            break;
        }
        kb += 16;
    }
    rate("ems-read", kb, t0);
    check(ok, "EMS random remap");
    if (np[0] < 4 || np[1] < 4) { out("EMS too few pages for the rest\n"); goto out; }
    /* save the map (47h), map handle 1 everywhere, restore (48h) */
    for (k = 0; k < 4; k++) ecall(0x4400 | k, k, h[0]);
    out("EMS 47 ah=%02X\n", ecall(0x4700, 0, h[0]));
    for (k = 0; k < 4; k++) ecall(0x4400 | k, k, h[1]);
    out("EMS 48 ah=%02X\n", ecall(0x4800, 0, h[0]));
    ok = 1;
    for (k = 0; k < 4; k++) if (!epsame(k, 0, (unsigned long)k << 14)) ok = 0;
    check(ok, "EMS 47h/48h restore");
    /* the same with 4E00h/4E01h */
    ecall(0x4E03, 0, 0);
    out("EMS 4E03 al=%u\n", er.h.al);
    if (er.h.al) {
        ecall_x(0x4E00, 0, 0, save, save);
        for (k = 0; k < 4; k++) ecall(0x4400 | k, 3 - k, h[1]);
        out("EMS 4E01 ah=%02X\n", ecall_x(0x4E01, 0, 0, save, map2));
        ok = 1;
        for (k = 0; k < 4; k++) if (!epsame(k, 0, (unsigned long)k << 14)) ok = 0;
        check(ok, "EMS 4E00h/4E01h restore");
    }
    /* 50h: handle 1's pages 3, 2, 1, 0 into physical 0-3 at once */
    for (k = 0; k < 4; k++) { mlist[k * 2] = 3 - k; mlist[k * 2 + 1] = k; }
    out("EMS 50 ah=%02X\n", ecall_x(0x5000, 4, h[1], mlist, mlist));
    ok = 1;
    for (k = 0; k < 4; k++) if (!epsame(k, 1, (unsigned long)(3 - k) << 14)) ok = 0;
    check(ok, "EMS 50h map several");
    /* 57h: conventional memory to handle 2's page 0 at 10h, and back */
    if (np[2]) {
        fill(5, 0, 4096);
        memset(mreg, 0, sizeof(mreg));
        *(unsigned long *)mreg = 4096;
        mreg[4] = 0; *(unsigned *)(mreg + 7) = FP_OFF(buf); *(unsigned *)(mreg + 9) = FP_SEG(buf);
        mreg[11] = 1; *(unsigned *)(mreg + 12) = h[2]; *(unsigned *)(mreg + 14) = 0x10; *(unsigned *)(mreg + 16) = 0;
        out("EMS 5700 to ah=%02X\n", ecall_x(0x5700, 0, 0, mreg, mreg));
        memset(buf, 0, 4096);
        mreg[4] = 1; *(unsigned *)(mreg + 5) = h[2]; *(unsigned *)(mreg + 7) = 0x10; *(unsigned *)(mreg + 9) = 0;
        mreg[11] = 0; *(unsigned *)(mreg + 12) = 0; *(unsigned *)(mreg + 14) = FP_OFF(buf); *(unsigned *)(mreg + 16) = FP_SEG(buf);
        out("EMS 5700 from ah=%02X\n", ecall_x(0x5700, 0, 0, mreg, mreg));
        check(same(5, 0, 4096), "EMS 57h move to a page and back");
    }
    /* 57h exchange: handle 0's page 2 with handle 1's page 2, all 16 KB */
    memset(mreg, 0, sizeof(mreg));
    *(unsigned long *)mreg = 16384;
    mreg[4] = 1; *(unsigned *)(mreg + 5) = h[0]; *(unsigned *)(mreg + 9) = 2;
    mreg[11] = 1; *(unsigned *)(mreg + 12) = h[1]; *(unsigned *)(mreg + 16) = 2;
    out("EMS 5701 ah=%02X\n", ecall_x(0x5701, 0, 0, mreg, mreg));
    ecall(0x4400, 2, h[0]);
    ecall(0x4401, 2, h[1]);
    check(epsame(0, 1, 2UL << 14) && epsame(1, 0, 2UL << 14), "EMS 57h exchange");
out:
    for (i = 0; i < NH; i++) if (np[i]) ecall(0x4500, 0, h[i]);
    ecall(0x4200, 0, 0);
    out("EMS free after %u\n", er.x.bx);
}

/* a file beside the program: DOSBox's own DOS keeps one current
 * directory for every VM, which Windows' tasks change meanwhile */
static char dir[80];

static char *path(const char *fmt, char t)
{
    static char p[2][100];
    static int w;
    w ^= 1;
    sprintf(p[w], "%s", dir);
    sprintf(p[w] + strlen(p[w]), fmt, t);
    return p[w];
}

int main(int argc, char **argv)
{
    char mode = argc > 1 ? argv[1][0] : 'X', name[16];
    int rounds = argc > 3 ? atoi(argv[3]) : 3, i;
    if (argc > 2) tag = argv[2][0];
    {
        char *e;
        strncpy(dir, argv[0], sizeof(dir) - 1);
        e = strrchr(dir, '\\');
        if (e) e[1] = 0; else dir[0] = 0;
    }
    logf = fopen(path("ML%c.TMP", tag), "w");
    seed = (unsigned)tag * 257U;
    out("MEMLOAD %c %c\n", mode, tag);
    switch (mode | 0x20) {
    case 'x': xms_workload(); break;
    case 'e': ems_workload(); break;
    case '3': xms3(); break;
    case 't':                           /* T TAG N: N slices released, then DONE_TAG */
        for (i = 0; i < rounds; i++) {
            union REGS r;
            r.x.ax = 0x1680;
            int86(0x2F, &r, &r);
        }
        fclose(fopen(path("DONE_%c", tag), "w"));
        out("INFO made DONE_%c\n", tag);
        break;
    case 'w': {                         /* W TAG OTHER: wait for DONE_OTHER */
        char o = argc > 3 ? argv[3][0] : 'A';
        struct find_t f;
        for (i = 0; i < 3000; i++) {
            union REGS r;
            int h, ff;
            r.x.ax = 0x3D00;
            r.x.dx = FP_OFF(path("DONE_%c", o));
            {
                struct SREGS sr;
                char __far *p = path("DONE_%c", o);
                segread(&sr);
                sr.ds = FP_SEG(p);
                r.x.dx = FP_OFF(p);
                intdosx(&r, &r, &sr);
            }
            h = r.x.cflag ? -(int)r.x.ax : r.x.ax;
            ff = _dos_findfirst(path("DONE_%c", o), 0, &f);
            if (!(i % 20) || h >= 0 || !ff) out("INFO wait %d open=%d find=%d\n", i, h, ff);
            if (h >= 0) {
                r.h.ah = 0x3E; r.x.bx = h; intdos(&r, &r);
                out("PASS saw the other box's file\n");
                break;
            }
            r.x.ax = 0x1680;
            int86(0x2F, &r, &r);
        }
        if (i == 3000) check(0, "saw the other box's file");
        break;
    }
    case 'c':
        conc = 1;
        for (i = 0; i < rounds; i++) {
            seed = (unsigned)tag * 257U + i;
            xms_workload();
            ems_workload();
        }
        break;
    }
    out("MEMLOAD end, %d failure(s)\n", fails);
    if (logf) {
        char done[100];
        fclose(logf);
        strcpy(done, path("ML%c.LOG", tag));
        remove(done);
        rename(path("ML%c.TMP", tag), done);
    }
    return fails ? 1 : 0;
}
