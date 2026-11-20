/*
 * us32s.c - US32S: a window spy for probe US32's question (where a list
 * box draws an item only partly inside it), run beside another program
 * (WIN.INI load=) to see its dialog from outside: every second it looks
 * for a top-level window titled US32S_TITLE (default "QuickTime Control
 * Panel") and, whenever what it sees has changed, logs every window under
 * it: class, style, extended style, ID, rectangle (relative to the
 * top-level window's client area) and client size, and for a list box
 * its item height (of item 0), count, top item, selection and scroll
 * range and position. Logs to TK.LOG (tests/apps/start.asm). Nothing is
 * sent to the other program's windows but GetWindowText, GetClassName
 * and the list box queries.
 */
#include <windows.h>

#define TK_REF "us32ref.h"
#include "tklog.h"

extern WORD FAR PASCAL WaitEvent(HTASK);
extern BOOL FAR PASCAL InitApp(HINSTANCE);

static HINSTANCE hi;
static HWND top;
static WORD lastsum;
static int snaps;
static char line[200];

static WORD sum;

static void addsum(const char *s)
{
    while (*s)
        sum = (WORD)(sum * 31 + (BYTE)*s++);
}

static void walk(HWND p, int depth, BOOL log)
{
    HWND c;
    for (c = GetWindow(p, GW_CHILD); c; c = GetWindow(c, GW_HWNDNEXT)) {
        char cls[32], txt[40];
        RECT w, cl;
        POINT o;
        GetClassName(c, cls, sizeof cls);
        GetWindowText(c, txt, sizeof txt);
        GetWindowRect(c, &w);
        GetClientRect(c, &cl);
        o.x = o.y = 0;
        ClientToScreen(top, &o);
        wsprintf(line, "%d %s st %lx ex %lx id %d at %d,%d %dx%d cl %dx%d v%d \"%s\"",
                 depth, (LPSTR)cls, GetWindowLong(c, GWL_STYLE), GetWindowLong(c, GWL_EXSTYLE),
                 GetWindowWord(c, GWW_ID), w.left - o.x, w.top - o.y, w.right - w.left,
                 w.bottom - w.top, cl.right, cl.bottom, IsWindowVisible(c) != 0, (LPSTR)txt);
        addsum(line);
        if (log) {
            out("SPY ");
            out(line);
            out("\r\n");
        }
        if (!lstrcmpi(cls, "ListBox")) {
            int lo = 0, hi2 = 0;
            GetScrollRange(c, SB_VERT, &lo, &hi2);
            wsprintf(line, "%d   lb ih %d n %d top %d sel %d bar %d-%d at %d", depth,
                     (int)SendMessage(c, LB_GETITEMHEIGHT, 0, 0), (int)SendMessage(c, LB_GETCOUNT, 0, 0),
                     (int)SendMessage(c, LB_GETTOPINDEX, 0, 0), (int)SendMessage(c, LB_GETCURSEL, 0, 0),
                     lo, hi2, GetScrollPos(c, SB_VERT));
            addsum(line);
            if (log) {
                out("SPY ");
                out(line);
                out("\r\n");
            }
        }
        walk(c, depth + 1, log);
    }
}

void FAR PASCAL __export __loadds tick(HWND h, UINT m, UINT id, DWORD t)
{
    RECT r;
    top = FindWindow(NULL, "QuickTime Control Panel");
    if (!top)
        return;
    sum = 0;
    walk(top, 0, FALSE);
    if (sum == lastsum)
        return;
    lastsum = sum;
    snaps++;
    GetWindowRect(top, &r);
    {
        POINT o;
        o.x = o.y = 0;
        ClientToScreen(top, &o);
        wsprintf(line, "SPY snapshot %d: top at %d,%d %dx%d client at %d,%d\r\n", snaps, r.left,
                 r.top, r.right - r.left, r.bottom - r.top, o.x, o.y);
    }
    out(line);
    walk(top, 0, TRUE);
    (void)h; (void)m; (void)id; (void)t;
}

int __cdecl tmain(void)
{
    MSG m;
    hi = (HINSTANCE)init_regs[5];
    WaitEvent(0);
    InitApp(hi);
    out("US32S start\r\n");
    SetTimer(0, 1, 1000, (TIMERPROC)tick);
    while (GetMessage(&m, 0, 0, 0)) {
        TranslateMessage(&m);
        DispatchMessage(&m);
    }
    return 0;
}
