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

static char cls_frame[] = "WFS_Frame";
static HACCEL accel;

BOOL ini_frame(RECT FAR *r, int *show);

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
        check(m, IDM_BOTH, d->panes == PANES_BOTH);
        check(m, IDM_TREEONLY, d->panes == PANES_TREE);
        check(m, IDM_DIRONLY, d->panes == PANES_DIR);
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

/* ---- Help ---- */

static void help(UINT cmd, DWORD data)
{
    WinHelp(g_frame, load_str(IDS_HELPFILE), cmd, data);
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
        ShellAbout(w, load_str(IDS_APPNAME), load_str(IDS_ABOUTOTHER),
                   LoadIcon(g_hinst, MAKEINTRESOURCE(IDI_WINFILE)));
        return 0;
    }
    if (d && id >= IDM_EXPONE && id <= IDM_SPLIT) {
        treewin_command(d, id);
        status_update();
        return 0;
    }
    if (d && id == IDM_REFRESH) {
        treewin_command(d, id);
        return 0;
    }
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
                             WS_CHILD | WS_VISIBLE | WS_CLIPCHILDREN | WS_BORDER,
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
        if (!HIWORD(lp))
            init_menu((HMENU)wp, LOWORD(lp) - (IsZoomed((HWND)LOWORD(
                SendMessage(g_mdi, WM_MDIGETACTIVE, 0, 0))) ? 1 : 0));
        break;
    case WM_COMMAND:
        return command(w, wp, lp);
    case WM_SYSCOLORCHANGE:
        bm_load();
        repaint_all();
        break;
    case WM_CLOSE:
        if (g_saveonexit)
            ini_save();
        DestroyWindow(w);
        return 0;
    case WM_DESTROY:
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
        !tree_register() || !dir_register())
        return 0;
    accel = LoadAccelerators(inst, MAKEINTRESOURCE(IDA_MAIN));
    /* first start: the whole screen, not maximised (WF1) */
    if (!ini_frame(&r, &fshow))
        SetRect(&r, 0, 0, GetSystemMetrics(SM_CXSCREEN), GetSystemMetrics(SM_CYSCREEN));
    g_frame = CreateWindow(cls_frame, load_str(IDS_APPNAME),
                               WS_OVERLAPPEDWINDOW | WS_CLIPCHILDREN,
                               r.left, r.top, r.right - r.left, r.bottom - r.top,
                               NULL, NULL, inst, NULL);
    if (!g_frame)
        return 0;
    if (show == SW_SHOWNORMAL || show == SW_SHOW)
        show = fshow;
    ShowWindow(g_frame, show);
    UpdateWindow(g_frame);
    ini_restore_windows(show);
    {
        HWND aw = (HWND)LOWORD(SendMessage(g_mdi, WM_MDIGETACTIVE, 0, 0));
        if (aw)
            SetFocus(aw);
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
    if (g_font)
        DeleteObject(g_font);
    if (status_font)
        DeleteObject(status_font);
    bm_free();
    return m.wParam;
}
