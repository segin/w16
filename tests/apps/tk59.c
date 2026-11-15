/*
 * tk59.c - KERNEL when a module or a module it imports cannot be found:
 * LoadLibrary, WinExec and LoadModule return codes, and whether KERNEL
 * puts up its "File Error" box (screenshots), with and without
 * SetErrorMode(SEM_NOOPENFILEERRORBOX).
 *
 * Run as the shell, on a C: drive whose WINDOWS\SYSTEM has no OLECLI.DLL
 * (CARDFILE.EXE imports OLECLI), with real keys and screenshots
 * (tests/inputrun.sh with tests/scripts/input/tk59.txt; boot-test case
 * missingdll@tk59). Every step is announced by a message box "TK59 step
 * N" (OK); a box the step itself causes comes after it, so each Return
 * closes one box and the shots show which boxes there were. TK.LOG has
 * the return codes.
 */
#include <windows.h>

#define TK_REF "tk59ref.h"
#include "tklog.h"

static void step(int n, const char *what)
{
    char t[80];
    wsprintf(t, "TK59 step %d: %s", n, (LPSTR)what);
    out(t);
    out("\r\n");
    MessageBox(NULL, t, "TK59", MB_OK);
}

int __cdecl tmain(void)
{
    HMODULE hk = GetModuleHandle("KERNEL");
    WORD (FAR PASCAL *we)(WORD) =
        (WORD (FAR PASCAL *)(WORD))GetProcAddress(hk, MAKEINTRESOURCE(30));
    int (FAR PASCAL *ia)(WORD) = (int (FAR PASCAL *)(WORD))
        GetProcAddress(GetModuleHandle("USER"), MAKEINTRESOURCE(5));
    struct { WORD env; LPSTR cmd; LPVOID show; DWORD res; } lp;
    WORD showw[2] = { 2, SW_SHOWNORMAL };
    UINT r;
    out("TK59 start\r\n");
    we(0);
    obs("InitApp", ia(init_regs[5]) != 0);

    step(1, "LoadLibrary NOSUCH.DLL");
    SetErrorMode(0);
    r = (UINT)LoadLibrary("NOSUCH.DLL");
    obs("LoadLibrary.nosuch", r < 32 ? r : 0x20);

    step(2, "LoadLibrary nosuch");
    SetErrorMode(0);
    r = (UINT)LoadLibrary("nosuch");
    obs("LoadLibrary.nosuch.lower", r < 32 ? r : 0x20);

    step(3, "LoadLibrary C:\\NODIR\\NOSUCH.DLL");
    SetErrorMode(0);
    r = (UINT)LoadLibrary("C:\\NODIR\\NOSUCH.DLL");
    obs("LoadLibrary.nodir", r < 32 ? r : 0x20);

    step(4, "LoadLibrary nosuch, error mode 8000h");
    SetErrorMode(0x8000);
    r = (UINT)LoadLibrary("nosuch");
    obs("LoadLibrary.nosuch.8000", r < 32 ? r : 0x20);

    step(5, "WinExec NOSUCH.EXE");
    SetErrorMode(0);
    r = WinExec("NOSUCH.EXE", SW_SHOWNORMAL);
    obs("WinExec.nosuch", r < 32 ? r : 0x20);

    step(6, "WinExec CARDFILE.EXE");
    SetErrorMode(0);
    r = WinExec("CARDFILE.EXE", SW_SHOWNORMAL);
    obs("WinExec.cardfile", r < 32 ? r : 0x20);

    step(7, "WinExec CARDFILE.EXE, error mode 8000h");
    SetErrorMode(0x8000);
    r = WinExec("CARDFILE.EXE", SW_SHOWNORMAL);
    obs("WinExec.cardfile.8000", r < 32 ? r : 0x20);

    step(8, "LoadModule CARDFILE.EXE");
    SetErrorMode(0);
    lp.env = 0;
    lp.cmd = "\0";
    lp.show = showw;
    lp.res = 0;
    r = (UINT)LoadModule("CARDFILE.EXE", &lp);
    obs("LoadModule.cardfile", r < 32 ? r : 0x20);

    step(9, "LoadLibrary CARDFILE.EXE");
    SetErrorMode(0);
    r = (UINT)LoadLibrary("CARDFILE.EXE");
    obs("LoadLibrary.cardfile", r < 32 ? r : 0x20);

    step(10, "done");
    out("TK59 done\r\n");
    return 0;
}
