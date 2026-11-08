/*
 * tk1.c - M1 KERNEL test and probe.
 *
 * Output (through OutputDebugString), one item per line:
 *   PASS <name> / FAIL <name>   checks of behaviour we are confident about
 *   OBS <name>=<value>          observations of details not yet verified
 *                               against real Windows 3.1 (run this program
 *                               on both and compare these lines)
 * The exit code is the number of failures.
 */
#include <windows.h>

extern WORD __cdecl entry_regs[11];   /* ax bx cx dx si di bp sp ds es ss */
extern WORD __cdecl init_regs[7];    /* ax bx cx dx si di es */
extern WORD __cdecl get_ahincr(void);
extern WORD __cdecl get_0040h(void);
extern WORD __cdecl dos3call_getver(void);

#define E_AX 0
#define E_BX 1
#define E_CX 2
#define E_DX 3
#define E_SI 4
#define E_DI 5
#define E_BP 6
#define E_SP 7
#define E_DS 8
#define E_ES 9
#define E_SS 10
#define I_AX 0
#define I_BX 1
#define I_CX 2
#define I_DX 3
#define I_SI 4
#define I_DI 5
#define I_ES 6

static int failures;

static void out(const char *s)
{
    OutputDebugString(s);
}

static void outhex(WORD v)
{
    char b[5];
    int i;
    for (i = 3; i >= 0; i--) {
        b[i] = "0123456789ABCDEF"[v & 15];
        v >>= 4;
    }
    b[4] = 0;
    out(b);
}

static void check(int ok, const char *name)
{
    out(ok ? "PASS " : "FAIL ");
    out(name);
    out("\r\n");
    if (!ok)
        failures++;
}

static void obs(const char *name, WORD v)
{
    out("OBS ");
    out(name);
    out("=");
    outhex(v);
    out("\r\n");
}

static WORD sel_of(void FAR *p)
{
    return (WORD)((DWORD)p >> 16);
}

static WORD off_of(void FAR *p)
{
    return (WORD)(DWORD)p;
}

static void test_task_start(void)
{
    WORD FAR *inst = (WORD FAR *)MAKELP(entry_regs[E_DS], 0);

    check(entry_regs[E_DS] == entry_regs[E_SS], "entry DS == SS");
    check(entry_regs[E_SI] == 0, "entry hPrevInstance == 0");
    check(init_regs[I_AX] != 0, "InitTask succeeds");
    check(init_regs[I_DI] == entry_regs[E_DI], "InitTask DI == hInstance");
    check(init_regs[I_ES] == entry_regs[E_ES], "InitTask ES == PSP");
    check(init_regs[I_SI] == 0, "InitTask SI == hPrevInstance");

    obs("entry.AX", entry_regs[E_AX]);
    obs("entry.BX(stack)", entry_regs[E_BX]);
    obs("entry.CX(heap)", entry_regs[E_CX]);
    obs("entry.DX", entry_regs[E_DX]);
    obs("entry.BP", entry_regs[E_BP]);
    obs("entry.SP", entry_regs[E_SP]);
    obs("entry.DI&1(hInstance odd)", entry_regs[E_DI] & 1);
    obs("entry.DI==DS", entry_regs[E_DI] == entry_regs[E_DS]);
    obs("InitTask.BX(cmdline)", init_regs[I_BX]);
    obs("InitTask.CX(stack limit)", init_regs[I_CX]);
    obs("InitTask.DX(nCmdShow)", init_regs[I_DX]);
    obs("inst[0A](stack top)", inst[5]);
    obs("inst[0C](stack min)", inst[6]);
    obs("inst[0E](stack bottom)", inst[7]);
}

static void test_version(void)
{
    DWORD v = GetVersion();
    WORD dos = dos3call_getver();
    DWORD f = GetWinFlags();

    check(LOWORD(v) == 0x0A03, "GetVersion == 3.10");
    check(HIWORD(v) == (WORD)(((dos & 0xFF) << 8) | (dos >> 8)),
          "GetVersion DOS version matches DOS3Call");
    check((f & WF_PMODE) != 0, "GetWinFlags WF_PMODE");
    check((f & (WF_STANDARD | WF_ENHANCED)) != 0, "GetWinFlags mode bit");
    obs("GetWinFlags", (WORD)f);
}

static void test_global(void)
{
    HGLOBAL h, hz, hf, hh;
    BYTE FAR *p;
    BYTE FAR *q;
    WORD i, inc, sel;
    int ok;

    h = GlobalAlloc(GMEM_MOVEABLE, 1000);
    check(h != 0, "GlobalAlloc moveable");
    p = GlobalLock(h);
    check(p != NULL && off_of(p) == 0, "GlobalLock gives sel:0");
    for (i = 0; i < 1000; i++)
        p[i] = (BYTE)i;
    ok = 1;
    for (i = 0; i < 1000; i++)
        if (p[i] != (BYTE)i)
            ok = 0;
    check(ok, "moveable block read/write");
    check(GlobalSize(h) >= 1000, "GlobalSize >= requested");
    check((GlobalFlags(h) & GMEM_LOCKCOUNT) == 1, "GlobalFlags lock count 1");
    check(LOWORD(GlobalHandle(sel_of(p))) == h, "GlobalHandle(sel) == handle");
    obs("moveable handle&1", h & 1);
    obs("moveable sel^handle", sel_of(p) ^ h);
    obs("GlobalSize(1000)", (WORD)GlobalSize(h));
    check(GlobalUnlock(h) == 0, "GlobalUnlock to zero returns 0");
    check(GlobalFree(h) == 0, "GlobalFree returns 0");

    hz = GlobalAlloc(GMEM_MOVEABLE | GMEM_ZEROINIT, 4000);
    p = GlobalLock(hz);
    ok = p != NULL;
    for (i = 0; ok && i < 4000; i++)
        if (p[i])
            ok = 0;
    check(ok, "GMEM_ZEROINIT block is zero");
    GlobalUnlock(hz);
    GlobalFree(hz);

    hf = GlobalAlloc(GMEM_FIXED, 100);
    p = GlobalLock(hf);
    check(hf != 0 && p != NULL, "GlobalAlloc fixed");
    obs("fixed handle&1", hf & 1);
    obs("fixed handle==sel", hf == sel_of(p));
    GlobalFree(hf);

    /* huge block: tiles are __AHINCR apart */
    inc = get_ahincr();
    check(inc != 0 && (inc & 7) == 0, "__AHINCR is a selector increment");
    obs("__AHINCR", inc);
    hh = GlobalAlloc(GMEM_MOVEABLE, 200000L);
    check(hh != 0, "GlobalAlloc 200000 bytes");
    p = GlobalLock(hh);
    sel = sel_of(p);
    ok = p != NULL;
    if (ok) {
        q = (BYTE FAR *)MAKELP(sel + 2 * inc, 0x1234);    /* byte 0x21234 */
        *q = 0x5A;
        p[0] = 0xA5;
        ok = *q == 0x5A && p[0] == 0xA5;
    }
    check(ok, "huge block tile access");
    check(GlobalSize(hh) >= 200000L, "huge GlobalSize");
    GlobalUnlock(hh);
    check(GlobalFree(hh) == 0, "huge GlobalFree");
}

static void test_selectors(void)
{
    WORD s = get_0040h();
    WORD FAR *bda;
    check(s != 0, "__0040H selector");
    if (s) {
        bda = (WORD FAR *)MAKELP(s, 0);
        obs("BDA COM1 port (0040:0000)", bda[0]);
    }
}

int __cdecl tmain(void)
{
    out("TK1 start\r\n");
    test_task_start();
    test_version();
    test_global();
    test_selectors();
    out("TK1 done, failures=");
    outhex((WORD)failures);
    out("\r\n");
    return failures;
}
