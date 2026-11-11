/*
 * pos.c - window position, size, Z order and visibility.
 *
 * SetWindowPos is the one place windows move (3.1 SDK): it asks the window
 * (WM_WINDOWPOSCHANGING), recomputes the client area (WM_NCCALCSIZE),
 * relinks the Z order, and tells it (WM_WINDOWPOSCHANGED, which
 * DefWindowProc turns into WM_MOVE and WM_SIZE). What it uncovers or
 * changes on screen is invalidated and repainted by the windows concerned
 * (w16 does not copy screen bits as Windows does; the result is the same
 * when windows paint the same: UNVERIFIED for all cases).
 */
#include "user.h"

void refresh_dcs(void);
void byte_align(PWND w);
void pointer_changed(const RECT *r);
void follow_icon_title(HWND h);
HWND next_to_activate(HWND h);
void invalidate_screen(const RECT *rc);

void calc_client(PWND w)
{
    NCCALCSIZE_PARAMS p;
    RECT r = w->rcWindow;
    PWND parent = PW(w->hwndParent);
    int px = 0, py = 0;
    /* WM_NCCALCSIZE works in the parent's client coordinates */
    if (parent && (w->style & WS_CHILD)) {
        px = parent->rcClient.left;
        py = parent->rcClient.top;
    }
    OffsetRect(&r, -px, -py);
    p.rgrc[0] = r;
    send_message((HWND)(WORD)w, WM_NCCALCSIZE, FALSE, (LPARAM)(NCCALCSIZE_PARAMS __far *)&p);
    if (!valid_wnd((HWND)(WORD)w))
        return;
    w->rcClient = p.rgrc[0];
    OffsetRect(&w->rcClient, px, py);
    if (w->rcClient.right < w->rcClient.left)
        w->rcClient.right = w->rcClient.left;
    if (w->rcClient.bottom < w->rcClient.top)
        w->rcClient.bottom = w->rcClient.top;
}

static void move_children(PWND w, int dx, int dy)
{
    HWND c;
    for (c = w->hwndChild; c; c = PW(c)->hwndNext) {
        PWND cw = PW(c);
        OffsetRect(&cw->rcWindow, dx, dy);
        OffsetRect(&cw->rcClient, dx, dy);
        if (cw->hrgnUpdate)
            OffsetRgn(cw->hrgnUpdate, dx, dy);
        move_children(cw, dx, dy);
    }
}

BOOL set_window_pos(HWND h, HWND after, int x, int y, int cx, int cy, UINT flags)
{
    PWND w = valid_wnd(h), parent;
    WINDOWPOS wp;
    RECT oldwin, oldcli;
    BOOL wasvis, nowvis, changed;
    int px = 0, py = 0;
    if (!w || h == hwndDesktop)
        return FALSE;
    parent = PW(w->hwndParent);
    if (parent && (w->style & WS_CHILD)) {
        px = parent->rcClient.left;
        py = parent->rcClient.top;
    }
    if ((flags & SWP_SHOWWINDOW) && (w->style & WS_VISIBLE))
        flags &= ~SWP_SHOWWINDOW;
    if ((flags & SWP_HIDEWINDOW) && !(w->style & WS_VISIBLE))
        flags &= ~SWP_HIDEWINDOW;
    if (flags & SWP_NOMOVE) {
        x = w->rcWindow.left - px;
        y = w->rcWindow.top - py;
    }
    if (flags & SWP_NOSIZE) {
        cx = w->rcWindow.right - w->rcWindow.left;
        cy = w->rcWindow.bottom - w->rcWindow.top;
    }
    wp.hwnd = h;
    wp.hwndInsertAfter = after;
    wp.x = x;
    wp.y = y;
    wp.cx = cx;
    wp.cy = cy;
    wp.flags = flags;
    if (!(flags & SWP_NOSENDCHANGING) || 1) {
        if ((flags & (SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_SHOWWINDOW | SWP_HIDEWINDOW)) !=
            (SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER) || !(flags & SWP_NOACTIVATE))
            send_message(h, WM_WINDOWPOSCHANGING, 0, (LPARAM)(WINDOWPOS __far *)&wp);
        if (!(w = valid_wnd(h)))
            return FALSE;
    }
    x = wp.x, y = wp.y, cx = wp.cx, cy = wp.cy, flags = wp.flags;
    after = wp.hwndInsertAfter;
    if (cx < 0) cx = 0;
    if (cy < 0) cy = 0;
    oldwin = w->rcWindow;
    oldcli = w->rcClient;
    wasvis = is_visible(w);

    /* Z order */
    if (!(flags & SWP_NOZORDER) && after != h && w->hwndParent) {
        if (!(after == HWND_TOP && PW(w->hwndParent)->hwndChild == h)) {
            unlink_window(w);
            link_window(w, after == (HWND)-1 ? HWND_TOP : (after == (HWND)-2 ? HWND_TOP : after));
            changed = TRUE;
        }
    }
    /* place */
    if (!(flags & SWP_NOMOVE) || !(flags & SWP_NOSIZE)) {
        SetRect(&w->rcWindow, x + px, y + py, x + px + cx, y + py + cy);
        calc_client(w);
        if (!(w = valid_wnd(h)))
            return FALSE;
        byte_align(w);
        if (w->rcClient.left != oldcli.left || w->rcClient.top != oldcli.top)
            move_children(w, w->rcClient.left - oldcli.left, w->rcClient.top - oldcli.top);
        if (w->hrgnUpdate)
            OffsetRgn(w->hrgnUpdate, w->rcWindow.left - oldwin.left, w->rcWindow.top - oldwin.top);
    } else if (flags & SWP_FRAMECHANGED) {
        calc_client(w);
        if (!(w = valid_wnd(h)))
            return FALSE;
    }
    if (flags & SWP_SHOWWINDOW)
        w->style |= WS_VISIBLE;
    if (flags & SWP_HIDEWINDOW) {
        w->style &= ~WS_VISIBLE;
        if (h == hwndFocus || IsChild(h, hwndFocus))
            set_focus(0);
        /* the active window hidden, by ShowWindow or SetWindowPos: the
           next visible, enabled top-level window becomes active, or none.
           Program Manager hides its Run dialog with SetWindowPos before
           WinExec (US1-style trace on 3.1); the dialog stayed "active"
           while hidden, so when it ended it gave the activation back to
           Program Manager over the program just started */
        if (h == hwndActive) {
            HWND t = w->hwndOwner;
            /* its owner first, if it can take it, else the next one */
            if (!t || !valid_wnd(t) || !(PW(t)->style & WS_VISIBLE) || (PW(t)->style & WS_DISABLED))
                t = next_to_activate(h);
            activate_window(t, FALSE);
            if (!(w = valid_wnd(h)))
                return FALSE;
        }
    }
    nowvis = is_visible(w);
    changed = !EqualRect(&oldwin, &w->rcWindow) || wasvis != nowvis ||
              !(flags & SWP_NOZORDER) || (flags & SWP_FRAMECHANGED);
    refresh_dcs();
    /* (not for popup menus: a menu opened under the pointer from the
       keyboard keeps its first item selected) */
    if (changed && (wasvis || nowvis) && GetClassWord(h, GCW_ATOM) != 32768) {
        pointer_changed(&oldwin);
        if (!EqualRect(&oldwin, &w->rcWindow))
            pointer_changed(&w->rcWindow);
    }
    if (changed && !(flags & SWP_NOREDRAW)) {
        /* what was under the old place, and the window in its new one */
        if (wasvis)
            invalidate_screen(&oldwin);
        if (nowvis)
            invalidate_screen(&w->rcWindow);
    }
    if ((flags & SWP_SHOWWINDOW) && (w->flags & WF_SENDSIZE)) {
        /* the WM_SIZE and WM_MOVE CreateWindow held back */
        PWND pw = (w->style & WS_CHILD) ? PW(w->hwndParent) : (PWND)0;
        w->flags &= ~WF_SENDSIZE;
        send_message(h, WM_SIZE, (w->style & WS_MINIMIZE) ? SIZE_MINIMIZED :
                     (w->style & WS_MAXIMIZE) ? SIZE_MAXIMIZED : SIZE_RESTORED,
                     MAKELONG(w->rcClient.right - w->rcClient.left,
                              w->rcClient.bottom - w->rcClient.top));
        if (!(w = valid_wnd(h)))
            return FALSE;
        send_message(h, WM_MOVE, 0, MAKELONG(w->rcClient.left - (pw ? pw->rcClient.left : 0),
                                             w->rcClient.top - (pw ? pw->rcClient.top : 0)));
        if (!(w = valid_wnd(h)))
            return FALSE;
    }
    if ((flags & SWP_SHOWWINDOW) && !(flags & SWP_NOACTIVATE) && !(w->style & WS_CHILD))
        activate_window(h, FALSE);
    /* a child shown without SWP_NOACTIVATE: WM_CHILDACTIVATE, which
       DefMDIChildProc turns into the MDI child's activation (3.1, File
       Manager's directory window: WM_SHOWWINDOW, WM_WINDOWPOSCHANGING,
       WM_CHILDACTIVATE as it is shown). UNVERIFIED: on moves and sizes
       without SWP_NOACTIVATE too, as the SDK says */
    if ((flags & SWP_SHOWWINDOW) && !(flags & SWP_NOACTIVATE) && (w->style & WS_CHILD))
        send_message(h, WM_CHILDACTIVATE, 0, 0);
    if ((w = valid_wnd(h)) != 0 && (w->style & WS_MINIMIZE) && !(flags & SWP_NOMOVE))
        follow_icon_title(h);
    if (!(w = valid_wnd(h)))
        return TRUE;
    wp.x = w->rcWindow.left - px;
    wp.y = w->rcWindow.top - py;
    wp.cx = w->rcWindow.right - w->rcWindow.left;
    wp.cy = w->rcWindow.bottom - w->rcWindow.top;
    wp.flags = flags;
    if (!EqualRect(&oldwin, &w->rcWindow) || (flags & (SWP_SHOWWINDOW | SWP_HIDEWINDOW | SWP_FRAMECHANGED)) ||
        !(flags & SWP_NOZORDER)) {
        if (EqualRect(&oldwin, &w->rcWindow))
            wp.flags |= SWP_NOMOVE | SWP_NOSIZE;
        else {
            if (oldwin.left == w->rcWindow.left && oldwin.top == w->rcWindow.top)
                wp.flags |= SWP_NOMOVE;
            if (oldwin.right - oldwin.left == wp.cx && oldwin.bottom - oldwin.top == wp.cy)
                wp.flags |= SWP_NOSIZE;
        }
        send_message(h, WM_WINDOWPOSCHANGED, 0, (LPARAM)(WINDOWPOS __far *)&wp);
    }
    return TRUE;
}

BOOL WINAPI SetWindowPos(HWND h, HWND after, int x, int y, int cx, int cy, UINT flags)
{
    return set_window_pos(h, after, x, y, cx, cy, flags);
}

BOOL WINAPI MoveWindow(HWND h, int x, int y, int cx, int cy, BOOL repaint)
{
    return set_window_pos(h, 0, x, y, cx, cy, SWP_NOZORDER | SWP_NOACTIVATE | (repaint ? 0 : SWP_NOREDRAW));
}

BOOL WINAPI BringWindowToTop(HWND h)
{
    PWND w = valid_wnd(h);
    if (w && !(w->style & WS_CHILD))
        activate_window(h, FALSE);
    else
        set_window_pos(h, HWND_TOP, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
    return w != 0;
}

/* minimised windows: icons in a row along the bottom of the parent
   (UNVERIFIED: Windows 3.1's arrangement in detail) */
static void icon_place(PWND w, POINT *pt)
{
    PWND parent = PW(w->hwndParent);
    RECT pr;
    int cx = sysmet[SM_CXICONSPACING], cy = sysmet[SM_CYICONSPACING];
    int n = 0;
    HWND s;
    pr = (w->style & WS_CHILD) ? parent->rcClient : PW(hwndDesktop)->rcWindow;
    for (s = parent->hwndChild; s; s = PW(s)->hwndNext)
        if (s != (HWND)(WORD)w && (PW(s)->style & WS_MINIMIZE))
            n++;
    pt->x = (n % ((pr.right - pr.left) / cx)) * cx + (cx - sysmet[SM_CXICON]) / 2;
    pt->y = (pr.bottom - pr.top) - cy * (1 + n / ((pr.right - pr.left) / cx));
    /* on the desktop the icon's window starts a whole spacing above the
       bottom (US4: 408 on a 480-line screen, its title at 444) */
    if (w->style & WS_CHILD)
        pt->y += (cy - sysmet[SM_CYICON]) / 2 - 2 + 6;
}

typedef struct PLACE { RECT normal; POINT icon; WORD hasicon; HWND title; } PLACE;
HWND make_icon_title(HWND h);
void move_icon_title(HWND t, HWND h);
HWND next_to_activate(HWND h);
#define PLACEPROP MAKEINTATOM(0x7E16)   /* w16's private property */

static PLACE *place_of(HWND h, BOOL create)
{
    PLACE *p = (PLACE *)MAKELP(get_ds(), GetProp(h, PLACEPROP));
    if (FP_OFF(p))
        return p;
    if (!create)
        return 0;
    {
        HLOCAL l = LocalAlloc(LPTR, sizeof(PLACE));
        if (!l)
            return 0;
        SetProp(h, PLACEPROP, (HANDLE)l);
        p = (PLACE *)MAKELP(get_ds(), l);
        p->normal = PW(h)->rcWindow;
        if (PW(h)->style & WS_CHILD)
            OffsetRect(&p->normal, -PW(PW(h)->hwndParent)->rcClient.left,
                       -PW(PW(h)->hwndParent)->rcClient.top);
        return p;
    }
}

BOOL WINAPI ShowWindow(HWND h, int cmd)
{
    PWND w = valid_wnd(h);
    BOOL wasvis;
    UINT f = SWP_NOZORDER | SWP_NOMOVE | SWP_NOSIZE;
    PLACE *pl;
    if (!w)
        return FALSE;
    wasvis = (w->style & WS_VISIBLE) != 0;
    switch (cmd) {
    case SW_HIDE:
        if (!wasvis)
            return FALSE;
        send_message(h, WM_SHOWWINDOW, FALSE, 0);
        set_window_pos(h, 0, 0, 0, 0, 0, f | SWP_HIDEWINDOW | SWP_NOACTIVATE);
        return wasvis;
    case SW_SHOWMINIMIZED:
    case SW_MINIMIZE:
    case SW_SHOWMINNOACTIVE:
        if (!(w->style & WS_MINIMIZE)) {
            POINT pt;
            pl = place_of(h, TRUE);
            if (pl && !(w->style & WS_MAXIMIZE)) {
                pl->normal = w->rcWindow;
                if (w->style & WS_CHILD)
                    OffsetRect(&pl->normal, -PW(w->hwndParent)->rcClient.left,
                               -PW(w->hwndParent)->rcClient.top);
            }
            w->style &= ~WS_MAXIMIZE;
            w->style |= WS_MINIMIZE;
            if (pl && pl->hasicon)
                pt = pl->icon;
            else
                icon_place(w, &pt);
            if (!wasvis)
                send_message(h, WM_SHOWWINDOW, TRUE, 0);
            set_window_pos(h, 0, pt.x, pt.y, sysmet[SM_CXICON] + 4, sysmet[SM_CYICON] + 4,
                           SWP_NOZORDER | SWP_SHOWWINDOW | SWP_FRAMECHANGED |
                           (cmd == SW_MINIMIZE || cmd == SW_SHOWMINNOACTIVE ? SWP_NOACTIVATE : 0));
            if ((w = valid_wnd(h)) != 0) {
                /* with the icon's client size, as from WM_WINDOWPOSCHANGED
                   (it was 0 x 0: w16's Clock sized its icon's digits to
                   it, and they came out huge and cut off) */
                send_message(h, WM_SIZE, SIZE_MINIMIZED,
                             MAKELONG(w->rcClient.right - w->rcClient.left,
                                      w->rcClient.bottom - w->rcClient.top));
                if (pl && !pl->title && (w = valid_wnd(h)) != 0)
                    pl->title = make_icon_title(h);
            }
            /* SW_MINIMIZE (Alt+Space N, the minimise box): the next
               top-level window becomes active (3.1 SDK; on 3.1 Program
               Manager is active again after Notepad is minimised - w16
               left Notepad's icon active, and the keys went to it). The
               minimised window goes to the bottom of the Z order: with
               Notepad, Calculator, Clock, Character Map and Paintbrush
               minimised in that order, minimising Program Manager makes
               Notepad's icon active, and Alt+Esc then goes Calculator,
               Clock (observed on 3.1) */
            if (cmd == SW_MINIMIZE && h == hwndActive && (w = valid_wnd(h)) != 0 &&
                !(w->style & WS_CHILD)) {
                HWND t;
                set_window_pos(h, HWND_BOTTOM, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
                t = next_to_activate(h);
                if (t)
                    activate_window(t, FALSE);
            }
        } else if (!wasvis) {
            send_message(h, WM_SHOWWINDOW, TRUE, 0);
            set_window_pos(h, 0, 0, 0, 0, 0, f | SWP_SHOWWINDOW |
                           (cmd == SW_SHOWMINNOACTIVE ? SWP_NOACTIVATE : 0));
        }
        return wasvis;
    case SW_SHOWMAXIMIZED:
        if (!(w->style & WS_MAXIMIZE)) {
            MINMAXINFO mm;
            int cxf = (w->style & WS_THICKFRAME) ? sysmet[SM_CXFRAME] : sysmet[SM_CXBORDER];
            int cyf = (w->style & WS_THICKFRAME) ? sysmet[SM_CYFRAME] : sysmet[SM_CYBORDER];
            RECT pr;
            pl = place_of(h, TRUE);
            if (pl && !(w->style & WS_MINIMIZE)) {
                pl->normal = w->rcWindow;
                if (w->style & WS_CHILD)
                    OffsetRect(&pl->normal, -PW(w->hwndParent)->rcClient.left,
                               -PW(w->hwndParent)->rcClient.top);
            }
            if (w->style & WS_CHILD) {
                PWND p = PW(w->hwndParent);
                SetRect(&pr, 0, 0, p->rcClient.right - p->rcClient.left,
                        p->rcClient.bottom - p->rcClient.top);
            } else
                SetRect(&pr, 0, 0, sysmet[SM_CXSCREEN], sysmet[SM_CYSCREEN]);
            mm.ptMaxSize.x = pr.right + 2 * cxf;
            mm.ptMaxSize.y = pr.bottom + 2 * cyf;
            mm.ptMaxPosition.x = -cxf;
            mm.ptMaxPosition.y = -cyf;
            mm.ptMinTrackSize.x = sysmet[SM_CXMINTRACK];
            mm.ptMinTrackSize.y = sysmet[SM_CYMINTRACK];
            mm.ptMaxTrackSize = mm.ptMaxSize;
            send_message(h, WM_GETMINMAXINFO, 0, (LPARAM)(MINMAXINFO __far *)&mm);
            if (!(w = valid_wnd(h)))
                return wasvis;
            w->style &= ~WS_MINIMIZE;
            w->style |= WS_MAXIMIZE;
            if (!wasvis)
                send_message(h, WM_SHOWWINDOW, TRUE, 0);
            set_window_pos(h, 0, mm.ptMaxPosition.x, mm.ptMaxPosition.y,
                           mm.ptMaxSize.x, mm.ptMaxSize.y,
                           SWP_NOZORDER | SWP_SHOWWINDOW | SWP_FRAMECHANGED);
            if ((w = valid_wnd(h)) != 0)
                send_message(h, WM_SIZE, SIZE_MAXIMIZED,
                             MAKELONG(w->rcClient.right - w->rcClient.left,
                                      w->rcClient.bottom - w->rcClient.top));
        } else if (!wasvis) {
            send_message(h, WM_SHOWWINDOW, TRUE, 0);
            set_window_pos(h, 0, 0, 0, 0, 0, f | SWP_SHOWWINDOW);
        }
        return wasvis;
    case SW_SHOWNORMAL:
    case SW_RESTORE:
        if (w->style & (WS_MINIMIZE | WS_MAXIMIZE)) {
            RECT r;
            pl = place_of(h, FALSE);
            if (w->style & WS_MINIMIZE) {
                if (!pl)
                    pl = place_of(h, TRUE);
                if (pl) {
                    pl->icon.x = w->rcWindow.left;
                    pl->icon.y = w->rcWindow.top;
                    if (w->style & WS_CHILD) {
                        pl->icon.x -= PW(w->hwndParent)->rcClient.left;
                        pl->icon.y -= PW(w->hwndParent)->rcClient.top;
                    }
                    pl->hasicon = 1;
                }
            }
            r = pl ? pl->normal : w->rcWindow;
            if (pl && pl->title) {
                destroy_window(pl->title);
                pl->title = 0;
            }
            w->style &= ~(WS_MINIMIZE | WS_MAXIMIZE);
            if (!wasvis)
                send_message(h, WM_SHOWWINDOW, TRUE, 0);
            set_window_pos(h, 0, r.left, r.top, r.right - r.left, r.bottom - r.top,
                           SWP_NOZORDER | SWP_SHOWWINDOW | SWP_FRAMECHANGED);
            if ((w = valid_wnd(h)) != 0)
                send_message(h, WM_SIZE, SIZE_RESTORED,
                             MAKELONG(w->rcClient.right - w->rcClient.left,
                                      w->rcClient.bottom - w->rcClient.top));
            /* restored: activated and on top (3.1 SDK: SW_RESTORE and
               SW_SHOWNORMAL activate; a Program Manager group restored
               from its icon comes up in front, active). A child gets
               WM_CHILDACTIVATE, which an MDI child turns into its
               activation; w16 left it where it was in the Z order */
            if ((w = valid_wnd(h)) != 0) {
                if (!(w->style & WS_CHILD))
                    activate_window(h, FALSE);
                else {
                    set_window_pos(h, HWND_TOP, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
                    send_message(h, WM_CHILDACTIVATE, 0, 0);
                }
            }
            return wasvis;
        }
        /* fall through */
    default:            /* SW_SHOW, SW_SHOWNA, SW_SHOWNOACTIVATE */
        if (wasvis) {
            if (cmd == SW_SHOWNORMAL || cmd == SW_SHOW || cmd == SW_RESTORE)
                if (!(w->style & WS_CHILD) && h != hwndActive)
                    activate_window(h, FALSE);
            return TRUE;
        }
        send_message(h, WM_SHOWWINDOW, TRUE, 0);
        if (!(w = valid_wnd(h)))
            return FALSE;
        set_window_pos(h, (cmd == SW_SHOWNA || cmd == SW_SHOWNOACTIVATE) ? 0 : HWND_TOP,
                       0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_SHOWWINDOW |
                       ((cmd == SW_SHOWNA || cmd == SW_SHOWNOACTIVATE) ? SWP_NOACTIVATE | SWP_NOZORDER : 0));
        return FALSE;
    }
}

BOOL WINAPI GetWindowPlacement(HWND h, WINDOWPLACEMENT __far *p)
{
    PWND w = valid_wnd(h);
    PLACE *pl;
    if (!w)
        return FALSE;
    pl = place_of(h, FALSE);
    p->length = sizeof(WINDOWPLACEMENT);
    p->flags = 0;
    p->showCmd = (w->style & WS_MINIMIZE) ? SW_SHOWMINIMIZED :
                 (w->style & WS_MAXIMIZE) ? SW_SHOWMAXIMIZED : SW_SHOWNORMAL;
    if (pl && pl->hasicon) {
        p->ptMinPosition = pl->icon;
    } else
        p->ptMinPosition.x = p->ptMinPosition.y = -1;
    p->ptMaxPosition.x = p->ptMaxPosition.y = -1;
    if (pl && (w->style & (WS_MINIMIZE | WS_MAXIMIZE)))
        p->rcNormalPosition = pl->normal;
    else {
        p->rcNormalPosition = w->rcWindow;
        if (w->style & WS_CHILD)
            OffsetRect(&p->rcNormalPosition, -PW(w->hwndParent)->rcClient.left,
                       -PW(w->hwndParent)->rcClient.top);
    }
    return TRUE;
}

BOOL WINAPI SetWindowPlacement(HWND h, const WINDOWPLACEMENT __far *p)
{
    PWND w = valid_wnd(h);
    PLACE *pl;
    if (!w)
        return FALSE;
    pl = place_of(h, TRUE);
    if (!pl)
        return FALSE;
    pl->normal = p->rcNormalPosition;
    if (p->ptMinPosition.x != -1 || p->ptMinPosition.y != -1) {
        pl->icon = p->ptMinPosition;
        pl->hasicon = 1;
    }
    if (!(w->style & (WS_MINIMIZE | WS_MAXIMIZE)))
        set_window_pos(h, 0, pl->normal.left, pl->normal.top,
                       pl->normal.right - pl->normal.left, pl->normal.bottom - pl->normal.top,
                       SWP_NOZORDER | SWP_NOACTIVATE);
    ShowWindow(h, p->showCmd);
    return TRUE;
}

/* the window to activate when h stops being the active one: the first
   visible, enabled top-level window in Z order other than h, not an icon
   title (#32772, a popup owned by a minimised window), one that is not
   minimised first. UNVERIFIED: 3.1's order of preference */
HWND next_to_activate(HWND h)
{
    HWND t, icon = 0;
    for (t = PW(hwndDesktop)->hwndChild; t; t = PW(t)->hwndNext) {
        PWND tw = PW(t);
        if (t == h || !(tw->style & WS_VISIBLE) || (tw->style & (WS_DISABLED | WS_CHILD)))
            continue;
        if (tw->hwndOwner && valid_wnd(tw->hwndOwner) && (PW(tw->hwndOwner)->style & WS_MINIMIZE))
            continue;
        if (!(tw->style & WS_MINIMIZE))
            return t;
        if (!icon)
            icon = t;
    }
    return icon;
}

/* a window going away: its placement record and icon title */
void free_place(HWND h)
{
    PLACE *p = place_of(h, FALSE);
    HLOCAL l;
    if (!p)
        return;
    if (p->title && valid_wnd(p->title))
        destroy_window(p->title);
    l = (HLOCAL)RemoveProp(h, PLACEPROP);
    if (l)
        LocalFree(l);
}

/* a minimised window activated, deactivated or renamed: its title
   repainted whole */
void redraw_icon_title(HWND h)
{
    PLACE *p = place_of(h, FALSE);
    if (p && p->title && valid_wnd(p->title))
        RedrawWindow(p->title, NULL, 0, RDW_INVALIDATE | RDW_ERASE | RDW_UPDATENOW);
}

/* a minimised window moved: its title follows */
void follow_icon_title(HWND h)
{
    PLACE *p = place_of(h, FALSE);
    if (p && p->title)
        move_icon_title(p->title, h);
}

void WINAPI CloseWindow(HWND h)
{
    ShowWindow(h, SW_MINIMIZE);
}

BOOL WINAPI OpenIcon(HWND h)
{
    return ShowWindow(h, SW_SHOWNORMAL);
}

void WINAPI ShowOwnedPopups(HWND h, BOOL show)
{
    HWND t;
    for (t = PW(hwndDesktop)->hwndChild; t; t = PW(t)->hwndNext)
        if (PW(t)->hwndOwner == h)
            ShowWindow(t, show ? SW_SHOWNA : SW_HIDE);
}
