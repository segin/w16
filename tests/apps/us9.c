/*
 * us9.c - probe US9: USER's DragObject (USER.464) and DragDetect
 * (USER.465), the sender's side of drag and drop (Schulman, Undocumented
 * Windows ch. 6, names the calls and messages; this observes them). Run
 * as the shell on Windows 3.1 and on w16 (tests/refrun.sh), compare
 * TK.LOG.
 *
 * Four windows: a source S (top left), T1 that refuses (DefWindowProc
 * without WS_EX_ACCEPTFILES), T2 that accepts (returns 1 to
 * WM_QUERYDROPOBJECT itself and a value of its own to WM_DROPOBJECT),
 * T3 that accepts files (DragAcceptFiles, all to DefWindowProc). A mouse
 * drag is played back (WH_JOURNALPLAYBACK) from S over T1 and T2 to a
 * drop on T3, then on T2, then on T1, then on S itself; S's
 * WM_LBUTTONDOWN calls DragObject(desktop, S, 8002h, 0, "FOO.TXT BAR.TXT
 * ", cursor). Every drag message any window gets is logged with its
 * parameters, the DRAGINFO block, the capture, the cursor and whether it
 * was sent; DefWindowProc's results; DragObject's result; the WM_DROPFILES
 * block. DragDetect: a press with a small move and one with a big move.
 */
#include <windows.h>

#define TK_REF "us9ref.h"
#include "tklog.h"
#include "shprobe.h"

extern DWORD FAR PASCAL DragObject(HWND, HWND, WORD, WORD, WORD, HCURSOR);
extern BOOL FAR PASCAL DragDetect(HWND, POINT FAR *);

static HWND S, T1, T2, T3;
static HINSTANCE inst;
static HCURSOR dragcur;
static char list[] = "FOO.TXT BAR.TXT ";
static int mode;                /* 0: DragObject, 1: DragDetect */

#define NEV 48
static EVENTMSG ev[NEV];
static int nev, cur, playing, waited = -1;
static HOOKPROC hk, thunk;

LRESULT CALLBACK __export Playback(int code, WPARAM wp, LPARAM lp)
{
    if (code == HC_GETNEXT) {
        EVENTMSG FAR *e = (EVENTMSG FAR *)lp;
        *e = ev[cur];
        e->time = GetTickCount();
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

static void play(void)
{
    DWORD end = GetTickCount() + 10000;
    cur = 0;
    waited = -1;
    playing = 1;
    if (!thunk)
        thunk = (HOOKPROC)MakeProcInstance((FARPROC)Playback, inst);
    hk = SetWindowsHook(WH_JOURNALPLAYBACK, thunk);
    while (playing && (long)(GetTickCount() - end) < 0)
        pump(50);
    nev = 0;
    pump(500);
}

static const char *wname(HWND w)
{
    return w == S ? "S" : w == T1 ? "T1" : w == T2 ? "T2" : w == T3 ? "T3" :
           w == NULL ? "0" : w == GetDesktopWindow() ? "desk" : "?";
}

static void outw(HWND w)
{
    out(wname(w));
}

static void logmsg(HWND w, UINT m, WPARAM wp, LPARAM lp)
{
    out("  ");
    outw(w);
    out(" msg ");
    outhex(m);
    out(" wp=");
    outhex(wp);
    out(" lp=");
    outhex(HIWORD(lp));
    outhex(LOWORD(lp));
    out(InSendMessage() ? " sent" : " posted");
    out(" cap=");
    outw(GetCapture());
    out(" cur=");
    out(GetCursor() == dragcur ? "drag" : GetCursor() == LoadCursor(NULL, IDC_ARROW) ? "arrow" : "other");
    out("\r\n");
    if (m >= 0x022A && m <= 0x022F && lp && !IsBadReadPtr((void FAR *)lp, 20)) {
        WORD FAR *d = (WORD FAR *)lp;
        int i;
        out("    info");
        for (i = 0; i < 10; i++) {
            out(" ");
            outhex(d[i]);
        }
        out(" src=");
        outw((HWND)d[0]);
        out("\r\n");
    }
}

static void dump_drop(HGLOBAL h)
{
    BYTE FAR *p = (BYTE FAR *)GlobalLock(h);
    WORD size = (WORD)GlobalSize(h), n;
    obs("drop.size", size);
    obs("drop.flags", GlobalFlags(h));
    if (p) {
        for (n = size; n > 8 && !p[n - 1] && !p[n - 2]; n--)
            ;
        obs_b("drop.bytes", p, n + 1 < size ? n + 1 : size);
        GlobalUnlock(h);
    }
    DragFinish((HDROP)h);
}

LRESULT CALLBACK __export WinProc(HWND w, UINT m, WPARAM wp, LPARAM lp)
{
    LRESULT r;
    if (m >= 0x0227 && m <= 0x0236)
        logmsg(w, m, wp, lp);
    if (w == S && m == WM_LBUTTONDOWN) {
        if (mode == 0) {
            DWORD x = DragObject(GetDesktopWindow(), S, 0x8002, 0, (WORD)(char NEAR *)list, dragcur);
            out("  DragObject = ");
            outhex(HIWORD(x));
            outhex(LOWORD(x));
            out(" cap=");
            outw(GetCapture());
            out("\r\n");
        } else {
            POINT pt;
            BOOL b;
            pt.x = LOWORD(lp);
            pt.y = HIWORD(lp);
            b = DragDetect(S, &pt);
            out("  DragDetect = ");
            outhex(b);
            out(" cap=");
            outw(GetCapture());
            out("\r\n");
        }
        return 0;
    }
    if (w == S && (m == WM_MOUSEMOVE || m == WM_LBUTTONUP) && mode == 1) {
        out("  S ");
        out(m == WM_MOUSEMOVE ? "move " : "up ");
        outhex(LOWORD(lp));
        out(",");
        outhex(HIWORD(lp));
        out("\r\n");
    }
    if (w == T2 && m == 0x022B)
        return 1;
    if (w == T2 && m == 0x022A)
        return 0x12345678L;
    if (w == T3 && m == WM_DROPFILES) {
        dump_drop((HGLOBAL)wp);
        return 0;
    }
    r = DefWindowProc(w, m, wp, lp);
    if (m >= 0x0227 && m <= 0x0236) {
        out("    def = ");
        outhex(HIWORD(r));
        outhex(LOWORD(r));
        out("\r\n");
    }
    return r;
}

static void drag_to(int x, int y)
{
    int i;
    add(WM_MOUSEMOVE, 60, 60);
    add(WM_LBUTTONDOWN, 60, 60);
    add(WM_MOUSEMOVE, 62, 61);
    add(WM_MOUSEMOVE, 66, 63);
    add(WM_MOUSEMOVE, 75, 70);
    add(WM_MOUSEMOVE, 250, 60);      /* T1 */
    add(WM_MOUSEMOVE, 255, 62);
    add(WM_MOUSEMOVE, 250, 250);     /* T2 */
    add(WM_MOUSEMOVE, 255, 255);
    add(WM_MOUSEMOVE, 255, 220);     /* T2's caption */
    add(WM_MOUSEMOVE, 450, 250);     /* T3 */
    add(WM_MOUSEMOVE, 455, 255);
    for (i = 1; i <= 4; i++)
        add(WM_MOUSEMOVE, 455 + (x - 455) * i / 4, 255 + (y - 255) * i / 4);
    add(WM_LBUTTONUP, x, y);
    play();
}

int __cdecl tmain(void)
{
    WNDCLASS wc;
    inst = (HINSTANCE)init_regs[5];
    WaitEvent(0);
    InitApp(inst);
    out("US9 DragObject\r\n");
    wc.style = 0;
    wc.cbClsExtra = wc.cbWndExtra = 0;
    wc.hIcon = NULL;
    wc.hCursor = LoadCursor(NULL, IDC_ARROW);
    wc.hbrBackground = (HBRUSH)(COLOR_WINDOW + 1);
    wc.lpszMenuName = NULL;
    wc.lpfnWndProc = WinProc;
    wc.hInstance = inst;
    wc.lpszClassName = "US9";
    RegisterClass(&wc);
    dragcur = LoadCursor(NULL, IDC_CROSS);
    S = CreateWindow("US9", "S", WS_OVERLAPPEDWINDOW | WS_VISIBLE, 0, 0, 150, 150, NULL, NULL, inst, NULL);
    T1 = CreateWindow("US9", "T1", WS_OVERLAPPEDWINDOW | WS_VISIBLE, 200, 0, 150, 150, NULL, NULL, inst, NULL);
    T2 = CreateWindow("US9", "T2", WS_OVERLAPPEDWINDOW | WS_VISIBLE, 200, 200, 150, 150, NULL, NULL, inst, NULL);
    T3 = CreateWindow("US9", "T3", WS_OVERLAPPEDWINDOW | WS_VISIBLE, 400, 200, 150, 150, NULL, NULL, inst, NULL);
    DragAcceptFiles(T3, TRUE);
    pump(1000);
    out("drop on T3\r\n");
    drag_to(460, 280);
    out("drop on T2\r\n");
    drag_to(260, 280);
    out("drop on T1\r\n");
    drag_to(260, 80);
    out("drop on S\r\n");
    drag_to(80, 80);
    mode = 1;
    out("DragDetect small\r\n");
    add(WM_MOUSEMOVE, 60, 60);
    add(WM_LBUTTONDOWN, 60, 60);
    add(WM_MOUSEMOVE, 61, 61);
    add(WM_MOUSEMOVE, 62, 60);
    add(WM_LBUTTONUP, 62, 60);
    play();
    out("DragDetect big\r\n");
    add(WM_MOUSEMOVE, 60, 60);
    add(WM_LBUTTONDOWN, 60, 60);
    add(WM_MOUSEMOVE, 61, 61);
    add(WM_MOUSEMOVE, 63, 60);
    add(WM_MOUSEMOVE, 70, 66);
    add(WM_LBUTTONUP, 70, 66);
    play();
    out("US9 done\r\n");
    return failures;
}
