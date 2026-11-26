/*
 * tk75.c - what WM_MDICREATE does with the style it is given. StarOffice
 * 3.1's StarView makes its MDI client with MDIS_ALLCHILDSTYLES (0001h)
 * and its document windows with WM_MDICREATE, style 42CF0000h (a child
 * with caption, system menu, frame and both boxes but no WS_VISIBLE),
 * CW_USEDEFAULT everywhere, while the client is still 0 x 0; it then
 * asks WM_MDIGETACTIVE and makes the child's controls. w16 showed and
 * activated such a child at once.
 * Run as the shell (boot-test case mdistyle). Each case makes one child
 * of an MDI client (of a plain overlapped window), the child's procedure
 * calling DefMDIChildProc, and logs: the messages the child got during
 * WM_MDICREATE (hex), whether it is visible, whether WM_MDIGETACTIVE
 * then names it and says it is maximised, IsZoomed, IsIconic, its style
 * (GetWindowLong), its place and size in the client, and what
 * WM_MDICREATE returned (0 or not). Then the child is destroyed with
 * WM_MDIDESTROY. Clients: "all" with MDIS_ALLCHILDSTYLES, "std"
 * without, "all0" with MDIS_ALLCHILDSTYLES and 0 x 0 (StarView's case).
 * Later steps (StarOffice's start): BringWindowToTop of a hidden MDI
 * child and of a plain child, SW_SHOWMAXIMIZED of a hidden child in a
 * 0 x 0 client then grown, and the top-level order of an owner and its
 * popups ("Z" lines) when one is sent to the bottom or the top.
 * Writes C:\TK75.LOG.
 */
#include <windows.h>

#ifndef MDIS_ALLCHILDSTYLES
#define MDIS_ALLCHILDSTYLES 0x0001
#endif

extern WORD __cdecl init_regs[7];       /* InitTask's ax bx cx dx si di es */
extern BOOL FAR PASCAL INITAPP(HINSTANCE inst);                 /* USER.5 */

static char line[300];
static int ln;
static WORD hinst;
static HWND main_wnd;
static int seqlog;

static void put(const char *s)
{
    while (*s)
        line[ln++] = *s++;
}

static void hex(WORD v)
{
    int i;
    for (i = 3; i >= 0; i--)
        line[ln++] = "0123456789ABCDEF"[(v >> (4 * i)) & 15];
}

static void dec(int v)
{
    char b[8];
    int n = 0;
    if (v < 0) {
        line[ln++] = '-';
        v = -v;
    }
    do
        b[n++] = (char)('0' + v % 10);
    while ((v /= 10) != 0);
    while (n)
        line[ln++] = b[--n];
}

static void flush(void)
{
    HFILE f = _lopen("C:\\TK75.LOG", OF_READWRITE);
    if (f == HFILE_ERROR)
        f = _lcreat("C:\\TK75.LOG", 0);
    if (f == HFILE_ERROR)
        return;
    _llseek(f, 0, 2);
    line[ln++] = '\r';
    line[ln++] = '\n';
    _lwrite(f, line, ln);
    _lclose(f);
    ln = 0;
}

LRESULT FAR PASCAL __export __loadds TK75_CHILD(HWND h, UINT m, WPARAM w, LPARAM l)
{
    if (seqlog) {
        put(" ");
        hex(m);
        if (ln > 250) {
            put(" +");
            flush();
        }
    }
    return DefMDIChildProc(h, m, w, l);
}

LRESULT FAR PASCAL __export __loadds TK75_PLAIN(HWND h, UINT m, WPARAM w, LPARAM l)
{
    if (seqlog) {
        put(" ");
        hex(m);
        if (ln > 250) {
            put(" +");
            flush();
        }
    }
    return DefWindowProc(h, m, w, l);
}

LRESULT FAR PASCAL __export __loadds TK75_MAIN(HWND h, UINT m, WPARAM w, LPARAM l)
{
    return DefWindowProc(h, m, w, l);
}

static void pump(void)
{
    MSG msg;
    int i;
    for (i = 0; i < 500 && PeekMessage(&msg, 0, 0, 0, PM_REMOVE); i++)
        DispatchMessage(&msg);
}

static HWND make_client(DWORD style, int cx, int cy)
{
    CLIENTCREATESTRUCT ccs;
    ccs.hWindowMenu = 0;
    ccs.idFirstChild = 100;
    return CreateWindow("MDICLIENT", NULL, WS_CHILD | WS_CLIPCHILDREN | WS_VISIBLE | style,
                        0, 0, cx, cy, main_wnd, (HMENU)1, (HINSTANCE)hinst, (LPSTR)&ccs);
}

/* a window's state after a step: "R name visible active ..." (cl: its
   MDI client, 0 none) */
static void state(HWND cl, HWND c, const char *name)
{
    HWND a;
    DWORD ga;
    RECT r;
    POINT p;
    put("R ");
    put(name);
    put(IsWindowVisible(c) ? " visible" : " hidden");
    if (cl) {
        ga = SendMessage(cl, WM_MDIGETACTIVE, 0, 0);
        a = (HWND)LOWORD(ga);
        put(a == c ? " active" : a ? " other-active" : " none-active");
        put(HIWORD(ga) ? " max" : " nomax");
    }
    put(IsZoomed(c) ? " zoomed" : "");
    put(IsIconic(c) ? " iconic" : "");
    put(" style=");
    hex(HIWORD(GetWindowLong(c, GWL_STYLE)));
    hex(LOWORD(GetWindowLong(c, GWL_STYLE)));
    GetWindowRect(c, &r);
    p.x = p.y = 0;
    ClientToScreen(GetParent(c), &p);
    put(" at ");
    dec(r.left - p.x);
    put(",");
    dec(r.top - p.y);
    put(" ");
    dec(r.right - r.left);
    put("x");
    dec(r.bottom - r.top);
    put(GetFocus() == c ? " focus" : "");
    flush();
}

/* where the focus is before a step: "F frame", "client", "child",
   "none" or "other" */
static void focus_note(HWND cl, HWND c)
{
    HWND f = GetFocus();
    put("F ");
    put(f == main_wnd ? "frame" : f && f == cl ? "client" : f && f == c ? "child" :
        !f ? "none" : "other");
    put(GetActiveWindow() == main_wnd ? " frame-active" : " frame-inactive");
    flush();
}

/* a step on window c: the messages it gets ("S ... | name"), then its
   state */
enum { A_SHOW, A_SHOWNORMAL, A_SHOWNA, A_SHOWMAX, A_SHOWMIN, A_RESTORE, A_HIDE,
       A_TOP, A_NOZ, A_SWPSHOW, A_MOVE, A_SMALL, A_BRING };
static void act(HWND cl, HWND c, const char *name, int what)
{
    focus_note(cl, c);
    put("S");
    seqlog = 1;
    switch (what) {
    case A_SHOW: ShowWindow(c, SW_SHOW); break;
    case A_SHOWNORMAL: ShowWindow(c, SW_SHOWNORMAL); break;
    case A_SHOWNA: ShowWindow(c, SW_SHOWNA); break;
    case A_SHOWMAX: ShowWindow(c, SW_SHOWMAXIMIZED); break;
    case A_SHOWMIN: ShowWindow(c, SW_SHOWMINIMIZED); break;
    case A_RESTORE: ShowWindow(c, SW_RESTORE); break;
    case A_HIDE: ShowWindow(c, SW_HIDE); break;
    case A_TOP: SetWindowPos(c, HWND_TOP, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE); break;
    case A_NOZ: SetWindowPos(c, 0, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER); break;
    case A_SWPSHOW:
        SetWindowPos(c, 0, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_SHOWWINDOW);
        break;
    case A_MOVE: MoveWindow(c, 3, 4, 120, 70, TRUE); break;
    case A_SMALL:
        SetWindowPos(c, 0, 0, 0, 20, 5, SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE);
        break;
    case A_BRING: BringWindowToTop(c); break;
    }
    seqlog = 0;
    put(" | ");
    put(name);
    flush();
    state(cl, c, name);
}

/* one WM_MDICREATE; keep: the child is not destroyed (returned) */
static HWND one(HWND cl, const char *name, DWORD style, int x, int y, int cx, int cy, int keep)
{
    MDICREATESTRUCT mc;
    HWND c;
    mc.szClass = "TK75C";
    mc.szTitle = "c";
    mc.hOwner = (HINSTANCE)hinst;
    mc.x = x;
    mc.y = y;
    mc.cx = cx;
    mc.cy = cy;
    mc.style = style;
    mc.lParam = 0;
    focus_note(cl, 0);
    put("S");
    seqlog = 1;
    c = (HWND)LOWORD(SendMessage(cl, WM_MDICREATE, 0, (LPARAM)(LPSTR)&mc));
    seqlog = 0;
    put(" | ");
    put(name);
    flush();
    if (!c) {
        put("R ");
        put(name);
        put(" failed");
        flush();
        return 0;
    }
    state(cl, c, name);
    if (keep)
        return c;
    SendMessage(cl, WM_MDIDESTROY, (WPARAM)c, 0);
    pump();
    return 0;
}

/* "Z name: ..." this probe's top-level windows from the top (m the main
   window, o an owner, p and q its popups, + visible), then the active one */
static void zorder(HWND o, HWND p, HWND q, const char *name)
{
    HWND w;
    HWND a = GetActiveWindow();
    put("Z ");
    put(name);
    put(":");
    for (w = GetWindow(GetDesktopWindow(), GW_CHILD); w; w = GetWindow(w, GW_HWNDNEXT)) {
        const char *n = w == main_wnd ? " m" : w == o ? " o" : w == p ? " p" : w == q ? " q" : 0;
        if (n) {
            put(n);
            if (IsWindowVisible(w))
                put("+");
        }
    }
    put(" active ");
    put(a == main_wnd ? "m" : a == o ? "o" : a == p ? "p" : a == q ? "q" : a ? "other" : "none");
    flush();
}

#define SV 0x42CF0000L          /* StarView's document window style */
#define CU ((int)CW_USEDEFAULT)

int __cdecl tmain(void)
{
    WNDCLASS wc;
    HWND cl, k;

    hinst = init_regs[5];
    INITAPP((HINSTANCE)hinst);
    put("T start");
    flush();
    wc.style = 0;
    wc.lpfnWndProc = (WNDPROC)TK75_MAIN;
    wc.cbClsExtra = 0;
    wc.cbWndExtra = 0;
    wc.hInstance = (HINSTANCE)hinst;
    wc.hIcon = 0;
    wc.hCursor = 0;
    wc.hbrBackground = GetStockObject(WHITE_BRUSH);
    wc.lpszMenuName = NULL;
    wc.lpszClassName = "TK75";
    RegisterClass(&wc);
    wc.lpfnWndProc = (WNDPROC)TK75_CHILD;
    wc.lpszClassName = "TK75C";
    RegisterClass(&wc);
    wc.lpfnWndProc = (WNDPROC)TK75_PLAIN;
    wc.lpszClassName = "TK75P";
    RegisterClass(&wc);
    main_wnd = CreateWindow("TK75", "TK75", WS_OVERLAPPEDWINDOW | WS_VISIBLE, 0, 0, 400, 300,
                            0, 0, (HINSTANCE)hinst, NULL);
    pump();

    /* MDIS_ALLCHILDSTYLES */
    cl = make_client(MDIS_ALLCHILDSTYLES, 300, 200);
    one(cl, "all sv", SV, CU, CU, CU, CU, 0);
    one(cl, "all sv+vis", SV | WS_VISIBLE, CU, CU, CU, CU, 0);
    one(cl, "all sv+vis+max", SV | WS_VISIBLE | WS_MAXIMIZE, CU, CU, CU, CU, 0);
    one(cl, "all sv+max", SV | WS_MAXIMIZE, CU, CU, CU, CU, 0);
    one(cl, "all sv+vis+min", SV | WS_VISIBLE | WS_MINIMIZE, CU, CU, CU, CU, 0);
    one(cl, "all child", WS_CHILD, CU, CU, CU, CU, 0);
    one(cl, "all 0", 0, 10, 10, 100, 50, 0);
    one(cl, "all sv place", SV, 10, 12, 100, 60, 0);
    /* a hidden child made while another is active */
    k = one(cl, "all vis first", SV | WS_VISIBLE, 5, 5, 100, 60, 1);
    one(cl, "all sv second", SV, CU, CU, CU, CU, 0);
    if (k) {
        SendMessage(cl, WM_MDIDESTROY, (WPARAM)k, 0);
        pump();
    }
    /* hidden, then shown (StarView shows its document windows later) */
    k = one(cl, "all sv keep", SV, CU, CU, CU, CU, 1);
    if (k) {
        act(cl, k, "all sv show", A_SHOW);
        SendMessage(cl, WM_MDIDESTROY, (WPARAM)k, 0);
        pump();
    }
    k = one(cl, "all sv keep2", SV, CU, CU, CU, CU, 1);
    if (k) {
        act(cl, k, "all sv shownormal", A_SHOWNORMAL);
        SendMessage(cl, WM_MDIDESTROY, (WPARAM)k, 0);
        pump();
    }
    k = one(cl, "all sv keep3", SV, CU, CU, CU, CU, 1);
    if (k) {
        act(cl, k, "all sv swpshow", A_SWPSHOW);
        act(cl, k, "all sv top", A_TOP);
        SendMessage(cl, WM_MDIDESTROY, (WPARAM)k, 0);
        pump();
    }
    /* BringWindowToTop of a hidden child (StarChart shows a control in
       its new document window, then brings the window to the top: on 3.1
       that activated it, through WM_CHILDACTIVATE) */
    k = one(cl, "all sv keep4", SV, CU, CU, CU, CU, 1);
    if (k) {
        act(cl, k, "all sv bring", A_BRING);
        act(cl, k, "all sv bring show", A_SHOW);
        act(cl, k, "all sv bring2", A_BRING);
        SendMessage(cl, WM_MDIDESTROY, (WPARAM)k, 0);
        pump();
    }
    k = one(cl, "all sv keep5", SV, CU, CU, CU, CU, 1);
    if (k) {
        act(cl, k, "all sv showmax", A_SHOWMAX);
        SendMessage(cl, WM_MDIDESTROY, (WPARAM)k, 0);
        pump();
    }
    k = one(cl, "all sv+max keep", SV | WS_MAXIMIZE, CU, CU, CU, CU, 1);
    if (k) {
        act(cl, k, "all sv+max show", A_SHOW);
        SendMessage(cl, WM_MDIDESTROY, (WPARAM)k, 0);
        pump();
    }
    DestroyWindow(cl);
    pump();

    /* without it */
    cl = make_client(0, 300, 200);
    one(cl, "std 0", 0, CU, CU, CU, CU, 0);
    one(cl, "std vis", WS_VISIBLE, CU, CU, CU, CU, 0);
    one(cl, "std max", WS_MAXIMIZE, CU, CU, CU, CU, 0);
    one(cl, "std min", WS_MINIMIZE, CU, CU, CU, CU, 0);
    one(cl, "std sv", SV, CU, CU, CU, CU, 0);
    one(cl, "std lowbits", 0x0115, CU, CU, CU, CU, 0);
    one(cl, "std scroll", WS_VSCROLL | WS_HSCROLL, 10, 10, 100, 50, 0);
    one(cl, "std popup", WS_POPUP | WS_BORDER, 10, 10, 100, 50, 0);
    DestroyWindow(cl);
    pump();

    /* StarView's: the client still 0 x 0 */
    cl = make_client(MDIS_ALLCHILDSTYLES | WS_CLIPSIBLINGS, 0, 0);
    one(cl, "all0 sv", SV, CU, CU, CU, CU, 0);
    /* then maximised there, still hidden, and the client grown (StarWriter:
       the maximised child's size while the client is 0 x 0; its client
       area's width sizes the document window's controls) */
    k = one(cl, "all0 sv keep", SV, CU, CU, CU, CU, 1);
    if (k) {
        act(cl, k, "all0 sv showmax", A_SHOWMAX);
        MoveWindow(cl, 0, 0, 300, 200, TRUE);
        state(cl, k, "all0 sv client grown");
        act(cl, k, "all0 sv show", A_SHOW);
        SendMessage(cl, WM_MDIDESTROY, (WPARAM)k, 0);
        pump();
    }
    DestroyWindow(cl);
    pump();
    cl = make_client(0, 0, 0);
    one(cl, "std0 0", 0, CU, CU, CU, CU, 0);
    DestroyWindow(cl);
    pump();

    /* plain children (DefWindowProc) of the main window: which of
       ShowWindow and SetWindowPos send WM_CHILDACTIVATE */
    {
        static const struct { int what; const char *name; } st[] = {
            { A_SHOW, "p show" }, { A_NOZ, "p noz" }, { A_TOP, "p top" }, { A_MOVE, "p move" },
            { A_HIDE, "p hide" }, { A_SHOWNORMAL, "p shownormal" }, { A_HIDE, "p hide2" },
            { A_SHOWNA, "p showna" }, { A_HIDE, "p hide3" }, { A_BRING, "p bring hidden" },
            { A_SWPSHOW, "p swpshow" }, { A_BRING, "p bring" },
            { A_SHOWMAX, "p showmax" }, { A_RESTORE, "p restore" }, { A_SHOWMIN, "p showmin" },
            { A_RESTORE, "p restore2" },
        };
        int i;
        HWND p = CreateWindow("TK75P", "p", WS_CHILD | WS_CAPTION | WS_THICKFRAME | WS_SYSMENU |
                              WS_MINIMIZEBOX | WS_MAXIMIZEBOX, 10, 10, 120, 80, main_wnd,
                              (HMENU)5, (HINSTANCE)hinst, NULL);
        pump();
        for (i = 0; i < sizeof st / sizeof st[0]; i++) {
            act(0, p, st[i].name, st[i].what);
            pump();
        }
        DestroyWindow(p);
        pump();
        put("S");
        seqlog = 1;
        p = CreateWindow("TK75P", "p", WS_CHILD | WS_VISIBLE, 10, 10, 120, 80, main_wnd,
                         (HMENU)5, (HINSTANCE)hinst, NULL);
        seqlog = 0;
        put(" | p created visible");
        flush();
        state(0, p, "p created visible");
        DestroyWindow(p);
        pump();
    }

    /* CreateWindow with less than the minimum tracking size (an MDI
       child made so came out 102 x 26 on 3.1) */
    {
        static const struct { DWORD style; int child; const char *name; } k[] = {
            { WS_OVERLAPPEDWINDOW, 0, "z overlapped" },
            { WS_POPUP | WS_THICKFRAME, 0, "z popup thick" },
            { WS_POPUP | WS_CAPTION, 0, "z popup caption" },
            { WS_CHILD | WS_THICKFRAME, 1, "z child thick" },
            { WS_CHILD | WS_CAPTION, 1, "z child caption" },
        };
        int i;
        for (i = 0; i < sizeof k / sizeof k[0]; i++) {
            HWND z = CreateWindow("TK75P", "z", k[i].style, 7, 8, 30, 10,
                                  k[i].child ? main_wnd : 0, (HMENU)(k[i].child ? 6 : 0),
                                  (HINSTANCE)hinst, NULL);
            if (z) {
                state(0, z, k[i].name);
                act(0, z, "z swp small", A_SMALL);
                DestroyWindow(z);
            }
            pump();
        }
    }

    /* an owned popup sent to the bottom (StarWriter's floating Stylist:
       made hidden with the frame as owner, SetWindowPos(HWND_BOTTOM,
       SWP_NOMOVE | SWP_NOSIZE | SWP_NOREDRAW | SWP_NOACTIVATE), then
       ShowWindow(SW_SHOW); w16 left it under its owner): the top-level
       order of this probe's windows after each step */
    {
        HWND o = CreateWindow("TK75P", "o", WS_OVERLAPPEDWINDOW | WS_VISIBLE, 50, 50, 200, 150,
                              0, 0, (HINSTANCE)hinst, NULL);
        HWND p = CreateWindow("TK75P", "p", WS_POPUP | WS_CAPTION, 60, 60, 100, 80, o, 0,
                              (HINSTANCE)hinst, NULL);
        HWND q = CreateWindow("TK75P", "q", WS_POPUP | WS_CAPTION | WS_VISIBLE, 70, 70, 100, 80,
                              o, 0, (HINSTANCE)hinst, NULL);
        pump();
        zorder(o, p, q, "z created");
        SetWindowPos(p, HWND_BOTTOM, 0, 0, 0, 0,
                     SWP_NOMOVE | SWP_NOSIZE | SWP_NOREDRAW | SWP_NOACTIVATE);
        zorder(o, p, q, "z hidden p to bottom");
        ShowWindow(p, SW_SHOW);
        pump();
        zorder(o, p, q, "z p shown");
        SetWindowPos(q, HWND_BOTTOM, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
        zorder(o, p, q, "z visible q to bottom");
        SetWindowPos(p, o, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
        zorder(o, p, q, "z p after o");
        SetWindowPos(main_wnd, HWND_TOP, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
        zorder(o, p, q, "z main to top");
        SetWindowPos(o, HWND_TOP, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE);
        pump();
        zorder(o, p, q, "z o to top");
        SetActiveWindow(main_wnd);
        pump();
        zorder(o, p, q, "z main active");
        SetActiveWindow(o);
        pump();
        zorder(o, p, q, "z o active");
        DestroyWindow(q);
        DestroyWindow(p);
        DestroyWindow(o);
        pump();
    }

    DestroyWindow(main_wnd);
    put("T done");
    flush();
    return 0;
}
