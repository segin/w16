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
#define MDI_MAXED     8
#define MDI_FRAMETEXT 10        /* local copy of the frame's own title */
#define MDI_SCROLL    12        /* the WS_VSCROLL/WS_HSCROLL it was made with */

static HWND client_of(HWND child)
{
    PWND w = valid_wnd(child);
    return w ? w->hwndParent : 0;
}

static HWND active_child(HWND client)
{
    return (HWND)GetWindowWord(client, MDI_ACTIVE);
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
    if (child && valid_wnd(child)) {
        set_window_pos(child, HWND_TOP, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
        send_message(child, WM_NCACTIVATE, TRUE, 0);
        send_message(child, WM_MDIACTIVATE, TRUE, MAKELONG(child, old));
        if (top_level(client) == hwndActive)
            set_focus(child);
    }
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
    if (x == (int)CW_USEDEFAULT) {
        /* cascade: UNVERIFIED step (caption height plus frame) */
        int step = sysmet[SM_CYCAPTION] + sysmet[SM_CYFRAME] - 1;
        x = y = (n % 8) * step;
    }
    if (cx == (int)CW_USEDEFAULT) {
        cx = (cw->rcClient.right - cw->rcClient.left) * 3 / 4;
        cy = (cw->rcClient.bottom - cw->rcClient.top) * 3 / 4;
    }
    style &= ~(WS_VISIBLE | WS_MAXIMIZE | WS_MINIMIZE);
    h = create_window(0, mc->szClass, mc->szTitle, style, x, y, cx, cy, client, (HMENU)id,
                      mc->hOwner, (void __far *)mc);
    if (!h)
        return 0;
    SetWindowWord(client, MDI_COUNT, n + 1);
    if (mc->style & WS_MINIMIZE)
        ShowWindow(h, SW_SHOWMINNOACTIVE);
    else if (mc->style & WS_MAXIMIZE)
        ShowWindow(h, SW_SHOWMAXIMIZED);
    else if (vis)
        ShowWindow(h, SW_SHOWNOACTIVATE);
    child_activate(client, h);
    return h;
}

static void mdi_destroy(HWND client, HWND child)
{
    if (active_child(client) == child) {
        HWND next = 0, s;
        for (s = GetWindow(client, GW_CHILD); s; s = GetWindow(s, GW_HWNDNEXT))
            if (s != child && GetWindow(s, GW_OWNER) == 0 && IsWindowVisible(s) &&
                GetClassWord(s, -32) != 32772) {
                next = s;
                break;
            }
        SetWindowWord(client, MDI_ACTIVE, 0);
        if (next)
            child_activate(client, next);
    }
    DestroyWindow(child);
    SetWindowWord(client, MDI_COUNT, GetWindowWord(client, MDI_COUNT) - 1);
}

LRESULT WINAPI MDIClientWndProc(HWND h, UINT msg, WPARAM wp, LPARAM lp)
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
        return MAKELONG(a, a && IsZoomed(a));
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
        if (lp) {           /* previous: the next one after it in Z order */
            s = GetWindow(cur, GW_HWNDNEXT);
            while (s && (GetWindow(s, GW_OWNER) || GetClassWord(s, -32) == 32772))
                s = GetWindow(s, GW_HWNDNEXT);
            if (s)
                child_activate(h, s);
        } else {            /* the bottom one */
            for (s = GetWindow(h, GW_CHILD); s; s = GetWindow(s, GW_HWNDNEXT))
                if (!GetWindow(s, GW_OWNER) && GetClassWord(s, -32) != 32772)
                    last = s;
            if (last && last != cur) {
                child_activate(h, last);
                set_window_pos(cur, HWND_BOTTOM, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE |
                               SWP_NOACTIVATE);
            }
        }
        return 0;
    }
    case WM_MDISETMENU: {
        HWND frame = GetParent(h);
        HMENU old = GetMenu(frame);
        if (!wp && lp)
            SetWindowWord(h, MDI_WINMENU, HIWORD(lp));
        else {
            if (LOWORD(lp))
                SetWindowWord(h, MDI_WINMENU, LOWORD(lp));
            if (LOWORD(wp) == 0 && HIWORD(lp) == 0)
                ;
            SetMenu(frame, (HMENU)LOWORD(lp));
        }
        return (LONG)(WORD)old;
    }
    case WM_MDICASCADE:
    case WM_MDITILE:
        return 0;               /* UNVERIFIED: not yet */
    case WM_MDIICONARRANGE:
        ArrangeIconicWindows(h);
        return 0;
    case WM_SETFOCUS: {
        HWND a = active_child(h);
        if (a && valid_wnd(a) && !IsIconic(a))
            set_focus(a);
        return 0;
    }
    case WM_SIZE: {
        HWND a = active_child(h);
        if (a && IsZoomed(a)) {
            RECT r;
            GetClientRect(h, &r);
            MoveWindow(a, -sysmet[SM_CXFRAME], -sysmet[SM_CYFRAME] - sysmet[SM_CYCAPTION] +
                       sysmet[SM_CYBORDER], r.right + 2 * sysmet[SM_CXFRAME],
                       r.bottom + 2 * sysmet[SM_CYFRAME] + sysmet[SM_CYCAPTION] - sysmet[SM_CYBORDER],
                       TRUE);
        }
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

void WINAPI CalcChildScroll(HWND h, WORD sb)
{
    (void)h, (void)sb;          /* UNVERIFIED: no scroll bars yet */
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
    GetWindowText((HWND)(WORD)w, text, sizeof text);
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
        set_window_pos(t, (HWND)(WORD)w, x, y, cx, r.bottom - r.top, SWP_NOACTIVATE);
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

LRESULT WINAPI IconTitleWndProc(HWND h, UINT msg, WPARAM wp, LPARAM lp)
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
               icon titles, pixel-compared) */
            r.left += 1;
            r.right -= 3;
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
