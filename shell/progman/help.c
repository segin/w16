/*
 * help.c - w16 Program Manager: Help.
 *
 * Program Manager asks WINHELP for topics of PROGMAN.HLP. The help file
 * itself is Microsoft's: users copy it from their Windows (w16 ships no
 * help text). So that it works, every request is the one Windows 3.1's
 * Program Manager makes, observed with a stand-in WINHELP.EXE that logs
 * the requests it receives (tests/apps/thelp.c, tests/scripts/help/):
 *   Help > Contents, and F1 outside menus and dialogs: HELP_INDEX;
 *   Help > Search for Help on: HELP_PARTIALKEY "";
 *   Help > How to Use Help: HELP_HELPONHELP (naming PROGMAN.HLP);
 *   a dialog's Help button, or F1 in it: HELP_CONTEXT, its topic;
 *   F1 on a menu item: HELP_CONTEXT, the item's topic.
 * Windows 2.x has no WinHelp (compat2.c says so instead).
 */
#include "progman.h"
#ifndef W16_WIN2
#include <commdlg.h>
#ifndef HELPMSGSTRING                   /* the SDK's name; not in every header */
#define HELPMSGSTRING       "commdlg_help"
#endif
#endif

static const char help_file[] = "PROGMAN.HLP";

/* topics of the menu items (observed; Options has no 1453h) */
static const struct { WORD id; WORD topic; } menu_topics[] = {
    { IDM_NEW, 0x13ED }, { IDM_OPEN, 0x13EE }, { IDM_MOVE, 0x13EF },
    { IDM_COPY, 0x13F0 }, { IDM_DELETE, 0x13F1 }, { IDM_PROPERTIES, 0x13F2 },
    { IDM_RUN, 0x13F3 }, { IDM_EXIT, 0x13F4 },
    { IDM_AUTOARRANGE, 0x1451 }, { IDM_MINONRUN, 0x1452 }, { IDM_SAVESETTINGS, 0x1454 },
    { IDM_CASCADE, 0x14B5 }, { IDM_TILE, 0x14B6 }, { IDM_ARRANGE, 0x14B7 },
    { IDM_HELPCONTENTS, 0x1519 }, { IDM_HELPHELP, 0x151A }, { IDM_ABOUT, 0x151B },
    { IDM_HELPSEARCH, 0x151C }, { IDM_TUTORIAL, 0x151D },
};
#define TOPIC_GROUPWINDOW   0x157C      /* a group in the Window menu */

static UINT menu_item;                  /* the highlighted menu item */
static BOOL menu_item_set;
static DWORD browse_topic;              /* the Browse dialog that is up */

void help_contents(HWND w)
{
    WinHelp(w, help_file, HELP_INDEX, 0);
}

void help_search(HWND w)
{
    WinHelp(w, help_file, HELP_PARTIALKEY, (DWORD)(LPCSTR)"");
}

void help_on_help(HWND w)
{
    WinHelp(w, help_file, HELP_HELPONHELP, 0);
}

void help_topic(HWND w, DWORD topic)
{
    WinHelp(w, help_file, HELP_CONTEXT, topic);
}

void help_quit(HWND w)
{
    WinHelp(w, help_file, HELP_QUIT, 0);
}

/* The frame's WM_MENUSELECT: remember the item F1 would be about. */
void help_menu_select(WPARAM wp, LPARAM lp)
{
    UINT flags = LOWORD(lp);
    menu_item_set = !(flags == 0xFFFF && !HIWORD(lp)) &&
                    !(flags & (MF_POPUP | MF_SEPARATOR | MF_SYSMENU));
    menu_item = wp;
}

static void help_menu_item(void)
{
    int i;
    if (!menu_item_set)
        return;                         /* UNVERIFIED: F1 on a popup's title */
    if (menu_item >= IDM_FIRSTCHILD) {
        help_topic(g_frame, TOPIC_GROUPWINDOW);
        return;
    }
    for (i = 0; i < (int)(sizeof menu_topics / sizeof menu_topics[0]); i++)
        if (menu_topics[i].id == menu_item) {
            help_topic(g_frame, menu_topics[i].topic);
            return;
        }
}

/* The Browse dialog about to open asks for this topic (observed: from
   Run 1B59h, Program Item Properties 1B5Ah, Change Icon 1B5Bh). */
void help_browse_topic(DWORD topic)
{
    browse_topic = topic;
}

#ifndef W16_WIN2
static HHOOK hook;
static UINT commdlg_help;               /* COMMDLG's Help notification */

/* Is msg COMMDLG's "Help pressed" to its owner? Then show the topic. */
BOOL help_commdlg(HWND dlg, UINT msg)
{
    if (!commdlg_help || msg != commdlg_help)
        return FALSE;
    help_topic(dlg, browse_topic);
    return TRUE;
}

/* F1 in a dialog or a menu (WH_MSGFILTER): what its Help button does,
   or the menu item's topic. */
LRESULT CALLBACK __export HelpFilter(int code, WPARAM wp, LPARAM lp)
{
    MSG FAR *m = (MSG FAR *)lp;
    if (code >= 0 && m->message == WM_KEYDOWN && m->wParam == VK_F1) {
        if (code == MSGF_MENU) {
            help_menu_item();
            return TRUE;
        }
        if (code == MSGF_DIALOGBOX) {
            HWND d = m->hwnd, b;
            while (d && (GetWindowLong(d, GWL_STYLE) & WS_CHILD))
                d = GetParent(d);
            /* our dialogs' Help (126), else COMMDLG's (pshHelp, 040Eh) */
            if (d && ((b = GetDlgItem(d, IDC_HELPBTN)) != NULL ||
                      (b = GetDlgItem(d, 0x40E)) != NULL)) {
                PostMessage(d, WM_COMMAND, GetWindowWord(b, GWW_ID), MAKELPARAM(b, BN_CLICKED));
                return TRUE;
            }
        }
    }
    return CallNextHookEx(hook, code, wp, lp);
}

void help_init(void)
{
    commdlg_help = RegisterWindowMessage(HELPMSGSTRING);
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
/* Windows 2.x: no hooks needed: there is no WinHelp to show anything */
BOOL help_commdlg(HWND dlg, UINT msg)
{
    (void)dlg;
    (void)msg;
    return FALSE;
}

void help_init(void)
{
    (void)help_menu_item;
}

void help_done(void)
{
}
#endif

DWORD help_browse_current(void)
{
    return browse_topic;
}
