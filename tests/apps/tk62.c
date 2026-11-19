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

int __cdecl tmain(void)
{
    out("TK62 start\r\n");
    chunk();
    big();
    ldt();
    dosalloc();
    badne();
    bigdg();
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
    entries();                  /* 3.1's KRNL286 ends here */
    hdelta();                   /* last: 3.1 ends here (both kernels) */
    out("TK62 done\r\n");
    return failures;
}
