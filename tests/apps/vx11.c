/*
 * vx11.c - VX11.EXE: what Windows 3.1's DPMI host in 386 Enhanced mode
 * does with the calls the WIN386 audit asked about
 * (docs/notes/win386-audit-triage.md). Run as the shell (tests/refrun.sh
 * VX11.EXE /3) under Windows 3.1's WIN386 and under w16's; logs to TK.LOG.
 * The file name picks the run: VX11.EXE the main one (calls a program may
 * make); VX11E, VX11P, VX11L, VX11S, VX11M the ones that might end
 * Windows (a freed selector in ES, a pointer into nothing, past a limit,
 * a huge 0300h copy, a huge 0800h size). Black box: only what the calls
 * return and what they leave in the probe's own memory (ARCHITECTURE.md 3).
 *
 * Main run, sections:
 *   ar    DPMI 0009h with data, code, a call gate, an LDT descriptor and
 *         DPL 0 (the access byte read back with 000Bh, carry, AX);
 *   sd    000Ch with a call gate, DPL 0 data and DPL 0 code;
 *   lim   0008h with limits past 1 MB (low 12 bits clear, set);
 *   map   0800h below 1 MB, at 1 MB, a device address twice, size 0, a
 *         range that wraps;
 *   mem   0501h/0502h 300 times (does the linear address come back?),
 *         0503h smaller and larger (does it move?), 0501h size 0;
 *   dos   0100h BX=0, 0102h to 0, 0101h DX=0 (all "~": under Windows
 *         3.1 KRNL386 has taken every paragraph of DOS memory by then,
 *         under w16's WIN386 not);
 *   vec   0205h and 0203h with a selector that is not allocated;
 *   d48   INT 21h AH=48h from the program (through KERNEL): a selector
 *         or a segment back?
 *   ifl   the flags a real-mode interrupt handler sees on entry: from
 *         0300h (flags IF on, IF off in the structure) and from INT F1h
 *         in protected mode with interrupts on and off.
 */
#include <windows.h>

#define TK_REF "vx3ref.h"
#include "tklog.h"

extern void __cdecl __far vx_int31(WORD __far *regs);
extern void __cdecl __far vx_int21(WORD __far *regs);
extern void __cdecl __far vx_intf1(WORD __far *regs, int cli);
extern WORD __cdecl __far vx_lar(WORD sel);
extern DWORD __cdecl __far vx_lsl(WORD sel);
extern WORD FAR PASCAL WaitEvent(HTASK);
extern BOOL FAR PASCAL InitApp(HINSTANCE);

/* regs: ax bx cx dx si di es flags */
static WORD r[8];
#define CF (r[7] & 1)

static void clr(void)
{
    int i;
    for (i = 0; i < 8; i++) r[i] = 0;
}

static void call31(WORD ax)
{
    r[0] = ax;
    vx_int31(r);
}

static void obs2(const char *a, const char *b, WORD v)
{
    char n[40];
    int i = 0, j = 0;
    while (a[j]) n[i++] = a[j++];
    j = 0;
    while (b[j]) n[i++] = b[j++];
    n[i] = 0;
    obs(n, v);
}

/* the 8 bytes of sel's descriptor (000Bh) */
static BYTE desc[8];
static WORD getdesc(WORD sel)
{
    int i;
    for (i = 0; i < 8; i++) desc[i] = 0;
    clr();
    r[1] = sel;
    r[6] = SELECTOROF((void FAR *)desc);
    r[5] = OFFSETOF((void FAR *)desc);
    call31(0x000B);
    return CF;
}

static WORD newsel(void)
{
    clr();
    r[2] = 1;
    call31(0x0000);
    return CF ? 0 : r[0];
}

static void freesel(WORD sel)
{
    clr();
    r[1] = sel;
    call31(0x0001);
}

/* a data selector, base 0, limit FFFFh, DPL 3 */
static void plain(WORD sel)
{
    clr(); r[1] = sel; r[2] = 0; r[3] = 0; call31(0x0007);
    clr(); r[1] = sel; r[2] = 0; r[3] = 0xFFFF; call31(0x0008);
    clr(); r[1] = sel; r[2] = 0x00F2; call31(0x0009);
}

static void ar(WORD sel, const char *name, WORD cx)
{
    plain(sel);
    clr();
    r[1] = sel;
    r[2] = cx;
    call31(0x0009);
    obs2("ar.cf.", name, CF);
    if (CF) obs2("ar.ax.", name, r[0]);
    getdesc(sel);
    obs2("ar.acc.", name, desc[5]);
    obs2("ar.lar.", name, vx_lar(sel));
}

static void sd(WORD sel, const char *name, BYTE acc, WORD target)
{
    static BYTE d[8];
    plain(sel);
    d[0] = 0x34; d[1] = 0x12;
    d[2] = (BYTE)target; d[3] = (BYTE)(target >> 8);
    d[4] = 0; d[5] = acc; d[6] = 0; d[7] = 0;
    clr();
    r[1] = sel;
    r[6] = SELECTOROF((void FAR *)d);
    r[5] = OFFSETOF((void FAR *)d);
    call31(0x000C);
    obs2("sd.cf.", name, CF);
    if (CF) obs2("sd.ax.", name, r[0]);
    getdesc(sel);
    obs2("sd.acc.", name, desc[5]);
    obs2("sd.lar.", name, vx_lar(sel));
}

/* 0008h: a limit past 1 MB whose low 12 bits are not all set */
static void lim(WORD sel, const char *name, DWORD l)
{
    DWORD got;
    plain(sel);
    clr();
    r[1] = sel; r[2] = HIWORD(l); r[3] = LOWORD(l);
    call31(0x0008);
    obs2("lim.cf.", name, CF);
    if (CF) obs2("lim.ax.", name, r[0]);
    got = vx_lsl(sel);
    obs2("lim.hi.", name, HIWORD(got));
    obs2("lim.lo.", name, LOWORD(got));
}

static void sec_ar(void)
{
    WORD s = newsel();
    out("ar\r\n");
    if (!s) { out("no selector\r\n"); return; }
    ar(s, "data3", 0x00F2);
    ar(s, "code3", 0x00FA);
    ar(s, "np3", 0x0072);
    ar(s, "data0", 0x0092);
    ar(s, "code0", 0x009A);
    ar(s, "data1", 0x00B2);
    ar(s, "gate", 0x00EC);
    ar(s, "ldt", 0x00E2);
    ar(s, "tss", 0x00E9);
    ar(s, "big", 0x40F2);
    ar(s, "gran", 0x80F2);
    sd(s, "gate", 0xEC, 0x0008);
    sd(s, "data0", 0x92, 0);
    sd(s, "code0", 0x9A, 0);
    sd(s, "data3", 0xF2, 0);
    lim(s, "1m", 0x00100000UL);
    lim(s, "1mfff", 0x00100FFFUL);
    lim(s, "ffff", 0x0000FFFFUL);
    plain(s);
    freesel(s);
}

static void map(const char *name, DWORD phys, DWORD size)
{
    clr();
    r[1] = HIWORD(phys); r[2] = LOWORD(phys);
    r[4] = HIWORD(size); r[5] = LOWORD(size);
    call31(0x0800);
    obs2("map.cf.", name, CF);
    if (!CF) {
        DWORD lin = MAKELONG(r[2], r[1]);
        obs2("map.same.", name, lin == phys);
        out("OBS ~map.lin."); out(name); out("="); outhex(r[1]); outhex(r[2]); out("\r\n");
    } else
        obs2("map.ax.", name, r[0]);
}

static void sec_map(void)
{
    DWORD a, b;
    out("map\r\n");
    map("a0000", 0xA0000UL, 0x10000UL);
    map("zero", 0, 0x1000);
    map("b8000", 0xB8000UL, 0x1000);
    map("1mb", 0x100000UL, 0x1000);
    map("dev", 0xFEC00000UL, 0x1000);
    a = MAKELONG(r[2], r[1]);
    map("dev2", 0xFEC00000UL, 0x1000);
    b = MAKELONG(r[2], r[1]);
    obs("map.dev.again", a == b);
    map("size0", 0xFEB00000UL, 0);
    map("wrap", 0xFFFFF000UL, 0x2000);
    map("odd", 0xFEA00010UL, 0x10);
}

static void sec_mem(void)
{
    DWORD first = 0, lin, h;
    int i, ok = 1, same = 0;
    out("mem\r\n");
    for (i = 0; i < 300; i++) {
        clr(); r[1] = 1; r[2] = 0;          /* 64 KB */
        call31(0x0501);
        if (CF) { ok = 0; break; }
        lin = MAKELONG(r[2], r[1]);
        h = MAKELONG(r[5], r[4]);
        if (!i) first = lin;
        else if (lin == first) same++;
        clr(); r[4] = HIWORD(h); r[5] = LOWORD(h);
        call31(0x0502);
        if (CF) { ok = 0; break; }
    }
    obs("mem.loop.ok", ok);
    obs("mem.loop.same", same == 299 ? 2 : same ? 1 : 0);
    clr(); r[1] = 1; r[2] = 0;
    call31(0x0501);
    if (CF) { obs("mem.a.cf", 1); return; }
    lin = MAKELONG(r[2], r[1]);
    h = MAKELONG(r[5], r[4]);
    clr(); r[1] = 0; r[2] = 0x8000; r[4] = HIWORD(h); r[5] = LOWORD(h);
    call31(0x0503);
    obs("mem.shrink.cf", CF);
    if (!CF) {
        obs("mem.shrink.same", MAKELONG(r[2], r[1]) == lin);
        obs("mem.shrink.handle", MAKELONG(r[5], r[4]) == h);
        lin = MAKELONG(r[2], r[1]);
        h = MAKELONG(r[5], r[4]);
    }
    clr(); r[1] = 2; r[2] = 0; r[4] = HIWORD(h); r[5] = LOWORD(h);
    call31(0x0503);
    obs("mem.grow.cf", CF);
    if (!CF) {
        obs("mem.grow.same", MAKELONG(r[2], r[1]) == lin);
        obs("mem.grow.handle", MAKELONG(r[5], r[4]) == h);
        h = MAKELONG(r[5], r[4]);
    }
    clr(); r[4] = HIWORD(h); r[5] = LOWORD(h);
    call31(0x0502);
    obs("mem.free.cf", CF);
    clr(); r[1] = 0; r[2] = 0;
    call31(0x0501);
    obs("mem.size0.cf", CF);
    if (!CF) {
        h = MAKELONG(r[5], r[4]);
        clr(); r[4] = HIWORD(h); r[5] = LOWORD(h);
        call31(0x0502);
    } else
        obs("mem.size0.ax", r[0]);
    clr(); r[4] = 0x1234; r[5] = 0x5678;
    call31(0x0502);
    obs("mem.badfree.cf", CF);
}

static void sec_dos(void)
{
    WORD sel;
    DWORD lim;
    out("dos\r\n");
    clr(); r[1] = 0;
    call31(0x0100);
    obs("~dos.0.cf", CF);
    if (CF) obs("~dos.0.ax", r[0]);
    clr(); r[1] = 1;
    call31(0x0100);
    obs("~dos.1.cf", CF);
    if (CF) { obs("~dos.1.ax", r[0]); out("OBS ~dos.1.bx="); outhex(r[1]); out("\r\n"); }
    else { sel = r[3]; clr(); r[3] = sel; call31(0x0101); }
    clr(); r[1] = 0;
    call31(0x0100);
    if (!CF) {
        sel = r[3];
        lim = vx_lsl(sel);
        obs("~dos.0.limhi", HIWORD(lim));
        obs("~dos.0.limlo", LOWORD(lim));
        clr(); r[3] = sel; call31(0x0101);
        obs("~dos.0.free.cf", CF);
    }
    clr(); r[1] = 0x10;
    call31(0x0100);
    if (!CF) {
        sel = r[3];
        clr(); r[1] = 0; r[3] = sel;
        call31(0x0102);
        obs("~dos.resize0.cf", CF);
        lim = vx_lsl(sel);
        obs("~dos.resize0.limhi", HIWORD(lim));
        obs("~dos.resize0.limlo", LOWORD(lim));
        clr(); r[3] = sel; call31(0x0101);
        obs("~dos.resize0.free.cf", CF);
    }
    clr(); r[3] = 0;
    call31(0x0101);
    obs("dos.free0.cf", CF);
    if (CF) obs("dos.free0.ax", r[0]);
}

static void sec_vec(void)
{
    WORD s = newsel(), oc, od;
    out("vec\r\n");
    if (!s) return;
    freesel(s);
    clr(); r[1] = 0x66; call31(0x0204);
    oc = r[2]; od = r[3];
    clr(); r[1] = 0x66; r[2] = s; r[3] = 0x10; call31(0x0205);
    obs("vec.205.cf", CF);
    clr(); r[1] = 0x66; call31(0x0204);
    obs("vec.205.kept", r[2] == s);
    clr(); r[1] = 0x66; r[2] = oc; r[3] = od; call31(0x0205);
    clr(); r[1] = 0x03; call31(0x0202);
    oc = r[2]; od = r[3];
    clr(); r[1] = 0x03; r[2] = s; r[3] = 0x10; call31(0x0203);
    obs("vec.203.cf", CF);
    clr(); r[1] = 0x03; call31(0x0202);
    obs("vec.203.kept", r[2] == s);
    clr(); r[1] = 0x03; r[2] = oc; r[3] = od; call31(0x0203);
    clr(); r[1] = 0x20; call31(0x0202);
    obs("vec.202.20.cf", CF);
    clr(); r[1] = 0x20; r[2] = oc; r[3] = od; call31(0x0203);
    obs("vec.203.20.cf", CF);
}

/* the real-mode call structure (DPMI 0.9) */
static struct {
    DWORD edi, esi, ebp, res, ebx, edx, ecx, eax;
    WORD flags, es, ds, fs, gs, ip, cs, sp, ss;
} rm;

static void d48(const char *name, WORD paras)
{
    WORD a;
    clr(); r[0] = 0x4800; r[1] = paras;
    vx_int21(r);
    obs2("d48.cf.", name, CF);
    if (CF) { obs2("d48.ax.", name, r[0]); out("OBS ~d48.bx="); outhex(r[1]); out("\r\n"); return; }
    a = r[0];
    obs2("d48.lar.", name, vx_lar(a) != 0xFFFF);
    if (vx_lar(a) != 0xFFFF) {
        DWORD l = vx_lsl(a);
        obs2("d48.limhi.", name, HIWORD(l));
        obs2("d48.limlo.", name, LOWORD(l));
        obs2("d48.next.", name, vx_lar(a + 8) != 0xFFFF);
        if (vx_lar(a + 8) != 0xFFFF) {
            l = vx_lsl(a + 8);
            obs2("d48.nlimhi.", name, HIWORD(l));
            obs2("d48.nlimlo.", name, LOWORD(l));
        }
        if (paras == 0x100) {
            /* 4Ah: larger, smaller */
            clr(); r[0] = 0x4A00; r[1] = 0x200; r[6] = a;
            vx_int21(r);
            obs("d4a.grow.cf", CF);
            if (CF) obs("d4a.grow.ax", r[0]);
            obs("d4a.grow.es", r[6] == a);
            l = vx_lsl(a);
            obs("d4a.grow.limlo", LOWORD(l));
            clr(); r[0] = 0x4A00; r[1] = 0x10; r[6] = a;
            vx_int21(r);
            obs("d4a.shrink.cf", CF);
            l = vx_lsl(a);
            obs("d4a.shrink.limlo", LOWORD(l));
            clr(); r[0] = 0x4A00; r[1] = 0xFFFF; r[6] = a;
            vx_int21(r);
            obs("d4a.huge.cf", CF);
            if (CF) { obs("d4a.huge.ax", r[0]); out("OBS ~d4a.huge.bx="); outhex(r[1]); out("\r\n"); }
        }
    }
    /* given back as the segment it is, in real mode */
    {
        int i;
        BYTE FAR *p = (BYTE FAR *)&rm;
        for (i = 0; i < sizeof(rm); i++) p[i] = 0;
    }
    if (vx_lar(a) == 0xFFFF) {
        rm.eax = 0x4900; rm.es = a;
        clr(); r[1] = 0x21; r[6] = SELECTOROF((void FAR *)&rm); r[5] = OFFSETOF((void FAR *)&rm);
        call31(0x0300);
        obs2("d48.free.cf.", name, (WORD)(rm.flags & 1));
    } else {
        clr(); r[0] = 0x4900; r[6] = a;
        vx_int21(r);
        /* (after the failed huge 4Ah of the 100h block Windows 3.1's 49h
         * failed too: not compared) */
        obs2(paras == 0x100 ? "~d48.free.cf." : "d48.free.cf.", name, CF);
        obs2(paras == 0x100 ? "~d48.free.lar." : "d48.free.lar.", name, vx_lar(a) != 0xFFFF);
    }
}

static void sec_d48(void)
{
    out("d48\r\n");
    d48("1", 1);
    d48("100", 0x100);
    d48("2000", 0x2000);
    d48("fffe", 0xFFFE);
    d48("big", 0xFFFF);
}

static void sec_ifl(void)
{
    WORD seg, sel, oc, od;
    BYTE FAR *p;
    static const BYTE code[] = { 0x9C, 0x2E, 0x8F, 0x06, 0x10, 0x00, 0xCF };
    int i;
    out("ifl\r\n");
    {
        DWORD d = GlobalDosAlloc(64);
        if (!d) { out("no DOS memory\r\n"); return; }
        seg = HIWORD(d); sel = LOWORD(d);
    }
    p = (BYTE FAR *)MAKELP(sel, 0);
    for (i = 0; i < sizeof(code); i++) p[i] = code[i];
    clr(); r[1] = 0xF1; call31(0x0204);
    out("OBS ~ifl.pmvec="); outhex(r[2]); out(":"); outhex(r[3]); out("\r\n");
    clr(); r[1] = 0xF1; call31(0x0200);
    oc = r[2]; od = r[3];
    clr(); r[1] = 0xF1; r[2] = seg; r[3] = 0; call31(0x0201);
    for (i = 0; i < 2; i++) {
        BYTE FAR *q = (BYTE FAR *)&rm;
        int k;
        for (k = 0; k < sizeof(rm); k++) q[k] = 0;
        rm.flags = i ? 0x0002 : 0x0202;
        *(WORD FAR *)(p + 0x10) = 0xFFFF;
        clr(); r[1] = 0xF1; r[6] = SELECTOROF((void FAR *)&rm); r[5] = OFFSETOF((void FAR *)&rm);
        call31(0x0300);
        obs(i ? "ifl.300.off" : "ifl.300.on", *(WORD FAR *)(p + 0x10) & 0x0300);
        obs(i ? "ifl.300.off.back" : "ifl.300.on.back", rm.flags & 0x0300);
    }
    for (i = 0; i < 2; i++) {
        *(WORD FAR *)(p + 0x10) = 0xFFFF;
        clr();
        vx_intf1(r, i);
        obs(i ? "ifl.int.off" : "ifl.int.on", *(WORD FAR *)(p + 0x10) & 0x0300);
    }
    clr(); r[1] = 0xF1; r[2] = oc; r[3] = od; call31(0x0201);
    GlobalDosFree(sel);
}

/* ---- the runs that might end Windows ---- */

/* E: a selector freed while it is in ES (0001h), a DOS block's selector
 * freed while in ES (0101h): what ES holds after */
static void run_e(void)
{
    WORD s = newsel();
    out("e\r\n");
    if (!s) return;
    plain(s);
    clr(); r[1] = s; r[6] = s;
    call31(0x0001);
    obs("e.1.cf", CF);
    obs("e.1.es", r[6] == 0 ? 0 : r[6] == s ? 1 : 2);
    out("e.1 survived\r\n");
    clr(); r[1] = 1;
    call31(0x0100);
    if (CF) return;
    s = r[3];
    clr(); r[3] = s; r[6] = s;
    call31(0x0101);
    /* (under Windows 3.1 0100h fails: KRNL386 has every paragraph) */
    obs("~e.101.cf", CF);
    obs("~e.101.es", r[6] == 0 ? 0 : r[6] == s ? 1 : 2);
    out("e.101 survived\r\n");
}

/* P: 000Bh writing its 8 bytes through ES:DI into linear memory nothing
 * is mapped at */
static void run_p(void)
{
    WORD s = newsel(), t = newsel();
    out("p\r\n");
    if (!s || !t) return;
    plain(s);
    clr(); r[1] = s; r[2] = 0x4000; r[3] = 0; call31(0x0007);   /* 40000000h */
    clr(); r[1] = t; r[6] = s; r[5] = 0x100;
    call31(0x000B);
    obs("p.cf", CF);
    if (CF) obs("p.ax", r[0]);
    out("p survived\r\n");
}

/* L: 000Bh with DI past ES's limit (memory there is the probe's own) */
static void run_l(void)
{
    static BYTE buf[64];
    WORD s = newsel(), t = newsel(), i, n = 0;
    DWORD base;
    out("l\r\n");
    if (!s || !t) return;
    clr(); r[1] = SELECTOROF((void FAR *)buf); call31(0x0006);
    base = MAKELONG(r[3], r[2]) + OFFSETOF((void FAR *)buf);
    plain(s);
    clr(); r[1] = s; r[2] = HIWORD(base); r[3] = LOWORD(base); call31(0x0007);
    clr(); r[1] = s; r[2] = 0; r[3] = 0x000F; call31(0x0008);
    for (i = 0; i < 64; i++) buf[i] = 0xEE;
    clr(); r[1] = t; r[6] = s; r[5] = 0x20;
    call31(0x000B);
    obs("l.cf", CF);
    if (CF) obs("l.ax", r[0]);
    for (i = 0x20; i < 0x28; i++) if (buf[i] != 0xEE) n++;
    obs("l.written", n);
    out("l survived\r\n");
}

/* S: 0300h copying 4000h words from the probe's 8 KB stack */
static void run_s(void)
{
    int i;
    BYTE FAR *q = (BYTE FAR *)&rm;
    out("s\r\n");
    for (i = 0; i < sizeof(rm); i++) q[i] = 0;
    rm.eax = 0x3000;                        /* DOS version: harmless */
    clr(); r[1] = 0x21; r[2] = 0x4000;
    r[6] = SELECTOROF((void FAR *)&rm); r[5] = OFFSETOF((void FAR *)&rm);
    call31(0x0300);
    obs("s.cf", CF);
    if (CF) obs("s.ax", r[0]);
    out("s survived\r\n");
}

/* M: 0800h with a size that wraps the address space */
static void run_m(void)
{
    out("m\r\n");
    map("huge", 0xE0000000UL, 0xFFFFFFFFUL);
    out("m survived\r\n");
}

int __cdecl tmain(void)
{
    char path[128], mode = 0;
    int n;
    HINSTANCE hi = (HINSTANCE)init_regs[5];
    WaitEvent(0);
    InitApp(hi);
    n = GetModuleFileName(hi, path, sizeof path);
    while (n > 0 && path[n - 1] != '\\')
        n--;
    if (path[n + 4] && path[n + 4] != '.') mode = (char)(path[n + 4] | 0x20);
    out("VX11 start ");
    if (mode) { char m[2]; m[0] = mode; m[1] = 0; out(m); }
    out("\r\n");
    switch (mode) {
    case 0:
        sec_ar();
        sec_map();
        sec_mem();
        sec_dos();
        sec_vec();
        sec_d48();
        sec_ifl();
        break;
    case 'e': run_e(); break;
    case 'p': run_p(); break;
    case 'l': run_l(); break;
    case 's': run_s(); break;
    case 'm': run_m(); break;
    }
    out("VX11 done\r\n");
    return 0;
}
