/*
 * fileops.c - w16 File Manager: the file commands: Open, Move, Copy,
 * Delete, Rename, Properties, Run, Print, Create Directory, Select Files.
 *
 * Dialogs: the original's layouts as observed (obs WF3, tools/snapdlg.py);
 * the "From:" box holds the selected names in capitals, each followed by
 * a blank ("ARIALI.TTF "); "Current Directory: " and the directory above.
 * The work is done with DOS calls (dosfn.asm); files keep their date,
 * time and attributes when copied.
 */
#include "winfile.h"

#define IDD_CURDIR      101
#define IDD_FROM        102
#define IDD_TO          103
#define IDD_STATUS1     104
#define IDD_NAME        201
#define IDD_STATUS2     201
#define IDD_TOCHECK     257
#define IDD_CLIPBOARD   256
#define IDD_MINIMIZED   214
#define IDD_SIZE        203
#define IDD_DATE        204
#define IDD_PATH        101
#define IDD_READONLY    215
#define IDD_HIDDEN      216
#define IDD_ARCHIVE     217
#define IDD_SYSTEM      218
#define IDD_DESELECT    6
#define IDD_HELP        254

static LPDIRWIN op_dir;
static int op_kind;                     /* the command */
static HGLOBAL op_from;                 /* the names, a text */

/* ---- the selection as "NAME NAME " ---- */

static HGLOBAL selected_text(LPDIRWIN d, BOOL tree)
{
    int n = (int)SendMessage(d->dirlb, LB_GETCOUNT, 0, 0), i;
    HGLOBAL h = GlobalAlloc(GMEM_MOVEABLE | GMEM_ZEROINIT, (DWORD)(n + 1) * 14 + PATHLEN);
    LPSTR p;
    if (!h)
        return NULL;
    p = (LPSTR)GlobalLock(h);
    if (tree) {
        /* the tree has the focus: its directory */
        s_cpy(p, d->path, PATHLEN);
        s_cat(p, " ", PATHLEN);
    } else {
        for (i = 0; i < n; i++)
            if (SendMessage(d->dirlb, LB_GETSEL, i, 0) > 0) {
                LPDENTRY e = dir_item(d, i);
                if (e && e->pic != BM_PARENT) {
                    lstrcat(p, e->name);
                    lstrcat(p, " ");
                }
            }
    }
    GlobalUnlock(h);
    return h;
}

static void cur_dir_text(HWND h, LPDIRWIN d)
{
    char t[PATHLEN + 24];
    wsprintf(t, load_str(IDS_CURDIR), (LPSTR)d->path);
    SetDlgItemText(h, IDD_CURDIR, t);
}

/* ---- copying and moving ---- */

static BOOL copy_file(LPCSTR from, LPCSTR to)
{
    int a, b, n, attr;
    HGLOBAL hb = GlobalAlloc(GMEM_MOVEABLE, 32768L);
    LPBYTE buf;
    DWORD stamp;
    BOOL ok = TRUE;
    if (!hb)
        return FALSE;
    buf = (LPBYTE)GlobalLock(hb);
    attr = dos_getattr(from);
    a = dos_open(from, 0x20);           /* read, deny write */
    if (a < 0) {
        GlobalUnlock(hb);
        GlobalFree(hb);
        return FALSE;
    }
    b = dos_create(to, 0);
    if (b < 0) {
        dos_close(a);
        GlobalUnlock(hb);
        GlobalFree(hb);
        return FALSE;
    }
    for (;;) {
        n = dos_read(a, buf, 32768U);
        if (n <= 0)
            break;
        if (dos_write(b, buf, n) != n) {
            ok = FALSE;
            break;
        }
    }
    stamp = dos_getftime(a);
    dos_setftime(b, stamp);
    dos_close(a);
    dos_close(b);
    GlobalUnlock(hb);
    GlobalFree(hb);
    if (!ok)
        dos_unlink(to);
    else if (attr >= 0)
        dos_setattr(to, (attr & ~ATTR_DIR) | ATTR_ARCHIVE);
    return ok;
}

static BOOL is_dir(LPCSTR path)
{
    int a = dos_getattr(path);
    return a >= 0 && (a & ATTR_DIR);
}

static BOOL copy_tree(LPCSTR from, LPCSTR to, BOOL move);

/* one file or directory; to is the full destination name */
static BOOL copy_one(LPCSTR from, LPCSTR to, BOOL move)
{
    if (is_dir(from))
        return copy_tree(from, to, move);
    if (!lstrcmpi(from, to))
        return FALSE;
    if (dos_getattr(to) >= 0) {
        if (g_confreplace) {
            char t[PATHLEN + 80];
            wsprintf(t, load_str(IDS_REPLACE), (LPSTR)to);
            if (MessageBox(g_frame, t, load_str(IDS_CONFREPLACE),
                           MB_YESNO | MB_ICONQUESTION) != IDYES)
                return TRUE;
        }
        dos_setattr(to, 0);
        dos_unlink(to);
    }
    if (move && (from[0] | 0x20) == (to[0] | 0x20))
        return dos_rename(from, to) == 0;
    if (!copy_file(from, to))
        return FALSE;
    if (move) {
        dos_setattr(from, 0);
        dos_unlink(from);
    }
    return TRUE;
}

static BOOL copy_tree(LPCSTR from, LPCSTR to, BOOL move)
{
    char spec[PATHLEN], a[PATHLEN], b[PATHLEN];
    FINDBUF f;
    BOOL ok = TRUE;
    if (move && (from[0] | 0x20) == (to[0] | 0x20) && dos_getattr(to) < 0)
        return dos_rename(from, to) == 0;
    if (!is_dir(to) && dos_mkdir(to) != 0)
        return FALSE;
    s_cpy(spec, from, sizeof spec);
    path_add(spec, "*.*", sizeof spec);
    if (dos_findfirst(spec, ATTR_DIR | ATTR_HS | ATTR_READONLY | ATTR_ARCHIVE, &f) == 0) {
        do {
            if (f.name[0] == '.')
                continue;
            s_cpy(a, from, sizeof a);
            path_add(a, f.name, sizeof a);
            s_cpy(b, to, sizeof b);
            path_add(b, f.name, sizeof b);
            if (!copy_one(a, b, move))
                ok = FALSE;
            /* the search continues in our buffer f: DOS keeps its state
               there, so the copies in between do not disturb it */
        } while (dos_findnext(&f) == 0);
    }
    if (move && ok)
        dos_rmdir(from);
    return ok;
}

static BOOL delete_tree(LPCSTR path)
{
    char spec[PATHLEN], a[PATHLEN];
    FINDBUF f;
    s_cpy(spec, path, sizeof spec);
    path_add(spec, "*.*", sizeof spec);
    if (dos_findfirst(spec, ATTR_DIR | ATTR_HS | ATTR_READONLY | ATTR_ARCHIVE, &f) == 0) {
        do {
            if (f.name[0] == '.')
                continue;
            s_cpy(a, path, sizeof a);
            path_add(a, f.name, sizeof a);
            if (f.attr & ATTR_DIR) {
                delete_tree(a);
            } else {
                dos_setattr(a, 0);
                dos_unlink(a);
            }
        } while (dos_findnext(&f) == 0);
    }
    return dos_rmdir(path) == 0;
}

/* the next name of a "A B C" list into buf; FALSE at the end */
static BOOL next_name(LPCSTR FAR *p, LPSTR buf, int n)
{
    int k = 0;
    while (**p == ' ')
        (*p)++;
    if (!**p)
        return FALSE;
    while (**p && **p != ' ') {
        if (k < n - 1)
            buf[k++] = **p;
        (*p)++;
    }
    buf[k] = 0;
    return TRUE;
}

/* a name relative to the window's directory, made full */
static void full_path(LPDIRWIN d, LPCSTR name, LPSTR out)
{
    if (name[0] && name[1] == ':') {
        s_cpy(out, name, PATHLEN);
    } else if (name[0] == '\\') {
        out[0] = d->path[0];
        out[1] = ':';
        s_cpy(out + 2, name, PATHLEN - 2);
    } else {
        s_cpy(out, d->path, PATHLEN);
        path_add(out, name, PATHLEN);
    }
    s_upper(out);
}

/* every window's listing again (the files changed) */
void refresh_all(void)
{
    HWND w;
    for (w = GetWindow(g_mdi, GW_CHILD); w; w = GetWindow(w, GW_HWNDNEXT)) {
        LPDIRWIN e = dirwin_of(w);
        if (!e)
            continue;
        {
            HGLOBAL sel = dir_sel_save(e);
            dir_read(e);
            dir_sel_restore(e, sel);
        }
    }
    status_update();
}

/* Move, Copy, Rename: from names to a destination */
static void do_copy(LPDIRWIN d, LPCSTR fromlist, LPCSTR dest, int kind)
{
    char name[PATHLEN], src[PATHLEN], dst[PATHLEN], target[PATHLEN];
    LPCSTR p = fromlist;
    BOOL todir;
    HCURSOR oc = SetCursor(LoadCursor(NULL, IDC_WAIT));
    full_path(d, dest, target);
    todir = is_dir(target);
    while (next_name(&p, name, sizeof name)) {
        full_path(d, name, src);
        if (todir && kind != IDM_RENAME) {
            int k = s_len(src);
            while (k > 0 && src[k - 1] != '\\')
                k--;
            s_cpy(dst, target, sizeof dst);
            path_add(dst, src + k, sizeof dst);
        } else {
            s_cpy(dst, target, sizeof dst);
        }
        if (kind == IDM_RENAME)
            dos_rename(src, dst);
        else
            copy_one(src, dst, kind == IDM_MOVE);
    }
    SetCursor(oc);
    refresh_all();
    if (kind != IDM_COPY)
        tree_read(d);
}

static void do_delete(LPDIRWIN d, LPCSTR list)
{
    char name[PATHLEN], path[PATHLEN], t[PATHLEN + 80];
    LPCSTR p = list;
    BOOL all = FALSE;
    while (next_name(&p, name, sizeof name)) {
        full_path(d, name, path);
        if (is_dir(path)) {
            if (g_confsubdel && !all) {
                int r;
                wsprintf(t, load_str(IDS_DELDIR), (LPSTR)path);
                r = MessageBox(g_frame, t, load_str(IDS_CONFDELDIR), MB_YESNOCANCEL | MB_ICONQUESTION);
                if (r == IDCANCEL)
                    break;
                if (r != IDYES)
                    continue;
            }
            delete_tree(path);
        } else {
            if (g_confdel && !all) {
                int r;
                wsprintf(t, load_str(IDS_DELFILE), (LPSTR)path);
                r = MessageBox(g_frame, t, load_str(IDS_CONFDEL), MB_YESNOCANCEL | MB_ICONQUESTION);
                if (r == IDCANCEL)
                    break;
                if (r != IDYES)
                    continue;
            }
            dos_setattr(path, 0);
            dos_unlink(path);
        }
    }
    refresh_all();
    tree_read(d);
}

/* ---- the dialogs ---- */

BOOL CALLBACK _export SuperDlgProc(HWND h, UINT msg, WPARAM wp, LPARAM lp)
{
    LPDIRWIN d = op_dir;
    (void)lp;
    switch (msg) {
    case WM_INITDIALOG: {
        LPSTR from = (LPSTR)GlobalLock(op_from);
        cur_dir_text(h, d);
        SetDlgItemText(h, IDD_FROM, from);
        GlobalUnlock(op_from);
        if (op_kind == IDM_MOVE || op_kind == IDM_COPY || op_kind == IDM_RENAME) {
            SetWindowText(h, load_str(op_kind == IDM_MOVE ? IDS_MOVE :
                                      op_kind == IDM_COPY ? IDS_COPY : IDS_RENAME));
            if (op_kind != IDM_COPY) {
                ShowWindow(GetDlgItem(h, IDD_TOCHECK), SW_HIDE);
                ShowWindow(GetDlgItem(h, IDD_CLIPBOARD), SW_HIDE);
            } else {
                ShowWindow(GetDlgItem(h, IDD_STATUS1), SW_HIDE);
                ShowWindow(GetDlgItem(h, 201), SW_HIDE);
                CheckRadioButton(h, IDD_CLIPBOARD, IDD_TOCHECK, IDD_TOCHECK);
            }
        }
        return TRUE;
    }
    case WM_COMMAND:
        switch (wp) {
        case IDOK: {
            char to[PATHLEN];
            int len = GetWindowTextLength(GetDlgItem(h, IDD_FROM)) + 1;
            HGLOBAL hf = GlobalAlloc(GMEM_MOVEABLE, len);
            LPSTR from = (LPSTR)GlobalLock(hf);
            GetDlgItemText(h, IDD_FROM, from, len);
            to[0] = 0;
            if (op_kind != IDM_DELETE && op_kind != IDM_PRINT)
                GetDlgItemText(h, IDD_TO, to, sizeof to);
            EndDialog(h, 1);
            switch (op_kind) {
            case IDM_MOVE:
            case IDM_COPY:
            case IDM_RENAME:
                if (to[0])
                    do_copy(d, from, to, op_kind);
                break;
            case IDM_DELETE:
                do_delete(d, from);
                break;
            case IDM_PRINT: {
                char name[PATHLEN], path[PATHLEN];
                LPCSTR p = from;
                while (next_name(&p, name, sizeof name)) {
                    full_path(d, name, path);
                    ShellExecute(g_frame, "print", path, NULL, d->path, SW_SHOWMINNOACTIVE);
                }
                break;
            }
            }
            GlobalUnlock(hf);
            GlobalFree(hf);
            return TRUE;
        }
        case IDCANCEL:
            EndDialog(h, 0);
            return TRUE;
        case IDD_HELP:
            WinHelp(g_frame, load_str(IDS_HELPFILE), HELP_CONTEXT, 5000L + op_kind);  /* UNVERIFIED */
            return TRUE;
        }
        break;
    }
    return FALSE;
}

BOOL CALLBACK _export MakeDirDlgProc(HWND h, UINT msg, WPARAM wp, LPARAM lp)
{
    LPDIRWIN d = op_dir;
    (void)lp;
    switch (msg) {
    case WM_INITDIALOG:
        cur_dir_text(h, d);
        return TRUE;
    case WM_COMMAND:
        if (wp == IDOK) {
            char name[PATHLEN], path[PATHLEN];
            GetDlgItemText(h, IDD_NAME, name, sizeof name);
            EndDialog(h, 1);
            if (name[0]) {
                full_path(d, name, path);
                dos_mkdir(path);
                refresh_all();
                tree_read(d);
            }
            return TRUE;
        }
        if (wp == IDCANCEL) {
            EndDialog(h, 0);
            return TRUE;
        }
        break;
    }
    return FALSE;
}

BOOL CALLBACK _export RunDlgProc(HWND h, UINT msg, WPARAM wp, LPARAM lp)
{
    LPDIRWIN d = op_dir;
    (void)lp;
    switch (msg) {
    case WM_INITDIALOG: {
        LPSTR from = (LPSTR)GlobalLock(op_from);
        char t[PATHLEN];
        cur_dir_text(h, d);
        s_cpy(t, from, sizeof t);
        if (t[0] && t[s_len(t) - 1] == ' ')
            t[s_len(t) - 1] = 0;
        SetDlgItemText(h, IDD_NAME, t);
        GlobalUnlock(op_from);
        return TRUE;
    }
    case WM_COMMAND:
        if (wp == IDOK) {
            char cmd[PATHLEN * 2];
            BOOL min = IsDlgButtonChecked(h, IDD_MINIMIZED);
            GetDlgItemText(h, IDD_NAME, cmd, sizeof cmd);
            EndDialog(h, 1);
            if (cmd[0]) {
                if (WinExec(cmd, min ? SW_SHOWMINNOACTIVE : SW_SHOWNORMAL) >= 32 && g_minonrun)
                    ShowWindow(g_frame, SW_MINIMIZE);
            }
            return TRUE;
        }
        if (wp == IDCANCEL) {
            EndDialog(h, 0);
            return TRUE;
        }
        break;
    }
    return FALSE;
}

BOOL CALLBACK _export AttribsDlgProc(HWND h, UINT msg, WPARAM wp, LPARAM lp)
{
    LPDIRWIN d = op_dir;
    static char path[PATHLEN];
    (void)lp;
    switch (msg) {
    case WM_INITDIALOG: {
        LPSTR from = (LPSTR)GlobalLock(op_from);
        char name[PATHLEN], t[PATHLEN + 24], u[24], v[24];
        LPCSTR p = from;
        FINDBUF f;
        next_name(&p, name, sizeof name);
        GlobalUnlock(op_from);
        full_path(d, name, path);
        wsprintf(t, load_str(IDS_PROPERTIES), (LPSTR)name);
        SetWindowText(h, t);
        SetDlgItemText(h, IDD_NAME, name);
        SetDlgItemText(h, IDD_PATH, d->path);
        if (dos_findfirst(path, ATTR_HS | ATTR_READONLY | ATTR_ARCHIVE | ATTR_DIR, &f) == 0) {
            char num[16];
            wsprintf(t, load_str(IDS_BYTES), (LPSTR)s_commas(num, f.size));
            SetDlgItemText(h, IDD_SIZE, t);
            fmt_date(u, f.date);
            fmt_time(v, f.time);
            wsprintf(t, "%s  %s", (LPSTR)u, (LPSTR)v);
            SetDlgItemText(h, IDD_DATE, t);
            CheckDlgButton(h, IDD_READONLY, (f.attr & ATTR_READONLY) != 0);
            CheckDlgButton(h, IDD_HIDDEN, (f.attr & ATTR_HIDDEN) != 0);
            CheckDlgButton(h, IDD_ARCHIVE, (f.attr & ATTR_ARCHIVE) != 0);
            CheckDlgButton(h, IDD_SYSTEM, (f.attr & ATTR_SYSTEM) != 0);
        }
        return TRUE;
    }
    case WM_COMMAND:
        if (wp == IDOK) {
            int a = dos_getattr(path);
            if (a >= 0) {
                a &= ~(ATTR_READONLY | ATTR_HIDDEN | ATTR_ARCHIVE | ATTR_SYSTEM);
                if (IsDlgButtonChecked(h, IDD_READONLY))
                    a |= ATTR_READONLY;
                if (IsDlgButtonChecked(h, IDD_HIDDEN))
                    a |= ATTR_HIDDEN;
                if (IsDlgButtonChecked(h, IDD_ARCHIVE))
                    a |= ATTR_ARCHIVE;
                if (IsDlgButtonChecked(h, IDD_SYSTEM))
                    a |= ATTR_SYSTEM;
                dos_setattr(path, a & ~ATTR_DIR);
            }
            EndDialog(h, 1);
            refresh_all();
            return TRUE;
        }
        if (wp == IDCANCEL) {
            EndDialog(h, 0);
            return TRUE;
        }
        break;
    }
    return FALSE;
}

/* does a name match a DOS pattern (* and ?, extension apart)? */
static BOOL match_part(LPCSTR n, int nl, LPCSTR p, int pl)
{
    int i = 0, j = 0;
    while (j < pl) {
        if (p[j] == '*')
            return TRUE;
        if (i >= nl) {
            if (p[j] != '?')
                return FALSE;
        } else if (p[j] != '?' && (p[j] | 0x20) != (n[i] | 0x20)) {
            return FALSE;
        }
        i++;
        j++;
    }
    return i >= nl;
}

BOOL dos_match(LPCSTR name, LPCSTR pat)
{
    LPCSTR nd = name, pd = pat;
    int nb, pb;
    while (*nd && *nd != '.')
        nd++;
    while (*pd && *pd != '.')
        pd++;
    nb = (int)(nd - name);
    pb = (int)(pd - pat);
    if (!match_part(name, nb, pat, pb))
        return FALSE;
    if (*nd)
        nd++;
    if (*pd)
        pd++;
    return match_part(nd, s_len(nd), pd, s_len(pd));
}

BOOL CALLBACK _export SelectDlgProc(HWND h, UINT msg, WPARAM wp, LPARAM lp)
{
    LPDIRWIN d = op_dir;
    (void)lp;
    switch (msg) {
    case WM_INITDIALOG:
        SetDlgItemText(h, IDD_NAME, "*.*");
        return TRUE;
    case WM_COMMAND:
        if (wp == IDOK || wp == IDD_DESELECT) {
            char pat[PATHLEN];
            int n = (int)SendMessage(d->dirlb, LB_GETCOUNT, 0, 0), i, first = -1;
            GetDlgItemText(h, IDD_NAME, pat, sizeof pat);
            for (i = 0; i < n; i++) {
                LPDENTRY e = dir_item(d, i);
                if (!e || e->pic == BM_PARENT || !dos_match(e->name, pat))
                    continue;
                SendMessage(d->dirlb, LB_SETSEL, wp == IDOK, MAKELPARAM(i, 0));
                if (first < 0)
                    first = i;
            }
            if (first >= 0 && wp == IDOK)
                SendMessage(d->dirlb, LB_SETCARETINDEX, first, 0);
            status_set(d, ST_SELECTED);
            return TRUE;
        }
        if (wp == IDCANCEL) {
            EndDialog(h, 0);
            return TRUE;
        }
        break;
    }
    return FALSE;
}

static int run_dialog(LPCSTR name, FARPROC proc)
{
    FARPROC p = MakeProcInstance(proc, g_hinst);
    int r = DialogBox(g_hinst, name, g_frame, (DLGPROC)p);
    FreeProcInstance(p);
    return r;
}

/* File > Open: a directory is shown, a program run, a document opened
   with its program (ShellExecute) */
void file_open(LPDIRWIN d)
{
    int i = (int)SendMessage(d->dirlb, LB_GETCARETINDEX, 0, 0);
    LPDENTRY e = dir_item(d, i);
    char path[PATHLEN];
    if (GetFocus() == d->treelb) {
        int t = (int)SendMessage(d->treelb, LB_GETCURSEL, 0, 0);
        tree_expand(d, t, 1);
        return;
    }
    if (!e)
        return;
    if (e->pic == BM_PARENT) {
        s_cpy(path, d->path, sizeof path);
        path_strip(path);
        tree_select_path(d, path);
        treewin_change_dir(d, path);
        return;
    }
    full_path(d, e->name, path);
    if (e->attr & ATTR_DIR) {
        tree_select_path(d, path);
        treewin_change_dir(d, path);
        return;
    }
    if ((UINT)ShellExecute(g_frame, NULL, path, NULL, d->path, SW_SHOWNORMAL) > 32 && g_minonrun)
        ShowWindow(g_frame, SW_MINIMIZE);
}

void file_command(LPDIRWIN d, WPARAM id)
{
    BOOL tree = GetFocus() == d->treelb;
    op_dir = d;
    op_kind = (int)id;
    op_from = selected_text(d, tree);
    switch (id) {
    case IDM_MOVE:
    case IDM_COPY:
    case IDM_RENAME:
        run_dialog("DLG_MOVE", (FARPROC)SuperDlgProc);
        break;
    case IDM_DELETE:
        run_dialog("DLG_DELETE", (FARPROC)SuperDlgProc);
        break;
    case IDM_PRINT:
        run_dialog("DLG_PRINT", (FARPROC)SuperDlgProc);
        break;
    case IDM_MAKEDIR:
        run_dialog("DLG_MAKEDIR", (FARPROC)MakeDirDlgProc);
        break;
    case IDM_RUN:
        run_dialog("DLG_RUN", (FARPROC)RunDlgProc);
        break;
    case IDM_ATTRIBS:
        run_dialog("DLG_ATTRIBS", (FARPROC)AttribsDlgProc);
        break;
    case IDM_SELECT:
        run_dialog("DLG_SELECT", (FARPROC)SelectDlgProc);
        break;
    case IDM_OPEN:
        file_open(d);
        break;
    }
    if (op_from)
        GlobalFree(op_from);
    op_from = NULL;
}
