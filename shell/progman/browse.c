/*
 * browse.c - w16 Program Manager: the Browse dialogs (Program Item
 * Properties and Change Icon).
 *
 * Observed on Windows 3.1: both are COMMDLG's Open dialog, titled
 * "Browse", Help shown, Read Only hidden; the file types are
 *   Program Item Properties: "Programs" (*.exe;*.pif;*.com;*.bat),
 *                            "All Files (*.*)";
 *   Change Icon: "Icon Files" (*.ico;*.exe;*.dll), "Programs (*.exe)",
 *                "Libraries (*.dll)", "Icons (*.ico)", "All Files (*.*)".
 * The chosen file comes back as a full path in capitals.
 *
 * The 3.x build calls GetOpenFileName. Windows 2.x has no COMMDLG, no
 * combo boxes and no owner-drawn list boxes, so the 2.x build has its own
 * dialog of the same layout and control IDs: a list box of files, a tree
 * of folders (pmdirlist), and drop-down lists of file types and drives
 * (pmcombo), all drawn here with our own pictures.
 */
#include "progman.h"
#ifndef W16_WIN2
#include <commdlg.h>
#endif

/* COMMDLG's control IDs (observed), used by the 2.x dialog too */
#define IDC_B_NAMELABEL 0x442
#define IDC_B_NAME      0x480
#define IDC_B_FILES     0x460
#define IDC_B_DIR       0x440
#define IDC_B_DIRS      0x461
#define IDC_B_TYPELABEL 0x441
#define IDC_B_TYPES     0x470
#define IDC_B_DRIVELABEL 0x443
#define IDC_B_DRIVES    0x471
#define IDC_B_HELP      0x40E

/* A DOS path: DOS itself bounds it (64 bytes of directory, a drive, a
   name); OFSTRUCT holds 128. */
#define DOS_PATH        144

/* String resource `ids` as a new string, '|' becoming NUL (filters). */
static LPSTR load_filter(int ids)
{
    LPSTR s = NULL, grown;
    int size = 64, n, i;
    for (;;) {                          /* as large as the string is */
        grown = mem_realloc(s, size);
        if (!grown) {
            mem_free(s);
            return NULL;
        }
        s = grown;
        n = LoadString(g_hinst, ids, s, size);
        if (n < size - 1)
            break;
        size *= 2;
    }
    for (i = 0; i < n; i++)
        if (s[i] == '|')
            s[i] = 0;
    return s;
}

#ifndef W16_WIN2

LPSTR browse_file(HWND owner, int filter_ids)
{
    OPENFILENAME ofn;
    char file[DOS_PATH];
    char title[32];                     /* "Browse" */
    LPSTR filter = load_filter(filter_ids), r = NULL;
    BYTE FAR *z = (BYTE FAR *)&ofn;
    unsigned i;
    if (!filter)
        return NULL;
    LoadString(g_hinst, IDS_BROWSE, title, sizeof title);
    file[0] = 0;
    for (i = 0; i < sizeof ofn; i++)
        z[i] = 0;
    ofn.lStructSize = sizeof ofn;
    ofn.hwndOwner = owner;
    ofn.lpstrFilter = filter;
    ofn.nFilterIndex = 1;
    ofn.lpstrFile = file;
    ofn.nMaxFile = sizeof file;
    ofn.lpstrTitle = title;
    /* UNVERIFIED: whether the original insists the file exists */
    ofn.Flags = OFN_HIDEREADONLY | OFN_SHOWHELP | OFN_PATHMUSTEXIST | OFN_FILEMUSTEXIST;
    if (GetOpenFileName(&ofn)) {
        AnsiUpper(file);
        r = s_dup(file);
    }
    mem_free(filter);
    return r;
}

#else /* W16_WIN2 */

/* ---- DOS ---- */

typedef struct {                        /* the DOS find buffer (DTA) */
    BYTE reserved[21];
    BYTE attr;
    WORD time, date;
    DWORD size;
    char name[13];
} dta;

extern int __cdecl dos_findfirst(const char FAR *spec, int attr, dta FAR *d);
extern int __cdecl dos_findnext(dta FAR *d);
extern int __cdecl dos_getcwd(int drive, char FAR *buf);
extern int __cdecl dos_getdrive(void);
extern int __cdecl dos_drivetype(int drive);

/* Lower case, as COMMDLG shows names. Our own: AnsiLower leaves the
   names unchanged on Windows/386 2.11 (observed). DOS names are ASCII. */
static void lower(char FAR *s)
{
    for (; *s; s++)
        if (*s >= 'A' && *s <= 'Z')
            *s += 'a' - 'A';
}

#define A_DIR           0x10
#define A_LABEL         0x08

/* "c:\dir\sub" of the current directory, in lower case (new string) */
static LPSTR current_dir(void)
{
    char buf[DOS_PATH];
    int d = dos_getdrive();
    buf[0] = (char)('a' + d);
    buf[1] = ':';
    buf[2] = '\\';
    if (dos_getcwd(d + 1, buf + 3))
        buf[3] = 0;
    lower(buf);
    return s_dup(buf);
}

/* ---- drawing our pictures ---- */

static void box(HDC dc, int l, int t, int r, int b, COLORREF fill)
{
    HBRUSH br = CreateSolidBrush(fill), ob = SelectObject(dc, br);
    HPEN op = SelectObject(dc, GetStockObject(BLACK_PEN));
    Rectangle(dc, l, t, r, b);
    SelectObject(dc, ob);
    SelectObject(dc, op);
    DeleteObject(br);
}

/* A folder, 16x12 at (x, y): open (its front leaning back) or closed. */
static void folder(HDC dc, int x, int y, BOOL open)
{
    HBRUSH br = CreateSolidBrush(RGB(255, 255, 0)), ob;
    HPEN op = SelectObject(dc, GetStockObject(BLACK_PEN));
    POINT p[4];
    ob = SelectObject(dc, br);
    Rectangle(dc, x + 1, y, x + 7, y + 3);              /* the tab */
    Rectangle(dc, x, y + 2, x + 15, y + 12);            /* the back */
    if (open) {
        p[0].x = x;       p[0].y = y + 11;
        p[1].x = x + 3;   p[1].y = y + 5;
        p[2].x = x + 16;  p[2].y = y + 5;
        p[3].x = x + 13;  p[3].y = y + 11;
        Polygon(dc, p, 4);
    }
    SelectObject(dc, ob);
    SelectObject(dc, op);
    DeleteObject(br);
}

/* A drive, 16x9 at (x, y): a box with a slot (removable) or a light. */
static void drive(HDC dc, int x, int y, int type)
{
    box(dc, x, y, x + 16, y + 9, RGB(192, 192, 192));
    if (type == 0)
        box(dc, x + 4, y + 3, x + 12, y + 5, RGB(0, 0, 0));
    else
        box(dc, x + 11, y + 5, x + 14, y + 7, type == 2 ? RGB(0, 0, 255) : RGB(0, 255, 0));
}

/* ---- pmcombo: a drop-down list ----
 * Shows the chosen string with a button; a click (or Alt+Down, F4)
 * drops a list box below it; Up and Down choose without dropping. The
 * parent hears CBN_SELCHANGE. With CBS_OWNERDRAWFIXED the parent draws
 * the shown item (WM_DRAWITEM). Strings live in global memory, as many
 * as are added. */
typedef struct {
    LPSTR FAR *str;
    int n, cur;
} combo;

static HWND drop_owner;                 /* the combo whose list is down */
static FARPROC drop_old, drop_thunk;

/* Window words of both controls: the state's handle (0), and its
   address (2), locked for the window's life. Windows 2.x runs programs
   in real mode, where a handle is not a segment. */
#define WW_HANDLE       0
#define WW_STATE        2
#define WW_SIZE         6

static LPVOID state_new(HWND w, WORD size)
{
    HGLOBAL h = GlobalAlloc(GMEM_MOVEABLE | GMEM_ZEROINIT, size);
    LPVOID p = h ? GlobalLock(h) : NULL;
    if (!p) {
        if (h)
            GlobalFree(h);
        return NULL;
    }
    SetWindowWord(w, WW_HANDLE, (WORD)h);
    SetWindowLong(w, WW_STATE, (LONG)p);
    return p;
}

static void state_free(HWND w)
{
    HGLOBAL h = (HGLOBAL)GetWindowWord(w, WW_HANDLE);
    GlobalUnlock(h);
    GlobalFree(h);
    SetWindowWord(w, WW_HANDLE, 0);
    SetWindowLong(w, WW_STATE, 0);
}

static combo FAR *combo_of(HWND w)
{
    return (combo FAR *)GetWindowLong(w, WW_STATE);
}

static void combo_notify(HWND w)
{
    SendMessage(GetParent(w), WM_COMMAND, GetWindowWord(w, GWW_ID),
                MAKELPARAM(w, CBN_SELCHANGE));
}

static void combo_choose(HWND w, int i)
{
    combo FAR *c = combo_of(w);
    if (i < 0 || i >= c->n || i == c->cur)
        return;
    c->cur = i;
    InvalidateRect(w, NULL, TRUE);
    combo_notify(w);
}

static void drop_close(HWND lb, BOOL take)
{
    HWND w = drop_owner;
    int i = (int)SendMessage(lb, LB_GETCURSEL, 0, 0);
    drop_owner = NULL;
    DestroyWindow(lb);
    if (w) {
        SetFocus(w);
        if (take)
            combo_choose(w, i);
    }
}

LRESULT CALLBACK __export DropProc(HWND lb, UINT msg, WPARAM wp, LPARAM lp)
{
    LRESULT r;
    switch (msg) {
    case WM_KEYDOWN:
        if (wp == VK_RETURN || wp == VK_ESCAPE || wp == VK_F4) {
            drop_close(lb, wp != VK_ESCAPE);
            return 0;
        }
        break;
    case WM_LBUTTONUP:
        r = CallWindowProc(drop_old, lb, msg, wp, lp);
        drop_close(lb, TRUE);
        return r;
    case WM_KILLFOCUS:
        if (drop_owner) {
            drop_owner = NULL;
            PostMessage(lb, WM_CLOSE, 0, 0);
        }
        break;
    }
    return CallWindowProc(drop_old, lb, msg, wp, lp);
}

static void combo_drop(HWND w)
{
    combo FAR *c = combo_of(w);
    RECT rc;
    HWND lb;
    HDC dc;
    TEXTMETRIC tm;
    int i, rows, h;
    if (drop_owner || !c->n)
        return;
    GetWindowRect(w, &rc);
    dc = GetDC(w);
    GetTextMetrics(dc, &tm);
    ReleaseDC(w, dc);
    /* as many rows as fit below it, up to every item */
    rows = (GetSystemMetrics(SM_CYSCREEN) - rc.bottom) / tm.tmHeight - 1;
    if (rows > c->n)
        rows = c->n;
    if (rows < 1)
        rows = 1;
    h = rows * tm.tmHeight + 2;
    lb = CreateWindow("ListBox", NULL, WS_POPUP | WS_BORDER | WS_VSCROLL | LBS_NOTIFY,
                      rc.left, rc.bottom, rc.right - rc.left, h, w, NULL, g_hinst, NULL);
    if (!lb)
        return;
    for (i = 0; i < c->n; i++)
        SendMessage(lb, LB_ADDSTRING, 0, (LPARAM)c->str[i]);
    SendMessage(lb, LB_SETCURSEL, c->cur, 0);
    if (!drop_thunk)
        drop_thunk = MakeProcInstance((FARPROC)DropProc, g_hinst);
    drop_old = (FARPROC)GetWindowLong(lb, GWL_WNDPROC);
    SetWindowLong(lb, GWL_WNDPROC, (LONG)drop_thunk);
    drop_owner = w;
    ShowWindow(lb, SW_SHOW);
    SetFocus(lb);
}

/* The drop button: a raised face with a down arrow over a bar. */
static void combo_button(HDC dc, const RECT FAR *r)
{
    int cx = (r->left + r->right) / 2, cy = (r->top + r->bottom) / 2, k;
    HPEN op;
    box(dc, r->left, r->top, r->right, r->bottom, RGB(192, 192, 192));
    op = SelectObject(dc, GetStockObject(BLACK_PEN));
    for (k = 0; k < 4; k++) {           /* the arrow */
        MoveTo(dc, cx - k, cy - 1 + k - 3);
        LineTo(dc, cx + k + 1, cy - 1 + k - 3);
    }
    MoveTo(dc, cx - 1, cy - 6);
    LineTo(dc, cx - 1, cy - 3);
    MoveTo(dc, cx, cy - 6);
    LineTo(dc, cx, cy - 3);
    MoveTo(dc, cx - 3, cy + 3);         /* the bar */
    LineTo(dc, cx + 4, cy + 3);
    SelectObject(dc, op);
}

LRESULT CALLBACK __export ComboProc(HWND w, UINT msg, WPARAM wp, LPARAM lp)
{
    combo FAR *c;
    LPSTR FAR *grown;
    PAINTSTRUCT ps;
    RECT rc, rb;
    int i;

    if (msg == WM_CREATE) {
        if (!state_new(w, sizeof(combo)))
            return -1;
        combo_of(w)->cur = -1;
        return 0;
    }
    if (!GetWindowWord(w, WW_HANDLE))
        return DefWindowProc(w, msg, wp, lp);
    c = combo_of(w);
    switch (msg) {
    case WM_DESTROY:
        for (i = 0; i < c->n; i++)
            mem_free(c->str[i]);
        mem_free((LPSTR)c->str);
        state_free(w);
        return 0;
    case CB_RESETCONTENT:
        for (i = 0; i < c->n; i++)
            mem_free(c->str[i]);
        mem_free((LPSTR)c->str);
        c->str = NULL;
        c->n = 0;
        c->cur = -1;
        InvalidateRect(w, NULL, TRUE);
        return 0;
    case CB_ADDSTRING:
        grown = (LPSTR FAR *)mem_realloc((LPSTR)c->str, (DWORD)(c->n + 1) * sizeof(LPSTR));
        if (!grown)
            return CB_ERRSPACE;
        c->str = grown;
        c->str[c->n] = s_dup((LPCSTR)lp);
        return c->n++;
    case CB_GETCOUNT:
        return c->n;
    case CB_SETCURSEL:
        c->cur = (int)wp < c->n ? (int)wp : -1;
        InvalidateRect(w, NULL, TRUE);
        return c->cur;
    case CB_GETCURSEL:
        return c->cur;
    case CB_GETLBTEXT:
        if ((int)wp < 0 || (int)wp >= c->n)
            return CB_ERR;
        s_cpy((LPSTR)lp, c->str[wp], 0x7FFF);
        return s_len(c->str[wp]);
    case CB_GETLBTEXTLEN:
        return (int)wp >= 0 && (int)wp < c->n ? s_len(c->str[wp]) : CB_ERR;
    case WM_GETDLGCODE:
        return DLGC_WANTARROWS;
    case WM_SETFOCUS:
    case WM_KILLFOCUS:
        InvalidateRect(w, NULL, TRUE);
        return 0;
    case WM_LBUTTONDOWN:
        SetFocus(w);
        combo_drop(w);
        return 0;
    case WM_SYSKEYDOWN:
        if (wp == VK_DOWN) {
            combo_drop(w);
            return 0;
        }
        break;
    case WM_KEYDOWN:
        switch (wp) {
        case VK_F4: combo_drop(w); break;
        case VK_UP: combo_choose(w, c->cur - 1); break;
        case VK_DOWN: combo_choose(w, c->cur + 1); break;
        case VK_HOME: combo_choose(w, 0); break;
        case VK_END: combo_choose(w, c->n - 1); break;
        }
        return 0;
    case WM_PAINT:
        BeginPaint(w, &ps);
        GetClientRect(w, &rc);
        rb = rc;
        rb.left = rb.right - (rc.bottom - rc.top);      /* a square button */
        rc.right = rb.left;
        box(ps.hdc, rc.left, rc.top, rc.right + 1, rc.bottom, GetSysColor(COLOR_WINDOW));
        combo_button(ps.hdc, &rb);
        InflateRect(&rc, -2, -2);
        if (c->cur >= 0) {
            BOOL hi = GetFocus() == w;
            HBRUSH b = CreateSolidBrush(GetSysColor(hi ? PM_COLOR_HIGHLIGHT : COLOR_WINDOW));
            FillRect(ps.hdc, &rc, b);
            DeleteObject(b);
            SetBkMode(ps.hdc, TRANSPARENT);
            SetTextColor(ps.hdc, GetSysColor(hi ? PM_COLOR_HIGHLIGHTTEXT : COLOR_WINDOWTEXT));
            if (GetWindowLong(w, GWL_STYLE) & CBS_OWNERDRAWFIXED) {
                DRAWITEMSTRUCT d;
                d.CtlType = ODT_COMBOBOX;
                d.CtlID = GetWindowWord(w, GWW_ID);
                d.itemID = c->cur;
                d.itemAction = ODA_DRAWENTIRE;
                d.itemState = hi ? ODS_SELECTED : 0;
                d.hwndItem = w;
                d.hDC = ps.hdc;
                d.rcItem = rc;
                d.itemData = (DWORD)c->str[c->cur];
                SendMessage(GetParent(w), WM_DRAWITEM, d.CtlID, (LPARAM)(LPSTR)&d);
            } else {
                TextOut(ps.hdc, rc.left + 1, rc.top, c->str[c->cur], s_len(c->str[c->cur]));
            }
        }
        EndPaint(w, &ps);
        return 0;
    }
    return DefWindowProc(w, msg, wp, lp);
}

/* ---- pmdirlist: the folder tree ----
 * The path from the root (open folders, each one step further in), then
 * the current directory's subdirectories (closed folders); a double
 * click or Enter on one sends LBN_DBLCLK. Items: as many as there are. */
typedef struct {
    LPSTR name;
    int level;
    BOOL open;
} diritem;

typedef struct {
    diritem FAR *it;
    int n, cur, first;
} dirlist;

#define PMDL_ADD        (WM_USER + 100)     /* wp: DL_ITEM(level, open) */
#define DL_ITEM(level, open)    ((WPARAM)((level) | ((open) << 8)))
#define PMDL_GETTEXT    (WM_USER + 101)     /* wp: index; the name */

static dirlist FAR *dl_of(HWND w)
{
    return (dirlist FAR *)GetWindowLong(w, WW_STATE);
}

static int dl_rowh(HWND w)
{
    TEXTMETRIC tm;
    HDC dc = GetDC(w);
    GetTextMetrics(dc, &tm);
    ReleaseDC(w, dc);
    return tm.tmHeight > 14 ? tm.tmHeight : 14;
}

static int dl_rows(HWND w)
{
    RECT rc;
    int r;
    GetClientRect(w, &rc);
    r = rc.bottom / dl_rowh(w);
    return r > 0 ? r : 1;
}

static void dl_scroll(HWND w, dirlist FAR *l)
{
    int max = l->n - dl_rows(w);
    if (max < 0)
        max = 0;
    if (l->first > max)
        l->first = max;
    if (l->first < 0)
        l->first = 0;
    SetScrollRange(w, SB_VERT, 0, max ? max : 1, FALSE);
    SetScrollPos(w, SB_VERT, l->first, TRUE);
}

static void dl_select(HWND w, dirlist FAR *l, int i)
{
    if (i >= l->n)
        i = l->n - 1;
    if (i < 0)
        i = 0;
    l->cur = i;
    if (i < l->first)
        l->first = i;
    else if (i >= l->first + dl_rows(w))
        l->first = i - dl_rows(w) + 1;
    dl_scroll(w, l);
    InvalidateRect(w, NULL, TRUE);
}

LRESULT CALLBACK __export DirListProc(HWND w, UINT msg, WPARAM wp, LPARAM lp)
{
    dirlist FAR *l;
    diritem FAR *grown;
    PAINTSTRUCT ps;
    RECT rc;
    int i, rowh;

    if (msg == WM_CREATE)
        return state_new(w, sizeof(dirlist)) ? 0 : -1;
    if (!GetWindowWord(w, WW_HANDLE))
        return DefWindowProc(w, msg, wp, lp);
    l = dl_of(w);
    switch (msg) {
    case LB_RESETCONTENT:
    case WM_DESTROY:
        for (i = 0; i < l->n; i++)
            mem_free(l->it[i].name);
        mem_free((LPSTR)l->it);
        l->it = NULL;
        l->n = l->cur = l->first = 0;
        if (msg == WM_DESTROY) {
            state_free(w);
        } else {
            dl_scroll(w, l);
            InvalidateRect(w, NULL, TRUE);
        }
        return 0;
    case PMDL_ADD:
        grown = (diritem FAR *)mem_realloc((LPSTR)l->it, (DWORD)(l->n + 1) * sizeof(diritem));
        if (!grown)
            return LB_ERRSPACE;
        l->it = grown;
        l->it[l->n].name = s_dup((LPCSTR)lp);
        l->it[l->n].level = LOBYTE(wp);
        l->it[l->n].open = HIBYTE(wp) != 0;
        dl_scroll(w, l);
        return l->n++;
    case PMDL_GETTEXT:
        return (int)wp >= 0 && (int)wp < l->n ? (LRESULT)l->it[wp].name : 0;
    case LB_GETCOUNT:
        return l->n;
    case LB_SETCURSEL:
        dl_select(w, l, (int)wp);
        return l->cur;
    case LB_GETCURSEL:
        return l->n ? l->cur : LB_ERR;
    case WM_GETDLGCODE:
        return DLGC_WANTARROWS;
    case WM_SETFOCUS:
    case WM_KILLFOCUS:
        InvalidateRect(w, NULL, TRUE);
        return 0;
    case WM_KEYDOWN:
        switch (wp) {
        case VK_UP: dl_select(w, l, l->cur - 1); break;
        case VK_DOWN: dl_select(w, l, l->cur + 1); break;
        case VK_HOME: dl_select(w, l, 0); break;
        case VK_END: dl_select(w, l, l->n - 1); break;
        case VK_RETURN:
            SendMessage(GetParent(w), WM_COMMAND, GetWindowWord(w, GWW_ID),
                        MAKELPARAM(w, LBN_DBLCLK));
            break;
        }
        return 0;
    case WM_LBUTTONDOWN:
    case WM_LBUTTONDBLCLK:
        SetFocus(w);
        i = l->first + HIWORD(lp) / dl_rowh(w);
        if (i < l->n) {
            dl_select(w, l, i);
            if (msg == WM_LBUTTONDBLCLK)
                SendMessage(GetParent(w), WM_COMMAND, GetWindowWord(w, GWW_ID),
                            MAKELPARAM(w, LBN_DBLCLK));
        }
        return 0;
    case WM_VSCROLL:
        switch (wp) {
        case SB_LINEUP: l->first--; break;
        case SB_LINEDOWN: l->first++; break;
        case SB_PAGEUP: l->first -= dl_rows(w); break;
        case SB_PAGEDOWN: l->first += dl_rows(w); break;
        case SB_THUMBPOSITION: l->first = LOWORD(lp); break;
        }
        dl_scroll(w, l);
        InvalidateRect(w, NULL, TRUE);
        return 0;
    case WM_PAINT:
        BeginPaint(w, &ps);
        GetClientRect(w, &rc);
        rowh = dl_rowh(w);
        SetBkMode(ps.hdc, TRANSPARENT);
        for (i = l->first; i < l->n && (i - l->first) * rowh < rc.bottom; i++) {
            int y = (i - l->first) * rowh, x = 3 + l->it[i].level * 8;
            folder(ps.hdc, x, y + (rowh - 12) / 2, l->it[i].open);
            x += 20;
            if (i == l->cur) {
                RECT t;
                HBRUSH b;
                BOOL hi = GetFocus() == w;
                t.left = x - 2;
                t.top = y;
                t.right = x + LOWORD(GetTextExtent(ps.hdc, l->it[i].name,
                                                   s_len(l->it[i].name))) + 2;
                t.bottom = y + rowh;
                b = CreateSolidBrush(GetSysColor(hi ? PM_COLOR_HIGHLIGHT : COLOR_WINDOW));
                FillRect(ps.hdc, &t, b);
                DeleteObject(b);
                SetTextColor(ps.hdc, GetSysColor(hi ? PM_COLOR_HIGHLIGHTTEXT
                                                    : COLOR_WINDOWTEXT));
            } else {
                SetTextColor(ps.hdc, GetSysColor(COLOR_WINDOWTEXT));
            }
            TextOut(ps.hdc, x, y + (rowh - 16 > 0 ? (rowh - 16) / 2 : 0), l->it[i].name,
                    s_len(l->it[i].name));
        }
        EndPaint(w, &ps);
        return 0;
    }
    return DefWindowProc(w, msg, wp, lp);
}

/* ---- the dialog ---- */

static LPSTR b_filter;                  /* name\0pattern\0... */
static LPSTR b_result;
static int b_drives[26];                /* drive types (the alphabet) */
static int b_ndrives;

/* Pattern number i of the filter ("*.exe;*.pif"). */
static LPCSTR filter_pattern(int i)
{
    LPCSTR p = b_filter;
    while (*p) {
        p += s_len(p) + 1;              /* the name */
        if (!i--)
            return p;
        p += s_len(p) + 1;              /* the pattern */
    }
    return "*.*";
}

/* The files of the current directory matching "a;b;c", lower case. */
static void fill_files(HWND dlg, LPCSTR patterns)
{
    HWND lb = GetDlgItem(dlg, IDC_B_FILES);
    LPSTR pats = s_dup(patterns), p, q;
    dta d;
    SendMessage(lb, WM_SETREDRAW, FALSE, 0);
    SendMessage(lb, LB_RESETCONTENT, 0, 0);
    for (p = pats; p && *p; p = q) {
        for (q = p; *q && *q != ';'; q++)
            ;
        if (*q)
            *q++ = 0;
        if (dos_findfirst(p, 0, &d) == 0) {
            do {
                /* a name two patterns match appears once: in the sorted
                   list the first one it prefixes is itself, if there */
                int at;
                char t[13];             /* a DOS 8.3 name */
                lower(d.name);
                at = (int)SendMessage(lb, LB_FINDSTRING, (WPARAM)-1, (LPARAM)(LPSTR)d.name);
                if (at >= 0)
                    SendMessage(lb, LB_GETTEXT, at, (LPARAM)(LPSTR)t);
                if (at < 0 || s_icmp(t, d.name))
                    SendMessage(lb, LB_ADDSTRING, 0, (LPARAM)(LPSTR)d.name);
            } while (dos_findnext(&d) == 0);
        }
    }
    mem_free(pats);
    SendMessage(lb, WM_SETREDRAW, TRUE, 0);
    InvalidateRect(lb, NULL, TRUE);
}

/* The folder tree and the directory line for the current directory. */
static void fill_dirs(HWND dlg)
{
    HWND dl = GetDlgItem(dlg, IDC_B_DIRS);
    LPSTR cwd = current_dir(), p, start;
    char root[4];
    int level = 0, cur;
    dta d;
    if (!cwd)
        return;
    SetDlgItemText(dlg, IDC_B_DIR, cwd);
    SendMessage(dl, LB_RESETCONTENT, 0, 0);
    root[0] = cwd[0];
    root[1] = ':';
    root[2] = '\\';
    root[3] = 0;
    SendMessage(dl, PMDL_ADD, DL_ITEM(0, 1), (LPARAM)(LPSTR)root);
    for (start = p = cwd + 3; *start; start = p) {
        char c;
        while (*p && *p != '\\')
            p++;
        c = *p;
        *p = 0;
        SendMessage(dl, PMDL_ADD, DL_ITEM(++level, 1), (LPARAM)start);
        *p = c;
        if (*p)
            p++;
    }
    cur = level;
    if (dos_findfirst("*.*", A_DIR, &d) == 0) {
        /* sorted: collect, then insert in order (the list keeps order) */
        LPSTR names = NULL;
        DWORD used = 0;
        do {
            if ((d.attr & A_DIR) && d.name[0] != '.') {
                int n = s_len(d.name) + 1;
                LPSTR grown = mem_realloc(names, used + n + 1);
                if (!grown)
                    break;
                names = grown;
                lower(d.name);
                s_cpy(names + used, d.name, n);
                used += n;
                names[used] = 0;
            }
        } while (dos_findnext(&d) == 0);
        while (names && names[0]) {     /* repeatedly the smallest */
            LPSTR q, best = NULL;
            for (q = names; *q; q += s_len(q) + 1)
                if (*q != 1 && (!best || s_icmp(q, best) < 0))
                    best = q;
            if (!best)
                break;
            SendMessage(dl, PMDL_ADD, DL_ITEM(level + 1, 0), (LPARAM)best);
            best[0] = 1;                /* taken */
        }
        mem_free(names);
    }
    SendMessage(dl, LB_SETCURSEL, cur, 0);
    mem_free(cwd);
}

static void fill_drives(HWND dlg)
{
    HWND cb = GetDlgItem(dlg, IDC_B_DRIVES);
    int d, cur = dos_getdrive();
    char text[20];                      /* "c: " and an 11-letter label */
    dta f;
    SendMessage(cb, CB_RESETCONTENT, 0, 0);
    b_ndrives = 0;
    for (d = 0; d < 26; d++) {
        int t = dos_drivetype(d + 1);
        char spec[7];
        if (t < 0)
            continue;
        text[0] = (char)('a' + d);
        text[1] = ':';
        text[2] = 0;
        /* the volume label; not of removable drives (no disk spun up) */
        spec[0] = (char)('A' + d);
        s_cpy(spec + 1, ":\\*.*", 6);
        if (t != 0 && dos_findfirst(spec, A_LABEL, &f) == 0 && (f.attr & A_LABEL)) {
            int i, j = 3;
            text[2] = ' ';
            for (i = 0; f.name[i] && j < (int)sizeof text - 1; i++)
                if (f.name[i] != '.')
                    text[j++] = f.name[i];
            text[j] = 0;
            lower(text);
        }
        b_drives[SendMessage(cb, CB_ADDSTRING, 0, (LPARAM)(LPSTR)text)] = t;
        if (d == cur)
            SendMessage(cb, CB_SETCURSEL, b_ndrives, 0);
        b_ndrives++;
    }
}

static void refresh(HWND dlg)
{
    int t = (int)SendDlgItemMessage(dlg, IDC_B_TYPES, CB_GETCURSEL, 0, 0);
    fill_dirs(dlg);
    fill_files(dlg, filter_pattern(t < 0 ? 0 : t));
}

static BOOL has_wild(LPCSTR s)
{
    for (; *s; s++)
        if (*s == '*' || *s == '?')
            return TRUE;
    return FALSE;
}

/* OK with the name typed: a pattern, a directory, or the file. */
static BOOL take_name(HWND dlg)
{
    LPSTR name = dlg_text(dlg, IDC_B_NAME);
    OFSTRUCT of;
    BOOL done = FALSE;
    if (!name)
        return FALSE;
    if (!name[0]) {
        mem_free(name);
        return FALSE;
    }
    if (name[1] == ':' && !name[2]) {   /* "a:": that drive */
        dos_setdrive((name[0] | 0x20) - 'a');
        refresh(dlg);
    } else if (has_wild(name)) {
        LPSTR slash = NULL, p;
        for (p = name; *p; p++)
            if (*p == '\\' || *p == ':')
                slash = p;
        if (slash) {                    /* a directory part: go there */
            char c = slash[1];
            if (slash[0] == ':')
                dos_setdrive((name[0] | 0x20) - 'a');
            slash[1] = 0;
            if (slash[0] == '\\' && slash > name)
                dos_chdir(name);
            slash[1] = c;
            SetDlgItemText(dlg, IDC_B_NAME, slash + 1);
            fill_dirs(dlg);
            fill_files(dlg, slash + 1);
        } else {
            fill_files(dlg, name);
        }
    } else if (dos_chdir(name) == 0) {  /* a directory */
        if (name[1] == ':')
            dos_setdrive((name[0] | 0x20) - 'a');
        refresh(dlg);
        SetDlgItemText(dlg, IDC_B_NAME, filter_pattern(
            (int)SendDlgItemMessage(dlg, IDC_B_TYPES, CB_GETCURSEL, 0, 0)));
    } else if (OpenFile(name, &of, OF_EXIST) != HFILE_ERROR) {
        AnsiUpper(of.szPathName);
        b_result = s_dup(of.szPathName);
        done = TRUE;
    } else {
        message(dlg, IDS_FILENOTFOUND, name);
    }
    mem_free(name);
    return done;
}

BOOL CALLBACK __export BrowseDlgProc(HWND dlg, UINT msg, WPARAM wp, LPARAM lp)
{
    LPCSTR p;
    int i;
    switch (msg) {
    case WM_DRAWITEM: {                 /* the shown drive: picture, text */
        const DRAWITEMSTRUCT FAR *d = (const DRAWITEMSTRUCT FAR *)lp;
        if (d->CtlID != IDC_B_DRIVES)
            return FALSE;
        drive(d->hDC, d->rcItem.left + 1, d->rcItem.top + (d->rcItem.bottom - d->rcItem.top - 9) / 2,
              b_drives[d->itemID]);
        TextOut(d->hDC, d->rcItem.left + 22, d->rcItem.top, (LPCSTR)d->itemData,
                s_len((LPCSTR)d->itemData));
        return TRUE;
    }
    case WM_INITDIALOG:
        for (p = b_filter, i = 0; *p; i++) {
            SendDlgItemMessage(dlg, IDC_B_TYPES, CB_ADDSTRING, 0, (LPARAM)p);
            p += s_len(p) + 1;
            p += s_len(p) + 1;
        }
        SendDlgItemMessage(dlg, IDC_B_TYPES, CB_SETCURSEL, 0, 0);
        SetDlgItemText(dlg, IDC_B_NAME, filter_pattern(0));
        fill_drives(dlg);
        refresh(dlg);
        SendDlgItemMessage(dlg, IDC_B_NAME, EM_SETSEL, 0, MAKELPARAM(0, 0x7FFF));
        return TRUE;
    case WM_COMMAND:
        switch (wp) {
        case IDC_B_FILES:
            if (HIWORD(lp) == LBN_SELCHANGE || HIWORD(lp) == LBN_DBLCLK) {
                char t[13];
                i = (int)SendDlgItemMessage(dlg, IDC_B_FILES, LB_GETCURSEL, 0, 0);
                if (i >= 0) {
                    SendDlgItemMessage(dlg, IDC_B_FILES, LB_GETTEXT, i, (LPARAM)(LPSTR)t);
                    SetDlgItemText(dlg, IDC_B_NAME, t);
                    if (HIWORD(lp) == LBN_DBLCLK && take_name(dlg))
                        EndDialog(dlg, IDOK);
                }
            }
            return TRUE;
        case IDC_B_DIRS:
            if (HIWORD(lp) == LBN_DBLCLK) {
                /* the path down to the chosen folder */
                int at = (int)SendDlgItemMessage(dlg, IDC_B_DIRS, LB_GETCURSEL, 0, 0), k;
                LPSTR path = NULL;
                for (k = 0; k <= at; k++) {
                    LPCSTR nm = (LPCSTR)SendDlgItemMessage(dlg, IDC_B_DIRS, PMDL_GETTEXT, k, 0);
                    /* only the chain of open folders, then the chosen one */
                    if (k < at && k > 0) {
                        HWND dl = GetDlgItem(dlg, IDC_B_DIRS);
                        if (!dl_of(dl)->it[k].open)
                            continue;
                    }
                    if (k > 1)
                        s_append(&path, "\\");
                    s_append(&path, nm);
                }
                if (path) {
                    dos_chdir(path);
                    mem_free(path);
                }
                refresh(dlg);
            }
            return TRUE;
        case IDC_B_TYPES:
            if (HIWORD(lp) == CBN_SELCHANGE) {
                i = (int)SendDlgItemMessage(dlg, IDC_B_TYPES, CB_GETCURSEL, 0, 0);
                SetDlgItemText(dlg, IDC_B_NAME, filter_pattern(i));
                fill_files(dlg, filter_pattern(i));
            }
            return TRUE;
        case IDC_B_DRIVES:
            if (HIWORD(lp) == CBN_SELCHANGE) {
                char t[20];
                i = (int)SendDlgItemMessage(dlg, IDC_B_DRIVES, CB_GETCURSEL, 0, 0);
                SendDlgItemMessage(dlg, IDC_B_DRIVES, CB_GETLBTEXT, i, (LPARAM)(LPSTR)t);
                dos_setdrive(t[0] - 'a');
                refresh(dlg);
            }
            return TRUE;
        case IDOK:
            if (take_name(dlg))
                EndDialog(dlg, IDOK);
            return TRUE;
        case IDCANCEL:
            EndDialog(dlg, IDCANCEL);
            return TRUE;
        case IDC_B_HELP:
            WinHelp(dlg, "PROGMAN.HLP", HELP_CONTENTS, 0);
            return TRUE;
        }
        break;
    }
    return FALSE;
}

LPSTR browse_file(HWND owner, int filter_ids)
{
    b_filter = load_filter(filter_ids);
    b_result = NULL;
    if (!b_filter)
        return NULL;
    run_dialog(owner, (FARPROC)BrowseDlgProc, IDD_BROWSE);
    mem_free(b_filter);
    b_filter = NULL;
    return b_result;
}

BOOL browse_register(void)
{
    WNDCLASS wc;
    wc.style = CS_DBLCLKS;
    wc.lpfnWndProc = ComboProc;
    wc.cbClsExtra = 0;
    wc.cbWndExtra = WW_SIZE;
    wc.hInstance = g_hinst;
    wc.hIcon = NULL;
    wc.hCursor = LoadCursor(NULL, IDC_ARROW);
    wc.hbrBackground = (HBRUSH)(COLOR_WINDOW + 1);
    wc.lpszMenuName = NULL;
    wc.lpszClassName = "pmcombo";
    if (!RegisterClass(&wc))
        return FALSE;
    wc.lpfnWndProc = DirListProc;
    wc.lpszClassName = "pmdirlist";
    return RegisterClass(&wc) != 0;
}

#endif /* W16_WIN2 */
