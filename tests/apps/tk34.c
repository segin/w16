/*
 * tk34.c - WinOldApCall (KERNEL.151), which WINOLDAP.MOD calls and no
 * published source describes. Calls it with 8 words on the stack, each
 * and AX = the command tail's number (default 0), and logs how many
 * bytes it popped and the registers it returned. Run as a program under
 * SNAP (tests/dropin.sh, and -r for Windows 3.1).
 */
#include <windows.h>

#define TK_REF "tk34ref.h"
#include "tklog.h"

extern WORD __cdecl u_regs[10];
extern WORD __cdecl u_popped;
extern void __cdecl __far u_call(FARPROC fn, int nzero, WORD ax, WORD bx, WORD cx);

int __cdecl tmain(void)
{
    LPSTR tail = (LPSTR)MAKELP(init_regs[6], init_regs[1]);
    FARPROC f = GetProcAddress(GetModuleHandle("KERNEL"), MAKEINTRESOURCE(151));
    WORD ax = 0, i, num[2], n = 0, in = 0;
    num[0] = num[1] = 0;
    for (i = 0; i < 16 && tail[i] && tail[i] != '\r'; i++)
        if (tail[i] >= '0' && tail[i] <= '9') {
            if (!in && n < 2)
                n++;
            in = 1;
            num[n - 1] = num[n - 1] * 10 + (tail[i] - '0');
        } else
            in = 0;
    if (n == 2) {
        f = GetProcAddress(GetModuleHandle("KERNEL"), MAKEINTRESOURCE(num[0]));
        ax = num[1];
    } else
        ax = num[0];
    out("TK34 start\r\n");
    obs("ordinal", n == 2 ? num[0] : 151);
    obs("WinOldApCall exported", f != 0);
    obs("AX given", ax);
    if (!f)
        return 1;
    u_call(f, 8, ax, 0x2222, 0x3333);
    obs("bytes popped", u_popped);
    obs("AX", u_regs[0]);
    obs("BX", u_regs[1]);
    obs("CX", u_regs[2]);
    obs("DX", u_regs[3]);
    obs("SI kept", u_regs[4] == 0x5555);
    obs("DI kept", u_regs[5] == 0x6666);
    obs("carry", u_regs[9] & 1);
    obs("GetCurrentPDB DX", HIWORD(GetCurrentPDB()));
    obs("GetCurrentPDB AX", LOWORD(GetCurrentPDB()));
    for (i = 0; i < 4; i += 3) {
        WORD s = u_regs[i];
        DWORD b = GetSelectorBase(s);
        obs(i ? "DX base hi" : "AX base hi", HIWORD(b));
        obs(i ? "DX base lo" : "AX base lo", LOWORD(b));
        obs(i ? "DX limit" : "AX limit", (WORD)GetSelectorLimit(s));
    }
    obs_env("ES", u_regs[8]);
    {
        BYTE FAR *p = (BYTE FAR *)MAKELP(u_regs[3], u_regs[0]);
        if (u_regs[3] && !IsBadReadPtr(p, 16))
            for (i = 0; i < 16; i += 2)
                obs("  [DX:AX+i]", *(WORD FAR *)(p + i));
    }
    if (n == 2 && num[0] == 343) {
        /* what GetWinOldApHooks (344) returns after it */
        u_call(GetProcAddress(GetModuleHandle("KERNEL"), MAKEINTRESOURCE(344)),
               0, 0, 0, 0);
        obs("then 344 AX", u_regs[0]);
        obs("then 344 DX", u_regs[3]);
        obs("then 344 BX", u_regs[1]);
        obs("then 344 CX", u_regs[2]);
        {
            /* three distinct words (Pascal: 1111h pushed first) */
            typedef DWORD (FAR PASCAL *F3)(WORD, WORD, WORD);
            typedef DWORD (FAR PASCAL *F0)(void);
            F3 r = (F3)f;
            F0 g = (F0)GetProcAddress(GetModuleHandle("KERNEL"),
                                      MAKEINTRESOURCE(344));
            DWORD v = r(0x1111, 0x2222, 0x3333);
            obs("343(1111,2222,3333) AX", LOWORD(v));
            v = g();
            obs("then 344 AX", LOWORD(v));
            obs("then 344 DX", HIWORD(v));
            r(0, 0, 0);
        }
    }
    out("TK34 done\r\n");
    return 0;
}
