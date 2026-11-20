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

HGLOBAL selected_text(LPDIRWIN d, BOOL tree)
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
                    /* a directory by its full path (obs WF10: "C:\WFTEST\BETA ") */
                    if (e->attr & ATTR_DIR) {
                        char full[PATHLEN];
                        s_cpy(full, d->path, sizeof full);
                        path_add(full, e->name, sizeof full);
                        lstrcat(p, full);
                    } else {
                        lstrcat(p, e->name);
                    }
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

/* ---- the operation's box shows what it works on: "Deleting..." (obs
   WF10; "Copying...", "Moving..." UNVERIFIED: too quick to catch in
   DOSBox) and the name, in its lines 104 and 201 ---- */
static HWND op_box;

static void progress(LPCSTR path)
{
    if (!op_box)
        return;
    SetDlgItemText(op_box, 104, load_str(op_kind == IDM_DELETE ? IDS_DELETING :
                                         op_kind == IDM_MOVE ? IDS_MOVING : IDS_COPYING));
    SetDlgItemText(op_box, 201, path);
    UpdateWindow(op_box);
}

/* ---- Confirm File Replace (obs WF7): the file there and the one coming,
   each with "n bytes date time"; Yes, Yes to All (enabled when more
   than one file goes), No, Cancel ---- */

static LPCSTR rep_to, rep_from;
static BOOL rep_many, rep_all;

static void file_line(HWND h, int id, LPCSTR path)
{
    FINDBUF f;
    char t[64], dt[16], tm[16], num[12];
    if (dos_findfirst(path, ATTR_HS | ATTR_READONLY | ATTR_ARCHIVE, &f) != 0)
        return;
    fmt_date(dt, f.date);
    fmt_time(tm, f.time);
    wsprintf(t, "%s bytes %s %s", (LPSTR)s_utoa(num, f.size), (LPSTR)dt, (LPSTR)tm);
    SetDlgItemText(h, id, t);
}

BOOL CALLBACK _export ReplaceDlgProc(HWND h, UINT msg, WPARAM wp, LPARAM lp)
{
    (void)lp;
    switch (msg) {
    case WM_INITDIALOG: {
        char t[PATHLEN + 2];
        SendDlgItemMessage(h, 100, STM_SETICON, (WPARAM)LoadIcon(NULL, IDI_EXCLAMATION), 0);
        SetDlgItemText(h, 103, rep_to);
        file_line(h, 105, rep_to);
        s_cpy(t, rep_from, sizeof t);
        s_cat(t, "?", sizeof t);
        SetDlgItemText(h, 102, t);
        file_line(h, 106, rep_from);
        EnableWindow(GetDlgItem(h, 107), rep_many);
        return TRUE;
    }
    case WM_COMMAND:
        if (wp == IDYES || wp == IDNO || wp == IDCANCEL || wp == 107) {
            EndDialog(h, wp);
            return TRUE;
        }
        break;
    }
    return FALSE;
}

/* IDYES, IDNO or IDCANCEL */
static int confirm_replace(LPCSTR to, LPCSTR from)
{
    FARPROC p;
    int r;
    if (rep_all || !g_confreplace)
        return IDYES;
    rep_to = to;
    rep_from = from;
    p = MakeProcInstance((FARPROC)ReplaceDlgProc, g_hinst);
    r = DialogBox(g_hinst, "DLG_REPLACE", GetActiveWindow(), (DLGPROC)p);
    FreeProcInstance(p);
    if (r == 107) {
        rep_all = TRUE;
        r = IDYES;
    }
    return r;
}

/* Confirm File Delete (WF08) and Confirm Directory Delete (the same box,
   its texts UNVERIFIED): "Delete File:", the path with "?" */
static LPCSTR del_path;
static BOOL del_dir;

BOOL CALLBACK _export DelConfDlgProc(HWND h, UINT msg, WPARAM wp, LPARAM lp)
{
    (void)lp;
    switch (msg) {
    case WM_INITDIALOG: {
        char t[PATHLEN + 2];
        SendDlgItemMessage(h, 100, STM_SETICON, (WPARAM)LoadIcon(NULL, IDI_EXCLAMATION), 0);
        SetWindowText(h, load_str(del_dir ? IDS_CONFDELDIR : IDS_CONFDEL));
        SetDlgItemText(h, 103, load_str(del_dir ? IDS_DELDIRLABEL : IDS_DELFILELABEL));
        if (del_dir) {
            /* the longer label pushes the path right (obs WF10) */
            MoveWindow(GetDlgItem(h, 103), 44, 29, 96, 16, FALSE);
            MoveWindow(GetDlgItem(h, 102), 140, 29, 280, 16, FALSE);
        }
        s_cpy(t, del_path, sizeof t);
        s_cat(t, "?", sizeof t);
        SetDlgItemText(h, 102, t);
        EnableWindow(GetDlgItem(h, 107), rep_many);
        return TRUE;
    }
    case WM_COMMAND:
        if (wp == IDYES || wp == IDNO || wp == IDCANCEL || wp == 107) {
            EndDialog(h, wp);
            return TRUE;
        }
        break;
    }
    return FALSE;
}

static int confirm_delete(LPCSTR path, BOOL dir)
{
    FARPROC p;
    int r;
    if (rep_all || !(dir ? g_confsubdel : g_confdel))
        return IDYES;
    del_path = path;
    del_dir = dir;
    p = MakeProcInstance((FARPROC)DelConfDlgProc, g_hinst);
    r = DialogBox(g_hinst, "DLG_DELCONF", GetActiveWindow(), (DLGPROC)p);
    FreeProcInstance(p);
    if (r == 107) {
        rep_all = TRUE;
        r = IDYES;
    }
    return r;
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
    progress(from);
    if (is_dir(from))
        return copy_tree(from, to, move);
    if (!lstrcmpi(from, to))
        return FALSE;
    if (dos_getattr(to) >= 0) {
        int r = confirm_replace(to, from);
        if (r == IDCANCEL)
            return FALSE;
        if (r != IDYES)
            return TRUE;
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

/* a directory and what is in it, each file and directory confirmed as
   it comes (obs WF10: BETA, then GAMMA.DOC in it); FALSE on Cancel */
static BOOL delete_tree(LPCSTR path)
{
    char spec[PATHLEN], a[PATHLEN];
    FINDBUF f;
    int r;
    s_cpy(spec, path, sizeof spec);
    path_add(spec, "*.*", sizeof spec);
    if (dos_findfirst(spec, ATTR_DIR | ATTR_HS | ATTR_READONLY | ATTR_ARCHIVE, &f) == 0) {
        do {
            if (f.name[0] == '.')
                continue;
            s_cpy(a, path, sizeof a);
            path_add(a, f.name, sizeof a);
            progress(a);
            r = confirm_delete(a, (f.attr & ATTR_DIR) != 0);
            if (r == IDCANCEL)
                return FALSE;
            if (r != IDYES)
                continue;
            if (f.attr & ATTR_DIR) {
                if (!delete_tree(a))
                    return FALSE;
            } else {
                dos_setattr(a, 0);
                dos_unlink(a);
            }
        } while (dos_findnext(&f) == 0);
    }
    dos_rmdir(path);
    return TRUE;
}

/* the next name of a "A B C" list into buf; FALSE at the end */
BOOL next_name(LPCSTR FAR *p, LPSTR buf, int n)
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
void full_path(LPDIRWIN d, LPCSTR name, LPSTR out)
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
    rep_all = FALSE;
    rep_many = s_len(fromlist) > 0 && fromlist[0] && (next_name(&p, name, sizeof name), next_name(&p, name, sizeof name));
    p = fromlist;
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
        if (kind == IDM_RENAME) {
            if (dos_rename(src, dst) == 0) {
                /* the new name takes the old one's place in the selection */
                int n = (int)SendMessage(d->dirlb, LB_GETCOUNT, 0, 0), i;
                for (i = 0; i < n; i++) {
                    LPDENTRY e = dir_item(d, i);
                    int k = s_len(dst);
                    while (k > 0 && dst[k - 1] != '\\')
                        k--;
                    if (e && !lstrcmpi(e->name, src + s_len(d->path) + (s_len(d->path) > 3)))
                        s_cpy(e->name, dst + k, sizeof e->name);
                }
            }
        } else
            copy_one(src, dst, kind == IDM_MOVE);
    }
    SetCursor(oc);
    refresh_all();
    if (kind != IDM_COPY)
        tree_refresh(d);
}

static void do_delete(LPDIRWIN d, LPCSTR list)
{
    char name[PATHLEN], path[PATHLEN];
    LPCSTR p = list;
    int count = 0;
    while (next_name(&p, name, sizeof name))
        count++;
    rep_all = FALSE;
    rep_many = count > 1;
    p = list;
    while (next_name(&p, name, sizeof name)) {
        /* a directory has more inside: Yes to All (obs WF10) */
        full_path(d, name, path);
        if (is_dir(path))
            rep_many = TRUE;
    }
    p = list;
    while (next_name(&p, name, sizeof name)) {
        BOOL dir;
        int r;
        full_path(d, name, path);
        dir = is_dir(path);
        progress(path);
        r = confirm_delete(path, dir);
        if (r == IDCANCEL)
            break;
        if (r != IDYES)
            continue;
        if (dir) {
            if (!delete_tree(path))
                break;
        } else {
            dos_setattr(path, 0);
            dos_unlink(path);
        }
    }
    refresh_all();
    tree_refresh(d);
}

void file_transfer(LPDIRWIN d, LPCSTR list, LPCSTR dest, int kind)
{
    do_copy(d, list, dest, kind);
    tree_refresh(d);
}

/* Copy to Clipboard: the first file's full path in the registered
   "FileName" format (what Packager and other programs read; UNVERIFIED
   which other formats 3.1 adds) */
static void copy_to_clipboard(LPDIRWIN d, LPCSTR list)
{
    char name[PATHLEN], path[PATHLEN];
    LPCSTR p = list;
    HGLOBAL h;
    UINT fmt = RegisterClipboardFormat("FileName");
    if (!next_name(&p, name, sizeof name))
        return;
    full_path(d, name, path);
    h = GlobalAlloc(GMEM_MOVEABLE | GMEM_DDESHARE, s_len(path) + 1);
    if (!h)
        return;
    lstrcpy((LPSTR)GlobalLock(h), path);
    GlobalUnlock(h);
    if (OpenClipboard(g_frame)) {
        EmptyClipboard();
        SetClipboardData(fmt, h);
        CloseClipboard();
    } else {
        GlobalFree(h);
    }
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
            /* the box stays up, disabled, while the work is done (WF08:
               the confirmations come over it); the radio buttons give
               way to the progress lines */
            EnableWindow(GetDlgItem(h, IDD_FROM), FALSE);
            if (GetDlgItem(h, IDD_TO))
                EnableWindow(GetDlgItem(h, IDD_TO), FALSE);
            EnableWindow(GetDlgItem(h, IDOK), FALSE);
            if (op_kind == IDM_COPY) {
                ShowWindow(GetDlgItem(h, IDD_TOCHECK), SW_HIDE);
                ShowWindow(GetDlgItem(h, IDD_CLIPBOARD), SW_HIDE);
                ShowWindow(GetDlgItem(h, IDD_STATUS1), SW_SHOWNA);
                ShowWindow(GetDlgItem(h, 201), SW_SHOWNA);
            }
            EnableWindow(h, FALSE);
            op_box = h;
            switch (op_kind) {
            case IDM_COPY:
                if (IsDlgButtonChecked(h, IDD_CLIPBOARD)) {
                    copy_to_clipboard(d, from);
                    break;
                }
                /* fall through */
            case IDM_MOVE:
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
            op_box = NULL;
            EnableWindow(h, TRUE);
            EndDialog(h, 1);
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
                tree_refresh(d);
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

int run_dialog(LPCSTR name, FARPROC proc)
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
    if (d->lastpane == ID_TREELIST) {
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
    /* the pane used last, which keeps its meaning while a dialog has
       had the focus (WF08) */
    BOOL tree = d->lastpane == ID_TREELIST;
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
