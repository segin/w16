/*
 * color.c - the Color applet: colour schemes, the sample, the basic and
 * custom colour grids, Save/Remove Scheme, and the Custom Color Selector.
 *
 * Observed on Windows 3.1 (tests/scripts/cpl/co*.txt, ARCHITECTURE.md
 * 14.3):
 * - OK sets the 21 system colours (SetSysColors) and writes all 21 WIN.INI
 *   [colors] keys, "R G B", in scheme order (below), also when nothing
 *   changed; CONTROL.INI [current] color schemes= the scheme's name, or
 *   empty once a colour was changed by hand. Cancel writes nothing.
 * - CONTROL.INI [color schemes]: name=21 colours as hex 0xBBGGRR, comma
 *   separated, scheme order. "Windows Default" is built in and first in
 *   the (owner-drawn, unsorted) combo box, whose items are "name=values";
 *   the others follow in the file's order.
 * - Removing a scheme (after a Yes) deletes its key and selects the one
 *   after it. Save Scheme and Remove Scheme start disabled.
 * - The dialog opens collapsed (the left part only); Color Palette >>
 *   widens it to the template's width and disables itself.
 * - The 48 basic colours (below) were read from the Custom Color
 *   Selector's Red/Green/Blue boxes after clicking each cell.
 * The pixel layout of the sample, the grids and their selection frames
 * was measured on the screen (VGA); see the drawing code.
 */
#include "maincpl.h"

#define NCOLORS 21

/* scheme order (CONTROL.INI values, WIN.INI [colors] keys) -> COLOR_ */
static const int sys_index[NCOLORS] = {
    COLOR_BACKGROUND, COLOR_APPWORKSPACE, COLOR_WINDOW, COLOR_WINDOWTEXT,
    COLOR_MENU, COLOR_MENUTEXT, COLOR_ACTIVECAPTION, COLOR_INACTIVECAPTION,
    COLOR_CAPTIONTEXT, COLOR_ACTIVEBORDER, COLOR_INACTIVEBORDER,
    COLOR_WINDOWFRAME, COLOR_SCROLLBAR, COLOR_BTNFACE, COLOR_BTNSHADOW,
    COLOR_BTNTEXT, COLOR_GRAYTEXT, COLOR_HIGHLIGHT, COLOR_HIGHLIGHTTEXT,
    COLOR_INACTIVECAPTIONTEXT, COLOR_BTNHIGHLIGHT
};
static const char * const ini_key[NCOLORS] = {
    "Background", "AppWorkspace", "Window", "WindowText", "Menu", "MenuText",
    "ActiveTitle", "InactiveTitle", "TitleText", "ActiveBorder",
    "InactiveBorder", "WindowFrame", "Scrollbar", "ButtonFace", "ButtonShadow",
    "ButtonText", "GrayText", "Hilight", "HilightText", "InactiveTitleText",
    "ButtonHilight"
};
/* scheme-order indices */
enum { S_DESKTOP, S_APPWS, S_WINDOW, S_WINTEXT, S_MENU, S_MENUTEXT, S_ACTTITLE,
       S_INACTTITLE, S_TITLETEXT, S_ACTBORDER, S_INACTBORDER, S_FRAME, S_SCROLL,
       S_BTNFACE, S_BTNSHADOW, S_BTNTEXT, S_GRAYTEXT, S_HILIGHT, S_HILIGHTTEXT,
       S_INACTTITLETEXT, S_BTNHILIGHT };

/* the Screen Element list (observed order) -> scheme index; the names are
   strings IDS_ELEMENT + scheme index */
static const int element_order[NCOLORS] = {
    S_DESKTOP, S_APPWS, S_WINDOW, S_WINTEXT, S_MENU, S_MENUTEXT, S_ACTTITLE,
    S_INACTTITLE, S_TITLETEXT, S_INACTTITLETEXT, S_ACTBORDER, S_INACTBORDER,
    S_FRAME, S_SCROLL, S_BTNFACE, S_BTNSHADOW, S_BTNTEXT, S_BTNHILIGHT,
    S_GRAYTEXT, S_HILIGHT, S_HILIGHTTEXT
};

static const char default_scheme[] =
    "C0C0C0,FFFFFF,FFFFFF,0,FFFFFF,0,800000,FFFFFF,FFFFFF,C0C0C0,C0C0C0,0,"
    "C0C0C0,C0C0C0,808080,0,C0C0C0,800000,FFFFFF,0,FFFFFF";

/* the basic colours, row by row (observed) */
static const COLORREF basic[48] = {
    RGB(255,128,128), RGB(255,255,232), RGB(128,255,128), RGB(0,255,128),
    RGB(128,255,255), RGB(0,128,255), RGB(255,128,192), RGB(255,128,255),
    RGB(255,0,0), RGB(255,255,128), RGB(128,255,0), RGB(0,255,64),
    RGB(0,255,255), RGB(0,128,192), RGB(128,128,192), RGB(255,0,255),
    RGB(128,64,64), RGB(255,255,0), RGB(0,255,0), RGB(0,128,128),
    RGB(0,64,128), RGB(128,128,255), RGB(128,0,64), RGB(255,0,128),
    RGB(128,0,0), RGB(255,128,0), RGB(0,128,0), RGB(0,128,64),
    RGB(0,0,255), RGB(0,0,160), RGB(128,0,128), RGB(128,0,255),
    RGB(64,0,0), RGB(128,64,0), RGB(0,64,0), RGB(0,64,64),
    RGB(0,0,128), RGB(0,0,64), RGB(64,0,64), RGB(64,0,128),
    RGB(0,0,0), RGB(128,128,0), RGB(128,128,64), RGB(128,128,128),
    RGB(64,128,128), RGB(192,192,192), RGB(130,130,130), RGB(255,255,255)
};

static char s_cpl_ini[] = "CONTROL.INI";
static char s_schemes[] = "color schemes";
static char s_current[] = "current";
/* the built-in scheme is "w16 Default" (branding: main.rc); Windows 3.1's
   name for it, "Windows Default", is still recognised in CONTROL.INI (a
   3.1 installation's [current] color schemes=, or a key of that name in
   [color schemes]) and refused as a name to save under */
static char s_stock_default[] = "Windows Default";
static char s_custom[] = "Custom Colors";

static COLORREF cur[NCOLORS];           /* the colours being edited */
static COLORREF custom[16];
static int element;                     /* scheme index being edited */
static int basic_sel = -1, custom_sel = -1;   /* the cell showing it */
static int basic_focus, custom_focus;   /* the grids' focus cells */
static FARPROC old_static;
static BOOL by_hand;                    /* a colour changed since the scheme */
static BOOL expanded;
static HWND hdlg;

/* ---- values ---- */

static COLORREF parse_hex(const char FAR **pp)
{
    const char FAR *p = *pp;
    DWORD v = 0;
    for (;; p++) {
        char c = *p;
        if (c >= '0' && c <= '9') v = v * 16 + (c - '0');
        else if (c >= 'A' && c <= 'F') v = v * 16 + (c - 'A' + 10);
        else if (c >= 'a' && c <= 'f') v = v * 16 + (c - 'a' + 10);
        else break;
    }
    while (*p == ',' || *p == ' ')
        p++;
    *pp = p;
    return v;   /* kept whole: Black Leather Jacket's 10E0E0E0 draws as on
                   3.1 only with its high byte; WIN.INI gets its RGB */
}

static void parse_scheme(const char FAR *p, COLORREF *c)
{
    int i;
    for (i = 0; i < NCOLORS; i++)
        c[i] = *p ? parse_hex(&p) : GetSysColor(sys_index[i]);
}

static void format_scheme(char *buf, const COLORREF *c)
{
    int i, n = 0;
    for (i = 0; i < NCOLORS; i++)
        n += wsprintf(buf + n, i ? ",%lX" : "%lX", c[i]);
}

/* ---- drawing helpers ---- */

static void fill(HDC dc, int l, int t, int r, int b, COLORREF c)
{
    RECT rc;
    HBRUSH br = CreateSolidBrush(c);
    SetRect(&rc, l, t, r, b);
    FillRect(dc, &rc, br);
    DeleteObject(br);
}

/* a one-pixel rectangle outline, inclusive coordinates */
static void frame(HDC dc, int l, int t, int r, int b, COLORREF c)
{
    fill(dc, l, t, r + 1, t + 1, c);
    fill(dc, l, b, r + 1, b + 1, c);
    fill(dc, l, t, l + 1, b + 1, c);
    fill(dc, r, t, r + 1, b + 1, c);
}

static void hline(HDC dc, int l, int r, int y, COLORREF c) { fill(dc, l, y, r + 1, y + 1, c); }
static void vline(HDC dc, int x, int t, int b, COLORREF c) { fill(dc, x, t, x + 1, b + 1, c); }

static void text_in(HDC dc, int l, int t, int r, int b, UINT fmt, int ids, COLORREF fg)
{
    char s[40];
    RECT rc;
    LoadString(g_hinst, ids, s, sizeof s);
    SetRect(&rc, l, t, r, b);
    SetTextColor(dc, fg);
    SetBkMode(dc, TRANSPARENT);
    DrawText(dc, s, -1, &rc, fmt);
}

/* a window's frame as the sample draws it (inclusive coordinates): the
   outline, the border colour, the inner outline 5 pixels in on the left
   and 6 elsewhere */
static void sample_frame(HDC dc, int l, int t, int r, int b, COLORREF border)
{
    COLORREF f = cur[S_FRAME];
    fill(dc, l, t, r + 1, b + 1, border);
    frame(dc, l, t, r, b, f);
    frame(dc, l + 5, t + 6, r - 6, b - 6, f);
}

/* a pushbutton-style 3-D box (inclusive): highlight top and left, two
   pixels of shadow bottom and right */
static void bevel(HDC dc, int l, int t, int r, int b, int w)
{
    int k;
    fill(dc, l, t, r + 1, b + 1, cur[S_BTNFACE]);
    for (k = 0; k < w; k++) {
        hline(dc, l, r - k - 1, t + k, cur[S_BTNHILIGHT]);
        vline(dc, l + k, t, b - k - 1, cur[S_BTNHILIGHT]);
    }
    for (k = 0; k < 2; k++) {
        hline(dc, l + k, r, b - k, cur[S_BTNSHADOW]);
        vline(dc, r - k, t + k, b, cur[S_BTNSHADOW]);
    }
}

/* a scroll bar arrow: rows of 1, 3, 5, 7 and a 3-wide stem of 3 rows */
static void arrow(HDC dc, int cx, int y, BOOL up)
{
    int k;
    for (k = 0; k < 4; k++)
        hline(dc, cx - k, cx + k, up ? y + k : y + 6 - k, cur[S_BTNTEXT]);
    for (k = 0; k < 3; k++)
        hline(dc, cx - 1, cx + 1, up ? y + 4 + k : y + k, cur[S_BTNTEXT]);
}

/* ---- the sample (static 717): measured on VGA, coordinates relative to
   the control's top left, inclusive ---- */

static void draw_sample(HDC dc, RECT *rc)
{
    int x = rc->left, y = rc->top;
    HFONT old = SelectObject(dc, GetStockObject(SYSTEM_FONT));
    SetViewportOrg(dc, x, y);
    fill(dc, 0, 0, rc->right - rc->left, rc->bottom - rc->top, cur[S_DESKTOP]);

    /* the inactive window: frame, caption, menu bar line, client */
    sample_frame(dc, 9, 9, 223, 165, cur[S_INACTBORDER]);
    fill(dc, 15, 16, 217, 34, cur[S_INACTTITLE]);
    text_in(dc, 15, 16, 218, 34, DT_CENTER | DT_SINGLELINE,
            IDS_SAMPLE_INACTIVE, cur[S_INACTTITLETEXT]);
    hline(dc, 9, 223, 34, cur[S_FRAME]);
    fill(dc, 15, 35, 18, 53, cur[S_MENU]);
    fill(dc, 15, 54, 18, 159, cur[S_APPWS]);
    hline(dc, 14, 18, 53, cur[S_FRAME]);
    hline(dc, 9, 14, 140, cur[S_FRAME]);

    /* the active window */
    sample_frame(dc, 18, 34, 232, 174, cur[S_ACTBORDER]);
    vline(dc, 41, 35, 39, cur[S_FRAME]);
    vline(dc, 207, 35, 39, cur[S_FRAME]);
    vline(dc, 41, 169, 173, cur[S_FRAME]);
    vline(dc, 207, 169, 173, cur[S_FRAME]);
    hline(dc, 18, 23, 149, cur[S_FRAME]);
    hline(dc, 226, 232, 149, cur[S_FRAME]);
    fill(dc, 24, 41, 226, 59, cur[S_ACTTITLE]);
    text_in(dc, 24, 41, 227, 59, DT_CENTER | DT_SINGLELINE,
            IDS_SAMPLE_ACTIVE, cur[S_TITLETEXT]);
    hline(dc, 18, 232, 59, cur[S_FRAME]);
    fill(dc, 24, 60, 226, 78, cur[S_MENU]);
    text_in(dc, 28, 60, 226, 78, DT_LEFT | DT_SINGLELINE | DT_NOPREFIX,
            IDS_SAMPLE_MENU, cur[S_MENUTEXT]);
    hline(dc, 23, 226, 78, cur[S_FRAME]);
    fill(dc, 24, 79, 226, 168, cur[S_APPWS]);

    /* the File menu dropped down: Disabled, Highlighted */
    fill(dc, 24, 79, 111, 95, cur[S_MENU]);
    text_in(dc, 24, 79, 111, 95, DT_LEFT | DT_SINGLELINE, IDS_SAMPLE_DISABLED, cur[S_GRAYTEXT]);
    fill(dc, 24, 95, 111, 111, cur[S_HILIGHT]);
    text_in(dc, 24, 95, 111, 111, DT_LEFT | DT_SINGLELINE, IDS_SAMPLE_HILIGHTED, cur[S_HILIGHTTEXT]);
    vline(dc, 111, 79, 111, cur[S_FRAME]);
    hline(dc, 23, 111, 111, cur[S_FRAME]);

    /* the scroll bar */
    vline(dc, 210, 79, 167, cur[S_FRAME]);
    bevel(dc, 211, 79, 225, 93, 1);
    arrow(dc, 218, 83, TRUE);
    hline(dc, 210, 226, 94, cur[S_FRAME]);
    hline(dc, 210, 226, 95, cur[S_FRAME]);
    fill(dc, 211, 96, 226, 151, cur[S_SCROLL]);
    hline(dc, 210, 226, 151, cur[S_FRAME]);
    hline(dc, 210, 226, 152, cur[S_FRAME]);
    bevel(dc, 211, 153, 225, 167, 1);
    arrow(dc, 218, 157, FALSE);

    /* the child window with its text and button */
    fill(dc, 122, 84, 205, 163, cur[S_WINDOW]);
    frame(dc, 122, 84, 204, 162, cur[S_FRAME]);
    text_in(dc, 123, 85, 204, 128, DT_CENTER | DT_WORDBREAK, IDS_SAMPLE_WINTEXT, cur[S_WINTEXT]);
    hline(dc, 135, 191, 128, cur[S_FRAME]);
    hline(dc, 135, 191, 156, cur[S_FRAME]);
    vline(dc, 134, 129, 155, cur[S_FRAME]);
    vline(dc, 192, 129, 155, cur[S_FRAME]);
    bevel(dc, 135, 129, 191, 155, 2);
    text_in(dc, 136, 129, 192, 155, DT_CENTER | DT_VCENTER | DT_SINGLELINE, IDS_SAMPLE_OK,
            cur[S_BTNTEXT]);

    SetViewportOrg(dc, 0, 0);
    SelectObject(dc, old);
}

/* ---- the colour grids (statics 32 and 80): cells 24 x 17 from 3,3,
   every 29 across and 22 down; the selected one framed twice, one pixel
   further out ---- */

static void cell_rect(HWND ctl, int k, RECT *r)
{
    GetWindowRect(ctl, r);
    ScreenToClient(hdlg, (POINT FAR *)&r->left);
    OffsetRect(r, 0, 0);
    r->left += 3 + (k % 8) * 29;
    r->top += 3 + (k / 8) * 22;
    r->right = r->left + 24;
    r->bottom = r->top + 17;
}

static void draw_grid(HDC dc, int id, const COLORREF *c, int n, int sel)
{
    HWND ctl = GetDlgItem(hdlg, id);
    int k, fk = GetFocus() == ctl ? (id == IDC_CO_BASIC ? basic_focus : custom_focus) : -1;
    for (k = 0; k < n; k++) {
        RECT r;
        cell_rect(ctl, k, &r);
        fill(dc, r.left + 1, r.top + 1, r.right - 1, r.bottom - 1, c[k]);
        frame(dc, r.left, r.top, r.right - 1, r.bottom - 1, RGB(0, 0, 0));
        if (k == sel) {
            frame(dc, r.left - 1, r.top - 1, r.right, r.bottom, RGB(0, 0, 0));
        }
        if (k == fk) {          /* observed: 3 pixels outside the cell */
            InflateRect(&r, 3, 3);
            DrawFocusRect(dc, &r);
        }
    }
}

static int grid_hit(int id, int n, POINT pt)
{
    HWND ctl = GetDlgItem(hdlg, id);
    int k;
    for (k = 0; k < n; k++) {
        RECT r;
        cell_rect(ctl, k, &r);
        if (PtInRect(&r, pt))
            return k;
    }
    return -1;
}

/* ---- state ---- */

static void invalidate_ctl(int id);
static void set_color(COLORREF c);

/* the grids are statics with WS_TABSTOP: subclassed so that they repaint
   their focus rectangle and take the arrow keys and Space (UNVERIFIED:
   3.1's keyboard behaviour in the grids) */
LRESULT FAR PASCAL __loadds GridProc(HWND w, UINT msg, WPARAM wp, LPARAM lp)
{
    int id = GetDlgCtrlID(w), n = id == IDC_CO_BASIC ? 48 : 16;
    int *f = id == IDC_CO_BASIC ? &basic_focus : &custom_focus;
    switch (msg) {
    case WM_SETFOCUS:
    case WM_KILLFOCUS:
        invalidate_ctl(id);
        break;
    case WM_GETDLGCODE:
        return DLGC_WANTARROWS | DLGC_WANTCHARS;
    case WM_KEYDOWN: {
        int k = *f;
        switch (wp) {
        case VK_LEFT:  k--; break;
        case VK_RIGHT: k++; break;
        case VK_UP:    k -= 8; break;
        case VK_DOWN:  k += 8; break;
        default: return CallWindowProc(old_static, w, msg, wp, lp);
        }
        if (k >= 0 && k < n) {
            *f = k;
            invalidate_ctl(id);
        }
        return 0;
    }
    case WM_CHAR:
        if (wp == ' ') {
            set_color(id == IDC_CO_BASIC ? basic[*f] : custom[*f]);
            return 0;
        }
        break;
    }
    return CallWindowProc(old_static, w, msg, wp, lp);
}

static void invalidate_ctl(int id)
{
    RECT r;
    GetWindowRect(GetDlgItem(hdlg, id), &r);
    ScreenToClient(hdlg, (POINT FAR *)&r.left);
    ScreenToClient(hdlg, (POINT FAR *)&r.right);
    InflateRect(&r, 4, 4);
    InvalidateRect(hdlg, &r, id != IDC_CO_SAMPLE);
}

static void find_cells(void)
{
    int k;
    basic_sel = custom_sel = -1;
    for (k = 0; k < 48; k++)
        if (basic[k] == cur[element]) { basic_sel = k; break; }
    if (basic_sel < 0)
        for (k = 0; k < 16; k++)
            if (custom[k] == cur[element]) { custom_sel = k; break; }
    invalidate_ctl(IDC_CO_BASIC);
    invalidate_ctl(IDC_CO_CUSTOM);
}

static void set_color(COLORREF c)
{
    cur[element] = c;
    if (!by_hand) {
        by_hand = TRUE;     /* observed: the combo box keeps its scheme */
        EnableWindow(GetDlgItem(hdlg, IDC_CO_SAVE), TRUE);
        EnableWindow(GetDlgItem(hdlg, IDC_CO_REMOVE), FALSE);
    }
    invalidate_ctl(IDC_CO_SAMPLE);
    find_cells();
}

static void select_scheme(int i)
{
    HWND cb = GetDlgItem(hdlg, IDC_CO_SCHEMES);
    int n = (int)SendMessage(cb, CB_GETLBTEXTLEN, i, 0);
    HLOCAL h = LocalAlloc(LMEM_FIXED, n + 1);
    char *s, *v;
    if (!h)
        return;
    s = (char *)LocalLock(h);
    SendMessage(cb, CB_GETLBTEXT, i, (LPARAM)(LPSTR)s);
    for (v = s; *v && *v != '='; v++)
        ;
    parse_scheme(*v ? v + 1 : v, cur);
    LocalUnlock(h);
    LocalFree(h);
    SendMessage(cb, CB_SETCURSEL, i, 0);
    by_hand = FALSE;
    EnableWindow(GetDlgItem(hdlg, IDC_CO_SAVE), FALSE);
    EnableWindow(GetDlgItem(hdlg, IDC_CO_REMOVE), i != 0);
    invalidate_ctl(IDC_CO_SAMPLE);
    find_cells();
}

/* a buffer with every key of a CONTROL.INI section (grows until it fits) */
static HLOCAL section_keys(const char *sec)
{
    UINT size = 512;
    for (;;) {
        HLOCAL h = LocalAlloc(LMEM_FIXED, size);
        int n;
        if (!h)
            return NULL;
        n = GetPrivateProfileString(sec, NULL, "", LocalLock(h), size, s_cpl_ini);
        LocalUnlock(h);
        if ((UINT)n < size - 2 || size >= 0x8000)
            return h;
        LocalFree(h);
        size *= 2;
    }
}

/* "name=values" items: w16 Default (3.1: Windows Default) first, then the
   file's schemes */
static int fill_schemes(const char *select)
{
    HWND cb = GetDlgItem(hdlg, IDC_CO_SCHEMES);
    char name[80], item[400];
    HLOCAL keys;
    int sel = -1, n;
    SendMessage(cb, CB_RESETCONTENT, 0, 0);
    LoadString(g_hinst, IDS_DEFAULT_SCHEME, name, sizeof name);
    wsprintf(item, "%s=%s", (LPSTR)name, (LPSTR)default_scheme);
    SendMessage(cb, CB_ADDSTRING, 0, (LPARAM)(LPSTR)item);
    if (!lstrcmpi(name, select) || !lstrcmpi(s_stock_default, select))
        sel = 0;
    if ((keys = section_keys(s_schemes)) != NULL) {
        const char *k;
        for (k = (const char *)LocalLock(keys); *k; k += lstrlen(k) + 1) {
            if (!lstrcmpi(k, name) || !lstrcmpi(k, s_stock_default))
                continue;
            n = wsprintf(item, "%s=", (LPSTR)k);
            GetPrivateProfileString(s_schemes, k, "", item + n, sizeof item - n, s_cpl_ini);
            n = (int)SendMessage(cb, CB_ADDSTRING, 0, (LPARAM)(LPSTR)item);
            if (!lstrcmpi(k, select))
                sel = n;
        }
        LocalFree(keys);
    }
    return sel;
}

static void scheme_name(int i, char *name, int size)
{
    HWND cb = GetDlgItem(hdlg, IDC_CO_SCHEMES);
    char item[400], *p;
    SendMessage(cb, CB_GETLBTEXT, i, (LPARAM)(LPSTR)item);
    for (p = item; *p && *p != '='; p++)
        ;
    *p = 0;
    lstrcpyn(name, item, size);
}

static void expand(void)
{
    RECT r;
    if (expanded)
        return;
    expanded = TRUE;
    SetRect(&r, 0, 0, 300, 194);
    MapDialogRect(hdlg, &r);
    {
        RECT w, c;
        GetWindowRect(hdlg, &w);
        GetClientRect(hdlg, &c);
        SetWindowPos(hdlg, NULL, 0, 0, r.right + (w.right - w.left - c.right),
                     w.bottom - w.top, SWP_NOMOVE | SWP_NOZORDER);
    }
    EnableWindow(GetDlgItem(hdlg, IDC_CO_ELEMENT), TRUE);
    EnableWindow(GetDlgItem(hdlg, IDC_CO_BASIC), TRUE);
    EnableWindow(GetDlgItem(hdlg, IDC_CO_CUSTOM), TRUE);
    EnableWindow(GetDlgItem(hdlg, IDC_CO_DEFINE), TRUE);
    EnableWindow(GetDlgItem(hdlg, IDC_CO_SAVE), TRUE);     /* observed */
    SetFocus(GetDlgItem(hdlg, IDC_CO_ELEMENT));
    EnableWindow(GetDlgItem(hdlg, IDC_CO_PALETTE), FALSE);
}

static void load_custom(void)
{
    char key[8], v[16];
    const char FAR *p;
    int k;
    for (k = 0; k < 16; k++) {
        wsprintf(key, "Color%c", 'A' + k);
        GetPrivateProfileString(s_custom, key, "FFFFFF", v, sizeof v, s_cpl_ini);
        p = v;
        custom[k] = parse_hex(&p);
    }
}

static void apply(void)
{
    int idx[NCOLORS], i;
    char v[16], name[80];
    for (i = 0; i < NCOLORS; i++) {
        idx[i] = sys_index[i];
        wsprintf(v, "%d %d %d", GetRValue(cur[i]), GetGValue(cur[i]), GetBValue(cur[i]));
        WriteProfileString("colors", ini_key[i], v);
    }
    SetSysColors(NCOLORS, idx, cur);
    i = (int)SendDlgItemMessage(hdlg, IDC_CO_SCHEMES, CB_GETCURSEL, 0, 0);
    if (by_hand || i < 0)
        name[0] = 0;
    else
        scheme_name(i, name, sizeof name);
    WritePrivateProfileString(s_current, s_schemes, name, s_cpl_ini);
}

/* ---- Save Scheme ---- */

static char save_name[80];

static void message(HWND w, UINT ids, LPCSTR arg, UINT flags, int *result)
{
    char t[40], f[300], m[400];
    LoadString(g_hinst, IDS_CONTROLPANEL, t, sizeof t);  /* observed */
    LoadString(g_hinst, ids, f, sizeof f);
    wsprintf(m, f, arg);
    *result = MessageBox(w, m, t, flags);
}

BOOL FAR PASCAL __loadds SaveSchemeProc(HWND dlg, UINT msg, WPARAM wp, LPARAM lp)
{
    (void)lp;
    switch (msg) {
    case WM_INITDIALOG:
        SetDlgItemText(dlg, IDC_SS_NAME, save_name);
        SendDlgItemMessage(dlg, IDC_SS_NAME, EM_LIMITTEXT, sizeof save_name - 1, 0);
        return TRUE;
    case WM_COMMAND:
        switch (wp) {
        case IDOK: {
            char def[80], *p;
            int r, bad = 0;
            GetDlgItemText(dlg, IDC_SS_NAME, save_name, sizeof save_name);
            LoadString(g_hinst, IDS_DEFAULT_SCHEME, def, sizeof def);
            for (p = save_name; *p; p++) {
                if (*p == '=') bad = IDS_SCHEME_EQUAL;
                else if ((*p == '[' || *p == ']') && !bad) bad = IDS_SCHEME_BRACKET;
            }
            if (!save_name[0])
                bad = IDS_SCHEME_BLANK;
            else if (!lstrcmpi(save_name, def) || !lstrcmpi(save_name, s_stock_default))
                bad = IDS_SCHEME_DEFAULT;
            if (bad) {
                message(dlg, bad, "", MB_OK | MB_ICONEXCLAMATION, &r);
                SetFocus(GetDlgItem(dlg, IDC_SS_NAME));
                return TRUE;
            }
            EndDialog(dlg, IDOK);
            return TRUE;
        }
        case IDCANCEL:
            EndDialog(dlg, IDCANCEL);
            return TRUE;
        case IDC_HELP:
            cpl_help(dlg, AP_COLOR);
            return TRUE;
        }
        break;
    }
    return FALSE;
}

static void save_scheme(void)
{
    char v[400];
    int sel = (int)SendDlgItemMessage(hdlg, IDC_CO_SCHEMES, CB_GETCURSEL, 0, 0);
    /* observed: the box starts with the selected scheme's name */
    save_name[0] = 0;
    if (sel >= 0)
        scheme_name(sel, save_name, sizeof save_name);
    if (DialogBox(g_hinst, MAKEINTRESOURCE(DLG_SAVESCHEME), hdlg, SaveSchemeProc) != IDOK)
        return;
    format_scheme(v, cur);
    WritePrivateProfileString(s_schemes, save_name, v, s_cpl_ini);
    sel = fill_schemes(save_name);
    if (sel >= 0)
        select_scheme(sel);
    EnableWindow(GetDlgItem(hdlg, IDC_CO_SAVE), TRUE);     /* observed */
}

static void remove_scheme(void)
{
    HWND cb = GetDlgItem(hdlg, IDC_CO_SCHEMES);
    int i = (int)SendMessage(cb, CB_GETCURSEL, 0, 0), r, n;
    char name[80];
    if (i < 0)
        return;
    scheme_name(i, name, sizeof name);
    if (i == 0) {
        message(hdlg, IDS_SCHEME_NOREMOVE, name, MB_OK | MB_ICONEXCLAMATION, &r);
        return;
    }
    message(hdlg, IDS_SCHEME_REMOVE, name, MB_YESNO | MB_ICONEXCLAMATION, &r);
    if (r != IDYES)
        return;
    WritePrivateProfileString(s_schemes, name, NULL, s_cpl_ini);
    SendMessage(cb, CB_DELETESTRING, i, 0);
    n = (int)SendMessage(cb, CB_GETCOUNT, 0, 0);
    select_scheme(i < n ? i : n - 1);
}

/* ---- the Custom Color Selector ----
 * The same colour model and pictures as w16's COMMDLG colour dialog
 * (shell/commdlg/color.c, whose HLS rounding, colour field, luminosity
 * bar, arrow, cross and sample were fitted to Windows 3.1 pixel for pixel);
 * that code is w16's own and is reused here for the Control Panel's
 * selector, which observation shows draws the same way: hue, saturation,
 * luminosity in 240ths (hue 0-239), the field in blocks of 4 hue by 8
 * saturation steps at luminosity 120, the bar in blocks of 8 luminosity
 * steps, the cross and the arrow as there. The field is the inside of
 * static 710's black frame; the bar is static 702, framed one pixel
 * outside; the sample the inside of static 709's frame, its left half the
 * colour, its right half the nearest solid colour.
 * Observed here: Add Color stores the colour in the custom box with the
 * focus and writes CONTROL.INI [Custom Colors] (ColorA..ColorP, hex
 * BBGGRR) without changing the screen element; the edits follow each
 * other as in COMMDLG. */

#define HLSMAX          240
#define RGBMAX          255
#define HUE_UNDEFINED   (HLSMAX * 2 / 3)

/* (a * b + k) / d in 32 bits, the quotient toward zero */
static int mad(int a, int b, int k, int d)
{
    int q = 0;
    _asm {
        mov ax, a
        imul word ptr b
        mov bx, ax
        mov cx, dx
        mov ax, k
        cwd
        add ax, bx
        adc dx, cx
        idiv word ptr d
        mov q, ax
    }
    return q;
}

static int scale_round(int a, int b, int d)
{
    return mad(a, b, d / 2, d);
}

static int h_, l_, s_;
static COLORREF cc;
static BOOL cc_updating;
static HBITMAP cc_field;
static RECT rfield, rbar, rsample;

static void rgb_to_hls(COLORREF c)
{
    int ch[3], hi = 0, lo = 0, i, spread, l, s, h;
    ch[0] = GetRValue(c);
    ch[1] = GetGValue(c);
    ch[2] = GetBValue(c);
    for (i = 1; i < 3; i++) {
        if (ch[i] > ch[hi])
            hi = i;
        if (ch[i] < ch[lo])
            lo = i;
    }
    spread = ch[hi] - ch[lo];
    l = scale_round(ch[hi] + ch[lo], HLSMAX, 2 * RGBMAX);
    if (spread == 0) {
        h_ = HUE_UNDEFINED;
        s_ = 0;
        l_ = l;
        return;
    }
    s = scale_round(spread, HLSMAX,
                    l <= HLSMAX / 2 ? ch[hi] + ch[lo] : 2 * RGBMAX - ch[hi] - ch[lo]);
    {
        int next = (hi + 1) % 3, prev = (hi + 2) % 3;
        int dn = scale_round(ch[hi] - ch[next], HLSMAX / 6, spread);
        int dp = scale_round(ch[hi] - ch[prev], HLSMAX / 6, spread);
        h = hi * (HLSMAX / 3) + dp - dn;
    }
    if (h < 0)
        h += HLSMAX;
    if (h >= HLSMAX)
        h -= HLSMAX;
    h_ = h;
    l_ = l;
    s_ = s;
}

static COLORREF hls_to_rgb(int h, int l, int s)
{
    int top, bot, span, sector, pos, up, down, v[3], i;
    if (s == 0) {
        int g = (int)(((unsigned)l * RGBMAX) / HLSMAX);
        return RGB(g, g, g);
    }
    if (l <= HLSMAX / 2)
        top = scale_round(l, HLSMAX + s, HLSMAX);
    else
        top = l + s - scale_round(l, s, HLSMAX);
    bot = 2 * l - top;
    span = top - bot;
    h %= HLSMAX;
    sector = h / (HLSMAX / 6);
    pos = h % (HLSMAX / 6);
    up = bot + scale_round(span, pos, HLSMAX / 6);
    down = bot + scale_round(span, HLSMAX / 6 - pos, HLSMAX / 6);
    switch (sector) {
    case 0: v[0] = top;  v[1] = up;   v[2] = bot;  break;
    case 1: v[0] = down; v[1] = top;  v[2] = bot;  break;
    case 2: v[0] = bot;  v[1] = top;  v[2] = up;   break;
    case 3: v[0] = bot;  v[1] = down; v[2] = top;  break;
    case 4: v[0] = up;   v[1] = bot;  v[2] = top;  break;
    default: v[0] = top; v[1] = bot;  v[2] = down; break;
    }
    for (i = 0; i < 3; i++)
        v[i] = scale_round(v[i], RGBMAX, HLSMAX);
    return RGB(v[0], v[1], v[2]);
}

static void ctl_rect(HWND dlg, int id, RECT *r)
{
    GetWindowRect(GetDlgItem(dlg, id), r);
    ScreenToClient(dlg, (POINT FAR *)&r->left);
    ScreenToClient(dlg, (POINT FAR *)&r->right);
}

static void fillr(HDC dc, const RECT *r, COLORREF c)
{
    HBRUSH b = CreateSolidBrush(c);
    FillRect(dc, r, b);
    DeleteObject(b);
}

#define SAT_STEP        8
#define HUE_STEP        4
#define LUM_STEP        8

static void make_field(HDC dc)
{
    int w = rfield.right - rfield.left, h = rfield.bottom - rfield.top;
    int hue, sat;
    HDC mem;
    HBITMAP old;
    RECT r;
    if (cc_field)
        return;
    cc_field = CreateCompatibleBitmap(dc, w, h);
    mem = CreateCompatibleDC(dc);
    if (!cc_field || !mem) {
        if (mem)
            DeleteDC(mem);
        return;
    }
    old = SelectObject(mem, cc_field);
    for (sat = HLSMAX; sat > 0; sat -= SAT_STEP) {
        r.top = mad(HLSMAX - sat, h, 0, HLSMAX);
        r.bottom = mad(HLSMAX - sat + SAT_STEP, h, 0, HLSMAX);
        for (hue = 0; hue < HLSMAX; hue += HUE_STEP) {
            r.left = mad(hue, w, 0, HLSMAX);
            r.right = mad(hue + HUE_STEP, w, 0, HLSMAX);
            fillr(mem, &r, hls_to_rgb(hue, HLSMAX / 2, sat));
        }
    }
    SelectObject(mem, old);
    DeleteDC(mem);
}

static void draw_cross(HDC dc)
{
    int cx = rfield.left + mad(h_, rfield.right - rfield.left, 0, HLSMAX - 1);
    int cy = rfield.top + mad(HLSMAX - s_, rfield.bottom - rfield.top - 1, 0, HLSMAX);
    int k;
    RECT r;
    HRGN clip = CreateRectRgn(rfield.left, rfield.top, rfield.right, rfield.bottom);
    SelectClipRgn(dc, clip);
    for (k = 0; k < 4; k++) {
        static const int dx[4] = { -9, 5, -1, -1 }, dy[4] = { -1, -1, -9, 5 };
        static const int w[4] = { 5, 5, 3, 3 }, hh[4] = { 3, 3, 5, 5 };
        r.left = cx + dx[k];
        r.top = cy + dy[k];
        r.right = r.left + w[k];
        r.bottom = r.top + hh[k];
        FillRect(dc, &r, GetStockObject(BLACK_BRUSH));
    }
    SelectClipRgn(dc, NULL);
    DeleteObject(clip);
}

static void draw_field(HDC dc)
{
    HDC mem;
    HBITMAP old;
    make_field(dc);
    mem = CreateCompatibleDC(dc);
    if (cc_field && mem) {
        old = SelectObject(mem, cc_field);
        BitBlt(dc, rfield.left, rfield.top, rfield.right - rfield.left,
               rfield.bottom - rfield.top, mem, 0, 0, SRCCOPY);
        SelectObject(mem, old);
    }
    if (mem)
        DeleteDC(mem);
    draw_cross(dc);
}

static int bar_row(int l, int n)
{
    int y = n - 3 - mad(l, n, 60, HLSMAX);
    return y < 0 ? 0 : y > n ? n : y;
}

static void draw_bar(HDC dc)
{
    RECT r, o;
    int lum, n = rbar.bottom - rbar.top;
    o = rbar;
    InflateRect(&o, 1, 1);
    FrameRect(dc, &o, GetStockObject(BLACK_BRUSH));
    r = rbar;
    for (lum = HLSMAX; lum >= 0; lum -= LUM_STEP) {
        r.top = rbar.top + bar_row(lum, n);
        r.bottom = lum ? rbar.top + bar_row(lum - LUM_STEP, n) : rbar.bottom;
        fillr(dc, &r, hls_to_rgb(h_, lum, s_));
    }
}

static void draw_arrow(HDC dc, BOOL erase)
{
    POINT p[3];
    RECT r;
    int n1 = rbar.bottom - rbar.top - 1;
    int x = rbar.right + 2, y = rbar.top + n1 - mad(l_, n1, HLSMAX - 1, HLSMAX);
    HPEN op;
    HBRUSH ob;
    if (erase) {
        r.left = x;
        r.top = rbar.top - 7;
        r.right = x + 8;
        r.bottom = rbar.bottom + 7;
        fillr(dc, &r, GetSysColor(COLOR_WINDOW));
    }
    /* observed here: 6 rows up and down from the tip (COMMDLG's: 5) */
    p[0].x = x + 6;
    p[0].y = y - 6;
    p[1].x = x;
    p[1].y = y;
    p[2].x = x + 6;
    p[2].y = y + 6;
    op = SelectObject(dc, GetStockObject(BLACK_PEN));
    ob = SelectObject(dc, GetStockObject(WHITE_BRUSH));
    Polygon(dc, p, 3);
    SelectObject(dc, op);
    SelectObject(dc, ob);
}

static void draw_cc_sample(HDC dc)
{
    RECT r = rsample;
    int mid = r.left + (r.right - r.left) / 2;
    r.right = mid;
    fillr(dc, &r, cc);
    r.left = mid;
    r.right = rsample.right;
    fillr(dc, &r, GetNearestColor(dc, cc));
}

static void cc_show(HWND dlg, BOOL edits)
{
    if (edits) {
        cc_updating = TRUE;
        SetDlgItemInt(dlg, IDC_CC_RED, GetRValue(cc), FALSE);
        SetDlgItemInt(dlg, IDC_CC_GREEN, GetGValue(cc), FALSE);
        SetDlgItemInt(dlg, IDC_CC_BLUE, GetBValue(cc), FALSE);
        SetDlgItemInt(dlg, IDC_CC_HUE, h_, FALSE);
        SetDlgItemInt(dlg, IDC_CC_SAT, s_, FALSE);
        SetDlgItemInt(dlg, IDC_CC_LUMV, l_, FALSE);
        cc_updating = FALSE;
    }
    {
        HDC dc = GetDC(dlg);
        draw_field(dc);
        draw_bar(dc);
        draw_arrow(dc, TRUE);
        draw_cc_sample(dc);
        ReleaseDC(dlg, dc);
    }
}

static void cc_edit(HWND dlg, int id)
{
    BOOL ok;
    UINT v;
    if (cc_updating)
        return;
    v = GetDlgItemInt(dlg, id, &ok, FALSE);
    if (!ok)
        return;
    switch (id) {
    case IDC_CC_HUE: case IDC_CC_SAT: case IDC_CC_LUMV:
        if (id == IDC_CC_HUE) {
            if (v > HLSMAX - 1) v = HLSMAX - 1;
            h_ = v;
        } else {
            if (v > HLSMAX) v = HLSMAX;
            if (id == IDC_CC_SAT) s_ = v; else l_ = v;
        }
        cc = hls_to_rgb(h_, l_, s_);
        cc_updating = TRUE;
        SetDlgItemInt(dlg, IDC_CC_RED, GetRValue(cc), FALSE);
        SetDlgItemInt(dlg, IDC_CC_GREEN, GetGValue(cc), FALSE);
        SetDlgItemInt(dlg, IDC_CC_BLUE, GetBValue(cc), FALSE);
        cc_updating = FALSE;
        break;
    default:
        if (v > 255) v = 255;
        cc = RGB(id == IDC_CC_RED ? v : GetRValue(cc), id == IDC_CC_GREEN ? v : GetGValue(cc),
                 id == IDC_CC_BLUE ? v : GetBValue(cc));
        rgb_to_hls(cc);
        cc_updating = TRUE;
        SetDlgItemInt(dlg, IDC_CC_HUE, h_, FALSE);
        SetDlgItemInt(dlg, IDC_CC_SAT, s_, FALSE);
        SetDlgItemInt(dlg, IDC_CC_LUMV, l_, FALSE);
        cc_updating = FALSE;
        break;
    }
    cc_show(dlg, FALSE);
}

static void add_custom(void)
{
    char key[8], v[16];
    int k = custom_focus;
    custom[k] = cc;
    wsprintf(key, "Color%c", 'A' + k);
    wsprintf(v, "%lX", cc);
    WritePrivateProfileString(s_custom, key, v, s_cpl_ini);
    /* UNVERIFIED for this dialog: the focus box moves on as in COMMDLG
       (down, then the next column) */
    custom_focus = k < 8 ? k + 8 : (k - 8 + 1) % 8;
    find_cells();
}

BOOL FAR PASCAL __loadds CustomColorProc(HWND dlg, UINT msg, WPARAM wp, LPARAM lp)
{
    switch (msg) {
    case WM_INITDIALOG: {
        cc = cur[element];
        rgb_to_hls(cc);
        ctl_rect(dlg, IDC_CC_SPECTRUM, &rfield);
        InflateRect(&rfield, -1, -1);
        ctl_rect(dlg, IDC_CC_LUM, &rbar);
        ctl_rect(dlg, IDC_CC_SAMPLE, &rsample);
        InflateRect(&rsample, -1, -1);
        cc_updating = TRUE;
        SetDlgItemInt(dlg, IDC_CC_RED, GetRValue(cc), FALSE);
        SetDlgItemInt(dlg, IDC_CC_GREEN, GetGValue(cc), FALSE);
        SetDlgItemInt(dlg, IDC_CC_BLUE, GetBValue(cc), FALSE);
        SetDlgItemInt(dlg, IDC_CC_HUE, h_, FALSE);
        SetDlgItemInt(dlg, IDC_CC_SAT, s_, FALSE);
        SetDlgItemInt(dlg, IDC_CC_LUMV, l_, FALSE);
        cc_updating = FALSE;
        return TRUE;
    }
    case WM_PAINT: {
        PAINTSTRUCT ps;
        BeginPaint(dlg, &ps);
        draw_field(ps.hdc);
        draw_bar(ps.hdc);
        draw_arrow(ps.hdc, FALSE);
        draw_cc_sample(ps.hdc);
        EndPaint(dlg, &ps);
        return TRUE;
    }
    case WM_LBUTTONDOWN: {
        POINT pt;
        RECT r;
        pt.x = LOWORD(lp);
        pt.y = HIWORD(lp);
        if (PtInRect(&rfield, pt)) {
            h_ = mad(pt.x - rfield.left, HLSMAX - 1, 0, rfield.right - rfield.left);
            s_ = HLSMAX - mad(pt.y - rfield.top, HLSMAX, 0, rfield.bottom - rfield.top - 1);
            if (s_ < 0) s_ = 0;
            cc = hls_to_rgb(h_, l_, s_);
            cc_show(dlg, TRUE);
            return TRUE;
        }
        r = rbar;
        r.right += 10;
        if (PtInRect(&r, pt)) {
            int n1 = rbar.bottom - rbar.top - 1;
            l_ = mad(rbar.top + n1 - pt.y, HLSMAX, 0, n1);
            if (l_ < 0) l_ = 0;
            if (l_ > HLSMAX) l_ = HLSMAX;
            cc = hls_to_rgb(h_, l_, s_);
            cc_show(dlg, TRUE);
            return TRUE;
        }
        break;
    }
    case WM_VSCROLL: {
        HWND a = (HWND)HIWORD(lp);
        int id = GetDlgCtrlID(a) - (IDC_CC_HUEA - IDC_CC_HUE), max;
        BOOL ok;
        int v = (int)GetDlgItemInt(dlg, id, &ok, FALSE);
        max = id == IDC_CC_HUE ? HLSMAX - 1 : id == IDC_CC_SAT || id == IDC_CC_LUMV ? HLSMAX : 255;
        v += wp == SB_LINEUP ? 1 : -1;
        if (v < 0) v = 0;
        if (v > max) v = max;
        SetDlgItemInt(dlg, id, v, FALSE);
        return TRUE;
    }
    case WM_COMMAND:
        switch (wp) {
        case IDC_CC_HUE: case IDC_CC_SAT: case IDC_CC_LUMV:
        case IDC_CC_RED: case IDC_CC_GREEN: case IDC_CC_BLUE:
            if (HIWORD(lp) == EN_CHANGE)
                cc_edit(dlg, wp);
            return TRUE;
        case IDC_CC_SOLID: {
            HDC dc = GetDC(dlg);
            cc = GetNearestColor(dc, cc);
            ReleaseDC(dlg, dc);
            rgb_to_hls(cc);
            cc_show(dlg, TRUE);
            return TRUE;
        }
        case IDC_CC_ADD:
            add_custom();
            return TRUE;
        case IDOK:
        case IDCANCEL:
            EndDialog(dlg, IDOK);
            return TRUE;
        case IDC_HELP:
            cpl_help(dlg, AP_COLOR);
            return TRUE;
        }
        break;
    case WM_DESTROY:
        if (cc_field) {
            DeleteObject(cc_field);
            cc_field = NULL;
        }
        break;
    }
    return FALSE;
}

/* ---- the Color dialog ---- */

static void init_dialog(HWND dlg)
{
    char cursch[80], name[60];
    HWND el = GetDlgItem(dlg, IDC_CO_ELEMENT);
    RECT r, w, c;
    int i, sel;

    hdlg = dlg;
    expanded = FALSE;
    for (i = 0; i < NCOLORS; i++)
        cur[i] = GetSysColor(sys_index[i]);
    load_custom();
    element = S_DESKTOP;
    for (i = 0; i < NCOLORS; i++) {
        LoadString(g_hinst, IDS_ELEMENT + element_order[i], name, sizeof name);
        SendMessage(el, CB_ADDSTRING, 0, (LPARAM)(LPSTR)name);
    }
    SendMessage(el, CB_SETCURSEL, 0, 0);
    GetPrivateProfileString(s_current, s_schemes, "", cursch, sizeof cursch, s_cpl_ini);
    sel = fill_schemes(cursch);
    if (sel >= 0) {
        SendDlgItemMessage(dlg, IDC_CO_SCHEMES, CB_SETCURSEL, sel, 0);
        EnableWindow(GetDlgItem(dlg, IDC_CO_REMOVE), sel != 0);
        by_hand = FALSE;
    } else
        by_hand = TRUE;
    EnableWindow(GetDlgItem(dlg, IDC_CO_SAVE), FALSE);
    if (sel <= 0)
        EnableWindow(GetDlgItem(dlg, IDC_CO_REMOVE), FALSE);
    find_cells();
    /* collapsed: the left part only, 151 dialog units (observed: 264
       pixels of client on VGA) */
    SetRect(&r, 0, 0, 151, 0);
    MapDialogRect(dlg, &r);
    GetWindowRect(dlg, &w);
    GetClientRect(dlg, &c);
    SetWindowPos(dlg, NULL, 0, 0, r.right + (w.right - w.left - c.right), w.bottom - w.top,
                 SWP_NOMOVE | SWP_NOZORDER);
    EnableWindow(el, FALSE);
    EnableWindow(GetDlgItem(dlg, IDC_CO_BASIC), FALSE);
    EnableWindow(GetDlgItem(dlg, IDC_CO_CUSTOM), FALSE);
    EnableWindow(GetDlgItem(dlg, IDC_CO_DEFINE), FALSE);
    old_static = (FARPROC)SetWindowLong(GetDlgItem(dlg, IDC_CO_BASIC), GWL_WNDPROC,
                                        (LONG)(WNDPROC)GridProc);
    SetWindowLong(GetDlgItem(dlg, IDC_CO_CUSTOM), GWL_WNDPROC, (LONG)(WNDPROC)GridProc);
    basic_focus = basic_sel >= 0 ? basic_sel : 0;
    custom_focus = custom_sel >= 0 ? custom_sel : 0;
}

BOOL FAR PASCAL __loadds ColorProc(HWND dlg, UINT msg, WPARAM wp, LPARAM lp)
{
    switch (msg) {
    case WM_INITDIALOG:
        init_dialog(dlg);
        return TRUE;
    case WM_PAINT: {
        PAINTSTRUCT ps;
        RECT r;
        BeginPaint(dlg, &ps);
        ctl_rect(dlg, IDC_CO_SAMPLE, &r);
        draw_sample(ps.hdc, &r);
        if (expanded) {
            draw_grid(ps.hdc, IDC_CO_BASIC, basic, 48, basic_sel);
            draw_grid(ps.hdc, IDC_CO_CUSTOM, custom, 16, custom_sel);
        }
        EndPaint(dlg, &ps);
        return TRUE;
    }
    case WM_MEASUREITEM: {
        MEASUREITEMSTRUCT FAR *m = (MEASUREITEMSTRUCT FAR *)lp;
        HDC dc = GetDC(dlg);
        TEXTMETRIC tm;
        HFONT old = SelectObject(dc, (HFONT)SendMessage(dlg, WM_GETFONT, 0, 0));
        GetTextMetrics(dc, &tm);
        SelectObject(dc, old);
        ReleaseDC(dlg, dc);
        m->itemHeight = tm.tmHeight;
        return TRUE;
    }
    case WM_DRAWITEM: {
        DRAWITEMSTRUCT FAR *d = (DRAWITEMSTRUCT FAR *)lp;
        char name[80];
        RECT r;
        BOOL hi = (d->itemState & ODS_SELECTED) != 0;
        if (d->itemID == (UINT)-1) {
            if (d->itemAction & ODA_FOCUS)
                DrawFocusRect(d->hDC, &d->rcItem);
            return TRUE;
        }
        if (d->itemAction == ODA_FOCUS) {
            r = d->rcItem;
            InflateRect(&r, 1, 1);
            DrawFocusRect(d->hDC, &r);
            return TRUE;
        }
        scheme_name(d->itemID, name, sizeof name);
        /* observed: without the focus the highlight is one pixel inside
           the item; with it, the whole item, and the focus rectangle one
           pixel outside it (over the combo box's own frame) */
        r = d->rcItem;
        FillRect(d->hDC, &r, (HBRUSH)(COLOR_WINDOW + 1));
        if (!(d->itemState & ODS_FOCUS))
            InflateRect(&r, -1, -1);
        SetBkColor(d->hDC, GetSysColor(hi ? COLOR_HIGHLIGHT : COLOR_WINDOW));
        SetTextColor(d->hDC, GetSysColor(hi ? COLOR_HIGHLIGHTTEXT : COLOR_WINDOWTEXT));
        ExtTextOut(d->hDC, d->rcItem.left, d->rcItem.top, ETO_OPAQUE, &r,
                   name, lstrlen(name), NULL);
        if (d->itemState & ODS_FOCUS) {
            r = d->rcItem;
            InflateRect(&r, 1, 1);
            DrawFocusRect(d->hDC, &r);
        }
        return TRUE;
    }
    case WM_LBUTTONDOWN: {
        POINT pt;
        int k;
        pt.x = LOWORD(lp);
        pt.y = HIWORD(lp);
        if (!expanded)
            break;
        if ((k = grid_hit(IDC_CO_BASIC, 48, pt)) >= 0) {
            basic_focus = k;
            SetFocus(GetDlgItem(dlg, IDC_CO_BASIC));
            set_color(basic[k]);
        } else if ((k = grid_hit(IDC_CO_CUSTOM, 16, pt)) >= 0) {
            custom_focus = k;
            SetFocus(GetDlgItem(dlg, IDC_CO_CUSTOM));
            set_color(custom[k]);
        }
        return TRUE;
    }
    case WM_COMMAND:
        switch (wp) {
        case IDC_CO_SCHEMES:
            if (HIWORD(lp) == CBN_SELCHANGE) {
                int i = (int)SendDlgItemMessage(dlg, IDC_CO_SCHEMES, CB_GETCURSEL, 0, 0);
                if (i >= 0)
                    select_scheme(i);
            }
            return TRUE;
        case IDC_CO_ELEMENT:
            if (HIWORD(lp) == CBN_SELCHANGE) {
                int i = (int)SendDlgItemMessage(dlg, IDC_CO_ELEMENT, CB_GETCURSEL, 0, 0);
                if (i >= 0) {
                    element = element_order[i];
                    find_cells();
                }
            }
            return TRUE;
        case IDC_CO_PALETTE:
            expand();
            return TRUE;
        case IDC_CO_DEFINE:     /* observed: disabled while the selector is up */
            EnableWindow(GetDlgItem(dlg, IDC_CO_DEFINE), FALSE);
            DialogBox(g_hinst, MAKEINTRESOURCE(DLG_CUSTOMCOLOR), dlg, CustomColorProc);
            EnableWindow(GetDlgItem(dlg, IDC_CO_DEFINE), TRUE);
            return TRUE;
        case IDC_CO_SAVE:
            save_scheme();
            return TRUE;
        case IDC_CO_REMOVE:
            remove_scheme();
            return TRUE;
        case IDOK:
            apply();
            EndDialog(dlg, IDOK);
            return TRUE;
        case IDCANCEL:
            EndDialog(dlg, IDCANCEL);
            return TRUE;
        case IDC_HELP:
            cpl_help(dlg, AP_COLOR);
            return TRUE;
        }
        break;
    }
    return FALSE;
}

void color_applet(HWND parent)
{
    cpl_dialog(parent, DLG_COLOR, ColorProc);
}
