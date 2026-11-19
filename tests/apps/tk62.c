/*
 * tk62.c - the KERNEL audit (docs/notes/kernel-audit-triage.md): its
 * critical findings as regressions, and the behaviour its other findings
 * would change, observed on Windows 3.1 before w16 changes it.
 *
 * Regressions (w16 corrupted its heap or faulted in KERNEL before):
 *  - "chunk" (C1): TK62A.EXE (tk62a.c) ends with a free block in front of
 *    its last block in a piece of the heap of their own; KERNEL then
 *    read a freed arena while freeing the task's blocks.
 *  - "big" (C2, H1): a block of FFFD0h bytes (its piece of the heap held
 *    a free arena whose size wrapped), and sizes past 4 GB less 15.
 *  - "ldt" (C4): GlobalReAlloc growing a block in place with no selector
 *    left for the rest of the free space after it (KRNL286): it made the
 *    block the whole free space, over other selectors.
 *  - "dosalloc" (H2): GlobalDosAlloc of 1 MB and more fails (w16 asked
 *    DPMI for the size's low 16 bits of paragraphs and used the rest).
 *  - "badne" (H9): TKBADNE.DLL, whose entry table length runs its tables
 *    past 64K, fails to load (w16 read past its copy of them).
 *  - "hdelta" (H6): LocalHandleDelta(4000h), then moveable blocks until
 *    a new handle table is needed: its size wrapped and the handles were
 *    written past it. Windows 3.1 ends there itself (both kernels), so
 *    it comes last.
 *  - "bigdg" (H7): TKBIGDG.DLL, whose DGROUP holds FFF8h bytes of file
 *    data, more than the FFF0h-byte segment w16 makes (it read past it);
 *    TKDG.DLL, 100h bytes, loads.
 *  - "entries" (C5): TKENT.DLL (tools/mkne.py --entries 13300), whose
 *    entry table fits its file but not the module table (fails), and
 *    TKENT2.DLL (12000 entries, loads).
 * Observations for the other findings (UNVERIFIED in w16 until checked
 * against the reference tables): IsBad*Ptr of NULL with 0 bytes and of an
 * expand-down segment, SelectorAccessRights on AllocSelector(0),
 * IsBadStringPtr past a 64K segment, AddAtom's count past FFFFh,
 * PostEvent's count, SetTaskSignalProc and GetAppCompatFlags of a
 * non-task, LockSegment(-1), GetCodeHandle of KERNEL, Catch's buffer,
 * KERNEL's AnsiUpper/Lower (79/80) for 80h-FFh, OpenFile's drive
 * letters and error codes, WinExec of an empty line, profile strings
 * with quotes, leading blanks and an empty key.
 * Second round (the audit's UNSURE items): OpenFile's nErrCode with no
 * handle left and with a directory of the name ("openerr", medium 37),
 * GetDriveType of a drive where IOCTL 4408h fails ("drivetype", F: a
 * DOSBox floppy mount, medium 39), LocalCompact(-1) over 48 blocks
 * ("lcompact", L4), GetFreeSpace and a fixed discardable block
 * ("freespace", L5), DirectResAlloc's owner and flags ("directres",
 * L27), the PATH variable's name in lower case ("envcase", L30),
 * GetTempFileName with drive 80h and a directory in the way
 * ("tempname", L31), OF_SEARCH ("ofsearch", L32), and KERNEL's own
 * segments as heap blocks ("kseg", medium 33: w16's are not yet).
 *
 * Runs in make test beside TK61 (tests/run-tests.sh); the reference run:
 * tests/tk62ref.sh.
 */
#include <windows.h>

#define TK_REF "tk62ref.h"
#define TK_REF286 "tk62ref286.h"
#include "tklog.h"
#include "tk62.h"

extern WORD __cdecl sel_ok(WORD sel);
extern WORD __cdecl sel_ar(WORD sel);
extern WORD __cdecl cur_ds(void);

WORD use386;                    /* tk17s.asm */
static tk62sh sh;
static char helper[160];

static FARPROC kord(WORD n)
{
    return GetProcAddress(GetModuleHandle("KERNEL"), MAKEINTRESOURCE(n));
}

static void hex4(char *p, WORD v)
{
    int i;
    for (i = 3; i >= 0; i--) {
        p[i] = "0123456789ABCDEF"[v & 15];
        v >>= 4;
    }
}

static void step(const char *name)
{
    out("== ");
    out(name);
    out("\r\n");
}

static WORD sel_of(HGLOBAL h)
{
    WORD s = SELECTOROF(GlobalLock(h));
    GlobalUnlock(h);
    return s;
}

/* ---- regressions ---- */

/* TK62A's command line: its path, then sh's DS and offset (and more) */
static void helper_cmd(char *cmd, const char *more)
{
    WORD n, i;
    n = GetModuleFileName((HINSTANCE)(entry_regs[E_DS] & ~1u), helper, sizeof helper - 12);
    for (i = n; i && helper[i - 1] != '\\'; i--)
        ;
    lstrcpy(helper + i, "TK62A.EXE");
    lstrcpy(cmd, helper);
    n = lstrlen(cmd);
    cmd[n++] = ' ';
    hex4(cmd + n, cur_ds());
    cmd[n + 4] = ' ';
    hex4(cmd + n + 5, (WORD)(void NEAR *)&sh);
    cmd[n + 9] = 0;
    lstrcat(cmd, more);
}

static void chunk(void)
{
    WORD n, i, t;
    char cmd[160];
    HGLOBAL h;
    step("chunk");
    n = GetModuleFileName((HINSTANCE)(entry_regs[E_DS] & ~1u), helper, sizeof helper - 12);
    for (i = n; i && helper[i - 1] != '\\'; i--)
        ;
    lstrcpy(helper + i, "TK62A.EXE");
    lstrcpy(cmd, helper);
    n = lstrlen(cmd);
    cmd[n++] = ' ';
    hex4(cmd + n, cur_ds());
    cmd[n + 4] = ' ';
    hex4(cmd + n + 5, (WORD)(void NEAR *)&sh);
    cmd[n + 9] = 0;
    t = WinExec(cmd, SW_SHOWNORMAL);
    obs("chunk: WinExec ok", t > 32);
    if (t <= 32)
        return;
    for (i = 0; i < 2000 && !sh.done; i++)
        Yield();
    for (i = 0; i < 2000 && IsTask((HTASK)sh.task); i++)
        Yield();
    obs("chunk: ran", sh.done);
    obs("chunk: big", sh.big);
    obs_env("chunk: blocks side by side", sh.hit);
    obs("chunk: ended", !IsTask((HTASK)sh.task));
    h = GlobalAlloc(GMEM_MOVEABLE, 0x1000);
    obs("chunk: alloc after", h != 0);
    GlobalFree(h);
}

static void big(void)
{
    HGLOBAL h;
    BYTE FAR *p, FAR *q;
    WORD inc = OFFSETOF(GetProcAddress(GetModuleHandle("KERNEL"), "__AHINCR"));
    step("big");
    h = GlobalAlloc(GMEM_MOVEABLE, 0xFFFD0UL);
    obs("big: FFFD0 ok", h != 0);
    if (h) {
        check(GlobalSize(h) >= 0xFFFD0UL, "big: size");
        p = (BYTE FAR *)GlobalLock(h);
        q = (BYTE FAR *)MAKELP(SELECTOROF(p) + 15 * inc, 0xFFCF);
        *q = 0x5A;
        *p = 0xA5;
        check(*q == 0x5A && *p == 0xA5, "big: ends");
        GlobalUnlock(h);
        GlobalFree(h);
    }
    h = GlobalAlloc(GMEM_MOVEABLE, 0xFFFFFFFFUL);
    obs("big: FFFFFFFF ok", h != 0);
    GlobalFree(h);
    h = GlobalAlloc(GMEM_MOVEABLE, 0xFFFFFFF1UL);
    obs("big: FFFFFFF1 ok", h != 0);
    GlobalFree(h);
    h = GlobalAlloc(GMEM_MOVEABLE, 0x1000);
    obs("big: alloc after", h != 0);
    GlobalFree(h);
}

#define MAXSEL 9000

static void ldt(void)
{
    HGLOBAL hs, a, b, r;
    WORD FAR *sels;
    WORD n = 0, i;
    DWORD sz0, sz1;
    step("ldt");
    hs = GlobalAlloc(GMEM_FIXED, MAXSEL * 2UL);
    a = GlobalAlloc(GMEM_MOVEABLE, 0x100);
    b = GlobalAlloc(GMEM_MOVEABLE, 0x1000);
    if (!hs || !a || !b) {
        out("ldt: no memory\r\n");
        return;
    }
    sels = (WORD FAR *)GlobalLock(hs);
    GlobalFree(b);                      /* free space right after a */
    sz0 = GlobalSize(a);
    while (n < MAXSEL && (sels[n] = AllocSelector(0)) != 0)
        n++;
    r = GlobalReAlloc(a, 0x200, 0);     /* in place only */
    sz1 = GlobalSize(a);
    for (i = 0; i < n; i++)
        FreeSelector(sels[i]);
    obs_env("ldt: selectors", n);
    obs_env("ldt: realloc ok", r != 0);
    /* failed, or the size asked for: not the free space after it */
    check(sz1 == sz0 || (sz1 >= 0x200 && sz1 < 0x400), "ldt: size");
    GlobalFree(a);
    GlobalUnlock(hs);
    GlobalFree(hs);
    a = GlobalAlloc(GMEM_MOVEABLE, 0x1000);
    obs("ldt: alloc after", a != 0);
    GlobalFree(a);
}

static void entries(void)
{
    HINSTANCE h;
    FARPROC p;
    step("entries");
    h = LoadLibrary("TKENT.DLL");
    obs("entries: 13300 ok", (UINT)h > 32);
    if ((UINT)h > 32)
        FreeLibrary(h);
    h = LoadLibrary("TKENT2.DLL");
    /* 3.1 failed this one too (KRNL386; KRNL286 ended Windows with the
       first): w16 loads what fits its module table */
    obs_env("entries: 12000 ok", (UINT)h > 32);
    if ((UINT)h > 32) {
        p = GetProcAddress(h, MAKEINTRESOURCE(12000));
        obs_env("entries: 12000 proc lo", OFFSETOF(p));
        obs_env("entries: 1 proc lo", OFFSETOF(GetProcAddress(h, MAKEINTRESOURCE(1))));
        FreeLibrary(h);
    }
}

static void dosalloc(void)
{
    DWORD d;
    step("dosalloc");
    d = GlobalDosAlloc(0x100000UL);
    check(d == 0, "dosalloc: 1 MB fails");
    if (d)
        GlobalDosFree(LOWORD(d));
    d = GlobalDosAlloc(0x10FFF0UL);
    check(d == 0, "dosalloc: 10FFF0h fails");
    if (d)
        GlobalDosFree(LOWORD(d));
    d = GlobalDosAlloc(0x100);
    check(d != 0, "dosalloc: 100h after");
    if (d)
        GlobalDosFree(LOWORD(d));
}

static void badne(void)
{
    HINSTANCE h;
    step("badne");
    h = LoadLibrary("TKBADNE.DLL");
    check((UINT)h <= 32, "badne: fails");
    if ((UINT)h > 32)
        FreeLibrary(h);
    h = GlobalAlloc(GMEM_MOVEABLE, 0x1000);
    check(h != 0, "badne: alloc after");
    GlobalFree(h);
}

#define NHD 80
UINT WINAPI LocalHandleDelta(UINT);           /* KERNEL.310 */

static void hdelta(void)
{
    UINT old, i, n = 0;
    HLOCAL h[NHD];
    step("hdelta");
    old = LocalHandleDelta(0);
    LocalHandleDelta(0x4000);
    for (i = 0; i < NHD; i++)
        if ((h[i] = LocalAlloc(LMEM_MOVEABLE, 2)) != 0)
            n++;
    LocalHandleDelta(old);
    obs_env("hdelta: blocks", n);
    for (i = 0; i < NHD; i++)
        if (h[i])
            LocalFree(h[i]);
    for (i = 0; i < NHD; i++)
        h[i] = LocalAlloc(LMEM_MOVEABLE, 2);
    check(h[NHD - 1] != 0, "hdelta: allocs after");
    for (i = 0; i < NHD; i++)
        if (h[i])
            LocalFree(h[i]);
}

static void bigdg(void)
{
    HINSTANCE h;
    HGLOBAL g;
    step("bigdg");
    h = LoadLibrary("TKBIGDG.DLL");
    obs("bigdg: FFF8 ok", (UINT)h > 32);
    if ((UINT)h > 32)
        FreeLibrary(h);
    h = LoadLibrary("TKDG.DLL");
    obs("bigdg: 100 ok", (UINT)h > 32);
    if ((UINT)h > 32)
        FreeLibrary(h);
    g = GlobalAlloc(GMEM_MOVEABLE, 0x1000);
    check(g != 0, "bigdg: alloc after");
    GlobalFree(g);
}

/* H11: TKWA imports TKWB; the order their WEPs run in when both go */
static char weplog[8];

static void wep_order(const char *name)
{
    char nm[40];
    lstrcpy(nm, name);
    lstrcat(nm, " 1st");
    obs(nm, (BYTE)weplog[0]);
    lstrcpy(nm, name);
    lstrcat(nm, " 2nd");
    obs(nm, (BYTE)weplog[1]);
}

static void wep(void)
{
    HINSTANCE ha, hb;
    void (FAR PASCAL *set)(char FAR *);
    step("wep");
    /* (3.1 in 386 Enhanced mode ended Windows in these LoadLibrary calls,
       twice; not looked into: Standard mode and KRNL286 agree) */
    if (GetWinFlags() & WF_ENHANCED)
        return;
    /* TKWB first, then TKWA (TKWB comes first in the module list): both
       reach 0 with the last FreeLibrary */
    weplog[0] = 0;
    hb = LoadLibrary("TKWB.DLL");
    ha = LoadLibrary("TKWA.DLL");
    obs("wep: loaded", (UINT)ha > 32 && (UINT)hb > 32);
    if ((UINT)ha <= 32 || (UINT)hb <= 32)
        return;
    set = (void (FAR PASCAL *)(char FAR *))GetProcAddress(ha, MAKEINTRESOURCE(1));
    set(weplog);
    FreeLibrary(hb);
    obs("wep: B freed first, log", (BYTE)weplog[0]);
    FreeLibrary(ha);
    wep_order("wep: B loaded first,");
    /* TKWA alone (it loads TKWB after itself) */
    weplog[0] = 0;
    ha = LoadLibrary("TKWA.DLL");
    if ((UINT)ha <= 32)
        return;
    set = (void (FAR PASCAL *)(char FAR *))GetProcAddress(ha, MAKEINTRESOURCE(1));
    set(weplog);
    FreeLibrary(ha);
    wep_order("wep: A alone,");
}

/* Medium 25: LockCurrentTask keeps other tasks from running? */
static void lock(void)
{
    WORD i, t, c0, c1, c2;
    char cmd[160];
    WORD (FAR PASCAL *lct)(BOOL) = (WORD (FAR PASCAL *)(BOOL))kord(33);
    WORD (FAR PASCAL *itl)(void) = (WORD (FAR PASCAL *)(void))kord(122);
    step("lock");
    sh.done = sh.stop = sh.count = 0;
    helper_cmd(cmd, " L");
    t = WinExec(cmd, SW_SHOWNORMAL);
    obs("lock: WinExec ok", t > 32);
    if (t <= 32)
        return;
    for (i = 0; i < 20; i++)
        Yield();
    c0 = sh.count;
    obs("lock: ret", lct(TRUE) == (WORD)GetCurrentTask());
    obs("lock: IsTaskLocked is us", itl() == (WORD)GetCurrentTask());
    for (i = 0; i < 20; i++)
        Yield();
    c1 = sh.count;
    lct(FALSE);
    obs("lock: IsTaskLocked after unlock", itl());
    for (i = 0; i < 20; i++)
        Yield();
    c2 = sh.count;
    obs("lock: helper ran before", c0 != 0);
    obs("lock: helper ran while locked", c1 != c0);
    obs("lock: helper ran after", c2 != c1);
    sh.stop = 1;
    for (i = 0; i < 2000 && IsTask((HTASK)sh.task); i++)
        Yield();
    obs("lock: helper ended", !IsTask((HTASK)sh.task));
}

/* L24: what Catch writes where (DS = SS here; CS, SP, BP, SI, DI) */
extern WORD __cdecl cur_cs(void);

static void catchmap(void)
{
    int buf[12];
    WORD i, ds = cur_ds(), cs = cur_cs(), sp = (WORD)(void NEAR *)buf;
    static char nm[] = "catchmap: word N";
    step("catchmap");
    for (i = 0; i < 12; i++)
        buf[i] = (int)0xCCCC;
    if (Catch(buf) != 0)
        return;
    for (i = 0; i < 10; i++) {
        WORD w = (WORD)buf[i], k;
        if (w == 0xCCCC)
            k = 0;              /* not written */
        else if (w == ds)
            k = 1;              /* DS or SS */
        else if (w == cs)
            k = 2;              /* CS */
        else if (w > sp && w < sp + 0x100)
            k = 3;              /* near our stack: SP or BP */
        else
            k = 4;
        nm[15] = (char)('0' + i);
        obs(nm, k);
    }
}

/* L3: a discarded block reallocated by its selector: which handle */
static void discsel(void)
{
    HGLOBAL h, r;
    step("discsel");
    h = GlobalAlloc(GMEM_MOVEABLE | GMEM_DISCARDABLE, 0x100);
    if (!h)
        return;
    GlobalDiscard(h);
    obs("discsel: discarded", (GlobalFlags(h) & GMEM_DISCARDED) != 0);
    r = GlobalReAlloc((HGLOBAL)((WORD)h | 1), 0x100, GMEM_MOVEABLE);
    obs("discsel: ok", r != 0);
    obs("discsel: odd", (WORD)r & 1);
    obs("discsel: same", r && ((WORD)r | 1) == ((WORD)h | 1));
    GlobalFree(r ? r : h);
}

/* ---- observations ---- */

static void badptr(void)
{
    WORD s, ds = cur_ds();
    UINT (FAR PASCAL *sar)(UINT, UINT, UINT) =
        (UINT (FAR PASCAL *)(UINT, UINT, UINT))kord(196);
    step("badptr");
    obs("badptr: read NULL 0", IsBadReadPtr(NULL, 0));
    obs("badptr: write NULL 0", IsBadWritePtr(NULL, 0));
    obs("badptr: hugeread NULL 0", IsBadHugeReadPtr(NULL, 0));
    obs("badptr: hugewrite NULL 0", IsBadHugeWritePtr(NULL, 0));
    obs("badptr: string NULL 0", IsBadStringPtr(NULL, 0));
    obs("badptr: read ds:0 0", IsBadReadPtr(MAKELP(ds, 0), 0));
    /* an expand-down copy of DS: offsets above its limit are valid */
    s = AllocSelector(ds);
    sar(s, 1, (sar(s, 0, 0) & ~0x1E) | 0x16);
    SetSelectorLimit(s, 0x0FFF);
    obs("badptr: ed rights", sar(s, 0, 0));
    obs("badptr: ed read 10", IsBadReadPtr(MAKELP(s, 0x10), 1));
    obs("badptr: ed read 2000", IsBadReadPtr(MAKELP(s, 0x2000), 1));
    obs("badptr: ed read FFF0 10", IsBadReadPtr(MAKELP(s, 0xFFF0), 0x10));
    obs("badptr: ed write 2000", IsBadWritePtr(MAKELP(s, 0x2000), 0x10));
    obs("badptr: ed string 2000", IsBadStringPtr(MAKELP(s, 0x2000), 0x10));
    FreeSelector(s);
    /* AllocSelector(0) given rights (a data segment) */
    s = AllocSelector(0);
    SetSelectorBase(s, GetSelectorBase(ds));
    SetSelectorLimit(s, 0xFF);
    sar(s, 1, 0x12);
    /* (what LAR says differs: w16's is a not-present data segment, 3.1's
       a system descriptor, ARCHITECTURE.md 9.2) */
    obs_env("rights: raw lar ok", sel_ok(s));
    obs_env("rights: raw ar", sel_ar(s) & 0xFF);
    obs("rights: raw read", IsBadReadPtr(MAKELP(s, 0), 0x10));
    FreeSelector(s);
}

static void strptr(void)
{
    HGLOBAL h;
    BYTE FAR *p;
    WORD i = 0;
    step("strptr");
    h = GlobalAlloc(GMEM_FIXED, 0x10000UL);
    if (!h)
        return;
    p = (BYTE FAR *)GlobalLock(h);
    do
        p[i] = 'A';
    while (++i);
    obs("strptr: 64K FFFF", IsBadStringPtr((LPSTR)p, 0xFFFF));
    obs("strptr: 64K+1 FFFF", IsBadStringPtr((LPSTR)p + 1, 0xFFFF));
    obs("strptr: 64K 100", IsBadStringPtr((LPSTR)p, 0x100));
    obs("strptr: 64K FFFE", IsBadStringPtr((LPSTR)p, 0xFFFE));
    p[0] = 'a';
    p[1] = 'b';
    p[2] = 'c';
    p[3] = 0;
    obs("strptr: abc 2", IsBadStringPtr((LPSTR)p, 2));
    obs("strptr: abc 3", IsBadStringPtr((LPSTR)p, 3));
    obs("strptr: abc 4", IsBadStringPtr((LPSTR)p, 4));
    obs("strptr: abc 0", IsBadStringPtr((LPSTR)p, 0));
    p[0] = 0;
    obs("strptr: empty 0", IsBadStringPtr((LPSTR)p, 0));
    GlobalUnlock(h);
    GlobalFree(h);
}

static void atoms(void)
{
    ATOM a = 0;
    DWORD i;
    step("atoms");
    for (i = 0; i < 0x101; i++)
        a = AddAtom("TK62BYTE");
    DeleteAtom(a);
    obs("atoms: 101h adds, found after 1 delete", FindAtom("TK62BYTE") != 0);
    for (i = 0; i < 0x200 && FindAtom("TK62BYTE"); i++)
        DeleteAtom(a);
    for (i = 0; i < 0x10001UL; i++)
        a = AddAtom("TK62ATOM");
    obs("atoms: added", a != 0);
    DeleteAtom(a);
    obs("atoms: found after 1 delete", FindAtom("TK62ATOM") != 0);
    DeleteAtom(a);
    obs("atoms: found after 2 deletes", FindAtom("TK62ATOM") != 0);
    for (i = 0; i < 4 && FindAtom("TK62ATOM"); i++)
        DeleteAtom(a);
    obs("atoms: deletes to go", (WORD)i);
}

static void events(void)
{
    void (FAR PASCAL *post)(HTASK) = (void (FAR PASCAL *)(HTASK))kord(31);
    WORD t = (WORD)GetCurrentTask();
    WORD FAR *ev = (WORD FAR *)MAKELP(t, 6);
    DWORD i;
    step("events");
    *ev = 0;
    for (i = 0; i < 0x10001UL; i++)
        post((HTASK)t);
    obs("events: 10001h posts", *ev);
    *ev = 0;
}

static void sigproc(void)
{
    DWORD (FAR PASCAL *ssp)(HTASK, DWORD) = (DWORD (FAR PASCAL *)(HTASK, DWORD))kord(38);
    DWORD (FAR PASCAL *gacf)(HTASK) = (DWORD (FAR PASCAL *)(HTASK))kord(354);
    HGLOBAL h = GlobalAlloc(GMEM_FIXED | GMEM_ZEROINIT, 0x100);
    WORD s = sel_of(h);
    DWORD r;
    step("sigproc");
    *(DWORD FAR *)MAKELP(s, 0x4E) = 0x11223344UL;
    *(DWORD FAR *)MAKELP(s, 0x2A) = 0x55667788UL;
    r = ssp((HTASK)s, 0x12345678UL);
    obs("sigproc: ret lo", LOWORD(r));
    obs("sigproc: ret hi", HIWORD(r));
    obs("sigproc: 2A lo", LOWORD(*(DWORD FAR *)MAKELP(s, 0x2A)));
    r = gacf((HTASK)s);
    obs("compat: lo", LOWORD(r));
    obs("compat: hi", HIWORD(r));
    GlobalFree(h);
}

static void lockseg(void)
{
    WORD ds = cur_ds();
    HGLOBAL r;
    step("lockseg");
    r = LockSegment((UINT)-1);
    obs("lockseg: -1 ret is ds", r && ((WORD)r | 1) == (ds | 1));
    obs("lockseg: -1 ret 0", r == 0);
    obs("lockseg: flags after", GlobalFlags((HGLOBAL)(ds & ~1u)) & 0xFF);
    r = ((HGLOBAL (FAR PASCAL *)(UINT))kord(24))((UINT)-1);
    obs("unlockseg: -1 ret is ds", r && ((WORD)r | 1) == (ds | 1));
    obs("unlockseg: flags after", GlobalFlags((HGLOBAL)(ds & ~1u)) & 0xFF);
}

static void codehandle(void)
{
    FARPROC p = GetProcAddress(GetModuleHandle("KERNEL"), "GlobalAlloc");
    HGLOBAL h = GetCodeHandle(p);
    step("codehandle");
    obs("codehandle: proc found", p != NULL);
    obs_env("codehandle: proc sel", SELECTOROF(p));
    obs_env("codehandle: handle", (WORD)h);
    /* (w16: 0, GlobalHandle's for KERNEL's code, which is no block of
       its heap: the KERNEL audit's medium 33, open) */
    obs_env("codehandle: kernel ok", h != 0);
    obs_env("codehandle: kernel is sel", h && ((WORD)h | 1) == (SELECTOROF(p) | 1));
}

static void catchbuf(void)
{
    int buf[12];
    WORD i, n = 0, last = 0;
    step("catch");
    for (i = 0; i < 12; i++)
        buf[i] = (int)0xCCCC;
    if (Catch(buf) == 0) {
        for (i = 0; i < 12; i++)
            if (buf[i] != (int)0xCCCC) {
                n++;
                last = i;
            }
        obs("catch: words written", n);
        /* (3.1's last word is the ninth, w16's the eighth: what each word
       holds is not probed yet) */
    obs_env("catch: last written", last);
    }
}

static void ansi(void)
{
    DWORD (FAR PASCAL *upr)(DWORD) = (DWORD (FAR PASCAL *)(DWORD))kord(79);
    DWORD (FAR PASCAL *lwr)(DWORD) = (DWORD (FAR PASCAL *)(DWORD))kord(80);
    WORD c, nu = 0, nl = 0;
    static char up[] = "ansi: up XX", lo[] = "ansi: low XX";
    char t[5];
    step("ansi");
    for (c = 0x80; c < 0x100; c++) {
        WORD u = LOWORD(upr(c)), l = LOWORD(lwr(c));
        hex4(t, c);
        up[9] = lo[10] = t[2];
        up[10] = lo[11] = t[3];
        if (u != c) {
            obs(up, u);
            nu++;
        }
        if (l != c) {
            obs(lo, l);
            nl++;
        }
    }
    obs("ansi: up changed", nu);
    obs("ansi: low changed", nl);
}

static void files(void)
{
    OFSTRUCT of;
    HFILE f;
    step("files");
    of.nErrCode = 0xEEEE;
    f = OpenFile("1:\\TK62.TXT", &of, OF_PARSE);
    obs("files: 1: ret", f == HFILE_ERROR ? 0xFFFF : 0);
    obs("files: 1: err", of.nErrCode);
    of.nErrCode = 0xEEEE;
    f = OpenFile("@:TK62.TXT", &of, OF_PARSE);
    obs("files: @: ret", f == HFILE_ERROR ? 0xFFFF : 0);
    obs("files: @: err", of.nErrCode);
    of.nErrCode = 0xEEEE;
    f = OpenFile("A:B:C", &of, OF_PARSE);
    obs("files: A:B:C ret", f == HFILE_ERROR ? 0xFFFF : 0);
    f = OpenFile("COM1:", &of, OF_PARSE);
    obs("files: COM1: ret", f == HFILE_ERROR ? 0xFFFF : 0);
    f = OpenFile("FOO:", &of, OF_PARSE);
    obs("files: FOO: ret", f == HFILE_ERROR ? 0xFFFF : 0);
    f = OpenFile("[:FOO", &of, OF_PARSE);
    obs("files: [: ret", f == HFILE_ERROR ? 0xFFFF : 0);
    f = OpenFile("z:FOO", &of, OF_PARSE);
    obs("files: z: ret", f == HFILE_ERROR ? 0xFFFF : 0);
    f = _lcreat("C:\\TK62RO.TMP", 1);   /* read-only */
    if (f != HFILE_ERROR)
        _lclose(f);
    of.nErrCode = 0xEEEE;
    f = OpenFile("C:\\TK62RO.TMP", &of, OF_WRITE);
    obs("files: read-only write ret", f == HFILE_ERROR ? 0xFFFF : 0);
    obs("files: read-only write err", of.nErrCode);
    if (f != HFILE_ERROR)
        _lclose(f);
}

static void winexec(void)
{
    UINT r;
    step("winexec");
    r = WinExec("", SW_SHOWNORMAL);
    obs("winexec: empty", r < 32 ? r : 0x20);
    r = WinExec("   ", SW_SHOWNORMAL);
    obs("winexec: blanks", r < 32 ? r : 0x20);
}

static void profile(void)
{
    static const char ini[] = "C:\\TK62.INI";
    static const char text[] = "[e]\r\n=v\r\nk2=x\r\n[q]\r\nk=\"in\"\r\nb=   lead\r\n";
    char buf[64];
    HFILE f;
    WORD n;
    step("profile");
    f = _lcreat(ini, 0);
    if (f == HFILE_ERROR)
        return;
    _lwrite(f, text, sizeof text - 1);
    _lclose(f);
    n = GetPrivateProfileString("e", NULL, "", buf, sizeof buf, ini);
    obs("profile: keys n", n);
    obs("profile: keys [0]", (BYTE)buf[0]);
    obs("profile: keys [1]", (BYTE)buf[1]);
    obs("profile: keys [2]", (BYTE)buf[2]);
    n = GetPrivateProfileString("q", "k", "", buf, sizeof buf, ini);
    obs("profile: quoted n", n);
    obs("profile: quoted [0]", (BYTE)buf[0]);
    n = GetPrivateProfileString("q", "none", "\"dq\"", buf, sizeof buf, ini);
    obs("profile: quoted default n", n);
    obs("profile: quoted default [0]", (BYTE)buf[0]);
    n = GetPrivateProfileString("q", "b", "", buf, sizeof buf, ini);
    obs("profile: lead n", n);
    obs("profile: lead [0]", (BYTE)buf[0]);
    WritePrivateProfileString("q", "w", "  two", ini);
    n = GetPrivateProfileString("q", "w", "", buf, sizeof buf, ini);
    obs("profile: written lead n", n);
    obs("profile: written lead [0]", (BYTE)buf[0]);
    WritePrivateProfileString(NULL, NULL, NULL, ini);   /* flush */
    n = GetPrivateProfileString("q", "w", "", buf, sizeof buf, ini);
    obs("profile: reread lead n", n);
    obs("profile: reread lead [0]", (BYTE)buf[0]);
}

/* ---- the audit's UNSURE items (second round) ---- */

/* INT 21h with DS:DX = a string in our DGROUP: AH=39h MD, 3Ah RD, 41h
   DEL; 0 or the DOS error */
static WORD dos_dx(WORD ax, WORD dx);
#pragma aux dos_dx = "int 21h" "jc short l1" "xor ax, ax" "l1:" \
    parm [ax] [dx] value [ax] modify [bx cx dx si di es];
static char dpath[160];
/* AH=47h: the current directory of the current drive to DS:SI */
static WORD dos_cwd(WORD si);
#pragma aux dos_cwd = "mov ah, 47h" "xor dl, dl" "int 21h" "jc short l2" \
    "xor ax, ax" "l2:" parm [si] value [ax] modify [bx cx dx di es];

static WORD dos_path(WORD ax, const char FAR *p)
{
    lstrcpy(dpath, p);
    if (SELECTOROF((char FAR *)dpath) != cur_ds())
        return 0xFFFF;
    return dos_dx(ax, (WORD)(void NEAR *)dpath);
}

/* lstrcmpi without USER (make test has none): 0 when equal */
static int cmp_ci(const char FAR *a, const char FAR *b)
{
    for (;; a++, b++) {
        char x = *a, y = *b;
        if (x >= 'a' && x <= 'z')
            x -= 32;
        if (y >= 'a' && y <= 'z')
            y -= 32;
        if (x != y)
            return 1;
        if (!x)
            return 0;
    }
}

/* is p the Windows directory's file `name`? */
static int in_windir(const char FAR *p, const char *name)
{
    char w[160];
    WORD n = GetWindowsDirectory(w, sizeof w);
    if (n && w[n - 1] != '\\')
        w[n++] = '\\';
    w[n] = 0;
    lstrcat(w, name);
    return cmp_ci(w, p) == 0;
}

/* Medium 37: OpenFile's nErrCode for real DOS errors, with a path and
   searched for */
#define NFH 300
static void openerr(void)
{
    OFSTRUCT of;
    HFILE f, fh[NFH];
    WORD n = 0, i;
    static char cwd[80];
    step("openerr");
    f = _lcreat("C:\\TK62OE.TMP", 0);
    if (f == HFILE_ERROR)
        return;
    _lclose(f);
    /* the current directory C:\ (not the Windows directory), so that
       where the search stopped shows */
    cwd[0] = '\\';
    if (SELECTOROF((char FAR *)cwd) != cur_ds() || dos_cwd((WORD)(void NEAR *)cwd + 1))
        return;
    dos_path(0x3B00, "C:\\");
    /* every handle in use */
    while (n < NFH && (fh[n] = _lopen("C:\\TK62OE.TMP", OF_READ)) != HFILE_ERROR)
        n++;
    obs_env("openerr: handles", n);
    of.nErrCode = 0xEEEE;
    f = OpenFile("C:\\TK62OE.TMP", &of, OF_READ);
    obs("openerr: full path ret", f == HFILE_ERROR ? 0xFFFF : 0);
    obs("openerr: full path err", of.nErrCode);
    if (f != HFILE_ERROR)
        _lclose(f);
    of.nErrCode = 0xEEEE;
    f = OpenFile("TKDG.DLL", &of, OF_READ);       /* in the Windows directory */
    obs("openerr: full search ret", f == HFILE_ERROR ? 0xFFFF : 0);
    obs("openerr: full search err", of.nErrCode);
    obs("openerr: full search path is windir", in_windir(of.szPathName, "TKDG.DLL"));
    obs("openerr: full search path is C:", cmp_ci(of.szPathName, "C:\\TKDG.DLL") == 0);
    if (f != HFILE_ERROR)
        _lclose(f);
    of.nErrCode = 0xEEEE;
    f = OpenFile("TK62NONE.TXT", &of, OF_READ);
    obs("openerr: full none ret", f == HFILE_ERROR ? 0xFFFF : 0);
    obs("openerr: full none err", of.nErrCode);
    if (f != HFILE_ERROR)
        _lclose(f);
    of.nErrCode = 0xEEEE;
    f = OpenFile("TK62NONE.TXT", &of, OF_EXIST);
    obs("openerr: full none exist err", of.nErrCode);
    for (i = 0; i < n; i++)
        _lclose(fh[i]);
    /* a directory where the file is looked for */
    {
        char w[160];
        WORD k = GetWindowsDirectory(w, sizeof w);
        if (k && w[k - 1] != '\\')
            w[k++] = '\\';
        w[k] = 0;
        lstrcat(w, "TK62DIR.TMP");
        obs_env("openerr: md", dos_path(0x3900, w));
        of.nErrCode = 0xEEEE;
        f = OpenFile(w, &of, OF_READWRITE);
        obs("openerr: dir path ret", f == HFILE_ERROR ? 0xFFFF : 0);
        obs("openerr: dir path err", of.nErrCode);
        if (f != HFILE_ERROR)
            _lclose(f);
        of.nErrCode = 0xEEEE;
        f = OpenFile("TK62DIR.TMP", &of, OF_READWRITE);
        obs("openerr: dir search ret", f == HFILE_ERROR ? 0xFFFF : 0);
        obs("openerr: dir search err", of.nErrCode);
        obs("openerr: dir search path is windir", in_windir(of.szPathName, "TK62DIR.TMP"));
        obs("openerr: dir search path is C:", cmp_ci(of.szPathName, "C:\\TK62DIR.TMP") == 0);
        if (f != HFILE_ERROR)
            _lclose(f);
        /* and a file of that name in the system directory, searched
           after the Windows directory: does the search go on? */
        {
            char sd[160];
            WORD m = GetSystemDirectory(sd, sizeof sd);
            if (m && sd[m - 1] != '\\')
                sd[m++] = '\\';
            sd[m] = 0;
            lstrcat(sd, "TK62DIR.TMP");
            f = _lcreat(sd, 0);
            if (f != HFILE_ERROR) {
                _lclose(f);
                of.nErrCode = 0xEEEE;
                f = OpenFile("TK62DIR.TMP", &of, OF_READWRITE);
                obs("openerr: dir+file search ret", f == HFILE_ERROR ? 0xFFFF : 0);
                obs("openerr: dir+file search err", of.nErrCode);
                obs("openerr: dir+file search path is sysdir", cmp_ci(of.szPathName, sd) == 0);
                if (f != HFILE_ERROR)
                    _lclose(f);
                dos_path(0x4100, sd);
            }
        }
        dos_path(0x3A00, w);
    }
    /* a sharing violation (if DOS has SHARE) */
    f = OpenFile("C:\\TK62OE.TMP", &of, OF_READ | OF_SHARE_EXCLUSIVE);
    if (f != HFILE_ERROR) {
        HFILE g;
        of.nErrCode = 0xEEEE;
        g = OpenFile("C:\\TK62OE.TMP", &of, OF_READ | OF_SHARE_EXCLUSIVE);
        obs_env("openerr: share ret", g == HFILE_ERROR ? 0xFFFF : 0);
        obs_env("openerr: share err", of.nErrCode);
        if (g != HFILE_ERROR)
            _lclose(g);
        _lclose(f);
    }
    OpenFile("C:\\TK62OE.TMP", &of, OF_DELETE);
    dos_path(0x3B00, cwd);
}

/* Medium 39: GetDriveType where IOCTL 4408h fails (F: is a DOSBox
   "floppy" mount: 4409h answers, 4408h fails) */
static void drivetype(void)
{
    HMODULE hs = GetModuleHandle("SYSTEM");
    DWORD (FAR PASCAL *inq)(WORD, WORD) = hs ?
        (DWORD (FAR PASCAL *)(WORD, WORD))GetProcAddress(hs, MAKEINTRESOURCE(1)) : NULL;
    step("drivetype");
    obs("drivetype: C", GetDriveType(2));
    obs("drivetype: F", GetDriveType(5));
    obs("drivetype: Y", GetDriveType(24));
    obs("drivetype: Z", GetDriveType(25));
    obs("drivetype: 26", GetDriveType(26));
    obs_env("drivetype: 0101", GetDriveType(0x101));
    if (inq) {
        DWORD r = inq(1, 5);
        obs_env("drivetype: inquire F lo", LOWORD(r));
        obs_env("drivetype: inquire F hi", HIWORD(r));
    }
}

/* L4: LocalCompact(-1) and more than 32 discardable blocks */
#define NLB 48
static void lcompact(void)
{
    HLOCAL h[NLB];
    WORD i, n = 0, d = 0;
    step("lcompact");
    for (i = 0; i < NLB; i++)
        if ((h[i] = LocalAlloc(LMEM_MOVEABLE | LMEM_DISCARDABLE, 8)) != 0)
            n++;
    obs("lcompact: allocated", n);
    LocalCompact((UINT)-1);
    for (i = 0; i < NLB; i++)
        if (h[i] && (LocalFlags(h[i]) & LMEM_DISCARDED))
            d++;
    obs("lcompact: discarded", d);
    for (i = 0; i < NLB; i++)
        if (h[i])
            LocalFree(h[i]);
}

/* L5: does GetFreeSpace count a fixed discardable block as free? */
static WORD fs_class(DWORD before, DWORD after)
{
    DWORD d = before > after ? before - after : 0;
    if (d < 0x8000UL)
        return 0;                       /* counted as free */
    if (d >= 0x18000UL && d < 0x28000UL)
        return 1;                       /* counted as used */
    return 2;
}

static void freespace(void)
{
    static const WORD fl[3] = { GMEM_FIXED, GMEM_FIXED | GMEM_DISCARDABLE,
                                GMEM_MOVEABLE | GMEM_DISCARDABLE };
    static char nm[] = "freespace: N class", fn[] = "freespace: N flags";
    WORD i;
    step("freespace");
    /* (3.1 in 386 Enhanced mode ended Windows here) */
    if (GetWinFlags() & WF_ENHANCED)
        return;
    for (i = 0; i < 3; i++) {
        DWORD f0, f1;
        HGLOBAL h;
        GetFreeSpace(0);
        f0 = GetFreeSpace(0);
        h = GlobalAlloc(fl[i], 0x20000UL);
        if (!h)
            continue;
        f1 = GetFreeSpace(0);
        nm[11] = fn[11] = (char)('0' + i);
        obs(nm, fs_class(f0, f1));
        obs(fn, GlobalFlags(h));
        if (i == 1) {
            HGLOBAL r = GlobalDiscard(h);
            obs("freespace: fixed discard ret", r != 0);
            obs("freespace: fixed discard flags", GlobalFlags(h));
        }
        GlobalFree(h);
    }
}

/* L27: DirectResAlloc (KERNEL.168): owner, flags */
static void directres(void)
{
    HGLOBAL (FAR PASCAL *dra)(HINSTANCE, WORD, WORD) =
        (HGLOBAL (FAR PASCAL *)(HINSTANCE, WORD, WORD))kord(168);
    WORD (FAR PASCAL *owner)(WORD) = (WORD (FAR PASCAL *)(WORD))kord(404);
    HINSTANCE hi = (HINSTANCE)(entry_regs[E_DS] & ~1u);
    WORD hmod = (WORD)GetModuleHandle("TK62");
    static const WORD fl[4] = { 0, 0x10, 0x1010, 0x1000 };
    static char nm[] = "directres: N ok", fn[] = "directres: N flags",
        on[] = "directres: N owner", pn[] = "directres: N odd",
        sn[] = "directres: N size";
    WORD i;
    step("directres");
    for (i = 0; i < 4; i++) {
        HGLOBAL h = dra(hi, fl[i], 0x100);
        WORD o;
        nm[11] = fn[11] = on[11] = pn[11] = sn[11] = (char)('0' + i);
        obs(nm, h != 0);
        if (!h)
            continue;
        o = owner(sel_of(h));
        obs(on, o == hmod ? 1 : o == (WORD)GetCurrentPDB() ? 2 :
                o == (WORD)GetCurrentTask() ? 3 : 0);
        obs(fn, GlobalFlags(h));
        obs(pn, (WORD)h & 1);
        obs(sn, GlobalSize(h) >= 0x100 && GlobalSize(h) < 0x200);
        GlobalFree(h);
    }
}

/* L30: OpenFile's search of the PATH: is the variable's name compared
   without case? (MOUNT.COM is on Z:, which is on the PATH) */
static void envcase(void)
{
    char FAR *e = GetDOSEnvironment(), FAR *p;
    OFSTRUCT of;
    step("envcase");
    obs("envcase: found", OpenFile("MOUNT.COM", &of, OF_EXIST) != HFILE_ERROR);
    for (p = e; *p; ) {
        if (p[0] == 'P' && p[1] == 'A' && p[2] == 'T' && p[3] == 'H' && p[4] == '=')
            break;
        while (*p)
            p++;
        p++;
    }
    if (!*p)
        return;
    p[0] = 'p'; p[1] = 'a'; p[2] = 't'; p[3] = 'h';
    obs("envcase: lower found", OpenFile("MOUNT.COM", &of, OF_EXIST) != HFILE_ERROR);
    p[0] = 'X'; p[1] = 'A'; p[2] = 'T'; p[3] = 'H';
    obs("envcase: XATH found", OpenFile("MOUNT.COM", &of, OF_EXIST) != HFILE_ERROR);
    p[0] = 'P'; p[1] = 'A'; p[2] = 'T'; p[3] = 'H';
}

extern WORD __cdecl get_0040h(void);     /* start.asm: __0040H */

static WORD bios_tick(void)
{
    return *(WORD FAR *)MAKELP(get_0040h(), 0x6C);
}

/* L31: GetTempFileName with drive 80h; a directory with the next name */
static void tempname(void)
{
    char a[160], b[160], c[160], d[160];
    WORD na, nb, nc, r, i;
    step("tempname");
    a[0] = 0;
    r = GetTempFileName(0x80, "TKP", 0x1234, a);
    obs("tempname: 80 ret", r);
    obs("tempname: 80 [0]", (BYTE)a[0]);
    obs("tempname: 80 [1]", (BYTE)a[1]);
    obs("tempname: 80 is ~", a[0] == '~');
    /* unique 0 takes its number from the clock (3.1 and w16 alike): the
       three calls must see the same time, else the next one starts
       elsewhere and misses the file or directory before it. A run with
       a timer tick in it, or whose third number is unrelated to the
       second (the clock moved), is done again (at most 8 times) */
    for (i = 0; ; i++) {
        WORD t0 = bios_tick(), cls, moved;
        na = GetTempFileName(0, "TKQ", 0, a);
        nb = GetTempFileName(0, "TKQ", 0, b);
        GetTempFileName(0, "TKQ", nb + 1, d);       /* (no file made) */
        r = dos_path(0x3900, d);
        nc = GetTempFileName(0, "TKQ", 0, c);
        cls = nc == 0 ? 0 : nc == nb + 1 ? 1 : nc == nb + 2 ? 2 : 3;
        moved = bios_tick() != t0 || nb != na + 1 || cls == 3;
        dos_path(0x3A00, d);
        dos_path(0x4100, a);
        dos_path(0x4100, b);
        if (nc)
            dos_path(0x4100, c);
        if (moved && i < 7) {
            obs_env("tempname: run again (clock)", i + 1);
            continue;
        }
        obs("tempname: made", na && nb);
        obs("tempname: 2nd is next", nb == na + 1);
        obs_env("tempname: a", na);
        obs_env("tempname: b", nb);
        obs_env("tempname: md", r);
        obs_env("tempname: c", nc);
        obs("tempname: 3rd after dir", cls);
        break;
    }
}

/* L32: OF_SEARCH with a path that does not hold the file */
static void ofsearch(void)
{
    OFSTRUCT of;
    HFILE f;
    step("ofsearch");
    of.nErrCode = 0xEEEE;
    f = OpenFile("C:\\TK62NO\\TKDG.DLL", &of, OF_EXIST);
    obs("ofsearch: plain ret", f == HFILE_ERROR ? 0xFFFF : 0);
    obs("ofsearch: plain err", of.nErrCode);
    of.nErrCode = 0xEEEE;
    f = OpenFile("C:\\TK62NO\\TKDG.DLL", &of, OF_EXIST | OF_SEARCH);
    obs("ofsearch: search ret", f == HFILE_ERROR ? 0xFFFF : 0);
    obs("ofsearch: search err", of.nErrCode);
    obs("ofsearch: search path is windir", in_windir(of.szPathName, "TKDG.DLL"));
    of.nErrCode = 0xEEEE;
    f = OpenFile("TK62NONE.TXT", &of, OF_EXIST | OF_SEARCH);
    obs("ofsearch: none ret", f == HFILE_ERROR ? 0xFFFF : 0);
    obs("ofsearch: none err", of.nErrCode);
}

/* Medium 33: KERNEL's own segments as blocks of the global heap */
static void kseg(void)
{
    HMODULE hk = GetModuleHandle("KERNEL");
    WORD (FAR PASCAL *owner)(WORD) = (WORD (FAR PASCAL *)(WORD))kord(404);
    WORD cs = SELECTOROF(GetProcAddress(hk, "GlobalAlloc"));
    WORD ds = SELECTOROF(GetProcAddress(hk, "THHOOK"));
    WORD ne = (WORD)hk, nseg, st, i;
    static char sn[] = "kseg: segment NN h";
    DWORD gh;
    step("kseg");
    gh = GlobalHandle(cs);
    obs_env("kseg: cs", cs);
    obs_env("kseg: cs handle", LOWORD(gh));
    obs_env("kseg: cs handle ok", LOWORD(gh) != 0);
    obs_env("kseg: cs handle sel", HIWORD(gh) == cs);
    obs_env("kseg: cs handle is cs", LOWORD(gh) == cs);
    obs_env("kseg: cs handle is cs-1", LOWORD(gh) == cs - 1);
    obs_env("kseg: cs flags", GlobalFlags((HGLOBAL)LOWORD(gh)));
    obs_env("kseg: cs size fits", LOWORD(gh) &&
        GlobalSize((HGLOBAL)LOWORD(gh)) >= GetSelectorLimit(cs) + 1UL);
    obs_env("kseg: cs owner is KERNEL", owner(cs) == (WORD)hk);
    obs_env("kseg: GetCodeHandle is handle", (WORD)GetCodeHandle(
        GetProcAddress(hk, "GlobalAlloc")) == LOWORD(gh));
    gh = GlobalHandle(ds);
    obs_env("kseg: ds", ds);
    obs_env("kseg: ds handle", LOWORD(gh));
    obs_env("kseg: ds handle ok", LOWORD(gh) != 0);
    obs_env("kseg: ds handle sel", HIWORD(gh) == ds);
    obs_env("kseg: ds handle is ds", LOWORD(gh) == ds);
    obs_env("kseg: ds handle is ds-1", LOWORD(gh) == ds - 1);
    obs_env("kseg: ds flags", GlobalFlags((HGLOBAL)LOWORD(gh)));
    obs_env("kseg: ds owner is KERNEL", owner(ds) == (WORD)hk);
    obs_env("kseg: module table owner is KERNEL", owner(ne) == (WORD)hk);
    gh = GlobalHandle(ne);
    obs_env("kseg: module table handle is ne", LOWORD(gh) == ne);
    nseg = *(WORD FAR *)MAKELP(ne, 0x1C);
    st = *(WORD FAR *)MAKELP(ne, 0x22);
    obs_env("kseg: segments", nseg);
    for (i = 0; i < nseg && i < 20; i++) {
        WORD FAR *e = (WORD FAR *)MAKELP(ne, st + 10 * i);
        sn[14] = (char)('0' + (i + 1) / 10);
        sn[15] = (char)('0' + (i + 1) % 10);
        sn[17] = 'h';
        obs_env(sn, e[4]);
        if (e[4]) {
            sn[17] = 'f';
            obs_env(sn, GlobalFlags((HGLOBAL)e[4]));
            sn[17] = 'g';
            obs_env(sn, LOWORD(GlobalHandle(e[4] | 1)));
        }
        out("  flags ");
        outhex(e[2]);
        out("\r\n");
    }
}

int __cdecl tmain(void)
{
    out("TK62 start\r\n");
    chunk();
    big();
    ldt();
    dosalloc();
    badne();
    bigdg();
    wep();
    lock();
    catchmap();
    discsel();
    badptr();
    strptr();
    atoms();
    events();
    sigproc();
    lockseg();
    codehandle();
    catchbuf();
    ansi();
    files();
    winexec();
    profile();
    openerr();
    drivetype();
    lcompact();
    freespace();
    directres();
    envcase();
    tempname();
    ofsearch();
    kseg();
    entries();                  /* 3.1's KRNL286 ends here */
    hdelta();                   /* last: 3.1 ends here (both kernels) */
    out("TK62 done\r\n");
    return failures;
}
