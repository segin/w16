/*
 * help.c - w16 Notepad: Help.
 *
 * The help file is the user's own NOTEPAD.HLP (w16 ships no help text);
 * the requests are the ones Windows 3.1's Notepad makes, observed with a
 * stand-in WINHELP.EXE that logs them (tests/apps/thelp.c,
 * tests/npscenarios.sh -h):
 *   Help > Contents, and F1 in the main window: HELP_INDEX;
 *   Help > Search for Help on: HELP_PARTIALKEY "";
 *   Help > How to Use Help: HELP_HELPONHELP;
 *   on exit (after help was asked for): HELP_QUIT;
 * all naming "notepad.hlp" (the original's string 34, in small letters).
 * F1 on a menu item, in Page Setup, Find or Open asks for nothing.
 * Windows 2.x has no WinHelp: the 2.x build has no Help menu.
 */
#include "notepad.h"

static BOOL asked;                      /* WinHelp was asked something */

void help(UINT cmd)
{
#ifndef W16_WIN2
    char file[16];
    LoadString(g_hinst, IDS_HELPFILE, file, sizeof file);
    switch (cmd) {
    case IDM_HELPCONTENTS:
        asked = TRUE;
        WinHelp(g_main, file, HELP_INDEX, 0);
        break;
    case IDM_HELPSEARCH:
        asked = TRUE;
        WinHelp(g_main, file, HELP_PARTIALKEY, (DWORD)(LPCSTR)"");
        break;
    case IDM_HELPHELP:
        asked = TRUE;
        WinHelp(g_main, file, HELP_HELPONHELP, 0);
        break;
    case 0:
        if (asked)
            WinHelp(g_main, file, HELP_QUIT, 0);
        break;
    }
#else
    (void)cmd;
    (void)asked;
#endif
}
