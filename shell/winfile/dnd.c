/*
 * dnd.c - w16 File Manager: dragging files and directories.
 *
 * The source side is USER's DragObject (USER.464; Schulman, Undocumented
 * Windows ch. 6, and w16's user/drag.c for what 3.1's does): the list of
 * names in our data segment ("NAME " for files, the full path for
 * directories, each followed by a blank), its kind (1 program, 2 other
 * file, 3 directory, 4 several) with 8000h so that other programs are
 * asked too, an OFSTRUCT for a single file. A window of another program
 * that takes files (WS_EX_ACCEPTFILES: DefWindowProc answers "FILE") gets
 * WM_DROPFILES posted with the block ARCHITECTURE.md 13.3 describes, as
 * 3.1's File Manager sends it (Program Manager makes items of them).
 *
 * Our own panes, the drive bar and minimised directory windows take
 * drops: a directory line of the file list or the tree, a drive, a
 * window's directory. Between drives the files are copied, on one drive
 * moved; Ctrl copies, Alt moves (3.1's rules). The lists are subclassed
 * to start a drag when the pointer leaves the pressed line with the
 * button down, and to answer the drop messages.
 */
#include "winfile.h"

#define WM_DROPOBJECT       0x022A
#define WM_QUERYDROPOBJECT  0x022B
#define WM_DRAGLOOP         0x022D
#define WM_DRAGSELECT       0x022E
#define WM_DRAGMOVE         0x022F
#define DOF_EXECUTABLE      0x0001
#define DOF_DOCUMENT        0x0002
#define DOF_DIRECTORY       0x0003
#define DOF_MULTIPLE        0x0004
#define DOF_EXTERNAL        0x8000
#define DRAG_FILE           0x454C4946L     /* "FILE" */

typedef struct tagDRAGINFO {
    HWND hwndSource, hwndSink;
    WORD wFlags;
    WORD list;                          /* near pointer in the source's DS */
    HGLOBAL hOfstruct;
    POINT pt;
    DWORD unknown;
} DRAGINFO, FAR *LPDRAGINFO;

DWORD FAR PASCAL DragObject(HWND, HWND, WORD, HANDLE, WORD, HCURSOR);

static WNDPROC lb_proc;                 /* USER's list box procedure */
static HWND press_lb;
static POINT press_pt;
static HWND drag_sink;                  /* the last window that took it */
static POINT drag_pt;
static BOOL drag_ours;                  /* the sink is one of our windows */
static int drag_kind;
static int drag_drive;                  /* the source's drive */
static LPDIRWIN drag_from;
static int hit_item;                    /* target_of's line, -1 none */
static HWND drag_lb;                    /* the file list dragged from */
static BOOL drag_hidden;                /* its dragged lines without pictures */

/* Over one of our windows the dragged lines lose their pictures, over
   another program's they have them; after the drop they stay as they
   were (obs WF11: NOTES.TXT blank during and after; WF12: CALC.HLP with
   its picture over Program Manager) */
static void hide_dragged(BOOL hide)
{
    int n, i;
    if (!drag_lb || hide == drag_hidden)
        return;
    drag_hidden = hide;
    n = (int)SendMessage(drag_lb, LB_GETCOUNT, 0, 0);
    for (i = 0; i < n; i++)
        if (SendMessage(drag_lb, LB_GETSEL, i, 0) > 0) {
            LPDENTRY e = dir_item(drag_from, i);
            RECT r;
            if (e)
                e->sel = (BYTE)hide;
            SendMessage(drag_lb, LB_GETITEMRECT, i, (LPARAM)(RECT FAR *)&r);
            InvalidateRect(drag_lb, &r, FALSE);
        }
    UpdateWindow(drag_lb);
}

/* ---- where a drop at (x, y) of window w would go: a full path, or
   FALSE when nowhere ---- */
static BOOL target_of(HWND w, int x, int y, LPSTR out)
{
    HWND p = GetParent(w);
    LPDIRWIN d;
    char cls[16];
    hit_item = -1;
    GetClassName(w, cls, sizeof cls);
    if (!lstrcmp(cls, "WFS_Drives")) {
        int i = x / 44;
        d = dirwin_of(p);
        if (!d || i < 0 || i >= g_ndrives)
            return FALSE;
        out[0] = (char)('A' + g_drivelist[i]);
        out[1] = ':';
        out[2] = '\\';
        if (dos_getcwd(g_drivelist[i] + 1, out + 3) != 0)
            out[3] = 0;
        return TRUE;
    }
    d = dirwin_of(p ? GetParent(p) : NULL);
    if (d && w == d->treelb) {
        int top = (int)SendMessage(w, LB_GETTOPINDEX, 0, 0);
        int i = top + y / g_dyitem;
        LPTNODE n = (LPTNODE)SendMessage(w, LB_GETITEMDATA, i, 0);
        if (i >= (int)SendMessage(w, LB_GETCOUNT, 0, 0) || (LONG)n == LB_ERR || !n)
            return FALSE;
        hit_item = i;
        tree_path_of(n, out, d->drive);
        return TRUE;
    }
    if (d && w == d->dirlb) {
        RECT r;
        int top = (int)SendMessage(w, LB_GETTOPINDEX, 0, 0), i;
        LPDENTRY e;
        GetClientRect(w, &r);
        if ((d->view & VIEW_DETAILMASK) == VIEW_NAMEONLY) {
            int rows = r.bottom / g_dyitem;
            if (rows < 1)
                rows = 1;
            i = top + (x / d->colw) * rows + y / g_dyitem;
        } else {
            i = top + y / g_dyitem;
        }
        s_cpy(out, d->path, PATHLEN);
        if (i < (int)SendMessage(w, LB_GETCOUNT, 0, 0) && (e = dir_item(d, i)) != NULL) {
            if (e->pic == BM_PARENT) {
                path_strip(out);
                hit_item = i;
            } else if (e->attr & ATTR_DIR) {
                path_add(out, e->name, PATHLEN);
                hit_item = i;
            }
        }
        return TRUE;
    }
    d = dirwin_of(w);
    if (d && IsIconic(w)) {
        s_cpy(out, d->path, PATHLEN);
        return TRUE;
    }
    return FALSE;
}

/* ---- the line under the pointer is framed while a drag is over it
   (obs WF11 L02: a 1-pixel frame round the whole line, inverted so that
   it goes again) ---- */
static HWND frame_lb;
static int frame_item = -1;

static void invert_frame(HWND lb, int i)
{
    RECT r;
    HDC dc;
    if (SendMessage(lb, LB_GETITEMRECT, i, (LPARAM)(RECT FAR *)&r) == LB_ERR)
        return;
    dc = GetDC(lb);
    PatBlt(dc, r.left, r.top, r.right - r.left, 1, DSTINVERT);
    PatBlt(dc, r.left, r.bottom - 1, r.right - r.left, 1, DSTINVERT);
    PatBlt(dc, r.left, r.top + 1, 1, r.bottom - r.top - 2, DSTINVERT);
    PatBlt(dc, r.right - 1, r.top + 1, 1, r.bottom - r.top - 2, DSTINVERT);
    ReleaseDC(lb, dc);
}

static void frame_off(void)
{
    if (frame_lb && frame_item >= 0)
        invert_frame(frame_lb, frame_item);
    frame_lb = NULL;
    frame_item = -1;
}

static void frame_on(HWND lb, int i)
{
    if (lb == frame_lb && i == frame_item)
        return;
    frame_off();
    if (i < 0)
        return;
    frame_lb = lb;
    frame_item = i;
    invert_frame(lb, i);
}


static BOOL copying(int destdrive)
{
    if (GetKeyState(VK_CONTROL) < 0)
        return TRUE;
    if (GetKeyState(VK_MENU) < 0)
        return FALSE;
    return destdrive != drag_drive;
}

static HCURSOR drag_cursor(BOOL copy)
{
    int id;
    switch (drag_kind) {
    case DOF_DIRECTORY:
        id = copy ? IDC_COPYDIR : IDC_DRAGDIR;
        break;
    case DOF_MULTIPLE:
        id = copy ? IDC_COPYFILES : IDC_DRAGFILES;
        break;
    default:
        id = copy ? IDC_COPYFILE : IDC_DRAGFILE;
        break;
    }
    return LoadCursor(g_hinst, MAKEINTRESOURCE(id));
}

/* a drop on one of our windows: copy or move there */
static LRESULT drop_here(HWND w, LPDRAGINFO di)
{
    char dest[PATHLEN], t[PATHLEN + 120];
    LPSTR list;
    BOOL copy;
    if (!drag_from || !target_of(w, di->pt.x, di->pt.y, dest))
        return 0;
    if (!lstrcmpi(dest, drag_from->path) && dirwin_of(GetParent(GetParent(w))) == drag_from)
        return 0;
    copy = copying((dest[0] | 0x20) - 'a');
    list = (LPSTR)MAKELP(HIWORD((DWORD)(LPVOID)&dest), di->list);
    if (g_confmouse) {
        /* "Confirm Mouse Operation", "Are you sure you want to move the
           selected files or directories to C:\WFTEST\ALPHA?" (obs WF11) */
        wsprintf(t, load_str(copy ? IDS_MOUSECOPY : IDS_MOUSEMOVE), (LPSTR)dest);
        if (MessageBox(g_frame, t, load_str(IDS_CONFMOUSE), MB_YESNO | MB_ICONEXCLAMATION) != IDYES)
            return 1;
    }
    file_transfer(drag_from, list, dest, copy ? IDM_COPY : IDM_MOVE);
    return 1;
}

/* WM_DROPFILES for another program's window, the block as 3.1 builds it */
static void post_dropfiles(HWND sink, POINT pt, LPCSTR list)
{
    char name[PATHLEN], path[PATHLEN];
    LPCSTR p = list;
    DWORD n = 8 + 1;
    HGLOBAL h;
    LPBYTE b;
    WORD at = 8;
    RECT r;
    while (next_name(&p, name, sizeof name)) {
        full_path(drag_from, name, path);
        n += s_len(path) + 1;
    }
    h = GlobalAlloc(GMEM_MOVEABLE | GMEM_ZEROINIT | GMEM_SHARE, n);
    if (!h)
        return;
    b = (LPBYTE)GlobalLock(h);
    *(WORD FAR *)b = 8;
    GetClientRect(sink, &r);
    *(int FAR *)(b + 2) = pt.x;
    *(int FAR *)(b + 4) = pt.y;
    *(WORD FAR *)(b + 6) = !PtInRect(&r, pt);
    p = list;
    while (next_name(&p, name, sizeof name)) {
        full_path(drag_from, name, path);
        lstrcpy((LPSTR)(b + at), path);
        at += s_len(path) + 1;
    }
    b[at] = 0;
    GlobalUnlock(h);
    PostMessage(sink, WM_DROPFILES, (WPARAM)h, 0);
}

static void start_drag(HWND lb)
{
    LPDIRWIN d = dirwin_of(GetParent(GetParent(lb)));
    HGLOBAL hl;
    LPSTR text;
    HLOCAL local;
    NPSTR np;
    char name[PATHLEN];
    LPCSTR p;
    int count = 0, kind = 0;
    HGLOBAL hof = NULL;
    DWORD r;
    if (!d)
        return;
    hl = selected_text(d, lb == d->treelb);
    if (!hl)
        return;
    text = (LPSTR)GlobalLock(hl);
    if (!text[0]) {
        GlobalUnlock(hl);
        GlobalFree(hl);
        return;
    }
    for (p = text; next_name(&p, name, sizeof name); count++) {
        char full[PATHLEN];
        int a;
        full_path(d, name, full);
        a = dos_getattr(full);
        kind = (a >= 0 && (a & ATTR_DIR)) ? DOF_DIRECTORY :
               is_program(name) ? DOF_EXECUTABLE : DOF_DOCUMENT;
    }
    if (count > 1)
        kind = DOF_MULTIPLE;
    local = LocalAlloc(LMEM_FIXED, s_len(text) + 1);
    if (!local) {
        GlobalUnlock(hl);
        GlobalFree(hl);
        return;
    }
    np = (NPSTR)local;
    lstrcpy((LPSTR)np, text);
    if (count == 1 && kind != DOF_DIRECTORY) {
        OFSTRUCT FAR *of;
        char full[PATHLEN];
        p = text;
        next_name(&p, name, sizeof name);
        full_path(d, name, full);
        hof = GlobalAlloc(GMEM_MOVEABLE | GMEM_SHARE, sizeof(OFSTRUCT));
        if (hof) {
            of = (OFSTRUCT FAR *)GlobalLock(hof);
            OpenFile(full, of, OF_PARSE);
            GlobalUnlock(hof);
        }
    }
    GlobalUnlock(hl);
    GlobalFree(hl);
    drag_lb = lb == d->dirlb ? lb : NULL;
    drag_hidden = FALSE;
    drag_kind = kind;
    drag_drive = d->drive;
    drag_from = d;
    drag_sink = NULL;
    drag_ours = FALSE;
    r = DragObject(GetDesktopWindow(), lb, (WORD)(kind | DOF_EXTERNAL), hof, (WORD)np,
                   drag_cursor(FALSE));
    if (r == DRAG_FILE && drag_sink && !drag_ours)
        post_dropfiles(drag_sink, drag_pt, (LPCSTR)np);
    drag_from = NULL;
    if (hof)
        GlobalFree(hof);
    LocalFree(local);
}

static BOOL is_ours(HWND w)
{
    return w && GetWindowTask(w) == GetCurrentTask();
}

LRESULT CALLBACK _export ListSubProc(HWND h, UINT msg, WPARAM wp, LPARAM lp)
{
    switch (msg) {
    case WM_LBUTTONDOWN:
        press_lb = h;
        press_pt.x = (short)LOWORD(lp);
        press_pt.y = (short)HIWORD(lp);
        break;
    case WM_LBUTTONUP:
        press_lb = NULL;
        break;
    case WM_MOUSEMOVE:
        if (press_lb == h && (wp & MK_LBUTTON)) {
            int dx = (short)LOWORD(lp) - press_pt.x, dy = (short)HIWORD(lp) - press_pt.y;
            if (dx * dx + dy * dy > 4) {
                press_lb = NULL;
                ReleaseCapture();
                start_drag(h);
                return 0;
            }
            return 0;
        }
        break;
    case WM_QUERYDROPOBJECT: {
        char dest[PATHLEN];
        LPDRAGINFO di = (LPDRAGINFO)lp;
        BOOL ok;
        if (wp || !is_ours(di->hwndSource)) {
            if (frame_lb == h)
                frame_off();
            return 0;
        }
        ok = target_of(h, di->pt.x, di->pt.y, dest);
        frame_on(h, ok ? hit_item : -1);
        return ok;
    }
    case WM_DROPOBJECT:
        /* 3.1 leaves the frame where it is (obs WF11) */
        frame_lb = NULL;
        frame_item = -1;
        return drop_here(h, (LPDRAGINFO)lp);
    case WM_DRAGLOOP:
        /* the source: the sink and the point for WM_DROPFILES, and the
           cursor for a copy or a move */
        if (drag_from) {
            LPDRAGINFO di = (LPDRAGINFO)lp;
            if (wp && di->hwndSink) {
                char dest[PATHLEN];
                drag_sink = di->hwndSink;
                drag_pt = di->pt;
                drag_ours = is_ours(drag_sink);
                hide_dragged(drag_ours);
                if (drag_ours && target_of(drag_sink, di->pt.x, di->pt.y, dest))
                    SetCursor(drag_cursor(copying((dest[0] | 0x20) - 'a')));
            } else if (!wp) {
                drag_sink = NULL;
            }
        }
        return 0;
    case WM_DRAGSELECT:
        if (!wp && frame_lb == h)
            frame_off();
        return 0;
    case WM_DRAGMOVE:
        return 0;
    }
    return CallWindowProc((FARPROC)lb_proc, h, msg, wp, lp);
}

/* the drive bar and minimised windows answer through these */
LRESULT dnd_query(HWND w, LPARAM lp)
{
    char dest[PATHLEN];
    LPDRAGINFO di = (LPDRAGINFO)lp;
    if (!is_ours(di->hwndSource))
        return 0;
    return target_of(w, di->pt.x, di->pt.y, dest);
}

LRESULT dnd_drop(HWND w, LPARAM lp)
{
    return drop_here(w, (LPDRAGINFO)lp);
}

void dnd_subclass(HWND lb)
{
    static FARPROC thunk;
    if (!thunk)
        thunk = MakeProcInstance((FARPROC)ListSubProc, g_hinst);
    if (!lb_proc)
        lb_proc = (WNDPROC)GetWindowLong(lb, GWL_WNDPROC);
    SetWindowLong(lb, GWL_WNDPROC, (LONG)thunk);
}
