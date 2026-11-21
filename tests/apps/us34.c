/*
 * us34.c - probe US34: a combo box's layout in every style and font: the
 * closed box's height, the edit or selection field, the button, the item
 * heights, the dropped list, and their pixels.
 *
 * Found with Control Panel's Desktop dialog: w16's drop-down combo boxes
 * there stood a few pixels taller than the edit fields beside them.
 *
 * Run as the shell on Windows 3.1 (tests/dropin.sh -r) and with w16's
 * USER (-f USER.EXE); the OBS lines of the two logs must be identical
 * (tests/boottest.sh case combo). Sections:
 *   w       combo boxes made with CreateWindow in a window (system font),
 *           of the three styles and three heights, then given each of
 *           eight fonts (none, ANSI_VAR_FONT, MS Sans Serif 8 and 14,
 *           SYSTEM_FIXED_FONT, Small Fonts 6, Arial 9 and 36 pixels; their
 *           metrics logged); for each the window and client height,
 *           the edit's or list's rectangle, CB_GETITEMHEIGHT for -1 and
 *           0, CB_GETDROPPEDCONTROLRECT, the dropped list's place and
 *           height, and checksums of the closed box's and the dropped
 *           list's pixels
 *   d1-d5   dialogs (us34.rc) in MS Sans Serif 8 (d1), the system font
 *           (d2), MS Sans Serif 10 (d3), with owner-drawn drop-down list
 *           boxes (d4: the WM_MEASUREITEMs they send are logged) and laid
 *           out as Control Panel's Desktop dialog (d5): each control's
 *           place and the same measures, closed, focused and dropped
 *   sih     CB_SETITEMHEIGHT for -1 and 0 on a drop-down list box in
 *           MS Sans Serif 8: the answer and the heights after it
 * Each dialog is also saved as C:\US34_nn.BMP, the screen, for looking at.
 */
#include <windows.h>

#define TK_REF "us34ref.h"
#include "tklog.h"

extern WORD FAR PASCAL WaitEvent(HTASK);
extern BOOL FAR PASCAL InitApp(HINSTANCE);
extern void __cdecl call_mouse_event(WORD status, WORD x, WORD y, WORD buttons);

static HINSTANCE hi;
static HWND hMain;
static char mrec[200];          /* WM_MEASUREITEMs */
static char drec[300];          /* box 401's WM_DRAWITEMs */
static int dsnap;               /* measure: the list dropped on the screen too */

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

static void rec_add(char *buf, int size, const char *t)
{
    int n = lstrlen(buf);
    if (n + lstrlen(t) + 2 < size) {
        if (n)
            buf[n++] = ' ';
        lstrcpy(buf + n, t);
    }
}

/* ---- the screen ---- */
static int shot;
static char bmpname[16] = "C:\\US34_00.BMP";
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
    bmpname[8] = (char)('0' + shot / 10);
    bmpname[9] = (char)('0' + shot % 10);
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

/* a checksum of the screen's pixels in a (screen coordinates) */
static WORD grab(RECT *a)
{
    int cx = a->right - a->left, cy = a->bottom - a->top, x, y;
    WORD stride = (WORD)(((cx * 4 + 31) / 32) * 4);
    HDC scr = GetDC(NULL), mem = CreateCompatibleDC(scr);
    HBITMAP bm = CreateCompatibleBitmap(scr, cx, cy), old;
    HGLOBAL hb = GlobalAlloc(GMEM_MOVEABLE, (DWORD)(WORD)(stride * cy));
    BYTE FAR *buf = (BYTE FAR *)GlobalLock(hb);
    WORD s = 0;
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
    for (y = 0; y < cy; y++) {
        BYTE FAR *row = buf + (WORD)(cy - 1 - y) * stride;
        for (x = 0; x < cx; x++) {
            int p = (x & 1) ? row[x >> 1] & 15 : row[x >> 1] >> 4;
            s = (WORD)(s * 31 + p + 1);
        }
    }
    GlobalUnlock(hb);
    GlobalFree(hb);
    DeleteObject(bm);
    DeleteDC(mem);
    ReleaseDC(NULL, scr);
    return s;
}

static void pointer_away(void)
{
    call_mouse_event(0x8001, 0xFFFF, 0xFFFF, 0);
    pump(50);
}

/* ---- one combo box's measures, NAME.* ---- */
static HWND find_list(HWND h)
{
    HWND c;
    if ((GetWindowLong(h, GWL_STYLE) & 3) == CBS_SIMPLE) {
        for (c = GetWindow(h, GW_CHILD); c; c = GetWindow(c, GW_HWNDNEXT)) {
            char cl[16];
            GetClassName(c, cl, sizeof cl);
            if (!lstrcmpi(cl, "ComboLBox") || !lstrcmpi(cl, "ListBox"))
                return c;
        }
        return 0;
    }
    return FindWindow("ComboLBox", NULL);
}

static HWND find_edit(HWND h)
{
    HWND c;
    for (c = GetWindow(h, GW_CHILD); c; c = GetWindow(c, GW_HWNDNEXT)) {
        char cl[16];
        GetClassName(c, cl, sizeof cl);
        if (!lstrcmpi(cl, "Edit"))
            return c;
    }
    return 0;
}

/* where the focus is: 1 the box, 2 its edit, 3 its list, 4 elsewhere, 0
   nowhere */
static WORD focus_of(HWND h)
{
    HWND f = GetFocus();
    char cl[16];
    if (!f)
        return 0;
    if (f == h)
        return 1;
    GetClassName(f, cl, sizeof cl);
    if (GetParent(f) == h && !lstrcmpi(cl, "Edit"))
        return 2;
    if (!lstrcmpi(cl, "ComboLBox") || GetParent(f) == h)
        return 3;
    return 4;
}

static void measure(const char *name, HWND h, BOOL drop)
{
    char nm[40];
    RECT w, c, r, a;
    POINT p;
    HWND e = find_edit(h), l;
    UINT k = (UINT)(GetWindowLong(h, GWL_STYLE) & 3);
    GetWindowRect(h, &w);
    GetClientRect(h, &c);
    p.x = w.left;
    p.y = w.top;
    ScreenToClient(GetParent(h), &p);
    obsf(name, "%d,%d w%d h%d", p.x, p.y, w.right - w.left, w.bottom - w.top);
    wsprintf(nm, "%s.client", (LPSTR)name);
    obsf(nm, "%d x %d", c.right, c.bottom, 0, 0);
    wsprintf(nm, "%s.ih", (LPSTR)name);
    obsf(nm, "-1: %d 0: %d", (int)SendMessage(h, CB_GETITEMHEIGHT, (WPARAM)-1, 0),
         (int)SendMessage(h, CB_GETITEMHEIGHT, 0, 0), 0, 0);
    if (e) {
        GetWindowRect(e, &r);
        GetClientRect(e, &a);
        wsprintf(nm, "%s.edit", (LPSTR)name);
        obsf(nm, "%d,%d w%d h%d", r.left - w.left, r.top - w.top, r.right - r.left, r.bottom - r.top);
        wsprintf(nm, "%s.editc", (LPSTR)name);
        obsf(nm, "%d x %d border %d", a.right, a.bottom,
             (GetWindowLong(e, GWL_STYLE) & WS_BORDER) != 0, 0);
        SendMessage(e, EM_GETRECT, 0, (LPARAM)(LPRECT)&a);
        wsprintf(nm, "%s.erect", (LPSTR)name);
        obsf(nm, "%d %d %d %d", a.left, a.top, a.right, a.bottom);
    }
    SetRectEmpty(&r);
    SendMessage(h, CB_GETDROPPEDCONTROLRECT, 0, (LPARAM)(LPRECT)&r);
    wsprintf(nm, "%s.dcr", (LPSTR)name);
    obsf(nm, "%d,%d w%d h%d", r.left - w.left, r.top - w.top, r.right - r.left, r.bottom - r.top);
    /* the closed box's pixels, 3 around */
    pointer_away();
    UpdateWindow(GetParent(h));
    pump(100);
    a = w;
    InflateRect(&a, 3, 3);
    wsprintf(nm, "%s.sum", (LPSTR)name);
    obs(nm, grab(&a));
    if (dsnap == 2)
        snap();
    if (e) {
        /* the edit alone painted again: whether the frame is its own */
        InvalidateRect(e, NULL, TRUE);
        UpdateWindow(e);
        pump(50);
        wsprintf(nm, "%s.esum", (LPSTR)name);
        obs(nm, grab(&a));
        InvalidateRect(h, NULL, TRUE);
        UpdateWindow(h);
        pump(50);
        wsprintf(nm, "%s.csum", (LPSTR)name);
        obs(nm, grab(&a));
    }
    if (k != CBS_SIMPLE && drop) {
        SendMessage(h, CB_SHOWDROPDOWN, TRUE, 0);
        pump(150);
        wsprintf(nm, "%s.dfocus", (LPSTR)name);
        obs(nm, focus_of(h));
    }
    l = find_list(h);
    wsprintf(nm, "%s.list", (LPSTR)name);
    if (l && IsWindowVisible(l)) {
        GetWindowRect(l, &r);
        GetClientRect(l, &a);
        obsf(nm, "%d,%d w%d h%d", r.left - w.left, r.top - w.top, r.right - r.left, r.bottom - r.top);
        wsprintf(nm, "%s.listc", (LPSTR)name);
        obsf(nm, "%d x %d ih %d top %d", a.right, a.bottom, (int)SendMessage(l, LB_GETITEMHEIGHT, 0, 0),
             (int)SendMessage(l, LB_GETTOPINDEX, 0, 0));
        UpdateWindow(l);
        pump(50);
        UnionRect(&a, &r, &w);
        InflateRect(&a, 2, 2);
        wsprintf(nm, "%s.lsum", (LPSTR)name);
        obs(nm, grab(&a));
        if (dsnap == 1) {
            snap();
        }
    } else
        obs(nm, 0);
    if (k != CBS_SIMPLE && drop) {
        SendMessage(h, CB_SHOWDROPDOWN, FALSE, 0);
        pump(100);
        wsprintf(nm, "%s.ufocus", (LPSTR)name);
        obs(nm, focus_of(h));
    }
}

/* ---- w: in a window ---- */
static HFONT f8, f14, fsm, far1, far2;
#define NF 8

static void sec_w(void)
{
    static const DWORD kinds[] = { CBS_DROPDOWN, CBS_DROPDOWNLIST, CBS_SIMPLE };
    static const int hs[] = { 18, 60, 120 };
    HFONT fonts[NF];
    int k, j, f, i;
    char nm[32];
    fonts[0] = 0;
    fonts[1] = GetStockObject(ANSI_VAR_FONT);
    fonts[2] = f8;
    fonts[3] = GetStockObject(SYSTEM_FIXED_FONT);
    fonts[4] = f14;
    fonts[5] = fsm;
    fonts[6] = far1;
    fonts[7] = far2;
    /* the fonts' metrics */
    {
        HDC dc = GetDC(hMain);
        for (f = 0; f < NF; f++) {
            TEXTMETRIC tm;
            HFONT o = SelectObject(dc, fonts[f] ? fonts[f] : GetStockObject(SYSTEM_FONT));
            GetTextMetrics(dc, &tm);
            SelectObject(dc, o);
            wsprintf(nm, "font.%d", f);
            obsf(nm, "h%d a%d il%d el%d", tm.tmHeight, tm.tmAscent, tm.tmInternalLeading,
                 tm.tmExternalLeading);
            wsprintf(nm, "font.%d.w", f);
            obsf(nm, "ave %d max %d", tm.tmAveCharWidth, tm.tmMaxCharWidth, 0, 0);
        }
        ReleaseDC(hMain, dc);
    }
    for (k = 0; k < 3; k++)
        for (j = 0; j < 3; j++)
            for (f = 0; f < NF; f++) {
                HWND h = CreateWindow("COMBOBOX", NULL, WS_CHILD | WS_VISIBLE | WS_VSCROLL | kinds[k],
                                      10, 10, 120, hs[j], hMain, (HMENU)100, hi, NULL);
                if (f)
                    SendMessage(h, WM_SETFONT, (WPARAM)fonts[f], TRUE);
                for (i = 0; i < 5; i++) {
                    char t[16];
                    wsprintf(t, "Item %d", i);
                    SendMessage(h, CB_ADDSTRING, 0, (LPARAM)(LPSTR)t);
                }
                SendMessage(h, CB_SETCURSEL, 1, 0);
                wsprintf(nm, "w.%d.%d.%d", k, hs[j], f);
                /* some dropped on the screen too */
                dsnap = k == 0 && j == 1 && (f == 0 || f == 3 || f == 4 || f == 6);
                if (k == 0 && j == 1 && f == 7)
                    dsnap = 2;     /* before it drops */
                measure(nm, h, TRUE);
                dsnap = 0;
                /* some on the screen, for looking at (C:\US34_01-12) */
                if (j == 1 && (f == 0 || f == 4 || f == 5 || f == 7)) {
                    pointer_away();
                    snap();
                }
                /* a font given after the first one: the height again */
                if (f == 2) {
                    SendMessage(h, WM_SETFONT, (WPARAM)f14, TRUE);
                    pump(50);
                    wsprintf(nm, "w.%d.%d.2to4", k, hs[j]);
                    measure(nm, h, FALSE);
                    if (k == 2 && j == 1) {
                        pointer_away();
                        snap();
                    }
                }
                DestroyWindow(h);
                pump(50);
            }
}

/* ---- the dialogs ---- */
static int dn;

BOOL FAR PASCAL __export __loadds dlg_proc(HWND h, UINT m, WPARAM w, LPARAM l)
{
    if (m == WM_MEASUREITEM) {
        MEASUREITEMSTRUCT FAR *mi = (MEASUREITEMSTRUCT FAR *)l;
        char t[24];
        /* (a fixed-height list's item number is whatever 3.1 had there) */
        if (mi->itemID == (UINT)-1 || mi->CtlID == 403)
            wsprintf(t, "%d:%d:%d:%d", mi->CtlID, mi->CtlType, (int)mi->itemID, mi->itemHeight);
        else
            wsprintf(t, "%d:%d:x:%d", mi->CtlID, mi->CtlType, mi->itemHeight);
        rec_add(mrec, sizeof mrec, t);
        if (mi->CtlID == 402)
            mi->itemHeight = mi->itemID == (UINT)-1 ? 10 : 18;
        else if (mi->CtlID == 403)
            mi->itemHeight = 20;
        return TRUE;
    }
    if (m == WM_DRAWITEM) {
        DRAWITEMSTRUCT FAR *di = (DRAWITEMSTRUCT FAR *)l;
        RECT r = di->rcItem;
        char t[40];
        /* the box's own (its selection field's) WM_DRAWITEMs, logged
           as ~d4.draw */
        if (di->CtlID == 401 && di->hwndItem == GetDlgItem(h, 401)) {
            wsprintf(t, "%d:%d:%x:%x@%d,%d,%d,%d", di->CtlType, (int)di->itemID, di->itemAction,
                     di->itemState, r.left, r.top, r.right, r.bottom);
            rec_add(drec, sizeof drec, t);
        }
        if (di->itemAction == ODA_FOCUS) {
            DrawFocusRect(di->hDC, &r);
            return TRUE;
        }
        FillRect(di->hDC, &r, (HBRUSH)((di->itemState & ODS_SELECTED) ? COLOR_HIGHLIGHT + 1 : COLOR_WINDOW + 1));
        if (di->itemID != (UINT)-1) {
            SendMessage(di->hwndItem, di->CtlType == ODT_COMBOBOX ? CB_GETLBTEXT : LB_GETTEXT,
                        di->itemID, (LPARAM)(LPSTR)t);
            SetBkMode(di->hDC, TRANSPARENT);
            SetTextColor(di->hDC, GetSysColor((di->itemState & ODS_SELECTED) ? COLOR_HIGHLIGHTTEXT : COLOR_WINDOWTEXT));
            TextOut(di->hDC, r.left + 2, r.top, t, lstrlen(t));
        }
        if (di->itemState & ODS_FOCUS)
            DrawFocusRect(di->hDC, &r);
        return TRUE;
    }
    if (m == WM_INITDIALOG) {
        HWND c;
        for (c = GetWindow(h, GW_CHILD); c; c = GetWindow(c, GW_HWNDNEXT)) {
            char cl[16];
            GetClassName(c, cl, sizeof cl);
            if (!lstrcmpi(cl, "ComboBox")) {
                static const char *it[] = { "(None)", "Boxes", "Paisley", "Weave", "Scottie", "Tulip" };
                int i;
                for (i = 0; i < 6; i++)
                    SendMessage(c, CB_ADDSTRING, 0, (LPARAM)(LPSTR)it[i]);
                SendMessage(c, CB_SETCURSEL, 0, 0);
            } else if (!lstrcmpi(cl, "Edit"))
                SetWindowText(c, "75");
        }
        return TRUE;
    }
    (void)w;
    return FALSE;
}

static void sec_dlg(int n)
{
    char nm[24];
    HWND dlg, c;
    mrec[0] = 0;
    dn = n;
    wsprintf(nm, "US34D%d", n);
    dlg = CreateDialog(hi, nm, hMain, (DLGPROC)dlg_proc);
    if (!dlg) {
        wsprintf(nm, "d%d", n);
        obs(nm, 0);
        return;
    }
    ShowWindow(dlg, SW_SHOWNORMAL);
    UpdateWindow(dlg);
    pump(300);
    if (n == 4 || n == 5) {
        wsprintf(nm, "d%d.measure", n);
        obs_str(nm, mrec);
    }
    if (n == 4) {
        obs_str("~d4.draw", drec);
        drec[0] = 0;
    }
    pointer_away();
    snap();
    for (c = GetWindow(dlg, GW_CHILD); c; c = GetWindow(c, GW_HWNDNEXT)) {
        char cl[16];
        int id = GetDlgCtrlID(c);
        GetClassName(c, cl, sizeof cl);
        if (!lstrcmpi(cl, "ComboBox")) {
            wsprintf(nm, "d%d.%d", n, id);
            measure(nm, c, TRUE);
            /* focused, then dropped from the keyboard */
            SetFocus(c);
            pump(100);
            wsprintf(nm, "d%d.%d.focus", n, id);
            measure(nm, c, FALSE);
            /* an edit box's, focused, on the screen */
            if (n <= 3 && (id % 100 == 2 || id % 100 == 3)) {
                pointer_away();
                snap();
            }
        } else if (!lstrcmpi(cl, "Edit")) {
            RECT w;
            POINT p;
            GetWindowRect(c, &w);
            p.x = w.left;
            p.y = w.top;
            ScreenToClient(dlg, &p);
            wsprintf(nm, "d%d.%d", n, id);
            obsf(nm, "edit %d,%d w%d h%d", p.x, p.y, w.right - w.left, w.bottom - w.top);
            SendMessage(c, EM_GETRECT, 0, (LPARAM)(LPRECT)&w);
            wsprintf(nm, "d%d.%d.erect", n, id);
            obsf(nm, "%d %d %d %d", w.left, w.top, w.right, w.bottom);
            GetClientRect(c, &w);
            wsprintf(nm, "d%d.%d.client", n, id);
            obsf(nm, "%d x %d", w.right, w.bottom, 0, 0);
        }
    }
    if (n == 4) {
        wsprintf(nm, "d%d.measure2", n);
        obs_str(nm, mrec);
    }
    /* the whole dialog with the first combo box dropped */
    c = GetDlgItem(dlg, n == 5 ? 501 : n * 100 + 1);
    if (c) {
        SetFocus(c);
        SendMessage(c, CB_SHOWDROPDOWN, TRUE, 0);
        pump(200);
        pointer_away();
        snap();
        SendMessage(c, CB_SHOWDROPDOWN, FALSE, 0);
        pump(100);
    }
    DestroyWindow(dlg);
    pump(200);
}

/* ---- CB_SETITEMHEIGHT ---- */
static void sec_sih(void)
{
    HWND dlg = CreateDialog(hi, "US34D1", hMain, (DLGPROC)dlg_proc), c;
    int i;
    char nm[24];
    static const WORD ids[] = { 101, 102, 103 };
    if (!dlg) {
        obs("sih", 0);
        return;
    }
    ShowWindow(dlg, SW_SHOWNORMAL);
    pump(200);
    for (i = 0; i < 3; i++) {
        c = GetDlgItem(dlg, ids[i]);
        wsprintf(nm, "sih.%d.m1", ids[i]);
        obs(nm, (WORD)SendMessage(c, CB_SETITEMHEIGHT, (WPARAM)-1, 20));
        wsprintf(nm, "sih.%d.m1h", ids[i]);
        measure(nm, c, TRUE);
        wsprintf(nm, "sih.%d.i0", ids[i]);
        obs(nm, (WORD)SendMessage(c, CB_SETITEMHEIGHT, 0, 17));
        wsprintf(nm, "sih.%d.i0h", ids[i]);
        measure(nm, c, TRUE);
    }
    pointer_away();
    snap();
    DestroyWindow(dlg);
    pump(200);
}

LRESULT FAR PASCAL __export __loadds main_proc(HWND h, UINT m, WPARAM w, LPARAM l)
{
    return DefWindowProc(h, m, w, l);
}

int __cdecl tmain(void)
{
    WNDCLASS wc;
    int n;
    HDC dc;
    hi = (HINSTANCE)init_regs[5];
    WaitEvent(0);
    InitApp(hi);
    out("US34 start\r\n");
    wc.style = 0;
    wc.lpfnWndProc = (WNDPROC)main_proc;
    wc.cbClsExtra = wc.cbWndExtra = 0;
    wc.hInstance = hi;
    wc.hIcon = 0;
    wc.hCursor = LoadCursor(NULL, IDC_ARROW);
    wc.hbrBackground = (HBRUSH)(COLOR_WINDOW + 1);
    wc.lpszMenuName = NULL;
    wc.lpszClassName = "US34M";
    RegisterClass(&wc);
    hMain = CreateWindow("US34M", "US34", WS_OVERLAPPEDWINDOW, 300, 150, 300, 300, 0, 0, hi, NULL);
    ShowWindow(hMain, SW_SHOWNORMAL);
    UpdateWindow(hMain);
    pump(300);
    dc = GetDC(NULL);
    f8 = CreateFont(-MulDiv(8, GetDeviceCaps(dc, LOGPIXELSY), 72), 0, 0, 0, FW_BOLD, 0, 0, 0,
                    ANSI_CHARSET, 0, 0, 0, VARIABLE_PITCH | FF_SWISS, "MS Sans Serif");
    f14 = CreateFont(-MulDiv(14, GetDeviceCaps(dc, LOGPIXELSY), 72), 0, 0, 0, FW_NORMAL, 0, 0, 0,
                     ANSI_CHARSET, 0, 0, 0, VARIABLE_PITCH | FF_SWISS, "MS Sans Serif");
    fsm = CreateFont(-6, 0, 0, 0, FW_NORMAL, 0, 0, 0, ANSI_CHARSET, 0, 0, 0,
                     VARIABLE_PITCH | FF_SWISS, "Small Fonts");
    far1 = CreateFont(-9, 0, 0, 0, FW_NORMAL, 0, 0, 0, ANSI_CHARSET, 0, 0, 0,
                      VARIABLE_PITCH | FF_SWISS, "Arial");
    far2 = CreateFont(-36, 0, 0, 0, FW_NORMAL, 0, 0, 0, ANSI_CHARSET, 0, 0, 0,
                      VARIABLE_PITCH | FF_SWISS, "Arial");
    ReleaseDC(NULL, dc);
    out("w\r\n");
    sec_w();
    for (n = 1; n <= 5; n++) {
        out("dlg\r\n");
        sec_dlg(n);
    }
    out("sih\r\n");
    sec_sih();
    out("US34 end\r\n");
    DeleteObject(f8);
    DeleteObject(f14);
    DeleteObject(fsm);
    DeleteObject(far1);
    DeleteObject(far2);
    DestroyWindow(hMain);
    return 0;
}
