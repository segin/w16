/*
 * drag.c - the sender's side of drag and drop: DragObject (USER.464) and
 * DragDetect (USER.465), and DefWindowProc's answers for the receiver.
 *
 * Schulman (Undocumented Windows ch. 6, 7) names the calls, the messages
 * and the DRAGINFO block; what they do was observed on Windows 3.1 with
 * probe US9 (tests/apps/us9.c: a drag played back over windows that
 * refuse, accept, and accept files):
 *
 * - DragObject(hwndScope, hwndFrom, wFlags, hOfstruct, list, hCursor)
 *   takes the capture for hwndFrom and shows hCursor. At every mouse move
 *   the window under the pointer gets WM_QUERYDROPOBJECT (22Bh), wParam 1
 *   over its non-client area, lParam a DRAGINFO block in USER's memory
 *   (18 bytes: source, target, wFlags, list, hOfstruct, x, y in the
 *   target's client coordinates, a DWORD 0); then the source gets
 *   WM_DRAGLOOP (22Dh), wParam whether the window accepted (non-zero
 *   answer), the block with target 0 and the screen point when it did
 *   not. A window newly accepting gets WM_DRAGSELECT (22Eh) wParam 1, the
 *   one it replaces WM_DRAGSELECT 0 with a copy of its last block (the
 *   second 18 bytes); the same window again gets WM_DRAGMOVE (22Fh) 0.
 *   The cursor is hCursor over an accepting window, else the no-drop one.
 * - On the button's release the capture goes, the arrow comes back, and
 *   an accepting window gets WM_DROPOBJECT (22Ah), wParam the source:
 *   its answer is DragObject's result; with no accepting window there is
 *   no message and the result is 0.
 * - DefWindowProc answers WM_QUERYDROPOBJECT 1 and WM_DROPOBJECT "FILE"
 *   (454C4946h) for a window with WS_EX_ACCEPTFILES, 0 otherwise; the
 *   others 0. WM_DROPFILES is not USER's: the source posts it (File
 *   Manager, on "FILE"; ARCHITECTURE.md 13.3 for the block).
 *
 * UNVERIFIED: the walk from the window under the pointer up through its
 * parents when a child refuses (here: the deepest first, as a program's
 * edit control lies over the window that accepts files), wFlags without
 * 8000h keeping the drag in the source's task (Schulman), Esc. The
 * no-drop cursor is w16's own drawing.
 */
#include "user.h"

extern int menu_mode;

typedef struct DRAGINFO {
    WORD hwndSource, hwndSink, wFlags, list, hOfstruct;
    int x, y;
    DWORD unknown;
} DRAGINFO;

#define WM_DROPOBJECT      0x022A
#define WM_QUERYDROPOBJECT 0x022B
#define WM_DRAGLOOP        0x022D
#define WM_DRAGSELECT      0x022E
#define WM_DRAGMOVE        0x022F
#define DRAG_FILE          0x454C4946L

static HGLOBAL hInfo;           /* two DRAGINFOs, kept for every drag */
static HCURSOR hcurNoDrop;

/* the no-drop cursor: a ring with a bar across, black with a white edge
   (w16's own drawing) */
static HCURSOR no_drop(void)
{
    static BYTE andb[128], xorb[128];
    int x, y;
    if (hcurNoDrop)
        return hcurNoDrop;
    for (y = 0; y < 32; y++)
        for (x = 0; x < 32; x++) {
            int dx = x - 15, dy = y - 15, d = dx * dx + dy * dy;
            int bar = dx - dy;  /* the bar from top left to bottom right */
            BOOL black = (d >= 8 * 8 && d <= 11 * 11) || (d < 8 * 8 && bar >= -2 && bar <= 2);
            BOOL white = !black && ((d >= 7 * 7 && d <= 12 * 12) ||
                                    (d < 8 * 8 && bar >= -3 && bar <= 3));
            BYTE m = (BYTE)(0x80 >> (x & 7));
            if (black || white)
                andb[y * 4 + x / 8] &= (BYTE)~m;
            else
                andb[y * 4 + x / 8] |= m;
            if (white)
                xorb[y * 4 + x / 8] |= m;
        }
    hcurNoDrop = CreateCursor(hInstUser, 15, 15, 32, 32, andb, xorb);
    return hcurNoDrop;
}

/* the first window at pt, from the deepest up through its parents
   within scope, that accepts: its handle, and info filled for it */
static HWND query(HWND scope, HWND from, WORD flags, DRAGINFO __far *info, POINT pt)
{
    HWND w = WindowFromPoint(pt);
    HTASK task = GetWindowTask(from);
    while (w && w != hwndDesktop) {
        PWND pw = valid_wnd(w);
        if (!pw)
            break;
        if ((scope == hwndDesktop || scope == w || IsChild(scope, w)) &&
            ((flags & 0x8000) || GetWindowTask(w) == task)) {
            POINT c = pt;
            BOOL nc = !PtInRect(&pw->rcClient, pt);
            ScreenToClient(w, &c);
            info->hwndSink = (WORD)w;
            info->x = c.x;
            info->y = c.y;
            if (send_message(w, WM_QUERYDROPOBJECT, nc, (LPARAM)info) && valid_wnd(w))
                return w;
        }
        if (!(pw->style & WS_CHILD))
            break;
        w = pw->hwndParent;
    }
    return 0;
}

/* one step: the window at pt asked, the source told, the windows told
   of the change (see the head of the file); returns the accepting window */
static HWND step(HWND scope, HWND from, WORD flags, HCURSOR hcur, DRAGINFO __far *info,
                 HWND sink, POINT pt)
{
    DRAGINFO __far *old = info + 1;
    HWND w = query(scope, from, flags, info, pt);
    if (!w) {
        info->hwndSink = 0;
        info->x = pt.x;
        info->y = pt.y;
    }
    SetCursor(w ? hcur : no_drop());    /* before WM_DRAGLOOP (US9) */
    send_message(from, WM_DRAGLOOP, w != 0, (LPARAM)info);
    if (w != sink) {
        if (sink && valid_wnd(sink))
            send_message(sink, WM_DRAGSELECT, 0, (LPARAM)old);
        if (w)
            send_message(w, WM_DRAGSELECT, 1, (LPARAM)info);
    } else if (w)
        send_message(w, WM_DRAGMOVE, 0, (LPARAM)info);
    if (w) {
        *old = *info;
        old->unknown = 0;
    }
    return w;
}

DWORD WINAPI U_DRAGOBJECT(HWND scope, HWND from, WORD flags, WORD hOfstruct, WORD list, HCURSOR hcur)
{
    DRAGINFO __far *info, __far *old;
    HWND sink = 0;
    DWORD r = 0;
    MSG msg;
    POINT last;
    if (!valid_wnd(from))
        return 0;
    if (!scope || !valid_wnd(scope))
        scope = hwndDesktop;
    if (!hInfo) {
        hInfo = GlobalAlloc(GMEM_FIXED | GMEM_ZEROINIT | GMEM_SHARE, 2 * sizeof(DRAGINFO));
        if (!hInfo)
            return 0;
    }
    info = (DRAGINFO __far *)GlobalLock(hInfo);
    old = info + 1;
    fmemset(info, 0, 2 * sizeof(DRAGINFO));
    info->hwndSource = (WORD)from;
    info->wFlags = flags;
    info->list = list;
    info->hOfstruct = hOfstruct;
    SetCapture(from);
    menu_mode++;                /* the cursor is the drag's (as in a menu) */
    SetCursor(hcur);
    GetCursorPos(&last);
    for (;;) {
        if (!GetMessage(&msg, 0, 0, 0)) {
            PostQuitMessage(msg.wParam);
            break;
        }
        /* a move to where the pointer already was is no step (3.1: no
           query at the press point); the release is one, at its point */
        if (msg.message == WM_MOUSEMOVE) {
            if (msg.pt.x != last.x || msg.pt.y != last.y)
                sink = step(scope, from, flags, hcur, info, sink, msg.pt);
            last = msg.pt;
            continue;
        }
        if (msg.message == WM_LBUTTONUP) {
            sink = step(scope, from, flags, hcur, info, sink, msg.pt);
            break;
        }
        if (msg.message == WM_KEYDOWN && msg.wParam == VK_ESCAPE) {
            sink = 0;
            break;
        }
        if (msg.message >= WM_MOUSEFIRST && msg.message <= WM_MOUSELAST)
            continue;
        TranslateMessage(&msg);
        DispatchMessage(&msg);
    }
    menu_mode--;
    ReleaseCapture();
    SetCursor(load_icon_cursor(0, IDC_ARROW, TRUE));
    if (sink && valid_wnd(sink)) {
        *info = *old;
        r = send_message(sink, WM_DROPOBJECT, (WPARAM)from, (LPARAM)info);
    }
    GlobalUnlock(hInfo);
    return r;
}

/* USER.465, DragDetect(hwnd, lpPoint): TRUE once the pointer leaves a
   square around *lpPoint (screen coordinates) with the left button down,
   FALSE when the button comes up first (Schulman ch. 6: the square is the
   point inflated by twice the border's width; US9 on 3.1: a point
   outside it from the start gave TRUE at the first move). The capture is
   the window's meanwhile. */
BOOL WINAPI U_DRAGDETECT(HWND h, POINT __far *lp)
{
    RECT r;
    MSG msg;
    if (!valid_wnd(h) || !lp)
        return FALSE;
    SetRect(&r, lp->x, lp->y, lp->x, lp->y);
    InflateRect(&r, 2 * sysmet[SM_CXBORDER], 2 * sysmet[SM_CYBORDER]);
    SetCapture(h);
    for (;;) {
        while (PeekMessage(&msg, 0, WM_MOUSEFIRST, WM_MOUSELAST, PM_REMOVE)) {
            if (msg.message == WM_LBUTTONUP) {
                ReleaseCapture();
                return FALSE;
            }
            if (msg.message == WM_MOUSEMOVE && !PtInRect(&r, msg.pt)) {
                ReleaseCapture();
                return TRUE;
            }
        }
        WaitMessage();
    }
}

/* DefWindowProc's drag messages */
LRESULT def_drag(PWND w, UINT msg)
{
    if (!(w->exstyle & WS_EX_ACCEPTFILES))
        return 0;
    if (msg == WM_QUERYDROPOBJECT)
        return 1;
    if (msg == WM_DROPOBJECT)
        return DRAG_FILE;
    return 0;
}
