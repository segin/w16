/*
 * main.c - MAIN.CPL's CPlApplet, the applet table and the cpArrow control.
 *
 * Observed on Windows 3.1 with Control Panel's calls logged (the CP1
 * probe, ARCHITECTURE.md 14): Control Panel asks with CPL_NEWINQUIRE and,
 * since the original MAIN.CPL leaves dwSize 0, then with CPL_INQUIRE; the
 * names and descriptions are string resources. Each applet's dialog is
 * modal, owned by the window Control Panel passes (NULL from
 * "CONTROL MAIN.CPL").
 */
#include "maincpl.h"

HINSTANCE g_hinst;

static const int name_ids[AP_COUNT] = {
    IDS_NAME_COLOR, IDS_NAME_FONTS, IDS_NAME_PORTS, IDS_NAME_MOUSE, IDS_NAME_DESKTOP,
    IDS_NAME_KEYBOARD, IDS_NAME_PRINTERS, IDS_NAME_INTL, IDS_NAME_DATETIME
};

int cpl_dialog(HWND parent, int id, DLGPROC proc)
{
    return DialogBox(g_hinst, MAKEINTRESOURCE(id), parent, proc);
}

/* UNVERIFIED: the original's help contexts in CONTROL.HLP are not known;
   the Help buttons open CONTROL.HLP at the applet's keyword */
void cpl_help(HWND dlg, int applet)
{
    char key[32], *s = key, *d = key;
    LoadString(g_hinst, name_ids[applet], key, sizeof key);
    for (; *s; s++)
        if (*s != '&')
            *d++ = *s;
    *d = 0;
    WinHelp(dlg, "CONTROL.HLP", HELP_KEY, (DWORD)(LPSTR)key);
}

static void not_yet(HWND parent, int applet)
{
    char t[40], m[160];
    LoadString(g_hinst, name_ids[applet], t, sizeof t);
    LoadString(g_hinst, IDS_NOTYET, m, sizeof m);
    MessageBox(parent, m, t, MB_OK | MB_ICONINFORMATION);
}

static void run(HWND parent, int applet)
{
    switch (applet) {
    case AP_COLOR:
        color_applet(parent);
        break;
    case AP_DESKTOP:
        desktop_applet(parent);
        break;
    case AP_MOUSE:
        mouse_applet(parent);
        break;
    case AP_KEYBOARD:
        keyboard_applet(parent);
        break;
    case AP_FONTS:
        fonts_applet(parent);
        break;
    case AP_PORTS:
        ports_applet(parent);
        break;
    default:
        not_yet(parent, applet);
        break;
    }
}

LONG FAR PASCAL __loadds CPlApplet(HWND hwnd, UINT msg, LPARAM l1, LPARAM l2)
{
    switch (msg) {
    case CPL_INIT:
        return 1;
    case CPL_GETCOUNT:
        return AP_COUNT;
    case CPL_INQUIRE: {
        LPCPLINFO p = (LPCPLINFO)l2;
        int i = (int)l1;
        if (i < 0 || i >= AP_COUNT)
            return 1;
        p->idIcon = IDI_APPLET + i;
        p->idName = name_ids[i];
        p->idInfo = IDS_INFO_COLOR + name_ids[i] - IDS_NAME_COLOR;
        p->lData = 0;
        return 0;
    }
    case CPL_DBLCLK:
        if (l1 >= 0 && l1 < AP_COUNT)
            run(hwnd, (int)l1);
        return 0;
    }
    return 0;
}

int __cdecl __far cpl_libmain(HINSTANCE h)
{
    g_hinst = h;
    arrow_register(h);
    return 1;
}

int FAR PASCAL __loadds WEP(int exit)
{
    (void)exit;
    arrow_unregister(g_hinst);
    return 1;
}
