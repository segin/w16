/*
 * marks.c - w16 Windows Help: bookmarks, annotations, Copy, Print Topic
 * and Print Setup (ARCHITECTURE.md 15.10).
 *
 * Observed on 3.1 (WH3, WH4; the files compared byte for byte by
 * tests/winhelp/hfstest.c):
 *  - bookmarks live in WINHELP.BMK in the Windows directory, a help file
 *    system (hfs.c, directory pages of 1024 bytes) with one file per help
 *    file, named its name without extension and the |SYSTEM date in 8
 *    lowercase hex digits ("NOTEPAD29b20284"): a word, the help file's
 *    format number (15h); the date; the number of bookmarks; the file's
 *    size; then each bookmark: the TOPICPOS of the first link after its
 *    topic's header, a long 0 (UNVERIFIED: a scroll position), its name.
 *    Written when a bookmark is defined. Bookmark > Define... ("Bookmark
 *    Define": the name, the topic's title at first, selected; the list of
 *    this file's bookmarks; OK, Cancel, Delete); the menu lists them "&1
 *    name", "&2 name"... under Define... (w16: More... after nine,
 *    UNVERIFIED);
 *  - annotations live in <name>.ANN (w16: in the Windows directory,
 *    UNVERIFIED for help files elsewhere), a help file system with
 *    directory pages of 128 bytes: @VERSION (08 62 6D 66 01 00), @LINK
 *    (a count, then per annotation the TOPICPOS as for bookmarks and two
 *    longs 0) and per annotation a file named the TOPICPOS in decimal
 *    and "!0" holding its text. The file is made, with no annotations,
 *    when Annotate opens. Annotate: "Annotation:" and a multi-line edit;
 *    Save (enabled once there is text), Cancel, Delete (with an
 *    annotation), Copy (with text), Paste (with text on the clipboard);
 *  - an annotated topic starts with the annotation mark (w16's own
 *    14 x 16 picture), which opens Annotate;
 *  - Edit > Copy...: "Copy", a sizeable dialog with the topic's text as
 *    shown, one line per screen line and an empty line after each of the
 *    topic's paragraphs records, Copy and Cancel;
 *  - Print Setup without a printer: COMMDLG's own message, then "Unable
 *    to setup printer." (information icon).
 */
#include "winhelp.h"
#include "hfs.h"
#include <commdlg.h>

/* ---- dialogs ---- */

/* controls where 3.1 has them, in pixels of the client area (the
   dialog-unit templates come out a pixel off here and there) */
static void place(HWND d, int id, int x, int y, int cx, int cy)
{
    MoveWindow(GetDlgItem(d, id), x, y, cx, cy, FALSE);
}

/* the Annotate and Copy dialogs' places: WIN.INI [Windows Help]
   A_WindowPosition and C_WindowPosition, written when they close (WH3,
   WH4); first, 43, 47 (Annotate) and -45, 65 (Copy) from the main
   window's corner, 307 x 237 and 470 x 225 */
static void dlg_place(HWND d, LPCSTR key, int dx, int dy, int cx, int cy)
{
    char s[64];
    int v[5], i;
    LPSTR p = s;
    RECT r;
    GetWindowRect(g_frame, &r);
    v[0] = r.left + dx;
    v[1] = r.top + dy;
    v[2] = cx;
    v[3] = cy;
    if (GetProfileString("Windows Help", key, "", s, sizeof s) && *p == '[') {
        p++;
        for (i = 0; i < 4; i++) {
            int n = 0, neg = 0;
            if (*p == '-') {
                neg = 1;
                p++;
            }
            while (*p >= '0' && *p <= '9')
                n = n * 10 + (*p++ - '0');
            v[i] = neg ? -n : n;
            if (*p == ',')
                p++;
        }
    }
    SetWindowPos(d, NULL, v[0], v[1], v[2], v[3], SWP_NOZORDER);
}

static void dlg_save(HWND d, LPCSTR key)
{
    RECT r;
    char s[64];
    GetWindowRect(d, &r);
    wsprintf(s, "[%d,%d,%d,%d,0]", r.left, r.top, r.right - r.left, r.bottom - r.top);
    WriteProfileString("Windows Help", key, s);
}

/* ---- files ---- */

static void windir_path(LPSTR out, LPCSTR name)
{
    int n = GetWindowsDirectory(out, PATHMAX - 14);
    if (n && out[n - 1] != '\\')
        out[n++] = '\\';
    lstrcpy(out + n, name);
}

static HFS FAR *fs_read(LPCSTR path)
{
    HFILE f = _lopen(path, OF_READ);
    HFS FAR *h;
    LONG n;
    LPB buf;
    if (f == HFILE_ERROR)
        return NULL;
    n = _llseek(f, 0, 2);
    _llseek(f, 0, 0);
    if (n <= 0 || n > 0xFF00L) {
        _lclose(f);
        return NULL;
    }
    buf = (LPB)hio_alloc(n);
    h = (HFS FAR *)hio_alloc(sizeof(HFS));
    if (!buf || !h || _lread(f, buf, (UINT)n) != (UINT)n || !hfs_load(h, buf, n)) {
        _lclose(f);
        if (buf)
            hio_free(buf);
        if (h)
            hio_free(h);
        return NULL;
    }
    _lclose(f);
    hio_free(buf);
    return h;
}

static BOOL fs_write(HFS FAR *h, LPCSTR path)
{
    HFILE f = _lcreat(path, 0);
    BOOL ok;
    if (f == HFILE_ERROR)
        return FALSE;
    ok = _lwrite(f, (LPCSTR)h->buf, (UINT)h->size) == (UINT)h->size;
    _lclose(f);
    return ok;
}

static HFS FAR *fs_new(WORD pagesize)
{
    HFS FAR *h = (HFS FAR *)hio_alloc(sizeof(HFS));
    if (h && !hfs_new(h, pagesize)) {
        hio_free(h);
        h = NULL;
    }
    return h;
}

static void fs_close(HFS FAR *h)
{
    if (h) {
        hfs_free(h);
        hio_free(h);
    }
}

/* the key bookmarks and annotations use: the TOPICPOS of the first link
   after the topic's header */
static DWORD topic_key(void)
{
    HLPLINK l;
    DWORD pos;
    if (!g_hlp || g_cur < 0)
        return 0;
    pos = g_hlp->topics[g_cur].pos;
    if (!hlp_link(g_hlp, pos, &l))
        return 0;
    return hlp_nextlink(g_hlp, pos, &l);
}

/* the topic a key belongs to: the last whose header is before it */
static int topic_of_key(DWORD key)
{
    int i, r = -1;
    for (i = 0; i < (int)g_hlp->ntopics; i++)
        if (g_hlp->topics[i].pos < key)
            r = i;
    return r;
}

static void base_upper(LPSTR out, LPCSTR path)
{
    LPCSTR b = path, p;
    int n = 0;
    for (p = path; *p; p++)
        if (*p == '\\' || *p == ':' || *p == '/')
            b = p + 1;
    while (*b && *b != '.' && n < 8)
        out[n++] = *b++;
    out[n] = 0;
    AnsiUpper(out);
}

/* ---- bookmarks ---- */

#define MAXBM 64
static struct {
    DWORD pos, scroll;
    char name[64];
} bms[MAXBM];
static int nbms;

static void bm_name(LPSTR out)
{
    char b[16];
    base_upper(b, g_file);
    wsprintf(out, "%s%08lx", (LPSTR)b, g_hlp->gendate);
}

static void bm_load(void)
{
    char path[PATHMAX], name[32];
    HFS FAR *f;
    DWORD n;
    LPB d;
    nbms = 0;
    if (!g_hlp)
        return;
    windir_path(path, "WINHELP.BMK");
    f = fs_read(path);
    if (!f)
        return;
    bm_name(name);
    d = hfs_get(f, name, &n);
    if (d && n >= 10) {
        WORD cnt = hlp_rw(d + 6), i;
        DWORD k = 10;
        for (i = 0; i < cnt && nbms < MAXBM && k + 9 <= n; i++) {
            bms[nbms].pos = hlp_rl(d + k);
            bms[nbms].scroll = hlp_rl(d + k + 4);
            hlp_strcpyn(bms[nbms].name, (LPCSTR)(d + k + 8), sizeof bms[0].name);
            k += 8 + hlp_strlen((LPCSTR)(d + k + 8)) + 1;
            nbms++;
        }
    }
    fs_close(f);
}

static void bm_save(void)
{
    char path[PATHMAX], name[32];
    HFS FAR *f;
    LPB b;
    DWORD k = 10;
    int i;
    windir_path(path, "WINHELP.BMK");
    f = fs_read(path);
    if (!f)
        f = fs_new(1024);
    if (!f)
        return;
    bm_name(name);
    b = (LPB)hio_alloc(10 + (DWORD)nbms * 72);
    if (b) {
        for (i = 0; i < nbms; i++) {
            WORD n = hlp_strlen(bms[i].name) + 1;
            *(DWORD FAR *)(b + k) = bms[i].pos;
            *(DWORD FAR *)(b + k + 4) = bms[i].scroll;
            m_cpy(b + k + 8, bms[i].name, n);
            k += 8 + n;
        }
        *(WORD FAR *)b = g_hlp->minor;
        *(DWORD FAR *)(b + 2) = g_hlp->gendate;
        *(WORD FAR *)(b + 6) = nbms;
        *(WORD FAR *)(b + 8) = (WORD)k;
        if (nbms)
            hfs_put(f, name, b, k);
        else
            hfs_del(f, name);
        fs_write(f, path);
        hio_free(b);
    }
    fs_close(f);
}

void bookmark_menu(void)
{
    HMENU m = GetSubMenu(GetMenu(g_frame), 2);
    int i;
    if (!m)
        return;
    while (GetMenuItemCount(m) > 1)
        DeleteMenu(m, 1, MF_BYPOSITION);
    bm_load();
    for (i = 0; i < nbms && i < 9; i++) {
        char t[80];
        wsprintf(t, "&%d %s", i + 1, (LPSTR)bms[i].name);
        AppendMenu(m, MF_STRING, IDM_BOOKMARK0 + i, t);
    }
    if (nbms > 9)
        AppendMenu(m, MF_STRING, IDM_MORE, "&More...");
}

void bookmark_go(int i)
{
    int t;
    bm_load();
    if (i < 0 || i >= nbms || !g_hlp)
        return;
    t = topic_of_key(bms[i].pos);
    if (t >= 0)
        show_topic(t, TRUE);
}

BOOL CALLBACK __export DefineProc(HWND d, UINT msg, WPARAM wp, LPARAM lp)
{
    int i;
    switch (msg) {
    case WM_INITDIALOG:
        place(d, 0x6A, 7, 8, 154, 16);
        place(d, IDC_BMNAME, 7, 28, 263, 23);
        place(d, IDC_BMLIST, 6, 54, 265, 93);
        place(d, IDOK, 280, 28, 70, 23);
        place(d, IDCANCEL, 280, 57, 70, 23);
        place(d, IDC_BMDELETE, 280, 86, 70, 23);
        SetDlgItemText(d, IDC_BMNAME, g_title);
        SendDlgItemMessage(d, IDC_BMNAME, EM_LIMITTEXT, 63, 0);
        SendDlgItemMessage(d, IDC_BMNAME, EM_SETSEL, 0, MAKELONG(0, 0x7FFF));
        for (i = 0; i < nbms; i++)
            SendDlgItemMessage(d, IDC_BMLIST, LB_INSERTSTRING, (WPARAM)-1, (LPARAM)(LPSTR)bms[i].name);
        return TRUE;
    case WM_COMMAND:
        switch (wp) {
        case IDOK: {
            char t[64];
            GetDlgItemText(d, IDC_BMNAME, t, sizeof t);
            if (t[0] && nbms < MAXBM) {
                bms[nbms].pos = topic_key();
                bms[nbms].scroll = 0;
                lstrcpy(bms[nbms].name, t);
                nbms++;
            }
            EndDialog(d, 1);
            return TRUE;
        }
        case IDCANCEL:
            EndDialog(d, 0);
            return TRUE;
        case IDC_BMLIST:
            if (HIWORD(lp) == LBN_SELCHANGE)
                EnableWindow(GetDlgItem(d, IDC_BMDELETE), TRUE);
            return TRUE;
        case IDC_BMDELETE: {
            int k = (int)SendDlgItemMessage(d, IDC_BMLIST, LB_GETCURSEL, 0, 0);
            if (k >= 0 && k < nbms) {
                for (i = k + 1; i < nbms; i++)
                    bms[i - 1] = bms[i];
                nbms--;
                SendDlgItemMessage(d, IDC_BMLIST, LB_DELETESTRING, k, 0);
                EnableWindow(GetDlgItem(d, IDC_BMDELETE), FALSE);
                SetFocus(GetDlgItem(d, IDC_BMNAME));
                bm_save();
            }
            return TRUE;
        }
        }
        break;
    }
    return FALSE;
}

static int run_dialog(int id, FARPROC fn)
{
    FARPROC p = MakeProcInstance(fn, g_inst);
    int r = DialogBox(g_inst, MAKEINTRESOURCE(id), g_frame, (DLGPROC)p);
    FreeProcInstance(p);
    return r;
}

void bookmark_define(void)
{
    if (!g_hlp || g_cur < 0)
        return;
    bm_load();
    if (run_dialog(DLG_DEFINE, (FARPROC)DefineProc) == 1)
        bm_save();
    bookmark_menu();
}

BOOL CALLBACK __export MoreProc(HWND d, UINT msg, WPARAM wp, LPARAM lp)
{
    int i;
    switch (msg) {
    case WM_INITDIALOG:
        for (i = 0; i < nbms; i++)
            SendDlgItemMessage(d, IDC_BMLIST, LB_INSERTSTRING, (WPARAM)-1, (LPARAM)(LPSTR)bms[i].name);
        SendDlgItemMessage(d, IDC_BMLIST, LB_SETCURSEL, 0, 0);
        return TRUE;
    case WM_COMMAND:
        if (wp == IDOK || (wp == IDC_BMLIST && HIWORD(lp) == LBN_DBLCLK)) {
            EndDialog(d, (int)SendDlgItemMessage(d, IDC_BMLIST, LB_GETCURSEL, 0, 0));
            return TRUE;
        }
        if (wp == IDCANCEL) {
            EndDialog(d, -1);
            return TRUE;
        }
        break;
    }
    return FALSE;
}

void bookmark_more(void)
{
    int r;
    bm_load();
    r = run_dialog(DLG_MORE, (FARPROC)MoreProc);
    if (r >= 0)
        bookmark_go(r);
}

/* ---- annotations ---- */

static void ann_path(LPSTR out, LPCSTR helpfile)
{
    char b[16];
    base_upper(b, helpfile);
    lstrcat(b, ".ANN");
    windir_path(out, b);
}

static const BYTE version[6] = { 0x08, 0x62, 0x6D, 0x66, 0x01, 0x00 };

static HFS FAR *ann_open(BOOL create)
{
    char path[PATHMAX];
    HFS FAR *f;
    ann_path(path, g_file);
    f = fs_read(path);
    if (!f && create) {
        BYTE link0[2];
        link0[0] = link0[1] = 0;
        f = fs_new(128);
        if (f) {
            hfs_put(f, "@LINK", link0, 2);
            hfs_put(f, "@VERSION", version, 6);
            fs_write(f, path);
        }
    }
    return f;
}

BOOL annot_has(LPCSTR file, DWORD key)
{
    HFS FAR *f;
    char path[PATHMAX];
    DWORD n;
    LPB d;
    BOOL r = FALSE;
    (void)key;
    ann_path(path, file);
    f = fs_read(path);
    if (!f)
        return FALSE;
    key = topic_key();
    d = hfs_get(f, "@LINK", &n);
    if (d && n >= 2) {
        WORD c = hlp_rw(d), i;
        for (i = 0; i < c && 2 + (DWORD)i * 12 + 12 <= n; i++)
            if (hlp_rl(d + 2 + i * 12) == key)
                r = TRUE;
    }
    fs_close(f);
    return r;
}

static char anntext[4096];

static void ann_buttons(HWND d)
{
    BOOL text = GetWindowTextLength(GetDlgItem(d, IDC_ANNTEXT)) > 0;
    EnableWindow(GetDlgItem(d, IDOK), text);
    EnableWindow(GetDlgItem(d, IDC_ANNCOPY), text);
    EnableWindow(GetDlgItem(d, IDC_ANNPASTE), IsClipboardFormatAvailable(CF_TEXT));
}

static BOOL ann_exists;

BOOL CALLBACK __export AnnotateProc(HWND d, UINT msg, WPARAM wp, LPARAM lp)
{
    switch (msg) {
    case WM_INITDIALOG:
        dlg_place(d, "A_WindowPosition", 43, 47, 307, 237);
        place(d, 0x69, 7, 5, 112, 16);
        place(d, IDC_ANNTEXT, 7, 23, 215, 180);
        place(d, IDOK, 233, 23, 60, 23);
        place(d, IDCANCEL, 233, 52, 60, 23);
        place(d, IDC_ANNDELETE, 233, 81, 60, 23);
        place(d, IDC_ANNCOPY, 233, 124, 60, 23);
        place(d, IDC_ANNPASTE, 233, 153, 60, 23);
        SendDlgItemMessage(d, IDC_ANNTEXT, EM_LIMITTEXT, sizeof anntext - 1, 0);
        SetDlgItemText(d, IDC_ANNTEXT, anntext);
        ann_buttons(d);
        EnableWindow(GetDlgItem(d, IDOK), FALSE);
        EnableWindow(GetDlgItem(d, IDC_ANNDELETE), ann_exists);
        SetFocus(GetDlgItem(d, IDC_ANNTEXT));
        return FALSE;
    case WM_COMMAND:
        switch (wp) {
        case IDC_ANNTEXT:
            if (HIWORD(lp) == EN_CHANGE)
                ann_buttons(d);
            return TRUE;
        case IDOK:
            GetDlgItemText(d, IDC_ANNTEXT, anntext, sizeof anntext);
            dlg_save(d, "A_WindowPosition");
            EndDialog(d, 1);
            return TRUE;
        case IDCANCEL:
            /* Esc typed in the annotation does nothing (WS06: 3.1's
               multi-line edit keeps it); the Cancel button closes */
            if (!LOWORD(lp) && GetFocus() == GetDlgItem(d, IDC_ANNTEXT))
                return TRUE;
            dlg_save(d, "A_WindowPosition");
            EndDialog(d, 0);
            return TRUE;
        case IDC_ANNDELETE:
            dlg_save(d, "A_WindowPosition");
            EndDialog(d, 2);
            return TRUE;
        case IDC_ANNCOPY: {
            HWND e = GetDlgItem(d, IDC_ANNTEXT);
            DWORD sel = SendMessage(e, EM_GETSEL, 0, 0);
            if (LOWORD(sel) == HIWORD(sel))
                SendMessage(e, EM_SETSEL, 0, MAKELONG(0, 0x7FFF));
            SendMessage(e, WM_COPY, 0, 0);
            SendMessage(e, EM_SETSEL, 0, sel);
            ann_buttons(d);
            return TRUE;
        }
        case IDC_ANNPASTE:
            SendDlgItemMessage(d, IDC_ANNTEXT, WM_PASTE, 0, 0);
            return TRUE;
        }
        break;
    }
    return FALSE;
}

void annotate(void)
{
    HFS FAR *f;
    char name[16], path[PATHMAX];
    DWORD key, n;
    LPB d;
    int r;
    if (!g_hlp || g_cur < 0)
        return;
    key = topic_key();
    f = ann_open(TRUE);
    if (!f)
        return;
    wsprintf(name, "%lu!0", key);
    anntext[0] = 0;
    d = hfs_get(f, name, &n);
    ann_exists = d != NULL;
    if (d) {
        if (n >= sizeof anntext)
            n = sizeof anntext - 1;
        m_cpy(anntext, d, (WORD)n);
        anntext[n] = 0;
    }
    r = run_dialog(DLG_ANNOTATE, (FARPROC)AnnotateProc);
    if (r == 1 || r == 2) {
        /* the text first, then @LINK (WH4's order) */
        DWORD ln;
        LPB link = hfs_get(f, "@LINK", &ln);
        WORD c = link && ln >= 2 ? hlp_rw(link) : 0, i, k = 0;
        LPB nl = (LPB)hio_alloc(2 + ((DWORD)c + 1) * 12);
        if (nl) {
            BOOL added = FALSE;
            for (i = 0; i < c; i++) {
                DWORD p = hlp_rl(link + 2 + i * 12);
                if (p == key)
                    continue;
                if (r == 1 && !added && p > key) {
                    m_set(nl + 2 + k * 12, 0, 12);
                    *(DWORD FAR *)(nl + 2 + k * 12) = key;
                    k++;
                    added = TRUE;
                }
                m_cpy(nl + 2 + k * 12, link + 2 + i * 12, 12);
                k++;
            }
            if (r == 1 && !added) {
                m_set(nl + 2 + k * 12, 0, 12);
                *(DWORD FAR *)(nl + 2 + k * 12) = key;
                k++;
            }
            *(WORD FAR *)nl = k;
            if (r == 1)
                hfs_put(f, name, (LPCB)anntext, lstrlen(anntext));
            else
                hfs_del(f, name);
            hfs_put(f, "@LINK", nl, 2 + (DWORD)k * 12);
            hio_free(nl);
        }
        ann_path(path, g_file);
        fs_write(f, path);
        show_offset(g_curoff, FALSE, g_vtopic.scroll);
    }
    fs_close(f);
}

void annot_open_mark(void)
{
    annotate();
}

/* ---- Copy ---- */

static LPSTR copytext;

BOOL CALLBACK __export CopyProc(HWND d, UINT msg, WPARAM wp, LPARAM lp)
{
    (void)lp;
    switch (msg) {
    case WM_INITDIALOG: {
        /* 3.1's place and sizes (WH4: the window at 168, 67, 470 x 225
           with the main window at 213, 2; client controls in pixels) */
        dlg_place(d, "C_WindowPosition", -45, 65, 470, 225);
        MoveWindow(GetDlgItem(d, 0x64), 11, 7, 403, 13, FALSE);
        MoveWindow(GetDlgItem(d, IDC_COPYTEXT), 11, 29, 375, 163, FALSE);
        MoveWindow(GetDlgItem(d, IDOK), 392, 29, 60, 23, FALSE);
        MoveWindow(GetDlgItem(d, IDCANCEL), 392, 59, 60, 23, FALSE);
        if (copytext)
            SetDlgItemText(d, IDC_COPYTEXT, copytext);
        /* the caret at the start, nothing selected (WS06) */
        SetFocus(GetDlgItem(d, IDC_COPYTEXT));
        SendDlgItemMessage(d, IDC_COPYTEXT, EM_SETSEL, 0, MAKELONG(0, 0));
        return FALSE;
    }
    case WM_SIZE: {
        int cx = LOWORD(lp), cy = HIWORD(lp);
        if (wp == SIZE_MINIMIZED)
            break;
        MoveWindow(GetDlgItem(d, IDC_COPYTEXT), 11, 29, cx - 87, cy - 35, TRUE);
        MoveWindow(GetDlgItem(d, IDOK), cx - 70, 29, 60, 23, TRUE);
        MoveWindow(GetDlgItem(d, IDCANCEL), cx - 70, 59, 60, 23, TRUE);
        return TRUE;
    }
    case WM_COMMAND:
        if (wp == IDOK) {
            HWND e = GetDlgItem(d, IDC_COPYTEXT);
            DWORD sel = SendMessage(e, EM_GETSEL, 0, 0);
            if (LOWORD(sel) == HIWORD(sel))
                SendMessage(e, EM_SETSEL, 0, MAKELONG(0, 0x7FFF));
            SendMessage(e, WM_COPY, 0, 0);
            dlg_save(d, "C_WindowPosition");
            EndDialog(d, 1);
            return TRUE;
        }
        if (wp == IDCANCEL) {
            dlg_save(d, "C_WindowPosition");
            EndDialog(d, 0);
            return TRUE;
        }
        break;
    }
    return FALSE;
}

void copy_dialog(void)
{
    DWORD n;
    if (!g_hlp || g_cur < 0)
        return;
    copytext = view_text(&g_vtopic, &n);
    run_dialog(DLG_COPY, (FARPROC)CopyProc);
    if (copytext)
        hio_free(copytext);
    copytext = NULL;
}

/* CopyTopic: the whole topic to the clipboard */
void copy_topic(void)
{
    DWORD n;
    LPSTR s = view_text(&g_vtopic, &n);
    HGLOBAL g;
    if (!s)
        return;
    g = GlobalAlloc(GMEM_MOVEABLE | GMEM_DDESHARE, n + 1);
    if (g) {
        LPSTR d = (LPSTR)GlobalLock(g);
        m_cpy(d, s, (WORD)n + 1);
        GlobalUnlock(g);
        if (OpenClipboard(g_frame)) {
            EmptyClipboard();
            SetClipboardData(CF_TEXT, g);
            CloseClipboard();
        } else {
            GlobalFree(g);
        }
    }
    hio_free(s);
}

/* ---- printing ---- */

static HGLOBAL devmode, devnames;

void print_setup(void)
{
    PRINTDLG p;
    m_set(&p, 0, sizeof p);
    p.lStructSize = sizeof p;
    p.hwndOwner = g_frame;
    p.hDevMode = devmode;
    p.hDevNames = devnames;
    p.Flags = PD_PRINTSETUP;
    if (PrintDlg(&p)) {
        devmode = p.hDevMode;
        devnames = p.hDevNames;
    } else if (CommDlgExtendedError()) {
        errorbox(IDS_NOPRINTER, NULL);
    }
}

/* the printer: the one Print Setup chose, else WIN.INI's device= */
static HDC printer_dc(void)
{
    char dev[128];
    LPSTR drv, port, p;
    if (devnames) {
        DEVNAMES FAR *dn = (DEVNAMES FAR *)GlobalLock(devnames);
        HDC dc;
        LPSTR b = (LPSTR)dn;
        void FAR *dm = devmode ? GlobalLock(devmode) : NULL;
        dc = CreateDC(b + dn->wDriverOffset, b + dn->wDeviceOffset, b + dn->wOutputOffset, dm);
        if (devmode)
            GlobalUnlock(devmode);
        GlobalUnlock(devnames);
        return dc;
    }
    if (!GetProfileString("windows", "device", "", dev, sizeof dev) || !dev[0])
        return NULL;
    for (p = dev; *p && *p != ','; p++)
        ;
    if (!*p)
        return NULL;
    *p++ = 0;
    drv = p;
    for (; *p && *p != ','; p++)
        ;
    if (!*p)
        return NULL;
    *p++ = 0;
    port = p;
    return CreateDC(drv, dev, port, NULL);
}

void print_topic(void)
{
    HDC dc;
    DOCINFO di;
    VIEW v;
    int w, h, mx, my, top, i;
    char title[80];
    if (!g_hlp || g_cur < 0)
        return;
    dc = printer_dc();
    if (!dc) {
        errorbox(IDS_NOPRINTER, NULL);
        return;
    }
    w = GetDeviceCaps(dc, HORZRES);
    h = GetDeviceCaps(dc, VERTRES);
    mx = GetDeviceCaps(dc, LOGPIXELSX) / 2;     /* half-inch margins */
    my = GetDeviceCaps(dc, LOGPIXELSY) / 2;
    m_set(&v, 0, sizeof v);
    view_init(&v, g_topic, FALSE, FALSE);
    if (!v.items) {
        DeleteDC(dc);
        return;
    }
    view_layout_dc(&v, g_hlp, g_thdr.scroll, g_thdr.nexttopic, (short)(w - 2 * mx), dc);
    hlp_strcpyn(title, g_title, sizeof title);
    m_set(&di, 0, sizeof di);
    di.cbSize = sizeof di;
    di.lpszDocName = title;
    if (StartDoc(dc, &di) > 0) {
        /* pages: the display list cut where an item would cross the
           page's bottom */
        top = 0;
        while (top < v.height) {
            RECT clip;
            int bottom = top + h - 2 * my, next = v.height;
            for (i = 0; i < (int)v.nitems; i++) {
                DITEM FAR *d = &v.items[i];
                if (d->y < bottom && d->y + d->cy > bottom && d->y > top && d->y < next)
                    next = d->y;
            }
            if (next > bottom)
                next = bottom;
            StartPage(dc);
            SaveDC(dc);
            IntersectClipRect(dc, mx, my, w - mx, my + (next - top));
            SetRect(&clip, 0, top, w, next);
            view_paint(&v, dc, &clip, (short)mx, (short)(my - top));
            RestoreDC(dc, -1);
            EndPage(dc);
            top = next;
        }
        EndDoc(dc);
    }
    view_clear(&v);
    fonts_free(&v);
    view_free(&v);
    DeleteDC(dc);
}
