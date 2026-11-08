/*
 * mdi2.c - a multiple-document interface for the Windows 2.x build.
 *
 * Windows/386 2.11 has no system MDI client class (observed: creating an
 * "MDICLIENT" window fails), so this provides one ("PMMDIClient") with
 * the messages the shared code sends: WM_MDICREATE, WM_MDIDESTROY,
 * WM_MDIACTIVATE, WM_MDIGETACTIVE, WM_MDINEXT, WM_MDICASCADE, WM_MDITILE,
 * WM_MDIICONARRANGE, WM_MDIMAXIMIZE, plus pm_DefFrameProc,
 * pm_DefMDIChildProc and pm_TranslateMDISysAccel. Children are found by
 * enumeration; nothing has a fixed limit. Behaviour follows the Windows
 * 3.x MDI the 3.x build uses (child IDs from idFirstChild, the window
 * list at the end of the Window menu, Ctrl+F6 and Ctrl+F4).
 */
#include "progman.h"

#ifdef W16_WIN2

/* client window words */
#define CW_ACTIVE       0               /* active child */
#define CW_MENU         2               /* the Window menu */
#define CW_FIRSTID      4               /* idFirstChild */
#define CW_LISTED       6               /* children listed in the menu */
#define CW_EXTRA        8

static char cls_client[] = MDI_CLIENT_CLASS;

static HWND active_of(HWND client)
{
    return (HWND)GetWindowWord(client, CW_ACTIVE);
}

static int child_count(HWND client)
{
    HWND c;
    int n = 0;
    for (c = GetWindow(client, GW_CHILD); c; c = GetWindow(c, GW_HWNDNEXT))
        n++;
    return n;
}

static HWND child_by_id(HWND client, WORD id)
{
    HWND c;
    for (c = GetWindow(client, GW_CHILD); c; c = GetWindow(c, GW_HWNDNEXT))
        if (GetWindowWord(c, GWW_ID) == id)
            return c;
    return NULL;
}

/* The Window menu lists the children: a separator, then "&1 Title" in ID
   order, the active one checked. Rebuilt whenever anything changes. */
static void rebuild_menu(HWND client)
{
    HMENU m = (HMENU)GetWindowWord(client, CW_MENU);
    WORD first = GetWindowWord(client, CW_FIRSTID);
    int listed = GetWindowWord(client, CW_LISTED), n = child_count(client), i;
    HWND act = active_of(client);
    if (!m)
        return;
    for (i = 0; i < listed; i++)
        ChangeMenu(m, first + i, NULL, 0, MF_DELETE | MF_BYCOMMAND);
    if (listed)                         /* the separator after the fixed items */
        ChangeMenu(m, WINDOW_MENU_FIXED, NULL, 0, MF_DELETE | MF_BYPOSITION);
    if (n)
        ChangeMenu(m, 0, NULL, 0, MF_APPEND | MF_SEPARATOR);
    for (i = 0; i < n; i++) {
        HWND c = child_by_id(client, first + i);
        char title[64], item[72];       /* menu text, as the original shows it */
        if (!c)
            continue;
        GetWindowText(c, title, sizeof title);
        item[0] = '&';
        s_utoa(item + 1, i + 1);
        s_cat(item, " ", sizeof item);
        s_cat(item, title, sizeof item);
        ChangeMenu(m, 0, item, first + i, MF_APPEND | MF_STRING | (c == act ? MF_CHECKED : 0));
    }
    SetWindowWord(client, CW_LISTED, n);
}

static void activate(HWND client, HWND w)
{
    HWND old = active_of(client);
    if (!w || w == old) {
        if (w)
            SetFocus(w);
        return;
    }
    SetWindowWord(client, CW_ACTIVE, (WORD)w);
    if (old && IsWindow(old)) {
        SendMessage(old, WM_NCACTIVATE, FALSE, 0);
        SendMessage(old, WM_MDIACTIVATE, FALSE, MAKELONG(w, old));
    }
    BringWindowToTop(w);
    SendMessage(w, WM_NCACTIVATE, TRUE, 0);
    SendMessage(w, WM_MDIACTIVATE, TRUE, MAKELONG(w, old));
    SetFocus(w);
    rebuild_menu(client);
}

/* The topmost child other than `not`. */
static HWND next_child(HWND client, HWND not)
{
    HWND c;
    for (c = GetWindow(client, GW_CHILD); c; c = GetWindow(c, GW_HWNDNEXT))
        if (c != not)
            return c;
    return NULL;
}

static HWND create_child(HWND client, const MDICREATESTRUCT FAR *mc)
{
    RECT r;
    int x = mc->x, y = mc->y, cx = mc->cx, cy = mc->cy, n = child_count(client);
    WORD id = GetWindowWord(client, CW_FIRSTID) + n;
    HWND w;
    GetClientRect(client, &r);
    if (x == (int)CW_USEDEFAULT) {      /* cascade */
        int step = GetSystemMetrics(SM_CYCAPTION) + GetSystemMetrics(SM_CYFRAME);
        x = y = (n * step) % (r.bottom > step * 4 ? r.bottom / 2 : step * 4);
    }
    if (cx == (int)CW_USEDEFAULT) {
        cx = r.right * 3 / 4;
        cy = r.bottom * 3 / 4;
    }
    w = CreateWindow(mc->szClass, mc->szTitle,
                     WS_CHILD | WS_VISIBLE | WS_CLIPSIBLINGS | WS_CAPTION | WS_SYSMENU |
                     WS_THICKFRAME | WS_MINIMIZEBOX | WS_MAXIMIZEBOX | (mc->style & 0xFFFFL),
                     x, y, cx, cy, client, (HMENU)id, mc->hOwner, (LPVOID)mc);
    if (w)
        activate(client, w);
    return w;
}

static void destroy_child(HWND client, HWND w)
{
    WORD first = GetWindowWord(client, CW_FIRSTID), id;
    HWND c;
    if (w == active_of(client)) {
        SetWindowWord(client, CW_ACTIVE, 0);
        activate(client, next_child(client, w));
    }
    id = GetWindowWord(w, GWW_ID);
    DestroyWindow(w);
    /* close the gap in the IDs, as Windows 3.x MDI does */
    for (c = GetWindow(client, GW_CHILD); c; c = GetWindow(c, GW_HWNDNEXT))
        if (GetWindowWord(c, GWW_ID) > id)
            SetWindowWord(c, GWW_ID, GetWindowWord(c, GWW_ID) - 1);
    (void)first;
    rebuild_menu(client);
}

static void cascade(HWND client)
{
    RECT r;
    HWND c, last = NULL;
    int step = GetSystemMetrics(SM_CYCAPTION) + GetSystemMetrics(SM_CYFRAME), i = 0;
    GetClientRect(client, &r);
    /* bottom of the z-order first, so the active window ends up in front */
    for (c = GetWindow(client, GW_CHILD); c; c = GetWindow(c, GW_HWNDNEXT))
        last = c;
    for (c = last; c; c = GetWindow(c, GW_HWNDPREV)) {
        if (IsIconic(c))
            continue;
        MoveWindow(c, i * step, i * step, r.right - (r.right / 4), r.bottom - (r.bottom / 4), TRUE);
        i++;
    }
}

static void tile(HWND client)
{
    RECT r;
    HWND c;
    int n = 0, cols = 1, rows, i = 0;
    GetClientRect(client, &r);
    for (c = GetWindow(client, GW_CHILD); c; c = GetWindow(c, GW_HWNDNEXT))
        if (!IsIconic(c))
            n++;
    if (!n)
        return;
    while (cols * cols < n)
        cols++;
    rows = (n + cols - 1) / cols;
    for (c = GetWindow(client, GW_CHILD); c; c = GetWindow(c, GW_HWNDNEXT)) {
        if (IsIconic(c))
            continue;
        MoveWindow(c, (i % cols) * r.right / cols, (i / cols) * r.bottom / rows,
                   r.right / cols, r.bottom / rows, TRUE);
        i++;
    }
}

/* Minimized children along the bottom, left to right. */
static void arrange_icons(HWND client)
{
    RECT r;
    HWND c;
    int x = 0, cx = GetSystemMetrics(SM_CXICON) * 2, cy = GetSystemMetrics(SM_CYICON) * 2;
    GetClientRect(client, &r);
    for (c = GetWindow(client, GW_CHILD); c; c = GetWindow(c, GW_HWNDNEXT)) {
        if (!IsIconic(c))
            continue;
        SetWindowPos(c, NULL, x + cx / 4, r.bottom - cy + 4, 0, 0, SWP_NOSIZE | SWP_NOZORDER);
        x += cx;
    }
}

LRESULT CALLBACK __export MdiClientWndProc(HWND client, UINT msg, WPARAM wp, LPARAM lp)
{
    switch (msg) {
    case WM_CREATE: {
        const CLIENTCREATESTRUCT FAR *cc =
            (const CLIENTCREATESTRUCT FAR *)((LPCREATESTRUCT)lp)->lpCreateParams;
        SetWindowWord(client, CW_MENU, (WORD)cc->hWindowMenu);
        SetWindowWord(client, CW_FIRSTID, cc->idFirstChild);
        return 0;
    }
    case WM_MDICREATE:
        return (LRESULT)(WORD)create_child(client, (const MDICREATESTRUCT FAR *)lp);
    case WM_MDIDESTROY:
        destroy_child(client, (HWND)wp);
        return 0;
    case WM_MDIACTIVATE:
        activate(client, (HWND)wp);
        return 0;
    case WM_MDIGETACTIVE:
        return MAKELONG(active_of(client), active_of(client) && IsZoomed(active_of(client)));
    case WM_MDINEXT: {
        HWND last = NULL, c;
        for (c = GetWindow(client, GW_CHILD); c; c = GetWindow(c, GW_HWNDNEXT))
            last = c;
        if (last && last != active_of(client))
            activate(client, last);     /* the bottom one comes forward */
        return 0;
    }
    case WM_MDICASCADE:
        cascade(client);
        return 0;
    case WM_MDITILE:
        tile(client);
        return 0;
    case WM_MDIICONARRANGE:
        arrange_icons(client);
        return 0;
    case WM_MDIMAXIMIZE:
        ShowWindow((HWND)wp, SW_SHOWMAXIMIZED);
        return 0;
    case WM_MDIRESTORE:
        ShowWindow((HWND)wp, SW_RESTORE);
        return 0;
    case WM_MDISETMENU:
        return 0;
    case WM_SETFOCUS:
        if (active_of(client))
            SetFocus(active_of(client));
        return 0;
    }
    return DefWindowProc(client, msg, wp, lp);
}

BOOL mdi2_register(HINSTANCE inst)
{
    WNDCLASS wc;
    wc.style = 0;
    wc.lpfnWndProc = MdiClientWndProc;
    wc.cbClsExtra = 0;
    wc.cbWndExtra = CW_EXTRA;
    wc.hInstance = inst;
    wc.hIcon = NULL;
    wc.hCursor = LoadCursor(NULL, IDC_ARROW);
    wc.hbrBackground = (HBRUSH)(COLOR_APPWORKSPACE + 1);
    wc.lpszMenuName = NULL;
    wc.lpszClassName = cls_client;
    return RegisterClass(&wc) != 0;
}

LRESULT FAR pm_DefFrameProc(HWND frame, HWND client, UINT msg, WPARAM wp, LPARAM lp)
{
    switch (msg) {
    case WM_COMMAND:
        if (client) {
            HWND c = child_by_id(client, wp);
            if (c) {                    /* a window chosen from the Window menu */
                if (IsIconic(c))
                    ShowWindow(c, SW_RESTORE);
                activate(client, c);
                return 0;
            }
        }
        break;
    case WM_SETFOCUS:
        if (client && active_of(client)) {
            SetFocus(active_of(client));
            return 0;
        }
        break;
    case WM_NCACTIVATE:
        if (client && active_of(client))
            SendMessage(active_of(client), WM_NCACTIVATE, wp, 0);
        break;
    }
    return DefWindowProc(frame, msg, wp, lp);
}

LRESULT FAR pm_DefMDIChildProc(HWND w, UINT msg, WPARAM wp, LPARAM lp)
{
    HWND client = GetParent(w);
    switch (msg) {
    case WM_NCLBUTTONDOWN:
    case WM_LBUTTONDOWN:
        SendMessage(client, WM_MDIACTIVATE, (WPARAM)w, 0);
        break;
    case WM_SETFOCUS:
        if (active_of(client) != w)
            SendMessage(client, WM_MDIACTIVATE, (WPARAM)w, 0);
        break;
    case WM_SETTEXT: {
        LRESULT r = DefWindowProc(w, msg, wp, lp);
        rebuild_menu(client);
        return r;
    }
    case WM_SIZE:
        if (wp == SIZE_MINIMIZED)
            arrange_icons(client);
        break;
    }
    return DefWindowProc(w, msg, wp, lp);
}

/* Ctrl+F6: next window; Ctrl+F4: close (minimize, for a group). */
BOOL FAR pm_TranslateMDISysAccel(HWND client, MSG FAR *m)
{
    HWND act;
    if (!client || (m->message != WM_KEYDOWN && m->message != WM_SYSKEYDOWN) ||
        GetKeyState(VK_CONTROL) >= 0)
        return FALSE;
    act = active_of(client);
    if (m->wParam == VK_F6) {
        SendMessage(client, WM_MDINEXT, 0, 0);
        return TRUE;
    }
    if (m->wParam == VK_F4 && act) {
        SendMessage(act, WM_SYSCOMMAND, SC_CLOSE, 0);
        return TRUE;
    }
    return FALSE;
}

#endif
