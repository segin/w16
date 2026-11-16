/*
 * tk50.c - the KERNEL exports w16 lacked (docs/exports/KERNEL.txt against
 * kernel/kernel.lnk), observed one by one: arguments, results, side
 * effects, the bytes each pops and which registers it keeps. Every call
 * goes through GetProcAddress by ordinal (a missing one logs FFFF) and
 * tk50s.asm's w_call, with known values in every register:
 *   kept mask: 01 BX  02 CX  04 SI  08 DI  10 BP  20 ES  40 carry kept
 *   set  80 DS  100 DX (where it is not a result)  200 carry kept clear
 *   (the second call, made only for calls without side effects)
 *   left: bytes of arguments the procedure left on the stack (0: RETF n
 *   took them all)
 * Runs under make test (KERNEL only) and as the shell of Windows 3.1
 * (tests/dropin.sh, -r for Microsoft's KRNL286; tests/refrun.sh for
 * KRNL386).
 */
#include <windows.h>

#define TK_REF "tk50ref.h"
#define TK_REF286 "tk50ref286.h"
#include "tklog.h"

extern WORD __cdecl w_regs[10];
extern WORD __cdecl w_popped, w_ds, w_es, w_bx, w_clc, w_ax;
extern WORD __cdecl cb_count, cb_ret, cb_log[];
extern WORD __cdecl dos_count;
extern DWORD __cdecl dos_next;
extern WORD __cdecl sst_regs[16], sst_hdr[16];
extern void __cdecl __far w_call(FARPROC fn, int nargs, WORD a0, WORD a1,
                                  WORD a2, WORD a3, WORD a4, WORD a5,
                                  WORD a6, WORD a7);
extern void __cdecl __far sst_test(FARPROC to, FARPROC back, WORD seg,
                                   WORD sp, WORD top);
extern void __cdecl __far rec_cb0(void);
extern void __cdecl __far rec_cb2(void);
extern void __cdecl __far rec_cb6(void);
extern void __cdecl __far rec_cb10(void);
extern void __cdecl __far dos_hook(void);
extern void __far PATCHME(void);
#pragma aux PATCHME "PATCHME"

#define SEG(p) SELECTOROF((void FAR *)(p))
#define OFF(p) OFFSETOF((void FAR *)(p))

static HMODULE hk;
static WORD r_ax, r_dx;
static char name[64];
static char buf[256];

static FARPROC kproc(WORD ord)
{
    return GetProcAddress(hk, MAKEINTRESOURCE(ord));
}

/* A "kept" mask: w16 may keep more registers than Windows 3.1, never
   fewer, so the check is that every bit 3.1 showed is set. */
static void kept(const char *n, WORD m)
{
    WORD i;
    out("OBS ");
    out(n);
    out("=");
    outhex(m);
    out("\r\n");
    for (i = 0; ref[i].name; i++)
        if (same(ref[i].name, n)) {
            if ((m & ref[i].value) != ref[i].value) {
                out("FAIL ");
                out(n);
                out(" (Windows 3.1 keeps ");
                outhex(ref[i].value);
                out(")\r\n");
                failures++;
            }
            break;
        }
}

static void nm(const char *what, const char *suffix)
{
    lstrcpy(name, what);
    lstrcat(name, " ");
    lstrcat(name, suffix);
}

static WORD mask(void)
{
    WORD m = 0, ds = w_ds ? w_ds : SEG(buf);
    if (w_regs[1] == (w_bx ? w_bx : 0x2222)) m |= 0x01;
    if (w_regs[2] == 0x3333) m |= 0x02;
    if (w_regs[4] == 0x5555) m |= 0x04;
    if (w_regs[5] == 0x6666) m |= 0x08;
    if (w_regs[6] == 0x7777) m |= 0x10;
    if (w_regs[8] == (w_es ? w_es : SEG(buf))) m |= 0x20;
    if (w_regs[9] & 1) m |= 0x40;
    if (w_regs[7] == ds) m |= 0x80;
    if (w_regs[3] == 0x4444) m |= 0x100;
    return m;
}

/* call export ord with n arguments; log its kept mask and stack use
   under `what`; twice (carry set, then clear) if `twice`. Returns 0 if
   the export is missing. */
static int kc(const char *what, WORD ord, int twice, int n, WORD a0,
              WORD a1, WORD a2, WORD a3, WORD a4, WORD a5, WORD a6, WORD a7)
{
    FARPROC f = kproc(ord);
    WORD m;
    if (!f) {
        nm(what, "missing");
        obs(name, 1);
        return 0;
    }
    out("> ");
    out(what);
    out("\r\n");
    w_clc = 0;
    w_call(f, n, a0, a1, a2, a3, a4, a5, a6, a7);
    m = mask();
    r_ax = w_regs[0];
    r_dx = w_regs[3];
    nm(what, "left");
    obs(name, w_popped);
    if (twice) {
        w_clc = 1;
        w_call(f, n, a0, a1, a2, a3, a4, a5, a6, a7);
        if (!(w_regs[9] & 1))
            m |= 0x200;
        w_clc = 0;
    }
    nm(what, "kept");
    kept(name, m);
    return 1;
}

static void res(const char *what, const char *reg, WORD v)
{
    nm(what, reg);
    obs(name, v);
}

static void res_env(const char *what, const char *reg, WORD v)
{
    nm(what, reg);
    obs_env(name, v);
}

#define K0(w, o, t)             kc(w, o, t, 0, 0, 0, 0, 0, 0, 0, 0, 0)
#define K1(w, o, t, a)          kc(w, o, t, 1, a, 0, 0, 0, 0, 0, 0, 0)
#define K2(w, o, t, a, b)       kc(w, o, t, 2, a, b, 0, 0, 0, 0, 0, 0)
#define K3(w, o, t, a, b, c)    kc(w, o, t, 3, a, b, c, 0, 0, 0, 0, 0)

static WORD FAR *thhook;

#define HP(s, o) ((BYTE FAR *)MAKELP((s) + (WORD)((o) >> 16) * inc, (WORD)(o)))

/* ---- hmemcpy (348) ---- */
static void t_hmemcpy(void)
{
    HGLOBAL h1 = GlobalAlloc(GMEM_MOVEABLE | GMEM_ZEROINIT, 0x20010L);
    HGLOBAL h2 = GlobalAlloc(GMEM_MOVEABLE | GMEM_ZEROINIT, 0x20010L);
    WORD inc = (WORD)(DWORD)kproc(114);
    WORD s1 = SEG(GlobalLock(h1)), s2 = SEG(GlobalLock(h2)), i, bad;
    BYTE FAR *a, FAR *b;
    for (i = 0; i < 0x40; i++)
        *HP(s1, 0xFFF0L + i) = (BYTE)(i + 1);
    /* n = 0 first */
    if (!kc("hmemcpy(n=0)", 348, 1, 6, s2, 0, s1, 0, 0, 0, 0, 0))
        goto done;
    res("hmemcpy(n=0)", "AX", r_ax);
    res("hmemcpy(n=0)", "DX", r_dx);
    w_call(kproc(348), 6, s2, 0xFFF8, s1, 0xFFF0, 0, 0x30, 0, 0);
    res("hmemcpy(0x30 across 64K)", "AX", w_regs[0]);
    res("hmemcpy(0x30 across 64K)", "DX", w_regs[3]);
    nm("hmemcpy(0x30 across 64K)", "kept");
    kept(name, mask());
    a = (BYTE FAR *)MAKELP(s2, 0xFFF8);
    b = (BYTE FAR *)MAKELP(s2 + inc, 0);
    bad = 0;
    for (i = 0; i < 8; i++)
        if (a[i] != i + 1) bad++;
    for (i = 0; i < 0x28; i++)
        if (b[i] != i + 9) bad++;
    if (b[0x28] != 0 || a[-1] != 0) bad++;
    res("hmemcpy(0x30 across 64K)", "wrong bytes", bad);
    /* overlapping, forwards and backwards within one segment */
    a = (BYTE FAR *)MAKELP(s1, 0);
    for (i = 0; i < 16; i++)
        a[i] = (BYTE)i;
    w_call(kproc(348), 6, s1, 2, s1, 0, 0, 8, 0, 0);
    res("hmemcpy(up overlap)", "bytes 0-3", ((WORD FAR *)a)[1]);
    res("hmemcpy(up overlap)", "bytes 8-9", ((WORD FAR *)a)[4]);
    for (i = 0; i < 16; i++)
        a[i] = (BYTE)i;
    w_call(kproc(348), 6, s1, 0, s1, 2, 0, 8, 0, 0);
    res("hmemcpy(down overlap)", "bytes 6-7", ((WORD FAR *)a)[3]);
    /* a large copy: 0x18000 bytes across two boundaries */
    for (i = 0; i < 0x100; i++) {
        *HP(s1, 0x8000L + i * 0x100L) = (BYTE)i;
        *HP(s1, 0x8000L + i * 0x100L + 0xFF) = (BYTE)~i;
    }
    w_call(kproc(348), 6, s2, 0x10, s1, 0x8000, 1, 0x8000, 0, 0);
    bad = 0;
    for (i = 0; i < 0x100; i++)
        if (*HP(s2, 0x10L + i * 0x100L) != (BYTE)i ||
            *HP(s2, 0x10L + i * 0x100L + 0xFF) != (BYTE)~i)
            bad++;
    res("hmemcpy(0x18000)", "wrong bytes", bad);
done:
    GlobalUnlock(h1);
    GlobalUnlock(h2);
    GlobalFree(h1);
    GlobalFree(h2);
}

static void t_dbcs(void)
{
    static const BYTE c[] = { 0x41, 0x81, 0x9F, 0xE0, 0xFF, 0 };
    int i;
    for (i = 0; c[i]; i++) {
        char w[32];
        lstrcpy(w, "IsDBCSLeadByte(");
        w[15] = "0123456789ABCDEF"[c[i] >> 4];
        w[16] = "0123456789ABCDEF"[c[i] & 15];
        w[17] = ')';
        w[18] = 0;
        if (!K1(w, 207, 1, c[i]))
            return;
        res(w, "AX", r_ax);
        res(w, "DX", r_dx);
    }
}

static void t_priority(void)
{
    WORD t = (WORD)GetCurrentTask(), FAR *tp = (WORD FAR *)MAKELP(t, 0);
    static const int d[] = { 0, 3, -5, 100, -100, 0x7FFF };
    int i;
    res("TDB priority", "before", tp[4]);
    for (i = 0; i < 6; i++) {
        char w[32];
        lstrcpy(w, "SetPriority(0,  )");
        w[15] = "0123456789ABCDEF"[i];
        if (!K2(w, 32, 0, 0, d[i]))
            return;
        res(w, "AX", r_ax);
        res(w, "TDB 08h", tp[4]);
        tp[4] = 0;
    }
    tp[4] = 3;
    if (!K2("SetPriority(0,2) from 3", 32, 0, 0, 2))
        return;
    res("SetPriority(0,2) from 3", "AX", r_ax);
    res("SetPriority(0,2) from 3", "TDB 08h", tp[4]);
    tp[4] = 0x0A;
    K2("SetPriority(0,7Fh) from 10", 32, 0, 0, 0x7F);
    res("SetPriority(0,7Fh) from 10", "AX", r_ax);
    tp[4] = 0xF6;
    K2("SetPriority(0,-128) from -10", 32, 0, 0, 0xFF80);
    res("SetPriority(0,-128) from -10", "AX", r_ax);
    tp[4] = 0;
    K2("SetPriority(0,100h)", 32, 0, 0, 0x100);
    res("SetPriority(0,100h)", "AX", r_ax);
    tp[4] = 0;
    K2("SetPriority(bad,1)", 32, 0, SEG(buf), 1);
    res_env("SetPriority(bad,1)", "AX", r_ax);
    tp[4] = 0;
    if (!K2("SetPriority(task,2)", 32, 0, t, 2))
        return;
    res("SetPriority(task,2)", "AX", r_ax);
    res("SetPriority(task,2)", "TDB 08h", tp[4]);
    tp[4] = 0;
}

/* GetHeapSpaces (KERNEL.138) of a small heap made as USER makes its
   second heaps (LocalInit(handle, 12h, 417h) in a 430h-byte block;
   seen with [w16] trace): USER's GetFreeSystemResources divides by its
   total */
static void t_heapspaces(void)
{
    HGLOBAL h = GlobalAlloc(GMEM_MOVEABLE | GMEM_ZEROINIT, 0x430);
    DWORD (FAR PASCAL *ghs)(WORD) = (DWORD (FAR PASCAL *)(WORD))kproc(138);
    BOOL (FAR PASCAL *li)(WORD, WORD, WORD) =
        (BOOL (FAR PASCAL *)(WORD, WORD, WORD))kproc(4);
    DWORD v;
    res("LocalInit(430h block,12h,417h)", "", li((WORD)h, 0x12, 0x417));
    v = ghs((WORD)h);
    res("GetHeapSpaces(small heap) total", "", HIWORD(v));
    res_env("GetHeapSpaces(small heap) free", "", LOWORD(v));
    v = ghs((WORD)h | 1);
    res("GetHeapSpaces(small heap sel) total", "", HIWORD(v));
    res("GetHeapSpaces(small heap) word 6", "", *(WORD FAR *)MAKELP((WORD)h | 1, 6));
    GlobalFree(h);
}

static void t_simple(void)
{
    WORD FAR *p;
    DWORD old;
    if (K0("GetFreeMemInfo", 316, 1)) {
        if (r_ax == 0xFFFF && r_dx == 0xFFFF) {
            res("GetFreeMemInfo", "-1", 1);
        } else {
            res("GetFreeMemInfo", "-1", 0);
            res_env("GetFreeMemInfo", "AX", r_ax);
            res_env("GetFreeMemInfo", "DX", r_dx);
        }
    }
    if (K0("GetLastDiskChange", 98, 1)) {
        res("GetLastDiskChange", "AL", r_ax & 0xFF);
        res_env("GetLastDiskChange", "AX", r_ax);
    }
    if (K0("GetLPErrMode", 99, 1)) {
        res("GetLPErrMode DX==THHOOK seg", "", thhook && r_dx == SEG(thhook));
        res_env("GetLPErrMode", "AX", r_ax);
        res_env("GetLPErrMode", "DX", r_dx);
        if (thhook && r_dx == SEG(thhook))
            res("GetLPErrMode", "*p", *(BYTE FAR *)MAKELP(r_dx, r_ax));
    }
    if (K0("ValidateCodeSegments", 100, 1)) {
        res("ValidateCodeSegments", "AX", r_ax);
        res("ValidateCodeSegments", "DX", r_dx);
    }
    if (K0("ValidateFreeSpaces", 200, 1)) {
        res("ValidateFreeSpaces", "AX", r_ax);
        res("ValidateFreeSpaces", "DX", r_dx);
    }
    if (K0("DiagQuery", 339, 1))
        res_env("DiagQuery", "AX", r_ax);
    if (K0("EnableDos", 41, 1)) {
        res("EnableDos", "AX", r_ax);
        res("EnableDos", "DX", r_dx);
    }
    if (K0("DisableDos", 42, 0)) {
        res("DisableDos", "AX", r_ax);
        res("DisableDos", "DX", r_dx);
        out("after DisableDos\r\n");    /* INT 21h still works? */
        K0("EnableDos again", 41, 0);
    }
    if (K0("EnableKernel", 124, 1)) {
        res("EnableKernel", "AX", r_ax);
        res("EnableKernel", "DX", r_dx);
    }
    if (K1("MemoryFreed(0)", 126, 1, 0)) {
        res("MemoryFreed(0)", "AX", r_ax);
        res("MemoryFreed(0)", "DX", r_dx);
    }
    if (K1("MemoryFreed(10h)", 126, 0, 0x10)) {
        res("MemoryFreed(10h)", "AX", r_ax);
        res("MemoryFreed(10h)", "DX", r_dx);
    }
    if (K2("LimitEmsPages(1000h)", 156, 1, 0, 0x1000)) {
        res("LimitEmsPages(1000h)", "AX", r_ax);
        res("LimitEmsPages(1000h)", "DX", r_dx);
    }
    if (K1("A20Proc(1)", 165, 1, 1)) {
        res("A20Proc(1)", "AX", r_ax);
        res("A20Proc(1)", "DX", r_dx);
    }
    if (K3("ReplaceInst", 201, 1, 1, 2, 3)) {
        res("ReplaceInst", "AX", r_ax);
        res("ReplaceInst", "DX", r_dx);
    }
    if (K1("SwapRecording(0)", 204, 1, 0)) {
        res("SwapRecording(0)", "AX", r_ax);
        res_env("SwapRecording(0)", "DX", r_dx);
    }
    if (K1("FlushCachedFileHandle(hk)", 319, 1, hk)) {
        res("FlushCachedFileHandle(hk)", "AX", r_ax);
        res("FlushCachedFileHandle(hk)", "DX", r_dx);
    }
    if (K1("FlushCachedFileHandle(0)", 319, 1, 0))
        res("FlushCachedFileHandle(0)", "AX", r_ax);
    /* KbdRst (123): KEYBOARD.DRV calls it from its INT 9 handler
       (Schulman); WriteOutProfiles (315), LogError
       (324), LogParamError (325): what each keeps and returns */
    if (K0("KbdRst", 123, 1)) {
        res("KbdRst", "AX", r_ax);
        res("KbdRst", "DX", r_dx);
    }
    if (K0("WriteOutProfiles", 315, 1)) {
        res("WriteOutProfiles", "DX", r_dx);
    }
    if (K3("LogError(0,0)", 324, 1, 0, 0, 0)) {
        res("LogError(0,0)", "AX", r_ax);
        res("LogError(0,0)", "DX", r_dx);
    }
    /* EMSCopy (160), Schulman: does nothing in 3.x, 14 bytes of
       arguments. Not called: with seven zero words it ended the probe on
       3.1 (KRNL286) */
    if (kc("LogParamError(0,0,0)", 325, 1, 5, 0, 0, 0, 0, 0, 0, 0, 0)) {
        res("LogParamError(0,0,0)", "AX", r_ax);
        res("LogParamError(0,0,0)", "DX", r_dx);
    }
    /* K329 = DebugFillBuffer(lp, n) */
    lstrcpy(buf, "abcdefghijklmnop");
    if (K3("K329(buf,8)", 329, 1, SEG(buf), OFF(buf), 8)) {
        res("K329(buf,8)", "AX", r_ax);
        res("K329(buf,8)", "DX", r_dx);
        res("K329(buf,8)", "buf[0]", (BYTE)buf[0]);
        res("K329(buf,8)", "buf[7]", (BYTE)buf[7]);
        res("K329(buf,8)", "buf[8]", (BYTE)buf[8]);
    }
    /* _DebugOutput(flags, fmt, ...): cdecl? */
    if (K3("_DebugOutput", 328, 1, 0, SEG("x%d"), OFF("x%d"))) {
        res("_DebugOutput", "AX", r_ax);
        res("_DebugOutput", "DX", r_dx);
    }
    /* GetWinDebugInfo(lp, flags) / SetWinDebugInfo(lp) */
    {
        WORD i;
        for (i = 0; i < 20; i++)
            ((WORD *)buf)[i] = 0xAAAA;
        if (K3("GetWinDebugInfo(buf,1Fh)", 355, 1, SEG(buf), OFF(buf), 0x1F)) {
            res("GetWinDebugInfo(buf,1Fh)", "AX", r_ax);
            res("GetWinDebugInfo(buf,1Fh)", "DX", r_dx);
            for (i = 0; i < 14; i++)
                obsn("GetWinDebugInfo buf word", i, ((WORD *)buf)[i]);
        }
        if (K2("SetWinDebugInfo(buf)", 356, 1, SEG(buf), OFF(buf))) {
            res("SetWinDebugInfo(buf)", "AX", r_ax);
            res("SetWinDebugInfo(buf)", "DX", r_dx);
        }
    }
    /* Reserved3/4 (79, 80): AnsiUpper/AnsiLower of Windows 2.x */
    lstrcpy(buf, "aZ\xE9\xC9{`}x");
    if (K2("Reserved3(str)", 79, 0, SEG(buf), OFF(buf))) {
        res("Reserved3(str) AX==buf", "", r_ax == OFF(buf));
        res("Reserved3(str) DX==buf", "", r_dx == SEG(buf));
        res("Reserved3(str)", "word0", ((WORD *)buf)[0]);
        res("Reserved3(str)", "word1", ((WORD *)buf)[1]);
        res("Reserved3(str)", "word2", ((WORD *)buf)[2]);
        res("Reserved3(str)", "word3", ((WORD *)buf)[3]);
    }
    lstrcpy(buf, "aZ\xE9\xC9{`}x");
    if (K2("Reserved4(str)", 80, 0, SEG(buf), OFF(buf))) {
        res("Reserved4(str) AX==buf", "", r_ax == OFF(buf));
        res("Reserved4(str) DX==buf", "", r_dx == SEG(buf));
        res("Reserved4(str)", "word0", ((WORD *)buf)[0]);
        res("Reserved4(str)", "word1", ((WORD *)buf)[1]);
        res("Reserved4(str)", "word2", ((WORD *)buf)[2]);
        res("Reserved4(str)", "word3", ((WORD *)buf)[3]);
    }
    if (K2("Reserved3(0,'q')", 79, 1, 0, 'q')) {
        res("Reserved3(0,'q')", "AX", r_ax);
        res("Reserved3(0,'q')", "DX", r_dx);
    }
    if (K2("Reserved3(0,E9h)", 79, 1, 0, 0xE9))
        res("Reserved3(0,E9h)", "AX", r_ax);
    if (K2("Reserved4(0,'Q')", 80, 1, 0, 'Q')) {
        res("Reserved4(0,'Q')", "AX", r_ax);
        res("Reserved4(0,'Q')", "DX", r_dx);
    }
    if (K2("Reserved4(0,C9h)", 80, 1, 0, 0xC9))
        res("Reserved4(0,C9h)", "AX", r_ax);
    /* InitTask1(proc): THHOOK 44h, Schulman */
    p = thhook;
    old = p ? *(DWORD FAR *)(p + 0x22) : 0;
    if (p)
        res_env("THHOOK 44h", "before", (WORD)old);
    if (K2("InitTask1", 141, 0, SEG(rec_cb0), OFF(rec_cb0))) {
        res_env("InitTask1", "AX", r_ax);
        res_env("InitTask1", "DX", r_dx);
        if (p)
            res("InitTask1 sets THHOOK 44h", "",
                *(DWORD FAR *)(p + 0x22) == (DWORD)(void FAR *)rec_cb0);
        K2("InitTask1(old)", 141, 0, HIWORD(old), LOWORD(old));
    }
}

static void t_files(void)
{
    WORD h;
    if (K3("OpenPathname(TK50.EXE,0)", 75, 0, SEG("C:\\TK50.EXE"), OFF("C:\\TK50.EXE"), 0)) {
        h = r_ax;
        res("OpenPathname(TK50.EXE,0) ok", "", h != 0xFFFF && h >= 5 && h < 0x100);
        res_env("OpenPathname(TK50.EXE,0)", "AX", r_ax);
        if (h != 0xFFFF && h < 0x100)
            res("OpenPathname: _lread of the handle", "",
                _lread(h, buf, 4) == 4);
        if (h != 0xFFFF && h < 0x100)
            _lclose(h);
    }
    if (K3("OpenPathname(nosuch,0)", 75, 0, SEG("C:\\NOSUCH.XYZ"), OFF("C:\\NOSUCH.XYZ"), 0)) {
        res("OpenPathname(nosuch,0)", "AX", r_ax);
        res_env("OpenPathname(nosuch,0)", "DX", r_dx);
    }
}

static void t_selectors(void)
{
    WORD k_cs = SEG(kproc(3)), us;
    FARPROC fx = (FARPROC)t_selectors;
    if (K1("IsSharedSelector(own DS)", 345, 1, SEG(buf))) {
        res("IsSharedSelector(own DS)", "AX", r_ax);
        res_env("IsSharedSelector(own DS)", "DX", r_dx);
    }
    if (K1("IsSharedSelector(own CS)", 345, 1, SEG(fx)))
        res("IsSharedSelector(own CS)", "AX", r_ax);
    if (K1("IsSharedSelector(KERNEL CS)", 345, 1, k_cs))
        res("IsSharedSelector(KERNEL CS)", "AX", r_ax);
    if (thhook && K1("IsSharedSelector(KERNEL DS)", 345, 1, SEG(thhook)))
        res("IsSharedSelector(KERNEL DS)", "AX", r_ax);
    if (K1("IsSharedSelector(hKernel)", 345, 1, hk))
        res("IsSharedSelector(hKernel)", "AX", r_ax);
    if (K1("IsSharedSelector(task)", 345, 1, (WORD)GetCurrentTask()))
        res("IsSharedSelector(task)", "AX", r_ax);
    us = GetModuleHandle("USER");
    if (us && K1("IsSharedSelector(hUser)", 345, 1, us))
        res("IsSharedSelector(hUser)", "AX", r_ax);
    /* GetCodeHandle(lpProc): DX:AX = selector:handle (Schulman) */
    if (K2("GetCodeHandle(own proc)", 93, 1, SEG(fx), OFF(fx))) {
        DWORD gh = GlobalHandle(SEG(fx));
        res("GetCodeHandle(own proc) AX==handle", "", r_ax == LOWORD(gh));
        res("GetCodeHandle(own proc) DX==CS", "", r_dx == SEG(fx));
        res_env("GetCodeHandle(own proc)", "AX", r_ax);
        res_env("GetCodeHandle(own proc)", "DX", r_dx);
    }
    if (K2("GetCodeHandle(hmod,1)", 93, 1, GetModuleHandle("TK50"), 1)) {
        res("GetCodeHandle(hmod,1) AX==handle", "",
            r_ax == LOWORD(GlobalHandle(SEG(fx))));
        res("GetCodeHandle(hmod,1) DX==CS", "", r_dx == SEG(fx));
        res("GetCodeHandle(hmod,1) DX==DS", "", r_dx == SEG(buf));
        res("GetCodeHandle(hmod,1) AX==DS handle", "",
            r_ax == LOWORD(GlobalHandle(SEG(buf))));
    }
    if (K2("GetCodeHandle(KERNEL proc)", 93, 1, SEG(kproc(3)), OFF(kproc(3)))) {
        res("GetCodeHandle(KERNEL proc) DX==CS", "", r_dx == k_cs);
        res("GetCodeHandle(KERNEL proc) AX==handle", "",
            r_ax == LOWORD(GlobalHandle(k_cs)));
    }
    if (K2("GetCodeHandle(0)", 93, 1, 0, 0)) {
        res_env("GetCodeHandle(0)", "AX", r_ax);
        res("GetCodeHandle(0)", "DX", r_dx);
    }
    if (K2("GetCodeHandle(data)", 93, 1, SEG(buf), OFF(buf))) {
        res("GetCodeHandle(data) AX==handle", "",
            r_ax == LOWORD(GlobalHandle(SEG(buf))));
        res("GetCodeHandle(data) DX==DS", "", r_dx == SEG(buf));
        res_env("GetCodeHandle(data)", "AX", r_ax);
        res_env("GetCodeHandle(data)", "DX", r_dx);
    }
}

/* LocalNotify (14) on our own heap: which word of the heap's information
   block it sets, what it returns */
static void t_localnotify(void)
{
    WORD ph = *(WORD FAR *)MAKELP(SEG(buf), 6), i, before[24];
    WORD FAR *ib = (WORD FAR *)MAKELP(SEG(buf), ph);
    DWORD prev;
    for (i = 0; i < 24; i++)
        before[i] = ib[i];
    if (!K2("LocalNotify", 14, 0, SEG(rec_cb6), OFF(rec_cb6)))
        return;
    prev = MAKELONG(r_ax, r_dx);
    res_env("LocalNotify prev seg==KERNEL CS", "", r_dx == SEG(kproc(3)));
    res_env("LocalNotify", "AX", r_ax);
    res_env("LocalNotify", "DX", r_dx);
    for (i = 0; i < 24; i++)
        if (ib[i] != before[i])
            obs_env(i == 12 ? "LocalNotify changed info word 18h" :
                    i == 15 ? "LocalNotify changed info word 1Eh" :
                    "LocalNotify changed another info word", 1);
    /* KRNL286 keeps it at 18h, w16's heap (KRNL386's layout) at 1Eh */
    res("LocalNotify prev == info 1Eh or 18h", "", prev == *(DWORD FAR *)&before[15] ||
        prev == *(DWORD FAR *)&before[12]);
    /* the procedure in use: notifications while it is set */
    cb_count = 0;
    cb_ret = 0;
    {
        HLOCAL a = LocalAlloc(LMEM_MOVEABLE, 0x100);
        HLOCAL b = LocalAlloc(LMEM_MOVEABLE | LMEM_DISCARDABLE, 0x100);
        HLOCAL c;
        LocalDiscard(b);
        c = LocalAlloc(LMEM_FIXED, 0x2000);    /* the heap grows */
        LocalFree(c);
        LocalReAlloc(a, 0x3000, LMEM_MOVEABLE);
        LocalFree(a);
        LocalFree(b);
    }
    res_env("LocalNotify calls", "", cb_count);     /* w16: none yet */
    for (i = 0; i < cb_count && i < 8; i++) {
        WORD FAR *e = (WORD FAR *)&cb_log[i * 16];
        obs_env("LocalNotify call wMsg", e[11]);
        obs_env("LocalNotify call wArg", e[9]);
        obs_env("LocalNotify call DS==ours", e[7] == SEG(buf));
    }
    K2("LocalNotify(prev)", 14, 0, HIWORD(prev), LOWORD(prev));
    res("LocalNotify(prev) returns ours", "",
        MAKELONG(r_ax, r_dx) == (DWORD)(void FAR *)rec_cb6);
}

static void t_dosproc(void)
{
    DWORD old;
    DWORD (FAR PASCAL *gs)(FARPROC) = (DWORD (FAR PASCAL *)(FARPROC))kproc(311);
    if (!gs) {
        obs("GetSetKernelDOSProc missing", 1);
        return;
    }
    /* no DOS call between setting the procedure and knowing the old one:
       KERNEL's own INT 21h handler may go through it */
    old = gs((FARPROC)dos_hook);
    dos_next = old;
    dos_count = 0;
    _lclose(_lopen("C:\\TK50.EXE", 0));
    res("INT 21h (_lopen/_lclose) through the DOS proc", "", dos_count != 0);
    res("GetSetKernelDOSProc prev seg==KERNEL CS", "", HIWORD(old) == SEG(kproc(3)));
    res("GetSetKernelDOSProc prev seg==KERNEL DS", "", thhook && HIWORD(old) == SEG(thhook));
    res_env("GetSetKernelDOSProc", "AX", LOWORD(old));
    res_env("GetSetKernelDOSProc", "DX", HIWORD(old));
    dos_count = 0;
    w_ax = 0x3000;
    w_call(kproc(102), 0, 0, 0, 0, 0, 0, 0, 0, 0);     /* DOS3Call */
    res("DOS3Call through the DOS proc", "", dos_count);
    res("DOS3Call(30h) AL", "", w_regs[0] & 0xFF);
    dos_count = 0;
    w_call(kproc(101), 0, 0, 0, 0, 0, 0, 0, 0, 0);     /* NoHookDOSCall */
    res("NoHookDOSCall through the DOS proc", "", dos_count);
    /* NetBIOSCall (103): INT 5Ch with the caller's ES:BX (an NCB);
       command 7Fh, which no NetBIOS knows (none in DOSBox) */
    {
        static BYTE ncb[64];
        WORD i;
        for (i = 0; i < sizeof ncb; i++)
            ncb[i] = 0;
        ncb[0] = 0x7F;
        w_es = SEG(ncb);
        w_bx = OFF(ncb);
        w_ax = 0x1234;
        if (kproc(103)) {
            w_call(kproc(103), 0, 0, 0, 0, 0, 0, 0, 0, 0);
            res("NetBIOSCall(7Fh) left", "", w_popped);
            res("NetBIOSCall(7Fh) kept", "", mask());
            res("NetBIOSCall(7Fh) AX", "", w_regs[0]);
            res("NetBIOSCall(7Fh) NCB retcode", "", ncb[1]);
            res("NetBIOSCall(7Fh) NCB cmd", "", ncb[0]);
        } else
            res("NetBIOSCall missing", "", 1);
        w_es = w_bx = 0;
    }
    w_ax = 0;
    K2("GetSetKernelDOSProc(old)", 311, 0, HIWORD(old), LOWORD(old));
    res("GetSetKernelDOSProc(old) returns ours", "",
        MAKELONG(r_ax, r_dx) == (DWORD)(void FAR *)dos_hook);
}

static void t_hooks(void)
{
    DWORD old;
    /* FatalExitHook (318): previous procedure */
    if (K2("FatalExitHook(cb)", 318, 0, SEG(rec_cb2), OFF(rec_cb2))) {
        old = MAKELONG(r_ax, r_dx);
        res_env("FatalExitHook", "AX", r_ax);
        res_env("FatalExitHook", "DX", r_dx);
        K2("FatalExitHook(old)", 318, 0, HIWORD(old), LOWORD(old));
        res("FatalExitHook(old) returns ours", "",
            MAKELONG(r_ax, r_dx) == (DWORD)(void FAR *)rec_cb2);
    }
    /* ToolHelpHook (341): returns the previous one */
    if (K2("ToolHelpHook(cb)", 341, 0, SEG(rec_cb0), OFF(rec_cb0))) {
        old = MAKELONG(r_ax, r_dx);
        res("ToolHelpHook prev", "", old != 0);
        res_env("ToolHelpHook", "AX", r_ax);
        res_env("ToolHelpHook", "DX", r_dx);
        cb_count = 0;
        cb_ret = 0;
        OutputDebugString("TK50 says hello\r\n");
        {
            WORD n = cb_count, i;
            K2("ToolHelpHook(old)", 341, 0, HIWORD(old), LOWORD(old));
            res("ToolHelpHook(old) returns ours", "",
                MAKELONG(r_ax, r_dx) == (DWORD)(void FAR *)rec_cb0);
            res("ToolHelpHook OutputDebugString calls", "", n);
            for (i = 0; i < n && i < 4; i++) {
                WORD FAR *e = (WORD FAR *)&cb_log[i * 16];
                obsn("ToolHelpHook call AX", i, e[1]);
                if (!IsBadStringPtr((LPSTR)MAKELP(e[8], e[5]), 64))
                    obs_str("ToolHelpHook call ES:SI", (LPSTR)MAKELP(e[8], e[5]));
            }
        }
    }
    /* RegisterPtrace (202): no result (Schulman) */
    if (K2("RegisterPtrace(0)", 202, 0, 0, 0)) {
        res_env("RegisterPtrace(0)", "AX", r_ax);
        res_env("RegisterPtrace(0)", "DX", r_dx);
    }
}

static void t_stack(void)
{
    HGLOBAL h = GlobalAlloc(GMEM_FIXED | GMEM_ZEROINIT, 0x1000);
    /* the block's selector: under KRNL286 its handle, under KRNL386 the
       handle | 1 (a fixed block's handle is even there: tk58.c) */
    WORD s = SELECTOROF(GlobalLock(h)), i, FAR *hp = (WORD FAR *)MAKELP(SEG(buf), 0);
    WORD before[8];
    FARPROC to = kproc(108), back = kproc(109);
    if (!to || !back) {
        obs("SwitchStackTo missing", 1);
        return;
    }
    for (i = 0; i < 8; i++)
        before[i] = hp[i];
    out("SwitchStackTo...\r\n");
    sst_test(to, back, s, 0xF00, 0x100);
    res("SwitchStackTo SS==new", "", sst_regs[0] == s);
    res_env("SwitchStackTo", "SP", sst_regs[1]);
    res("SwitchStackTo SP", "", sst_regs[1]);
    res("SwitchStackTo BP==SP+?", "", sst_regs[2] - sst_regs[1]);
    res("SwitchStackTo DS==ours", "", sst_regs[3] == SEG(buf));
    res("SwitchStackTo DS==new", "", sst_regs[3] == s);
    res("SwitchStackTo BX", "", sst_regs[6]);
    res("SwitchStackTo CX", "", sst_regs[7]);
    for (i = 0; i < 8; i++)
        if (i == 1 || i == 2)
            obs_env(i == 1 ? "SwitchStackTo new seg word 2" : "SwitchStackTo new seg word 4",
                    sst_hdr[i]);
        else
            obsn("SwitchStackTo new seg word", i * 2, sst_hdr[i]);
    res("SwitchStackTo new seg word 4==our SS", "", sst_hdr[2] == SEG(buf));
    for (i = 0; i < 8; i++)
        if (sst_hdr[8 + i] != before[i]) {
            obsn("SwitchStackTo changed our DGROUP word", i * 2, 1);
            if (i == 2 || i == 3)
                obsn("SwitchStackTo our DGROUP word", i * 2, sst_hdr[8 + i]);
        }
    res("SwitchStackBack SS==ours", "", sst_regs[8] == SEG(buf));
    res("SwitchStackBack AX", "", sst_regs[12]);
    res("SwitchStackBack DX", "", sst_regs[13]);
    res("SwitchStackBack BX==our SS", "", sst_regs[14] == SEG(buf));
    res("SwitchStackBack CX==new seg word 2", "", sst_regs[15] == sst_hdr[1]);
    res("SwitchStackBack SP==new seg word 2 + 2", "", sst_regs[9] == sst_hdr[1] + 2);
    res_env("SwitchStackBack", "SP", sst_regs[9]);
    for (i = 0; i < 8; i++)
        if (hp[i] != before[i])
            obsn("SwitchStackBack left our DGROUP word changed", i * 2, 1);
    GlobalFree(h);
}

static void t_patch(void)
{
    BYTE FAR *p = (BYTE FAR *)(FARPROC)PATCHME;
    WORD hs = LOWORD(GlobalHandle(SEG(p))), i;
    if (K1("PatchCodeHandle(0)", 110, 1, 0)) {
        res("PatchCodeHandle(0)", "AX", r_ax);
        res("PatchCodeHandle(0)", "DX", r_dx);
    }
    if (K1("PatchCodeHandle(own code)", 110, 0, hs)) {
        res("PatchCodeHandle(own code) AX==handle", "", r_ax == hs);
        res("PatchCodeHandle(own code) AX==CS", "", r_ax == SEG(p));
        res("PatchCodeHandle(own code) DX==CS", "", r_dx == SEG(p));
        res_env("PatchCodeHandle(own code)", "AX", r_ax);
        res_env("PatchCodeHandle(own code)", "DX", r_dx);
        for (i = 0; i < 4; i++)
            obsn("PATCHME byte", i, p[i]);
    }
}

static void t_callproc(void)
{
    static WORD wnd[3];
    FARPROC f = kproc(53);
    if (!f) {
        obs("CallProcInstance missing", 1);
        return;
    }
    out("CallProcInstance...\r\n");
    wnd[0] = SEG(buf);
    wnd[1] = OFF(rec_cb10);
    wnd[2] = SEG(rec_cb10);
    cb_count = 0;
    cb_ret = 0x5A5A;
    w_es = SEG(wnd);
    w_bx = OFF(wnd);
    w_ds = (WORD)GetCurrentTask();      /* CallProcInstance sets DS */
    w_call(f, 5, 0x1234, 0x0F, 0x22, 0x3344, 0x5566, 0, 0, 0);
    w_ds = w_es = w_bx = 0;
    res("CallProcInstance AX", "", w_regs[0]);
    res("CallProcInstance left", "", w_popped);
    res("CallProcInstance calls", "", cb_count);
    if (cb_count) {
        WORD FAR *e = (WORD FAR *)&cb_log[0];
        res("CallProcInstance proc DS==hInstance", "", e[7] == SEG(buf));
        res("CallProcInstance proc arg lParam lo", "", e[9]);
        res("CallProcInstance proc arg msg", "", e[12]);
        res("CallProcInstance proc AX==hInstance", "", e[1] == SEG(buf));
        res("CallProcInstance proc BX==wnd", "", e[2] == OFF(wnd));
        res("CallProcInstance proc ES==wnd", "", e[8] == SEG(wnd));
    }
}

/* EMSCopy (KERNEL.160) with seven zero words ended the probe on Windows 3.1
   (KRNL286), so it is only tried on request */
static int try_emscopy;

static void t_emscopy(void)
{
    out("EMSCopy...\r\n");
    if (kc("EMSCopy", 160, 0, 7, 0, 0, 0, 0, 0, 0, 0, 0)) {
        res("EMSCopy", "AX", r_ax);
        res("EMSCopy", "DX", r_dx);
    }
}

static void t_swaparea(void)
{
    out("SetSwapAreaSize...\r\n");
    if (K1("SetSwapAreaSize(0)", 106, 0, 0)) {
        res("SetSwapAreaSize(0)", "AX", r_ax);
        res("SetSwapAreaSize(0)", "DX", r_dx);
    }
    if (K1("SetSwapAreaSize(100h)", 106, 0, 0x100)) {
        res("SetSwapAreaSize(100h)", "AX", r_ax);
        res_env("SetSwapAreaSize(100h)", "DX", r_dx);
    }
}

/* IsROMFile (KERNEL.326) with 0 and with KERNEL's hModule ended the probe on
   Windows 3.1 (KRNL286), so it is only tried on request */
static int try_romfile;

static void t_romfile(void)
{
    out("IsROMFile...\r\n");
    if (K1("IsROMFile(hk)", 326, 0, hk)) {
        res("IsROMFile(hk)", "AX", r_ax);
        res("IsROMFile(hk)", "DX", r_dx);
    }
}

/* DebugDefineSegment (KERNEL.314) ended the probe on Windows 3.1 (KRNL286),
   so it is only tried on request */
static int try_debugseg;

static void t_debugseg(void)
{
    if (kc("DebugDefineSegment", 314, 0, 6, SEG("TK50"), OFF("TK50"), 0, SEG(buf), 0, 1,
           0, 0)) {
        res("DebugDefineSegment", "AX", r_ax);
        res("DebugDefineSegment", "DX", r_dx);
    }
}

static void t_handletable(void)
{
    out("DefineHandleTable...\r\n");
    if (K1("DefineHandleTable(0)", 94, 0, 0)) {
        res_env("DefineHandleTable(0)", "AX", r_ax);
        res("DefineHandleTable(0)", "DX", r_dx);
    }
}

static void t_initlib(void)
{
    HGLOBAL h = GlobalAlloc(GMEM_FIXED | GMEM_ZEROINIT, 0x400);
    WORD s = (WORD)h, i;
    WORD FAR *p = (WORD FAR *)MAKELP(s, 0);
    if (!kproc(116)) {
        obs("InitLib missing", 1);
        return;
    }
    out("InitLib...\r\n");
    w_ds = s;
    K0("InitLib", 116, 0);
    w_ds = 0;
    res("InitLib", "AX", r_ax);
    res("InitLib DS kept", "", w_regs[7] == s);
    res("InitLib ES==KERNEL DS", "", thhook && w_regs[8] == SEG(thhook));
    for (i = 0; i < 16; i++)
        obsn("InitLib seg word", i * 2, p[i]);
    GlobalFree(h);
}

int __cdecl tmain(void)
{
    hk = GetModuleHandle("KERNEL");
    out("TK50 start\r\n");
    thhook = (WORD FAR *)kproc(332);
    obs("THHOOK missing", thhook == 0);
    {
        DWORD (FAR PASCAL *gmh)(void) = (DWORD (FAR PASCAL *)(void))kproc(28);
        obs("THHOOK 00h==GlobalMasterHandle", thhook &&
            thhook[0] == LOWORD(gmh()));
    }
    t_heapspaces();
    t_hmemcpy();
    t_dbcs();
    t_priority();
    t_simple();
    t_files();
    t_selectors();
    t_localnotify();
    t_dosproc();
    t_hooks();
    t_patch();
    t_stack();
    t_initlib();
    t_callproc();
    if (try_emscopy)            /* it ended the probe on 3.1: see above */
        t_emscopy();
    t_handletable();
    t_swaparea();
    if (try_romfile)            /* it ended the probe on 3.1: see above */
        t_romfile();
    if (try_debugseg)           /* it ended the probe on 3.1: see above */
        t_debugseg();
    out("TK50 done, failures=");
    outhex((WORD)failures);
    out("\r\n");
    /* last, after the summary: DoSignal (139), KEYBOARD's Ctrl-Break
       (Schulman). Not called: CVWBreak (205), which the keyboard
       interrupt handler jumps to (Schulman: an interrupt procedure), and
       IsRomModule (323); each, called as a far procedure, ended the probe
       on 3.1 (KRNL286) */
    if (K0("DoSignal", 139, 0)) {
        res("DoSignal", "AX", r_ax);
        res("DoSignal", "DX", r_dx);
    }
    return failures;
}
