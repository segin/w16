/*
 * tk17.c - selector services: KERNEL's selector constants (__0000H ...
 * __F000H, __ROMBIOS, __AHINCR, __AHSHIFT, __WINFLAGS), AllocSelector,
 * AllocSelectorArray, FreeSelector, Get/SetSelectorBase and Limit,
 * SelectorAccessRights, PrestoChangoSelector, the aliases
 * (AllocDStoCSAlias, AllocCStoDSAlias, AllocAlias), LongPtrAdd,
 * GlobalDOSAlloc/Free, GlobalFix/Unfix, GlobalWire/UnWire,
 * GlobalPageLock/Unlock, LockSegment/UnlockSegment, GlobalHandleNoRIP,
 * huge blocks (GlobalAlloc and GlobalReAlloc past 64K) and what the
 * global heap APIs say about raw selectors.
 *
 * Descriptors are looked at with LAR, LSL, VERR and VERW (tk17s.asm).
 * Selector values and linear addresses of heap memory depend on the
 * system and are printed as OBS ~ (checked relative to each other).
 *
 * Output and checks: see tklog.h; reference values in tk17ref.h.
 */
#include <windows.h>

#ifdef TK17A
#define TK_REF "tk17aref.h"
#define TK_REF286 "tk17aref286.h"
#else
#define TK_REF "tk17ref.h"
#define TK_REF286 "tk17ref286.h"
#endif
#include "tklog.h"

extern WORD __cdecl sel_ok(WORD sel);
extern WORD __cdecl sel_ar(WORD sel);
extern DWORD __cdecl sel_lsl(WORD sel);
extern WORD __cdecl sel_rw(WORD sel);
extern WORD __cdecl get_desc(WORD sel, BYTE FAR *buf);
extern WORD __cdecl cur_cs(void);
extern WORD __cdecl cur_ds(void);
extern WORD __cdecl ret_ax(void FAR *fn, WORD arg);

/* not in win16.h (names from windows.lib) */
UINT WINAPI AllocCStoDSAlias(UINT);
UINT WINAPI AllocAlias(UINT);
UINT WINAPI AllocSelectorArray(UINT);
UINT WINAPI SelectorAccessRights(UINT, UINT, UINT);
DWORD WINAPI LongPtrAdd(DWORD, DWORD);
DWORD WINAPI GlobalHandleNoRIP(UINT);

#define STEP(s) out("-- " s "\r\n")

static char nbuf[80];

/* "a b" into nbuf */
const char *nm(const char *a, const char *b)
{
    WORD i = 0;
    while (*a && i < sizeof nbuf - 1)
        nbuf[i++] = *a++;
    if (i < sizeof nbuf - 1)
        nbuf[i++] = ' ';
    while (*b && i < sizeof nbuf - 1)
        nbuf[i++] = *b++;
    nbuf[i] = 0;
    return nbuf;
}

void obs32(const char *p, const char *what, DWORD v)
{
    char n[80];
    WORD i = 0;
    const char *s = nm(p, what);
    while (*s)
        n[i++] = *s++;
    n[i] = ' '; n[i + 1] = 'h'; n[i + 2] = 'i'; n[i + 3] = 0;
    obs(n, HIWORD(v));
    n[i + 1] = 'l'; n[i + 2] = 'o';
    obs(n, LOWORD(v));
}

#define D_BASE  1       /* the base is a fixed value: plain OBS */
#define D_LIMIT 2       /* so is the limit */
#define D_BOTH  3
/* A selector Windows has not made a segment yet (AllocSelector(0)) or
   has freed: Windows marks it with a system type at DPL 0, which DPMI
   cannot make (w16 gives a not-present data segment), so what the CPU
   says about it is not compared. */
#define D_RAW   4

static void obs_if(int plain, const char *name, WORD v)
{
    if (plain)
        obs(name, v);
    else
        obs_env(name, v);
}

/* What the CPU and the selector functions say about sel. */
void desc(const char *p, WORD sel, WORD fixed)
{
    DWORD lim = sel_lsl(sel);
    BYTE d[8];
    WORD i;
    int cpu = !(fixed & D_RAW);
    for (i = 0; i < 8; i++)
        d[i] = 0;
    obs(nm(p, "rpl/ti"), sel & 7);
    obs_if(cpu, nm(p, "DPMI 000B ok"), get_desc(sel, d));
    obs_if(cpu, nm(p, "desc[5..6]"), d[5] | (d[6] & 0xF0) << 8);
    if (fixed & D_LIMIT)
        obs_if(cpu, nm(p, "desc limit 16..19"), d[6] & 15);
    if (fixed & D_LIMIT)
        obs_if(cpu, nm(p, "desc limit 0..15"), d[0] | d[1] << 8);
    obs_if(cpu, nm(p, "lar ok"), sel_ok(sel));
    obs_if(cpu, nm(p, "ar"), sel_ar(sel));
    obs_if(cpu, nm(p, "verr/verw"), sel_rw(sel));
    if ((fixed & D_LIMIT) && cpu) {
        obs32(p, "lsl", lim);
    } else {
        obs_env(nm(p, "lsl hi"), HIWORD(lim));
        obs_env(nm(p, "lsl lo"), LOWORD(lim));
    }
    obs_if(cpu, nm(p, "GetSelectorLimit==lsl"), GetSelectorLimit(sel) == lim);
    if (fixed & D_BASE)
        obs32(p, "base", GetSelectorBase(sel));
    obs_if(cpu, nm(p, "SelectorAccessRights"), SelectorAccessRights(sel, 0, 0));
}

void heapinfo(const char *p, WORD sel)
{
    DWORD gh = GlobalHandle(sel);
    obs(nm(p, "GlobalHandle lo==sel"), LOWORD(gh) == sel);
    obs(nm(p, "GlobalHandle lo==sel&~1"), LOWORD(gh) == (sel & ~1));
    obs(nm(p, "GlobalHandle lo==0"), LOWORD(gh) == 0);
    obs(nm(p, "GlobalHandle hi==sel"), HIWORD(gh) == sel);
    obs(nm(p, "GlobalHandle hi==sel|1"), HIWORD(gh) == (sel | 1));
    obs(nm(p, "GlobalSize lo"), LOWORD(GlobalSize(sel)));
    obs(nm(p, "GlobalFlags"), GlobalFlags(sel));
}

WORD kconst(const char *name)
{
    FARPROC f = GetProcAddress(GetModuleHandle("KERNEL"), name);
    obs(nm(name, "GetProcAddress hi"), HIWORD((DWORD)f));
    return LOWORD((DWORD)f);
}

void constants(void)
{
    static const char *names[] = {
        "__0000H", "__0040H", "__A000H", "__B000H", "__B800H",
        "__C000H", "__D000H", "__E000H", "__F000H", "__ROMBIOS", 0
    };
    WORD i, s, inc, shift;
    STEP("constants");
    for (i = 0; names[i]; i++) {
        s = kconst(names[i]);
        obs_env(names[i], s);
        desc(names[i], s, D_BOTH);
        heapinfo(names[i], s);
    }
    obs("__ROMBIOS==__F000H", kconst("__ROMBIOS") == kconst("__F000H"));
    inc = kconst("__AHINCR");
    shift = kconst("__AHSHIFT");
    obs("__AHINCR", inc);
    obs("__AHSHIFT", shift);
    obs("__WINFLAGS==GetWinFlags", kconst("__WINFLAGS") == (WORD)GetWinFlags());
    /* selector 40h itself (Schulman: usable in Windows) */
    obs("sel 0040 lar ok", sel_ok(0x40));
    obs32("sel 0040", "GetSelectorBase", GetSelectorBase(0x40));
    /* its limit is the DOS extender's (DOSX FFFFh, WIN386 2FFh) */
    obs_env("sel 0040 lsl hi", HIWORD(sel_lsl(0x40)));
    obs_env("sel 0040 lsl lo", LOWORD(sel_lsl(0x40)));
    obs("cs ar", sel_ar(cur_cs()));
    obs("ds ar", sel_ar(cur_ds()));
    obs("cs rpl/ti", cur_cs() & 7);
    obs("cs SelectorAccessRights", SelectorAccessRights(cur_cs(), 0, 0));
    obs("ds SelectorAccessRights", SelectorAccessRights(cur_ds(), 0, 0));
}

static WORD inc_;
WORD use386;                    /* tk17s.asm: 32-bit LAR and LSL */

void alloc_selector(void)
{
    WORD s, s2, t, r, i;
    HGLOBAL h;
    WORD hs;
    STEP("AllocSelector(0)");
    s = AllocSelector(0);
    obs("AllocSelector(0)!=0", s != 0);
    obs_env("AllocSelector(0)", s);
    desc("as0", s, D_BOTH | D_RAW);
    heapinfo("as0", s);

    STEP("SetSelectorBase/Limit");
    r = SetSelectorBase(s, 0x00012340UL);
    obs("SetSelectorBase ret==sel", r == s);
    obs("SetSelectorBase ret==0", r == 0);
    obs_env("SetSelectorBase ret", r);
    desc("as0 base12340", s, D_BOTH | D_RAW);
    r = SetSelectorLimit(s, 0xFFF);
    obs("SetSelectorLimit ret==sel", r == s);
    obs("SetSelectorLimit ret==0", r == 0);
    obs_env("SetSelectorLimit ret", r);
    desc("as0 lim FFF", s, D_BOTH | D_RAW);
    SetSelectorLimit(s, 0x2FFFFUL);
    desc("as0 lim 2FFFF", s, D_BOTH | D_RAW);
    SetSelectorLimit(s, 0xFFFFFFFFUL);
    desc("as0 lim FFFFFFFF", s, D_BOTH | D_RAW);
    SetSelectorLimit(s, 0x100000UL);
    desc("as0 lim 100000", s, D_BOTH | D_RAW);
    SetSelectorLimit(s, 0xFFFF);
    SetSelectorBase(s, 0x00FFFFF0UL);
    desc("as0 base FFFFF0", s, D_BOTH | D_RAW);
    SetSelectorBase(s, 0x12345678UL);
    obs32("as0 base 12345678", "GetSelectorBase", GetSelectorBase(s));
    SetSelectorBase(s | 3, 0x00023450UL);
    obs32("as0 (sel|3)", "GetSelectorBase", GetSelectorBase(s));
    obs32("as0 (sel&~7)", "GetSelectorBase", GetSelectorBase(s & ~7));

    STEP("SelectorAccessRights");
    SetSelectorBase(s, 0);
    FreeSelector(s);
    s = AllocSelectorArray(1);          /* a data segment (F3h) */
    SetSelectorBase(s, 0x00012340UL);
    SetSelectorLimit(s, 0xFFFF);
    obs("SAR(data) get", SelectorAccessRights(s, 0, 0));
    /* only values with the code/data bit (10h) set: DPMI cannot make
       system descriptors */
    for (i = 0; i < 16; i++) {
        static const WORD set[16] = {
            0x0010, 0x0012, 0x0013, 0x0014, 0x0016, 0x0018, 0x001A, 0x001E,
            0x00F2, 0x00FA, 0x0F12, 0x8012, 0x4012, 0x2012, 0x1012, 0xFFFF
        };
        WORD before = sel_ar(s);
        r = SelectorAccessRights(s, 1, set[i]);
        obsn("SAR set ret", set[i], r);
        obsn("SAR set ar", set[i], sel_ar(s));
        obsn("SAR set get", set[i], SelectorAccessRights(s, 0, 0));
        obsn("SAR set lsl lo", set[i], LOWORD(sel_lsl(s)));
        /* back to what it was */
        SelectorAccessRights(s, 1, 0x0012);
        (void)before;
    }
    obs("SAR(op 2) ret", SelectorAccessRights(s, 2, 0x001A));
    obs("SAR(op 2) ar", sel_ar(s));
    SelectorAccessRights(s, 1, 0x0012);
    FreeSelector(s);
    s = AllocSelector(0);

    STEP("FreeSelector");
    SetSelectorBase(s, 0);              /* see the raw-selector note in bad() */
    r = FreeSelector(s);
    obs("FreeSelector ret", r);
    desc("as0 freed", s, D_BOTH | D_RAW);
    obs("FreeSelector(0) ret", FreeSelector(0));
    s2 = AllocSelector(0);
    obs("AllocSelector after free: same", s2 == s);
    t = AllocSelector(0);
    obs_env("AllocSelector next - prev", t - s2);
    obs_env("AllocSelector after free", s2);
    obs_env("AllocSelector next", t);
    FreeSelector(t);
    FreeSelector(s2);

    STEP("AllocSelector(sel)");
    h = GlobalAlloc(GMEM_MOVEABLE, 100);
    hs = SELECTOROF(GlobalLock(h));
    obs_env("100-byte block h", h);
    obs_env("100-byte block sel", hs);
    desc("100-byte block", hs, 0);
    s = AllocSelector(hs);
    obs_env("AllocSelector(100-byte block)", s);
    obs("AllocSelector(block)!=0", s != 0);
    obs("AllocSelector(block) base same", GetSelectorBase(s) == GetSelectorBase(hs));
    obs("AllocSelector(block) lsl", LOWORD(sel_lsl(s)));
    obs("AllocSelector(block) ar", sel_ar(s));
    obs("AllocSelector(block) rpl/ti", s & 7);
    heapinfo("AllocSelector(block)", s);
    /* write through the copy */
    desc("AllocSelector(block)", s, 0);
    if (sel_rw(s) & 2) {
        *(BYTE FAR *)MAKELP(s, 5) = 0x77;
        obs("write through copy seen", *(BYTE FAR *)MAKELP(hs, 5));
    }
    obs("FreeSelector(copy) ret", FreeSelector(s));
    obs("block still ok", sel_ok(hs));
    obs("AllocSelector(handle) ok", (r = AllocSelector(h)) != 0);
    if (r) {
        obs("AllocSelector(handle) base same", GetSelectorBase(r) == GetSelectorBase(hs));
        obs("AllocSelector(handle) lsl", LOWORD(sel_lsl(r)));
        FreeSelector(r);
    }
    GlobalUnlock(h);
    GlobalFree(h);

    STEP("AllocSelector(code)");
    s = AllocSelector(cur_cs());
    obs("AllocSelector(cs) ar", sel_ar(s));
    obs("AllocSelector(cs) base same", GetSelectorBase(s) == GetSelectorBase(cur_cs()));
    obs("AllocSelector(cs) lsl==cs lsl", sel_lsl(s) == sel_lsl(cur_cs()));
    FreeSelector(s);

    STEP("AllocSelector(huge)");
    h = GlobalAlloc(GMEM_MOVEABLE, 0x30000UL + 0x1230);
    hs = SELECTOROF(GlobalLock(h));
    s = AllocSelector(hs);
    obs("AllocSelector(huge)!=0", s != 0);
    for (i = 0; i < 4; i++) {           /* (the fifth is not ours) */
        WORD x = s + i * inc_;
        obsn("AllocSelector(huge) tile lar ok", i, sel_ok(x));
        if (!sel_ok(x))
            continue;
        obsn("AllocSelector(huge) tile lsl hi", i, HIWORD(sel_lsl(x)));
        obsn("AllocSelector(huge) tile lsl lo", i, LOWORD(sel_lsl(x)));
        obsn("AllocSelector(huge) tile ar", i, sel_ar(x));
        obsn("AllocSelector(huge) tile base - block", i,
             (WORD)((GetSelectorBase(x) - GetSelectorBase(hs)) >> 4));
    }
    desc("AllocSelector(huge)", s, 0);
    if (sel_rw(s + 2 * inc_) & 2) {
        *(BYTE FAR *)MAKELP(s + 2 * inc_, 7) = 0x99;
        obs("huge copy tile 2 write seen", *(BYTE FAR *)MAKELP(hs + 2 * inc_, 7));
    }
    r = FreeSelector(s);
    obs("FreeSelector(huge copy) ret", r);
    for (i = 0; i < 4; i++)
        obsn("FreeSelector(huge copy) tile lar ok after", i, sel_ok(s + i * inc_));
    GlobalUnlock(h);
    GlobalFree(h);
}

void selector_array(void)
{
    WORD s, i, r;
    STEP("AllocSelectorArray");
    s = AllocSelectorArray(3);
    obs("AllocSelectorArray(3)!=0", s != 0);
    for (i = 0; i < 4; i++) {
        WORD x = s + i * inc_;
        if (i < 3)
            obsn("ASA tile lar ok", i, sel_ok(x));
        if (i < 3) {
            obsn("ASA tile ar", i, sel_ar(x));
            obsn("ASA tile lsl lo", i, LOWORD(sel_lsl(x)));
            obsn("ASA tile lsl hi", i, HIWORD(sel_lsl(x)));
            obsn("ASA tile base lo", i, LOWORD(GetSelectorBase(x)));
            obsn("ASA tile base hi", i, HIWORD(GetSelectorBase(x)));
            obsn("ASA tile rpl/ti", i, x & 7);
        }
    }
    heapinfo("ASA", s);
    r = FreeSelector(s);
    obs("FreeSelector(array) ret", r);
    for (i = 0; i < 3; i++)
        obsn("ASA tile lar ok after free", i, sel_ok(s + i * inc_));
    for (i = 1; i < 3; i++)
        FreeSelector(s + i * inc_);

    /* an array whose first limit spans the others */
    s = AllocSelectorArray(3);
    SetSelectorBase(s, 0x10000UL);
    SetSelectorLimit(s, 0x2FFFFUL);
    r = FreeSelector(s);
    for (i = 0; i < 3; i++)
        obsn("ASA lim 2FFFF lar ok after free", i, sel_ok(s + i * inc_));
    for (i = 1; i < 3; i++)
        FreeSelector(s + i * inc_);
}

void aliases(void)
{
    HGLOBAL h, hf;
    WORD hs, fs, a, r, x;
    FARPROC p1, p2;
    STEP("AllocDStoCSAlias");
    hf = GlobalAlloc(GMEM_FIXED, 100);
    fs = SELECTOROF(GlobalLock(hf));
    a = AllocDStoCSAlias(fs);
    obs("DStoCS(fixed)!=0", a != 0);
    obs_env("DStoCS(fixed)", a);
    desc("DStoCS(fixed)", a, D_LIMIT);
    obs("DStoCS(fixed) base same", GetSelectorBase(a) == GetSelectorBase(fs));
    heapinfo("DStoCS(fixed)", a);
    obs("DStoCS(fixed) block GlobalFlags", GlobalFlags(hf));
    r = FreeSelector(a);
    obs("FreeSelector(DStoCS) ret", r);
    obs("block GlobalFlags after FreeSelector(alias)", GlobalFlags(hf));
    obs("DStoCS(handle) ok", (a = AllocDStoCSAlias(hf)) != 0);
    if (a) {
        obs("DStoCS(handle) base same", GetSelectorBase(a) == GetSelectorBase(fs));
        FreeSelector(a);
    }
    GlobalFree(hf);

    h = GlobalAlloc(GMEM_MOVEABLE, 0x200);
    hs = SELECTOROF(GlobalLock(h));
    GlobalUnlock(h);
    obs("moveable GlobalFlags before alias", GlobalFlags(h));
    a = AllocDStoCSAlias(hs);
    obs("DStoCS(moveable)!=0", a != 0);
    obs("DStoCS(moveable) ar", sel_ar(a));
    obs("DStoCS(moveable) lsl", LOWORD(sel_lsl(a)));
    obs("moveable GlobalFlags after alias", GlobalFlags(h));
    heapinfo("DStoCS(moveable)", a);
    FreeSelector(a);
    obs("moveable GlobalFlags after free alias", GlobalFlags(h));
    GlobalFree(h);

    STEP("AllocCStoDSAlias");
    a = AllocCStoDSAlias(cur_cs());
    obs("CStoDS(cs)!=0", a != 0);
    desc("CStoDS(cs)", a, 0);
    obs("CStoDS(cs) base same", GetSelectorBase(a) == GetSelectorBase(cur_cs()));
    obs("CStoDS(cs) lsl same", sel_lsl(a) == sel_lsl(cur_cs()));
    heapinfo("CStoDS(cs)", a);
    FreeSelector(a);
    a = AllocCStoDSAlias(cur_ds());
    obs("CStoDS(ds) ar", sel_ar(a));
    FreeSelector(a);
    a = AllocAlias(cur_cs());
    obs("AllocAlias(cs) ar", sel_ar(a));
    obs("AllocAlias(cs) base same", GetSelectorBase(a) == GetSelectorBase(cur_cs()));
    FreeSelector(a);
    p1 = GetProcAddress(GetModuleHandle("KERNEL"), "ALLOCALIAS");
    p2 = GetProcAddress(GetModuleHandle("KERNEL"), "ALLOCCSTODSALIAS");
    obs("AllocAlias==AllocCStoDSAlias", p1 == p2);

    STEP("PrestoChangoSelector");
    h = GlobalAlloc(GMEM_FIXED, 0x300);
    hs = SELECTOROF(GlobalLock(h));
    a = AllocSelector(0);
    x = a;
    r = PrestoChangoSelector(hs, a);
    obs("PCS(data,new) ret==dst", r == a);
    obs("PCS(data,new) ret==0", r == 0);
    desc("PCS(data,new) dst", a, D_LIMIT);
    obs("PCS(data,new) base same", GetSelectorBase(a) == GetSelectorBase(hs));
    r = PrestoChangoSelector(a, hs & ~1);
    obs("PCS(code,data&~1) ret==dst|1", r == (hs | 1));
    obs("PCS(code,data&~1) ret==dst&~1", r == (hs & ~1));
    obs("PCS(code,blk) blk ar", sel_ar(hs));
    PrestoChangoSelector(a, hs);        /* code -> data again */
    obs("PCS back blk ar", sel_ar(hs));
    r = PrestoChangoSelector(cur_cs(), a);
    obs("PCS(cs,x) ar", sel_ar(a));
    obs("PCS(cs,x) lsl same", sel_lsl(a) == sel_lsl(cur_cs()));
    obs("PCS(0,x) ret", PrestoChangoSelector(0, a) == a);
    obs("PCS(0,x) ar", sel_ar(a));
    FreeSelector(x);
    GlobalFree(h);
}

static int lpa_raw;

void lpa(const char *name, WORD s, WORD off, DWORD add)
{
    DWORD r;
    SetSelectorBase(s, 0x10000UL);
    SetSelectorLimit(s, 0xFFFF);
    r = LongPtrAdd(MAKELONG(off, s), add);
    obs(nm(name, "ret hi==sel"), HIWORD(r) == s);
    obs(nm(name, "ret hi==0"), HIWORD(r) == 0);
    obs(nm(name, "ret lo"), LOWORD(r));
    obs32(name, "base", GetSelectorBase(s));
    if (lpa_raw) {                      /* see D_RAW */
        obs_env(nm(name, "lsl lo"), LOWORD(sel_lsl(s)));
        obs_env(nm(name, "ar"), sel_ar(s));
        return;
    }
    obs32(name, "lsl", sel_lsl(s));
    obs(nm(name, "ar"), sel_ar(s));
}

void longptradd(void)
{
    WORD s;
    STEP("LongPtrAdd");
    s = AllocSelectorArray(1);
    lpa("LPA(s:0, 30000)", s, 0, 0x30000UL);
    lpa("LPA(s:1234, 10)", s, 0x1234, 0x10UL);
    lpa("LPA(s:5, 3)", s, 5, 3);
    lpa("LPA(s:0, -10)", s, 0, 0xFFFFFFF0UL);
    lpa("LPA(s:10, 1FFFF)", s, 0x10, 0x1FFFFUL);
    lpa("LPA(s:0, 0)", s, 0, 0);
    lpa("LPA(s:0, EF0000)", s, 0, 0xEF0000UL);
    lpa("LPA(s:0, FF0000)", s, 0, 0xFF0000UL);
    lpa("LPA(s:0, 7FFF0000)", s, 0, 0x7FFF0000UL);
    lpa("LPA(s:8, FFFFFFF8)", s, 8, 0xFFFFFFF8UL);
    SetSelectorBase(s, 0);
    FreeSelector(s);

    /* a raw selector (AllocSelector(0)) */
    s = AllocSelector(0);
    lpa_raw = 1;
    lpa("LPA(raw:0, 30000)", s, 0, 0x30000UL);
    lpa_raw = 0;
    SetSelectorBase(s, 0);
    FreeSelector(s);

    /* LongPtrAdd of a tiled selector (a huge block's copy) ended the
       probe on Windows 3.1 Standard mode: not probed. */
}

void dosalloc(void)
{
    DWORD d;
    WORD s, para, i, z;
    BYTE FAR *p;
    STEP("GlobalDOSAlloc");
    d = GlobalDosAlloc(100);
    s = LOWORD(d);
    para = HIWORD(d);
    obs("GDA(100)!=0", d != 0);
    obs_env("GDA(100) sel", s);
    obs_env("GDA(100) para", para);
    obs("GDA(100) para < A000", para < 0xA000);
    obs("GDA(100) base==para<<4", GetSelectorBase(s) == ((DWORD)para << 4));
    desc("GDA(100)", s, D_LIMIT);
    heapinfo("GDA(100)", s);
    p = MAKELP(s, 0);
    for (i = z = 0; i < 100; i++)
        z |= p[i];
    obs_env("GDA(100) zeroed", z == 0);   /* not on Windows 3.1 */
    obs("GlobalLock(GDA) sel", SELECTOROF(GlobalLock(s)) == s);
    obs("GlobalFlags(GDA) after lock", GlobalFlags(s));
    GlobalUnlock(s);
    obs("GlobalDosFree ret", GlobalDosFree(s));
    obs("GDA freed lar ok", sel_ok(s));
    /* GlobalDosFree(0) returned 0 but the probe died soon after */

    d = GlobalDosAlloc(0);
    obs("GDA(0)!=0", d != 0);
    if (d) {
        obs("GDA(0) GlobalSize", LOWORD(GlobalSize(LOWORD(d))));
        GlobalDosFree(LOWORD(d));
    }

}

void locks(void)
{
    HGLOBAL h, hd, hf;
    WORD r;
    DWORD w;
    STEP("GlobalFix / Wire / PageLock");
    h = GlobalAlloc(GMEM_MOVEABLE, 0x100);
    hd = GlobalAlloc(GMEM_MOVEABLE | GMEM_DISCARDABLE, 0x100);
    hf = GlobalAlloc(GMEM_FIXED, 0x100);
    r = ret_ax((void FAR *)GlobalFix, h);
    obs("GlobalFix(moveable) ax==h", r == h);
    obs("GlobalFix(moveable) ax==h|1", r == (h | 1));
    obs_env("GlobalFix(moveable) ax", r);
    obs("GlobalFlags after GlobalFix", GlobalFlags(h));
    GlobalFix(h);
    obs("GlobalFlags after GlobalFix x2", GlobalFlags(h));
    r = ret_ax((void FAR *)GlobalUnfix, h);
    obs_env("GlobalUnfix ax", r);
    obs("GlobalFlags after GlobalUnfix", GlobalFlags(h));
    GlobalUnfix(h);
    obs("GlobalFlags after GlobalUnfix x2", GlobalFlags(h));
    GlobalFix(hd);
    obs("GlobalFlags(discardable) after GlobalFix", GlobalFlags(hd));
    GlobalUnfix(hd);
    obs("GlobalFlags(discardable) after GlobalUnfix", GlobalFlags(hd));
    GlobalFix(hf);
    obs("GlobalFlags(fixed) after GlobalFix", GlobalFlags(hf));
    GlobalUnfix(hf);

    w = (DWORD)GlobalWire(h);
    obs("GlobalWire off", LOWORD(w));
    obs("GlobalWire sel==h|1", HIWORD(w) == (h | 1));
    obs("GlobalFlags after GlobalWire", GlobalFlags(h));
    obs("GlobalUnWire ret", GlobalUnWire(h));
    obs("GlobalFlags after GlobalUnWire", GlobalFlags(h));
    w = (DWORD)GlobalWire(hd);
    obs("GlobalFlags(discardable) after GlobalWire", GlobalFlags(hd));
    obs("GlobalUnWire(discardable) ret", GlobalUnWire(hd));
    obs("GlobalWire(0) ", (WORD)((DWORD)GlobalWire(0) != 0));

    obs("GlobalPageLock ret", GlobalPageLock(h));
    obs("GlobalFlags after GlobalPageLock", GlobalFlags(h));
    obs("GlobalPageLock 2 ret", GlobalPageLock(h));
    obs("GlobalPageUnlock ret", GlobalPageUnlock(h));
    obs("GlobalPageUnlock 2 ret", GlobalPageUnlock(h));
    obs("GlobalFlags after GlobalPageUnlock", GlobalFlags(h));
    obs("GlobalPageLock(sel) ret", GlobalPageLock(h | 1));
    obs("GlobalPageUnlock(sel) ret", GlobalPageUnlock(h | 1));
    obs("GlobalPageLock(discardable) ret", GlobalPageLock(hd));
    obs("GlobalFlags(discardable) after GlobalPageLock", GlobalFlags(hd));
    GlobalPageUnlock(hd);
    obs("GlobalPageLock(fixed) ret", GlobalPageLock(hf));
    GlobalPageUnlock(hf);
    obs("GlobalPageLock(0) ret", GlobalPageLock(0));

    STEP("LockSegment");
    r = LockSegment(h);
    obs("LockSegment(moveable) ret==h", r == h);
    obs("LockSegment(moveable) ret==h|1", r == (h | 1));
    obs("GlobalFlags after LockSegment(moveable)", GlobalFlags(h));
    r = ret_ax((void FAR *)UnlockSegment, h);
    obs("UnlockSegment(moveable) ax==h", r == h);
    obs("UnlockSegment(moveable) ax==h|1", r == (h | 1));
    obs("UnlockSegment(moveable) ax==0", r == 0);
    obs("GlobalFlags after UnlockSegment(moveable)", GlobalFlags(h));
    r = LockSegment(hd);
    obs("LockSegment(discardable) ret==h", r == hd);
    obs("LockSegment(discardable) ret==h|1", r == (hd | 1));
    obs("GlobalFlags after LockSegment(discardable)", GlobalFlags(hd));
    r = ret_ax((void FAR *)UnlockSegment, hd);
    obs("UnlockSegment(discardable) ax==h", r == hd);
    obs("UnlockSegment(discardable) ax==0", r == 0);
    obs("GlobalFlags after UnlockSegment(discardable)", GlobalFlags(hd));
    r = LockSegment(hf);
    obs("LockSegment(fixed) ret==h", r == hf);
    obs("LockSegment(fixed) ret==h|1", r == (hf | 1));
    obs("GlobalFlags after LockSegment(fixed)", GlobalFlags(hf));
    UnlockSegment(hf);
    obs("LockSegment(0) ret", LockSegment(0));

    STEP("GlobalHandleNoRIP");
    w = GlobalHandleNoRIP(h | 1);
    obs("GHNoRIP(sel) == GlobalHandle", w == GlobalHandle(h | 1));
    obs("GHNoRIP(0)", LOWORD(GlobalHandleNoRIP(0)));
    GlobalFree(h);
    GlobalFree(hd);
    GlobalFree(hf);
}

void huge_blocks(void)
{
    HGLOBAL h, h2, hn;
    WORD s, i, ok;
    DWORD b;
    BYTE FAR *p;
    STEP("huge GlobalAlloc");
    h = GlobalAlloc(GMEM_MOVEABLE, 0x2A345UL);
    s = SELECTOROF(GlobalLock(h));
    obs("huge GlobalSize hi", HIWORD(GlobalSize(h)));
    obs("huge GlobalSize lo", LOWORD(GlobalSize(h)));
    b = GetSelectorBase(s);
    for (i = 0; i < 4; i++) {
        WORD x = s + i * inc_;
        if (i == 3)
            break;                      /* the selector after: not ours */
        obsn("huge tile lar ok", i, sel_ok(x));
        obsn("huge tile ar", i, sel_ar(x));
        obsn("huge tile lsl hi", i, HIWORD(sel_lsl(x)));
        obsn("huge tile lsl lo", i, LOWORD(sel_lsl(x)));
        obsn("huge tile base - base0 hi", i, HIWORD(GetSelectorBase(x) - b));
        obsn("huge tile base - base0 lo", i, LOWORD(GetSelectorBase(x) - b));
        obsn("huge tile GlobalHandle lo==h", i, LOWORD(GlobalHandle(x)) == h);
        obsn("huge tile GlobalSize lo", i, LOWORD(GlobalSize(x)));
    }
    GlobalUnlock(h);
    GlobalFree(h);

    STEP("GlobalReAlloc past 64K");
    h = GlobalAlloc(GMEM_MOVEABLE, 0x1000);
    h2 = GlobalAlloc(GMEM_MOVEABLE, 0x1000);     /* likely next selector */
    obs_env("h2 - h", h2 - h);
    obs_env("h", h);
    p = GlobalLock(h);
    obs_env("GlobalLock(h) sel", SELECTOROF(p));
    if (!p)
        return;
    for (i = 0; i < 0x1000; i++)
        p[i] = (BYTE)i;
    GlobalUnlock(h);
    STEP("GlobalReAlloc(h, 25000h, GMEM_MOVEABLE)");
    hn = GlobalReAlloc(h, 0x25000UL, GMEM_MOVEABLE);
    obs("ReAlloc moveable 1000->25000 ok", hn != 0);
    obs("ReAlloc moveable 1000->25000 same handle", hn == h);
    if (hn) {
        s = SELECTOROF(GlobalLock(hn));
        obs("ReAlloc 25000 GlobalSize hi", HIWORD(GlobalSize(hn)));
        obs("ReAlloc 25000 GlobalSize lo", LOWORD(GlobalSize(hn)));
        p = MAKELP(s, 0);
        for (i = ok = 0; i < 0x1000; i++)
            ok |= p[i] != (BYTE)i;
        obs("ReAlloc 25000 data kept", !ok);
        for (i = 0; i < 3; i++) {
            obsn("ReAlloc 25000 tile lsl hi", i, HIWORD(sel_lsl(s + i * inc_)));
            obsn("ReAlloc 25000 tile lsl lo", i, LOWORD(sel_lsl(s + i * inc_)));
            obsn("ReAlloc 25000 tile GlobalHandle==hn", i,
                 LOWORD(GlobalHandle(s + i * inc_)) == hn);
        }
        *(BYTE FAR *)MAKELP(s + 2 * inc_, 0x4FFF) = 0x42;
        obs("ReAlloc 25000 last byte", *(BYTE FAR *)MAKELP(s + 2 * inc_, 0x4FFF));
        obs_env("ReAlloc 25000 new memory byte", *(BYTE FAR *)MAKELP(s + inc_, 0x10));
        GlobalUnlock(hn);
        obs("h2 still ok", GlobalSize(h2) >= 0x1000);
        /* shrink back */
        h = GlobalReAlloc(hn, 0x800, GMEM_MOVEABLE);
        obs("ReAlloc 25000->800 same handle", h == hn);
        s = hn | 1;
        obs("ReAlloc 800 GlobalSize lo", LOWORD(GlobalSize(hn)));
        obs("ReAlloc 800 tile0 lsl", LOWORD(sel_lsl(s)));
        obs("ReAlloc 800 tile1 lar ok", sel_ok(s + inc_));
        obs("ReAlloc 800 tile1 GlobalHandle==hn", LOWORD(GlobalHandle(s + inc_)) == hn);
        /* and grow once more */
        h = GlobalReAlloc(hn, 0x12000UL, GMEM_MOVEABLE);
        obs("ReAlloc 800->12000 same handle", h == hn);
        obs("ReAlloc 12000 tile1 lsl", LOWORD(sel_lsl((h | 1) + inc_)));
        GlobalFree(h);
    }
    GlobalFree(h2);

    STEP("GlobalReAlloc fixed past 64K");
    h = GlobalAlloc(GMEM_FIXED, 0x1000);
    h2 = GlobalAlloc(GMEM_FIXED, 0x1000);
    hn = GlobalReAlloc(h, 0x20000UL, 0);
    obs("ReAlloc fixed 1000->20000 (no MOVEABLE) ok", hn != 0);
    obs("ReAlloc fixed 1000->20000 (no MOVEABLE) same", hn == h);
    if (!hn) {
        hn = GlobalReAlloc(h, 0x20000UL, GMEM_MOVEABLE);
        obs("ReAlloc fixed 1000->20000 (MOVEABLE) ok", hn != 0);
        obs("ReAlloc fixed 1000->20000 (MOVEABLE) same", hn == h);
    }
    if (hn) {
        obs("ReAlloc fixed 20000 tile1 lsl", LOWORD(sel_lsl((hn | 1) + inc_)));
        obs("ReAlloc fixed 20000 GlobalSize hi", HIWORD(GlobalSize(hn)));
        GlobalFree(hn);
    } else {
        GlobalFree(h);
    }
    GlobalFree(h2);
    /* growing a huge block by more tiles */
    h = GlobalAlloc(GMEM_MOVEABLE, 0x18000UL);
    hn = GlobalReAlloc(h, 0x38000UL, GMEM_MOVEABLE);
    obs("ReAlloc 18000->38000 same handle", hn == h);
    if (hn) {
        s = hn | 1;
        for (i = 0; i < 4; i++)
            obsn("ReAlloc 38000 tile lsl", i, LOWORD(sel_lsl(s + i * inc_)));
        GlobalFree(hn);
    }
}

/* Raw selectors and bad ones in the global heap APIs; last, as some of
   them may disturb the probe. */
void bad(void)
{
    WORD s, f;
    HGLOBAL h;
    STEP("bad selectors");
    f = AllocSelector(0);
    FreeSelector(f);                    /* f: a free selector */
    obs("GetSelectorBase(free) lo", LOWORD(GetSelectorBase(f)));
    obs("GetSelectorBase(free) hi", HIWORD(GetSelectorBase(f)));
    /* KRNL286 reads the free slot's limit word, KRNL386 uses LSL */
    obs_env("GetSelectorLimit(free) lo", LOWORD(GetSelectorLimit(f)));
    obs("GetSelectorBase(0) lo", LOWORD(GetSelectorBase(0)));
    obs("GetSelectorLimit(0) lo", LOWORD(GetSelectorLimit(0)));
    obs("SelectorAccessRights(free)", SelectorAccessRights(f, 0, 0));
    obs("AllocSelector(free) ok", AllocSelector(f) != 0);
    obs("AllocDStoCSAlias(0)", AllocDStoCSAlias(0));
    obs("AllocDStoCSAlias(free)!=0", AllocDStoCSAlias(f) != 0);
    obs("AllocCStoDSAlias(0)", AllocCStoDSAlias(0));
    obs("GlobalHandle(free) lo", LOWORD(GlobalHandle(f)));
    obs("GlobalHandleNoRIP(free) lo", LOWORD(GlobalHandleNoRIP(f)));

    s = AllocSelector(0);
    SetSelectorBase(s, 0x10000UL);
    SetSelectorLimit(s, 0xFFFF);
    heapinfo("raw selector", s);
    obs("GlobalLock(raw) ", (WORD)((DWORD)GlobalLock(s) != 0));
    /* GlobalFree of it stopped Windows (KRNL286): not probed */
    obs_env("raw after GlobalFree lar ok", sel_ok(s));
    SetSelectorBase(s, 0);
    FreeSelector(s);

    h = GlobalAlloc(GMEM_MOVEABLE, 0x100);
    s = SELECTOROF(GlobalLock(h));
    GlobalUnlock(h);
    f = FreeSelector(s);
    obs("FreeSelector(GlobalAlloc sel) ret==sel", f == s);
    obs("FreeSelector(GlobalAlloc sel) ret==0", f == 0);
    obs("GlobalAlloc sel after FreeSelector lar ok", sel_ok(s));
    obs("GlobalSize after FreeSelector", LOWORD(GlobalSize(h)));
}

/* Last: these stopped Windows 3.1 (Standard mode) in earlier runs or may
   disturb the heap. */
/* (TK17A) where LongPtrAdd's limit is: more additions to base 10000h */
void lpa_more(void)
{
    static const DWORD add[] = {
        0x0FFFFFUL, 0x100000UL, 0x3F0000UL, 0x400000UL, 0x7F0000UL,
        0x800000UL, 0xBF0000UL, 0xC00000UL, 0xDF0000UL, 0xE00000UL, 0
    };
    char name[24];
    WORD i, s = AllocSelectorArray(1);
    STEP("LongPtrAdd limits");
    for (i = 0; add[i]; i++) {
        DWORD r;
        SetSelectorBase(s, 0x10000UL);
        r = LongPtrAdd(MAKELONG(0, s), add[i]);
        lstrcpy(name, "LPA+");
        name[4] = "0123456789ABCDEF"[(WORD)(add[i] >> 20) & 15];
        name[5] = "0123456789ABCDEF"[(WORD)(add[i] >> 16) & 15];
        name[6] = 0;
        obs(nm(name, "ok"), HIWORD(r) == s);
        obs(nm(name, "base hi"), HIWORD(GetSelectorBase(s)));
    }
    SetSelectorBase(s, 0);
    FreeSelector(s);
}

void dos_last(void)
{
    DWORD d;
    WORD s;
    STEP("GlobalDOSAlloc, last");
    d = GlobalDosAlloc(0x100000UL);
    obs("GDA(1MB)", d != 0);
    d = GlobalDosAlloc(0x18000UL);
    s = LOWORD(d);
    obs("GDA(18000)!=0", d != 0);
    if (d) {
        obs("GDA(18000) base==para<<4", GetSelectorBase(s) == ((DWORD)HIWORD(d) << 4));
        obs32("GDA(18000)", "lsl", sel_lsl(s));
        obs("GDA(18000) tile1 lar ok", sel_ok(s + inc_));
        obs32("GDA(18000) tile1", "lsl", sel_lsl(s + inc_));
        obs("GDA(18000) tile1 base", (WORD)((GetSelectorBase(s + inc_) - GetSelectorBase(s)) >> 4));
        heapinfo("GDA(18000)", s);
        obs("GlobalDosFree(18000) ret", GlobalDosFree(s));
    }
    /* a GlobalAlloc block freed with GlobalDosFree, and the reverse */
    {
        HGLOBAL h = GlobalAlloc(GMEM_FIXED, 64);
        obs("GlobalDosFree(GlobalAlloc block) ret", GlobalDosFree(h));
        obs("GlobalAlloc block after GlobalDosFree: GlobalSize", LOWORD(GlobalSize(h)));
        GlobalFree(h);
        d = GlobalDosAlloc(64);
        obs("GlobalFree(GDA block) ret", GlobalFree(LOWORD(d)));
        obs("GDA block after GlobalFree: lar ok", sel_ok(LOWORD(d)));
    }
}


#ifdef TK17A
/* Pointer checks and string functions in the first 64K of a 96K block:
   its first selector's limit goes past FFFFh on a 386 (17FFFh); w16 read
   it with a 16-bit LSL (7FFFh) and called offsets beyond 7FFFh bad */
static void huge_ptrs(void)
{
    HGLOBAL h = GlobalAlloc(GMEM_MOVEABLE | GMEM_ZEROINIT, 0x18000L);
    char FAR *b = h ? (char FAR *)GlobalLock(h) : NULL;
    char FAR *p;
    if (!b) {
        obs("hp: GlobalAlloc(18000h)", 0);
        return;
    }
    p = b + 0x9000;
    p[0] = 'x';
    p[1] = 0;
    obs("hp: IsBadReadPtr(9000h,10h)", IsBadReadPtr(p, 0x10));
    obs("hp: IsBadWritePtr(9000h,10h)", IsBadWritePtr(p, 0x10));
    obs("hp: IsBadStringPtr(9000h)", IsBadStringPtr(p, 0xFFFF));
    obs("hp: IsBadReadPtr(FFF0h,10h)", IsBadReadPtr(b + 0xFFF0, 0x10));
    obs("hp: lstrlen(9000h)", lstrlen(p));
    obs("hp: lstrcpy(9100h, 9000h)==dst", lstrcpy(b + 0x9100, p) == b + 0x9100);
    obs("hp: copied", (BYTE)b[0x9100]);
    GlobalUnlock(h);
    GlobalFree(h);
}
#endif

int __cdecl tmain(void)
{
#ifdef TK17A
    out("TK17A start\r\n");
#else
    out("TK17 start\r\n");
#endif
    use386 = (GetWinFlags() & (WF_CPU386 | WF_CPU486)) != 0;
    inc_ = LOWORD((DWORD)GetProcAddress(GetModuleHandle("KERNEL"), "__AHINCR"));
#ifdef TK17A
    /* TK17A: huge blocks, in a fresh program (TK17's earlier steps left
       Windows 3.1 unable to finish these) */
    huge_blocks();
    huge_ptrs();
    lpa_more();
    dos_last();
#else
    constants();
    alloc_selector();
    selector_array();
    aliases();
    longptradd();
    dosalloc();
    locks();
    bad();
#endif
    out("TK17 done, failures=");
    outhex((WORD)failures);
    out("\r\n");
    return failures;
}
