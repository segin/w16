/*
 * main.c - w16 Notepad: the window, its edit control, menus, commands.
 *
 * Observed on Windows 3.1 (SNAP dump and info, ARCHITECTURE.md 15.1):
 *   class "Notepad": class style CS_BYTEALIGNCLIENT, background
 *   COLOR_WINDOW + 1, no extra bytes; the window WS_OVERLAPPEDWINDOW with
 *   WS_VSCROLL and WS_HSCROLL (both ranges 0..100) at CW_USEDEFAULT, and
 *   WS_EX_ACCEPTFILES (it takes files dropped from File Manager);
 *   the text is an edit control, ID 15, without scroll bars of its own:
 *   WS_CHILD | WS_VISIBLE | ES_MULTILINE | ES_AUTOVSCROLL | ES_NOHIDESEL,
 *   plus ES_AUTOHSCROLL unless Word Wrap is on, in the stock fixed font
 *   (Fixedsys), placed at (8, 2) in the client area, 15 pixels narrower
 *   and 4 pixels shorter than it; its text lives in Notepad's own data
 *   segment (the edit window's instance is Notepad's).
 *   The frame's scroll bars follow the edit control: their positions are
 *   what the edit control answers to WM_VSCROLL / WM_HSCROLL with the code
 *   EM_GETTHUMB (WM_USER + 14), observed equal at every step; scroll bar
 *   requests on the frame go on to the edit control. Word Wrap hides the
 *   horizontal bar (range 0..0).
 *   Title "Notepad - " followed by "(Untitled)" or the file's name.
 *   Menu items are enabled when a menu opens: Undo if the edit control can
 *   undo, Cut/Copy/Delete if text is selected, Paste if the clipboard
 *   holds text, Print if a printer is set up.
 *   Several copies may run at once.
 */
#include "notepad.h"
#ifndef W16_WIN2
#include <shellapi.h>
#include <commdlg.h>
#ifndef FINDMSGSTRING                   /* the SDK's name; not in every header */
#define FINDMSGSTRING       "commdlg_FindReplace"
#endif
/* SHELL.22 (SDK: ShellAbout); not declared in OpenWatcom's shellapi.h */
int FAR PASCAL ShellAbout(HWND, LPCSTR, LPCSTR, HICON);
#endif

HINSTANCE g_hinst;
HWND g_main, g_edit;
BOOL g_wrap;
char g_file[DOS_PATH];

static char cls_main[] = "Notepad";
static HACCEL g_accel;

#define EM_GETTHUMB         (WM_USER + 14)  /* "internal": Schulman, ch. 7 */

/* ---- messages ---- */

/* A message box with string `ids`, "%%" replaced by `arg`, captioned
   "Notepad". */
int msg_box(HWND owner, int ids, const char FAR *arg, UINT flags)
{
    char cap[32];
    LPSTR text = format_msg(ids, arg ? arg : "");
    int r;
    LoadString(g_hinst, IDS_APPNAME, cap, sizeof cap);
    r = MessageBox(owner, text ? text : "", cap, flags);
    mem_free(text);
    return r;
}

/* ---- the title ---- */

void set_title(void)
{
    char title[DOS_PATH + 32];
    int n = LoadString(g_hinst, IDS_TITLEPREFIX, title, sizeof title);
    if (g_file[0]) {
        s_cpy(title + n, s_basename(g_file), sizeof title - n);
#ifndef W16_WIN2
        AnsiUpper(title + n);
#else
        {
            char *c;
            for (c = title + n; *c; c++)
                if (*c >= 'a' && *c <= 'z')
                    *c -= 'a' - 'A';
        }
#endif
    } else {
        LoadString(g_hinst, IDS_UNTITLED, title + n, sizeof title - n);
    }
    SetWindowText(g_main, title);
}

/* ---- the edit control and the scroll bars ---- */

static int vthumb = -1, hthumb = -1;

/* Put the frame's scroll boxes where the edit control has its text. */
void sync_scroll(void)
{
    int v, h;
    if (!g_edit)
        return;
    v = (int)SendMessage(g_edit, WM_VSCROLL, EM_GETTHUMB, 0);
    if (v != vthumb) {
        vthumb = v;
        SetScrollPos(g_main, SB_VERT, v, TRUE);
    }
    if (!g_wrap) {
        h = (int)SendMessage(g_edit, WM_HSCROLL, EM_GETTHUMB, 0);
        if (h != hthumb) {
            hthumb = h;
            SetScrollPos(g_main, SB_HORZ, h, TRUE);
        }
    }
}

static void place_edit(void)
{
    RECT r;
    GetClientRect(g_main, &r);
    MoveWindow(g_edit, 8, 2, r.right - 15, r.bottom - 4, TRUE);
}

/* The edit control keeps its text in the local heap of the segment its
   window's instance handle names (SDK: CreateWindow; Petzold, Programming
   Windows, ch. 6, for the method). The original lets it use Notepad's own
   data segment, which bounds the text at about 51K; w16's Notepad gives it
   a 64K segment of its own, so it holds at least as much (ZOI). Kept for
   the program's life and shared by both Word Wrap edit controls (one at a
   time). Without the segment, Notepad's own data segment as the original. */
static HINSTANCE edit_ds(void)
{
    static HINSTANCE ds;
    HGLOBAL h;
    LPSTR p;
    if (ds)
        return ds;
    h = GlobalAlloc(GMEM_MOVEABLE | GMEM_ZEROINIT, 0x10000L);
    p = h ? (LPSTR)GlobalLock(h) : NULL;
    if (!p) {
        if (h)
            GlobalFree(h);
        return ds = g_hinst;
    }
    /* the first 16 bytes are the instance data a local heap needs */
    if (!LocalInit(SELECTOROF(p), 16, 0xFFFE)) {
        GlobalUnlock(h);
        GlobalFree(h);
        return ds = g_hinst;
    }
    return ds = (HINSTANCE)SELECTOROF(p);
}

HWND make_edit(BOOL wrap)
{
    HWND e = CreateWindow("Edit", "",
                          WS_CHILD | WS_VISIBLE | ES_MULTILINE | ES_AUTOVSCROLL | ES_NOHIDESEL |
                          (wrap ? 0 : ES_AUTOHSCROLL),
                          0, 0, 0, 0, g_main, (HMENU)IDC_EDIT, edit_ds(), NULL);
    if (e) {
        SendMessage(e, WM_SETFONT, (WPARAM)GetStockObject(SYSTEM_FIXED_FONT), 0);
        /* observed: the original takes typing and pasting past 30000
           characters (and past its 51K load limit), which USER's default
           limit stops; so the limit is lifted. UNVERIFIED: the value the
           original passes (0 here: no limit beyond the segment) */
        SendMessage(e, EM_LIMITTEXT, 0, 0);
    }
    return e;
}

/* Word Wrap: a new edit control with the other style takes the text
   (observed: the selection starts over at the beginning). */
static void toggle_wrap(void)
{
    HWND old = g_edit, e;
    BOOL modified = (BOOL)SendMessage(old, EM_GETMODIFY, 0, 0);
    WORD len = (WORD)SendMessage(old, WM_GETTEXTLENGTH, 0, 0);
    LPSTR buf = mem_alloc((DWORD)len + 1);
    if (!buf) {
        msg_box(g_main, IDS_WRAPLARGE, NULL, MB_OK | MB_ICONEXCLAMATION);
        return;
    }
    GetWindowText(old, buf, len + 1);
    e = make_edit(!g_wrap);
    if (!e) {
        mem_free(buf);
        msg_box(g_main, IDS_WRAPLARGE, NULL, MB_OK | MB_ICONEXCLAMATION);
        return;
    }
    g_wrap = !g_wrap;
    g_edit = e;
    place_edit();
    SetWindowText(e, buf);
    mem_free(buf);
    SendMessage(e, EM_SETMODIFY, modified, 0);
    DestroyWindow(old);
    SetScrollRange(g_main, SB_HORZ, 0, g_wrap ? 0 : 100, TRUE);
    vthumb = hthumb = -1;
    sync_scroll();
    SetFocus(e);
}

/* ---- menus ---- */

static void enable(HMENU m, int id, BOOL on)
{
    EnableMenuItem(m, id, MF_BYCOMMAND | (on ? MF_ENABLED : MF_GRAYED));
}

static void init_menu(HMENU m)
{
    DWORD sel = SendMessage(g_edit, EM_GETSEL, 0, 0);
    BOOL some = LOWORD(sel) != HIWORD(sel);
    enable(m, IDM_UNDO, (BOOL)SendMessage(g_edit, EM_CANUNDO, 0, 0));
    enable(m, IDM_CUT, some);
    enable(m, IDM_COPY, some);
    enable(m, IDM_DELETE, some);
    enable(m, IDM_PASTE, IsClipboardFormatAvailable(CF_TEXT));
    enable(m, IDM_PRINT, can_print());
    CheckMenuItem(m, IDM_WORDWRAP, MF_BYCOMMAND | (g_wrap ? MF_CHECKED : MF_UNCHECKED));
#ifdef W16_WIN2
    /* Windows 2.x has no WinHelp */
    enable(m, IDM_HELPCONTENTS, FALSE);
    enable(m, IDM_HELPSEARCH, FALSE);
    enable(m, IDM_HELPHELP, FALSE);
#endif
}

/* ---- commands ---- */

static void command(WORD id)
{
    switch (id) {
    case IDM_NEW:
        file_new();
        break;
    case IDM_OPEN:
        file_open();
        break;
    case IDM_SAVE:
        file_save(FALSE);
        break;
    case IDM_SAVEAS:
        file_save(TRUE);
        break;
    case IDM_PRINT:
        print_file();
        break;
    case IDM_PAGESETUP:
        page_setup();
        break;
    case IDM_PRINTSETUP:
        print_setup();
        break;
    case IDM_EXIT:
        PostMessage(g_main, WM_CLOSE, 0, 0);
        break;
    case IDM_UNDO:
        SendMessage(g_edit, EM_UNDO, 0, 0);
        break;
    case IDM_CUT:
    case IDM_COPY:
    case IDM_PASTE:
    case IDM_DELETE:                    /* the IDs are WM_CUT .. WM_CLEAR */
        SendMessage(g_edit, id, 0, 0);
        break;
    case IDM_SELECTALL:
        SendMessage(g_edit, EM_SETSEL, 0, MAKELONG(0, 0xFFFF));
        break;
    case IDM_TIMEDATE:
        insert_time_date();
        break;
    case IDM_WORDWRAP:
        toggle_wrap();
        break;
    case IDM_FIND:
        find_dialog();
        break;
    case IDM_FINDNEXT:
        find_next();
        break;
    case IDM_HELPCONTENTS:
    case IDM_HELPSEARCH:
    case IDM_HELPHELP:
        help(id);
        break;
    case IDM_ABOUT:
#ifndef W16_WIN2
        {
            char name[32];
            LoadString(g_hinst, IDS_APPNAME, name, sizeof name);
            ShellAbout(g_main, name, "", LoadIcon(g_hinst, MAKEINTRESOURCE(IDI_NOTEPAD)));
        }
#else
        about_dialog2();
#endif
        break;
    }
    sync_scroll();
}

/* ---- the window ---- */

LRESULT FAR PASCAL __export NPWndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp)
{
    switch (msg) {
    case WM_CREATE:
        g_main = hwnd;
        g_edit = make_edit(FALSE);
        if (!g_edit)
            return -1;
        SetScrollRange(hwnd, SB_VERT, 0, 100, FALSE);
        SetScrollRange(hwnd, SB_HORZ, 0, 100, FALSE);
#ifndef W16_WIN2
        DragAcceptFiles(hwnd, TRUE);
#endif
        return 0;
    case WM_SIZE:
        if (wp != SIZE_MINIMIZED) {
            place_edit();
            sync_scroll();
        }
        return 0;
    case WM_SETFOCUS:
        SetFocus(g_edit);
        return 0;
    case WM_VSCROLL:
    case WM_HSCROLL:
        SendMessage(g_edit, msg, wp, lp);
        sync_scroll();
        return 0;
    case WM_INITMENUPOPUP:
        /* observed: opening any menu updates every item (Print is grayed
           after the Edit menu opened) */
        if (!HIWORD(lp))
            init_menu(GetMenu(hwnd));
        return 0;
    case WM_COMMAND:
        if (LOWORD(wp) == IDC_EDIT && (HWND)LOWORD(lp) == g_edit) {
            if ((HIWORD(lp) == EN_ERRSPACE || HIWORD(lp) == EN_MAXTEXT) && !file_is_loading())
                msg_box(hwnd, IDS_NOMEMORY, NULL, MB_OK | MB_ICONEXCLAMATION);
            else
                sync_scroll();
            return 0;
        }
        command((WORD)wp);
        return 0;
#ifndef W16_WIN2
    case WM_DROPFILES:
        file_drop((HANDLE)wp);
        return 0;
#endif
    case WM_CLOSE:
        if (file_query_save())
            DestroyWindow(hwnd);
        return 0;
    case WM_QUERYENDSESSION:
        return file_query_save();
    case WM_DESTROY:
        help(0);                        /* HELP_QUIT */
#ifndef W16_WIN2
        DragAcceptFiles(hwnd, FALSE);
#endif
        PostQuitMessage(0);
        return 0;
    default:
#ifndef W16_WIN2
        if (msg == g_findmsg && g_findmsg)
            return find_message(lp);
#endif
        break;
    }
    return DefWindowProc(hwnd, msg, wp, lp);
}

static BOOL register_class(HINSTANCE hinst)
{
    WNDCLASS wc;
    wc.style = CS_BYTEALIGNCLIENT;
    wc.lpfnWndProc = NPWndProc;
    wc.cbClsExtra = 0;
    wc.cbWndExtra = 0;
    wc.hInstance = hinst;
#ifndef W16_WIN2
    wc.hIcon = LoadIcon(hinst, MAKEINTRESOURCE(IDI_NOTEPAD));
#else
    wc.hIcon = NULL;                    /* 2.x cannot read 3.x icons */
#endif
    wc.hCursor = LoadCursor(NULL, IDC_ARROW);
    wc.hbrBackground = (HBRUSH)(COLOR_WINDOW + 1);
#ifndef W16_WIN2
    wc.lpszMenuName = MAKEINTRESOURCE(IDM_MENU);
#else
    wc.lpszMenuName = NULL;             /* load_menu2 builds it */
#endif
    wc.lpszClassName = cls_main;
    return RegisterClass(&wc);
}

/* The command line: [/P] [file]. Blanks around it are dropped. */
static BOOL parse_cmdline(LPSTR cmd, char *file)
{
    BOOL print = FALSE;
    int n;
    while (*cmd == ' ' || *cmd == '\t')
        cmd++;
    if ((cmd[0] == '/' || cmd[0] == '-') && (cmd[1] == 'p' || cmd[1] == 'P') &&
        (cmd[2] == ' ' || cmd[2] == '\t' || !cmd[2])) {
        print = TRUE;
        cmd += 2;
        while (*cmd == ' ' || *cmd == '\t')
            cmd++;
    }
    s_cpy(file, cmd, DOS_PATH);
    for (n = s_len(file); n > 0 && (file[n - 1] == ' ' || file[n - 1] == '\t'); n--)
        file[n - 1] = 0;
    return print;
}

int PASCAL WinMain(HINSTANCE hinst, HINSTANCE hprev, LPSTR cmdline, int show)
{
    char title[48], file[DOS_PATH];
    BOOL print;
    MSG msg;

    g_hinst = hinst;
    if (!hprev && !register_class(hinst))
        return 1;
    g_accel = LoadAccelerators(hinst, "MAINACC");
#ifndef W16_WIN2
    g_findmsg = RegisterWindowMessage(FINDMSGSTRING);
#endif
    LoadString(hinst, IDS_APPNAME, title, sizeof title);
    if (!CreateWindow(cls_main, title, WS_OVERLAPPEDWINDOW | WS_VSCROLL | WS_HSCROLL,
                      CW_USEDEFAULT, 0, CW_USEDEFAULT, 0, NULL,
#ifdef W16_WIN2
                      load_menu2(),
#else
                      NULL,
#endif
                      hinst, NULL))
        return 1;
    set_title();

    /* observed: a file's messages (too large, not found) come before the
       window is shown */
    print = parse_cmdline(cmdline, file);
    if (file[0]) {
        if (print) {
            /* observed: the window shows the file while it prints (with
               no printer: "Cannot print..." over it), then Notepad ends */
            if (file_load(file, FALSE)) {
                ShowWindow(g_main, show);
                UpdateWindow(g_main);
                print_file();
            }
            DestroyWindow(g_main);
            return 0;
        }
        file_load(file, TRUE);
    }
    ShowWindow(g_main, show);
    UpdateWindow(g_main);

    while (GetMessage(&msg, NULL, 0, 0)) {
#ifndef W16_WIN2
        if (g_finddlg && IsDialogMessage(g_finddlg, &msg))
            continue;
#endif
        if (!TranslateAccelerator(g_main, g_accel, &msg)) {
            TranslateMessage(&msg);
            DispatchMessage(&msg);
        }
        if (msg.hwnd == g_edit)         /* keys and the mouse may scroll */
            sync_scroll();
    }
    return (int)msg.wParam;
}
