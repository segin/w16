/*
 * us38.c - probe US38: popup-menu items with accelerator text after a tab
 * ('\t') or a backspace (08h, "\a" in a resource script), in resource
 * menus and in menus built with AppendMenu, InsertMenu, ModifyMenu and
 * ChangeMenu: the popup's size, its pixels, and GetMenuString's answer.
 *
 * Found with Microsoft Visual C++ 1.52's IDE: its File menu's items are
 * "&New\bCtrl+N" and the like (08h, beside '\t' in its other menus); w16
 * drew the 08h as a box glyph in the middle of the text.
 *
 * Run as the shell on Windows 3.1 (tests/dropin.sh -r) and with w16's
 * USER (-f USER.EXE); the OBS lines of the two logs and the screens it
 * saves (C:\US38_nn.BMP, each popup and its surroundings) must be
 * identical (tests/boottest.sh case menutab). Logs:
 *   OBS str.M.I="..."   GetMenuString of item I of popup M (control
 *                       characters written \xNN), and its length
 *   OBS pM.cx, pM.cy    the popup window's size, opened by
 *                       TrackPopupMenu (a timer then saves the screen,
 *                       selects the first item with Down for some, saves
 *                       again, and closes it with Escape)
 * Popups: 0-2 the resource menu US38 (tabs, 08h, mixed), 3 several tabs
 * and backspaces in one item, 4 ampersands beside them, 5 accelerators
 * longer than the text, 6 built with InsertMenu, ModifyMenu and
 * ChangeMenu, 7 submenus with 08h; then a menu bar with 08h and '\t' in
 * its items, and the resource menu's third popup opened from it with
 * Alt+M; 8 texts that start with 08h; bars 10-14 (C:\US38_nn.BMP after
 * popup 8's) with such texts, ampersands, '\t' and 08h in their items.
 * The pointer is moved away (630,470) before each popup.
 */
#include <windows.h>

#define TK_REF "us38ref.h"
#include "tklog.h"

extern BOOL FAR PASCAL InitApp(HINSTANCE);
extern void __cdecl call_keybd_event(WORD ax, WORD bx);

static HINSTANCE hi;
static HWND hMain;
static int cur;                 /* the popup being shown */
static int phase;               /* 0 its first timer, 1 after Down */
static int downs;               /* select the first item too */
static int barmode;             /* the popup came from the bar */

static void pump(DWORD ms)
{
    MSG m;
    DWORD t = GetTickCount();
    while (GetTickCount() - t < ms)
        if (PeekMessage(&m, 0, 0, 0, PM_REMOVE)) {
            TranslateMessage(&m);
            DispatchMessage(&m);
        }
}

static void key(WORD vk, WORD scan)
{
    call_keybd_event(vk, scan);
    call_keybd_event(0x8000 | vk, scan);
}

/* ---- the screen ---- */
static int shot;
static char bmpname[16] = "C:\\US38_00.BMP";
typedef struct { BITMAPINFOHEADER h; RGBQUAD pal[16]; } BMI16;
static BMI16 bmi;

/* a part of the screen (screen coordinates) as a 4-bit BMP */
static void snap(RECT *a)
{
    int cx = a->right - a->left, cy = a->bottom - a->top;
    WORD stride = (WORD)(((cx * 4 + 31) / 32) * 4);
    HDC scr = GetDC(NULL), mem = CreateCompatibleDC(scr);
    HBITMAP bm = CreateCompatibleBitmap(scr, cx, cy), old;
    HGLOBAL hb = GlobalAlloc(GMEM_MOVEABLE, (WORD)(stride * cy));
    BYTE FAR *buf = (BYTE FAR *)GlobalLock(hb);
    BITMAPFILEHEADER fh;
    HFILE f;
    shot++;
    bmpname[8] = (char)('0' + shot / 10);
    bmpname[9] = (char)('0' + shot % 10);
    old = SelectObject(mem, bm);
    BitBlt(mem, 0, 0, cx, cy, scr, a->left, a->top, SRCCOPY);
    SelectObject(mem, old);
    bmi.h.biSize = sizeof bmi.h;
    bmi.h.biWidth = cx;
    bmi.h.biHeight = cy;
    bmi.h.biPlanes = 1;
    bmi.h.biBitCount = 4;
    bmi.h.biCompression = 0;
    bmi.h.biSizeImage = (WORD)(stride * cy);
    GetDIBits(scr, bm, 0, cy, buf, (BITMAPINFO FAR *)&bmi, DIB_RGB_COLORS);
    f = _lcreat(bmpname, 0);
    fh.bfType = 0x4D42;
    fh.bfOffBits = sizeof fh + sizeof bmi;
    fh.bfSize = fh.bfOffBits + bmi.h.biSizeImage;
    fh.bfReserved1 = fh.bfReserved2 = 0;
    _lwrite(f, (LPCSTR)&fh, sizeof fh);
    _lwrite(f, (LPCSTR)&bmi, sizeof bmi);
    _lwrite(f, (LPCSTR)buf, (UINT)bmi.h.biSizeImage);
    _lclose(f);
    GlobalUnlock(hb);
    GlobalFree(hb);
    DeleteObject(bm);
    DeleteDC(mem);
    ReleaseDC(NULL, scr);
}

/* the shown popup menu window (class #32768) */
static HWND popup_wnd(void)
{
    HWND h = GetWindow(GetDesktopWindow(), GW_CHILD);
    char cls[16];
    for (; h; h = GetWindow(h, GW_HWNDNEXT))
        if (IsWindowVisible(h) && GetClassName(h, cls, sizeof cls) &&
            lstrcmp(cls, "#32768") == 0)
            return h;
    return 0;
}

static void snap_popup(void)
{
    HWND p = popup_wnd();
    RECT r;
    char nm[24];
    if (!p) {
        wsprintf(nm, "p%d.none", cur);
        obs(nm, phase);
        return;
    }
    GetWindowRect(p, &r);
    if (phase == 0) {
        wsprintf(nm, "p%d.cx", cur);
        obs(nm, r.right - r.left);
        wsprintf(nm, "p%d.cy", cur);
        obs(nm, r.bottom - r.top);
    }
    InflateRect(&r, 4, 4);
    snap(&r);
}

LRESULT FAR PASCAL __export __loadds main_proc(HWND h, UINT m, WPARAM w, LPARAM l)
{
    if (m == WM_TIMER && w == 1) {
        KillTimer(h, 1);
        snap_popup();
        if (phase == 0 && downs) {
            phase = 1;
            key(VK_DOWN, 0x50);
            SetTimer(h, 1, 300, NULL);
            return 0;
        }
        key(VK_ESCAPE, 0x01);
        if (barmode)
            key(VK_ESCAPE, 0x01);
        return 0;
    }
    return DefWindowProc(h, m, w, l);
}

static void log_strings(int mi, HMENU hm)
{
    int n = GetMenuItemCount(hm), i, k, len;
    char buf[80], t[200], nm[24];
    for (i = 0; i < n; i++) {
        len = GetMenuString(hm, i, buf, sizeof buf, MF_BYPOSITION);
        for (k = 0, t[0] = 0; k < len && buf[k]; k++) {
            int e = lstrlen(t);
            if ((BYTE)buf[k] < 0x20)
                wsprintf(t + e, "\\x%02X", (BYTE)buf[k]);
            else {
                t[e] = buf[k];
                t[e + 1] = 0;
            }
        }
        wsprintf(nm, "str.%d.%d", mi, i);
        obs_str(nm, t);
        wsprintf(nm, "len.%d.%d", mi, i);
        obs(nm, len);
    }
}

static void show_popup(int mi, HMENU hm, int down)
{
    POINT p;
    log_strings(mi, hm);
    cur = mi;
    phase = 0;
    downs = down;
    barmode = 0;
    p.x = 40;
    p.y = 60;
    ClientToScreen(hMain, &p);
    SetCursorPos(630, 470);
    SetTimer(hMain, 1, 400, NULL);
    TrackPopupMenu(hm, 0, p.x, p.y, 0, hMain, NULL);
    KillTimer(hMain, 1);
    pump(200);
}

int __cdecl tmain(void)
{
    WNDCLASS wc;
    HMENU res, hm, sub, bar;
    RECT r;
    int i, k;

    hi = (HINSTANCE)init_regs[5];
    InitApp(hi);
    out("US38 start\r\n");
    wc.style = 0;
    wc.lpfnWndProc = main_proc;
    wc.cbClsExtra = wc.cbWndExtra = 0;
    wc.hInstance = hi;
    wc.hIcon = 0;
    wc.hCursor = LoadCursor(NULL, IDC_ARROW);
    wc.hbrBackground = (HBRUSH)(COLOR_WINDOW + 1);
    wc.lpszMenuName = NULL;
    wc.lpszClassName = "US38";
    RegisterClass(&wc);
    hMain = CreateWindow("US38", "US38", WS_OVERLAPPEDWINDOW | WS_VISIBLE, 0, 0, 560, 420,
                         0, 0, hi, NULL);
    UpdateWindow(hMain);
    pump(500);

    /* 0-2: the resource menu's popups */
    res = LoadMenu(hi, "US38");
    show_popup(0, GetSubMenu(res, 0), 0);
    show_popup(1, GetSubMenu(res, 1), 1);
    show_popup(2, GetSubMenu(res, 2), 1);

    /* 3: several tabs and backspaces in one item */
    hm = CreatePopupMenu();
    AppendMenu(hm, MF_STRING, 301, "A\tB\tC");
    AppendMenu(hm, MF_STRING, 302, "A\bB\bC");
    AppendMenu(hm, MF_STRING, 303, "A\tB\bC");
    AppendMenu(hm, MF_STRING, 304, "A\bB\tC");
    AppendMenu(hm, MF_STRING, 305, "\bLead");
    AppendMenu(hm, MF_STRING, 306, "\tLead2");
    AppendMenu(hm, MF_STRING, 307, "Wide item text\tX");
    show_popup(3, hm, 0);
    DestroyMenu(hm);

    /* 4: ampersands beside them */
    hm = CreatePopupMenu();
    AppendMenu(hm, MF_STRING, 401, "&\tX");
    AppendMenu(hm, MF_STRING, 402, "Ite&m&\bCtrl");
    AppendMenu(hm, MF_STRING, 403, "x&&y\bZ");
    AppendMenu(hm, MF_STRING, 404, "&&\tW");
    AppendMenu(hm, MF_STRING, 405, "Pl&ain");
    AppendMenu(hm, MF_STRING, 406, "Two\b&Amp");
    show_popup(4, hm, 1);
    DestroyMenu(hm);

    /* 5: accelerators longer than the texts */
    hm = CreatePopupMenu();
    AppendMenu(hm, MF_STRING, 501, "Ab\bCtrl+Shift+F12");
    AppendMenu(hm, MF_STRING | MF_CHECKED, 502, "Longer text here");
    AppendMenu(hm, MF_STRING, 503, "Q\bF1");
    AppendMenu(hm, MF_STRING | MF_GRAYED, 504, "Gray\tCtrl+Shift+Ins");
    show_popup(5, hm, 1);
    DestroyMenu(hm);

    /* 6: InsertMenu, ModifyMenu, ChangeMenu */
    hm = CreatePopupMenu();
    AppendMenu(hm, MF_STRING, 601, "x");
    AppendMenu(hm, MF_STRING, 602, "y");
    AppendMenu(hm, MF_STRING, 603, "z");
    ModifyMenu(hm, 0, MF_BYPOSITION | MF_STRING, 601, "&Mod\bCtrl+M");
    InsertMenu(hm, 1, MF_BYPOSITION | MF_STRING, 604, "&Ins\tCtrl+I");
    ModifyMenu(hm, 603, MF_BYCOMMAND | MF_STRING, 603, "&Zed item\bShift+Z");
    ChangeMenu(hm, 602, "&Changed\bF2", 605, MF_CHANGE | MF_STRING);
    ChangeMenu(hm, 0, "&Appended\bF9", 606, MF_APPEND | MF_STRING);
    InsertMenu(hm, 0, MF_BYPOSITION | MF_STRING, 607, "&First\b");
    show_popup(6, hm, 1);
    DestroyMenu(hm);

    /* 7: submenus whose texts end in 08h */
    hm = CreatePopupMenu();
    sub = CreatePopupMenu();
    AppendMenu(sub, MF_STRING, 701, "&Edit\bCtrl+1");
    AppendMenu(sub, MF_STRING, 702, "&Debug\bCtrl+2");
    AppendMenu(hm, MF_STRING, 703, "Scan &All Dependencies");
    AppendMenu(hm, MF_POPUP, (UINT)sub, "&Load Workspace\b");
    AppendMenu(hm, MF_STRING, 704, "&Open\b");
    AppendMenu(hm, MF_STRING, 705, "E&xecute 12345678.123\bCtrl+F5");
    show_popup(7, hm, 0);
    DestroyMenu(hm);

    /* 8: texts that start with 08h */
    hm = CreatePopupMenu();
    AppendMenu(hm, MF_STRING, 801, "ab");
    AppendMenu(hm, MF_STRING, 802, "\bLeadLeadLeadLead");
    AppendMenu(hm, MF_STRING, 803, "\b&Lead\tTab");
    AppendMenu(hm, MF_STRING, 804, "\b");
    AppendMenu(hm, MF_STRING, 805, "cd");
    show_popup(8, hm, 1);
    DestroyMenu(hm);

    /* bars 10-14: a text that starts with 08h, with and without
       ampersands; texts after '\t' and 08h in a bar */
    for (k = 0; k < 5; k++) {
        static const char *const bt[5][4] = {
            { "One", "\bFour", "Five", 0 },
            { "One", "\bF&our&x", "Five", 0 },
            { "One", "\b&Four", "Five", 0 },
            { "A\tTabtext", "Tw&o\tLongtab", "Thr\bRightRightRight", "Fou" },
            { "&One", "\bA\bB", "\bC\tD", "E" },
        };
        HMENU b = CreateMenu();
        for (i = 0; i < 4 && bt[k][i]; i++)
            AppendMenu(b, MF_STRING, 900 + i, bt[k][i]);
        SetMenu(hMain, b);
        DrawMenuBar(hMain);
        pump(500);
        log_strings(10 + k, b);
        GetWindowRect(hMain, &r);
        r.bottom = r.top + 60;
        snap(&r);
        SetMenu(hMain, 0);
        DestroyMenu(b);
    }

    /* the bar: 08h and tabs in its items; the third popup of the
       resource menu from it (Alt+M) */
    bar = CreateMenu();
    AppendMenu(bar, MF_STRING, 801, "&One");
    AppendMenu(bar, MF_STRING, 802, "T&wo\bX");
    AppendMenu(bar, MF_STRING, 803, "Th&ree\tY");
    AppendMenu(bar, MF_POPUP, (UINT)GetSubMenu(res, 2), "&Mixed");
    AppendMenu(bar, MF_STRING, 804, "\bF&our");
    AppendMenu(bar, MF_STRING, 805, "Fi&ve");
    SetMenu(hMain, bar);
    DrawMenuBar(hMain);
    pump(500);
    log_strings(15, bar);
    GetWindowRect(hMain, &r);
    r.bottom = r.top + 60;
    snap(&r);
    cur = 9;
    phase = 0;
    downs = 1;
    barmode = 1;
    SetTimer(hMain, 1, 400, NULL);
    call_keybd_event(VK_MENU, 0x38);
    key('M', 0x32);
    call_keybd_event(0x8000 | VK_MENU, 0x38);
    pump(2500);
    KillTimer(hMain, 1);
    out("US38 end\r\n");
    return 0;
}
