/*
 * main.c - w16 Registration Info Editor (REGEDIT.EXE), a clean-room clone
 * of Windows 3.1's (ARCHITECTURE.md 15.8). Every fact comes from black-box
 * observation of 3.1's REGEDIT on the reference install (SNAP probes
 * RE1-RE11, tests/scripts/regedit/obs/, run with tests/regobs.sh) and from
 * the SDK; the original's code and resources were never looked at.
 *
 * The frame (class "Registration Editor", title "Registration Info
 * Editor", WS_OVERLAPPEDWINDOW, accepting dropped files)
 * holds one child dialog, the view, at its client's origin and of its
 * client's size:
 *   - the basic view (no switch): the registered file types (basic.c);
 *   - the advanced view (/V or -V): the database as a tree (advanced.c).
 * The frame starts sized for the view's template, or for WIN.INI [RegEdit]
 * width= and height= (the view's size, written there at exit).
 *
 * The command line: /V (any case, also -V) the advanced view; /S silent;
 * any other word a registration file to merge: then no window is made, the
 * file is merged (regfile.c) and, unless /S, a message box says so; the
 * program ends.
 *
 * New Style (w16's own, not in 3.1; newstyle.c): SYSTEM.INI [w16]
 * RegeditNewStyle=1 starts in a Windows 95 Registry Editor-like view; =2
 * starts in 3.1's views; either adds View > New Style to every view to
 * switch. 0 (the default) is stock 3.1: no menu item, nothing different.
 */
#include "regedit.h"
#include <shellapi.h>

HINSTANCE g_inst;
HWND g_frame, g_view;
int g_mode;
int g_nsSetting;
BOOL g_dirty;
char g_sel[PATHMAX];
static int start_mode;          /* the classic view this run started in */
static HACCEL accel_ns, accel_adv;
static char cls[64], title[64];

int rstr(UINT id, LPSTR buf, int cb)
{
    return LoadString(g_inst, id, buf, cb);
}

/* a message box with the program's title; idfmt's %s gets arg */
int msgbox(HWND owner, UINT idfmt, LPCSTR arg, UINT flags)
{
    static char fmt[512], text[512 + PATHMAX];
    char cap[64];
    rstr(idfmt, fmt, sizeof fmt);
    if (arg)
        wsprintf(text, fmt, arg);
    else
        lstrcpy(text, fmt);
    rstr(g_mode == MODE_NEWSTYLE ? IDS_NS_TITLE : IDS_TITLE, cap, sizeof cap);
    return MessageBox(owner, text, cap, flags);
}

/* WinHelp as 3.1's REGEDIT calls it (tests/rehelp.sh, REH1-REH8): the
   basic view's regedit.hlp, the advanced view's regeditv.hlp; every
   request but HELP_QUIT is followed by HELP_SETCONTENTS with the view's
   contents topic (2801h, 2881h) */
void help(UINT cmd, DWORD data)
{
    char file[32];
    BOOL v = g_mode == MODE_ADVANCED;
    rstr(v ? IDS_HELPFILEV : IDS_HELPFILE, file, sizeof file);
    WinHelp(g_frame, file, cmd, data);
    if (cmd != HELP_QUIT)
        WinHelp(g_frame, file, HELP_SETCONTENTS, v ? HELPID_CONTENTSV : HELPID_CONTENTS);
}

void help_context(DWORD id)
{
    help(HELP_CONTEXT, id);
}

/* F1: in a menu the item's topic, in a dialog its Help button, in the
   main window the contents (REH2, REH4, REH5), through a message filter */
static HHOOK filter;
static UINT menu_item;
static FARPROC filter_inst;
UINT g_filedlg_help;            /* the topic of the COMMDLG box up */
UINT g_help_msg;                /* COMMDLG's "commdlg_help" */

LRESULT FAR PASCAL _export MessageFilter(int code, WPARAM wp, LPARAM lp)
{
    MSG FAR *m = (MSG FAR *)lp;
    if (code >= 0 && m->message == WM_KEYDOWN && m->wParam == VK_F1) {
        if (code == MSGF_MENU) {
            if (menu_item)
                help_context((g_mode == MODE_ADVANCED ? HELPID_MENUV : HELPID_MENU) + menu_item);
            return 1;
        }
        if (code == MSGF_DIALOGBOX) {
            HWND top = GetActiveWindow();   /* the box up, whichever window the key came to (REH8) */
            if (top == g_frame)
                help_context(g_mode == MODE_ADVANCED ? HELPID_CONTENTSV : HELPID_CONTENTS);
            else if (GetDlgItem(top, IDC_HELP))
                PostMessage(top, WM_COMMAND, IDC_HELP, MAKELONG(GetDlgItem(top, IDC_HELP), BN_CLICKED));
            else if (g_filedlg_help)
                help_context(g_filedlg_help);
            return 1;
        }
    }
    return CallNextHookEx(filter, code, wp, lp);
}

void about(void)
{
    char s[64], o[128];
    rstr(g_mode == MODE_NEWSTYLE ? IDS_NS_ABOUT : IDS_ABOUT, s, sizeof s);
    rstr(IDS_ABOUTOTHER, o, sizeof o);
    ShellAbout(g_frame, s, o, LoadIcon(g_inst, MAKEINTRESOURCE(IDI_REGEDIT)));
}

/* COMMDLG's Open or Save As box for registration files or programs */
BOOL file_dialog(HWND owner, BOOL save, UINT idtitle, UINT idfilter, LPCSTR defext,
                 LPSTR file, int cb)
{
    static char filter[128], ttl[64];
    OPENFILENAME o;
    int i, n;
    n = rstr(idfilter, filter, sizeof filter);
    for (i = 0; i < n; i++)
        if (filter[i] == '|')
            filter[i] = 0;
    rstr(idtitle, ttl, sizeof ttl);
    _fmemset(&o, 0, sizeof o);
    o.lStructSize = sizeof o;
    o.hwndOwner = owner;
    o.lpstrFilter = filter;
    o.nFilterIndex = 1;
    o.lpstrFile = file;
    o.nMaxFile = cb;
    o.lpstrTitle = ttl;
    o.lpstrDefExt = defext;
    o.Flags = OFN_HIDEREADONLY | OFN_SHOWHELP |
              (save ? OFN_OVERWRITEPROMPT : OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST);
    return save ? GetSaveFileName(&o) : GetOpenFileName(&o);
}

/* View > New Style, added to a classic view's menu when [w16]
   RegeditNewStyle is 1 or 2 */
void add_view_menu(HMENU m)
{
    char s[32];
    HMENU pop;
    int n;
    if (!g_nsSetting)
        return;
    pop = CreatePopupMenu();
    rstr(IDS_NS_MENUITEM, s, sizeof s);
    AppendMenu(pop, MF_STRING, IDM_NEWSTYLE, s);
    n = GetMenuItemCount(m);
    InsertMenu(m, n - 1, MF_BYPOSITION | MF_POPUP, (UINT)pop, "&View");
}

static void size_frame(int cx, int cy);
static int keep_cx = -1, keep_cy = -1;  /* the template's size, kept unsnapped (RE2) */
static BOOL in_size;            /* size_frame at work: no snapping yet */

/* the view fills the frame's client area. Its list's height is whole
   lines; a frame taller than that shrinks to fit (RE10: a frame moved to
   260 pixels' height became 254), except for the first sizing from the
   template (RE2: 208 pixels for a 162-pixel list) */
void layout_view(void)
{
    RECT r, l;
    HWND lb;
    if (!g_view)
        return;
    GetClientRect(g_frame, &r);
    if (g_mode == MODE_NEWSTYLE) {
        ns_size(r.right, r.bottom);
        return;
    }
    MoveWindow(g_view, 0, 0, r.right, r.bottom, TRUE);
    lb = GetDlgItem(g_view, g_mode == MODE_BASIC ? IDC_TYPES : IDC_TREE);
    if (in_size || (r.right == keep_cx && r.bottom == keep_cy) || !lb || IsIconic(g_frame) || IsZoomed(g_frame))
        return;
    GetWindowRect(lb, &l);
    ScreenToClient(g_view, (POINT FAR *)&l.right);
    if (l.bottom < r.bottom + 1)
        size_frame(r.right, r.bottom - (r.bottom + 1 - l.bottom));
}

static void save_size(void)
{
    RECT r;
    char s[12];
    if (g_mode == MODE_NEWSTYLE || !g_view || IsIconic(g_frame))
        return;
    GetClientRect(g_view, &r);
    wsprintf(s, "%d", r.right);
    WriteProfileString("RegEdit", "width", s);
    wsprintf(s, "%d", r.bottom);
    WriteProfileString("RegEdit", "height", s);
}

/* the frame sized so that its client area is cx x cy */
static void size_frame(int cx, int cy)
{
    RECT r;
    r.left = r.top = 0;
    r.right = cx;
    r.bottom = cy;
    AdjustWindowRect(&r, GetWindowLong(g_frame, GWL_STYLE), TRUE);
    in_size = TRUE;
    SetWindowPos(g_frame, NULL, 0, 0, r.right - r.left, r.bottom - r.top,
                 SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE);
    /* AdjustWindowRect counts the menu bar as SM_CYMENU; the bar drawn
       may differ by a pixel: correct by what the client area came out */
    {
        RECT c, w;
        GetClientRect(g_frame, &c);
        if (c.right != cx || c.bottom != cy) {
            GetWindowRect(g_frame, &w);
            SetWindowPos(g_frame, NULL, 0, 0, w.right - w.left + cx - c.right,
                         w.bottom - w.top + cy - c.bottom,
                         SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE);
        }
    }
    in_size = FALSE;
    layout_view();
}

static FARPROC basic_proc, adv_proc;

/* switch to a view: the old one's child windows and menu go, the new
   one's come; the selected key (g_sel) is kept */
void set_mode(int mode)
{
    HMENU old = GetMenu(g_frame), m;
    char t[64];
    if (g_view) {
        if (g_mode == MODE_NEWSTYLE)
            ns_save();
        DestroyWindow(g_view);
        g_view = NULL;
    }
    g_mode = mode;
    if (mode == MODE_NEWSTYLE) {
        m = LoadMenu(g_inst, MAKEINTRESOURCE(MENU_NEWSTYLE));
        SetMenu(g_frame, m);
        rstr(IDS_NS_TITLE, t, sizeof t);
        SetWindowText(g_frame, t);
        g_view = ns_create(g_frame);
    } else {
        m = LoadMenu(g_inst, MAKEINTRESOURCE(mode == MODE_BASIC ? MENU_BASIC : MENU_ADVANCED));
        add_view_menu(m);
        SetMenu(g_frame, m);
        SetWindowText(g_frame, title);
        g_view = CreateDialog(g_inst, MAKEINTRESOURCE(mode == MODE_BASIC ? DLG_BASIC : DLG_ADVANCED),
                              g_frame, (DLGPROC)(mode == MODE_BASIC ? basic_proc : adv_proc));
    }
    if (old)
        DestroyMenu(old);
    DrawMenuBar(g_frame);
}

static BOOL query_exit(void)
{
    if (g_dirty) {
        int r = msgbox(g_frame, IDS_SAVEASK, NULL, MB_YESNOCANCEL);   /* no icon (RE6) */
        if (r == IDCANCEL)
            return FALSE;
        if (r == IDYES)
            adv_save();
    }
    return TRUE;
}


LRESULT FAR PASCAL _export FrameProc(HWND w, UINT msg, WPARAM wp, LPARAM lp)
{
    if (g_help_msg && msg == g_help_msg) {
        help_context(g_filedlg_help);   /* a COMMDLG box's Help (REH3) */
        return 0;
    }
    switch (msg) {
    case WM_SIZE:
        if (wp != SIZE_MINIMIZED)
            layout_view();
        return 0;
    case WM_MENUSELECT:
        /* the item F1 asks about (not a popup, not a separator) */
        menu_item = (LOWORD(lp) & (MF_POPUP | MF_SEPARATOR)) || LOWORD(lp) == 0xFFFF ? 0 : wp;
        break;
    case WM_SETFOCUS:
        if (g_view && g_mode != MODE_NEWSTYLE)
            SetFocus(g_view);
        else if (g_view)
            ns_command(0);
        return 0;
    case WM_COMMAND:
        switch (wp) {
        case IDM_EXIT:
            PostMessage(w, WM_CLOSE, 0, 0);
            return 0;
        case IDM_HELPCONTENTS:
            help_context(g_mode == MODE_ADVANCED ? HELPID_CONTENTSV : HELPID_CONTENTS);
            return 0;
        case IDM_HELPSEARCH:
            help(HELP_PARTIALKEY, (DWORD)(LPSTR)"");
            return 0;
        case IDM_HELPHELP:
            help(HELP_HELPONHELP, 0);
            return 0;
        case IDM_ABOUT:
            about();
            return 0;
        case IDM_NEWSTYLE:
            if (g_mode == MODE_NEWSTYLE) {
                set_mode(start_mode);
                WritePrivateProfileString("w16", "RegeditNewStyle", "2", "SYSTEM.INI");
            } else {
                if (!query_exit())
                    return 0;
                set_mode(MODE_NEWSTYLE);
                WritePrivateProfileString("w16", "RegeditNewStyle", "1", "SYSTEM.INI");
            }
            layout_view();
            return 0;
        }
        if (g_mode == MODE_BASIC)
            basic_command(g_view, wp);
        else if (g_mode == MODE_ADVANCED)
            adv_command(g_view, wp);
        else
            ns_command(wp);
        return 0;
    case WM_DROPFILES: {
        char f[PATHMAX];
        int n = DragQueryFile((HDROP)wp, (UINT)-1, NULL, 0), i;
        for (i = 0; i < n; i++) {
            DragQueryFile((HDROP)wp, i, f, sizeof f);
            if (g_mode == MODE_NEWSTYLE)
                ns_command(0);
            switch (merge_file(f, NULL)) {
            case MERGE_OK:
                msgbox(w, IDS_MERGED, f, MB_OK | MB_ICONEXCLAMATION);
                break;
            case MERGE_NOTREG:
                msgbox(w, IDS_NOTREG, NULL, MB_OK | MB_ICONEXCLAMATION);
                break;
            default:
                msgbox(w, IDS_CANTOPEN, f, MB_OK | MB_ICONEXCLAMATION);
            }
        }
        DragFinish((HDROP)wp);
        if (g_mode == MODE_BASIC)
            basic_refresh(g_view);
        else if (g_mode == MODE_ADVANCED)
            adv_refresh(g_view);
        else
            ns_command(IDM_NS_REFRESH);
        return 0;
    }
    case WM_CLOSE:
        if (g_mode == MODE_ADVANCED && !query_exit())
            return 0;
        save_size();
        if (g_mode == MODE_NEWSTYLE)
            ns_save();
        DestroyWindow(w);
        return 0;
    case WM_DESTROY:
        DragAcceptFiles(w, FALSE);
        help(HELP_QUIT, 0);
        UnhookWindowsHookEx(filter);
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProc(w, msg, wp, lp);
}

/* the command line's words: switches and a file */
static void parse(LPSTR cmd, BOOL *adv, BOOL *silent, LPSTR file)
{
    LPSTR p = cmd;
    file[0] = 0;
    while (*p) {
        LPSTR w;
        while (*p == ' ' || *p == '\t')
            p++;
        if (!*p)
            break;
        w = p;
        while (*p && *p != ' ' && *p != '\t')
            p++;
        if (*p)
            *p++ = 0;
        if ((w[0] == '/' || w[0] == '-') && w[1] && !w[2]) {
            if (w[1] == 'v' || w[1] == 'V')
                *adv = TRUE;
            else if (w[1] == 's' || w[1] == 'S')
                *silent = TRUE;
        } else if (!file[0] && lstrlen(w) < PATHMAX) {
            lstrcpy(file, w);
        }
    }
}

int PASCAL WinMain(HINSTANCE inst, HINSTANCE prev, LPSTR cmdline, int show)
{
    static char cmd[PATHMAX], file[PATHMAX];
    BOOL adv = FALSE, silent = FALSE;
    MSG m;
    int cx, cy;
    RECT r;
    g_inst = inst;
    rstr(IDS_CLASS, cls, sizeof cls);
    rstr(IDS_TITLE, title, sizeof title);
    lstrcpyn(cmd, cmdline, sizeof cmd);
    parse(cmd, &adv, &silent, file);
    if (file[0]) {
        switch (merge_file(file, NULL)) {
        case MERGE_OK:
            if (!silent)
                msgbox(NULL, IDS_MERGED, file, MB_OK | MB_ICONEXCLAMATION);
            break;
        case MERGE_NOTREG:
            if (!silent)
                msgbox(NULL, IDS_NOTREG, NULL, MB_OK | MB_ICONEXCLAMATION);
            break;
        default:
            if (!silent)
                msgbox(NULL, IDS_CANTOPEN, file, MB_OK | MB_ICONEXCLAMATION);
        }
        return 0;
    }
    g_nsSetting = GetPrivateProfileInt("w16", "RegeditNewStyle", 0, "SYSTEM.INI");
    if (g_nsSetting < 0 || g_nsSetting > 2)
        g_nsSetting = 0;
    start_mode = adv ? MODE_ADVANCED : MODE_BASIC;
    if (prev) {
        /* one at a time: a second start brings the first one up (RE12) */
        HWND w = FindWindow(cls, NULL);
        if (w) {
            if (IsIconic(w))
                ShowWindow(w, SW_RESTORE);
            SetActiveWindow(GetLastActivePopup(w));
        }
        return 0;
    }
    {
        WNDCLASS wc;
        _fmemset(&wc, 0, sizeof wc);
        wc.lpfnWndProc = FrameProc;
        wc.hInstance = inst;
        wc.hIcon = LoadIcon(inst, MAKEINTRESOURCE(IDI_REGEDIT));
        wc.hCursor = LoadCursor(NULL, IDC_ARROW);
        wc.hbrBackground = (HBRUSH)(COLOR_WINDOW + 1);
        wc.lpszClassName = cls;
        if (!RegisterClass(&wc))
            return 0;
    }
    basic_proc = MakeProcInstance((FARPROC)BasicDlgProc, inst);
    adv_proc = MakeProcInstance((FARPROC)AdvDlgProc, inst);
    g_frame = CreateWindow(cls, title, WS_OVERLAPPEDWINDOW,
                           CW_USEDEFAULT, 0, CW_USEDEFAULT, 0, NULL, NULL, inst, NULL);
    if (!g_frame)
        return 0;
    DragAcceptFiles(g_frame, TRUE);
    set_mode(g_nsSetting == 1 ? MODE_NEWSTYLE : start_mode);
    if (g_mode == MODE_NEWSTYLE) {
        ns_size(-1, -1);                /* its own size (newstyle.c) */
    } else {
        /* the template's size, or WIN.INI's (RE10) */
        GetClientRect(g_view, &r);
        cx = GetProfileInt("RegEdit", "width", -1);
        cy = GetProfileInt("RegEdit", "height", -1);
        keep_cx = r.right;              /* the template's size: never snapped (RE2, RE11) */
        keep_cy = r.bottom;
        size_frame(cx < 0 ? r.right : cx, cy < 0 ? r.bottom : cy);
    }
    ShowWindow(g_frame, show);
    UpdateWindow(g_frame);
    g_help_msg = RegisterWindowMessage("commdlg_help");   /* COMMDLG's HELPMSGSTRING */
    filter_inst = MakeProcInstance((FARPROC)MessageFilter, inst);
    filter = SetWindowsHookEx(WH_MSGFILTER, (HOOKPROC)filter_inst, inst, GetCurrentTask());
    accel_ns = LoadAccelerators(inst, MAKEINTRESOURCE(ACC_NEWSTYLE));
    accel_adv = LoadAccelerators(inst, MAKEINTRESOURCE(ACC_ADVANCED));
    while (GetMessage(&m, NULL, 0, 0)) {
        if (g_mode == MODE_NEWSTYLE) {
            if (ns_translate(&m))
                continue;
            if (TranslateAccelerator(g_frame, accel_ns, &m))
                continue;
        } else if (g_mode == MODE_ADVANCED && TranslateAccelerator(g_frame, accel_adv, &m)) {
            continue;
        } else if (g_view && IsDialogMessage(g_view, &m)) {
            continue;
        }
        TranslateMessage(&m);
        DispatchMessage(&m);
    }
    return m.wParam;
}
