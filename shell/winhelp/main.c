/*
 * main.c - w16 Windows Help (WINHELP.EXE): the main window, navigation,
 * the button bar, history (ARCHITECTURE.md 15.10).
 *
 * Observed on 3.1 (tests/whobs.sh, WH1-WH3):
 *  - started by hand (WINHELP.EXE [FILE]) the main window's class is
 *    MS_WINDOC; started by USER's WinHelp ("winhelp.exe -x") it is
 *    MS_WINHELP, the class WinHelp looks for, so a Help window opened by
 *    hand is never taken over by an application. Help > How to Use Help
 *    goes through WinHelp: from a hand-started window it starts a second
 *    instance;
 *  - the main window (WS_OVERLAPPEDWINDOW | WS_CLIPCHILDREN) holds the
 *    button bar MS_WINICON (21 pixels high, standard push buttons 59 x 21
 *    side by side from its left edge, bold 8-point Helv, the rest of the
 *    bar dark grey), the non-scrolling region MS_WINNSR and the topic
 *    window MS_WINTOPIC (a vertical scroll bar when the topic is longer);
 *    with no file the button bar has no height;
 *  - its first place: x a third of the screen in, y 2, width the rest
 *    less 2, height the screen's less 4 (VGA: 213, 2, 425, 476); WIN.INI
 *    [Windows Help] M_WindowPosition=[x,y,cx,cy,maximized] keeps the last
 *    one closed; a second instance is put 24 pixels right and down;
 *  - the caption is the file's title (|SYSTEM), "Windows Help" (w16:
 *    "w16 Help") with no file; Back is greyed when there is nothing to go
 *    back to (a jump pushes the topic left, Back pops it); History lists
 *    every topic shown, the latest first, in MS_WIN_PATH "Windows Help
 *    History" (w16: "w16 Help History"), a third of the screen in each
 *    dimension (WIN.INI H_WindowPosition), with the previous topic
 *    selected.
 */
#include "winhelp.h"
#include <commdlg.h>
#include <shellapi.h>

HINSTANCE g_inst;
HWND g_frame, g_icon, g_topic, g_nsr, g_history, g_note, g_second;
HWND g_secondtopic, g_secondnsr;
BOOL g_api;
HLP FAR *g_hlp;
char g_file[PATHMAX];
int g_cur = -1;
DWORD g_curoff;
HLPTHDR g_thdr;
char g_title[128];
VIEW g_vtopic, g_vnsr, g_vnote, g_vsecond, g_vsecondnsr;
HCURSOR g_hand;
HFONT g_btnfont;
HWND g_owner;
DWORD g_contents = 0xFFFFFFFFUL;
BOOL g_ontop;
PLACE FAR *g_hist;
int g_nhist;
static PLACE FAR *back;
static int nback;
static char appname[64];
static int secondwin = -1;
static int start_pos[5];        /* the main window's place as read */
static int hist_pos[4];         /* the History window's */
static BOOL ever_opened;        /* a help file was shown */          /* the [WINDOWS] definition shown in g_second */

/* ---- buttons ---- */

typedef struct {
    HWND hwnd;
    WORD id;
    char name[16];          /* CreateButton's id (e.g. "gb"); "" for the standard ones */
    char macro[256];
} BUTTON;
static BUTTON FAR *buttons;
static int nbuttons;
static BOOL browse;
#define BTN_W 59
#define BTN_H 21

int rstr(UINT id, LPSTR buf, int cb)
{
    return LoadString(g_inst, id, buf, cb);
}

int msgbox(HWND owner, UINT id, LPCSTR arg, UINT flags)
{
    static char fmt[256], text[256 + PATHMAX];
    rstr(id, fmt, sizeof fmt);
    if (arg)
        wsprintf(text, fmt, arg);
    else
        lstrcpy(text, fmt);
    return MessageBox(owner, text, appname, flags);
}

/* 3.1's errors: OK and the information icon, captioned with the
   program's name (WH3) */
void errorbox(UINT id, LPCSTR arg)
{
    msgbox(g_frame && IsWindowVisible(g_frame) ? g_frame : NULL, id, arg, MB_OK | MB_ICONINFORMATION);
}

/* ---- open files (the main window's, and others for popups, secondary
   windows and jumps into other files) ---- */

#define NOPEN 4
static struct {
    char path[PATHMAX];
    HLP FAR *h;
    DWORD used;
} opened[NOPEN];
static DWORD usetick;

void full_path(LPCSTR name, LPSTR out)
{
    OFSTRUCT of;
    if (OpenFile(name, &of, OF_EXIST) != HFILE_ERROR)
        lstrcpy(out, of.szPathName);
    else
        lstrcpy(out, name);
    AnsiUpper(out);
}

static int last_error;

HLP FAR *hlp_get(LPCSTR file)
{
    char path[PATHMAX];
    int i, victim = -1, err;
    HLP FAR *h;
    full_path(file, path);
    for (i = 0; i < NOPEN; i++)
        if (opened[i].h && !lstrcmpi(opened[i].path, path)) {
            opened[i].used = ++usetick;
            return opened[i].h;
        }
    h = hlp_open(path, &err);
    last_error = err;
    if (!h)
        return NULL;
    for (i = 0; i < NOPEN; i++) {
        if (!opened[i].h) {
            victim = i;
            break;
        }
        /* never the ones on screen */
        if (opened[i].h == g_hlp || opened[i].h == g_vsecond.hlp || opened[i].h == g_vnote.hlp)
            continue;
        if (victim < 0 || opened[i].used < opened[victim].used)
            victim = i;
    }
    if (victim < 0) {
        hlp_close(h);
        last_error = HLPE_NOMEM;
        return NULL;
    }
    if (opened[victim].h)
        hlp_close(opened[victim].h);
    opened[victim].h = h;
    opened[victim].used = ++usetick;
    lstrcpy(opened[victim].path, path);
    return h;
}

static LPCSTR path_of(HLP FAR *h)
{
    int i;
    for (i = 0; i < NOPEN; i++)
        if (opened[i].h == h)
            return opened[i].path;
    return "";
}

static LPCSTR base_name(LPCSTR p)
{
    LPCSTR b = p;
    for (; *p; p++)
        if (*p == '\\' || *p == '/' || *p == ':')
            b = p + 1;
    return b;
}

static void open_error(LPCSTR name)
{
    char up[PATHMAX];
    lstrcpy(up, base_name(name));
    AnsiUpper(up);
    errorbox(last_error == HLPE_NOFILE ? IDS_NOFILE : last_error == HLPE_NOMEM ? IDS_NOMEM : IDS_NOTHELP, up);
}

static void set_caption(void)
{
    char t[160];
    if (!g_hlp)
        lstrcpy(t, appname);
    else if (g_hlp->title[0])
        lstrcpy(t, g_hlp->title);
    else
        wsprintf(t, "%s - %s", (LPSTR)appname, (LPSTR)base_name(g_file));
    SetWindowText(g_frame, t);
}

static void clear_buttons(void);
static void standard_buttons(void);

/* make name the main window's file; its [CONFIG] macros run */
BOOL open_file(LPCSTR name, BOOL quiet)
{
    HLP FAR *h = hlp_get(name);
    if (!h) {
        if (!quiet)
            open_error(name);
        return FALSE;
    }
    if (h == g_hlp)
        return TRUE;
    popup_close();
    g_hlp = h;
    ever_opened = TRUE;
    lstrcpy(g_file, path_of(h));
    g_contents = 0xFFFFFFFFUL;
    g_cur = -1;
    view_clear(&g_vtopic);
    view_clear(&g_vnsr);
    fonts_free(&g_vtopic);
    fonts_free(&g_vnsr);
    g_vtopic.hlp = g_vnsr.hlp = NULL;
    clear_buttons();
    standard_buttons();
    set_caption();
    layout_frame();
    if (h->config) {
        LPCSTR m = h->config;
        while (*m) {
            run_macros(m);
            m += lstrlen(m) + 1;
        }
    }
    bookmark_menu();
    update_buttons();
    return TRUE;
}

/* ---- showing topics ---- */

static void place_of_current(PLACE FAR *p)
{
    lstrcpy(p->file, g_file);
    p->off = g_curoff;
    p->scroll = g_vtopic.scroll;
    hlp_strcpyn(p->title, g_title, sizeof p->title);
}

void history_add(void)
{
    int i;
    if (g_nhist == MAXHIST)
        g_nhist--;
    for (i = g_nhist; i > 0; i--)
        g_hist[i] = g_hist[i - 1];
    place_of_current(&g_hist[0]);
    g_nhist++;
    if (g_history) {
        HWND lb = GetDlgItem(g_history, 1);
        int k;
        SendMessage(lb, WM_SETREDRAW, FALSE, 0);
        SendMessage(lb, LB_RESETCONTENT, 0, 0);
        for (k = 0; k < g_nhist; k++)
            SendMessage(lb, LB_ADDSTRING, 0, (LPARAM)(LPSTR)g_hist[k].title);
        SendMessage(lb, LB_SETCURSEL, g_nhist > 1 ? 1 : 0, 0);
        SendMessage(lb, WM_SETREDRAW, TRUE, 0);
        InvalidateRect(lb, NULL, TRUE);
    }
}

BOOL show_topic(int topic, BOOL record)
{
    HLPTHDR t;
    char macros[512];
    RECT r;
    if (!g_hlp || !hlp_topichdr(g_hlp, topic, &t, g_title, sizeof g_title, macros, sizeof macros))
        return FALSE;
    popup_close();
    if (record && g_cur >= 0) {
        if (nback == MAXBACK) {
            int i;
            for (i = 1; i < MAXBACK; i++)
                back[i - 1] = back[i];
            nback--;
        }
        place_of_current(&back[nback++]);
    }
    g_cur = topic;
    g_thdr = t;
    g_curoff = g_hlp->topics[topic].off;
    /* the non-scrolling region, then the rest */
    GetClientRect(g_frame, &r);
    if (t.nonscroll != 0xFFFFFFFFUL && t.nonscroll != t.scroll)
        view_layout(&g_vnsr, g_hlp, t.nonscroll, t.scroll, (short)r.right);
    else
        view_layout(&g_vnsr, g_hlp, 0, 0, (short)r.right);
    g_vtopic.scroll = 0;
    g_vtopic.annotated = annot_has(g_file, g_curoff);
    layout_frame();
    GetClientRect(g_topic, &r);
    view_layout(&g_vtopic, g_hlp, t.scroll, t.nexttopic ? t.nexttopic : 0, (short)r.right);
    view_setscroll(&g_vtopic);
    GetClientRect(g_topic, &r);
    if (r.right != g_vtopic.width) {
        /* the scroll bar came or went */
        view_layout(&g_vtopic, g_hlp, t.scroll, t.nexttopic ? t.nexttopic : 0, (short)r.right);
        view_setscroll(&g_vtopic);
    }
    InvalidateRect(g_topic, NULL, TRUE);
    InvalidateRect(g_nsr, NULL, TRUE);
    history_add();
    update_buttons();
    if (macros[0]) {
        LPCSTR m = macros;
        while (*m) {
            run_macros(m);
            m += lstrlen(m) + 1;
        }
    }
    return TRUE;
}

BOOL show_offset(DWORD off, BOOL record, short scroll)
{
    int t = g_hlp ? hlp_topic_at(g_hlp, off) : -1;
    if (t < 0) {
        errorbox(IDS_NOTOPIC, NULL);
        return FALSE;
    }
    if (!show_topic(t, record))
        return FALSE;
    if (scroll) {
        g_vtopic.scroll = scroll;
        view_setscroll(&g_vtopic);
        InvalidateRect(g_topic, NULL, TRUE);
    }
    return TRUE;
}

void go_contents(void)
{
    DWORD off;
    if (!g_hlp)
        return;
    off = g_contents != 0xFFFFFFFFUL ? g_contents : g_hlp->contents;
    show_offset(off, TRUE, 0);
}

void go_back(void)
{
    PLACE p;
    if (!nback)
        return;
    p = back[--nback];
    if (lstrcmpi(p.file, g_file) && !open_file(p.file, FALSE))
        return;
    show_offset(p.off, FALSE, p.scroll);
}

/* ---- secondary windows ---- */

static int window_def(HLP FAR *h, LPCSTR name)
{
    WORD i;
    for (i = 0; i < h->nwin; i++)
        if (!lstrcmpi(h->win[i].name, name))
            return i;
    return -1;
}

static BOOL show_secondary(HLP FAR *h, int topic, LPCSTR name)
{
    HLPTHDR t;
    RECT r;
    int w = window_def(h, name);
    if (!hlp_topichdr(h, topic, &t, NULL, 0, NULL, 0))
        return FALSE;
    if (!g_second || secondwin != w) {
        int sx = GetSystemMetrics(SM_CXSCREEN), sy = GetSystemMetrics(SM_CYSCREEN);
        int x = CW_USEDEFAULT, y = 0, cx = CW_USEDEFAULT, cy = 0;
        LPCSTR cap = name;
        if (g_second)
            DestroyWindow(g_second);
        if (w >= 0) {
            HLPWIN FAR *d = &h->win[w];
            /* positions are 1024ths of the screen (WH2: GLOSSARY.HLP's
               123, 123, 400, 500 is 76, 57, 250 x 234 on VGA) */
            x = (int)((long)d->x * sx / 1024);
            y = (int)((long)d->y * sy / 1024);
            cx = (int)((long)d->cx * sx / 1024);
            cy = (int)((long)d->cy * sy / 1024);
            if (d->caption[0])
                cap = d->caption;
        }
        g_second = CreateWindow("MS_WINTOPIC_SECONDARY", cap,
                                WS_OVERLAPPEDWINDOW | WS_CLIPSIBLINGS,
                                x, y, cx, cy, NULL, NULL, g_inst, NULL);
        if (!g_second)
            return FALSE;
        secondwin = w;
        g_secondnsr = CreateWindow("MS_WINNSR", "", WS_CHILD, 0, 0, 0, 0, g_second, NULL, g_inst,
                                   (LPVOID)(VIEW FAR *)&g_vsecondnsr);
        g_secondtopic = CreateWindow("MS_WINTOPIC", "", WS_CHILD | WS_VISIBLE, 0, 0, 0, 0, g_second,
                                     NULL, g_inst, (LPVOID)(VIEW FAR *)&g_vsecond);
        view_init(&g_vsecond, g_secondtopic, FALSE, FALSE);
        view_init(&g_vsecondnsr, g_secondnsr, TRUE, FALSE);
        if (w >= 0 && (h->win[w].flags & 0x0100))
            g_vsecond.bg = h->win[w].rgb;
        GetClientRect(g_second, &r);
        MoveWindow(g_secondtopic, 0, 0, r.right, r.bottom, FALSE);
        ShowWindow(g_second, SW_SHOWNORMAL);
    }
    GetClientRect(g_secondtopic, &r);
    g_vsecond.scroll = 0;
    view_layout(&g_vsecond, h, t.scroll, t.nexttopic ? t.nexttopic : 0, (short)r.right);
    view_setscroll(&g_vsecond);
    GetClientRect(g_secondtopic, &r);
    if (r.right != g_vsecond.width) {
        view_layout(&g_vsecond, h, t.scroll, t.nexttopic ? t.nexttopic : 0, (short)r.right);
        view_setscroll(&g_vsecond);
    }
    InvalidateRect(g_secondtopic, NULL, TRUE);
    return TRUE;
}

LRESULT CALLBACK __export SecondProc(HWND w, UINT msg, WPARAM wp, LPARAM lp)
{
    switch (msg) {
    case WM_SIZE:
        if (g_secondtopic)
            MoveWindow(g_secondtopic, 0, 0, LOWORD(lp), HIWORD(lp), TRUE);
        return 0;
    case WM_SETFOCUS:
        if (g_secondtopic)
            SetFocus(g_secondtopic);
        return 0;
    case WM_DESTROY:
        if (g_second == w) {
            g_second = g_secondtopic = g_secondnsr = NULL;
            view_clear(&g_vsecond);
            fonts_free(&g_vsecond);
            g_vsecond.hlp = NULL;
            secondwin = -1;
        }
        return 0;
    }
    return DefWindowProc(w, msg, wp, lp);
}

/* ---- jumps ---- */

BOOL jump_hash(HLP FAR *from, LPCSTR file, DWORD hash, LPCSTR window, BOOL popup, POINT pt)
{
    HLP FAR *h = from ? from : g_hlp;
    DWORD off;
    int t;
    if (file) {
        h = hlp_get(file);
        if (!h) {
            open_error(file);
            return FALSE;
        }
    }
    if (!h)
        return FALSE;
    if (!hlp_context_hash(h, (LONG)hash, &off) || (t = hlp_topic_at(h, off)) < 0) {
        errorbox(IDS_NOTOPIC, NULL);
        return FALSE;
    }
    if (popup) {
        popup_show(h, t, pt);
        return TRUE;
    }
    if (window && lstrcmpi(window, "main")) {
        return show_secondary(h, t, window);
    }
    if (h != g_hlp && !open_file(path_of(h), FALSE))
        return FALSE;
    return show_topic(t, TRUE);
}

BOOL jump_context(HLP FAR *from, LPCSTR file, LPCSTR ctx, LPCSTR window, BOOL popup)
{
    POINT pt;
    GetCursorPos(&pt);
    return jump_hash(from, file, (DWORD)hlp_hash(ctx), window, popup, pt);
}

/* ---- the button bar ---- */

static void clear_buttons(void)
{
    int i;
    for (i = 0; i < nbuttons; i++)
        if (buttons[i].hwnd)
            DestroyWindow(buttons[i].hwnd);
    nbuttons = 0;
    browse = FALSE;
}

static int button_find(LPCSTR name)
{
    int i;
    for (i = 0; i < nbuttons; i++)
        if (!lstrcmpi(buttons[i].name, name))
            return i;
    return -1;
}

static void place_buttons(void)
{
    int i;
    for (i = 0; i < nbuttons; i++)
        MoveWindow(buttons[i].hwnd, i * BTN_W, 0, BTN_W, BTN_H, TRUE);
}

static void new_button(WORD id, LPCSTR name, LPCSTR text, LPCSTR macro)
{
    BUTTON FAR *b;
    if (nbuttons == MAXBUTTONS || !g_icon || !buttons)
        return;
    b = &buttons[nbuttons++];
    b->id = id;
    hlp_strcpyn(b->name, name, sizeof b->name);
    hlp_strcpyn(b->macro, macro ? macro : "", sizeof b->macro);
    b->hwnd = CreateWindow("Button", text, WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
                           0, 0, BTN_W, BTN_H, g_icon, (HMENU)id, g_inst, NULL);
    if (b->hwnd && g_btnfont)
        SendMessage(b->hwnd, WM_SETFONT, (WPARAM)g_btnfont, 0);
    place_buttons();
}

/* Contents, Search, Back, History (WH1) */
static void standard_buttons(void)
{
    new_button(IDB_CONTENTS, "btn_contents", "&Contents", NULL);
    new_button(IDB_SEARCH, "btn_search", "&Search", NULL);
    new_button(IDB_BACK, "btn_back", "&Back", NULL);
    new_button(IDB_HISTORY, "btn_history", "His&tory", NULL);
}

void browse_buttons(void)
{
    if (browse)
        return;
    browse = TRUE;
    new_button(IDB_PREV, "btn_previous", "<<", NULL);
    new_button(IDB_NEXT, "btn_next", ">>", NULL);
    update_buttons();
}

void add_button(LPCSTR id, LPCSTR text, LPCSTR macro)
{
    int i = button_find(id);
    if (i >= 0) {
        SetWindowText(buttons[i].hwnd, text);
        hlp_strcpyn(buttons[i].macro, macro, sizeof buttons[i].macro);
        return;
    }
    new_button(IDB_USER + nbuttons, id, text, macro);
}

void destroy_button(LPCSTR id)
{
    int i = button_find(id), k;
    if (i < 0)
        return;
    DestroyWindow(buttons[i].hwnd);
    for (k = i + 1; k < nbuttons; k++)
        buttons[k - 1] = buttons[k];
    nbuttons--;
    place_buttons();
}

void change_button(LPCSTR id, LPCSTR macro)
{
    int i = button_find(id);
    if (i >= 0)
        hlp_strcpyn(buttons[i].macro, macro, sizeof buttons[i].macro);
}

void enable_button(LPCSTR id, BOOL on)
{
    int i = button_find(id);
    if (i >= 0)
        EnableWindow(buttons[i].hwnd, on);
}

void update_buttons(void)
{
    int i;
    for (i = 0; i < nbuttons; i++) {
        BOOL on = TRUE;
        switch (buttons[i].id) {
        case IDB_BACK: on = nback > 0; break;
        case IDB_SEARCH: on = g_hlp && g_hlp->f_kwbtree; break;
        case IDB_PREV: on = g_cur >= 0 && g_thdr.browseback != 0xFFFFFFFFUL; break;
        case IDB_NEXT: on = g_cur >= 0 && g_thdr.browsefor != 0xFFFFFFFFUL; break;
        default: continue;
        }
        if (IsWindowEnabled(buttons[i].hwnd) != on)
            EnableWindow(buttons[i].hwnd, on);
    }
    {
        HMENU m = GetMenu(g_frame);
        UINT f = g_hlp ? MF_ENABLED : MF_GRAYED;
        EnableMenuItem(m, IDM_PRINT, f);
        EnableMenuItem(m, IDM_COPY, f);
        EnableMenuItem(m, IDM_ANNOTATE, f);
        EnableMenuItem(m, IDM_DEFINE, f);
    }
}

static void button_command(WORD id)
{
    int i;
    for (i = 0; i < nbuttons; i++)
        if (buttons[i].id == id) {
            if (buttons[i].macro[0])
                run_macros(buttons[i].macro);
            break;
        }
}

LRESULT CALLBACK __export IconProc(HWND w, UINT msg, WPARAM wp, LPARAM lp)
{
    switch (msg) {
    case WM_ERASEBKGND: {
        RECT r;
        GetClientRect(w, &r);
        FillRect((HDC)wp, &r, GetStockObject(GRAY_BRUSH));
        /* a black line along the bottom, under the buttons too (WH1) */
        PatBlt((HDC)wp, 0, r.bottom - 1, r.right, 1, BLACKNESS);
        return 1;
    }
    case WM_COMMAND:
        if (HIWORD(lp) == BN_CLICKED) {
            PostMessage(g_frame, WM_COMMAND, wp, 0);
            return 0;
        }
        break;
    case WM_CTLCOLOR:
        /* the buttons' rounded corners show the bar's grey (WH1) */
        if (HIWORD(lp) == CTLCOLOR_BTN)
            return (LRESULT)GetStockObject(GRAY_BRUSH);
        break;
    }
    return DefWindowProc(w, msg, wp, lp);
}

/* ---- layout of the main window ---- */

void layout_frame(void)
{
    RECT r;
    int ih = g_hlp ? BTN_H : 0, nh = 0;
    if (!g_frame || !g_icon)
        return;
    GetClientRect(g_frame, &r);
    if (g_vnsr.first && g_vnsr.hlp) {
        if (g_vnsr.width != r.right)
            view_layout(&g_vnsr, g_vnsr.hlp, g_vnsr.first, g_vnsr.end, (short)r.right);
        nh = g_vnsr.height + 1;
    }
    MoveWindow(g_icon, 0, 0, r.right, ih, TRUE);
    MoveWindow(g_nsr, 0, ih, r.right, nh, TRUE);
    ShowWindow(g_nsr, nh ? SW_SHOWNA : SW_HIDE);
    MoveWindow(g_topic, 0, ih + nh, r.right, r.bottom - ih - nh, TRUE);
}

LRESULT CALLBACK __export NsrProc(HWND w, UINT msg, WPARAM wp, LPARAM lp)
{
    if (msg == WM_PAINT) {
        /* the region and, under it, a black line (UNVERIFIED: no 3.1
           help file of the reference install has a non-scrolling region) */
        PAINTSTRUCT ps;
        RECT r;
        VIEW *v = (VIEW *)GetWindowWord(w, 0);
        BeginPaint(w, &ps);
        GetClientRect(w, &r);
        if (v)
            view_paint(v, ps.hdc, &ps.rcPaint, 0, 0);
        PatBlt(ps.hdc, 0, r.bottom - 1, r.right, 1, BLACKNESS);
        EndPaint(w, &ps);
        return 0;
    }
    if (msg == WM_SIZE)
        return 0;       /* laid out by layout_frame */
    return TopicProc(w, msg, wp, lp);
}

/* ---- the history window ---- */

LRESULT CALLBACK __export PathProc(HWND w, UINT msg, WPARAM wp, LPARAM lp)
{
    switch (msg) {
    case WM_SIZE:
        MoveWindow(GetDlgItem(w, 1), 0, 0, LOWORD(lp), HIWORD(lp), TRUE);
        return 0;
    case WM_SETFOCUS:
        SetFocus(GetDlgItem(w, 1));
        return 0;
    case WM_COMMAND:
        if (wp == IDCANCEL) {
            DestroyWindow(w);
            return 0;
        }
        if (wp == 1 && HIWORD(lp) == LBN_DBLCLK) {
            int i = (int)SendMessage(GetDlgItem(w, 1), LB_GETCURSEL, 0, 0);
            if (i >= 0 && i < g_nhist) {
                PLACE p = g_hist[i];
                if (!lstrcmpi(p.file, g_file) || open_file(p.file, FALSE))
                    show_offset(p.off, TRUE, 0);
            }
            return 0;
        }
        break;
    case WM_VKEYTOITEM:
        if (wp == VK_RETURN) {
            PostMessage(w, WM_COMMAND, 1, MAKELONG(GetDlgItem(w, 1), LBN_DBLCLK));
            return -2;
        }
        if (wp == VK_ESCAPE) {
            DestroyWindow(w);
            return -2;
        }
        return -1;
    case WM_DESTROY: {
        RECT r;
        GetWindowRect(w, &r);
        hist_pos[0] = r.left;
        hist_pos[1] = r.top;
        hist_pos[2] = r.right - r.left;
        hist_pos[3] = r.bottom - r.top;
        g_history = NULL;
        return 0;
    }
    }
    return DefWindowProc(w, msg, wp, lp);
}

static BOOL read_pos(LPCSTR key, int *x, int *y, int *cx, int *cy, int *max)
{
    char s[64];
    LPSTR p = s;
    int v[5], i;
    if (!GetProfileString("Windows Help", key, "", s, sizeof s) || *p != '[')
        return FALSE;
    p++;
    for (i = 0; i < 5; i++) {
        int n = 0, neg = 0;
        if (*p == '-') {
            neg = 1;
            p++;
        }
        if (*p < '0' || *p > '9')
            return FALSE;
        while (*p >= '0' && *p <= '9')
            n = n * 10 + (*p++ - '0');
        v[i] = neg ? -n : n;
        if (*p == ',')
            p++;
    }
    *x = v[0];
    *y = v[1];
    *cx = v[2];
    *cy = v[3];
    *max = v[4];
    return TRUE;
}

void show_history(void)
{
    int x, y, cx, cy, k;
    HWND lb;
    char cap[64];
    if (g_history) {
        SetActiveWindow(g_history);
        return;
    }
    x = hist_pos[0];
    y = hist_pos[1];
    cx = hist_pos[2];
    cy = hist_pos[3];
    rstr(IDS_HISTORY, cap, sizeof cap);
    g_history = CreateWindow("MS_WIN_PATH", cap, WS_CAPTION | WS_SYSMENU | WS_THICKFRAME | WS_CLIPSIBLINGS,
                             x, y, cx, cy, g_frame, NULL, g_inst, NULL);
    if (!g_history)
        return;
    lb = CreateWindow("ListBox", "", WS_CHILD | WS_VISIBLE | LBS_NOTIFY | LBS_NOINTEGRALHEIGHT |
                      LBS_WANTKEYBOARDINPUT, 0, 0, 0, 0, g_history, (HMENU)1, g_inst, NULL);
    SendMessage(lb, WM_SETFONT, (WPARAM)g_btnfont, 0);
    {
        RECT r;
        GetClientRect(g_history, &r);
        MoveWindow(lb, 0, 0, r.right, r.bottom, FALSE);
    }
    for (k = 0; k < g_nhist; k++)
        SendMessage(lb, LB_ADDSTRING, 0, (LPARAM)(LPSTR)g_hist[k].title);
    SendMessage(lb, LB_SETCURSEL, g_nhist > 1 ? 1 : 0, 0);
    ShowWindow(g_history, SW_SHOWNORMAL);
}

/* ---- Always on Top ---- */

void set_ontop(BOOL on)
{
    g_ontop = on;
    SetWindowPos(g_frame, on ? HWND_TOPMOST : HWND_NOTOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE);
    CheckMenuItem(GetMenu(g_frame), IDM_ONTOP, on ? MF_CHECKED : MF_UNCHECKED);
}

/* ---- File > Open ---- */

void file_open_dialog(void)
{
    static char filter[128], title[32];
    char file[PATHMAX];
    OPENFILENAME o;
    int i, n = rstr(IDS_FILTER, filter, sizeof filter);
    for (i = 0; i < n; i++)
        if (filter[i] == '|')
            filter[i] = 0;
    rstr(IDS_OPENTITLE, title, sizeof title);
    lstrcpy(file, "*.hlp");
    m_set(&o, 0, sizeof o);
    o.lStructSize = sizeof o;
    o.hwndOwner = g_frame;
    o.lpstrFilter = filter;
    o.nFilterIndex = 1;
    o.lpstrFile = file;
    o.nMaxFile = sizeof file;
    o.lpstrTitle = title;
    o.lpstrDefExt = "hlp";
    o.Flags = OFN_HIDEREADONLY | OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST;
    if (!GetOpenFileName(&o))
        return;
    if (open_file(file, FALSE))
        go_contents();
}

/* ---- commands ---- */

void do_command(UINT id)
{
    switch (id) {
    case IDM_OPEN: file_open_dialog(); break;
    case IDM_PRINT: print_topic(); break;
    case IDM_PRINTSETUP: print_setup(); break;
    case IDM_EXIT: DestroyWindow(g_frame); break;
    case IDM_COPY: copy_dialog(); break;
    case IDM_ANNOTATE: annotate(); break;
    case IDM_DEFINE: bookmark_define(); break;
    case IDM_MORE: bookmark_more(); break;
    case IDM_HELPHELP: WinHelp(g_frame, NULL, HELP_HELPONHELP, 0); break;
    case IDM_ONTOP: set_ontop(!g_ontop); break;
    case IDM_ABOUT: {
        char o[80], a[32];
        rstr(IDS_ABOUTOTHER, o, sizeof o);
        rstr(IDS_ABOUTAPP, a, sizeof a);
        ShellAbout(g_frame, a, o, LoadIcon(g_inst, MAKEINTRESOURCE(IDI_WINHELP)));
        break;
    }
    case IDB_CONTENTS: go_contents(); break;
    case IDB_SEARCH: search_dialog(NULL); break;
    case IDB_BACK: go_back(); break;
    case IDB_HISTORY: show_history(); break;
    case IDB_PREV:
        if (g_cur >= 0 && g_thdr.browseback != 0xFFFFFFFFUL)
            show_offset(g_thdr.browseback, TRUE, 0);
        break;
    case IDB_NEXT:
        if (g_cur >= 0 && g_thdr.browsefor != 0xFFFFFFFFUL)
            show_offset(g_thdr.browsefor, TRUE, 0);
        break;
    default:
        if (id >= IDM_BOOKMARK0 && id < IDM_BOOKMARK0 + 9)
            bookmark_go(id - IDM_BOOKMARK0);
        else if (id >= IDB_USER && id < IDB_USER + MAXBUTTONS)
            button_command((WORD)id);
        else
            menu_command(id);
        break;
    }
    if (g_topic && GetActiveWindow() == g_frame)
        SetFocus(g_topic);
}

/* a button's mnemonic typed anywhere in the main window (3.1: plain or
   with Alt) */
static BOOL button_key(char c)
{
    int i;
    c = (char)(DWORD)AnsiUpper((LPSTR)(DWORD)(BYTE)c);
    for (i = 0; i < nbuttons; i++) {
        char t[64];
        LPSTR p;
        GetWindowText(buttons[i].hwnd, t, sizeof t);
        for (p = t; *p; p++)
            if (*p == '&' && p[1]) {
                if ((char)(DWORD)AnsiUpper((LPSTR)(DWORD)(BYTE)p[1]) == c) {
                    if (IsWindowEnabled(buttons[i].hwnd))
                        PostMessage(g_frame, WM_COMMAND, buttons[i].id, 0);
                    return TRUE;
                }
                break;
            }
    }
    return FALSE;
}

/* at exit (WH2, WS01-WS04): M_WindowPosition only when the window is not
   where it was read from (or the default: a second instance's cascade
   counts as a move), then H_WindowPosition always, the History window's
   last place or its default */
static void save_position(void)
{
    WINDOWPLACEMENT wp;
    char s[64];
    wp.length = sizeof wp;
    if (GetWindowPlacement(g_frame, &wp)) {
        int v[5];
        v[0] = wp.rcNormalPosition.left;
        v[1] = wp.rcNormalPosition.top;
        v[2] = wp.rcNormalPosition.right - wp.rcNormalPosition.left;
        v[3] = wp.rcNormalPosition.bottom - wp.rcNormalPosition.top;
        v[4] = IsZoomed(g_frame) ? 1 : 0;
        if (v[0] != start_pos[0] || v[1] != start_pos[1] || v[2] != start_pos[2] ||
            v[3] != start_pos[3] || v[4] != start_pos[4]) {
            wsprintf(s, "[%d,%d,%d,%d,%d]", v[0], v[1], v[2], v[3], v[4]);
            WriteProfileString("Windows Help", "M_WindowPosition", s);
        }
    }
    /* (not when no help file was ever opened: WS04) */
    if (ever_opened) {
        wsprintf(s, "[%d,%d,%d,%d,0]", hist_pos[0], hist_pos[1], hist_pos[2], hist_pos[3]);
        WriteProfileString("Windows Help", "H_WindowPosition", s);
    }
}

LRESULT CALLBACK __export FrameProc(HWND w, UINT msg, WPARAM wp, LPARAM lp)
{
    if (msg == g_wmwinhelp && msg)
        return api_request((HWND)wp, (HGLOBAL)LOWORD(lp));
    switch (msg) {
    case WM_SIZE:
        if (wp != SIZE_MINIMIZED)
            layout_frame();
        return 0;
    case WM_SETFOCUS:
        if (g_topic)
            SetFocus(g_topic);
        return 0;
    case WM_COMMAND:
        do_command(wp);
        return 0;
    case WM_INITMENU:
        update_buttons();
        return 0;
    case WM_SYSCHAR:
        if (button_key((char)wp))
            return 0;
        break;
    case WM_CHAR:
        if (button_key((char)wp))
            return 0;
        break;
    case WM_DESTROY:
        save_position();
        popup_close();
        if (g_second)
            DestroyWindow(g_second);
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProc(w, msg, wp, lp);
}

/* the topic window passes characters on to the frame (button keys) */
LRESULT CALLBACK __export MainTopicProc(HWND w, UINT msg, WPARAM wp, LPARAM lp)
{
    if (msg == WM_CHAR || msg == WM_SYSCHAR) {
        if (button_key((char)wp))
            return 0;
    }
    /* no file: the logo over black (WH1; w16's own picture) */
    if (w == g_topic && !g_hlp && (msg == WM_PAINT || msg == WM_ERASEBKGND)) {
        if (msg == WM_ERASEBKGND)
            return 1;
        {
            PAINTSTRUCT ps;
            RECT r;
            int i;
            BeginPaint(w, &ps);
            GetClientRect(w, &r);
            PatBlt(ps.hdc, 0, 0, r.right, r.bottom, BLACKNESS);
            /* the dither's ones navy, then the letters' ones white */
            for (i = 0; i < 2; i++) {
                HBITMAP bm = LoadBitmap(g_inst, MAKEINTRESOURCE(i ? IDB_LOGO2 : IDB_LOGO));
                HDC m;
                HBITMAP o;
                BITMAP b;
                if (!bm)
                    continue;
                m = CreateCompatibleDC(ps.hdc);
                o = SelectObject(m, bm);
                GetObject(bm, sizeof b, &b);
                SetTextColor(ps.hdc, RGB(0, 0, 0));
                SetBkColor(ps.hdc, i ? RGB(255, 255, 255) : RGB(0, 0, 128));
                BitBlt(ps.hdc, 0, 0, b.bmWidth, b.bmHeight, m, 0, 0, i ? SRCPAINT : SRCCOPY);
                SelectObject(m, o);
                DeleteDC(m);
                DeleteObject(bm);
            }
            EndPaint(w, &ps);
            return 0;
        }
    }
    return TopicProc(w, msg, wp, lp);
}

/* ---- start ---- */

static BOOL register_classes(void)
{
    WNDCLASS wc;
    HICON icon = LoadIcon(g_inst, MAKEINTRESOURCE(IDI_WINHELP));
    m_set(&wc, 0, sizeof wc);
    wc.hInstance = g_inst;
    wc.hCursor = LoadCursor(NULL, IDC_ARROW);
    wc.cbWndExtra = 6;
    wc.lpfnWndProc = FrameProc;
    wc.hIcon = icon;
    wc.lpszClassName = "MS_WINDOC";
    RegisterClass(&wc);
    wc.lpszClassName = "MS_WINHELP";
    RegisterClass(&wc);
    wc.hIcon = NULL;
    wc.lpfnWndProc = MainTopicProc;
    wc.lpszClassName = "MS_WINTOPIC";
    RegisterClass(&wc);
    wc.lpfnWndProc = NsrProc;
    wc.lpszClassName = "MS_WINNSR";
    RegisterClass(&wc);
    wc.lpfnWndProc = IconProc;
    wc.lpszClassName = "MS_WINICON";
    RegisterClass(&wc);
    wc.lpfnWndProc = NoteProc;
    wc.lpszClassName = "MS_WINNOTE";
    wc.style = CS_SAVEBITS;
    RegisterClass(&wc);
    wc.style = CS_HREDRAW | CS_VREDRAW;
    wc.lpfnWndProc = PathProc;
    wc.cbWndExtra = 0;
    wc.hbrBackground = (HBRUSH)(COLOR_WINDOW + 1);
    wc.hIcon = icon;
    wc.lpszClassName = "MS_WIN_PATH";
    RegisterClass(&wc);
    wc.hbrBackground = NULL;
    wc.cbWndExtra = 6;
    wc.lpfnWndProc = SecondProc;
    wc.lpszClassName = "MS_WINTOPIC_SECONDARY";
    RegisterClass(&wc);
    return TRUE;
}

/* the command line: [-x] [-k keyword | -i context | -n number] [file] */
static void parse_cmdline(LPSTR cmd, LPSTR file, char *opt, LPSTR arg)
{
    *file = 0;
    *opt = 0;
    *arg = 0;
    for (;;) {
        while (*cmd == ' ' || *cmd == '\t')
            cmd++;
        if (!*cmd)
            break;
        if (*cmd == '-' || *cmd == '/') {
            char c = cmd[1];
            if (c >= 'A' && c <= 'Z')
                c += 32;
            cmd += 2;
            if (c == 'x') {
                g_api = TRUE;
                continue;
            }
            if (c == 'k' || c == 'i' || c == 'n') {
                int n = 0;
                *opt = c;
                while (*cmd == ' ')
                    cmd++;
                while (*cmd && *cmd != ' ' && n < 79)
                    arg[n++] = *cmd++;
                arg[n] = 0;
            }
            while (*cmd && *cmd != ' ')
                cmd++;
            continue;
        }
        {
            int n = 0;
            while (*cmd && *cmd != ' ' && *cmd != '\t' && n < PATHMAX - 1)
                file[n++] = *cmd++;
            file[n] = 0;
        }
    }
}

int PASCAL WinMain(HINSTANCE inst, HINSTANCE prev, LPSTR cmd, int show)
{
    MSG m;
    int x, y, cx, cy, mx, k;
    char file[PATHMAX], opt, arg[80];
    LOGFONT lf;
    (void)prev;
    g_inst = inst;
    g_hist = (PLACE FAR *)hio_alloc(sizeof(PLACE) * MAXHIST);
    back = (PLACE FAR *)hio_alloc(sizeof(PLACE) * MAXBACK);
    buttons = (BUTTON FAR *)hio_alloc(sizeof(BUTTON) * MAXBUTTONS);
    if (!g_hist || !back || !buttons)
        return 0;
    rstr(IDS_APPNAME, appname, sizeof appname);
    parse_cmdline(cmd, file, &opt, arg);
    register_classes();
    g_wmwinhelp = RegisterWindowMessage("WM_WINHELP");
    g_hand = LoadCursor(inst, MAKEINTRESOURCE(IDCUR_HAND));
    m_set(&lf, 0, sizeof lf);
    lf.lfHeight = -10;
    lf.lfWeight = FW_BOLD;
    lf.lfPitchAndFamily = VARIABLE_PITCH | FF_SWISS;
    lstrcpy(lf.lfFaceName, "Helv");
    g_btnfont = CreateFontIndirect(&lf);
    if (!read_pos("M_WindowPosition", &x, &y, &cx, &cy, &mx)) {
        int sx = GetSystemMetrics(SM_CXSCREEN), sy = GetSystemMetrics(SM_CYSCREEN);
        x = sx / 3;
        y = 2;
        cx = sx - x - 2;
        cy = sy - 4;
        mx = 0;
    }
    start_pos[0] = x;
    start_pos[1] = y;
    start_pos[2] = cx;
    start_pos[3] = cy;
    start_pos[4] = mx;
    if (!read_pos("H_WindowPosition", &hist_pos[0], &hist_pos[1], &hist_pos[2], &hist_pos[3], &k)) {
        int sx = GetSystemMetrics(SM_CXSCREEN), sy = GetSystemMetrics(SM_CYSCREEN);
        hist_pos[0] = sx / 3;
        hist_pos[1] = sy / 3;
        hist_pos[2] = sx / 3;
        hist_pos[3] = sy / 3;
    }
    /* another Help window up: this one 24 pixels right and down */
    if (FindWindow("MS_WINDOC", NULL) || FindWindow("MS_WINHELP", NULL)) {
        x += 24;
        y += 24;
    }
    g_frame = CreateWindow(g_api ? "MS_WINHELP" : "MS_WINDOC", appname,
                           WS_OVERLAPPEDWINDOW | WS_CLIPCHILDREN, x, y, cx, cy,
                           NULL, LoadMenu(inst, MAKEINTRESOURCE(1)), inst, NULL);
    if (!g_frame)
        return 0;
    g_icon = CreateWindow("MS_WINICON", "", WS_CHILD | WS_VISIBLE | WS_CLIPSIBLINGS, 0, 0, 0, 0,
                          g_frame, NULL, inst, NULL);
    g_nsr = CreateWindow("MS_WINNSR", "", WS_CHILD, 0, 0, 0, 0, g_frame, NULL, inst,
                         (LPVOID)(VIEW FAR *)&g_vnsr);
    g_topic = CreateWindow("MS_WINTOPIC", "", WS_CHILD | WS_VISIBLE, 0, 0, 0, 0, g_frame, NULL, inst,
                           (LPVOID)(VIEW FAR *)&g_vtopic);
    view_init(&g_vtopic, g_topic, FALSE, FALSE);
    view_init(&g_vnsr, g_nsr, TRUE, FALSE);
    layout_frame();
    update_buttons();
    if (!g_api) {
        /* by hand: shown at once, with the file's contents */
        ShowWindow(g_frame, mx ? SW_SHOWMAXIMIZED : show);
        UpdateWindow(g_frame);
        if (file[0] && open_file(file, FALSE)) {
            BOOL ok = FALSE;
            if (opt == 'k')
                ok = key_jump(arg, FALSE);
            else if (opt == 'i') {
                POINT pt;
                pt.x = pt.y = 0;
                ok = jump_hash(g_hlp, NULL, (DWORD)hlp_hash(arg), NULL, FALSE, pt);
            } else if (opt == 'n') {
                DWORD off, id = 0;
                LPSTR p;
                for (p = arg; *p >= '0' && *p <= '9'; p++)
                    id = id * 10 + (*p - '0');
                if (hlp_mapid(g_hlp, id, &off))
                    ok = show_offset(off, FALSE, 0);
            }
            if (!ok)
                go_contents();
        }
    }
    while (GetMessage(&m, NULL, 0, 0)) {
        if (g_history && IsDialogMessage(g_history, &m))
            continue;
        TranslateMessage(&m);
        DispatchMessage(&m);
    }
    {
        int i;
        for (i = 0; i < NOPEN; i++)
            if (opened[i].h)
                hlp_close(opened[i].h);
    }
    if (g_btnfont)
        DeleteObject(g_btnfont);
    return m.wParam;
}
