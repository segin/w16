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

HINSTANCE g_hinst;
HWND g_frame, g_client;
BOOL g_autoarrange, g_minonrun, g_savesettings = TRUE;
char g_ini[] = "PROGMAN.INI";

static char cls_frame[] = "Progman";
static char s_settings[] = "Settings";
static char s_groups[] = "Groups";
static HMENU window_menu;

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

/* Order=: the groups' GroupN numbers in z-order, bottom first, each
   preceded by a blank (observed: "Order= 2 3 4 1"). */
static void write_order(void)
{
    LPSTR buf = s_dup("");
    HWND w, last = NULL;
    char num[8];
    int n;
    if (!buf)
        return;
    for (w = GetWindow(g_client, GW_CHILD); w; w = GetWindow(w, GW_HWNDNEXT))
        last = w;
    for (w = last; w; w = GetWindow(w, GW_HWNDPREV))
        if ((n = group_ininum(w)) != 0) {
            num[0] = ' ';
            s_utoa(num + 1, n);
            if (!s_append(&buf, num))
                break;
        }
    WritePrivateProfileString(s_settings, "Order", buf, g_ini);
    mem_free(buf);
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

void ini_remove_group(int n)
{
    char key[12];
    group_key(key, n);
    WritePrivateProfileString(s_groups, key, NULL, g_ini);
    write_order();
}

void ini_groups_changed(void)
{
    write_order();
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
    write_order();
    group_save_all();
}

static void check(int id, BOOL on)
{
    CheckMenuItem(GetMenu(g_frame), id, MF_BYCOMMAND | (on ? MF_CHECKED : MF_UNCHECKED));
}

/* ---- commands ---- */

static void command(HWND hwnd, WORD id)
{
    HWND active = (HWND)(WORD)SendMessage(g_client, WM_MDIGETACTIVE, 0, 0);

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
    case IDM_MOVE:
    case IDM_COPY:
    case IDM_PROPERTIES:
        message(hwnd, IDS_NOTYET, NULL);        /* TODO: M-shell 2 */
        break;
    default:
        /* the Window menu's group list and other MDI commands */
        DefFrameProc(hwnd, g_client, WM_COMMAND, id, 0);
        break;
    }
}

LRESULT CALLBACK __export FrameWndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp)
{
    switch (msg) {
    case WM_CREATE: {
        CLIENTCREATESTRUCT cc;
        g_frame = hwnd;
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
    case WM_CLOSE:
    case WM_QUERYENDSESSION:
        save_settings();
        if (msg == WM_QUERYENDSESSION)
            return TRUE;
        DestroyWindow(hwnd);
        return 0;
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
    wc.hIcon = LoadIcon(g_hinst, MAKEINTRESOURCE(IDI_PROGMAN));
    wc.hCursor = LoadCursor(NULL, IDC_ARROW);
    wc.hbrBackground = (HBRUSH)(COLOR_APPWORKSPACE + 1);
    wc.lpszMenuName = MAKEINTRESOURCE(IDM_MAIN);
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
    GetPrivateProfileString("boot", "shell", "", buf, sizeof buf, "SYSTEM.INI");
    {
        int n = s_len(buf), k = n;
        while (k > 0 && buf[k - 1] != '\\' && buf[k - 1] != ':')
            k--;
        LoadString(hinst, s_icmp(buf + k, "PROGMAN.EXE") == 0 ? IDS_EXITWINDOWS : IDS_EXIT,
                   name, sizeof name);
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
