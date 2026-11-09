/*
 * control.c - w16 Control Panel: the window, the applet DLLs, CONTROL.INI.
 *
 * Observed on Windows 3.1 (tests/cpobs.sh; SNAP.EXE dumps, the CP1 applet
 * probe's log, screenshots measured pixel by pixel; ARCHITECTURE.md 14):
 *
 * Windows. The frame is class "CtlPanelClass", caption "Control Panel",
 * style WS_CAPTION | WS_SYSMENU | WS_THICKFRAME | WS_MINIMIZEBOX |
 * WS_CLIPCHILDREN (no maximize box). Its client holds two children: the
 * icon list, class "lb", ID 0x14, style WS_CHILD | WS_VISIBLE | WS_BORDER |
 * 0x0711 (the LBS_ bits of a multi-column owner-draw list box, but a class
 * of Control Panel's own), WS_VSCROLL added when the icons do not fit; and
 * the status line, class "Text", ID 0x15, WS_CHILD | WS_VISIBLE |
 * WS_BORDER, its text the selected applet's description. The list covers
 * the client's width and its height less the status line, overlapping the
 * status line's top border by one pixel.
 *
 * Icons. Cells are as wide as the widest applet name (in the 8 point MS
 * Sans Serif the names are drawn in) and SM_CYICON + 6 + the font height
 * (51) high, filled row by row. The icon sits 6 pixels below the cell top,
 * centred; the name right under it, centred; the selected applet's name
 * on COLOR_HIGHLIGHT exactly as wide as its text. The list shows
 * (list width - 18) / cell width columns.
 *
 * Applets. MAIN.CPL from the SYSTEM directory first, then each module named
 * in CONTROL.INI [MMCPL] (other than the NumApps/X/Y/W/H keys), then the
 * other *.CPL files of the SYSTEM directory in directory order. Calls:
 * CPL_INIT (0: unload), CPL_GETCOUNT, then per applet CPL_NEWINQUIRE and,
 * when that returns nonzero, CPL_INQUIRE. Choosing an applet: CPL_SELECT,
 * then CPL_DBLCLK (applet, lData). Closing: the window hides, CPL_STOP per
 * applet (last first), CPL_EXIT. The window exists (and is shown, at its
 * old or default size) while the applets load.
 *
 * CONTROL.INI [MMCPL]: NumApps=<applets> is written once they are loaded;
 * X=, Y=, W=, H= (the window's normal rectangle) when Control Panel
 * closes. At start-up the window takes X/Y/W/H (default 0, 0, 430, 240 on
 * a 640x480 screen); if NumApps differs from the saved value and the
 * icons do not fit, it is resized to six columns (fewer if the screen is
 * too narrow) and as many rows as needed (at most the screen's height).
 *
 * Command line: "CONTROL <applet name>" (any case, '&' ignored) opens
 * that applet with the window shown and disabled, then closes Control
 * Panel; "CONTROL <file>.CPL" loads only that file, without any window
 * (hwnd NULL in every call), and opens its first applet.
 */
#include "control.h"

extern void FAR PASCAL DOS3Call(void);      /* KERNEL.102: INT 21h */
extern int FAR PASCAL ShellAbout(HWND, LPCSTR, LPCSTR, HICON);  /* SHELL.22 */

static void mem_zero(void FAR *p, unsigned n)
{
    char FAR *c = p;
    while (n--)
        *c++ = 0;
}

static HINSTANCE hinst;
static HWND hframe, hlist, hstatus;
static HMENU hsettings;
static HFONT hfont;
static int font_h;                      /* tmHeight of hfont */
static HFONT hsfont;                    /* the status line's */
static int sfont_h;
static char s_control_ini[] = "CONTROL.INI";
static char s_mmcpl[] = "MMCPL";
static char cls_frame[] = "CtlPanelClass";
static char cls_list[] = "lb";
static char cls_text[] = "Text";
static char s_helpfile[] = "CONTROL.HLP";

/* ---- the applets ---- */

typedef struct {
    HINSTANCE h;
    APPLET_PROC proc;
} MODULE;

typedef struct {
    int mod;                            /* index in modules */
    int num;                            /* applet number in its module */
    LONG data;                          /* lData */
    HICON icon;
    int width;                          /* name's text extent */
    DWORD helpctx;
    char name[32];                      /* '&' removed */
    char menu[36];                      /* as given, for the menu */
    char info[64];
    char help[128];
} APPLET;

static HGLOBAL hmods, happs;
static MODULE FAR *mods;
static APPLET FAR *apps;
static int nmods, napps, capmods, capapps;
static int cur;                         /* selected applet */
static int cell_w, cell_h;
static int top_row;                     /* first row shown */
static BOOL cmdline_mode;               /* opened for one applet: exit after */

static BOOL grow(HGLOBAL *h, void FAR * FAR *p, int *cap, int need, int size)
{
    HGLOBAL n;
    int c;
    if (need <= *cap)
        return TRUE;
    c = *cap ? *cap * 2 : 16;
    while (c < need)
        c *= 2;
    if (*h) {
        GlobalUnlock(*h);
        n = GlobalReAlloc(*h, (DWORD)c * size, GMEM_MOVEABLE | GMEM_ZEROINIT);
    } else
        n = GlobalAlloc(GMEM_MOVEABLE | GMEM_ZEROINIT, (DWORD)c * size);
    if (!n) {
        if (*h)
            *p = GlobalLock(*h);
        return FALSE;
    }
    *h = n;
    *p = GlobalLock(n);
    *cap = c;
    return TRUE;
}

static void str_n(char FAR *d, const char FAR *s, int max)
{
    while (--max > 0 && *s)
        *d++ = *s++;
    *d = 0;
}

/* name without '&' ("&&" keeps one) */
static void strip_amp(char FAR *d, const char FAR *s)
{
    for (; *s; s++) {
        if (*s == '&') {
            if (s[1] != '&')
                continue;
            s++;
        }
        *d++ = *s;
    }
    *d = 0;
}

static void status_text(const char FAR *s)
{
    if (hstatus)
        SetWindowText(hstatus, s);
}

/* load one applet DLL; FALSE if it gives no applets */
static BOOL load_module(const char FAR *path)
{
    HINSTANCE h;
    APPLET_PROC proc;
    int count;
    int i, m;
    char buf[160];
    UINT oldmode;

    if (hframe) {
        int n = LoadString(hinst, IDS_LOADING, buf, 40);
        str_n(buf + n, path, sizeof buf - n);
        status_text(buf);
    }
    oldmode = SetErrorMode(SEM_NOOPENFILEERRORBOX);
    h = LoadLibrary(path);
    SetErrorMode(oldmode);
    if ((UINT)h < 32)
        return FALSE;
    proc = (APPLET_PROC)GetProcAddress(h, "CPlApplet");
    if (!proc) {
        FreeLibrary(h);
        return FALSE;
    }
    if (!proc(hframe, CPL_INIT, 0, 0)) {     /* the whole LONG (observed) */
        FreeLibrary(h);
        return FALSE;
    }
    count = (int)LOWORD(proc(hframe, CPL_GETCOUNT, 0, 0));  /* observed: DX ignored */
    if (count <= 0 || !grow(&hmods, (void FAR * FAR *)&mods, &capmods, nmods + 1, sizeof(MODULE))) {
        proc(hframe, CPL_EXIT, 0, 0);
        FreeLibrary(h);
        return FALSE;
    }
    m = nmods++;
    mods[m].h = h;
    mods[m].proc = proc;
    for (i = 0; i < count; i++) {
        static NEWCPLINFO ni;
        static CPLINFO ci;
        APPLET FAR *a;
        if (!grow(&happs, (void FAR * FAR *)&apps, &capapps, napps + 1, sizeof(APPLET)))
            break;
        a = &apps[napps];
        mem_zero(a, sizeof *a);
        a->mod = m;
        a->num = i;
        /* observed: only dwSize is cleared before CPL_NEWINQUIRE, and the
           answer is used when the applet set dwSize, whatever it returns;
           otherwise CPL_INQUIRE follows */
        ni.dwSize = 0;
        proc(hframe, CPL_NEWINQUIRE, i, (LPARAM)(NEWCPLINFO FAR *)&ni);
        if (ni.dwSize) {
            a->data = ni.lData;
            a->icon = ni.hIcon;
            a->helpctx = ni.dwHelpContext;
            str_n(a->menu, ni.szName, sizeof a->menu);
            str_n(a->info, ni.szInfo, sizeof a->info);
            str_n(a->help, ni.szHelpFile, sizeof a->help);
        } else {
            mem_zero(&ci, sizeof ci);
            /* UNVERIFIED: the answer is used whatever CPL_INQUIRE returns */
            proc(hframe, CPL_INQUIRE, i, (LPARAM)(CPLINFO FAR *)&ci);
            a->data = ci.lData;
            a->icon = LoadIcon(h, MAKEINTRESOURCE(ci.idIcon));
            LoadString(h, ci.idName, a->menu, sizeof a->menu);
            LoadString(h, ci.idInfo, a->info, sizeof a->info);
        }
        strip_amp(a->name, a->menu);
        napps++;
    }
    return TRUE;
}

/* DOS find first/next (INT 21h 4Eh/4Fh through DOS3Call) over a DTA */
static struct {
    char reserved[21];
    char attr;
    WORD time, date;
    DWORD size;
    char name[13];
} dta;

static BOOL dos_find(const char FAR *spec, BOOL first)
{
    BOOL ok = FALSE;
    WORD dseg = SELECTOROF(spec), doff = OFFSETOF(spec);
    void FAR *pd = &dta;
    WORD tseg = SELECTOROF(pd), toff = OFFSETOF(pd);
    _asm {
        push ds
        mov dx, toff
        mov ds, tseg
        mov ah, 1Ah
        call DOS3Call
        pop ds
        push ds
        mov dx, doff
        mov ds, dseg
        xor cx, cx
        mov ah, 4Eh
        cmp first, 0
        jne go
        mov ah, 4Fh
    go:
        call DOS3Call
        pop ds
        mov ax, 1
        jnc done
        xor ax, ax
    done:
        mov ok, ax
    }
    return ok;
}

static int dir_join(char *buf, int size, const char FAR *name)
{
    int n = GetSystemDirectory(buf, size - 14);
    if (n && buf[n - 1] != '\\')
        buf[n++] = '\\';
    str_n(buf + n, name, size - n);
    return n;
}

/* a buffer holding a whole [MMCPL] key list (it grows until it fits) */
static HLOCAL mmcpl_keys(void)
{
    UINT size = 256;
    for (;;) {
        HLOCAL h = LocalAlloc(LMEM_FIXED, size);
        int n;
        if (!h)
            return NULL;
        n = GetPrivateProfileString(s_mmcpl, NULL, "", (char *)h, size, s_control_ini);
        if ((UINT)n < size - 2 || size >= 0x8000)
            return h;
        LocalFree(h);
        size *= 2;
    }
}

static BOOL is_geometry_key(const char *k)
{
    return !lstrcmpi(k, "NumApps") || !lstrcmpi(k, "X") || !lstrcmpi(k, "Y")
        || !lstrcmpi(k, "W") || !lstrcmpi(k, "H");
}

static void load_all(void)
{
    char path[160], val[160];
    HLOCAL keys;
    int n;

    dir_join(path, sizeof path, "MAIN.CPL");
    load_module(path);
    if ((keys = mmcpl_keys()) != NULL) {
        const char *k;
        for (k = (const char *)keys; *k; k += lstrlen(k) + 1) {
            if (is_geometry_key(k))
                continue;
            GetPrivateProfileString(s_mmcpl, k, "", val, sizeof val, s_control_ini);
            if (val[0])
                load_module(val);
        }
        LocalFree(keys);
    }
    /* the SYSTEM directory's other *.CPL files, in directory order; all
       listed first, since loading a library moves the DTA */
    n = dir_join(path, sizeof path, "*.CPL");
    {
        HLOCAL list = NULL, nl;
        UINT used = 0, size = 0, len, off;
        char *p;
        BOOL more;
        for (more = dos_find(path, TRUE); more; more = dos_find(path, FALSE)) {
            if (!lstrcmpi(dta.name, "MAIN.CPL"))
                continue;
            len = lstrlen(dta.name) + 1;
            if (used + len > size) {
                size = size ? size * 2 : 128;
                nl = list ? LocalReAlloc(list, size, LMEM_MOVEABLE)
                          : LocalAlloc(LMEM_MOVEABLE, size);
                if (!nl)
                    break;
                list = nl;
            }
            p = LocalLock(list);
            lstrcpy(p + used, dta.name);
            LocalUnlock(list);
            used += len;
        }
        for (off = 0; off < used; off += len) {
            p = LocalLock(list);
            str_n(path + n, p + off, sizeof path - n);
            len = lstrlen(p + off) + 1;
            LocalUnlock(list);
            load_module(path);
        }
        if (list)
            LocalFree(list);
    }
}

/* ---- layout ---- */

static void measure(void)
{
    HDC dc = GetDC(NULL);
    HFONT old = SelectObject(dc, hfont);
    int i;
    cell_w = 1;
    for (i = 0; i < napps; i++) {
        apps[i].width = LOWORD(GetTextExtent(dc, apps[i].name, lstrlen(apps[i].name)));
        if (apps[i].width > cell_w)
            cell_w = apps[i].width;
    }
    SelectObject(dc, old);
    ReleaseDC(NULL, dc);
    cell_h = GetSystemMetrics(SM_CYICON) + 6 + font_h;
}

/* The list and the status line hang one border width over the frame's
   client edge, so their borders fall on the frame's inner line; the list's
   bottom border is the status line's top border (observed: the list at
   client -1,-1). The list shows (client width - SM_CXVSCROLL + border) /
   cell width columns whether or not its scroll bar is shown. */
#define BX GetSystemMetrics(SM_CXBORDER)
#define BY GetSystemMetrics(SM_CYBORDER)

/* columns shown in a list window of outer width w */
static int columns_for(int w)
{
    int c = (w - BX - GetSystemMetrics(SM_CXVSCROLL)) / cell_w;
    return c < 1 ? 1 : c;
}

static int list_columns(void)
{
    RECT r;
    GetWindowRect(hlist, &r);
    return columns_for(r.right - r.left);
}

static int rows_for(int cols)
{
    return (napps + cols - 1) / cols;
}

/* the status line: the 10 point font's height + 6, and its borders */
static int status_height(void)
{
    return sfont_h + 6 + 2 * BY;
}

static void layout_children(void)
{
    RECT r;
    int sh = status_height();
    GetClientRect(hframe, &r);
    MoveWindow(hlist, -BX, -BY, r.right + 2 * BX, r.bottom - sh + 3 * BY, TRUE);
    MoveWindow(hstatus, -BX, r.bottom - sh + BY, r.right + 2 * BX, sh, TRUE);
}

static void update_scroll(void)
{
    RECT r;
    int rows, vis, max;
    if (!hlist || !cell_h)
        return;
    GetClientRect(hlist, &r);
    rows = rows_for(list_columns());
    vis = r.bottom / cell_h;
    max = rows - vis;
    if (max < 0)
        max = 0;
    if (top_row > max)
        top_row = max;
    SetScrollRange(hlist, SB_VERT, 0, max, FALSE);
    SetScrollPos(hlist, SB_VERT, top_row, TRUE);
}

/* window size for cols columns of applets (fewer if the screen is too
   narrow) and as many rows as they need, at most the screen. The frame
   adds 2 SM_CXFRAME across and 2 SM_CYFRAME + SM_CYCAPTION + SM_CYMENU
   down (observed: 46 pixels on VGA, one more than AdjustWindowRect's) */
static void size_for(int *w, int *h, int cols)
{
    int cw = cols * cell_w + GetSystemMetrics(SM_CXVSCROLL) - BX;
    int fx = 2 * GetSystemMetrics(SM_CXFRAME);
    int fy = 2 * GetSystemMetrics(SM_CYFRAME) + GetSystemMetrics(SM_CYCAPTION)
             + GetSystemMetrics(SM_CYMENU);
    if (cw + fx > GetSystemMetrics(SM_CXSCREEN))
        cw = GetSystemMetrics(SM_CXSCREEN) - fx;
    *w = cw + fx;
    *h = rows_for(columns_for(cw + 2 * BX)) * cell_h + status_height() - BY + fy;
    if (*h > GetSystemMetrics(SM_CYSCREEN))
        *h = GetSystemMetrics(SM_CYSCREEN);
}

static BOOL icons_fit(void)
{
    RECT r;
    GetClientRect(hlist, &r);
    return rows_for(list_columns()) * cell_h <= r.bottom;
}

/* ---- the icon list ("lb") ---- */

static void item_rect(int i, RECT *r)
{
    int cols = list_columns();
    int x = (i % cols) * cell_w, y = (i / cols - top_row) * cell_h;
    SetRect(r, x, y, x + cell_w, y + cell_h);
}

static void invalidate_item(int i)
{
    RECT r;
    if (i < 0 || i >= napps)
        return;
    item_rect(i, &r);
    InvalidateRect(hlist, &r, TRUE);
}

static void draw_item(HDC dc, int i)
{
    RECT r, t;
    APPLET FAR *a = &apps[i];
    int x;
    item_rect(i, &r);
    if (!RectVisible(dc, &r))
        return;
    if (a->icon)
        DrawIcon(dc, r.left + (cell_w - GetSystemMetrics(SM_CXICON)) / 2, r.top + 6, a->icon);
    x = r.left + (cell_w - a->width) / 2;
    SetRect(&t, x, r.top + 6 + GetSystemMetrics(SM_CYICON), x + a->width,
            r.top + 6 + GetSystemMetrics(SM_CYICON) + font_h);
    if (i == cur) {
        SetBkColor(dc, GetSysColor(COLOR_HIGHLIGHT));
        SetTextColor(dc, GetSysColor(COLOR_HIGHLIGHTTEXT));
        ExtTextOut(dc, x, t.top, ETO_OPAQUE | ETO_CLIPPED, &t, a->name, lstrlen(a->name), NULL);
    } else {
        SetBkColor(dc, GetSysColor(COLOR_WINDOW));
        SetTextColor(dc, GetSysColor(COLOR_WINDOWTEXT));
        ExtTextOut(dc, x, t.top, 0, NULL, a->name, lstrlen(a->name), NULL);
    }
}

static void ensure_visible(int i)
{
    RECT r;
    int row = i / list_columns(), vis;
    GetClientRect(hlist, &r);
    vis = r.bottom / cell_h;
    if (vis < 1)
        vis = 1;
    if (row < top_row)
        top_row = row;
    else if (row >= top_row + vis)
        top_row = row - vis + 1;
    else
        return;
    update_scroll();
    InvalidateRect(hlist, NULL, TRUE);
}

static void select_applet(int i)
{
    if (i < 0 || i >= napps)
        return;
    if (i != cur) {
        invalidate_item(cur);
        cur = i;
        invalidate_item(cur);
    }
    ensure_visible(i);
    status_text(apps[i].info);
}

static void open_applet(int i)
{
    APPLET FAR *a;
    if (i < 0 || i >= napps)
        return;
    a = &apps[i];
    select_applet(i);
    UpdateWindow(hframe);
    mods[a->mod].proc(hframe, CPL_SELECT, a->num, a->data);
    mods[a->mod].proc(hframe, CPL_DBLCLK, a->num, a->data);
}

static int hit(int x, int y)
{
    int cols = list_columns(), c = x / cell_w, i;
    if (x < 0 || y < 0 || c >= cols)
        return -1;
    i = (y / cell_h + top_row) * cols + c;
    return i < napps ? i : -1;
}

LRESULT FAR PASCAL __export ListProc(HWND w, UINT msg, WPARAM wp, LPARAM lp)
{
    switch (msg) {
    case WM_PAINT: {
        PAINTSTRUCT ps;
        HFONT old;
        int i;
        BeginPaint(w, &ps);
        old = SelectObject(ps.hdc, hfont);
        for (i = 0; i < napps; i++)
            draw_item(ps.hdc, i);
        SelectObject(ps.hdc, old);
        EndPaint(w, &ps);
        return 0;
    }
    case WM_LBUTTONDOWN:
    case WM_LBUTTONDBLCLK: {
        int i = hit(LOWORD(lp), HIWORD(lp));
        SetFocus(w);
        if (i >= 0) {
            select_applet(i);
            if (msg == WM_LBUTTONDBLCLK)
                open_applet(i);
        }
        return 0;
    }
    case WM_KEYDOWN: {
        int cols = list_columns(), i = cur;
        RECT r;
        GetClientRect(w, &r);
        switch (wp) {
        case VK_LEFT:  i--; break;
        case VK_RIGHT: i++; break;
        case VK_UP:    if (i - cols >= 0) i -= cols; break;
        case VK_DOWN:  if (i + cols < napps) i += cols; break;
        case VK_HOME:  i = 0; break;
        case VK_END:   i = napps - 1; break;
        case VK_PRIOR: i -= cols * (r.bottom / cell_h > 1 ? r.bottom / cell_h : 1); if (i < 0) i = cur % cols; break;
        case VK_NEXT:  i += cols * (r.bottom / cell_h > 1 ? r.bottom / cell_h : 1); while (i >= napps) i -= cols; break;
        case VK_RETURN: open_applet(cur); return 0;
        default: return DefWindowProc(w, msg, wp, lp);
        }
        select_applet(i);
        return 0;
    }
    case WM_VSCROLL: {
        RECT r;
        int vis, rows = rows_for(list_columns()), t = top_row;
        GetClientRect(w, &r);
        vis = r.bottom / cell_h;
        if (vis < 1)
            vis = 1;
        switch (wp) {
        case SB_LINEUP:        t--; break;
        case SB_LINEDOWN:      t++; break;
        case SB_PAGEUP:        t -= vis; break;
        case SB_PAGEDOWN:      t += vis; break;
        case SB_THUMBPOSITION: t = LOWORD(lp); break;
        case SB_TOP:           t = 0; break;
        case SB_BOTTOM:        t = rows; break;
        default: return 0;
        }
        if (t > rows - vis)
            t = rows - vis;
        if (t < 0)
            t = 0;
        if (t != top_row) {
            ScrollWindow(w, 0, (top_row - t) * cell_h, NULL, NULL);
            top_row = t;
            SetScrollPos(w, SB_VERT, t, TRUE);
            UpdateWindow(w);
        }
        return 0;
    }
    case WM_SIZE:
        update_scroll();
        return 0;
    case WM_GETDLGCODE:
        return DLGC_WANTARROWS | DLGC_WANTCHARS;
    }
    return DefWindowProc(w, msg, wp, lp);
}

/* ---- the status line ("Text") ---- */

LRESULT FAR PASCAL __export TextProc(HWND w, UINT msg, WPARAM wp, LPARAM lp)
{
    switch (msg) {
    case WM_PAINT: {
        PAINTSTRUCT ps;
        RECT r, box;
        char text[80];
        HBRUSH b;
        HFONT old;
        int n;
        BeginPaint(w, &ps);
        GetClientRect(w, &r);
        SetRect(&box, 8, 2, r.right - 8, r.bottom - 2);
        b = CreateSolidBrush(GetSysColor(COLOR_BTNSHADOW));
        SetRect(&r, box.left, box.top, box.right - 1, box.top + 1);
        FillRect(ps.hdc, &r, b);
        SetRect(&r, box.left, box.top, box.left + 1, box.bottom - 1);
        FillRect(ps.hdc, &r, b);
        DeleteObject(b);
        b = CreateSolidBrush(GetSysColor(COLOR_BTNHIGHLIGHT));
        SetRect(&r, box.left, box.bottom - 1, box.right, box.bottom);
        FillRect(ps.hdc, &r, b);
        SetRect(&r, box.right - 1, box.top, box.right, box.bottom);
        FillRect(ps.hdc, &r, b);
        DeleteObject(b);
        n = GetWindowText(w, text, sizeof text);
        old = SelectObject(ps.hdc, hsfont);
        SetBkMode(ps.hdc, TRANSPARENT);
        SetTextColor(ps.hdc, GetSysColor(COLOR_BTNTEXT));
        SetRect(&r, box.left + 1, box.top + 1, box.right - 1, box.bottom - 1);
        ExtTextOut(ps.hdc, box.left + 3, box.top + 1, ETO_CLIPPED, &r, text, n, NULL);
        SelectObject(ps.hdc, old);
        EndPaint(w, &ps);
        return 0;
    }
    case WM_SETTEXT:
        DefWindowProc(w, msg, wp, lp);
        InvalidateRect(w, NULL, TRUE);
        return TRUE;
    }
    return DefWindowProc(w, msg, wp, lp);
}

/* ---- the frame ---- */

static void put_int(const char *key, int v)
{
    char b[8];
    wsprintf(b, "%d", v);
    WritePrivateProfileString(s_mmcpl, key, b, s_control_ini);
}

static void save_placement(void)
{
    WINDOWPLACEMENT wp;
    wp.length = sizeof wp;
    GetWindowPlacement(hframe, &wp);
    put_int("X", wp.rcNormalPosition.left);
    put_int("Y", wp.rcNormalPosition.top);
    put_int("W", wp.rcNormalPosition.right - wp.rcNormalPosition.left);
    put_int("H", wp.rcNormalPosition.bottom - wp.rcNormalPosition.top);
}

static void unload_all(HWND w)
{
    int m, i;
    for (m = nmods - 1; m >= 0; m--) {
        for (i = napps - 1; i >= 0; i--)
            if (apps[i].mod == m)
                mods[m].proc(w, CPL_STOP, apps[i].num, apps[i].data);
        mods[m].proc(w, CPL_EXIT, 0, 0);
        FreeLibrary(mods[m].h);
    }
    nmods = napps = 0;
}

static void build_menu(void)
{
    int i;
    char item[48];
    /* observed: "<name>...", and a name without '&' gets its first
       letter as the mnemonic */
    for (i = 0; i < napps; i++) {
        const char FAR *p = apps[i].menu;
        int n = 0;
        while (*p && *p != '&')
            p++;
        if (!*p)
            item[n++] = '&';
        lstrcpy(item + n, apps[i].menu);
        lstrcat(item, "...");
        InsertMenu(hsettings, i, MF_BYPOSITION | MF_STRING, IDM_APPLET + i, item);
    }
}

static void help(HWND w)
{
    APPLET FAR *a = cur < napps ? &apps[cur] : NULL;
    if (a && a->help[0])
        WinHelp(w, a->help, HELP_CONTEXT, a->helpctx);
    else
        WinHelp(w, s_helpfile, HELP_INDEX, 0);
}

LRESULT FAR PASCAL __export FrameProc(HWND w, UINT msg, WPARAM wp, LPARAM lp)
{
    switch (msg) {
    case WM_SIZE:
        if (hlist)
            layout_children();
        break;
    case WM_SETFOCUS:
        if (hlist)
            SetFocus(hlist);
        return 0;
    case WM_COMMAND:
        if (wp >= IDM_APPLET && wp < IDM_APPLET + (WPARAM)napps) {
            open_applet(wp - IDM_APPLET);
            return 0;
        }
        switch (wp) {
        case IDM_EXIT:
            PostMessage(w, WM_CLOSE, 0, 0);
            return 0;
        case IDM_HELPCONTENTS:
            WinHelp(w, s_helpfile, HELP_INDEX, 0);
            return 0;
        case IDM_HELPSEARCH:
            WinHelp(w, s_helpfile, HELP_PARTIALKEY, (DWORD)(LPSTR)"");
            return 0;
        case IDM_HELPHELP:
            WinHelp(w, NULL, HELP_HELPONHELP, 0);
            return 0;
        case IDM_HELPAPPLET:
            help(w);
            return 0;
        case IDM_ABOUT: {
            char t[64];
            LoadString(hinst, IDS_ABOUT, t, sizeof t);
            ShellAbout(w, t, "", LoadIcon(hinst, MAKEINTRESOURCE(IDI_CONTROL)));
            return 0;
        }
        }
        break;
    case WM_SYSCOLORCHANGE:
    case WM_WININICHANGE:
        InvalidateRect(w, NULL, TRUE);
        if (hlist)
            InvalidateRect(hlist, NULL, TRUE);
        break;
    case WM_CLOSE:
        save_placement();
        ShowWindow(w, SW_HIDE);
        unload_all(w);
        DestroyWindow(w);
        return 0;
    case WM_DESTROY:
        WinHelp(w, s_helpfile, HELP_QUIT, 0);
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProc(w, msg, wp, lp);
}

static BOOL register_classes(void)
{
    WNDCLASS wc;
    mem_zero(&wc, sizeof wc);
    wc.hInstance = hinst;
    wc.hCursor = LoadCursor(NULL, IDC_ARROW);
    wc.lpfnWndProc = FrameProc;
    wc.hIcon = LoadIcon(hinst, MAKEINTRESOURCE(IDI_CONTROL));
    wc.hbrBackground = (HBRUSH)(COLOR_WINDOW + 1);
    wc.lpszClassName = cls_frame;
    if (!RegisterClass(&wc))
        return FALSE;
    wc.style = CS_DBLCLKS;
    wc.lpfnWndProc = ListProc;
    wc.hIcon = NULL;
    wc.lpszClassName = cls_list;
    if (!RegisterClass(&wc))
        return FALSE;
    wc.style = 0;
    wc.lpfnWndProc = TextProc;
    wc.hbrBackground = (HBRUSH)(COLOR_BTNFACE + 1);
    wc.lpszClassName = cls_text;
    return RegisterClass(&wc);
}

/* observed: the applet names in 8 point MS Sans Serif, the status line
   in 10 point */
static HFONT make_font(int points, int *height)
{
    HDC dc = GetDC(NULL);
    TEXTMETRIC tm;
    HFONT f, old;
    f = CreateFont(-MulDiv(points, GetDeviceCaps(dc, LOGPIXELSY), 72), 0, 0, 0, FW_NORMAL,
                   0, 0, 0, ANSI_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                   DEFAULT_QUALITY, VARIABLE_PITCH | FF_SWISS, "MS Sans Serif");
    old = SelectObject(dc, f);
    GetTextMetrics(dc, &tm);
    *height = tm.tmHeight;
    SelectObject(dc, old);
    ReleaseDC(NULL, dc);
    return f;
}

/* the command line's first word, upper case, and the whole tail */
static BOOL is_cpl_file(const char FAR *s)
{
    const char FAR *e = s;
    while (*e && *e != ' ')
        e++;
    return e - s > 4 && (e[-4] == '.') && (e[-3] | 0x20) == 'c' && (e[-2] | 0x20) == 'p'
           && (e[-1] | 0x20) == 'l';
}

static int find_applet(const char FAR *name)
{
    int i;
    for (i = 0; i < napps; i++)
        if (!lstrcmpi(apps[i].name, name))
            return i;
    return -1;
}

static void message(UINT id)
{
    char t[40], m[256];
    LoadString(hinst, IDS_TITLE, t, sizeof t);
    LoadString(hinst, id, m, sizeof m);
    MessageBox(hframe, m, t, MB_OK | MB_ICONEXCLAMATION);
}

/* "CONTROL file.CPL": that file only, no window */
static int run_file(LPSTR cmd)
{
    char path[160];
    int n = 0, i;
    while (cmd[n] && cmd[n] != ' ' && n < (int)sizeof path - 1) {
        path[n] = cmd[n];
        n++;
    }
    path[n] = 0;
    if (!load_module(path)) {
        message(IDS_NOCPL);
        return 1;
    }
    while (cmd[n] == ' ')
        n++;
    i = cmd[n] ? find_applet(cmd + n) : 0;  /* UNVERIFIED: a named applet */
    if (i < 0)
        i = 0;
    mods[apps[i].mod].proc(NULL, CPL_DBLCLK, apps[i].num, apps[i].data);
    /* observed: no CPL_STOP or CPL_EXIT in this mode */
    return 0;
}

int PASCAL WinMain(HINSTANCE hInstance, HINSTANCE hPrev, LPSTR cmd, int show)
{
    MSG m;
    HACCEL acc;
    HMENU menu;
    char title[40];
    int x, y, w, h, saved, want;

    (void)hPrev;
    hinst = hInstance;
    while (*cmd == ' ')
        cmd++;
    hfont = make_font(8, &font_h);
    hsfont = make_font(10, &sfont_h);
    if (*cmd && is_cpl_file(cmd))
        return run_file(cmd);
    if (!register_classes())
        return 1;
    LoadString(hinst, IDS_TITLE, title, sizeof title);
    menu = LoadMenu(hinst, MAKEINTRESOURCE(IDM_CONTROL));
    hsettings = GetSubMenu(menu, 0);
    x = GetPrivateProfileInt(s_mmcpl, "X", 0, s_control_ini);
    y = GetPrivateProfileInt(s_mmcpl, "Y", 0, s_control_ini);
    /* UNVERIFIED: the default size observed on 640x480 (430x240) is
       scaled with the screen */
    w = GetPrivateProfileInt(s_mmcpl, "W", MulDiv(GetSystemMetrics(SM_CXSCREEN), 43, 64), s_control_ini);
    h = GetPrivateProfileInt(s_mmcpl, "H", GetSystemMetrics(SM_CYSCREEN) / 2, s_control_ini);
    hframe = CreateWindow(cls_frame, title, WS_CAPTION | WS_SYSMENU | WS_THICKFRAME |
                          WS_MINIMIZEBOX | WS_CLIPCHILDREN, x, y, w, h, NULL, menu, hinst, NULL);
    if (!hframe)
        return 1;
    hlist = CreateWindow(cls_list, "", WS_CHILD | WS_VISIBLE | WS_BORDER | 0x0711,
                         0, 0, 0, 0, hframe, (HMENU)ID_LIST, hinst, NULL);
    hstatus = CreateWindow(cls_text, "", WS_CHILD | WS_VISIBLE | WS_BORDER,
                           0, 0, 0, 0, hframe, (HMENU)ID_STATUS, hinst, NULL);
    layout_children();
    ShowWindow(hframe, show);
    UpdateWindow(hframe);

    load_all();
    if (!napps) {
        message(IDS_NOCPL);
        DestroyWindow(hframe);
        return 1;
    }
    saved = GetPrivateProfileInt(s_mmcpl, "NumApps", -1, s_control_ini);
    put_int("NumApps", napps);
    measure();
    build_menu();
    if (saved != napps && !icons_fit()) {
        size_for(&w, &want, 6);
        MoveWindow(hframe, x, y, w, want, TRUE);
    }
    layout_children();
    update_scroll();
    cur = 0;
    select_applet(0);
    InvalidateRect(hlist, NULL, TRUE);
    SetFocus(hlist);

    if (*cmd) {
        int i = find_applet(cmd);
        if (i >= 0) {
            cmdline_mode = TRUE;
            UpdateWindow(hframe);
            EnableWindow(hframe, FALSE);
            open_applet(i);
            EnableWindow(hframe, TRUE);
            PostMessage(hframe, WM_CLOSE, 0, 0);
        }
    }

    acc = LoadAccelerators(hinst, MAKEINTRESOURCE(IDA_CONTROL));
    while (GetMessage(&m, NULL, 0, 0)) {
        if (!TranslateAccelerator(hframe, acc, &m)) {
            TranslateMessage(&m);
            DispatchMessage(&m);
        }
    }
    DeleteObject(hfont);
    DeleteObject(hsfont);
    return m.wParam;
}
