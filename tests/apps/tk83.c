/*
 * tk83.c - how much of its caller's stack each of KERNEL's string
 * functions uses below the return address. Found with Adobe Type
 * Manager 3.0's installer: it loads an entry's name with LoadString into
 * a buffer below its stack pointer (a function's that has returned) and
 * then copies it with lstrcpy; Windows 3.1's lstrcpy leaves that part of
 * the stack alone, w16's wrote over it and the installer said its
 * configuration file was missing an entry with a name of stack garbage.
 *
 * Each function is called through tk83s.asm's meter, which fills the
 * stack below it with CDh and measures the lowest byte written. Logged:
 * "OBS use:NAME=N" (bytes below the return address's slot, the smallest
 * of 8 calls: an interrupt on the same stack would only add) and the
 * result's checks. Run as the shell on 3.1 and with w16's KERNEL (the
 * boot-test case strstack); w16 must not use more than 3.1 does.
 */
#include <windows.h>
#include <dos.h>

#define TK_REF "tk83ref.h"
#include "tklog.h"

extern DWORD __cdecl __far stackuse(FARPROC fn, WORD nargs, ...);

static char buf[64];
static char src[] = "Disk";

/* the smallest of 8 measurements; *res the call's AX */
static WORD meter(const char *name, FARPROC fn, WORD n, WORD a0, WORD a1, WORD a2,
                  WORD a3, WORD a4, WORD *res)
{
    WORD best = 0xFFFF, u, i;
    DWORD v;
    for (i = 0; i < 8; i++) {
        buf[0] = 'X';
        buf[1] = 0;
        v = stackuse(fn, n, a0, a1, a2, a3, a4);
        u = LOWORD(v);
        *res = HIWORD(v);
        if (u < best)
            best = u;
    }
    out("OBS use:");
    out(name);
    out("=");
    outhex(best);
    out("\r\n");
    return best;
}

int __cdecl tmain(void)
{
    HMODULE k = GetModuleHandle("KERNEL");
    FARPROC pcpy = GetProcAddress(k, MAKEINTRESOURCE(88));
    FARPROC pcat = GetProcAddress(k, MAKEINTRESOURCE(89));
    FARPROC plen = GetProcAddress(k, MAKEINTRESOURCE(90));
    FARPROC pcpyn = GetProcAddress(k, MAKEINTRESOURCE(353));
    WORD bs = FP_SEG((char FAR *)buf), bo = FP_OFF((char FAR *)buf);
    WORD ss = FP_SEG((char FAR *)src), so = FP_OFF((char FAR *)src);
    WORD r;

    out("TK83 start\r\n");
    meter("lstrcpy", pcpy, 4, bs, bo, ss, so, 0, &r);
    check(r == bo && lstrcmp(buf, "Disk") == 0, "lstrcpy copies");
    meter("lstrcat", pcat, 4, bs, bo, ss, so, 0, &r);
    check(r == bo && lstrcmp(buf, "XDisk") == 0, "lstrcat appends");
    meter("lstrlen", plen, 2, ss, so, 0, 0, 0, &r);
    check(r == 4, "lstrlen counts");
    meter("lstrcpyn", pcpyn, 5, bs, bo, ss, so, 3, &r);
    check(r == bo && lstrcmp(buf, "Di") == 0, "lstrcpyn copies n - 1");
    meter("lstrcpy-nullsrc", pcpy, 4, bs, bo, 0, 0, 0, &r);
    out("TK83 end\r\n");
    return failures;
}
