/*
 * main.c - w16 File Manager: the frame, the MDI client, the status bar,
 * the menus and their commands, WinMain.
 *
 * Observed (WF1): the frame is class WFS_Frame (no class style, the
 * application-workspace brush), title "File Manager", WS_OVERLAPPEDWINDOW
 * | WS_CLIPCHILDREN | WS_CLIPSIBLINGS; first start: at 0,0, 640 x 480 on
 * VGA, not maximised. Its MDIClient (ID 1, WS_CLIPCHILDREN | WS_BORDER)
 * is at -1,-1, two pixels wider than the client, down to the status bar
 * (a pixel over it); the Window menu's list starts at ID 1006. The status
 * bar is drawn in the frame's client: 22 pixels high, button-face
 * coloured, two sunken boxes 276 pixels wide at x 8 and 292, from 2 to 19
 * pixels down, their text 4 pixels in and 5 down: the drive's free and
 * total space (or, with the file list focused, the selection) and the
 * directory's file count and bytes.
 */
#include "winfile.h"

HINSTANCE g_hinst;
HWND g_frame, g_mdi;
HFONT g_font;
static HFONT status_font;              /* MS Sans Serif 10 (WF01) */
int g_dytext = 13, g_dxtext = 5, g_dyitem = 16;
BOOL g_statusbar = TRUE, g_minonrun, g_lower = TRUE, g_saveonexit = TRUE;
BOOL g_confdel = TRUE, g_confsubdel = TRUE, g_confreplace = TRUE, g_confmouse = TRUE,
     g_confdisk = TRUE;
int g_dystatus = 22;
DWORD g_free_unknown;                   /* bit n: drive 'A' + n changed out
                                           of sight (main.c file_changed) */

static char cls_frame[] = "WFS_Frame";
static HACCEL accel;

BOOL ini_frame(RECT FAR *r, int *show);

/* ---- the file system changing under us: KERNEL's FileCdr (KERNEL.130;
   Schulman ch. 5: File Manager is its one user; tests/apps/tk64.c has
   KERNEL's side). KERNEL calls cdr_proc, in whichever task made the
   change, after each successful create, delete, rename, attribute change
   or directory change, with the DOS function and a full path; cdr_proc
   keeps them and posts one undocumented WM_FILESYSCHANGE (0034h,
   Schulman ch. 7) for any number of them. Observed on 3.1 (obs WF20,
   scenario WF20; the boot-test case w16winfile's other scenarios): a
   file another program makes appears in a window showing its directory,
   but a file that was there and is written again leaves the listing as
   it was (its old size: WF01, where SNAP and WINFILE.INI change
   C:\WINDOWS); with the Search Results window up a change on its drive
   asks to update it, and a change made while that box is up asks again
   after it. So each change is applied to the windows showing its
   directory: a new name rereads them, as does a name that goes (delete,
   rename) or a directory made or removed (the trees too). UNVERIFIED:
   what 3.1 does on an attribute change (nothing here), a rename into a
   window's directory, and whether it rereads or inserts. */
#define WM_FILESYSCHANGE    0x0034
#define CDR_MAX             8
DWORD FAR PASCAL FileCdr(FARPROC proc);
static FARPROC cdr_thunk;
static BOOL cdr_posted, cdr_lost;
static int cdr_n;
static WORD cdr_code[CDR_MAX];
static char cdr_path[CDR_MAX][PATHLEN];

/* runs in the other task, on its stack: only plain stores and a post
   (__loadds: -zWs callbacks take DS from SS, which is that task's here) */
void FAR PASCAL _export __loadds cdr_proc(WORD code, LPSTR path)
{
    if (cdr_n < CDR_MAX) {
        int i;
        for (i = 0; i < PATHLEN - 1 && path[i]; i++)
            cdr_path[cdr_n][i] = path[i];
        cdr_path[cdr_n][i] = 0;
        cdr_code[cdr_n++] = code;
    } else {
        cdr_lost = TRUE;                /* too many: reread everything */
    }
    if (!cdr_posted && g_frame) {
        cdr_posted = TRUE;
        PostMessage(g_frame, WM_FILESYSCHANGE, 0, 0);
    }
}

/* is `name` in the window's list? */
static BOOL listed(LPDIRWIN d, LPCSTR name)
{
    int n = (int)SendMessage(d->dirlb, LB_GETCOUNT, 0, 0), i;
    for (i = 0; i < n; i++) {
        LPDENTRY e = dir_item(d, i);
        if (e && s_icmp(e->name, name) == 0)
            return TRUE;
    }
    return FALSE;
}

static void reread(LPDIRWIN d)
{
    HGLOBAL sel = dir_sel_save(d);
    dir_read(d);
    dir_sel_restore(d, sel);
    status_set(d, ST_SELECTED);         /* the selection's line after it (WF21:
                                           "Selected 0 file(s) (0 bytes)") */
}

/* one change: the windows showing its directory */
static void file_changed(WORD code, LPSTR path)
{
    char dir[PATHLEN];
    LPSTR name = path, p;
    BYTE f = HIBYTE(code);
    BOOL isdir = f == 0x39 || f == 0x3A, shown = FALSE;
    HWND w;
    for (p = path; *p; p++)
        if (*p == '\\' || *p == '/')
            name = p + 1;
    s_cpy(dir, path, sizeof dir);
    if (name - path < PATHLEN)
        dir[name - path > 3 ? name - path - 1 : name - path] = 0;
    for (w = GetWindow(g_mdi, GW_CHILD); w; w = GetWindow(w, GW_HWNDNEXT)) {
        LPDIRWIN d = dirwin_of(w);
        if (!d)
            continue;
        if (isdir && d->panes != PANES_DIR && (d->path[0] | 0x20) == (path[0] | 0x20))
            tree_refresh(d);
        if (s_icmp(d->path, dir) != 0)
            continue;
        shown = TRUE;
        if (f == 0x41 || f == 0x56 || f == 0x3A) {
            if (listed(d, name))
                reread(d);
        } else if (f != 0x43 && !listed(d, name)) {
            reread(d);
        }
    }
    /* a change no window shows: the drive's free bytes are not known
       until it is read again (FM_GETDRIVEINFO: FFFFFFFFh; WF21) */
    if (!shown && ((path[0] | 0x20) >= 'a' && (path[0] | 0x20) <= 'z'))
        g_free_unknown |= 1UL << ((path[0] | 0x20) - 'a');
    search_changed(path[0]);
}

static void files_changed(void)
{
    WORD code[CDR_MAX];                 /* (not static: the question box */
    char path[CDR_MAX][PATHLEN];        /* below brings the next ones in) */
    int n = cdr_n, i;
    BOOL lost = cdr_lost;
    for (i = 0; i < n; i++) {
        code[i] = cdr_code[i];
        s_cpy(path[i], cdr_path[i], PATHLEN);
    }
    cdr_n = 0;
    cdr_lost = FALSE;
    cdr_posted = FALSE;
    if (lost) {
        refresh_all();
        search_changed(path[0][0]);
        return;
    }
    for (i = 0; i < n; i++)
        file_changed(code[i], path[i]);
    status_update();
}

LPSTR load_str(UINT ids)
{
    static char buf[256];
    if (!LoadString(g_hinst, ids, buf, sizeof buf))
        buf[0] = 0;
    return buf;
}

HWND active_dirwin(void)
{
    HWND w = (HWND)LOWORD(SendMessage(g_mdi, WM_MDIGETACTIVE, 0, 0));
    return dirwin_of(w) ? w : NULL;
}

/* ---- the font and the sizes that follow from it ---- */

static void make_font(void)
{
    LOGFONT lf;
    TEXTMETRIC tm;
    HDC dc;
    HFONT of;
    if (g_font)
        DeleteObject(g_font);
    ini_font(&lf);
    g_font = CreateFontIndirect(&lf);
    dc = GetDC(NULL);
    of = SelectObject(dc, g_font);
    GetTextMetrics(dc, &tm);
    SelectObject(dc, of);
    ReleaseDC(NULL, dc);
    g_dytext = tm.tmHeight;
    g_dxtext = tm.tmAveCharWidth;
    g_dyitem = g_dytext > CY_BM ? g_dytext : CY_BM;
    if (!status_font) {
        LOGFONT sf;
        LPBYTE b = (LPBYTE)&sf;
        int i;
        for (i = 0; i < (int)sizeof sf; i++)
            b[i] = 0;
        dc = GetDC(NULL);
        sf.lfHeight = -MulDiv(10, GetDeviceCaps(dc, LOGPIXELSY), 72);
        ReleaseDC(NULL, dc);
        sf.lfWeight = FW_NORMAL;
        sf.lfPitchAndFamily = VARIABLE_PITCH | FF_SWISS;
        s_cpy(sf.lfFaceName, "MS Sans Serif", sizeof sf.lfFaceName);
        status_font = CreateFontIndirect(&sf);
        dc = GetDC(NULL);
        of = SelectObject(dc, status_font);
        GetTextMetrics(dc, &tm);
        SelectObject(dc, of);
        ReleaseDC(NULL, dc);
        g_dystatus = tm.tmHeight + 6;
    }
}

/* ---- the status bar ---- */

static void status_rects(RECT FAR *bar, RECT FAR *left, RECT FAR *right)
{
    RECT r;
    int w;
    GetClientRect(g_frame, &r);
    bar->left = 0;
    bar->right = r.right;
    bar->bottom = r.bottom;
    bar->top = r.bottom - g_dystatus;
    /* 276 on VGA whatever the window's width (WF5 E10, E11) */
    w = (GetSystemMetrics(SM_CXSCREEN) - 88) / 2;
    left->left = 8;
    left->right = left->left + w;
    left->top = bar->top + 2;
    left->bottom = bar->top + g_dystatus - 2;
    *right = *left;
    right->left = left->right + 8;
    right->right = right->left + w;
}

void status_invalidate(void)
{
    RECT bar, l, r;
    if (!g_statusbar)
        return;
    status_rects(&bar, &l, &r);
    InvalidateRect(g_frame, &bar, FALSE);
}

void status_update(void)
{
    status_invalidate();
}

void status_set(LPDIRWIN d, int what)
{
    d->stleft = what;
    status_invalidate();
}

static void sunken(HDC dc, RECT FAR *r)
{
    HPEN gray = CreatePen(PS_SOLID, 1, GetSysColor(COLOR_BTNSHADOW));
    HPEN white = CreatePen(PS_SOLID, 1, GetSysColor(COLOR_BTNHIGHLIGHT));
    HPEN op = SelectObject(dc, gray);
    /* the light edges take the corners (WF01) */
    MoveTo(dc, r->left, r->bottom - 2);
    LineTo(dc, r->left, r->top);
    LineTo(dc, r->right - 1, r->top);
    SelectObject(dc, white);
    MoveTo(dc, r->left, r->bottom - 1);
    LineTo(dc, r->right - 1, r->bottom - 1);
    LineTo(dc, r->right - 1, r->top - 1);
    SelectObject(dc, op);
    DeleteObject(gray);
    DeleteObject(white);
}

static void status_text(LPSTR left, LPSTR right)
{
    HWND w = active_dirwin();
    LPDIRWIN d = dirwin_of(w);
    char a[16], b[16];
    left[0] = right[0] = 0;
    if (is_search((HWND)LOWORD(SendMessage(g_mdi, WM_MDIGETACTIVE, 0, 0)))) {
        /* "n file(s) found", the second box empty (WF10) */
        wsprintf(left, load_str(IDS_FOUND), g_found);
        return;
    }
    if (!d)
        return;
    if (d->stleft == ST_SELECTED) {
        DWORD bytes;
        int n = dir_selected(d, &bytes);
        wsprintf(left, load_str(IDS_SELECTED), n, (LPSTR)s_commas(a, bytes));
    } else if (d->stleft == ST_DRIVE) {
        DWORD fr, tot;
        if (dos_freespace(d->drive + 1, &fr, &tot) == 0) {
            s_commas(a, fr / 1024);
            s_commas(b, tot / 1024);
            wsprintf(left, load_str(IDS_DRIVEFREE), 'A' + d->drive, (LPSTR)a, (LPSTR)b);
        }
    }
    if (d->panes != PANES_TREE)
        wsprintf(right, load_str(IDS_TOTAL), d->nfiles, (LPSTR)s_commas(a, d->bytes));
}

static void status_paint(HDC dc)
{
    RECT bar, l, r, t;
    char left[96], right[96];
    HBRUSH face;
    HFONT of;
    status_rects(&bar, &l, &r);
    face = CreateSolidBrush(GetSysColor(COLOR_BTNFACE));
    FillRect(dc, &bar, face);
    DeleteObject(face);
    sunken(dc, &l);
    sunken(dc, &r);
    status_text(left, right);
    of = SelectObject(dc, status_font);
    SetBkMode(dc, TRANSPARENT);
    SetTextColor(dc, GetSysColor(COLOR_BTNTEXT));
    t = l;
    InflateRect(&t, -1, -1);
    ExtTextOut(dc, l.left + 3, bar.top + 3, ETO_CLIPPED, &t, left, s_len(left), NULL);
    t = r;
    InflateRect(&t, -1, -1);
    ExtTextOut(dc, r.left + 3, bar.top + 3, ETO_CLIPPED, &t, right, s_len(right), NULL);
    SelectObject(dc, of);
}

void frame_layout(void)
{
    RECT r;
    GetClientRect(g_frame, &r);
    MoveWindow(g_mdi, -1, -1, r.right + 2,
               r.bottom + 2 - (g_statusbar ? g_dystatus + 1 : 0), TRUE);
    InvalidateRect(g_frame, NULL, TRUE);
}

/* ---- menus ---- */

static void check(HMENU m, UINT id, BOOL on)
{
    CheckMenuItem(m, id, MF_BYCOMMAND | (on ? MF_CHECKED : MF_UNCHECKED));
}

static void init_menu(HMENU m, int pos)
{
    LPDIRWIN d = dirwin_of(active_dirwin());
    if (pos == MENU_VIEW && d) {
        /* a minimised window: the pane items grey and unchecked (WF05) */
        BOOL icon = IsIconic(d->hwnd);
        UINT en = MF_BYCOMMAND | (icon ? MF_GRAYED : MF_ENABLED);
        EnableMenuItem(m, IDM_BOTH, en);
        EnableMenuItem(m, IDM_TREEONLY, en);
        EnableMenuItem(m, IDM_DIRONLY, en);
        EnableMenuItem(m, IDM_SPLIT, en);
        check(m, IDM_BOTH, !icon && d->panes == PANES_BOTH);
        check(m, IDM_TREEONLY, !icon && d->panes == PANES_TREE);
        check(m, IDM_DIRONLY, !icon && d->panes == PANES_DIR);
        check(m, IDM_VNAME, (d->view & VIEW_DETAILMASK) == VIEW_NAMEONLY);
        check(m, IDM_VDETAILS, (d->view & VIEW_DETAILMASK) == VIEW_EVERYTHING);
        check(m, IDM_VOTHER, (d->view & VIEW_DETAILMASK) != VIEW_NAMEONLY &&
                               (d->view & VIEW_DETAILMASK) != VIEW_EVERYTHING);
        check(m, IDM_BYNAME, d->sort == SORT_NAME);
        check(m, IDM_BYTYPE, d->sort == SORT_TYPE);
        check(m, IDM_BYSIZE, d->sort == SORT_SIZE);
        check(m, IDM_BYDATE, d->sort == SORT_DATE);
    } else if (pos == MENU_OPTIONS) {
        check(m, IDM_STATUSBAR, g_statusbar);
        check(m, IDM_MINONRUN, g_minonrun);
        check(m, IDM_SAVESETTINGS, g_saveonexit);
    } else if (pos == MENU_TREE && d) {
        check(m, IDM_EXPANDTREE, (d->view & VIEW_PLUSES) != 0);
    }
}

/* ---- Help ----
   What 3.1's File Manager asks WINHELP for (tests/wfhelp.sh, WFH1-8):
   Contents and F1 in the window HELP_INDEX, Search for Help on
   HELP_PARTIALKEY "", How to Use Help HELP_HELPONHELP, HELP_QUIT at the
   end; F1 on a menu item HELP_CONTEXT 5000 + the item's command ID (File >
   Open 5101, Help > Search for Help on 5030). */

static void help(UINT cmd, DWORD data)
{
    WinHelp(g_frame, load_str(IDS_HELPFILE), cmd, data);
}

static HHOOK filter;
static UINT menu_item;                  /* the last item WM_MENUSELECT named */

LRESULT CALLBACK _export MessageFilter(int code, WPARAM wp, LPARAM lp)
{
    MSG FAR *m = (MSG FAR *)lp;
    if (code == MSGF_MENU && m->message == WM_KEYDOWN && m->wParam == VK_F1) {
        if (menu_item && menu_item != 0xFFFF)
            PostMessage(g_frame, WM_COMMAND, IDM_HELPCONTEXT, (LPARAM)menu_item);
        return 1;
    }
    return CallNextHookEx(filter, code, wp, lp);
}

static void repaint_all(void);

/* a new list font (Options > Font): the lines, columns, bars again */
void frame_newfont(void)
{
    HWND w;
    make_font();
    for (w = GetWindow(g_mdi, GW_CHILD); w; w = GetWindow(w, GW_HWNDNEXT)) {
        LPDIRWIN d = dirwin_of(w);
        if (!d)
            continue;
        SendMessage(d->treelb, LB_SETITEMHEIGHT, 0, MAKELPARAM(g_dyitem, 0));
        SendMessage(d->dirlb, LB_SETITEMHEIGHT, 0, MAKELPARAM(g_dyitem, 0));
        dir_columns(d);
        treewin_layout(d);
    }
    repaint_all();
}

static void repaint_all(void)
{
    HWND w;
    for (w = GetWindow(g_mdi, GW_CHILD); w; w = GetWindow(w, GW_HWNDNEXT)) {
        LPDIRWIN d = dirwin_of(w);
        if (!d)
            continue;
        InvalidateRect(d->treelb, NULL, TRUE);
        InvalidateRect(d->dirlb, NULL, TRUE);
        InvalidateRect(d->drives, NULL, TRUE);
    }
}

static LRESULT command(HWND w, WPARAM id, LPARAM lp)
{
    HWND aw = active_dirwin();
    LPDIRWIN d = dirwin_of(aw);
    (void)lp;
    switch (id) {
    case IDM_EXIT:
        PostMessage(w, WM_CLOSE, 0, 0);
        return 0;
    case IDM_STATUSBAR:
        g_statusbar = !g_statusbar;
        ini_write_int("StatusBar", g_statusbar);
        frame_layout();
        return 0;
    case IDM_MINONRUN:
        g_minonrun = !g_minonrun;
        ini_write_int("MinOnRun", g_minonrun);
        return 0;
    case IDM_SAVESETTINGS:
        g_saveonexit = !g_saveonexit;
        ini_write_int("Save Settings", g_saveonexit);
        return 0;
    case IDM_EXPANDTREE:
        if (d) {
            d->view ^= VIEW_PLUSES;
            tree_read(d);
        }
        return 0;
    case IDM_OPEN:
    case IDM_MOVE:
    case IDM_COPY:
    case IDM_DELETE:
    case IDM_RENAME:
    case IDM_ATTRIBS:
    case IDM_RUN:
    case IDM_PRINT:
    case IDM_MAKEDIR:
    case IDM_SELECT:
        if (d)
            file_command(d, id);
        return 0;
    case IDM_SEARCH:
        file_search(d);
        return 0;
    case IDM_DISKCOPY:
        disk_copy();
        return 0;
    case IDM_LABEL:
        disk_label();
        return 0;
    case IDM_FORMAT:
        disk_format();
        return 0;
    case IDM_SYSDISK:
        disk_sysdisk();
        return 0;
    case IDM_DRIVESMORE:
        disk_select();
        return 0;
    case IDM_ASSOCIATE:
        if (d)
            file_associate(d);
        return 0;
    case IDM_VOTHER:
        if (d)
            dlg_other(d);
        return 0;
    case IDM_VINCLUDE:
        if (d)
            dlg_include(d);
        return 0;
    case IDM_CONFIRM:
        dlg_confirm();
        return 0;
    case IDM_FONT:
        dlg_font();
        return 0;
    case IDM_CASCADE:
        SendMessage(g_mdi, WM_MDICASCADE, 0, 0);
        return 0;
    case IDM_TILE:
        /* one above the other (WF04 S04) */
        SendMessage(g_mdi, WM_MDITILE, GetKeyState(VK_SHIFT) < 0 ? MDITILE_VERTICAL : MDITILE_HORIZONTAL, 0);
        return 0;
    case IDM_ARRANGE:
        SendMessage(g_mdi, WM_MDIICONARRANGE, 0, 0);
        return 0;
    case IDM_NEWWINDOW:
        if (d) {
            HWND nw = treewin_create(d->path, d->spec, d->view, d->sort, d->attribs, d->split,
                                     NULL, SW_SHOWNORMAL, NULL);
            if (nw) {
                LPDIRWIN e = dirwin_of(nw);
                if (e) {
                    tree_clone(e, d);
                    if (e->panes != d->panes)
                        treewin_set_panes(e, d->panes);
                }
            }
        }
        return 0;
    case IDM_HELPINDEX:
        help(HELP_INDEX, 0);
        return 0;
    case IDM_HELPSEARCH:
        help(HELP_PARTIALKEY, (DWORD)(LPSTR)"");
        return 0;
    case IDM_HELPHELP:
        help(HELP_HELPONHELP, 0);
        return 0;
    case IDM_ABOUT:
    {
        /* SHELL's box: "About File Manager", SHELL's own picture (WF06) */
        char app[40];
        LoadString(g_hinst, IDS_APPNAME, app, sizeof app);
        ShellAbout(w, app, load_str(IDS_ABOUTOTHER), NULL);
    }
        return 0;
    }
    if (d && id >= IDM_EXPONE && id <= IDM_SPLIT) {
        treewin_command(d, id);
        status_update();
        return 0;
    }
    if (id == IDM_REFRESH) {
        if (d)
            treewin_command(d, id);
        /* then each extension (WF15) */
        ext_refresh();
        return 0;
    }
    /* an extension's item: File Manager's own commands first (F5 with a
       fifth extension, delta 1000, is Refresh, not its item 3: WF14) */
    if (ext_command(id))
        return 0;
    return DefFrameProc(w, g_mdi, WM_COMMAND, id, lp);
}

LRESULT CALLBACK _export FrameWndProc(HWND w, UINT msg, WPARAM wp, LPARAM lp)
{
    switch (msg) {
    case WM_CREATE: {
        CLIENTCREATESTRUCT cc;
        cc.hWindowMenu = GetSubMenu(GetMenu(w), MENU_WINDOW);
        cc.idFirstChild = IDM_CHILDSTART;
        g_frame = w;
        g_mdi = CreateWindow("MDIClient", NULL,
                             WS_CHILD | WS_VISIBLE | WS_CLIPCHILDREN | WS_BORDER | WS_VSCROLL | WS_HSCROLL,
                             0, 0, 0, 0, w, (HMENU)1, g_hinst, (LPSTR)&cc);
        return g_mdi ? 0 : -1;
    }
    case WM_SIZE:
        if (wp != SIZE_MINIMIZED)
            frame_layout();
        return 0;
    case WM_PAINT: {
        PAINTSTRUCT ps;
        HDC dc = BeginPaint(w, &ps);
        if (g_statusbar && !IsIconic(w))
            status_paint(dc);
        EndPaint(w, &ps);
        return 0;
    }
    case WM_INITMENUPOPUP:
        if (!HIWORD(lp)) {
            init_menu((HMENU)wp, LOWORD(lp) - (IsZoomed((HWND)LOWORD(
                SendMessage(g_mdi, WM_MDIGETACTIVE, 0, 0))) ? 1 : 0));
            ext_initmenu((HMENU)wp);
        }
        break;
    case WM_MENUSELECT:
        /* the item under the menu's highlight, for F1 */
        menu_item = (LOWORD(lp) & (MF_POPUP | MF_SEPARATOR)) ? 0 : wp;
        break;
    case WM_COMMAND:
        if (wp == IDM_HELPCONTEXT) {
            help(HELP_CONTEXT, 5000L + (WORD)lp);
            return 0;
        }
        return command(w, wp, lp);
    case FM_GETFOCUS:
    case FM_GETDRIVEINFO:
    case FM_GETSELCOUNT:
    case FM_GETSELCOUNTLFN:
    case FM_GETFILESEL:
    case FM_GETFILESELLFN:
    case FM_REFRESH_WINDOWS:
    case FM_RELOAD_EXTENSIONS:
        return ext_message(msg, wp, lp);
    case WM_SYSCOLORCHANGE:
        bm_load();
        repaint_all();
        break;
    case WM_CLOSE:
        if (g_saveonexit)
            ini_save();
        DestroyWindow(w);
        return 0;
    case WM_FILESYSCHANGE:
        files_changed();
        return 0;
    case WM_DESTROY:
        if (cdr_thunk) {
            FileCdr(NULL);
            FreeProcInstance(cdr_thunk);
            cdr_thunk = NULL;
        }
        help(HELP_QUIT, 0);
        PostQuitMessage(0);
        return 0;
    }
    return DefFrameProc(w, g_mdi, msg, wp, lp);
}

static BOOL register_frame(void)
{
    WNDCLASS wc;
    wc.style = 0;
    wc.lpfnWndProc = FrameWndProc;
    wc.cbClsExtra = 0;
    wc.cbWndExtra = 0;
    wc.hInstance = g_hinst;
    wc.hIcon = LoadIcon(g_hinst, MAKEINTRESOURCE(IDI_WINFILE));
    wc.hCursor = LoadCursor(NULL, IDC_ARROW);
    wc.hbrBackground = (HBRUSH)(COLOR_APPWORKSPACE + 1);
    wc.lpszMenuName = MAKEINTRESOURCE(IDM_MAIN);
    wc.lpszClassName = cls_frame;
    return RegisterClass(&wc);
}

/* Ctrl+letter: that drive in the active window */
static BOOL drive_key(MSG FAR *m)
{
    HWND aw;
    LPDIRWIN d;
    int drive, i;
    if (m->message != WM_KEYDOWN || m->wParam < 'A' || m->wParam > 'Z')
        return FALSE;
    if (GetKeyState(VK_CONTROL) >= 0 || GetKeyState(VK_MENU) < 0)
        return FALSE;
    aw = active_dirwin();
    d = dirwin_of(aw);
    if (!d)
        return FALSE;
    drive = (int)(m->wParam - 'A');
    i = drive_index(drive);
    if (i < 0) {
        MessageBeep(0);
        return TRUE;
    }
    treewin_set_drive(d, drive);
    return TRUE;
}

int PASCAL WinMain(HINSTANCE inst, HINSTANCE prev, LPSTR cmd, int show)
{
    MSG m;
    RECT r;
    int fshow = SW_SHOWNORMAL;
    (void)cmd;
    if (prev) {
        /* one File Manager: bring the running one up */
        HWND w = FindWindow(cls_frame, NULL);
        if (w) {
            if (IsIconic(w))
                ShowWindow(w, SW_RESTORE);
            BringWindowToTop(GetLastActivePopup(w));
        }
        return 0;
    }
    g_hinst = inst;
    util_init();
    ini_load();
    make_font();
    bm_load();
    drives_scan();
    if (!register_frame() || !treewin_register() || !drives_register() ||
        !tree_register() || !dir_register() || !search_register())
        return 0;
    accel = LoadAccelerators(inst, MAKEINTRESOURCE(IDA_MAIN));
    filter = SetWindowsHookEx(WH_MSGFILTER, (HOOKPROC)MakeProcInstance((FARPROC)MessageFilter, inst),
                              inst, GetCurrentTask());
    /* first start: the whole screen, not maximised (WF1) */
    if (!ini_frame(&r, &fshow))
        SetRect(&r, 0, 0, GetSystemMetrics(SM_CXSCREEN), GetSystemMetrics(SM_CYSCREEN));
    g_frame = CreateWindow(cls_frame, load_str(IDS_APPNAME),
                               WS_OVERLAPPEDWINDOW | WS_CLIPCHILDREN,
                               r.left, r.top, r.right - r.left, r.bottom - r.top,
                               NULL, NULL, inst, NULL);
    if (!g_frame)
        return 0;
    cdr_thunk = MakeProcInstance((FARPROC)cdr_proc, inst);
    if (!LOWORD(FileCdr(cdr_thunk))) {          /* another has it */
        FreeProcInstance(cdr_thunk);
        cdr_thunk = NULL;
    }
    /* the extensions, before the frame shows or has a directory window
       (WF15) */
    ext_load();
    if (show == SW_SHOWNORMAL || show == SW_SHOW)
        show = fshow;
    ShowWindow(g_frame, show);
    UpdateWindow(g_frame);
    ini_restore_windows(show);
    {
        /* the last made (the top) is the active one, even an icon (WF05) */
        HWND aw = GetWindow(g_mdi, GW_CHILD);
        if (aw) {
            SendMessage(g_mdi, WM_MDIACTIVATE, (WPARAM)aw, 0);
            SetFocus(aw);
        }
    }
    while (GetMessage(&m, NULL, 0, 0)) {
        if (TranslateMDISysAccel(g_mdi, &m))
            continue;
        if (drive_key(&m))
            continue;
        if (accel && TranslateAccelerator(g_frame, accel, &m))
            continue;
        TranslateMessage(&m);
        DispatchMessage(&m);
    }
    /* the frame is gone: FMEVENT_UNLOAD gets hwnd 0 (WF15) */
    ext_unload(FALSE);
    if (filter)
        UnhookWindowsHookEx(filter);
    if (g_font)
        DeleteObject(g_font);
    if (status_font)
        DeleteObject(status_font);
    bm_free();
    return m.wParam;
}
