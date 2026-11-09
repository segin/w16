/*
 * tk40.c - the state a "__GP" recovery routine is entered in: loads
 * PENWIN.DLL (tests/apps/tk40d.asm, a stand-in whose GPFAULT faults
 * inside the range of its own __GP table), checks HasGPHandler
 * (KERNEL.338) knows the range, makes it fault, and logs the registers,
 * flags and stack its recovery routine saw. Run as the shell
 * (tests/dropin.sh, and -r for 3.1).
 */
#include <windows.h>

#define TK_REF "tk40ref.h"
#include "tklog.h"

typedef WORD (FAR PASCAL *HASGP)(DWORD);
typedef void (FAR PASCAL *VOIDFN)(void);
typedef void (FAR PASCAL *SETFN)(WORD);

static const char *names[] = { "ax", "bx", "cx", "dx", "si", "di", "bp",
    "sp", "ds", "es", "ss", "flags" };

int __cdecl tmain(void)
{
    HINSTANCE h;
    WORD FAR *r, FAR *gp, sel, i;
    HASGP hasgp = (HASGP)GetProcAddress(GetModuleHandle("KERNEL"), MAKEINTRESOURCE(338));
    char n[16];

    out("TK40 start\r\n");
    h = LoadLibrary("C:\\PENWIN.DLL");
    obs("PENWIN loaded", h >= (HINSTANCE)32);
    if (h < (HINSTANCE)32)
        return 1;
    r = (WORD FAR *)GetProcAddress(h, "RESULTS");
    gp = (WORD FAR *)GetProcAddress(h, "__GP");
    obs("__GP found", gp != NULL);
    obs("  __GP seg is GPFAULT's", gp[0] == SELECTOROF(GetProcAddress(h, "GPFAULT")));
    obs("  range", gp[2] - gp[1]);
    if (hasgp) {
        obs("HasGPHandler(low) is recovery", hasgp(MAKELONG(gp[1], gp[0])) == gp[3]);
        obs("HasGPHandler(high)", hasgp(MAKELONG(gp[2], gp[0])));
    }
    sel = AllocSelector(0);
    FreeSelector(sel);
    ((SETFN)GetProcAddress(h, "SETBADSEL"))(sel);
    ((VOIDFN)GetProcAddress(h, "GPFAULT"))();
    obs("recovered", r[20]);
    obs("no fault", r[21]);
    for (i = 0; i < 12; i++)
        if (i == 7)
            obs("sp - sp before the fault", r[7] - r[22]);
        else if (i == 8)
            obs("ds is PENWIN's", r[8] == SELECTOROF(r));
        else if (i == 9)
            obs("es", r[9] == sel ? 0xBAD : r[9]);
        else if (i == 10)
            obs("ss is ours", r[10] == SELECTOROF((void FAR *)&h));
        else if (i == 11)
            obs("flags", r[11] & 0x0FD5);
        else
            obs(names[i], r[i]);
    for (i = 0; i < 8; i++) {
        wsprintf(n, "stk[%u]", i);
        obs(n, r[12 + i]);
    }
    obs("cs is GPFAULT's", r[23] == gp[0]);
    obs("stk[0] is the fault's IP", r[12] == gp[1]);
    obs("stk[1] is the error code", r[13] == (sel & 0xFFFC));
    obs("stk[1] is ss", r[13] == SELECTOROF((void FAR *)&h));
    obs("stk[1] is cs", r[13] == gp[0]);
    obs("stk[1] is our ds", r[13] == SELECTOROF((void FAR *)names));
    obs("es is our ds", r[9] == SELECTOROF((void FAR *)names));
    obs("es is es before the fault", r[9] == r[24]);
    obs("raw flags", r[11]);
    obs("bad sel", sel);
    {
        /* K327 called from inside a __GP range */
        static const WORD errs[] = { 0x8007, 0x7007, 0x6040, 0x0001 };
        for (i = 0; i < 4; i++) {
            wsprintf(n, "KCALL %04X", errs[i]);
            out(n);
            out("\r\n");
            ((SETFN)GetProcAddress(h, "KCALL"))(errs[i]);
            obs("  recovered", r[20]);
            obs("  no fault (K327 returned)", r[21]);
            if (r[21]) {
                obs("  sled: si - 5555h", r[4] - 0x5555);
                obs("  ax", r[0]);
                obs("  bx", r[1]);
                obs("  cx", r[2]);
                obs("  dx", r[3]);
                obs("  sp - sp before the call", r[7] - r[22]);
            }
            if (r[20]) {
                obs("  ax", r[0]);
                obs("  bx", r[1]);
                obs("  cx", r[2]);
                obs("  dx", r[3]);
                obs("  bp", r[6]);
                obs("  sp - sp before the call", r[7] - r[22]);
                obs("  stk[0]", r[12]);
                obs("  stk[1]", r[13]);
                obs("  stk[2]", r[14]);
                obs("  stk[3]", r[15]);
                obs("  flags", r[11]);
            }
        }
    }
    out("TK40 done\r\n");
    return 0;
}
