/*
 * us29.c - probe US29: what Windows 3.1's USER does in the places the
 * USER audit found unsafe in w16 (docs/notes/user-audit-triage.md, in git history).
 *
 * Run as the shell, with its helper task US29A.EXE (us29a.c) and DLL
 * US29D.DLL (us29d.c) in C:\, on Windows 3.1 (tests/dropin.sh -r) and
 * with w16's USER (-f USER.EXE); the OBS lines of the two logs must be
 * identical (tests/boottest.sh case uaudit). The
 * run (the program's file name says which: US29.EXE the main run,
 * US29CR.EXE run cr, ...; shell= takes no arguments), each a boot of its
 * own because 3.1 may not survive what it does:
 *   (none)  the main run, sections:
 *     wsp     wsprintf with a precision (%.2d ...), a NULL %s
 *     tf      a timer not matching a PeekMessage filter still comes later
 *     fw      FindWindow of a 150-character title
 *     cb      combo boxes with a 300-character item selected
 *     nc      messages a window gets when WM_NCCREATE returns 0, WM_CREATE -1
 *     dc      a cache DC given back by DestroyWindow; CS_CLASSDC, CS_PARENTDC
 *     en      EnumWindows/EnumTaskWindows/EnumChildWindows whose callback
 *             destroys and reorders windows
 *     dw      the order of WM_DESTROY/WM_NCDESTROY when an owned window
 *             destroys its owner in its WM_DESTROY
 *     sq      200 key events with nobody reading: what is left
 *     cl      classes of a task that ended, of a module still in use by
 *             another instance, of a DLL freed
 *     se1     a task that ends while it handles a message another task
 *             sent it
 *     hk      a system-wide hook in a DLL, set by a task that ended
 *     mq      SetMessageQueue with a child window and a timer
 *   dw    an owned window that destroys its owner in its WM_DESTROY (3.1
 *         recurses there until its stack runs out: not compared)
 *   cr    windows destroyed by their own creation messages
 *   se    a task that ends inside a message sent to it while its own
 *         SendMessage is being handled (3.1 does not survive it)
 *   sd ss sc  SetParent of the desktop, of a window to itself, to its child
 *         (3.1 does not survive the first)
 *   hm    a system-wide hook whose DLL is freed, then GetMessage's hooks
 * Each run ends with "US29 end".
 */
#include <windows.h>

#define TK_REF "us29ref.h"
#include "tklog.h"

extern WORD FAR PASCAL WaitEvent(HTASK);
extern BOOL FAR PASCAL InitApp(HINSTANCE);
extern void __cdecl call_keybd_event(WORD ax, WORD bx);
extern void __cdecl call_mouse_event(WORD status, WORD x, WORD y, WORD buttons);

typedef WORD (FAR PASCAL *FN1)(WORD);
typedef DWORD (FAR PASCAL *FN0)(void);

static HINSTANCE hi;
static HWND hMain;
static char b[512];

/* ---- a record of events: short tokens, space-separated ---- */
static char rec[300];
static void rec_clear(void) { rec[0] = 0; }
static void rec_add(const char *s)
{
    int n = lstrlen(rec);
    if (n + lstrlen(s) + 2 >= sizeof rec)
        return;
    if (n)
        rec[n++] = ' ';
    lstrcpy(rec + n, s);
}
static void rec_hex(const char *pre, WORD v)
{
    char t[16];
    wsprintf(t, "%s%x", (LPSTR)pre, v);
    rec_add(t);
}

static void pump_once(void)
{
    MSG m;
    if (PeekMessage(&m, 0, 0, 0, PM_REMOVE)) {
        TranslateMessage(&m);
        DispatchMessage(&m);
    }
}

static void pump(DWORD ms)
{
    DWORD t = GetTickCount();
    while (GetTickCount() - t < ms)
        pump_once();
}

static BOOL wait_flag(volatile int *f, DWORD ms)
{
    DWORD t = GetTickCount();
    while (!*f && GetTickCount() - t < ms)
        pump_once();
    return *f != 0;
}

static BOOL wait_gone(DWORD ms)
{
    DWORD t = GetTickCount();
    while (GetModuleHandle("US29A") && GetTickCount() - t < ms)
        pump_once();
    pump(200);
    return GetModuleHandle("US29A") == 0;
}

static WORD usage(void)
{
    HMODULE m = GetModuleHandle("US29A");
    return m ? GetModuleUsage(m) : 0;
}

/* ---- the main window: the helper task's reports come here ---- */
static volatile int se_recv, se_back, cl_got, cw_got, hk_got;
static LONG se_r;
static WORD se_isw, cl_bits, cl_during, cw_bits[2], cw_n;
static HINSTANCE cl_hinst;
static HWND cw_hwnd[2], se_hwnd;

static WORD class_state(HINSTANCE h)
{
    WNDCLASS wc;
    return (GetClassInfo(h, "US29LOC", &wc) ? 1 : 0) | (GetClassInfo(0, "US29GLB", &wc) ? 2 : 0);
}

LRESULT FAR PASCAL __export __loadds main_proc(HWND h, UINT m, WPARAM w, LPARAM l)
{
    switch (m) {
    case WM_USER + 1:           /* se: US29A's window in l; it ends on WM_USER+2 */
        se_recv = 1;
        se_r = SendMessage((HWND)LOWORD(l), WM_USER + 2, 0, 0);
        se_isw = IsWindow((HWND)LOWORD(l));
        se_back = 1;
        return 77;
    case WM_USER + 10:          /* cl: the helper registered its classes */
        cl_bits = w;
        cl_hinst = (HINSTANCE)LOWORD(l);
        cl_during = class_state(cl_hinst);
        cl_got = 1;
        return 0;
    case WM_USER + 12:          /* cl: a waiting instance */
        if (cw_n < 2) {
            cw_bits[cw_n] = w;
            cw_hwnd[cw_n] = (HWND)HIWORD(l);
            cw_n++;
        }
        cw_got = 1;
        return 0;
    case WM_USER + 13:          /* se1: US29A waits in GetMessage */
        se_hwnd = (HWND)LOWORD(l);
        se_recv = 1;
        return 0;
    case WM_USER + 11:          /* hk: hooks set */
        hk_got = 1;
        return 0;
    }
    return DefWindowProc(h, m, w, l);
}

static UINT run_helper(const char *tail)
{
    char cmd[40];
    wsprintf(cmd, "C:\\US29A.EXE %s", (LPSTR)tail);
    return WinExec(cmd, SW_SHOWMINNOACTIVE);
}

static void reg(const char *name, WNDPROC p, UINT style)
{
    WNDCLASS wc;
    wc.style = style;
    wc.lpfnWndProc = p;
    wc.cbClsExtra = wc.cbWndExtra = 0;
    wc.hInstance = hi;
    wc.hIcon = 0;
    wc.hCursor = LoadCursor(NULL, IDC_ARROW);
    wc.hbrBackground = (HBRUSH)(COLOR_WINDOW + 1);
    wc.lpszMenuName = NULL;
    wc.lpszClassName = name;
    RegisterClass(&wc);
}

static HWND popup(const char *cls, const char *title, HWND owner)
{
    return CreateWindow(cls, title, WS_POPUP, 0, 0, 50, 50, owner, 0, hi, NULL);
}

/* ---- wsp ---- */
static void wsp1(const char *name, const char *fmt, WORD v)
{
    char o[64];
    wsprintf(o, fmt, v);
    obs_str(name, o);
}

static void sec_wsp(void)
{
    char o[64];
    wsp1("wsp.2d", "%.2d", 5);
    wsp1("wsp.5u", "%.5u", 42);
    wsp1("wsp.4x", "%.4x", 0xAB);
    wsp1("wsp.4X", "%.4X", 0xAB);
    wsp1("wsp5.3d", "[%5.3d]", 7);
    wsp1("wsp-5.3d", "[%-5.3d]", 7);
    wsp1("wsp.0d0", "[%.0d]", 0);
    wsp1("wsp.3dneg", "%.3d", (WORD)-5);
    wsp1("wsp05.3d", "[%05.3d]", 7);
    wsp1("wsp.3c", "[%.3c]", 'x');
    wsp1("wsp#.4x", "%#.4x", 0x1F);
    wsp1("wsp.10d", "%.10d", 1);
    wsp1("wsp.3i", "%.3i", 9);
    wsp1("wsp06d", "[%06d]", (WORD)-12);
    wsprintf(o, "%.3ld", 7L);
    obs_str("wsp.3ld", o);
    wsprintf(o, "%.6lX", 0x12345L);
    obs_str("wsp.6lX", o);
    wsprintf(o, "[%.2s]", (LPSTR)"abcdef");
    obs_str("wsp.2s", o);
    wsprintf(o, "[%5.2s]", (LPSTR)"abc");
    obs_str("wsp5.2s", o);
    wsprintf(o, "[%-5.2s]", (LPSTR)"abc");
    obs_str("wsp-5.2s", o);
    wsprintf(o, "[%.0s]", (LPSTR)"abc");
    obs_str("wsp.0s", o);
    /* NULL: 3.1 prints what it finds (two bytes, 60h 10h), w16 nothing:
       not compared */
    obs_env("wsp.null.n", wsprintf(o, "[%s]", (LPSTR)NULL));
    obs_env("wsp.null.1", (BYTE)o[1]);
    obs_env("wsp.null.2", (BYTE)o[2]);
}

/* ---- tf ---- */
static void sec_tf(void)
{
    HWND a = popup("US29X", "a", 0), bb = popup("US29X", "b", 0);
    MSG m;
    DWORD t;
    BOOL got;
    SetTimer(bb, 1, 60, NULL);
    t = GetTickCount();
    while (GetTickCount() - t < 300)
        ;
    obs("tf.filtered", PeekMessage(&m, a, 0, 0, PM_REMOVE));
    obs("tf.range", PeekMessage(&m, 0, WM_USER, WM_USER, PM_REMOVE));
    got = FALSE;
    t = GetTickCount();
    while (!got && GetTickCount() - t < 1000)
        got = PeekMessage(&m, 0, WM_TIMER, WM_TIMER, PM_REMOVE) && m.hwnd == bb;
    obs("tf.later", got);
    KillTimer(bb, 1);
    DestroyWindow(a);
    DestroyWindow(bb);
}

/* ---- fw ---- */
static void sec_fw(void)
{
    char t[210];
    HWND h;
    int i;
    for (i = 0; i < 150; i++)
        t[i] = 'Q';
    t[150] = 0;
    h = popup("US29X", t, 0);
    obs("fw.150", FindWindow(NULL, t) == h);
    t[127] = 0;
    obs("fw.127of150", FindWindow(NULL, t) == h);
    t[126] = 0;
    obs("fw.126of150", FindWindow(NULL, t) == h);
    DestroyWindow(h);
    {
        static const int lens[] = { 60, 64, 65, 66, 67, 68, 70, 72, 75, 78, 79, 80, 100, 127, 128, 200, 0 };
        char nm[16];
        for (i = 0; lens[i]; i++) {
            int k;
            for (k = 0; k < lens[i]; k++)
                t[k] = (char)('A' + k % 26);
            t[k] = 0;
            h = popup("US29X", t, 0);
            wsprintf(nm, "fw.len%d", lens[i]);
            obs(nm, FindWindow(NULL, t) == h);
            DestroyWindow(h);
        }
    }
}

/* ---- cb ---- */
static void sec_cb(void)
{
    static const DWORD styles[2] = { CBS_DROPDOWNLIST, CBS_DROPDOWN };
    static const char *names[2] = { "cb.dl", "cb.dd" };
    char item[301], nm[32];
    int i, k;
    for (i = 0; i < 300; i++)
        item[i] = (char)('0' + i % 10);
    item[300] = 0;
    for (k = 0; k < 2; k++) {
        HWND c = CreateWindow("COMBOBOX", NULL, WS_CHILD | WS_VISIBLE | styles[k] | CBS_HASSTRINGS,
                              10, 10, 200, 100, hMain, (HMENU)100, hi, NULL);
        SendMessage(c, CB_ADDSTRING, 0, (LPARAM)(LPSTR)item);
        SendMessage(c, CB_ADDSTRING, 0, (LPARAM)(LPSTR)"short");
        wsprintf(nm, "%s.lbtextlen", (LPSTR)names[k]);
        obs(nm, (WORD)SendMessage(c, CB_GETLBTEXTLEN, 0, 0));
        SendMessage(c, CB_SETCURSEL, 0, 0);
        InvalidateRect(c, NULL, TRUE);
        UpdateWindow(c);
        wsprintf(nm, "%s.textlen", (LPSTR)names[k]);
        obs(nm, (WORD)SendMessage(c, WM_GETTEXTLENGTH, 0, 0));
        b[0] = 0;
        wsprintf(nm, "%s.gettext", (LPSTR)names[k]);
        obs(nm, GetWindowText(c, b, sizeof b));
        wsprintf(nm, "%s.gettext20", (LPSTR)names[k]);
        obs(nm, GetWindowText(c, b, 20));
        obs_str(nm, b);
        SendMessage(c, CB_SETCURSEL, 1, 0);
        wsprintf(nm, "%s.short.textlen", (LPSTR)names[k]);
        obs(nm, (WORD)SendMessage(c, WM_GETTEXTLENGTH, 0, 0));
        wsprintf(nm, "%s.short.gettext", (LPSTR)names[k]);
        obs(nm, GetWindowText(c, b, sizeof b));
        wsprintf(nm, "%s.short.gettext3", (LPSTR)names[k]);
        obs(nm, GetWindowText(c, b, 3));
        obs_str(nm, b);
        SendMessage(c, CB_SETCURSEL, (WPARAM)-1, 0);
        wsprintf(nm, "%s.none.textlen", (LPSTR)names[k]);
        obs(nm, (WORD)SendMessage(c, WM_GETTEXTLENGTH, 0, 0));
        b[0] = 'z';
        b[1] = 0;
        wsprintf(nm, "%s.none.gettext", (LPSTR)names[k]);
        obs(nm, GetWindowText(c, b, sizeof b));
        obs_str(nm, b);
        DestroyWindow(c);
    }
}

/* ---- nc ---- */
static int nc_mode;
LRESULT FAR PASCAL __export __loadds nc_proc(HWND h, UINT m, WPARAM w, LPARAM l)
{
    rec_hex("", m);
    if (m == WM_NCCREATE && nc_mode == 0)
        return 0;
    if (m == WM_CREATE && nc_mode == 1)
        return -1;
    if (m == WM_NCCREATE && nc_mode == 2) {
        DestroyWindow(h);
        return 1;
    }
    if (m == WM_CREATE && nc_mode == 3) {
        DestroyWindow(h);
        return 0;
    }
    if (m == WM_GETMINMAXINFO && nc_mode == 4)
        DestroyWindow(h);
    if (m == WM_SIZE && nc_mode == 5)
        DestroyWindow(h);
    return DefWindowProc(h, m, w, l);
}

static void nc_run(const char *name, int mode, DWORD style, HWND parent)
{
    HWND h;
    nc_mode = mode;
    rec_clear();
    h = CreateWindow("US29N", "n", style, 0, 0, 50, 50, parent, 0, hi, NULL);
    obs(name, h != 0);
    obs_str(name, rec);
    if (h && IsWindow(h))
        DestroyWindow(h);
}

static void sec_nc(void)
{
    nc_run("nc.ncfail.popup", 0, WS_POPUP, 0);
    nc_run("nc.ncfail.over", 0, WS_OVERLAPPEDWINDOW, 0);
    nc_run("nc.ncfail.child", 0, WS_CHILD | WS_VISIBLE, hMain);
    nc_run("nc.crfail.popup", 1, WS_POPUP, 0);
    nc_run("nc.crfail.child", 1, WS_CHILD | WS_VISIBLE, hMain);
    nc_run("nc.mm.popthick", 0, WS_POPUP | WS_THICKFRAME, 0);
    nc_run("nc.mm.popcaption", 0, WS_POPUP | WS_CAPTION, 0);
    nc_run("nc.mm.overthin", 0, WS_OVERLAPPED | WS_CAPTION, 0);
    nc_run("nc.mm.overbare", 0, WS_OVERLAPPED, 0);
    nc_run("nc.mm.childthick", 0, WS_CHILD | WS_THICKFRAME, hMain);
    nc_run("nc.mm.popmax", 0, WS_POPUP | WS_MAXIMIZEBOX, 0);
}

static void sec_cr(void)
{
    nc_run("cr.ncdestroy", 2, WS_POPUP, 0);
    nc_run("cr.crdestroy", 3, WS_POPUP, 0);
    nc_run("cr.mmdestroy", 4, WS_OVERLAPPEDWINDOW, 0);
    nc_run("cr.sizedestroy", 5, WS_POPUP | WS_VISIBLE, 0);
    nc_run("cr.sizedestroy.child", 5, WS_CHILD | WS_VISIBLE, hMain);
}

/* ---- dc ---- */
static void sec_dc(void)
{
    HWND x = popup("US29X", "x", 0), w1, w2, c;
    HDC d, dd[5], d1, d2;
    HPEN pen = CreatePen(PS_SOLID, 1, RGB(255, 0, 0)), old;
    int i, kept = 0, transp = 0;
    RECT r;
    DWORD org;
    POINT p;
    d = GetDC(x);
    SelectObject(d, pen);
    SetBkMode(d, TRANSPARENT);
    DestroyWindow(x);
    for (i = 0; i < 5; i++)
        dd[i] = GetDC(hMain);
    for (i = 0; i < 5; i++) {
        if (!dd[i])
            continue;
        old = SelectObject(dd[i], GetStockObject(BLACK_PEN));
        if (old == pen)
            kept++;
        if (GetBkMode(dd[i]) == TRANSPARENT)
            transp++;
        SelectObject(dd[i], old);
    }
    for (i = 0; i < 5; i++)
        if (dd[i])
            ReleaseDC(hMain, dd[i]);
    obs("dc.freed.pen", kept);
    obs("dc.freed.bkmode", transp);
    DeleteObject(pen);

    /* CS_CLASSDC: one DC for the class's windows */
    w1 = CreateWindow("US29CD", "1", WS_CHILD | WS_VISIBLE, 10, 10, 40, 40, hMain, 0, hi, NULL);
    w2 = CreateWindow("US29CD", "2", WS_CHILD | WS_VISIBLE, 60, 20, 40, 40, hMain, 0, hi, NULL);
    d1 = GetDC(w1);
    SetBkMode(d1, TRANSPARENT);
    ReleaseDC(w1, d1);
    d2 = GetDC(w2);
    obs("dc.class.same", d1 == d2);
    obs("dc.class.bkmode", GetBkMode(d2));
    org = GetDCOrg(d2);
    p.x = p.y = 0;
    ClientToScreen(w2, &p);
    obs("dc.class.org", LOWORD(org) == (WORD)p.x && HIWORD(org) == (WORD)p.y);
    ReleaseDC(w2, d2);
    d1 = GetDC(w1);
    d2 = GetDC(w2);
    obs("dc.class.both", d1 == d2);
    org = GetDCOrg(d1);
    obs("dc.class.both.org1", LOWORD(org) == (WORD)p.x && HIWORD(org) == (WORD)p.y);
    ReleaseDC(w2, d2);
    ReleaseDC(w1, d1);
    d1 = GetDC(hMain);
    obs("dc.class.notcache", d1 != d2);
    ReleaseDC(hMain, d1);
    d1 = GetWindowDC(w1);
    obs("dc.class.windowdc", d1 == d2);
    ReleaseDC(w1, d1);
    DestroyWindow(w1);
    DestroyWindow(w2);

    /* CS_PARENTDC: the parent's clipping */
    c = CreateWindow("US29PD", "p", WS_CHILD | WS_VISIBLE, 10, 10, 20, 20, hMain, 0, hi, NULL);
    d = GetDC(c);
    GetClipBox(d, &r);
    wsprintf(b, "%d %d %d %d", r.left, r.top, r.right, r.bottom);
    obs_str("dc.parent.clip", b);
    org = GetDCOrg(d);
    p.x = p.y = 0;
    ClientToScreen(c, &p);
    obs("dc.parent.org", LOWORD(org) == (WORD)p.x && HIWORD(org) == (WORD)p.y);
    ReleaseDC(c, d);
    DestroyWindow(c);
}

/* ---- en ---- */
static HWND ew[5];
static int ecount, emode;
BOOL FAR PASCAL __export __loadds enum_cb(HWND h, LPARAM l)
{
    int i;
    char t[4];
    (void)l;
    if (++ecount > 40)
        return FALSE;
    for (i = 0; i < 5; i++)
        if (h == ew[i])
            break;
    if (i == 5)
        return TRUE;
    t[0] = (char)(i == 4 ? 'g' : '1' + i);
    t[1] = IsWindow(h) ? 0 : 'd';
    t[2] = 0;
    rec_add(t);
    if (emode == 0 && i == 3) {         /* top-level: 4 3 2 1 in z-order */
        DestroyWindow(ew[2]);
        SetWindowPos(ew[0], HWND_TOP, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
    }
    if (emode == 1 && i == 0) {         /* children: 1 2 3 4 */
        DestroyWindow(ew[1]);
        SetWindowPos(ew[3], HWND_TOP, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
    }
    return TRUE;
}

static void en_make_top(void)
{
    int i;
    for (i = 0; i < 4; i++)
        ew[i] = popup("US29X", "e", 0);
    ew[4] = 0;
}

static void en_free(void)
{
    int i;
    for (i = 0; i < 5; i++)
        if (ew[i] && IsWindow(ew[i]))
            DestroyWindow(ew[i]);
}

static void sec_en(void)
{
    HWND p;
    int i;
    en_make_top();
    rec_clear();
    ecount = 0;
    emode = 0;
    EnumWindows((WNDENUMPROC)enum_cb, 0);
    obs_str("en.windows", rec);
    en_free();

    en_make_top();
    rec_clear();
    ecount = 0;
    EnumTaskWindows(GetCurrentTask(), (WNDENUMPROC)enum_cb, 0);
    obs_str("en.task", rec);
    en_free();

    p = popup("US29X", "p", 0);
    for (i = 0; i < 4; i++)
        ew[i] = CreateWindow("US29X", "c", WS_CHILD, 0, 0, 10, 10, p, (HMENU)i, hi, NULL);
    ew[4] = CreateWindow("US29X", "g", WS_CHILD, 0, 0, 5, 5, ew[2], 0, hi, NULL);
    rec_clear();
    ecount = 0;
    emode = 1;
    EnumChildWindows(p, (WNDENUMPROC)enum_cb, 0);
    obs_str("en.child", rec);
    DestroyWindow(p);
}

/* ---- dw ---- */
static HWND dw_o, dw_w;
static int dw_mode;
LRESULT FAR PASCAL __export __loadds dw_proc(HWND h, UINT m, WPARAM w, LPARAM l)
{
    if (m == WM_DESTROY || m == WM_NCDESTROY) {
        char t[8];
        wsprintf(t, "%c%x", h == dw_o ? 'O' : h == dw_w ? 'W' : '?', m);
        rec_add(t);
        if (m == WM_DESTROY && h == dw_w && dw_mode == 1)
            DestroyWindow(dw_o);
    }
    return DefWindowProc(h, m, w, l);
}

static void dw_run(const char *name, int mode, BOOL owner_first)
{
    dw_mode = 0;
    dw_o = popup("US29O", "o", 0);
    dw_w = popup("US29O", "w", dw_o);
    dw_mode = mode;
    rec_clear();
    DestroyWindow(owner_first ? dw_o : dw_w);
    obs_str(name, rec);
    wsprintf(b, "%s.alive", (LPSTR)name);
    obs(b, (IsWindow(dw_o) ? 1 : 0) | (IsWindow(dw_w) ? 2 : 0));
    dw_mode = 0;
    if (IsWindow(dw_w))
        DestroyWindow(dw_w);
    if (IsWindow(dw_o))
        DestroyWindow(dw_o);
}

static void sec_dw(void)
{
    dw_run("dw.owner", 0, TRUE);
}

static void sec_dw2(void)
{
    dw_run("dw.owned.kills", 1, FALSE);
    dw_run("dw.owner.ownedkills", 1, TRUE);
}

/* ---- sq ---- */
static void sec_sq(void)
{
    MSG m;
    int i, down = 0, up = 0, other = 0;
    UINT last = 0;
    SetActiveWindow(hMain);
    SetFocus(hMain);
    pump(300);
    while (PeekMessage(&m, 0, 0, 0, PM_REMOVE))
        ;
    for (i = 0; i < 100; i++) {
        call_keybd_event(0x0041, 0x001E);
        call_keybd_event(0x8041, 0x001E);
    }
    while (PeekMessage(&m, 0, WM_KEYFIRST, WM_KEYLAST, PM_REMOVE)) {
        if (m.message == WM_KEYDOWN)
            down++;
        else if (m.message == WM_KEYUP)
            up++;
        else
            other++;
        last = m.message;
    }
    obs("sq.down", down);
    obs("sq.up", up);
    obs("sq.other", other);
    obs("sq.last", last);
    obs("sq.state", (GetKeyState('A') & 0x8000) != 0);
    /* the left button, at the window's middle */
    pump(300);
    while (PeekMessage(&m, 0, 0, 0, PM_REMOVE))
        ;
    {
        POINT p;
        WORD x, y;
        p.x = 200;
        p.y = 150;
        ClientToScreen(hMain, &p);
        x = (WORD)MulDiv(p.x, 32767, GetSystemMetrics(SM_CXSCREEN)) * 2;
        y = (WORD)MulDiv(p.y, 32767, GetSystemMetrics(SM_CYSCREEN)) * 2;
        call_mouse_event(0x8001, x, y, 0);      /* MOVE | ABSOLUTE */
        pump(200);
        while (PeekMessage(&m, 0, 0, 0, PM_REMOVE))
            ;
        down = up = other = 0;
        for (i = 0; i < 100; i++) {
            call_mouse_event(0x8002, x, y, 0);  /* LEFTDOWN */
            call_mouse_event(0x8004, x, y, 0);  /* LEFTUP */
        }
        last = 0;
        while (PeekMessage(&m, 0, WM_MOUSEFIRST, WM_MOUSELAST, PM_REMOVE)) {
            if (m.message == WM_LBUTTONDOWN || m.message == WM_LBUTTONDBLCLK)
                down++;
            else if (m.message == WM_LBUTTONUP)
                up++;
            else
                other++;
            last = m.message;
        }
        obs("sq.m.down", down);
        obs("sq.m.up", up);
        obs("sq.m.other", other);
        obs("sq.m.last", last);
        obs("sq.m.state", (GetKeyState(VK_LBUTTON) & 0x8000) != 0);
    }
}

/* ---- cl ---- */
static void sec_cl(void)
{
    WNDCLASS wc;
    HINSTANCE d;
    cl_got = 0;
    obs("cl.run", run_helper("c") > 32);
    obs("cl.reported", wait_flag(&cl_got, 20000));
    obs("cl.bits", cl_bits);
    obs("cl.during", cl_during);
    obs("cl.gone", wait_gone(20000));
    obs("cl.after", class_state(cl_hinst));
    obs("cl.after.null", class_state(0));
    cl_got = 0;
    run_helper("c");
    wait_flag(&cl_got, 20000);
    obs("cl.rerun.bits", cl_bits);
    wait_gone(20000);

    /* two instances: the classes last while one runs */
    cw_n = 0;
    cw_got = 0;
    run_helper("w");
    wait_flag(&cw_got, 20000);
    cw_got = 0;
    run_helper("w");
    wait_flag(&cw_got, 20000);
    obs("cl.w.n", cw_n);
    obs("cl.w.bits0", cw_bits[0]);
    obs("cl.w.bits1", cw_bits[1]);
    obs("cl.w.usage", usage());
    PostMessage(cw_hwnd[0], WM_CLOSE, 0, 0);
    {
        DWORD t = GetTickCount();
        while (usage() > 1 && GetTickCount() - t < 20000)
            pump_once();
        pump(200);
    }
    obs("cl.w.usage1", usage());
    obs("cl.w.after1", class_state(0));
    PostMessage(cw_hwnd[1], WM_CLOSE, 0, 0);
    obs("cl.w.gone", wait_gone(20000));
    obs("cl.w.after2", class_state(0));

    /* a DLL's classes */
    d = LoadLibrary("US29D.DLL");
    obs("cl.dll.loaded", d >= 32);
    obs("cl.dll.global", GetClassInfo(0, "US29DG", &wc) != 0);
    obs("cl.dll.local", GetClassInfo(d, "US29DL", &wc) != 0);
    FreeLibrary(d);
    obs("cl.dll.freed", GetModuleHandle("US29D") == 0);
    obs("cl.dll.global.after", GetClassInfo(0, "US29DG", &wc) != 0);
    obs("cl.dll.local.after", GetClassInfo(d, "US29DL", &wc) != 0);
    d = LoadLibrary("US29D.DLL");
    obs("cl.dll.again", GetClassInfo(0, "US29DG", &wc) != 0);
    FreeLibrary(d);
}

/* ---- se ---- */
static void sec_se1(void)
{
    LONG r;
    se_recv = 0;
    obs("se1.run", run_helper("r") > 32);
    obs("se1.ready", wait_flag(&se_recv, 20000));
    r = SendMessage(se_hwnd, WM_USER + 2, 0, 0);
    obs("se1.r.lo", LOWORD(r));
    obs("se1.r.hi", HIWORD(r));
    obs("se1.isw", IsWindow(se_hwnd));
    obs("se1.gone", wait_gone(20000));
}

static void sec_se(void)
{
    MSG m;
    BOOL got = FALSE;
    DWORD t;
    se_recv = se_back = 0;
    se_r = -1;
    obs("se.run", run_helper("s") > 32);
    obs("se.recv", wait_flag(&se_recv, 20000));
    obs("se.back", wait_flag(&se_back, 20000));
    obs("se.r.lo", LOWORD(se_r));
    obs("se.r.hi", HIWORD(se_r));
    obs("se.isw", se_isw);
    obs("se.gone", wait_gone(20000));
    PostMessage(hMain, WM_USER + 50, 1, 2);
    t = GetTickCount();
    while (!got && GetTickCount() - t < 2000)
        got = PeekMessage(&m, hMain, WM_USER + 50, WM_USER + 50, PM_REMOVE);
    obs("se.queue", got);
}

/* ---- hk ---- */
static HINSTANCE hkdll;
static void sec_hk(void)
{
    FN0 counts, reset;
    FN1 seta, unhook;
    MSG m;
    DWORD c;
    int i;
    hkdll = LoadLibrary("US29D.DLL");
    counts = (FN0)GetProcAddress(hkdll, "US29D_COUNTS");
    reset = (FN0)GetProcAddress(hkdll, "US29D_RESET");
    seta = (FN1)GetProcAddress(hkdll, "US29D_SETA");
    unhook = (FN1)GetProcAddress(hkdll, "US29D_UNHOOK");
    hk_got = 0;
    obs("hk.run", run_helper("h") > 32);
    obs("hk.reported", wait_flag(&hk_got, 20000));
    obs("hk.gone", wait_gone(20000));
    reset();
    PostMessage(hMain, WM_USER + 60, 0, 0);
    for (i = 0; i < 3; i++)
        PeekMessage(&m, 0, 0, 0, PM_NOREMOVE);
    PeekMessage(&m, hMain, WM_USER + 60, WM_USER + 60, PM_REMOVE);
    c = counts();
    obs("hk.after.sys", LOWORD(c) != 0);
    obs("hk.after.task", HIWORD(c) != 0);
    unhook(0);
    obs("hk.own.set", seta(0));
    reset();
    PostMessage(hMain, WM_USER + 60, 0, 0);
    for (i = 0; i < 3; i++)
        PeekMessage(&m, 0, 0, 0, PM_NOREMOVE);
    PeekMessage(&m, hMain, WM_USER + 60, WM_USER + 60, PM_REMOVE);
    c = counts();
    obs("hk.own.sys", LOWORD(c) != 0);
    unhook(0);
    FreeLibrary(hkdll);
}

static void sec_hm(void)
{
    FN1 seta;
    MSG m;
    int i;
    hkdll = LoadLibrary("US29D.DLL");
    seta = (FN1)GetProcAddress(hkdll, "US29D_SETA");
    obs("hm.set", seta(0));
    FreeLibrary(hkdll);
    obs("hm.freed", GetModuleHandle("US29D") == 0);
    PostMessage(hMain, WM_USER + 60, 0, 0);
    for (i = 0; i < 3; i++)
        PeekMessage(&m, 0, 0, 0, PM_NOREMOVE);
    obs("hm.alive", 1);
}

/* ---- mq ---- */
static volatile int mq_posted, mq_timer;
LRESULT FAR PASCAL __export __loadds mq_proc(HWND h, UINT m, WPARAM w, LPARAM l)
{
    if (m == WM_USER + 5)
        mq_posted = 1;
    if (m == WM_TIMER)
        mq_timer = 1;
    return DefWindowProc(h, m, w, l);
}

static void sec_mq(void)
{
    HWND p = popup("US29M", "q", 0), c;
    c = CreateWindow("US29M", "c", WS_CHILD, 0, 0, 10, 10, p, 0, hi, NULL);
    SetTimer(c, 1, 60, NULL);
    /* 3.1 returns 1467h, leaves the window on the freed queue (its
       GetWindowTask another task's, PostMessage to it failing, its timer
       gone); w16 moves the windows and timers: not compared */
    obs_env("mq.set", SetMessageQueue(20) != 0);
    obs_env("mq.task", GetWindowTask(c) == GetCurrentTask());
    mq_posted = mq_timer = 0;
    obs_env("mq.post", PostMessage(c, WM_USER + 5, 0, 0));
    obs_env("mq.posted", wait_flag(&mq_posted, 1000));
    obs_env("mq.timer", wait_flag(&mq_timer, 1000));
    KillTimer(c, 1);
    DestroyWindow(p);
}

/* ---- sp ---- */
static void sec_sp(int which)
{
    HWND k = CreateWindow("US29X", "k", WS_CHILD | WS_VISIBLE, 0, 0, 30, 30, hMain, 0, hi, NULL);
    HWND kk = CreateWindow("US29X", "kk", WS_CHILD | WS_VISIBLE, 0, 0, 10, 10, k, 0, hi, NULL);
    HWND t = popup("US29X", "t", 0), r;
    if (which != 0)
        goto self;
    out("sp desktop\r\n");
    r = SetParent(GetDesktopWindow(), t);
    obs("sp.desktop", r == 0 ? 0 : r == t ? 1 : 2);
    obs("sp.desktop.parent", GetParent(GetDesktopWindow()) == t);
    return;
self:
    if (which != 1)
        goto child;
    out("sp self\r\n");
    r = SetParent(k, k);
    obs("sp.self", r == 0 ? 0 : r == hMain ? 1 : r == k ? 2 : 3);
    obs("sp.self.parent", GetParent(k) == k ? 2 : GetParent(k) == hMain);
    if (GetParent(k) != hMain)
        SetParent(k, hMain);
    return;
child:
    out("sp child\r\n");
    r = SetParent(k, kk);
    obs("sp.child", r == 0 ? 0 : r == hMain ? 1 : 3);
    obs("sp.child.parent", GetParent(k) == kk ? 2 : GetParent(k) == hMain);
    out("sp done\r\n");
}

int __cdecl tmain(void)
{
    char path[128], mode[4];
    int i, n;
    hi = (HINSTANCE)init_regs[5];
    WaitEvent(0);
    InitApp(hi);
    /* the run: the file name's letters after US29 (US29CR.EXE: cr) */
    n = GetModuleFileName(hi, path, sizeof path);
    while (n > 0 && path[n - 1] != '\\')
        n--;
    for (i = 0; i < 3 && path[n + 4 + i] && path[n + 4 + i] != '.'; i++)
        mode[i] = (char)(path[n + 4 + i] | 0x20);
    mode[i] = 0;
    out("US29 start ");
    out(mode);
    out("\r\n");

    reg("US29P", (WNDPROC)main_proc, 0);
    reg("US29X", DefWindowProc, 0);
    reg("US29N", (WNDPROC)nc_proc, 0);
    reg("US29O", (WNDPROC)dw_proc, 0);
    reg("US29M", (WNDPROC)mq_proc, 0);
    reg("US29CD", DefWindowProc, CS_CLASSDC);
    reg("US29PD", DefWindowProc, CS_PARENTDC);
    hMain = CreateWindow("US29P", "US29", WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU,
                         0, 0, 400, 300, 0, 0, hi, NULL);
    ShowWindow(hMain, SW_SHOWNORMAL);
    UpdateWindow(hMain);
    pump(200);

    if (!mode[0]) {
        out("wsp\r\n"); sec_wsp();
        out("tf\r\n"); sec_tf();
        out("fw\r\n"); sec_fw();
        out("cb\r\n"); sec_cb();
        out("nc\r\n"); sec_nc();
        out("dc\r\n"); sec_dc();
        out("en\r\n"); sec_en();
        out("dw\r\n"); sec_dw();
        out("sq\r\n"); sec_sq();
        out("cl\r\n"); sec_cl();
        out("se1\r\n"); sec_se1();
        out("hk\r\n"); sec_hk();
        out("mq\r\n"); sec_mq();
    } else if (!lstrcmp(mode, "dw")) {
        sec_dw2();
    } else if (!lstrcmp(mode, "cr")) {
        sec_cr();
    } else if (!lstrcmp(mode, "sd")) {
        sec_sp(0);
    } else if (!lstrcmp(mode, "ss")) {
        sec_sp(1);
    } else if (!lstrcmp(mode, "sc")) {
        sec_sp(2);
    } else if (!lstrcmp(mode, "se")) {
        sec_se();
    } else if (!lstrcmp(mode, "hm")) {
        sec_hm();
    }
    out("US29 end\r\n");
    DestroyWindow(hMain);
    return 0;
}
