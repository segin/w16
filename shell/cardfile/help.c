/*
 * help.c - w16 Cardfile: Help.
 *
 * Cardfile asks WINHELP for topics of CARDFILE.HLP (string 18,
 * "cardfile.hlp"); the file is Microsoft's, copied by the user (w16
 * ships no help text). Every request is the one Windows 3.1's Cardfile
 * makes, observed with a stand-in WINHELP.EXE that logs what it gets
 * (tests/apps/thelp.c, tests/cardhelp.sh, tests/scripts/cfhelp/):
 *   Help > Contents and F1: HELP_INDEX;
 *   Help > Search for Help On: HELP_PARTIALKEY "";
 *   Help > How to Use Help: HELP_HELPONHELP;
 *   F1 on a menu item: nothing;
 *   the Find box's Help button: what COMMDLG asks for.
 * Windows 2.x has no WinHelp (compat2.c).
 */
#include "cardfile.h"

static char help_file[16];

static LPCSTR file(void)
{
    if (!help_file[0])
        LoadString(g_hinst, IDS_HELPFILE, help_file, sizeof help_file);
    return help_file;
}

void help_contents(void)
{
    WinHelp(g_frame, file(), HELP_INDEX, 0);
}

void help_search(void)
{
    WinHelp(g_frame, file(), HELP_PARTIALKEY, (DWORD)(LPCSTR)"");
}

void help_on_help(void)
{
    WinHelp(g_frame, file(), HELP_HELPONHELP, 0);
}

void help_quit(void)
{
    WinHelp(g_frame, file(), HELP_QUIT, 0);
}

/* F1 in a menu does nothing: the original installs no message hook
   (it imports none) and the stand-in WINHELP saw no request (cfhelp) */
void help_menu_select(WPARAM wp, LPARAM lp)
{
    (void)wp;
    (void)lp;
}

void help_init(void)
{
}

void help_done(void)
{
}
