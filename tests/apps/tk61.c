/*
 * tk61.c - what happens to a selector when KERNEL frees it: GlobalFree
 * (fixed, moveable, huge), FreeSelector, GlobalReAlloc, GlobalDiscard,
 * FreeLibrary, a task's end. For each: what LAR, LSL, VERR/VERW and DPMI
 * 000Bh say about the freed selector; FS, GS and ES that held it; which
 * selectors the next allocations get (the reuse order); whether the DPMI
 * host got it back (INT 31h 0000h hands it out again). Also FS, GS and
 * ES after heap calls that free nothing ("live").
 *
 * Pietrek, "Windows Internals" (1993), ch. 2 (Get_Sel, Free_Sel). Probed
 * black-box on Windows 3.1 (tests/tk5ref.sh OUT TK61 for KRNL386 in both
 * modes, with REFRUN_KRNL286=1 for KRNL286); reference values in
 * tk61ref.h and tk61ref286.h (ARCHITECTURE.md 9.2 has the rules).
 *
 * Starts TK61A.EXE (tk61a.c) from its own directory and loads TKDLL.DLL
 * (with TKDLB.DLL) from the Windows directory. Flag files in C:\ (not
 * there in the usual runs):
 *  - TK61DFS.FLG: the "deadfs" steps, a freed selector left in FS across
 *    timer interrupts (with TK61DF1.FLG), INT 21h (TK61DF2.FLG) and INT
 *    31h (TK61DF3.FLG); under Microsoft's DOSX the first two end Windows;
 *  - TK61DIAG.FLG: the DPMI host's free chain after each step of "gfree"
 *    and "reuse" (DIAG lines), and the selectors (OBS ~).
 * w16's DOSXT trace (dosx/trace.inc) shows each step as INT 31h 0400h
 * with SI = 6100h + the step's number.
 *
 * Output and checks: see tklog.h.
 */
#include <windows.h>

#define TK_REF "tk61ref.h"
#define TK_REF286 "tk61ref286.h"
#include "tklog.h"
#include "tk61.h"

extern WORD __cdecl sel_ok(WORD sel);
extern WORD __cdecl sel_ar(WORD sel);
extern DWORD __cdecl sel_lsl(WORD sel);
extern WORD __cdecl sel_rw(WORD sel);
extern WORD __cdecl get_desc(WORD sel, BYTE FAR *buf);
extern WORD __cdecl cur_ds(void);
extern WORD __cdecl get_0040h(void);
extern WORD __cdecl call_regs(void FAR *fn, WORD arg, WORD sel, WORD FAR *out);
extern WORD __cdecl get_fs(void);
extern WORD __cdecl get_gs(void);
extern void __cdecl zero_fsgs(void);
extern void __cdecl set_fsgs(WORD fs, WORD gs);
extern WORD __cdecl dpmi_alloc1(void);
extern void __cdecl dpmi_free1(WORD sel);
extern void __cdecl mark(WORD n);
extern WORD __cdecl deadfs(WORD what, void FAR *fn, WORD h, WORD sel, WORD bios);

UINT WINAPI AllocSelectorArray(UINT);

WORD use386;                    /* tk17s.asm, tk61s.asm */
extern WORD __cdecl vendor_api(void);
extern WORD __cdecl dpmi_version(void);
extern WORD __cdecl host_keeps_fs(void);

/* Everything here depends on the DOS extender too (KRNL286 gives freed
   selectors to it; KRNL386 needs its LDT selector): checked against 3.1
   only on a host that behaves as Windows 3.1's DOSX and WIN386 do (the
   vendor API's LDT selector; in Standard mode FS kept across INT 31h
   0001h: w16's DOSX too); under HDPMI16 (make test), which zeroes FS
   there, plain OBS ~ lines */
static WORD hostok;
#define OBS(n, v) (hostok ? obs((n), (v)) : obs_env((n), (v)))

/* obsn under the same rule */
static void OBSN(const char *prefix, WORD n, WORD v)
{
    char name[64];
    WORD i = 0, d;
    while (*prefix && i < sizeof name - 7)
        name[i++] = *prefix++;
    name[i++] = '(';
    for (d = 0; d < 4; d++)
        name[i++] = "0123456789ABCDEF"[(n >> (12 - 4 * d)) & 15];
    name[i++] = ')';
    name[i] = 0;
    OBS(name, v);
}

static WORD nstep;
#define STEP(s) (out("-- " s "\r\n"), mark(++nstep))

static char nbuf[80];
static const char *nm(const char *a, const char *b)
{
    WORD i = 0;
    while (*a && i < sizeof nbuf - 2)
        nbuf[i++] = *a++;
    nbuf[i++] = ' ';
    while (*b && i < sizeof nbuf - 1)
        nbuf[i++] = *b++;
    nbuf[i] = 0;
    return nbuf;
}

/* the free-list link of a freed selector's descriptor (limit 0..15) */
static WORD link_of(WORD sel)
{
    BYTE d[8];
    if (!get_desc(sel, d))
        return 0xFFFF;
    return d[0] | d[1] << 8;
}

/* what the CPU and DPMI say about a freed selector */
static void freed(const char *p, WORD sel)
{
    BYTE d[8];
    WORD i, ok;
    DWORD lim = sel_lsl(sel);
    for (i = 0; i < 8; i++)
        d[i] = 0;
    OBS(nm(p, "lar ok"), sel_ok(sel));
    OBS(nm(p, "ar"), sel_ar(sel));
    OBS(nm(p, "verr/verw"), sel_rw(sel));
    OBS(nm(p, "lsl lo"), LOWORD(lim));
    OBS(nm(p, "lsl hi"), HIWORD(lim));
    ok = get_desc(sel, d);
    OBS(nm(p, "DPMI 000B ok"), ok);
    OBS(nm(p, "desc[5]"), d[5]);
    OBS(nm(p, "desc[6]"), d[6]);
    obs_env(nm(p, "desc[0..1]"), d[0] | d[1] << 8);
    obs_env(nm(p, "desc[2..3]"), d[2] | d[3] << 8);
    obs_env(nm(p, "desc[4],[7]"), d[4] | d[7] << 8);
    /* the end of the list: FFFFh (KRNL386), 0 (DOSX) */
    OBS(nm(p, "desc[0..1] is an LDT offset"), ok && (!(d[0] & 7) || (d[0] & d[1]) == 0xFF));
}

/* follows the free list from a freed selector through the links of
   descriptors in the freed format (access byte 0); the number of steps
   (at most 0FFFh) and the last link */
static void walk(const char *p, WORD sel)
{
    BYTE d[8];
    WORD n = 0, l = 0;
    while (n < 0x0FFF && get_desc(sel, d) && d[5] == 0) {
        l = d[0] | d[1] << 8;
        n++;
        if (l & 7)
            break;
        sel = l | 7;
    }
    obs_env(nm(p, "walk steps"), n);
    obs_env(nm(p, "walk last link"), l);
}

/* with C:\TK61DIAG.FLG: the DPMI host's free chain (its head from INT
   31h 0000h, given back at once with 0001h, then the links of up to five
   descriptors), to see which selectors KERNEL gave the host and in which
   order (not compared) */
static WORD diag;
static void dchain(const char *p)
{
    BYTE d[8];
    WORD h, i, s;
    if (!diag)
        return;
    h = dpmi_alloc1();
    if (h)
        dpmi_free1(h);
    out("DIAG ");
    out(p);
    out(":");
    for (s = h, i = 0; s && i < 6; i++) {
        out(" ");
        outhex(s);
        if (!get_desc(s, d) || d[5])
            break;
        s = d[0] | d[1] << 8;
        if (s)
            s |= 7;
    }
    out("\r\n");
}
static void dval(const char *p, WORD v)
{
    if (diag)
        obs_env(p, v);
}

static WORD fixed_block(void)
{
    return SELECTOROF(GlobalLock(GlobalAlloc(GMEM_FIXED, 0x100)));
}

/* FS, GS and ES after fn(arg) with all three holding sel */
static void regs_after(const char *p, void FAR *fn, WORD arg, WORD sel)
{
    WORD r[3];
    call_regs(fn, arg, sel, r);
    OBS(nm(p, "FS kept"), r[0] == sel);
    OBS(nm(p, "FS 0"), r[0] == 0);
    OBS(nm(p, "GS kept"), r[1] == sel);
    OBS(nm(p, "GS 0"), r[1] == 0);
    OBS(nm(p, "ES kept"), r[2] == sel);
    OBS(nm(p, "ES 0"), r[2] == 0);
}

static FARPROC kp(const char *name)
{
    return GetProcAddress(GetModuleHandle("KERNEL"), name);
}

/* where sel comes back among the next n single-selector allocations
   (fixed blocks; FFFFh: not among them); the blocks are freed again in
   reverse order, which leaves the free list as it was */
static WORD back_at(WORD sel, WORD n)
{
    WORD got[16], i, at = 0xFFFF;
    for (i = 0; i < n && i < 16; i++) {
        got[i] = fixed_block();
        if (got[i] == sel && at == 0xFFFF)
            at = i;
    }
    while (i--)
        GlobalFree((HGLOBAL)GlobalHandle(got[i]));
    return at;
}

static void gfree(void)
{
    WORD s, h, a, b;
    STEP("gfree");
    s = fixed_block();
    regs_after("gfree fixed", (void FAR *)kp("GlobalFree"), (WORD)GlobalHandle(s), s);
    freed("gfree fixed", s);

    h = (WORD)GlobalAlloc(GMEM_MOVEABLE, 0x100);
    s = SELECTOROF(GlobalLock((HGLOBAL)h));
    GlobalUnlock((HGLOBAL)h);
    regs_after("gfree moveable", (void FAR *)kp("GlobalFree"), h, s);
    freed("gfree moveable", s);

    /* two in a row: the second's link names the first */
    dchain("chain start");
    a = fixed_block();
    dval("chain a", a);
    dchain("after alloc a");
    b = fixed_block();
    dval("chain b", b);
    dchain("after alloc b");
    GlobalFree((HGLOBAL)GlobalHandle(a));
    dchain("after free a");
    GlobalFree((HGLOBAL)GlobalHandle(b));
    dchain("after free b");
    OBS("chain: link(second) = first", link_of(b) == (a & ~7u));
    OBS("chain: link(first) = second", link_of(a) == (b & ~7u));
    walk("chain", b);
    OBS("chain: next alloc = second", fixed_block() == b);
    OBS("chain: then the first", fixed_block() == a);
    GlobalFree((HGLOBAL)GlobalHandle(a));
    GlobalFree((HGLOBAL)GlobalHandle(b));
}

static void reuse(void)
{
    WORD x, y, z, p, q, r, d;
    STEP("reuse");
    x = fixed_block();
    y = fixed_block();
    z = fixed_block();
    GlobalFree((HGLOBAL)GlobalHandle(x));
    GlobalFree((HGLOBAL)GlobalHandle(z));
    GlobalFree((HGLOBAL)GlobalHandle(y));
    dval("x", x); dval("y", y); dval("z", z);
    dchain("after frees");
    p = fixed_block();
    dchain("after p");
    q = fixed_block();
    dchain("after q");
    r = fixed_block();
    dchain("after r");
    dval("p", p); dval("q", q); dval("r", r);
    OBS("reuse: 1st = last freed", p == y);
    OBS("reuse: 2nd = 2nd last freed", q == z);
    OBS("reuse: 3rd = first freed", r == x);
    /* AllocSelector(0) takes from the same list */
    GlobalFree((HGLOBAL)GlobalHandle(r));
    dchain("after free r");
    d = AllocSelector(0);
    dval("AllocSelector(0)", d);
    OBS("reuse: AllocSelector(0) = freed block", d == r);
    FreeSelector(d);
    OBS("reuse: FreeSelector then GlobalAlloc", (r = fixed_block()) == d);
    /* the DPMI host did not get it: INT 31h 0000h gives another */
    GlobalFree((HGLOBAL)GlobalHandle(r));
    d = dpmi_alloc1();
    OBS("reuse: DPMI 0000h gave one", d != 0);
    OBS("reuse: DPMI 0000h = freed", d == r);
    OBS("reuse: freed lar ok after DPMI 0000h", sel_ok(r));
    if (d)
        dpmi_free1(d);
    OBS("reuse: GlobalAlloc after DPMI 0000h/0001h = freed", fixed_block() == r);
    GlobalFree((HGLOBAL)GlobalHandle(p));
    GlobalFree((HGLOBAL)GlobalHandle(q));
    GlobalFree((HGLOBAL)GlobalHandle(r));
    /* GlobalCompact keeps them */
    p = fixed_block();
    GlobalFree((HGLOBAL)GlobalHandle(p));
    GlobalCompact(0xFFFFFFFFUL);
    OBS("reuse: GlobalAlloc after GlobalCompact = freed", fixed_block() == p);
    GlobalFree((HGLOBAL)GlobalHandle(p));
}

static void selfree(void)
{
    WORD s, t, h;
    STEP("selfree");
    s = AllocSelector(0);               /* not loadable: FreeSelector alone */
    FreeSelector(s);
    freed("FreeSelector raw", s);
    h = (WORD)GlobalAlloc(GMEM_FIXED, 0x100);
    t = AllocSelector(SELECTOROF(GlobalLock((HGLOBAL)h)));
    regs_after("FreeSelector copy", (void FAR *)kp("FreeSelector"), t, t);
    freed("FreeSelector copy", t);
    /* calls that free nothing, with FS, GS and ES holding a live block */
    s = SELECTOROF(GlobalLock((HGLOBAL)h));
    regs_after("GlobalSize live", (void FAR *)kp("GlobalSize"), h, s);
    regs_after("GlobalFlags live", (void FAR *)kp("GlobalFlags"), h, s);
    regs_after("GlobalLock live", (void FAR *)kp("GlobalLock"), h, s);
    regs_after("GlobalUnlock live", (void FAR *)kp("GlobalUnlock"), h, s);
    regs_after("GlobalHandle live", (void FAR *)kp("GlobalHandle"), s, s);
    regs_after("LockSegment live", (void FAR *)kp("LockSegment"), s, s);
    regs_after("UnlockSegment live", (void FAR *)kp("UnlockSegment"), s, s);
    regs_after("GlobalFix live", (void FAR *)kp("GlobalFix"), h, s);
    regs_after("GlobalUnfix live", (void FAR *)kp("GlobalUnfix"), h, s);
    regs_after("GlobalPageLock live", (void FAR *)kp("GlobalPageLock"), h, s);
    regs_after("GlobalPageUnlock live", (void FAR *)kp("GlobalPageUnlock"), h, s);
    regs_after("GetSelectorBase live", (void FAR *)kp("GetSelectorBase"), s, s);
    regs_after("IsTask live", (void FAR *)kp("IsTask"), s, s);
    GlobalFree((HGLOBAL)h);
    s = AllocSelectorArray(2);
    FreeSelector(s + 8);
    FreeSelector(s);
    freed("FreeSelector array", s);
    OBS("array: first back at", back_at(s, 4));
    OBS("array: second at", back_at(s + 8, 4));
}

static void hugeblk(void)
{
    WORD t, i, a;
    HGLOBAL h;
    STEP("huge");
    /* a fixed block of three tiles, freed */
    h = GlobalAlloc(GMEM_FIXED, 0x20001UL);
    t = SELECTOROF(GlobalLock(h));
    GlobalFree(h);
    for (i = 0; i < 3; i++)
        OBSN("huge freed lar ok", i, sel_ok(t + 8 * i));
    freed("huge tile 0", t);
    freed("huge tile 2", t + 16);
    OBS("huge: link(tile 1) = tile 0", link_of(t + 8) == (t & ~7u));
    OBS("huge: link(tile 0) = tile 1", link_of(t) == ((t + 8) & ~7u));
    OBS("huge: link(tile 2) = tile 1", link_of(t + 16) == ((t + 8) & ~7u));
    OBS("huge: link(tile 1) = tile 2", link_of(t + 8) == ((t + 16) & ~7u));
    OBS("huge: tile 0 back at", back_at(t, 6));
    OBS("huge: tile 1 back at", back_at(t + 8, 6));
    OBS("huge: tile 2 back at", back_at(t + 16, 6));
    walk("huge", t + 16);
    a = AllocSelectorArray(3);
    obs_env("huge: AllocSelectorArray(3) = tile 0", a == t);
    obs_env("huge: AllocSelectorArray(3) - tile 0", a - t);
    for (i = 0; i < 3; i++)
        FreeSelector(a + 8 * i);

    /* GlobalReAlloc of a moveable three-tile block down to 100h */
    h = GlobalAlloc(GMEM_MOVEABLE, 0x20001UL);
    t = SELECTOROF(GlobalLock(h));
    GlobalUnlock(h);
    h = GlobalReAlloc(h, 0x100, GMEM_MOVEABLE);
    OBS("shrink: same selector", SELECTOROF(GlobalLock(h)) == t);
    GlobalUnlock(h);
    for (i = 0; i < 3; i++)
        OBSN("shrink: tile lar ok", i, sel_ok(t + 8 * i));
    freed("shrink tile 2", t + 16);
    OBS("shrink: tile 1 back at", back_at(t + 8, 6));
    OBS("shrink: tile 2 back at", back_at(t + 16, 6));
    /* and up again past 64K */
    a = SELECTOROF(GlobalLock(h = GlobalReAlloc(h, 0x18000UL, GMEM_MOVEABLE)));
    GlobalUnlock(h);
    OBS("grow: same selector", a == t);
    OBS("grow: old lar ok", sel_ok(t));
    GlobalFree(h);
}

static void discard(void)
{
    HGLOBAL h;
    WORD s, i;
    DWORD base;
    BYTE d[8];
    STEP("discard");
    h = GlobalAlloc(GMEM_MOVEABLE | GMEM_DISCARDABLE, 0x100);
    s = SELECTOROF(GlobalLock(h));
    GlobalUnlock(h);
    base = GetSelectorBase(s);
    GlobalDiscard(h);
    OBS("discarded lar ok", sel_ok(s));
    OBS("discarded ar", sel_ar(s));
    for (i = 0; i < 8; i++)
        d[i] = 0;
    OBS("discarded DPMI 000B ok", get_desc(s, d));
    OBS("discarded desc[5]", d[5]);
    OBS("discarded desc[6]", d[6]);
    obs_env("discarded desc[0..1]", d[0] | d[1] << 8);
    OBS("discarded desc[0..1] = owner", (d[0] | d[1] << 8) == GetCurrentPDB());
    obs_env("discarded desc[2..3]", d[2] | d[3] << 8);
    obs_env("discarded desc[4],[7]", d[4] | d[7] << 8);
    obs_env("discarded: old base lo", LOWORD(base));
    obs_env("discarded: old base hi", HIWORD(base));
    GlobalFree(h);
    freed("discarded then freed", s);
    obs_env("discarded then freed: back at", back_at(s, 4));
}

static void freelib(void)
{
    HINSTANCE l;
    WORD ds, cs;
    STEP("freelib");
    l = LoadLibrary("TKDLL.DLL");
    OBS("LoadLibrary ok", (WORD)l > 32);
    if ((WORD)l <= 32)
        return;
    ds = (WORD)l | 1;
    cs = SELECTOROF(GetProcAddress(l, "DLL_ADD"));
    FreeLibrary(l);
    OBS("freelib: data lar ok", sel_ok(ds));
    OBS("freelib: code lar ok", sel_ok(cs));
    {
        BYTE d[8];
        d[5] = 0;
        get_desc(ds, d);
        OBS("freelib: data desc[5]", d[5]);
    }
    obs_env("freelib: data back at", back_at(ds, 12));
    obs_env("freelib: code back at", back_at(cs, 12));
}

static char helper[128];
static tk61sh sh;

static void hex4(char *p, WORD v)
{
    WORD i;
    for (i = 0; i < 4; i++)
        p[i] = "0123456789ABCDEF"[(v >> (12 - 4 * i)) & 15];
}

static void child(void)
{
    WORD n, i, fs, gs, t, own;
    char cmd[160];
    STEP("task");
    /* KRNL286 (Standard mode): Windows 3.1 ended there, "Standard Mode:
       Fault in MS-DOS Extender" (in DOSX at 005B:0B58, the error code a
       selector): KRNL286 leaves the helper's freed blocks in FS and GS,
       and Microsoft's DOSX faults reloading them at the next timer tick
       ("deadfs") */
    if (k286()) {
        out("task: not under KRNL286\r\n");
        return;
    }
    n = GetModuleFileName((HINSTANCE)(entry_regs[E_DS] & ~1u), helper, sizeof helper - 12);
    for (i = n; i && helper[i - 1] != '\\'; i--)
        ;
    lstrcpy(helper + i, "TK61A.EXE");
    lstrcpy(cmd, helper);
    n = lstrlen(cmd);
    cmd[n++] = ' ';
    hex4(cmd + n, cur_ds());
    cmd[n + 4] = ' ';
    hex4(cmd + n + 5, (WORD)(void NEAR *)&sh);
    cmd[n + 9] = 0;
    t = WinExec(cmd, SW_SHOWNORMAL);
    OBS("task: WinExec ok", t > 32);
    if (t <= 32)
        return;
    /* our own FS and GS: a live block of ours */
    own = fixed_block();
    set_fsgs(own, own);
    for (i = 0; i < 2000 && !sh.done; i++)
        Yield();
    for (i = 0; i < 2000 && IsTask((HTASK)sh.task); i++)
        Yield();
    fs = get_fs();
    gs = get_gs();
    zero_fsgs();
    OBS("task: ran", sh.done);
    OBS("task: ended", !IsTask((HTASK)sh.task));
    OBS("task: FS = its fixed block", use386 && fs == sh.fixed);
    OBS("task: FS 0", fs == 0);
    OBS("task: FS ours", use386 && fs == own);
    OBS("task: GS = its moveable block", use386 && gs == sh.moveable);
    OBS("task: GS 0", gs == 0);
    OBS("task: GS ours", use386 && gs == own);
    GlobalFree((HGLOBAL)GlobalHandle(own));
    obs_env("task: FS", fs);
    OBS("task: fixed lar ok", sel_ok(sh.fixed));
    OBS("task: moveable lar ok", sel_ok(sh.moveable));
    OBS("task: DS lar ok", sel_ok(sh.ds));
    OBS("task: CS lar ok", sel_ok(sh.cs));
    freed("task fixed", sh.fixed);
    obs_env("task: fixed back at", back_at(sh.fixed, 16));
    obs_env("task: moveable back at", back_at(sh.moveable, 16));
}

/* with C:\TK61DFS.FLG: a dead selector in FS across interrupts, INT 21h
   and INT 31h */
static int flag(const char *name)
{
    OFSTRUCT of;
    return OpenFile(name, &of, OF_EXIST) != HFILE_ERROR;
}

/* a copy of a block's selector (AllocSelector), freed by FreeSelector,
   which leaves FS as it was on both kernels */
static void dead1(WORD what, const char *name)
{
    WORD s = fixed_block(), c = AllocSelector(s);
    out(name);
    out("...\r\n");
    OBS(name, deadfs(what, (void FAR *)kp("FreeSelector"), c, c, get_0040h()) == c);
    GlobalFree((HGLOBAL)GlobalHandle(s));
}

static void deadfs_steps(void)
{
    WORD s;
    if (!use386 || !flag("C:\\TK61DFS.FLG"))
        return;
    STEP("deadfs");
    s = fixed_block();
    out("deadfs: ticks, live...\r\n");
    OBS("deadfs: ticks live, FS kept",
        deadfs(0, (void FAR *)kp("GetSelectorBase"), s, s, get_0040h()) == s);
    GlobalFree((HGLOBAL)GlobalHandle(s));
    if (flag("C:\\TK61DF1.FLG"))
        dead1(0, "deadfs: ticks, FS kept");
    if (flag("C:\\TK61DF2.FLG"))
        dead1(1, "deadfs: INT 21h, FS kept");
    if (flag("C:\\TK61DF3.FLG"))
        dead1(2, "deadfs: INT 31h, FS kept");
}

int __cdecl tmain(void)
{
    out("TK61 start\r\n");
    use386 = (GetWinFlags() & (WF_CPU386 | WF_CPU486)) != 0;
    obs("386", use386);
    hostok = vendor_api() && use386 &&
             ((GetWinFlags() & WF_ENHANCED) || host_keeps_fs());
    obs_env("host as 3.1's", hostok);
    obs_env("DPMI version", dpmi_version());
    {
        OFSTRUCT of;
        diag = OpenFile("C:\\TK61DIAG.FLG", &of, OF_EXIST) != HFILE_ERROR;
    }
    gfree();
    reuse();
    selfree();
    hugeblk();
    discard();
    freelib();
    deadfs_steps();
    child();
    out("TK61 done\r\n");
    return failures;
}
