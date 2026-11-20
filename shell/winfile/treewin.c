/*
 * treewin.c - w16 File Manager: a directory window (MDI child of class
 * WFS_Tree, its title the directory and pattern, "C:\WINDOWS\*.*").
 *
 * Observed (WF1, WF2; VGA): the drive bar (child 1) across the top; under
 * it the tree (child 5) from x -1 (its border on the window's) to the
 * split position plus one, and the file list (child 2) from the split
 * plus 3 to a pixel past the right edge, both down to a pixel past the
 * bottom; the 3 pixels between them are black. Tree Only and Directory
 * Only put the one pane at x 3, the rest of the client in the window
 * colour. Returning to Tree and Directory puts the split in the middle
 * (WF2: 246 in a 492-pixel client); a new window takes the active one's
 * directory, views and split. Windows showing the same directory are
 * told apart by ":1", ":2"... after the title, in the order they were
 * made (WF2, WF4).
 */
#include "winfile.h"

static char cls_tree[] = "WFS_Tree";

typedef struct tagCREATEPARAMS {
    LPCSTR path, spec;
    WORD view, sort, attribs;
    int split;
} CREATEPARAMS;

LPDIRWIN dirwin_of(HWND w)
{
    char cls[16];
    if (!w || !IsWindow(w))
        return NULL;
    GetClassName(w, cls, sizeof cls);
    if (lstrcmp(cls, cls_tree))
        return NULL;
    return (LPDIRWIN)GetWindowLong(w, 0);
}

int drives_height(void);

void treewin_layout(LPDIRWIN d)
{
    RECT r;
    int dh = drives_height(), cy;
    GetClientRect(d->hwnd, &r);
    cy = r.bottom - dh + 1;
    if (cy < 0)
        cy = 0;
    MoveWindow(d->drives, 0, 0, r.right, dh, TRUE);
    switch (d->panes) {
    case PANES_TREE:
        ShowWindow(d->dir, SW_HIDE);
        MoveWindow(d->tree, 3, dh, r.right - 2, cy, TRUE);
        ShowWindow(d->tree, SW_SHOWNA);
        break;
    case PANES_DIR:
        ShowWindow(d->tree, SW_HIDE);
        MoveWindow(d->dir, 3, dh, r.right - 2, cy, TRUE);
        ShowWindow(d->dir, SW_SHOWNA);
        break;
    default:
        MoveWindow(d->tree, -1, dh, d->split + 1, cy, TRUE);
        MoveWindow(d->dir, d->split + 3, dh, r.right + 1 - (d->split + 3), cy, TRUE);
        ShowWindow(d->tree, SW_SHOWNA);
        ShowWindow(d->dir, SW_SHOWNA);
        break;
    }
    InvalidateRect(d->hwnd, NULL, TRUE);
}

void treewin_set_panes(LPDIRWIN d, int panes)
{
    RECT r;
    HWND f = GetFocus();
    if (panes == PANES_BOTH && d->panes != PANES_BOTH) {
        GetClientRect(d->hwnd, &r);
        d->split = r.right / 2;
    }
    if (panes == PANES_DIR && d->panes == PANES_TREE) {
        /* from Tree Only straight to Directory Only the tree starts
           afresh: Indicate Expandable Branches is off, the focus goes to
           the drive bar and the list's caret line is selected (WF03 S12,
           S13; WF2 A12) */
        d->panes = panes;
        d->view &= ~VIEW_PLUSES;
        treewin_layout(d);
        tree_read(d);
        SetFocus(d->drives);
        SendMessage(d->dirlb, LB_SETSEL, TRUE,
                    MAKELPARAM((int)SendMessage(d->dirlb, LB_GETCARETINDEX, 0, 0), 0));
        status_set(d, ST_NONE);
        return;
    }
    d->panes = panes;
    treewin_layout(d);
    /* the focus leaves a pane that goes (WF6) */
    if (panes == PANES_TREE && f == d->dirlb)
        SetFocus(d->treelb);
    else if (panes == PANES_DIR && f == d->treelb)
        SetFocus(d->dirlb);
    if (panes == PANES_TREE)
        status_set(d, ST_DRIVE);
    else if (panes == PANES_DIR)
        status_set(d, GetFocus() == d->dirlb ? ST_SELECTED : ST_NONE);
    else
        status_set(d, d->stleft == ST_NONE ? ST_DRIVE : d->stleft);   /* WF03 S13, WF6 F02 */
}

/* the title a window would have without its number */
static void base_title(LPDIRWIN d, LPSTR t, int n)
{
    s_cpy(t, d->path, n);
    path_add(t, d->spec, n);
}

static void set_text(LPDIRWIN d)
{
    char t[PATHLEN + 8], u[PATHLEN + 8];
    base_title(d, t, sizeof t);
    if (d->number) {
        char num[8];
        wsprintf(num, ":%d", d->number);
        s_cat(t, num, sizeof t);
    }
    GetWindowText(d->hwnd, u, sizeof u);
    if (lstrcmp(t, u))
        SetWindowText(d->hwnd, t);
}

/* A window's title, set when it is made, changes directory or is
   refreshed: the directory and pattern, and ":n" when other windows show
   the same; n is one more than theirs, and one of them without a number
   becomes ":1" (WF2: ":1", ":2"; WF04 S07: F5 in ":1" beside ":2" makes
   it ":3"). The others keep theirs. */
void treewin_title(LPDIRWIN d)
{
    HWND v;
    char t[PATHLEN + 8], u[PATHLEN + 8];
    int top = 0, same = 0;
    base_title(d, t, sizeof t);
    for (v = GetWindow(g_mdi, GW_CHILD); v; v = GetWindow(v, GW_HWNDNEXT)) {
        LPDIRWIN e = dirwin_of(v);
        if (!e || e == d || GetWindow(v, GW_OWNER))
            continue;
        base_title(e, u, sizeof u);
        if (lstrcmpi(t, u))
            continue;
        same++;
        if (!e->number) {
            e->number = 1;
            set_text(e);
        }
        if (e->number > top)
            top = e->number;
    }
    d->number = same ? top + 1 : 0;
    set_text(d);
}

void treewin_change_dir(LPDIRWIN d, LPCSTR path)
{
    if (!lstrcmpi(path, d->path) && d->ready)
        return;
    s_cpy(d->path, path, sizeof d->path);
    treewin_title(d);
    dir_read(d);
    status_set(d, ST_DRIVE);
}

void treewin_set_drive(LPDIRWIN d, int drive)
{
    char cwd[PATHLEN];
    if (drive == d->drive)
        return;
    cwd[0] = (char)('A' + drive);
    cwd[1] = ':';
    cwd[2] = '\\';
    if (dos_getcwd(drive + 1, cwd + 3) != 0) {
        /* a drive DOS cannot read (A: without a disk in DOSBox): "Error
           Selecting Drive", "Drive A does not exist." (WF06) */
        char text[64], title[40];
        LoadString(g_hinst, IDS_SELDRIVE, title, sizeof title);
        LoadString(g_hinst, IDS_NOTEXIST, cwd, sizeof cwd);
        wsprintf(text, cwd, 'A' + drive);
        MessageBox(g_frame, text, title, MB_OK | MB_ICONSTOP);
        return;
    }
    d->drive = drive;
    d->drivefocus = drive_index(drive);
    s_cpy(d->path, cwd, sizeof d->path);
    InvalidateRect(d->drives, NULL, TRUE);
    tree_read(d);
    treewin_title(d);
    dir_read(d);
    status_set(d, ST_DRIVE);
}

LRESULT treewin_command(LPDIRWIN d, WPARAM id)
{
    int i = (int)SendMessage(d->treelb, LB_GETCURSEL, 0, 0);
    switch (id) {
    case IDM_EXPONE:
        tree_expand(d, i, 1);
        break;
    case IDM_EXPSUB:
        tree_expand(d, i, 2);
        break;
    case IDM_EXPALL:
        tree_expand(d, 0, 2);
        break;
    case IDM_COLLAPSE:
        tree_collapse(d, i);
        break;
    case IDM_TREEONLY:
        treewin_set_panes(d, PANES_TREE);
        break;
    case IDM_DIRONLY:
        treewin_set_panes(d, PANES_DIR);
        break;
    case IDM_BOTH:
        treewin_set_panes(d, PANES_BOTH);
        break;
    case IDM_VNAME:
    case IDM_VDETAILS: {
        WORD v = id == IDM_VNAME ? VIEW_NAMEONLY : VIEW_EVERYTHING;
        if ((d->view & VIEW_DETAILMASK) != v) {
            BOOL relist = ((d->view & VIEW_DETAILMASK) == VIEW_NAMEONLY) != (v == VIEW_NAMEONLY);
            d->view = (WORD)((d->view & ~VIEW_DETAILMASK) | v);
            if (relist)
                dir_set_view(d);
            else
                InvalidateRect(d->dirlb, NULL, TRUE);
        }
        break;
    }
    case IDM_BYNAME:
    case IDM_BYTYPE:
    case IDM_BYSIZE:
    case IDM_BYDATE:
        if (d->sort != (WORD)(id - IDM_BYNAME + SORT_NAME)) {
            HGLOBAL sel = dir_sel_save(d);
            d->sort = (WORD)(id - IDM_BYNAME + SORT_NAME);
            dir_read(d);
            dir_sel_restore(d, sel);
        }
        break;
    case IDM_REFRESH:
        tree_read(d);
        treewin_title(d);
        dir_read(d);
        status_set(d, ST_DRIVE);
        break;
    }
    return 0;
}

static HICON window_icon(LPDIRWIN d)
{
    return LoadIcon(g_hinst, MAKEINTRESOURCE(d->panes == PANES_TREE ? IDI_TREE :
                                             d->panes == PANES_DIR ? IDI_DIR : IDI_TREEDIR));
}

static void paint(LPDIRWIN d)
{
    PAINTSTRUCT ps;
    HDC dc = BeginPaint(d->hwnd, &ps);
    {
        /* the split's 3 pixels are the window colour, but black beside
           the panes' horizontal scroll bars and on the panes' top row
           (WF01); with one pane they are at the left edge (WF03 S11) */
        RECT r;
        int x = d->panes == PANES_BOTH ? d->split : 0;
        GetClientRect(d->hwnd, &r);
        r.left = x;
        r.right = x + 3;
        r.top = r.bottom - GetSystemMetrics(SM_CYHSCROLL);
        fill_sys(dc, &r, COLOR_WINDOWFRAME);
        r.top = drives_height();        /* and its top row, under the bar */
        r.bottom = r.top + 1;
        fill_sys(dc, &r, COLOR_WINDOWFRAME);
    }
    EndPaint(d->hwnd, &ps);
}

LRESULT CALLBACK _export TreeWndProc(HWND w, UINT msg, WPARAM wp, LPARAM lp)
{
    LPDIRWIN d = (LPDIRWIN)GetWindowLong(w, 0);
    switch (msg) {
    case WM_CREATE: {
        CREATESTRUCT FAR *cs = (CREATESTRUCT FAR *)lp;
        MDICREATESTRUCT FAR *mc = (MDICREATESTRUCT FAR *)cs->lpCreateParams;
        CREATEPARAMS FAR *p = (CREATEPARAMS FAR *)mc->lParam;
        HGLOBAL h = GlobalAlloc(GMEM_FIXED | GMEM_ZEROINIT, sizeof(DIRWIN));
        if (!h)
            return -1;
        d = (LPDIRWIN)GlobalLock(h);
        SetWindowLong(w, 0, (LONG)d);
        d->hwnd = w;
        s_cpy(d->path, p->path, sizeof d->path);
        s_cpy(d->spec, p->spec, sizeof d->spec);
        d->drive = (d->path[0] | 0x20) - 'a';
        d->view = p->view;
        d->sort = p->sort;
        d->attribs = p->attribs;
        d->split = p->split;
        d->panes = PANES_BOTH;
        d->lowerdrives = g_lower;
        d->drives = CreateWindow("WFS_Drives", NULL, WS_CHILD | WS_VISIBLE, 0, 0, 0, 0,
                                 w, (HMENU)ID_DRIVES, g_hinst, NULL);
        d->tree = CreateWindow("DirTree", NULL, WS_CHILD | WS_VISIBLE, 0, 0, 0, 0,
                               w, (HMENU)ID_TREE, g_hinst, NULL);
        d->treelb = GetDlgItem(d->tree, ID_TREELIST);
        d->dir = CreateWindow("WFS_Dir", NULL, WS_CHILD | WS_VISIBLE, 0, 0, 0, 0,
                              w, (HMENU)ID_DIR, g_hinst, NULL);
        dir_set_view(d);
        tree_read(d);
        dir_read(d);
        d->ready = TRUE;
        return 0;
    }
    case WM_SIZE:
        if (d && wp != SIZE_MINIMIZED)
            treewin_layout(d);
        break;
    case WM_PAINT:
        if (d && IsIconic(w)) {
            /* the icon shows the panes the window has (WF04 S06): the
               class has none, so the window draws it */
            PAINTSTRUCT ps;
            HDC dc = BeginPaint(w, &ps);
            DrawIcon(dc, 0, 0, window_icon(d));
            EndPaint(w, &ps);
            return 0;
        }
        if (d) {
            paint(d);
            return 0;
        }
        break;
    case WM_QUERYDRAGICON:
        if (d)
            return (LRESULT)(WORD)window_icon(d);
        break;
    case WM_SETFOCUS:
        if (d) {
            if (d->panes == PANES_DIR)
                SetFocus(d->dirlb);
            else
                SetFocus(d->treelb);
        }
        break;
    case WM_MDIACTIVATE:
        status_update();
        break;
    case WM_DESTROY:
        if (d) {
            HGLOBAL h;
            arena_free(&d->tarena);
            arena_free(&d->darena);
            SetWindowLong(w, 0, 0);
            h = GlobalHandle(SELECTOROF(d));
            GlobalUnlock(h);
            GlobalFree(h);
        }
        break;
    }
    return DefMDIChildProc(w, msg, wp, lp);
}

/* make a directory window; rc NULL: MDI's default place and size */
HWND treewin_create(LPCSTR path, LPCSTR spec, WORD view, WORD sort, WORD attribs,
                    int split, const RECT FAR *rc, int showcmd, POINT FAR *icon)
{
    MDICREATESTRUCT mc;
    CREATEPARAMS p;
    char title[PATHLEN];
    HWND w;
    p.path = path;
    p.spec = spec;
    p.view = view;
    p.sort = sort;
    p.attribs = attribs;
    p.split = split;
    s_cpy(title, path, sizeof title);
    path_add(title, spec, sizeof title);
    mc.szClass = cls_tree;
    mc.szTitle = title;
    mc.hOwner = g_hinst;
    if (rc) {
        mc.x = rc->left;
        mc.y = rc->top;
        mc.cx = rc->right - rc->left;
        mc.cy = rc->bottom - rc->top;
    } else {
        mc.x = mc.y = mc.cx = mc.cy = CW_USEDEFAULT;
    }
    mc.style = showcmd == SW_SHOWMAXIMIZED ? WS_MAXIMIZE :
               showcmd == SW_SHOWMINIMIZED ? WS_MINIMIZE : 0;
    mc.lParam = (LPARAM)(CREATEPARAMS FAR *)&p;
    w = (HWND)SendMessage(g_mdi, WM_MDICREATE, 0, (LPARAM)(LPMDICREATESTRUCT)&mc);
    if (w && showcmd == SW_SHOWMINIMIZED && icon && icon->x != -1) {
        /* the icon where it was */
        RECT r;
        GetWindowRect(w, &r);
        SetWindowPos(w, NULL, icon->x, icon->y, 0, 0, SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE);
    }
    /* windows keep the order they are made in, icons too (WF05) */
    if (w)
        SetWindowPos(w, HWND_TOP, 0, 0, 0, 0, SWP_NOSIZE | SWP_NOMOVE | SWP_NOACTIVATE);
    if (w && dirwin_of(w))
        treewin_title(dirwin_of(w));
    return w;
}

BOOL treewin_register(void)
{
    WNDCLASS wc;
    wc.style = 0;
    wc.lpfnWndProc = TreeWndProc;
    wc.cbClsExtra = 0;
    wc.cbWndExtra = sizeof(LONG);
    wc.hInstance = g_hinst;
    wc.hIcon = NULL;
    wc.hCursor = LoadCursor(NULL, IDC_ARROW);
    wc.hbrBackground = (HBRUSH)(COLOR_WINDOW + 1);
    wc.lpszMenuName = NULL;
    wc.lpszClassName = cls_tree;
    return RegisterClass(&wc);
}
