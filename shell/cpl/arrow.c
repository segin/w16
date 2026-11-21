/*
 * arrow.c - the spin control class of Windows 3.1's applets ("cpArrow"),
 * shared by MAIN.CPL and CPWIN386.CPL: a narrow box with an up and a down
 * arrow, which sends its parent WM_VSCROLL SB_LINEUP or SB_LINEDOWN with
 * its window handle.
 */
#include <windows.h>
#include "arrow.h"

char cls_arrow[] = "cpArrow";

/* ---- cpArrow ----
   Observed (pixels, dumps): the controls have WS_BORDER (USER draws the
   black box) and are one pixel wider than their template says (13 pixels
   for 7 dialog units, in the Desktop and Custom Color dialogs: UNVERIFIED
   whether the rule is "odd width"); inside, COLOR_BTNFACE split by a black
   line 9/20 of the window's height down; an up arrow whose base is 2 rows
   above the line and a down arrow from 3 rows below it, each 6 rows of 1,
   3, 3, 5, 5, 7 pixels, centred. */
static void arrow_paint(HWND w, HDC dc)
{
    RECT r, t;
    HBRUSH face, black;
    int mid, cx, k;
    GetClientRect(w, &r);
    face = CreateSolidBrush(GetSysColor(COLOR_BTNFACE));
    black = GetStockObject(BLACK_BRUSH);
    FillRect(dc, &r, face);
    mid = r.top + 9 * (r.bottom - r.top) / 20;
    SetRect(&t, r.left, mid, r.right, mid + 1);
    FillRect(dc, &t, black);
    cx = (r.right - r.left) / 2;
    for (k = 0; k < 6; k++) {
        int half = (k + 1) / 2;
        SetRect(&t, cx - half, mid - 7 + k, cx + half + 1, mid - 6 + k);
        FillRect(dc, &t, black);
        SetRect(&t, cx - half, mid + 8 - k, cx + half + 1, mid + 9 - k);
        FillRect(dc, &t, black);
    }
    DeleteObject(face);
}

LRESULT FAR PASCAL __loadds ArrowProc(HWND w, UINT msg, WPARAM wp, LPARAM lp)
{
    switch (msg) {
    case WM_CREATE: {
        RECT r;
        GetWindowRect(w, &r);
        if (!((r.right - r.left) & 1))
            SetWindowPos(w, NULL, 0, 0, r.right - r.left + 1, r.bottom - r.top,
                         SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE);
        break;
    }
    case WM_PAINT: {
        PAINTSTRUCT ps;
        BeginPaint(w, &ps);
        arrow_paint(w, ps.hdc);
        EndPaint(w, &ps);
        return 0;
    }
    case WM_LBUTTONDOWN:
    case WM_LBUTTONDBLCLK: {
        RECT r;
        GetClientRect(w, &r);
        SendMessage(GetParent(w), WM_VSCROLL,
                    (int)HIWORD(lp) < r.bottom * 9 / 20 ? SB_LINEUP : SB_LINEDOWN,
                    MAKELONG(0, w));
        return 0;
    }
    }
    return DefWindowProc(w, msg, wp, lp);
}


/* a global class: the first applet DLL to load registers it (3.1 shows
   cpArrow windows in MAIN.CPL's and CPWIN386.CPL's dialogs alike); a
   second registration fails harmlessly */
static BOOL registered;

void arrow_register(HINSTANCE h)
{
    WNDCLASS wc;
    wc.style = CS_GLOBALCLASS | CS_DBLCLKS;
    wc.lpfnWndProc = ArrowProc;
    wc.cbClsExtra = wc.cbWndExtra = 0;
    wc.hInstance = h;
    wc.hIcon = NULL;
    wc.hCursor = LoadCursor(NULL, IDC_ARROW);
    wc.hbrBackground = NULL;
    wc.lpszMenuName = NULL;
    wc.lpszClassName = cls_arrow;
    registered = RegisterClass(&wc);
}

void arrow_unregister(HINSTANCE h)
{
    if (registered)
        UnregisterClass(cls_arrow, h);
}
