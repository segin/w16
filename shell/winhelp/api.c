/*
 * api.c - w16 Windows Help: requests from USER's WinHelp.
 *
 * USER (user/misc.c, probe US7) finds the window of class MS_WINHELP,
 * starting "winhelp.exe -x" when there is none, and sends it the
 * registered message "WM_WINHELP", wParam the caller's window, lParam's
 * low word a shared global block: WORD size, WORD command, DWORD data
 * (HELP_CONTEXT's, HELP_SETCONTENTS' and HELP_CONTEXTPOPUP's number),
 * DWORD reserved, WORD offset of the help file's name, WORD offset of the
 * data (a key's string, HELP_COMMAND's macros, HELP_MULTIKEY's and
 * HELP_SETWINPOS' structures), then the strings.
 *
 * The commands (Windows 3.1 SDK, Programmer's Reference Vol. 2, WinHelp):
 * HELP_CONTEXT, HELP_CONTEXTPOPUP ([MAP] numbers), HELP_KEY,
 * HELP_PARTIALKEY (an empty key: the Search dialog), HELP_MULTIKEY (an
 * alternative keyword table), HELP_COMMAND (macros), HELP_CONTENTS
 * (HELP_INDEX), HELP_SETCONTENTS (HELP_SETINDEX), HELP_HELPONHELP,
 * HELP_SETWINPOS, HELP_FORCEFILE, HELP_QUIT. One WINHELP serves every
 * application: the window shows whichever file the last request named;
 * HELP_QUIT from an application ends it only when no other application
 * that asked for help is still running.
 */
#include "winhelp.h"

UINT g_wmwinhelp;

#define MAXAPPS 16
static HWND apps[MAXAPPS];
static int napps;

static void app_add(HWND h)
{
    int i;
    if (!h)
        return;
    for (i = 0; i < napps; i++)
        if (apps[i] == h)
            return;
    if (napps < MAXAPPS)
        apps[napps++] = h;
}

/* HELP_QUIT: forget the application; quit when no other is left */
static void app_quit(HWND h)
{
    int i, k = 0;
    for (i = 0; i < napps; i++)
        if (apps[i] != h && IsWindow(apps[i]))
            apps[k++] = apps[i];
    napps = k;
    if (!napps)
        PostMessage(g_frame, WM_CLOSE, 0, 0);
}

void show_main(void)
{
    if (!IsWindowVisible(g_frame))
        ShowWindow(g_frame, SW_SHOWNORMAL);
    else if (IsIconic(g_frame))
        ShowWindow(g_frame, SW_RESTORE);
    BringWindowToTop(g_frame);
    SetActiveWindow(g_frame);
}

static BOOL context_number(DWORD id, BOOL popup)
{
    DWORD off;
    if (!hlp_mapid(g_hlp, id, &off)) {
        errorbox(IDS_NOTOPIC, NULL);
        return FALSE;
    }
    if (popup) {
        POINT pt;
        int t = hlp_topic_at(g_hlp, off);
        if (t < 0)
            return FALSE;
        GetCursorPos(&pt);
        popup_show(g_hlp, t, pt);
        return TRUE;
    }
    return show_offset(off, TRUE, 0);
}

static void set_winpos(const BYTE FAR *d)
{
    /* HELPWININFO: size, x, y, dx, dy (in 1024ths of the screen), the
       show command, the window's name */
    int sx = GetSystemMetrics(SM_CXSCREEN), sy = GetSystemMetrics(SM_CYSCREEN);
    const short FAR *s = (const short FAR *)d;
    int x = (int)((long)s[1] * sx / 1024), y = (int)((long)s[2] * sy / 1024);
    int cx = (int)((long)s[3] * sx / 1024), cy = (int)((long)s[4] * sy / 1024);
    MoveWindow(g_frame, x, y, cx, cy, TRUE);
    if (s[5])
        ShowWindow(g_frame, s[5]);
}

LRESULT api_request(HWND from, HGLOBAL block)
{
    BYTE FAR *p;
    WORD cmd, ofile, odata;
    DWORD data;
    char file[PATHMAX];
    LPCSTR str = NULL;
    BOOL ok = TRUE, quiet;
    if (!block)
        return FALSE;
    p = (BYTE FAR *)GlobalLock(block);
    if (!p)
        return FALSE;
    cmd = *(WORD FAR *)(p + 2);
    data = *(DWORD FAR *)(p + 4);
    ofile = *(WORD FAR *)(p + 12);
    odata = *(WORD FAR *)(p + 14);
    file[0] = 0;
    if (ofile)
        hlp_strcpyn(file, (LPCSTR)(p + ofile), sizeof file);
    if (odata)
        str = (LPCSTR)(p + odata);
    if (cmd != HELP_QUIT) {
        app_add(from);
        g_owner = from;
    }
    if (cmd == HELP_HELPONHELP)
        lstrcpy(file, "WINHELP.HLP");
    quiet = cmd == HELP_SETCONTENTS || cmd == HELP_QUIT;
    if (cmd != HELP_QUIT && file[0]) {
        if (!open_file(file, quiet)) {
            GlobalUnlock(block);
            if (!g_hlp && !IsWindowVisible(g_frame))
                PostMessage(g_frame, WM_CLOSE, 0, 0);
            return FALSE;
        }
    }
    switch (cmd) {
    case HELP_CONTEXT:
        show_main();
        ok = context_number(data, FALSE);
        break;
    case HELP_CONTEXTPOPUP:
        ok = context_number(data, TRUE);
        break;
    case HELP_QUIT:
        app_quit(from);
        break;
    case HELP_CONTENTS:
    case HELP_HELPONHELP:
        show_main();
        go_contents();
        break;
    case HELP_SETCONTENTS: {
        DWORD off;
        if (g_hlp && hlp_mapid(g_hlp, data, &off))
            g_contents = off;
        break;
    }
    case HELP_FORCEFILE:
        show_main();
        if (g_cur < 0)
            go_contents();
        break;
    case HELP_KEY:
    case HELP_PARTIALKEY:
        /* the Search dialog comes up without the Help window when the
           key is not one topic's (WS05); the window shows with a topic */
        if (g_cur < 0)
            go_contents();
        if (!str || !*str)
            search_dialog(NULL);
        else
            ok = key_jump(str, cmd == HELP_PARTIALKEY);
        break;
    case HELP_MULTIKEY:
        show_main();
        if (str) {
            /* MULTIKEYHELP: WORD size, the table's letter, the key */
            if (g_cur < 0)
                go_contents();
            ok = multikey_jump((char)str[2], str + 3);
        }
        break;
    case HELP_COMMAND:
        if (str)
            ok = run_macros(str);
        break;
    case HELP_SETWINPOS:
        if (str)
            set_winpos((const BYTE FAR *)str);
        break;
    default:
        ok = FALSE;
        break;
    }
    GlobalUnlock(block);
    (void)ok;
    return TRUE;
}
