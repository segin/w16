/*
 * group.c - program groups: .GRP files and the PMGroup MDI child windows.
 *
 * .GRP layout, as observed in files written by Windows 3.1 Setup
 * (tests: .tools/ref/win31; offsets in bytes, all little-endian):
 *   00  "PMCC"
 *   04  checksum: makes the 16-bit word sum of the file zero (an odd
 *       final byte is summed as a word with a zero high byte)
 *   06  cbGroup: size of the group data (the whole file so far)
 *   08  nCmdShow of the group window
 *   0A  rcNormal: window rectangle in MDI client coordinates
 *   12  ptMin: position when minimized (-1,-1 = not set)
 *   16  pName: offset of the group name
 *   18  cxIcon, cyIcon (32, 32)
 *   1C  two bytes 01 04, 1E a word 0000 (meaning not yet known)
 *   20  cItems: number of item slots
 *   22  rgiItems[cItems]: item offsets, 0 = empty slot
 * Item (24 bytes): x, y (icon position in client coordinates), iIcon,
 *   cbHeader, cbANDPlane, cbXORPlane, pHeader, pANDPlane, pXORPlane,
 *   pName, pCommand, pIconPath. The header is 12 bytes: hotspot x, y,
 *   cx, cy, AND row bytes, planes, bits per pixel. The icon bits are in
 *   the display driver's own format (VGA: 4 planes of 1 bit).
 * Data after cbGroup (tags: working directory, hotkey, run minimized) is
 * not written by Setup and is not interpreted yet.
 */
#include "progman.h"

#define MAX_ITEMS       64
#define CX_SPACING      75              /* observed item grid */
#define CY_SPACING      72
#define X_FIRST         21
/* observed: the icon is drawn 2 pixels right of and below the item's
   position, its title starts 1 pixel below the icon */
#define ICON_DX         2
#define ICON_DY         2
#define TITLE_DY        (ICON_DY + 32 + 1)

typedef struct {
    int x, y;
    HICON icon;
    WORD pname, pcmd, picon;
    BOOL own_icon;                      /* we created it: destroy it */
} item;

/* Each group lives in its own global memory block, not in the 64K
   automatic data segment, so the number and size of groups is not
   limited by the local heap. The block stays locked while the group
   exists. (TODO for the real-mode Windows 2.x build: lock per use.) */
typedef struct {
    HGLOBAL hself;                      /* this block */
    HWND hwnd;
    HGLOBAL hdata;                      /* the .GRP file contents */
    WORD size;
    int ininum;                         /* N of GroupN= in PROGMAN.INI */
    char path[128];
    int nitems;
    item items[MAX_ITEMS];
    int sel;                            /* selected item, -1 = none */
} group;

static group FAR *groups[MAX_GROUPS];
static HFONT title_font;
static HICON def_icon;
static char cls_group[] = "PMGroup";

/* ---- .GRP access ---- */

static WORD rd16(const BYTE FAR *p, WORD off)
{
    return p[off] | (p[off + 1] << 8);
}

static void wr16(BYTE FAR *p, WORD off, WORD v)
{
    p[off] = (BYTE)v;
    p[off + 1] = (BYTE)(v >> 8);
}

/* Is a NUL-terminated string at off inside the data? */
static BOOL str_ok(const BYTE FAR *p, WORD size, WORD off)
{
    if (off == 0 || off >= size)
        return FALSE;
    while (off < size)
        if (!p[off++])
            return TRUE;
    return FALSE;
}

static WORD checksum(const BYTE FAR *p, WORD size)
{
    WORD sum = 0, i;
    for (i = 0; i + 1 < size; i += 2)
        sum += rd16(p, i);
    if (size & 1)
        sum += p[size - 1];
    return sum;
}

static group FAR *grp_of(HWND hwnd)
{
    int i;
    for (i = 0; i < MAX_GROUPS; i++)
        if (groups[i] && groups[i]->hwnd == hwnd)
            return groups[i];
    return NULL;
}

static HICON item_icon(group FAR *g, const BYTE FAR *p, WORD it, BOOL FAR *own)
{
    WORD cbh = rd16(p, it + 6), cba = rd16(p, it + 8), cbx = rd16(p, it + 10);
    WORD ph = rd16(p, it + 12), pa = rd16(p, it + 14), px = rd16(p, it + 16);
    HDC dc;
    int planes, bits;
    HICON h;

    *own = FALSE;
    if (cbh < 12 || ph + 12 > g->size || pa + cba > g->size || px + cbx > g->size)
        return def_icon;
    /* The cached bits are device dependent: use them only if they match
       the display. TODO: otherwise extract the icon from pIconPath. */
    dc = GetDC(NULL);
    planes = GetDeviceCaps(dc, PLANES);
    bits = GetDeviceCaps(dc, BITSPIXEL);
    ReleaseDC(NULL, dc);
    if (p[ph + 10] != planes || p[ph + 11] != bits)
        return def_icon;
    h = CreateIcon(g_hinst, rd16(p, ph + 4), rd16(p, ph + 6), p[ph + 10], p[ph + 11],
                   p + pa, p + px);
    if (!h)
        return def_icon;
    *own = TRUE;
    return h;
}

static BOOL parse(group FAR *g)
{
    const BYTE FAR *p = (const BYTE FAR *)GlobalLock(g->hdata);
    WORD n, i;
    BOOL ok = FALSE;

    if (g->size < 0x22 || p[0] != 'P' || p[1] != 'M' || p[2] != 'C' || p[3] != 'C')
        goto out;
    if (rd16(p, 6) > g->size)
        goto out;
    g->size = rd16(p, 6);
    n = rd16(p, 0x20);
    if (0x22 + 2 * n > g->size || !str_ok(p, g->size, rd16(p, 0x16)))
        goto out;
    g->nitems = 0;
    for (i = 0; i < n && g->nitems < MAX_ITEMS; i++) {
        WORD it = rd16(p, 0x22 + 2 * i);
        item FAR *t = &g->items[g->nitems];
        if (!it)
            continue;                   /* empty slot */
        if (it + 24 > g->size)
            continue;
        t->x = (int)rd16(p, it);
        t->y = (int)rd16(p, it + 2);
        t->pname = rd16(p, it + 18);
        t->pcmd = rd16(p, it + 20);
        t->picon = rd16(p, it + 22);
        if (!str_ok(p, g->size, t->pname) || !str_ok(p, g->size, t->pcmd))
            continue;
        if (!str_ok(p, g->size, t->picon))
            t->picon = 0;
        t->icon = item_icon(g, p, it, &t->own_icon);
        g->nitems++;
    }
    ok = TRUE;
out:
    GlobalUnlock(g->hdata);
    return ok;
}

static BOOL read_file(group FAR *g)
{
    OFSTRUCT of;
    HFILE f = OpenFile(g->path, &of, OF_READ);
    long len;
    BYTE FAR *p;

    if (f == HFILE_ERROR)
        return FALSE;
    len = _llseek(f, 0, 2);
    _llseek(f, 0, 0);
    if (len <= 0 || len > 0xFFF0L) {
        _lclose(f);
        return FALSE;
    }
    g->size = (WORD)len;
    g->hdata = GlobalAlloc(GMEM_MOVEABLE, len);
    if (!g->hdata) {
        _lclose(f);
        return FALSE;
    }
    p = (BYTE FAR *)GlobalLock(g->hdata);
    if (_lread(f, p, g->size) != g->size) {
        GlobalUnlock(g->hdata);
        _lclose(f);
        return FALSE;
    }
    GlobalUnlock(g->hdata);
    _lclose(f);
    return TRUE;
}

static group FAR *new_group(void)
{
    HGLOBAL h = GlobalAlloc(GMEM_MOVEABLE | GMEM_ZEROINIT, sizeof(group));
    group FAR *g;
    if (!h)
        return NULL;
    g = (group FAR *)GlobalLock(h);
    if (!g) {
        GlobalFree(h);
        return NULL;
    }
    g->hself = h;
    return g;
}

static void free_group(group FAR *g)
{
    HGLOBAL h = g->hself;
    int i;
    for (i = 0; i < g->nitems; i++)
        if (g->items[i].own_icon)
            DestroyIcon(g->items[i].icon);
    if (g->hdata)
        GlobalFree(g->hdata);
    GlobalUnlock(h);
    GlobalFree(h);
}

/* Create the window for a group file. ininum is N of GroupN=. */
HWND group_load(const char *grpfile, int ininum)
{
    group FAR *g = new_group();
    MDICREATESTRUCT mc;
    const BYTE FAR *p;
    char name[64];
    int slot, show;
    RECT rc;
    POINT pmin;

    if (!g)
        return NULL;
    for (slot = 0; slot < MAX_GROUPS && groups[slot]; slot++)
        ;
    if (slot == MAX_GROUPS) {
        free_group(g);
        return NULL;
    }
    s_cpy(g->path, grpfile, sizeof g->path);
    g->ininum = ininum;
    g->sel = -1;
    if (!read_file(g) || !parse(g)) {
        free_group(g);
        return NULL;
    }
    p = (const BYTE FAR *)GlobalLock(g->hdata);
    s_cpy(name, (const char FAR *)p + rd16(p, 0x16), sizeof name);
    show = rd16(p, 8);
    rc.left = (int)rd16(p, 0x0A);
    rc.top = (int)rd16(p, 0x0C);
    rc.right = (int)rd16(p, 0x0E);
    rc.bottom = (int)rd16(p, 0x10);
    pmin.x = (int)rd16(p, 0x12);
    pmin.y = (int)rd16(p, 0x14);
    GlobalUnlock(g->hdata);
    if (g->nitems)
        g->sel = 0;
    groups[slot] = g;

    mc.szClass = cls_group;
    mc.szTitle = name;
    mc.hOwner = g_hinst;
    mc.x = rc.left;
    mc.y = rc.top;
    mc.cx = rc.right - rc.left;
    mc.cy = rc.bottom - rc.top;
    mc.style = 0x0115;                  /* observed low word of the style */
    mc.lParam = 0;
    g->hwnd = (HWND)(WORD)SendMessage(g_client, WM_MDICREATE, 0, (LPARAM)(LPVOID)&mc);
    if (!g->hwnd) {
        groups[slot] = NULL;
        free_group(g);
        return NULL;
    }
    /* observed: group windows accept dropped files (Windows 3.1) */
    DragAcceptFiles(g->hwnd, TRUE);
    /* each new group goes on top, so creating them in Order= sequence
       rebuilds the saved z-order */
    SetWindowPos(g->hwnd, HWND_TOP, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
    if (pmin.x != -1 || pmin.y != -1) {
        /* where the group sits when minimized */
        WINDOWPLACEMENT wp;
        wp.length = sizeof wp;
        GetWindowPlacement(g->hwnd, &wp);
        wp.flags = WPF_SETMINPOSITION;
        wp.ptMinPosition = pmin;
        wp.showCmd = SW_SHOWNA;
        SetWindowPlacement(g->hwnd, &wp);
    }
    if (show == SW_SHOWMINIMIZED || show == SW_MINIMIZE || show == SW_SHOWMINNOACTIVE)
        ShowWindow(g->hwnd, SW_SHOWMINNOACTIVE);
    else if (show == SW_SHOWMAXIMIZED)
        SendMessage(g_client, WM_MDIMAXIMIZE, (WPARAM)g->hwnd, 0);
    return g->hwnd;
}

/* Write the window state back into each .GRP (Save Settings on Exit). */
void group_save_all(void)
{
    int i;
    for (i = 0; i < MAX_GROUPS; i++) {
        group FAR *g = groups[i];
        WINDOWPLACEMENT wp;
        BYTE FAR *p;
        OFSTRUCT of;
        HFILE f;
        WORD show;
        if (!g)
            continue;
        wp.length = sizeof wp;
        GetWindowPlacement(g->hwnd, &wp);
        show = IsIconic(g->hwnd) ? SW_SHOWMINIMIZED :
               IsZoomed(g->hwnd) ? SW_SHOWMAXIMIZED : SW_SHOWNORMAL;
        p = (BYTE FAR *)GlobalLock(g->hdata);
        wr16(p, 8, show);
        wr16(p, 0x0A, wp.rcNormalPosition.left);
        wr16(p, 0x0C, wp.rcNormalPosition.top);
        wr16(p, 0x0E, wp.rcNormalPosition.right);
        wr16(p, 0x10, wp.rcNormalPosition.bottom);
        if (wp.flags & WPF_SETMINPOSITION || IsIconic(g->hwnd)) {
            wr16(p, 0x12, wp.ptMinPosition.x);
            wr16(p, 0x14, wp.ptMinPosition.y);
        }
        wr16(p, 4, 0);
        wr16(p, 4, (WORD)-checksum(p, g->size));
        f = OpenFile(g->path, &of, OF_WRITE);
        if (f != HFILE_ERROR) {
            _lwrite(f, (LPCSTR)p, g->size);
            _lclose(f);
        }
        GlobalUnlock(g->hdata);
    }
}

void group_destroy_all(void)
{
    int i;
    for (i = 0; i < MAX_GROUPS; i++)
        if (groups[i]) {
            free_group(groups[i]);
            groups[i] = NULL;
        }
}

int group_ininum(HWND hwnd)
{
    group FAR *g = grp_of(hwnd);
    return g ? g->ininum : 0;
}

/* ---- layout and painting ---- */

static void title_rect(HDC dc, group FAR *g, int i, RECT *r)
{
    const BYTE FAR *p = (const BYTE FAR *)GlobalLock(g->hdata);
    item FAR *t = &g->items[i];
    HFONT old = title_font ? SelectObject(dc, title_font) : NULL;
    int cx = t->x + ICON_DX + 16;
    r->left = cx - CX_SPACING / 2 + 1;
    r->right = cx + CX_SPACING / 2 - 1;
    r->top = t->y + TITLE_DY;
    r->bottom = r->top + 100;
    DrawText(dc, (LPCSTR)p + t->pname, -1, r, DT_CALCRECT | DT_CENTER | DT_WORDBREAK | DT_NOPREFIX);
    /* DT_CALCRECT keeps the left edge; centre the result under the icon */
    {
        int w = r->right - r->left;
        r->left = cx - w / 2;
        r->right = r->left + w;
    }
    /* observed highlight: 2 pixels either side of the text and 2 extra
       rows below it (seen for one-line titles) */
    InflateRect(r, 2, 0);
    r->bottom += 2;
    if (old)
        SelectObject(dc, old);
    GlobalUnlock(g->hdata);
}

static void paint(HWND hwnd, group FAR *g)
{
    PAINTSTRUCT ps;
    HDC dc = BeginPaint(hwnd, &ps);
    const BYTE FAR *p = (const BYTE FAR *)GlobalLock(g->hdata);
    HFONT old = title_font ? SelectObject(dc, title_font) : NULL;
    /* observed: the selection is highlighted only in the active group
       while Program Manager itself is the active window */
    BOOL active = (HWND)(WORD)SendMessage(g_client, WM_MDIGETACTIVE, 0, 0) == hwnd &&
                  GetActiveWindow() == g_frame;
    int i;

    for (i = 0; i < g->nitems; i++) {
        item FAR *t = &g->items[i];
        RECT r;
        BOOL hi = active && i == g->sel;
        DrawIcon(dc, t->x + ICON_DX, t->y + ICON_DY, t->icon);
        title_rect(dc, g, i, &r);
        SetBkMode(dc, hi ? OPAQUE : TRANSPARENT);
        SetBkColor(dc, GetSysColor(COLOR_HIGHLIGHT));
        SetTextColor(dc, GetSysColor(hi ? COLOR_HIGHLIGHTTEXT : COLOR_WINDOWTEXT));
        if (hi) {
            HBRUSH b = CreateSolidBrush(GetSysColor(COLOR_HIGHLIGHT));
            FillRect(dc, &r, b);
            DeleteObject(b);
        }
        InflateRect(&r, -2, 0);
        r.bottom -= 2;
        DrawText(dc, (LPCSTR)p + t->pname, -1, &r, DT_CENTER | DT_WORDBREAK | DT_NOPREFIX);
    }
    if (old)
        SelectObject(dc, old);
    GlobalUnlock(g->hdata);
    EndPaint(hwnd, &ps);
}

static int hit(HWND hwnd, group FAR *g, int x, int y)
{
    HDC dc = GetDC(hwnd);
    int i, found = -1;
    for (i = 0; i < g->nitems && found < 0; i++) {
        item FAR *t = &g->items[i];
        RECT r;
        if (x >= t->x + ICON_DX && x < t->x + ICON_DX + 32 &&
            y >= t->y + ICON_DY && y < t->y + ICON_DY + 32)
            found = i;
        title_rect(dc, g, i, &r);
        if (x >= r.left && x < r.right && y >= r.top && y < r.bottom)
            found = i;
    }
    ReleaseDC(hwnd, dc);
    return found;
}

static void invalidate_item(HWND hwnd, group FAR *g, int i)
{
    HDC dc;
    RECT r;
    if (i < 0 || i >= g->nitems)
        return;
    dc = GetDC(hwnd);
    title_rect(dc, g, i, &r);
    ReleaseDC(hwnd, dc);
    InvalidateRect(hwnd, &r, TRUE);
}

static void select_item(HWND hwnd, group FAR *g, int i)
{
    if (i == g->sel)
        return;
    invalidate_item(hwnd, g, g->sel);
    g->sel = i;
    invalidate_item(hwnd, g, g->sel);
}

/* Keyboard navigation: the nearest item in the given direction. */
static int neighbour(group FAR *g, int dx, int dy)
{
    item FAR *c;
    int i, best = -1;
    long bestd = 0x7FFFFFFFL;
    if (g->sel < 0)
        return g->nitems ? 0 : -1;
    c = &g->items[g->sel];
    for (i = 0; i < g->nitems; i++) {
        item FAR *t = &g->items[i];
        int ddx = t->x - c->x, ddy = t->y - c->y;
        long d;
        if (i == g->sel)
            continue;
        if ((dx > 0 && ddx <= 0) || (dx < 0 && ddx >= 0) ||
            (dy > 0 && ddy <= 0) || (dy < 0 && ddy >= 0))
            continue;
        /* prefer the same row or column */
        d = dx ? (long)(ddx < 0 ? -ddx : ddx) + 8L * (ddy < 0 ? -ddy : ddy)
               : (long)(ddy < 0 ? -ddy : ddy) + 8L * (ddx < 0 ? -ddx : ddx);
        if (d < bestd) {
            bestd = d;
            best = i;
        }
    }
    return best;
}

BOOL group_open_selected(HWND hwnd)
{
    group FAR *g = grp_of(hwnd);
    const BYTE FAR *p;
    char cmd[128];
    if (!g || g->sel < 0)
        return FALSE;
    p = (const BYTE FAR *)GlobalLock(g->hdata);
    s_cpy(cmd, (const char FAR *)p + g->items[g->sel].pcmd, sizeof cmd);
    GlobalUnlock(g->hdata);
    if (pm_exec(g_frame, cmd, NULL, FALSE) && g_minonrun)
        ShowWindow(g_frame, SW_MINIMIZE);
    return TRUE;
}

/* Arrange Icons: the observed grid, as many columns as fit. */
void group_arrange(HWND hwnd)
{
    group FAR *g = grp_of(hwnd);
    RECT rc;
    int cols, i;
    if (!g)
        return;
    GetClientRect(hwnd, &rc);
    cols = (rc.right - rc.left) / CX_SPACING;
    if (cols < 1)
        cols = 1;
    for (i = 0; i < g->nitems; i++) {
        g->items[i].x = X_FIRST + (i % cols) * CX_SPACING;
        g->items[i].y = (i / cols) * CY_SPACING;
    }
    InvalidateRect(hwnd, NULL, TRUE);
}

LRESULT CALLBACK __export GroupWndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp)
{
    group FAR *g = grp_of(hwnd);
    int i;

    switch (msg) {
    case WM_PAINT:
        if (g) {
            paint(hwnd, g);
            return 0;
        }
        break;
    case WM_MDIACTIVATE:
    case WM_NCACTIVATE:                 /* also sent when the frame changes */
        if (g)
            invalidate_item(hwnd, g, g->sel);
        break;
    case WM_SIZE:
        if (g && g_autoarrange && wp != SIZE_MINIMIZED)
            group_arrange(hwnd);
        break;
    case WM_LBUTTONDOWN:
        if (g && (i = hit(hwnd, g, (short)LOWORD(lp), (short)HIWORD(lp))) >= 0)
            select_item(hwnd, g, i);
        return 0;
    case WM_LBUTTONDBLCLK:
        if (g && (i = hit(hwnd, g, (short)LOWORD(lp), (short)HIWORD(lp))) >= 0) {
            select_item(hwnd, g, i);
            group_open_selected(hwnd);
        }
        return 0;
    case WM_KEYDOWN:
        if (!g)
            break;
        switch (wp) {
        case VK_LEFT:  i = neighbour(g, -1, 0); break;
        case VK_RIGHT: i = neighbour(g, 1, 0);  break;
        case VK_UP:    i = neighbour(g, 0, -1); break;
        case VK_DOWN:  i = neighbour(g, 0, 1);  break;
        case VK_HOME:  i = g->nitems ? 0 : -1;  break;
        case VK_END:   i = g->nitems - 1;       break;
        default:       return 0;
        }
        if (i >= 0)
            select_item(hwnd, g, i);
        return 0;
    case WM_CHAR:
        /* first letter jumps to the next item whose name starts with it */
        if (g && g->nitems) {
            const BYTE FAR *p = (const BYTE FAR *)GlobalLock(g->hdata);
            char c = (char)wp;
            int k;
            if (c >= 'a' && c <= 'z')
                c -= 'a' - 'A';
            for (k = 1; k <= g->nitems; k++) {
                int j = (g->sel + k) % g->nitems;
                char f = p[g->items[j].pname];
                if (f >= 'a' && f <= 'z')
                    f -= 'a' - 'A';
                if (f == c) {
                    select_item(hwnd, g, j);
                    break;
                }
            }
            GlobalUnlock(g->hdata);
        }
        return 0;
    case WM_DROPFILES:
        DragFinish((HDROP)wp);          /* TODO: add items for the files */
        return 0;
    case WM_DESTROY:
        if (g) {
            for (i = 0; i < MAX_GROUPS; i++)
                if (groups[i] == g)
                    groups[i] = NULL;
            free_group(g);
        }
        break;
    }
    return DefMDIChildProc(hwnd, msg, wp, lp);
}

BOOL group_register(void)
{
    WNDCLASS wc;
    LOGFONT lf;

    def_icon = LoadIcon(g_hinst, MAKEINTRESOURCE(IDI_DEFITEM));
    /* the icon title font, as the user configured it (Windows 3.1) */
    if (SystemParametersInfo(SPI_GETICONTITLELOGFONT, sizeof lf, &lf, 0))
        title_font = CreateFontIndirect(&lf);

    wc.style = CS_DBLCLKS;
    wc.lpfnWndProc = GroupWndProc;
    wc.cbClsExtra = 0;
    wc.cbWndExtra = 0;
    wc.hInstance = g_hinst;
    wc.hIcon = LoadIcon(g_hinst, MAKEINTRESOURCE(IDI_GROUP));
    wc.hCursor = LoadCursor(NULL, IDC_ARROW);
    wc.hbrBackground = (HBRUSH)(COLOR_WINDOW + 1);
    wc.lpszMenuName = NULL;
    wc.lpszClassName = cls_group;
    return RegisterClass(&wc) != 0;
}
