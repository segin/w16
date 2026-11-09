/*
 * help.c - w16 Calculator: Help and About.
 *
 * Calculator asks WINHELP for topics of calc.hlp (string 72); the help
 * file is Microsoft's (w16 ships none). The requests are the original's,
 * observed with the logging stand-in WINHELP.EXE (tests/apps/thelp.c,
 * tests/calcscenarios.sh): F1 and Help > Contents: HELP_INDEX; Search for
 * Help On: HELP_PARTIALKEY ""; How to Use Help: HELP_HELPONHELP; F1 on a
 * menu item: nothing; closing Calculator: HELP_QUIT.
 */
#include <windows.h>
#include "calc.h"
#include "ui.h"
/* SHELL.22 (not in the headers we have) */
int WINAPI ShellAbout(HWND, LPCSTR, LPCSTR, HICON);

static char help_file[16];

static const char *file(void)
{
    if (!help_file[0])
        LoadString(g_hinst, IDS_HELPFILE, help_file, sizeof help_file);
    return help_file;
}

void help_command(int id)
{
    switch (id) {
    case IDM_HELPCONTENTS:
        WinHelp(g_wnd, file(), HELP_INDEX, 0);
        break;
    case IDM_HELPSEARCH:
        WinHelp(g_wnd, file(), HELP_PARTIALKEY, (DWORD)(LPCSTR)"");
        break;
    case IDM_HELPHELP:
        WinHelp(g_wnd, file(), HELP_HELPONHELP, 0);
        break;
    }
}

void help_menuselect(WPARAM wp, LPARAM lp)
{
    (void)wp;
    (void)lp;
}

void help_quit(HWND w)
{
    WinHelp(w, file(), HELP_QUIT, 0);
}

/* About: SHELL.DLL's ShellAbout with the application's name and icon */
void about(void)
{
    char name[32], text[96];
    LoadString(g_hinst, IDS_CALC, name, sizeof name);
    LoadString(g_hinst, IDS_ABOUT, text, sizeof text);
    ShellAbout(g_wnd, name, text, LoadIcon(g_hinst, "SC"));
}
