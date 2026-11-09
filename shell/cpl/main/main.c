/*
 * main.c - MAIN.CPL's CPlApplet, the applet table and the cpArrow control.
 *
 * Observed on Windows 3.1 with Control Panel's calls logged (the CP1
 * probe, ARCHITECTURE.md 14): Control Panel asks with CPL_NEWINQUIRE and,
 * since the original MAIN.CPL leaves dwSize 0, then with CPL_INQUIRE; the
 * names and descriptions are string resources. Each applet's dialog is
 * modal, owned by the window Control Panel passes (NULL from
 * "CONTROL MAIN.CPL").
 */
#include "maincpl.h"

HINSTANCE g_hinst;
char cls_arrow[] = "cpArrow";

static const int name_ids[AP_COUNT] = {
    IDS_NAME_COLOR, IDS_NAME_FONTS, IDS_NAME_PORTS, IDS_NAME_MOUSE, IDS_NAME_DESKTOP,
    IDS_NAME_KEYBOARD, IDS_NAME_PRINTERS, IDS_NAME_INTL, IDS_NAME_DATETIME
};

int cpl_dialog(HWND parent, int id, DLGPROC proc)
{
    return DialogBox(g_hinst, MAKEINTRESOURCE(id), parent, proc);
}

/* UNVERIFIED: the original's help contexts in CONTROL.HLP are not known;
   the Help buttons open CONTROL.HLP at the applet's keyword */
void cpl_help(HWND dlg, int applet)
{
    char key[32], *s = key, *d = key;
    LoadString(g_hinst, name_ids[applet], key, sizeof key);
    for (; *s; s++)
        if (*s != '&')
            *d++ = *s;
    *d = 0;
    WinHelp(dlg, "CONTROL.HLP", HELP_KEY, (DWORD)(LPSTR)key);
}

static void not_yet(HWND parent, int applet)
{
    char t[40], m[160];
    LoadString(g_hinst, name_ids[applet], t, sizeof t);
    LoadString(g_hinst, IDS_NOTYET, m, sizeof m);
    MessageBox(parent, m, t, MB_OK | MB_ICONINFORMATION);
}

static void run(HWND parent, int applet)
{
    switch (applet) {
    case AP_KEYBOARD:
        keyboard_applet(parent);
        break;
    default:
        not_yet(parent, applet);
        break;
    }
}

LONG FAR PASCAL __loadds CPlApplet(HWND hwnd, UINT msg, LPARAM l1, LPARAM l2)
{
    switch (msg) {
    case CPL_INIT:
        return 1;
    case CPL_GETCOUNT:
        return AP_COUNT;
    case CPL_INQUIRE: {
        LPCPLINFO p = (LPCPLINFO)l2;
        int i = (int)l1;
        if (i < 0 || i >= AP_COUNT)
            return 1;
        p->idIcon = IDI_APPLET + i;
        p->idName = name_ids[i];
        p->idInfo = IDS_INFO_COLOR + name_ids[i] - IDS_NAME_COLOR;
        p->lData = 0;
        return 0;
    }
    case CPL_DBLCLK:
        run(hwnd, (int)l1);
        return 0;
    }
    return 0;
}

/* ---- cpArrow ----
   Observed (pixels): a black box, its inside COLOR_BTNFACE, split by a
   black line 9 pixels below the top; an up arrow whose base is 2 rows
   above the line and a down arrow from 3 rows below it, each 6 rows of
   1, 3, 3, 5, 5, 7 pixels, centred. */
static void arrow_paint(HWND w, HDC dc)
{
    RECT r, t;
    HBRUSH face, black;
    int mid, cx, k;
    GetClientRect(w, &r);
    face = CreateSolidBrush(GetSysColor(COLOR_BTNFACE));
    black = GetStockObject(BLACK_BRUSH);
    FillRect(dc, &r, face);
    FrameRect(dc, &r, black);
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

int __cdecl __far cpl_libmain(HINSTANCE h)
{
    WNDCLASS wc;
    g_hinst = h;
    wc.style = CS_GLOBALCLASS | CS_DBLCLKS;
    wc.lpfnWndProc = ArrowProc;
    wc.cbClsExtra = wc.cbWndExtra = 0;
    wc.hInstance = h;
    wc.hIcon = NULL;
    wc.hCursor = LoadCursor(NULL, IDC_ARROW);
    wc.hbrBackground = NULL;
    wc.lpszMenuName = NULL;
    wc.lpszClassName = cls_arrow;
    RegisterClass(&wc);
    return 1;
}

int FAR PASCAL __loadds WEP(int exit)
{
    (void)exit;
    UnregisterClass(cls_arrow, g_hinst);
    return 1;
}
