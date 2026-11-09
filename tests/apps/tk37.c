/*
 * tk37.c - who sets the RT_ICON/RT_CURSOR handlers of a module loaded
 * after USER: SetResourceHandler on MORICONS.DLL (loaded here) and on
 * this program returns the type's handler, compared with USER.357
 * (LoadDIBIconHandler) and USER.356 (LoadDIBCursorHandler) and with the
 * RT_BITMAP handler (KERNEL's default). Run as the shell
 * (tests/dropin.sh, and -r for 3.1).
 * Result so far: 3.1 returns 0 for every type here (MORICONS.DLL, the
 * program, SHELL.DLL), so the probe does not tell yet; w16 returns
 * USER.357 for MORICONS.DLL's RT_ICON.
 */
#include <windows.h>

#define TK_REF "tk37ref.h"
#include "tklog.h"

static void look(const char *what, HINSTANCE h)
{
    HMODULE u = GetModuleHandle("USER");
    FARPROC i357 = GetProcAddress(u, MAKEINTRESOURCE(357));
    FARPROC c356 = GetProcAddress(u, MAKEINTRESOURCE(356));
    RSRCHDLRPROC icon, cur, bmp;
    out(what);
    out("\r\n");
    icon = SetResourceHandler(h, RT_ICON, NULL);
    SetResourceHandler(h, RT_ICON, icon);
    cur = SetResourceHandler(h, RT_CURSOR, NULL);
    SetResourceHandler(h, RT_CURSOR, cur);
    bmp = SetResourceHandler(h, RT_BITMAP, NULL);
    SetResourceHandler(h, RT_BITMAP, bmp);
    obs("  RT_ICON handler is USER.357", (FARPROC)icon == i357);
    obs("  RT_ICON handler is RT_BITMAP's", icon == bmp);
    obs("  RT_CURSOR handler is USER.356", (FARPROC)cur == c356);
    obs("  RT_CURSOR handler is RT_BITMAP's", cur == bmp);
    obs("  RT_ICON sel", SELECTOROF(icon));
    obs("  RT_ICON off", OFFSETOF(icon));
    obs("  RT_BITMAP sel", SELECTOROF(bmp));
    obs("  RT_BITMAP off", OFFSETOF(bmp));
    obs("  RT_GROUP_ICON sel", SELECTOROF(SetResourceHandler(h, RT_GROUP_ICON, NULL)));
    obs("  USER.357 sel", SELECTOROF(i357));
}

int __cdecl tmain(void)
{
    HINSTANCE h;
    out("TK37 start\r\n");
    h = LoadLibrary("MORICONS.DLL");
    obs("MORICONS loaded", h >= (HINSTANCE)32);
    if (h >= (HINSTANCE)32)
        look("MORICONS.DLL", h);
    look("TK37.EXE", GetModuleHandle("TK37"));
    look("SHELL.DLL", GetModuleHandle("SHELL"));
    out("TK37 done\r\n");
    return 0;
}
