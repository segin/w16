/*
 * tk38.c - KERNEL's GP-fault recovery tables and signal handlers:
 *  - "__GP" (KERNEL.342 in KERNEL; Schulman, Undocumented Windows ch. 5:
 *    a table of {segment, low offset, high offset, handler offset}
 *    ending with a 0 segment) in KERNEL, USER, GDI and DISPLAY: whether
 *    it is exported and its first entries;
 *  - HasGPHandler (KERNEL.338): for an address inside a table range, at
 *    its ends and outside;
 *  - SetSigHandler (KERNEL.140) and DoSignal (KERNEL.139): what
 *    SetSigHandler returns and stores (Schulman: the TDB's 26h), and
 *    what DoSignal calls the handler with.
 * Run as the shell (tests/dropin.sh, and -r for 3.1).
 */
#include <windows.h>

#define TK_REF "tk38ref.h"
#include "tklog.h"

typedef struct { WORD seg, low, high, func; } GPTAB;
typedef WORD (FAR PASCAL *HASGP)(DWORD);
typedef WORD (FAR PASCAL *SETSIG)(FARPROC, DWORD FAR *, WORD FAR *, WORD, WORD);
typedef void (FAR PASCAL *DOSIG)(void);

static WORD sig_calls, sig_a, sig_b;
static HASGP hasgp;

WORD FAR PASCAL __export SigProc(WORD a, WORD b)
{
    sig_calls++;
    sig_a = a;
    sig_b = b;
    return 0x1234;
}

static void table(const char *mod)
{
    HMODULE h = GetModuleHandle(mod);
    GPTAB FAR *p;
    WORD n = 0, i;
    out(mod);
    out("\r\n");
    if (!h)
        return;
    p = (GPTAB FAR *)GetProcAddress(h, "__GP");
    obs("  __GP exported", p != NULL);
    if (!p)
        return;
    if (!lstrcmpi(mod, "KERNEL"))
        obs("  __GP is KERNEL.342", (FARPROC)p == GetProcAddress(h, MAKEINTRESOURCE(342)));
    for (n = 0; n < 200 && p[n].seg; n++)
        ;
    obs("  entries", n);
    for (i = 0; i < n && i < 6; i++) {
        obsn("  seg is CS of __GP's segment", i, p[i].seg == SELECTOROF(p));
        obsn("  low", i, p[i].low);
        obsn("  high", i, p[i].high);
        obsn("  func", i, p[i].func);
    }
    if (n && hasgp) {
        GPTAB e = p[0];
        obs("  HasGPHandler(low)", hasgp(MAKELONG(e.low, e.seg)));
        obs("  HasGPHandler(low+1)", hasgp(MAKELONG(e.low + 1, e.seg)));
        obs("  HasGPHandler(high-1)", hasgp(MAKELONG(e.high - 1, e.seg)));
        obs("  HasGPHandler(high)", hasgp(MAKELONG(e.high, e.seg)));
        obs("  HasGPHandler(low-1)", hasgp(MAKELONG(e.low - 1, e.seg)));
        obs("  HasGPHandler(func)", hasgp(MAKELONG(e.func, e.seg)));
    }
}

int __cdecl tmain(void)
{
    HMODULE k = GetModuleHandle("KERNEL");
    SETSIG setsig = (SETSIG)GetProcAddress(k, MAKEINTRESOURCE(140));
    DOSIG dosig = (DOSIG)GetProcAddress(k, MAKEINTRESOURCE(139));
    WORD FAR *tdb = (WORD FAR *)MAKELP(GetCurrentTask(), 0);
    DWORD old = 0x11111111;
    WORD oldtype = 0x2222, r;
    FARPROC fp;

    out("TK38 start\r\n");
    hasgp = (HASGP)GetProcAddress(k, MAKEINTRESOURCE(338));
    obs("HasGPHandler exported", hasgp != NULL);
    table("KERNEL");
    table("USER");
    table("GDI");
    table("DISPLAY");

    obs("SetSigHandler exported", setsig != NULL);
    obs("TDB 26h before", tdb[0x26 / 2]);
    obs("TDB 28h before", tdb[0x28 / 2]);
    if (setsig) {
        fp = MakeProcInstance((FARPROC)SigProc, (HINSTANCE)init_regs[5]);
        r = setsig(fp, &old, &oldtype, 2, 1);
        obs("SetSigHandler returns", r);
        obs("  old proc off", LOWORD(old));
        obs("  old proc sel", HIWORD(old));
        obs("  old type", oldtype);
        obs("TDB 26h is the new proc (off)", tdb[0x26 / 2] == OFFSETOF(fp));
        obs("TDB 28h is the new proc (sel)", tdb[0x28 / 2] == SELECTOROF(fp));
        obs("TDB 24h", tdb[0x24 / 2]);
        if (dosig) {
            dosig();
            obs("DoSignal: handler calls", sig_calls);
            obs("  a (first)", sig_a);
            obs("  b (second)", sig_b);
        }
        old = 0x11111111;
        oldtype = 0x2222;
        r = setsig((FARPROC)0, &old, &oldtype, 0, 1);
        obs("SetSigHandler(0, type 0) returns", r);
        obs("  old was ours", old == (DWORD)fp);
        obs("  old type", oldtype);
        obs("TDB 26h after", tdb[0x26 / 2]);
    }
    out("TK38 done\r\n");
    return 0;
}
