/*
 * search.c - w16 File Manager: File > Search and the Search Results
 * window.
 *
 * Observed (obs WF3, WF8): the Search box starts from the current
 * directory with "Search All Subdirectories" checked. The results are an
 * MDI child of class WFS_Search titled "Search Results: " and the start
 * directory with the pattern ("Search Results: C:\*.TXT"), made at MDI's
 * default place, holding an owner-drawn sorted ListBox with strings (ID
 * 6; LBS_NOTIFY | LBS_SORT | LBS_OWNERDRAWFIXED | LBS_HASSTRINGS |
 * LBS_NOINTEGRALHEIGHT | LBS_WANTKEYBOARDINPUT | LBS_EXTENDEDSEL,
 * WS_BORDER) at -1,-1 two pixels larger than the client: full paths, the
 * first selected, drawn like the file list (picture, lower case). The
 * status bar says "n file(s) found" in its first box only. There is one
 * such window: a new search uses it again. Enter or a double click opens
 * the file.
 */
#include "winfile.h"

#define IDD_PATTERN     201
#define IDD_START       101
#define IDD_SUBDIRS     232
#define IDD_HELP        254

static char cls_search[] = "WFS_Search";
static HWND search_win, search_lb;
int g_found;

static char s_pattern[PATHLEN], s_start[PATHLEN];
static BOOL s_subdirs = TRUE;
static LPDIRWIN s_dir;

BOOL is_search(HWND w)
{
    return w && w == search_win;
}

/* every file under dir matching the pattern */
static void search_dir(LPCSTR dir, LPCSTR pat, BOOL sub)
{
    char spec[PATHLEN], path[PATHLEN];
    FINDBUF f;
    s_cpy(spec, dir, sizeof spec);
    path_add(spec, pat, sizeof spec);
    if (dos_findfirst(spec, ATTR_DIR | ATTR_READONLY | ATTR_ARCHIVE, &f) == 0) {
        do {
            if (f.name[0] == '.' || (f.attr & ATTR_VOLUME))
                continue;
            s_cpy(path, dir, sizeof path);
            path_add(path, f.name, sizeof path);
            SendMessage(search_lb, LB_ADDSTRING, 0, (LPARAM)(LPSTR)path);
            g_found++;
        } while (dos_findnext(&f) == 0);
    }
    if (!sub)
        return;
    s_cpy(spec, dir, sizeof spec);
    path_add(spec, "*.*", sizeof spec);
    if (dos_findfirst(spec, ATTR_DIR, &f) == 0) {
        do {
            if (!(f.attr & ATTR_DIR) || f.name[0] == '.')
                continue;
            s_cpy(path, dir, sizeof path);
            path_add(path, f.name, sizeof path);
            search_dir(path, pat, TRUE);
        } while (dos_findnext(&f) == 0);
    }
}

static void run_search(void)
{
    char t[PATHLEN + 40], whole[PATHLEN];
    HCURSOR oc;
    s_cpy(whole, s_start, sizeof whole);
    path_add(whole, s_pattern, sizeof whole);
    wsprintf(t, load_str(IDS_SEARCHTITLE), (LPSTR)whole);
    if (!search_win) {
        MDICREATESTRUCT mc;
        mc.szClass = cls_search;
        mc.szTitle = t;
        mc.hOwner = g_hinst;
        mc.x = mc.y = mc.cx = mc.cy = CW_USEDEFAULT;
        mc.style = 0;
        mc.lParam = 0;
        search_win = (HWND)SendMessage(g_mdi, WM_MDICREATE, 0, (LPARAM)(LPMDICREATESTRUCT)&mc);
        if (!search_win)
            return;
    } else {
        SetWindowText(search_win, t);
        SendMessage(g_mdi, WM_MDIACTIVATE, (WPARAM)search_win, 0);
    }
    oc = SetCursor(LoadCursor(NULL, IDC_WAIT));
    SendMessage(search_lb, WM_SETREDRAW, FALSE, 0);
    SendMessage(search_lb, LB_RESETCONTENT, 0, 0);
    g_found = 0;
    search_dir(s_start, s_pattern, s_subdirs);
    if (g_found)
        SendMessage(search_lb, LB_SETSEL, TRUE, 0);
    SendMessage(search_lb, WM_SETREDRAW, TRUE, 0);
    InvalidateRect(search_lb, NULL, TRUE);
    SetCursor(oc);
    status_update();
    if (!g_found) {
        MessageBox(g_frame, load_str(IDS_NOTFOUND), load_str(IDS_SEARCHBOX), MB_OK | MB_ICONINFORMATION);
        DestroyWindow(search_win);
    }
}

BOOL CALLBACK _export SearchDlgProc(HWND h, UINT msg, WPARAM wp, LPARAM lp)
{
    (void)lp;
    switch (msg) {
    case WM_INITDIALOG:
        SetDlgItemText(h, IDD_PATTERN, s_pattern);
        SetDlgItemText(h, IDD_START, s_dir ? s_dir->path : "C:\\");
        CheckDlgButton(h, IDD_SUBDIRS, s_subdirs);
        return TRUE;
    case WM_COMMAND:
        switch (wp) {
        case IDOK:
            GetDlgItemText(h, IDD_PATTERN, s_pattern, sizeof s_pattern);
            GetDlgItemText(h, IDD_START, s_start, sizeof s_start);
            s_subdirs = IsDlgButtonChecked(h, IDD_SUBDIRS);
            if (!s_pattern[0])
                s_cpy(s_pattern, "*.*", sizeof s_pattern);
            s_upper(s_pattern);
            s_upper(s_start);
            EndDialog(h, 1);
            run_search();
            return TRUE;
        case IDCANCEL:
            EndDialog(h, 0);
            return TRUE;
        case IDD_HELP:
            WinHelp(g_frame, load_str(IDS_HELPFILE), HELP_CONTEXT, 5000L + IDM_SEARCH);
            return TRUE;
        }
        break;
    }
    return FALSE;
}

void file_search(LPDIRWIN d)
{
    s_dir = d;
    /* the pattern: the selected file's, as 3.1 starts with ("*.TTF"
       with ARIALI.TTF selected: obs WF3) */
    if (d) {
        HGLOBAL h = selected_text(d, FALSE);
        char name[PATHLEN];
        LPCSTR p;
        if (h) {
            p = (LPCSTR)GlobalLock(h);
            if (next_name(&p, name, sizeof name) && *s_ext(name)) {
                s_cpy(s_pattern, "*.", sizeof s_pattern);
                s_cat(s_pattern, s_ext(name), sizeof s_pattern);
            } else if (!s_pattern[0]) {
                s_cpy(s_pattern, "*.*", sizeof s_pattern);
            }
            GlobalUnlock(h);
            GlobalFree(h);
        }
    }
    run_dialog("DLG_SEARCH", (FARPROC)SearchDlgProc);
}

static void draw_item(DRAWITEMSTRUCT FAR *di)
{
    char t[PATHLEN];
    HDC dc = di->hDC;
    RECT r = di->rcItem;
    BOOL sel = (di->itemState & ODS_SELECTED) != 0;
    BOOL filled = sel && GetFocus() == di->hwndItem;
    int a, pic;
    HFONT of;
    if ((int)di->itemID < 0)
        return;
    SendMessage(di->hwndItem, LB_GETTEXT, di->itemID, (LPARAM)(LPSTR)t);
    a = dos_getattr(t);
    pic = (a >= 0 && (a & ATTR_DIR)) ? BM_CLOSED :
          (a >= 0 && (a & ATTR_HS)) ? BM_SYSFILE :
          is_program(t) ? BM_PROGRAM : is_document(t) ? BM_DOC : BM_FILE;
    if (g_lower)
        s_lower(t);
    of = SelectObject(dc, g_font);
    fill_sys(dc, &r, filled ? COLOR_HIGHLIGHT : COLOR_WINDOW);
    SetTextColor(dc, GetSysColor(filled ? COLOR_HIGHLIGHTTEXT : COLOR_WINDOWTEXT));
    SetBkMode(dc, TRANSPARENT);
    bm_draw(dc, r.left + 2, r.top, pic, filled);   /* as with details (WF10) */
    TextOut(dc, r.left + 19, r.top + (g_dyitem - g_dytext + 1) / 2, t, s_len(t));
    SelectObject(dc, of);
    if (sel && !filled) {
        HBRUSH hb = CreateSolidBrush(GetSysColor(COLOR_HIGHLIGHT));
        InflateRect(&r, -1, 0);
        FrameRect(dc, &r, hb);
        DeleteObject(hb);
        r = di->rcItem;
    }
    if (di->itemState & ODS_FOCUS) {
        SetTextColor(dc, GetSysColor(filled ? COLOR_HIGHLIGHTTEXT : COLOR_WINDOWTEXT));
        SetBkColor(dc, GetSysColor(filled ? COLOR_HIGHLIGHT : COLOR_WINDOW));
        DrawFocusRect(dc, &r);
    }
}

/* the selection as "C:\A\B.TXT C:\C.TXT " (file commands from here) */
HGLOBAL search_selected(void)
{
    int n = (int)SendMessage(search_lb, LB_GETCOUNT, 0, 0), i;
    HGLOBAL h = GlobalAlloc(GMEM_MOVEABLE | GMEM_ZEROINIT, (DWORD)(n + 1) * PATHLEN);
    LPSTR p;
    char t[PATHLEN];
    if (!h)
        return NULL;
    p = (LPSTR)GlobalLock(h);
    for (i = 0; i < n; i++)
        if (SendMessage(search_lb, LB_GETSEL, i, 0) > 0) {
            SendMessage(search_lb, LB_GETTEXT, i, (LPARAM)(LPSTR)t);
            lstrcat(p, t);
            lstrcat(p, " ");
        }
    GlobalUnlock(h);
    return h;
}

static void open_selected(void)
{
    char t[PATHLEN];
    int i = (int)SendMessage(search_lb, LB_GETCARETINDEX, 0, 0);
    if (i < 0 || SendMessage(search_lb, LB_GETTEXT, i, (LPARAM)(LPSTR)t) == LB_ERR)
        return;
    if ((UINT)ShellExecute(g_frame, NULL, t, NULL, NULL, SW_SHOWNORMAL) > 32 && g_minonrun)
        ShowWindow(g_frame, SW_MINIMIZE);
}

LRESULT CALLBACK _export SearchWndProc(HWND w, UINT msg, WPARAM wp, LPARAM lp)
{
    switch (msg) {
    case WM_CREATE:
        search_win = w;
        search_lb = CreateWindow("ListBox", NULL,
                                 WS_CHILD | WS_VISIBLE | WS_BORDER | LBS_NOTIFY | LBS_SORT |
                                 LBS_OWNERDRAWFIXED | LBS_HASSTRINGS | LBS_NOINTEGRALHEIGHT |
                                 LBS_WANTKEYBOARDINPUT | LBS_EXTENDEDSEL,
                                 0, 0, 0, 0, w, (HMENU)ID_DIRLIST, g_hinst, NULL);
        return 0;
    case WM_SIZE:
        if (wp != SIZE_MINIMIZED)
            MoveWindow(search_lb, -1, -1, LOWORD(lp) + 2, HIWORD(lp) + 2, TRUE);
        break;
    case WM_SETFOCUS:
        SetFocus(search_lb);
        break;
    case WM_MEASUREITEM:
        ((MEASUREITEMSTRUCT FAR *)lp)->itemHeight = g_dyitem;
        return TRUE;
    case WM_DRAWITEM:
        draw_item((DRAWITEMSTRUCT FAR *)lp);
        return TRUE;
    case WM_COMMAND:
        if (wp == ID_DIRLIST) {
            if (HIWORD(lp) == LBN_DBLCLK)
                open_selected();
            else if (HIWORD(lp) == LBN_SETFOCUS || HIWORD(lp) == LBN_KILLFOCUS)
                InvalidateRect(search_lb, NULL, FALSE);
            return 0;
        }
        break;
    case WM_VKEYTOITEM:
        if (wp == VK_RETURN) {
            open_selected();
            return -2;
        }
        return -1;
    case WM_MDIACTIVATE:
        status_update();
        break;
    case WM_PAINT:
        if (IsIconic(w)) {
            PAINTSTRUCT ps;
            HDC dc = BeginPaint(w, &ps);
            DrawIcon(dc, 0, 0, LoadIcon(g_hinst, MAKEINTRESOURCE(IDI_DIR)));
            EndPaint(w, &ps);
            return 0;
        }
        break;
    case WM_DESTROY:
        search_win = NULL;
        search_lb = NULL;
        g_found = 0;
        status_update();
        break;
    }
    return DefMDIChildProc(w, msg, wp, lp);
}

BOOL search_register(void)
{
    WNDCLASS wc;
    wc.style = 0;
    wc.lpfnWndProc = SearchWndProc;
    wc.cbClsExtra = 0;
    wc.cbWndExtra = 0;
    wc.hInstance = g_hinst;
    wc.hIcon = NULL;
    wc.hCursor = LoadCursor(NULL, IDC_ARROW);
    wc.hbrBackground = (HBRUSH)(COLOR_WINDOW + 1);
    wc.lpszMenuName = NULL;
    wc.lpszClassName = cls_search;
    return RegisterClass(&wc);
}
