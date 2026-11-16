/*
 * main.c - w16 Write: the frame window, files, menus.
 *
 * Observed on Windows 3.1 (WR1 window dump): the frame is class
 * "MSWRITE_MENU", WS_OVERLAPPEDWINDOW | WS_CLIPSIBLINGS, extended style
 * WS_EX_ACCEPTFILES, title "Write - (Untitled)"; its children are two
 * scroll bars, "MSWRITE_PAGEINFO" (the black "Page n" box at the bottom
 * left, 120 pixels wide) and "MSWRITE_DOC" (the text, ex-style accept
 * files), all WS_CHILD | WS_VISIBLE | WS_CLIPSIBLINGS. The scroll bars
 * overlap the frame's border by one pixel; the horizontal one starts
 * right of the page box. Save As writes [MSWrite] Backup=0|1 to WIN.INI.
 */
#include "write.h"
#include <commdlg.h>
#include <shellapi.h>
/* SHELL.22; not in OpenWatcom's shellapi.h */
int FAR PASCAL ShellAbout(HWND, LPCSTR, LPCSTR, HICON);

HINSTANCE g_hinst;
HWND g_frame, g_pageinfo, g_hscroll, g_vscroll;
char g_path[PATH_MAX_DOS];
BOOL g_backup;
static HACCEL accel;
static int saveFormat = FMT_WRI;
static const char s_frame[] = "MSWRITE_MENU";
static const char s_ini[] = "MSWrite";

/* ---- title ---- */

void set_title(void)
{
    char t[PATH_MAX_DOS + 16], name[PATH_MAX_DOS];
    const char *p, *base;
    load_str(IDS_WRITE, t, sizeof t);
    s_cat(t, " - ", sizeof t);
    if (g_path[0]) {
        for (p = base = g_path; *p; p++)
            if (*p == '\\' || *p == ':')
                base = p + 1;
        s_cpy(name, base, sizeof name);
    } else {
        load_str(IDS_UNTITLED, name, sizeof name);
    }
    s_cat(t, name, sizeof t);
    SetWindowText(g_frame, t);
}

/* ---- layout of the children ---- */

static void place_children(int cx, int cy)
{
    int sb = GetSystemMetrics(SM_CXVSCROLL), sh = GetSystemMetrics(SM_CYHSCROLL);
    /* observed at 624 x 362: doc 0,0 608x346; page box 0,346 120x17;
       horizontal bar 120,346 489x17; vertical bar 608,-1 17x348 */
    MoveWindow(g_docwnd, 0, 0, cx - sb + 1, cy - sh + 1, TRUE);
    MoveWindow(g_pageinfo, 0, cy - sh + 1, 120, sh, TRUE);
    MoveWindow(g_hscroll, 120, cy - sh + 1, cx - 120 - sb + 2, sh, TRUE);
    MoveWindow(g_vscroll, cx - sb + 1, -1, sb, cy - sh + 3, TRUE);
}

/* ---- files ---- */

static BOOL ask_save(void)
{
    char name[PATH_MAX_DOS];
    int r;
    if (!g_doc.dirty)
        return TRUE;
    if (g_path[0])
        s_cpy(name, g_path, sizeof name);
    else
        load_str(IDS_UNTITLED, name, sizeof name);
    r = msgbox(g_frame, IDS_SAVECHANGES, name, MB_YESNOCANCEL | MB_ICONEXCLAMATION);
    if (r == IDCANCEL)
        return FALSE;
    if (r == IDYES)
        return SendMessage(g_frame, WM_COMMAND, IDM_SAVE, 0) != 0;
    return TRUE;
}

static void new_doc(void)
{
    doc_new(&g_doc);
    saveFormat = FMT_WRI;
    g_path[0] = 0;
    set_title();
    ole_init();
    undo_clear();
    view_reset();
}

/* the convert prompt for files that are not Write's: Convert (IDOK),
   No Conversion (7), Cancel */
BOOL CALLBACK __export ConvertDlgProc(HWND d, UINT msg, WPARAM wp, LPARAM lp)
{
    if (msg == WM_INITDIALOG) {
        char fmt[160], name[160];
        load_str(IDS_CONVERTQ, fmt, sizeof fmt);
        /* "%s\nDo you want to convert..." with the file kind first */
        load_str((WORD)lp, name, sizeof name);
        {
            char out[320], *s, *o = out;
            for (s = fmt; *s && o < out + sizeof out - 1; s++) {
                if (s[0] == '%' && s[1] == 's') {
                    const char *a = name;
                    while (*a && o < out + sizeof out - 1)
                        *o++ = *a++;
                    s++;
                } else {
                    *o++ = *s;
                }
            }
            *o = 0;
            SetDlgItemText(d, 99, out);
        }
        return TRUE;
    }
    if (msg == WM_COMMAND && (wp == IDOK || wp == IDCANCEL || wp == 7)) {
        EndDialog(d, wp);
        return TRUE;
    }
    return FALSE;
}

static BOOL open_path(LPCSTR path)
{
    BOOL isText;
    int r = doc_load(&g_doc, path, &isText);
    saveFormat = FMT_WRI;
    if (r == LOAD_NOTWRI) {
        FARPROC fp = MakeProcInstance((FARPROC)ConvertDlgProc, g_hinst);
        int a = DialogBoxParam(g_hinst, MAKEINTRESOURCE(IDD_CONVERT), g_frame, (DLGPROC)fp,
                               IDS_TEXTDOC);
        FreeProcInstance(fp);
        if (a == IDCANCEL)
            return FALSE;
        r = doc_load_text(&g_doc, path);
        saveFormat = FMT_TEXT;
        /* UNVERIFIED: Convert treats the text as OEM (ANSI after) */
    }
    if (r == LOAD_NOFILE) {
        msgbox(g_frame, IDS_NOTFOUNDFILE, path, MB_OK | MB_ICONEXCLAMATION);
        doc_init(&g_doc);
        return FALSE;
    }
    if (r != LOAD_OK) {
        msgbox(g_frame, r == LOAD_CORRUPT ? IDS_CORRUPT : IDS_NOMEMORY, path,
               MB_OK | MB_ICONEXCLAMATION);
        doc_init(&g_doc);
        return FALSE;
    }
    {
        OFSTRUCT of;
        if (OpenFile(path, &of, OF_EXIST | OF_READ) != HFILE_ERROR)
            AnsiLower(of.szPathName), s_cpy(g_path, of.szPathName, sizeof g_path);
        else
            s_cpy(g_path, path, sizeof g_path);
        AnsiUpper(g_path);
    }
    set_title();
    ole_init();
    undo_clear();
    view_reset();
    return TRUE;
}

static char filter[256];

static void make_filter(BOOL save)
{
    static const WORD open_ids[] = { IDS_FLTWRI, IDS_EXTWRI, IDS_FLTDOC, IDS_EXTDOC,
                                     IDS_FLTTXT, IDS_EXTTXT, IDS_FLTALL, 0, 0 };
    static const WORD save_ids[] = { IDS_FLTWRI, IDS_EXTWRI, IDS_FLTDOC, IDS_EXTDOC,
                                     IDS_FLTDOCTXT, IDS_EXTDOC, IDS_FLTTXT, IDS_EXTTXT,
                                     IDS_FLTALL, 0, 0 };
    const WORD *ids = save ? save_ids : open_ids;
    char *p = filter, s[64];
    int i;
    for (i = 0; ids[i] || ids[i + 1] || i % 2; i++) {
        if (i % 2 == 0) {
            load_str(ids[i], s, sizeof s);
        } else if (ids[i]) {
            s_cpy(s, "*.", sizeof s);
            load_str(ids[i], s + 2, sizeof s - 2);
        } else {
            s_cpy(s, "*.*", sizeof s);
        }
        s_cpy(p, s, (int)(filter + sizeof filter - 1 - p));
        p += s_len(p) + 1;
        if (i % 2 == 1 && !ids[i + 1])
            break;
    }
    *p = 0;
}

/* the Save As hook: the Read Only box becomes "Backup" (observed: control
   410h, "&Backup", an auto check box) */
BOOL CALLBACK __export SaveHook(HWND d, UINT msg, WPARAM wp, LPARAM lp)
{

    if (msg == WM_INITDIALOG) {
        char s[32];
        load_str(IDS_BACKUP, s, sizeof s);
        SetDlgItemText(d, 0x410, s);
        CheckDlgButton(d, 0x410, g_backup);
        /* an untitled document: the name field stays empty (observed,
           WR1; COMMDLG itself would show the filter, "*.wri") */
        if (!((OPENFILENAME FAR *)lp)->lpstrFile[0])
            SetDlgItemText(d, 0x480, "");
        return TRUE;
    }
    if (msg == WM_COMMAND && wp == IDOK)
        g_backup = IsDlgButtonChecked(d, 0x410);
    return FALSE;
}

static BOOL file_dialog(BOOL save, char *path)
{
    OPENFILENAME ofn;
    char title[32], ext[4];
    FARPROC hook = NULL;
    BOOL ok;
    make_filter(save);
    load_str(save ? IDS_SAVEASTITLE : IDS_OPENTITLE, title, sizeof title);
    load_str(IDS_EXTWRI, ext, sizeof ext);
    m_set(&ofn, 0, sizeof ofn);
    ofn.lStructSize = sizeof ofn;
    ofn.hwndOwner = g_frame;
    ofn.hInstance = g_hinst;
    ofn.lpstrFilter = filter;
    /* the format the document came in: a converted text file is saved
       as text unless another type is chosen (WR8) */
    ofn.nFilterIndex = saveFormat == FMT_TEXT ? 4 : saveFormat == FMT_WORD ? 2 : 1;
    ofn.lpstrFile = path;
    ofn.nMaxFile = PATH_MAX_DOS;
    ofn.lpstrTitle = title;
    ofn.lpstrDefExt = ext;
    if (save) {
        hook = MakeProcInstance((FARPROC)SaveHook, g_hinst);
        ofn.Flags = OFN_OVERWRITEPROMPT | OFN_ENABLEHOOK | OFN_PATHMUSTEXIST;
        ofn.lpfnHook = (UINT (CALLBACK *)(HWND, UINT, WPARAM, LPARAM))hook;
        ok = GetSaveFileName(&ofn);
        FreeProcInstance(hook);
        if (ok) {
            saveFormat = ofn.nFilterIndex == 4 ? FMT_TEXT :
                         ofn.nFilterIndex == 2 || ofn.nFilterIndex == 3 ? FMT_WORD : FMT_WRI;
            WriteProfileString(s_ini, "Backup", g_backup ? "1" : "0");
        }
    } else {
        ofn.Flags = OFN_HIDEREADONLY | OFN_FILEMUSTEXIST;
        ok = GetOpenFileName(&ofn);
    }
    return ok;
}

static BOOL save_to(LPCSTR path)
{
    int fmt = saveFormat;
    if (fmt == FMT_WORD)
        fmt = FMT_WRI;                  /* Word for DOS: not yet (plan) */
    if (g_backup) {
        /* the old file becomes name.BKP (UNVERIFIED: the original's
           backup naming for .WRI) */
        char bkp[PATH_MAX_DOS];
        int i, dot = -1;
        OFSTRUCT of;
        s_cpy(bkp, path, sizeof bkp);
        for (i = 0; bkp[i]; i++)
            if (bkp[i] == '.')
                dot = i;
            else if (bkp[i] == '\\')
                dot = -1;
        if (dot >= 0)
            bkp[dot] = 0;
        s_cat(bkp, ".BKP", sizeof bkp);
        OpenFile(bkp, &of, OF_DELETE);
        {
            /* DOS rename (AH=56h) through the file system: KERNEL has no
               rename call; DOS3Call is the documented way */
            HFILE f = OpenFile(path, &of, OF_EXIST);
            if (f != HFILE_ERROR) {
                char from[PATH_MAX_DOS];
                s_cpy(from, of.szPathName, sizeof from);
                OpenFile(bkp, &of, OF_PARSE);
                {
                    extern void dos_rename(LPCSTR from, LPCSTR to);
                    dos_rename(from, of.szPathName);
                }
            }
        }
    }
    if (!doc_save(&g_doc, path, fmt)) {
        msgbox(g_frame, IDS_CANTSAVE, path, MB_OK | MB_ICONEXCLAMATION);
        return FALSE;
    }
    return TRUE;
}

/* ---- commands ---- */

static void about(void)
{
    char t[32];
    load_str(IDS_WRITE, t, sizeof t);
    ShellAbout(g_frame, t, "", LoadIcon(g_hinst, MAKEINTRESOURCE(IDI_WRITE)));
}

static void help(UINT cmd, DWORD data)
{
    char f[16];
    load_str(IDS_HELPFILE, f, sizeof f);
    WinHelp(g_frame, f, cmd, data);
}

static LRESULT command(HWND w, WORD id)
{
    char path[PATH_MAX_DOS];
    switch (id) {
    case IDM_NEW:
        if (ask_save())
            new_doc();
        break;
    case IDM_OPEN:
        if (!ask_save())
            break;
        path[0] = 0;
        if (file_dialog(FALSE, path) && !open_path(path))
            new_doc();
        break;
    case IDM_SAVE:
        if (!g_path[0])
            return command(w, IDM_SAVEAS);
        if (!save_to(g_path))
            return FALSE;
        ole_saved();
        return TRUE;
    case IDM_SAVEAS:
        s_cpy(path, g_path, sizeof path);
        if (!file_dialog(TRUE, path))
            return FALSE;
        if (!save_to(path))
            return FALSE;
        s_cpy(g_path, path, sizeof g_path);
        AnsiUpper(g_path);
        set_title();
        ole_saved();
        return TRUE;
    case IDM_EXIT:
        PostMessage(w, WM_CLOSE, 0, 0);
        break;
    case IDM_REGULAR: case IDM_BOLD: case IDM_ITALIC: case IDM_UNDERLINE:
    case IDM_SUPER: case IDM_SUB: case IDM_REDUCE: case IDM_ENLARGE:
        cmd_char(id);
        break;
    case IDM_FONTS:
        cmd_fonts();
        break;
    case IDM_NORMAL: case IDM_LEFT: case IDM_CENTER: case IDM_RIGHT: case IDM_JUSTIFY:
    case IDM_SINGLE: case IDM_ONEHALF: case IDM_DOUBLE:
        cmd_para(id);
        break;
    case IDM_INDENTS:
        cmd_indents();
        break;
    case IDM_TABS:
        cmd_tabs();
        break;
    case IDM_PAGELAYOUT:
        cmd_pagelayout();
        break;
    case IDM_HEADER:
    case IDM_FOOTER:
        cmd_header(id == IDM_FOOTER);
        break;
    case IDM_GOTO:
        cmd_goto();
        break;
    case IDM_FIND:
    case IDM_REPLACE:
        cmd_find(id == IDM_REPLACE);
        break;
    case IDM_FINDAGAIN:
        cmd_findagain();
        break;
    case IDM_CUT:
    case IDM_COPY:
        cmd_cut_copy(id == IDM_CUT);
        break;
    case IDM_PASTE:
        cmd_paste();
        break;
    case IDM_HELPF1:                    /* F1 (the original's 62192) */
    case IDM_HELPCONTENTS:
        help(HELP_INDEX, 0);
        break;
    case IDM_HELPSEARCH:
        help(HELP_PARTIALKEY, (DWORD)(LPSTR)"");
        break;
    case IDM_HELPHELP:
        help(HELP_HELPONHELP, 0);
        break;
    case IDM_UNDO:
        cmd_undo();
        break;
    case IDM_INSERTOBJ:
        undo_save(IDS_EDITING);
        cmd_insertobj();
        break;
    case IDM_ABOUT:
        about();
        break;
    }
    return 0;
}

LRESULT FAR PASCAL __export FrameWndProc(HWND w, UINT msg, WPARAM wp, LPARAM lp)
{
    switch (msg) {
    case WM_CREATE:
        g_frame = w;
        /* created in this order: the window dump lists the children
           horizontal bar, vertical bar, page box, text (WR1) */
        g_hscroll = CreateWindow("ScrollBar", NULL, WS_CHILD | WS_VISIBLE | WS_CLIPSIBLINGS | SBS_HORZ,
                                 0, 0, 0, 0, w, NULL, g_hinst, NULL);
        g_vscroll = CreateWindow("ScrollBar", NULL, WS_CHILD | WS_VISIBLE | WS_CLIPSIBLINGS | SBS_VERT,
                                 0, 0, 0, 0, w, NULL, g_hinst, NULL);
        g_pageinfo = CreateWindow("MSWRITE_PAGEINFO", NULL, WS_CHILD | WS_VISIBLE | WS_CLIPSIBLINGS,
                                  0, 0, 0, 0, w, NULL, g_hinst, NULL);
        CreateWindow("MSWRITE_DOC", NULL, WS_CHILD | WS_VISIBLE | WS_CLIPSIBLINGS,
                     0, 0, 0, 0, w, NULL, g_hinst, NULL);
        SetScrollRange(g_vscroll, SB_CTL, 0, 100, FALSE);
        SetScrollRange(g_hscroll, SB_CTL, 0, 100, FALSE);
        DragAcceptFiles(w, TRUE);       /* both have WS_EX_ACCEPTFILES (WR1) */
        DragAcceptFiles(g_docwnd, TRUE);
        return 0;
    case WM_SIZE:
        place_children(LOWORD(lp), HIWORD(lp));
        return 0;
    case WM_SETFOCUS:
        SetFocus(g_docwnd);
        return 0;
    case WM_VSCROLL:
        SendMessage(g_docwnd, WM_VSCROLL, wp, lp);
        return 0;
    case WM_HSCROLL:
        SendMessage(g_docwnd, WM_HSCROLL, wp, lp);
        return 0;
    case WM_INITMENUPOPUP:
        if (!HIWORD(lp))
            menu_update((HMENU)wp);
        return 0;
    case WM_COMMAND:
        return command(w, wp);
    case WM_DROPFILES: {
        char path[PATH_MAX_DOS];
        DragQueryFile((HDROP)wp, 0, path, sizeof path);
        DragFinish((HDROP)wp);
        if (ask_save() && !open_path(path))
            new_doc();
        return 0;
    }
    case WM_CLOSE:
        if (ask_save())
            DestroyWindow(w);
        return 0;
    case WM_QUERYENDSESSION:
        return ask_save();
    case WM_USER + 1:                   /* ole.c: an object changed */
        ole_notify(wp, lp);
        return 0;
    case WM_DESTROY:
        ole_done();
        help(HELP_QUIT, 0);             /* with WRITE.HLP named (WR11) */
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProc(w, msg, wp, lp);
}

int PASCAL WinMain(HINSTANCE hi, HINSTANCE prev, LPSTR cmd, int show)
{
    MSG m;
    char path[PATH_MAX_DOS];
    int i;
    g_hinst = hi;
    if (!prev) {
        WNDCLASS wc;
        m_set(&wc, 0, sizeof wc);
        /* CS_BYTEALIGNCLIENT, as 3.1's own frames (Program Manager's:
           GCW_STYLE 1000h): the client area and the menu bar on 8-pixel
           boundaries; observed here as the first window at (4, 0) */
        wc.style = CS_BYTEALIGNCLIENT;
        wc.lpfnWndProc = FrameWndProc;
        wc.hInstance = hi;
        wc.hIcon = LoadIcon(hi, MAKEINTRESOURCE(IDI_WRITE));
        wc.hCursor = LoadCursor(NULL, IDC_ARROW);
        wc.hbrBackground = (HBRUSH)(COLOR_SCROLLBAR + 1);   /* the corner between the bars (WR6) */
        wc.lpszMenuName = "MW_MENU";
        wc.lpszClassName = s_frame;
        if (!RegisterClass(&wc) || !view_register(hi))
            return 0;
    }
    g_backup = GetProfileInt(s_ini, "Backup", 0) != 0;
    accel = LoadAccelerators(hi, "MW_ACCTB");
    doc_new(&g_doc);
    CreateWindow(s_frame, "", WS_OVERLAPPEDWINDOW | WS_CLIPSIBLINGS,
                 CW_USEDEFAULT, 0, CW_USEDEFAULT, 0, NULL, NULL, hi, NULL);
    if (!g_frame)
        return 0;
    for (i = 0; cmd[i] == ' '; i++)
        ;
    s_cpy(path, cmd + i, sizeof path);
    for (i = s_len(path); i > 0 && path[i - 1] == ' '; i--)
        path[i - 1] = 0;
    if (path[0]) {
        if (!open_path(path))
            new_doc();
    } else {
        new_doc();
    }
    ShowWindow(g_frame, show);
    UpdateWindow(g_frame);
    while (GetMessage(&m, NULL, 0, 0)) {
        /* Ctrl+B, I, U, Z, X, C, V are character accelerators, but a
           character without Ctrl down is typed: Tab is 09h like Ctrl+I
           (observed: a Tab posted as WM_CHAR is a tab, WR4). The
           accelerators come before the Find dialog's keys: F1 there asks
           for Help (WR11; UNVERIFIED for the other keys) */
        if (!(m.message == WM_CHAR && m.wParam < ' ' && GetKeyState(VK_CONTROL) >= 0) &&
            TranslateAccelerator(g_frame, accel, &m))
            continue;
        if (find_dialog() && IsDialogMessage(find_dialog(), &m))
            continue;
        TranslateMessage(&m);
        DispatchMessage(&m);
    }
    doc_free(&g_doc);
    return m.wParam;
}
