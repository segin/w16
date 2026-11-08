/*
 * tk19.c - the global heap: Windows 3.1 KRNL286's arenas, Burgermaster,
 * selector table and heap behaviour, black-box.
 *
 * Windows 3.1 runs KRNL286 only on an 80286; on a 386 its Standard mode
 * runs KRNL386, whose heap is laid out differently. Reference runs:
 * REFRUN_KRNL286=1 tests/refrun.sh TK19.EXE /S (KRNL286 copied over
 * KRNL386.EXE in the scratch copy) or REFRUN_CPU=286 (an 80286 in
 * DOSBox-X). Under KRNL386 the probe only says so.
 *
 * The structures probed are the ones published in Schulman et al.,
 * "Undocumented Windows" (1992), ch. 5 (Global Arena, Global Heap,
 * Selector Table, THHOOK) and Pietrek, "Windows Internals" (1993), ch. 2.
 * kernel/global.c describes what was found.
 *
 * Output and checks: see tklog.h; reference values in tk19ref.h. When
 * TOOLHELP.DLL can be loaded (on Windows), its view of the heap is checked
 * against the arenas too.
 */
#include <windows.h>
#include <toolhelp.h>

#define TK_REF "tk19ref.h"
#include "tklog.h"

extern DWORD FAR PASCAL K_GLOBALMASTERHANDLE(void);
extern void FAR * FAR PASCAL K_GLOBALWIRE(WORD h);
extern WORD  FAR PASCAL K_GLOBALUNWIRE(WORD h);
extern void  FAR PASCAL K_GLOBALNOTIFY(FARPROC p);
extern DWORD FAR PASCAL K_GLOBALHANDLENORIP(WORD h);
extern WORD  FAR PASCAL K_GLOBALLRUOLDEST(WORD h);
extern WORD  FAR PASCAL K_GLOBALLRUNEWEST(WORD h);
extern DWORD FAR PASCAL K_GETFREESPACE(WORD f);
extern DWORD FAR PASCAL K_GETSELECTORBASE(WORD s);
extern WORD  FAR PASCAL K_GLOBALFIX(WORD h);
extern WORD  FAR PASCAL K_GLOBALUNFIX(WORD h);
extern DWORD FAR PASCAL K_GETCURRENTPDB(void);
extern DWORD FAR PASCAL K_GETCURRENTTASK(void);
extern WORD __cdecl get_ahincr(void);           /* start.asm */

WORD notify_n, notify_rec[32], notify_ret;     /* tk19n.asm */
extern BOOL FAR PASCAL NOTIFY_PROC(HGLOBAL h);

WORD lar(WORD s);
#pragma aux lar = "lar ax, ax" "jz ok" "xor ax, ax" "ok:" parm [ax] value [ax];

#define SELTAB  0x72            /* observed: THHOOK 18h */

static WORD bm, pdb;

static WORD W(WORD sel, WORD off) { return *w_at(sel, off); }
static BYTE B(WORD sel, WORD off) { return *(BYTE FAR *)MAKELP(sel, off); }
static DWORD base(WORD s) { return K_GETSELECTORBASE(s); }

/* GetSelectorLimit where KERNEL has it, else LSL (w16 needs a 386) */
typedef DWORD (FAR PASCAL *GSL)(WORD);
static GSL gsl;
DWORD lsl32(WORD s);
#pragma aux lsl32 = \
    0x66 0x0F 0xB7 0xC0     /* movzx eax, ax */ \
    0x66 0x0F 0x03 0xC0     /* lsl eax, eax */ \
    0x74 0x03               /* jz +3 */ \
    0x66 0x33 0xC0          /* xor eax, eax */ \
    0x66 0x8B 0xD0          /* mov edx, eax */ \
    0x66 0xC1 0xEA 0x10     /* shr edx, 16 */ \
    parm [ax] value [dx ax];

static DWORD limit(WORD s)
{
    return gsl ? gsl(s) : lsl32(s);
}

/* arena fields (Schulman, KRNL286 Global Arena) */
#define A_COUNT(a)  B(a, 0)
#define A_OWNER(a)  W(a, 1)
#define A_SIZE(a)   W(a, 3)
#define A_FLAGS(a)  B(a, 5)
#define A_PREV(a)   W(a, 6)
#define A_NEXT(a)   W(a, 8)
#define A_HANDLE(a) W(a, 10)
#define A_LPREV(a)  W(a, 12)
#define A_LNEXT(a)  W(a, 14)

static WORD first(void) { return W(bm, 6); }
static WORD last(void) { return W(bm, 8); }

static WORD seltab(WORD s)
{
    return W(bm, SELTAB + (s >> 3) * 2);
}

static WORD arena(HGLOBAL h)
{
    return h ? seltab(h | 1) : 0;
}

/* the name "prefix suffix" */
static const char *nm(const char *p, const char *s)
{
    static char b[80];
    WORD i = 0;
    while (*p && i < 40)
        b[i++] = *p++;
    b[i++] = ' ';
    while (*s && i < sizeof b - 1)
        b[i++] = *s++;
    b[i] = 0;
    return b;
}

static void obs32(const char *name, DWORD v)
{
    char n[64];
    lstrcpy(n, name);
    obs(nm(n, "high"), HIWORD(v));
    obs(nm(n, "low"), LOWORD(v));
}

/* ---- the whole heap ---- */

static void walk_checks(const char *tag)
{
    WORD a = first(), n = 0, ok_link = 1, ok_adj = 1, ok_lim = 1, ok_free = 1;
    WORD ok_busy = 1, ok_nt = 1, nfree = 0, nlru = 0, ok_flist = 1, ok_lru = 1, f;
    DWORD prevaddr = 0;
    char t[48];

    while (n < 8000) {
        WORD nx = A_NEXT(a);
        n++;
        if (limit(a) != 0xF)
            ok_lim = 0;
        if (nx != a && A_PREV(nx) != a)
            ok_link = 0;
        if (A_OWNER(a) == 0xFFFC) {
            if (A_SIZE(a) != 0 || A_HANDLE(a) || A_COUNT(a) != 'M')
                ok_nt = 0;
        } else if (nx != a && A_OWNER(nx) != 0xFFFC &&
                   base(nx) != base(a) + 0x10 + ((DWORD)A_SIZE(a) << 4)) {
            ok_adj = 0;
        }
        if (A_OWNER(a) == 0 && A_HANDLE(a) == 0) {
            nfree++;
            if (A_COUNT(a) != 'M' || A_FLAGS(a))
                ok_free = 0;
        } else if (A_HANDLE(a)) {
            WORD h = A_HANDLE(a), s = h | 1;
            if (base(s) != base(a) + 0x10 || GlobalSize(h) != (DWORD)A_SIZE(a) << 4 ||
                seltab(s) != a || !(A_SIZE(a) & 1))
                ok_busy = 0;
            if (A_LNEXT(a))
                nlru++;
        }
        if (nx == a)
            break;
        a = nx;
    }
    lstrcpy(t, tag);
    check(a == last() && A_COUNT(a) == 'Z' && A_OWNER(a) == 0xFFFF && A_SIZE(a) == 1,
          nm(t, "walk: last sentinel"));
    check(n == W(bm, 4), nm(t, "walk: count (BM 04h)"));
    check(ok_link, nm(t, "walk: prev links"));
    check(ok_lim, nm(t, "walk: arena selectors limit 0Fh"));
    check(ok_adj, nm(t, "walk: arenas adjacent (size in paragraphs)"));
    check(ok_free, nm(t, "walk: free arenas"));
    check(ok_busy, nm(t, "walk: blocks follow their arenas, selector table"));
    check(ok_nt, nm(t, "walk: not-there arenas"));
    check(nlru == W(bm, 0x1C), nm(t, "walk: LRU count (BM 1Ch)"));
    check(nfree == W(bm, 0x22), nm(t, "walk: free count (BM 22h)"));
    /* the free list: address order, from the first sentinel's 0Eh */
    n = 0;
    for (f = A_LNEXT(first()); f != last() && n < 8000; f = A_LNEXT(f), n++) {
        if (A_OWNER(f) || A_HANDLE(f) || base(f) <= prevaddr || A_LPREV(A_LNEXT(f)) != f)
            ok_flist = 0;
        prevaddr = base(f);
    }
    check(ok_flist && n == nfree && A_LPREV(first()) == 0xFFFF && A_LNEXT(last()) == 0xFFFF,
          nm(t, "free list"));
    /* the LRU ring from BM 1Ah */
    n = 0;
    f = W(bm, 0x1A);
    if (f) {
        do {
            if (!A_HANDLE(f) || A_LPREV(A_LNEXT(f)) != f)
                ok_lru = 0;
            f = A_LNEXT(f);
            n++;
        } while (f != W(bm, 0x1A) && n < 8000);
    }
    check(ok_lru && n == nlru, nm(t, "LRU ring"));
}

/* ---- one block ---- */

static void block(const char *tag, HGLOBAL h)
{
    WORD a = arena(h), s = h | 1;
    char t[48];
    lstrcpy(t, tag);
    if (!a || limit(a) != 0xF) {
        obs(nm(t, "has arena"), 0);
        return;
    }
    obs(nm(t, "has arena"), 1);
    obs(nm(t, "handle odd"), h & 1);
    obs(nm(t, "arena count"), A_COUNT(a));
    obs(nm(t, "arena owner==PDB"), A_OWNER(a) == pdb);
    obs(nm(t, "arena size"), A_SIZE(a));
    obs(nm(t, "arena flags"), A_FLAGS(a));
    obs(nm(t, "arena handle==h"), A_HANDLE(a) == h);
    obs(nm(t, "in LRU"), A_LNEXT(a) != 0);
    obs(nm(t, "data-arena"), (WORD)(base(s) - base(a)));
    obs(nm(t, "GlobalSize"), (WORD)GlobalSize(h));
    obs(nm(t, "GlobalFlags"), GlobalFlags(h));
    obs(nm(t, "GlobalHandle.lo==h"), LOWORD(GlobalHandle(s)) == h);
    obs(nm(t, "GlobalHandle.hi==sel"), HIWORD(GlobalHandle(s)) == s);
    obs(nm(t, "access"), lar(s) >> 8);
    obs_env(nm(t, "owner"), A_OWNER(a));
    obs_env(nm(t, "paragraph"), (WORD)(base(a) >> 4));
}

/* a discarded block: no arena, a not-present descriptor */
static void discarded(const char *tag, HGLOBAL h)
{
    WORD s = h | 1;
    char t[48];
    lstrcpy(t, tag);
    obs(nm(t, "GlobalFlags"), GlobalFlags(h));
    obs(nm(t, "GlobalSize"), (WORD)GlobalSize(h));
    obs(nm(t, "GlobalHandle"), (WORD)GlobalHandle(s));
    obs(nm(t, "GlobalLock"), HIWORD((DWORD)GlobalLock(h)));
    obs(nm(t, "access"), lar(s) >> 8);
    obs(nm(t, "limit==PDB"), (WORD)limit(s) == pdb);
    obs(nm(t, "selector table==PDB"), seltab(s) == pdb);
}

/* ---- TOOLHELP's view (on Windows) ---- */

typedef BOOL (FAR PASCAL *GFIRST)(GLOBALENTRY FAR *, WORD);
typedef BOOL (FAR PASCAL *GINFO)(GLOBALINFO FAR *);
typedef BOOL (FAR PASCAL *GEHANDLE)(GLOBALENTRY FAR *, HGLOBAL);

static void toolhelp(HGLOBAL h)
{
    HINSTANCE th;
    GFIRST gfirst, gnext;
    GINFO ginfo;
    GEHANDLE geh;
    GLOBALINFO gi;
    GLOBALENTRY ge;
    WORD n = 0, ok = 1, a;
    BOOL more, i;

    th = LoadLibrary("TOOLHELP.DLL");
    obs_env("TOOLHELP loaded", th > HINSTANCE_ERROR);
    if (th <= HINSTANCE_ERROR)
        return;
    gfirst = (GFIRST)GetProcAddress(th, "GlobalFirst");
    gnext = (GFIRST)GetProcAddress(th, "GlobalNext");
    ginfo = (GINFO)GetProcAddress(th, "GlobalInfo");
    geh = (GEHANDLE)GetProcAddress(th, "GlobalEntryHandle");
    gi.dwSize = sizeof gi;
    ginfo(&gi);
    obs_env("TOOLHELP wcItems", gi.wcItems);
    obs_env("TOOLHELP wcItemsFree", gi.wcItemsFree);
    obs_env("TOOLHELP wcItemsLRU", gi.wcItemsLRU);
    obs_env("BM count", W(bm, 4));
    obs_env("BM free count", W(bm, 0x22));
    obs_env("BM LRU count", W(bm, 0x1C));
    /* observed: wcItems is one more than the Burgermaster's count */
    check(gi.wcItems == W(bm, 4) + 1 && gi.wcItemsFree == W(bm, 0x22) &&
          gi.wcItemsLRU == W(bm, 0x1C), "TOOLHELP GlobalInfo: Burgermaster counts");
    ge.dwSize = sizeof ge;
    a = first();
    for (more = gfirst(&ge, GLOBAL_ALL); more && n < 8000; more = gnext(&ge, GLOBAL_ALL)) {
        if (ge.dwAddress != base(a) || ge.dwBlockSize != (DWORD)A_SIZE(a) << 4 ||
            ge.hBlock != A_HANDLE(a) || ge.wcLock != A_COUNT(a) ||
            (ge.wFlags & ~GF_PDB_OWNER) != A_FLAGS(a) ||
            ge.dwNext != (a == last() ? 0 : A_NEXT(a))) {
            if (ok) {
                obs_env("TOOLHELP mismatch at", n);
                obs_env("TOOLHELP mismatch arena", a);
                obs_env("TOOLHELP mismatch address", (WORD)(ge.dwAddress >> 4));
                obs_env("TOOLHELP mismatch size", (WORD)(ge.dwBlockSize >> 4));
                obs_env("TOOLHELP mismatch hBlock", ge.hBlock);
                obs_env("TOOLHELP mismatch wcLock", ge.wcLock);
                obs_env("TOOLHELP mismatch wFlags", ge.wFlags);
                obs_env("TOOLHELP mismatch dwNext", (WORD)ge.dwNext);
            }
            ok = 0;
        }
        a = A_NEXT(a);
        n++;
    }
    check(ok && n == W(bm, 4), "TOOLHELP GlobalFirst/Next: the arenas");
    ge.dwSize = sizeof ge;
    a = arena(h);
    i = geh(&ge, h);
    obs_env("TOOLHELP GlobalEntryHandle", i);
    obs_env("TOOLHELP GlobalEntryHandle address", (WORD)(ge.dwAddress >> 4));
    obs_env("TOOLHELP GlobalEntryHandle hOwner", ge.hOwner);
    obs_env("TOOLHELP GlobalEntryHandle wFlags", ge.wFlags);
    check(i && ge.dwAddress == base(a) && ge.hBlock == h, "TOOLHELP GlobalEntryHandle");
    obs("TOOLHELP GlobalEntryHandle: PDB owner flag", ge.wFlags);
    FreeLibrary(th);
}

/* ---- behaviour ---- */

static void nrec(const char *tag, HGLOBAL h)
{
    char t[48];
    lstrcpy(t, tag);
    obs(nm(t, "notify called"), notify_n != 0);
    obs_env(nm(t, "notify calls"), notify_n);
    if (notify_n) {
        obs(nm(t, "notify arg==h"), notify_rec[0] == h);
        obs(nm(t, "notify DS==SS"), notify_rec[1] == notify_rec[3]);
        obs(nm(t, "notify ES==SS"), notify_rec[2] == notify_rec[3]);
        obs(nm(t, "notify SS==ours"), notify_rec[3] == entry_regs[E_DS]);
    }
    notify_n = 0;
}

static WORD lru_head(void)
{
    return W(bm, 0x1A);
}

static WORD discarded_any(const HGLOBAL *h)
{
    return (GlobalFlags(h[0]) | GlobalFlags(h[2]) | GlobalFlags(h[3])) & GMEM_DISCARDED;
}

#define NFILL 1200

static HGLOBAL fill[NFILL];

static void tests(void)
{
    HGLOBAL h[12];
    WORD i, t = LOWORD(K_GETCURRENTTASK()), nf = 0, first_disc = 0, inc = get_ahincr();
    DWORD b0, v;
    FARPROC np;
    static const char *const nd[4] = { "D0", "D1", "D2", "D3" };

    /* allocations */
    h[0] = GlobalAlloc(GMEM_FIXED, 1);
    block("fixed 1", h[0]);
    h[1] = GlobalAlloc(GMEM_FIXED, 100);
    block("fixed 100", h[1]);
    h[2] = GlobalAlloc(GMEM_MOVEABLE, 100);
    block("moveable 100", h[2]);
    h[3] = GlobalAlloc(GMEM_MOVEABLE | GMEM_DISCARDABLE, 100);
    block("discardable 100", h[3]);
    h[4] = GlobalAlloc(GMEM_FIXED | GMEM_DISCARDABLE, 100);
    block("fixed discardable", h[4]);
    h[5] = GlobalAlloc(GMEM_MOVEABLE | GMEM_DISCARDABLE | GMEM_NOTIFY, 100);
    block("discardable notify", h[5]);
    obs("new discardable is the LRU head", lru_head() == arena(h[5]));
    h[6] = GlobalAlloc(GMEM_MOVEABLE | GMEM_SHARE, 100);
    block("share", h[6]);
    obs("share owner==module",
        A_OWNER(arena(h[6])) == (WORD)GetModuleHandle(MAKELP(0, entry_regs[E_DS] & ~1)));
    h[7] = GlobalAlloc(GMEM_MOVEABLE, 0x1000);
    block("moveable 1000h", h[7]);
    obs("fixed 0", GlobalAlloc(GMEM_FIXED, 0));
    h[8] = GlobalAlloc(GMEM_MOVEABLE, 0);
    discarded("moveable 0", h[8]);
    obs("moveable 0 handle odd", h[8] & 1);
    h[9] = GlobalAlloc(GMEM_MOVEABLE | GMEM_DISCARDABLE, 0);
    discarded("discardable 0", h[9]);
    GlobalFree(h[8]);
    GlobalFree(h[9]);
    walk_checks("allocated");

    /* huge blocks: one arena, a selector per 64K, only the first in the
       selector table */
    h[8] = GlobalAlloc(GMEM_MOVEABLE, 0x10000L);
    obs32("64K GlobalSize", GlobalSize(h[8]));
    obs32("64K limit", limit(h[8] | 1));
    obs32("64K tile 1 limit", limit((h[8] | 1) + inc));
    GlobalFree(h[8]);
    h[8] = GlobalAlloc(GMEM_MOVEABLE, 0x30000L);
    obs32("192K GlobalSize", GlobalSize(h[8]));
    for (i = 0; i < 4; i++) {
        WORD x = (h[8] | 1) + i * inc;
        obsn("192K tile limit high", i, HIWORD(limit(x)));
        obsn("192K tile limit low", i, LOWORD(limit(x)));
        obsn("192K tile base-first", i, HIWORD(base(x) - base(h[8] | 1)));
        obsn("192K tile selector table==arena", i, seltab(x) == arena(h[8]));
        obsn("192K tile GlobalHandle==h", i, LOWORD(GlobalHandle(x)) == h[8]);
    }
    GlobalFree(h[8]);

    /* locks */
    obs("lock discardable", LOWORD((DWORD)GlobalLock(h[3])));
    obs("discardable locked count", A_COUNT(arena(h[3])));
    obs("discardable locked GlobalFlags", GlobalFlags(h[3]));
    GlobalLock(h[3]);
    obs("unlock 2", GlobalUnlock(h[3]));
    obs("unlock 1", GlobalUnlock(h[3]));
    obs("unlock 0", GlobalUnlock(h[3]));
    GlobalLock(h[2]);
    obs("moveable locked count", A_COUNT(arena(h[2])));
    obs("moveable locked GlobalFlags", GlobalFlags(h[2]));
    obs("moveable unlock", GlobalUnlock(h[2]));
    K_GLOBALFIX(h[2]);
    obs("moveable fixed count", A_COUNT(arena(h[2])));
    obs("moveable fixed GlobalFlags", GlobalFlags(h[2]));
    obs("moveable fixed unlock", GlobalUnlock(h[2]));
    obs("moveable fixed count after unlock", A_COUNT(arena(h[2])));
    K_GLOBALUNFIX(h[2]);
    obs("moveable unfixed count", A_COUNT(arena(h[2])));
    K_GLOBALFIX(h[1]);
    obs("fixed fixed count", A_COUNT(arena(h[1])));
    obs("fixed fixed GlobalFlags", GlobalFlags(h[1]));
    K_GLOBALUNFIX(h[1]);
    GlobalLock(h[1]);
    obs("fixed locked count", A_COUNT(arena(h[1])));

    /* LRU */
    K_GLOBALLRUOLDEST(h[3]);
    obs("LRUOldest: tail", A_LPREV(lru_head()) == arena(h[3]));
    K_GLOBALLRUNEWEST(h[3]);
    obs("LRUNewest: head", lru_head() == arena(h[3]));
    GlobalReAlloc(h[2], 0, GMEM_MODIFY | GMEM_MOVEABLE | GMEM_DISCARDABLE);
    obs("MODIFY discardable: in LRU", A_LNEXT(arena(h[2])) != 0);
    obs("MODIFY discardable: head", lru_head() == arena(h[2]));
    obs("MODIFY discardable: GlobalFlags", GlobalFlags(h[2]));
    GlobalReAlloc(h[2], 0, GMEM_MODIFY | GMEM_MOVEABLE);
    obs("MODIFY not discardable: in LRU", A_LNEXT(arena(h[2])) != 0);
    obs("MODIFY not discardable: GlobalFlags", GlobalFlags(h[2]));

    /* discarding */
    obs("discard moveable", GlobalReAlloc(h[2], 0, GMEM_MOVEABLE));
    obs("discard fixed", GlobalReAlloc(h[1], 0, GMEM_MOVEABLE));
    obs("discard without GMEM_MOVEABLE", GlobalReAlloc(h[3], 0, 0));
    GlobalLock(h[3]);
    obs("discard locked", GlobalReAlloc(h[3], 0, GMEM_MOVEABLE));
    GlobalUnlock(h[3]);
    obs("discard discardable==h", GlobalReAlloc(h[3], 0, GMEM_MOVEABLE) == h[3]);
    discarded("discarded", h[3]);
    obs("realloc discarded==h", GlobalReAlloc(h[3], 50, GMEM_MOVEABLE) == h[3]);
    block("reallocated", h[3]);

    /* GlobalNotify */
    K_GLOBALNOTIFY((FARPROC)MAKELP(0x1234, 0x5678));
    obs("GlobalNotify: TDB 2Eh", W(t, 0x2E) == 0x5678 && W(t, 0x30) == 0x1234);
    {
        /* where it went (Pietrek's TDB has it at 2Eh) */
        WORD o, seen = 0xFFFF, ds = entry_regs[E_DS];
        for (o = 0; o < 0x1FD; o++)
            if (W(t, o) == 0x5678 && W(t, o + 2) == 0x1234)
                seen = o;
        obs_env("GlobalNotify: in the TDB at", seen);
        seen = 0xFFFF;
        for (o = 0; o < 0xFD; o++)
            if (W(pdb, o) == 0x5678 && W(pdb, o + 2) == 0x1234)
                seen = o;
        obs_env("GlobalNotify: in the PDB at", seen);
        seen = 0xFFFF;
        for (o = 0; o < 0x40; o++)
            if (W(ds, o) == 0x5678 && W(ds, o + 2) == 0x1234)
                seen = o;
        obs_env("GlobalNotify: in DGROUP at", seen);
    }
    np = MakeProcInstance((FARPROC)NOTIFY_PROC, (HINSTANCE)(entry_regs[E_DS] & ~1));
    K_GLOBALNOTIFY(np);
    obs("GlobalNotify(proc): TDB 2Eh", W(t, 0x2E) == LOWORD((DWORD)np) && W(t, 0x30) == HIWORD((DWORD)np));
    notify_n = 0;
    notify_ret = 0;
    obs("notify 0: discard", GlobalReAlloc(h[5], 0, GMEM_MOVEABLE));
    obs("notify 0: GlobalFlags", GlobalFlags(h[5]));
    nrec("notify 0", h[5]);
    notify_ret = 1;
    obs("notify 1: discard==h", GlobalReAlloc(h[5], 0, GMEM_MOVEABLE) == h[5]);
    obs("notify 1: GlobalFlags", GlobalFlags(h[5]));
    nrec("notify 1", h[5]);
    GlobalReAlloc(h[5], 100, GMEM_MOVEABLE);
    GlobalReAlloc(h[4], 0, GMEM_MOVEABLE);
    obs("plain discard: notify called", notify_n != 0);
    obs("fixed discardable: discarded", GlobalFlags(h[4]));
    notify_n = 0;
    notify_ret = 0;
    GlobalCompact(-1);
    obs("compact(-1), notify 0: GlobalFlags", GlobalFlags(h[5]));
    nrec("compact(-1), notify 0", h[5]);
    notify_ret = 1;
    GlobalCompact(-1);
    obs("compact(-1), notify 1: GlobalFlags", GlobalFlags(h[5]));
    nrec("compact(-1), notify 1", h[5]);

    /* reallocating */
    GlobalFree(h[7]);
    h[7] = GlobalAlloc(GMEM_FIXED, 0x100);
    h[8] = GlobalAlloc(GMEM_FIXED, 0x100);
    obs("fixed blocks adjacent", arena(h[8]) == A_NEXT(arena(h[7])));
    b0 = base(h[7]);
    obs("grow blocked fixed", GlobalReAlloc(h[7], 0x200, 0));
    obs("grow blocked fixed, GMEM_MOVEABLE==h", GlobalReAlloc(h[7], 0x200, GMEM_MOVEABLE) == h[7]);
    obs("grow blocked fixed: moved", base(h[7]) != b0);
    obs("grow blocked fixed: GlobalSize", (WORD)GlobalSize(h[7]));
    GlobalFree(h[8]);
    b0 = base(h[7]);
    obs("shrink==h", GlobalReAlloc(h[7], 0x20, 0) == h[7]);
    obs("shrink: GlobalSize", (WORD)GlobalSize(h[7]));
    obs("grow into free space==h", GlobalReAlloc(h[7], 0x100, 0) == h[7]);
    obs("grow into free space: same place", base(h[7]) == b0);
    obs("grow into free space: GlobalSize", (WORD)GlobalSize(h[7]));
    GlobalFree(h[7]);
    h[7] = GlobalAlloc(GMEM_MOVEABLE, 0x100);
    h[8] = GlobalAlloc(GMEM_MOVEABLE, 0x100);
    obs("moveable blocks adjacent", arena(h[8]) == A_NEXT(arena(h[7])));
    *(WORD FAR *)MAKELP(h[7] | 1, 0) = 0x1919;
    b0 = base(h[7] | 1);
    obs("grow blocked moveable==h", GlobalReAlloc(h[7], 0x200, 0) == h[7]);
    obs("grow blocked moveable: moved", base(h[7] | 1) != b0);
    obs("grow blocked moveable: contents", *(WORD FAR *)MAKELP(h[7] | 1, 0));

    /* compacting: a moveable block moves down into free space below it */
    b0 = base(h[8] | 1);
    *(WORD FAR *)MAKELP(h[8] | 1, 0) = 0x1920;
    GlobalFree(h[7]);
    v = GlobalCompact(0);
    obs("compact(0): block moved down", base(h[8] | 1) < b0);
    obs("compact(0): contents", *(WORD FAR *)MAKELP(h[8] | 1, 0));
    obs("compact(0) result low bits", LOWORD(v) & 0xF);
    walk_checks("compacted");

    /* GlobalWire */
    K_GLOBALFIX(h[8]);
    b0 = base(h[8] | 1);
    v = (DWORD)K_GLOBALWIRE(h[8]);
    obs("wire: sel:0", v == MAKELONG(0, h[8] | 1));
    obs("wire: count", A_COUNT(arena(h[8])));
    obs("wire: not higher", base(h[8] | 1) <= b0);
    obs("unwire", K_GLOBALUNWIRE(h[8]));
    obs("unwire: count", A_COUNT(arena(h[8])));
    obs("unwire again", K_GLOBALUNWIRE(h[8]));
    GlobalFree(h[8]);

    /* freeing */
    GlobalLock(h[3]);
    obs("free locked", GlobalFree(h[3]));
    obs("free locked: GlobalSize", (WORD)GlobalSize(h[3]));
    obs("free twice", GlobalFree(h[3]));
    obs("free bogus", GlobalFree(0x1234));
    obs("GlobalHandleNoRIP(bogus)", (WORD)K_GLOBALHANDLENORIP(0x1234));

    /* discarding when memory runs out: oldest first, locked blocks stay */
    for (i = 0; i < 4; i++)
        h[i] = GlobalAlloc(GMEM_MOVEABLE | GMEM_DISCARDABLE | GMEM_NOTIFY, 0x8000);
    GlobalLock(h[1]);
    notify_n = 0;
    while (nf < NFILL && (fill[nf] = GlobalAlloc(GMEM_FIXED, 0xFF00L)) != 0) {
        nf++;
        if (!first_disc && discarded_any(h)) {
            first_disc = nf;
            obs("full: oldest discarded first", (GlobalFlags(h[0]) & GMEM_DISCARDED) != 0);
        }
    }
    while (nf < NFILL && (fill[nf] = GlobalAlloc(GMEM_FIXED, 0x1000)) != 0) {
        nf++;
        if (!first_disc && discarded_any(h)) {
            first_disc = nf;
            obs("full: oldest discarded first", (GlobalFlags(h[0]) & GMEM_DISCARDED) != 0);
        }
    }
    obs_env("full: blocks", nf);
    obs("full: allocation failed", nf < NFILL);
    obs("full: discarded some", first_disc != 0);
    for (i = 0; i < 4; i++)
        obs(nm(nd[i], "GlobalFlags when full"), GlobalFlags(h[i]));
    obs("full: notify called", notify_n != 0);
    walk_checks("full");
    while (nf)
        GlobalFree(fill[--nf]);
    for (i = 0; i < 4; i++)
        GlobalFree(h[i]);
    for (i = 4; i < 7; i++)
        GlobalFree(h[i]);
    GlobalFree(h[0]);
    K_GLOBALNOTIFY(NULL);
    walk_checks("emptied");
    v = K_GETFREESPACE(0);
    obs("GetFreeSpace low bits", LOWORD(v) & 0xF);
}

int __cdecl tmain(void)
{
    DWORD gm = K_GLOBALMASTERHANDLE();
    HGLOBAL h;

    bm = HIWORD(gm);
    pdb = LOWORD(K_GETCURRENTPDB());
    gsl = (GSL)GetProcAddress(GetModuleHandle("KERNEL"), MAKEINTRESOURCE(188));
    if (limit(W(bm, 6)) != 0xF) {
        /* KRNL386: arenas are 32-bit offsets into the Burgermaster */
        out("TK19: not a KRNL286 heap, nothing probed\r\n");
        return 0;
    }
    obs("GlobalMasterHandle: handle==selector", LOWORD(gm) == bm);
    dump("BM", bm, 0, 0x24);
    obs("BM check", W(bm, 0));
    obs("BM freeze", W(bm, 2));
    obs("BM hdelta", W(bm, 0x12));
    obs("BM 24h", W(bm, 0x24));
    obs("BM 26h", W(bm, 0x26));
    obs("BM selector table", seltab(bm) == A_NEXT(first()));
    obs("BM arena owner", A_OWNER(A_NEXT(first())));
    obs("BM arena count", A_COUNT(A_NEXT(first())));
    obs("BM arena handle==selector", A_HANDLE(A_NEXT(first())) == bm);
    obs("BM GlobalSize>=selector table", GlobalSize(bm) >= SELTAB + 0x3FFC);
    obs("first sentinel prev==itself", A_PREV(first()) == first());
    obs("first sentinel count", A_COUNT(first()));
    obs("first sentinel owner", A_OWNER(first()));
    obs("first sentinel size", A_SIZE(first()));
    obs("first sentinel 0Ch", A_LPREV(first()));
    obs("last sentinel next==itself", A_NEXT(last()) == last());
    obs("last sentinel count", A_COUNT(last()));
    obs("last sentinel size", A_SIZE(last()));
    obs("last sentinel 0Eh", A_LNEXT(last()));
    {
        FARPROC hook = GetProcAddress(GetModuleHandle("KERNEL"), "THHOOK");
        obs_env("THHOOK exported", hook != NULL);
        if (hook) {
            WORD s = HIWORD((DWORD)hook), o = LOWORD((DWORD)hook);
            obs("THHOOK 00h==handle", W(s, o) == LOWORD(gm));
            obs("THHOOK 02h==selector", W(s, o + 2) == bm);
            obs("THHOOK SelTableStart", W(s, o + 0x18));
            obs("THHOOK SelTableStart high", W(s, o + 0x1A));
        }
    }
    walk_checks("start");
    tests();
    h = GlobalAlloc(GMEM_MOVEABLE, 100);
    toolhelp(h);
    GlobalFree(h);
    out("TK19 done, failures=");
    outhex((WORD)failures);
    out("\r\n");
    return failures;
}
