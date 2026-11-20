/*
 * dir.c - w16 File Manager: the file list (class WFS_Dir, child 2 of a
 * directory window, holding an owner-drawn ListBox, child 6, of its own
 * size; WF1, WF2: LBS_NOTIFY | LBS_SORT | LBS_OWNERDRAWFIXED |
 * LBS_NOINTEGRALHEIGHT | LBS_WANTKEYBOARDINPUT | LBS_EXTENDEDSEL |
 * LBS_DISABLENOSCROLL, WS_BORDER, with LBS_MULTICOLUMN and WS_HSCROLL in
 * the Name view, WS_VSCROLL and WS_HSCROLL with details).
 *
 * Observed (WF1-WF3; MS Sans Serif 8 on VGA): ".." first (not in the
 * root), then the directories, then the files, in the sort order; the
 * first entry after ".." selected when a directory is shown. Name view:
 * columns as wide as the widest name plus 21; a line's picture 3 pixels
 * into it, the name 18 pixels after the picture's start; the selected
 * line is highlighted the whole column wide. Details: sizes without
 * separators, right-aligned; the date and time as [intl] gives them.
 */
#include "winfile.h"

static char cls_dir[] = "WFS_Dir";

#define DIR_PICX    3                   /* the picture, into the line */
#define DIR_TEXTX   (DIR_PICX + 17)     /* the name */
/* the details' columns: right edges after the widest name (size, date,
   time) and the attributes' left edge */
#define DX_SIZE     59
#define DX_DATE     55
#define DX_TIME     67
#define DX_FLAGS    33

LPDENTRY dir_item(LPDIRWIN d, int i)
{
    LRESULT r = SendMessage(d->dirlb, LB_GETITEMDATA, i, 0);
    return (r == LB_ERR || !r) ? NULL : (LPDENTRY)r;
}

static int kind_rank(LPDENTRY e)
{
    if (e->pic == BM_PARENT)
        return 0;
    if (e->attr & ATTR_DIR)
        return 1;
    return 2;
}

/* WM_COMPAREITEM: ".." first, then the directories, then the files,
   each by the sort (size and date: largest and newest first; directories
   too, by what DOS says of them: WF03 S03), then by name */
static int compare(LPDIRWIN d, LPDENTRY a, LPDENTRY b)
{
    int ra = kind_rank(a), rb = kind_rank(b), c;
    if (ra != rb)
        return ra < rb ? -1 : 1;
    if (ra > 0) {
        switch (d->sort) {
        case SORT_TYPE:
            c = lstrcmpi(s_ext(a->name), s_ext(b->name));
            if (c)
                return c;
            break;
        case SORT_SIZE:
            if (a->size != b->size)
                return a->size > b->size ? -1 : 1;
            break;
        case SORT_DATE:
            if (a->date != b->date)
                return a->date > b->date ? -1 : 1;
            if (a->time != b->time)
                return a->time > b->time ? -1 : 1;
            break;
        }
    }
    return lstrcmpi(a->name, b->name);
}

/* does the file pass the window's File Type choice? */
static BOOL wanted(LPDIRWIN d, FINDBUF FAR *f)
{
    if ((f->attr & ATTR_HS) && !(d->attribs & ATTR_HS))
        return FALSE;
    if (f->attr & ATTR_DIR)
        return (d->attribs & ATTR_DIR) != 0;
    if (is_program(f->name))
        return (d->attribs & ATTR_PROGS) != 0;
    if (is_document(f->name))
        return (d->attribs & ATTR_DOCS) != 0;
    return (d->attribs & ATTR_OTHER) != 0;
}

static BYTE pic_of(LPCSTR name, BYTE attr)
{
    if (attr & ATTR_DIR)
        return BM_CLOSED;
    if (attr & ATTR_HS)
        return BM_SYSFILE;
    if (is_program(name))
        return BM_PROGRAM;
    if (is_document(name))
        return BM_DOC;
    return BM_FILE;
}

static void add(LPDIRWIN d, FINDBUF FAR *f, BYTE pic)
{
    LPDENTRY e = (LPDENTRY)arena_alloc(&d->darena, sizeof(DENTRY));
    if (!e)
        return;
    e->size = f->size;
    e->date = f->date;
    e->time = f->time;
    e->attr = f->attr;
    e->pic = pic;
    e->sel = 0;
    s_cpy(e->name, f->name, sizeof e->name);
    SendMessage(d->dirlb, LB_ADDSTRING, 0, (LPARAM)e);
}

/* the widest name, for the columns */
static int widest(LPDIRWIN d)
{
    int n = (int)SendMessage(d->dirlb, LB_GETCOUNT, 0, 0), i, w = 0;
    HDC dc = GetDC(d->dirlb);
    HFONT of = SelectObject(dc, g_font);
    for (i = 0; i < n; i++) {
        LPDENTRY e = dir_item(d, i);
        char t[16];
        int x;
        if (!e)
            continue;
        s_cpy(t, e->pic == BM_PARENT ? ".." : e->name, sizeof t);
        if (g_lower)
            s_lower(t);
        x = LOWORD(GetTextExtent(dc, t, s_len(t)));
        if (x > w)
            w = x;
    }
    SelectObject(dc, of);
    ReleaseDC(d->dirlb, dc);
    return w;
}

/* the width of a line with details: as far as the attributes' "rhsa"
   (UNVERIFIED: WF03's scroll bars only show that it is wider than the
   pane) */
static int details_width(LPDIRWIN d)
{
    int x = DIR_TEXTX + (d->colw - 21);
    if (d->view & VIEW_SIZE)
        x += DX_SIZE;
    if (d->view & VIEW_DATE)
        x += DX_DATE;
    if (d->view & VIEW_TIME)
        x += DX_TIME;
    if (d->view & VIEW_FLAGS)
        x += DX_FLAGS + 4 * g_dxtext;
    return x;
}

void dir_columns(LPDIRWIN d)
{
    d->colw = widest(d) + 21;
    if ((d->view & VIEW_DETAILMASK) == VIEW_NAMEONLY)
        SendMessage(d->dirlb, LB_SETCOLUMNWIDTH, d->colw, 0);
    else
        SendMessage(d->dirlb, LB_SETHORIZONTALEXTENT, details_width(d), 0);
}

/* read the directory into the list */
void dir_read(LPDIRWIN d)
{
    char spec[PATHLEN];
    FINDBUF f;
    int a = ATTR_DIR | ATTR_READONLY | ATTR_ARCHIVE | ((d->attribs & ATTR_HS) ? ATTR_HS : 0);

    SendMessage(d->dirlb, WM_SETREDRAW, FALSE, 0);
    SendMessage(d->dirlb, LB_RESETCONTENT, 0, 0);
    arena_free(&d->darena);
    d->nfiles = 0;
    d->bytes = 0;
    if (s_len(d->path) > 3) {
        FINDBUF up;
        s_cpy(up.name, "..", sizeof up.name);
        up.attr = ATTR_DIR;
        up.size = 0;
        up.date = up.time = 0;
        add(d, &up, BM_PARENT);
    }
    /* directories: the pattern applies to them too (WF07: none listed
       with *.TXT) */
    s_cpy(spec, d->path, sizeof spec);
    path_add(spec, d->spec, sizeof spec);
    if (dos_findfirst(spec, a, &f) == 0) {
        do {
            if ((f.attr & ATTR_DIR) && f.name[0] != '.' && wanted(d, &f)) {
                add(d, &f, BM_CLOSED);
                d->nfiles++;            /* "Total n file(s)" counts them (WF01) */
                d->bytes += f.size;     /* and what DOS says of their size */
            }
        } while (dos_findnext(&f) == 0);
    }
    /* files matching the pattern */
    s_cpy(spec, d->path, sizeof spec);
    path_add(spec, d->spec, sizeof spec);
    if (dos_findfirst(spec, a, &f) == 0) {
        do {
            if (f.attr & (ATTR_DIR | ATTR_VOLUME))
                continue;
            if (!wanted(d, &f))
                continue;
            add(d, &f, pic_of(f.name, f.attr));
            d->nfiles++;
            d->bytes += f.size;
        } while (dos_findnext(&f) == 0);
    }
    dir_columns(d);
    {
        int n = (int)SendMessage(d->dirlb, LB_GETCOUNT, 0, 0);
        int first = (n > 1 && s_len(d->path) > 3) ? 1 : 0;
        if (n > 0) {
            SendMessage(d->dirlb, LB_SETCARETINDEX, first, 0);
            /* details start at that line (WF03 S01, WF05) */
            if ((d->view & VIEW_DETAILMASK) != VIEW_NAMEONLY)
                SendMessage(d->dirlb, LB_SETTOPINDEX, first, 0);
        }
    }
    SendMessage(d->dirlb, WM_SETREDRAW, TRUE, 0);
    InvalidateRect(d->dirlb, NULL, TRUE);
}

/* the number of selected files and their bytes */
int dir_selected(LPDIRWIN d, DWORD FAR *bytes)
{
    int n = (int)SendMessage(d->dirlb, LB_GETCOUNT, 0, 0), i, k = 0;
    *bytes = 0;
    for (i = 0; i < n; i++) {
        if (SendMessage(d->dirlb, LB_GETSEL, i, 0) > 0) {
            LPDENTRY e = dir_item(d, i);
            if (e && e->pic != BM_PARENT) {
                k++;
                *bytes += e->size;
            }
        }
    }
    return k;
}

/* ---- keeping the selection over a new listing (a view or sort change):
   the selected names, the caret's first ---- */

/* the saved selection: the caret's line, then the caret's name and the
   selected names, 13 bytes each, ending with an empty one */
HGLOBAL dir_sel_save(LPDIRWIN d)
{
    int n, i, k = 1, caret;
    HGLOBAL h;
    LPSTR p;
    LPDENTRY c;
    if (!d->dirlb)
        return NULL;
    n = (int)SendMessage(d->dirlb, LB_GETCOUNT, 0, 0);
    h = GlobalAlloc(GMEM_MOVEABLE | GMEM_ZEROINIT, (DWORD)(n + 2) * 13 + sizeof(int));
    if (!h)
        return NULL;
    p = (LPSTR)GlobalLock(h);
    caret = (int)SendMessage(d->dirlb, LB_GETCARETINDEX, 0, 0);
    *(int FAR *)p = caret;
    p += sizeof(int);
    c = dir_item(d, caret);
    if (c)
        s_cpy(p, c->name, 13);
    for (i = 0; i < n; i++)
        if (SendMessage(d->dirlb, LB_GETSEL, i, 0) > 0) {
            LPDENTRY e = dir_item(d, i);
            if (e)
                s_cpy(p + 13 * k++, e->name, 13);
        }
    GlobalUnlock(h);
    return h;
}

void dir_sel_restore(LPDIRWIN d, HGLOBAL h)
{
    int n, i, k, first = -1, caret;
    LPSTR p;
    BOOL details = (d->view & VIEW_DETAILMASK) != VIEW_NAMEONLY;
    if (!h)
        return;
    p = (LPSTR)GlobalLock(h);
    caret = *(int FAR *)p;
    p += sizeof(int);
    n = (int)SendMessage(d->dirlb, LB_GETCOUNT, 0, 0);
    if (!p[13]) {
        /* nothing was selected: the caret keeps its line, and with
           details the list starts there (WF03 S01, S04: ".." scrolled
           away) */
        if (caret >= 0 && caret < n) {
            SendMessage(d->dirlb, LB_SETCARETINDEX, caret, 0);
            if (details)
                SendMessage(d->dirlb, LB_SETTOPINDEX, caret, 0);
        }
    } else {
        /* the same names selected, the caret on its name; the list starts
           at the top (WF02 S05; but WF5 E06, the same keys, once started
           a line down: UNVERIFIED) */
        for (i = 0; i < n; i++) {
            LPDENTRY e = dir_item(d, i);
            if (!e)
                continue;
            for (k = 1; p[13 * k]; k++)
                if (!lstrcmp(p + 13 * k, e->name)) {
                    SendMessage(d->dirlb, LB_SETSEL, TRUE, MAKELPARAM(i, 0));
                    if (first < 0)
                        first = i;
                    break;
                }
            if (p[0] && !lstrcmp(p, e->name))
                SendMessage(d->dirlb, LB_SETCARETINDEX, i, 0);
        }
        if (details)
            SendMessage(d->dirlb, LB_SETTOPINDEX, 0, 0);
    }
    GlobalUnlock(h);
    GlobalFree(h);
}

/* the list's style for the view: the Name view is a multicolumn list
   scrolled sideways; details scroll both ways. The list is made again
   (a list box's style is fixed when it is made). */
void dir_set_view(LPDIRWIN d)
{
    RECT r;
    DWORD style = WS_CHILD | WS_VISIBLE | WS_BORDER | WS_HSCROLL |
                  LBS_NOTIFY | LBS_SORT | LBS_OWNERDRAWFIXED | LBS_NOINTEGRALHEIGHT |
                  LBS_WANTKEYBOARDINPUT | LBS_EXTENDEDSEL | LBS_DISABLENOSCROLL;
    BOOL focus = GetFocus() == d->dirlb;
    HGLOBAL sel = d->ready ? dir_sel_save(d) : NULL;
    if ((d->view & VIEW_DETAILMASK) == VIEW_NAMEONLY)
        style |= LBS_MULTICOLUMN;
    else
        style |= WS_VSCROLL;
    GetClientRect(d->dir, &r);
    if (d->dirlb)
        DestroyWindow(d->dirlb);
    d->dirlb = CreateWindow("ListBox", NULL, style, 0, 0, r.right, r.bottom,
                            d->dir, (HMENU)ID_DIRLIST, g_hinst, NULL);
    /* 3.1's list box grows a made window by its border all round: put it
       back where it belongs (seen with WF03's dump) */
    if (d->dirlb)
        MoveWindow(d->dirlb, 0, 0, r.right, r.bottom, FALSE);
    if (d->ready) {
        dir_read(d);
        dir_sel_restore(d, sel);
    }
    if (focus)
        SetFocus(d->dirlb);
}

/* ---- drawing ---- */

static void draw_item(LPDIRWIN d, DRAWITEMSTRUCT FAR *di)
{
    LPDENTRY e = (LPDENTRY)di->itemData;
    HDC dc = di->hDC;
    RECT r = di->rcItem, box;
    BOOL sel = (di->itemState & ODS_SELECTED) != 0;
    BOOL focus = (di->itemState & ODS_FOCUS) != 0;
    /* a selection in a list without the focus is only framed (WF03 S13) */
    BOOL filled = sel && GetFocus() == di->hwndItem;
    HFONT of;
    char t[40];
    BOOL details = (d->view & VIEW_DETAILMASK) != VIEW_NAMEONLY;
    /* with details everything starts a pixel further left (WF03) */
    int left = r.left - (details ? 1 : 0);
    int x = left + DIR_PICX, ty = r.top + (g_dyitem - g_dytext + 1) / 2, tw;

    if ((int)di->itemID < 0 || !e)
        return;
    of = SelectObject(dc, g_font);
    box = r;
    if (filled) {
        fill_sys(dc, &box, COLOR_HIGHLIGHT);
        SetTextColor(dc, GetSysColor(COLOR_HIGHLIGHTTEXT));
    } else {
        fill_sys(dc, &box, COLOR_WINDOW);
        SetTextColor(dc, GetSysColor(COLOR_WINDOWTEXT));
    }
    SetBkMode(dc, TRANSPARENT);
    bm_draw(dc, x, r.top, e->pic, filled);
    s_cpy(t, e->pic == BM_PARENT ? ".." : e->name, sizeof t);
    if (g_lower)
        s_lower(t);
    if (e->pic == BM_PARENT)
        t[0] = 0;
    TextOut(dc, left + DIR_TEXTX, ty, t, s_len(t));
    if ((d->view & VIEW_DETAILMASK) != VIEW_NAMEONLY && e->pic != BM_PARENT) {
        /* the columns' right edges, from the widest name (WF2 A02: the
           size's 59 past it, the date's 55 past that; WF03) */
        int x = left + DIR_TEXTX + (d->colw - 21) + 1;
        if (d->view & VIEW_SIZE) {
            x += DX_SIZE;
            if (!(e->attr & ATTR_DIR)) {
                s_utoa(t, e->size);
                tw = LOWORD(GetTextExtent(dc, t, s_len(t)));
                TextOut(dc, x - tw, ty, t, s_len(t));
            }
        }
        if (d->view & VIEW_DATE) {
            x += DX_DATE;
            fmt_date(t, e->date);
            tw = LOWORD(GetTextExtent(dc, t, s_len(t)));
            TextOut(dc, x - tw, ty, t, s_len(t));
        }
        if (d->view & VIEW_TIME) {
            x += DX_TIME;
            fmt_time(t, e->time);
            if (g_lower)
                s_lower(t);
            tw = LOWORD(GetTextExtent(dc, t, s_len(t)));
            TextOut(dc, x - tw, ty, t, s_len(t));
        }
        if (d->view & VIEW_FLAGS) {
            x += DX_FLAGS;
            fmt_attr(t, e->attr);
            TextOut(dc, x, ty, t, s_len(t));
        }
    }
    if (sel && !filled) {
        /* a pixel in from the line's sides (WF03 S12) */
        HBRUSH hb = CreateSolidBrush(GetSysColor(COLOR_HIGHLIGHT));
        InflateRect(&box, -1, 0);
        FrameRect(dc, &box, hb);
        DeleteObject(hb);
    }
    SelectObject(dc, of);
    if (focus) {
        /* the pattern in the line's colours: on a selected line black and
           yellow dots on the highlight (WF3 B00) */
        SetBkColor(dc, GetSysColor(filled ? COLOR_HIGHLIGHT : COLOR_WINDOW));
        DrawFocusRect(dc, &r);
    }
}

/* ---- the window ---- */

static LPDIRWIN me(HWND w)
{
    return dirwin_of(GetParent(w));
}

LRESULT CALLBACK _export DirWndProc(HWND w, UINT msg, WPARAM wp, LPARAM lp)
{
    LPDIRWIN d;
    switch (msg) {
    case WM_SIZE:
        d = me(w);
        if (d && d->dirlb)
            MoveWindow(d->dirlb, 0, 0, LOWORD(lp), HIWORD(lp), TRUE);
        return 0;
    case WM_SETFOCUS:
        d = me(w);
        if (d && d->dirlb)
            SetFocus(d->dirlb);
        return 0;
    case WM_MEASUREITEM:
        ((MEASUREITEMSTRUCT FAR *)lp)->itemHeight = g_dyitem;
        return TRUE;
    case WM_DRAWITEM:
        d = me(w);
        if (d)
            draw_item(d, (DRAWITEMSTRUCT FAR *)lp);
        return TRUE;
    case WM_COMPAREITEM:
        d = me(w);
        if (!d)
            return 0;
        {
            COMPAREITEMSTRUCT FAR *ci = (COMPAREITEMSTRUCT FAR *)lp;
            return compare(d, (LPDENTRY)ci->itemData1, (LPDENTRY)ci->itemData2);
        }
    case WM_DELETEITEM:
        return TRUE;
    case WM_COMMAND:
        d = me(w);
        if (!d || wp != ID_DIRLIST)
            break;
        switch (HIWORD(lp)) {
        case LBN_SETFOCUS:
        case LBN_KILLFOCUS:
            /* the selection is filled or framed with the focus */
            InvalidateRect(d->dirlb, NULL, FALSE);
            status_update();
            break;
        case LBN_SELCHANGE:
            status_set(d, ST_SELECTED);
            break;
        case LBN_DBLCLK:
            SendMessage(g_frame, WM_COMMAND, IDM_OPEN, 0);
            break;
        }
        return 0;
    case WM_CHARTOITEM:
        return -1;
    case WM_VKEYTOITEM:
        d = me(w);
        if (!d)
            return -1;
        if (wp == VK_TAB) {
            HWND next = GetKeyState(VK_SHIFT) < 0 ?
                        (d->panes == PANES_DIR ? d->drives : d->treelb) : d->drives;
            SetFocus(next);
            return -2;
        }
        if (wp == VK_RETURN) {
            SendMessage(g_frame, WM_COMMAND, IDM_OPEN, 0);
            return -2;
        }
        return -1;
    }
    return DefWindowProc(w, msg, wp, lp);
}

BOOL dir_register(void)
{
    WNDCLASS wc;
    wc.style = 0;
    wc.lpfnWndProc = DirWndProc;
    wc.cbClsExtra = 0;
    wc.cbWndExtra = 0;
    wc.hInstance = g_hinst;
    wc.hIcon = NULL;
    wc.hCursor = LoadCursor(NULL, IDC_ARROW);
    wc.hbrBackground = NULL;
    wc.lpszMenuName = NULL;
    wc.lpszClassName = cls_dir;
    return RegisterClass(&wc);
}
