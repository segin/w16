/*
 * us6.c - probe US6: EM_GETTHUMB (WM_USER+14), the edit-control message
 * the 3.1 DDK's WINDOWS.H marks internal (Schulman, Undocumented Windows,
 * ch. 7, gives only its number). Notepad sends it when its edit control
 * notifies EN_HSCROLL/EN_VSCROLL. What it returns is observed here: a
 * multi-line edit control with both scroll bars, empty and then with 50
 * lines, scrolled to various lines and columns; for each, EM_GETTHUMB
 * with wParam 0 and 1 (and lParam 0), the scroll bars' positions and
 * ranges, and the first visible line. Run as the shell on Windows 3.1
 * (tests/refrun.sh build/dos/US6.EXE) and on w16's USER (tests/userboot.sh
 * -s build/dos/US6.EXE); us6ref.h holds 3.1's values.
 *
 * The thumb is also dragged with the mouse: a SYSTEM.DRV timer procedure
 * (CreateSystemTimer, so at interrupt time like the mouse driver) feeds
 * USER's mouse_event (USER.299, tests/apps/mevent.asm) a press on the
 * vertical thumb, moves and a release, while the probe pumps messages;
 * the parent logs EM_GETTHUMB at each EN_VSCROLL/EN_HSCROLL.
 */
#include <windows.h>

#define TK_REF "us6ref.h"
#include "tklog.h"

#define EM_GETTHUMB (WM_USER + 14)

extern WORD FAR PASCAL WaitEvent(HTASK);
extern WORD FAR PASCAL S_CREATESYSTEMTIMER(WORD ms, FARPROC proc);
extern WORD FAR PASCAL S_KILLSYSTEMTIMER(WORD id);
extern void __cdecl call_mouse_event(WORD status, WORD x, WORD y, WORD buttons);

/* the mouse script the timer plays: status, screen x, y */
typedef struct { WORD st; int x, y; } MSTEP;
static MSTEP mscript[16];
static volatile int mstep, msteps;

void FAR __loadds mtimer(void)
{
    if (mstep < msteps) {
        MSTEP *s = &mscript[mstep++];
        /* normalised 0-65535 (x * 65536 / 640, y * 65536 / 480, rounded up
           so that USER's mapping back lands on the pixel) */
        call_mouse_event(s->st | 0x8001, (WORD)(s->x * 102 + s->x * 2 / 5 + 1),
                         (WORD)(s->y * 136 + s->y * 8 / 15 + 1), 2);
    }
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

/* a drag from (x, y) by (dx, dy) in four steps */
static void drag(int x, int y, int dx, int dy)
{
    int i;
    msteps = 0;
    mscript[msteps].st = 0; mscript[msteps].x = x; mscript[msteps++].y = y;
    mscript[msteps].st = 2; mscript[msteps].x = x; mscript[msteps++].y = y;
    for (i = 1; i <= 4; i++) {
        mscript[msteps].st = 0;
        mscript[msteps].x = x + dx * i / 4;
        mscript[msteps++].y = y + dy * i / 4;
    }
    mscript[msteps].st = 4; mscript[msteps].x = x + dx; mscript[msteps++].y = y + dy;
    mstep = 0;
}
extern BOOL FAR PASCAL InitApp(HINSTANCE);

static char text[4000];
static char ntag[24];          /* the step under way, for notifications */
static int nnote;

/* the parent: EM_GETTHUMB as the edit control notifies scrolling (what
   Notepad does) */
LRESULT FAR PASCAL __export __loadds wnd_proc(HWND h, UINT m, WPARAM wp, LPARAM lp)
{
    if (m == WM_COMMAND && (HIWORD(lp) == EN_VSCROLL || HIWORD(lp) == EN_HSCROLL) && nnote < 12) {
        char nm[48];
        HWND e = (HWND)LOWORD(lp);
        wsprintf(nm, "%s.n%d.%s", (LPSTR)ntag, nnote, HIWORD(lp) == EN_VSCROLL ? (LPSTR)"v" : (LPSTR)"h");
        obs(nm, (WORD)SendMessage(e, EM_GETTHUMB, 0, 0L));
        wsprintf(nm, "%s.n%d.pos", (LPSTR)ntag, nnote);
        obs(nm, GetScrollPos(e, HIWORD(lp) == EN_VSCROLL ? SB_VERT : SB_HORZ));
        nnote++;
    }
    return DefWindowProc(h, m, wp, lp);
}

static void step(const char *t)
{
    lstrcpy(ntag, t);
    nnote = 0;
}

static void state(HWND e, const char *tag)
{
    char nm[64];
    int lo, hi;
    wsprintf(nm, "%s.thumb0", (LPSTR)tag);
    obs(nm, (WORD)SendMessage(e, EM_GETTHUMB, 0, 0L));
    wsprintf(nm, "%s.thumb1", (LPSTR)tag);
    obs(nm, (WORD)SendMessage(e, EM_GETTHUMB, 1, 0L));
    wsprintf(nm, "%s.thumb0.hi", (LPSTR)tag);
    obs(nm, HIWORD(SendMessage(e, EM_GETTHUMB, 0, 0L)));
    wsprintf(nm, "%s.vpos", (LPSTR)tag);
    obs(nm, GetScrollPos(e, SB_VERT));
    wsprintf(nm, "%s.hpos", (LPSTR)tag);
    obs(nm, GetScrollPos(e, SB_HORZ));
    GetScrollRange(e, SB_VERT, &lo, &hi);
    wsprintf(nm, "%s.vrange", (LPSTR)tag);
    obs(nm, hi - lo);
    GetScrollRange(e, SB_HORZ, &lo, &hi);
    wsprintf(nm, "%s.hrange", (LPSTR)tag);
    obs(nm, hi - lo);
    wsprintf(nm, "%s.first", (LPSTR)tag);
    obs(nm, (WORD)SendMessage(e, EM_GETFIRSTVISIBLELINE, 0, 0L));
}

int __cdecl tmain(void)
{
    static const int lines[] = { 1, 4, 15, 20, 30, -10, 100, -1000, 0 };
    WNDCLASS wc;
    HWND w, e;
    HINSTANCE tk_hinst = (HINSTANCE)init_regs[5];      /* DI at start-up */
    int i, n = 0;
    char tag[16];
    WaitEvent(0);
    InitApp(tk_hinst);
    out("US6 start\r\n");
    wc.style = 0;
    wc.lpfnWndProc = wnd_proc;
    wc.cbClsExtra = wc.cbWndExtra = 0;
    wc.hInstance = tk_hinst;
    wc.hIcon = 0;
    wc.hCursor = LoadCursor(NULL, IDC_ARROW);
    wc.hbrBackground = (HBRUSH)(COLOR_WINDOW + 1);
    wc.lpszMenuName = NULL;
    wc.lpszClassName = "US6";
    RegisterClass(&wc);
    w = CreateWindow("US6", "US6", WS_OVERLAPPEDWINDOW | WS_VISIBLE, 20, 20, 300, 200,
                     NULL, NULL, tk_hinst, NULL);
    e = CreateWindow("Edit", "", WS_CHILD | WS_VISIBLE | WS_BORDER | WS_VSCROLL | WS_HSCROLL |
                     ES_MULTILINE | ES_AUTOVSCROLL | ES_AUTOHSCROLL, 0, 0, 200, 100, w,
                     (HMENU)1, tk_hinst, NULL);
    state(e, "empty");
    for (i = 0; i < 50; i++) {
        int k;
        n += wsprintf(text + n, "line %d", i);
        if (i % 5 == 0)
            for (k = 0; k < 50; k++)
                text[n++] = (char)('a' + k % 26);
        text[n++] = '\r';
        text[n++] = '\n';
    }
    text[n] = 0;
    SetWindowText(e, text);
    state(e, "full");
    for (i = 0; lines[i]; i++) {
        SendMessage(e, EM_LINESCROLL, 0, MAKELONG(lines[i], 0));
        wsprintf(tag, "v%d", i);
        state(e, tag);
    }
    SendMessage(e, EM_LINESCROLL, 0, MAKELONG(0, 10));
    state(e, "h10");
    SendMessage(e, EM_LINESCROLL, 0, MAKELONG(0, 25));
    state(e, "h35");
    step("pgdn");
    SendMessage(e, WM_VSCROLL, SB_PAGEDOWN, 0L);
    state(e, "pgdn");
    step("track40");
    SendMessage(e, WM_VSCROLL, SB_THUMBTRACK, MAKELONG(40, 0));
    state(e, "track40");
    step("track70");
    SendMessage(e, WM_VSCROLL, SB_THUMBTRACK, MAKELONG(70, 0));
    state(e, "track70");
    step("thumbpos40");
    SendMessage(e, WM_VSCROLL, SB_THUMBPOSITION, MAKELONG(40, 0));
    state(e, "thumbpos40");
    step("hpos");
    SendMessage(e, WM_HSCROLL, SB_THUMBTRACK, MAKELONG(30, 0));
    state(e, "htrack30");
    step("hend");
    SendMessage(e, WM_HSCROLL, SB_THUMBPOSITION, MAKELONG(30, 0));
    state(e, "hthumbpos30");
    step("vline");
    SendMessage(e, WM_VSCROLL, SB_LINEDOWN, 0L);
    state(e, "linedown");
    /* the thumbs dragged with the mouse */
    {
        RECT r;
        WORD id;
        SendMessage(e, EM_LINESCROLL, 0, MAKELONG(-1000, -1000));
        GetWindowRect(e, &r);
        id = S_CREATESYSTEMTIMER(55, (FARPROC)mtimer);
        step("vdrag");
        drag(r.right - 9, r.top + 1 + 17 + 8, 0, 40);
        pump(2500);
        state(e, "vdrag");
        step("hdrag");
        drag(r.left + 1 + 17 + 8, r.bottom - 9, 60, 0);
        pump(2500);
        state(e, "hdrag");
        S_KILLSYSTEMTIMER(id);
    }
    DestroyWindow(w);
    out("US6 end\r\n");
    return 0;
}
