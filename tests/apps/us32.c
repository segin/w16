/*
 * us32.c - probe US32: a list box's height (LBS_NOINTEGRALHEIGHT or not)
 * and where it draws an item only partly inside it.
 *
 * Found with QuickTime's Control Panel (QTW16.CPL, its Files list): on w16
 * the selected item at the list's bottom edge, partly visible, was drawn
 * whole past the list's border, over the static below it, once the list
 * scrolled.
 *
 * Run as the shell on Windows 3.1 (tests/dropin.sh -r) and with w16's
 * USER (-f USER.EXE); the OBS lines of the two logs must be identical
 * (tests/boottest.sh case lbclip). Sections:
 *   cls     the system classes' styles (GetClassInfo), and a superclass of
 *           LISTBOX registered from them with this program's instance
 *   ih      list boxes made with CreateWindow in a window (system font)
 *           of several styles and heights; each one's window and client
 *           height as made, after 20 items, MoveWindow to 75, SetWindowPos
 *           to 90, and WM_SETFONT with ANSI_VAR_FONT
 *   cb      combo boxes: a drop-down's list as dropped, a simple one's
 *           list, for 3 and 20 items and several heights
 *   d1-d8   dialogs (us32.rc) of a list box 45 dialog units high with a
 *           static just below; 12 items; for each: its height, its DC's
 *           clip box, then the item partly shown selected and scrolled by
 *           every way (LB_SETCURSEL, a click on the scroll bar's down
 *           arrow, the Down key, LB_SETTOPINDEX, WM_VSCROLL, a click on
 *           the item), each time the top item, the selection, how many
 *           pixels of the highlight colour lie outside the list box's
 *           window in the area around it and down past the static, and a
 *           checksum of that area's pixels (each step also saved as
 *           C:\US32_nnn.BMP, the screen, for looking at)
 */
#include <windows.h>

#define TK_REF "us32ref.h"
#include "tklog.h"

extern WORD FAR PASCAL WaitEvent(HTASK);
extern BOOL FAR PASCAL InitApp(HINSTANCE);
extern void __cdecl call_keybd_event(WORD ax, WORD bx);
extern void __cdecl call_mouse_event(WORD status, WORD x, WORD y, WORD buttons);

static HINSTANCE hi;
static HWND hMain;
static WNDPROC lbproc;
static int odvar;               /* WM_MEASUREITEM: variable heights */
static int dn;                  /* the dialog section (1-11) */
static char drawrec[300];       /* WM_DRAWITEMs since the last step */

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

static void obsf(const char *name, const char *fmt, int a, int b, int c, int d)
{
    char t[64];
    wsprintf(t, fmt, a, b, c, d);
    obs_str(name, t);
}

/* ---- the screen ---- */
static int shot;
static char bmpname[16] = "C:\\US32_000.BMP";
typedef struct { BITMAPINFOHEADER h; RGBQUAD pal[16]; } BMI16;
static BMI16 bmi;

static void snap(void)
{
    int cx = GetSystemMetrics(SM_CXSCREEN), cy = GetSystemMetrics(SM_CYSCREEN), y;
    WORD stride = (WORD)(((cx * 4 + 31) / 32) * 4);
    HDC scr = GetDC(NULL), mem = CreateCompatibleDC(scr);
    HBITMAP bm = CreateCompatibleBitmap(scr, cx, cy), old;
    HGLOBAL hb = GlobalAlloc(GMEM_MOVEABLE, (DWORD)stride * 32);
    BYTE FAR *buf = (BYTE FAR *)GlobalLock(hb);
    BITMAPFILEHEADER fh;
    HFILE f;
    shot++;
    bmpname[8] = (char)('0' + shot / 100);
    bmpname[9] = (char)('0' + shot / 10 % 10);
    bmpname[10] = (char)('0' + shot % 10);
    old = SelectObject(mem, bm);
    BitBlt(mem, 0, 0, cx, cy, scr, 0, 0, SRCCOPY);
    SelectObject(mem, old);
    bmi.h.biSize = sizeof bmi.h;
    bmi.h.biWidth = cx;
    bmi.h.biHeight = cy;
    bmi.h.biPlanes = 1;
    bmi.h.biBitCount = 4;
    bmi.h.biCompression = 0;
    bmi.h.biSizeImage = 0;
    for (y = 0; y < cy; y++)
        bmi.h.biSizeImage += stride;
    f = _lcreat(bmpname, 0);
    fh.bfType = 0x4D42;
    fh.bfOffBits = sizeof fh + sizeof bmi;
    fh.bfSize = fh.bfOffBits + bmi.h.biSizeImage;
    fh.bfReserved1 = fh.bfReserved2 = 0;
    _llseek(f, fh.bfOffBits, 0);
    for (y = 0; y < cy; y += 32) {
        GetDIBits(scr, bm, y, 32, buf, (BITMAPINFO FAR *)&bmi, DIB_RGB_COLORS);
        _lwrite(f, buf, stride * 32);
    }
    _llseek(f, 0, 0);
    _lwrite(f, (LPCSTR)&fh, sizeof fh);
    _lwrite(f, (LPCSTR)&bmi, sizeof bmi);
    _lclose(f);
    GlobalUnlock(hb);
    GlobalFree(hb);
    DeleteObject(bm);
    DeleteDC(mem);
    ReleaseDC(NULL, scr);
}

/* the area a (screen coordinates): a checksum of its pixels, and how many
   of the highlight colour lie outside the rectangle in; *fl: whether the
   pixels at fp and right of it are not both the window colour */
static POINT fp;
static int fl;

static void grab(RECT *a, RECT *in, WORD *sum, WORD *hl)
{
    int cx = a->right - a->left, cy = a->bottom - a->top, x, y;
    WORD stride = (WORD)(((cx * 4 + 31) / 32) * 4);
    HDC scr = GetDC(NULL), mem = CreateCompatibleDC(scr);
    HBITMAP bm = CreateCompatibleBitmap(scr, cx, cy), old;
    HGLOBAL hb = GlobalAlloc(GMEM_MOVEABLE, (DWORD)(WORD)(stride * cy));
    BYTE FAR *buf = (BYTE FAR *)GlobalLock(hb);
    COLORREF hc = GetSysColor(COLOR_HIGHLIGHT), wc = GetSysColor(COLOR_WINDOW);
    int hidx = -1, widx = -1, i;
    WORD s = 0, n = 0;
    old = SelectObject(mem, bm);
    BitBlt(mem, 0, 0, cx, cy, scr, a->left, a->top, SRCCOPY);
    SelectObject(mem, old);
    bmi.h.biSize = sizeof bmi.h;
    bmi.h.biWidth = cx;
    bmi.h.biHeight = cy;
    bmi.h.biPlanes = 1;
    bmi.h.biBitCount = 4;
    bmi.h.biCompression = 0;
    bmi.h.biSizeImage = 0;
    GetDIBits(scr, bm, 0, cy, buf, (BITMAPINFO FAR *)&bmi, DIB_RGB_COLORS);
    for (i = 0; i < 16; i++) {
        COLORREF c = RGB(bmi.pal[i].rgbRed, bmi.pal[i].rgbGreen, bmi.pal[i].rgbBlue);
        if (c == hc && hidx < 0)
            hidx = i;
        if (c == wc && widx < 0)
            widx = i;
    }
    fl = 0;
    for (y = 0; y < cy; y++) {
        BYTE FAR *row = buf + (WORD)(cy - 1 - y) * stride;
        for (x = 0; x < cx; x++) {
            int p = (x & 1) ? row[x >> 1] & 15 : row[x >> 1] >> 4;
            s = (WORD)(s * 31 + p + 1);
            if (a->top + y == fp.y && (a->left + x == fp.x || a->left + x == fp.x + 1) && p != widx)
                fl = 1;
            if (p == hidx) {
                POINT pt;
                pt.x = a->left + x;
                pt.y = a->top + y;
                if (!PtInRect(in, pt))
                    n++;
            }
        }
    }
    GlobalUnlock(hb);
    GlobalFree(hb);
    DeleteObject(bm);
    DeleteDC(mem);
    ReleaseDC(NULL, scr);
    *sum = s;
    *hl = n;
}

/* ---- input ---- */
static WORD absx(int x) { return (WORD)MulDiv(x, 32767, GetSystemMetrics(SM_CXSCREEN)) * 2 + 1; }
static WORD absy(int y) { return (WORD)MulDiv(y, 32767, GetSystemMetrics(SM_CYSCREEN)) * 2 + 1; }

static void click_at(int x, int y)
{
    call_mouse_event(0x8001, absx(x), absy(y), 0);
    pump(100);
    /* down and up queued together: a press on a scroll arrow runs a
       tracking loop until the button is up */
    call_mouse_event(0x8002, absx(x), absy(y), 0);
    call_mouse_event(0x8004, absx(x), absy(y), 0);
    pump(300);
    /* the pointer out of the way: the screen's bottom right corner */
    call_mouse_event(0x8001, 0xFFFF, 0xFFFF, 0);
    pump(300);
}

static void key(WORD vk, WORD scan)
{
    call_keybd_event(vk, scan);
    call_keybd_event(0x8000 | vk, scan);
    pump(300);
}

/* ---- the superclass ---- */
/* what the superclass's windows get while they are made (logged as
   ~ lines, not compared): message, then the client height after it */
static char trace[700];
static int tracing;

static void trace_add(const char *t)
{
    int n = lstrlen(trace);
    if (n + lstrlen(t) + 2 < sizeof trace) {
        if (n)
            trace[n++] = ' ';
        lstrcpy(trace + n, t);
    }
}

LRESULT FAR PASCAL __export __loadds sc_proc(HWND h, UINT m, WPARAM w, LPARAM l)
{
    LRESULT r;
    if (tracing == 2 && (m == WM_SIZE || m == WM_SETFOCUS || m == WM_KILLFOCUS || m == WM_PAINT ||
                         m == WM_SETFONT || m == WM_WINDOWPOSCHANGED || m == WM_NCCALCSIZE ||
                         m == WM_SHOWWINDOW || m == WM_CREATE)) {
        char t[8];
        wsprintf(t, "%x", m);
        trace_add(t);
    }
    if (tracing == 2 && m == LB_ADDSTRING && !SendMessage(h, LB_GETCOUNT, 0, 0))
        trace_add("A");
    r = CallWindowProc((FARPROC)lbproc, h, m, w, l);
    if (tracing == 1 && (m == WM_NCCREATE || m == WM_CREATE || m == WM_SIZE || m == WM_SETFONT ||
                    m == WM_WINDOWPOSCHANGED || m == WM_SHOWWINDOW || m == WM_NCCALCSIZE)) {
        RECT c;
        char t[16];
        GetClientRect(h, &c);
        wsprintf(t, "%x/%d", m, c.bottom);
        trace_add(t);
    }
    return r;
}

static void sec_cls(void)
{
    WNDCLASS wc;
    static const char *names[] = { "LISTBOX", "ComboLBox", "COMBOBOX", "STATIC", "EDIT", "BUTTON", 0 };
    char nm[32];
    int i;
    for (i = 0; names[i]; i++) {
        BOOL ok = GetClassInfo(NULL, names[i], &wc);
        wsprintf(nm, "cls.%s", (LPSTR)names[i]);
        obsf(nm, "%d style %x extra %d", ok != 0, wc.style, wc.cbWndExtra, 0);
    }
    GetClassInfo(NULL, "LISTBOX", &wc);
    lbproc = wc.lpfnWndProc;
    wc.lpfnWndProc = (WNDPROC)sc_proc;
    wc.hInstance = hi;
    wc.lpszClassName = "US32LB";
    obs("cls.super", RegisterClass(&wc) != 0);
}

/* ---- heights ---- */
static void hgt(const char *name, HWND h)
{
    RECT w, c;
    POINT p;
    GetWindowRect(h, &w);
    GetClientRect(h, &c);
    p.x = w.left;
    p.y = w.top;
    ScreenToClient(GetParent(h) ? GetParent(h) : hMain, &p);
    obsf(name, "%d,%d h%d c%d", p.x, p.y, w.bottom - w.top, c.bottom);
}

static const DWORD ihstyle[] = {
    WS_BORDER | WS_VSCROLL,
    WS_BORDER | WS_VSCROLL | LBS_NOINTEGRALHEIGHT,
    WS_VSCROLL,
    WS_BORDER,
    0,
    WS_BORDER | WS_VSCROLL | LBS_OWNERDRAWFIXED | LBS_HASSTRINGS,
    WS_BORDER | WS_VSCROLL | LBS_OWNERDRAWVARIABLE | LBS_HASSTRINGS,
    WS_BORDER | WS_VSCROLL | WS_HSCROLL,
    WS_BORDER | LBS_MULTICOLUMN | WS_HSCROLL,
    WS_DLGFRAME | WS_VSCROLL,
};
static const int ihh[] = { 40, 57, 64, 7, 100 };

static void sec_ih(void)
{
    int v, k, i;
    char nm[32];
    for (v = 0; v < sizeof ihstyle / sizeof ihstyle[0]; v++)
        for (k = 0; k < sizeof ihh / sizeof ihh[0]; k++) {
            HWND h;
            odvar = (ihstyle[v] & LBS_OWNERDRAWVARIABLE) != 0;
            h = CreateWindow("LISTBOX", NULL, WS_CHILD | WS_VISIBLE | LBS_NOTIFY | ihstyle[v],
                             10, 10, 120, ihh[k], hMain, (HMENU)100, hi, NULL);
            wsprintf(nm, "ih.%d.%d", v, ihh[k]);
            if (!h) {
                obs(nm, 0);
                continue;
            }
            hgt(nm, h);
            for (i = 0; i < 20; i++) {
                char t[16];
                wsprintf(t, "Item %d", i);
                SendMessage(h, LB_ADDSTRING, 0, (LPARAM)(LPSTR)t);
            }
            wsprintf(nm, "ih.%d.%d.items", v, ihh[k]);
            hgt(nm, h);
            MoveWindow(h, 10, 10, 120, 75, TRUE);
            wsprintf(nm, "ih.%d.%d.move75", v, ihh[k]);
            hgt(nm, h);
            SetWindowPos(h, 0, 0, 0, 120, 90, SWP_NOMOVE | SWP_NOZORDER);
            wsprintf(nm, "ih.%d.%d.swp90", v, ihh[k]);
            hgt(nm, h);
            SendMessage(h, WM_SETFONT, (WPARAM)GetStockObject(ANSI_VAR_FONT), 0);
            wsprintf(nm, "ih.%d.%d.font", v, ihh[k]);
            hgt(nm, h);
            obsf(nm, "ih %d top %d", (int)SendMessage(h, LB_GETITEMHEIGHT, 0, 0),
                 (int)SendMessage(h, LB_GETTOPINDEX, 0, 0), 0, 0);
            DestroyWindow(h);
        }
    /* made, then the font set at once (the dialog manager's order) */
    for (v = 0; v < 2; v++)
        for (k = 0; k < sizeof ihh / sizeof ihh[0]; k++) {
            HWND h = CreateWindow("LISTBOX", NULL, WS_CHILD | WS_VISIBLE | LBS_NOTIFY | WS_BORDER |
                                  WS_VSCROLL | (v ? LBS_OWNERDRAWFIXED | LBS_HASSTRINGS : 0),
                                  10, 10, 120, ihh[k], hMain, (HMENU)100, hi, NULL);
            SendMessage(h, WM_SETFONT, (WPARAM)GetStockObject(ANSI_VAR_FONT), 0);
            wsprintf(nm, "ihf.%d.%d", v, ihh[k]);
            hgt(nm, h);
            SendMessage(h, WM_SETFONT, (WPARAM)GetStockObject(SYSTEM_FIXED_FONT), TRUE);
            wsprintf(nm, "ihf.%d.%d.fixed", v, ihh[k]);
            hgt(nm, h);
            DestroyWindow(h);
        }
    odvar = 0;
}

/* ---- a new font, then sized or not ---- */
static void sec_sf(void)
{
    int k;
    char nm[32];
    /* 6: the list box hidden; 7: in a hidden window; 8: in a hidden
       window, ownerdrawn (items of 20) */
    HWND hid = CreateWindow("US32M", "hidden", WS_OVERLAPPEDWINDOW, 0, 0, 200, 200, 0, 0, hi, NULL);
    for (k = 0; k < 13; k++) {
        HWND h;
        trace[0] = 0;
        tracing = 1;
        h = CreateWindow("US32LB", NULL, WS_CHILD | (k == 6 ? 0 : WS_VISIBLE) | WS_BORDER | WS_VSCROLL |
                         LBS_NOTIFY | (k == 8 ? LBS_OWNERDRAWFIXED | LBS_HASSTRINGS : 0),
                         10, 10, 120, k == 5 || k >= 7 ? 118 : 57, k == 7 || k == 8 ? hid : hMain,
                         (HMENU)100, hi, NULL);
        trace_add("|");
        SendMessage(h, WM_SETFONT, (WPARAM)GetStockObject(ANSI_VAR_FONT), k == 1);
        trace_add("|");
        switch (k) {
        case 2:
            SetWindowPos(h, 0, 0, 0, 0, 0, SWP_NOSIZE | SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE);
            break;
        case 3:
            SetWindowPos(h, 0, 0, 0, 0, 0, SWP_NOSIZE | SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE |
                         SWP_FRAMECHANGED);
            break;
        case 9:
        case 10:
        case 11:
        case 12: {
            /* moved by a pixel: with its size (9), without (10); moved
               nowhere with its size by MoveWindow (11); the size asked for
               when made (12) */
            RECT r;
            POINT o;
            GetWindowRect(h, &r);
            o.x = r.left;
            o.y = r.top;
            ScreenToClient(hMain, &o);
            if (k == 9)
                SetWindowPos(h, 0, o.x + 1, o.y, r.right - r.left, r.bottom - r.top,
                             SWP_NOZORDER | SWP_NOACTIVATE);
            else if (k == 10)
                SetWindowPos(h, 0, o.x + 1, o.y, 0, 0, SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE);
            else if (k == 11)
                MoveWindow(h, o.x, o.y, r.right - r.left, r.bottom - r.top, TRUE);
            else
                SetWindowPos(h, 0, 0, 0, 120, 118, SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE);
            break;
        }
        case 4:
        case 5: {
            RECT r;
            GetWindowRect(h, &r);
            SetWindowPos(h, 0, 0, 0, r.right - r.left, r.bottom - r.top,
                         SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE);
            break;
        }
        }
        tracing = 0;
        wsprintf(nm, "sf.%d", k);
        hgt(nm, h);
        wsprintf(nm, "~sf.%d.trace", k);
        obs_str(nm, trace);
        DestroyWindow(h);
    }
    DestroyWindow(hid);
}

/* ---- the scroll bar's range and position ---- */
static void sec_bar(void)
{
    static const int ns[] = { 0, 3, 4, 5, 6, 8, 12, 50, 105, 106, 300 };
    int k, i, lo, hi2;
    char nm[32];
    for (k = 0; k < sizeof ns / sizeof ns[0]; k++) {
        /* 4 whole items of 16 and a part */
        HWND h = CreateWindow("LISTBOX", NULL, WS_CHILD | WS_VISIBLE | WS_BORDER | WS_VSCROLL |
                              LBS_NOINTEGRALHEIGHT, 10, 10, 120, 70, hMain, (HMENU)100, hi, NULL);
        for (i = 0; i < ns[k]; i++) {
            char t[16];
            wsprintf(t, "Item %d", i);
            SendMessage(h, LB_ADDSTRING, 0, (LPARAM)(LPSTR)t);
        }
        GetScrollRange(h, SB_VERT, &lo, &hi2);
        wsprintf(nm, "bar.%d", ns[k]);
        obsf(nm, "%d-%d at %d vis %d", lo, hi2, GetScrollPos(h, SB_VERT),
             (GetWindowLong(h, GWL_STYLE) & WS_VSCROLL) != 0);
        for (i = 1; i < 300 && i <= ns[k]; i = i * 3 + 1) {
            SendMessage(h, LB_SETTOPINDEX, i, 0);
            wsprintf(nm, "bar.%d.top%d", ns[k], i);
            obsf(nm, "top %d at %d", (int)SendMessage(h, LB_GETTOPINDEX, 0, 0),
                 GetScrollPos(h, SB_VERT), 0, 0);
        }
        for (i = 0; i <= 100; i += 25) {
            SendMessage(h, WM_VSCROLL, SB_THUMBPOSITION, MAKELONG(i, 0));
            wsprintf(nm, "bar.%d.thumb%d", ns[k], i);
            obsf(nm, "top %d at %d", (int)SendMessage(h, LB_GETTOPINDEX, 0, 0),
                 GetScrollPos(h, SB_VERT), 0, 0);
        }
        SendMessage(h, WM_VSCROLL, SB_BOTTOM, 0);
        wsprintf(nm, "bar.%d.bottom", ns[k]);
        obsf(nm, "top %d at %d", (int)SendMessage(h, LB_GETTOPINDEX, 0, 0),
             GetScrollPos(h, SB_VERT), 0, 0);
        SendMessage(h, WM_VSCROLL, SB_PAGEUP, 0);
        wsprintf(nm, "bar.%d.pageup", ns[k]);
        obsf(nm, "top %d at %d", (int)SendMessage(h, LB_GETTOPINDEX, 0, 0),
             GetScrollPos(h, SB_VERT), 0, 0);
        DestroyWindow(h);
    }
}

/* ---- combo boxes ---- */
static void sec_cb(void)
{
    static const DWORD kinds[] = { CBS_DROPDOWN, CBS_DROPDOWNLIST, CBS_SIMPLE };
    static const int hs[] = { 30, 60, 100, 157 };
    static const int ns[] = { 3, 20 };
    int k, j, n, i;
    char nm[32];
    for (k = 0; k < 3; k++)
        for (j = 0; j < 4; j++)
            for (n = 0; n < 2; n++) {
                HWND h = CreateWindow("COMBOBOX", NULL, WS_CHILD | WS_VISIBLE | WS_VSCROLL | kinds[k],
                                      10, 10, 120, hs[j], hMain, (HMENU)100, hi, NULL), l = 0;
                for (i = 0; i < ns[n]; i++) {
                    char t[16];
                    wsprintf(t, "Item %d", i);
                    SendMessage(h, CB_ADDSTRING, 0, (LPARAM)(LPSTR)t);
                }
                wsprintf(nm, "cb.%d.%d.%d", k, hs[j], ns[n]);
                hgt(nm, h);
                if (kinds[k] == CBS_SIMPLE) {
                    HWND c;
                    for (c = GetWindow(h, GW_CHILD); c; c = GetWindow(c, GW_HWNDNEXT)) {
                        char cl[16];
                        GetClassName(c, cl, sizeof cl);
                        if (!lstrcmpi(cl, "ComboLBox") || !lstrcmpi(cl, "ListBox"))
                            l = c;
                    }
                } else {
                    SendMessage(h, CB_SHOWDROPDOWN, TRUE, 0);
                    pump(100);
                    l = FindWindow("ComboLBox", NULL);
                }
                wsprintf(nm, "cb.%d.%d.%d.list", k, hs[j], ns[n]);
                if (l) {
                    RECT w, c, b;
                    GetWindowRect(l, &w);
                    GetClientRect(l, &c);
                    GetWindowRect(h, &b);
                    obsf(nm, "%d,%d h%d c%d", w.left - b.left, w.top - b.top, w.bottom - w.top, c.bottom);
                    wsprintf(nm, "cb.%d.%d.%d.listw", k, hs[j], ns[n]);
                    obsf(nm, "%d of %d", w.right - w.left, b.right - b.left, 0, 0);
                } else
                    obs(nm, 0);
                if (kinds[k] != CBS_SIMPLE)
                    SendMessage(h, CB_SHOWDROPDOWN, FALSE, 0);
                DestroyWindow(h);
                pump(50);
            }
}

/* ---- the dialogs ---- */
BOOL FAR PASCAL __export __loadds dlg_proc(HWND h, UINT m, WPARAM w, LPARAM l)
{
    if (m == WM_MEASUREITEM) {
        MEASUREITEMSTRUCT FAR *mi = (MEASUREITEMSTRUCT FAR *)l;
        char t[16];
        wsprintf(t, "M%d:%d", mi->itemID, mi->itemHeight);
        if (tracing)
            trace_add(t);
        mi->itemHeight = odvar ? 12 + 5 * (mi->itemID & 1) : 15;
        return TRUE;
    }
    if (m == WM_DRAWITEM) {
        DRAWITEMSTRUCT FAR *di = (DRAWITEMSTRUCT FAR *)l;
        char t[64];
        RECT r = di->rcItem;
        {
            /* the record: item:action/state */
            char tk[16];
            int n2 = lstrlen(drawrec);
            wsprintf(tk, "%d:%x/%x", (int)di->itemID, di->itemAction, di->itemState);
            if (n2 + lstrlen(tk) + 2 < sizeof drawrec) {
                if (n2)
                    drawrec[n2++] = ' ';
                lstrcpy(drawrec + n2, tk);
            }
        }
        if (di->itemID == (UINT)-1)
            return TRUE;
        if (di->itemAction == ODA_FOCUS) {
            DrawFocusRect(di->hDC, &r);
            return TRUE;
        }
        if (dn == 10) {
            /* the item's own clip region, as an owner may set it (and
               SaveDC/RestoreDC round it) */
            HRGN rg = CreateRectRgn(r.left, r.top, r.right, r.bottom);
            SaveDC(di->hDC);
            SelectClipRgn(di->hDC, rg);
            DeleteObject(rg);
        }
        FillRect(di->hDC, &r, (HBRUSH)((di->itemState & ODS_SELECTED) ? COLOR_HIGHLIGHT + 1 : COLOR_WINDOW + 1));
        SendMessage(di->hwndItem, LB_GETTEXT, di->itemID, (LPARAM)(LPSTR)t);
        SetBkMode(di->hDC, TRANSPARENT);
        SetTextColor(di->hDC, GetSysColor((di->itemState & ODS_SELECTED) ? COLOR_HIGHLIGHTTEXT : COLOR_WINDOWTEXT));
        TextOut(di->hDC, r.left + 2, r.top, t, lstrlen(t));
        if (dn == 10)
            RestoreDC(di->hDC, -1);
        return TRUE;
    }
    if (m == WM_INITDIALOG) {
        HWND lb = GetDlgItem(h, 101);
        int i;
        if (tracing == 2)
            trace_add("I");
        for (i = 0; i < 12; i++) {
            char t[40];
            wsprintf(t, "C:\\W16\\SYSTEM\\QTFILE%02d.EXE", i);
            SendMessage(lb, LB_ADDSTRING, 0, (LPARAM)(LPSTR)t);
        }
        return TRUE;
    }
    (void)w;
    return FALSE;
}

static HWND dlg, lst;

static RECT area, lrect;


static void measure(const char *step)
{
    char nm[32];
    WORD sum, hl;
    BOOL od = (GetWindowLong(lst, GWL_STYLE) & (LBS_OWNERDRAWFIXED | LBS_OWNERDRAWVARIABLE)) != 0;
    UpdateWindow(dlg);
    pump(200);
    /* the focus rectangle's top line on the top item: the dotted line's
       pixels at client (2,0) and (3,0) */
    fp.x = 2;
    fp.y = 0;
    ClientToScreen(lst, &fp);
    grab(&area, &lrect, &sum, &hl);
    /* owner-drawn: 3.1 scrolls the bits (ScrollWindow) and draws the
       items that come in, and the selection and focus changes with
       ODA_SELECT and ODA_FOCUS; w16 draws every item shown again
       (ODA_DRAWENTIRE): the records, and so the pixels (this owner's
       focus rectangle is an XOR), are logged, not compared */
    if (od) {
        wsprintf(nm, "~d%d.%s.draw", dn, (LPSTR)step);
        obs_str(nm, drawrec);
    }
    drawrec[0] = 0;
    wsprintf(nm, "d%d.%s.focusline", dn, (LPSTR)step);
    if (od)
        obs_env(nm, fl);
    else
        obs(nm, fl);
    wsprintf(nm, "d%d.%s", dn, (LPSTR)step);
    obsf(nm, "top %d sel %d out %d", (int)SendMessage(lst, LB_GETTOPINDEX, 0, 0),
         (int)SendMessage(lst, LB_GETCURSEL, 0, 0), hl, 0);
    {
        int lo, hi2;
        GetScrollRange(lst, SB_VERT, &lo, &hi2);
        wsprintf(nm, "d%d.%s.bar", dn, (LPSTR)step);
        obsf(nm, "%d-%d at %d focus %d", lo, hi2, GetScrollPos(lst, SB_VERT), GetFocus() == lst);
    }
    wsprintf(nm, "d%d.%s.sum", dn, (LPSTR)step);
    if (od)
        obs_env(nm, sum);
    else
        obs(nm, sum);
    snap();
}

/* back to the start: the top item first, the partly shown one selected */
static void reset(int p)
{
    SendMessage(lst, LB_SETCURSEL, p, 0);
    SendMessage(lst, LB_SETTOPINDEX, 0, 0);
    pump(200);
    UpdateWindow(dlg);
    drawrec[0] = 0;
}

static void sec_dlg(int n)
{
    char nm[16];
    RECT c, sr;
    HDC hdc;
    int ih, p;
    dn = n;
    odvar = n == 6;
    wsprintf(nm, "US32D%d", n);
    trace[0] = 0;
    /* d2, d7: the whole life up to the first look, messages before they are
       handled; the others: as they are made, the height after each */
    tracing = n == 2 || n == 7 ? 2 : 1;
    dlg = CreateDialog(hi, nm, hMain, (DLGPROC)dlg_proc);
    if (tracing == 1) {
        tracing = 0;
        wsprintf(nm, "~d%d.trace", n);
        obs_str(nm, trace);
    } else
        trace_add("|");
    if (!dlg) {
        wsprintf(nm, "d%d", n);
        obs(nm, 0);
        return;
    }
    ShowWindow(dlg, SW_SHOWNORMAL);
    if (tracing == 2)
        trace_add("S");
    UpdateWindow(dlg);
    pump(300);
    lst = GetDlgItem(dlg, 101);
    wsprintf(nm, "d%d", n);
    hgt(nm, lst);
    wsprintf(nm, "d%d.static", n);
    hgt(nm, GetDlgItem(dlg, 102));
    GetClientRect(lst, &c);
    ih = (int)SendMessage(lst, LB_GETITEMHEIGHT, 0, 0);
    wsprintf(nm, "d%d.ih", n);
    obs(nm, ih);
    /* the list's DC: its clip box */
    hdc = GetDC(lst);
    {
        RECT b;
        GetClipBox(hdc, &b);
        wsprintf(nm, "d%d.clip", n);
        obsf(nm, "%d %d %d %d", b.left, b.top, b.right, b.bottom);
    }
    ReleaseDC(lst, hdc);
    GetWindowRect(lst, &lrect);
    GetWindowRect(GetDlgItem(dlg, 102), &sr);
    area.left = lrect.left - 4;
    area.right = lrect.right + 4;
    area.top = lrect.top;
    area.bottom = sr.bottom + 6;
    /* the item the bottom edge cuts (or, with whole items, the first not
       shown) */
    p = ih > 0 ? c.bottom / ih : 0;
    if (n == 6)
        p = 4;
    if (n == 11)
        area.bottom = lrect.bottom + 30;     /* 12+17+12+17 = 58; the fifth item (12) ends at 70 */
    measure("start");
    if (tracing == 2) {
        tracing = 0;
        wsprintf(nm, "~d%d.life", n);
        obs_str(nm, trace);
    }
    /* the focus rectangle on no selection, as things happen to the list */
    if (n == 1 || n == 3) {
        RECT wr;
        GetWindowRect(lst, &wr);
        SetWindowPos(lst, 0, 0, 0, wr.right - wr.left, wr.bottom - wr.top,
                     SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE);
        measure("same");
        InvalidateRect(lst, NULL, TRUE);
        measure("inval");
        SetFocus(GetDlgItem(dlg, 102) ? dlg : dlg);
        SetFocus(lst);
        measure("refocus");
        SendMessage(lst, LB_SETCURSEL, (WPARAM)-1, 0);
        measure("nosel");
    }
    SendMessage(lst, LB_SETCURSEL, p, 0);
    measure("setcursel");
    reset(p);
    measure("pre");
    /* the scroll bar's down arrow */
    click_at(lrect.right - 1 - GetSystemMetrics(SM_CXVSCROLL) / 2,
             lrect.bottom - 1 - GetSystemMetrics(SM_CYVSCROLL) / 2);
    measure("arrow");
    reset(p);
    SetFocus(lst);
    pump(100);
    key(VK_DOWN, 0x50);
    measure("down");
    reset(p);
    key(VK_UP, 0x48);
    measure("up");
    reset(p);
    SendMessage(lst, LB_SETTOPINDEX, 1, 0);
    measure("settop1");
    reset(p);
    SendMessage(lst, WM_VSCROLL, SB_LINEDOWN, 0);
    measure("vscroll");
    reset(p);
    /* a click on the partly shown item */
    click_at(lrect.left + 20, lrect.bottom - 3);
    measure("clickpart");
    reset(p);
    SendMessage(lst, LB_SETCURSEL, 11, 0);
    measure("last");
    DestroyWindow(dlg);
    pump(200);
}

LRESULT FAR PASCAL __export __loadds main_proc(HWND h, UINT m, WPARAM w, LPARAM l)
{
    if (m == WM_MEASUREITEM) {
        MEASUREITEMSTRUCT FAR *mi = (MEASUREITEMSTRUCT FAR *)l;
        mi->itemHeight = odvar ? 12 + 5 * (mi->itemID & 1) : 20;
        return TRUE;
    }
    if (m == WM_DRAWITEM)
        return TRUE;
    return DefWindowProc(h, m, w, l);
}

int __cdecl tmain(void)
{
    WNDCLASS wc;
    int n;
    hi = (HINSTANCE)init_regs[5];
    WaitEvent(0);
    InitApp(hi);
    out("US32 start\r\n");
    wc.style = 0;
    wc.lpfnWndProc = (WNDPROC)main_proc;
    wc.cbClsExtra = wc.cbWndExtra = 0;
    wc.hInstance = hi;
    wc.hIcon = 0;
    wc.hCursor = LoadCursor(NULL, IDC_ARROW);
    wc.hbrBackground = (HBRUSH)(COLOR_WINDOW + 1);
    wc.lpszMenuName = NULL;
    wc.lpszClassName = "US32M";
    RegisterClass(&wc);
    hMain = CreateWindow("US32M", "US32", WS_OVERLAPPEDWINDOW, 300, 200, 300, 250, 0, 0, hi, NULL);
    ShowWindow(hMain, SW_SHOWNORMAL);
    UpdateWindow(hMain);
    pump(300);
    out("cls\r\n");
    sec_cls();
    out("ih\r\n");
    sec_ih();
    out("cb\r\n");
    sec_cb();
    out("sf\r\n");
    sec_sf();
    out("bar\r\n");
    sec_bar();
    for (n = 1; n <= 11; n++) {
        out("dlg\r\n");
        sec_dlg(n);
    }
    out("US32 end\r\n");
    DestroyWindow(hMain);
    return 0;
}
