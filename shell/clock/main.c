/*
 * main.c - Clock's window, menus, timer and CLOCK.INI.
 *
 * Observed on Windows 3.1 (SNAP.EXE dumps and screenshots, the CLOCK.INI
 * the original writes; ARCHITECTURE.md 15.x):
 * - One window of class "Clock", style WS_OVERLAPPEDWINDOW |
 *   WS_CLIPSIBLINGS; with no title it is WS_BORDER | WS_THICKFRAME and has
 *   no menu, its window rectangle unchanged. Double-clicking the clock
 *   switches between the two; without a title the client area answers
 *   WM_NCHITTEST with HTCAPTION, so it can be dragged.
 * - Settings menu: Analog / Digital (checked), Set Font... (grayed in
 *   analog), No Title, Seconds, Date (checked), About Clock... . The
 *   system menu gets a separator and "Always on Top" (command 5,
 *   checked when on; the window is then WS_EX_TOPMOST).
 * - The title is "Clock" in digital, "Clock - <date>" in analog, and
 *   "Clock - <date without the year>" minimized.
 * - CLOCK.INI (in the Windows directory), section [Clock], written on
 *   exit in this order: Maximized=0|1, Options=analog,minimized,
 *   noseconds,notitle,topmost,nodate (0|1 each), Position=left,top,
 *   right,bottom (the restored window rectangle). sFont=<face> is written
 *   when a font is chosen. Without a CLOCK.INI the clock is digital at
 *   0,0,208,226, with seconds and date.
 * - When no timer is left, the original says "Not enough timing
 *   resources for Clock. Close other applications and try again." in a
 *   system-modal box titled Clock and does not start.
 * - A second Clock activates the first and ends.
 * - The timer ticks every second; minimized the clock shows minutes and
 *   is redrawn far less often (UNVERIFIED: here once a minute).
 */
#include "clock.h"

HINSTANCE g_hinst;
HWND g_wnd;
OPTIONS g_opt;
LOGFONT g_lf;
HBRUSH g_brshadow, g_brface, g_brteal;
HPEN g_pnhand, g_pnshadow, g_pnface, g_pncyan, g_pnred;
int g_aspv, g_asph;

static char s_class[] = "Clock";
static char s_app[] = "Clock";
static char s_ini[] = "clock.ini";
static char s_sect[] = "Clock";
static HMENU menu;
static CLOCKTIME now;
static BOOL started;                    /* the window is up: save on exit */
static BOOL timer_icon;                 /* the timer runs at the icon rate */

#define TIMER_ID 1

/* ---- the time from DOS ---- */

static DWORD dos_time(void);
#pragma aux dos_time = "mov ah, 2Ch" "int 21h" value [cx dx] modify [ax];
static DWORD dos_date(void);
#pragma aux dos_date = "mov ah, 2Ah" "int 21h" value [cx dx] modify [ax];

void time_now(CLOCKTIME *t)
{
    DWORD tm = dos_time(), d = dos_date();
    t->hour = HIBYTE(HIWORD(tm));
    t->minute = LOBYTE(HIWORD(tm));
    t->second = HIBYTE(LOWORD(tm));
    t->year = (int)HIWORD(d);
    t->month = HIBYTE(LOWORD(d));
    t->day = LOBYTE(LOWORD(d));
}

/* ---- colours ---- */

static void make_colors(void)
{
    g_brshadow = CreateSolidBrush(GetSysColor(COLOR_BTNSHADOW));
    g_brface = CreateSolidBrush(GetSysColor(COLOR_BTNFACE));
    g_brteal = CreateSolidBrush(RGB(0, 128, 128));
    g_pnhand = CreatePen(PS_SOLID, 1, GetSysColor(COLOR_WINDOWTEXT));
    g_pnshadow = CreatePen(PS_SOLID, 1, GetSysColor(COLOR_BTNSHADOW));
    g_pnface = CreatePen(PS_SOLID, 1, GetSysColor(COLOR_BTNFACE));
    g_pncyan = CreatePen(PS_SOLID, 1, RGB(0, 255, 255));
    g_pnred = CreatePen(PS_SOLID, 1, RGB(255, 0, 0));
}

static void free_colors(void)
{
    DeleteObject(g_brshadow);
    DeleteObject(g_brface);
    DeleteObject(g_brteal);
    DeleteObject(g_pnhand);
    DeleteObject(g_pnshadow);
    DeleteObject(g_pnface);
    DeleteObject(g_pncyan);
    DeleteObject(g_pnred);
}

static void erase(HDC dc)
{
    RECT rc;
    HBRUSH b;
    SetBkMode(dc, OPAQUE);
#ifndef W16_WIN2
    b = CreateSolidBrush(GetNearestColor(dc, GetSysColor(COLOR_BTNFACE)));
#else
    b = CreateSolidBrush(GetSysColor(COLOR_BTNFACE));   /* 2.x: nearest is white */
#endif
    GetClientRect(g_wnd, &rc);
    FillRect(dc, &rc, b);
    DeleteObject(b);
}

/* ---- CLOCK.INI ---- */

static void load_settings(RECT *pos, BOOL *havepos, BOOL *maximized)
{
    char buf[80];
    const char FAR *p;
    int v[6], i;

    for (i = 0; i < 6; i++)
        v[i] = 0;
    GetPrivateProfileString(s_sect, "Options", "", buf, sizeof buf, s_ini);
    p = buf;
    for (i = 0; i < 6; i++) {
        int n = s_atoi(&p);
        if (n < 0)
            break;
        v[i] = n;
    }
    g_opt.analog = v[0] != 0;
    g_opt.minimized = v[1] != 0;
    g_opt.noseconds = v[2] != 0;
    g_opt.notitle = v[3] != 0;
    g_opt.topmost = v[4] != 0;
    g_opt.nodate = v[5] != 0;
    *maximized = GetPrivateProfileInt(s_sect, "Maximized", 0, s_ini) != 0;
    *havepos = FALSE;
    if (GetPrivateProfileString(s_sect, "Position", "", buf, sizeof buf, s_ini)) {
        int r[4];
        p = buf;
        for (i = 0; i < 4 && (r[i] = s_atoi(&p)) != -1; i++)
            ;
        if (i == 4) {
            pos->left = r[0];
            pos->top = r[1];
            pos->right = r[2];
            pos->bottom = r[3];
            *havepos = TRUE;
        }
    }
    /* the digital font: Arial unless CLOCK.INI names another
       (UNVERIFIED: what the original takes from a face read back) */
    g_lf.lfWeight = FW_NORMAL;
    g_lf.lfPitchAndFamily = VARIABLE_PITCH | FF_SWISS;
#ifndef W16_WIN2
    s_cpy(g_lf.lfFaceName, "arial", LF_FACESIZE);
#else
    s_cpy(g_lf.lfFaceName, "Helv", LF_FACESIZE);   /* 2.x has no Arial */
#endif
    if (GetPrivateProfileString(s_sect, "sFont", "", buf, sizeof buf, s_ini))
        s_cpy(g_lf.lfFaceName, buf, LF_FACESIZE);
}

static void put_num(char *b, int *n, int v)
{
    if (v < 0) {
        b[(*n)++] = '-';
        v = -v;
    }
    *n += s_utoa(b + *n, (unsigned)v);
}

static void save_settings(void)
{
    char buf[80];
    int n = 0, i;
    int v[6];
    RECT rc;

    WritePrivateProfileString(s_sect, "Maximized", IsZoomed(g_wnd) ? "1" : "0", s_ini);
    v[0] = g_opt.analog;
    v[1] = IsIconic(g_wnd) != 0;
    v[2] = g_opt.noseconds;
    v[3] = g_opt.notitle;
    v[4] = g_opt.topmost;
    v[5] = g_opt.nodate;
    for (i = 0; i < 6; i++) {
        if (i)
            buf[n++] = ',';
        put_num(buf, &n, v[i]);
    }
    buf[n] = 0;
    WritePrivateProfileString(s_sect, "Options", buf, s_ini);
#ifdef W16_WIN2
    ck_restored_rect(g_wnd, &rc);
#else
    {
        WINDOWPLACEMENT wp;
        wp.length = sizeof wp;
        GetWindowPlacement(g_wnd, &wp);
        rc = wp.rcNormalPosition;
    }
#endif
    n = 0;
    put_num(buf, &n, rc.left);
    buf[n++] = ',';
    put_num(buf, &n, rc.top);
    buf[n++] = ',';
    put_num(buf, &n, rc.right);
    buf[n++] = ',';
    put_num(buf, &n, rc.bottom);
    buf[n] = 0;
    WritePrivateProfileString(s_sect, "Position", buf, s_ini);
}

/* ---- title, menus ---- */

static void set_title(void)
{
    char t[48], d[32];
    s_cpy(t, s_app, sizeof t);
    if (IsIconic(g_wnd) || (g_opt.analog && !g_opt.nodate)) {   /* observed (CK4) */
        intl_date(&now, d, !IsIconic(g_wnd));
        s_cat(t, " - ", sizeof t);
        s_cat(t, d, sizeof t);
    }
    GetWindowText(g_wnd, d, sizeof d);
    {
        int i = 0;
        while (d[i] && d[i] == t[i])
            i++;
        if (d[i] != t[i])
            SetWindowText(g_wnd, t);
    }
}

static void set_menu_checks(void)
{
    CheckMenuItem(menu, IDM_ANALOG, g_opt.analog ? MF_CHECKED : MF_UNCHECKED);
    CheckMenuItem(menu, IDM_DIGITAL, g_opt.analog ? MF_UNCHECKED : MF_CHECKED);
    EnableMenuItem(menu, IDM_SETFONT, g_opt.analog ? MF_GRAYED | MF_DISABLED : MF_ENABLED);
    CheckMenuItem(menu, IDM_SECONDS, g_opt.noseconds ? MF_UNCHECKED : MF_CHECKED);
    CheckMenuItem(menu, IDM_DATE, g_opt.nodate ? MF_UNCHECKED : MF_CHECKED);
#ifndef W16_WIN2
    CheckMenuItem(GetSystemMenu(g_wnd, FALSE), IDM_TOPMOST,
                  g_opt.topmost ? MF_CHECKED : MF_UNCHECKED);
#endif
}

#define TITLE_STYLES (WS_DLGFRAME | WS_SYSMENU | WS_MINIMIZEBOX | WS_MAXIMIZEBOX)

static void set_notitle(BOOL on)
{
    LONG st = GetWindowLong(g_wnd, GWL_STYLE);
    g_opt.notitle = on;
    if (on) {
        SetWindowLong(g_wnd, GWL_STYLE, st & ~TITLE_STYLES);
        SetMenu(g_wnd, NULL);
    } else {
        SetWindowLong(g_wnd, GWL_STYLE, st | TITLE_STYLES);
        SetMenu(g_wnd, menu);
    }
    SetWindowPos(g_wnd, NULL, 0, 0, 0, 0,
                 SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_DRAWFRAME);
}

static void set_topmost(BOOL on)
{
    g_opt.topmost = on;
#ifndef W16_WIN2
    SetWindowPos(g_wnd, on ? HWND_TOPMOST : HWND_NOTOPMOST, 0, 0, 0, 0,
                 SWP_NOMOVE | SWP_NOSIZE);
#endif
    set_menu_checks();
}

static void start_timer(void)
{
    BOOL ic = IsIconic(g_wnd);
    if (ic != timer_icon || !started) {
        KillTimer(g_wnd, TIMER_ID);
        timer_icon = ic;
        SetTimer(g_wnd, TIMER_ID, ic ? 60000u : 1000u, NULL);
    }
}

static void relayout(void)
{
    digi_reset();
    InvalidateRect(g_wnd, NULL, FALSE);   /* WM_PAINT erases (observed: once) */
}

static void about(void)
{
#ifdef W16_WIN2
    ck_about(g_wnd);
#else
    ShellAbout(g_wnd, s_app, "", LoadIcon(g_hinst, MAKEINTRESOURCE(IDI_CLOCK)));
#endif
}

static void command(UINT id)
{
    switch (id) {
    case IDM_ANALOG:
    case IDM_DIGITAL:
        if (g_opt.analog != (id == IDM_ANALOG)) {
            g_opt.analog = id == IDM_ANALOG;
            set_menu_checks();
            set_title();
            relayout();
        }
        break;
    case IDM_SETFONT:
        if (font_choose(g_wnd)) {
            WritePrivateProfileString(s_sect, "sFont", g_lf.lfFaceName, s_ini);
            relayout();
        }
        break;
    case IDM_NOTITLE:
        set_notitle(!g_opt.notitle);
        break;
    case IDM_SECONDS:
        g_opt.noseconds = !g_opt.noseconds;
        set_menu_checks();
        relayout();
        break;
    case IDM_DATE:
        /* analog: only the title changes (observed: no repaint) */
        g_opt.nodate = !g_opt.nodate;
        set_menu_checks();
        set_title();
        if (!g_opt.analog)
            relayout();
        break;
    case IDM_ABOUT:
        about();
        break;
    }
}

/* ---- painting ---- */

static void paint(HDC dc)
{
    time_now(&now);                     /* the timer may be a minute away */
    set_title();
    if (IsIconic(g_wnd)) {
        if (g_opt.analog) {
            SetBkMode(dc, TRANSPARENT);
            face_icon(dc, &now);
        } else {
            digi_paint(dc, &now, TRUE);
        }
    } else if (g_opt.analog) {
        SetBkMode(dc, TRANSPARENT);
        face_paint(dc, &now);
    } else {
        digi_paint(dc, &now, TRUE);
    }
}

static void tick(void)
{
    CLOCKTIME old = now;
    HDC dc;
    time_now(&now);
    if (old.second == now.second && old.minute == now.minute && old.hour == now.hour &&
        old.day == now.day)
        return;
    if (old.day != now.day || old.month != now.month || old.year != now.year) {
        set_title();
        if (!g_opt.analog) {
            relayout();
            return;
        }
    }
    if (IsIconic(g_wnd)) {
        if (old.minute != now.minute || old.hour != now.hour)
            InvalidateRect(g_wnd, NULL, TRUE);
        return;
    }
    if (!g_opt.analog && !digi_changed(&now))
        return;
    if (g_opt.analog && g_opt.noseconds && old.minute == now.minute && old.hour == now.hour)
        return;
    dc = GetDC(g_wnd);
    if (g_opt.analog) {
        SetBkMode(dc, TRANSPARENT);
        face_tick(dc, &old, &now);
    } else {
        digi_paint(dc, &now, FALSE);
    }
    ReleaseDC(g_wnd, dc);
}

/* ---- the window ---- */

LRESULT FAR PASCAL __export ClockWndProc(HWND w, UINT msg, WPARAM wp, LPARAM lp)
{
    switch (msg) {
    case WM_CREATE:
        g_wnd = w;
        return 0;
    case WM_ERASEBKGND:
        erase((HDC)wp);
        return 1;
    case WM_PAINT: {
        PAINTSTRUCT ps;
        BeginPaint(w, &ps);
        erase(ps.hdc);
        paint(ps.hdc);
        EndPaint(w, &ps);
        return 0;
    }
    case WM_MOVE:
#ifdef W16_WIN2
        ck_track_rect(w);
#endif
        break;
    case WM_SIZE:
#ifdef W16_WIN2
        ck_track_rect(w);
#endif
        face_size(LOWORD(lp), HIWORD(lp));
        digi_size(NULL, LOWORD(lp), HIWORD(lp));
        if (started) {
            set_title();
            start_timer();
        }
        break;
    case WM_TIMER:
        tick();
        return 0;
    case WM_COMMAND:
        command(wp);
        return 0;
    case WM_SYSCOMMAND:
        if (wp == IDM_TOPMOST) {
            set_topmost(!g_opt.topmost);
            return 0;
        }
        break;
    case WM_NCHITTEST:
        if (g_opt.notitle && !IsIconic(w)) {
            LRESULT r = DefWindowProc(w, msg, wp, lp);
            return r == HTCLIENT ? HTCAPTION : r;
        }
        break;
    case WM_LBUTTONDBLCLK:
        if (!IsIconic(w))
            set_notitle(!g_opt.notitle);
        return 0;
    case WM_NCLBUTTONDBLCLK:
        if (g_opt.notitle && wp == HTCAPTION && !IsIconic(w)) {
            set_notitle(FALSE);
            return 0;
        }
        break;
    case WM_WININICHANGE:
        intl_load();
        set_title();
        relayout();
        return 0;
    case WM_SYSCOLORCHANGE:
        free_colors();
        make_colors();
        relayout();
        return 0;
    case WM_DESTROY:
        KillTimer(w, TIMER_ID);
        if (started)
            save_settings();
        digi_free();
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProc(w, msg, wp, lp);
}

static void screen_aspect(void)
{
    HDC dc = GetDC(NULL);
    int vr = GetDeviceCaps(dc, VERTRES), hr = GetDeviceCaps(dc, HORZRES);
    int vs = GetDeviceCaps(dc, VERTSIZE), hs = GetDeviceCaps(dc, HORZSIZE);
    ReleaseDC(NULL, dc);
    g_aspv = MulDiv(vr, 100, vs);
    g_asph = MulDiv(hr, 100, hs);
}

int PASCAL WinMain(HINSTANCE inst, HINSTANCE prev, LPSTR cmd, int show)
{
    WNDCLASS wc;
    MSG msg;
    RECT pos;
    BOOL havepos, maximized;
    DWORD style;
    char text[100];
    (void)cmd;

    if (prev) {
        /* activate the running Clock (UNVERIFIED: how the original does) */
        HWND h = FindWindow(s_class, NULL);
        if (h) {
            if (IsIconic(h))
                ShowWindow(h, SW_SHOWNORMAL);
            else
                BringWindowToTop(h);
        }
        return 0;
    }
    g_hinst = inst;
    screen_aspect();
    make_colors();
    face_init();
    intl_load();
    load_settings(&pos, &havepos, &maximized);

    wc.style = CS_HREDRAW | CS_VREDRAW | CS_DBLCLKS;
    wc.lpfnWndProc = ClockWndProc;
    wc.cbClsExtra = 0;
    wc.cbWndExtra = 0;
    wc.hInstance = inst;
    wc.hIcon = NULL;                    /* the clock draws its own icon */
    wc.hCursor = LoadCursor(NULL, IDC_ARROW);
    wc.hbrBackground = NULL;
    wc.lpszMenuName = NULL;
    wc.lpszClassName = s_class;
    if (!RegisterClass(&wc))
        return 0;
    menu = ck_load_menu(inst, MAKEINTRESOURCE(IDM_MENU));

    if (!havepos) {
        /* UNVERIFIED: the original's default is 0,0,208,226 at 640x480,
           a 200 x 180 client area */
        pos.left = 0;
        pos.top = 0;
        pos.right = 200 + 2 * GetSystemMetrics(SM_CXFRAME);
        pos.bottom = 180 + 2 * GetSystemMetrics(SM_CYFRAME) +
                     GetSystemMetrics(SM_CYCAPTION) + GetSystemMetrics(SM_CYMENU);
    }
    style = WS_OVERLAPPEDWINDOW | WS_CLIPSIBLINGS;
    if (g_opt.notitle)
        style = (style & ~TITLE_STYLES) | WS_BORDER;
    time_now(&now);
    g_wnd = CreateWindow(s_class, s_app, style, pos.left, pos.top,
                         pos.right - pos.left, pos.bottom - pos.top, NULL,
                         g_opt.notitle ? NULL : menu, inst, NULL);
    if (!g_wnd)
        return 0;
    if (!SetTimer(g_wnd, TIMER_ID, 1000, NULL)) {
        LoadString(inst, IDS_NOTIMER, text, sizeof text);
        MessageBox(NULL, text, s_app, MB_OK | MB_ICONHAND | MB_SYSTEMMODAL);
        DestroyWindow(g_wnd);
        return 0;
    }
#ifndef W16_WIN2
    {
        HMENU sys = GetSystemMenu(g_wnd, FALSE);
        LoadString(inst, IDS_TOPMOST, text, sizeof text);
        AppendMenu(sys, MF_SEPARATOR, 0, NULL);
        AppendMenu(sys, MF_STRING, IDM_TOPMOST, text);
    }
#endif
    set_menu_checks();
    set_title();
    if (g_opt.topmost)
        set_topmost(TRUE);
    if (g_opt.minimized)
        show = SW_SHOWMINNOACTIVE;
    else if (maximized)
        show = SW_SHOWMAXIMIZED;
    started = TRUE;
    ShowWindow(g_wnd, show);
    UpdateWindow(g_wnd);
    start_timer();

    while (GetMessage(&msg, NULL, 0, 0)) {
        TranslateMessage(&msg);
        DispatchMessage(&msg);
    }
    free_colors();
    return msg.wParam;
}
