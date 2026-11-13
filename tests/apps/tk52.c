/*
 * tk52.c - the KERNEL exports that end a program: FatalAppExit
 * (KERNEL.137), DebugBreak (KERNEL.203) and UndefDynlink (KERNEL.120),
 * one per run, picked by the first letter of the command tail (f, b, u).
 * Run as the shell with a screenshot (tests/dropin.sh -c 'WIN /S /B f'
 * -s shot.png, and -r for Windows 3.1): TK.LOG says whether the call
 * returned; the screen shows what Windows put up.
 */
#include <windows.h>

#define TK_REF "tk52ref.h"
#include "tklog.h"

static char msg[] = "TK52 FatalAppExit message";

int __cdecl tmain(void)
{
    char FAR *tail = (char FAR *)MAKELP(init_regs[6], init_regs[1]);
    HMODULE hk = GetModuleHandle("KERNEL");
    char c = tail[0];
    out("TK52 start, tail=");
    out(tail);
    out("\r\n");
    if (tail[0] && tail[1] == 'q') {    /* with a message queue */
        WORD (FAR PASCAL *we)(WORD) =
            (WORD (FAR PASCAL *)(WORD))GetProcAddress(hk, MAKEINTRESOURCE(30));
        int (FAR PASCAL *ia)(WORD) = (int (FAR PASCAL *)(WORD))
            GetProcAddress(GetModuleHandle("USER"), MAKEINTRESOURCE(5));
        we(0);
        obs("InitApp", ia(init_regs[5]) != 0);
    }
    if (c == 'f') {
        void (FAR PASCAL *f)(WORD, LPCSTR) =
            (void (FAR PASCAL *)(WORD, LPCSTR))GetProcAddress(hk, MAKEINTRESOURCE(137));
        out("calling FatalAppExit(0, msg)\r\n");
        if (f)
            f(0, msg);
        out("FatalAppExit returned\r\n");
    } else if (c == 's') {
        /* USER's SysErrorBox (USER.320) with button code 8 in the middle,
           to compare with FatalAppExit's box */
        int (FAR PASCAL *f)(LPCSTR, LPCSTR, WORD, WORD, WORD) =
            (int (FAR PASCAL *)(LPCSTR, LPCSTR, WORD, WORD, WORD))
            GetProcAddress(GetModuleHandle("USER"), MAKEINTRESOURCE(320));
        out("calling SysErrorBox(button 8)\r\n");
        if (f)
            obs("SysErrorBox", f(msg, "Button 8", 0, 0x8008, 0));
    } else if (c == 'b') {
        void (FAR PASCAL *f)(void) =
            (void (FAR PASCAL *)(void))GetProcAddress(hk, MAKEINTRESOURCE(203));
        out("calling DebugBreak\r\n");
        if (f)
            f();
        out("DebugBreak returned\r\n");
    } else if (c == 'u') {
        void (FAR PASCAL *f)(void) =
            (void (FAR PASCAL *)(void))GetProcAddress(hk, MAKEINTRESOURCE(120));
        out("calling UndefDynlink\r\n");
        if (f)
            f();
        out("UndefDynlink returned\r\n");
    }
    out("TK52 done\r\n");
    return 0;
}
