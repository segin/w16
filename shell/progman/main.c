/*
 * main.c - Program Manager frame window, menus, and PROGMAN.INI.
 *
 * Observed on Windows 3.1 (SNAP.EXE dump): the frame is class "Progman",
 * style WS_OVERLAPPEDWINDOW | WS_CLIPCHILDREN | WS_CLIPSIBLINGS; the MDI
 * client has ID 1 and style WS_CHILD | WS_VISIBLE | WS_CLIPCHILDREN |
 * WS_BORDER; group windows are class "PMGroup" with IDs from 500.
 * PROGMAN.INI: [Settings] Window=left top right bottom showcmd (1 seen),
 * Order=<GroupN numbers, bottom of the z-order first>; [Groups] GroupN=.
 */
#include "progman.h"
#include <dde.h>

HINSTANCE g_hinst;
HWND g_frame, g_client;
BOOL g_autoarrange, g_minonrun, g_savesettings = TRUE;
BOOL g_isshell;                         /* SYSTEM.INI shell= is us */
char g_ini[] = "PROGMAN.INI";

static char cls_frame[] = "Progman";
static char s_settings[] = "Settings";
static char s_groups[] = "Groups";
static HMENU window_menu;

/* Wine delivers commands from a maximized group's control menu (in the
   menu bar) to the frame as WM_SYSCOMMAND, where Windows 3.1 has them
   reach the group (observed, tests under Wine 11: SC_CLOSE at the frame
   closed Program Manager). The frame notes when that menu was last used
   and passes such a command on. */
static HWND menu_child;                 /* group whose control menu was used */
static DWORD menu_closed_at;            /* GetTickCount when the menu closed */

/* ---- settings ---- */

/* The options as loaded: observed, the original writes an option only
   when it changed (an untouched PROGMAN.INI keeps no such keys). */
static BOOL loaded_autoarrange, loaded_minonrun, loaded_savesettings;

static BOOL ini_bool(const char *key, BOOL def)
{
    return GetPrivateProfileInt(s_settings, key, def, g_ini) != 0;
}

static void ini_put_bool(const char *key, BOOL v, BOOL loaded)
{
    if (v != loaded)
        WritePrivateProfileString(s_settings, key, v ? "1" : "0", g_ini);
}

static void group_key(char *key, int n)     /* "GroupN": N fits an int */
{
    s_cpy(key, "Group", 12);
    s_utoa(key + 5, n);
}

static void load_groups(void)
{
    LPSTR order = profile_get(s_settings, "Order", g_ini);
    const char FAR *p = order;
    char key[12];
    int n;

    /* Order lists GroupN numbers bottom to top; create them in that order
       so the last one ends up active. */
    if (!order)
        return;
    while ((n = s_atoi(&p)) > 0) {
        LPSTR path;
        group_key(key, n);
        if ((path = profile_get(s_groups, key, g_ini)) != NULL) {
            group_load(path, n);
            mem_free(path);
        }
    }
    mem_free(order);
}

/* Order=: the groups' GroupN numbers in z-order, bottom first. Observed:
   written on exit each preceded by a blank ("Order= 2 3 4 1"), but when a
   group is created only separated by blanks ("Order=2 3 4 1 5"). */
LPSTR order_string(BOOL leading)
{
    LPSTR buf = s_dup("");
    HWND w, last = NULL;
    char num[8];
    int n;
    if (!buf)
        return NULL;
    for (w = GetWindow(g_client, GW_CHILD); w; w = GetWindow(w, GW_HWNDNEXT))
        last = w;
    for (w = last; w; w = GetWindow(w, GW_HWNDPREV))
        if ((n = group_ininum(w)) != 0) {
            num[0] = ' ';
            s_utoa(num + 1, n);
            if (!s_append(&buf, buf[0] || leading ? num : num + 1))
                break;
        }
    return buf;
}

static void write_order(BOOL leading)
{
    LPSTR buf = order_string(leading);
    if (buf) {
        WritePrivateProfileString(s_settings, "Order", buf, g_ini);
        mem_free(buf);
    }
}

/* Record a new group file under the lowest free GroupN (observed: with
   Group1..4 taken, the new one became Group5). Order= is rewritten when
   the window exists, by the caller. */
int ini_add_group(const char FAR *path)
{
    char key[12];
    int n;
    for (n = 1;; n++) {
        LPSTR have;
        group_key(key, n);
        have = profile_get(s_groups, key, g_ini);
        if (!have)
            break;
        mem_free(have);
    }
    WritePrivateProfileString(s_groups, key, path, g_ini);
    return n;
}

void ini_set_group(int n, const char FAR *path)
{
    char key[12];
    group_key(key, n);
    WritePrivateProfileString(s_groups, key, path, g_ini);
}

/* Remove GroupN=. With `order` (from order_string before the group's
   window went away), also write that as Order=: observed, deleting a
   group writes Order= without leading blank and still naming the deleted
   group, until Order= is next written in full. */
void ini_remove_group(int n, LPSTR order)
{
    char key[12];
    group_key(key, n);
    WritePrivateProfileString(s_groups, key, NULL, g_ini);
    if (order)
        WritePrivateProfileString(s_settings, "Order", order, g_ini);
}

/* Observed: creating a group writes Order= in full, as on exit. */
void ini_groups_changed(void)
{
    write_order(TRUE);
}

/* Start each program named in WIN.INI [windows] key (blank- or
   comma-separated). */
static void run_list(const char *key, BOOL minimized)
{
    LPSTR list = profile_get("windows", key, "WIN.INI");
    LPSTR p = list;
    if (!list)
        return;
    while (*p) {
        LPSTR w;
        while (*p == ' ' || *p == ',' || *p == '\t')
            p++;
        w = p;
        while (*p && *p != ' ' && *p != ',' && *p != '\t')
            p++;
        if (*p)
            *p++ = 0;
        if (*w)
            pm_exec(g_frame, w, NULL, minimized);
    }
    mem_free(list);
}

/* As the shell, Program Manager starts the session's programs (observed:
   with w16's Program Manager as the shell and nothing doing this, a
   WIN.INI load= program never ran; with the original it did). load=
   programs start minimized, run= ones normally, then the StartUp group
   (named by PROGMAN.INI [Settings] Startup=, else "StartUp") unless
   Shift is held. */
static void start_session(void)
{
    LPSTR name;
    HWND w;
    run_list("load", TRUE);
    run_list("run", FALSE);
    if (GetKeyState(VK_SHIFT) < 0)
        return;
    name = profile_get(s_settings, "Startup", g_ini);
    for (w = GetWindow(g_client, GW_CHILD); w; w = GetWindow(w, GW_HWNDNEXT))
        if (group_name(w) && !s_icmp(group_name(w), name ? name : "StartUp")) {
            group_run_all(w);
            break;
        }
    mem_free(name);
}

static void save_settings(void)
{
    char buf[48];                       /* four ints and a digit */
    WINDOWPLACEMENT wp;
    RECT *r = &wp.rcNormalPosition;

    ini_put_bool("AutoArrange", g_autoarrange, loaded_autoarrange);
    ini_put_bool("MinOnRun", g_minonrun, loaded_minonrun);
    ini_put_bool("SaveSettings", g_savesettings, loaded_savesettings);
    if (!g_savesettings)
        return;

    wp.length = sizeof wp;
    GetWindowPlacement(g_frame, &wp);
    s_itoa(buf, r->left);
    s_cat(buf, " ", sizeof buf);
    s_itoa(buf + s_len(buf), r->top);
    s_cat(buf, " ", sizeof buf);
    s_itoa(buf + s_len(buf), r->right);
    s_cat(buf, " ", sizeof buf);
    s_itoa(buf + s_len(buf), r->bottom);
    s_cat(buf, IsZoomed(g_frame) ? " 3" : " 1", sizeof buf);   /* UNVERIFIED: 3 */
    WritePrivateProfileString(s_settings, "Window", buf, g_ini);
    write_order(TRUE);
    group_save_all();
}

static void check(int id, BOOL on)
{
    CheckMenuItem(GetMenu(g_frame), id, MF_BYCOMMAND | (on ? MF_CHECKED : MF_UNCHECKED));
}

/* ---- commands ---- */

#ifdef PM_DEBUGLOG
void dbg(const char FAR *s, int v);
#endif

static void command(HWND hwnd, WORD id)
{
    HWND active = (HWND)(WORD)SendMessage(g_client, WM_MDIGETACTIVE, 0, 0);
#ifdef PM_DEBUGLOG
    dbg("WM_COMMAND", id);
#endif

    switch (id) {
    case IDM_OPEN:
        if (active)
            group_open_selected(active);
        break;
    case IDM_RUN:
        run_dialog(hwnd, (FARPROC)RunDlgProc, IDD_RUN);
        break;
    case IDM_EXIT:
        SendMessage(hwnd, WM_CLOSE, 0, 0);
        break;
    case IDM_AUTOARRANGE:
        g_autoarrange = !g_autoarrange;
        check(id, g_autoarrange);
        if (g_autoarrange && active)
            group_arrange(active);
        break;
    case IDM_MINONRUN:
        g_minonrun = !g_minonrun;
        check(id, g_minonrun);
        break;
    case IDM_SAVESETTINGS:
        g_savesettings = !g_savesettings;
        check(id, g_savesettings);
        break;
    case IDM_CASCADE:
        SendMessage(g_client, WM_MDICASCADE, 0, 0);
        break;
    case IDM_TILE:
        SendMessage(g_client, WM_MDITILE, 0, 0);
        break;
    case IDM_ARRANGE:
        if (active && !IsIconic(active))
            group_arrange(active);
        else
            SendMessage(g_client, WM_MDIICONARRANGE, 0, 0);
        break;
    case IDM_HELPCONTENTS:
        WinHelp(hwnd, "PROGMAN.HLP", HELP_INDEX, 0);
        break;
    case IDM_HELPSEARCH:
        WinHelp(hwnd, "PROGMAN.HLP", HELP_PARTIALKEY, (DWORD)(LPCSTR)"");
        break;
    case IDM_HELPHELP:
        WinHelp(hwnd, NULL, HELP_HELPONHELP, 0);
        break;
    case IDM_TUTORIAL:
        pm_exec(hwnd, "WINTUTOR.EXE", NULL, FALSE);
        break;
    case IDM_ABOUT:
        run_dialog(hwnd, (FARPROC)AboutDlgProc, IDD_ABOUT);
        break;
    case IDM_NEW:
        cmd_new(hwnd);
        break;
    case IDM_DELETE:
        cmd_delete(hwnd);
        break;
    case IDM_PROPERTIES:
        cmd_properties(hwnd);
        break;
    case IDM_MOVE:
    case IDM_COPY:
        cmd_movecopy(hwnd, id == IDM_COPY);
        break;
    default:
        /* the Window menu's group list and other MDI commands */
        DefFrameProc(hwnd, g_client, WM_COMMAND, id, 0);
        break;
    }
}

LRESULT CALLBACK __export FrameWndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp)
{
#ifdef PM_DEBUGLOG
    if (msg == WM_COMMAND)
        dbg("FrameWndProc WM_COMMAND", wp);
    if (msg == WM_SYSCOMMAND) {
        dbg("FrameWndProc WM_SYSCOMMAND", wp);
        dbg("  lParam lo", LOWORD(lp));
        dbg("  lParam hi", HIWORD(lp));
    }
    if (msg == WM_INITMENUPOPUP) {
        HWND act = (HWND)(WORD)SendMessage(g_client, WM_MDIGETACTIVE, 0, 0);
        dbg("FrameWndProc WM_INITMENUPOPUP hmenu", wp);
        dbg("  system menu flag", HIWORD(lp));
        dbg("  active child's system menu", act ? (int)GetSystemMenu(act, FALSE) : 0);
        dbg("  frame's system menu", (int)GetSystemMenu(hwnd, FALSE));
    }
    if (msg == WM_CLOSE)
        dbg("FrameWndProc WM_CLOSE", 0);
#endif
    switch (msg) {
    case WM_CREATE: {
        CLIENTCREATESTRUCT cc;
        g_frame = hwnd;
#ifdef W16_WIN2
        SetMenu(hwnd, pm_load_menu(g_hinst, MAKEINTRESOURCE(IDM_MAIN)));
#endif
        window_menu = GetSubMenu(GetMenu(hwnd), MENU_WINDOW);
        cc.hWindowMenu = window_menu;
        cc.idFirstChild = IDM_FIRSTCHILD;
        g_client = CreateWindow(MDI_CLIENT_CLASS, NULL,
                                WS_CHILD | WS_VISIBLE | WS_CLIPCHILDREN | WS_BORDER,
                                0, 0, 0, 0, hwnd, (HMENU)1, g_hinst, (LPVOID)&cc);
        return g_client ? 0 : -1;
    }
    case WM_SIZE:
        /* observed: the MDI client sits at (-1,-1) and is 2 pixels larger
           than the frame's client area, so its border hides under the
           frame's edge */
        if (g_client && wp != SIZE_MINIMIZED)
            MoveWindow(g_client, -1, -1, LOWORD(lp) + 2, HIWORD(lp) + 2, TRUE);
        return 0;
    case WM_COMMAND:
        command(hwnd, wp);
        return 0;
    case WM_MENUSELECT:
        if (LOWORD(lp) == 0xFFFF && !HIWORD(lp)) {      /* the menu closed */
            menu_closed_at = GetTickCount();
        } else {
            HWND act = (HWND)(WORD)SendMessage(g_client, WM_MDIGETACTIVE, 0, 0);
            menu_child = act && (HMENU)HIWORD(lp) == GetSystemMenu(act, FALSE) ? act : NULL;
        }
        break;
    case WM_SYSCOMMAND:
        if ((wp & 0xFFF0) != SC_KEYMENU && (wp & 0xFFF0) != SC_MOUSEMENU) {
            HWND child = menu_child;
            menu_child = NULL;
            /* within a second of that menu closing: it was its command */
            if (child && IsWindow(child) && GetTickCount() - menu_closed_at < 1000)
                return SendMessage(child, WM_SYSCOMMAND, wp, lp);
        }
        break;
    case WM_DDE_INITIATE:
    case WM_DDE_EXECUTE:
    case WM_DDE_REQUEST:
    case WM_DDE_TERMINATE:
    case WM_DDE_ACK:
        return dde_message(hwnd, msg, wp, lp);
    case WM_CLOSE:
        /* Observed: as the shell, Program Manager asks first, in a
           system-modal box "Exit Windows" / "This will end your Windows
           session." (OK, Cancel, information icon), then ends the
           session; not being the shell, it closes without asking. */
        if (g_isshell) {
            if (run_dialog(hwnd, (FARPROC)ExitWinDlgProc, IDD_EXITWIN) != IDOK)
                return 0;
            save_settings();
            ExitWindows(0, 0);          /* returns if an application refused */
            return 0;
        }
        save_settings();
        DestroyWindow(hwnd);
        return 0;
    case WM_QUERYENDSESSION:
        /* observed: when Windows ends while Program Manager is not the
           shell, nothing is saved (as the shell, it saved before
           ExitWindows) */
        return TRUE;
    case WM_DESTROY:
        WinHelp(hwnd, NULL, HELP_QUIT, 0);
        PostQuitMessage(0);
        return 0;
    }
    return DefFrameProc(hwnd, g_client, msg, wp, lp);
}

static BOOL register_frame(void)
{
    WNDCLASS wc;
    wc.style = 0;
    wc.lpfnWndProc = FrameWndProc;
    wc.cbClsExtra = 0;
    wc.cbWndExtra = 0;
    wc.hInstance = g_hinst;
#ifdef W16_WIN2
    wc.hIcon = LoadIcon(NULL, IDI_APPLICATION);     /* TODO: 2.x icons */
    wc.lpszMenuName = NULL;             /* built in WM_CREATE: pm_load_menu */
#else
    wc.hIcon = LoadIcon(g_hinst, MAKEINTRESOURCE(IDI_PROGMAN));
    wc.lpszMenuName = MAKEINTRESOURCE(IDM_MAIN);
#endif
    wc.hCursor = LoadCursor(NULL, IDC_ARROW);
    wc.hbrBackground = (HBRUSH)(COLOR_APPWORKSPACE + 1);
    wc.lpszClassName = cls_frame;
    return RegisterClass(&wc) != 0;
}

int PASCAL WinMain(HINSTANCE hinst, HINSTANCE hprev, LPSTR cmdline, int show)
{
    char buf[64], name[32];
    const char FAR *p = buf;
    int x = CW_USEDEFAULT, y = 0, cx = CW_USEDEFAULT, cy = 0, wshow = 0;
    HACCEL acc;
    HMENU file;
    MSG msg;

    (void)cmdline;
    g_hinst = hinst;
#ifdef PM_DEBUGLOG
    dbg("WinMain", 0);
#endif
    if (hprev) {
        /* one Program Manager: bring the existing one forward */
        HWND w = FindWindow(cls_frame, NULL);
        if (w) {
            if (IsIconic(w))
                ShowWindow(w, SW_RESTORE);
            SetActiveWindow(w);
        }
        return 0;
    }
#ifdef W16_WIN2
    if (!mdi2_register(hinst))
        return 1;
#endif
    if (!register_frame() || !group_register() || !hotkey_register())
        return 1;

    g_autoarrange = ini_bool("AutoArrange", FALSE);
    g_minonrun = ini_bool("MinOnRun", FALSE);
    g_savesettings = ini_bool("SaveSettings", TRUE);
    loaded_autoarrange = g_autoarrange;
    loaded_minonrun = g_minonrun;
    loaded_savesettings = g_savesettings;
    if (GetPrivateProfileString(s_settings, "Window", "", buf, sizeof buf, g_ini)) {
        int l = s_atoi(&p), t = s_atoi(&p), r = s_atoi(&p), b = s_atoi(&p);
        wshow = s_atoi(&p);
        if (r > l && b > t) {
            x = l;
            y = t;
            cx = r - l;
            cy = b - t;
        }
    }

    LoadString(hinst, IDS_APPNAME, name, sizeof name);
    if (!CreateWindow(cls_frame, name,
                      WS_OVERLAPPEDWINDOW | WS_CLIPCHILDREN | WS_CLIPSIBLINGS,
                      x, y, cx, cy, NULL, NULL, hinst, NULL))
        return 1;

    /* "Exit Windows..." when we are the shell, otherwise "Exit" */
    file = GetSubMenu(GetMenu(g_frame), MENU_FILE);
    {
        LPSTR sh = profile_get("boot", "shell", "SYSTEM.INI");
        int k = sh ? s_len(sh) : 0;
        while (k > 0 && sh[k - 1] != '\\' && sh[k - 1] != ':')
            k--;
        g_isshell = sh && s_icmp(sh + k, "PROGMAN.EXE") == 0;
        mem_free(sh);
        LoadString(hinst, g_isshell ? IDS_EXITWINDOWS : IDS_EXIT, name, sizeof name);
        ModifyMenu(file, IDM_EXIT, MF_BYCOMMAND | MF_STRING, IDM_EXIT, name);
    }
    check(IDM_AUTOARRANGE, g_autoarrange);
    check(IDM_MINONRUN, g_minonrun);
    check(IDM_SAVESETTINGS, g_savesettings);

    /* Window= ends with a show command (observed: 1 = SW_SHOWNORMAL). The
       one Windows passes us wins if it asks for minimized. */
    if (show != SW_SHOWMINIMIZED && show != SW_SHOWMINNOACTIVE && show != SW_MINIMIZE &&
        (wshow == SW_SHOWNORMAL || wshow == SW_SHOWMAXIMIZED))
        show = wshow;
    ShowWindow(g_frame, show);
    UpdateWindow(g_frame);
    load_groups();
    if (g_isshell)
        start_session();

    acc = LoadAccelerators(hinst, MAKEINTRESOURCE(IDA_MAIN));
    while (GetMessage(&msg, NULL, 0, 0)) {
        if (!TranslateMDISysAccel(g_client, &msg) &&
            !TranslateAccelerator(g_frame, acc, &msg)) {
            TranslateMessage(&msg);
            DispatchMessage(&msg);
        }
    }
    group_destroy_all();
    return msg.wParam;
}
