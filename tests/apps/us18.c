/*
 * us18.c - probe US18: a maximised MDI child's controls in the frame's
 * menu bar, and what goes with them (user/mdi.c).
 *
 * An MDI frame of its own (File, Window and Help popups, made with
 * CreateMenu/AppendMenu), its MDICLIENT (the Window popup as the window
 * menu, first child id 500) and children "One", "Two", "Three". At each
 * step the probe logs, as OBS lines to compare with Windows 3.1's run:
 *   - the frame's menu bar: the count, then each item's GetMenuState,
 *     GetMenuItemID, GetMenuString and what its GetSubMenu is (one of the
 *     probe's popups, a child's system menu, none, or something else);
 *   - the Window popup's items (the MDI client's list of children);
 *   - every child's system menu's states, style and whether it is the
 *     active one; WM_MDIGETACTIVE's two words (as which child, and the
 *     maximised flag);
 *   - the frame's caption (GetWindowText);
 *   - the messages the frame and the children were sent that matter here
 *     (WM_SETTEXT to the frame, WM_COMMAND, WM_SYSCOMMAND, WM_SIZE codes,
 *     WM_MDIACTIVATE, WM_INITMENU), in order.
 * The steps: WM_MDIMAXIMIZE the active child; WM_MDIACTIVATE another (it
 * is maximised in turn); WM_MDINEXT; SetWindowText on the maximised child
 * and on the frame; WM_MDISETMENU with a new bar while maximised;
 * WM_COMMAND SC_RESTORE etc. sent to the frame as the bar's items would;
 * WM_MDIRESTORE; maximising with ShowWindow and with WM_SYSCOMMAND;
 * minimising the maximised child; WM_MDIDESTROY of the maximised child;
 * a child whose window procedure does not pass WM_SIZE on to
 * DefMDIChildProc, maximised; separators' states (AppendMenu and a
 * template); the system menus of a plain top-level and child window;
 * an icon activated while another child is maximised, and the state given
 * back (the icon is maximised, then goes back to its icon), with a scroll
 * bar set on the icon and taken away in its maximised WM_SIZE (3.1 sends
 * a second WM_SIZE for the larger client area).
 *
 * Run as the shell: tests/userboot.sh -n -s build/dos/US18.EXE (w16's
 * USER), tests/refrun.sh build/dos/US18.EXE (Windows 3.1); compare the
 * OBS lines (tests/boottest.sh case mdimax does).
 */
#include <windows.h>

#define TK_REF "us18ref.h"
#include "tklog.h"

extern WORD FAR PASCAL WaitEvent(HTASK);
extern BOOL FAR PASCAL InitApp(HINSTANCE);

static HINSTANCE hi;
static HWND hFrame, hClient, hc[4];
static HMENU hBar, hFile, hWin, hHelp, hBar2, hWin2;
static char rec[1500];
static int quiet_size;          /* the child that keeps WM_SIZE from DefMDIChildProc */
static int trace;               /* a scroll bar set on an icon, taken away maximised */

static void append(const char *s)
{
    int n = lstrlen(rec);
    if (n + lstrlen(s) < (int)sizeof rec)
        lstrcpy(rec + n, s);
}

static int which_child(HWND h)
{
    int i;
    for (i = 1; i <= 3; i++)
        if (hc[i] && h == hc[i])
            return i;
    return h ? 9 : 0;
}

LRESULT FAR PASCAL __export __loadds frame_proc(HWND h, UINT m, WPARAM wp, LPARAM lp)
{
    char b[80];
    switch (m) {
    case WM_SETTEXT:
        wsprintf(b, "F:settext(%s);", lp ? (LPSTR)lp : (LPSTR)"");
        append(b);
        break;
    case WM_COMMAND:
        wsprintf(b, "F:cmd(%04X);", wp);
        append(b);
        break;
    case WM_SYSCOMMAND:
        wsprintf(b, "F:syscmd(%04X);", wp & 0xFFF0);
        append(b);
        break;
    case WM_INITMENU:
        append("F:initmenu;");
        break;
    case WM_DESTROY:
        if (h == hFrame)
            PostQuitMessage(0);
        break;
    }
    return DefFrameProc(h, hClient, m, wp, lp);
}

LRESULT FAR PASCAL __export __loadds child_proc(HWND h, UINT m, WPARAM wp, LPARAM lp)
{
    char b[60];
    int c = which_child(h);
    switch (m) {
    case WM_SYSCOMMAND:
        wsprintf(b, "C%d:syscmd(%04X);", c, wp & 0xFFF0);
        append(b);
        break;
    case WM_SIZE:
        wsprintf(b, "C%d:size(%d,%d,%d);", c, wp, (int)LOWORD(lp), (int)HIWORD(lp));
        append(b);
        if (trace && wp == SIZE_MINIMIZED) {
            /* what scroll bars do to an icon */
            SetScrollRange(h, SB_VERT, 0, 10, TRUE);
            wsprintf(b, "C%d:sbstyle(%04X);", c, HIWORD(GetWindowLong(h, GWL_STYLE)));
            append(b);
        }
        if (trace && wp == SIZE_MAXIMIZED) {
            RECT r;
            GetClientRect(h, &r);
            wsprintf(b, "C%d:maxstyle(%04X,%d,%d);", c, HIWORD(GetWindowLong(h, GWL_STYLE)), r.right, r.bottom);
            append(b);
            SetScrollRange(h, SB_VERT, 0, 0, TRUE);
        }
        if (c && c == quiet_size)
            return 0;
        break;
    case WM_MDIACTIVATE:
        wsprintf(b, "C%d:mdiact(%d);", c, wp);
        append(b);
        break;
    case WM_SETTEXT:
        wsprintf(b, "C%d:settext;", c);
        append(b);
        break;
    }
    return DefMDIChildProc(h, m, wp, lp);
}

static void pump(void)
{
    MSG msg;
    DWORD t = GetTickCount();
    while (GetTickCount() - t < 300)
        while (PeekMessage(&msg, 0, 0, 0, PM_REMOVE)) {
            TranslateMessage(&msg);
            DispatchMessage(&msg);
        }
}

static char tag[24];

static void o(const char *what, WORD v)
{
    char n[64];
    wsprintf(n, "%s.%s", (LPSTR)tag, (LPSTR)what);
    obs(n, v);
}

static void os(const char *what, const char FAR *s)
{
    char n[64];
    wsprintf(n, "%s.%s", (LPSTR)tag, (LPSTR)what);
    obs_str(n, s);
}

/* what a submenu is: 1 File, 2 Window, 3 Help, 4 the second bar's
   Window popup, 10+i child i's system menu, 0 none, FFFF other */
static WORD what_menu(HMENU m)
{
    int i;
    if (!m)
        return 0;
    if (m == hFile) return 1;
    if (m == hWin) return 2;
    if (m == hHelp) return 3;
    if (m == hWin2) return 4;
    for (i = 1; i <= 3; i++)
        if (hc[i] && IsWindow(hc[i]) && m == GetSystemMenu(hc[i], FALSE))
            return 10 + i;
    return 0xFFFF;
}

static void dump_items(const char *pfx, HMENU m)
{
    int n = GetMenuItemCount(m), i;
    char w[40], t[64];
    wsprintf(w, "%s.count", (LPSTR)pfx);
    o(w, n);
    for (i = 0; i < n; i++) {
        wsprintf(w, "%s%d.state", (LPSTR)pfx, i);
        o(w, GetMenuState(m, i, MF_BYPOSITION));
        wsprintf(w, "%s%d.id", (LPSTR)pfx, i);
        o(w, GetMenuItemID(m, i));
        wsprintf(w, "%s%d.sub", (LPSTR)pfx, i);
        o(w, what_menu(GetSubMenu(m, i)));
        t[0] = 0;
        wsprintf(w, "%s%d.strlen", (LPSTR)pfx, i);
        o(w, GetMenuString(m, i, t, sizeof t, MF_BYPOSITION));
        wsprintf(w, "%s%d.str", (LPSTR)pfx, i);
        os(w, t);
    }
}

static void state(const char *name)
{
    char t[80], w[40];
    DWORD a;
    int i;
    pump();
    for (i = 1; i <= 3; i++)            /* (a new window may get a dead one's handle) */
        if (hc[i] && !IsWindow(hc[i]))
            hc[i] = 0;
    lstrcpy(tag, name);
    out("STEP ");
    out(name);
    out("\r\n");
    o("bar.is", GetMenu(hFrame) == hBar ? 1 : GetMenu(hFrame) == hBar2 ? 2 : 0xFFFF);
    dump_items("bar", GetMenu(hFrame));
    dump_items("win", GetMenu(hFrame) == hBar2 ? hWin2 : hWin);
    if (GetMenu(hFrame) == hBar2)
        dump_items("oldbar", hBar);
    GetWindowText(hFrame, t, sizeof t);
    os("frametext", t);
    o("frametextlen", GetWindowTextLength(hFrame));
    a = SendMessage(hClient, WM_MDIGETACTIVE, 0, 0);
    o("active", which_child((HWND)LOWORD(a)));
    o("activemax", HIWORD(a));
    for (i = 1; i <= 3; i++) {
        HMENU s;
        if (!hc[i] || !IsWindow(hc[i]))
            continue;
        wsprintf(w, "c%d.style", i);
        o(w, HIWORD(GetWindowLong(hc[i], GWL_STYLE)));
        wsprintf(w, "c%d.zoomed", i);
        o(w, IsZoomed(hc[i]));
        s = GetSystemMenu(hc[i], FALSE);
        wsprintf(w, "c%d.sys", i);
        dump_items(w, s);
    }
    os("msgs", rec);
    rec[0] = 0;
}

static HWND make_child(const char *title, int n)
{
    MDICREATESTRUCT mc;
    mc.szClass = "US18C";
    mc.szTitle = title;
    mc.hOwner = hi;
    mc.x = mc.y = mc.cx = mc.cy = CW_USEDEFAULT;
    mc.style = 0;
    mc.lParam = n;
    return (HWND)LOWORD(SendMessage(hClient, WM_MDICREATE, 0, (LPARAM)(LPVOID)&mc));
}

/* a menu template: an item, a separator (flags 0, id 0, no text), an item */
static BYTE tmpl[] = {
    0, 0, 0, 0,                                 /* header: version 0, no extra */
    0x10 | 0x80, 0, 'P', 0,                     /* MF_POPUP | MF_END "P" */
    0, 0, 1, 0, 'A', 0,                         /* "A" id 1 */
    0, 0, 0, 0, 0,                              /* separator */
    0x80, 0, 2, 0, 'B', 0                       /* MF_END "B" id 2 */
};

int __cdecl tmain(void)
{
    WNDCLASS wc;
    CLIENTCREATESTRUCT cc;
    HWND h;
    hi = (HINSTANCE)init_regs[5];
    WaitEvent(0);
    InitApp(hi);
    out("US18 start\r\n");

    /* separators */
    {
        HMENU m = CreatePopupMenu(), t;
        AppendMenu(m, MF_STRING, 1, "A");
        AppendMenu(m, MF_SEPARATOR, 0, NULL);
        InsertMenu(m, 0, MF_BYPOSITION | MF_SEPARATOR, 0, NULL);
        lstrcpy(tag, "sep");
        o("append", GetMenuState(m, 2, MF_BYPOSITION));
        o("insert", GetMenuState(m, 0, MF_BYPOSITION));
        DestroyMenu(m);
        t = LoadMenuIndirect(tmpl);
        o("tmpl", GetMenuState(GetSubMenu(t, 0), 1, MF_BYPOSITION));
        o("tmplid", GetMenuItemID(GetSubMenu(t, 0), 1));
        DestroyMenu(t);
    }

    wc.style = 0;
    wc.lpfnWndProc = frame_proc;
    wc.cbClsExtra = wc.cbWndExtra = 0;
    wc.hInstance = hi;
    wc.hIcon = LoadIcon(NULL, IDI_APPLICATION);
    wc.hCursor = LoadCursor(NULL, IDC_ARROW);
    wc.hbrBackground = (HBRUSH)(COLOR_APPWORKSPACE + 1);
    wc.lpszMenuName = NULL;
    wc.lpszClassName = "US18F";
    RegisterClass(&wc);
    wc.lpfnWndProc = child_proc;
    wc.hbrBackground = (HBRUSH)(COLOR_WINDOW + 1);
    wc.lpszClassName = "US18C";
    RegisterClass(&wc);

    /* a new window's system menu, before anything is done to it */
    {
        HWND t = CreateWindow("US18F", "T", WS_OVERLAPPEDWINDOW, 0, 0, 200, 100, 0, 0, hi, NULL);
        HWND c = CreateWindow("US18F", "C", WS_CHILD | WS_OVERLAPPEDWINDOW, 0, 0, 100, 50, t, 0, hi, NULL);
        lstrcpy(tag, "plain");
        dump_items("top", GetSystemMenu(t, FALSE));
        dump_items("child", GetSystemMenu(c, FALSE));
        /* hidden children shown: where they go in the Z order */
        {
            static const int cmds[] = { SW_SHOW, SW_SHOWNORMAL, SW_SHOWNA, SW_SHOWNOACTIVATE };
            int k, j;
            for (k = 0; k < 4; k++) {
                HWND a[3], w;
                WORD order = 0;
                for (j = 0; j < 3; j++)
                    a[j] = CreateWindow("US18F", "Z", WS_CHILD, j * 10, 0, 10, 10, t, (HMENU)(j + 1), hi, NULL);
                for (j = 0; j < 3; j++)
                    ShowWindow(a[j], cmds[k]);
                for (w = GetWindow(t, GW_CHILD); w; w = GetWindow(w, GW_HWNDNEXT))
                    order = order * 16 + GetWindowWord(w, GWW_ID);
                {
                    char nm[24];
                    wsprintf(nm, "zorder.show%d", cmds[k]);
                    o(nm, order);
                }
                for (j = 0; j < 3; j++)
                    DestroyWindow(a[j]);
            }
        }
        DestroyWindow(t);
    }

    hBar = CreateMenu();
    hFile = CreatePopupMenu();
    AppendMenu(hFile, MF_STRING, 100, "&New");
    AppendMenu(hFile, MF_STRING, 101, "E&xit");
    hWin = CreatePopupMenu();
    AppendMenu(hWin, MF_STRING, 200, "&Cascade");
    AppendMenu(hWin, MF_STRING, 201, "&Tile");
    hHelp = CreatePopupMenu();
    AppendMenu(hHelp, MF_STRING, 300, "&About");
    AppendMenu(hBar, MF_POPUP, (UINT)hFile, "&File");
    AppendMenu(hBar, MF_POPUP, (UINT)hWin, "&Window");
    AppendMenu(hBar, MF_POPUP, (UINT)hHelp, "&Help");
    hBar2 = CreateMenu();
    hWin2 = CreatePopupMenu();
    AppendMenu(hWin2, MF_STRING, 210, "&Arrange");
    AppendMenu(hBar2, MF_STRING, 110, "&Go");
    AppendMenu(hBar2, MF_POPUP, (UINT)hWin2, "&Windows");

    hFrame = CreateWindow("US18F", "US18", WS_OVERLAPPEDWINDOW | WS_CLIPCHILDREN,
                          40, 30, 560, 400, 0, hBar, hi, NULL);
    cc.hWindowMenu = hWin;
    cc.idFirstChild = 500;
    hClient = CreateWindow("MDICLIENT", NULL, WS_CHILD | WS_CLIPCHILDREN | WS_VISIBLE,
                           0, 0, 0, 0, hFrame, (HMENU)1, hi, (LPVOID)&cc);
    ShowWindow(hFrame, SW_SHOWNORMAL);
    UpdateWindow(hFrame);
    hc[1] = make_child("One", 1);
    hc[2] = make_child("Two", 2);
    hc[3] = make_child("Three", 3);
    state("made");

    SendMessage(hClient, WM_MDIMAXIMIZE, (WPARAM)hc[3], 0);
    state("max3");
    SendMessage(hClient, WM_MDIACTIVATE, (WPARAM)hc[1], 0);
    state("act1");
    SendMessage(hClient, WM_MDINEXT, 0, 0);
    state("next");
    h = (HWND)LOWORD(SendMessage(hClient, WM_MDIGETACTIVE, 0, 0));
    SetWindowText(h, "Renamed");
    state("childtext");
    SetWindowText(hFrame, "Frame2");
    state("frametext");
    lstrcpy(tag, "setmenu");
    {
        HMENU r = (HMENU)LOWORD(SendMessage(hClient, WM_MDISETMENU, 0, MAKELONG(hBar2, hWin2)));
        o("ret", r == hBar ? 1 : r == hBar2 ? 2 : r ? 0xFFFF : 0);
    }
    DrawMenuBar(hFrame);
    state("setmenu2");
    SendMessage(hClient, WM_MDISETMENU, 0, MAKELONG(hBar, hWin));
    DrawMenuBar(hFrame);
    state("setmenu1");
    /* the bar's items as the menu would send them */
    SendMessage(hFrame, WM_COMMAND, SC_RESTORE, 0);
    state("cmdrestore");
    SendMessage(hClient, WM_MDIMAXIMIZE, (WPARAM)hc[2], 0);
    state("max2");
    SendMessage(hFrame, WM_COMMAND, SC_NEXTWINDOW, 0);
    state("cmdnext");
    SendMessage(hClient, WM_MDIRESTORE, (WPARAM)LOWORD(SendMessage(hClient, WM_MDIGETACTIVE, 0, 0)), 0);
    state("restore");
    ShowWindow(hc[1], SW_SHOWMAXIMIZED);
    state("showmax1");
    ShowWindow(hc[1], SW_SHOWNORMAL);
    state("shownormal1");
    SendMessage(hc[2], WM_SYSCOMMAND, SC_MAXIMIZE, 0);
    state("sysmax2");
    SendMessage(hc[2], WM_SYSCOMMAND, SC_MINIMIZE, 0);
    state("min2");
    SendMessage(hClient, WM_MDIMAXIMIZE, (WPARAM)hc[1], 0);
    state("max1");
    SendMessage(hFrame, WM_COMMAND, SC_CLOSE, 0);
    state("cmdclose");
    h = (HWND)LOWORD(SendMessage(hClient, WM_MDIGETACTIVE, 0, 0));
    if (h && !IsZoomed(h))
        SendMessage(hClient, WM_MDIMAXIMIZE, (WPARAM)h, 0);
    state("maxagain");
    SendMessage(hClient, WM_MDIDESTROY, (WPARAM)h, 0);
    if (hc[1] == h) hc[1] = 0;
    if (hc[2] == h) hc[2] = 0;
    if (hc[3] == h) hc[3] = 0;
    state("destroyed");
    /* a child that keeps WM_SIZE from DefMDIChildProc */
    quiet_size = 3;
    hc[3] = make_child("Quiet", 3);
    state("made3");
    SendMessage(hClient, WM_MDIMAXIMIZE, (WPARAM)hc[3], 0);
    state("quietmax");
    SendMessage(hClient, WM_MDIRESTORE, (WPARAM)hc[3], 0);
    state("quietrestore");
    quiet_size = 0;
    /* every child gone while one is maximised */
    for (h = GetWindow(hClient, GW_CHILD); h; h = GetWindow(h, GW_HWNDNEXT))
        if (!GetWindow(h, GW_OWNER)) {
            SendMessage(hClient, WM_MDIMAXIMIZE, (WPARAM)h, 0);
            break;
        }
    state("maxlast");
    while ((h = (HWND)LOWORD(SendMessage(hClient, WM_MDIGETACTIVE, 0, 0))) != 0) {
        int i;
        SendMessage(hClient, WM_MDIDESTROY, (WPARAM)h, 0);
        for (i = 1; i <= 3; i++)
            if (hc[i] == h)
                hc[i] = 0;
    }
    state("none");
    /* an iconic child activated while another is maximised, then the
       maximised state given back: the first goes back to its icon (last:
       3.1 then loses track of its bar - two system menus and two restore
       buttons later) */
    hc[1] = make_child("Ein", 1);
    hc[2] = make_child("Zwei", 2);
    trace = 1;
    SendMessage(hc[2], WM_SYSCOMMAND, SC_MINIMIZE, 0);
    trace = 0;
    SendMessage(hClient, WM_MDIMAXIMIZE, (WPARAM)hc[1], 0);
    state("max1b");
    trace = 1;
    SendMessage(hClient, WM_MDIACTIVATE, (WPARAM)hc[2], 0);
    trace = 0;
    state("acticon2");
    SendMessage(hClient, WM_MDIACTIVATE, (WPARAM)hc[1], 0);
    state("act1again");
    out("US18 end\r\n");
    DestroyWindow(hFrame);
    return 0;
}
