/*
 * us39.c - probe US39: SetFocus on a disabled or minimised window (or a
 * child of one), and the activation around another task's MessageBox
 * owned by one of this task's windows.
 *
 * Found with Microsoft Query (Visual C++ 1.52): its Cue Cards, another
 * task, say "Cue Cards uses Arial font..." with a MessageBox owned by
 * Query's main window, and Query then calls SetFocus on that window,
 * which the box has disabled. On Windows 3.1 the box stays active; on
 * w16 the SetFocus activated Query's window again, the box took no keys,
 * and closing Query closed the Cue Cards window under its own box.
 *
 * Run as the shell on Windows 3.1 (tests/dropin.sh -r) and with w16's
 * USER (-f USER.EXE); the OBS lines of the two logs must be identical
 * (tests/boottest.sh case msquery). Windows are logged by number: 0 none,
 * 1 the main window A, 2 the second top-level window B, 3 A's child C,
 * 4 B's child D, 5 a window of another task, 9 any other window.
 * Sections (each starts with A active and the focus on A, all enabled
 * and open):
 *   dis      EnableWindow(A, FALSE) with the focus on A, on C; then
 *            SetFocus(A) and SetFocus(C) with A disabled, A active or B
 *   dchild   SetFocus(C) with C disabled, A active or B
 *   min      SetFocus(A) and SetFocus(C) with A minimised and B active
 *   en       SetFocus(A) and SetFocus(C) with B active (all enabled)
 *   dlg      a modeless dialog (an edit, 6, and a push button, 7; the
 *            dialog 8) with the focus on the button (dlg.bt) or the edit
 *            (dlg.e) and a MessageBox owned by it, ended from a timer: the
 *            state before, inside (.in, .box: the box active) and after;
 *            then Alt+B (the button, "&B") and Alt+C (a check box 12,
 *            "&C") given to IsDialogMessage with the focus in the edit:
 *            the control's WM_COMMAND (.cmd), the focus then (.cmdfoc)
 *            and after
 *   mb       the program run again ("US39 B"): task B finds A, makes a
 *            hidden window and calls MessageBox(A, ...); from that
 *            window's timer, in the box's modal loop, it asks A (SendMessage) to log the state, to
 *            call SetFocus(A), to log again, then ends the box with
 *            EndDialog; A logs the state after the box
 * Each step logs SetFocus's answer (.r), GetActiveWindow (.act),
 * GetFocus (.foc) and whether A is enabled (.en).
 */
#include <windows.h>

#define TK_REF "us39ref.h"
#include "tklog.h"

extern WORD FAR PASCAL WaitEvent(HTASK);
extern BOOL FAR PASCAL InitApp(HINSTANCE);

#define UM_STATE (WM_USER + 1)
#define UM_FOCUS (WM_USER + 2)
#define UM_DONE  (WM_USER + 5)

static HINSTANCE hi;
static HWND hA, hB, hC, hD;
static char nm[64];
static BOOL bdone;
/* task B's (its window procedure runs with the first instance's DS,
   __loadds: btimer uses only what that DS holds) */
static HWND bA, bBox, bOwn;
static int bstep;
static void btimer(HWND h, UINT id);
/* the dlg section: a modeless dialog, its edit and push button */
static HWND dDlg, dE, dBt;
static const char *dpfx;
static HWND find_box(HWND owner);

static void pump(DWORD ms)
{
    MSG m;
    DWORD t = GetTickCount();
    do {
        while (PeekMessage(&m, 0, 0, 0, PM_REMOVE)) {
            TranslateMessage(&m);
            DispatchMessage(&m);
        }
    } while (GetTickCount() - t < ms);
}

static WORD wn(HWND h)
{
    if (!h)
        return 0;
    if (h == hA)
        return 1;
    if (h == hB)
        return 2;
    if (h == hC)
        return 3;
    if (h == hD)
        return 4;
    if (h && h == dE)
        return 6;
    if (h && h == dBt)
        return 7;
    if (h && h == dDlg)
        return 8;
    if (GetWindowTask(h) != GetCurrentTask())
        return 5;
    return 9;
}

static void o(const char *pfx, const char *what, WORD v)
{
    wsprintf(nm, "%s.%s", (LPCSTR)pfx, (LPCSTR)what);
    obs(nm, v);
}

static void state(const char *pfx)
{
    o(pfx, "act", wn(GetActiveWindow()));
    o(pfx, "foc", wn(GetFocus()));
    o(pfx, "en", IsWindowEnabled(hA) ? 1 : 0);
}

static void sf(const char *pfx, HWND h)
{
    HWND r = SetFocus(h);
    o(pfx, "r", wn(r));
    state(pfx);
}

/* back to the start: everything enabled and open, A active, focus on A */
static void reset(void)
{
    EnableWindow(hA, TRUE);
    EnableWindow(hC, TRUE);
    if (IsIconic(hA))
        ShowWindow(hA, SW_RESTORE);
    SetActiveWindow(hA);
    SetFocus(hA);
    pump(100);
}

LRESULT FAR PASCAL __export __loadds main_proc(HWND h, UINT m, WPARAM w, LPARAM l)
{
    static int n;
    char p[16];
    if (h == hA && m == UM_STATE) {
        wsprintf(p, "mb.s%d", (int)w);
        state(p);
        return 0;
    }
    if (h == hA && m == UM_FOCUS) {
        sf("mb.sf", hA);
        return 0;
    }
    if (h == hA && m == UM_DONE) {
        bdone = TRUE;
        return 0;
    }
    if (h == hA && m == WM_TIMER && w == 5) {
        /* the dlg section: the state inside the box, then the box ends */
        HWND box = find_box(dDlg);
        char q[24];
        KillTimer(hA, 5);
        wsprintf(q, "%s.in", (LPCSTR)dpfx);
        state(q);
        o(q, "box", box != 0 && GetActiveWindow() == box);
        if (box)
            EndDialog(box, IDOK);
        return 0;
    }
    if (m == WM_TIMER && hA && GetWindowTask(h) != GetWindowTask(hA)) {
        btimer(h, w);
        return 0;
    }
    (void)n;
    return DefWindowProc(h, m, w, l);
}

/* ---- task B: the MessageBox owned by A ---- */

/* the box: a top-level dialog owned by OWNER (no enumeration callback: a
   second instance's __loadds callback would run on whatever DS the
   shared code segment's fixup names) */
static HWND find_box(HWND owner)
{
    char c[16];
    HWND h;
    for (h = GetWindow(GetDesktopWindow(), GW_CHILD); h; h = GetWindow(h, GW_HWNDNEXT)) {
        GetClassName(h, c, sizeof c);
        if (GetWindow(h, GW_OWNER) == owner && lstrcmp(c, "#32770") == 0)
            return h;
    }
    return 0;
}

static void btimer(HWND h, UINT id)
{
    switch (bstep++) {
    case 0:
        SendMessage(hA, UM_STATE, 1, 0);
        break;
    case 1:
        SendMessage(hA, UM_FOCUS, 0, 0);
        break;
    case 2:
        SendMessage(hA, UM_STATE, 2, 0);
        break;
    case 3:
        KillTimer(h, id);
        bBox = find_box(hA);
        SendMessage(hA, UM_STATE, bBox ? 7 : 8, 0);
        if (bBox)
            EndDialog(bBox, IDOK);
        break;
    }
}

static int task_b(void)
{
    bA = FindWindow("US39M", "US39 A");
    if (!bA)
        return 1;
    bOwn = CreateWindow("US39M", "US39 B", WS_OVERLAPPEDWINDOW, 0, 0, 200, 100, 0, 0, hi, NULL);
    SetTimer(bOwn, 1, 300, NULL);
    MessageBox(bA, "US39 box", "US39", MB_OK | MB_ICONASTERISK);
    pump(200);
    SendMessage(bA, UM_STATE, 3, 0);
    DestroyWindow(bOwn);
    PostMessage(bA, UM_DONE, 0, 0);
    return 0;
}

/* ---- the dlg section ---- */
static HWND cmd_focus;                  /* GetFocus in the last WM_COMMAND */
static WORD cmd_id;

BOOL FAR PASCAL __export __loadds dlg_proc(HWND h, UINT m, WPARAM w, LPARAM l)
{
    (void)h, (void)l;
    if (m == WM_COMMAND && (w == 11 || w == 12)) {
        cmd_id = w;
        cmd_focus = GetFocus();
        return TRUE;
    }
    return m == WM_INITDIALOG;
}

/* a dialog template (Windows 3.x layout): an edit (10), a push button
   (11, "&B") and a check box (12, "&C"), all tab stops */
static BYTE tmpl[128];

static BYTE *put_w(BYTE *p, WORD v)
{
    *p++ = (BYTE)v;
    *p++ = (BYTE)(v >> 8);
    return p;
}

static BYTE *put_s(BYTE *p, const char *s)
{
    do
        *p++ = *s;
    while (*s++);
    return p;
}

static BYTE *put_item(BYTE *p, int x, int y, int cx, int cy, WORD id, DWORD style, BYTE cls, const char *text)
{
    p = put_w(p, x); p = put_w(p, y); p = put_w(p, cx); p = put_w(p, cy);
    p = put_w(p, id);
    p = put_w(p, LOWORD(style)); p = put_w(p, HIWORD(style));
    *p++ = cls;
    p = put_s(p, text);
    *p++ = 0;                           /* no creation data */
    return p;
}

static void make_tmpl(void)
{
    BYTE *p = tmpl;
    DWORD st = WS_POPUP | WS_CAPTION | WS_SYSMENU | DS_MODALFRAME;
    p = put_w(p, LOWORD(st)); p = put_w(p, HIWORD(st));
    *p++ = 3;
    p = put_w(p, 10); p = put_w(p, 60); p = put_w(p, 120); p = put_w(p, 50);
    p = put_s(p, "");                   /* no menu */
    p = put_s(p, "");                   /* the dialog class */
    p = put_s(p, "US39 D");
    p = put_item(p, 5, 5, 60, 12, 10, WS_CHILD | WS_VISIBLE | WS_BORDER | WS_TABSTOP | ES_LEFT, 0x81, "");
    p = put_item(p, 70, 5, 40, 14, 11, WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_PUSHBUTTON, 0x80, "&B");
    put_item(p, 5, 25, 40, 12, 12, WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_AUTOCHECKBOX, 0x80, "&C");
}

/* a MessageBox owned by the dialog with the focus on FOC, ended from A's
   timer; the state before, inside and after */
static void dlg_box(const char *pfx, HWND foc)
{
    char q[24];
    dpfx = pfx;
    SetActiveWindow(dDlg);
    SetFocus(foc);
    pump(100);
    wsprintf(q, "%s.before", (LPCSTR)pfx);
    state(q);
    SetTimer(hA, 5, 300, NULL);
    MessageBox(dDlg, "US39 dlg", "US39", MB_OK);
    pump(200);
    wsprintf(q, "%s.after", (LPCSTR)pfx);
    state(q);
}

static void mn_key(const char *pfx, char ch)
{
    MSG m;
    char q[24];
    SetActiveWindow(dDlg);
    SetFocus(dE);
    pump(100);
    cmd_id = 0;
    cmd_focus = 0;
    m.hwnd = dE;
    m.message = WM_SYSCHAR;
    m.wParam = ch;
    m.lParam = 0x20300001L;             /* Alt down */
    m.time = GetTickCount();
    m.pt.x = m.pt.y = 0;
    o(pfx, "idm", IsDialogMessage(dDlg, &m));
    pump(100);
    o(pfx, "cmd", cmd_id);
    wsprintf(q, "%s.cmdfoc", (LPCSTR)pfx);
    obs(q, wn(cmd_focus));
    state(pfx);
}

static void sec_dlg(void)
{
    make_tmpl();
    dDlg = CreateDialogIndirect(hi, (LPCSTR)tmpl, hA, (DLGPROC)dlg_proc);
    if (!dDlg) {
        o("dlg", "made", 0);
        return;
    }
    dE = GetDlgItem(dDlg, 10);
    dBt = GetDlgItem(dDlg, 11);
    ShowWindow(dDlg, SW_SHOW);
    pump(100);
    dlg_box("dlg.bt", dBt);
    dlg_box("dlg.e", dE);
    /* a mnemonic (Alt+B, Alt+C) through IsDialogMessage with the focus
       in the edit: where the focus is in the control's WM_COMMAND and
       after */
    mn_key("dlg.mb", 'b');
    mn_key("dlg.mc", 'c');
    DestroyWindow(dDlg);
    dDlg = dE = dBt = 0;
}

static HWND mk(const char *title, DWORD style, int x, int y, int cx, int cy, HWND parent)
{
    return CreateWindow("US39M", title, style, x, y, cx, cy, parent, 0, hi, NULL);
}

int __cdecl tmain(void)
{
    WNDCLASS wc;
    LPSTR cmd = (LPSTR)MAKELP(init_regs[6], init_regs[1]);
    DWORD t;
    hi = (HINSTANCE)init_regs[5];
    WaitEvent(0);
    InitApp(hi);
    if (cmd[0] == 'B')
        return task_b();
    out("US39 start\r\n");
    wc.style = 0;
    wc.lpfnWndProc = (WNDPROC)main_proc;
    wc.cbClsExtra = wc.cbWndExtra = 0;
    wc.hInstance = hi;
    wc.hIcon = 0;
    wc.hCursor = LoadCursor(NULL, IDC_ARROW);
    wc.hbrBackground = (HBRUSH)(COLOR_WINDOW + 1);
    wc.lpszMenuName = NULL;
    wc.lpszClassName = "US39M";
    RegisterClass(&wc);
    hA = mk("US39 A", WS_OVERLAPPEDWINDOW | WS_VISIBLE, 0, 0, 300, 200, 0);
    hC = mk("C", WS_CHILD | WS_VISIBLE | WS_BORDER, 10, 10, 80, 40, hA);
    hB = mk("US39 B", WS_OVERLAPPEDWINDOW | WS_VISIBLE, 320, 0, 300, 200, 0);
    hD = mk("D", WS_CHILD | WS_VISIBLE | WS_BORDER, 10, 10, 80, 40, hB);
    pump(200);
    reset();
    state("init");

    out("dis\r\n");
    EnableWindow(hA, FALSE);
    state("dis.offA");
    sf("dis.aA", hA);           /* A disabled and active */
    sf("dis.aC", hC);
    reset();
    SetFocus(hC);
    EnableWindow(hA, FALSE);
    state("dis.offC");
    reset();
    EnableWindow(hA, FALSE);
    SetActiveWindow(hB);
    state("dis.b");
    sf("dis.bA", hA);           /* A disabled, B active */
    sf("dis.bC", hC);
    reset();

    out("dchild\r\n");
    EnableWindow(hC, FALSE);
    sf("dchild.aC", hC);
    SetActiveWindow(hB);
    sf("dchild.bC", hC);
    reset();

    out("min\r\n");
    ShowWindow(hA, SW_SHOWMINNOACTIVE);
    SetActiveWindow(hB);
    state("min.b");
    sf("min.bA", hA);
    SetActiveWindow(hB);
    sf("min.bC", hC);
    reset();

    out("en\r\n");
    SetActiveWindow(hB);
    sf("en.bA", hA);
    SetActiveWindow(hB);
    sf("en.bC", hC);
    reset();

    out("dlg\r\n");
    sec_dlg();
    reset();

    out("mb\r\n");
    DestroyWindow(hB);
    hB = hD = 0;
    reset();
    state("mb.s0");
    o("mb", "exec", WinExec("US39.EXE B", SW_SHOWNORMAL) > 32);
    t = GetTickCount();
    while (!bdone && GetTickCount() - t < 15000)
        pump(50);
    o("mb", "done", bdone);
    pump(500);
    state("mb.s4");
    out("US39 end\r\n");
    DestroyWindow(hA);
    return 0;
}
