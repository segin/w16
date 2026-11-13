/*
 * main.c - w16 Cardfile: the main window, its children, menus, commands.
 *
 * Observed on Windows 3.1 (CF1, SNAP.EXE dump): the main window is class
 * "Cardfile", WS_OVERLAPPEDWINDOW with WS_EX_ACCEPTFILES, at 4,0 sized
 * 430 x 323. Its children, in Z order: a ListBox (ID 204, LBS_NOTIFY,
 * shown in List view), the "Card" window (205, WS_CLIPCHILDREN) that
 * draws the stack and holds the card's Edit control (200, ES_MULTILINE |
 * ES_NOHIDESEL, WS_EX_ACCEPTFILES), a centred Static with the view's
 * name (201), a centred Static with the number of cards (202), and
 * between them a horizontal ScrollBar (203) of arrows only that turns
 * the cards. A line under them closes the status area.
 */
#include "cardfile.h"

HINSTANCE g_hinst;
HWND g_frame, g_card, g_edit, g_list, g_scroll, g_viewname, g_count;
HFONT g_font;
int g_cx, g_cy;
BOOL g_listview, g_picmode;
char g_path[144];
BOOL g_dirty, g_carddirty;
PICTURE g_pic;
int g_fmt = FMT_MGC;
DWORD g_lastobj;

static char cls_frame[] = "Cardfile";
static HACCEL accel;
static int status_h;                    /* the status area, with its line */
static int sys_cy;                      /* the System font's height */

/* ---- strings and messages ---- */

LPSTR load_str(UINT ids)
{
    static char buf[256];
    if (!LoadString(g_hinst, ids, buf, sizeof buf))
        buf[0] = 0;
    return buf;
}

/* A message box with string ids, "%%" replaced by arg, titled
   "Cardfile" (string 11). */
int msg_box(UINT ids, LPCSTR arg, UINT flags)
{
    char text[400], title[40];
    LoadString(g_hinst, IDS_TITLE, title, sizeof title);
    fmt_msg(text, sizeof text, load_str(ids), arg);
    return MessageBox(g_frame, text, title, flags);
}

/* ---- the window's title and status area ---- */

void set_title(void)
{
    char t[180];
    LoadString(g_hinst, IDS_CARDFILE, t, sizeof t);
    s_cat(t, " - ", sizeof t);
    if (g_path[0])
        s_cat(t, base_name(g_path), sizeof t);
    else
        s_cat(t, load_str(IDS_UNTITLED), sizeof t);
    SetWindowText(g_frame, t);
}

/* " 1 Card", else the number, a blank and " Cards" ("2  Cards"; CF1) */
void update_status(void)
{
    char t[40];
    if (g_ncards == 1) {
        s_cpy(t, load_str(IDS_ONECARD), sizeof t);
    } else {
        s_utoa(t, g_ncards);
        s_cat(t, " ", sizeof t);
        s_cat(t, load_str(IDS_CARDS), sizeof t);
    }
    SetWindowText(g_count, t);
    SetWindowText(g_viewname, load_str(g_listview ? IDS_LISTVIEW : IDS_CARDVIEW));
}

/* Place the children for the client size. Observed with a 422 x 277
   client: the view name 0,0 193x17, the arrows 193,0 36x17, the count
   229,0 194x17 (one pixel past the client), the line at y 17, the list
   0,18 and the Card window 0,18 423x260 (one past, both ways). */
void layout(void)
{
    RECT r;
    /* the arrows: 36 pixels on VGA (UNVERIFIED: as two arrows of
       SM_CXHSCROLL + 1) */
    int w, h, sb = 2 * (GetSystemMetrics(SM_CXHSCROLL) + 1), half;
    GetClientRect(g_frame, &r);
    w = r.right;
    h = r.bottom;
    half = (w - sb) / 2;
    MoveWindow(g_viewname, 0, 0, half, status_h - 1, TRUE);
    MoveWindow(g_scroll, half, 0, sb, status_h - 1, TRUE);
    MoveWindow(g_count, half + sb, 0, w - half - sb + 1, status_h - 1, TRUE);
    MoveWindow(g_list, 0, status_h, w, h - status_h + 1, TRUE);
    MoveWindow(g_card, 0, status_h, w + 1, h - status_h + 1, TRUE);
}

/* In List view the window shrinks to the list's whole lines: observed
   (CF1, CF11) the list keeps its height of whole lines (257 pixels
   asked 260) and the window loses the 3 pixels below it; returning to
   Card view keeps the smaller window. UNVERIFIED: that resizing in List
   view does the same. */
static BOOL fitting;

static void fit_list(void)
{
    RECT c, l, w;
    int want;
    if (fitting || !g_listview || IsIconic(g_frame) || IsZoomed(g_frame))
        return;
    GetClientRect(g_frame, &c);
    GetWindowRect(g_list, &l);
    want = status_h + (l.bottom - l.top) - 1;
    if (want >= c.bottom)
        return;
    fitting = TRUE;
    GetWindowRect(g_frame, &w);
    SetWindowPos(g_frame, NULL, 0, 0, w.right - w.left, (w.bottom - w.top) - (c.bottom - want),
                 SWP_NOMOVE | SWP_NOZORDER);
    fitting = FALSE;
}

void show_view(BOOL list)
{
    g_listview = list;
    if (list) {
        body_store();
        SendMessage(g_list, LB_SETCURSEL, g_cur, 0);
        ShowWindow(g_list, SW_SHOW);
        ShowWindow(g_card, SW_HIDE);
        SetFocus(g_list);
        fit_list();
    } else {
        ShowWindow(g_card, SW_SHOW);
        ShowWindow(g_list, SW_HIDE);
        SetFocus(g_edit);
    }
    update_status();
}

/* fill the list with the index lines */
void list_fill(void)
{
    WORD i;
    SendMessage(g_list, WM_SETREDRAW, FALSE, 0);
    SendMessage(g_list, LB_RESETCONTENT, 0, 0);
    for (i = 0; i < g_ncards; i++)
        SendMessage(g_list, LB_ADDSTRING, 0, (LPARAM)(LPSTR)card_hdr(i)->index);
    SendMessage(g_list, LB_SETCURSEL, g_cur, 0);
    SendMessage(g_list, WM_SETREDRAW, TRUE, 0);
    InvalidateRect(g_list, NULL, TRUE);
}

/* the cards changed: the stack, the list and the count */
void card_changed(void)
{
    list_fill();
    InvalidateRect(g_card, NULL, TRUE);
    update_status();
}

/* ---- files ---- */

/* Ask to save changes; FALSE when the user cancelled. */
BOOL query_save(void)
{
    int r;
    body_store();
    if (!g_dirty)
        return TRUE;
    {
        /* the name in capitals: "(UNTITLED)" (CF9) */
        char name[144];
        s_cpy(name, g_path[0] ? base_name(g_path) : load_str(IDS_UNTITLED), sizeof name);
        AnsiUpper(name);
        r = msg_box(IDS_SAVECHANGES, name, MB_YESNOCANCEL | MB_ICONEXCLAMATION);
    }
    if (r == IDCANCEL)
        return FALSE;
    if (r == IDYES) {
        do_command(g_path[0] ? IDM_SAVE : IDM_SAVEAS);
        return !g_dirty;
    }
    return TRUE;
}

/* A new card file: one blank card. */
static void file_new(void)
{
    static char blank[INDEX_BYTES + 1];
    cards_reset();
    g_path[0] = 0;
    g_srcpath[0] = 0;
    g_fmt = FMT_MGC;
    g_lastobj = 0;
    card_insert(blank);
    g_cur = 0;
    body_load(0);
    g_dirty = FALSE;
    set_title();
    card_changed();
}

static void file_open(LPCSTR path)
{
    if (!crd_open(path, FALSE)) {
        file_new();
        return;
    }
    s_cpy(g_path, path, sizeof g_path);
    AnsiUpper(g_path);
    if (!g_ncards) {
        static char blank[INDEX_BYTES + 1];
        card_insert(blank);
    }
    g_cur = 0;
    body_load(0);
    g_dirty = FALSE;
    set_title();
    card_changed();
}

static void file_save(LPCSTR path, int fmt)
{
    HCURSOR old;
    if (!body_store())
        return;
    old = SetCursor(LoadCursor(NULL, IDC_WAIT));
    if (crd_save(path, fmt)) {
        s_cpy(g_path, path, sizeof g_path);
        AnsiUpper(g_path);
        g_dirty = FALSE;
        set_title();
    }
    SetCursor(old);
}

/* ---- cards ---- */

static void card_add(void)
{
    if (!body_store())
        return;
    dlg_add();
}

static void card_delete(void)
{
    char idx[INDEX_BYTES + 1];
    s_cpy(idx, card_hdr(g_cur)->index, sizeof idx);
    if (msg_box(IDS_DELETE, idx, MB_OKCANCEL | MB_ICONEXCLAMATION) != IDOK)
        return;
    pic_free(&g_pic);
    card_remove(g_cur);
    if (!g_ncards) {
        static char blank[INDEX_BYTES + 1];
        card_insert(blank);
    }
    if (g_cur >= g_ncards)
        g_cur = 0;
    body_load(g_cur);
    g_dirty = TRUE;
    card_changed();
}

static void card_duplicate(void)
{
    CARDHDR copy;
    WORD at;
    if (!body_store())
        return;
    mem_cpy(&copy, card_hdr(g_cur), sizeof copy);
    at = card_insert(copy.index);
    if (at == 0xFFFF) {
        msg_box(IDS_NOMEM, NULL, MB_OK | MB_ICONEXCLAMATION);
        return;
    }
    {
        LPCARD c = card_hdr(at);
        c->where = copy.where;
        c->pos = copy.pos;
        c->fmt = copy.fmt;
    }
    /* UNVERIFIED: whether an object on the card is cloned at once */
    g_cur = at;
    body_load(at);
    g_dirty = TRUE;
    card_changed();
}

/* ---- menus ---- */

static void enable(HMENU m, UINT id, BOOL on)
{
    EnableMenuItem(m, id, MF_BYCOMMAND | (on ? MF_ENABLED : MF_GRAYED));
}

void enable_menus(void)
{
    /* Observed (dialogs scenario, CF6): on an empty card in Text mode
       with nothing on the clipboard, Cut, Copy, Paste and Find Next are
       grayed and so is Print (Print All is not); in Picture mode with a
       picture, Cut, Copy and Paste are enabled. UNVERIFIED: that Print
       follows an empty card. */
    HMENU m = GetMenu(g_frame);
    BOOL card = !g_listview, sel, pic = g_pic.kind != PIC_NONE, paste;
    DWORD s = SendMessage(g_edit, EM_GETSEL, 0, 0);
    sel = HIWORD(s) != LOWORD(s);
    if (g_picmode)
        paste = ole_can_paste() || IsClipboardFormatAvailable(CF_BITMAP);
    else
        paste = IsClipboardFormatAvailable(CF_TEXT);
    enable(m, IDM_UNDO, card && !g_picmode && SendMessage(g_edit, EM_CANUNDO, 0, 0));
    enable(m, IDM_CUT, card && (g_picmode ? pic : sel));
    enable(m, IDM_COPY, card && (g_picmode ? pic : sel));
    enable(m, IDM_PASTE, card && paste);
    enable(m, IDM_PRINT, GetWindowTextLength(g_edit) || pic || card_hdr(g_cur)->index[0]);
    enable(m, IDM_FINDNEXT, find_has_text());
    enable(m, IDM_INDEX, TRUE);
    enable(m, IDM_RESTORE, card);
    enable(m, IDM_TEXT, card);
    enable(m, IDM_PICTURE, card);
    CheckMenuItem(m, IDM_TEXT, MF_BYCOMMAND | (g_picmode ? MF_UNCHECKED : MF_CHECKED));
    CheckMenuItem(m, IDM_PICTURE, MF_BYCOMMAND | (g_picmode ? MF_CHECKED : MF_UNCHECKED));
    CheckMenuItem(m, IDM_CARDVIEW, MF_BYCOMMAND | (g_listview ? MF_UNCHECKED : MF_CHECKED));
    CheckMenuItem(m, IDM_LISTVIEW, MF_BYCOMMAND | (g_listview ? MF_CHECKED : MF_UNCHECKED));
    ole_menu(GetSubMenu(m, MENU_EDIT));
}

/* ---- commands ---- */

void do_command(WORD id)
{
    char path[144];
    int fmt;
    switch (id) {
    case IDM_NEW:
        if (query_save())
            file_new();
        break;
    case IDM_OPEN:
        if (!query_save())
            break;
        path[0] = 0;
        if (file_dialog(FD_OPEN, path, &fmt))
            file_open(path);
        break;
    case IDM_SAVE:
        if (!g_path[0]) {
            do_command(IDM_SAVEAS);
            break;
        }
        file_save(g_path, -1);
        break;
    case IDM_SAVEAS:
        s_cpy(path, g_path, sizeof path);
        fmt = g_fmt;
        if (!file_dialog(FD_SAVEAS, path, &fmt))
            break;
        /* the 3.0 type always asks first, pictures or not; No saves
           nothing (CF13) */
        if (fmt == FMT_MGC &&
            msg_box(IDS_SAVE30, NULL, MB_YESNO | MB_ICONEXCLAMATION) != IDYES)
            break;
        file_save(path, fmt);
        break;
    case IDM_MERGE:
        path[0] = 0;
        if (!body_store())
            break;
        if (file_dialog(FD_MERGE, path, &fmt) && crd_open(path, TRUE)) {
            g_dirty = TRUE;
            body_load(g_cur);
            card_changed();
        }
        break;
    case IDM_PRINT:
    case IDM_PRINTALL:
        body_store();
        print_cards(id == IDM_PRINTALL);
        break;
    case IDM_PAGESETUP:
        dlg_pagesetup();
        break;
    case IDM_PRINTSETUP:
        print_setup();
        break;
    case IDM_EXIT:
        SendMessage(g_frame, WM_CLOSE, 0, 0);
        break;
    case IDM_UNDO:
        edit_undo();
        break;
    case IDM_CUT:
        edit_cut();
        break;
    case IDM_COPY:
        edit_copy();
        break;
    case IDM_PASTE:
        edit_paste();
        break;
    case IDM_INDEX:
        body_store();
        dlg_index();
        break;
    case IDM_RESTORE:
        edit_restore();
        break;
    case IDM_TEXT:
    case IDM_PICTURE:
        g_picmode = id == IDM_PICTURE;
        if (!g_picmode)
            SetFocus(g_edit);
        InvalidateRect(g_card, NULL, FALSE);
        break;
    case IDM_CARDVIEW:
    case IDM_LISTVIEW:
        if ((id == IDM_LISTVIEW) != g_listview)
            show_view(id == IDM_LISTVIEW);
        break;
    case IDM_ADD:
        card_add();
        break;
    case IDM_DELETE:
        card_delete();
        break;
    case IDM_DUPLICATE:
        card_duplicate();
        break;
    case IDM_AUTODIAL:
        dlg_autodial();
        break;
    case IDM_GOTO:
        dlg_goto();
        break;
    case IDM_FIND:
        dlg_find();
        break;
    case IDM_FINDNEXT:
        find_next();
        break;
    case IDM_HELPCONTENTS:
        help_contents();
        break;
    case IDM_HELPSEARCH:
        help_search();
        break;
    case IDM_HELPHELP:
        help_on_help();
        break;
    case IDM_ABOUT:
        about_box();
        break;
    default:
        ole_command(id);
        break;
    }
}

/* ---- the main window ---- */

static void paint_frame(HWND w)
{
    PAINTSTRUCT ps;
    RECT r;
    HDC dc = BeginPaint(w, &ps);
    GetClientRect(w, &r);
    /* the line under the status area */
    r.top = status_h - 1;
    r.bottom = status_h;
    FillRect(dc, &r, (HBRUSH)GetStockObject(BLACK_BRUSH));
    EndPaint(w, &ps);
}

LRESULT CALLBACK __export FrameWndProc(HWND w, UINT msg, WPARAM wp, LPARAM lp)
{
    switch (msg) {
    case WM_SIZE:
        if (wp != SIZE_MINIMIZED) {
            layout();
            fit_list();
        }
        break;
    case WM_PAINT:
        paint_frame(w);
        return 0;
    case WM_SETFOCUS:
        SetFocus(g_listview ? g_list : g_edit);
        return 0;
    case WM_INITMENU:
        enable_menus();
        break;
    case WM_MENUSELECT:
        help_menu_select(wp, lp);
        break;
    case WM_COMMAND:
        if (LOWORD(lp) && (HWND)LOWORD(lp) == g_list) {
            if (HIWORD(lp) == LBN_SELCHANGE) {
                WORD sel = (WORD)SendMessage(g_list, LB_GETCURSEL, 0, 0);
                if (sel != (WORD)LB_ERR && sel != g_cur)
                    go_card(sel);
            } else if (HIWORD(lp) == LBN_DBLCLK) {
                do_command(IDM_INDEX);
            }
            return 0;
        }
        if (LOWORD(lp) && (HWND)LOWORD(lp) == g_edit)
            return 0;
        do_command(wp);
        return 0;
    case WM_HSCROLL:
        if (wp == SB_LINEUP || wp == SB_PAGEUP)
            go_next(-1);
        else if (wp == SB_LINEDOWN || wp == SB_PAGEDOWN)
            go_next(1);
        return 0;
    case WM_DROPFILES:
        {
            char path[144];
            DragQueryFile((HDROP)wp, 0, path, sizeof path);
            DragFinish((HDROP)wp);
            if (query_save())
                file_open(path);
        }
        return 0;
    case WM_CLOSE:
        if (!query_save())
            return 0;
        DestroyWindow(w);
        return 0;
    case WM_QUERYENDSESSION:
        return query_save();
    case WM_DESTROY:
        help_quit();
        temp_close();
        PostQuitMessage(0);
        return 0;
    }
    if (msg == g_findmsg && g_findmsg)
        return find_message(msg, lp);
    return DefWindowProc(w, msg, wp, lp);
}

static BOOL init(HINSTANCE hinst, HINSTANCE prev, LPSTR cmd, int show)
{
    WNDCLASS wc;
    HDC dc;
    TEXTMETRIC tm;
    HFONT old;

    g_hinst = hinst;
    if (!prev) {
        mem_set(&wc, 0, sizeof wc);
        /* 100Bh, as observed (CF11): the menu bar items go on 8-pixel
           boundaries, the client area's left edge too */
        wc.style = CS_BYTEALIGNCLIENT | CS_DBLCLKS | CS_HREDRAW | CS_VREDRAW;
        wc.lpfnWndProc = FrameWndProc;
        wc.hInstance = hinst;
        wc.hIcon = LoadIcon(hinst, MAKEINTRESOURCE(IDI_CARDFILE));
        wc.hCursor = LoadCursor(NULL, IDC_ARROW);
        wc.hbrBackground = (HBRUSH)(COLOR_WINDOW + 1);
        wc.lpszMenuName = MAKEINTRESOURCE(IDM_MAIN);
        wc.lpszClassName = cls_frame;
        if (!RegisterClass(&wc) || !cardwnd_register(hinst))
            return FALSE;
    }
    if (!cards_init())
        return FALSE;
    g_font = GetStockObject(SYSTEM_FIXED_FONT);
    dc = GetDC(NULL);
    old = SelectObject(dc, g_font);
    GetTextMetrics(dc, &tm);
    g_cx = tm.tmAveCharWidth;
    g_cy = tm.tmHeight + tm.tmExternalLeading;
    SelectObject(dc, GetStockObject(SYSTEM_FONT));
    GetTextMetrics(dc, &tm);
    sys_cy = tm.tmHeight;
    SelectObject(dc, old);
    ReleaseDC(NULL, dc);
    status_h = sys_cy + 2;

    accel = LoadAccelerators(hinst, MAKEINTRESOURCE(IDA_MAIN));
    g_frame = CreateWindowEx(WS_EX_ACCEPTFILES, cls_frame, "", WS_OVERLAPPEDWINDOW,
                             4, 0, 430, 323, NULL, NULL, hinst, NULL);
    if (!g_frame)
        return FALSE;
    g_list = CreateWindow("ListBox", "", WS_CHILD | LBS_NOTIFY, 0, 0, 0, 0,
                          g_frame, (HMENU)ID_LIST, hinst, NULL);
    g_card = CreateWindow("Card", "", WS_CHILD | WS_VISIBLE | WS_CLIPCHILDREN, 0, 0, 0, 0,
                          g_frame, (HMENU)ID_CARD, hinst, NULL);
    g_edit = CreateWindowEx(WS_EX_ACCEPTFILES, "Edit", "",
                            WS_CHILD | WS_VISIBLE | ES_MULTILINE | ES_NOHIDESEL,
                            0, 0, 0, 0, g_card, (HMENU)ID_EDIT, hinst, NULL);
    g_viewname = CreateWindow("Static", "", WS_CHILD | WS_VISIBLE | SS_CENTER, 0, 0, 0, 0,
                              g_frame, (HMENU)ID_VIEWNAME, hinst, NULL);
    g_count = CreateWindow("Static", "", WS_CHILD | WS_VISIBLE | SS_CENTER, 0, 0, 0, 0,
                           g_frame, (HMENU)ID_COUNT, hinst, NULL);
    g_scroll = CreateWindow("ScrollBar", "", WS_CHILD | WS_VISIBLE | WS_HSCROLL | SBS_HORZ, 0, 0, 0, 0,
                            g_frame, (HMENU)ID_SCROLL, hinst, NULL);
    if (!g_list || !g_card || !g_edit || !g_viewname || !g_count || !g_scroll)
        return FALSE;
    SendMessage(g_list, WM_SETFONT, (WPARAM)g_font, 0);
    SendMessage(g_edit, WM_SETFONT, (WPARAM)g_font, 0);
    g_oldedit = (FARPROC)GetWindowLong(g_edit, GWL_WNDPROC);
    SetWindowLong(g_edit, GWL_WNDPROC, (LONG)MakeProcInstance((FARPROC)EditWndProc, hinst));
    if (!ole_init())
        return FALSE;
    help_init();
    page_defaults();
    layout();
    card_place_edit();

    file_new();
    while (*cmd == ' ')
        cmd++;
    if (*cmd)
        file_open(cmd);
    ShowWindow(g_frame, show);
    UpdateWindow(g_frame);
    SetFocus(g_edit);
    return TRUE;
}

int PASCAL WinMain(HINSTANCE hinst, HINSTANCE prev, LPSTR cmd, int show)
{
    MSG m;
    if (!init(hinst, prev, cmd, show)) {
        if (g_frame)
            DestroyWindow(g_frame);
        return 0;
    }
    while (GetMessage(&m, NULL, 0, 0)) {
        if (g_finddlg && IsDialogMessage(g_finddlg, &m))
            continue;
        if (TranslateAccelerator(g_frame, accel, &m))
            continue;
        TranslateMessage(&m);
        DispatchMessage(&m);
    }
    ole_done();
    help_done();
    return m.wParam;
}
