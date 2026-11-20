/*
 * us33.c - probe US33: how a list box draws (owner-drawn ones above all),
 * what GetDC(NULL) covers, LB_SETTABSTOPS, and the frames of child
 * dialogs. Found with QuickTime's Control Panel (tests/qtcpl.sh) and
 * probe US32; run as the shell on Windows 3.1 (tests/dropin.sh -r) and
 * with w16's USER (-f USER.EXE): the OBS lines of the two logs must be
 * identical (tests/boottest.sh case lbdraw). Sections:
 *   od1-od10 dialogs (us33.rc) of a list box of 20 items, owner-drawn
 *           (fixed and variable heights, with and without LBS_HASSTRINGS,
 *           single, multiple and extended selection, multicolumn) and not
 *           (single and extended selection: their pixels alone), driven
 *           step by step (focus, LB_SETCURSEL, every key, clicks on items,
 *           on the scroll bar's arrows and shaft, the thumb sent and
 *           dragged, LB_SETTOPINDEX, Shift and Ctrl with the mouse and
 *           the keys, LB_SETSEL, items added and deleted, the focus
 *           taken away); for each step every WM_DRAWITEM in order
 *           (item:action/state@top,bottom, "p" when drawn inside the list
 *           box's WM_PAINT, whose update rectangle is logged as
 *           P(top,bottom)), the clip box and colours of each item's DC
 *           (~), the draws among the list box's own messages (~trace),
 *           the top item, the selection and the caret, and a checksum of
 *           the list box's pixels (the owner's focus rectangle is an XOR,
 *           so a focus drawn twice or not at all shows)
 *   dfr     DrawFocusRect's dots by the DC's colours, and in child windows
 *           an odd and an even number of pixels across
 *   tto     TabbedTextOut and GetTabbedTextExtent in four fonts: the
 *           default tab, one stop, two, a negative one
 *   gdc     GetDC(NULL), GetDC and GetWindowDC of the desktop window:
 *           the clip box, GetPixel and PtVisible in a top-level window
 *           and in its child, and whether PatBlt through it paints over
 *           them; the desktop window repainted under top-level windows
 *   tab     LBS_USETABSTOPS list boxes in a dialog (MS Sans Serif 8) and
 *           in a window (system font): LB_SETTABSTOPS's answer and where
 *           each item's words start (default stops, none, one, several,
 *           text past a stop and past the right edge), LB_GETTEXT; a list
 *           box without the style; one given ANSI_VAR_FONT first
 *   dlgf    CreateDialogIndirect of child and popup templates of several
 *           frame styles, with and without DS_SETFONT: the window and
 *           client sizes (a top-level one with a font and WS_DLGFRAME
 *           logged as ~: user/dlg.c keeps it bigger for now)
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
static int dn;                  /* the od dialog (1-8) */
static int odvar;               /* WM_MEASUREITEM: variable heights */
static char drawrec[1200];      /* WM_DRAWITEMs since the last step */
static char cliprec[3000];       /* their DCs' clip boxes */
static char trace[2400];        /* the draws and the list box's messages */
static int tracing;
static int inpaint;             /* inside the list box's WM_PAINT */
static WNDPROC oldlb;
static int lbw;                 /* the list box's client width */

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
    char t[80];
    wsprintf(t, fmt, a, b, c, d);
    obs_str(name, t);
}

static void rec(char *buf, int size, const char *t)
{
    int n = lstrlen(buf);
    if (n + lstrlen(t) + 2 < size) {
        if (n)
            buf[n++] = ' ';
        lstrcpy(buf + n, t);
    } else if (n + 2 < size && buf[n - 1] != '+')
        lstrcpy(buf + n, "+");
}

/* ---- the screen ---- */
typedef struct { BITMAPINFOHEADER h; RGBQUAD pal[16]; } BMI16;
static BMI16 bmi;
static HGLOBAL hgrab;
static BYTE FAR *gbuf;
static WORD gstride;
static int gcx, gcy;

/* the screen's area a into gbuf (4 bits a pixel, top row first) */
static void grab(RECT *a)
{
    HDC scr = GetDC(NULL), mem = CreateCompatibleDC(scr);
    HBITMAP bm, old;
    gcx = a->right - a->left;
    gcy = a->bottom - a->top;
    gstride = (WORD)(((gcx * 4 + 31) / 32) * 4);
    bm = CreateCompatibleBitmap(scr, gcx, gcy);
    if (hgrab)
        GlobalFree(hgrab);
    hgrab = GlobalAlloc(GMEM_MOVEABLE, (DWORD)(WORD)(gstride * gcy));
    gbuf = (BYTE FAR *)GlobalLock(hgrab);
    old = SelectObject(mem, bm);
    BitBlt(mem, 0, 0, gcx, gcy, scr, a->left, a->top, SRCCOPY);
    SelectObject(mem, old);
    bmi.h.biSize = sizeof bmi.h;
    bmi.h.biWidth = gcx;
    bmi.h.biHeight = gcy;
    bmi.h.biPlanes = 1;
    bmi.h.biBitCount = 4;
    bmi.h.biCompression = 0;
    bmi.h.biSizeImage = 0;
    GetDIBits(scr, bm, 0, gcy, gbuf, (BITMAPINFO FAR *)&bmi, DIB_RGB_COLORS);
    DeleteObject(bm);
    DeleteDC(mem);
    ReleaseDC(NULL, scr);
}

static int px(int x, int y)
{
    BYTE FAR *row = gbuf + (WORD)(gcy - 1 - y) * gstride;
    return (x & 1) ? row[x >> 1] & 15 : row[x >> 1] >> 4;
}

static COLORREF pcolor(int p)
{
    return RGB(bmi.pal[p].rgbRed, bmi.pal[p].rgbGreen, bmi.pal[p].rgbBlue);
}

static WORD checksum(RECT *a)
{
    int x, y;
    WORD s = 0;
    grab(a);
    for (y = 0; y < gcy; y++)
        for (x = 0; x < gcx; x++)
            s = (WORD)(s * 31 + px(x, y) + 1);
    return s;
}

/* how many pixels of colour c in the screen's area a */
static int count_color(RECT *a, COLORREF c)
{
    int x, y, n = 0;
    grab(a);
    for (y = 0; y < gcy; y++)
        for (x = 0; x < gcx; x++)
            if (pcolor(px(x, y)) == c)
                n++;
    return n;
}

/* ---- input ---- */
static WORD absx(int x) { return (WORD)MulDiv(x, 32767, GetSystemMetrics(SM_CXSCREEN)) * 2 + 1; }
static WORD absy(int y) { return (WORD)MulDiv(y, 32767, GetSystemMetrics(SM_CYSCREEN)) * 2 + 1; }

static void mouse_away(void)
{
    call_mouse_event(0x8001, 0xFFFF, 0xFFFF, 0);
    pump(250);
}

static void click_at(int x, int y)
{
    call_mouse_event(0x8001, absx(x), absy(y), 0);
    pump(100);
    call_mouse_event(0x8002, absx(x), absy(y), 0);
    call_mouse_event(0x8004, absx(x), absy(y), 0);
    pump(300);
    mouse_away();
}

/* pressed at (x, y), moved to (x2, y2), let go: all queued together (the
   scroll bar's tracking loop reads them) */
static void drag(int x, int y, int x2, int y2)
{
    call_mouse_event(0x8001, absx(x), absy(y), 0);
    pump(100);
    call_mouse_event(0x8002, absx(x), absy(y), 0);
    call_mouse_event(0x8001, absx(x2), absy(y2), 0);
    call_mouse_event(0x8004, absx(x2), absy(y2), 0);
    pump(400);
    mouse_away();
}

static void key(WORD vk, WORD scan)
{
    call_keybd_event(vk, scan);
    call_keybd_event(0x8000 | vk, scan);
    pump(250);
}

static void mod_down(WORD vk, WORD scan) { call_keybd_event(vk, scan); }
static void mod_up(WORD vk, WORD scan) { call_keybd_event(0x8000 | vk, scan); }

/* ---- the owner-drawn dialogs ---- */
static HWND dlg, lst;

/* the list box keeps strings */
static BOOL strs(HWND lb)
{
    DWORD s = GetWindowLong(lb, GWL_STYLE);
    return !(s & (LBS_OWNERDRAWFIXED | LBS_OWNERDRAWVARIABLE)) || (s & LBS_HASSTRINGS);
}

/* the list box's messages in the trace: what each draw came from */
static void tr_msg(UINT m, WPARAM w)
{
    char t[12];
    t[0] = 0;
    switch (m) {
    case WM_SETFOCUS: lstrcpy(t, "F"); break;
    case WM_KILLFOCUS: lstrcpy(t, "K"); break;
    case WM_SHOWWINDOW: lstrcpy(t, "S"); break;
    case WM_SIZE: lstrcpy(t, "Z"); break;
    case WM_ERASEBKGND: lstrcpy(t, "E"); break;
    case WM_VSCROLL: wsprintf(t, "V%d", w); break;
    case WM_HSCROLL: wsprintf(t, "H%d", w); break;
    case WM_KEYDOWN: wsprintf(t, "D%x", w); break;
    case WM_CHAR: wsprintf(t, "C%x", w); break;
    case WM_LBUTTONDOWN: lstrcpy(t, "B"); break;
    case WM_LBUTTONUP: lstrcpy(t, "U"); break;
    case WM_TIMER: lstrcpy(t, "T"); break;
    default:
        if (m >= WM_USER && m < WM_USER + 0x40 && m != LB_GETTEXT)
            wsprintf(t, "L%x", m - WM_USER);
    }
    if (t[0])
        rec(trace, sizeof trace, t);
}

LRESULT FAR PASCAL __export __loadds lb_sub(HWND h, UINT m, WPARAM w, LPARAM l)
{
    LRESULT r;
    if (tracing && m != WM_PAINT) {
        tr_msg(m, w);
        r = CallWindowProc((FARPROC)oldlb, h, m, w, l);
        if (m == WM_KEYDOWN || m == WM_LBUTTONDOWN || m == WM_VSCROLL || m == WM_SETFOCUS ||
            m == WM_KILLFOCUS || (m >= WM_USER && m < WM_USER + 0x40 && m != LB_GETTEXT))
            rec(trace, sizeof trace, ">");
        return r;
    }
    if (m == WM_PAINT) {
        RECT u;
        char t[32];
        GetUpdateRect(h, &u, FALSE);
        if (u.left || u.right != lbw)
            wsprintf(t, "P(%d,%d,%d-%d)", u.top, u.bottom, u.left, u.right);
        else
            wsprintf(t, "P(%d,%d)", u.top, u.bottom);
        rec(drawrec, sizeof drawrec, t);
        if (tracing)
            rec(trace, sizeof trace, t);
        inpaint++;
        r = CallWindowProc((FARPROC)oldlb, h, m, w, l);
        inpaint--;
        return r;
    }
    return CallWindowProc((FARPROC)oldlb, h, m, w, l);
}

BOOL FAR PASCAL __export __loadds od_proc(HWND h, UINT m, WPARAM w, LPARAM l)
{
    if (m == WM_MEASUREITEM) {
        MEASUREITEMSTRUCT FAR *mi = (MEASUREITEMSTRUCT FAR *)l;
        mi->itemHeight = odvar ? 12 + 5 * (mi->itemID & 1) : 15;
        return TRUE;
    }
    if (m == WM_DRAWITEM) {
        DRAWITEMSTRUCT FAR *di = (DRAWITEMSTRUCT FAR *)l;
        char t[96];
        RECT r = di->rcItem, cb;
        if (r.left || r.right != lbw)
            wsprintf(t, "%s%d:%x/%x@%d,%d,%d-%d", (LPSTR)(inpaint ? "p" : ""), (int)di->itemID,
                     di->itemAction, di->itemState, r.top, r.bottom, r.left, r.right);
        else
            wsprintf(t, "%s%d:%x/%x@%d,%d", (LPSTR)(inpaint ? "p" : ""), (int)di->itemID,
                     di->itemAction, di->itemState, r.top, r.bottom);
        rec(drawrec, sizeof drawrec, t);
        if (tracing)
            rec(trace, sizeof trace, t);
        GetClipBox(di->hDC, &cb);
        wsprintf(t, "%d,%d,%d,%d", cb.left, cb.top, cb.right, cb.bottom);
        wsprintf(t + lstrlen(t), "/%lx/%lx/%d", GetTextColor(di->hDC), GetBkColor(di->hDC),
                 GetBkMode(di->hDC));
        {
            TEXTMETRIC tm;
            DWORD o = GetDCOrg(di->hDC), b = GetBrushOrg(di->hDC);
            GetTextMetrics(di->hDC, &tm);
            wsprintf(t + lstrlen(t), "/h%d,%d/o%d,%d/b%d,%d", tm.tmHeight, tm.tmAveCharWidth,
                     LOWORD(o), HIWORD(o), LOWORD(b), HIWORD(b));
        }
        rec(cliprec, sizeof cliprec, t);
        if (di->itemID == (UINT)-1) {
            if (di->itemAction & ODA_FOCUS)
                DrawFocusRect(di->hDC, &r);
            return TRUE;
        }
        if (di->itemAction == ODA_FOCUS) {
            DrawFocusRect(di->hDC, &r);
            return TRUE;
        }
        FillRect(di->hDC, &r, (HBRUSH)((di->itemState & ODS_SELECTED) ? COLOR_HIGHLIGHT + 1 : COLOR_WINDOW + 1));
        if (GetWindowLong(di->hwndItem, GWL_STYLE) & LBS_HASSTRINGS)
            SendMessage(di->hwndItem, LB_GETTEXT, di->itemID, (LPARAM)(LPSTR)t);
        else
            wsprintf(t, "Data %ld", di->itemData);
        SetBkMode(di->hDC, TRANSPARENT);
        SetTextColor(di->hDC, GetSysColor((di->itemState & ODS_SELECTED) ? COLOR_HIGHLIGHTTEXT : COLOR_WINDOWTEXT));
        TextOut(di->hDC, r.left + 2, r.top, t, lstrlen(t));
        if (di->itemState & ODS_FOCUS)
            DrawFocusRect(di->hDC, &r);
        return TRUE;
    }
    if (m == WM_INITDIALOG) {
        HWND lb = GetDlgItem(h, 101);
        int i;
        RECT c;
        lst = lb;
        oldlb = (WNDPROC)GetWindowLong(lb, GWL_WNDPROC);
        SetWindowLong(lb, GWL_WNDPROC, (LONG)(FARPROC)lb_sub);
        GetClientRect(lb, &c);
        lbw = c.right;
        if (GetWindowLong(lb, GWL_STYLE) & LBS_MULTICOLUMN)
            SendMessage(lb, LB_SETCOLUMNWIDTH, 70, 0);
        for (i = 0; i < 20; i++) {
            char t[16];
            if (strs(lb)) {
                wsprintf(t, "Item %d", i);
                SendMessage(lb, LB_ADDSTRING, 0, (LPARAM)(LPSTR)t);
            } else
                SendMessage(lb, LB_ADDSTRING, 0, (LPARAM)(100 + i));
        }
        return TRUE;
    }
    (void)w;
    return FALSE;
}

static RECT larea;

static void measure(const char *step)
{
    char nm[32];
    BOOL ml = (GetWindowLong(lst, GWL_STYLE) & (LBS_MULTIPLESEL | LBS_EXTENDEDSEL)) != 0;
    rec(trace, sizeof trace, "|");
    UpdateWindow(dlg);
    pump(150);
    tracing = 0;
    wsprintf(nm, "~od%d.%s.trace", dn, (LPSTR)step);
    obs_str(nm, trace);
    trace[0] = 0;
    {
        /* the caret item's top two rows and its text's first row, as
           colour indices (where the checksums differ, why) */
        RECT ir, a;
        POINT o;
        char t[140];
        int x, k = 0, y;
        SendMessage(lst, LB_GETITEMRECT, (WPARAM)SendMessage(lst, LB_GETCARETINDEX, 0, 0),
                    (LPARAM)(RECT FAR *)&ir);
        o.x = ir.left;
        o.y = ir.top;
        ClientToScreen(lst, &o);
        if (ir.top >= 0 && ir.top < 80) {
            SetRect(&a, o.x, o.y, o.x + 40, o.y + 8);
            grab(&a);
            for (y = 0; y < 8; y += 1) {
                if (y > 1 && y != 6)
                    continue;
                for (x = 0; x < 40; x++)
                    t[k++] = "0123456789ABCDEF"[px(x, y)];
                t[k++] = ' ';
            }
            t[k] = 0;
            wsprintf(nm, "~od%d.%s.px", dn, (LPSTR)step);
            obs_str(nm, t);
        }
    }
    wsprintf(nm, "od%d.%s.draw", dn, (LPSTR)step);
    obs_str(nm, drawrec);
    wsprintf(nm, "~od%d.%s.clip", dn, (LPSTR)step);
    obs_str(nm, cliprec);
    drawrec[0] = cliprec[0] = 0;
    wsprintf(nm, "od%d.%s", dn, (LPSTR)step);
    {
        int sel = (int)SendMessage(lst, LB_GETCURSEL, 0, 0);
        if (ml) {
            /* the selected items as bits */
            int i, b = 0;
            for (i = 0; i < 16; i++)
                if (SendMessage(lst, LB_GETSEL, i, 0) > 0)
                    b |= 1 << i;
            sel = b;
        }
        obsf(nm, "top %d sel %x caret %d focus %d", (int)SendMessage(lst, LB_GETTOPINDEX, 0, 0), sel,
             (int)SendMessage(lst, LB_GETCARETINDEX, 0, 0), GetFocus() == lst);
    }
    {
        int lo, hi2;
        GetScrollRange(lst, SB_VERT, &lo, &hi2);
        wsprintf(nm, "od%d.%s.bar", dn, (LPSTR)step);
        obsf(nm, "%d-%d at %d", lo, hi2, GetScrollPos(lst, SB_VERT), 0);
        if (GetWindowLong(lst, GWL_STYLE) & LBS_MULTICOLUMN) {
            GetScrollRange(lst, SB_HORZ, &lo, &hi2);
            wsprintf(nm, "od%d.%s.hbar", dn, (LPSTR)step);
            obsf(nm, "%d-%d at %d vis %d", lo, hi2, GetScrollPos(lst, SB_HORZ),
                 (GetWindowLong(lst, GWL_STYLE) & WS_HSCROLL) != 0);
        }
    }
    wsprintf(nm, "od%d.%s.sum", dn, (LPSTR)step);
    obs(nm, checksum(&larea));
    tracing = 1;
}

static void sec_od(int n)
{
    char nm[16];
    RECT wr, cr;
    int sb, ih, ml, mc;
    POINT o;
    dn = n;
    odvar = n == 3 || n == 4 || n == 7;
    wsprintf(nm, "US33O%d", n);
    drawrec[0] = cliprec[0] = trace[0] = 0;
    tracing = 1;
    dlg = CreateDialog(hi, nm, hMain, (DLGPROC)od_proc);
    if (!dlg) {
        wsprintf(nm, "od%d", n);
        obs(nm, 0);
        return;
    }
    ShowWindow(dlg, SW_SHOWNORMAL);
    UpdateWindow(dlg);
    pump(300);
    GetWindowRect(lst, &wr);
    GetClientRect(lst, &cr);
    larea = wr;
    InflateRect(&larea, 3, 3);
    ml = (GetWindowLong(lst, GWL_STYLE) & (LBS_MULTIPLESEL | LBS_EXTENDEDSEL)) != 0;
    mc = (GetWindowLong(lst, GWL_STYLE) & LBS_MULTICOLUMN) != 0;
    ih = (int)SendMessage(lst, LB_GETITEMHEIGHT, 0, 0);
    wsprintf(nm, "od%d.size", n);
    obsf(nm, "%dx%d cl %dx%d", wr.right - wr.left, wr.bottom - wr.top, cr.right, cr.bottom);
    o.x = o.y = 0;
    ClientToScreen(lst, &o);
    sb = GetSystemMetrics(SM_CXVSCROLL);
    measure("start");
    SetFocus(GetDlgItem(dlg, 1));
    pump(100);
    measure("blur");
    SetFocus(lst);
    pump(100);
    measure("focus");
    if (ml)
        SendMessage(lst, LB_SETSEL, TRUE, 2);
    else
        SendMessage(lst, LB_SETCURSEL, 2, 0);
    measure("sel2");
    key(VK_DOWN, 0x50);
    measure("down");
    key(VK_UP, 0x48);
    measure("up");
    key(VK_NEXT, 0x51);
    measure("pgdn");
    key(VK_NEXT, 0x51);
    measure("pgdn2");
    key(VK_PRIOR, 0x49);
    measure("pgup");
    key(VK_END, 0x4F);
    measure("end");
    key(VK_HOME, 0x47);
    measure("home");
    if (mc) {
        key(VK_RIGHT, 0x4D);
        measure("right");
        key(VK_LEFT, 0x4B);
        measure("left");
    }
    if (!ml) {
        SendMessage(lst, LB_SETCURSEL, 12, 0);
        measure("sel12");
        SendMessage(lst, LB_SETCURSEL, (WPARAM)-1, 0);
        measure("selnone");
    }
    SendMessage(lst, LB_SETTOPINDEX, 5, 0);
    measure("top5");
    /* a click on the second row shown */
    click_at(o.x + 20, o.y + ih + ih / 2);
    measure("click");
    if (mc) {
        SendMessage(lst, WM_HSCROLL, SB_LINEDOWN, 0);
        measure("hline");
        SendMessage(lst, WM_HSCROLL, SB_LINEUP, 0);
        measure("hlineup");
    } else {
        /* the scroll bar: its down arrow, its up arrow, the shaft just
           above the down arrow and just below the up arrow, the thumb */
        click_at(wr.right - 1 - sb / 2, wr.bottom - 1 - GetSystemMetrics(SM_CYVSCROLL) / 2);
        measure("arrowdn");
        click_at(wr.right - 1 - sb / 2, wr.top + 1 + GetSystemMetrics(SM_CYVSCROLL) / 2);
        measure("arrowup");
        click_at(wr.right - 1 - sb / 2, wr.bottom - 2 - GetSystemMetrics(SM_CYVSCROLL));
        measure("pagedn");
        click_at(wr.right - 1 - sb / 2, wr.top + 1 + GetSystemMetrics(SM_CYVSCROLL));
        measure("pageup");
        SendMessage(lst, WM_VSCROLL, SB_THUMBPOSITION, MAKELONG(60, 0));
        measure("thumb60");
        SendMessage(lst, WM_VSCROLL, SB_LINEUP, 0);
        measure("lineup");
        SendMessage(lst, WM_VSCROLL, SB_TOP, 0);
        measure("sbtop");
        /* the thumb (at the top) dragged to the shaft's middle */
        drag(wr.right - 1 - sb / 2, wr.top + 1 + GetSystemMetrics(SM_CYVSCROLL) + 4,
             wr.right - 1 - sb / 2, (wr.top + wr.bottom) / 2 + 2);
        measure("drag");
    }
    if (ml) {
        key(VK_SPACE, 0x39);
        measure("space");
        mod_down(VK_SHIFT, 0x2A);
        key(VK_DOWN, 0x50);
        mod_up(VK_SHIFT, 0x2A);
        pump(100);
        measure("shiftdn");
        mod_down(VK_CONTROL, 0x1D);
        click_at(o.x + 20, o.y + ih / 2);
        mod_up(VK_CONTROL, 0x1D);
        pump(100);
        measure("ctrlclick");
        mod_down(VK_SHIFT, 0x2A);
        click_at(o.x + 20, o.y + 2 * ih + ih / 2);
        mod_up(VK_SHIFT, 0x2A);
        pump(100);
        measure("shiftclick");
        SendMessage(lst, LB_SETSEL, TRUE, (LPARAM)-1);
        measure("selall");
        SendMessage(lst, LB_SETSEL, FALSE, MAKELONG(SendMessage(lst, LB_GETTOPINDEX, 0, 0) + 1, 0));
        measure("unsel1");
        SendMessage(lst, LB_SETCARETINDEX, (WPARAM)SendMessage(lst, LB_GETTOPINDEX, 0, 0), 0);
        measure("caret");
    }
    InvalidateRect(lst, NULL, TRUE);
    measure("inval");
    {
        int t = (int)SendMessage(lst, LB_GETTOPINDEX, 0, 0);
        if (strs(lst))
            SendMessage(lst, LB_INSERTSTRING, t + 1, (LPARAM)(LPSTR)"New");
        else
            SendMessage(lst, LB_INSERTSTRING, t + 1, (LPARAM)999);
        measure("insert");
        SendMessage(lst, LB_DELETESTRING, t + 2, 0);
        measure("delete");
        if (strs(lst))
            SendMessage(lst, LB_ADDSTRING, 0, (LPARAM)(LPSTR)"Last");
        else
            SendMessage(lst, LB_ADDSTRING, 0, (LPARAM)998);
        measure("add");
    }
    SetFocus(GetDlgItem(dlg, 1));
    pump(100);
    measure("kill");
    SendMessage(lst, LB_RESETCONTENT, 0, 0);
    measure("reset");
    SetFocus(lst);
    pump(100);
    measure("emptyfocus");
    tracing = 0;
    DestroyWindow(dlg);
    pump(200);
}

/* ---- GetDC(NULL) ---- */
static void dc_probe(const char *name, HDC hdc, HWND kid)
{
    RECT b, w, k;
    char nm[32];
    int type;
    POINT p, q;
    COLORREF c;
    GetWindowRect(hMain, &w);
    GetWindowRect(kid, &k);
    type = GetClipBox(hdc, &b);
    wsprintf(nm, "gdc.%s.clip", (LPSTR)name);
    obsf(nm, "%d %d %d %d", b.left, b.top, b.right, b.bottom);
    wsprintf(nm, "gdc.%s.type", (LPSTR)name);
    obs(nm, type);
    {
        POINT org;
        DWORD d = GetDCOrg(hdc);
        org.x = LOWORD(d);
        org.y = HIWORD(d);
        wsprintf(nm, "gdc.%s.org", (LPSTR)name);
        obsf(nm, "%d %d", org.x, org.y, 0, 0);
    }
    /* a point in the main window's client area, clear of the child */
    p.x = w.right - 30;
    p.y = w.bottom - 20;
    q.x = (k.left + k.right) / 2;
    q.y = (k.top + k.bottom) / 2;
    /* (PtVisible: a BOOL; 3.1's GDI gives 201h, w16's 1) */
    c = GetPixel(hdc, p.x, p.y);
    wsprintf(nm, "gdc.%s.pixel", (LPSTR)name);
    {
        char t[64];
        wsprintf(t, "%lx window %d visible %d", c, c == GetSysColor(COLOR_WINDOW), PtVisible(hdc, p.x, p.y) != 0);
        obs_str(nm, t);
    }
    c = GetPixel(hdc, q.x, q.y);
    wsprintf(nm, "gdc.%s.kidpixel", (LPSTR)name);
    {
        char t[64];
        wsprintf(t, "%lx face %d visible %d", c, c == GetSysColor(COLOR_BTNFACE), PtVisible(hdc, q.x, q.y) != 0);
        obs_str(nm, t);
    }
    /* a corner of the screen: the desktop */
    c = GetPixel(hdc, 2, 470);
    wsprintf(nm, "gdc.%s.deskpixel", (LPSTR)name);
    {
        char t[64];
        wsprintf(t, "%lx bg %d visible %d", c, c == GetSysColor(COLOR_BACKGROUND), PtVisible(hdc, 2, 470) != 0);
        obs_str(nm, t);
    }
    /* painted through it: over the main window, its child, the desktop */
    PatBlt(hdc, p.x - 4, p.y - 4, 8, 8, BLACKNESS);
    PatBlt(hdc, q.x - 4, q.y - 4, 8, 8, BLACKNESS);
    PatBlt(hdc, 2, 466, 8, 8, BLACKNESS);
    {
        RECT a;
        SetRect(&a, p.x - 4, p.y - 4, p.x + 4, p.y + 4);
        wsprintf(nm, "gdc.%s.paint", (LPSTR)name);
        obs(nm, count_color(&a, RGB(0, 0, 0)));
        SetRect(&a, q.x - 4, q.y - 4, q.x + 4, q.y + 4);
        wsprintf(nm, "gdc.%s.kidpaint", (LPSTR)name);
        obs(nm, count_color(&a, RGB(0, 0, 0)));
        SetRect(&a, 2, 466, 10, 474);
        wsprintf(nm, "gdc.%s.deskpaint", (LPSTR)name);
        obs(nm, count_color(&a, RGB(0, 0, 0)));
    }
}

static void sec_gdc(void)
{
    HWND kid = CreateWindow("BUTTON", "Kid", WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON, 20, 20, 80, 30,
                            hMain, (HMENU)200, hi, NULL);
    HWND desk = GetDesktopWindow();
    HDC hdc;
    RECT w;
    WORD s1, s2;
    int i;
    UpdateWindow(hMain);
    pump(300);
    for (i = 0; i < 5; i++) {
        static const char *names[] = { "null", "desk", "deskwin", "nullwin", "deskex" };
        switch (i) {
        case 0: hdc = GetDC(NULL); break;
        case 1: hdc = GetDC(desk); break;
        case 2: hdc = GetWindowDC(desk); break;
        case 3: hdc = GetWindowDC(NULL); break;
        default: hdc = GetDCEx(desk, 0, DCX_WINDOW | DCX_CACHE); break;
        }
        dc_probe(names[i], hdc, kid);
        ReleaseDC(i == 0 || i == 3 ? NULL : desk, hdc);
        /* the window and the desktop drawn again */
        InvalidateRect(desk, NULL, TRUE);
        InvalidateRect(hMain, NULL, TRUE);
        UpdateWindow(desk);
        UpdateWindow(hMain);
        UpdateWindow(kid);
        pump(300);
    }
    /* the desktop window repainted: the windows on it stay */
    GetWindowRect(hMain, &w);
    s1 = checksum(&w);
    InvalidateRect(desk, NULL, TRUE);
    UpdateWindow(desk);
    pump(300);
    s2 = checksum(&w);
    obs("gdc.deskpaint.same", s1 == s2);
    RedrawWindow(desk, NULL, 0, RDW_INVALIDATE | RDW_ERASE | RDW_UPDATENOW);
    pump(300);
    obs("gdc.deskredraw.same", s1 == checksum(&w));
    {
        /* the desktop's corner after its repaint: its own colour */
        RECT a;
        SetRect(&a, 0, 460, 20, 480);
        obs("gdc.deskcorner", count_color(&a, GetSysColor(COLOR_BACKGROUND)));
    }
    DestroyWindow(kid);
    pump(200);
}

/* ---- tab stops ---- */
static const char *tabitems[] = {
    "A\tB\tC\tD",
    "\tE\t\tF",
    "WWWWWWWWWWWWWW\tG",
    "H\tI\tJ\tK\tL\tM\tN\tO\tP\tQ\tR\tS",
    "Processor:\t80486",
    0
};

/* where the words of item i start: the left x of each run of columns
   with ink in the item's rectangle (runs at least 3 columns apart) */
static void ink_runs(HWND lb, int i, char *out2)
{
    RECT r;
    POINT o;
    int x, y, last = -10, n = 0;
    COLORREF bg = GetSysColor(COLOR_WINDOW);
    SendMessage(lb, LB_GETITEMRECT, i, (LPARAM)(RECT FAR *)&r);
    o.x = r.left;
    o.y = r.top;
    ClientToScreen(lb, &o);
    OffsetRect(&r, o.x - r.left, o.y - r.top);
    /* a little further than the item, to the window's right */
    r.right += 6;
    grab(&r);
    out2[0] = 0;
    for (x = 0; x < gcx; x++) {
        BOOL ink = FALSE;
        for (y = 1; y < gcy - 1; y++) {
            COLORREF c = pcolor(px(x, y));
            if (c != bg && c == GetSysColor(COLOR_WINDOWTEXT))
                ink = TRUE;
        }
        if (ink) {
            if (x - last > 3 && n < 20) {
                char t[8];
                wsprintf(t, n ? ",%d" : "%d", x);
                lstrcat(out2, t);
                n++;
            }
            last = x;
        }
    }
}

static void tab_case(HWND lb, const char *name)
{
    int i;
    char nm[40], t[128];
    InvalidateRect(lb, NULL, TRUE);
    UpdateWindow(lb);
    pump(150);
    for (i = 0; tabitems[i]; i++) {
        wsprintf(nm, "tab.%s.%d", (LPSTR)name, i);
        ink_runs(lb, i, t);
        obs_str(nm, t);
    }
}

static void tab_set(HWND lb, const char *name, int n, int *tabs)
{
    char nm[40];
    LRESULT r = SendMessage(lb, LB_SETTABSTOPS, n, (LPARAM)(int FAR *)tabs);
    wsprintf(nm, "tab.%s.ret", (LPSTR)name);
    obs(nm, (WORD)r);
    tab_case(lb, name);
}

static void tab_all(HWND lb, const char *pre)
{
    static int one[] = { 20 }, three[] = { 10, 30, 60 }, back[] = { 50, 20, 70 }, odd[] = { 13, 27 };
    char nm[24];
    int i;
    for (i = 0; tabitems[i]; i++)
        SendMessage(lb, LB_ADDSTRING, 0, (LPARAM)(LPSTR)tabitems[i]);
    wsprintf(nm, "%s.dflt", (LPSTR)pre);
    tab_case(lb, nm);
    wsprintf(nm, "%s.zero", (LPSTR)pre);
    tab_set(lb, nm, 0, NULL);
    wsprintf(nm, "%s.one", (LPSTR)pre);
    tab_set(lb, nm, 1, one);
    wsprintf(nm, "%s.three", (LPSTR)pre);
    tab_set(lb, nm, 3, three);
    wsprintf(nm, "%s.back", (LPSTR)pre);
    tab_set(lb, nm, 3, back);
    wsprintf(nm, "%s.odd", (LPSTR)pre);
    tab_set(lb, nm, 2, odd);
    /* the font changed after the stops were set */
    SendMessage(lb, WM_SETFONT, (WPARAM)GetStockObject(SYSTEM_FONT), TRUE);
    wsprintf(nm, "%s.oddsys", (LPSTR)pre);
    tab_case(lb, nm);
    {
        char t[64];
        LRESULT n = SendMessage(lb, LB_GETTEXT, 0, (LPARAM)(LPSTR)t);
        wsprintf(nm, "tab.%s.gettext", (LPSTR)pre);
        obsf(nm, "%d same %d", (int)n, !lstrcmp(t, tabitems[0]), 0, 0);
        n = SendMessage(lb, LB_GETTEXTLEN, 1, 0);
        wsprintf(nm, "tab.%s.textlen", (LPSTR)pre);
        obs(nm, (WORD)n);
    }
}

BOOL FAR PASCAL __export __loadds plain_proc(HWND h, UINT m, WPARAM w, LPARAM l)
{
    (void)h, (void)w, (void)l;
    return m == WM_INITDIALOG;
}

static void sec_tab(void)
{
    HWND d, lb;
    char nm[24];
    int k;
    for (k = 1; k <= 2; k++) {
        wsprintf(nm, "US33T%d", k);
        d = CreateDialog(hi, nm, hMain, (DLGPROC)plain_proc);
        ShowWindow(d, SW_SHOWNORMAL);
        UpdateWindow(d);
        pump(300);
        lb = GetDlgItem(d, 101);
        wsprintf(nm, "d%d", k);
        tab_all(lb, nm);
        DestroyWindow(d);
        pump(200);
    }
    /* in a window, the system font */
    lb = CreateWindow("LISTBOX", NULL, WS_CHILD | WS_VISIBLE | WS_BORDER | LBS_USETABSTOPS |
                      LBS_NOINTEGRALHEIGHT, 5, 5, 280, 110, hMain, (HMENU)101, hi, NULL);
    tab_all(lb, "w");
    DestroyWindow(lb);
    pump(200);
    /* in a window, given a proportional font before its stops: the stops'
       dialog units from that font or from the system font's */
    lb = CreateWindow("LISTBOX", NULL, WS_CHILD | WS_VISIBLE | WS_BORDER | LBS_USETABSTOPS |
                      LBS_NOINTEGRALHEIGHT, 5, 5, 280, 110, hMain, (HMENU)101, hi, NULL);
    SendMessage(lb, WM_SETFONT, (WPARAM)GetStockObject(ANSI_VAR_FONT), FALSE);
    tab_all(lb, "v");
    DestroyWindow(lb);
    pump(200);
}

/* ---- TabbedTextOut and GetTabbedTextExtent themselves ---- */
static void tto_font(HDC hdc, const char *fn)
{
    static int two[] = { 20, 45 }, one[] = { 30 }, neg[] = { -30 };
    static const char *s[] = { "A\tB", "A\tB\tC\tD", "\t", "WWWWWWWW\tX", "a\t\tb", 0 };
    TEXTMETRIC tm;
    char nm[40];
    int i;
    GetTextMetrics(hdc, &tm);
    wsprintf(nm, "tto.%s.tm", (LPSTR)fn);
    obsf(nm, "ave %d max %d h %d zero %d", tm.tmAveCharWidth, tm.tmMaxCharWidth, tm.tmHeight,
         LOWORD(GetTextExtent(hdc, "0", 1)));
    for (i = 0; s[i]; i++) {
        int n = lstrlen(s[i]);
        DWORD a = GetTabbedTextExtent(hdc, s[i], n, 0, NULL);
        DWORD b = GetTabbedTextExtent(hdc, s[i], n, 1, one);
        DWORD c = GetTabbedTextExtent(hdc, s[i], n, 2, two);
        DWORD d = GetTabbedTextExtent(hdc, s[i], n, 1, neg);
        LONG e = TabbedTextOut(hdc, 10, 10, s[i], n, 2, two, 5);
        LONG f = TabbedTextOut(hdc, 10, 30, s[i], n, 0, NULL, 3);
        wsprintf(nm, "tto.%s.%d", (LPSTR)fn, i);
        {
            char t[80];
            wsprintf(t, "%d %d %d %d", LOWORD(a), LOWORD(b), LOWORD(c), LOWORD(d));
            wsprintf(t + lstrlen(t), " out %d %d h %d", LOWORD(e), LOWORD(f), HIWORD(a));
            obs_str(nm, t);
        }
    }
}

/* ---- DrawFocusRect's dots, by the DC's colours ---- */
static void sec_dfr(void)
{
    static const COLORREF tc[] = { RGB(0, 0, 0), RGB(255, 255, 255), RGB(0, 0, 0), RGB(255, 255, 0) };
    static const COLORREF bc[] = { RGB(255, 255, 255), RGB(0, 0, 0), RGB(0, 0, 128), RGB(0, 0, 128) };
    static const COLORREF fill[] = { RGB(255, 255, 255), RGB(255, 255, 255), RGB(0, 0, 128), RGB(0, 0, 128) };
    int k, x, y;
    for (k = 0; k < 4; k++) {
        HDC hdc = GetDC(hMain);
        RECT r, a;
        HBRUSH b = CreateSolidBrush(fill[k]);
        POINT o;
        char t[80], nm[24];
        SetRect(&r, 11 + k, 10, 31 + k, 20);
        a = r;
        FillRect(hdc, &a, b);
        DeleteObject(b);
        SetTextColor(hdc, tc[k]);
        SetBkColor(hdc, bc[k]);
        DrawFocusRect(hdc, &r);
        ReleaseDC(hMain, hdc);
        o.x = r.left;
        o.y = r.top;
        ClientToScreen(hMain, &o);
        SetRect(&a, o.x, o.y, o.x + 20, o.y + 10);
        grab(&a);
        /* the top row and the left column, as colour indices */
        for (x = 0; x < 20; x++)
            t[x] = "0123456789ABCDEF"[px(x, 0)];
        t[20] = ' ';
        for (y = 0; y < 10; y++)
            t[21 + y] = "0123456789ABCDEF"[px(0, y)];
        t[31] = 0;
        wsprintf(nm, "dfr.%d", k);
        obs_str(nm, t);
    }
    InvalidateRect(hMain, NULL, TRUE);
    UpdateWindow(hMain);
    /* in child windows whose client areas start an odd and an even
       number of pixels across: the dots aligned to the screen or to the
       DC; the first pixel's screen parity (x + y) logged with them */
    for (k = 0; k < 4; k++) {
        HWND c = CreateWindow("US33M", NULL, WS_CHILD | WS_VISIBLE, 10 + (k & 1), 60 + (k >> 1), 60, 30,
                              hMain, (HMENU)300, hi, NULL);
        HDC hdc;
        RECT r, a;
        POINT o;
        char t[80], nm[24];
        UpdateWindow(c);
        hdc = GetDC(c);
        SetRect(&r, 2, 2, 22, 12);
        DrawFocusRect(hdc, &r);
        ReleaseDC(c, hdc);
        o.x = 2;
        o.y = 2;
        ClientToScreen(c, &o);
        SetRect(&a, o.x, o.y, o.x + 20, o.y + 10);
        grab(&a);
        for (x = 0; x < 20; x++)
            t[x] = "0123456789ABCDEF"[px(x, 0)];
        t[20] = ' ';
        t[21] = (char)('0' + ((o.x + o.y) & 1));
        t[22] = 0;
        wsprintf(nm, "dfr.child%d", k);
        obs_str(nm, t);
        DestroyWindow(c);
    }
    UpdateWindow(hMain);
}

static void sec_tto(void)
{
    HDC hdc = GetDC(hMain);
    HFONT f = CreateFont(-11, 0, 0, 0, FW_BOLD, 0, 0, 0, 0, 0, 0, 0, 0, "MS Sans Serif"), old;
    tto_font(hdc, "sys");
    old = SelectObject(hdc, GetStockObject(ANSI_VAR_FONT));
    tto_font(hdc, "var");
    SelectObject(hdc, f);
    tto_font(hdc, "mss");
    SelectObject(hdc, GetStockObject(SYSTEM_FIXED_FONT));
    tto_font(hdc, "fix");
    SelectObject(hdc, old);
    ReleaseDC(hMain, hdc);
    DeleteObject(f);
    InvalidateRect(hMain, NULL, TRUE);
    UpdateWindow(hMain);
}

/* ---- child dialogs' frames ---- */
static BYTE tpl[64];

static void dlgf_one(DWORD style, const char *face)
{
    HWND h, parent = (style & WS_CHILD) ? hMain : 0;
    RECT r, c;
    POINT o;
    char nm[40];
    BYTE *p = tpl;
    *(DWORD *)p = style; p += 4;
    *p++ = 0;                                   /* items */
    *(int *)p = 6; p += 2;
    *(int *)p = 4; p += 2;
    *(int *)p = 100; p += 2;
    *(int *)p = 60; p += 2;
    *p++ = 0;                                   /* menu */
    *p++ = 0;                                   /* class */
    *p++ = 'T'; *p++ = 0;                       /* caption */
    if (style & DS_SETFONT) {
        *(int *)p = 8; p += 2;
        lstrcpy((LPSTR)p, face); p += lstrlen(face) + 1;
    }
    h = CreateDialogIndirect(hi, (LPCSTR)tpl, parent, (DLGPROC)plain_proc);
    /* a top-level template with a font and WS_DLGFRAME: logged, not
       compared (w16 keeps a bigger window there for now: user/dlg.c) */
    wsprintf(nm, "%sdlgf.%lx.%s",
             (LPSTR)(!(style & WS_CHILD) && (style & DS_SETFONT) && (style & WS_DLGFRAME) ? "~" : ""),
             style, (LPSTR)((style & DS_SETFONT) ? face : "none"));
    if (!h) {
        obs(nm, 0);
        return;
    }
    GetWindowRect(h, &r);
    GetClientRect(h, &c);
    o.x = r.left;
    o.y = r.top;
    if (parent)
        ScreenToClient(parent, &o);
    {
        char t[64];
        wsprintf(t, "%d,%d %dx%d", o.x, o.y, r.right - r.left, r.bottom - r.top);
        wsprintf(t + lstrlen(t), " cl %dx%d", c.right, c.bottom);
        obs_str(nm, t);
    }
    {
        DWORD st = GetWindowLong(h, GWL_STYLE);
        wsprintf(nm, "dlgf.%lx.%s.style", style, (LPSTR)((style & DS_SETFONT) ? face : "none"));
        {
            char t[24];
            wsprintf(t, "%lx", st);
            obs_str(nm, t);
        }
    }
    DestroyWindow(h);
}

static void sec_dlgf(void)
{
    static const DWORD st[] = {
        WS_CHILD | WS_CAPTION, WS_CHILD | WS_CAPTION | WS_SYSMENU, WS_CHILD | WS_BORDER,
        WS_CHILD | WS_DLGFRAME, WS_CHILD | DS_MODALFRAME, WS_CHILD | DS_MODALFRAME | WS_CAPTION,
        WS_CHILD | WS_THICKFRAME, WS_CHILD | WS_CAPTION | WS_DISABLED, WS_CHILD,
        WS_POPUP | WS_CAPTION, WS_POPUP | WS_DLGFRAME, WS_POPUP | DS_MODALFRAME | WS_CAPTION, 0
    };
    int i;
    for (i = 0; st[i]; i++) {
        dlgf_one(st[i], "");
        dlgf_one(st[i] | DS_SETFONT, "MS Sans Serif");
        dlgf_one(st[i] | DS_SETFONT, "Helv");
    }
}

LRESULT FAR PASCAL __export __loadds main_proc(HWND h, UINT m, WPARAM w, LPARAM l)
{
    return DefWindowProc(h, m, w, l);
}

int __cdecl tmain(void)
{
    WNDCLASS wc;
    int n;
    hi = (HINSTANCE)init_regs[5];
    WaitEvent(0);
    InitApp(hi);
    out("US33 start\r\n");
    wc.style = 0;
    wc.lpfnWndProc = (WNDPROC)main_proc;
    wc.cbClsExtra = wc.cbWndExtra = 0;
    wc.hInstance = hi;
    wc.hIcon = 0;
    wc.hCursor = LoadCursor(NULL, IDC_ARROW);
    wc.hbrBackground = (HBRUSH)(COLOR_WINDOW + 1);
    wc.lpszMenuName = NULL;
    wc.lpszClassName = "US33M";
    RegisterClass(&wc);
    hMain = CreateWindow("US33M", "US33", WS_OVERLAPPEDWINDOW, 300, 200, 300, 250, 0, 0, hi, NULL);
    ShowWindow(hMain, SW_SHOWNORMAL);
    UpdateWindow(hMain);
    pump(300);
    mouse_away();
    out("gdc\r\n");
    sec_gdc();
    out("dlgf\r\n");
    sec_dlgf();
    out("dfr\r\n");
    sec_dfr();
    out("tto\r\n");
    sec_tto();
    out("tab\r\n");
    sec_tab();
    for (n = 1; n <= 10; n++) {
        out("od\r\n");
        sec_od(n);
    }
    out("US33 end\r\n");
    DestroyWindow(hMain);
    return 0;
}
