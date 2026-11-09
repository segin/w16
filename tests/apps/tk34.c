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
    WORD ax = 0, i;
    for (i = 0; i < 8 && tail[i] && tail[i] != '\r'; i++)
        if (tail[i] >= '0' && tail[i] <= '9')
            ax = ax * 10 + (tail[i] - '0');
    out("TK34 start\r\n");
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
    obs_env("ES", u_regs[8]);
    out("TK34 done\r\n");
    return 0;
}
