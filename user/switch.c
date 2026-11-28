/*
 * switch.c - switching windows from the keyboard: Alt+Tab, Alt+Shift+Tab,
 * Alt+Esc, Alt+Shift+Esc, and the switch window (class #32771).
 *
 * Observed on Windows 3.1 with probe US43 (tests/apps/us43.c: five tasks'
 * windows logging every message, real keys from tests/inputrun.sh;
 * boot-test case alttab; ARCHITECTURE.md 10.4):
 * - The key reaches the focus window as WM_SYSKEYDOWN; DefWindowProc sends
 *   its top-level window WM_SYSCOMMAND SC_PREVWINDOW (Shift up) or
 *   SC_NEXTWINDOW (Shift down) with lParam the key, VK_TAB or VK_ESCAPE;
 *   DefWindowProc's SC_PREVWINDOW/SC_NEXTWINDOW (after WH_CBT's
 *   HCBT_SYSCOMMAND) runs a loop in that task until Alt is let go: the
 *   window has the capture, every message goes through WH_MSGFILTER with
 *   MSGF_NEXTWINDOW (the Tab's own WM_SYSCHAR too), keys and the mouse are
 *   kept from the windows, other messages are dispatched (other tasks run
 *   meanwhile). No window gets anything else until the end.
 * - Alt+Tab with WIN.INI [windows] CoolSwitch=1 (the default; Control
 *   Panel's "Fast Alt+Tab Switching", SPI_GETFASTTASKSWITCH): the switch
 *   window, 400 x 80 in the middle of the screen, shows the next window's
 *   class icon and title; Tab goes on, Shift+Tab back, Esc gives up (when
 *   it is let go), any other key gives up and is left for the window.
 *   With CoolSwitch=0 the same keys show the window itself: its frame is
 *   drawn active (an icon's title highlighted) and, as the switch window,
 *   over the part of it other windows cover, with its class background
 *   for its client area; the active window is drawn inactive meanwhile.
 * - The ring: the top-level windows without an owner, in Z order from the
 *   active window's (its owner's) onwards, round the end and back to it:
 *   visible ones whose last active popup is enabled; icons too. The box
 *   shows the owner, the activation goes to its last active popup.
 * - Alt let go: SetWindowPos(window, HWND_TOP, SWP_NOMOVE | SWP_NOSIZE),
 *   which activates it (the window itself again when nothing else was
 *   chosen); an icon then gets a posted WM_SYSCOMMAND SC_RESTORE.
 * - Alt+Esc: the same loop without the box; let go, the next window comes
 *   to the top and is activated and the active window's group goes to the
 *   bottom (icons stay icons: their titles are highlighted). Alt+Shift+Esc
 *   activates the bottom window, sending nothing down.
 * - The window shown destroyed meanwhile: the box stays as it was; Tab
 *   then starts again from the top of the Z order; Alt let go with it
 *   gone keeps the active window.
 * - With a system-modal window up, nothing (the keys are the box's).
 * The switch window exists from the start, hidden, above all other
 * windows (WM_WINDOWPOSCHANGING's hwndInsertAfter is it for a window
 * brought to the top: pos.c); w16 links it into the Z order only while
 * it shows (switch_init).
 */
#include "user.h"

HWND hwndSwitch;                /* #32771 */
BYTE switch_active;             /* the loop runs */
BYTE switch_activating;         /* its activation (input.c) */
static int coolswitch = -1;     /* WIN.INI CoolSwitch, once read */
static HWND shown;              /* what the switch window shows */
static HWND drawn;              /* CoolSwitch=0: the window drawn active */
static BOOL cool;               /* this loop's kind of display */
extern BYTE __far switch_ended;       /* input.c: the Alt key's release after it */
extern HWND pos_companion, pos_companion_after;     /* pos.c: Alt+Esc's old window */

#define BOX_CX 400
#define BOX_CY 80

int fast_switch(void)
{
    if (coolswitch < 0)
        coolswitch = GetProfileInt("windows", "CoolSwitch", 1) != 0;
    return coolswitch;
}

void set_fast_switch(BOOL on)
{
    coolswitch = on != 0;
}

/* the switch window, made at start-up, hidden. 3.1 keeps it hidden above
   every window (a window brought up reports it as hwndInsertAfter: pos.c);
   w16 keeps it out of the Z order while hidden: a hidden window first in
   it changed what Write, Program Manager and Write's font dialog drew
   (boot-test cases write, mdimax, writefonts) */
void switch_init(void)
{
    hwndSwitch = create_window(WS_EX_TOPMOST, MAKEINTRESOURCE(32771), NULL, WS_POPUP,
                               0, 0, 0, 0, 0, 0, hInstUser, NULL);
    if (valid_wnd(hwndSwitch))
        unlink_window(PW(hwndSwitch));
}

/* the window that stands for H in the ring: its top-level owner */
static HWND root_of(HWND h)
{
    PWND w;
    h = top_level(h);
    while ((w = valid_wnd(h)) != 0 && w->hwndOwner && valid_wnd(w->hwndOwner))
        h = w->hwndOwner;
    return valid_wnd(h) ? h : 0;
}

/* what is activated for a ring window: its last active popup */
static HWND popup_of(HWND h)
{
    HWND p = GetLastActivePopup(h);
    PWND w = valid_wnd(p);
    return w && (w->style & WS_VISIBLE) && !(w->style & WS_DISABLED) ? p : h;
}

static BOOL in_ring(HWND t)
{
    PWND w = valid_wnd(t);
    PWND p;
    if (!w || t == hwndSwitch || w->hwndParent != hwndDesktop || w->hwndOwner ||
        !(w->style & WS_VISIBLE) || ((PCLS)w->cls)->atom == 32772)
        return FALSE;
    p = valid_wnd(GetLastActivePopup(t));
    if (!p)
        p = w;
    return !(p->style & WS_DISABLED);
}

/* the next ring window after C in Z order, round the end (C itself when
   there is no other; a C gone starts from the top) */
static HWND ring_next(HWND c)
{
    HWND t, first = PW(hwndDesktop)->hwndChild;
    int n;
    if (!valid_wnd(c))
        c = 0;
    t = c ? PW(c)->hwndNext : first;
    for (n = 0; n < 2000; n++) {
        if (!t) {
            if (!c)
                return 0;
            t = first;
        }
        if (t == c)
            return in_ring(c) ? c : 0;
        if (in_ring(t))
            return t;
        t = PW(t)->hwndNext;
    }
    return 0;
}

/* the ring window before C: the last one found going round from it */
static HWND ring_prev(HWND c)
{
    HWND t, first = PW(hwndDesktop)->hwndChild, last = 0;
    int n;
    if (!valid_wnd(c))
        c = 0;
    t = c ? PW(c)->hwndNext : first;
    for (n = 0; n < 2000; n++) {
        if (!t) {
            if (!c)
                break;
            t = first;
        }
        if (t == c)
            break;
        if (in_ring(t))
            last = t;
        t = PW(t)->hwndNext;
    }
    return last ? last : (c && in_ring(c) ? c : 0);
}

/* drawn active or not, whatever it is (an icon: its title) */
static void draw_as(HWND h, BOOL active)
{
    PWND w = valid_wnd(h);
    if (!w)
        return;
    if (active)
        w->flags |= WF_ACTIVEFRAME;
    else
        w->flags &= ~WF_ACTIVEFRAME;
    if (!is_visible(w))
        return;
    if (w->style & WS_MINIMIZE)
        redraw_icon_title(h);
    else
        nc_paint(h, 0, active);
}

static void box_hide(void)
{
    if (valid_wnd(hwndSwitch) && (PW(hwndSwitch)->style & WS_VISIBLE))
        set_window_pos(hwndSwitch, 0, 0, 0, 0, 0, SWP_HIDEWINDOW | SWP_NOMOVE | SWP_NOSIZE |
                       SWP_NOZORDER | SWP_NOACTIVATE);
    if (valid_wnd(hwndSwitch))
        unlink_window(PW(hwndSwitch));
}

/* the switch window over RC, on top, its paint this task's */
static void box_place(const RECT *rc)
{
    PWND s = valid_wnd(hwndSwitch);
    if (!s)
        return;
    s->hq = cur_hq();
    set_window_pos(hwndSwitch, HWND_TOP, rc->left, rc->top, rc->right - rc->left, rc->bottom - rc->top,
                   SWP_SHOWWINDOW | SWP_NOACTIVATE);
    if (valid_wnd(hwndSwitch))
        RedrawWindow(hwndSwitch, NULL, 0, RDW_INVALIDATE | RDW_ERASE | RDW_UPDATENOW);
}

/* show ring window C: the box, or (CoolSwitch=0) the window itself */
static void show(HWND c)
{
    RECT rc;
    shown = c;
    if (cool) {
        SetRect(&rc, (sysmet[SM_CXSCREEN] - BOX_CX) / 2, (sysmet[SM_CYSCREEN] - BOX_CY) / 2, 0, 0);
        rc.right = rc.left + BOX_CX;
        rc.bottom = rc.top + BOX_CY;
        box_hide();
        box_place(&rc);
        return;
    }
    {
        HWND p = popup_of(c), s;
        PWND w = valid_wnd(p);
        box_hide();
        if (drawn && drawn != p)
            draw_as(drawn, FALSE);
        drawn = p;
        if (!w)
            return;
        /* the part of it other windows cover */
        SetRectEmpty(&rc);
        if (!(w->style & WS_MINIMIZE))
            for (s = PW(hwndDesktop)->hwndChild; s && s != p; s = PW(s)->hwndNext) {
                RECT i;
                if (s == hwndSwitch || !(PW(s)->style & WS_VISIBLE))
                    continue;
                if (IntersectRect(&i, &PW(s)->rcWindow, &w->rcWindow))
                    UnionRect(&rc, &rc, &i);
            }
        draw_as(p, TRUE);
        box_place(&rc);
    }
}

/* the switch window's drawing */
static void box_paint(HDC hdc)
{
    PWND v = valid_wnd(shown), s = PW(hwndSwitch);
    if (!v)
        return;                         /* gone: the box stays as it was */
    if (cool) {
        RECT r;
        HICON icon = ((PCLS)v->cls)->hIcon;
        char text[80];
        int n, x;
        HFONT of;
        SetRect(&r, 0, 0, BOX_CX, BOX_CY);
        FillRect(hdc, &r, sysbrush[COLOR_BTNFACE]);
        FrameRect(hdc, &r, sysbrush[COLOR_WINDOWFRAME]);
        /* the inset: shadow above and left, highlight below and right */
        SetRect(&r, 4, 4, BOX_CX - 5, 5);
        FillRect(hdc, &r, sysbrush[COLOR_BTNSHADOW]);
        SetRect(&r, 4, 4, 5, BOX_CY - 5);
        FillRect(hdc, &r, sysbrush[COLOR_BTNSHADOW]);
        SetRect(&r, 4, BOX_CY - 5, BOX_CX - 4, BOX_CY - 4);
        FillRect(hdc, &r, sysbrush[COLOR_BTNHIGHLIGHT]);
        SetRect(&r, BOX_CX - 5, 4, BOX_CX - 4, BOX_CY - 4);
        FillRect(hdc, &r, sysbrush[COLOR_BTNHIGHLIGHT]);
        /* the class icon (UNVERIFIED: a window without one) */
        if (!icon)
            icon = (HICON)send_message(shown, WM_QUERYDRAGICON, 0, 0);
        if (!icon)
            icon = LoadIcon(0, IDI_APPLICATION);
        if ((v = valid_wnd(shown)) == 0)
            return;
        DrawIcon(hdc, 40, 24, icon);
        /* the title in the system font, centred on the box from 104 to
           296, from 104 when longer, cut at 391 (3.1: "US43 C", 48 pixels,
           at 176; a title of 378 pixels from 104, its last letter cut) */
        def_gettext(v, sizeof text, text);
        n = lstr_len(text);
        of = SelectObject(hdc, hfontSys);
        x = 104 + (192 - (int)LOWORD(GetTextExtent(hdc, text, n))) / 2;
        if (x < 104)
            x = 104;
        SetRect(&r, 104, 32, BOX_CX - 9, 48);
        SetBkColor(hdc, syscolor[COLOR_BTNFACE]);
        SetTextColor(hdc, syscolor[COLOR_BTNTEXT]);
        ExtTextOut(hdc, x, 32, ETO_CLIPPED | ETO_OPAQUE, &r, text, n, NULL);
        SelectObject(hdc, of);
        return;
    }
    /* CoolSwitch=0: the shown window's frame, active, and its client
       area's background, where the switch window is over it */
    {
        PWND p = valid_wnd(drawn);
        RECT cr;
        HBRUSH br;
        POINT org;
        if (!p || (p->style & WS_MINIMIZE))
            return;
        org.x = p->rcWindow.left - s->rcWindow.left;
        org.y = p->rcWindow.top - s->rcWindow.top;
        SaveDC(hdc);
        SetViewportOrg(hdc, org.x, org.y);
        cr = p->rcClient;
        OffsetRect(&cr, -p->rcWindow.left, -p->rcWindow.top);
        br = ((PCLS)p->cls)->hbrBackground;
        if ((WORD)br && (WORD)br <= COLOR_BTNHIGHLIGHT + 1)
            br = sysbrush[(WORD)br - 1];
        if (br) {
            UnrealizeObject(br);
            SetBrushOrg(hdc, p->rcClient.left, p->rcClient.top);
            FillRect(hdc, &cr, br);
        }
        nc_draw(hdc, p, TRUE);
        RestoreDC(hdc, -1);
    }
}

LRESULT WINAPI __loadds SwitchWndProc(HWND h, UINT msg, WPARAM wp, LPARAM lp)
{
    switch (msg) {
    case WM_PAINT: {
        PAINTSTRUCT ps;
        HDC hdc = BeginPaint(h, &ps);
        box_paint(hdc);
        EndPaint(h, &ps);
        return 0;
    }
    case WM_ERASEBKGND:
        return 1;
    case WM_NCHITTEST:
        return HTNOWHERE;
    }
    return DefWindowProc(h, msg, wp, lp);
}

/* activate ring window T (its last active popup), bringing it up; an
   icon chosen by Alt+Tab is then restored */
static void switch_to(HWND t, BOOL restore)
{
    HWND p = popup_of(t);
    if (!valid_wnd(p))
        return;
    switch_activating++;
    SetWindowPos(p, HWND_TOP, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE);
    switch_activating--;
    if (restore && IsIconic(p))
        PostMessage(p, WM_SYSCOMMAND, SC_RESTORE, 0);
}

/* DefWindowProc's SC_PREVWINDOW and SC_NEXTWINDOW for top-level window H */
void switch_windows(HWND h, WPARAM cmd, LPARAM lp)
{
    BOOL tab = LOWORD(lp) == VK_TAB, back = (cmd & 0xFFF0) == SC_NEXTWINDOW;
    BOOL alt_down = GetKeyState(VK_MENU) < 0, esc_down = GetKeyState(VK_ESCAPE) < 0;
    BOOL cancel = FALSE;
    HWND cur = root_of(hwndActive ? hwndActive : h), cand = 0, t;
    MSG m;
    if (GetSysModalWindow() || !valid_wnd(h))
        return;
    if (!LOWORD(lp))                    /* sent by a program: at once */
        alt_down = esc_down = FALSE;
    cool = fast_switch();
    drawn = 0;
    if (tab) {
        cand = back ? ring_prev(cur) : ring_next(cur);
        if (!cool && hwndActive)
            drawn = hwndActive;         /* drawn inactive from now on */
        if (cand)
            show(cand);
    }
    switch_active++;
    SetCapture(h);
    while (alt_down || esc_down) {
        if (!PeekMessage(&m, 0, 0, 0, PM_NOREMOVE)) {
            /* nothing queued and Alt up (its message went elsewhere) */
            if (!(keystate[VK_MENU] & 0x80) && !(keystate[VK_ESCAPE] & 0x80))
                break;
            WaitMessage();
            continue;
        }
        if (m.message == WM_QUIT)
            break;
        if ((m.message == WM_KEYDOWN || m.message == WM_SYSKEYDOWN) && m.wParam != VK_TAB &&
            m.wParam != VK_ESCAPE && m.wParam != VK_SHIFT && m.wParam != VK_MENU) {
            /* another key: given up, the key left for the window (3.1:
               F11 with the box up, then WM_SYSKEYDOWN 7Ah to the window) */
            if (CallMsgFilter(&m, MSGF_NEXTWINDOW)) {
                PeekMessage(&m, 0, m.message, m.message, PM_REMOVE | PM_NOYIELD);
                continue;
            }
            cancel = TRUE;
            break;
        }
        if (!PeekMessage(&m, 0, 0, 0, PM_REMOVE | PM_NOYIELD))
            continue;
        /* (USER's own timer, the caret's, goes by the hook: 3.1, US43) */
        if (m.message != WM_SYSTIMER && CallMsgFilter(&m, MSGF_NEXTWINDOW))
            continue;
        switch (m.message) {
        case WM_KEYDOWN:
        case WM_SYSKEYDOWN:
            if (m.wParam == VK_TAB && tab && !cancel) {
                cand = GetKeyState(VK_SHIFT) < 0 ? ring_prev(cand) : ring_next(cand);
                if (cand)
                    show(cand);
            } else if (m.wParam == VK_ESCAPE) {
                esc_down = TRUE;
                if (tab)
                    cancel = TRUE;      /* when it is let go */
            }
            break;
        case WM_KEYUP:
        case WM_SYSKEYUP:
            if (m.wParam == VK_MENU)
                alt_down = FALSE;
            else if (m.wParam == VK_ESCAPE) {
                esc_down = FALSE;
                if (cancel)
                    alt_down = FALSE;
            }
            break;
        default:
            if ((m.message >= WM_KEYFIRST && m.message <= WM_KEYLAST) ||
                (m.message >= WM_MOUSEFIRST && m.message <= WM_MOUSELAST) ||
                (m.message >= WM_NCMOUSEMOVE && m.message <= WM_NCMBUTTONDBLCLK))
                break;
            DispatchMessage(&m);
        }
    }
    if (m.message == WM_QUIT)
        PostQuitMessage(m.wParam);
    if (GetCapture() == h)
        ReleaseCapture();
    switch_active--;
    switch_ended = 1;
    box_hide();
    if (valid_wnd(hwndSwitch))
        PW(hwndSwitch)->hq = 0;
    if (!cool && drawn) {
        /* the frames as they are again */
        t = drawn;
        drawn = 0;
        draw_as(t, FALSE);
        if (hwndActive && valid_wnd(hwndActive))
            draw_as(hwndActive, TRUE);
    }
    shown = 0;
    if (!valid_wnd(cur))
        cur = 0;
    if (tab) {
        if (cancel || !valid_wnd(cand))
            cand = cur;
        if (cand)
            switch_to(cand, cand != cur);
        return;
    }
    /* Alt+Esc: the next one up, the active one's group down; Alt+Shift+Esc:
       the bottom one up */
    t = back ? ring_prev(cur) : ring_next(cur);
    if (!t || t == cur)
        return;
    /* (3.1, US43: the next window's WM_WINDOWPOSCHANGING, the active
       one's and its group's going down, the activation, then their
       WM_WINDOWPOSCHANGED in that order; Alt+Shift+Esc: the active one
       under the new one's group, with no WM_WINDOWPOSCHANGED: pos.c) */
    /* (Alt+Shift+Esc from an owned popup moved nothing: 3.1, US43, from
       C's popup Cp to B; from A to Cp, A got WM_WINDOWPOSCHANGING) */
    if (cur && (!back || !GetWindow(popup_of(cur), GW_OWNER))) {
        pos_companion = popup_of(cur);
        pos_companion_after = back ? HWND_TOP : HWND_BOTTOM;
    }
    switch_to(t, FALSE);
    pos_companion = 0;
}

/* the keys with no window active (the active program ended and none took
   over): the next window at once */
void switch_window(HWND cur, BOOL tobottom)
{
    HWND t = ring_next(cur ? root_of(cur) : 0);
    (void)tobottom;
    if (t)
        switch_to(t, FALSE);
}
