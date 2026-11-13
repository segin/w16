/*
 * file.c - w16 COMMDLG: GetOpenFileName, GetSaveFileName, GetFileTitle
 * and the Open / Save As dialog (templates 1536, 1537).
 *
 * Observed on Windows 3.1 (tests/scripts/commdlg/, ARCHITECTURE.md
 * 17): the lists hold the names as DOS gives them (capitals) and draw them
 * in lower case; the folder list shows the path from the root (open
 * folders, the current one marked) and the current folder's subfolders
 * (closed); the drive list "x: label" (fixed disks), "x: \\server\share"
 * (network) or "x:"; the folder line in lower case. The chosen name comes
 * back as a full path in capitals (a default extension is appended as
 * given, after that). FILEMUSTEXIST implies PATHMUSTEXIST, CREATEPROMPT
 * both, in the returned Flags too. Errors are message boxes titled with
 * the dialog's caption: "<name as typed>\nCannot find this file...",
 * "...Path does not exist...", "...This filename is not valid.", "...is a
 * reserved device name...", exclamation icons; "...does not exist.\n\n
 * Create the file?" a question (Yes/No); "<FULL PATH>\nThis file already
 * exists.\n\nReplace existing file?" Yes/No, exclamation. A buffer too
 * small for the result: FNERR_BUFFERTOOSMALL, its first two bytes the
 * length the path needs (observed: the length without the NUL).
 * While the dialog sets itself up, its hook already receives the
 * notifications that causes: WM_COMMAND (cmb1, 8000h) then the edit's
 * EN_UPDATE/EN_CHANGE, LBSELCHSTRING (cmb1, index), the same for cmb2;
 * WM_INITDIALOG comes last.
 */
#include "commdlg.h"


/* WM_COMMAND notification the dialog sends itself while it sets up a
   combo box (observed: 8000h) */
#define CBN_INIT        0x8000

typedef struct {
    LPOPENFILENAME ofn;
    BOOL save, multi;
    HWND dlg;
    cd_hook hook;
    LPSTR FAR *pats;                    /* the filters' patterns */
    int npats;
    BOOL has_custom;                    /* entry 0 is the custom filter */
    LPSTR startdir;                     /* for OFN_NOCHANGEDIR */
    char caption[80];                   /* the window's caption */
    BOOL ended;                         /* EndDialog called */
    BOOL pend_type, pend_drive;         /* selections made while dropped */
} fstate;

/* Flags bits the original sets in the caller's structure while a combo
   box's list is down (observed: still set when the dialog ends with the
   list down) */
#define OFN_TYPESDOWN   0x10000000L
#define OFN_DRIVESDOWN  0x20000000L

/* ---- names ---- */

static BOOL is_wild(LPCSTR s)
{
    for (; *s; s++)
        if (*s == '*' || *s == '?')
            return TRUE;
    return FALSE;
}

/* the name part of a path */
static LPSTR name_part(LPCSTR p)
{
    LPCSTR n = p;
    for (; *p; p = AnsiNext(p))
        if (*p == '\\' || *p == '/' || *p == ':')
            n = p + 1;
    return (LPSTR)n;
}

/* characters DOS does not take in a name */
static BOOL bad_char(char c)
{
    static const char bad[] = "\"*+,/:;<=>?[]|";
    int i;
    if ((BYTE)c < ' ')
        return TRUE;
    for (i = 0; bad[i]; i++)
        if (c == bad[i])
            return TRUE;
    return FALSE;
}

/* a DOS name: up to 8 + 3 good characters, at most one dot, not empty */
static BOOL valid_name(LPCSTR n)
{
    int base = 0, ext = -1;
    if (!*n)
        return FALSE;
    for (; *n; n++) {
        if (*n == '.') {
            if (ext >= 0)
                return FALSE;
            ext = 0;
            continue;
        }
        if (bad_char(*n) || *n == ' ')
            return FALSE;
        if (ext >= 0)
            ext++;
        else
            base++;
    }
    return base > 0;
}

static BOOL device_name(LPCSTR n)
{
    static const char *dev[] = { "CON", "PRN", "AUX", "NUL", "COM1", "COM2", "COM3",
        "COM4", "LPT1", "LPT2", "LPT3", "CLOCK$", 0 };
    char b[13];
    int i;
    for (i = 0; n[i] && n[i] != '.' && i < 12; i++)
        b[i] = n[i];
    b[i] = 0;
    for (i = 0; dev[i]; i++)
        if (!lstrcmpi(b, dev[i]))
            return TRUE;
    return FALSE;
}

/* ---- GetFileTitle ---- */

/* The name part of a path (as the Open dialog shows it); fails (-1) for a
   name that is not valid, returns the size needed when buf is too small.
   UNVERIFIED: the exact return values for invalid names. */
short CDAPI GetFileTitle(LPCSTR path, LPSTR buf, UINT size)
{
    LPCSTR n;
    int len;
    if (!path)
        return -1;
    n = name_part(path);
    if (!*n || is_wild(n))
        return -1;
    len = s_len(n);
    if (!buf || (UINT)len + 1 > size)
        return (short)(len + 1);
    s_cpy(buf, n, size);
    return 0;
}

/* ---- the lists ---- */

static LPCSTR cur_pattern(fstate FAR *st)
{
    int i = (int)SendDlgItemMessage(st->dlg, cmb1, CB_GETCURSEL, 0, 0);
    if (i < 0 || i >= st->npats)
        return "*.*";
    return st->pats[i];
}

/* the name box shows the current pattern in lower case (observed:
   Paintbrush's "*.BMP;*.DIB" appears as "*.bmp;*.dib") */
static void pattern_text(fstate FAR *st)
{
    LPSTR p = s_dup(cur_pattern(st));
    if (!p)
        return;
    s_lower(p);
    SetDlgItemText(st->dlg, edt1, p);
    mem_free(p);
}

/* the files of the current folder matching "a;b;c" */
static void fill_files(fstate FAR *st, LPCSTR patterns)
{
    HWND lb = GetDlgItem(st->dlg, lst1);
    LPSTR pats = s_dup(patterns), p, q;
    if (!lb || !pats)
        return;
    SendMessage(lb, WM_SETREDRAW, FALSE, 0);
    SendMessage(lb, LB_RESETCONTENT, 0, 0);
    for (p = pats; *p; p = q) {
        for (q = p; *q && *q != ';'; q++)
            ;
        if (*q)
            *q++ = 0;
        while (*p == ' ')
            p++;
        if (*p)
            SendMessage(lb, LB_DIR, DDL_READWRITE, (LPARAM)p);
    }
    mem_free(pats);
    SendMessage(lb, WM_SETREDRAW, TRUE, 0);
    InvalidateRect(lb, NULL, TRUE);
}

/* the folder list: the path from the root, then the subfolders */
static void fill_dirs(fstate FAR *st)
{
    HWND lb = GetDlgItem(st->dlg, lst2);
    LPSTR cwd = cur_dir(), p, start;
    char root[4], low[DOS_PATH];
    int level = 0, n = 0, first_sub;
    dos_dta d;
    if (!cwd)
        return;
    s_cpy(low, cwd, sizeof low);
    s_lower(low);
    SetDlgItemText(st->dlg, stc1, low);
    if (!lb) {
        mem_free(cwd);
        return;
    }
    SendMessage(lb, WM_SETREDRAW, FALSE, 0);
    SendMessage(lb, LB_RESETCONTENT, 0, 0);
    root[0] = cwd[0];
    root[1] = ':';
    root[2] = '\\';
    root[3] = 0;
    SendMessage(lb, LB_INSERTSTRING, n, (LPARAM)(LPSTR)root);
    SendMessage(lb, LB_SETITEMDATA, n++, MAKELONG(0, cwd[3] ? PIC_OPEN : PIC_OPENCUR));
    for (start = p = cwd + 3; *start; start = p) {
        char c;
        while (*p && *p != '\\')
            p = AnsiNext(p);
        c = *p;
        *p = 0;
        level++;
        SendMessage(lb, LB_INSERTSTRING, n, (LPARAM)start);
        SendMessage(lb, LB_SETITEMDATA, n++, MAKELONG(level, c ? PIC_OPEN : PIC_OPENCUR));
        *p = c;
        if (*p)
            p++;
    }
    first_sub = n;
    if (dos_findfirst("*.*", 0x10, &d) == 0) {
        do {
            int k;
            if (!(d.attr & 0x10) || d.name[0] == '.')
                continue;
            /* in order (lstrcmpi), as a sorted list box keeps it */
            for (k = first_sub; k < n; k++) {
                char t[13];
                SendMessage(lb, LB_GETTEXT, k, (LPARAM)(LPSTR)t);
                if (lstrcmpi(d.name, t) < 0)
                    break;
            }
            SendMessage(lb, LB_INSERTSTRING, k, (LPARAM)(LPSTR)d.name);
            SendMessage(lb, LB_SETITEMDATA, k, MAKELONG(level + 1, PIC_CLOSED));
            n++;
        } while (dos_findnext(&d) == 0);
    }
    SendMessage(lb, LB_SETCURSEL, first_sub - 1, 0);
    SendMessage(lb, WM_SETREDRAW, TRUE, 0);
    InvalidateRect(lb, NULL, TRUE);
    mem_free(cwd);
}

/* the volume label of drive d (0 = A:), lower case, or "" */
static void volume_label(int d, LPSTR out)
{
    char spec[7];
    dos_dta f;
    int i, j = 0;
    out[0] = 0;
    spec[0] = (char)('A' + d);
    s_cpy(spec + 1, ":\\*.*", 6);
    if (dos_findfirst(spec, 0x08, &f) == 0 && (f.attr & 0x08)) {
        for (i = 0; f.name[i]; i++)
            if (f.name[i] != '.')
                out[j++] = f.name[i];
        out[j] = 0;
        s_lower(out);
    }
}

/* the current drive chosen in the drive list */
static void select_drive(fstate FAR *st)
{
    HWND cb = GetDlgItem(st->dlg, cmb2);
    int d, cur = dos_getdrive(), sel = -1;
    for (d = (int)SendMessage(cb, CB_GETCOUNT, 0, 0) - 1; d >= 0; d--)
        if (LOWORD(SendMessage(cb, CB_GETITEMDATA, d, 0)) == (WORD)cur)
            sel = d;
    SendMessage(cb, CB_SETCURSEL, sel, 0);
}

static void fill_drives(fstate FAR *st)
{
    HWND cb = GetDlgItem(st->dlg, cmb2);
    int d, cur = dos_getdrive(), sel = -1;
    if (!cb)
        return;
    SendMessage(cb, CB_RESETCONTENT, 0, 0);
    for (d = 0; d < 26; d++) {
        int kind = drive_kind(d), i;
        char text[DOS_PATH + 4];
        if (kind < 0)
            continue;
        text[0] = (char)('a' + d);
        text[1] = ':';
        text[2] = 0;
        if (kind == PIC_NET) {
            char local[3], remote[DOS_PATH];
            UINT n = sizeof remote;
            local[0] = (char)('A' + d);
            local[1] = ':';
            local[2] = 0;
            if (WNetGetConnection(local, remote, &n) == WN_SUCCESS) {
                text[2] = ' ';
                s_cpy(text + 3, remote, sizeof text - 3);
                s_lower(text + 3);
            }
        } else
        if (kind != PIC_FLOPPY && kind != PIC_NET) {
            char label[13];
            volume_label(d, label);
            if (label[0]) {
                text[2] = ' ';
                s_cpy(text + 3, label, sizeof text - 3);
            }
        }
        i = (int)SendMessage(cb, CB_ADDSTRING, 0, (LPARAM)(LPSTR)text);
        if (i >= 0)
            SendMessage(cb, CB_SETITEMDATA, i, MAKELONG(d, kind));
    }
    (void)cur;
    (void)sel;
    select_drive(st);
}

/* the drive list refilled and its notification sent (observed after
   every change of folder: WM_COMMAND cmb2 8000h, LBSELCHSTRING) */
static void drives_refresh(fstate FAR *st)
{
    fill_drives(st);
    SendMessage(st->dlg, WM_COMMAND, cmb2, MAKELONG(GetDlgItem(st->dlg, cmb2), CBN_INIT));
}

static void notify_sel(fstate FAR *st, int id, int index, int code)
{
    if (st->hook)
        call_hook(st->hook, st->dlg, msg_lbselch, id, MAKELONG(index, code));
}

/* ---- set-up ---- */

/* the filter strings: description, pattern, ... (custom first) */
static void fill_types(fstate FAR *st)
{
    LPOPENFILENAME o = st->ofn;
    HWND cb = GetDlgItem(st->dlg, cmb1);
    LPCSTR p;
    int sel;
    st->npats = 0;
    if (o->lpstrCustomFilter && o->lpstrCustomFilter[0]) {
        LPSTR c = o->lpstrCustomFilter;
        LPSTR FAR *g = mem_realloc(st->pats, (st->npats + 1) * sizeof(LPSTR));
        if (g) {
            st->pats = g;
            st->pats[st->npats++] = c + s_len(c) + 1;
            st->has_custom = TRUE;
            if (cb)
                SendMessage(cb, CB_ADDSTRING, 0, (LPARAM)c);
        }
    }
    for (p = o->lpstrFilter; p && *p; ) {
        LPSTR FAR *g = mem_realloc(st->pats, (st->npats + 1) * sizeof(LPSTR));
        if (!g)
            break;
        st->pats = g;
        if (cb)
            SendMessage(cb, CB_ADDSTRING, 0, (LPARAM)p);
        p += s_len(p) + 1;
        st->pats[st->npats++] = (LPSTR)p;
        p += s_len(p) + 1;
    }
    sel = (int)o->nFilterIndex;
    if (!st->has_custom)
        sel--;
    if (sel < 0 || sel >= st->npats)
        sel = 0;
    if (cb)
        SendMessage(cb, CB_SETCURSEL, sel, 0);
}

static void init_dialog(fstate FAR *st)
{
    LPOPENFILENAME o = st->ofn;
    HWND dlg = st->dlg;
    LPSTR t;
    LPCSTR pat;
    if (o->lpstrTitle) {
        SetWindowText(dlg, o->lpstrTitle);
    } else {
        t = load_str(st->save ? IDS_SAVEAS : IDS_OPEN);
        if (t) {
            SetWindowText(dlg, t);
            mem_free(t);
        }
    }
    GetWindowText(dlg, st->caption, sizeof st->caption);
    if (st->save) {
        t = load_str(IDS_SAVETYPE);
        if (t) {
            SetDlgItemText(dlg, stc2, t);
            mem_free(t);
        }
    }
    if (!(o->Flags & OFN_SHOWHELP)) {
        ShowWindow(GetDlgItem(dlg, pshHelp), SW_HIDE);
        EnableWindow(GetDlgItem(dlg, pshHelp), FALSE);
    }
    if (o->Flags & OFN_HIDEREADONLY)
        ShowWindow(GetDlgItem(dlg, chx1), SW_HIDE);
    else
        CheckDlgButton(dlg, chx1, (o->Flags & OFN_READONLY) != 0);
    st->startdir = cur_dir();
    if (o->lpstrInitialDir && o->lpstrInitialDir[0]) {
        if (o->lpstrInitialDir[1] == ':')
            dos_setdrive((o->lpstrInitialDir[0] | 0x20) - 'a');
        dos_chdir(o->lpstrInitialDir);
    }
    {   /* the name box as wide as the file list below it (observed: the
           original moves it one pixel left and widens it by two, to the
           list's edges) */
        HWND e = GetDlgItem(dlg, edt1), l = GetDlgItem(dlg, lst1);
        if (e && l) {
            RECT re, rl;
            int w, h;
            GetWindowRect(e, &re);
            GetWindowRect(l, &rl);
            w = rl.right - rl.left;
            h = re.bottom - re.top;
            ScreenToClient(dlg, (LPPOINT)&re.left);
            ScreenToClient(dlg, (LPPOINT)&rl.left);
            MoveWindow(e, rl.left, re.top, w, h, FALSE);
        }
    }
    /* a path, as DOS bounds it; several names: what lpstrFile holds */
    SendDlgItemMessage(dlg, edt1, EM_LIMITTEXT,
                       st->multi && o->nMaxFile > DOS_PATH ? (WPARAM)(o->nMaxFile - 1) : DOS_PATH - 1, 0);
    /* observed: F4 does not drop the lists down, Down does */
    SendDlgItemMessage(dlg, cmb1, CB_SETEXTENDEDUI, TRUE, 0);
    SendDlgItemMessage(dlg, cmb2, CB_SETEXTENDEDUI, TRUE, 0);
    fill_types(st);
    /* observed order of the notifications (the hook sees them) */
    SendMessage(dlg, WM_COMMAND, cmb1, MAKELONG(GetDlgItem(dlg, cmb1), CBN_INIT));
    pat = cur_pattern(st);
    if (o->lpstrFile && o->lpstrFile[0]) {
        /* the text as given, in lower case, in the name box: a path is
           not gone to (observed: "C:\WINDOWS\SYSTEM\README.TXT" shows
           whole, the folder stays). The caller's buffer itself comes back
           in lower case, even after Cancel. A pattern without a folder
           lists those files (UNVERIFIED: a pattern with one). */
        AnsiLower(o->lpstrFile);
        SetDlgItemText(dlg, edt1, o->lpstrFile);
        if (is_wild(o->lpstrFile) && name_part(o->lpstrFile) == o->lpstrFile)
            fill_files(st, o->lpstrFile);
    } else {
        pattern_text(st);
    }
    fill_dirs(st);
    fill_drives(st);
    SendMessage(dlg, WM_COMMAND, cmb2, MAKELONG(GetDlgItem(dlg, cmb2), CBN_INIT));
    subclass(GetDlgItem(dlg, lst2), (FARPROC)dwLBSubclass);
    subclass(GetDlgItem(dlg, IDOK), (FARPROC)dwOKSubclass);
}

/* ---- several files (OFN_ALLOWMULTISELECT) ----
 * Observed: the name box lists the selected names in lower case, one
 * space apart; OK returns "<FOLDER> name name ..." (the folder in capitals
 * as the current directory is, the names as in the box), nFileOffset at
 * the first name, nFileExtension 0, an empty lpstrFileTitle. */

static void multi_names(fstate FAR *st)
{
    HWND lb = GetDlgItem(st->dlg, lst1);
    int n = (int)SendMessage(lb, LB_GETSELCOUNT, 0, 0), i, k;
    int FAR *sel;
    LPSTR text;
    char t[DOS_PATH];
    if (n <= 0)
        return;
    sel = mem_alloc((DWORD)n << 1);                  /* ints */
    text = mem_alloc(((DWORD)n << 3) + ((DWORD)n << 2) + n + 1);  /* 12 + space each */
    if (sel && text) {
        n = (int)SendMessage(lb, LB_GETSELITEMS, n, (LPARAM)sel);
        for (i = k = 0; i < n; i++) {
            SendMessage(lb, LB_GETTEXT, sel[i], (LPARAM)(LPSTR)t);
            s_lower(t);
            if (i)
                text[k++] = ' ';
            s_cpy(text + k, t, 13);
            k += s_len(t);
        }
        SetDlgItemText(st->dlg, edt1, text);
    }
    mem_free(sel);
    mem_free(text);
}

/* OK with several names in the box: TRUE ends the dialog */
static BOOL take_multi(fstate FAR *st, LPCSTR names)
{
    LPOPENFILENAME o = st->ofn;
    LPSTR dir = cur_dir(), r;
    DWORD need;
    if (!dir)
        return FALSE;
    need = (DWORD)s_len(dir) + 1 + s_len(names) + 1;
    r = mem_alloc(need);
    if (r) {
        s_cpy(r, dir, (int)need);
        s_cat(r, " ", (int)need);
        s_cat(r, names, (int)need);
        o->nFileOffset = s_len(dir) + 1;
        o->nFileExtension = 0;
        if (need > o->nMaxFile) {
            if (o->lpstrFile && o->nMaxFile >= 2) {
                o->lpstrFile[0] = (char)LOBYTE(need - 1);
                o->lpstrFile[1] = (char)HIBYTE(need - 1);
            }
            cd_seterr(FNERR_BUFFERTOOSMALL);
        } else {
            s_cpy(o->lpstrFile, r, (int)o->nMaxFile);
            if (o->lpstrFileTitle && o->nMaxFileTitle)
                o->lpstrFileTitle[0] = 0;
            {
                int i = (int)SendDlgItemMessage(st->dlg, cmb1, CB_GETCURSEL, 0, 0);
                o->nFilterIndex = st->has_custom ? i : i + 1;
            }
            if (IsWindowVisible(GetDlgItem(st->dlg, chx1))) {
                if (IsDlgButtonChecked(st->dlg, chx1))
                    o->Flags |= OFN_READONLY;
                else
                    o->Flags &= ~OFN_READONLY;
            }
        }
        mem_free(r);
    }
    mem_free(dir);
    if (cd_geterr())
        return TRUE;
    if (st->hook && call_hook(st->hook, st->dlg, msg_fileok, 0, (LPARAM)o))
        return FALSE;
    return TRUE;
}

/* a typed pattern goes into the caller's custom filter, after its
   description (observed: an empty buffer becomes "\0*.ini") */
static void keep_pattern(fstate FAR *st, LPCSTR pat)
{
    LPOPENFILENAME o = st->ofn;
    int d, room;
    if (!o->lpstrCustomFilter || o->nMaxCustFilter < 2)
        return;
    d = s_len(o->lpstrCustomFilter) + 1;
    room = (int)o->nMaxCustFilter - d;
    if (room < 2)
        return;
    s_cpy(o->lpstrCustomFilter + d, pat, room - 1);
    o->lpstrCustomFilter[d + s_len(o->lpstrCustomFilter + d) + 1] = 0;
}

/* ---- OK ---- */

static void refresh(fstate FAR *st, LPCSTR pattern)
{
    fill_dirs(st);
    fill_files(st, pattern ? pattern : cur_pattern(st));
}

static BOOL change_drive(fstate FAR *st, int d)
{
    if (drive_kind(d) < 0) {
        cd_msgbox_c(st->dlg, st->caption, IDS_NODRIVE, (char)('a' + d), MB_OK | MB_ICONEXCLAMATION);
        return FALSE;
    }
    dos_setdrive(d);
    if (dos_getdrive() != d) {
        cd_msgbox_c(st->dlg, st->caption, IDS_CANTSELDRIVE, (char)('a' + d), MB_OK | MB_ICONEXCLAMATION);
        return FALSE;
    }
    {   /* a drive without a readable disk */
        char b[DOS_PATH];
        if (!dos_getcwd(d + 1, b)) {
            cd_msgbox_c(st->dlg, st->caption, IDS_CANTREAD, (char)('a' + d), MB_OK | MB_ICONEXCLAMATION);
            return FALSE;
        }
    }
    return TRUE;
}

/* the results into the caller's structure; FALSE: buffer too small */
static BOOL give_result(fstate FAR *st, LPCSTR full)
{
    LPOPENFILENAME o = st->ofn;
    int len = s_len(full);
    LPCSTR n = name_part(full), e;
    o->nFileOffset = (UINT)(n - full);
    e = s_rchr(n, '.');
    o->nFileExtension = e ? (UINT)(e + 1 - full) : (UINT)len;
    if (o->lpstrDefExt && (!e || lstrcmpi(e + 1, o->lpstrDefExt)))
        o->Flags |= OFN_EXTENSIONDIFFERENT;
    else
        o->Flags &= ~OFN_EXTENSIONDIFFERENT;
    if ((DWORD)len + 1 > o->nMaxFile) {
        if (o->lpstrFile && o->nMaxFile >= 2) {
            o->lpstrFile[0] = (char)LOBYTE(len);
            o->lpstrFile[1] = (char)HIBYTE(len);
        }
        cd_seterr(FNERR_BUFFERTOOSMALL);
        return FALSE;
    }
    s_cpy(o->lpstrFile, full, (int)o->nMaxFile);
    if (o->lpstrFileTitle && o->nMaxFileTitle)
        s_cpy(o->lpstrFileTitle, n, (int)o->nMaxFileTitle);
    if (!(o->Flags & OFN_HIDEREADONLY) || IsWindowVisible(GetDlgItem(st->dlg, chx1))) {
        if (IsDlgButtonChecked(st->dlg, chx1))
            o->Flags |= OFN_READONLY;
        else
            o->Flags &= ~OFN_READONLY;
    }
    {
        int i = (int)SendDlgItemMessage(st->dlg, cmb1, CB_GETCURSEL, 0, 0);
        o->nFilterIndex = st->has_custom ? i : i + 1;
    }
    return TRUE;
}

/* does the folder part of a full path exist? */
static BOOL path_exists(LPCSTR full)
{
    char b[DOS_PATH];
    LPSTR n;
    int a;
    s_cpy(b, full, sizeof b);
    n = name_part(b);
    if (n <= b + 3)                     /* the root */
        return dos_drive_exists((b[0] | 0x20) - 'a');
    n[-1] = 0;
    a = dos_getattr(b);
    return a >= 0 && (a & 0x10);
}

/* OK with the name typed. TRUE: the dialog ends. */
static BOOL take_name(fstate FAR *st)
{
    LPOPENFILENAME o = st->ofn;
    HWND dlg = st->dlg;
    char typed[DOS_PATH], full[DOS_PATH + 4];
    LPSTR name, p;
    OFSTRUCT of;
    int a;
    DWORD fl = o->Flags;

    if (st->multi) {                    /* several names: as many as typed */
        int len = GetWindowTextLength(GetDlgItem(dlg, edt1));
        LPSTR all = mem_alloc(len + 1), q;
        BOOL several = FALSE, done = FALSE;
        if (all) {
            GetDlgItemText(dlg, edt1, all, len + 1);
            for (q = all; *q == ' '; q++)
                ;
            for (p = q + s_len(q); p > q && p[-1] == ' '; )
                *--p = 0;
            several = s_chr(q, ' ') != NULL;
            if (several)
                done = take_multi(st, q);
            mem_free(all);
        }
        if (several)
            return done;
    }
    GetDlgItemText(dlg, edt1, typed, sizeof typed);
    for (p = typed; *p == ' '; p++)
        ;
    if (p != typed)
        s_cpy(typed, p, sizeof typed);
    for (p = typed + s_len(typed); p > typed && p[-1] == ' '; )
        *--p = 0;
    if (!typed[0])
        return FALSE;

    /* "x:": that drive */
    if (typed[1] == ':' && !typed[2]) {
        if (change_drive(st, (typed[0] | 0x20) - 'a')) {
            refresh(st, NULL);
            pattern_text(st);
            drives_refresh(st);
        }
        SendDlgItemMessage(dlg, edt1, EM_SETSEL, 0, MAKELPARAM(0, -1));
        return FALSE;
    }

    name = name_part(typed);

    /* a pattern: list it (in its folder) */
    if (is_wild(name)) {
        if (name != typed) {
            char c = *name;
            *name = 0;
            if (typed[1] == ':' && !change_drive(st, (typed[0] | 0x20) - 'a')) {
                *name = c;
                return FALSE;
            }
            if (name > typed + 1 && !(name == typed + 2 && typed[1] == ':')) {
                if (name[-1] == '\\' && name - 1 > typed && name[-2] != ':')
                    name[-1] = 0;
                if (!dos_chdir(typed)) {
                    *name = c;
                    cd_msgbox(dlg, st->caption, IDS_PATHNOTFOUND, typed, MB_OK | MB_ICONEXCLAMATION);
                    return FALSE;
                }
            }
            *name = c;
        }
        SetDlgItemText(dlg, edt1, name);
        keep_pattern(st, name);
        refresh(st, name);
        drives_refresh(st);
        SendDlgItemMessage(dlg, edt1, EM_SETSEL, 0, MAKELPARAM(0, -1));
        return FALSE;
    }

    /* a folder: go there */
    a = dos_getattr(typed);
    if (a >= 0 && (a & 0x10)) {
        if (typed[1] == ':')
            change_drive(st, (typed[0] | 0x20) - 'a');
        dos_chdir(typed);
        refresh(st, NULL);
        pattern_text(st);
        drives_refresh(st);
        SendDlgItemMessage(dlg, edt1, EM_SETSEL, 0, MAKELPARAM(0, -1));
        return FALSE;
    }

    /* a file: the file list scrolls to the first name starting with the
       typed text's first character (observed: "setupx.txt" puts
       scrnsave.scr on top, "zzz.txt" zigzag.bmp, "c:\nodir\x.txt"
       calc.exe; nothing is selected) */
    {
        char first[2];
        int at;
        first[0] = typed[0];
        first[1] = 0;
        at = (int)SendDlgItemMessage(dlg, lst1, LB_FINDSTRING, (WPARAM)-1, (LPARAM)(LPSTR)first);
        if (at >= 0)
            SendDlgItemMessage(dlg, lst1, LB_SETTOPINDEX, at, 0);
    }
    if (!valid_name(name)) {
        cd_msgbox(dlg, st->caption, IDS_INVALIDNAME, typed, MB_OK | MB_ICONEXCLAMATION);
        return FALSE;
    }
    if (device_name(name)) {
        cd_msgbox(dlg, st->caption, IDS_DEVICENAME, typed, MB_OK | MB_ICONEXCLAMATION);
        return FALSE;
    }
    if (OpenFile(typed, &of, OF_PARSE) == HFILE_ERROR) {
        cd_msgbox(dlg, st->caption, IDS_INVALIDNAME, typed, MB_OK | MB_ICONEXCLAMATION);
        return FALSE;
    }
    s_cpy(full, of.szPathName, sizeof full);
    if (o->lpstrDefExt && o->lpstrDefExt[0] && !s_chr(name_part(full), '.')) {
        s_cat(full, ".", sizeof full);
        s_cat(full, o->lpstrDefExt, sizeof full);
    }

    if ((fl & OFN_PATHMUSTEXIST) && !path_exists(full)) {
        cd_msgbox(dlg, st->caption, IDS_PATHNOTFOUND, typed, MB_OK | MB_ICONEXCLAMATION);
        return FALSE;
    }
    a = dos_getattr(full);
    if (a < 0) {                        /* not there */
        if (fl & OFN_CREATEPROMPT) {
            if (cd_msgbox(dlg, st->caption, IDS_CREATEPROMPT, typed,
                          MB_YESNO | MB_ICONQUESTION) != IDYES)
                return FALSE;
        } else if (fl & OFN_FILEMUSTEXIST) {
            cd_msgbox(dlg, st->caption, IDS_FILENOTFOUND, typed, MB_OK | MB_ICONEXCLAMATION);
            return FALSE;
        }
        if (st->save && !(fl & OFN_NOTESTFILECREATE)) {
            int e = dos_creat_new(full);
            if (e == 0) {
                dos_delete(full);
            } else if (e == 3) {
                cd_msgbox(dlg, st->caption, IDS_PATHNOTFOUND, typed, MB_OK | MB_ICONEXCLAMATION);
                return FALSE;
            } else if (e == 0x13) {
                cd_msgbox_c(dlg, st->caption, IDS_WRITEPROTECT, full[0], MB_OK | MB_ICONEXCLAMATION);
                return FALSE;
            } else if (e == 5) {
                cd_msgbox(dlg, st->caption, IDS_ACCESSDENIED, typed, MB_OK | MB_ICONEXCLAMATION);
                return FALSE;
            }
        }
    } else {
        if (st->save && (fl & OFN_NOREADONLYRETURN) && (a & 0x01)) {
            cd_msgbox(dlg, st->caption, IDS_READONLY, full, MB_OK | MB_ICONEXCLAMATION);
            return FALSE;
        }
        if (st->save && (fl & OFN_OVERWRITEPROMPT)) {
            if (cd_msgbox(dlg, st->caption, IDS_FILEEXISTS, full,
                          MB_YESNO | MB_ICONEXCLAMATION) != IDYES)
                return FALSE;
        }
        if (!st->save || !(fl & OFN_NOTESTFILECREATE)) {
            /* in use by another program? */
            int e = dos_open_test(full, st->save ? 0x12 : 0x20);
            if (e == 0x20 || e == 0x21) {
                UINT r = OFN_SHAREWARN;
                if (st->hook && !(fl & OFN_SHAREAWARE))
                    r = call_hook(st->hook, dlg, msg_sharevi, 0, (LPARAM)(LPSTR)full);
                if (!(fl & OFN_SHAREAWARE) && r != OFN_SHAREFALLTHROUGH) {
                    if (r == OFN_SHAREWARN)
                        cd_msgbox(dlg, st->caption, IDS_SHARING, full, MB_OK | MB_ICONEXCLAMATION);
                    return FALSE;
                }
            }
        }
    }

    /* the application's last word */
    if (!give_result(st, full))
        return TRUE;                    /* ends: FALSE with the error */
    if (st->hook && call_hook(st->hook, dlg, msg_fileok, 0, (LPARAM)o))
        return FALSE;
    return TRUE;
}

/* ---- the dialog procedure ---- */

static void end(fstate FAR *st, BOOL ok)
{
    st->ended = TRUE;
    if (st->startdir && (st->ofn->Flags & OFN_NOCHANGEDIR)) {
        if (st->startdir[1] == ':')
            dos_setdrive(st->startdir[0] - 'A');
        dos_chdir(st->startdir);
    }
    EndDialog(st->dlg, ok);
}

static BOOL file_proc(HWND dlg, UINT msg, WPARAM wp, LPARAM lp, BOOL save)
{
    fstate FAR *st;
    int i;

    if (msg == WM_INITDIALOG) {
        st = (fstate FAR *)lp;
        st->save = save;
        st->dlg = dlg;
        state_set(dlg, st);
        init_dialog(st);
        if (st->hook)
            return (BOOL)call_hook(st->hook, dlg, msg, wp, (LPARAM)st->ofn);
        return TRUE;
    }
    st = (fstate FAR *)state_get(dlg);
    if (msg == WM_MEASUREITEM) {
        measure_item(dlg, (MEASUREITEMSTRUCT FAR *)lp);
        return TRUE;
    }
    if (!st)
        return FALSE;
    if (st->hook) {
        UINT r = call_hook(st->hook, dlg, msg, wp, lp);
        if (r)
            return (BOOL)r;
    }
    switch (msg) {
    case WM_DRAWITEM: {
        const DRAWITEMSTRUCT FAR *d = (const DRAWITEMSTRUCT FAR *)lp;
        if (d->CtlID == lst2)
            draw_dir_item(d);
        else if (d->CtlID == cmb2)
            draw_drive_item(d);
        else
            draw_file_item(d, st->save);
        return TRUE;
    }
    case WM_DESTROY:
        state_remove(dlg);
        return FALSE;
    case WM_COMMAND:
        switch (wp) {
        case IDOK:
            if (take_name(st))
                end(st, cd_geterr() == 0);
            return TRUE;
        case IDCANCEL:
            end(st, FALSE);
            return TRUE;
        case pshHelp:
            send_help(dlg, st->ofn->hwndOwner, (LPARAM)st->ofn);
            return TRUE;
        case lst1:
            if (HIWORD(lp) == LBN_SELCHANGE && st->multi) {
                multi_names(st);
            } else if (HIWORD(lp) == LBN_SELCHANGE) {
                char t[DOS_PATH];
                i = (int)SendDlgItemMessage(dlg, lst1, LB_GETCURSEL, 0, 0);
                if (i >= 0) {
                    SendDlgItemMessage(dlg, lst1, LB_GETTEXT, i, (LPARAM)(LPSTR)t);
                    s_lower(t);
                    SetDlgItemText(dlg, edt1, t);
                    notify_sel(st, lst1, i, CD_LBSELCHANGE);
                }
            } else if (HIWORD(lp) == LBN_DBLCLK) {
                SendMessage(dlg, WM_COMMAND, IDOK, 0);
            }
            return TRUE;
        case lst2:
            if (HIWORD(lp) == LBN_DBLCLK) {
                /* the path down to the chosen folder */
                int at = (int)SendDlgItemMessage(dlg, lst2, LB_GETCURSEL, 0, 0), k;
                int level = (int)LOWORD(SendDlgItemMessage(dlg, lst2, LB_GETITEMDATA, at, 0));
                char path[DOS_PATH];
                path[0] = 0;
                for (k = 0; k <= at; k++) {
                    DWORD data = SendDlgItemMessage(dlg, lst2, LB_GETITEMDATA, k, 0);
                    char t[DOS_PATH];
                    if (k < at && (HIWORD(data) == PIC_CLOSED || (int)LOWORD(data) >= level))
                        continue;
                    SendDlgItemMessage(dlg, lst2, LB_GETTEXT, k, (LPARAM)(LPSTR)t);
                    if (k > 0 && path[s_len(path) - 1] != '\\')
                        s_cat(path, "\\", sizeof path);
                    s_cat(path, t, sizeof path);
                }
                dos_chdir(path);
                refresh(st, NULL);
            }
            return TRUE;
        case cmb1:
            switch (HIWORD(lp)) {
            case CBN_INIT:                  /* the type chosen: list it */
                i = (int)SendDlgItemMessage(dlg, cmb1, CB_GETCURSEL, 0, 0);
                st->ofn->nFilterIndex = st->has_custom ? i : i + 1;
                pattern_text(st);
                fill_files(st, cur_pattern(st));
                notify_sel(st, cmb1, i, CD_LBSELCHANGE);
                break;
            case CBN_DROPDOWN:
                if (!st->ended)
                    st->ofn->Flags |= OFN_TYPESDOWN;
                break;
            case CBN_CLOSEUP:
                if (st->ended)
                    break;
                st->ofn->Flags &= ~OFN_TYPESDOWN;
                if (st->pend_type) {
                    st->pend_type = FALSE;
                    SendMessage(dlg, WM_COMMAND, cmb1, MAKELONG(LOWORD(lp), CBN_INIT));
                }
                break;
            case CBN_SELCHANGE:
                if (st->ofn->Flags & OFN_TYPESDOWN)
                    st->pend_type = TRUE;
                else
                    SendMessage(dlg, WM_COMMAND, cmb1, MAKELONG(LOWORD(lp), CBN_INIT));
                break;
            }
            return TRUE;
        case cmb2:
            switch (HIWORD(lp)) {
            case CBN_INIT: {                /* the drive chosen: go there */
                int d;
                i = (int)SendDlgItemMessage(dlg, cmb2, CB_GETCURSEL, 0, 0);
                d = LOWORD(SendDlgItemMessage(dlg, cmb2, CB_GETITEMDATA, i, 0));
                if (i >= 0 && d != dos_getdrive()) {
                    if (change_drive(st, d)) {
                        refresh(st, NULL);
                    } else {
                        select_drive(st);
                        i = (int)SendDlgItemMessage(dlg, cmb2, CB_GETCURSEL, 0, 0);
                    }
                }
                notify_sel(st, cmb2, i, CD_LBSELCHANGE);
                break;
            }
            case CBN_DROPDOWN:
                if (!st->ended)
                    st->ofn->Flags |= OFN_DRIVESDOWN;
                break;
            case CBN_CLOSEUP:
                if (st->ended)
                    break;
                st->ofn->Flags &= ~OFN_DRIVESDOWN;
                if (st->pend_drive) {
                    st->pend_drive = FALSE;
                    SendMessage(dlg, WM_COMMAND, cmb2, MAKELONG(LOWORD(lp), CBN_INIT));
                }
                break;
            case CBN_SELCHANGE:
                if (st->ofn->Flags & OFN_DRIVESDOWN)
                    st->pend_drive = TRUE;
                else
                    SendMessage(dlg, WM_COMMAND, cmb2, MAKELONG(LOWORD(lp), CBN_INIT));
                break;
            }
            return TRUE;
        }
        break;
    }
    return FALSE;
}

BOOL CDPROC FileOpenDlgProc(HWND dlg, UINT msg, WPARAM wp, LPARAM lp)
{
    return file_proc(dlg, msg, wp, lp, FALSE);
}

BOOL CDPROC FileSaveDlgProc(HWND dlg, UINT msg, WPARAM wp, LPARAM lp)
{
    return file_proc(dlg, msg, wp, lp, TRUE);
}

/* the OK button: as a button (observed: subclassed) */
LRESULT CDPROC dwOKSubclass(HWND w, UINT msg, WPARAM wp, LPARAM lp)
{
    return call_old(w, msg, wp, lp);
}

/* the folder list: Enter opens the folder (UNVERIFIED: what else the
   original's subclass does) */
LRESULT CDPROC dwLBSubclass(HWND w, UINT msg, WPARAM wp, LPARAM lp)
{
    if (msg == WM_GETDLGCODE)
        return call_old(w, msg, wp, lp);
    return call_old(w, msg, wp, lp);
}

/* ---- the calls ---- */

static BOOL file_dialog(LPOPENFILENAME o, BOOL save)
{
    fstate FAR *st;
    cd_template t;
    int r;
    cd_seterr(0);
    if (!o) {
        cd_seterr(CDERR_INITIALIZATION);
        return FALSE;
    }
    if (o->lStructSize != sizeof(OPENFILENAME)) {
        cd_seterr(CDERR_STRUCTSIZE);
        return FALSE;
    }
    if ((o->Flags & OFN_ENABLEHOOK) && !o->lpfnHook) {
        cd_seterr(CDERR_NOHOOK);
        return FALSE;
    }
    if (o->Flags & OFN_CREATEPROMPT)
        o->Flags |= OFN_FILEMUSTEXIST;
    if (o->Flags & OFN_FILEMUSTEXIST)
        o->Flags |= OFN_PATHMUSTEXIST;
    st = mem_alloc(sizeof(fstate));     /* at offset 0: state_get */
    if (!st) {
        cd_seterr(CDERR_MEMALLOCFAILURE);
        return FALSE;
    }
    st->ofn = o;
    st->save = save;
    st->multi = (o->Flags & OFN_ALLOWMULTISELECT) != 0;
    st->hook = (o->Flags & OFN_ENABLEHOOK) ? (cd_hook)o->lpfnHook : NULL;
    t.app = o->hInstance;
    t.use_handle = (o->Flags & OFN_ENABLETEMPLATEHANDLE) != 0;
    t.use_name = !t.use_handle && (o->Flags & OFN_ENABLETEMPLATE);
    t.name = o->lpTemplateName;
    t.ours = MAKEINTRESOURCE(st->multi ? DLG_MULTIOPEN : DLG_OPEN);
    pics_load();
    r = cd_modal(&t, o->hwndOwner,
                 save ? (FARPROC)FileSaveDlgProc : (FARPROC)FileOpenDlgProc, (LPARAM)(LPVOID)st);
    mem_free(st->pats);
    mem_free(st->startdir);
    mem_free(st);
    return r == 1;
}

BOOL CDAPI GetOpenFileName(LPOPENFILENAME o)
{
    return file_dialog(o, FALSE);
}

BOOL CDAPI GetSaveFileName(LPOPENFILENAME o)
{
    return file_dialog(o, TRUE);
}
