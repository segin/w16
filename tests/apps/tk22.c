/*
 * tk22.c - the Windows boot probe: run as the shell of a scratch Windows
 * 3.1 install started with WIN /S /B (tests/dropin.sh), it records what
 * KERNEL has loaded by the time the shell runs, and writes to the boot
 * log:
 *   - every boot module's handle, usage count and file name;
 *   - DiagOutput (KERNEL.340, which USER imports; Schulman's name) with
 *     and without a CR/LF, to see what BOOTLOG.TXT gets;
 *   - a LoadLibrary that works and one that fails, to see whether they
 *     are logged after boot, and how a failure is.
 * BOOTLOG.TXT itself is the other half of the output.
 */
#include <windows.h>

#define TK_REF "tk22ref.h"
#include "tklog.h"

extern void FAR PASCAL K_DIAGOUTPUT(LPCSTR s);

static const char *const mods[] = {
    "KERNEL", "SYSTEM", "KEYBOARD", "MOUSE", "DISPLAY", "SOUND", "COMM",
    "FONTS", "OEMFONTS", "FIXFONTS", "GDI", "USER", "SHELL", "MMSYSTEM",
    "TIMER", "LZEXPAND", "VER", "PROGMAN", "TK22", NULL
};

int __cdecl tmain(void)
{
    char buf[128];
    WORD i, h;

    out("TK22 start\r\n");
    for (i = 0; mods[i]; i++) {
        h = GetModuleHandle(mods[i]);
        out(mods[i]);
        out(": ");
        if (!h) {
            out("not loaded\r\n");
            continue;
        }
        out("\r\n");
        obs_env("hmod", h);
        obs("usage", GetModuleUsage(h));
        buf[0] = 0;
        GetModuleFileName(h, buf, sizeof buf);
        obs_str("file", buf);
    }
    obs("GetWinFlags", (WORD)GetWinFlags());
    obs("GetNumTasks", GetNumTasks());
    K_DIAGOUTPUT("TK22 one");
    K_DIAGOUTPUT("TK22 two\r\n");
    K_DIAGOUTPUT("TK22 three");
    h = LoadLibrary("TKDLL.DLL");
    obs("LoadLibrary(TKDLL) > 32", h > 32);
    h = LoadLibrary("TKNOSUCH.DLL");
    obs("LoadLibrary(TKNOSUCH)", h);
    K_DIAGOUTPUT("TK22 end");
    out("TK22 done, failures=");
    outhex((WORD)failures);
    out("\r\n");
    return failures;
}
