/*
 * win.c - windows: creation, destruction, the window tree, window data.
 *
 * Each window is a WND in USER's local heap followed by its cbWndExtra
 * bytes; the HWND is its near offset (Pietrek, Windows Internals, ch. 4).
 * Siblings are linked through hwndNext in Z order, topmost first; the
 * desktop is the root of the top-level windows.
 */
#include "user.h"

HWND hwndDesktop;

/* CS_BYTEALIGNCLIENT: the client area's left edge goes to the nearest
   multiple of 8 on screen (probe US5: asked for x 10 and 333, Windows 3.1
   put such windows at 12 and 332, their client areas at 16 and 336) */
void byte_align(PWND w)
{
    int dx;
    WORD cs = ((PCLS)w->cls)->style;
    /* not an icon (3.1: Notepad's, CS_BYTEALIGNCLIENT, minimised at 21
       like the rest; w16 had it at 24) */
    if (!(cs & (CS_BYTEALIGNCLIENT | CS_BYTEALIGNWINDOW)) || (w->style & (WS_CHILD | WS_MINIMIZE)))
        return;
    /* CS_BYTEALIGNWINDOW: the window's left edge (dialogs, #32770: US4's
       message boxes are at 136, 168, 120 where centring gives 137, 169,
       122) */
    /* nearest multiple of 8 also left of the screen: (x + 4) & ~7 (C's
       division rounds towards zero, which put a window dragged partly off
       the left edge 8 pixels right of where Windows 3.1 puts it) */
    if (cs & CS_BYTEALIGNCLIENT)
        dx = ((w->rcClient.left + 4) & ~7) - w->rcClient.left;
    else
        dx = ((w->rcWindow.left + 4) & ~7) - w->rcWindow.left;
    if (dx) {
        OffsetRect(&w->rcWindow, dx, 0);
        OffsetRect(&w->rcClient, dx, 0);
    }
}
void free_place(HWND h);
void free_props(PWND w);
void free_scroll(PWND w);
void purge_window_msgs(HQUEUE hq, HWND h);

PWND valid_wnd(HWND h)
{
    PWND w = PW(h);
    WORD sz;
    if (!h || ((WORD)h & 1))
        return 0;
    /* a live window is a local block of ours that points to a class */
    sz = LocalSize((HLOCAL)h);
    if (sz < sizeof(WND) || LocalHandle((UINT)h) != (HLOCAL)h)
        return 0;
    if (!w->cls || (w->flags & 0x8000))
        return 0;
    return w;
}

HWND top_level(HWND h)
{
    PWND w = valid_wnd(h);
    while (w && (w->style & WS_CHILD) && w->hwndParent)
        w = PW(w->hwndParent);
    return (HWND)(WORD)w;
}

void unlink_window(PWND w)
{
    PWND p = PW(w->hwndParent);
    HWND __far *link;
    if (!p)
        return;
    for (link = &p->hwndChild; *link; link = &PW(*link)->hwndNext)
        if (*link == (HWND)(WORD)w) {
            *link = w->hwndNext;
            break;
        }
    w->hwndNext = 0;
}

/* after: HWND_TOP (0), HWND_BOTTOM (1) or a sibling */
void link_window(PWND w, HWND after)
{
    PWND p = PW(w->hwndParent);
    HWND __far *link;
    if (!p)
        return;
    if (after == HWND_TOP) {
        w->hwndNext = p->hwndChild;
        p->hwndChild = (HWND)(WORD)w;
        return;
    }
    for (link = &p->hwndChild; *link; link = &PW(*link)->hwndNext)
        if (after != HWND_BOTTOM && *link == after) {
            link = &PW(*link)->hwndNext;
            break;
        }
    w->hwndNext = *link;
    *link = (HWND)(WORD)w;
}

BOOL is_visible(PWND w)
{
    for (; w; w = PW(w->hwndParent))
        if (!(w->style & WS_VISIBLE))
            return FALSE;
    return TRUE;
}

/* window text: a block of the string heap (uheap.c; 3.1 keeps it out of
   DGROUP too: US15), WND+32 its handle, 0 for none (an empty text frees
   it, as 3.1's does) */
static void set_text(PWND w, LPCSTR s)
{
    if (w->text)
        uh_free(&uhStr, w->text);
    w->text = (s && *s) ? uh_strdup(s) : 0;
}

/* WM_GETMINMAXINFO's defaults (3.1 SDK; the maximised size covers the
   frame off screen) */
void minmax(PWND w, MINMAXINFO __far *mm)
{
    int cxf = sysmet[SM_CXFRAME], cyf = sysmet[SM_CYFRAME];
    if (!(w->style & WS_THICKFRAME)) {
        cxf = (w->style & WS_DLGFRAME) || (w->exstyle & WS_EX_DLGMODALFRAME) ?
              sysmet[SM_CXDLGFRAME] : sysmet[SM_CXBORDER];
        cyf = (w->style & WS_DLGFRAME) || (w->exstyle & WS_EX_DLGMODALFRAME) ?
              sysmet[SM_CYDLGFRAME] : sysmet[SM_CYBORDER];
    }
    mm->ptMaxSize.x = sysmet[SM_CXSCREEN] + 2 * cxf;
    mm->ptMaxSize.y = sysmet[SM_CYSCREEN] + 2 * cyf;
    mm->ptMaxPosition.x = -cxf;
    mm->ptMaxPosition.y = -cyf;
    mm->ptMinTrackSize.x = sysmet[SM_CXMINTRACK];
    mm->ptMinTrackSize.y = sysmet[SM_CYMINTRACK];
    mm->ptMaxTrackSize.x = mm->ptMaxSize.x;
    mm->ptMaxTrackSize.y = mm->ptMaxSize.y;
}

/* CW_USEDEFAULT positions: UNVERIFIED cascade (Windows 3.1 staggers new
   overlapped windows by the caption height; the exact sequence is to be
   observed) */
static int cascade;

HWND create_window(DWORD exstyle, LPCSTR clsname, LPCSTR title, DWORD style,
                   int x, int y, int cx, int cy, HWND parent, HMENU menu,
                   HINSTANCE hinst, void __far *param)
{
    PCLS c = find_class(clsname, hinst);
    PWND w, pw;
    HWND h;
    CREATESTRUCT cs;
    MINMAXINFO mm;
    LONG r;
    BOOL show = FALSE;
    int sw = SW_SHOW;

    if (!c) {
        ulog("CreateWindow: no class ");
        if (!ISATOM(clsname))
            ulog(clsname);
        ulogx("", LOWORD((DWORD)clsname));
        return 0;
    }
    pw = parent ? valid_wnd(parent) : (PWND)0;
    if ((style & WS_CHILD) && !pw)
        return 0;
    w = (PWND)LocalAlloc(LPTR, sizeof(WND) + c->cbWndExtra);
    if (!w)
        return 0;
    h = (HWND)(WORD)w;
    w->cls = (WORD)c;
    c->cwnd++;
    w->hInstance = hinst;
    w->lpfnWndProc = c->lpfnWndProc;
    w->exstyle = exstyle;
    w->hq = hwndDesktop ? cur_hq() : 0;
    if (!(style & WS_CHILD) && !(style & WS_POPUP))
        style |= WS_CAPTION | WS_CLIPSIBLINGS;  /* overlapped (3.1 SDK) */
    if (style & WS_CHILD) {
        w->hwndParent = parent;
        w->hMenu = (WORD)menu;
    } else {
        w->hwndParent = hwndDesktop;
        style |= WS_CLIPSIBLINGS;               /* top-level windows clip */
        if (pw)
            w->hwndOwner = top_level(parent);
        w->hMenu = (WORD)menu;
        if (!menu && c->lpszMenuName)
            w->hMenu = (WORD)LoadMenu(hinst, class_menu_name(c));
    }
    if ((style & WS_VISIBLE) && hwndDesktop) {
        show = TRUE;
        style &= ~WS_VISIBLE;
    }
    w->style = style;
    if (title && !ISATOM(title))
        set_text(w, title);

    /* default position and size */
    if (!(style & (WS_CHILD | WS_POPUP))) {
        if (x == CW_USEDEFAULT) {
            int step = sysmet[SM_CYCAPTION] + sysmet[SM_CYFRAME];
            x = y = (cascade++ % 8) * step;
            if (y + step > sysmet[SM_CYSCREEN] / 2)
                cascade = 0;
            if (cy != CW_USEDEFAULT && cy)
                sw = cy;                /* y of CW_USEDEFAULT: the show command */
            if (cx == CW_USEDEFAULT) {
                /* the screen's width less two frames, down to the row of
                   icons (Notepad started first on 3.1: 632x408 at 0,0 on
                   640x480, then moved to x 4 by CS_BYTEALIGNCLIENT;
                   UNVERIFIED for later, cascaded ones) */
                cx = sysmet[SM_CXSCREEN] - x - 2 * sysmet[SM_CXFRAME];
                cy = sysmet[SM_CYSCREEN] - y - sysmet[SM_CYICONSPACING];
            }
        } else if (cx == CW_USEDEFAULT) {
            cx = sysmet[SM_CXSCREEN] * 3 / 4;
            cy = sysmet[SM_CYSCREEN] * 3 / 4;
        }
    } else {
        if (x == CW_USEDEFAULT)
            x = y = 0;
        if (cx == CW_USEDEFAULT)
            cx = cy = 0;
    }
    if (pw && (style & WS_CHILD)) {
        x += pw->rcClient.left;
        y += pw->rcClient.top;
    }
    SetRect(&w->rcWindow, x, y, x + cx, y + cy);
    w->rcClient = w->rcWindow;
    if (w->hwndParent)
        link_window(w, (style & WS_CHILD) ? HWND_BOTTOM : HWND_TOP);
    else if (!hwndDesktop)
        hwndDesktop = h;

    /* WM_GETMINMAXINFO for windows that can be sized (3.1 sends it for
       overlapped and thick-frame windows) */
    if (!(style & WS_CHILD) || (style & WS_THICKFRAME)) {
        minmax(w, &mm);
        send_message(h, WM_GETMINMAXINFO, 0, (LPARAM)(MINMAXINFO __far *)&mm);
        if (cx > mm.ptMaxTrackSize.x)
            cx = mm.ptMaxTrackSize.x;
        if (cy > mm.ptMaxTrackSize.y)
            cy = mm.ptMaxTrackSize.y;
        if (cx < 0)
            cx = 0;
        if (cy < 0)
            cy = 0;
        w->rcWindow.right = w->rcWindow.left + cx;
        w->rcWindow.bottom = w->rcWindow.top + cy;
    }

    cs.lpCreateParams = param;
    cs.hInstance = hinst;
    cs.hMenu = menu;
    cs.hwndParent = parent;
    cs.cy = cy;
    cs.cx = cx;
    cs.y = (style & WS_CHILD) && pw ? y - pw->rcClient.top : y;
    cs.x = (style & WS_CHILD) && pw ? x - pw->rcClient.left : x;
    cs.style = style;
    cs.lpszName = title;
    cs.lpszClass = clsname;
    cs.dwExStyle = exstyle;

    r = send_message(h, WM_NCCREATE, 0, (LPARAM)(CREATESTRUCT __far *)&cs);
    if (!valid_wnd(h))
        return 0;
    if (!r) {
        destroy_window(h);
        return 0;
    }
    calc_client(w);
    byte_align(w);
    r = send_message(h, WM_CREATE, 0, (LPARAM)(CREATESTRUCT __far *)&cs);
    if (!valid_wnd(h))
        return 0;
    if (r == -1) {
        destroy_window(h);
        return 0;
    }
    w->flags |= WF_CREATED;
    /* a window made invisible gets WM_SIZE and WM_MOVE when it is first
       shown (3.1 trace: Notepad's main window got none in CreateWindow;
       its WM_SIZE handler places its edit control, which did not exist
       yet, so w16's early WM_SIZE left the edit at 0,0 instead of 8,2) */
    if (!(style & WS_VISIBLE))
        w->flags |= WF_SENDSIZE;
    else {
        send_message(h, WM_SIZE, SIZE_RESTORED,
                     MAKELONG(w->rcClient.right - w->rcClient.left,
                              w->rcClient.bottom - w->rcClient.top));
        {
            int mx = w->rcClient.left, my = w->rcClient.top;
            if ((style & WS_CHILD) && pw) {
                mx -= pw->rcClient.left;
                my -= pw->rcClient.top;
            }
            send_message(h, WM_MOVE, 0, MAKELONG(mx, my));
        }
    }
    if ((style & WS_CHILD) && !(exstyle & WS_EX_NOPARENTNOTIFY) && pw)
        send_message(parent, WM_PARENTNOTIFY, WM_CREATE, MAKELONG(h, w->hMenu));
    if (show && valid_wnd(h)) {
        if (style & WS_MINIMIZE)
            sw = SW_SHOWMINIMIZED;
        else if (style & WS_MAXIMIZE)
            sw = SW_SHOWMAXIMIZED;
        ShowWindow(h, sw);
    }
    return valid_wnd(h) ? h : 0;
}

HWND WINAPI CreateWindow(LPCSTR cls, LPCSTR title, DWORD style, int x, int y,
                         int cx, int cy, HWND parent, HMENU menu, HINSTANCE hinst,
                         void __far *param)
{
    return create_window(0, cls, title, style, x, y, cx, cy, parent, menu, hinst, param);
}

HWND WINAPI CreateWindowEx(DWORD ex, LPCSTR cls, LPCSTR title, DWORD style, int x, int y,
                           int cx, int cy, HWND parent, HMENU menu, HINSTANCE hinst,
                           void __far *param)
{
    return create_window(ex, cls, title, style, x, y, cx, cy, parent, menu, hinst, param);
}

static void free_window(PWND w)
{
    HWND h = (HWND)(WORD)w;
    kill_window_timers(h);
    if (w->hrgnUpdate) {
        if ((WORD)w->hrgnUpdate > 1)
            DeleteObject(w->hrgnUpdate);
        w->hrgnUpdate = 0;
    }
    if (w->flags & WF_INPAINT) {
        w->flags &= ~WF_INPAINT;
        dec_paint(w->hq);
    }
    if (w->text)
        uh_free(&uhStr, w->text);
    if (!(w->style & WS_CHILD) && w->hMenu)
        DestroyMenu((HMENU)w->hMenu);
    if (w->hSysMenu)
        DestroyMenu((HMENU)w->hSysMenu);
    free_props(w);
    free_scroll(w);
    ((PCLS)w->cls)->cwnd--;
    w->cls = 0;
    w->flags |= 0x8000;
    LocalFree((HLOCAL)w);
}

static void destroy_tree(HWND h)
{
    PWND w = valid_wnd(h);
    HWND c;
    if (!w)
        return;
    w->flags |= WF_DESTROYING;
    clip_window_destroyed(h);       /* clip.c: WM_RENDERALLFORMATS first */
    {
        void savebits_forget(HWND h);
        savebits_forget(h);
    }
    send_message(h, WM_DESTROY, 0, 0);
    while ((w = valid_wnd(h)) != 0 && (c = w->hwndChild) != 0) {
        PWND cw = valid_wnd(c);
        if (!cw)
            break;
        destroy_tree(c);
        if (valid_wnd(c)) {             /* still there: unlink it anyway */
            unlink_window(PW(c));
        }
    }
    if ((w = valid_wnd(h)) == 0)
        return;
    free_place(h);
    send_message(h, WM_NCDESTROY, 0, 0);
    if ((w = valid_wnd(h)) == 0)
        return;
    purge_window_msgs(w->hq, h);
    unlink_window(w);
    free_window(w);
}

BOOL destroy_window(HWND h)
{
    PWND w = valid_wnd(h), p;
    HWND parent, owned, n;
    RECT rc;
    BOOL wasvis;
    if (!w || h == hwndDesktop)
        return FALSE;
    if (w->flags & WF_DESTROYING)
        return TRUE;
    p = PW(w->hwndParent);
    parent = w->hwndParent;
    if ((w->style & WS_CHILD) && !(w->exstyle & WS_EX_NOPARENTNOTIFY) && p)
        send_message(parent, WM_PARENTNOTIFY, WM_DESTROY, MAKELONG(h, w->hMenu));
    if (!(w = valid_wnd(h)))
        return TRUE;
    /* owned windows go first */
    if (!(w->style & WS_CHILD)) {
        for (owned = PW(hwndDesktop)->hwndChild; owned; owned = n) {
            n = PW(owned)->hwndNext;
            if (PW(owned)->hwndOwner == h) {
                destroy_window(owned);
                n = PW(hwndDesktop)->hwndChild;
            }
        }
    }
    if (!(w = valid_wnd(h)))
        return TRUE;
    wasvis = is_visible(w);
    rc = w->rcWindow;
    if (wasvis)
        set_window_pos(h, 0, 0, 0, 0, 0, SWP_HIDEWINDOW | SWP_NOACTIVATE | SWP_NOMOVE |
                       SWP_NOSIZE | SWP_NOZORDER);
    if (!(w = valid_wnd(h)))
        return TRUE;
    if (hwndCapture && (h == hwndCapture || IsChild(h, hwndCapture)))
        ReleaseCapture();       /* the input queue goes back with it */
    destroy_tree(h);
    if (hwndFocus && !valid_wnd(hwndFocus))
        hwndFocus = 0;
    if (hwndActive && !valid_wnd(hwndActive)) {
        hwndActive = 0;
        /* activate the next top-level window */
        {
            HWND t;
            for (t = PW(hwndDesktop)->hwndChild; t; t = PW(t)->hwndNext) {
                PWND tw = PW(t);
                if ((tw->style & WS_VISIBLE) && !(tw->style & WS_DISABLED) &&
                    !(tw->exstyle & WS_EX_TOPMOST && 0)) {
                    activate_window(t, FALSE);
                    break;
                }
            }
        }
    }
    (void)rc;
    return TRUE;
}

BOOL WINAPI DestroyWindow(HWND h)
{
    return destroy_window(h);
}

BOOL WINAPI IsWindow(HWND h)
{
    return valid_wnd(h) != 0;
}

HWND WINAPI GetDesktopWindow(void)
{
    return hwndDesktop;
}

HWND WINAPI GetParent(HWND h)
{
    PWND w = valid_wnd(h);
    if (!w)
        return 0;
    if (w->style & WS_CHILD)
        return w->hwndParent;
    return w->hwndOwner;
}

HWND WINAPI SetParent(HWND h, HWND parent)
{
    PWND w = valid_wnd(h);
    HWND old;
    BOOL vis;
    if (!w)
        return 0;
    if (!parent)
        parent = hwndDesktop;
    if (!valid_wnd(parent))
        return 0;
    old = w->hwndParent;
    vis = (w->style & WS_VISIBLE) != 0;
    if (vis)
        ShowWindow(h, SW_HIDE);
    {
        int dx = PW(parent)->rcClient.left - PW(old)->rcClient.left;
        int dy = PW(parent)->rcClient.top - PW(old)->rcClient.top;
        unlink_window(w);
        w->hwndParent = parent;
        link_window(w, HWND_TOP);
        OffsetRect(&w->rcWindow, dx, dy);
        OffsetRect(&w->rcClient, dx, dy);
    }
    if (vis)
        ShowWindow(h, SW_SHOWNA);
    return old;
}

BOOL WINAPI IsChild(HWND parent, HWND h)
{
    PWND w = valid_wnd(h);
    while (w && (w->style & WS_CHILD)) {
        if (w->hwndParent == parent)
            return TRUE;
        w = valid_wnd(w->hwndParent);
    }
    return FALSE;
}

HWND WINAPI GetWindow(HWND h, UINT cmd)
{
    PWND w = valid_wnd(h);
    HWND r;
    if (!w)
        return 0;
    switch (cmd) {
    case GW_HWNDFIRST:
        return w->hwndParent ? PW(w->hwndParent)->hwndChild : 0;
    case GW_HWNDLAST:
        if (!w->hwndParent)
            return 0;
        for (r = (HWND)(WORD)w; PW(r)->hwndNext; r = PW(r)->hwndNext)
            ;
        return r;
    case GW_HWNDNEXT:
        return w->hwndNext;
    case GW_HWNDPREV:
        if (!w->hwndParent)
            return 0;
        r = PW(w->hwndParent)->hwndChild;
        if (r == h)
            return 0;
        while (r && PW(r)->hwndNext != h)
            r = PW(r)->hwndNext;
        return r;
    case GW_OWNER:
        return w->hwndOwner;
    case GW_CHILD:
        return w->hwndChild;
    }
    return 0;
}

HWND WINAPI GetNextWindow(HWND h, UINT cmd)
{
    return GetWindow(h, cmd == GW_HWNDPREV ? GW_HWNDPREV : GW_HWNDNEXT);
}

HWND WINAPI GetTopWindow(HWND h)
{
    PWND w = valid_wnd(h ? h : hwndDesktop);
    return w ? w->hwndChild : 0;
}

BOOL WINAPI IsWindowVisible(HWND h)
{
    PWND w = valid_wnd(h);
    return w && is_visible(w);
}

BOOL WINAPI IsWindowEnabled(HWND h)
{
    PWND w = valid_wnd(h);
    return w && !(w->style & WS_DISABLED);
}

BOOL WINAPI EnableWindow(HWND h, BOOL enable)
{
    PWND w = valid_wnd(h);
    BOOL was;
    if (!w)
        return FALSE;
    was = (w->style & WS_DISABLED) != 0;
    if (enable && was) {
        w->style &= ~WS_DISABLED;
        send_message(h, WM_ENABLE, TRUE, 0);
    } else if (!enable && !was) {
        send_message(h, WM_CANCELMODE, 0, 0);
        w->style |= WS_DISABLED;
        if (h == hwndFocus || IsChild(h, hwndFocus))
            set_focus(0);
        send_message(h, WM_ENABLE, FALSE, 0);
    }
    return was;
}

BOOL WINAPI IsIconic(HWND h)
{
    PWND w = valid_wnd(h);
    return w && (w->style & WS_MINIMIZE);
}

BOOL WINAPI IsZoomed(HWND h)
{
    PWND w = valid_wnd(h);
    return w && (w->style & WS_MAXIMIZE);
}

/* ---- window text ---- */
int WINAPI GetWindowText(HWND h, LPSTR buf, int n)
{
    if (!valid_wnd(h) || n <= 0)
        return 0;
    buf[0] = 0;
    return (int)send_message(h, WM_GETTEXT, n, (LPARAM)buf);
}

int WINAPI GetWindowTextLength(HWND h)
{
    if (!valid_wnd(h))
        return 0;
    return (int)send_message(h, WM_GETTEXTLENGTH, 0, 0);
}

void WINAPI SetWindowText(HWND h, LPCSTR s)
{
    if (valid_wnd(h))
        send_message(h, WM_SETTEXT, 0, (LPARAM)s);
}

/* DefWindowProc's part */
LONG def_gettext(PWND w, int n, LPSTR buf)
{
    if (n <= 0)
        return 0;
    if (!w->text) {
        buf[0] = 0;
        return 0;
    }
    lstr_cpyn(buf, (LPCSTR)UH_PTR(&uhStr, w->text), n);
    return lstr_len(buf);
}

LONG def_gettextlength(PWND w)
{
    return w->text ? lstr_len((LPCSTR)UH_PTR(&uhStr, w->text)) : 0;
}

void def_settext(PWND w, LPCSTR s)
{
    set_text(w, s);
}

/* ---- window words and longs ---- */
static void __far *extra_field(PWND w, int i, WORD size)
{
    if (i >= 0 && i + size <= (WORD)((PCLS)w->cls)->cbWndExtra)
        return WND_EXTRA(w) + i;
    return 0;
}

WORD WINAPI GetWindowWord(HWND h, int i)
{
    PWND w = valid_wnd(h);
    WORD __far *p;
    if (!w)
        return 0;
    switch (i) {
    case GWW_HINSTANCE: return (WORD)w->hInstance;
    case GWW_HWNDPARENT: return (w->style & WS_CHILD) ? (WORD)w->hwndParent : (WORD)w->hwndOwner;
    case GWW_ID: return w->hMenu;
    }
    p = extra_field(w, i, 2);
    return p ? *p : 0;
}

WORD WINAPI SetWindowWord(HWND h, int i, WORD v)
{
    PWND w = valid_wnd(h);
    WORD old, __far *p;
    if (!w)
        return 0;
    switch (i) {
    case GWW_HINSTANCE: old = (WORD)w->hInstance; w->hInstance = (HINSTANCE)v; return old;
    case GWW_ID: old = w->hMenu; w->hMenu = v; return old;
    case GWW_HWNDPARENT:
        if (w->style & WS_CHILD)
            return 0;
        old = (WORD)w->hwndOwner;
        w->hwndOwner = (HWND)v;
        return old;
    }
    p = extra_field(w, i, 2);
    if (!p)
        return 0;
    old = *p;
    *p = v;
    return old;
}

LONG WINAPI GetWindowLong(HWND h, int i)
{
    PWND w = valid_wnd(h);
    LONG __far *p;
    if (!w)
        return 0;
    switch (i) {
    case GWL_WNDPROC: return (LONG)w->lpfnWndProc;
    case GWL_STYLE: return w->style;
    case GWL_EXSTYLE: return w->exstyle;
    }
    p = extra_field(w, i, 4);
    return p ? *p : 0;
}

LONG WINAPI SetWindowLong(HWND h, int i, LONG v)
{
    PWND w = valid_wnd(h);
    LONG old, __far *p;
    if (!w)
        return 0;
    switch (i) {
    case GWL_WNDPROC: old = (LONG)w->lpfnWndProc; w->lpfnWndProc = (WNDPROC)v; return old;
    case GWL_STYLE: old = w->style; w->style = v; return old;
    case GWL_EXSTYLE: old = w->exstyle; w->exstyle = v; return old;
    }
    p = extra_field(w, i, 4);
    if (!p)
        return 0;
    old = *p;
    *p = v;
    return old;
}

/* ---- enumeration and search ---- */
BOOL WINAPI EnumWindows(WNDENUMPROC f, LPARAM lp)
{
    HWND h, n;
    for (h = PW(hwndDesktop)->hwndChild; h; h = n) {
        n = PW(h)->hwndNext;
        if (!((BOOL (__far __pascal *)(HWND, LPARAM))f)(h, lp))
            return FALSE;
        if (n && !valid_wnd(n))
            break;
    }
    return TRUE;
}

static BOOL enum_children(HWND parent, WNDENUMPROC f, LPARAM lp)
{
    HWND h, n;
    PWND w = valid_wnd(parent);
    if (!w)
        return TRUE;
    for (h = w->hwndChild; h; h = n) {
        n = PW(h)->hwndNext;
        if (!((BOOL (__far __pascal *)(HWND, LPARAM))f)(h, lp))
            return FALSE;
        if (!valid_wnd(h))
            break;
        if (!enum_children(h, f, lp))
            return FALSE;
        if (n && !valid_wnd(n))
            break;
    }
    return TRUE;
}

BOOL WINAPI EnumChildWindows(HWND parent, WNDENUMPROC f, LPARAM lp)
{
    return enum_children(parent, f, lp);
}

BOOL WINAPI EnumTaskWindows(HTASK t, WNDENUMPROC f, LPARAM lp)
{
    HWND h, n;
    HQUEUE hq = K_GETTASKQUEUE(t);
    for (h = PW(hwndDesktop)->hwndChild; h; h = n) {
        n = PW(h)->hwndNext;
        if (PW(h)->hq == hq && !((BOOL (__far __pascal *)(HWND, LPARAM))f)(h, lp))
            return FALSE;
    }
    return TRUE;
}

HWND WINAPI FindWindow(LPCSTR cls, LPCSTR title)
{
    HWND h;
    char buf[128];
    PCLS c = cls ? find_class(cls, 0) : (PCLS)0;
    ATOM a = 0;
    if (cls) {
        if (ISATOM(cls))
            a = LOWORD((DWORD)cls);
        else
            a = c ? c->atom : FindAtom(cls);
        if (!a)
            return 0;
    }
    for (h = PW(hwndDesktop)->hwndChild; h; h = PW(h)->hwndNext) {
        PWND w = PW(h);
        if (cls && ((PCLS)w->cls)->atom != a)
            continue;
        if (title) {
            def_gettext(w, sizeof buf, buf);
            if (lstrcmpi(buf, title))
                continue;
        }
        return h;
    }
    return 0;
}

HTASK WINAPI GetWindowTask(HWND h)
{
    PWND w = valid_wnd(h);
    return w && w->hq ? q_of(w->hq)->task : 0;
}

HWND WINAPI GetLastActivePopup(HWND h)
{
    PWND w = valid_wnd(h);
    if (!w)
        return 0;
    return w->hwndLastActive && valid_wnd(w->hwndLastActive) ? w->hwndLastActive : h;
}

/* ---- coordinates ---- */
void WINAPI ClientToScreen(HWND h, POINT __far *p)
{
    PWND w = valid_wnd(h);
    if (w) {
        p->x += w->rcClient.left;
        p->y += w->rcClient.top;
    }
}

void WINAPI ScreenToClient(HWND h, POINT __far *p)
{
    PWND w = valid_wnd(h);
    if (w) {
        p->x -= w->rcClient.left;
        p->y -= w->rcClient.top;
    }
}

void WINAPI MapWindowPoints(HWND from, HWND to, POINT __far *p, UINT n)
{
    int dx = 0, dy = 0;
    PWND w;
    if (from && (w = valid_wnd(from)) != 0)
        dx += w->rcClient.left, dy += w->rcClient.top;
    if (to && (w = valid_wnd(to)) != 0)
        dx -= w->rcClient.left, dy -= w->rcClient.top;
    while (n--) {
        p->x += dx;
        p->y += dy;
        p++;
    }
}

void WINAPI GetWindowRect(HWND h, RECT __far *r)
{
    PWND w = valid_wnd(h);
    if (w)
        *r = w->rcWindow;
}

void WINAPI GetClientRect(HWND h, RECT __far *r)
{
    PWND w = valid_wnd(h);
    if (w)
        SetRect(r, 0, 0, w->rcClient.right - w->rcClient.left,
                w->rcClient.bottom - w->rcClient.top);
}

static HWND child_from_point(HWND parent, POINT pt, BOOL skiphidden)
{
    HWND h;
    for (h = PW(parent)->hwndChild; h; h = PW(h)->hwndNext) {
        PWND w = PW(h);
        if (skiphidden && !(w->style & WS_VISIBLE))
            continue;
        if (PtInRect(&w->rcWindow, pt))
            return h;
    }
    return 0;
}

HWND WINAPI WindowFromPoint(POINT pt)
{
    HWND h = hwndDesktop, c;
    for (;;) {
        PWND w = PW(h);
        if (h != hwndDesktop && ((w->style & WS_DISABLED) || (w->style & WS_MINIMIZE)))
            return h;
        if (h != hwndDesktop && !PtInRect(&w->rcClient, pt))
            return h;
        c = child_from_point(h, pt, TRUE);
        if (!c)
            return h;
        h = c;
    }
}

HWND WINAPI ChildWindowFromPoint(HWND parent, POINT pt)
{
    PWND w = valid_wnd(parent);
    HWND c;
    if (!w)
        return 0;
    pt.x += w->rcClient.left;
    pt.y += w->rcClient.top;
    if (!PtInRect(&w->rcWindow, pt))
        return 0;
    c = child_from_point(parent, pt, FALSE);
    return c ? c : parent;
}

/* ---- properties ----
   As 3.1's (US15): a window's properties are one block, WND+36 its
   handle, an entry of an atom and the data per property (4 bytes; 3.1's
   grew by 4 a property), string names global atoms (3.1: GlobalFindAtom
   finds a property's name while a window has it and not after). w16 keeps
   the block in the object heap (uheap.c), not DGROUP. Entries with atom 0
   are unused (a block can be larger than asked for). Atoms from C000h up
   are names this list holds a reference to. */
typedef struct PROP { ATOM atom; HANDLE data; } PROP;
#define PROPS(w)    ((PROP __far *)UH_PTR(&uhObj, (w)->props))

static UINT nprops(PWND w)
{
    return w->props ? uh_size(&uhObj, w->props) / sizeof(PROP) : 0;
}

static ATOM prop_atom(LPCSTR name, BOOL add)
{
    if (ISATOM(name)) {
        ATOM a = LOWORD((DWORD)name);
        char b[64];
        /* a string atom given as a number: one more reference, so that
           every entry from C000h up holds one */
        if (add && a >= 0xC000u && ga_name(a, b, sizeof b))
            return ga_add(b);
        return a;
    }
    return add ? ga_add(name) : ga_find(name);
}

static void prop_unref(ATOM a)
{
    if (a >= 0xC000u)
        ga_delete(a);
}

/* the entry's index, -1 none */
static int find_prop(PWND w, ATOM a)
{
    UINT i, n = nprops(w);
    PROP __far *p = PROPS(w);
    if (!a)
        return -1;
    for (i = 0; i < n; i++)
        if (p[i].atom == a)
            return i;
    return -1;
}

BOOL WINAPI SetProp(HWND h, LPCSTR name, HANDLE data)
{
    PWND w = valid_wnd(h);
    PROP __far *p;
    UINT i, n;
    ATOM a;
    WORD b;
    if (!w)
        return FALSE;
    i = find_prop(w, prop_atom(name, FALSE));
    if (i != (UINT)-1) {
        PROPS(w)[i].data = data;
        return TRUE;
    }
    a = prop_atom(name, TRUE);
    if (!a)
        return FALSE;
    n = nprops(w);
    p = PROPS(w);
    for (i = 0; i < n && p[i].atom; i++)
        ;
    if (i == n) {
        b = uh_realloc(&uhObj, w->props, (n + 1) * sizeof(PROP));
        if (!b) {
            prop_unref(a);
            return FALSE;
        }
        w->props = b;
    }
    p = PROPS(w);
    p[i].atom = a;
    p[i].data = data;
    return TRUE;
}

HANDLE WINAPI GetProp(HWND h, LPCSTR name)
{
    PWND w = valid_wnd(h);
    int i = w ? find_prop(w, prop_atom(name, FALSE)) : -1;
    return i >= 0 ? PROPS(w)[i].data : 0;
}

/* entry i goes: the last one takes its place, the block shrinks */
static void drop_prop(PWND w, UINT i)
{
    UINT n = nprops(w);
    PROP __far *p = PROPS(w);
    prop_unref(p[i].atom);
    while (n && !p[n - 1].atom)
        n--;
    if (n && i < n - 1)
        p[i] = p[n - 1];
    n--;
    if (!n) {
        uh_free(&uhObj, w->props);
        w->props = 0;
    } else {
        WORD b;
        p[n].atom = 0;
        b = uh_realloc(&uhObj, w->props, n * sizeof(PROP));   /* smaller: in place */
        if (b)
            w->props = b;
    }
}

HANDLE WINAPI RemoveProp(HWND h, LPCSTR name)
{
    PWND w = valid_wnd(h);
    int i = w ? find_prop(w, prop_atom(name, FALSE)) : -1;
    HANDLE d;
    if (i < 0)
        return 0;
    d = PROPS(w)[i].data;
    drop_prop(w, i);
    return d;
}

/* DestroyWindow: the properties left go, their names' references with
   them */
void free_props(PWND w)
{
    UINT i, n = nprops(w);
    for (i = 0; i < n; i++)
        prop_unref(PROPS(w)[i].atom);
    uh_free(&uhObj, w->props);
    w->props = 0;
}

/* newest first; the procedure may remove the property it is given (each
   entry is read again after the call: nothing in the heap is held) */
int WINAPI EnumProps(HWND h, PROPENUMPROC f)
{
    PWND w = valid_wnd(h);
    int r = -1;
    UINT i;
    char name[64];
    if (!w)
        return -1;
    for (i = nprops(w); i-- > 0;) {
        PROP pp;
        if (!(w = valid_wnd(h)) || i >= nprops(w))
            break;
        pp = PROPS(w)[i];
        if (!pp.atom)
            continue;
        if (pp.atom >= 0xC000u) {
            ga_name(pp.atom, name, sizeof name);
            r = ((int (__far __pascal *)(HWND, LPCSTR, HANDLE))f)(h, name, pp.data);
        } else
            r = ((int (__far __pascal *)(HWND, LPCSTR, HANDLE))f)(h, MAKEINTATOM(pp.atom), pp.data);
        if (!r)
            break;
    }
    return r;
}
