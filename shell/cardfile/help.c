/*
 * help.c - w16 Cardfile: Help.
 *
 * Cardfile asks WINHELP for topics of CARDFILE.HLP (string 18,
 * "cardfile.hlp"); the file is Microsoft's, copied by the user (w16
 * ships no help text). Every request is the one Windows 3.1's Cardfile
 * makes, observed with a stand-in WINHELP.EXE that logs what it gets
 * (tests/apps/thelp.c, tests/scripts/help/cardfile.txt):
 *   Help > Contents and F1: HELP_INDEX;
 *   Help > Search for Help On: HELP_PARTIALKEY "";
 *   Help > How to Use Help: HELP_HELPONHELP;
 *   F1 on a menu item: HELP_CONTEXT with the item's topic;
 *   the Find box's Help button: HELP_CONTEXT.
 * Windows 2.x has no WinHelp (compat2.c).
 */
#include "cardfile.h"

static char help_file[16];

/* menu items' topics: UNVERIFIED until CF11 (see tests/scripts/help) */
static const struct { WORD id; WORD topic; } menu_topics[] = {
    { 0, 0 }
};

static UINT menu_item;
static BOOL menu_item_set;

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

void help_menu_select(WPARAM wp, LPARAM lp)
{
    UINT flags = LOWORD(lp);
    menu_item_set = !(flags == 0xFFFF && !HIWORD(lp)) &&
                    !(flags & (MF_POPUP | MF_SEPARATOR | MF_SYSMENU));
    menu_item = wp;
}

#ifndef W16_WIN2
static HHOOK hook;

static void help_menu_item(void)
{
    int i;
    if (!menu_item_set)
        return;
    for (i = 0; i < (int)(sizeof menu_topics / sizeof menu_topics[0]); i++)
        if (menu_topics[i].id && menu_topics[i].id == menu_item) {
            WinHelp(g_frame, file(), HELP_CONTEXT, menu_topics[i].topic);
            return;
        }
}

LRESULT CALLBACK __export HelpFilter(int code, WPARAM wp, LPARAM lp)
{
    MSG FAR *m = (MSG FAR *)lp;
    if (code == MSGF_MENU && m->message == WM_KEYDOWN && m->wParam == VK_F1) {
        help_menu_item();
        return TRUE;
    }
    return CallNextHookEx(hook, code, wp, lp);
}

void help_init(void)
{
    hook = SetWindowsHookEx(WH_MSGFILTER, (HOOKPROC)MakeProcInstance((FARPROC)HelpFilter, g_hinst),
                            g_hinst, GetCurrentTask());
}

void help_done(void)
{
    if (hook)
        UnhookWindowsHookEx(hook);
    hook = NULL;
}
#else
void help_init(void)
{
    (void)menu_topics;
    (void)menu_item;
}

void help_done(void)
{
}
#endif
