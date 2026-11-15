/*
 * mdi.c - the multiple document interface (3.1 SDK): the MDICLIENT class,
 * DefFrameProc, DefMDIChildProc, TranslateMDISysAccel, CalcChildScroll,
 * ArrangeIconicWindows, and icon titles (class #32772).
 *
 * The MDI client keeps its state in its window extra bytes (Windows 3.1's
 * class has 16: US2) - w16's layout of them is its own: the active child,
 * the window menu, the first child id, the number of children, and whether
 * the active child is maximised.
 */
#include "user.h"

void menu_track_bar(HWND h, char key);
HBRUSH sys_brush(HBRUSH b);

/* MDICLIENT extra bytes (w16's layout) */
#define MDI_ACTIVE    0
#define MDI_WINMENU   2
#define MDI_FIRSTID   4
#define MDI_COUNT     6
#define MDI_MAXED     8         /* the maximised child whose controls are in the frame's bar */
#define MDI_FRAMETEXT 10        /* the frame's own title: a block of the string heap, 0 none */
#define MDI_SCROLL    12        /* the WS_VSCROLL/WS_HSCROLL it was made with */
#define MDI_FROMICON  14        /* the maximised child was an icon before */

HBITMAP frame_bitmap(int which);
void menu_track_bar_at(HWND h, int i);

static HWND client_of(HWND child)
{
    PWND w = valid_wnd(child);
    return w ? w->hwndParent : 0;
}

static HWND active_child(HWND client)
{
    return (HWND)GetWindowWord(client, MDI_ACTIVE);
}

static HWND maxed_child(HWND client)
{
    HWND m = (HWND)GetWindowWord(client, MDI_MAXED);
    return m && valid_wnd(m) ? m : 0;
}

/* ---- a maximised child (probe US18, tests/apps/us18.c, on Windows 3.1) ----
   While an MDI child is maximised its controls are in the frame's menu
   bar: first a popup holding the child's own system menu, drawn as the
   child's system-menu box (MF_POPUP | MF_BITMAP: GetMenuState 0914h with
   the nine items), last its restore button, a command SC_RESTORE (F120h)
   at the bar's right end (MF_HELP | MF_BITMAP, 4004h); the frame's caption
   is "Frame - [Child]"; the child's style loses WS_SYSMENU; its system
   menu grays Move, Size and Maximize (Restore and Minimize enabled), and
   once restored grays Restore only. It all happens in DefMDIChildProc's
   WM_SIZE: a child that keeps WM_SIZE from it is maximised with no change
   to the bar, the caption or its style, and WM_MDIGETACTIVE then says it
   is not maximised. Choosing either control sends the frame WM_COMMAND
   with the SC_ code, which DefFrameProc passes to the active child as
   WM_SYSCOMMAND. */

/* the frame's own title (DefFrameProc's WM_SETTEXT keeps it), and the
   maximised child's in brackets after it (US18: "US18 - [Three]"; a new
   frame title while maximised gives "Frame2 - [Renamed]"). The frame is
   not sent WM_SETTEXT for it. */
static void frame_text(HWND client)
{
    HWND frame = GetParent(client), m = maxed_child(client);
    WORD t = GetWindowWord(client, MDI_FRAMETEXT);
    char buf[200];
    int n;
    if (!frame)
        return;
    if (!t) {
        GetWindowText(frame, buf, 120);
        t = uh_strdup(buf);
        SetWindowWord(client, MDI_FRAMETEXT, t);
    }
    if (t)
        lstr_cpyn(buf, (LPCSTR)UH_PTR(&uhStr, t), 120);
    else
        buf[0] = 0;
    if (m) {
        n = lstr_len(buf);
        lstr_cpyn(buf + n, " - [", 5);
        n += 4;
        GetWindowText(m, buf + n, sizeof buf - n - 2);
        n = lstr_len(buf);
        buf[n] = ']';
        buf[n + 1] = 0;
    }
    DefWindowProc(frame, WM_SETTEXT, 0, (LPARAM)(LPSTR)buf);
}

/* the system menu's states: maximised (Move, Size, Maximize grayed) or
   restored (Restore grayed) (US18) */
static void child_sys_states(HWND child, BOOL maxed)
{
    HMENU s = GetSystemMenu(child, FALSE);
    EnableMenuItem(s, SC_RESTORE, maxed ? MF_ENABLED : MF_GRAYED);
    EnableMenuItem(s, SC_MOVE, maxed ? MF_GRAYED : MF_ENABLED);
    EnableMenuItem(s, SC_SIZE, maxed ? MF_GRAYED : MF_ENABLED);
    EnableMenuItem(s, SC_MINIMIZE, MF_ENABLED);
    EnableMenuItem(s, SC_MAXIMIZE, maxed ? MF_GRAYED : MF_ENABLED);
}

/* the child's controls into the frame's bar, or out of it. The system
   box's bitmap is OBM_CLOSE, which menu.c draws as a child's box (its
   right half and a black line, 19 x 18) */
static void bar_add(HWND client, HWND child)
{
    HMENU bar = GetMenu(GetParent(client)), s = GetSystemMenu(child, FALSE);
    if (!bar || !s)
        return;
    InsertMenu(bar, 0, MF_BYPOSITION | MF_POPUP | MF_BITMAP, (UINT)s,
               (LPCSTR)MAKELONG(frame_bitmap(0), 0));
    AppendMenu(bar, MF_HELP | MF_BITMAP, SC_RESTORE, (LPCSTR)MAKELONG(frame_bitmap(1), 0));
}

static void bar_remove_from(HMENU bar, HWND child)
{
    PWND w = valid_wnd(child);
    int n;
    if (!bar || !IsMenu(bar))
        return;
    if (w && w->hSysMenu && GetSubMenu(bar, 0) == (HMENU)w->hSysMenu)
        RemoveMenu(bar, 0, MF_BYPOSITION);
    n = GetMenuItemCount(bar);
    if (n > 0 && GetMenuItemID(bar, n - 1) == SC_RESTORE &&
        (GetMenuState(bar, n - 1, MF_BYPOSITION) & MF_BITMAP))
        RemoveMenu(bar, n - 1, MF_BYPOSITION);
}

static void bar_remove(HWND client, HWND child)
{
    bar_remove_from(GetMenu(GetParent(client)), child);
}

/* child stops being the maximised one: its controls leave the bar */
static void unmax(HWND client, HWND child, BOOL restored)
{
    PWND w;
    SetWindowWord(client, MDI_MAXED, 0);
    if ((w = valid_wnd(child)) != 0)
        w->style |= WS_SYSMENU;
    if (restored)
        child_sys_states(child, FALSE);
    bar_remove(client, child);
}

/* DefMDIChildProc's WM_SIZE */
static void child_size(HWND client, HWND h, WPARAM how)
{
    HWND m = maxed_child(client);
    HWND frame = GetParent(client);
    if (how == SIZE_MAXIMIZED) {
        PWND w;
        if (m == h)
            return;
        if (m)
            unmax(client, m, TRUE);
        SetWindowWord(client, MDI_MAXED, (WORD)h);
        if ((w = valid_wnd(h)) != 0)
            w->style &= ~WS_SYSMENU;
        child_sys_states(h, TRUE);
        bar_add(client, h);
    } else if (m == h) {
        /* its states stay (US18: only a child that gives the maximised
           state to another gets Restore grayed again) */
        unmax(client, h, FALSE);
        SetWindowWord(client, MDI_FROMICON, 0);
    } else if (how == SIZE_MINIMIZED) {
        /* minimised (not from maximised): Size and Minimize grayed (US18) */
        HMENU s = GetSystemMenu(h, FALSE);
        EnableMenuItem(s, SC_RESTORE, MF_ENABLED);
        EnableMenuItem(s, SC_MOVE, MF_ENABLED);
        EnableMenuItem(s, SC_SIZE, MF_GRAYED);
        EnableMenuItem(s, SC_MINIMIZE, MF_GRAYED);
        EnableMenuItem(s, SC_MAXIMIZE, MF_ENABLED);
        return;
    } else
        return;
    frame_text(client);
    DrawMenuBar(frame);
}

static HWND next_child(HWND client, HWND h);
static void child_activate(HWND client, HWND child);

/* DefMDIChildProc's WM_SIZE: the active child minimised gives the
   activation to the next one that is not (US18: 3.1's maximised child
   minimised, WM_SIZE minimised, then it got WM_MDIACTIVATE(0) and the
   restored child WM_MDIACTIVATE(1), not maximised) */
static void child_sized(HWND client, HWND h, WPARAM how)
{
    child_size(client, h, how);
    if (how == SIZE_MINIMIZED && active_child(client) == h) {
        HWND n = next_child(client, h);
        if (n && !IsIconic(n))
            child_activate(client, n);
    }
}

/* the maximised child of an MDI client, 0 none (Alt+-: defwnd.c) */
HWND mdi_maxed_child(HWND client)
{
    return valid_wnd(client) ? maxed_child(client) : 0;
}

/* Alt+- in MDI child c whose controls are in the frame's bar: its system
   menu opens there (3.1: Program Manager's maximised group, the popup
   under the bitmap, the bitmap inverted, Restore selected). FALSE: not
   maximised */
BOOL mdi_sysmenu_key(HWND c)
{
    HWND client = client_of(c);
    if (!client || maxed_child(client) != c)
        return FALSE;
    menu_track_bar_at(GetParent(client), 0);
    return TRUE;
}

/* ---- the window menu's list of children (US18) ----
   After the window menu's own items, a separator and one item per child
   in id order, "&1 One", "&2 Two" ..., id the child's (the first child id
   upwards), the active one checked; none at all, separator included,
   with no children. Children's ids stay consecutive: when one goes, those
   after it move down one (3.1: "One", id 500, closed, "Two" became
   "&1 Two", id 500). UNVERIFIED: beyond nine children (3.1 SDK: a "More
   Windows..." item and a dialog) - here the first nine are listed. */
#define MDI_LISTMAX 9

static void winmenu_clear(HWND client, HMENU wm)
{
    UINT first = GetWindowWord(client, MDI_FIRSTID);
    int i, at = -1;
    if (!wm || !IsMenu(wm))
        return;
    for (i = GetMenuItemCount(wm) - 1; i >= 0; i--) {
        UINT id = GetMenuItemID(wm, i);
        if (id >= first && id < first + MDI_LISTMAX + 1 &&
            !(GetMenuState(wm, i, MF_BYPOSITION) & (MF_POPUP | MF_SEPARATOR))) {
            RemoveMenu(wm, i, MF_BYPOSITION);
            at = i;
        }
    }
    if (at > 0 && (GetMenuState(wm, at - 1, MF_BYPOSITION) & MF_SEPARATOR))
        RemoveMenu(wm, at - 1, MF_BYPOSITION);
}

static BOOL is_mdi_child(HWND s)
{
    return !GetWindow(s, GW_OWNER) && GetClassWord(s, -32) != 32772;
}

static void winmenu_update(HWND client, HWND skip)
{
    HMENU wm = (HMENU)GetWindowWord(client, MDI_WINMENU);
    UINT first = GetWindowWord(client, MDI_FIRSTID), k;
    HWND s, act = active_child(client);
    BOOL sep = FALSE;
    if (!wm || !IsMenu(wm))
        return;
    winmenu_clear(client, wm);
    for (k = 0; k < MDI_LISTMAX; k++)
        for (s = GetWindow(client, GW_CHILD); s; s = GetWindow(s, GW_HWNDNEXT))
            if (s != skip && is_mdi_child(s) && GetWindowWord(s, GWW_ID) == first + k) {
                char t[90];
                int n;
                if (!sep)
                    AppendMenu(wm, MF_SEPARATOR, 0, NULL);
                sep = TRUE;
                t[0] = '&';
                t[1] = (char)('1' + k);
                t[2] = ' ';
                GetWindowText(s, t + 3, sizeof t - 3);
                n = lstr_len(t);
                (void)n;
                AppendMenu(wm, MF_STRING | (s == act ? MF_CHECKED : 0), first + k, t);
                break;
            }
}

static BOOL mdi_creating;       /* WM_MDICREATE under way */

/* the child to activate after h: the first visible one under it that
   is not minimised, else a minimised one (US18: Windows 3.1, with the
   maximised child closed, activated the restored one, not the icon in
   front of it; the same after the active child is minimised) */
static HWND next_child(HWND client, HWND h)
{
    HWND s, icon = 0;
    for (s = GetWindow(client, GW_CHILD); s; s = GetWindow(s, GW_HWNDNEXT))
        if (s != h && is_mdi_child(s) && IsWindowVisible(s)) {
            if (!IsIconic(s))
                return s;
            if (!icon)
                icon = s;
        }
    return icon;
}

static void child_activate(HWND client, HWND child)
{
    HWND old = active_child(client);
    if (old == child)
        return;
    SetWindowWord(client, MDI_ACTIVE, (WORD)child);
    if (old && valid_wnd(old)) {
        send_message(old, WM_NCACTIVATE, FALSE, 0);
        send_message(old, WM_MDIACTIVATE, FALSE, MAKELONG(child, old));
    }
    /* the maximised child deactivated: the new one is maximised, then the
       old one restored (US18, 3.1's order: old WM_MDIACTIVATE(0), new
       WM_SIZE maximised, old WM_SIZE restored, new WM_MDIACTIVATE(1)) */
    /* A child maximised so from its icon goes back to its icon when it
       gives the state away (US18: WM_SIZE minimised, 36 x 36, in place
       of restored) */
    if (old && child && old == maxed_child(client) && valid_wnd(child)) {
        void restore_noactivate(HWND h);
        BOOL wasicon = IsIconic(child), oldicon = (HWND)GetWindowWord(client, MDI_FROMICON) == old;
        ShowWindow(child, SW_SHOWMAXIMIZED);
        SetWindowWord(client, MDI_FROMICON, wasicon ? (WORD)child : 0);
        if (valid_wnd(old)) {
            if (oldicon)
                ShowWindow(old, SW_SHOWMINNOACTIVE);
            else
                restore_noactivate(old);
        }
    }
    if (child && valid_wnd(child)) {
        set_window_pos(child, HWND_TOP, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
        send_message(child, WM_NCACTIVATE, TRUE, 0);
        send_message(child, WM_MDIACTIVATE, TRUE, MAKELONG(child, old));
        /* the focus to the child when the frame is active - but not as
           WM_MDICREATE makes it (3.1: File Manager's directory window,
           made with the frame active, did not get the focus; its tree
           had it from the program's own SetFocus. w16 then took it to the
           child, whose WM_SETFOCUS handler put it back, and File Manager
           went on to read the root of the drive) */
        if (top_level(client) == hwndActive && !mdi_creating)
            set_focus(child);
    }
    if (!mdi_creating)
        winmenu_update(client, 0);          /* the check mark */
}

static HWND mdi_create(HWND client, MDICREATESTRUCT __far *mc)
{
    PWND cw = valid_wnd(client);
    int n = GetWindowWord(client, MDI_COUNT);
    UINT id = GetWindowWord(client, MDI_FIRSTID) + n;
    int x = mc->x, y = mc->y, cx = mc->cx, cy = mc->cy;
    DWORD style = mc->style | WS_CHILD | WS_CLIPSIBLINGS | WS_CAPTION | WS_SYSMENU |
                  WS_THICKFRAME | WS_MINIMIZEBOX | WS_MAXIMIZEBOX;
    HWND h;
    BOOL vis = TRUE;
    if (!cw)
        return 0;
    {
        /* the default place and size: with step the cascade's (22), the
           client's height holds k = height / (3 step) steps; the size is
           the client's less k steps each way, the n-th child k-cycling
           down and right by a step (Windows 3.1: File Manager's first
           directory window in a 632 x 411 client, 500 x 279 at 0,0;
           the formula is also Wine's, its 16-bit MDI) */
        int step = sysmet[SM_CYCAPTION] + 2 * sysmet[SM_CYBORDER];
        int w = cw->rcClient.right - cw->rcClient.left, ht = cw->rcClient.bottom - cw->rcClient.top;
        int k = ht / (3 * step);
        if (x == (int)CW_USEDEFAULT)
            x = y = step * (n % (k + 1));
        if (cx == (int)CW_USEDEFAULT) {
            cx = w - step * k;
            cy = ht - step * k;
        }
    }
    style &= ~(WS_VISIBLE | WS_MAXIMIZE | WS_MINIMIZE);
    /* a maximised child is restored first, and the new one is not
       maximised (US18: 3.1's maximised child got WM_SIZE restored before
       the new child's first WM_SIZE) */
    if (maxed_child(client)) {
        void restore_noactivate(HWND h);
        restore_noactivate(maxed_child(client));
    }
    mdi_creating = TRUE;
    h = create_window(0, mc->szClass, mc->szTitle, style, x, y, cx, cy, client, (HMENU)id,
                      mc->hOwner, (void __far *)mc);
    if (!h) {
        mdi_creating = FALSE;
        return 0;
    }
    SetWindowWord(client, MDI_COUNT, n + 1);
    /* a new child's system menu has Minimize grayed until it is first
       maximised (US18: 3.1's GetMenuState on a new child's system menu,
       also for one that never passes WM_SIZE on; UNVERIFIED why) */
    EnableMenuItem(GetSystemMenu(h, FALSE), SC_MINIMIZE, MF_GRAYED);
    if (mc->style & WS_MINIMIZE)
        ShowWindow(h, SW_SHOWMINNOACTIVE);
    else if (mc->style & WS_MAXIMIZE)
        ShowWindow(h, SW_SHOWMAXIMIZED);
    else if (vis)
        ShowWindow(h, SW_SHOW);         /* WM_CHILDACTIVATE activates it */
    child_activate(client, h);
    mdi_creating = FALSE;
    winmenu_update(client, 0);
    /* made, the active child gets the focus when the frame is active and
       the focus is on the frame or the client (3.1: Program Manager's
       groups, made after its frame took the focus, answer the arrow keys
       straight away) - not before it is made (File Manager, above) */
    if (top_level(client) == hwndActive && active_child(client) == h && !IsIconic(h) &&
        (hwndFocus == client || hwndFocus == top_level(client)))
        set_focus(h);
    return h;
}

/* WM_MDIDESTROY. The active child is deactivated and the next one
   activated first - maximised in its place if it was (US18: old
   WM_MDIACTIVATE(0), next WM_SIZE maximised, old WM_SIZE restored, next
   WM_MDIACTIVATE(1)); the last child is only deactivated, and its
   controls leave the bar */
static void mdi_destroy(HWND client, HWND child)
{
    UINT id;
    HWND s;
    if (!valid_wnd(child))
        return;
    if (active_child(client) == child) {
        HWND next = next_child(client, child);
        if (next)
            child_activate(client, next);
        else {
            SetWindowWord(client, MDI_ACTIVE, 0);
            send_message(child, WM_NCACTIVATE, FALSE, 0);
            send_message(child, WM_MDIACTIVATE, FALSE, MAKELONG(0, child));
        }
    }
    if (maxed_child(client) == child) {
        unmax(client, child, FALSE);
        frame_text(client);
        DrawMenuBar(GetParent(client));
    }
    if (!valid_wnd(child))
        return;
    id = GetWindowWord(child, GWW_ID);
    DestroyWindow(child);
    SetWindowWord(client, MDI_COUNT, GetWindowWord(client, MDI_COUNT) - 1);
    /* the ids after it move down one */
    for (s = GetWindow(client, GW_CHILD); s; s = GetWindow(s, GW_HWNDNEXT))
        if (is_mdi_child(s) && GetWindowWord(s, GWW_ID) > id &&
            GetWindowWord(s, GWW_ID) < GetWindowWord(client, MDI_FIRSTID) + 0x1000)
            SetWindowWord(s, GWW_ID, GetWindowWord(s, GWW_ID) - 1);
    winmenu_update(client, 0);
}

void WINAPI U_SCROLLCHILDREN(HWND h, UINT msg, WPARAM wp, LPARAM lp);
void WINAPI CalcChildScroll(HWND h, WORD sb);

/* Window > Cascade and Tile (WM_MDICASCADE, WM_MDITILE), as Windows 3.1
   arranges Program Manager's groups (tests/scripts/user/mditile.txt:
   StartUp, Main and Games open, Games active, Accessories minimised, in
   a 504x290 client area):
   - the visible children that are not minimised take part; the row of
     icons at the bottom (SM_CYICONSPACING) is left out of the area when
     there are minimised ones;
   - cascade: the children from the bottom of the Z order up, the active
     one last; the i-th at i steps of 22 (SM_CYCAPTION + 2) right and
     down, all the size of the area less n steps: StartUp at 0,0, Main
     at 22,22, Games at 44,44, all 438x152; the Z order stays;
   - tile (vertical, the default): the active child first, then the
     others from the top; floor(sqrt(n)) rows of n / rows columns (the
     extra ones in the last rows): Games, Main, StartUp as three columns
     of 168x218.
   UNVERIFIED beyond that sample: the step's derivation, the order of
   extra windows, MDITILE_HORIZONTAL (rows and columns swapped here). A
   maximised child is restored first. */
static void arrange(HWND client, BOOL tile, WPARAM how)
{
    PWND cw = valid_wnd(client);
    HWND *list, s, act = active_child(client);
    int n = 0, i, step, icons = 0;
    RECT c;
    if (!cw)
        return;
    if (act && IsZoomed(act))
        ShowWindow(act, SW_RESTORE);
    if (act && (!valid_wnd(act) || !(PW(act)->style & WS_VISIBLE)))
        act = 0;
    for (s = cw->hwndChild; s; s = PW(s)->hwndNext)
        n++;
    list = (HWND __near *)LocalAlloc(LPTR, (n + 1) * sizeof(HWND));
    if (!list)
        return;
    n = 0;
    if (act && IsIconic(act)) {
        icons = 1;
        act = 0;
    }
    if (tile && act)
        list[n++] = act;
    for (s = cw->hwndChild; s; s = PW(s)->hwndNext) {
        PWND sw = PW(s);
        if (sw->hwndOwner || !(sw->style & WS_VISIBLE) || s == act)
            continue;
        if (sw->style & WS_MINIMIZE) {
            icons = 1;
            continue;
        }
        list[n++] = s;
    }
    if (!tile) {
        /* from the bottom up, the active one last */
        for (i = 0; i < n / 2; i++) {
            HWND t = list[i];
            list[i] = list[n - 1 - i];
            list[n - 1 - i] = t;
        }
        if (act)
            list[n++] = act;
    }
    if (!n) {
        LocalFree((HLOCAL)list);
        return;
    }
    GetClientRect(client, &c);
    if (icons)
        c.bottom -= sysmet[SM_CYICONSPACING];
    if (c.bottom < sysmet[SM_CYCAPTION])
        c.bottom = sysmet[SM_CYCAPTION];
    if (!tile) {
        int w, h;
        step = sysmet[SM_CYCAPTION] + 2 * sysmet[SM_CYBORDER];
        w = c.right - n * step;
        h = c.bottom - n * step;
        if (w < sysmet[SM_CXMIN]) w = sysmet[SM_CXMIN];
        if (h < sysmet[SM_CYMIN]) h = sysmet[SM_CYMIN];
        for (i = 0; i < n; i++)
            set_window_pos(list[i], 0, i * step, i * step, w, h, SWP_NOACTIVATE | SWP_NOZORDER);
    } else {
        int rows = 1, k = 0, row;
        BOOL horz = (how & MDITILE_HORIZONTAL) != 0;
        while ((rows + 1) * (rows + 1) <= n)
            rows++;
        for (row = 0; row < rows; row++) {
            int cols = n / rows + (row >= rows - n % rows ? 1 : 0), col;
            for (col = 0; col < cols && k < n; col++, k++) {
                int x0, x1, y0, y1;
                if (!horz) {
                    x0 = c.right * col / cols; x1 = c.right * (col + 1) / cols;
                    y0 = c.bottom * row / rows; y1 = c.bottom * (row + 1) / rows;
                } else {
                    y0 = c.bottom * col / cols; y1 = c.bottom * (col + 1) / cols;
                    x0 = c.right * row / rows; x1 = c.right * (row + 1) / rows;
                }
                set_window_pos(list[k], 0, x0, y0, x1 - x0, y1 - y0,
                               SWP_NOACTIVATE | SWP_NOZORDER);
            }
        }
    }
    LocalFree((HLOCAL)list);
    CalcChildScroll(client, SB_BOTH);
}

LRESULT WINAPI __loadds MDIClientWndProc(HWND h, UINT msg, WPARAM wp, LPARAM lp)
{
    switch (msg) {
    case WM_CREATE: {
        CLIENTCREATESTRUCT __far *cc =
            (CLIENTCREATESTRUCT __far *)((CREATESTRUCT __far *)lp)->lpCreateParams;
        if (cc) {
            SetWindowWord(h, MDI_WINMENU, (WORD)cc->hWindowMenu);
            SetWindowWord(h, MDI_FIRSTID, cc->idFirstChild);
        }
        /* its scroll bars show only when children go past its edges:
           none at first (Program Manager's MDI client has the styles
           and no bars) */
        {
            PWND w = valid_wnd(h);
            if (w && (w->style & (WS_VSCROLL | WS_HSCROLL))) {
                SetWindowWord(h, MDI_SCROLL, HIWORD(w->style & (WS_VSCROLL | WS_HSCROLL)));
                w->style &= ~(WS_VSCROLL | WS_HSCROLL);
                calc_client(w);
            }
        }
        return 0;
    }
    case WM_MDICREATE:
        return (LONG)(WORD)mdi_create(h, (MDICREATESTRUCT __far *)lp);
    case WM_MDIDESTROY:
        mdi_destroy(h, (HWND)wp);
        return 0;
    case WM_MDIACTIVATE:
        child_activate(h, (HWND)wp);
        return 0;
    case WM_MDIGETACTIVE: {
        HWND a = active_child(h);
        return MAKELONG(a, a && a == maxed_child(h));
    }
    case WM_MDIMAXIMIZE:
        ShowWindow((HWND)wp, SW_SHOWMAXIMIZED);
        return 0;
    case WM_MDIRESTORE:
        ShowWindow((HWND)wp, SW_RESTORE);
        return 0;
    case WM_MDINEXT: {
        HWND cur = wp ? (HWND)wp : active_child(h), s, last = 0;
        if (!cur)
            return 0;
        /* next: the child under it in Z order, the current one going to
           the bottom (Windows 3.1, tests/scripts/user/mditile.txt: from
           Main over StartUp, Games, Accessories, two Ctrl+F6 activate
           StartUp, then Games, leaving Games, Accessories, Main,
           StartUp); previous: the bottom one (UNVERIFIED) */
        if (!lp) {
            s = GetWindow(cur, GW_HWNDNEXT);
            while (s && (GetWindow(s, GW_OWNER) || GetClassWord(s, -32) == 32772))
                s = GetWindow(s, GW_HWNDNEXT);
            if (s) {
                child_activate(h, s);
                set_window_pos(cur, HWND_BOTTOM, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE |
                               SWP_NOACTIVATE);
            }
        } else {
            for (s = GetWindow(h, GW_CHILD); s; s = GetWindow(s, GW_HWNDNEXT))
                if (!GetWindow(s, GW_OWNER) && GetClassWord(s, -32) != 32772)
                    last = s;
            if (last && last != cur)
                child_activate(h, last);
        }
        return 0;
    }
    case WM_MDISETMENU: {
        /* wParam TRUE: the window menu's list made again, lParam not
           used; else lParam's frame menu (0: the same) and window menu
           (0: the same). The list leaves the old window menu, a maximised
           child's controls the old bar, and both go into the new ones
           (US18). The old frame menu is returned */
        HWND frame = GetParent(h), m = maxed_child(h);
        HMENU old = GetMenu(frame);
        if (!wp) {
            if (LOWORD(lp) && m)
                bar_remove_from(old, m);
            if (HIWORD(lp)) {
                winmenu_clear(h, (HMENU)GetWindowWord(h, MDI_WINMENU));
                SetWindowWord(h, MDI_WINMENU, HIWORD(lp));
            }
            if (LOWORD(lp)) {
                SetMenu(frame, (HMENU)LOWORD(lp));
                if (m)
                    bar_add(h, m);
            }
        }
        winmenu_update(h, 0);
        return (LONG)(WORD)old;
    }
    case WM_MDICASCADE:
        arrange(h, FALSE, wp);
        return 1;
    case WM_MDITILE:
        arrange(h, TRUE, wp);
        return 1;
    case WM_MDIICONARRANGE:
        ArrangeIconicWindows(h);
        return 0;
    case WM_HSCROLL:
    case WM_VSCROLL:
        U_SCROLLCHILDREN(h, msg, wp, lp);
        return 0;
    case WM_SETFOCUS: {
        HWND a = active_child(h);
        if (a && valid_wnd(a) && !IsIconic(a))
            set_focus(a);
        return 0;
    }
    case WM_SIZE: {
        HWND a = maxed_child(h);
        if (a && IsZoomed(a)) {
            RECT r;
            GetClientRect(h, &r);
            MoveWindow(a, -sysmet[SM_CXFRAME], -sysmet[SM_CYFRAME] - sysmet[SM_CYCAPTION] +
                       sysmet[SM_CYBORDER], r.right + 2 * sysmet[SM_CXFRAME],
                       r.bottom + 2 * sysmet[SM_CYFRAME] + sysmet[SM_CYCAPTION] - sysmet[SM_CYBORDER],
                       TRUE);
        } else if (wp != SIZE_MINIMIZED)
            /* the bars follow the area (3.1: File Manager made narrower
               than its directory window shows a horizontal bar, thumb at
               the left) */
            CalcChildScroll(h, SB_BOTH);
        break;
    }
    }
    return DefWindowProc(h, msg, wp, lp);
}

LRESULT WINAPI DefFrameProc(HWND h, HWND client, UINT msg, WPARAM wp, LPARAM lp)
{
    if (client && valid_wnd(client)) {
        switch (msg) {
        case WM_COMMAND: {
            UINT first = GetWindowWord(client, MDI_FIRSTID);
            if (wp >= first && wp < first + GetWindowWord(client, MDI_COUNT)) {
                HWND s;
                for (s = GetWindow(client, GW_CHILD); s; s = GetWindow(s, GW_HWNDNEXT))
                    if (GetWindowWord(s, GWW_ID) == wp) {
                        if (IsIconic(s))
                            ShowWindow(s, SW_RESTORE);
                        child_activate(client, s);
                        break;
                    }
                return 0;
            }
            if (wp >= 0xF000) {             /* a maximised child's system menu */
                HWND a = active_child(client);
                if (a)
                    return send_message(a, WM_SYSCOMMAND, wp, lp);
            }
            break;
        }
        case WM_SETFOCUS:
            set_focus(client);
            return 0;
        case WM_SETTEXT: {
            /* the frame's own title kept; with a maximised child its
               title follows in brackets (US18) */
            WORD t = GetWindowWord(client, MDI_FRAMETEXT);
            if (t)
                uh_free(&uhStr, t);
            SetWindowWord(client, MDI_FRAMETEXT, uh_strdup(lp ? (LPCSTR)lp : ""));
            if (maxed_child(client)) {
                frame_text(client);
                return 1;
            }
            break;
        }
        case WM_SIZE: {
            RECT r;
            GetClientRect(h, &r);
            MoveWindow(client, 0, 0, r.right, r.bottom, TRUE);
            return 0;
        }
        case WM_NCACTIVATE: {
            HWND a = active_child(client);
            if (a)
                send_message(a, WM_NCACTIVATE, wp, lp);
            break;
        }
        case WM_MENUCHAR:
            break;
        }
    }
    return DefWindowProc(h, msg, wp, lp);
}

LRESULT WINAPI DefMDIChildProc(HWND h, UINT msg, WPARAM wp, LPARAM lp)
{
    HWND client = client_of(h);
    switch (msg) {
    case WM_SIZE:
        if (client)
            child_sized(client, h, wp);
        break;
    case WM_SETTEXT: {
        LRESULT r = DefWindowProc(h, msg, wp, lp);
        if (client) {
            if (maxed_child(client) == h)
                frame_text(client);
            winmenu_update(client, 0);
        }
        return r;
    }
    case WM_DESTROY:
        /* destroyed while maximised, not by WM_MDIDESTROY */
        if (client && maxed_child(client) == h) {
            unmax(client, h, FALSE);
            frame_text(client);
            DrawMenuBar(GetParent(client));
        }
        break;
    case WM_WINDOWPOSCHANGED:
        /* moved or sized: the client's scroll bars follow */
        if (client && !IsZoomed(h))
            CalcChildScroll(client, SB_BOTH);
        break;
    case WM_SETFOCUS:
        if (client && active_child(client) != h)
            child_activate(client, h);
        break;
    case WM_CHILDACTIVATE:
        if (client)
            child_activate(client, h);
        return 0;
    case WM_MOUSEACTIVATE: {
        LONG r = DefWindowProc(h, msg, wp, lp);
        if (client && active_child(client) != h)
            child_activate(client, h);
        return r;
    }
    case WM_NCACTIVATE:
        break;
    case WM_CLOSE:
        if (client)
            send_message(client, WM_MDIDESTROY, (WPARAM)h, 0);
        return 0;
    case WM_SYSCOMMAND:
        switch (wp & 0xFFF0) {
        case SC_MAXIMIZE:
            if (client)
                send_message(client, WM_MDIMAXIMIZE, (WPARAM)h, 0);
            return 0;
        case SC_NEXTWINDOW:
            if (client)
                send_message(client, WM_MDINEXT, 0, 0);
            return 0;
        case SC_KEYMENU:
            if (client && GetParent(client))
                return send_message(GetParent(client), WM_SYSCOMMAND, wp, lp);
            break;
        }
        break;
    case WM_GETMINMAXINFO: {
        MINMAXINFO __far *mm = (MINMAXINFO __far *)lp;
        RECT r;
        if (client) {
            GetClientRect(client, &r);
            mm->ptMaxSize.x = r.right + 2 * sysmet[SM_CXFRAME];
            mm->ptMaxSize.y = r.bottom + 2 * sysmet[SM_CYFRAME] + sysmet[SM_CYCAPTION] -
                              sysmet[SM_CYBORDER];
            mm->ptMaxPosition.x = -sysmet[SM_CXFRAME];
            mm->ptMaxPosition.y = -sysmet[SM_CYFRAME] - sysmet[SM_CYCAPTION] + sysmet[SM_CYBORDER];
            mm->ptMaxTrackSize = mm->ptMaxSize;
        }
        return 0;
    }
    case WM_MENUCHAR:
        return MAKELONG(0, 2);
    }
    return DefWindowProc(h, msg, wp, lp);
}

BOOL WINAPI TranslateMDISysAccel(HWND client, MSG __far *m)
{
    HWND a;
    if (m->message != WM_KEYDOWN && m->message != WM_SYSKEYDOWN)
        return FALSE;
    if (!(GetKeyState(VK_CONTROL) & 0x8000) || !valid_wnd(client))
        return FALSE;
    a = active_child(client);
    if (!a || !IsWindowEnabled(a))
        return FALSE;
    switch (m->wParam) {
    case VK_F4:
        PostMessage(a, WM_SYSCOMMAND, SC_CLOSE, 0);
        return TRUE;
    case VK_F6:
    case VK_TAB:
        PostMessage(a, WM_SYSCOMMAND, (GetKeyState(VK_SHIFT) & 0x8000) ? SC_PREVWINDOW :
                    SC_NEXTWINDOW, 0);
        return TRUE;
    }
    return FALSE;
}

/* The MDI client's scroll bars (made with WS_HSCROLL/WS_VSCROLL, kept in
   MDI_SCROLL): shown while children reach past its edges; the range runs
   over the children's extent and the client area, the position 0 being
   where the children are now (UNVERIFIED: Windows 3.1's exact ranges and
   which children count - here the visible, not minimised ones). */
void WINAPI CalcChildScroll(HWND h, WORD sb)
{
    PWND w = valid_wnd(h);
    WORD want = (WORD)GetWindowWord(h, MDI_SCROLL);
    RECT u, c;
    HWND s;
    int bar;
    if (!w || !want)
        return;
    GetClientRect(h, &c);
    u = c;
    for (s = w->hwndChild; s; s = PW(s)->hwndNext) {
        PWND sw = PW(s);
        RECT r = sw->rcWindow;
        if (!(sw->style & WS_VISIBLE) || (sw->style & WS_MINIMIZE) || sw->hwndOwner)
            continue;
        OffsetRect(&r, -w->rcClient.left, -w->rcClient.top);
        UnionRect(&u, &u, &r);
    }
    for (bar = SB_HORZ; bar <= SB_VERT; bar++) {
        int lo = bar == SB_HORZ ? u.left : u.top;
        int hi = bar == SB_HORZ ? u.right - c.right : u.bottom - c.bottom;
        DWORD style = bar == SB_HORZ ? WS_HSCROLL : WS_VSCROLL;
        if (sb != SB_BOTH && sb != (WORD)bar)
            continue;
        if (!(want & HIWORD(style)))
            continue;
        if (lo < 0 || hi > 0) {
            SetScrollRange(h, bar, lo, hi, FALSE);
            SetScrollPos(h, bar, 0, FALSE);
            if (!(PW(h)->style & style))
                ShowScrollBar(h, bar, TRUE);
            else
                RedrawWindow(h, NULL, NULL, RDW_FRAME | RDW_INVALIDATE);
        } else if (PW(h)->style & style)
            ShowScrollBar(h, bar, FALSE);
    }
}

/* USER.463: the MDI client's WM_HSCROLL/WM_VSCROLL - the children move */
void WINAPI U_SCROLLCHILDREN(HWND h, UINT msg, WPARAM wp, LPARAM lp)
{
    int bar = msg == WM_VSCROLL ? SB_VERT : SB_HORZ, lo, hi, pos, np;
    RECT c;
    if (!valid_wnd(h))
        return;
    GetScrollRange(h, bar, &lo, &hi);
    pos = GetScrollPos(h, bar);
    GetClientRect(h, &c);
    switch (wp) {
    case SB_LINEUP: np = pos - 8; break;
    case SB_LINEDOWN: np = pos + 8; break;
    case SB_PAGEUP: np = pos - (bar == SB_VERT ? c.bottom : c.right); break;
    case SB_PAGEDOWN: np = pos + (bar == SB_VERT ? c.bottom : c.right); break;
    case SB_TOP: np = lo; break;
    case SB_BOTTOM: np = hi; break;
    case SB_THUMBTRACK:
    case SB_THUMBPOSITION: np = (int)LOWORD(lp); break;
    case SB_ENDSCROLL:
        CalcChildScroll(h, bar);
        return;
    default: return;
    }
    if (np < lo)
        np = lo;
    if (np > hi)
        np = hi;
    if (np == pos)
        return;
    SetScrollPos(h, bar, np, TRUE);
    if (bar == SB_VERT)
        ScrollWindow(h, 0, pos - np, NULL, NULL);
    else
        ScrollWindow(h, pos - np, 0, NULL, NULL);
}

/* minimised children in rows along the bottom (UNVERIFIED spacing) */
UINT WINAPI ArrangeIconicWindows(HWND parent)
{
    PWND p = valid_wnd(parent);
    HWND s;
    int x, y, cx = sysmet[SM_CXICONSPACING], cy = sysmet[SM_CYICONSPACING];
    int width, n = 0;
    if (!p)
        return 0;
    width = p->rcClient.right - p->rcClient.left;
    x = 0;
    y = p->rcClient.bottom - p->rcClient.top - cy;
    for (s = p->hwndChild; s; s = PW(s)->hwndNext) {
        if (!(PW(s)->style & WS_MINIMIZE) || !(PW(s)->style & WS_VISIBLE))
            continue;
        if (x + cx > width && x) {
            x = 0;
            y -= cy;
        }
        set_window_pos(s, 0, x + (cx - sysmet[SM_CXICON]) / 2, y + 2, 0, 0,
                       SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE);
        x += cx;
        n++;
    }
    return n ? cy : 0;
}

/* ---- icon titles (#32772): the caption under a minimised window ---- */
static HFONT hfontIconTitle;

static HFONT icon_font(void)
{
    if (!hfontIconTitle) {
        LOGFONT lf;
        char face[LF_FACESIZE];
        int size = GetProfileInt("desktop", "IconTitleSize", 8);
        fmemset(&lf, 0, sizeof lf);
        GetProfileString("desktop", "IconTitleFaceName", "MS Sans Serif", face, sizeof face);
        lstr_cpyn(lf.lfFaceName, face, LF_FACESIZE);
        lf.lfHeight = -(size * GetDeviceCaps(hdcScreen, LOGPIXELSY) / 72);
        lf.lfWeight = FW_NORMAL;
        hfontIconTitle = CreateFontIndirect(&lf);
    }
    return hfontIconTitle;
}

/* An icon title is a window of class #32772 in the minimised window's
   parent, owned by it, just under the icon, sized to its text (word
   wrapped to the icon spacing); it shows the owner's text, white on the
   active caption colour when the owner is active, else black on the
   parent's background (measured on Windows 3.1; UNVERIFIED in detail). */
static void title_place(HWND t, PWND w)
{
    HDC hdc = hdcScreen;
    RECT r;
    char text[80];
    HFONT old;
    PWND p = PW(w->hwndParent);
    GetWindowText(HW(w), text, sizeof text);
    old = SelectObject(hdc, icon_font());
    SetRect(&r, 0, 0, sysmet[SM_CXICONSPACING] - 2, 0);
    DrawText(hdc, text, -1, &r, DT_CALCRECT | DT_CENTER | DT_WORDBREAK | DT_NOPREFIX);
    SelectObject(hdc, old);
    {
        /* the text's width plus 4, centred under the icon (Windows 3.1:
           "Accessories", 57 pixels, under the icon at 93-129 is a title
           window at 81-142, 13 high: SNAP's window dump) */
        int cx = r.right - r.left + 4;
        int x = (w->rcWindow.left + w->rcWindow.right) / 2 - cx / 2;
        int y = w->rcWindow.bottom;
        if (w->style & WS_CHILD) {
            x -= p->rcClient.left;
            y -= p->rcClient.top;
        }
        set_window_pos(t, HW(w), x, y, cx, r.bottom - r.top, SWP_NOACTIVATE);
    }
}

HWND make_icon_title(HWND h)
{
    PWND w = valid_wnd(h);
    HWND t;
    if (!w)
        return 0;
    t = create_window(WS_EX_NOPARENTNOTIFY, MAKEINTRESOURCE(32772), NULL,
                      ((w->style & WS_CHILD) ? WS_CHILD : WS_POPUP) | WS_CLIPSIBLINGS,
                      0, 0, 0, 0, (w->style & WS_CHILD) ? w->hwndParent : h, 0,
                      hInstUser, NULL);
    if (!t)
        return 0;
    PW(t)->hwndOwner = h;
    title_place(t, PW(h));
    set_window_pos(t, h, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_SHOWWINDOW | SWP_NOACTIVATE);
    return t;
}

void move_icon_title(HWND t, HWND h)
{
    if (valid_wnd(t) && valid_wnd(h))
        title_place(t, PW(h));
}

LRESULT WINAPI __loadds IconTitleWndProc(HWND h, UINT msg, WPARAM wp, LPARAM lp)
{
    PWND w = valid_wnd(h);
    switch (msg) {
    case WM_PAINT: {
        PAINTSTRUCT ps;
        HDC hdc = BeginPaint(h, &ps);
        PWND o = valid_wnd(w->hwndOwner);
        if (o) {
            char text[80];
            RECT r;
            BOOL active = (o->flags & WF_ACTIVEFRAME) != 0;
            HFONT old = SelectObject(hdc, icon_font());
            GetClientRect(h, &r);
            FillRect(hdc, &r, sysbrush[active ? COLOR_ACTIVECAPTION :
                                        (w->style & WS_CHILD) ? COLOR_APPWORKSPACE : COLOR_BACKGROUND]);
            GetWindowText(w->hwndOwner, text, sizeof text);
            SetBkMode(hdc, TRANSPARENT);
            SetTextColor(hdc, syscolor[active ? COLOR_CAPTIONTEXT : COLOR_WINDOWTEXT]);
            /* the text one pixel in from the left (Program Manager's
               icon titles, pixel-compared), centred in one pixel more than
               the text's width: Paintbrush's second line "(Untitled)",
               an odd number of pixels narrower, lands half a pixel right
               (3.1, pixel-compared) */
            r.left += 1;
            r.right -= 2;
            DrawText(hdc, text, -1, &r, DT_CENTER | DT_WORDBREAK | DT_NOPREFIX);
            SelectObject(hdc, old);
        }
        EndPaint(h, &ps);
        return 0;
    }
    case WM_ERASEBKGND:
        return 1;
    case WM_NCHITTEST:
        return HTTRANSPARENT;
    }
    return DefWindowProc(h, msg, wp, lp);
}
