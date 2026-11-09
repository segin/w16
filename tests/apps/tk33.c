/*
 * tk33.c - how KERNEL starts a DOS program: TK33.EXE is copied over
 * SYSTEM\WINOLDAP.MOD in the scratch install (tests/dropin.sh -f), and
 * SNAP (the shell) runs COMMAND.COM with WinExec. Whatever KERNEL starts
 * in WINOLDAP's place logs here what it was given: its module and file
 * name, the command tail (raw bytes), nCmdShow, hPrevInstance, and the
 * environment's first string. Run with -r on Windows 3.1 to see the
 * reference.
 */
#include <windows.h>

#define TK_REF "tk33ref.h"
#include "tklog.h"

int __cdecl tmain(void)
{
    LPSTR tail = (LPSTR)MAKELP(init_regs[6], init_regs[1]);   /* ES:BX */
    char buf[128];
    WORD i, n;
    out("TK33 start\r\n");
    GetModuleFileName(GetModuleHandle("WINOLDAP"), buf, sizeof buf);
    obs_str("file of module WINOLDAP", buf);
    GetModuleFileName(init_regs[5], buf, sizeof buf);
    obs_str("own file", buf);
    obs("nCmdShow (DX)", init_regs[3]);
    obs("hPrevInstance (SI)", init_regs[4]);
    obs_env("tail offset", init_regs[1]);
    n = (BYTE)tail[-1];
    obs("tail length byte before ES:BX", n);
    for (i = 0; i < 80 && tail[i] && tail[i] != '\r'; i++)
        buf[i] = tail[i];
    buf[i] = 0;
    obs_str("tail", buf);
    obs("tail length", i);
    dump("tail bytes", SELECTOROF(tail), OFFSETOF(tail) - 1, 24);
    GetModuleFileName(GetModuleHandle("WINOLDAP"), buf, sizeof buf);
    obs_str("again: file of module WINOLDAP", buf);
    obs_str("again: tail", tail);
    out("TK33 done\r\n");
    return 0;
}
