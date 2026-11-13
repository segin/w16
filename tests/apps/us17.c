/*
 * us17.c - probe US17: the scroll-bar menu, w16's extension beyond
 * Windows 3.1 (user/scroll.c; [w16] ScrollBarMenu in SYSTEM.INI, on by
 * default, ScrollBarMenu=0 for 3.1's behaviour).
 *
 * A window with both scroll bars and three SCROLLBAR controls (vertical,
 * horizontal, and a vertical one with an empty range). Each bar is
 * right-clicked with real mouse input: a SYSTEM.DRV timer procedure
 * (CreateSystemTimer, so at interrupt time like the mouse driver) feeds
 * USER's mouse_event (USER.299, tests/apps/mevent.asm) a move, a right
 * press and a right release, while the probe pumps messages. A WM_TIMER
 * of the probe's own (dispatched by the menu loop while a menu is up)
 * looks for a visible #32768 window; when there is one it reads the menu
 * (w16 keeps the HMENU in the menu window's word 0: a w16 layout, used
 * only when a menu is there) and then chooses an item with posted keys
 * (Down n times, Return), or Escape, or a left click by mouse_event on an
 * item or outside the menu. The window logs every WM_VSCROLL/WM_HSCROLL
 * (code, position, which control) and every menu message it is sent
 * (WM_INITMENU, WM_INITMENUPOPUP, WM_MENUSELECT, WM_COMMAND, WM_ENTERIDLE),
 * which it must never get: the menu is USER's own.
 *
 * With the menu on (the default), each item must send what its scroll
 * code is and then SB_ENDSCROLL, with 3.1's 16-bit packing (lParam =
 * MAKELONG(pos, hwndCtl), hwndCtl 0 for the window's own bars). With
 * [w16] ScrollBarMenu=0, and on Windows 3.1 itself, no menu appears and
 * no message at all comes from the right clicks; the window's caption
 * then says "US17 done" and tests/boottest.sh's sbmenu case compares that
 * screen and the log's message lines with Windows 3.1's (identical).
 * Finally, with the menu on, the vertical bar's menu is left open for the
 * screenshot.
 *
 * Run as the shell: tests/boottest.sh sbmenu, or by hand
 * tests/userboot.sh -n -s build/dos/US17.EXE [-i w16:ScrollBarMenu=0].
 */
#include <windows.h>

#define TK_REF "us17ref.h"
#include "tklog.h"

extern WORD FAR PASCAL WaitEvent(HTASK);
extern WORD FAR PASCAL S_CREATESYSTEMTIMER(WORD ms, FARPROC proc);
extern WORD FAR PASCAL S_KILLSYSTEMTIMER(WORD id);
extern void __cdecl call_mouse_event(WORD status, WORD x, WORD y, WORD buttons);
extern BOOL FAR PASCAL InitApp(HINSTANCE);

/* the mouse script the timer plays: status, screen x, y */
typedef struct { WORD st; int x, y; } MSTEP;
static MSTEP mscript[8];
static volatile int mstep, msteps;

void FAR __loadds mtimer(void)
{
    if (mstep < msteps) {
        MSTEP *s = &mscript[mstep++];
        /* normalised 0-65535 (as us6.c) */
        call_mouse_event(s->st | 0x8001, (WORD)(s->x * 102 + s->x * 2 / 5 + 1),
                         (WORD)(s->y * 136 + s->y * 8 / 15 + 1), 2);
    }
}

static void mouse_add(WORD st, int x, int y)
{
    mscript[msteps].st = st;
    mscript[msteps].x = x;
    mscript[msteps].y = y;
    msteps++;
}

/* move to x, y and click button (2 left, 8 right: press; the release is
   the next bit) */
static void mouse_click(int x, int y, WORD press)
{
    msteps = 0;
    mouse_add(0, x, y);
    mouse_add(press, x, y);
    mouse_add(press << 1, x, y);
    mstep = 0;
}

static HWND hMain, hCV, hCH, hCE;
static char rec[256];           /* the scroll messages of a step */
static char extra[64];          /* menu messages the window got */
static int menuon;              /* [w16] ScrollBarMenu */

/* what the WM_TIMER does when it finds the menu up */
enum { A_NONE, A_KEYS, A_ESC, A_CLICKITEM, A_RCLICKITEM, A_OUTSIDE };
static int act, actarg, seen;
static int clickx, clicky;      /* where the right click was */
static const char *inspect;     /* the menu's expected items, or 0 */
static char itag[16];

static void append(char *buf, int size, const char *s)
{
    int n = lstrlen(buf);
    if (n + lstrlen(s) < size)
        lstrcpy(buf + n, s);
}

static const char *ctlname(WORD h)
{
    if (!h)
        return "w";
    if ((HWND)h == hCV)
        return "cv";
    if ((HWND)h == hCH)
        return "ch";
    if ((HWND)h == hCE)
        return "ce";
    return "?";
}

static HWND find_menu(void)
{
    HWND h;
    char cls[16];
    for (h = GetWindow(GetDesktopWindow(), GW_CHILD); h; h = GetWindow(h, GW_HWNDNEXT))
        if (IsWindowVisible(h) && GetClassName(h, cls, sizeof cls) && !lstrcmp(cls, "#32768"))
            return h;
    return 0;
}

/* the menu's items as "Scroll Here|-|Top|..." against want */
static void read_menu(HWND mw)
{
    HMENU m = (HMENU)GetWindowWord(mw, 0);
    char got[200], t[40], nm[40];
    int i, n = GetMenuItemCount(m);
    RECT r;
    got[0] = 0;
    for (i = 0; i < n; i++) {
        UINT st = GetMenuState(m, i, MF_BYPOSITION);
        if (st & MF_SEPARATOR)
            lstrcpy(t, "-");
        else
            GetMenuString(m, i, t, sizeof t, MF_BYPOSITION);
        /* (a separator is MF_DISABLED as well, as on 3.1: US18) */
        if ((st & (MF_GRAYED | MF_DISABLED | MF_CHECKED)) && !(st & MF_SEPARATOR))
            append(t, sizeof t, "(state)");
        if (i)
            append(got, sizeof got, "|");
        append(got, sizeof got, t);
    }
    out("OBS ");
    out(itag);
    out(".items=\"");
    out(got);
    out("\"\r\n");
    wsprintf(nm, "%s: the menu's items", (LPSTR)itag);
    check(!lstrcmp(got, inspect), nm);
    GetWindowRect(mw, &r);
    wsprintf(nm, "%s: the menu at the pointer", (LPSTR)itag);
    check(r.left == clickx && r.top == clicky, nm);
}

LRESULT FAR PASCAL __export __loadds wnd_proc(HWND h, UINT m, WPARAM wp, LPARAM lp)
{
    char b[40];
    switch (m) {
    case WM_VSCROLL:
    case WM_HSCROLL:
        wsprintf(b, "%c%d,%d,%s;", m == WM_VSCROLL ? 'V' : 'H', wp, (int)LOWORD(lp),
                 (LPSTR)ctlname(HIWORD(lp)));
        append(rec, sizeof rec, b);
        return 0;
    case WM_INITMENU:
    case WM_INITMENUPOPUP:
    case WM_MENUSELECT:
    case WM_COMMAND:
    case WM_ENTERIDLE:
        wsprintf(b, "M%04X;", m);
        append(extra, sizeof extra, b);
        break;
    case WM_TIMER: {
        HWND mw;
        int i;
        if (act == A_NONE || (mw = find_menu()) == 0)
            return 0;
        seen = 1;
        if (inspect)
            read_menu(mw);
        inspect = 0;
        switch (act) {
        case A_KEYS:
            /* nothing is selected when the menu opens by mouse: Down
               selects the first item */
            for (i = 0; i <= actarg; i++)
                PostMessage(hMain, WM_KEYDOWN, VK_DOWN, 1L);
            PostMessage(hMain, WM_KEYDOWN, VK_RETURN, 1L);
            break;
        case A_ESC:
            PostMessage(hMain, WM_KEYDOWN, VK_ESCAPE, 1L);
            break;
        case A_CLICKITEM:
        case A_RCLICKITEM: {
            /* item actarg (counting separators) by its place: items
               SM_CYMENU high, separators 7, under a one-pixel line (US4) */
            RECT r;
            int y = 1, k;
            GetWindowRect(mw, &r);
            for (k = 0; k < actarg; k++)
                y += (k == 1 || k == 4 || k == 7) ? 7 : GetSystemMetrics(SM_CYMENU);
            mouse_click(r.left + 20, r.top + y + GetSystemMetrics(SM_CYMENU) / 2,
                        act == A_CLICKITEM ? 2 : 8);
            break;
        }
        case A_OUTSIDE: {
            /* the left click on the window's client area, away from the
               controls and the menu */
            POINT p;
            p.x = 150;
            p.y = 160;
            ClientToScreen(hMain, &p);
            mouse_click(p.x, p.y, 2);
            break;
        }
        }
        act = A_NONE;
        return 0;
    }
    }
    return DefWindowProc(h, m, wp, lp);
}

static void pump(DWORD ms)
{
    DWORD end = GetTickCount() + ms;
    MSG m;
    while (GetTickCount() < end) {
        while (PeekMessage(&m, 0, 0, 0, PM_REMOVE)) {
            TranslateMessage(&m);
            DispatchMessage(&m);
        }
    }
}

static int nstep;

/* a right click at x, y; then action a (arg) on the menu if one comes up;
   want: the scroll messages expected with the menu on (none without) */
static void step(const char *tag, int x, int y, int a, int arg, const char *want, const char *items)
{
    char nm[48];
    DWORD end;
    wsprintf(nm, "US17 %d", ++nstep);
    SetWindowText(hMain, nm);           /* the screen changes at every step */
    pump(150);
    rec[0] = extra[0] = 0;
    act = a;
    actarg = arg;
    seen = 0;
    clickx = x;
    clicky = y;
    inspect = items;
    lstrcpy(itag, tag);
    mouse_click(x, y, 8);
    /* the right click played; with the menu on, its action (the
       menu's WM_TIMER), and any mouse script that started; then time
       for what follows */
    end = GetTickCount() + 3000;
    while (mstep < msteps && GetTickCount() < end)
        pump(20);
    if (menuon && a != A_NONE)
        while ((act != A_NONE || mstep < msteps) && GetTickCount() < end)
            pump(20);
    pump(600);
    act = A_NONE;
    if (!menuon)
        want = "";
    out("OBS ");
    out(tag);
    out("=\"");
    out(rec);
    out("\"\r\n");
    wsprintf(nm, "%s: the messages", (LPSTR)tag);
    check(!lstrcmp(rec, want), nm);
    if (lstrcmp(rec, want)) {
        out("  expected \"");
        out(want);
        out("\"\r\n");
    }
    wsprintf(nm, "%s: no menu messages", (LPSTR)tag);
    check(!extra[0], nm);
    if (extra[0]) {
        out("  got ");
        out(extra);
        out("\r\n");
    }
    wsprintf(nm, "%s: a menu %s", (LPSTR)tag, menuon && a != A_NONE ? (LPSTR)"came up" : (LPSTR)"did not come up");
    check(seen == (menuon && a != A_NONE), nm);
}

static const char vitems[] = "Scroll Here|-|Top|Bottom|-|Page Up|Page Down|-|Scroll Up|Scroll Down";
static const char hitems[] = "Scroll Here|-|Left Edge|Right Edge|-|Page Left|Page Right|-|Scroll Left|Scroll Right";

/* the position whose thumb is centred on the click at "at" pixels into a
   bar len long, range 0-100 (scroll.c's rule) */
static int here(int at, int len, int arrow)
{
    int travel = len - 3 * arrow + 2;
    int a = at - (arrow - 1) - arrow / 2;
    /* (unsigned: no long arithmetic in a probe without the C library) */
    unsigned v = a > 0 ? ((unsigned)a * 200u + travel) / (2u * travel) : 0;
    return v > 100 ? 100 : (int)v;
}

/* all seven items of one bar by keys, then the extras */
static void bar_items(const char *tag, char dir, const char *ctl, int x, int y, int pos,
                      const char *items)
{
    static const int codes[] = { SB_THUMBPOSITION, SB_TOP, SB_BOTTOM, SB_PAGEUP, SB_PAGEDOWN,
                                 SB_LINEUP, SB_LINEDOWN };
    static const char *const names[] = { "here", "top", "bottom", "pageup", "pagedown",
                                         "lineup", "linedown" };
    char want[96], t[24];
    int k;
    for (k = 0; k < 7; k++) {
        if (k == 0)
            wsprintf(want, "%c%d,%d,%s;%c%d,%d,%s;%c%d,0,%s;", dir, SB_THUMBTRACK, pos, (LPSTR)ctl,
                     dir, SB_THUMBPOSITION, pos, (LPSTR)ctl, dir, SB_ENDSCROLL, (LPSTR)ctl);
        else
            wsprintf(want, "%c%d,0,%s;%c%d,0,%s;", dir, codes[k], (LPSTR)ctl, dir, SB_ENDSCROLL,
                     (LPSTR)ctl);
        wsprintf(t, "%s.%s", (LPSTR)tag, (LPSTR)names[k]);
        step(t, x, y, A_KEYS, k, want, k == 0 ? items : 0);
    }
}

int __cdecl tmain(void)
{
    WNDCLASS wc;
    HINSTANCE tk_hinst = (HINSTANCE)init_regs[5];      /* DI at start-up */
    RECT c, r;
    POINT p;
    WORD id;
    int arrowv = GetSystemMetrics(SM_CYVSCROLL), arrowh = GetSystemMetrics(SM_CXHSCROLL);
    int x, y, pos;
    char want[64];
    WaitEvent(0);
    InitApp(tk_hinst);
    out("US17 start\r\n");
    menuon = GetPrivateProfileInt("w16", "ScrollBarMenu", 1, "SYSTEM.INI") != 0;
    obs("~ScrollBarMenu", menuon);
    wc.style = 0;
    wc.lpfnWndProc = wnd_proc;
    wc.cbClsExtra = wc.cbWndExtra = 0;
    wc.hInstance = tk_hinst;
    wc.hIcon = 0;
    wc.hCursor = LoadCursor(NULL, IDC_ARROW);
    wc.hbrBackground = (HBRUSH)(COLOR_WINDOW + 1);
    wc.lpszMenuName = NULL;
    wc.lpszClassName = "US17";
    RegisterClass(&wc);
    hMain = CreateWindow("US17", "US17", WS_OVERLAPPEDWINDOW | WS_VSCROLL | WS_HSCROLL | WS_VISIBLE,
                         40, 40, 320, 240, NULL, NULL, tk_hinst, NULL);
    SetScrollRange(hMain, SB_VERT, 0, 100, FALSE);
    SetScrollRange(hMain, SB_HORZ, 0, 100, FALSE);
    SetScrollPos(hMain, SB_VERT, 50, TRUE);
    SetScrollPos(hMain, SB_HORZ, 50, TRUE);
    hCV = CreateWindow("ScrollBar", "", WS_CHILD | WS_VISIBLE | SBS_VERT, 20, 10, 17, 120, hMain,
                       (HMENU)1, tk_hinst, NULL);
    hCH = CreateWindow("ScrollBar", "", WS_CHILD | WS_VISIBLE | SBS_HORZ, 60, 10, 150, 17, hMain,
                       (HMENU)2, tk_hinst, NULL);
    hCE = CreateWindow("ScrollBar", "", WS_CHILD | WS_VISIBLE | SBS_VERT, 240, 10, 17, 100, hMain,
                       (HMENU)3, tk_hinst, NULL);
    /* (not redrawn here: w16's SetScrollPos on a control paints it at
       once through UpdateWindow, before its parent's first WM_PAINT, which
       then covers it; 3.1 keeps it. A USER difference of its own, outside
       this probe's subject: tests/boottest.sh sbmenu would fail on it) */
    SetScrollRange(hCV, SB_CTL, 0, 100, FALSE);
    SetScrollPos(hCV, SB_CTL, 50, FALSE);
    SetScrollRange(hCH, SB_CTL, 0, 100, FALSE);
    SetScrollPos(hCH, SB_CTL, 50, FALSE);
    UpdateWindow(hMain);
    SetTimer(hMain, 1, 100, NULL);
    id = S_CREATESYSTEMTIMER(55, (FARPROC)mtimer);
    pump(1000);

    /* the client area on the screen */
    GetClientRect(hMain, &c);
    p.x = p.y = 0;
    ClientToScreen(hMain, &p);
    OffsetRect(&c, p.x, p.y);

    /* the window's vertical bar: from a pixel above the client area to
       one below (US4), the click at its middle */
    x = c.right + GetSystemMetrics(SM_CXVSCROLL) / 2 - 1;
    y = (c.top + c.bottom) / 2;
    pos = here(y - (c.top - 1), c.bottom - c.top + 2, arrowv);
    bar_items("wv", 'V', "w", x, y, pos, vitems);
    step("wv.esc", x, y, A_ESC, 0, "", 0);
    wsprintf(want, "V%d,0,w;V%d,0,w;", SB_BOTTOM, SB_ENDSCROLL);
    step("wv.clickbottom", x, y, A_CLICKITEM, 3, want, 0);
    /* the right button chooses too (TPM_RIGHTBUTTON) */
    wsprintf(want, "V%d,0,w;V%d,0,w;", SB_TOP, SB_ENDSCROLL);
    step("wv.rclicktop", x, y, A_RCLICKITEM, 2, want, 0);
    /* a click outside closes it */
    step("wv.outside", x, y, A_OUTSIDE, 0, "", 0);

    /* the window's horizontal bar */
    x = (c.left + c.right) / 2;
    y = c.bottom + GetSystemMetrics(SM_CYHSCROLL) / 2 - 1;
    pos = here(x - (c.left - 1), c.right - c.left + 2, arrowh);
    bar_items("wh", 'H', "w", x, y, pos, hitems);

    /* the vertical control */
    GetWindowRect(hCV, &r);
    x = (r.left + r.right) / 2;
    y = (r.top + r.bottom) / 2;
    pos = here(y - r.top, r.bottom - r.top, arrowv);
    bar_items("cv", 'V', "cv", x, y, pos, vitems);

    /* the horizontal control */
    GetWindowRect(hCH, &r);
    x = (r.left + r.right) / 2;
    y = (r.top + r.bottom) / 2;
    pos = here(x - r.left, r.right - r.left, arrowh);
    bar_items("ch", 'H', "ch", x, y, pos, hitems);

    /* a control with nothing to scroll: no menu */
    GetWindowRect(hCE, &r);
    step("ce.empty", (r.left + r.right) / 2, (r.top + r.bottom) / 2, A_NONE, 0, "", 0);
    /* (a range for it now: 3.1 draws an empty-range control's thumb at
       the top, w16 draws none - another USER difference outside this
       probe's subject, kept off the screen compared) */
    SetScrollRange(hCE, SB_CTL, 0, 100, TRUE);

    /* a disabled control gets no mouse input: no menu */
    EnableWindow(hCV, FALSE);
    GetWindowRect(hCV, &r);
    step("cv.disabled", (r.left + r.right) / 2, (r.top + r.bottom) / 2, A_NONE, 0, "", 0);
    EnableWindow(hCV, TRUE);

    SetWindowText(hMain, "US17 done");
    out(failures ? "US17 failures\r\n" : "US17 all passed\r\n");
    out("US17 end\r\n");
    /* the screen to compare: the vertical bar right-clicked once more,
       its menu left up with the menu on, nothing on 3.1 */
    pump(300);
    act = A_NONE;
    x = c.right + GetSystemMetrics(SM_CXVSCROLL) / 2 - 1;
    y = (c.top + c.bottom) / 2;
    mouse_click(x, y, 8);
    while (id)                          /* (for ever: Windows stays up) */
        pump(1000);
    S_KILLSYSTEMTIMER(id);
    return 0;
}
