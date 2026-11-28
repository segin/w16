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
HWND icon_title_of(HWND h);
void byte_align(PWND w, BOOL made);
void pointer_changed(const RECT *r);
void follow_icon_title(HWND h);
static void icon_title_above(HWND h);
HWND next_to_activate(HWND h);
void invalidate_screen(const RECT *rc);
void invalidate_screen_except(const RECT *rc, const RECT *except);
void invalidate_window_screen_rgn(HWND h, HRGN rgn);
void savebits_show(HWND h);
BOOL savebits_restore(HWND h, const RECT *oldwin);
void savebits_forget(HWND h);

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
    send_message(HW(w), WM_NCCALCSIZE, FALSE, (LPARAM)(NCCALCSIZE_PARAMS __far *)&p);
    if (!valid_wnd(HW(w)))
        return;
    w->rcClient = p.rgrc[0];
    OffsetRect(&w->rcClient, px, py);
    if (w->rcClient.right < w->rcClient.left)
        w->rcClient.right = w->rcClient.left;
    if (w->rcClient.bottom < w->rcClient.top)
        w->rcClient.bottom = w->rcClient.top;
}

/* Owned top-level windows stay with their owner (TK75 "z ..." on 3.1):
   - one sent to HWND_BOTTOM goes just above its owner, and the owner
     with all its owned windows, in their order, to the bottom
     (StarWriter's floating Stylist, owned by the frame, is sent to the
     bottom while hidden and then shown: w16 left it under the frame);
   - a window brought to the top has the windows it owns put above it,
     in their order. */
static BOOL max_client_rule;     /* ShowWindow's maximise: WM_SIZE by the client size */

static void owner_order(PWND w, HWND after)
{
    PWND d = PW(hwndDesktop);
    HWND list[32], s, owner;
    int n = 0, i;
    if (after == HWND_BOTTOM && w->hwndOwner && valid_wnd(w->hwndOwner) &&
        PW(w->hwndOwner)->hwndParent == hwndDesktop) {
        owner = w->hwndOwner;
        /* just above its owner */
        unlink_window(w);
        for (s = d->hwndChild; s && PW(s)->hwndNext != owner; s = PW(s)->hwndNext)
            ;
        link_window(w, d->hwndChild == owner ? HWND_TOP : s);
        /* the group, in its order, to the bottom */
        for (s = d->hwndChild; s && n < 32; s = PW(s)->hwndNext)
            if (s == owner || PW(s)->hwndOwner == owner)
                list[n++] = s;
        for (i = 0; i < n; i++) {
            unlink_window(PW(list[i]));
            link_window(PW(list[i]), HWND_BOTTOM);
        }
    } else if (after == HWND_TOP || after == (HWND)-1 || after == (HWND)-2) {
        HWND me = HW(w);
        owner = w->hwndOwner && valid_wnd(w->hwndOwner) &&
                PW(w->hwndOwner)->hwndParent == hwndDesktop ? w->hwndOwner : 0;
        /* an owned window: its owner and the owner's other windows come up
           with it, under it (TK75 "z p shown") */
        for (s = d->hwndChild; s && n < 32; s = PW(s)->hwndNext)
            if (s != me && (PW(s)->hwndOwner == me ||
                            (owner && (s == owner || PW(s)->hwndOwner == owner))))
                list[n++] = s;
        for (i = n - 1; i >= 0; i--) {
            unlink_window(PW(list[i]));
            link_window(PW(list[i]), HWND_TOP);
        }
        if (owner) {
            unlink_window(w);
            link_window(w, HWND_TOP);
        }
    }
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

/* already where HWND_TOP puts it: first, or under topmost windows only */
static BOOL at_top(PWND w)
{
    HWND s, h = HW(w);
    for (s = PW(w->hwndParent)->hwndChild; s && s != h; s = PW(s)->hwndNext)
        if (!(PW(s)->exstyle & WS_EX_TOPMOST) || (w->exstyle & WS_EX_TOPMOST) ||
            w->hwndParent != hwndDesktop)
            return FALSE;
    return s == h;
}

/* A top-level window's group, the windows owner_order moves with it, in
   the order they end up in (3.1, US42: Alt+Tab to C's owned popup Cp
   sent Cp WM_WINDOWPOSCHANGING, then C with hwndInsertAfter Cp and
   SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE; their WM_WINDOWPOSCHANGED in
   the same order after the activation; C brought up, Cp first). GRP
   gets them, *K the window's own place; 0 when it moves alone */
#define NGRP 8
static int pos_group(PWND w, HWND after, HWND *grp, int *k)
{
    HWND me = HW(w), s, owner = 0;
    int n = 0;
    PWND d = PW(hwndDesktop);
    *k = 0;
    if (w->hwndOwner && valid_wnd(w->hwndOwner) && PW(w->hwndOwner)->hwndParent == hwndDesktop)
        owner = w->hwndOwner;
    if (owner) {
        /* up: it, its owner's other windows, the owner; down: the
           owner's other windows, it, the owner */
        if (after == HWND_TOP)
            grp[n++] = me;
        for (s = d->hwndChild; s && n < NGRP - 2; s = PW(s)->hwndNext)
            if (s != me && s != owner && PW(s)->hwndOwner == owner && ((PCLS)PW(s)->cls)->atom != 32772)
                grp[n++] = s;
        if (after == HWND_BOTTOM) {
            *k = n;
            grp[n++] = me;
        }
        grp[n++] = owner;
    } else if (after == HWND_TOP) {
        /* up: the windows it owns, then it */
        for (s = d->hwndChild; s && n < NGRP - 1; s = PW(s)->hwndNext)
            if (s != me && PW(s)->hwndOwner == me && ((PCLS)PW(s)->cls)->atom != 32772)
                grp[n++] = s;
        *k = n;
        grp[n++] = me;
    }
    if (n < 2) {
        *k = 0;
        return 0;
    }
    return n;
}

/* a group window's WM_WINDOWPOSCHANGING; the first one's place is the
   moved window's */
static void group_changing(HWND *grp, int i, HWND first_after)
{
    WINDOWPOS g;
    if (!valid_wnd(grp[i]))
        return;
    g.hwnd = grp[i];
    g.hwndInsertAfter = i ? grp[i - 1] : first_after;
    g.x = g.y = g.cx = g.cy = 0;
    g.flags = SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE;
    send_message(grp[i], WM_WINDOWPOSCHANGING, 0, (LPARAM)(WINDOWPOS __far *)&g);
}

/* S's place in Z order: how many windows are above it */
static HWND above_of(HWND s)
{
    HWND t;
    WORD n = 0;
    for (t = PW(hwndDesktop)->hwndChild; t && t != s; t = PW(t)->hwndNext)
        n++;
    return (HWND)n;
}

static void send_changed_msg(HWND h, WINDOWPOS *p);

/* a group window's WM_WINDOWPOSCHANGED, when its place changed */
static void group_changed(HWND *grp, HWND *gprev, int i)
{
    WINDOWPOS g;
    PWND gw = valid_wnd(grp[i]);
    if (!gw || above_of(grp[i]) == gprev[i])
        return;
    /* where it went in the Z order, the screen there drawn again (what it
       covers now, what it covered before) */
    if (is_visible(gw))
        invalidate_screen(&gw->rcWindow);
    g.hwnd = grp[i];
    g.hwndInsertAfter = i ? grp[i - 1] : 0;
    if (!i) {
        HWND t;
        for (t = PW(hwndDesktop)->hwndChild; t && t != grp[0] && PW(t)->hwndNext != grp[0];
             t = PW(t)->hwndNext)
            ;
        g.hwndInsertAfter = t == grp[0] ? 0 : t;
    }
    g.x = gw->rcWindow.left;
    g.y = gw->rcWindow.top;
    g.cx = gw->rcWindow.right - gw->rcWindow.left;
    g.cy = gw->rcWindow.bottom - gw->rcWindow.top;
    g.flags = SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE | 0x1800;
    send_changed_msg(grp[i], &g);
}

/* Alt+Esc's window going down (switch.c), and the WM_WINDOWPOSCHANGED
   messages held back meanwhile */
HWND pos_companion, pos_companion_after;
static int defer_changed, quiet_changed;
static struct { HWND h; WINDOWPOS wp; } deferred[NGRP + 1];
static int ndeferred;

static void send_changed_msg(HWND h, WINDOWPOS *p)
{
    if (quiet_changed)
        return;
    if (defer_changed && ndeferred < NGRP + 1) {
        deferred[ndeferred].h = h;
        deferred[ndeferred].wp = *p;
        ndeferred++;
        return;
    }
    send_message(h, WM_WINDOWPOSCHANGED, 0, (LPARAM)(WINDOWPOS __far *)p);
}

/* SetWindowPos's activation, made before WM_WINDOWPOSCHANGED (input.c) */
extern BOOL activate_in_pos;
static HWND pos_activate;

BOOL set_window_pos(HWND h, HWND after, int x, int y, int cx, int cy, UINT flags)
{
    PWND w = valid_wnd(h), parent;
    WINDOWPOS wp;
    RECT oldwin, oldcli;
    BOOL wasvis, nowvis, changed, send_changed, restored = FALSE, zmoved = FALSE;
    HRGN above = 0;             /* what covered it before it came up */
    HWND shown_after;
    HWND grp[NGRP], gprev[NGRP];
    int ng = 0, k = 0, i;
    int px = 0, py = 0;
    BOOL client_rule = max_client_rule;     /* (this call's only) */
    max_client_rule = FALSE;
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
    wp.hwnd = h;
    /* hwndInsertAfter as the place it goes (3.1, US42): a top-level window
       brought up goes under the switch window (#32771, always above the
       others: switch.c), one sent down under the last window */
    shown_after = after;
    if (!(flags & SWP_NOZORDER) && w->hwndParent == hwndDesktop) {
        if (after == HWND_TOP && !(w->exstyle & WS_EX_TOPMOST)) {
            HWND s, last = 0;
            HWND it = icon_title_of(h);
            for (s = parent->hwndChild; s && s != h && (PW(s)->exstyle & WS_EX_TOPMOST); s = PW(s)->hwndNext)
                last = s;
            shown_after = last;
            /* a window with an icon title (shown, or hidden since it was
               restored): under it, as it goes up first (3.1, US42) */
            if (it)
                shown_after = it;
        } else if (after == HWND_BOTTOM) {
            HWND s, last = 0;
            for (s = parent->hwndChild; s; s = PW(s)->hwndNext)
                if (s != h)
                    last = s;
            if (last)
                shown_after = last;
        }
    }
    wp.hwndInsertAfter = shown_after;
    /* the caller's values as given: 0,0 0x0 for SWP_NOMOVE | SWP_NOSIZE
       (3.1, US42: SetWindowPos(HWND_TOP, 0, 0, 0, 0, ...)) */
    wp.x = x;
    wp.y = y;
    wp.cx = cx;
    wp.cy = cy;
    wp.flags = flags;
    /* the windows of its group that move with it (owner_order) */
    ng = 0;
    if (!(flags & SWP_NOZORDER) && w->hwndParent == hwndDesktop && (after == HWND_TOP || after == HWND_BOTTOM))
        ng = pos_group(w, after, grp, &k);
    for (i = 0; i < k; i++)
        group_changing(grp, i, shown_after);
    if (!(flags & SWP_NOSENDCHANGING) || 1) {
        if (k > 0)
            wp.hwndInsertAfter = grp[k - 1];
        if ((flags & (SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_SHOWWINDOW | SWP_HIDEWINDOW)) !=
            (SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER) || !(flags & SWP_NOACTIVATE))
            send_message(h, WM_WINDOWPOSCHANGING, 0, (LPARAM)(WINDOWPOS __far *)&wp);
        if (!(w = valid_wnd(h)))
            return FALSE;
        if (k > 0 && wp.hwndInsertAfter == grp[k - 1])
            wp.hwndInsertAfter = shown_after;
    }
    for (i = k + 1; i < ng; i++)
        group_changing(grp, i, shown_after);
    if (!(w = valid_wnd(h)))
        return FALSE;
    x = wp.x, y = wp.y, cx = wp.cx, cy = wp.cy, flags = wp.flags;
    if (wp.hwndInsertAfter != shown_after)
        after = wp.hwndInsertAfter;
    if (flags & SWP_NOMOVE) {
        x = w->rcWindow.left - px;
        y = w->rcWindow.top - py;
    }
    if (flags & SWP_NOSIZE) {
        cx = w->rcWindow.right - w->rcWindow.left;
        cy = w->rcWindow.bottom - w->rcWindow.top;
    }
    /* a new size for an overlapped or thick-framed window: WM_GETMINMAXINFO,
       and the size kept within its tracking sizes, an icon's and a
       maximised window's excepted
       (TK75 on 3.1: WM_WINDOWPOSCHANGING, WM_GETMINMAXINFO,
       WM_NCCALCSIZE; an overlapped window sized to 20 x 5 stayed
       102 x 26, a thick-framed popup came out 20 x 8; minimised to
       36 x 36 it was asked too) */
    if (!(flags & SWP_NOSIZE) && (!(w->style & (WS_CHILD | WS_POPUP)) || (w->style & WS_THICKFRAME)) &&
        (cx != w->rcWindow.right - w->rcWindow.left || cy != w->rcWindow.bottom - w->rcWindow.top)) {
        MINMAXINFO mm;
        void minmax(PWND w, MINMAXINFO __far *mm);
        minmax(w, &mm);
        send_message(h, WM_GETMINMAXINFO, 0, (LPARAM)(MINMAXINFO __far *)&mm);
        if (!(w = valid_wnd(h)))
            return FALSE;
        if (!(w->style & (WS_MINIMIZE | WS_MAXIMIZE))) {
            if (cx > mm.ptMaxTrackSize.x)
                cx = mm.ptMaxTrackSize.x;
            if (cy > mm.ptMaxTrackSize.y)
                cy = mm.ptMaxTrackSize.y;
            if (cx < mm.ptMinTrackSize.x)
                cx = mm.ptMinTrackSize.x;
            if (cy < mm.ptMinTrackSize.y)
                cy = mm.ptMinTrackSize.y;
        }
    }
    if (cx < 0) cx = 0;
    if (cy < 0) cy = 0;
    oldwin = w->rcWindow;
    oldcli = w->rcClient;
    wasvis = is_visible(w);

    for (i = 0; i < ng; i++)
        gprev[i] = valid_wnd(grp[i]) ? above_of(grp[i]) : 0;
    /* Z order */
    if (!(flags & SWP_NOZORDER) && w->hwndParent == hwndDesktop) {
        if (after == (HWND)-1)          /* HWND_TOPMOST */
            w->exstyle |= WS_EX_TOPMOST;
        else if (after == (HWND)-2)     /* HWND_NOTOPMOST */
            w->exstyle &= ~WS_EX_TOPMOST;
    }
    if (!(flags & SWP_NOZORDER) && after != h && w->hwndParent) {
        if (!(after == HWND_TOP && at_top(w)) || ng) {
            HWND s, was_above = w->hwndParent == hwndDesktop ? above_of(h) : 0;
            if (wasvis) {
                above = CreateRectRgn(0, 0, 0, 0);
                for (s = PW(w->hwndParent)->hwndChild; s && s != h; s = PW(s)->hwndNext)
                    if (PW(s)->style & WS_VISIBLE) {
                        HRGN t = CreateRectRgnIndirect(&PW(s)->rcWindow);
                        CombineRgn(above, above, t, RGN_OR);
                        DeleteObject(t);
                    }
            }
            unlink_window(w);
            link_window(w, after == (HWND)-1 ? HWND_TOP : (after == (HWND)-2 ? HWND_TOP : after));
            if (w->hwndParent == hwndDesktop)
                owner_order(w, after);
            changed = TRUE;
            /* (a top-level window put back where it was has not moved:
               3.1, US42, SetWindowPos(HWND_TOP) of the window already on
               top sent no WM_WINDOWPOSCHANGED) */
            zmoved = w->hwndParent != hwndDesktop || above_of(h) != was_above;
        }
    }
    /* Alt+Esc: the window going down, its WM_WINDOWPOSCHANGING after this
       one's and its group's, its WM_WINDOWPOSCHANGED after this one's;
       Alt+Shift+Esc: the old one under this one's group, with none
       (3.1, US42; switch.c) */
    if (pos_companion && pos_companion != h) {
        HWND c = pos_companion, ca = pos_companion_after;
        BOOL quiet = ca != HWND_BOTTOM;
        pos_companion = 0;
        if (quiet && ng)
            ca = grp[ng - 1];
        else if (quiet)
            ca = h;
        defer_changed++;
        if (quiet)
            quiet_changed++;
        set_window_pos(c, ca, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
        if (quiet)
            quiet_changed--;
        defer_changed--;
        if (!(w = valid_wnd(h)))
            return FALSE;
    }
    /* place */
    if (!(flags & SWP_NOMOVE) || !(flags & SWP_NOSIZE)) {
        SetRect(&w->rcWindow, x + px, y + py, x + px + cx, y + py + cy);
        calc_client(w);
        if (!(w = valid_wnd(h)))
            return FALSE;
        byte_align(w, FALSE);
        if (w->rcClient.left != oldcli.left || w->rcClient.top != oldcli.top)
            move_children(w, w->rcClient.left - oldcli.left, w->rcClient.top - oldcli.top);
        if (w->hrgnUpdate)
            OffsetRgn(w->hrgnUpdate, w->rcWindow.left - oldwin.left, w->rcWindow.top - oldwin.top);
    } else if (flags & SWP_FRAMECHANGED) {
        calc_client(w);
        if (!(w = valid_wnd(h)))
            return FALSE;
        /* the children with the client area: a menu bar given with
           SetMenu moves it down (StarOffice's frames get their menu after
           their MDI client is made: the client stayed under the caption,
           19 pixels too high, and StarWriter laid out its style tree from
           that) */
        if (w->rcClient.left != oldcli.left || w->rcClient.top != oldcli.top)
            move_children(w, w->rcClient.left - oldcli.left, w->rcClient.top - oldcli.top);
    }
    if (flags & SWP_SHOWWINDOW) {
        /* CS_SAVEBITS: the screen under it kept (savebits.c) */
        if (!(w->style & WS_CHILD) && (((PCLS)w->cls)->style & CS_SAVEBITS))
            savebits_show(h);
        w->style |= WS_VISIBLE;
    }
    if (flags & SWP_HIDEWINDOW) {
        w->style &= ~WS_VISIBLE;
        /* CS_SAVEBITS: the image under it back now, before another
           window is activated and comes up over its place (Windows
           Write's File > Open box: the owner, brought up first, hid the
           place from the image, and the box stayed on screen) */
        if (wasvis)
            restored = savebits_restore(h, &oldwin);
        if (h == hwndFocus || IsChild(h, hwndFocus)) {
            set_focus(0);
            /* WM_KILLFOCUS can destroy it (USER audit, sweep) */
            if (!(w = valid_wnd(h)))
                return TRUE;
        }
        /* the active window hidden by SetWindowPos stays the active one
           (ShowWindow(SW_HIDE) moves the activation, see there) */
    }
    nowvis = is_visible(w);
    changed = !EqualRect(&oldwin, &w->rcWindow) || wasvis != nowvis ||
              !(flags & SWP_NOZORDER) || (flags & SWP_FRAMECHANGED);
    /* WM_WINDOWPOSCHANGED tells what this call did, decided now: the
       messages sent below (WM_SIZE and WM_MOVE held back since
       CreateWindow, WM_CHILDACTIVATE) can make the window move itself,
       and that move has a WM_WINDOWPOSCHANGED of its own. w16 compared
       the place at the end with the one before, so a window shown for
       the first time that moved itself on its first WM_MOVE got a
       second WM_MOVE from this call's message, and moved again:
       QuickTime's movie window, shown by its player, went 32 pixels
       further right than on 3.1 (docs/notes/quicktime-handover.md) */
    wp.x = w->rcWindow.left - px;
    wp.y = w->rcWindow.top - py;
    wp.cx = w->rcWindow.right - w->rcWindow.left;
    wp.cy = w->rcWindow.bottom - w->rcWindow.top;
    wp.flags = flags & ~(SWP_NOMOVE | SWP_NOSIZE);     /* what did change, below */
    /* (a child asked to the top it is already at gets none: TK75 on 3.1,
       SetWindowPos(HWND_TOP, SWP_NOMOVE | SWP_NOSIZE) of the only child
       sent WM_WINDOWPOSCHANGING and WM_CHILDACTIVATE alone) */
    send_changed = !EqualRect(&oldwin, &w->rcWindow) ||
                   (flags & (SWP_SHOWWINDOW | SWP_HIDEWINDOW | SWP_FRAMECHANGED)) ||
                   (!(flags & SWP_NOZORDER) && zmoved);
    if (send_changed) {
        /* moved or sized: the window or its client area (US18: on 3.1 a
           scroll bar a window's WM_SIZE takes away gives it a second
           WM_SIZE with the larger client area; w16 sent none, and Program
           Manager's group maximised from its icon kept its scroll bars) */
        if (oldwin.left == w->rcWindow.left && oldwin.top == w->rcWindow.top &&
            oldcli.left == w->rcClient.left && oldcli.top == w->rcClient.top)
            wp.flags |= SWP_NOMOVE;
        /* (when ShowWindow maximises, the client area's size alone
           decides: TK75 "all0 sv showmax" on 3.1, an MDI child maximised
           in a 0 x 0 client went from 102 x 26 to 102 x 27, its client
           area 94 x 0 both times, and got no WM_SIZE, so its client did
           not count it maximised yet; elsewhere a new window size with
           the same client area does get one: US34's 2-pixel lists) */
        if ((client_rule || (oldwin.right - oldwin.left == wp.cx &&
                                 oldwin.bottom - oldwin.top == wp.cy)) &&
            oldcli.right - oldcli.left == w->rcClient.right - w->rcClient.left &&
            oldcli.bottom - oldcli.top == w->rcClient.bottom - w->rcClient.top)
            wp.flags |= SWP_NOSIZE;
    }
    refresh_dcs();
    /* (not for popup menus: a menu opened under the pointer from the
       keyboard keeps its first item selected) */
    if (changed && (wasvis || nowvis) && GetClassWord(h, GCW_ATOM) != 32768) {
        pointer_changed(&oldwin);
        if (!EqualRect(&oldwin, &w->rcWindow))
            pointer_changed(&w->rcWindow);
    }
    if (wasvis && nowvis && !EqualRect(&oldwin, &w->rcWindow))
        savebits_forget(h);
    if (changed && !(flags & SWP_NOREDRAW)) {
        if (nowvis && ((flags & SWP_NOZORDER) || !w->hwndParent ||
                       PW(w->hwndParent)->hwndChild == h)) {
            /* above its siblings: what it left uncovered, and the window
               itself; not what it now covers. Observed on 3.1 (throwaway
               probe, a child moved with MoveWindow(..., FALSE) and shown
               over its parent, which has no WS_CLIPCHILDREN): the parent
               got no WM_PAINT for it. Windows Write shows its ruler that
               way and paints it at once (UpdateWindow); w16 also gave the
               main window the ruler's place, and its paint then erased the
               ruler */
            if (wasvis)
                invalidate_screen_except(&oldwin, &w->rcWindow);
            if (wasvis && above && EqualRect(&oldwin, &w->rcWindow) &&
                !(flags & SWP_FRAMECHANGED)) {
                /* only brought up: what was covered (3.1, the same probe:
                   a window activated again after a popup over it went
                   was painted only where the popup had been) */
                invalidate_window_screen_rgn(h, above);
            } else
                invalidate(h, 0, 0, TRUE, TRUE, TRUE);
        } else {
            /* what was under the old place (unless the image under it was
               put back), and the window in its new one */
            if (wasvis && !restored)
                invalidate_screen(&oldwin);
            if (nowvis)
                invalidate_screen(&w->rcWindow);
        }
    }
    if (above) {
        DeleteObject(above);
        above = 0;
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
    if ((flags & SWP_SHOWWINDOW) && !(flags & SWP_NOACTIVATE) && !(w->style & WS_CHILD)) {
        activate_window(h, FALSE);
        if (!(w = valid_wnd(h)))
            return TRUE;
    }
    /* a child placed without SWP_NOACTIVATE: WM_CHILDACTIVATE, which
       DefMDIChildProc turns into the MDI child's activation, whether
       anything changed or not (TK75 on 3.1: SetWindowPos of a plain child
       with SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER sent WM_WINDOWPOSCHANGING
       and WM_CHILDACTIVATE; with SWP_SHOWWINDOW, WM_CHILDACTIVATE before
       WM_WINDOWPOSCHANGED; ShowWindow's maximise, minimise and restore
       send it, its show and hide and MoveWindow do not; US18:
       WM_MDIMAXIMIZE or ShowWindow SW_SHOWMAXIMIZED on a child that is
       not the active one activates it before its WM_SIZE) */
    if (!(flags & SWP_NOACTIVATE) && (w->style & WS_CHILD))
        send_message(h, WM_CHILDACTIVATE, 0, 0);
    if ((w = valid_wnd(h)) != 0 && (w->style & WS_MINIMIZE) && !(flags & SWP_NOMOVE))
        follow_icon_title(h);
    /* an icon moved in the Z order takes its title along, just above it
       (3.1: a minimised Calculator sent to the bottom left its title
       under Program Manager with it; w16's stayed on top: boot-test case
       taskman. TM1 and TM2 on 3.1 list a title right before its icon) */
    if (zmoved && (w = valid_wnd(h)) != 0 && (w->style & WS_MINIMIZE))
        icon_title_above(h);
    if (!(w = valid_wnd(h)))
        return TRUE;
    /* SetWindowPos activating it: the activation between WM_WINDOWPOSCHANGING
       and WM_WINDOWPOSCHANGED (3.1, US42: Alt+Tab's SetWindowPos, the
       window's WM_WINDOWPOSCHANGING, WH_CBT's HCBT_ACTIVATE, the old
       window's deactivation, the new one's activation, then its
       WM_WINDOWPOSCHANGED) */
    if (pos_activate == h) {
        pos_activate = 0;
        if (!(w->style & WS_CHILD) && is_visible(w) && h != hwndActive) {
            activate_in_pos = !(flags & SWP_NOZORDER);
            activate_window(h, FALSE);
            activate_in_pos = FALSE;
            if (!(w = valid_wnd(h)))
                return TRUE;
        }
    }
    /* the client area kept its place, its size: 0800h, 1000h (3.1, US42:
       1803h for a window only brought up) */
    if (oldcli.left == w->rcClient.left && oldcli.top == w->rcClient.top)
        wp.flags |= 0x0800;
    if (oldcli.right - oldcli.left == w->rcClient.right - w->rcClient.left &&
        oldcli.bottom - oldcli.top == w->rcClient.bottom - w->rcClient.top)
        wp.flags |= 0x1000;
    for (i = 0; i < k; i++)
        group_changed(grp, gprev, i);
    if (send_changed && valid_wnd(h))
        send_changed_msg(h, &wp);
    for (i = k + 1; i < ng; i++)
        group_changed(grp, gprev, i);
    if (!defer_changed && ndeferred) {
        int n = ndeferred;
        ndeferred = 0;
        for (i = 0; i < n; i++)
            if (valid_wnd(deferred[i].h))
                send_message(deferred[i].h, WM_WINDOWPOSCHANGED, 0,
                             (LPARAM)(WINDOWPOS __far *)&deferred[i].wp);
    }
    return TRUE;
}

/* without SWP_NOACTIVATE a shown top-level window is activated, whether
   it is shown now or was already (3.1 SDK; US24 on 3.1: with an icon
   active, SetWindowPos of the main window with SWP_SHOWWINDOW, with
   SWP_NOZORDER and with neither made it active, with SWP_NOACTIVATE not.
   Microsoft's WINOLDAP comes back to the Windows application that way;
   w16 activated only a window being shown, and the DOS program's icon
   stayed active). Here for the API only: USER's own calls are as they
   were (UNVERIFIED which of them 3.1 makes without SWP_NOACTIVATE) */
BOOL WINAPI SetWindowPos(HWND h, HWND after, int x, int y, int cx, int cy, UINT flags)
{
    BOOL r;
    PWND w = valid_wnd(h);
    if (w && !(flags & (SWP_NOACTIVATE | SWP_HIDEWINDOW | SWP_SHOWWINDOW)) && !(w->style & WS_CHILD))
        pos_activate = h;
    r = set_window_pos(h, after, x, y, cx, cy, flags);
    pos_activate = 0;
    w = valid_wnd(h);
    if (r && w && !(flags & (SWP_NOACTIVATE | SWP_HIDEWINDOW)) && !(w->style & WS_CHILD) &&
        is_visible(w) && h != hwndActive)
        activate_window(h, FALSE);
    return r;
}

BOOL WINAPI MoveWindow(HWND h, int x, int y, int cx, int cy, BOOL repaint)
{
    return set_window_pos(h, 0, x, y, cx, cy, SWP_NOZORDER | SWP_NOACTIVATE | (repaint ? 0 : SWP_NOREDRAW));
}

BOOL WINAPI BringWindowToTop(HWND h)
{
    PWND w = valid_wnd(h);
    /* a child: SetWindowPos to the top without SWP_NOACTIVATE, so it gets
       WM_CHILDACTIVATE, hidden or not, and an MDI child is activated
       (TK75 on 3.1: "all sv bring", "p bring hidden"; StarChart brings
       its new hidden document window to the top and waits for that
       activation before it shows and maximises the window and draws the
       chart) */
    if (w && !(w->style & WS_CHILD))
        activate_window(h, FALSE);
    else
        set_window_pos(h, HWND_TOP, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE);
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
    /* (hidden icons do not count: US24 on 3.1, a third icon made while
       the second was hidden took the second's place) */
    for (s = parent->hwndChild; s; s = PW(s)->hwndNext)
        if (s != HW(w) && (PW(s)->style & (WS_MINIMIZE | WS_VISIBLE)) == (WS_MINIMIZE | WS_VISIBLE))
            n++;
    pt->x = (n % ((pr.right - pr.left) / cx)) * cx + (cx - sysmet[SM_CXICON]) / 2;
    pt->y = (pr.bottom - pr.top) - cy * (1 + n / ((pr.right - pr.left) / cx));
    /* the icon's window starts a whole spacing above the bottom, on the
       desktop (US4: 408 on a 480-line screen, its title at 444) and in a
       parent window alike (TK75 on 3.1: 128 in a 200-line MDI client,
       201 in a 273-line window; w16 put a child's icon 24 lines lower) */
}

/* another shown icon of the same parent at PT (the parent's coordinates) */
static BOOL icon_taken(PWND w, POINT pt)
{
    HWND s;
    PWND parent = PW(w->hwndParent);
    int px = (w->style & WS_CHILD) ? parent->rcClient.left : 0;
    int py = (w->style & WS_CHILD) ? parent->rcClient.top : 0;
    for (s = parent->hwndChild; s; s = PW(s)->hwndNext)
        if (s != HW(w) && (PW(s)->style & (WS_MINIMIZE | WS_VISIBLE)) == (WS_MINIMIZE | WS_VISIBLE) &&
            PW(s)->rcWindow.left - px == pt.x && PW(s)->rcWindow.top - py == pt.y)
            return TRUE;
    return FALSE;
}

typedef struct PLACE { RECT normal; POINT icon; WORD hasicon; HWND title; } PLACE;
static void hide_icon_title(PLACE *pl);
static void show_icon_title(PLACE *pl, HWND h);
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

/* CreateWindow of a top-level window with WS_MINIMIZE, after WM_CREATE:
   it goes to its icon's place, the size it was given kept as its normal
   place (probe US24 on Windows 3.1: after WM_CREATE a move to 21,408,
   36 x 36, WM_MOVE and WM_SIZE SIZE_MINIMIZED, still hidden;
   GetWindowPlacement then gives the icon's place and the given rectangle
   as normal). w16 left it at the given place and size: WINOLDAP's icon,
   made that way, showed as a window of CW_USEDEFAULT's size at 0,0 with
   the icon in its corner (Alt+Esc or Alt+Tab out of the MS-DOS Prompt).
   UNVERIFIED: child windows (not done for them); which place 3.1 takes
   when other icons are hidden (US24's third window went to the second
   place with a hidden icon there) */
void minimize_new(HWND h)
{
    PWND w = valid_wnd(h);
    PLACE *pl;
    POINT pt;
    if (!w)
        return;
    pl = place_of(h, TRUE);
    icon_place(w, &pt);
    if (pl) {
        pl->icon = pt;
        pl->hasicon = 1;
    }
    set_window_pos(h, 0, pt.x, pt.y, sysmet[SM_CXICON] + 4, sysmet[SM_CYICON] + 4,
                   SWP_NOZORDER | SWP_NOACTIVATE | SWP_FRAMECHANGED);
    if ((w = valid_wnd(h)) != 0 && (w->flags & WF_SENDSIZE)) {
        w->flags &= ~WF_SENDSIZE;
        send_message(h, WM_MOVE, 0, MAKELONG(w->rcClient.left, w->rcClient.top));
        if ((w = valid_wnd(h)) != 0)
            send_message(h, WM_SIZE, SIZE_MINIMIZED,
                         MAKELONG(w->rcClient.right - w->rcClient.left,
                                  w->rcClient.bottom - w->rcClient.top));
    }
}

static BOOL restore_noact;      /* restore_noactivate under way */

/* a maximised or minimised window restored without being activated (an
   MDI child giving its maximised state to the one activated: mdi.c) */
void restore_noactivate(HWND h)
{
    restore_noact = TRUE;
    ShowWindow(h, SW_RESTORE);
    restore_noact = FALSE;
}

static BOOL max_hidden;         /* maximize_hidden under way */

/* a hidden window maximised and left hidden, not activated (WM_MDICREATE
   of a maximised child: TK75 on 3.1, WM_GETMINMAXINFO and the move,
   WM_MOVE and WM_SIZE before WM_SHOWWINDOW; with MDIS_ALLCHILDSTYLES and
   no WS_VISIBLE it stays hidden, maximised) */
void maximize_hidden(HWND h)
{
    max_hidden = TRUE;
    ShowWindow(h, SW_SHOWMAXIMIZED);
    max_hidden = FALSE;
}

static BOOL same_size(const RECT *a, const RECT *b)
{
    return a->right - a->left == b->right - b->left && a->bottom - a->top == b->bottom - b->top;
}

/* WM_SHOWWINDOW, then WM_SETVISIBLE (0009h: TK75 and TK71 on 3.1, after
   WM_SHOWWINDOW whenever ShowWindow shows or hides a window, top-level
   or child; documented in the 2.1 SDK as sent just before a window is
   made visible or invisible). FALSE: the window is gone */
static BOOL show_msgs(HWND h, BOOL show)
{
    send_message(h, WM_SHOWWINDOW, show, 0);
    if (!valid_wnd(h))
        return FALSE;
    send_message(h, 0x0009, show, 0);
    return valid_wnd(h) != 0;
}

BOOL WINAPI ShowWindow(HWND h, int cmd)
{
    PWND w = valid_wnd(h);
    BOOL wasvis;
    RECT sized;
    UINT f = SWP_NOZORDER | SWP_NOMOVE | SWP_NOSIZE;
    PLACE *pl;
    if (!w)
        return FALSE;
    wasvis = (w->style & WS_VISIBLE) != 0;
    /* WH_CBT's HCBT_MINMAX, the show command in lParam, when the window is
       to be minimised, maximised or restored from either (US30 cbt on
       3.1: 6, 3, 9; not for a show that changes neither); nonzero: nothing */
    {
        BOOL st = (cmd == SW_SHOWMINIMIZED || cmd == SW_MINIMIZE || cmd == SW_SHOWMINNOACTIVE) ?
                  !(w->style & WS_MINIMIZE) :
                  cmd == SW_SHOWMAXIMIZED ? !(w->style & WS_MAXIMIZE) :
                  (cmd == SW_SHOWNORMAL || cmd == SW_RESTORE) ?
                  (w->style & (WS_MINIMIZE | WS_MAXIMIZE)) != 0 : FALSE;
        if (st && cbt_hook(HCBT_MINMAX, (WPARAM)h, cmd))
            return wasvis;
        if (!(w = valid_wnd(h)))
            return FALSE;
    }
    switch (cmd) {
    case SW_HIDE:
        if (!wasvis)
            return FALSE;
        if (!show_msgs(h, FALSE))
            return wasvis;
        set_window_pos(h, 0, 0, 0, 0, 0, f | SWP_HIDEWINDOW | SWP_NOACTIVATE);
        /* the active window hidden by ShowWindow: its owner becomes
           active if it can take it, else the next visible, enabled
           top-level window, or none. SetWindowPos(SWP_HIDEWINDOW), with
           or without SWP_NOACTIVATE, leaves the hidden window active on
           3.1 (throwaway probe: an active popup hidden while its owner
           was disabled, an icon of the same program shown: SetWindowPos
           left the popup active, ShowWindow made the icon active; a new
           window shown then was activated, and destroying the hidden
           popup with nothing else shown gave the activation to its
           enabled owner). Program Manager hides its Run dialog with
           SetWindowPos before WinExec: w16 activated the next window
           there, with the owner disabled an icon (Notepad's in the
           boot-test icons), and keys typed before the program's window
           came up (a loaded machine) went to that icon. UNVERIFIED:
           3.1's next window (the probe's ShowWindow chose the icon over
           another program's open window; next_to_activate prefers open
           windows) */
        if (h == hwndActive && (w = valid_wnd(h)) != 0) {
            HWND t = w->hwndOwner;
            if (!t || !valid_wnd(t) || !(PW(t)->style & WS_VISIBLE) || (PW(t)->style & WS_DISABLED))
                t = next_to_activate(h);
            activate_window(t, FALSE);
        }
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
            /* its place as an icon before, unless another icon is there
               now (3.1, US42: D minimised again while B's icon had its
               old place went to the next free one) */
            if (pl && pl->hasicon && !icon_taken(w, pl->icon))
                pt = pl->icon;
            else
                icon_place(w, &pt);
            if (!wasvis && !max_hidden)
                show_msgs(h, TRUE);
            if (!(w = valid_wnd(h)))        /* (USER audit, sweep) */
                return wasvis;
            sized = w->rcWindow;
            set_window_pos(h, 0, pt.x, pt.y, sysmet[SM_CXICON] + 4, sysmet[SM_CYICON] + 4,
                           SWP_NOZORDER | SWP_SHOWWINDOW | SWP_FRAMECHANGED |
                           (cmd == SW_MINIMIZE || cmd == SW_SHOWMINNOACTIVE ? SWP_NOACTIVATE : 0));
            if ((w = valid_wnd(h)) != 0) {
                /* with the icon's client size, as from WM_WINDOWPOSCHANGED
                   (it was 0 x 0: w16's Clock sized its icon's digits to
                   it, and they came out huge and cut off); it comes from
                   there, once (US18), but for a size that stays the same */
                if (same_size(&sized, &w->rcWindow))
                    send_message(h, WM_SIZE, SIZE_MINIMIZED,
                                 MAKELONG(w->rcClient.right - w->rcClient.left,
                                          w->rcClient.bottom - w->rcClient.top));
                if (pl && (w = valid_wnd(h)) != 0)
                    show_icon_title(pl, h);
            }
            /* the active window minimised with SW_SHOWMINIMIZED stays
               active, an icon without the focus (US30 iconopen on 3.1:
               WM_KILLFOCUS, GetFocus() 0; the keys then go to the icon
               as WM_SYSKEYDOWN). It kept the focus, so opening the icon
               again sent no WM_SETFOCUS (below): Microsoft's WINOA386
               gives a box going from full screen to a window the
               keyboard (SHELL 5) at that WM_SETFOCUS */
            if (cmd == SW_SHOWMINIMIZED && h == hwndActive && (w = valid_wnd(h)) != 0 &&
                !(w->style & WS_CHILD) && hwndFocus && (hwndFocus == h || IsChild(h, hwndFocus)))
                set_focus(0);
            /* a child minimised without activation gives the focus it
               holds to its top-level window (3.1: Program Manager makes
               its groups shown, then minimises the iconic ones with
               SW_SHOWMINNOACTIVE; the arrow keys went to the Accessories
               icon instead of Main. Given to the MDI client, it kept the
               next group from taking it: TK75 on 3.1, a child activated
               with the focus on the client does not get it. UNVERIFIED:
               where 3.1 puts it) */
            if ((cmd == SW_MINIMIZE || cmd == SW_SHOWMINNOACTIVE) && (w = valid_wnd(h)) != 0 &&
                (w->style & WS_CHILD) && hwndFocus && (hwndFocus == h || IsChild(h, hwndFocus)))
                set_focus(top_level(h));
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
                /* the focus goes first (US30 cbt on 3.1: SETFOCUS 0, then
                   the next window's ACTIVATE and SETFOCUS) */
                if (hwndFocus && (hwndFocus == h || IsChild(h, hwndFocus)))
                    set_focus(0);
                set_window_pos(h, HWND_BOTTOM, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
                /* the window active before it, while it is there to take
                   it, else the next one (3.1, US42: D, activated by Alt+Esc
                   from C's popup Cp, minimised: Cp, at the bottom, was
                   activated over E right under D; B, activated as a
                   dialog closed, minimised: A, the next) */
                t = hwndActivePrev;
                if (!t || t == h || !valid_wnd(t) || !is_visible(PW(t)) ||
                    (PW(t)->style & (WS_DISABLED | WS_MINIMIZE | WS_CHILD)))
                    t = next_to_activate(h);
                if (t)
                    activate_window(t, FALSE);
            }
        } else if (!wasvis) {
            if (!show_msgs(h, TRUE))
                return wasvis;
            set_window_pos(h, 0, 0, 0, 0, 0, f | SWP_SHOWWINDOW |
                           (cmd == SW_SHOWMINNOACTIVE ? SWP_NOACTIVATE : 0));
            /* an icon made by CreateWindow (WS_MINIMIZE) shown: its title
               too (3.1: WINOLDAP's icon, "COMMAND" under it) */
            if ((w = valid_wnd(h)) != 0 && (w->style & WS_MINIMIZE) && (w->style & WS_VISIBLE) &&
                (pl = place_of(h, FALSE)) != 0)
                show_icon_title(pl, h);
        }
        return wasvis;
    case SW_SHOWMAXIMIZED:
        if (!(w->style & WS_MAXIMIZE)) {
            MINMAXINFO mm;
            void minmax(PWND w, MINMAXINFO __far *mm);
            pl = place_of(h, TRUE);
            if (pl && !(w->style & WS_MINIMIZE)) {
                pl->normal = w->rcWindow;
                if (w->style & WS_CHILD)
                    OffsetRect(&pl->normal, -PW(w->hwndParent)->rcClient.left,
                               -PW(w->hwndParent)->rcClient.top);
            } else if (pl) {
                /* maximised from its icon: the icon's place kept, its
                   title gone, as when restored (the title stayed behind
                   a maximised MDI child made from an icon) */
                pl->icon.x = w->rcWindow.left;
                pl->icon.y = w->rcWindow.top;
                if (w->style & WS_CHILD) {
                    pl->icon.x -= PW(w->hwndParent)->rcClient.left;
                    pl->icon.y -= PW(w->hwndParent)->rcClient.top;
                }
                pl->hasicon = 1;
            }
            /* the screen's size, a child's too (TK75 on 3.1: a plain
               child maximised in a 400 x 300 window came out 648 x 488
               at -4,-4; w16 gave it its parent's client area. An MDI
               child's comes from DefMDIChildProc) */
            minmax(w, &mm);
            send_message(h, WM_GETMINMAXINFO, 0, (LPARAM)(MINMAXINFO __far *)&mm);
            if (!(w = valid_wnd(h)))
                return wasvis;
            /* never smaller than the minimum tracking size (TK75 "all0 sv
               showmax" on 3.1: an MDI child maximised in a 0 x 0 client
               came out 102 x 27, not 8 x 27; StarWriter sizes its document
               window's controls from that client area's width) */
            if (mm.ptMaxSize.x < mm.ptMinTrackSize.x)
                mm.ptMaxSize.x = mm.ptMinTrackSize.x;
            if (mm.ptMaxSize.y < mm.ptMinTrackSize.y)
                mm.ptMaxSize.y = mm.ptMinTrackSize.y;
            /* an icon opened: WM_QUERYOPEN first, FALSE keeps it an icon
               (3.1 SDK; US18: 3.1 sends it after WM_GETMINMAXINFO when an
               MDI child is maximised from its icon. Program Manager's
               groups answer it, and without it a group maximised from its
               icon kept scroll bars it had no need of) */
            if (w->style & WS_MINIMIZE) {
                if (!send_message(h, WM_QUERYOPEN, 0, 0) || !(w = valid_wnd(h)))
                    return wasvis;
                if (pl && pl->title)
                    hide_icon_title(pl);
                if (!(w = valid_wnd(h)))
                    return wasvis;
            }
            w->style &= ~WS_MINIMIZE;
            w->style |= WS_MAXIMIZE;
            /* (a hidden child maximised gets no WM_SHOWWINDOW or
               WM_SETVISIBLE: TK75 "all sv showmax" and "all0 sv showmax"
               on 3.1, an MDI child shown with SW_SHOWMAXIMIZED) */
            if (!wasvis && !max_hidden && !(w->style & WS_CHILD))
                show_msgs(h, TRUE);
            if (!(w = valid_wnd(h)))        /* (USER audit, sweep) */
                return wasvis;
            sized = w->rcWindow;
            max_client_rule = TRUE;     /* (for this call alone) */
            set_window_pos(h, 0, mm.ptMaxPosition.x, mm.ptMaxPosition.y,
                           mm.ptMaxSize.x, mm.ptMaxSize.y, SWP_NOZORDER | SWP_FRAMECHANGED |
                           (max_hidden ? SWP_NOACTIVATE : SWP_SHOWWINDOW));
            /* WM_SIZE comes from DefWindowProc's WM_WINDOWPOSCHANGED, once
               (US18: one WM_SIZE per maximise or restore on 3.1; w16 sent
               a second here); here only when the size stayed the same */
            if ((w = valid_wnd(h)) != 0 && same_size(&sized, &w->rcWindow))
                send_message(h, WM_SIZE, SIZE_MAXIMIZED,
                             MAKELONG(w->rcClient.right - w->rcClient.left,
                                      w->rcClient.bottom - w->rcClient.top));
            /* and active (3.1 SDK: SW_SHOWMAXIMIZED activates; US30 cbt on
               3.1: SC_MAXIMIZE of an inactive window, MINMAX then its
               ACTIVATE) */
            if ((w = valid_wnd(h)) != 0 && !(w->style & WS_CHILD) && h != hwndActive &&
                !max_hidden)
                activate_window(h, FALSE);
        } else if (!wasvis && !max_hidden) {
            if (!show_msgs(h, TRUE))
                return wasvis;
            set_window_pos(h, 0, 0, 0, 0, 0, f | SWP_SHOWWINDOW);
        }
        return wasvis;
    case SW_SHOWNORMAL:
    case SW_RESTORE:
        if (w->style & (WS_MINIMIZE | WS_MAXIMIZE)) {
            RECT r;
            BOOL wasicon = (w->style & WS_MINIMIZE) != 0;
            /* an icon opened: WM_QUERYOPEN first (3.1 SDK) */
            if ((w->style & WS_MINIMIZE) &&
                (!send_message(h, WM_QUERYOPEN, 0, 0) || !(w = valid_wnd(h))))
                return wasvis;
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
            if (pl && pl->title)
                hide_icon_title(pl);
            w->style &= ~(WS_MINIMIZE | WS_MAXIMIZE);
            if (!wasvis && !max_hidden)
                show_msgs(h, TRUE);
            if (!(w = valid_wnd(h)))        /* (USER audit, sweep) */
                return wasvis;
            sized = w->rcWindow;
            /* a child comes to the top in the same call, and gets its
               WM_CHILDACTIVATE (which an MDI child turns into its
               activation) from it, before WM_WINDOWPOSCHANGED (TK75 on
               3.1: WM_WINDOWPOSCHANGING, WM_GETMINMAXINFO, WM_NCCALCSIZE,
               WM_CHILDACTIVATE, WM_WINDOWPOSCHANGED, WM_MOVE, WM_SIZE;
               w16 sent a second WM_CHILDACTIVATE after them) */
            set_window_pos(h, (w->style & WS_CHILD) && !restore_noact ? HWND_TOP : 0,
                           r.left, r.top, r.right - r.left, r.bottom - r.top,
                           SWP_SHOWWINDOW | SWP_FRAMECHANGED |
                           ((w->style & WS_CHILD) && !restore_noact ? 0 : SWP_NOZORDER) |
                           (restore_noact ? SWP_NOACTIVATE : 0));
            if ((w = valid_wnd(h)) != 0 && same_size(&sized, &w->rcWindow))
                send_message(h, WM_SIZE, SIZE_RESTORED,
                             MAKELONG(w->rcClient.right - w->rcClient.left,
                                      w->rcClient.bottom - w->rcClient.top));
            /* restored: activated and on top (3.1 SDK: SW_RESTORE and
               SW_SHOWNORMAL activate; a Program Manager group restored
               from its icon comes up in front, active; a child above) */
            /* the active window's icon opened: no new activation, but
               the focus and WM_ACTIVATE again, now not minimised (US30
               iconopen on 3.1: WM_QUERYOPEN, WM_SETFOCUS, WM_ACTIVATE
               WA_ACTIVE; no WM_NCACTIVATE or WM_ACTIVATEAPP). w16 sent
               nothing, and Microsoft's WINOA386, whose full-screen box
               is such an icon, never gave the windowed box the keyboard:
               the VM kept the Alt of Alt+Enter down and typed letters
               were lost (boot-test win386tree) */
            if ((w = valid_wnd(h)) != 0 && !restore_noact && !(w->style & WS_CHILD) &&
                wasicon && h == hwndActive) {
                if (hwndFocus != h && !IsChild(h, hwndFocus))
                    set_focus(h);
                if (valid_wnd(h) && h == hwndActive)
                    send_message(h, WM_ACTIVATE, WA_ACTIVE, MAKELONG(0, 0));
            } else if (w && !restore_noact && !(w->style & WS_CHILD))
                activate_window(h, FALSE);
            /* a child opened from its icon takes the focus, its window
               active (TK75 on 3.1: a plain child, WM_SETFOCUS after its
               WM_SIZE; not when restored from maximised) */
            else if (w && wasicon && !restore_noact && top_level(h) == hwndActive && hwndFocus != h)
                set_focus(h);
            return wasvis;
        }
        /* fall through */
    default:            /* SW_SHOW, SW_SHOWNA, SW_SHOWNOACTIVATE */
        if (wasvis) {
            /* an icon is activated; an open window shown again is not
               (US30 show on 3.1: SW_SHOW, SW_SHOWNORMAL and SW_RESTORE of
               a visible normal window left another one active) */
            if (cmd == SW_SHOWNORMAL || cmd == SW_SHOW || cmd == SW_RESTORE)
                if (!(w->style & WS_CHILD) && (w->style & WS_MINIMIZE) && h != hwndActive)
                    activate_window(h, FALSE);
            return TRUE;
        }
        if (!show_msgs(h, TRUE))
            return FALSE;
        w = valid_wnd(h);
        /* a child keeps its place in the Z order (US18: three hidden
           children shown with SW_SHOW or SW_SHOWNORMAL stay in the order
           they were made on 3.1; w16 brought each to the top, and
           COMMDLG's dialogs, which show their controls one by one, came
           out with their tab order backwards: Terminal's Send Text File,
           Tab from the file name went to Strip LF) */
        /* nor is it activated: no WM_CHILDACTIVATE (TK75 on 3.1:
           SW_SHOW, SW_SHOWNORMAL and SW_SHOWNA of a hidden child sent
           WM_SHOWWINDOW, WM_SETVISIBLE, WM_WINDOWPOSCHANGING,
           WM_WINDOWPOSCHANGED; an MDI child shown so stays inactive) */
        set_window_pos(h, (cmd == SW_SHOWNA || cmd == SW_SHOWNOACTIVATE) ? 0 : HWND_TOP,
                       0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_SHOWWINDOW |
                       ((cmd == SW_SHOWNA || cmd == SW_SHOWNOACTIVATE) ? SWP_NOACTIVATE | SWP_NOZORDER : 0) |
                       ((w->style & WS_CHILD) ? SWP_NOZORDER | SWP_NOACTIVATE : 0));
        return FALSE;
    }
}

/* a bad WINDOWPLACEMENT: FALSE (3.1, US27; SetWindowPlacement too) */
BOOL WINAPI GetWindowPlacement(HWND h, WINDOWPLACEMENT __far *p)
{
    PWND w = valid_wnd(h);
    PLACE *pl;
    if (!w || GP_BAD(p))
        return FALSE;
    pl = place_of(h, FALSE);
    p->length = sizeof(WINDOWPLACEMENT);
    p->flags = 0;
    p->showCmd = (w->style & WS_MINIMIZE) ? SW_SHOWMINIMIZED :
                 (w->style & WS_MAXIMIZE) ? SW_SHOWMAXIMIZED : SW_SHOWNORMAL;
    if (w->style & WS_MINIMIZE) {
        /* an icon: where it is now, moved or arranged since it was
           minimised (3.1: Program Manager saves a group icon dragged or
           arranged with Window > Arrange Icons at its new place, read
           from here; w16 gave the place it was minimised to) */
        p->ptMinPosition.x = w->rcWindow.left;
        p->ptMinPosition.y = w->rcWindow.top;
        if (w->style & WS_CHILD) {
            p->ptMinPosition.x -= PW(w->hwndParent)->rcClient.left;
            p->ptMinPosition.y -= PW(w->hwndParent)->rcClient.top;
        }
    } else if (pl && pl->hasicon) {
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
    if (!w || GP_BAD(p))
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

/* The icon title window lives as long as its window (US30 show on 3.1:
   after a restore it is still there, hidden, and the next minimise shows
   the same window again). w16 destroyed it at each restore and made a new
   one at each minimise, which WH_CBT and EnumWindows saw. */
static void hide_icon_title(PLACE *pl)
{
    if (valid_wnd(pl->title))
        set_window_pos(pl->title, 0, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER |
                       SWP_NOACTIVATE | SWP_HIDEWINDOW);
    else
        pl->title = 0;
}

static void show_icon_title(PLACE *pl, HWND h)
{
    if (pl->title && !valid_wnd(pl->title))
        pl->title = 0;
    if (!pl->title) {
        pl->title = make_icon_title(h);
        return;
    }
    move_icon_title(pl->title, h);
    set_window_pos(pl->title, h, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_SHOWWINDOW |
                   SWP_NOACTIVATE);
}

/* the title window under an icon (menu.c places an icon's system menu
   by it), or 0 */
HWND icon_title_of(HWND h)
{
    PLACE *p = place_of(h, FALSE);
    return p && p->title && valid_wnd(p->title) ? p->title : 0;
}

/* an icon being dragged with the mouse: it and its title hidden while
   its picture is the pointer, shown again at the end (track.c; 3.1: the
   Games icon and its title gone from their place while it was dragged).
   The focus it had comes back with it. */
void icon_drag_hide(HWND h, BOOL hide)
{
    PLACE *pl = place_of(h, FALSE);
    static HWND hadfocus;
    if (!valid_wnd(h))
        return;
    if (hide) {
        hadfocus = hwndFocus;
        if (pl)
            hide_icon_title(pl);
        set_window_pos(h, 0, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER |
                       SWP_NOACTIVATE | SWP_HIDEWINDOW);
        /* the place painted now: the drag's loop dispatches nothing */
        if (valid_wnd(h))
            RedrawWindow((PW(h)->style & WS_CHILD) ? PW(h)->hwndParent : hwndDesktop, NULL, 0,
                         RDW_UPDATENOW | RDW_ERASENOW | RDW_ALLCHILDREN);
    } else {
        set_window_pos(h, 0, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER |
                       SWP_NOACTIVATE | SWP_SHOWWINDOW);
        if ((pl = place_of(h, FALSE)) != 0 && valid_wnd(h) && (PW(h)->style & WS_MINIMIZE))
            show_icon_title(pl, h);
        if (hadfocus && hadfocus != hwndFocus && valid_wnd(hadfocus) && is_visible(PW(hadfocus)))
            set_focus(hadfocus);
        hadfocus = 0;
    }
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

/* an icon's title put right above it in the Z order */
static void icon_title_above(HWND h)
{
    PLACE *p = place_of(h, FALSE);
    HWND t, prev;
    if (!p || !(t = p->title) || !valid_wnd(t) || PW(t)->hwndParent != PW(h)->hwndParent)
        return;
    prev = GetWindow(h, GW_HWNDPREV);
    if (prev == t)
        return;
    set_window_pos(t, prev ? prev : HWND_TOP, 0, 0, 0, 0,
                   SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
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
    HWND t, n;
    /* the next one taken first; a ShowWindow that destroyed or moved
       windows ends the walk (USER audit, sweep) */
    for (t = PW(hwndDesktop)->hwndChild; t; t = n) {
        n = PW(t)->hwndNext;
        if (PW(t)->hwndOwner == h)
            ShowWindow(t, show ? SW_SHOWNA : SW_HIDE);
        if (n && (!valid_wnd(n) || PW(n)->hwndParent != hwndDesktop))
            break;
    }
}

/* ---- BeginDeferWindowPos, DeferWindowPos, EndDeferWindowPos (3.1 SDK):
   the positions are kept in a global block (the handle) and EndDefer-
   WindowPos makes them with SetWindowPos, one window after another in
   the order given. UNVERIFIED: 3.1 moves them all at once (one repaint),
   what the block is (here a moveable global block: count, room, the
   entries) and whether a second DeferWindowPos for the same window
   replaces the first (here it does, as later Windows documents). A bad
   handle: 0 / FALSE. ---- */
typedef struct DWPENT { HWND h, after; int x, y, cx, cy; UINT flags; } DWPENT;
typedef struct DWPHDR { WORD magic, count, room; } DWPHDR;
#define DWP_MAGIC 0x5744                /* "DW" */

static DWPHDR __far *dwp_lock(HDWP d)
{
    DWPHDR __far *p;
    if (!d)
        return 0;
    p = (DWPHDR __far *)GlobalLock((HGLOBAL)d);
    if (p && (GlobalSize((HGLOBAL)d) < sizeof(DWPHDR) || p->magic != DWP_MAGIC)) {
        GlobalUnlock((HGLOBAL)d);
        return 0;
    }
    return p;
}

HDWP WINAPI BeginDeferWindowPos(int n)
{
    HGLOBAL g;
    DWPHDR __far *p;
    if (n < 0)
        return 0;
    if (n < 1)
        n = 1;
    g = GlobalAlloc(GMEM_MOVEABLE | GMEM_SHARE | GMEM_ZEROINIT,
                    sizeof(DWPHDR) + (DWORD)n * sizeof(DWPENT));
    if (!g)
        return 0;
    p = (DWPHDR __far *)GlobalLock(g);
    p->magic = DWP_MAGIC;
    p->count = 0;
    p->room = n;
    GlobalUnlock(g);
    return (HDWP)g;
}

HDWP WINAPI DeferWindowPos(HDWP d, HWND h, HWND after, int x, int y, int cx, int cy, UINT flags)
{
    DWPHDR __far *p = dwp_lock(d);
    DWPENT __far *e;
    WORD i;
    if (!p)
        return 0;
    if (!valid_wnd(h)) {
        GlobalUnlock((HGLOBAL)d);
        return 0;
    }
    e = (DWPENT __far *)(p + 1);
    for (i = 0; i < p->count && e[i].h != h; i++)
        ;
    if (i == p->count) {
        if (p->count == p->room) {
            HGLOBAL g;
            WORD room = p->room * 2;
            GlobalUnlock((HGLOBAL)d);
            g = GlobalReAlloc((HGLOBAL)d, sizeof(DWPHDR) + (DWORD)room * sizeof(DWPENT),
                              GMEM_MOVEABLE | GMEM_ZEROINIT);
            if (!g) {
                /* the whole sequence fails (3.1 SDK) */
                GlobalFree((HGLOBAL)d);
                return 0;
            }
            d = (HDWP)g;
            p = (DWPHDR __far *)GlobalLock(g);
            p->room = room;
            e = (DWPENT __far *)(p + 1);
        }
        p->count++;
    }
    e[i].h = h;
    e[i].after = after;
    e[i].x = x;
    e[i].y = y;
    e[i].cx = cx;
    e[i].cy = cy;
    e[i].flags = flags;
    GlobalUnlock((HGLOBAL)d);
    return d;
}

BOOL WINAPI EndDeferWindowPos(HDWP d)
{
    DWPHDR __far *p = dwp_lock(d);
    WORD i, n;
    if (!p)
        return FALSE;
    n = p->count;
    GlobalUnlock((HGLOBAL)d);
    for (i = 0; i < n; i++) {
        DWPENT e;
        p = (DWPHDR __far *)GlobalLock((HGLOBAL)d);
        e = ((DWPENT __far *)(p + 1))[i];
        GlobalUnlock((HGLOBAL)d);
        if (valid_wnd(e.h))
            SetWindowPos(e.h, e.after, e.x, e.y, e.cx, e.cy, e.flags);
    }
    GlobalFree((HGLOBAL)d);
    return TRUE;
}
