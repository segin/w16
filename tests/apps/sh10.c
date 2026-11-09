/*
 * sh10.c - SH10: where the "licensed to" lines of ShellAbout come from:
 * every string resource of the loaded system modules (LoadString), so
 * that the one holding the user's name (set by Setup) can be found.
 * Black-box: only what LoadString returns.
 */
#include <windows.h>

#define TK_REF "sh10ref.h"
#include "tklog.h"
#include "shprobe.h"

static char buf[300];

static void strings(const char *mod)
{
    HINSTANCE h = GetModuleHandle(mod);
    WORD i;
    int n;
    out("MODULE ");
    out(mod);
    out("\r\n");
    if (!h)
        return;
    for (i = 0; i < 0x8000; i++) {
        n = LoadString(h, i, buf, sizeof buf);
        if (n) {
            out("STR ");
            outhex(i);
            out(" ");
            out(buf);
            out("\r\n");
        }
    }
}

int __cdecl tmain(void)
{
    WaitEvent(0);
    InitApp((HINSTANCE)init_regs[5]);
    out("SH10 strings\r\n");
    strings("USER");
    strings("KERNEL");
    strings("GDI");
    strings("SHELL");
    strings("SYSTEM");
    strings("DISPLAY");
    out("SH10 done\r\n");
    return failures;
}
