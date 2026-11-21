/*
 * newstyle.c - w16 REGEDIT's New Style view: a Windows 95 Registry
 * Editor-like layout over the same database (w16's own; not in Windows
 * 3.1; ARCHITECTURE.md 15.8, docs/w16-settings.md RegeditNewStyle).
 *
 * Within Win16's means and drawn by this file (no common controls, no
 * art from any Microsoft program):
 *   - a key tree on the left: "My Computer", HKEY_CLASSES_ROOT under it,
 *     the keys below, each with a folder (open when selected), dotted
 *     lines and +/- boxes; an owner-drawn list box whose lines are
 *     inserted and removed as keys expand and collapse;
 *   - a splitter (drag it, or View > Split and the mouse or arrow keys);
 *   - on the right a header row (Name, Data; its divider can be dragged)
 *     over the selected key's values: a 3.1 key holds one unnamed string,
 *     shown as "(Default)" with its data in quotes or "(value not set)";
 *   - a status bar with the key's path, "My Computer\HKEY_CLASSES_ROOT\..."
 * Menus: Registry (Import/Export Registry File..., Connect/Disconnect and
 * Print grayed, Exit), Edit (Modify, New > Key / String Value, Delete,
 * Rename, Find..., Find Next), View (Status Bar, Split, Refresh, New
 * Style), Help (Help Topics, About Registry Editor). Rename edits a key's
 * name in place (a copy under the new name, then the old one deleted: the
 * API has no rename); Modify and New > String Value open Edit String for
 * the key's value. Changes are written at once.
 * Its window placement, split and column are kept in WIN.INI [RegEdit]
 * NSWindow=, NSSplit=, NSColumn=, NSStatusBar= (written only by this view).
 */
#include "regedit.h"
#include <shellapi.h>

#define ITEM_H     18
#define INDENT     19
#define STATUS_H   20
#define HEADER_H   18
#define SPLIT_W    4

#define F_EXPANDED 1
#define F_KIDS     2

static HWND ns, tv, lv, rn;     /* the container, tree, values, rename edit */
static HFONT font;
static HBITMAP bm_computer, bm_closed, bm_open, bm_string;
static int split = 200, col = 120, status_on = 1;
static int cxc, cyc;            /* the container's client size */
static int dragging;            /* 1 splitter, 2 column, 3 View > Split */
static int split_save;
static char find_str[PATHMAX];
static BOOL find_keys = TRUE, find_data = TRUE, find_whole;
static char paths_buf[1];       /* (paths are kept in a hidden list) */
static HWND paths;              /* the hidden list of the lines' paths */
static int rename_line = -1;
static FARPROC rename_proc_inst;
static FARPROC rename_old;

/* ---- our own pictures, 16 x 16, drawn once on the window colour ---- */

/* palette letters: k black, w white, g gray, s silver, y yellow, o olive,
   b blue, n navy, r red, c cyan, '.' the background */
static const char *pic_computer[16] = {
    "................", "..kkkkkkkkkkkk..", "..kwwwwwwwwwwgk.", "..kwnnnnnnnnwgk.",
    "..kwnbbbbbbnwgk.", "..kwnbcbbbbnwgk.", "..kwnbbbbbbnwgk.", "..kwnbbbbbbnwgk.",
    "..kwnnnnnnnnwgk.", "..kwwwwwwwwwwgk.", "..kkkkkkkkkkkkk.", ".....kssssk.....",
    "..kkkkkkkkkkkkk.", ".kwwwwwwwwwwwgk.", ".kgggggggggggggk", ".kkkkkkkkkkkkkkk" };
static const char *pic_closed[16] = {
    "................", "................", "..kkkkk.........", ".kyywyyk........",
    ".kwyyyyykkkkkkk.", ".kyyyyyyyyyyyyok", ".kyyyyyyyyyyyyok", ".kyyyyyyyyyyyyok",
    ".kyyyyyyyyyyyyok", ".kyyyyyyyyyyyyok", ".kyyyyyyyyyyyyok", ".kyyyyyyyyyyyyok",
    ".kooooooooooooook", "..kkkkkkkkkkkkkk", "................", "................" };
static const char *pic_open[16] = {
    "................", "................", "..kkkkk.........", ".kyywyyk........",
    ".kwyyyyykkkkkk..", ".kyyyyyyyyyyyok..", ".kyykkkkkkkkkkkk", ".kyykwwwwwwwwwyk",
    ".kykwyyyyyyyyyk.", ".kykwyyyyyyyyok.", ".kkwyyyyyyyyyk..", ".kkyyyyyyyyyok..",
    ".kkooooooooook..", "..kkkkkkkkkkk...", "................", "................" };
static const char *pic_string[16] = {
    "................", ".kkkkkkkkkkkkkk.", ".kwwwwwwwwwwwwk.", ".kwwwwwwwwwwwwk.",
    ".kwwwwwrwwwwwwk.", ".kwwwwwrwwwwwwk.", ".kwrrrwrrrwwwwk.", ".kwwwrwrwwrwwwk.",
    ".kwrrrwrwwrwwwk.", ".kwrwrwrwwrwwwk.", ".kwrrrwrrrwwwwk.", ".kwwwwwwwwwwwwk.",
    ".kwwwwwwwwwwwwk.", ".kkkkkkkkkkkkkk.", "................", "................" };

static COLORREF pal(char c, COLORREF bg)
{
    switch (c) {
    case 'k': return RGB(0, 0, 0);
    case 'w': return RGB(255, 255, 255);
    case 'g': return RGB(128, 128, 128);
    case 's': return RGB(192, 192, 192);
    case 'y': return RGB(255, 255, 0);
    case 'o': return RGB(128, 128, 0);
    case 'b': return RGB(0, 0, 255);
    case 'n': return RGB(0, 0, 128);
    case 'r': return RGB(255, 0, 0);
    case 'c': return RGB(0, 255, 255);
    }
    return bg;
}

static HBITMAP make_pic(HDC dc, const char **rows)
{
    HDC m = CreateCompatibleDC(dc);
    HBITMAP b = CreateCompatibleBitmap(dc, 16, 16), old;
    COLORREF bg = GetSysColor(COLOR_WINDOW);
    int x, y;
    old = SelectObject(m, b);
    for (y = 0; y < 16; y++)
        for (x = 0; x < 16; x++)
            SetPixel(m, x, y, pal(rows[y][x], bg));
    SelectObject(m, old);
    DeleteDC(m);
    return b;
}

static void draw_pic(HDC dc, HBITMAP b, int x, int y)
{
    HDC m = CreateCompatibleDC(dc);
    HBITMAP old = SelectObject(m, b);
    BitBlt(dc, x, y, 16, 16, m, 0, 0, SRCCOPY);
    SelectObject(m, old);
    DeleteDC(m);
}

/* ---- the tree's lines ---- */

static WORD depth_of(int i) { return LOWORD(SendMessage(tv, LB_GETITEMDATA, i, 0)); }
static WORD flags_of(int i) { return HIWORD(SendMessage(tv, LB_GETITEMDATA, i, 0)); }
static void set_flags(int i, WORD f)
{
    SendMessage(tv, LB_SETITEMDATA, i, MAKELONG(depth_of(i), f));
}

static void line_path(int i, LPSTR out)
{
    out[0] = 0;
    if (i >= 2 && SendMessage(paths, LB_GETTEXTLEN, i, 0) < PATHMAX)
        SendMessage(paths, LB_GETTEXT, i, (LPARAM)out);
}

static BOOL has_kids(LPCSTR path)
{
    HKEY k;
    char n[128];
    BOOL r;
    if (RegOpenKey(HKEY_CLASSES_ROOT, (LPSTR)path, &k))
        return FALSE;
    r = !RegEnumKey(k, 0, n, sizeof n);
    RegCloseKey(k);
    return r;
}

static void insert_line(int at, LPCSTR name, LPCSTR path, WORD depth, WORD fl)
{
    SendMessage(tv, LB_INSERTSTRING, at, (LPARAM)name);
    SendMessage(tv, LB_SETITEMDATA, at, MAKELONG(depth, fl));
    SendMessage(paths, LB_INSERTSTRING, at, (LPARAM)path);
}

/* the subkeys of line i inserted below it (in RegEnumKey's order) */
static void expand(int i)
{
    static char p[PATHMAX], name[128];
    HKEY k;
    DWORD n;
    int at = i + 1, len;
    WORD d = depth_of(i);
    if (flags_of(i) & F_EXPANDED)
        return;
    set_flags(i, flags_of(i) | F_EXPANDED);
    if (i == 0) {
        char r[32];
        rstr(IDS_NS_ROOT, r, sizeof r);
        insert_line(1, r, "", 1, F_KIDS);
        return;
    }
    line_path(i, p);
    len = lstrlen(p);
    if (RegOpenKey(HKEY_CLASSES_ROOT, p, &k))
        return;
    for (n = 0; !RegEnumKey(k, n, name, sizeof name); n++) {
        if (len + lstrlen(name) + 2 >= PATHMAX)
            continue;
        if (len) lstrcat(p, "\\");
        lstrcat(p, name);
        insert_line(at++, name, p, d + 1, has_kids(p) ? F_KIDS : 0);
        p[len] = 0;
    }
    RegCloseKey(k);
    if (at == i + 1)
        set_flags(i, flags_of(i) & ~F_KIDS);
}

static void collapse(int i)
{
    WORD d = depth_of(i);
    int n = (int)SendMessage(tv, LB_GETCOUNT, 0, 0);
    while (i + 1 < n && depth_of(i + 1) > d) {
        SendMessage(tv, LB_DELETESTRING, i + 1, 0);
        SendMessage(paths, LB_DELETESTRING, i + 1, 0);
        n--;
    }
    set_flags(i, flags_of(i) & ~F_EXPANDED);
}

/* does the line at depth a, above or at line i, have a later sibling? */
static BOOL later_sibling(int i, WORD a)
{
    int n = (int)SendMessage(tv, LB_GETCOUNT, 0, 0);
    for (i++; i < n; i++) {
        WORD d = depth_of(i);
        if (d < a)
            return FALSE;
        if (d == a)
            return TRUE;
    }
    return FALSE;
}

static void dotted_v(HDC dc, int x, int y0, int y1, COLORREF c)
{
    int y;
    for (y = y0 + ((y0 + x) & 1); y < y1; y += 2)
        SetPixel(dc, x, y, c);
}

static void dotted_h(HDC dc, int x0, int x1, int y, COLORREF c)
{
    int x;
    for (x = x0 + ((x0 + y) & 1); x < x1; x += 2)
        SetPixel(dc, x, y, c);
}

static void draw_tree_item(DRAWITEMSTRUCT FAR *d)
{
    static char text[256];
    HDC dc = d->hDC;
    int i = d->itemID, y = d->rcItem.top, mid = y + ITEM_H / 2;
    WORD dep, fl, a;
    COLORREF gray = RGB(128, 128, 128);
    int x0, tx;
    DWORD ext;
    RECT r;
    HBITMAP pic;
    if (i < 0)
        return;
    dep = depth_of(i);
    fl = flags_of(i);
    x0 = d->rcItem.left + 2 + INDENT * dep;     /* the picture's left */
    FillRect(dc, &d->rcItem, (HBRUSH)(COLOR_WINDOW + 1));
    for (a = 1; a <= dep; a++) {
        int lx = d->rcItem.left + 2 + INDENT * (a - 1) + 8;
        if (a == dep) {
            dotted_v(dc, lx, y, later_sibling(i, a) ? y + ITEM_H : mid + 1, gray);
            dotted_h(dc, lx, x0, mid, gray);
        } else {
            /* the ancestor at depth a: find it above, then its siblings */
            int j = i;
            while (j > 0 && depth_of(j) > a)
                j--;
            if (later_sibling(j, a))
                dotted_v(dc, lx, y, y + ITEM_H, gray);
        }
    }
    if (dep >= 1 && (fl & F_KIDS)) {
        /* the +/- box, on the line left of the picture */
        int bx = x0 - INDENT + 4, by = mid - 4;
        HBRUSH wb = GetStockObject(WHITE_BRUSH);
        RECT b;
        SetRect(&b, bx, by, bx + 9, by + 9);
        FillRect(dc, &b, wb);
        FrameRect(dc, &b, GetStockObject(GRAY_BRUSH));
        SetRect(&b, bx + 2, mid, bx + 7, mid + 1);
        FillRect(dc, &b, GetStockObject(BLACK_BRUSH));
        if (!(fl & F_EXPANDED)) {
            SetRect(&b, bx + 4, by + 2, bx + 5, by + 7);
            FillRect(dc, &b, GetStockObject(BLACK_BRUSH));
        }
    }
    pic = i == 0 ? bm_computer : ((d->itemState & ODS_SELECTED) ? bm_open : bm_closed);
    draw_pic(dc, pic, x0, y + 1);
    SendMessage(tv, LB_GETTEXT, i, (LPARAM)(LPSTR)text);
    tx = x0 + 16 + 4;
    ext = GetTextExtent(dc, text, lstrlen(text));
    SetRect(&r, tx - 2, y + 1, tx + LOWORD(ext) + 2, y + ITEM_H - 1);
    if ((d->itemState & ODS_SELECTED) && GetFocus() == tv) {
        SetBkColor(dc, GetSysColor(COLOR_HIGHLIGHT));
        SetTextColor(dc, GetSysColor(COLOR_HIGHLIGHTTEXT));
    } else if (d->itemState & ODS_SELECTED) {
        SetBkColor(dc, GetSysColor(COLOR_BTNFACE));
        SetTextColor(dc, GetSysColor(COLOR_WINDOWTEXT));
    } else {
        SetBkColor(dc, GetSysColor(COLOR_WINDOW));
        SetTextColor(dc, GetSysColor(COLOR_WINDOWTEXT));
    }
    ExtTextOut(dc, tx, y + (ITEM_H - HIWORD(ext)) / 2, ETO_OPAQUE, &r, text, lstrlen(text), NULL);
    if ((d->itemState & ODS_FOCUS) && GetFocus() == tv)
        DrawFocusRect(dc, &r);
}

static void draw_value_item(DRAWITEMSTRUCT FAR *d)
{
    static char name[32], val[PATHMAX + 2];
    HDC dc = d->hDC;
    int y = d->rcItem.top;
    DWORD ext;
    RECT r;
    if ((int)d->itemID < 0)
        return;
    FillRect(dc, &d->rcItem, (HBRUSH)(COLOR_WINDOW + 1));
    draw_pic(dc, bm_string, d->rcItem.left + 2, y + 1);
    rstr(IDS_NS_DEFAULT, name, sizeof name);
    ext = GetTextExtent(dc, name, lstrlen(name));
    SetRect(&r, d->rcItem.left + 20, y + 1, d->rcItem.left + 24 + LOWORD(ext), y + ITEM_H - 1);
    if ((d->itemState & ODS_SELECTED) && GetFocus() == lv) {
        SetBkColor(dc, GetSysColor(COLOR_HIGHLIGHT));
        SetTextColor(dc, GetSysColor(COLOR_HIGHLIGHTTEXT));
    } else {
        SetBkColor(dc, GetSysColor(COLOR_WINDOW));
        SetTextColor(dc, GetSysColor(COLOR_WINDOWTEXT));
    }
    ExtTextOut(dc, r.left + 2, y + (ITEM_H - HIWORD(ext)) / 2, ETO_OPAQUE, &r, name, lstrlen(name), NULL);
    if ((d->itemState & ODS_FOCUS) && GetFocus() == lv)
        DrawFocusRect(dc, &r);
    SetBkColor(dc, GetSysColor(COLOR_WINDOW));
    SetTextColor(dc, GetSysColor(COLOR_WINDOWTEXT));
    SendMessage(lv, LB_GETTEXT, d->itemID, (LPARAM)(LPSTR)val);
    SetRect(&r, d->rcItem.left + col + 2, y, d->rcItem.right, y + ITEM_H);
    ExtTextOut(dc, r.left + 2, y + (ITEM_H - HIWORD(ext)) / 2, ETO_CLIPPED, &r, val, lstrlen(val), NULL);
}

/* ---- selection, values, status ---- */

static int cur_line(void)
{
    return (int)SendMessage(tv, LB_GETCURSEL, 0, 0);
}

static void show_values(void)
{
    static char v[PATHMAX], s[PATHMAX + 2];
    int i = cur_line();
    SendMessage(lv, LB_RESETCONTENT, 0, 0);
    if (i >= 1) {
        reg_get(g_sel, v, sizeof v);
        if (v[0])
            wsprintf(s, "\"%s\"", (LPSTR)v);
        else
            rstr(IDS_NS_NOTSET, s, sizeof s);
        SendMessage(lv, LB_ADDSTRING, 0, (LPARAM)(LPSTR)s);
    }
    InvalidateRect(ns, NULL, FALSE);
}

static void on_select(void)
{
    int i = cur_line();
    line_path(i < 0 ? 0 : i, g_sel);
    show_values();
}

/* select the key at path, expanding its ancestors */
static void select_path(LPCSTR path)
{
    static char p[PATHMAX], want[PATHMAX];
    int i = 1, n;
    LPCSTR s = path;
    expand(0);
    if (!path[0]) {
        expand(1);
        SendMessage(tv, LB_SETCURSEL, 1, 0);
        on_select();
        return;
    }
    want[0] = 0;
    for (;;) {
        LPCSTR e = s;
        int l = lstrlen(want);
        while (*e && *e != '\\')
            e++;
        if (l + (e - s) + 2 >= PATHMAX)
            break;
        if (l) lstrcat(want, "\\");
        lstrcpyn(want + lstrlen(want), s, (e - s) + 1);
        expand(i);
        n = (int)SendMessage(tv, LB_GETCOUNT, 0, 0);
        for (i++; i < n; i++) {
            line_path(i, p);
            if (!lstrcmpi(p, want))
                break;
        }
        if (i >= n) {
            i = 1;
            break;
        }
        if (!*e)
            break;
        s = e + 1;
    }
    SendMessage(tv, LB_SETCURSEL, i, 0);
    on_select();
}

static void rebuild(void)
{
    static char keep[PATHMAX];
    lstrcpy(keep, g_sel);
    SendMessage(tv, WM_SETREDRAW, FALSE, 0);
    SendMessage(tv, LB_RESETCONTENT, 0, 0);
    SendMessage(paths, LB_RESETCONTENT, 0, 0);
    insert_line(0, "", "", 0, F_KIDS);
    {
        char c[32];
        rstr(IDS_NS_COMPUTER, c, sizeof c);
        SendMessage(tv, LB_DELETESTRING, 0, 0);
        SendMessage(tv, LB_INSERTSTRING, 0, (LPARAM)(LPSTR)c);
        SendMessage(tv, LB_SETITEMDATA, 0, MAKELONG(0, F_KIDS));
    }
    select_path(keep);
    SendMessage(tv, WM_SETREDRAW, TRUE, 0);
    InvalidateRect(tv, NULL, TRUE);
}

static void paint_status(HDC dc)
{
    static char s[PATHMAX + 64];
    RECT r;
    HBRUSH face = CreateSolidBrush(GetSysColor(COLOR_BTNFACE));
    HPEN shadow = CreatePen(PS_SOLID, 1, GetSysColor(COLOR_BTNSHADOW)), old;
    int i = cur_line(), n;
    SetRect(&r, 0, cyc - STATUS_H, cxc, cyc);
    FillRect(dc, &r, face);
    /* a sunken panel */
    old = SelectObject(dc, shadow);
    MoveTo(dc, 2, cyc - 2);
    LineTo(dc, 2, cyc - STATUS_H + 3);
    LineTo(dc, cxc - 2, cyc - STATUS_H + 3);
    SelectObject(dc, GetStockObject(WHITE_PEN));
    MoveTo(dc, 3, cyc - 2);
    LineTo(dc, cxc - 2, cyc - 2);
    LineTo(dc, cxc - 2, cyc - STATUS_H + 2);
    SelectObject(dc, old);
    n = rstr(IDS_NS_COMPUTER, s, sizeof s);
    if (i >= 1) {
        s[n++] = '\\';
        n += rstr(IDS_NS_ROOT, s + n, sizeof s - n);
        if (g_sel[0]) {
            s[n++] = '\\';
            lstrcpyn(s + n, g_sel, sizeof s - n);
        } else {
            s[n] = 0;
        }
    }
    SetBkMode(dc, TRANSPARENT);
    SetTextColor(dc, GetSysColor(COLOR_BTNTEXT));
    SetRect(&r, 6, cyc - STATUS_H + 4, cxc - 4, cyc - 3);
    DrawText(dc, s, -1, &r, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
    DeleteObject(face);
    DeleteObject(shadow);
}

static void paint_header(HDC dc)
{
    char s[16];
    RECT r;
    int x0 = split + SPLIT_W + 2, cells[3], k;
    HBRUSH face = CreateSolidBrush(GetSysColor(COLOR_BTNFACE));
    HPEN shadow = CreatePen(PS_SOLID, 1, GetSysColor(COLOR_BTNSHADOW)), old;
    cells[0] = x0;
    cells[1] = x0 + col;
    cells[2] = cxc - 2;
    SetBkMode(dc, TRANSPARENT);
    SetTextColor(dc, GetSysColor(COLOR_BTNTEXT));
    for (k = 0; k < 2; k++) {
        SetRect(&r, cells[k], 2, cells[k + 1], 2 + HEADER_H);
        FillRect(dc, &r, face);
        old = SelectObject(dc, GetStockObject(WHITE_PEN));
        MoveTo(dc, r.left, r.bottom - 1);
        LineTo(dc, r.left, r.top);
        LineTo(dc, r.right - 1, r.top);
        SelectObject(dc, shadow);
        MoveTo(dc, r.left + 1, r.bottom - 2);
        LineTo(dc, r.right - 2, r.bottom - 2);
        LineTo(dc, r.right - 2, r.top);
        SelectObject(dc, GetStockObject(BLACK_PEN));
        MoveTo(dc, r.left, r.bottom - 1);
        LineTo(dc, r.right - 1, r.bottom - 1);
        LineTo(dc, r.right - 1, r.top - 1);
        SelectObject(dc, old);
        rstr(k ? IDS_NS_DATA : IDS_NS_NAME, s, sizeof s);
        r.left += 6;
        DrawText(dc, s, -1, &r, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
    }
    DeleteObject(face);
    DeleteObject(shadow);
}

/* a sunken 2-pixel edge round a pane */
static void sunken(HDC dc, int l, int t, int r, int b)
{
    HPEN shadow = CreatePen(PS_SOLID, 1, GetSysColor(COLOR_BTNSHADOW)), old;
    old = SelectObject(dc, shadow);
    MoveTo(dc, l, b - 1);
    LineTo(dc, l, t);
    LineTo(dc, r, t);
    SelectObject(dc, GetStockObject(BLACK_PEN));
    MoveTo(dc, l + 1, b - 2);
    LineTo(dc, l + 1, t + 1);
    LineTo(dc, r - 1, t + 1);
    SelectObject(dc, GetStockObject(WHITE_PEN));
    MoveTo(dc, l, b - 1);
    LineTo(dc, r - 1, b - 1);
    LineTo(dc, r - 1, t - 1);
    SelectObject(dc, old);
    DeleteObject(shadow);
}

static int panes_bottom(void)
{
    return cyc - (status_on ? STATUS_H : 0);
}

static void place_panes(void)
{
    int b = panes_bottom();
    if (split < 20) split = 20;
    if (split > cxc - 40) split = cxc - 40 > 20 ? cxc - 40 : 20;
    MoveWindow(tv, 2, 2, split - 4, b - 4, TRUE);
    MoveWindow(lv, split + SPLIT_W + 2, 2 + HEADER_H, cxc - split - SPLIT_W - 4,
               b - 4 - HEADER_H, TRUE);
    InvalidateRect(ns, NULL, TRUE);
}

void ns_size(int cx, int cy)
{
    if (cx < 0) {
        /* the start: WIN.INI's placement, else 600 x 400 at the default place */
        char s[64];
        int x = CW_USEDEFAULT, y = 0, w = 600, h = 400;
        GetProfileString("RegEdit", "NSWindow", "", s, sizeof s);
        if (s[0]) {
            LPSTR p = s;
            int v[4], k;
            for (k = 0; k < 4 && *p; k++) {
                BOOL neg = *p == '-';
                if (neg) p++;
                for (v[k] = 0; *p >= '0' && *p <= '9'; p++)
                    v[k] = v[k] * 10 + *p - '0';
                if (neg) v[k] = -v[k];
                while (*p == ',' || *p == ' ') p++;
            }
            if (k == 4) {
                x = v[0]; y = v[1]; w = v[2]; h = v[3];
            }
        }
        if (x == CW_USEDEFAULT)
            SetWindowPos(g_frame, NULL, 0, 0, w, h, SWP_NOMOVE | SWP_NOZORDER);
        else
            MoveWindow(g_frame, x, y, w, h, TRUE);
        return;
    }
    MoveWindow(ns, 0, 0, cx, cy, TRUE);
}

void ns_save(void)
{
    char s[64];
    RECT r;
    if (!ns || IsIconic(g_frame))
        return;
    GetWindowRect(g_frame, &r);
    wsprintf(s, "%d,%d,%d,%d", r.left, r.top, r.right - r.left, r.bottom - r.top);
    WriteProfileString("RegEdit", "NSWindow", s);
    wsprintf(s, "%d", split);
    WriteProfileString("RegEdit", "NSSplit", s);
    wsprintf(s, "%d", col);
    WriteProfileString("RegEdit", "NSColumn", s);
    wsprintf(s, "%d", status_on);
    WriteProfileString("RegEdit", "NSStatusBar", s);
}

/* ---- commands ---- */

static void edit_string(void);

static void begin_rename(void)
{
    static char t[128];
    int i = cur_line();
    RECT r;
    DWORD ext;
    HDC dc;
    if (i < 2)
        return;
    SendMessage(tv, LB_GETITEMRECT, i, (LPARAM)(RECT FAR *)&r);
    SendMessage(tv, LB_GETTEXT, i, (LPARAM)(LPSTR)t);
    dc = GetDC(tv);
    SelectObject(dc, font);
    ext = GetTextExtent(dc, t, lstrlen(t));
    ReleaseDC(tv, dc);
    r.left += 2 + INDENT * depth_of(i) + 16 + 2;
    rename_line = i;
    MoveWindow(rn, r.left, r.top, max(LOWORD(ext) + 24, 80), ITEM_H, TRUE);
    SetWindowText(rn, t);
    SendMessage(rn, EM_SETSEL, 0, MAKELONG(0, -1));
    ShowWindow(rn, SW_SHOW);
    SetFocus(rn);
}

static BOOL valid_name(LPCSTR s)
{
    int n = 0;
    for (; *s; s++, n++)
        if ((BYTE)*s <= 0x20 || (BYTE)*s >= 0x80 || *s == '\\')
            return FALSE;
    return n <= 63;
}

static void end_rename(BOOL keep)
{
    static char name[128], from[PATHMAX], to[PATHMAX], old[128];
    int i = rename_line;
    if (i < 0)
        return;
    rename_line = -1;
    GetWindowText(rn, name, sizeof name);
    ShowWindow(rn, SW_HIDE);
    SetFocus(tv);
    if (!keep)
        return;
    line_path(i, from);
    lstrcpy(old, path_last(from));
    if (!lstrcmp(old, name))
        return;
    if (!name[0]) {
        msgbox(g_frame, IDS_NS_RENEMPTY, old, MB_OK | MB_ICONEXCLAMATION);
        return;
    }
    if (!valid_name(name)) {
        msgbox(g_frame, IDS_NS_RENBAD, old, MB_OK | MB_ICONEXCLAMATION);
        return;
    }
    lstrcpy(to, from);
    *path_last(to) = 0;
    if (lstrlen(to) + lstrlen(name) >= PATHMAX)
        return;
    lstrcat(to, name);
    if (lstrcmpi(old, name) && reg_exists(to)) {
        msgbox(g_frame, IDS_NS_RENEXISTS, old, MB_OK | MB_ICONEXCLAMATION);
        return;
    }
    if (!lstrcmpi(old, name)) {
        /* only the case changes: by way of a temporary name */
        static char tmp[PATHMAX];
        lstrcpy(tmp, to);
        lstrcat(tmp, "~");
        if (reg_copy(from, tmp) || reg_delete(from) || reg_copy(tmp, to) || reg_delete(tmp))
            return;
    } else if (reg_copy(from, to) || reg_delete(from)) {
        return;
    }
    lstrcpy(g_sel, to);
    rebuild();
}

LRESULT FAR PASCAL _export RenameProc(HWND w, UINT msg, WPARAM wp, LPARAM lp)
{
    if (msg == WM_KEYDOWN && (wp == VK_RETURN || wp == VK_ESCAPE)) {
        end_rename(wp == VK_RETURN);
        return 0;
    }
    if (msg == WM_CHAR && (wp == '\r' || wp == 27))
        return 0;
    if (msg == WM_KILLFOCUS && rename_line >= 0) {
        LRESULT r = CallWindowProc(rename_old, w, msg, wp, lp);
        end_rename(TRUE);
        return r;
    }
    return CallWindowProc(rename_old, w, msg, wp, lp);
}

static void new_key(void)
{
    static char p[PATHMAX], name[32], fmt[32];
    int i = cur_line(), n;
    if (i < 1)
        return;
    rstr(IDS_NS_NEWKEY, fmt, sizeof fmt);
    for (n = 1;; n++) {
        wsprintf(name, fmt, n);
        lstrcpy(p, g_sel);
        if (lstrlen(p) + lstrlen(name) + 2 >= PATHMAX)
            return;
        if (p[0]) lstrcat(p, "\\");
        lstrcat(p, name);
        if (!reg_exists(p))
            break;
    }
    if (reg_create(p))
        return;
    lstrcpy(g_sel, p);
    rebuild();
    begin_rename();
}

static void delete_sel(void)
{
    char t[64];
    static char q[160];
    if (GetFocus() == lv) {
        if (SendMessage(lv, LB_GETCURSEL, 0, 0) < 0)
            return;
        rstr(IDS_NS_DELVALT, t, sizeof t);
        rstr(IDS_NS_DELVAL, q, sizeof q);
        if (MessageBox(g_frame, q, t, MB_YESNO | MB_ICONQUESTION) == IDYES) {
            reg_set(g_sel, "");
            show_values();
        }
        return;
    }
    if (cur_line() < 2)
        return;
    rstr(IDS_NS_DELKEYT, t, sizeof t);
    rstr(IDS_NS_DELKEY, q, sizeof q);
    if (MessageBox(g_frame, q, t, MB_YESNO | MB_ICONQUESTION) != IDYES)
        return;
    {
        int i = cur_line();
        reg_delete(g_sel);
        collapse(i);
        SendMessage(tv, LB_DELETESTRING, i, 0);
        SendMessage(paths, LB_DELETESTRING, i, 0);
        if (i >= (int)SendMessage(tv, LB_GETCOUNT, 0, 0))
            i--;
        SendMessage(tv, LB_SETCURSEL, i, 0);
        on_select();
        InvalidateRect(tv, NULL, TRUE);
    }
}

BOOL FAR PASCAL _export EditStrProc(HWND d, UINT msg, WPARAM wp, LPARAM lp)
{
    static char v[PATHMAX];
    (void)lp;
    switch (msg) {
    case WM_INITDIALOG:
        rstr(IDS_NS_DEFAULT, v, sizeof v);
        SetDlgItemText(d, IDC_NS_NAME, v);
        reg_get(g_sel, v, sizeof v);
        SetDlgItemText(d, IDC_NS_DATA, v);
        SendDlgItemMessage(d, IDC_NS_DATA, EM_SETSEL, 0, MAKELONG(0, -1));
        SetFocus(GetDlgItem(d, IDC_NS_DATA));
        return FALSE;
    case WM_COMMAND:
        if (wp == IDOK) {
            GetDlgItemText(d, IDC_NS_DATA, v, sizeof v);
            reg_set(g_sel, v);
            EndDialog(d, IDOK);
            return TRUE;
        }
        if (wp == IDCANCEL) {
            EndDialog(d, IDCANCEL);
            return TRUE;
        }
    }
    return FALSE;
}

static void edit_string(void)
{
    FARPROC fp;
    if (cur_line() < 1)
        return;
    fp = MakeProcInstance((FARPROC)EditStrProc, g_inst);
    if (DialogBox(g_inst, MAKEINTRESOURCE(DLG_EDITSTRING), g_frame, (DLGPROC)fp) == IDOK)
        show_values();
    FreeProcInstance(fp);
    SendMessage(lv, LB_SETCURSEL, 0, 0);
}

BOOL FAR PASCAL _export NsFindProc(HWND d, UINT msg, WPARAM wp, LPARAM lp)
{
    (void)lp;
    switch (msg) {
    case WM_INITDIALOG:
        SetDlgItemText(d, IDC_NS_DATA, find_str);
        CheckDlgButton(d, IDC_NS_LOOKKEYS, find_keys);
        CheckDlgButton(d, IDC_NS_LOOKDATA, find_data);
        CheckDlgButton(d, IDC_NS_WHOLE, find_whole);
        return TRUE;
    case WM_COMMAND:
        if (wp == IDOK) {
            GetDlgItemText(d, IDC_NS_DATA, find_str, sizeof find_str);
            find_keys = IsDlgButtonChecked(d, IDC_NS_LOOKKEYS);
            find_data = IsDlgButtonChecked(d, IDC_NS_LOOKDATA);
            find_whole = IsDlgButtonChecked(d, IDC_NS_WHOLE);
            EndDialog(d, IDOK);
            return TRUE;
        }
        if (wp == IDCANCEL) {
            EndDialog(d, IDCANCEL);
            return TRUE;
        }
    }
    return FALSE;
}

static BOOL matches(LPCSTR s)
{
    static char a[PATHMAX], b[PATHMAX];
    int la, lb, k;
    lstrcpy(a, s);
    lstrcpy(b, find_str);
    AnsiUpper(a);
    AnsiUpper(b);
    if (find_whole)
        return !lstrcmp(a, b);
    la = lstrlen(a);
    lb = lstrlen(b);
    for (k = 0; k + lb <= la; k++) {
        char c = a[k + lb];
        int eq;
        a[k + lb] = 0;
        eq = !lstrcmp(a + k, b);
        a[k + lb] = c;
        if (eq)
            return TRUE;
    }
    return FALSE;
}

/* the whole database in the tree's order (depth first, RegEnumKey's
   order): the first key after `after` whose name or value matches */
static char fp_path[PATHMAX], fp_found[PATHMAX];
static BOOL fp_passed;

static BOOL find_rec(void)
{
    static char name[128], val[PATHMAX];
    HKEY k;
    DWORD i;
    int n = lstrlen(fp_path);
    if (RegOpenKey(HKEY_CLASSES_ROOT, fp_path, &k))
        return FALSE;
    for (i = 0; !RegEnumKey(k, i, name, sizeof name); i++) {
        if (n + lstrlen(name) + 2 >= PATHMAX)
            continue;
        if (n) lstrcat(fp_path, "\\");
        lstrcat(fp_path, name);
        if (fp_passed) {
            reg_get(fp_path, val, sizeof val);
            if ((find_keys && matches(name)) || (find_data && val[0] && matches(val))) {
                lstrcpy(fp_found, fp_path);
                RegCloseKey(k);
                return TRUE;
            }
        } else if (!lstrcmpi(fp_path, fp_found)) {
            fp_passed = TRUE;
        }
        if (find_rec()) {
            RegCloseKey(k);
            return TRUE;
        }
        fp_path[n] = 0;
    }
    RegCloseKey(k);
    return FALSE;
}

static void find_next(void)
{
    HCURSOR old;
    BOOL ok;
    if (!find_str[0])
        return;
    old = SetCursor(LoadCursor(NULL, IDC_WAIT));
    lstrcpy(fp_found, g_sel);
    fp_passed = cur_line() < 2;
    fp_path[0] = 0;
    ok = find_rec();
    SetCursor(old);
    if (ok) {
        select_path(fp_found);
        SetFocus(tv);
    } else {
        char t[64];
        static char m[128];
        rstr(IDS_NS_TITLE, t, sizeof t);
        rstr(IDS_NS_FINDDONE, m, sizeof m);
        MessageBox(g_frame, m, t, MB_OK | MB_ICONINFORMATION);
    }
}

void ns_command(UINT id)
{
    static char f[PATHMAX];
    FARPROC fp;
    int i;
    switch (id) {
    case 0:                             /* the focus to the tree */
        if (tv)
            SetFocus(tv);
        return;
    case IDM_NS_IMPORT:
        lstrcpy(f, "*.reg");
        if (!file_dialog(g_frame, FALSE, IDS_NS_IMPTITLE, IDS_REGFILTER, "reg", f, sizeof f))
            return;
        switch (merge_file(f, NULL)) {
        case MERGE_OK:
            msgbox(g_frame, IDS_NS_IMPORTED, f, MB_OK | MB_ICONINFORMATION);
            break;
        case MERGE_NOTREG:
            msgbox(g_frame, IDS_NOTREG, NULL, MB_OK | MB_ICONEXCLAMATION);
            break;
        default:
            msgbox(g_frame, IDS_CANTOPEN, f, MB_OK | MB_ICONEXCLAMATION);
        }
        rebuild();
        return;
    case IDM_NS_EXPORT:
        lstrcpy(f, "*.reg");
        if (file_dialog(g_frame, TRUE, IDS_NS_EXPTITLE, IDS_REGFILTER, "reg", f, sizeof f)) {
            if (save_file(f, g_sel))
                msgbox(g_frame, IDS_NS_EXPORTED, f, MB_OK | MB_ICONINFORMATION);
            else
                msgbox(g_frame, IDS_CANTOPEN, f, MB_OK | MB_ICONEXCLAMATION);
        }
        return;
    case IDM_NS_MODIFY:
    case IDM_NS_NEWSTR:
        edit_string();
        return;
    case IDM_NS_NEWKEY:
        new_key();
        return;
    case IDM_NS_DELETE:
        delete_sel();
        return;
    case IDM_NS_RENAME:
        if (GetFocus() != lv)
            begin_rename();
        return;
    case IDM_NS_FIND:
        fp = MakeProcInstance((FARPROC)NsFindProc, g_inst);
        i = DialogBox(g_inst, MAKEINTRESOURCE(DLG_NSFIND), g_frame, (DLGPROC)fp);
        FreeProcInstance(fp);
        if (i == IDOK)
            find_next();
        return;
    case IDM_NS_FINDNEXT:
        find_next();
        return;
    case IDM_NS_STATUS:
        status_on = !status_on;
        CheckMenuItem(GetMenu(g_frame), IDM_NS_STATUS, status_on ? MF_CHECKED : MF_UNCHECKED);
        place_panes();
        return;
    case IDM_NS_SPLIT: {
        POINT p;
        dragging = 3;
        split_save = split;
        p.x = split + SPLIT_W / 2;
        p.y = panes_bottom() / 2;
        ClientToScreen(ns, &p);
        SetCursorPos(p.x, p.y);
        SetCapture(ns);
        SetCursor(LoadCursor(NULL, IDC_SIZEWE));
        return;
    }
    case IDM_NS_REFRESH:
        rebuild();
        return;
    case IDM_NS_HELP:
        help(HELP_INDEX, 0);
        return;
    case IDM_NS_ABOUT:
        about();
        return;
    }
}

/* keys the tree and the value list handle before the accelerators */
BOOL ns_translate(MSG FAR *m)
{
    int i;
    if (m->message != WM_KEYDOWN && m->message != WM_CHAR)
        return FALSE;
    if (m->hwnd == rn)
        return FALSE;
    if (dragging == 3 && m->message == WM_KEYDOWN) {
        if (m->wParam == VK_LEFT || m->wParam == VK_RIGHT) {
            split += m->wParam == VK_LEFT ? -8 : 8;
            place_panes();
        } else if (m->wParam == VK_RETURN || m->wParam == VK_ESCAPE) {
            if (m->wParam == VK_ESCAPE)
                split = split_save;
            dragging = 0;
            ReleaseCapture();
            place_panes();
        }
        return TRUE;
    }
    if (m->message == WM_KEYDOWN && m->wParam == VK_TAB) {
        SetFocus(GetFocus() == tv ? lv : tv);
        if (GetFocus() == lv && SendMessage(lv, LB_GETCURSEL, 0, 0) < 0)
            SendMessage(lv, LB_SETCURSEL, 0, 0);
        InvalidateRect(tv, NULL, FALSE);
        InvalidateRect(lv, NULL, FALSE);
        return TRUE;
    }
    if (m->hwnd == lv && m->message == WM_KEYDOWN && m->wParam == VK_RETURN) {
        edit_string();
        return TRUE;
    }
    if (m->hwnd != tv)
        return FALSE;
    i = cur_line();
    if (i < 0)
        return FALSE;
    if (m->message == WM_KEYDOWN) {
        switch (m->wParam) {
        case VK_RIGHT:
            if (!(flags_of(i) & F_KIDS))
                return TRUE;
            if (!(flags_of(i) & F_EXPANDED)) {
                expand(i);
            } else if (i + 1 < (int)SendMessage(tv, LB_GETCOUNT, 0, 0)) {
                SendMessage(tv, LB_SETCURSEL, i + 1, 0);
                on_select();
            }
            InvalidateRect(tv, NULL, TRUE);
            return TRUE;
        case VK_LEFT:
            if (flags_of(i) & F_EXPANDED) {
                collapse(i);
            } else if (i > 0) {
                WORD d = depth_of(i);
                while (i > 0 && depth_of(i) >= d)
                    i--;
                SendMessage(tv, LB_SETCURSEL, i, 0);
                on_select();
            }
            InvalidateRect(tv, NULL, TRUE);
            return TRUE;
        case VK_RETURN:
            if (flags_of(i) & F_EXPANDED)
                collapse(i);
            else
                expand(i);
            InvalidateRect(tv, NULL, TRUE);
            return TRUE;
        case VK_ADD:
            expand(i);
            InvalidateRect(tv, NULL, TRUE);
            return TRUE;
        case VK_SUBTRACT:
            collapse(i);
            InvalidateRect(tv, NULL, TRUE);
            return TRUE;
        }
    }
    return FALSE;
}

/* a click on a +/- box toggles it */
static FARPROC tree_proc_inst;
static FARPROC tree_old;

LRESULT FAR PASCAL _export TreeProc(HWND w, UINT msg, WPARAM wp, LPARAM lp)
{
    if (msg == WM_LBUTTONDOWN || msg == WM_LBUTTONDBLCLK) {
        int top = (int)SendMessage(w, LB_GETTOPINDEX, 0, 0);
        int i = top + HIWORD(lp) / ITEM_H;
        int n = (int)SendMessage(w, LB_GETCOUNT, 0, 0);
        if (i < n) {
            int bx = 2 + INDENT * depth_of(i) - INDENT + 4;
            int x = LOWORD(lp);
            if ((msg == WM_LBUTTONDBLCLK || (x >= bx && x < bx + 9 && depth_of(i) >= 1))
                && (flags_of(i) & F_KIDS)) {
                LRESULT r = 0;
                if (msg == WM_LBUTTONDOWN && !(x >= bx && x < bx + 9))
                    r = CallWindowProc(tree_old, w, msg, wp, lp);
                if (flags_of(i) & F_EXPANDED)
                    collapse(i);
                else
                    expand(i);
                InvalidateRect(w, NULL, TRUE);
                return r;
            }
        }
    }
    if (msg == WM_SETFOCUS || msg == WM_KILLFOCUS)
        InvalidateRect(w, NULL, FALSE);
    return CallWindowProc(tree_old, w, msg, wp, lp);
}

LRESULT FAR PASCAL _export NsProc(HWND w, UINT msg, WPARAM wp, LPARAM lp)
{
    switch (msg) {
    case WM_SIZE:
        cxc = LOWORD(lp);
        cyc = HIWORD(lp);
        place_panes();
        return 0;
    case WM_PAINT: {
        PAINTSTRUCT ps;
        HDC dc = BeginPaint(w, &ps);
        int b = panes_bottom();
        HBRUSH face = CreateSolidBrush(GetSysColor(COLOR_BTNFACE));
        RECT r;
        HFONT of = SelectObject(dc, font);
        GetClientRect(w, &r);
        r.bottom = b;
        FillRect(dc, &r, face);
        DeleteObject(face);
        sunken(dc, 0, 0, split, b);
        sunken(dc, split + SPLIT_W, 0, cxc, b);
        paint_header(dc);
        if (status_on)
            paint_status(dc);
        SelectObject(dc, of);
        EndPaint(w, &ps);
        return 0;
    }
    case WM_SETCURSOR: {
        POINT p;
        GetCursorPos(&p);
        ScreenToClient(w, &p);
        if (dragging || (p.x >= split && p.x < split + SPLIT_W && p.y < panes_bottom())) {
            SetCursor(LoadCursor(NULL, IDC_SIZEWE));
            return TRUE;
        }
        if (p.y >= 2 && p.y < 2 + HEADER_H && abs(p.x - (split + SPLIT_W + 2 + col)) <= 2) {
            SetCursor(LoadCursor(NULL, IDC_SIZEWE));
            return TRUE;
        }
        break;
    }
    case WM_LBUTTONDOWN: {
        int x = LOWORD(lp), y = HIWORD(lp);
        if (dragging == 3) {
            dragging = 0;
            ReleaseCapture();
            place_panes();
            return 0;
        }
        if (x >= split && x < split + SPLIT_W && y < panes_bottom())
            dragging = 1;
        else if (y >= 2 && y < 2 + HEADER_H && abs(x - (split + SPLIT_W + 2 + col)) <= 2)
            dragging = 2;
        if (dragging)
            SetCapture(w);
        return 0;
    }
    case WM_MOUSEMOVE:
        if (dragging == 1 || dragging == 3) {
            split = (int)(short)LOWORD(lp) - SPLIT_W / 2;
            place_panes();
        } else if (dragging == 2) {
            col = (int)(short)LOWORD(lp) - (split + SPLIT_W + 2);
            if (col < 20) col = 20;
            InvalidateRect(w, NULL, FALSE);
            InvalidateRect(lv, NULL, FALSE);
        }
        return 0;
    case WM_LBUTTONUP:
        if (dragging == 1 || dragging == 2) {
            dragging = 0;
            ReleaseCapture();
        }
        return 0;
    case WM_MEASUREITEM:
        ((MEASUREITEMSTRUCT FAR *)lp)->itemHeight = ITEM_H;
        return TRUE;
    case WM_DRAWITEM: {
        DRAWITEMSTRUCT FAR *d = (DRAWITEMSTRUCT FAR *)lp;
        HFONT of = SelectObject(d->hDC, font);
        if (d->CtlID == IDC_NS_TREE)
            draw_tree_item(d);
        else
            draw_value_item(d);
        SelectObject(d->hDC, of);
        return TRUE;
    }
    case WM_COMMAND:
        if (wp == IDC_NS_TREE && HIWORD(lp) == LBN_SELCHANGE) {
            on_select();
        } else if (wp == IDC_NS_LIST && HIWORD(lp) == LBN_DBLCLK) {
            edit_string();
        } else if ((wp == IDC_NS_TREE || wp == IDC_NS_LIST) &&
                   (HIWORD(lp) == LBN_SETFOCUS || HIWORD(lp) == LBN_KILLFOCUS)) {
            InvalidateRect(wp == IDC_NS_TREE ? tv : lv, NULL, FALSE);
        }
        return 0;
    case WM_DESTROY:
        if (rn) {
            SetWindowLong(rn, GWL_WNDPROC, (LONG)rename_old);
            FreeProcInstance(rename_proc_inst);
        }
        if (tv) {
            SetWindowLong(tv, GWL_WNDPROC, (LONG)tree_old);
            FreeProcInstance(tree_proc_inst);
        }
        DeleteObject(bm_computer);
        DeleteObject(bm_closed);
        DeleteObject(bm_open);
        DeleteObject(bm_string);
        DeleteObject(font);
        ns = tv = lv = rn = paths = NULL;
        return 0;
    }
    return DefWindowProc(w, msg, wp, lp);
}

HWND ns_create(HWND frame)
{
    static BOOL registered;
    HDC dc;
    RECT r;
    if (!registered) {
        WNDCLASS wc;
        _fmemset(&wc, 0, sizeof wc);
        wc.lpfnWndProc = NsProc;
        wc.hInstance = g_inst;
        wc.hCursor = LoadCursor(NULL, IDC_ARROW);
        wc.lpszClassName = "W16RegNewStyle";
        if (!RegisterClass(&wc))
            return NULL;
        registered = TRUE;
    }
    split = GetProfileInt("RegEdit", "NSSplit", 200);
    col = GetProfileInt("RegEdit", "NSColumn", 120);
    status_on = GetProfileInt("RegEdit", "NSStatusBar", 1) != 0;
    CheckMenuItem(GetMenu(frame), IDM_NS_STATUS, status_on ? MF_CHECKED : MF_UNCHECKED);
    font = CreateFont(-11, 0, 0, 0, FW_NORMAL, 0, 0, 0, ANSI_CHARSET, OUT_DEFAULT_PRECIS,
                      CLIP_DEFAULT_PRECIS, DEFAULT_QUALITY, VARIABLE_PITCH | FF_SWISS,
                      "MS Sans Serif");
    dc = GetDC(frame);
    bm_computer = make_pic(dc, pic_computer);
    bm_closed = make_pic(dc, pic_closed);
    bm_open = make_pic(dc, pic_open);
    bm_string = make_pic(dc, pic_string);
    ReleaseDC(frame, dc);
    GetClientRect(frame, &r);
    ns = CreateWindow("W16RegNewStyle", "", WS_CHILD | WS_VISIBLE | WS_CLIPCHILDREN,
                      0, 0, r.right, r.bottom, frame, (HMENU)1, g_inst, NULL);
    tv = CreateWindow("ListBox", "", WS_CHILD | WS_VISIBLE | WS_VSCROLL | WS_TABSTOP |
                      LBS_NOTIFY | LBS_OWNERDRAWFIXED | LBS_HASSTRINGS | LBS_NOINTEGRALHEIGHT,
                      0, 0, 10, 10, ns, (HMENU)IDC_NS_TREE, g_inst, NULL);
    lv = CreateWindow("ListBox", "", WS_CHILD | WS_VISIBLE | WS_VSCROLL | WS_TABSTOP |
                      LBS_NOTIFY | LBS_OWNERDRAWFIXED | LBS_HASSTRINGS | LBS_NOINTEGRALHEIGHT,
                      0, 0, 10, 10, ns, (HMENU)IDC_NS_LIST, g_inst, NULL);
    rn = CreateWindow("Edit", "", WS_CHILD | WS_BORDER | ES_AUTOHSCROLL,
                      0, 0, 10, 10, tv, (HMENU)IDC_NS_RENAME, g_inst, NULL);
    paths = CreateWindow("ListBox", "", WS_CHILD, 0, 0, 0, 0, ns, (HMENU)IDC_HIDDEN1, g_inst, NULL);
    SendMessage(rn, WM_SETFONT, (WPARAM)font, 0);
    rename_proc_inst = MakeProcInstance((FARPROC)RenameProc, g_inst);
    rename_old = (FARPROC)SetWindowLong(rn, GWL_WNDPROC, (LONG)rename_proc_inst);
    tree_proc_inst = MakeProcInstance((FARPROC)TreeProc, g_inst);
    tree_old = (FARPROC)SetWindowLong(tv, GWL_WNDPROC, (LONG)tree_proc_inst);
    (void)paths_buf;
    cxc = r.right;
    cyc = r.bottom;
    rebuild();
    place_panes();
    SetFocus(tv);
    return ns;
}
