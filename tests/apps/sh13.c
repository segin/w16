/*
 * sh13.c - SH13: what a drop from File Manager delivers. SH13 opens a
 * window that accepts files (DragAcceptFiles), starts WINFILE.EXE, and
 * plays back a mouse drag (WH_JOURNALPLAYBACK, SDK 3.1) of an item of
 * File Manager's list box with the focus onto its window. Every drag
 * message the window gets is logged; the WM_DROPFILES block is dumped
 * (size, flags, bytes) and read with DragQueryFile/DragQueryPoint.
 * Black-box: what File Manager and USER hand the receiving window.
 */
#include <windows.h>

#define TK_REF "sh13ref.h"
#include "tklog.h"
#include "shprobe.h"

static HWND mainw;
static HINSTANCE inst;
static int dropped;

/* ---- mouse playback ---- */
#define NEV 64
static EVENTMSG ev[NEV];
static int nev, cur, playing;
static HOOKPROC hk, thunk;

static int waited = -1;

LRESULT CALLBACK __export Playback(int code, WPARAM wp, LPARAM lp)
{
    if (code == HC_GETNEXT) {
        EVENTMSG FAR *e = (EVENTMSG FAR *)lp;
        *e = ev[cur];
        e->time = GetTickCount();
        /* USER asks again for the same event while it waits: the delay
           counts only the first time */
        if (cur && cur != waited && ev[cur].message == WM_MOUSEMOVE) {
            waited = cur;
            return 50;
        }
        return 0;
    }
    if (code == HC_SKIP) {
        if (++cur >= nev) {
            UnhookWindowsHook(WH_JOURNALPLAYBACK, thunk);
            playing = 0;
        }
        return 0;
    }
    return DefHookProc(code, wp, lp, &hk);
}

static void add(UINT m, int x, int y)
{
    if (nev < NEV) {
        ev[nev].message = m;
        ev[nev].paramL = x;
        ev[nev].paramH = y;
        ev[nev].time = 0;
        nev++;
    }
}

static void drag(int x1, int y1, int x2, int y2)
{
    int i;
    DWORD end = GetTickCount() + 10000;
    add(WM_MOUSEMOVE, x1, y1);
    add(WM_LBUTTONDOWN, x1, y1);
    /* a few small steps first, as a hand moves: programs start a drag
       once the mouse has left a small square around the press */
    add(WM_MOUSEMOVE, x1 + 1, y1);
    add(WM_MOUSEMOVE, x1 + 3, y1 + 1);
    add(WM_MOUSEMOVE, x1 + 6, y1 + 3);
    for (i = 1; i <= 16; i++)
        add(WM_MOUSEMOVE, x1 + 6 + (x2 - x1 - 6) * i / 16, y1 + 3 + (y2 - y1 - 3) * i / 16);
    add(WM_MOUSEMOVE, x2, y2);
    add(WM_LBUTTONUP, x2, y2);
    cur = 0;
    waited = -1;
    playing = 1;
    if (!thunk)
        thunk = (HOOKPROC)MakeProcInstance((FARPROC)Playback, inst);
    hk = SetWindowsHook(WH_JOURNALPLAYBACK, thunk);
    while (playing && (long)(GetTickCount() - end) < 0)
        pump(50);
    nev = 0;
}

/* "dropN.what": one set of names per drop */
static char ndrop = '0';
static char *dn(const char *what)
{
    static char n[40];
    lstrcpy(n, "drop0.");
    n[4] = ndrop;
    lstrcat(n, what);
    return n;
}

static void dump_drop(HGLOBAL h)
{
    BYTE FAR *p = (BYTE FAR *)GlobalLock(h);
    WORD size = (WORD)GlobalSize(h), i, n;
    char name[160];
    POINT pt;
    obs_env(dn("handle"), (WORD)h);
    obs(dn("size"), size);
    obs(dn("flags"), GlobalFlags(h));
    if (p) {
        for (n = size; n > 8 && !p[n - 1] && !p[n - 2]; n--)
            ;
        obs_b(dn("bytes"), p, n + 1 < size ? n + 1 : size);
        GlobalUnlock(h);
    }
    n = DragQueryFile((HDROP)h, (UINT)-1, NULL, 0);
    obs(dn("count"), n);
    for (i = 0; i < n && i < 8; i++) {
        DragQueryFile((HDROP)h, i, name, sizeof name);
        obs_s(dn("name"), name);
    }
    obs(dn("client"), DragQueryPoint((HDROP)h, &pt));
    obs(dn("x"), pt.x);
    obs(dn("y"), pt.y);
    DragFinish((HDROP)h);
    ndrop++;
}

/* File Manager's directory pane: its list box furthest to the right */
static HWND best;
static int best_x;

BOOL CALLBACK __export FindLb(HWND w, LPARAM lp)
{
    char cls[16];
    RECT r;
    (void)lp;
    GetClassName(w, cls, sizeof cls);
    GetWindowRect(w, &r);
    if (!lstrcmpi(cls, "ListBox") && IsWindowVisible(w) && (!best || r.left > best_x)) {
        best = w;
        best_x = r.left;
    }
    return TRUE;
}

static HWND rightmost_listbox(HWND frame)
{
    best = NULL;
    if (frame)
        EnumChildWindows(frame, (WNDENUMPROC)MakeProcInstance((FARPROC)FindLb, inst), 0);
    return best;
}

LRESULT CALLBACK __export MainProc(HWND w, UINT m, WPARAM wp, LPARAM lp)
{
    if (m >= 0x0227 && m <= 0x0236) {
        out("MSG ");
        outhex(m);
        out(" wp=");
        outhex(wp);
        out(" lp=");
        outhex(HIWORD(lp));
        outhex(LOWORD(lp));
        out(InSendMessage() ? " sent\r\n" : " posted\r\n");
    }
    if (m == WM_DROPFILES) {
        dropped++;
        dump_drop((HGLOBAL)wp);
        return 0;
    }
    return DefWindowProc(w, m, wp, lp);
}

int __cdecl tmain(void)
{
    WNDCLASS wc;
    HWND lb;
    RECT r;
    POINT pt;
    int item;

    inst = (HINSTANCE)init_regs[5];
    WaitEvent(0);
    InitApp(inst);
    out("SH13 drop from File Manager\r\n");
    wc.style = 0;
    wc.cbClsExtra = wc.cbWndExtra = 0;
    wc.hIcon = NULL;
    wc.hCursor = LoadCursor(NULL, IDC_ARROW);
    wc.hbrBackground = (HBRUSH)(COLOR_WINDOW + 1);
    wc.lpszMenuName = NULL;
    wc.lpfnWndProc = MainProc;
    wc.hInstance = inst;
    wc.lpszClassName = "SH13";
    RegisterClass(&wc);
    WinExec("WINFILE.EXE", SW_SHOWNORMAL);
    pump(5000);
    /* File Manager on the left, the target on the right */
    {
        HWND fm = FindWindow("WFS_Frame", NULL);
        ShowWindow(fm, SW_SHOWNORMAL);
        MoveWindow(fm, 0, 0, 400, 480, TRUE);
    }
    mainw = CreateWindow("SH13", "SH13 drop target", WS_OVERLAPPEDWINDOW | WS_VISIBLE,
                         410, 100, 220, 200, NULL, NULL, inst, NULL);
    DragAcceptFiles(mainw, TRUE);
    pump(1000);
    /* the list box with the focus is File Manager's (its last active pane) */
    SetActiveWindow(FindWindow("WFS_Frame", NULL));
    pump(1000);
    lb = rightmost_listbox(FindWindow("WFS_Frame", NULL));
    obs("listbox", lb != NULL);
    for (item = 5; item < 8 && lb; item++) {
        if (SendMessage(lb, LB_GETITEMRECT, item, (LPARAM)(RECT FAR *)&r) == LB_ERR)
            break;
        pt.x = r.left + 20;
        pt.y = (r.top + r.bottom) / 2;
        ClientToScreen(lb, &pt);
        out("drag item ");
        outint(item);
        out("\r\n");
        dropped = 0;
        drag(pt.x, pt.y, 500 + item, 200 + item);
        pump(1500);
        obsn("dropped", item, dropped);
        snap(item == 5 ? "C:\\SH13A.BMP" : item == 6 ? "C:\\SH13B.BMP" : "C:\\SH13C.BMP");
        SetActiveWindow(FindWindow("WFS_Frame", NULL));
        pump(500);
    }
    out("SH13 done\r\n");
    return failures;
}
