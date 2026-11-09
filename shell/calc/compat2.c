/*
 * compat2.c - w16 Calculator on Windows 2.x (W16_WIN2): stand-ins for
 * what Windows/386 2.11 lacks (compat2.h). Empty in the 3.x build.
 */
#include <windows.h>
#include "calc.h"
#include "ui.h"
#include "compat2.h"

#ifdef W16_WIN2

/* no WinHelp in 2.x: say so (Help > ... ) */
BOOL FAR ca_WinHelp(HWND w, LPCSTR file, UINT cmd, DWORD data)
{
    char title[32];
    (void)file;
    (void)data;
    if (cmd == HELP_QUIT)
        return TRUE;
    LoadString(g_hinst, IDS_CALC, title, sizeof title);
    MessageBox(w, "Help is not available on this version of Windows.", title, MB_OK);
    return FALSE;
}

/* no SHELL.DLL: a message box with the name and the About text */
int FAR ca_ShellAbout(HWND w, LPCSTR name, LPCSTR text, HICON icon)
{
    (void)icon;
    MessageBox(w, text, name, MB_OK);
    return 1;
}

#else
int calc_compat2_unused;
#endif
