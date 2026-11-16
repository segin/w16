/*
 * tk41.c - what K327 (HandleParamError; Schulman, Undocumented Windows
 * ch. 5: some "__GP" recovery routines jump to it) does when a function
 * of the usual frame calls it: returns to it, or unwinds to its caller.
 * Calls tk41s.asm's FAKE_API(err, 7) for several error codes and logs
 * what came back. Run as the shell (tests/dropin.sh, and -r for 3.1).
 */
#include <windows.h>

#define TK_REF "tk41ref.h"
#include "tklog.h"

extern WORD __cdecl k41_cont, k41_regs[6], k41_back[8];
extern void __cdecl __far k41_call(WORD err);
extern FARPROC __cdecl k41_fn;
extern DWORD FAR PASCAL FAKE_API(WORD err, WORD x);

static void try(WORD err)
{
    char n[24];
    wsprintf(n, "err %04X", err);
    out(n);
    out("\r\n");
    k41_cont = 0xFFFF;
    k41_call(err);
    obs("  returned to fake_api", k41_cont);
    obs("  caller: AX", k41_back[0]);
    obs("  caller: BX", k41_back[1]);
    obs("  caller: CX", k41_back[2]);
    obs("  caller: DX", k41_back[3]);
    obs("  caller: SI", k41_back[4]);
    obs("  caller: DI", k41_back[5]);
    obs("  caller: SP change", k41_back[6]);
    obs("  caller: carry", k41_back[7] & 1);
    if (k41_cont == 1) {
        obs("  AX after K327", k41_regs[0]);
        obs("  BX after K327", k41_regs[1]);
        obs("  CX after K327", k41_regs[2]);
        obs("  DX after K327", k41_regs[3]);
    }
}

int __cdecl tmain(void)
{
    out("TK41 start\r\n");
    k41_fn = GetProcAddress(GetModuleHandle("KERNEL"), MAKEINTRESOURCE(327));
    obs("K327 exported", k41_fn != NULL);
    if (!k41_fn)
        return 1;
    try(0xC007);
    try(0x8001);
    try(0x0001);
    try(0x4001);
    try(0x6040);
    try(0x7007);
    out("TK41 done\r\n");
    return 0;
}
