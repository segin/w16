/*
 * tk25.c - GetCodeInfo (KERNEL.104): the SEGINFO it fills for code in
 * this program, in SHELL.DLL and in USER, by procedure address and by
 * MAKELONG(segment number, hModule). Run as the shell (tests/dropin.sh,
 * and -r for Windows 3.1). USER's SetWindowsHook calls it on the hook
 * procedure.
 */
#include <windows.h>

#define TK_REF "tk25ref.h"
#include "tklog.h"

extern BOOL FAR PASCAL K_GETCODEINFO(FARPROC p, WORD FAR *info);

static void show(const char *what, FARPROC p)
{
    WORD info[8], i, r;
    char n[48];
    for (i = 0; i < 8; i++)
        info[i] = 0xEEEE;
    r = K_GETCODEINFO(p, info);
    lstrcpy(n, what);
    lstrcat(n, " result");
    obs(n, r);
    for (i = 0; i < 8; i++) {
        static const char *const f[8] = { "offSegment", "cbSegment", "flags",
            "cbAlloc", "h==sel-1|sel", "alignShift", "reserved0", "reserved1" };
        lstrcpy(n, what);
        lstrcat(n, " ");
        lstrcat(n, f[i]);
        if (i == 4)
            obs(n, info[4] == (HIWORD((DWORD)p) & ~1) ? 1 :
                   info[4] == HIWORD((DWORD)p) ? 2 : 0);
        else
            obs(n, info[i]);
    }
}

void FAR PASCAL dummy(void)
{
}

int __cdecl tmain(void)
{
    HMODULE sh, us;
    out("TK25 start\r\n");
    show("own", (FARPROC)dummy);
    show("own segno 1", (FARPROC)MAKELONG(1, GetModuleHandle("TK25")));
    sh = LoadLibrary("SHELL.DLL");
    show("SHELL RegisterShellHook", GetProcAddress(sh, MAKEINTRESOURCE(102)));
    show("SHELL segno 3", (FARPROC)MAKELONG(3, sh));
    us = GetModuleHandle("USER");
    show("USER MessageBox", GetProcAddress(us, MAKEINTRESOURCE(1)));
    show("bad", (FARPROC)MAKELONG(0x1234, 0x0007));
    out("TK25 done, failures=");
    outhex((WORD)failures);
    out("\r\n");
    return failures;
}
