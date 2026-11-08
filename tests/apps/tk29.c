/*
 * tk29.c - module usage counts under a booted Windows: KERNEL's, USER's,
 * GDI's, before and after loading and freeing DLLs that import them
 * (SHELL.DLL, TOOLHELP.DLL, LZEXPAND.DLL, VER.DLL, a font), as the shell
 * (tests/dropin.sh, and -r for Windows 3.1).
 */
#include <windows.h>

#define TK_REF "tk29ref.h"
#include "tklog.h"

static const char *const mods[] = { "KERNEL", "USER", "GDI", "SYSTEM", "KEYBOARD", 0 };

static void usage(const char *when)
{
    WORD i;
    char n[64];
    for (i = 0; mods[i]; i++) {
        lstrcpy(n, when);
        lstrcat(n, " ");
        lstrcat(n, mods[i]);
        obs(n, GetModuleUsage(GetModuleHandle(mods[i])));
    }
}

int __cdecl tmain(void)
{
    HINSTANCE a, b, c, d, e;
    out("TK29 start\r\n");
    usage("start");
    a = LoadLibrary("SHELL.DLL");
    obs("SHELL loaded", a >= 32);
    usage("+SHELL");
    b = LoadLibrary("TOOLHELP.DLL");
    obs("TOOLHELP loaded", b >= 32);
    usage("+TOOLHELP");
    c = LoadLibrary("LZEXPAND.DLL");
    obs("LZEXPAND loaded", c >= 32);
    usage("+LZEXPAND");
    d = LoadLibrary("VER.DLL");
    obs("VER loaded", d >= 32);
    usage("+VER");
    e = LoadLibrary("SSERIFE.FON");
    obs("SSERIFE loaded", e >= 32);
    usage("+SSERIFE");
    if (e >= 32) FreeLibrary(e);
    if (d >= 32) FreeLibrary(d);
    if (c >= 32) FreeLibrary(c);
    if (b >= 32) FreeLibrary(b);
    if (a >= 32) FreeLibrary(a);
    usage("freed");
    out("TK29 done, failures=");
    outhex((WORD)failures);
    out("\r\n");
    return failures;
}
