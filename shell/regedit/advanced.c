/*
 * advanced.c - w16 REGEDIT's advanced view (REGEDIT /V): the whole
 * database as a tree (ARCHITECTURE.md 15.8; observed on Windows 3.1,
 * probes RE2, RE4, RE6, RE7, RE10, RE11, RE14-RE16; its calls into SHELL
 * logged by XHELL.DLL, RE15, RE16).
 *
 * The view: "Full Path:" the selected key's path ("\" the root,
 * "\MPlayer\shell"), "Value:" an edit of its value, and below them an
 * owner-drawn list of every key, always expanded, each line "name" or
 * "name = value", subkeys in RegEnumKey's order (newest first). Lines are
 * 16 pixels high; a key at depth d (the root "\" 0) has its text 4 + 12d
 * pixels in; from depth 1 a tree line runs down from the top of its row at
 * 12 pixels left of the text and turns right at the row's 9th pixel for 8
 * pixels; it goes on to the row's bottom while a later sibling follows, as
 * do the lines of ancestors with later siblings. The selection is
 * highlighted from 4 pixels before the text to 4 past it, with the focus
 * rectangle round that.
 *
 * The view edits its own copy of the database: it is read at the start
 * (RegCreateKey of HKEY_CLASSES_ROOT, then every key) and again after
 * Save Changes and Restore To Last Save; nothing reaches SHELL before Save
 * Changes. The lines are the copy: the list's strings, a hidden list of
 * their paths (431h's neighbour 621h) and one of their values (622h),
 * each line's depth and a "to write" mark in its item data, and a list of
 * the keys deleted. Save Changes (RE16): RegOpenKey of the root, each
 * deleted key's RegDeleteKey (in the order deleted), RegCloseKey; then
 * RegCreateKey of the root, a RegSetValue for every marked line from the
 * bottom of the tree up (so a key's subkeys are made before it, and come
 * out in the order shown), RegCloseKey; SHELL writes REG.DAT at each
 * close. Restore To Last Save asks "Are you sure you want to lose all
 * changes?" (OK/Cancel) and reads the database again; Exit with changes
 * asks "Changes have been made to the registration database. Do you want
 * to save them?" (Yes/No/Cancel); No writes nothing.
 *
 * Typing a value marks its line. Add Key (Path:, Key:, Value:) puts a new
 * key first under the selected one; Copy Key (From: the selected key's
 * path, editable; To:) puts a copy of a subtree first under the To key's
 * parent; Delete Key (Key:, also the Del key) removes a subtree; each new
 * key is selected, after a deletion the line that takes its place. A path
 * not in the tree: "The key '...' does not exist." Find Key looks for a
 * key whose name contains the text (any case) from the line after the
 * selection down; Find Next again; nothing found leaves the selection.
 * Save Registration File writes the selected key's subtree from the copy
 * (regfile.c's format).
 */
#include "regedit.h"

#define IDC_DELETED   0x624     /* w16's own: the keys deleted (not shown) */
#define MARK_WRITE    1

static HWND tree(HWND v) { return GetDlgItem(v, IDC_TREE); }
static HWND paths(HWND v) { return GetDlgItem(v, IDC_HIDDEN1); }
static HWND values(HWND v) { return GetDlgItem(v, IDC_HIDDEN2); }
static char find_text[PATHMAX];
static int list_top, tmpl_cx, tmpl_cy, tmpl_w;
static BOOL loading;            /* the edit is being filled: no change */

/* ---- the copy ---- */

static WORD depth_of(HWND v, int i) { return LOWORD(SendMessage(tree(v), LB_GETITEMDATA, i, 0)); }
static WORD mark_of(HWND v, int i)  { return HIWORD(SendMessage(tree(v), LB_GETITEMDATA, i, 0)); }

static void get_str(HWND lb, int i, LPSTR out)
{
    out[0] = 0;
    if (i >= 0 && SendMessage(lb, LB_GETTEXTLEN, i, 0) < PATHMAX)
        SendMessage(lb, LB_GETTEXT, i, (LPARAM)out);
}

static void line_text(LPSTR out, LPCSTR name, LPCSTR val)
{
    if (val[0])
        wsprintf(out, "%s = %s", name, val);
    else
        lstrcpy(out, name);
}

static void insert_line(HWND v, int at, LPCSTR path, LPCSTR val, WORD depth, WORD mark)
{
    static char text[2 * PATHMAX];
    LPCSTR name = path[0] ? path_last((LPSTR)path) : "\\";
    line_text(text, name, val);
    at = (int)SendMessage(tree(v), LB_INSERTSTRING, at, (LPARAM)(LPSTR)text);
    SendMessage(tree(v), LB_SETITEMDATA, at, MAKELONG(depth, mark));
    SendMessage(paths(v), LB_INSERTSTRING, at, (LPARAM)path);
    SendMessage(values(v), LB_INSERTSTRING, at, (LPARAM)val);
}

static void delete_line(HWND v, int i)
{
    SendMessage(tree(v), LB_DELETESTRING, i, 0);
    SendMessage(paths(v), LB_DELETESTRING, i, 0);
    SendMessage(values(v), LB_DELETESTRING, i, 0);
}

static char tpath[PATHMAX];

static void read_keys(HWND v, HKEY k, WORD depth)
{
    static char name[128], val[PATHMAX];
    HKEY c;
    DWORD i;
    int n = lstrlen(tpath);
    for (i = 0; !RegEnumKey(k, i, name, sizeof name); i++) {
        LONG cb = sizeof val;
        if (n + lstrlen(name) + 2 >= PATHMAX)
            continue;
        if (n) lstrcat(tpath, "\\");
        lstrcat(tpath, name);
        if (RegQueryValue(k, name, val, &cb))
            val[0] = 0;
        insert_line(v, -1, tpath, val, depth, 0);
        if (!RegOpenKey(k, name, &c)) {
            read_keys(v, c, depth + 1);
            RegCloseKey(c);
        }
        tpath[n] = 0;
    }
}

/* the database read into the lines (RE15: RegCreateKey of the root) */
static void load(HWND v)
{
    HKEY k;
    SendMessage(tree(v), WM_SETREDRAW, FALSE, 0);
    SendMessage(tree(v), LB_RESETCONTENT, 0, 0);
    SendMessage(paths(v), LB_RESETCONTENT, 0, 0);
    SendMessage(values(v), LB_RESETCONTENT, 0, 0);
    SendMessage(GetDlgItem(v, IDC_DELETED), LB_RESETCONTENT, 0, 0);
    insert_line(v, -1, "", "", 0, 0);
    if (!RegCreateKey(HKEY_CLASSES_ROOT, NULL, &k)) {
        tpath[0] = 0;
        read_keys(v, k, 1);
        RegCloseKey(k);
    }
    SendMessage(tree(v), WM_SETREDRAW, TRUE, 0);
    InvalidateRect(tree(v), NULL, TRUE);
    g_dirty = FALSE;
}

/* Save Changes, as RE16 saw it */
static void save_changes(HWND v)
{
    static char p[PATHMAX], val[PATHMAX];
    HWND del = GetDlgItem(v, IDC_DELETED);
    HKEY k;
    int i, n;
    if (!RegOpenKey(HKEY_CLASSES_ROOT, NULL, &k)) {
        n = (int)SendMessage(del, LB_GETCOUNT, 0, 0);
        for (i = 0; i < n; i++) {
            get_str(del, i, p);
            RegDeleteKey(HKEY_CLASSES_ROOT, p);
        }
        RegCloseKey(k);
    }
    if (!RegCreateKey(HKEY_CLASSES_ROOT, NULL, &k)) {
        for (i = (int)SendMessage(tree(v), LB_GETCOUNT, 0, 0) - 1; i >= 0; i--) {
            if (!(mark_of(v, i) & MARK_WRITE))
                continue;
            get_str(paths(v), i, p);
            get_str(values(v), i, val);
            RegSetValue(HKEY_CLASSES_ROOT, p, REG_SZ, val, 0);
        }
        RegCloseKey(k);
    }
}

/* the line of a path ("" the root), -1 if none */
static int find_path(HWND v, LPCSTR path)
{
    static char p[PATHMAX];
    int n = (int)SendMessage(tree(v), LB_GETCOUNT, 0, 0), i;
    if (!path[0])
        return 0;
    for (i = 1; i < n; i++) {
        get_str(paths(v), i, p);
        if (!lstrcmpi(p, path))
            return i;
    }
    return -1;
}

/* the line after line i's subtree */
static int subtree_end(HWND v, int i);

/* Merge in this view goes into the copy (RE19): each key of the file,
   made with its missing ancestors as new lines first under their parents,
   or an existing one's value set; all marked to write */
static HWND merge_view;
static char merged[PATHMAX];

static BOOL inserted;

static int ensure_line(HWND v, LPCSTR path)
{
    char parent[PATHMAX];               /* one per level: the recursion */
    int i = find_path(v, path), p;
    if (i >= 0)
        return i;
    lstrcpy(parent, path);
    *path_last(parent) = 0;
    if (parent[0])
        parent[lstrlen(parent) - 1] = 0;
    p = ensure_line(v, parent);
    if (p < 0)
        return -1;
    insert_line(v, p + 1, path, "", depth_of(v, p) + 1, MARK_WRITE);
    inserted = TRUE;
    return p + 1;
}

void adv_merge_line(LPCSTR key, LPCSTR val)
{
    static char text[2 * PATHMAX], k[PATHMAX];
    HWND v = merge_view;
    int i;
    if (!key[0] || lstrlen(key) >= PATHMAX)
        return;
    lstrcpy(k, key);
    inserted = FALSE;
    i = ensure_line(v, k);
    if (i <= 0)
        return;
    line_text(text, path_last(k), val);
    {
        WORD d = depth_of(v, i);
        SendMessage(tree(v), LB_DELETESTRING, i, 0);
        SendMessage(tree(v), LB_INSERTSTRING, i, (LPARAM)(LPSTR)text);
        SendMessage(tree(v), LB_SETITEMDATA, i, MAKELONG(d, MARK_WRITE));
    }
    SendMessage(values(v), LB_DELETESTRING, i, 0);
    SendMessage(values(v), LB_INSERTSTRING, i, (LPARAM)val);
    /* the line merged is selected, an existing key's too (RE6, RE19) */
    SendMessage(tree(v), LB_SETCURSEL, i, 0);
    lstrcpy(merged, k);
    g_dirty = TRUE;
}

static int subtree_end(HWND v, int i)
{
    int n = (int)SendMessage(tree(v), LB_GETCOUNT, 0, 0);
    WORD d = depth_of(v, i);
    for (i++; i < n && depth_of(v, i) > d; i++)
        ;
    return i;
}

static void show_sel(HWND v)
{
    static char p[PATHMAX + 1], val[PATHMAX];
    int s = (int)SendMessage(tree(v), LB_GETCURSEL, 0, 0);
    if (s < 0)
        s = 0;
    get_str(paths(v), s, g_sel);
    p[0] = '\\';
    lstrcpy(p + 1, g_sel);
    SetDlgItemText(v, IDC_PATH, p);
    get_str(values(v), s, val);
    loading = TRUE;
    SetDlgItemText(v, IDC_VALUE, val);
    loading = FALSE;
}

static void select_line(HWND v, int i)
{
    int n = (int)SendMessage(tree(v), LB_GETCOUNT, 0, 0);
    if (i >= n)
        i = n - 1;
    if (i < 0)
        i = 0;
    SendMessage(tree(v), LB_SETCURSEL, i, 0);
    show_sel(v);
}

void adv_refresh(HWND v)
{
    static char keep[PATHMAX];
    int s;
    lstrcpy(keep, g_sel);
    load(v);
    s = find_path(v, keep);
    select_line(v, s < 0 ? 0 : s);
}

/* does line i's ancestor (or itself) at depth a have a later sibling? */
static BOOL later_sibling(HWND v, int i, WORD a)
{
    int n = (int)SendMessage(tree(v), LB_GETCOUNT, 0, 0);
    for (i++; i < n; i++) {
        WORD d = depth_of(v, i);
        if (d < a)
            return FALSE;
        if (d == a)
            return TRUE;
    }
    return FALSE;
}

static void draw_item(HWND v, DRAWITEMSTRUCT FAR *d)
{
    static char text[2 * PATHMAX];
    WORD depth, a;
    int y = d->rcItem.top, tx;
    HDC dc = d->hDC;
    HPEN pen, old;
    DWORD ext;
    RECT r;
    if (d->itemID == (UINT)-1)
        return;
    depth = LOWORD(d->itemData);
    tx = d->rcItem.left + 4 + 12 * depth;
    if (d->itemAction & ODA_DRAWENTIRE) {
        FillRect(dc, &d->rcItem, (HBRUSH)(COLOR_WINDOW + 1));
        pen = CreatePen(PS_SOLID, 1, GetSysColor(COLOR_WINDOWTEXT));
        old = SelectObject(dc, pen);
        for (a = 1; a <= depth; a++) {
            int x = d->rcItem.left + 4 + 12 * (a - 1);
            BOOL more = later_sibling(v, d->itemID, a);
            if (a == depth) {
                MoveTo(dc, x, y);
                LineTo(dc, x, more ? y + 16 : y + 9);
                MoveTo(dc, x, y + 8);
                LineTo(dc, x + 8, y + 8);
            } else if (more) {
                MoveTo(dc, x, y);
                LineTo(dc, x, y + 16);
            }
        }
        SelectObject(dc, old);
        DeleteObject(pen);
    }
    /* the text from the copy (the paths and values), so that a value typed
       shows only when the line is drawn again (RE7) */
    {
        static char p[PATHMAX], val[PATHMAX];
        get_str(paths(v), d->itemID, p);
        get_str(values(v), d->itemID, val);
        line_text(text, d->itemID ? path_last(p) : "\\", val);
    }
    ext = GetTextExtent(dc, text, lstrlen(text));
    r.left = tx - 4;
    r.right = tx + LOWORD(ext) + 4;
    r.top = y;
    r.bottom = y + 16;
    if (d->itemAction & (ODA_DRAWENTIRE | ODA_SELECT)) {
        if (d->itemState & ODS_SELECTED) {
            SetBkColor(dc, GetSysColor(COLOR_HIGHLIGHT));
            SetTextColor(dc, GetSysColor(COLOR_HIGHLIGHTTEXT));
        } else {
            SetBkColor(dc, GetSysColor(COLOR_WINDOW));
            SetTextColor(dc, GetSysColor(COLOR_WINDOWTEXT));
        }
        ExtTextOut(dc, tx, y + 1, ETO_OPAQUE, &r, text, lstrlen(text), NULL);
        if (d->itemState & ODS_FOCUS)
            DrawFocusRect(dc, &r);
    } else if (d->itemAction & ODA_FOCUS) {
        DrawFocusRect(dc, &r);
    }
}

/* ---- the dialogs ---- */

static int dlg_id;
static HWND dlg_view;

/* "The key '...' does not exist." */
static void no_key(HWND d, LPCSTR path)
{
    msgbox(d, IDS_NOKEY, path, MB_OK);
}

BOOL FAR PASCAL _export KeyDlgProc(HWND d, UINT msg, WPARAM wp, LPARAM lp)
{
    static char a[PATHMAX + 1], b[PATHMAX], p[PATHMAX], q[PATHMAX];
    HWND v = dlg_view;
    (void)lp;
    switch (msg) {
    case WM_INITDIALOG:
        a[0] = '\\';
        lstrcpy(a + 1, g_sel);
        if (dlg_id == DLG_ADDKEY)
            SetDlgItemText(d, IDC_KEYPATH, a);
        else if (dlg_id == DLG_COPYKEY || dlg_id == DLG_DELKEY)
            SetDlgItemText(d, IDC_KEY, a);
        else if (dlg_id == DLG_FINDKEY)
            SetDlgItemText(d, IDC_KEY, find_text);
        return TRUE;
    case WM_COMMAND:
        switch (wp) {
        case IDOK: {
            LPSTR pa = a, pb = b;
            int i, j, n;
            GetDlgItemText(d, IDC_KEY, a, sizeof a);
            GetDlgItemText(d, IDC_KEYVALUE, b, sizeof b);
            if (dlg_id == DLG_FINDKEY) {
                lstrcpy(find_text, a);
                EndDialog(d, IDOK);
                return TRUE;
            }
            if (*pa == '\\') pa++;
            if (*pb == '\\') pb++;
            if (dlg_id == DLG_ADDKEY) {
                /* a new key first under the selected one */
                i = find_path(v, g_sel);
                if (!a[0] || i < 0 || lstrlen(g_sel) + lstrlen(a) + 2 >= PATHMAX)
                    return TRUE;
                lstrcpy(p, g_sel);
                if (p[0]) lstrcat(p, "\\");
                lstrcat(p, a);
                insert_line(v, i + 1, p, b, depth_of(v, i) + 1, MARK_WRITE);
                lstrcpy(g_sel, p);
            } else if (dlg_id == DLG_DELKEY) {
                i = find_path(v, pa);
                if (i <= 0) {
                    no_key(d, pa);
                    return TRUE;
                }
                n = subtree_end(v, i) - i;
                while (n--)
                    delete_line(v, i);
                SendMessage(GetDlgItem(v, IDC_DELETED), LB_ADDSTRING, 0, (LPARAM)(LPSTR)pa);
                get_str(paths(v), i < (int)SendMessage(tree(v), LB_GETCOUNT, 0, 0) ? i : i - 1, g_sel);
            } else {
                /* Copy: the From subtree, renamed, first under To's parent */
                int to_parent, end, at;
                WORD dd;
                i = find_path(v, pa);
                if (i <= 0) {
                    no_key(d, pa);
                    return TRUE;
                }
                if (!*pb)
                    return TRUE;
                lstrcpy(q, pb);
                *path_last(q) = 0;
                if (q[0] && q[lstrlen(q) - 1] == '\\')
                    q[lstrlen(q) - 1] = 0;
                to_parent = find_path(v, q);
                if (to_parent < 0) {
                    no_key(d, q);
                    return TRUE;
                }
                end = subtree_end(v, i);
                at = to_parent + 1;
                dd = depth_of(v, to_parent) + 1 - depth_of(v, i);
                /* the source moves down as lines go in above it */
                for (j = 0; j < end - i; j++) {
                    int src = i + j + (at <= i ? j : 0);
                    static char sp[PATHMAX], sv[PATHMAX];
                    get_str(paths(v), src, sp);
                    get_str(values(v), src, sv);
                    if (lstrlen(pb) + lstrlen(sp) - lstrlen(pa) + 1 >= PATHMAX)
                        continue;
                    lstrcpy(p, pb);
                    lstrcat(p, sp + lstrlen(pa));
                    insert_line(v, at + j, p, sv, depth_of(v, src) + dd, MARK_WRITE);
                }
                lstrcpy(g_sel, pb);
            }
            g_dirty = TRUE;
            EndDialog(d, IDOK);
            return TRUE;
        }
        case IDCANCEL:
            EndDialog(d, IDCANCEL);
            return TRUE;
        case IDC_HELP:
            help(HELP_CONTEXT, 0);
            return TRUE;
        }
        break;
    }
    return FALSE;
}

static int key_dialog(HWND v, int id)
{
    FARPROC fp = MakeProcInstance((FARPROC)KeyDlgProc, g_inst);
    int r;
    dlg_id = id;
    dlg_view = v;
    r = DialogBox(g_inst, MAKEINTRESOURCE(id), g_frame, (DLGPROC)fp);
    FreeProcInstance(fp);
    return r;
}

static void find_next(HWND v)
{
    static char t[PATHMAX], up[PATHMAX];
    int n = (int)SendMessage(tree(v), LB_GETCOUNT, 0, 0);
    int i = (int)SendMessage(tree(v), LB_GETCURSEL, 0, 0);
    int lu;
    if (!find_text[0])
        return;
    lstrcpy(up, find_text);
    AnsiUpper(up);
    lu = lstrlen(up);
    for (i++; i < n; i++) {
        LPSTR s;
        int ls, k;
        get_str(paths(v), i, t);
        s = path_last(t);
        AnsiUpper(s);
        ls = lstrlen(s);
        for (k = 0; k + lu <= ls; k++) {
            char c = s[k + lu];
            int eq;
            s[k + lu] = 0;
            eq = !lstrcmp(s + k, up);
            s[k + lu] = c;
            if (eq) {
                select_line(v, i);
                return;
            }
        }
    }
    MessageBeep(0);
}

/* Save Registration File: the selected key's subtree from the copy */
static void save_reg_file(HWND v, LPCSTR file)
{
    static char p[PATHMAX], val[PATHMAX];
    OFSTRUCT of;
    HFILE f = OpenFile(file, &of, OF_CREATE | OF_WRITE);
    int i, end, s = (int)SendMessage(tree(v), LB_GETCURSEL, 0, 0);
    if (f == HFILE_ERROR) {
        msgbox(g_frame, IDS_CANTOPEN, file, MB_OK | MB_ICONEXCLAMATION);
        return;
    }
    if (s < 0)
        s = 0;
    _lwrite(f, "REGEDIT\r\n", 9);
    end = subtree_end(v, s);
    for (i = s ? s : 1; i < end; i++) {
        BOOL kids = i + 1 < end && depth_of(v, i + 1) > depth_of(v, i);
        get_str(values(v), i, val);
        if (!val[0] && kids)
            continue;
        get_str(paths(v), i, p);
        _lwrite(f, "HKEY_CLASSES_ROOT\\", 18);
        _lwrite(f, p, lstrlen(p));
        _lwrite(f, " = ", 3);
        _lwrite(f, val, lstrlen(val));
        _lwrite(f, "\r\n", 2);
    }
    _lclose(f);
}

void adv_command(HWND v, UINT id)
{
    static char f[PATHMAX];
    switch (id) {
    case IDM_SAVECHANGES:
        save_changes(v);
        g_sel[0] = 0;               /* the root selected again (RE4) */
        adv_refresh(v);
        break;
    case IDM_RESTORE:
        if (msgbox(g_frame, IDS_LOSECHANGES, NULL, MB_OKCANCEL) != IDOK)
            return;
        g_sel[0] = 0;
        adv_refresh(v);
        break;
    case IDM_ADD:
    case IDM_COPY:
        if (key_dialog(v, id == IDM_ADD ? DLG_ADDKEY : DLG_COPYKEY) == IDOK) {
            InvalidateRect(tree(v), NULL, TRUE);
            select_line(v, find_path(v, g_sel));
        }
        break;
    case IDM_DELETE:
        if (key_dialog(v, DLG_DELKEY) == IDOK) {
            InvalidateRect(tree(v), NULL, TRUE);
            /* the line in its place, scrolled to from the top (RE16) */
            SendMessage(tree(v), LB_SETTOPINDEX, 0, 0);
            select_line(v, find_path(v, g_sel));
        }
        break;
    case IDM_FIND:
        if (key_dialog(v, DLG_FINDKEY) == IDOK)
            find_next(v);
        break;
    case IDM_FINDNEXT:
        find_next(v);
        break;
    case IDM_SAVEFILE:
        lstrcpy(f, "*.reg");
        if (file_dialog(g_frame, TRUE, IDS_SAVETITLE, IDS_REGFILTER, "reg", f, sizeof f))
            save_reg_file(v, f);
        break;
    case IDM_MERGE:
        lstrcpy(f, "*.reg");
        if (!file_dialog(g_frame, FALSE, IDS_MERGETITLE, IDS_REGFILTER, "reg", f, sizeof f))
            return;
        merge_view = v;
        merged[0] = 0;
        /* the list shows the merge only after the message: the path and
           the value show the last key merged at once; a key the merge
           made is selected (RE6, RE19) */
        SendMessage(tree(v), WM_SETREDRAW, FALSE, 0);
        switch (merge_file(f, adv_merge_line)) {
        case MERGE_OK:
            if (merged[0]) {
                static char p[PATHMAX + 1], val[PATHMAX];
                int i = find_path(v, merged);
                p[0] = '\\';
                lstrcpy(p + 1, merged);
                SetDlgItemText(v, IDC_PATH, p);
                get_str(values(v), i, val);
                loading = TRUE;
                SetDlgItemText(v, IDC_VALUE, val);
                loading = FALSE;
                lstrcpy(g_sel, merged);
            }
            msgbox(g_frame, IDS_MERGED, f, MB_OK | MB_ICONEXCLAMATION);
            break;
        case MERGE_NOTREG:
            msgbox(g_frame, IDS_NOTREG, NULL, MB_OK | MB_ICONEXCLAMATION);
            break;
        default:
            msgbox(g_frame, IDS_CANTOPEN, f, MB_OK | MB_ICONEXCLAMATION);
        }
        SendMessage(tree(v), WM_SETREDRAW, TRUE, 0);
        InvalidateRect(tree(v), NULL, TRUE);
        break;
    }
    SetFocus(tree(v));
}

/* Exit (or a switch of view) with changes answered Yes */
void adv_save(void)
{
    if (g_view && g_mode == MODE_ADVANCED)
        save_changes(g_view);
    g_dirty = FALSE;
}

BOOL FAR PASCAL _export AdvDlgProc(HWND v, UINT msg, WPARAM wp, LPARAM lp)
{
    switch (msg) {
    case WM_INITDIALOG: {
        RECT r;
        int s;
        GetWindowRect(tree(v), &r);
        ScreenToClient(v, (POINT FAR *)&r);
        GetClientRect(v, &r);
        tmpl_cx = r.right;
        tmpl_cy = r.bottom;
        GetWindowRect(GetDlgItem(v, IDC_VALUE), &r);
        tmpl_w = r.right - r.left;
        list_top = 41;      /* measured (RE2): 1 below the Value edit */
        /* the hidden lists, off the window (RE2) */
        CreateWindow("ListBox", "", WS_CHILD | WS_VISIBLE | WS_BORDER | LBS_NOTIFY,
                     -175, -163, 20, 15, v, (HMENU)IDC_HIDDEN1, g_inst, NULL);
        CreateWindow("ListBox", "", WS_CHILD | WS_VISIBLE | WS_BORDER | LBS_NOTIFY,
                     -175, -163, 20, 15, v, (HMENU)IDC_HIDDEN2, g_inst, NULL);
        CreateWindow("ListBox", "", WS_CHILD, 0, 0, 0, 0, v, (HMENU)IDC_DELETED, g_inst, NULL);
        load(v);
        s = find_path(v, g_sel);
        select_line(v, s < 0 ? 0 : s);
        return TRUE;
    }
    case WM_SIZE: {
        int cx = LOWORD(lp), cy = HIWORD(lp);
        RECT r;
        HWND c = GetDlgItem(v, IDC_VALUE);
        MoveWindow(tree(v), -1, list_top, cx + 2, cy + 1 - list_top, TRUE);
        /* resized, the path and the value reach the right edge (RE10:
           at 492 pixels the edit ran from 95 to 492); at the template's
           size they keep the template's width (RE2: to 270) */
        if (!tmpl_cx)
            return TRUE;                /* (before WM_INITDIALOG) */
        GetWindowRect(c, &r);
        ScreenToClient(v, (POINT FAR *)&r);
        ScreenToClient(v, (POINT FAR *)&r.right);
        MoveWindow(c, r.left, r.top, (cx == tmpl_cx && cy == tmpl_cy) ? tmpl_w : cx + 1 - r.left,
                   r.bottom - r.top, TRUE);
        c = GetDlgItem(v, IDC_PATH);
        GetWindowRect(c, &r);
        ScreenToClient(v, (POINT FAR *)&r);
        ScreenToClient(v, (POINT FAR *)&r.right);
        MoveWindow(c, r.left, r.top, (cx == tmpl_cx && cy == tmpl_cy) ? tmpl_w : cx + 1 - r.left,
                   r.bottom - r.top, TRUE);
        return TRUE;
    }
    case WM_MEASUREITEM:
        ((MEASUREITEMSTRUCT FAR *)lp)->itemHeight = 16;
        return TRUE;
    case WM_DRAWITEM:
        draw_item(v, (DRAWITEMSTRUCT FAR *)lp);
        return TRUE;
    case WM_COMMAND:
        if (wp == IDC_TREE && HIWORD(lp) == LBN_SELCHANGE) {
            show_sel(v);
        } else if (wp == IDC_VALUE && HIWORD(lp) == EN_CHANGE && !loading) {
            static char val[PATHMAX];
            int s = (int)SendMessage(tree(v), LB_GETCURSEL, 0, 0);
            DWORD data;
            if (s < 0)
                return TRUE;
            GetDlgItemText(v, IDC_VALUE, val, sizeof val);
            data = SendMessage(tree(v), LB_GETITEMDATA, s, 0);
            SendMessage(tree(v), LB_SETITEMDATA, s, MAKELONG(LOWORD(data), HIWORD(data) | MARK_WRITE));
            SendMessage(values(v), LB_DELETESTRING, s, 0);
            SendMessage(values(v), LB_INSERTSTRING, s, (LPARAM)(LPSTR)val);
            /* the line is not drawn again now: it shows the value when the
               list gets the focus back (RE7: C1, C2) */
            g_dirty = TRUE;
        } else if (wp == IDC_VALUE && HIWORD(lp) == EN_KILLFOCUS) {
            RECT r;
            int s = (int)SendMessage(tree(v), LB_GETCURSEL, 0, 0);
            if (s >= 0 && SendMessage(tree(v), LB_GETITEMRECT, s, (LPARAM)(RECT FAR *)&r) != LB_ERR)
                InvalidateRect(tree(v), &r, TRUE);
        }
        return TRUE;
    }
    return FALSE;
}
