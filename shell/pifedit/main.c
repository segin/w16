/*
 * main.c - w16 PIF Editor (PIFEDIT.EXE), a clean-room clone of Windows
 * 3.1's (ARCHITECTURE.md 15.11). Every fact comes from black-box
 * observation of 3.1's PIF Editor on the reference install (SNAP probes
 * PE1-PE18 and the WinHelp probes PH1-PH2, tests/scripts/pifedit/obs/ and
 * tests/scripts/pifedithelp/, run with tests/pifobs.sh and
 * tests/pifhelp.sh), its help file read as user documentation, and the
 * published PIF descriptions (pif.c); the program's code and resources
 * were never looked at.
 *
 * The frame (class "Pif", WS_OVERLAPPEDWINDOW, accepting dropped files,
 * title "PIF Editor - (Untitled)" or "- NAME.PIF") holds one child
 * dialog, the view of the mode (dlg.c), at its client's origin, and below
 * it a 25-pixel status line saying what F1 would explain for the control
 * with the focus ("Press F1 for Help on Program Filename.") or what the
 * menu item under the cursor does. It starts in the mode Windows runs in;
 * Mode switches, with a warning when the other mode is chosen.
 */
#include "pifedit.h"
#include <commdlg.h>
#include <shellapi.h>
#include <string.h>

/* SHELL.22 (SDK: ShellAbout); not declared in OpenWatcom's shellapi.h */
int FAR PASCAL ShellAbout(HWND, LPCSTR, LPCSTR, HICON);

HINSTANCE g_inst;
HWND g_frame, g_view, g_adv;
BOOL g_enh, g_dirty;
static BOOL running_enh;
static char path[144];          /* the file, "" when untitled */
static char barfrm[96];         /* the frame's status line */
static char baradv[96];         /* the Advanced dialog's */
static HWND lastfocus;
static HWND viewfocus;           /* the view's control last focused */
static HFONT barfont;
static HHOOK filter;

int rstr(UINT id, LPSTR buf, int cb)
{
    return LoadString(g_inst, id, buf, cb);
}

int msgbox(HWND owner, UINT id, UINT flags)
{
    char text[400], cap[32];
    rstr(id, text, sizeof text);
    rstr(IDS_TITLE, cap, sizeof cap);
    return MessageBox(owner, text, cap, flags);
}

/* ---- the status line (PE1, PE5, PE6, PE18) ---- */

/* the line: a black line across the client, the face colour, a sunken box
   8 pixels in from each side, 3 below the line, the text drawn 3 pixels into it,
   MS Sans Serif 10 */
void status_paint(HWND w, HDC dc)
{
    RECT c, r;
    HBRUSH face;
    HPEN pen, old;
    HFONT of;
    LPCSTR text = (w == g_frame) ? barfrm : baradv;
    int top;
    GetClientRect(w, &c);
    top = c.bottom - 25;
    face = CreateSolidBrush(GetSysColor(COLOR_BTNFACE));
    SetRect(&r, 0, top, c.right, c.bottom);
    FillRect(dc, &r, face);
    DeleteObject(face);
    pen = CreatePen(PS_SOLID, 1, GetSysColor(COLOR_WINDOWFRAME));
    old = SelectObject(dc, pen);
    MoveTo(dc, 0, top);
    LineTo(dc, c.right, top);
    SelectObject(dc, old);
    DeleteObject(pen);
    pen = CreatePen(PS_SOLID, 1, GetSysColor(COLOR_BTNSHADOW));
    old = SelectObject(dc, pen);
    MoveTo(dc, 8, top + 22);
    LineTo(dc, 8, top + 3);
    LineTo(dc, c.right - 8, top + 3);
    SelectObject(dc, old);
    DeleteObject(pen);
    pen = CreatePen(PS_SOLID, 1, GetSysColor(COLOR_BTNHIGHLIGHT));
    old = SelectObject(dc, pen);
    MoveTo(dc, c.right - 9, top + 3);
    LineTo(dc, c.right - 9, top + 22);
    LineTo(dc, 7, top + 22);
    SelectObject(dc, old);
    DeleteObject(pen);
    if (!barfont)
        barfont = CreateFont(-13, 0, 0, 0, FW_NORMAL, 0, 0, 0, ANSI_CHARSET, OUT_DEFAULT_PRECIS,
                             CLIP_DEFAULT_PRECIS, DEFAULT_QUALITY, VARIABLE_PITCH | FF_SWISS,
                             "MS Sans Serif");
    of = SelectObject(dc, barfont);
    SetBkColor(dc, GetSysColor(COLOR_BTNFACE));
    SetTextColor(dc, GetSysColor(COLOR_BTNTEXT));
    SetRect(&r, 9, top + 4, c.right - 9, top + 22);
    ExtTextOut(dc, 11, top + 4, ETO_CLIPPED | ETO_OPAQUE, &r, text, lstrlen(text), NULL);
    SelectObject(dc, of);
}

static void bar_redraw(HWND w)
{
    RECT c;
    GetClientRect(w, &c);
    c.top = c.bottom - 21;
    c.bottom -= 3;
    c.left = 9;
    c.right -= 9;
    InvalidateRect(w, &c, FALSE);
}

void status_text(HWND w, LPCSTR text)
{
    LPSTR bar = (w == g_frame) ? barfrm : baradv;
    if (lstrcmp(bar, text) == 0)
        return;
    lstrcpy(bar, text);
    bar_redraw(w);
}

void status_set(HWND w, int topic)
{
    char fmt[64], name[64], text[96];
    text[0] = 0;
    if (topic >= T_ADVBUTTON) {
        rstr(topic == T_ADVBUTTON ? IDS_ADVBUTTON : topic == T_ADVOK ? IDS_ADVOK : IDS_ADVCANCEL,
             text, sizeof text);
    } else if (topic) {
        rstr(IDS_FMTHELPON, fmt, sizeof fmt);
        rstr(IDS_TOPIC + topic, name, sizeof name);
        wsprintf(text, fmt, (LPSTR)name);
    }
    status_text(w, text);
}

/* the topic of a control (PE5, PE6) */
int status_topic(HWND ctl)
{
    switch (GetDlgCtrlID(ctl)) {
    case IDC_PROGRAM: return T_PROGRAM;
    case IDC_TITLE: return T_TITLE;
    case IDC_PARAMS: return T_PARAMS;
    case IDC_DIR: return T_DIR;
    case IDC_TEXT: case IDC_GRAPHICS: return T_VIDEOMODE;
    case IDC_MEMREQ: case IDC_MEMDES: return T_MEMREQ;
    case IDC_XMSREQ: case IDC_XMSLIM: return T_XMS;
    case IDC_COM1: case IDC_COM2: case IDC_COM3: case IDC_COM4: case IDC_KEYBOARD:
        return T_DIRECTMOD;
    case IDC_NOEXCHANGE: return T_NOEXCHANGE;
    case IDC_NOSWITCH: return T_NOSWITCH;
    case IDC_CLOSEEXIT: return T_CLOSEEXIT;
    case IDC_NOSAVE: return T_NOSAVE;
    case IDC_ALTTAB: case IDC_ALTESC: case IDC_ALTENTER: case IDC_ALTSPACE:
    case IDC_ALTPRTSC: case IDC_PRTSC: case IDC_CTRLESC:
        return T_RESERVE;
    case IDC_VTEXT: case IDC_VLOW: case IDC_VHIGH: return T_VIDEOMEM;
    case IDC_EMSREQ: case IDC_EMSLIM: return T_EMS;
    case IDC_FULLSCREEN: case IDC_WINDOWED: return T_DISPLAY;
    case IDC_BACKGROUND: case IDC_EXCLUSIVE: return T_EXECUTION;
    case IDC_ADVANCED: return T_ADVBUTTON;
    case IDC_FGPRIO: case IDC_BGPRIO: return T_PRIORITY;
    case IDC_IDLE: return T_IDLE;
    case IDC_EMSLOCK: return T_EMSLOCK;
    case IDC_XMSLOCK: return T_XMSLOCK;
    case IDC_HMA: return T_HMA;
    case IDC_LOCKMEM: return T_LOCKMEM;
    case IDC_MONTEXT: case IDC_MONLOW: case IDC_MONHIGH: return T_MONITOR;
    case IDC_EMULATE: return T_EMULATE;
    case IDC_RETAIN: return T_RETAIN;
    case IDC_FASTPASTE: return T_FASTPASTE;
    case IDC_ALLOWCLOSE: return T_ALLOWCLOSE;
    case IDC_HOTKEY: return T_HOTKEY;
    case IDOK: return T_ADVOK;
    case IDCANCEL: return T_ADVCANCEL;
    }
    return T_NONE;
}

static HWND adv_window(void)
{
    HWND f = GetFocus();
    while (f) {
        char cls[8];
        if (GetParent(f) == g_frame && f != g_view)
            return NULL;
        GetClassName(f, cls, sizeof cls);
        if (GetWindow(f, GW_OWNER) == g_frame && lstrcmp(cls, "#32770") == 0 &&
            GetDlgItem(f, IDC_HOTKEY))
            return f;
        f = GetParent(f);
    }
    return NULL;
}

/* the focus moved: the status line of the window it is in says so; when
   it is in none of ours (a message box, a common dialog) the frame's line
   is empty (PE3, PE4) */
static void focus_check(void)
{
    HWND f = GetFocus(), a;
    if (f == lastfocus)
        return;
    lastfocus = f;
    if (!f || GetWindowTask(f) != GetCurrentTask())
        return;
    if (GetParent(f) == g_view) {
        viewfocus = f;
        status_set(g_frame, status_topic(f));
        return;
    }
    a = adv_window();
    if (a && GetParent(f) == a) {
        status_set(a, status_topic(f));
        return;
    }
    status_text(g_frame, "");
    if (g_adv)
        status_text(g_adv, "");
}

/* the window's title: "PIF Editor - (Untitled)" or "PIF Editor - NAME.PIF" */
static void set_title(void)
{
    char t[64], n[32];
    LPCSTR p = path, q;
    rstr(IDS_TITLE, t, sizeof t);
    lstrcat(t, " - ");
    if (!path[0]) {
        rstr(IDS_UNTITLED, n, sizeof n);
    } else {
        for (q = path; *q; q++)
            if (*q == '\\' || *q == ':')
                p = q + 1;
        lstrcpy(n, p);
        AnsiUpper(n);
    }
    lstrcat(t, n);
    SetWindowText(g_frame, t);
}

/* ---- help (probes PH01-PH24, tests/pifhelp.sh) ----
   Every request but Search and How to Use Help is followed by
   HELP_SETCONTENTS with the contents topic of where it came from: 50h
   the Standard-mode window, 51h the 386 Enhanced one, 52h Advanced. */

#define HELPID_STD      0x50
#define HELPID_ENH      0x51
#define HELPID_ADV      0x52

static void help(UINT cmd, DWORD data, DWORD contents)
{
    char file[32];
    rstr(IDS_HELPFILE, file, sizeof file);
    WinHelp(g_frame, file, cmd, data);
    if (contents)
        WinHelp(g_frame, file, HELP_SETCONTENTS, contents);
}

/* the context of a control of the window (F1, PH08-PH16): its ID, but in
   the Standard-mode window the number boxes and the reserved keys have
   384h-38Bh (in the 386 Enhanced window and Advanced, their IDs) */
static DWORD view_context(UINT id)
{
    static const WORD std_ids[] = { IDC_MEMREQ, IDC_XMSREQ, IDC_XMSLIM, IDC_ALTTAB,
                                    IDC_ALTESC, IDC_ALTPRTSC, IDC_PRTSC, IDC_CTRLESC };
    int i;
    if (!g_enh)
        for (i = 0; i < 8; i++)
            if (std_ids[i] == id)
                return 0x384 + i;
    return id;
}

/* F1 with the focus on ctl (in the window or in Advanced) */
void help_f1(HWND ctl)
{
    HWND p = ctl ? GetParent(ctl) : NULL;
    UINT id = ctl ? GetDlgCtrlID(ctl) : 0;
    if (p && p != g_view)
        help(HELP_CONTEXT, id, HELPID_ADV);
    else
        help(HELP_CONTEXT, view_context(id), g_enh ? HELPID_ENH : HELPID_STD);
}

static void help_cmd(UINT id)
{
    DWORD c = g_enh ? HELPID_ENH : HELPID_STD;
    switch (id) {
    case IDM_HELPCONTENTS:
        help(HELP_CONTEXT, c, c);
        break;
    case IDM_HELPSEARCH:
        help(HELP_PARTIALKEY, (DWORD)(LPSTR)"", 0);
        break;
    case IDM_HELPHELP:
        WinHelp(g_frame, NULL, HELP_HELPONHELP, 0);
        break;
    default:            /* the Standard, 386 Enhanced, Advanced options */
        help(HELP_CONTEXT, id, c);
        break;
    }
}

/* ---- files ---- */

static void load_view(void)
{
    _fmemset(&g_v, 0, sizeof g_v);
    pif_get(&g_v, g_enh);
    view_fill();
    g_dirty = FALSE;
}

static void do_new(void)
{
    pif_new();
    path[0] = 0;
    load_view();
    set_title();
    SetFocus(GetDlgItem(g_view, IDC_PROGRAM));
}

static BOOL save_to(LPCSTR to)
{
    if (!pif_save(to)) {
        msgbox(g_frame, IDS_CANTSAVE, MB_OK | MB_ICONEXCLAMATION);
        return FALSE;
    }
    if (to != path)
        lstrcpy(path, to);
    g_dirty = FALSE;
    set_title();
    return TRUE;
}

/* the program's extension must be .EXE, .COM or .BAT; else 3.1 asks
   (OK saves all the same; Cancel, the default, does not: PE4) */
static BOOL check_program(void)
{
    LPCSTR p = g_v.program, ext = NULL;
    for (; *p; p++) {
        if (*p == '.')
            ext = p + 1;
        else if (*p == '\\' || *p == ':')
            ext = NULL;
    }
    if (ext && (lstrcmpi(ext, "EXE") == 0 || lstrcmpi(ext, "COM") == 0 || lstrcmpi(ext, "BAT") == 0))
        return TRUE;
    return msgbox(g_frame, IDS_BADEXT, MB_OKCANCEL | MB_ICONEXCLAMATION | MB_DEFBUTTON2) == IDOK;
}

static void filter_string(LPSTR buf, int cb)
{
    LPSTR p;
    rstr(IDS_FILTER, buf, cb);
    for (p = buf; *p; p++)
        if (*p == '|')
            *p = 0;
}

static BOOL file_dialog(BOOL save, LPSTR name)
{
    OPENFILENAME ofn;
    char filt[96], title[32];
    filter_string(filt, sizeof filt);
    rstr(save ? IDS_SAVETITLE : IDS_OPENTITLE, title, sizeof title);
    rstr(IDS_DEFEXT, name, 144);            /* "*.pif" */
    _fmemset(&ofn, 0, sizeof ofn);
    ofn.lStructSize = sizeof ofn;
    ofn.hwndOwner = g_frame;
    ofn.lpstrFilter = filt;
    ofn.nFilterIndex = 1;
    ofn.lpstrFile = name;
    ofn.nMaxFile = 144;
    ofn.lpstrTitle = title;
    ofn.lpstrDefExt = "PIF";
    ofn.Flags = OFN_HIDEREADONLY | (save ? OFN_OVERWRITEPROMPT : 0);
    return save ? GetSaveFileName(&ofn) : GetOpenFileName(&ofn);
}

static BOOL do_save(BOOL as)
{
    char name[144];
    if (!view_commit(TRUE))
        return FALSE;
    if (as || !path[0]) {
        if (!file_dialog(TRUE, name))
            return FALSE;
    } else {
        lstrcpy(name, path);
    }
    if (!check_program())
        return FALSE;
    pif_put(&g_v, g_enh);
    return save_to(name);
}

/* a change made: Yes saves, No goes on, Cancel stops (PE4) */
static BOOL ask_save(void)
{
    if (!g_dirty)
        return TRUE;
    switch (msgbox(g_frame, IDS_SAVECHANGES, MB_YESNOCANCEL | MB_ICONEXCLAMATION)) {
    case IDYES:
        return do_save(FALSE);
    case IDNO:
        return TRUE;
    }
    return FALSE;
}

static void open_file(LPCSTR name)
{
    int r;
    LPCSTR p, ext = NULL;
    for (p = name; *p; p++) {
        if (*p == '.')
            ext = p + 1;
        else if (*p == '\\' || *p == ':')
            ext = NULL;
    }
    if (!ext || lstrcmpi(ext, "PIF") != 0) {
        if (msgbox(g_frame, IDS_NOTPIFEXT, MB_OKCANCEL | MB_ICONEXCLAMATION | MB_DEFBUTTON2) != IDOK)
            return;
    }
    r = pif_load(name, g_frame);
    if (r && r != IDS_OLDPIF && r != IDS_INVALIDPIF) {
        /* what was shown stays (PE19) */
        msgbox(g_frame, r, MB_OK | MB_ICONEXCLAMATION);
        return;
    }
    /* a converted file: said before it is shown (PF10) */
    if (r)
        msgbox(g_frame, r, MB_OK | MB_ICONEXCLAMATION);
    lstrcpy(path, name);
    load_view();
    /* read with a wrong checksum (Setup's PIFs have 0), the box's state,
       checked when the flags byte is 0, is what is saved (PF10; PE13,
       PE21: with a right one the file's bit; UNVERIFIED: PE9 saved the
       file's bit with File > Save) */
    if (g_badsum)
        g_v.closeset = TRUE;
    set_title();
    /* the program's text selected (PF10) */
    {
        HWND c = GetDlgItem(g_view, IDC_PROGRAM);
        SetFocus(c);
        SendMessage(c, EM_SETSEL, 0, MAKELPARAM(0, -1));
    }
}

static void do_open(void)
{
    char name[144];
    if (!ask_save())
        return;
    if (file_dialog(FALSE, name))
        open_file(name);
}

/* Mode: the other mode than Windows runs in is chosen only after a
   warning (PE3, PE11) */
static void set_mode(BOOL enh)
{
    HMENU m = GetMenu(g_frame);
    if (enh == g_enh)
        return;
    if (enh != running_enh &&
        msgbox(g_frame, enh ? IDS_NOT386 : IDS_NOTSTD, MB_OKCANCEL | MB_ICONINFORMATION) != IDOK)
        return;
    if (!view_commit(TRUE))
        return;
    pif_put(&g_v, g_enh);
    g_enh = enh;
    CheckMenuItem(m, IDM_STANDARD, enh ? MF_UNCHECKED : MF_CHECKED);
    CheckMenuItem(m, IDM_ENHANCED, enh ? MF_CHECKED : MF_UNCHECKED);
    {
        BOOL d = g_dirty;
        pif_get(&g_v, g_enh);
        view_create();
        g_dirty = d;
    }
    lastfocus = NULL;
    SetFocus(GetDlgItem(g_view, IDC_PROGRAM));
}

/* the menu item under the cursor (PE18): its line; a menu's title the
   line of its first item; the menu closed: the focus's line again */
static void menu_select(WPARAM wp, LPARAM lp)
{
    WORD flags = LOWORD(lp);
    UINT id = (UINT)wp, sid;
    char text[96];
    if (flags == 0xFFFF && HIWORD(lp) == 0) {
        lastfocus = NULL;
        focus_check();
        return;
    }
    if (flags & MF_SEPARATOR) {
        status_text(g_frame, "");
        return;
    }
    if (flags & MF_POPUP) {
        id = GetMenuItemID((HMENU)wp, 0);
        if (flags & MF_SYSMENU)
            id = SC_RESTORE;
    }
    if ((flags & MF_SYSMENU) || id >= 0xF000 && id < 0xFFF0)
        sid = IDS_SYSMENU + ((id - 0xF000) >> 4);
    else if (id == IDM_HELPCONTENTS)
        sid = IDS_MENU + 1;
    else if (id == IDM_HELPHELP)
        sid = IDS_MENU + 2;
    else
        sid = IDS_MENU + id;
    text[0] = 0;
    rstr(sid, text, sizeof text);
    status_text(g_frame, text);
}

static void command(UINT id)
{
    switch (id) {
    case IDM_NEW:
        if (ask_save())
            do_new();
        break;
    case IDM_OPEN:
        do_open();
        break;
    case IDM_SAVE:
        do_save(FALSE);
        break;
    case IDM_SAVEAS:
        do_save(TRUE);
        break;
    case IDM_EXIT:
        PostMessage(g_frame, WM_CLOSE, 0, 0);
        break;
    case IDM_ABOUT: {
        char t[32];
        rstr(IDS_TITLE, t, sizeof t);
        ShellAbout(g_frame, t, NULL, LoadIcon(g_inst, MAKEINTRESOURCE(IDI_PIFEDIT)));
        break;
    }
    case IDM_STANDARD:
        set_mode(FALSE);
        break;
    case IDM_ENHANCED:
        set_mode(TRUE);
        break;
    case IDM_HELPCONTENTS: case IDM_HELPSEARCH: case IDM_HELPSTD:
    case IDM_HELPENH: case IDM_HELPADV: case IDM_HELPHELP:
        help_cmd(id);
        break;
    }
}

static LRESULT CALLBACK frame_proc(HWND w, UINT msg, WPARAM wp, LPARAM lp)
{
    switch (msg) {
    case WM_PAINT: {
        PAINTSTRUCT ps;
        HDC dc = BeginPaint(w, &ps);
        status_paint(w, dc);
        EndPaint(w, &ps);
        return 0;
    }
    case WM_SIZE:
        InvalidateRect(w, NULL, TRUE);
        break;
    case WM_SETFOCUS:
        /* back from a message box or a common dialog: the control that
           had the focus, an edit's text selected, as a dialog does (PE4) */
        if (g_view) {
            HWND c = viewfocus && IsChild(g_view, viewfocus) ? viewfocus : GetDlgItem(g_view, IDC_PROGRAM);
            SetFocus(c);
            if (SendMessage(c, WM_GETDLGCODE, 0, 0) & DLGC_HASSETSEL)
                SendMessage(c, EM_SETSEL, 0, MAKELPARAM(0, -1));
        }
        return 0;
    case WM_COMMAND:
        if (LOWORD(lp) == 0) {
            command((UINT)wp);
            return 0;
        }
        break;
    case WM_MENUSELECT:
        menu_select(wp, lp);
        return 0;
    case WM_DROPFILES: {
        char name[144];
        DragQueryFile((HDROP)wp, 0, name, sizeof name);
        DragFinish((HDROP)wp);
        if (ask_save())
            open_file(name);
        return 0;
    }
    case WM_QUERYENDSESSION:
        return ask_save();
    case WM_CLOSE:
        if (!ask_save())
            return 0;
        DestroyWindow(w);
        return 0;
    case WM_DESTROY: {
        char file[32];
        rstr(IDS_HELPFILE, file, sizeof file);
        WinHelp(w, file, HELP_QUIT, 0);
        if (barfont)
            DeleteObject(barfont);
        PostQuitMessage(0);
        return 0;
    }
    }
    return DefWindowProc(w, msg, wp, lp);
}

/* F1 and the status line inside modal loops (message boxes, Advanced) */
static LRESULT CALLBACK filter_proc(int code, WPARAM wp, LPARAM lp)
{
    MSG FAR *m = (MSG FAR *)lp;
    if (code >= 0) {
        HWND a = adv_window();
        focus_check();
        /* F1 is help, but in the shortcut key box with Ctrl or Alt held
           a key like any other (PE12: Ctrl+F1) */
        if (a && m->message == WM_KEYDOWN && m->wParam == VK_F1 &&
            !(GetDlgCtrlID(GetFocus()) == IDC_HOTKEY &&
              (GetKeyState(VK_CONTROL) < 0 || GetKeyState(VK_MENU) < 0))) {
            help_f1(GetFocus());
            return 1;
        }
    }
    return CallNextHookEx(filter, code, wp, lp);
}

int PASCAL WinMain(HINSTANCE inst, HINSTANCE prev, LPSTR cmd, int show)
{
    WNDCLASS wc;
    MSG msg;
    char cls[16], t[32];
    HMENU m;
    (void)prev;
    g_inst = inst;
    g_fieldfix = GetPrivateProfileInt("w16", "PifFieldFix", 1, "SYSTEM.INI") != 0;
    rstr(IDS_CLASS, cls, sizeof cls);
    wc.style = CS_BYTEALIGNCLIENT;
    wc.lpfnWndProc = frame_proc;
    wc.cbClsExtra = 0;
    wc.cbWndExtra = 0;
    wc.hInstance = inst;
    wc.hIcon = LoadIcon(inst, MAKEINTRESOURCE(IDI_PIFEDIT));
    wc.hCursor = LoadCursor(NULL, IDC_ARROW);
    wc.hbrBackground = (HBRUSH)(COLOR_WINDOW + 1);
    wc.lpszMenuName = NULL;
    wc.lpszClassName = cls;
    if (!RegisterClass(&wc) || !hotkey_register(inst))
        return 0;
    running_enh = g_enh = (GetWinFlags() & WF_ENHANCED) != 0;
    rstr(IDS_TITLE, t, sizeof t);
    m = LoadMenu(inst, MAKEINTRESOURCE(MENU_PIFEDIT));
    CheckMenuItem(m, g_enh ? IDM_ENHANCED : IDM_STANDARD, MF_CHECKED);
    /* at 12, 48 on VGA (PE1, PE2): taken as one and a half dialog base
       units across and three down (UNVERIFIED on other displays) */
    {
        DWORD du = GetDialogBaseUnits();
        g_frame = CreateWindow(cls, t, WS_OVERLAPPEDWINDOW | WS_CLIPSIBLINGS,
                               LOWORD(du) * 3 / 2, HIWORD(du) * 3, 0, 0, NULL, m, inst, NULL);
    }
    if (!g_frame)
        return 0;
    DragAcceptFiles(g_frame, TRUE);
    pif_new();
    view_create();
    DragAcceptFiles(g_view, TRUE);
    load_view();
    set_title();
    while (*cmd == ' ')
        cmd++;
    if (*cmd) {
        char name[144];
        lstrcpyn(name, cmd, sizeof name);
        open_file(name);
    }
    ShowWindow(g_frame, show);
    UpdateWindow(g_frame);
    SetFocus(GetDlgItem(g_view, IDC_PROGRAM));
    filter = SetWindowsHookEx(WH_MSGFILTER, (HOOKPROC)filter_proc, inst, GetCurrentTask());
    while (GetMessage(&msg, NULL, 0, 0)) {
        if (msg.message == WM_KEYDOWN && msg.wParam == VK_F1 && g_view &&
            (GetParent(msg.hwnd) == g_view || msg.hwnd == g_view)) {
            help_f1(GetFocus());
            continue;
        }
        if (!g_view || !IsDialogMessage(g_view, &msg)) {
            TranslateMessage(&msg);
            DispatchMessage(&msg);
        }
        focus_check();
    }
    UnhookWindowsHookEx(filter);
    return msg.wParam;
}
