/*
 * tree.c - w16 File Manager: the directory tree (class DirTree, child 5
 * of a directory window, holding an owner-drawn ListBox, child 3, of its
 * own size; WF1: style LBS_NOTIFY | LBS_OWNERDRAWFIXED |
 * LBS_NOINTEGRALHEIGHT | LBS_WANTKEYBOARDINPUT | LBS_DISABLENOSCROLL,
 * WS_BORDER and both scroll bars).
 *
 * Observed (WF1, WF2; MS Sans Serif 8 on VGA): lines 16 pixels high; a
 * level's folder picture 18 pixels right of its parent's, the root's at
 * x 3; the name 4 pixels after the picture; a vertical line 8 pixels into
 * the parent's picture, down the children (to the middle of the last),
 * and from it a horizontal line through each child's middle to 4 pixels
 * before its picture. The selected directory has the open folder; its
 * picture and name stand on a highlight-coloured box from 3 pixels before
 * the picture, the whole line high, with the focus rectangle on its edge
 * when the tree has the focus, else only a frame of that colour. With
 * Indicate Expandable Branches, folders show "+" when they have
 * subdirectories not shown and "-" when they are expanded (WF2 A06).
 */
#include "winfile.h"

static char cls_tree[] = "DirTree";

#define TREE_X0     3                   /* the root's picture */
#define TREE_DX     18                  /* a level's indent */

static int pic_x(int level)
{
    return TREE_X0 + level * TREE_DX;
}

static LPTNODE item(LPDIRWIN d, int i)
{
    LRESULT r = SendMessage(d->treelb, LB_GETITEMDATA, i, 0);
    return r == LB_ERR ? NULL : (LPTNODE)r;
}

/* "C:\" for the root, else "C:\A\B" */
void tree_path_of(LPTNODE n, LPSTR buf, int drive)
{
    LPTNODE chain[32];
    int k = 0;
    buf[0] = (char)('A' + drive);
    buf[1] = ':';
    buf[2] = '\\';
    buf[3] = 0;
    /* a DOS path has at most 32 levels (64 characters) */
    while (n && n->parent && k < 32) {
        chain[k++] = n;
        n = n->parent;
    }
    while (k--)
        path_add(buf, chain[k]->name, PATHLEN);
}

static BOOL has_subdirs(LPDIRWIN d, LPCSTR path)
{
    char spec[PATHLEN];
    FINDBUF f;
    int a = ATTR_DIR | ((d->attribs & ATTR_HS) ? ATTR_HS : 0);
    s_cpy(spec, path, sizeof spec);
    path_add(spec, "*.*", sizeof spec);
    if (dos_findfirst(spec, a, &f) != 0)
        return FALSE;
    do {
        if ((f.attr & ATTR_DIR) && f.name[0] != '.')
            return TRUE;
    } while (dos_findnext(&f) == 0);
    return FALSE;
}

/* insert the subdirectories of item i (node n) after it, sorted by name;
   returns how many */
static int read_children(LPDIRWIN d, int i, LPTNODE n)
{
    char spec[PATHLEN];
    FINDBUF f;
    HGLOBAL hl = NULL;
    LPTNODE FAR *list = NULL;
    int count = 0, cap = 0, k, j;
    int a = ATTR_DIR | ((d->attribs & ATTR_HS) ? ATTR_HS : 0);

    tree_path_of(n, spec, d->drive);
    path_add(spec, "*.*", sizeof spec);
    if (dos_findfirst(spec, a, &f) == 0) {
        do {
            LPTNODE c;
            if (!(f.attr & ATTR_DIR) || f.name[0] == '.')
                continue;
            c = (LPTNODE)arena_alloc(&d->tarena, sizeof(TNODE));
            if (!c)
                break;
            c->parent = n;
            c->level = (BYTE)(n->level + 1);
            c->flags = 0;
            s_cpy(c->name, f.name, sizeof c->name);
            if (count == cap) {
                HGLOBAL nh;
                cap = cap ? cap * 2 : 32;
                nh = hl ? GlobalReAlloc(hl, (DWORD)cap * sizeof(LPTNODE), GMEM_MOVEABLE)
                        : GlobalAlloc(GMEM_MOVEABLE, (DWORD)cap * sizeof(LPTNODE));
                if (!nh)
                    break;
                if (hl && nh != hl)
                    ;
                hl = nh;
            }
            list = (LPTNODE FAR *)GlobalLock(hl);
            list[count++] = c;
            GlobalUnlock(hl);
        } while (dos_findnext(&f) == 0);
    }
    if (!count) {
        n->flags = (BYTE)((n->flags & ~TF_HASKIDS) | TF_SCANNED | TF_EXPANDED);
        if (hl)
            GlobalFree(hl);
        return 0;
    }
    list = (LPTNODE FAR *)GlobalLock(hl);
    /* insertion sort by name (DOS gives them in directory order) */
    for (k = 1; k < count; k++) {
        LPTNODE t = list[k];
        for (j = k; j > 0 && lstrcmpi(list[j - 1]->name, t->name) > 0; j--)
            list[j] = list[j - 1];
        list[j] = t;
    }
    list[count - 1]->flags |= TF_LASTCHILD;
    SendMessage(d->treelb, WM_SETREDRAW, FALSE, 0);
    for (k = 0; k < count; k++) {
        if (d->view & VIEW_PLUSES) {
            char p[PATHLEN];
            tree_path_of(list[k], p, d->drive);
            if (has_subdirs(d, p))
                list[k]->flags |= TF_HASKIDS;
            list[k]->flags |= TF_SCANNED;
        }
        SendMessage(d->treelb, LB_INSERTSTRING, i + 1 + k, (LPARAM)list[k]);
    }
    SendMessage(d->treelb, WM_SETREDRAW, TRUE, 0);
    GlobalUnlock(hl);
    GlobalFree(hl);
    n->flags |= TF_EXPANDED | TF_HASKIDS | TF_SCANNED;
    return count;
}

/* the item of node n's subdirectory with this name, -1 if none */
static int find_child(LPDIRWIN d, int i, LPCSTR name)
{
    int n = (int)SendMessage(d->treelb, LB_GETCOUNT, 0, 0), k;
    LPTNODE p = item(d, i);
    for (k = i + 1; k < n; k++) {
        LPTNODE c = item(d, k);
        if (!c || c->level <= p->level)
            break;
        if (c->parent == p && !lstrcmpi(c->name, name))
            return k;
    }
    return -1;
}

/* the drive's root and its subdirectories, then the path to d->path
   opened; the current directory selected */
void tree_read(LPDIRWIN d)
{
    LPTNODE root;
    SendMessage(d->treelb, WM_SETREDRAW, FALSE, 0);
    SendMessage(d->treelb, LB_RESETCONTENT, 0, 0);
    arena_free(&d->tarena);
    root = (LPTNODE)arena_alloc(&d->tarena, sizeof(TNODE));
    if (!root)
        return;
    root->parent = NULL;
    root->level = 0;
    root->flags = TF_LASTCHILD;
    root->name[0] = (char)('A' + d->drive);
    root->name[1] = ':';
    root->name[2] = '\\';
    root->name[3] = 0;
    SendMessage(d->treelb, LB_ADDSTRING, 0, (LPARAM)root);
    read_children(d, 0, root);
    tree_select_path(d, d->path);
    {
        /* the current directory's subdirectories are shown too (WF1) */
        int i = (int)SendMessage(d->treelb, LB_GETCURSEL, 0, 0);
        LPTNODE n = item(d, i);
        if (n && !(n->flags & TF_EXPANDED))
            read_children(d, i, n);
    }
    SendMessage(d->treelb, WM_SETREDRAW, TRUE, 0);
    InvalidateRect(d->treelb, NULL, TRUE);
}

/* the tree read again after the disk changed, what was expanded expanded
   again and the same directory selected (3.1 keeps the tree's look after
   a file command: WF08) */
void tree_refresh(LPDIRWIN d)
{
    int n = (int)SendMessage(d->treelb, LB_GETCOUNT, 0, 0), i, k = 0, top;
    HGLOBAL h = GlobalAlloc(GMEM_MOVEABLE | GMEM_ZEROINIT, (DWORD)(n + 1) * PATHLEN);
    LPSTR p;
    char sel[PATHLEN];
    if (!h) {
        tree_read(d);
        return;
    }
    p = (LPSTR)GlobalLock(h);
    top = (int)SendMessage(d->treelb, LB_GETTOPINDEX, 0, 0);
    for (i = 0; i < n; i++) {
        LPTNODE t = item(d, i);
        if (t && (t->flags & TF_EXPANDED) && item(d, i + 1) && item(d, i + 1)->level > t->level)
            tree_path_of(t, p + (DWORD)PATHLEN * k++, d->drive);
    }
    s_cpy(sel, d->path, sizeof sel);
    SendMessage(d->treelb, WM_SETREDRAW, FALSE, 0);
    SendMessage(d->treelb, LB_RESETCONTENT, 0, 0);
    arena_free(&d->tarena);
    {
        LPTNODE root = (LPTNODE)arena_alloc(&d->tarena, sizeof(TNODE));
        if (root) {
            root->parent = NULL;
            root->level = 0;
            root->flags = TF_LASTCHILD;
            root->name[0] = (char)('A' + d->drive);
            root->name[1] = ':';
            root->name[2] = '\\';
            root->name[3] = 0;
            SendMessage(d->treelb, LB_ADDSTRING, 0, (LPARAM)root);
        }
    }
    for (i = 0; i < k; i++) {
        LPSTR q = p + (DWORD)PATHLEN * i;
        int c;
        tree_select_path(d, q);
        c = (int)SendMessage(d->treelb, LB_GETCURSEL, 0, 0);
        if (item(d, c) && !(item(d, c)->flags & TF_EXPANDED))
            read_children(d, c, item(d, c));
    }
    tree_select_path(d, sel);
    SendMessage(d->treelb, LB_SETTOPINDEX, top, 0);
    SendMessage(d->treelb, WM_SETREDRAW, TRUE, 0);
    InvalidateRect(d->treelb, NULL, TRUE);
    GlobalUnlock(h);
    GlobalFree(h);
}

/* a copy of another window's tree as it stands, what is expanded and the
   selection (Window > New Window: WF04 S03) */
void tree_clone(LPDIRWIN d, LPDIRWIN src)
{
    LPTNODE at[33];                     /* the last copy at each level */
    int n = (int)SendMessage(src->treelb, LB_GETCOUNT, 0, 0), i;
    SendMessage(d->treelb, WM_SETREDRAW, FALSE, 0);
    SendMessage(d->treelb, LB_RESETCONTENT, 0, 0);
    arena_free(&d->tarena);
    for (i = 0; i < n; i++) {
        LPTNODE s = item(src, i), c;
        if (!s || s->level > 32)
            continue;
        c = (LPTNODE)arena_alloc(&d->tarena, sizeof(TNODE));
        if (!c)
            break;
        *c = *s;
        c->parent = s->level ? at[s->level - 1] : NULL;
        at[s->level] = c;
        SendMessage(d->treelb, LB_ADDSTRING, 0, (LPARAM)c);
    }
    SendMessage(d->treelb, LB_SETCURSEL,
                (int)SendMessage(src->treelb, LB_GETCURSEL, 0, 0), 0);
    SendMessage(d->treelb, WM_SETREDRAW, TRUE, 0);
    InvalidateRect(d->treelb, NULL, TRUE);
}

/* open the tree down to path (reading what is not read yet) and select it */
void tree_select_path(LPDIRWIN d, LPCSTR path)
{
    char comp[13];
    LPCSTR p = path + 3;                /* past "C:\" */
    int i = 0;
    while (*p) {
        int k = 0, c;
        LPTNODE n = item(d, i);
        while (*p && *p != '\\') {
            if (k < 12)
                comp[k++] = *p;
            p++;
        }
        comp[k] = 0;
        if (*p == '\\')
            p++;
        if (!(n->flags & TF_EXPANDED))
            read_children(d, i, n);
        c = find_child(d, i, comp);
        if (c < 0)
            break;
        i = c;
    }
    SendMessage(d->treelb, LB_SETCURSEL, i, 0);
}

/* Tree > Expand One Level (how 1), Expand Branch (how 2: every level
   below), Expand All (the root, every level) */
void tree_expand(LPDIRWIN d, int i, int how)
{
    LPTNODE n = item(d, i);
    int k;
    if (!n)
        return;
    SendMessage(d->treelb, WM_SETREDRAW, FALSE, 0);
    if (!(n->flags & TF_EXPANDED))
        read_children(d, i, n);
    if (how > 1) {
        /* every item below n, in order; new ones come right after their
           parent, so walk on until the level is back at n's */
        for (k = i + 1;; k++) {
            LPTNODE c = item(d, k);
            if (!c || c->level <= n->level)
                break;
            if (!(c->flags & TF_EXPANDED))
                read_children(d, k, c);
        }
    }
    SendMessage(d->treelb, WM_SETREDRAW, TRUE, 0);
    InvalidateRect(d->treelb, NULL, TRUE);
}

void tree_collapse(LPDIRWIN d, int i)
{
    LPTNODE n = item(d, i);
    int cur;
    if (!n)
        return;
    SendMessage(d->treelb, WM_SETREDRAW, FALSE, 0);
    for (;;) {
        LPTNODE c = item(d, i + 1);
        if (!c || c->level <= n->level)
            break;
        SendMessage(d->treelb, LB_DELETESTRING, i + 1, 0);
    }
    n->flags &= ~TF_EXPANDED;
    cur = (int)SendMessage(d->treelb, LB_GETCURSEL, 0, 0);
    if (cur == LB_ERR)
        SendMessage(d->treelb, LB_SETCURSEL, i, 0);
    SendMessage(d->treelb, WM_SETREDRAW, TRUE, 0);
    InvalidateRect(d->treelb, NULL, TRUE);
}

/* ---- drawing ---- */

static void draw_item(LPDIRWIN d, DRAWITEMSTRUCT FAR *di)
{
    LPTNODE n = (LPTNODE)di->itemData;
    HDC dc = di->hDC;
    RECT r = di->rcItem, box;
    int x, y = r.top, mid = r.top + g_dyitem / 2, pic, tw;
    BOOL sel = (di->itemState & ODS_SELECTED) != 0;
    BOOL focus = GetFocus() == di->hwndItem;
    BOOL plus;
    char name[16];
    LPTNODE a;
    HPEN pen, op;
    DWORD ext;

    if ((int)di->itemID < 0 || !n)
        return;
    if (di->itemAction == ODA_FOCUS)
        ;                               /* drawn with the item below */
    x = pic_x(n->level);
    /* the lines */
    pen = CreatePen(PS_SOLID, 1, GetSysColor(COLOR_WINDOWTEXT));
    op = SelectObject(dc, pen);
    fill_sys(dc, &r, COLOR_WINDOW);
    if (n->level > 0) {
        int lx = pic_x(n->level - 1) + 8;
        MoveTo(dc, lx, y);
        LineTo(dc, lx, (n->flags & TF_LASTCHILD) ? mid + 1 : r.bottom);
        MoveTo(dc, lx, mid);
        LineTo(dc, x - 3, mid);
        for (a = n->parent; a && a->level > 0; a = a->parent) {
            if (!(a->flags & TF_LASTCHILD)) {
                int ax = pic_x(a->level - 1) + 8;
                MoveTo(dc, ax, y);
                LineTo(dc, ax, r.bottom);
            }
        }
    }
    SelectObject(dc, op);
    DeleteObject(pen);

    /* the picture */
    plus = (d->view & VIEW_PLUSES) != 0;
    if (sel) {
        if (plus && (n->flags & TF_EXPANDED) && n->level == 0)
            pic = BM_OPENMINUS;
        else if (plus && (n->flags & TF_EXPANDED) && (n->flags & TF_HASKIDS))
            pic = BM_OPENMINUS;
        else if (plus && !(n->flags & TF_EXPANDED) && (n->flags & TF_HASKIDS))
            pic = BM_OPENPLUS;
        else
            pic = BM_OPEN;
    } else {
        if (plus && (n->flags & TF_EXPANDED) && (n->flags & TF_HASKIDS))
            pic = BM_CLOSEDMINUS;
        else if (plus && !(n->flags & TF_EXPANDED) && (n->flags & TF_HASKIDS))
            pic = BM_CLOSEDPLUS;
        else
            pic = BM_CLOSED;
    }
    s_cpy(name, n->name, sizeof name);
    if (g_lower)
        s_lower(name);
    {
        HFONT of = SelectObject(dc, g_font);
        ext = GetTextExtent(dc, name, s_len(name));
        tw = LOWORD(ext);
        box.left = x - 3;
        box.top = r.top;
        box.right = x + CX_BM + 3 + tw + 3;
        box.bottom = r.bottom;
        if (sel && focus) {
            HBRUSH hb = CreateSolidBrush(GetSysColor(COLOR_HIGHLIGHT));
            FillRect(dc, &box, hb);
            DeleteObject(hb);
            SetTextColor(dc, GetSysColor(COLOR_HIGHLIGHTTEXT));
        } else {
            SetTextColor(dc, GetSysColor(COLOR_WINDOWTEXT));
        }
        SetBkMode(dc, TRANSPARENT);
        bm_draw(dc, x, y, pic, sel && focus);
        TextOut(dc, x + CX_BM + 3, y + (g_dyitem - g_dytext + 1) / 2, name, s_len(name));
        SelectObject(dc, of);
    }
    if (sel) {
        if (focus) {
            /* the focus rectangle's pattern takes the DC's colours: 3.1's
               inverts the box's colours with black and white dots (WF01) */
            SetTextColor(dc, GetSysColor(COLOR_WINDOWTEXT));
            SetBkColor(dc, GetSysColor(COLOR_WINDOW));
            DrawFocusRect(dc, &box);
        } else {
            HBRUSH hb = CreateSolidBrush(GetSysColor(COLOR_HIGHLIGHT));
            FrameRect(dc, &box, hb);
            DeleteObject(hb);
        }
    }
}

/* ---- the window ---- */

static LPDIRWIN me(HWND w)
{
    return dirwin_of(GetParent(w));
}

static int cur(LPDIRWIN d)
{
    return (int)SendMessage(d->treelb, LB_GETCURSEL, 0, 0);
}

/* the selected directory changed: show it in the list and the title */
static void selected(LPDIRWIN d)
{
    int i = cur(d);
    LPTNODE n = item(d, i);
    char p[PATHLEN];
    if (!n)
        return;
    tree_path_of(n, p, d->drive);
    treewin_change_dir(d, p);
}

/* a letter: the next directory whose name starts with it */
static int find_letter(LPDIRWIN d, int ch)
{
    int n = (int)SendMessage(d->treelb, LB_GETCOUNT, 0, 0), k, i = cur(d);
    char c = (char)(DWORD)AnsiUpper((LPSTR)(DWORD)(BYTE)ch);
    for (k = 1; k <= n; k++) {
        LPTNODE t = item(d, (i + k) % n);
        char f;
        if (!t || !t->parent)
            continue;
        f = (char)(DWORD)AnsiUpper((LPSTR)(DWORD)(BYTE)t->name[0]);
        if (f == c)
            return (i + k) % n;
    }
    return -1;
}

LRESULT CALLBACK _export TreeControlWndProc(HWND w, UINT msg, WPARAM wp, LPARAM lp)
{
    LPDIRWIN d;
    int i;
    switch (msg) {
    case WM_CREATE:
        CreateWindow("ListBox", NULL,
                     WS_CHILD | WS_VISIBLE | WS_BORDER | WS_VSCROLL | WS_HSCROLL |
                     LBS_NOTIFY | LBS_OWNERDRAWFIXED | LBS_NOINTEGRALHEIGHT |
                     LBS_WANTKEYBOARDINPUT | LBS_DISABLENOSCROLL,
                     0, 0, 0, 0, w, (HMENU)ID_TREELIST, g_hinst, NULL);
        return 0;
    case WM_SIZE:
        MoveWindow(GetDlgItem(w, ID_TREELIST), 0, 0, LOWORD(lp), HIWORD(lp), TRUE);
        return 0;
    case WM_SETFOCUS:
        SetFocus(GetDlgItem(w, ID_TREELIST));
        return 0;
    case WM_MEASUREITEM:
        ((MEASUREITEMSTRUCT FAR *)lp)->itemHeight = g_dyitem;
        return TRUE;
    case WM_DRAWITEM:
        d = me(w);
        if (d)
            draw_item(d, (DRAWITEMSTRUCT FAR *)lp);
        return TRUE;
    case WM_COMMAND:
        d = me(w);
        if (!d || wp != ID_TREELIST)
            break;
        switch (HIWORD(lp)) {
        case LBN_SELCHANGE:
            selected(d);
            break;
        case LBN_DBLCLK:
            i = cur(d);
            if (item(d, i) && (item(d, i)->flags & TF_EXPANDED) && item(d, i + 1) &&
                item(d, i + 1)->level > item(d, i)->level)
                tree_collapse(d, i);
            else
                tree_expand(d, i, 1);
            break;
        case LBN_SETFOCUS:
            d->lastpane = ID_TREELIST;
            /* fall through */
        case LBN_KILLFOCUS:
            InvalidateRect(d->treelb, NULL, FALSE);
            status_update();
            break;
        }
        return 0;
    case WM_CHARTOITEM:
        d = me(w);
        if (!d)
            return -1;
        switch (wp) {
        case '+':
            tree_expand(d, cur(d), 1);
            return -2;
        case '*':
            tree_expand(d, cur(d), 2);
            return -2;
        case '-':
            tree_collapse(d, cur(d));
            return -2;
        }
        i = find_letter(d, wp);
        return i >= 0 ? i : -2;
    case WM_VKEYTOITEM:
        d = me(w);
        if (!d)
            return -1;
        if (wp == VK_TAB) {
            HWND next = GetKeyState(VK_SHIFT) < 0 ? d->drives :
                        (d->panes == PANES_TREE ? d->drives : d->dirlb);
            SetFocus(next);
            return -2;
        }
        if (wp == VK_RETURN) {
            i = cur(d);
            if (item(d, i) && (item(d, i)->flags & TF_EXPANDED) && item(d, i + 1) &&
                item(d, i + 1)->level > item(d, i)->level)
                tree_collapse(d, i);
            else
                tree_expand(d, i, 1);
            return -2;
        }
        return -1;
    }
    return DefWindowProc(w, msg, wp, lp);
}

BOOL tree_register(void)
{
    WNDCLASS wc;
    wc.style = 0;
    wc.lpfnWndProc = TreeControlWndProc;
    wc.cbClsExtra = 0;
    wc.cbWndExtra = 0;
    wc.hInstance = g_hinst;
    wc.hIcon = NULL;
    wc.hCursor = LoadCursor(NULL, IDC_ARROW);
    wc.hbrBackground = NULL;
    wc.lpszMenuName = NULL;
    wc.lpszClassName = cls_tree;
    return RegisterClass(&wc);
}
