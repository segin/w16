/*
 * menu.c - menus: the API, the menu bar, popup menus and the menu loop.
 *
 * A menu is a block in USER's local heap (HMENU is its near handle) with an
 * array of items in another block; each item has its flags (MF_*), its id
 * or popup, and its text (a local copy) or bitmap. Menus are loaded from
 * the 3.x menu template (3.1 SDK: MENUITEMTEMPLATEHEADER, then items, a
 * popup's items following it, MF_END marking the last of each level).
 *
 * Measured on Windows 3.1 (ARCHITECTURE.md 10.4): the menu bar is
 * SM_CYMENU high plus a black line under it; each top-level item is its
 * text's width plus a system-font character cell (8 pixels on VGA) on each
 * side, the first starting at the bar's left edge; the text cell's top is
 * the bar's top; the mnemonic's underline is under the character cell,
 * one row below the font's ascent.
 */
#include "user.h"

HBRUSH sys_brush(HBRUSH b);
int cxSysCharMenu(void);

/* ---- the menu heaps ----
   Windows 3.1 keeps menus out of USER's DGROUP: at start-up USER
   allocates two moveable global blocks of 418h bytes (GMEM_DDESHARE |
   GMEM_MOVEABLE | GMEM_ZEROINIT) and makes each a local heap with
   LocalInit(h, 12h, 417h); menu structures and their item arrays go in
   the first, the items' strings in the second (Pietrek, Windows
   Internals, ch. 1, LoadWindows; Schulman, Undocumented Windows, ch. 6,
   "USER Heaps"). Probe US13 confirmed it on 3.1: both are local heaps in
   USER-owned 420h-byte blocks, a CreateMenu block (fixed, 14h bytes) and
   the item arrays are in one, item texts (their length + 1) in the other,
   and nothing of a menu lands in DGROUP; each grows to 64K (2712 empty
   menus; 2503 items in one menu; 510 items of 120 characters).
   An HMENU is the near handle of a fixed block in the menu heap. The
   heaps are reached through far pointers made from their selector
   (MK_FP), the Local* calls through uentry.asm's lh_* (DS switched to the
   heap's selector and back). A heap's selector is refreshed after each
   allocation in it (a moveable segment that grows could get a new one),
   so no far pointer into a heap is kept across a call that allocates. */
HGLOBAL hMenuHeap, hMenuStrHeap;
static WORD msel, ssel;         /* their selectors */

static WORD heap_sel(HGLOBAL h)
{
    return h ? HIWORD(GlobalHandle((UINT)h)) : 0;
}

static HGLOBAL make_heap(void)
{
    HGLOBAL h = GlobalAlloc(GMEM_DDESHARE | GMEM_MOVEABLE | GMEM_ZEROINIT, 0x418);
    if (h && !LocalInit(heap_sel(h), 0x12, 0x417)) {
        GlobalFree(h);
        h = 0;
    }
    return h;
}

/* user_init: the two heaps (3.1: after the global atom table) */
void menu_heaps_init(void)
{
    hMenuHeap = make_heap();
    hMenuStrHeap = make_heap();
    msel = heap_sel(hMenuHeap);
    ssel = heap_sel(hMenuStrHeap);
}

/* GetFreeSystemResources: the selectors of the menu heaps */
WORD menu_heap_sel(void)
{
    return msel;
}

WORD menu_str_heap_sel(void)
{
    return ssel;
}

static WORD m_alloc(UINT flags, UINT n)
{
    WORD r = msel ? lh_alloc(msel, flags, n) : 0;
    msel = heap_sel(hMenuHeap);
    return r;
}

static WORD m_realloc(WORD h, UINT n, UINT flags)
{
    WORD r = lh_realloc(msel, h, n, flags);
    msel = heap_sel(hMenuHeap);
    return r;
}

/* a copy of s in the string heap, 0 if it does not fit */
static WORD str_dup(LPCSTR s)
{
    WORD n = lstr_len(s) + 1, r = ssel ? lh_alloc(ssel, LMEM_FIXED, n) : 0;
    ssel = heap_sel(hMenuStrHeap);
    if (r)
        fmemcpy(MK_FP(ssel, r), s, n);
    return r;
}

#define STEXT(t)    ((LPCSTR)MK_FP(ssel, (t)))

typedef struct MITEM {          /* 1Ah bytes, as 3.1's ITEM (Schulman ch. 6) */
    UINT   flags;               /* MF_* */
    UINT   id;                  /* or the popup's HMENU */
    WORD   text;                /* string-heap handle, or an HBITMAP with MF_BITMAP */
    RECT   rc;                  /* where it was last laid out (menu coordinates) */
    WORD   hbmCheck, hbmUncheck;
    WORD   unused[4];           /* (to 3.1's size: as many items fit in 64K) */
} MITEM;
typedef MITEM __far *LPMITEM;

typedef struct MENU {           /* 14h bytes, as 3.1's (Schulman ch. 6; US13) */
    WORD   magic;               /* 'MU' */
    WORD   items;               /* menu-heap block of MITEMs */
    WORD   count;
    WORD   flags;               /* MENU_POPUP, MENU_SYSMENU */
    HWND   hwnd;                /* the window it is the bar or popup of */
    int    cx, cy;              /* laid-out size */
    int    tabx;                /* a popup's accelerator column */
    WORD   unused[2];
} MENU;
typedef MENU __far *LPMENU;
#define MENU_MAGIC   0x554D
#define MENU_POPUP   1
#define MENU_SYSMENU 2

static LPMENU valid_menu(HMENU h)
{
    LPMENU m;
    WORD o = (WORD)h;
    if (!o || (o & 1) || !msel || (DWORD)o + sizeof(MENU) - 1 > seg_limit(msel) ||
        lh_size(msel, o) < sizeof(MENU))
        return 0;
    m = (LPMENU)MK_FP(msel, o);
    return m->magic == MENU_MAGIC ? m : 0;
}

static LPMITEM item_at(LPMENU m, UINT i)
{
    return (LPMITEM)MK_FP(msel, m->items + i * sizeof(MITEM));
}

static HMENU new_menu(WORD flags)
{
    WORD h = m_alloc(LPTR, sizeof(MENU));
    LPMENU m;
    if (!h)
        return 0;
    m = (LPMENU)MK_FP(msel, h);
    m->magic = MENU_MAGIC;
    m->flags = flags;
    return (HMENU)h;
}

HMENU WINAPI CreateMenu(void)
{
    return new_menu(0);
}

HMENU WINAPI CreatePopupMenu(void)
{
    return new_menu(MENU_POPUP);
}

static void free_item(LPMITEM it)
{
    if (!(it->flags & (MF_BITMAP | MF_OWNERDRAW)) && it->text)
        lh_free(ssel, it->text);
    it->text = 0;
}

BOOL WINAPI DestroyMenu(HMENU h)
{
    LPMENU m = valid_menu(h);
    UINT i;
    if (!m)
        return FALSE;
    m->magic = 0;               /* (a menu that holds itself ends here) */
    for (i = 0; i < m->count; i++) {
        LPMITEM it = item_at(m, i);
        if (it->flags & MF_POPUP)
            DestroyMenu((HMENU)it->id);         /* frees only: m stays good */
        free_item(it);
    }
    if (m->items)
        lh_free(msel, m->items);
    lh_free(msel, (WORD)h);
    return TRUE;
}

BOOL WINAPI IsMenu(HMENU h)
{
    return valid_menu(h) != 0;
}

/* the index of an item by position, or by command searching popups too;
   the menu that holds it in *owner. -1: none */
static int find_item(HMENU h, UINT what, UINT flags, HMENU *owner)
{
    LPMENU m = valid_menu(h);
    UINT i;
    if (!m)
        return -1;
    if (flags & MF_BYPOSITION) {
        if (what >= m->count)
            return -1;
        *owner = h;
        return what;
    }
    for (i = 0; i < m->count; i++) {
        LPMITEM it = item_at(m, i);
        if ((it->flags & MF_POPUP) && (UINT)it->id != what) {
            int r = find_item((HMENU)it->id, what, flags, owner);
            if (r >= 0)
                return r;
        } else if (it->id == what) {
            *owner = h;
            return i;
        }
    }
    return -1;
}

/* the item itself (for a look or a change of its flags: nothing may be
   allocated while it is held) */
static LPMITEM find_it(HMENU h, UINT what, UINT flags)
{
    HMENU owner = 0;
    int i = find_item(h, what, flags, &owner);
    return i < 0 ? (LPMITEM)0 : item_at(valid_menu(owner), i);
}

/* an item's text field for flags and data: a string copied into the
   string heap (FALSE if it does not fit), a bitmap or owner-draw value */
static BOOL item_text(UINT flags, LPCSTR data, WORD *text)
{
    *text = 0;
    if (flags & (MF_BITMAP | MF_OWNERDRAW))
        *text = LOWORD((DWORD)data);
    else if (!(flags & MF_SEPARATOR) && data)
        return (*text = str_dup(data)) != 0;
    return TRUE;
}

static void store_item(LPMITEM it, UINT flags, UINT id, LPCSTR data, WORD text)
{
    it->flags = flags & ~(MF_BYPOSITION | MF_CHANGE | MF_APPEND | MF_DELETE | MF_REMOVE | MF_INSERT);
    it->id = id;
    it->text = text;
    if ((flags & MF_SEPARATOR) || (!data && !(flags & (MF_BITMAP | MF_OWNERDRAW | MF_POPUP))))
        it->flags |= MF_SEPARATOR;
}

static BOOL insert_item(HMENU h, UINT pos, UINT flags, UINT id, LPCSTR data)
{
    LPMENU m = valid_menu(h);
    HMENU owner = h;
    UINT at;
    WORD blk, text;
    if (!m)
        return FALSE;
    if (pos == (UINT)-1)
        at = m->count;
    else if (flags & MF_BYPOSITION)
        at = pos > m->count ? m->count : pos;
    else {
        int i = find_item(h, pos, 0, &owner);
        if (i < 0) {
            owner = h;
            at = m->count;
        } else
            at = i;
    }
    if (!item_text(flags, data, &text))
        return FALSE;
    m = valid_menu(owner);
    blk = m->items ? m_realloc(m->items, (m->count + 1) * sizeof(MITEM), LMEM_MOVEABLE | LMEM_ZEROINIT)
                   : m_alloc(LMEM_FIXED | LMEM_ZEROINIT, sizeof(MITEM));
    m = valid_menu(owner);              /* the heap's selector may be new */
    if (!blk) {
        if (text && !(flags & (MF_BITMAP | MF_OWNERDRAW)))
            lh_free(ssel, text);
        return FALSE;
    }
    m->items = blk;
    if (at < m->count)
        fmemcpy(item_at(m, at + 1), item_at(m, at), (m->count - at) * sizeof(MITEM));
    m->count++;
    {
        LPMITEM it = item_at(m, at);
        fmemset(it, 0, sizeof(MITEM));
        store_item(it, flags, id, data, text);
    }
    return TRUE;
}

BOOL WINAPI AppendMenu(HMENU h, UINT flags, UINT id, LPCSTR data)
{
    return insert_item(h, (UINT)-1, flags | MF_BYPOSITION, id, data);
}

BOOL WINAPI InsertMenu(HMENU h, UINT pos, UINT flags, UINT id, LPCSTR data)
{
    return insert_item(h, pos, flags, id, data);
}

static BOOL remove_item(HMENU h, UINT what, UINT flags, BOOL destroy)
{
    HMENU owner = 0;
    int i = find_item(h, what, flags, &owner);
    LPMENU m;
    LPMITEM it;
    if (i < 0)
        return FALSE;
    m = valid_menu(owner);
    it = item_at(m, i);
    if (destroy && (it->flags & MF_POPUP))
        DestroyMenu((HMENU)it->id);
    free_item(it);
    if ((UINT)i + 1 < m->count)
        fmemcpy(item_at(m, i), item_at(m, i + 1), (m->count - i - 1) * sizeof(MITEM));
    m->count--;
    return TRUE;
}

BOOL WINAPI RemoveMenu(HMENU h, UINT what, UINT flags)
{
    return remove_item(h, what, flags, FALSE);
}

BOOL WINAPI DeleteMenu(HMENU h, UINT what, UINT flags)
{
    return remove_item(h, what, flags, TRUE);
}

BOOL WINAPI ModifyMenu(HMENU h, UINT what, UINT flags, UINT id, LPCSTR data)
{
    LPMITEM it;
    WORD text;
    if (!find_it(h, what, flags) || !item_text(flags, data, &text))
        return FALSE;
    it = find_it(h, what, flags);       /* again: the string may have moved things */
    free_item(it);
    store_item(it, flags, id, data, text);
    return TRUE;
}

BOOL WINAPI ChangeMenu(HMENU h, UINT what, LPCSTR data, UINT id, UINT flags)
{
    if (flags & MF_APPEND)
        return AppendMenu(h, flags & ~MF_APPEND, id, data);
    if (flags & MF_DELETE)
        return DeleteMenu(h, what, flags & ~MF_DELETE);
    if (flags & MF_REMOVE)
        return RemoveMenu(h, what, flags & ~MF_REMOVE);
    if (flags & MF_CHANGE)
        return ModifyMenu(h, what, flags & ~MF_CHANGE, id, data);
    return InsertMenu(h, what, flags & ~MF_INSERT, id, data);
}

int WINAPI GetMenuItemCount(HMENU h)
{
    LPMENU m = valid_menu(h);
    return m ? m->count : -1;
}

UINT WINAPI GetMenuItemID(HMENU h, int pos)
{
    LPMENU m = valid_menu(h);
    LPMITEM it;
    if (!m || pos < 0 || (UINT)pos >= m->count)
        return (UINT)-1;
    it = item_at(m, pos);
    return (it->flags & MF_POPUP) ? (UINT)-1 : it->id;
}

HMENU WINAPI GetSubMenu(HMENU h, int pos)
{
    LPMENU m = valid_menu(h);
    LPMITEM it;
    if (!m || pos < 0 || (UINT)pos >= m->count)
        return 0;
    it = item_at(m, pos);
    return (it->flags & MF_POPUP) ? (HMENU)it->id : 0;
}

UINT WINAPI GetMenuState(HMENU h, UINT what, UINT flags)
{
    LPMITEM it = find_it(h, what, flags);
    if (!it)
        return (UINT)-1;
    if (it->flags & MF_POPUP) {
        LPMENU sub = valid_menu((HMENU)it->id);
        return ((sub ? sub->count : 0) << 8) | (it->flags & 0xFF);
    }
    return it->flags;
}

int WINAPI GetMenuString(HMENU h, UINT what, LPSTR buf, int n, UINT flags)
{
    LPMITEM it = find_it(h, what, flags);
    if (!it || n <= 0)
        return 0;
    if ((it->flags & (MF_BITMAP | MF_OWNERDRAW | MF_SEPARATOR)) || !it->text) {
        buf[0] = 0;
        return 0;
    }
    lstr_cpyn(buf, STEXT(it->text), n);
    return lstr_len(buf);
}

BOOL WINAPI CheckMenuItem(HMENU h, UINT what, UINT flags)
{
    LPMITEM it = find_it(h, what, flags);
    UINT old;
    if (!it)
        return (BOOL)-1;
    old = it->flags & MF_CHECKED;
    it->flags = (it->flags & ~MF_CHECKED) | (flags & MF_CHECKED);
    return old;
}

BOOL WINAPI EnableMenuItem(HMENU h, UINT what, UINT flags)
{
    LPMITEM it = find_it(h, what, flags);
    UINT old;
    if (!it)
        return (BOOL)-1;
    old = it->flags & (MF_GRAYED | MF_DISABLED);
    it->flags = (it->flags & ~(MF_GRAYED | MF_DISABLED)) | (flags & (MF_GRAYED | MF_DISABLED));
    return old;
}

BOOL WINAPI HiliteMenuItem(HWND hw, HMENU h, UINT what, UINT flags)
{
    LPMITEM it = find_it(h, what, flags);
    if (!it)
        return FALSE;
    it->flags = (it->flags & ~MF_HILITE) | (flags & MF_HILITE);
    DrawMenuBar(hw);
    return TRUE;
}

BOOL WINAPI SetMenuItemBitmaps(HMENU h, UINT what, UINT flags, HBITMAP un, HBITMAP ck)
{
    LPMITEM it = find_it(h, what, flags);
    if (!it)
        return FALSE;
    it->hbmUncheck = (WORD)un;
    it->hbmCheck = (WORD)ck;
    return TRUE;
}

DWORD WINAPI GetMenuCheckMarkDimensions(void)
{
    return MAKELONG(14, 14);    /* OBM_CHECK on VGA: 14x14 (display resource) */
}

HMENU WINAPI GetMenu(HWND h)
{
    PWND w = valid_wnd(h);
    return w && !(w->style & WS_CHILD) ? (HMENU)w->hMenu : 0;
}

BOOL WINAPI SetMenu(HWND h, HMENU menu)
{
    PWND w = valid_wnd(h);
    LPMENU m;
    if (!w || (w->style & WS_CHILD))
        return FALSE;
    w->hMenu = (WORD)menu;
    if ((m = valid_menu(menu)) != 0)
        m->hwnd = h;
    set_window_pos(h, 0, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER |
                   SWP_NOACTIVATE | SWP_FRAMECHANGED);
    return TRUE;
}

void WINAPI DrawMenuBar(HWND h)
{
    PWND w = valid_wnd(h);
    if (w && !(w->style & WS_CHILD) && w->hMenu && is_visible(w))
        send_message(h, WM_NCPAINT, 1, 0);
}

/* ---- loading ---- */
static const BYTE __far *load_items(HMENU h, const BYTE __far *p)
{
    for (;;) {
        UINT flags = *(const WORD __far *)p, id = 0;
        LPCSTR text;
        p += 2;
        if (!(flags & MF_POPUP)) {
            id = *(const WORD __far *)p;
            p += 2;
        }
        text = (LPCSTR)p;
        while (*p)
            p++;
        p++;
        if (flags & MF_POPUP) {
            HMENU sub = CreatePopupMenu();
            p = load_items(sub, p);
            AppendMenu(h, (flags & ~MF_END) | MF_POPUP, (UINT)sub, text);
        } else if (!id && !*text && !(flags & ~MF_END))
            AppendMenu(h, MF_SEPARATOR, 0, NULL);
        else
            AppendMenu(h, flags & ~MF_END, id, text);
        if (flags & MF_END)
            return p;
    }
}

HMENU WINAPI LoadMenuIndirect(const void __far *tmpl)
{
    const BYTE __far *p = (const BYTE __far *)tmpl;
    HMENU h = CreateMenu();
    if (!h)
        return 0;
    p += 4 + ((const WORD __far *)p)[1];        /* the header and its extra bytes */
    load_items(h, p);
    return h;
}

HMENU WINAPI LoadMenu(HINSTANCE hinst, LPCSTR name)
{
    HRSRC r = FindResource(hinst, name, RT_MENU);
    HGLOBAL g;
    HMENU h = 0;
    void __far *p;
    if (!r)
        return 0;
    g = LoadResource(hinst, r);
    p = LockResource(g);
    if (p) {
        h = LoadMenuIndirect(p);
        GlobalUnlock(g);
    }
    FreeResource(g);
    return h;
}

/* ---- the system menu (UNVERIFIED wording and order: Windows 3.1's
   own resource is to be compared on screen) ---- */
static HMENU make_sysmenu(PWND w)
{
    HMENU m = new_menu(MENU_POPUP | MENU_SYSMENU);
    BOOL child = (w->style & WS_CHILD) != 0;
    AppendMenu(m, MF_STRING, SC_RESTORE, "&Restore");
    AppendMenu(m, MF_STRING, SC_MOVE, "&Move");
    AppendMenu(m, MF_STRING, SC_SIZE, "&Size");
    AppendMenu(m, MF_STRING, SC_MINIMIZE, "Mi&nimize");
    AppendMenu(m, MF_STRING, SC_MAXIMIZE, "Ma&ximize");
    AppendMenu(m, MF_SEPARATOR, 0, NULL);
    AppendMenu(m, MF_STRING, SC_CLOSE, child ? "&Close\tCtrl+F4" : "&Close\tAlt+F4");
    if (child) {
        AppendMenu(m, MF_SEPARATOR, 0, NULL);
        AppendMenu(m, MF_STRING, SC_NEXTWINDOW, "Nex&t\tCtrl+F6");
    } else {
        AppendMenu(m, MF_SEPARATOR, 0, NULL);
        AppendMenu(m, MF_STRING, SC_TASKLIST, "S&witch To...\tCtrl+Esc");
    }
    return m;
}

HMENU WINAPI GetSystemMenu(HWND h, BOOL revert)
{
    PWND w = valid_wnd(h);
    if (!w)
        return 0;
    if (revert) {
        if (w->hSysMenu)
            DestroyMenu((HMENU)w->hSysMenu);
        w->hSysMenu = 0;
        return 0;
    }
    if (!w->hSysMenu)
        w->hSysMenu = (WORD)make_sysmenu(w);
    return (HMENU)w->hSysMenu;
}

/* the system menu's items enabled for the window's state, before
   WM_INITMENU (US4: a normal window's Restore is grayed; the others
   UNVERIFIED) */
static void sysmenu_states(PWND w, HMENU m)
{
    DWORD s = w->style;
    BOOL mm = (s & (WS_MINIMIZE | WS_MAXIMIZE)) != 0;
    EnableMenuItem(m, SC_RESTORE, mm ? MF_ENABLED : MF_GRAYED);
    EnableMenuItem(m, SC_MOVE, (s & WS_MAXIMIZE) ? MF_GRAYED : MF_ENABLED);
    EnableMenuItem(m, SC_SIZE, (!(s & WS_THICKFRAME) || mm) ? MF_GRAYED : MF_ENABLED);
    EnableMenuItem(m, SC_MINIMIZE, (!(s & WS_MINIMIZEBOX) || (s & WS_MINIMIZE)) ? MF_GRAYED : MF_ENABLED);
    EnableMenuItem(m, SC_MAXIMIZE, (!(s & WS_MAXIMIZEBOX) || (s & WS_MAXIMIZE)) ? MF_GRAYED : MF_ENABLED);
}

/* ---- the menu bar ---- */
static void text_of(LPMITEM it, char *buf, int n)
{
    if ((it->flags & (MF_BITMAP | MF_OWNERDRAW | MF_SEPARATOR)) || !it->text)
        buf[0] = 0;
    else
        lstr_cpyn(buf, STEXT(it->text), n);
}

static int item_text_width(HDC hdc, LPMITEM it)
{
    char buf[128], out[128];
    int i, k = 0;
    text_of(it, buf, sizeof buf);
    for (i = 0; buf[i] && buf[i] != '\t'; i++) {
        if (buf[i] == '&') {
            i++;
            if (!buf[i])
                break;
        }
        out[k++] = buf[i];
    }
    return k ? LOWORD(GetTextExtent(hdc, out, k)) : 0;
}

/* lay out the bar for a width; returns its height without the line under it */
/* align: the window's class has CS_BYTEALIGNCLIENT (probe US5: then the
   items start on 8-pixel boundaries, rounded to the nearest; Program
   Manager's frame class has it) */
static int layout_bar(LPMENU m, int width, BOOL align)
{
    HDC hdc = hdcScreen;
    HFONT old = SelectObject(hdc, GetStockObject(SYSTEM_FONT));
    int x = 0, y = 0, h = sysmet[SM_CYMENU];
    UINT i;
    for (i = 0; i < m->count; i++) {
        LPMITEM it = item_at(m, i);
        /* the text's width plus a system-font cell (8 pixels) on each side
           (US5: File, Options, Window, Help with texts of 24, 50, 52, 30
           pixels start 0, 40, 106, 174 pixels into the bar) */
        int cc = cxSysCharMenu();
        int w = item_text_width(hdc, it) + 2 * cc;
        if (x + w > width && x > 0) {
            /* a further row one pixel more down (3.1: File Manager made
               narrow, its bar on two rows, the second's text 19 pixels
               below the first's, the bar 37 high) */
            x = 0;
            y += h + sysmet[SM_CYBORDER];
        }
        SetRect(&it->rc, x, y, x + w, y + h);
        x += w;
        if (align) {
            /* the item reaches to where the next one starts (Program
               Manager's Help item highlighted on 3.1: 2 pixels wider than
               its text plus margins) */
            x = (x + 4) / 8 * 8;
            it->rc.right = x;
        }
    }
    SelectObject(hdc, old);
    m->cx = width;
    m->cy = y + h;
    return m->cy;
}

int menu_bar_height(PWND w, int width)
{
    LPMENU m = valid_menu((HMENU)w->hMenu);
    if (!m)
        return 0;
    return layout_bar(m, width, (((PCLS)w->cls)->style & CS_BYTEALIGNCLIENT) != 0) +
           sysmet[SM_CYBORDER];
}

/* draw a menu item's text with its mnemonic underlined, tabbed text
   right-aligned at right */
static void draw_item_text(HDC hdc, LPMITEM it, int x, int y, int right)
{
    char buf[128];
    char *tab;
    text_of(it, buf, sizeof buf);
    for (tab = buf; *tab && *tab != '\t'; tab++)
        ;
    {
        RECT r;
        SetRect(&r, x, y, 0x7FFF, y + cySysChar);
        DrawText(hdc, buf, (int)(tab - buf), &r, DT_SINGLELINE | DT_NOCLIP);
    }
    if (*tab) {
        /* the accelerator, from x 'right' (a popup's tab column) */
        RECT r;
        tab++;
        SetRect(&r, right, y, 0x7FFF, y + cySysChar);
        DrawText(hdc, tab, -1, &r, DT_SINGLELINE | DT_NOCLIP | DT_NOPREFIX);
    }
}

static void draw_bar_item(HDC hdc, LPMITEM it, int ox, int oy)
{
    BOOL hil = (it->flags & MF_HILITE) != 0;
    RECT r = it->rc;
    OffsetRect(&r, ox, oy);
    FillRect(hdc, &r, sysbrush[hil ? COLOR_HIGHLIGHT : COLOR_MENU]);
    SetBkMode(hdc, TRANSPARENT);
    SetTextColor(hdc, syscolor[(it->flags & MF_GRAYED) ? COLOR_GRAYTEXT :
                               hil ? COLOR_HIGHLIGHTTEXT : COLOR_MENUTEXT]);
    draw_item_text(hdc, it, r.left + cxSysCharMenu(), r.top, r.right);
}

void menu_draw_bar(HDC hdc, PWND w, int x, int y, int width)
{
    LPMENU m = valid_menu((HMENU)w->hMenu);
    HFONT old;
    UINT i;
    RECT r;
    if (!m)
        return;
    layout_bar(m, width, (((PCLS)w->cls)->style & CS_BYTEALIGNCLIENT) != 0);
    m->hwnd = (HWND)(WORD)w;
    old = SelectObject(hdc, GetStockObject(SYSTEM_FONT));
    SetRect(&r, x, y, x + width, y + m->cy);
    FillRect(hdc, &r, sysbrush[COLOR_MENU]);
    for (i = 0; i < m->count; i++)
        draw_bar_item(hdc, item_at(m, i), x, y);
    /* the black line under the bar */
    {
        HBRUSH ob = SelectObject(hdc, sysbrush[COLOR_WINDOWFRAME]);
        PatBlt(hdc, x, y + m->cy, width, sysmet[SM_CYBORDER], PATCOPY);
        SelectObject(hdc, ob);
    }
    SelectObject(hdc, old);
}

/* the padding of a menu-bar item: a system-font character cell (8 on
   VGA, measured), which is the dialog base unit, not tmAveCharWidth */
int cxSysCharMenu(void)
{
    return LOWORD(GetDialogBaseUnits());
}

/* ---- popup menus ----
   Measured (US4: a File menu with a check mark, a separator, a grayed
   item and an accelerator; the system menu): a black frame and a one-pixel
   shadow right and below it, starting a pixel in; items SM_CYMENU high,
   separators 7 with their line 3 down; the text 15 pixels in, the
   accelerators left-aligned 8 pixels after the widest text, 14 pixels
   from the right edge. UNVERIFIED: the margins as rules, the shadow's
   colour (light gray over white here) and the width of a menu without
   accelerators (the same 14 assumed). */
#define POP_LEFT    15
#define POP_GAP     8
#define POP_RIGHT   14
typedef struct POPUP {
    HMENU  menu;
    HWND   hwnd;                /* its window (class #32768) */
    int    sel;                 /* selected item, -1 none */
    HWND   owner;
} POPUP;

static void layout_popup(LPMENU m)
{
    HDC hdc = hdcScreen;
    HFONT old = SelectObject(hdc, GetStockObject(SYSTEM_FONT));
    int y = 0, wmax = 0, tabmax = 0;
    UINT i;
    for (i = 0; i < m->count; i++) {
        LPMITEM it = item_at(m, i);
        char buf[128], *tab;
        int h = (it->flags & MF_SEPARATOR) ? cySysChar / 2 - 1 : sysmet[SM_CYMENU];
        text_of(it, buf, sizeof buf);
        for (tab = buf; *tab && *tab != '\t'; tab++)
            ;
        {
            int w = item_text_width(hdc, it);
            if (w > wmax)
                wmax = w;
            if (*tab) {
                int tw = LOWORD(GetTextExtent(hdc, tab + 1, lstr_len(tab + 1)));
                if (tw > tabmax)
                    tabmax = tw;
            }
        }
        SetRect(&it->rc, 0, y, 0, y + h);
        y += h;
    }
    m->cx = POP_LEFT + wmax + (tabmax ? POP_GAP + tabmax : 0) + POP_RIGHT + 2;
    m->cy = y + 2;
    m->tabx = POP_LEFT + wmax + POP_GAP;
    for (i = 0; i < m->count; i++) {
        LPMITEM it = item_at(m, i);
        it->rc.left = 1;
        it->rc.right = m->cx - 1;
        OffsetRect(&it->rc, 0, 1);
    }
    SelectObject(hdc, old);
}

static HBITMAP bmCheck;

static void draw_popup_item(HDC hdc, LPMENU m, LPMITEM it, BOOL hil)
{
    RECT r = it->rc;
    BOOL grayhil = hil && (it->flags & MF_GRAYED);
    if (it->flags & MF_SEPARATOR) {
        HBRUSH ob;
        FillRect(hdc, &r, sysbrush[COLOR_MENU]);
        ob = SelectObject(hdc, sysbrush[COLOR_MENUTEXT]);
        PatBlt(hdc, r.left, (r.top + r.bottom) / 2, r.right - r.left, 1, PATCOPY);
        SelectObject(hdc, ob);
        return;
    }
    FillRect(hdc, &r, sysbrush[hil ? COLOR_HIGHLIGHT : COLOR_MENU]);
    SetBkMode(hdc, TRANSPARENT);
    SetTextColor(hdc, syscolor[(it->flags & MF_GRAYED) ? COLOR_GRAYTEXT :
                               hil ? COLOR_HIGHLIGHTTEXT : COLOR_MENUTEXT]);
    if (it->flags & MF_CHECKED) {
        if (!bmCheck)
            bmCheck = LoadBitmap(0, MAKEINTRESOURCE(OBM_CHECK));
        {
            HBITMAP old = SelectObject(hdcMem, bmCheck);
            COLORREF tc = SetTextColor(hdc, syscolor[hil ? COLOR_HIGHLIGHTTEXT : COLOR_MENUTEXT]);
            COLORREF bc = SetBkColor(hdc, syscolor[hil ? COLOR_HIGHLIGHT : COLOR_MENU]);
            BitBlt(hdc, r.left, r.top + (r.bottom - r.top - 14) / 2, 14, 14, hdcMem, 0, 0, SRCCOPY);
            SetTextColor(hdc, tc);
            SetBkColor(hdc, bc);
            SelectObject(hdcMem, old);
        }
    }
    if (grayhil) {
        /* a grayed item under the highlight: its text in the highlight
           text colour through GrayString's checkerboard (US4: the system
           menu's Restore) */
        HDC mem = CreateCompatibleDC(hdc);
        int cx = r.right - r.left, cy = r.bottom - r.top;
        HBITMAP bm = CreateBitmap(cx, cy, 1, 1, NULL), ob = SelectObject(mem, bm);
        HFONT of = SelectObject(mem, GetStockObject(SYSTEM_FONT));
        HBRUSH obr;
        DWORD org = GetDCOrg(hdc);
        PatBlt(mem, 0, 0, cx, cy, WHITENESS);
        SetBkMode(mem, TRANSPARENT);
        SetTextColor(mem, RGB(0, 0, 0));
        draw_item_text(mem, it, POP_LEFT, 0, m->tabx);
        UnrealizeObject(hbrGray);
        /* the checkerboard starts at the item's corner, not the screen's:
           Windows Write's grayed Undo under the highlight, its item at
           49,42 on screen, had the other phase than w16 gave it from the
           screen; the system menu's Restore (US4) sat at an even place.
           UNVERIFIED: its shortcut (Ctrl+Z) was one pixel further right
           on 3.1 than in the item's other states */
        (void)org;
        SetBrushOrg(mem, 0, 0);
        obr = SelectObject(mem, hbrGray);
        PatBlt(mem, 0, 0, cx, cy, 0x00FA0089L);     /* DPo */
        SelectObject(mem, obr);
        {
            COLORREF tc = SetTextColor(hdc, RGB(0, 0, 0)), bc = SetBkColor(hdc, RGB(255, 255, 255));
            obr = SelectObject(hdc, sysbrush[COLOR_HIGHLIGHTTEXT]);
            BitBlt(hdc, r.left, r.top, cx, cy, mem, 0, 0, 0x00B8074AL);   /* PSDPxax */
            SelectObject(hdc, obr);
            SetTextColor(hdc, tc);
            SetBkColor(hdc, bc);
        }
        SelectObject(mem, of);
        SelectObject(mem, ob);
        DeleteObject(bm);
        DeleteDC(mem);
        return;
    }
    draw_item_text(hdc, it, r.left + POP_LEFT, r.top, r.left + m->tabx);
}

LRESULT WINAPI __loadds MenuWndProc(HWND h, UINT msg, WPARAM wp, LPARAM lp)
{
    switch (msg) {
    case WM_PAINT: {
        PAINTSTRUCT ps;
        HMENU hm = (HMENU)GetWindowWord(h, 0);
        LPMENU m = valid_menu(hm);
        HDC hdc = BeginPaint(h, &ps);
        if (m) {
            HFONT old = SelectObject(hdc, GetStockObject(SYSTEM_FONT));
            UINT i;
            RECT r;
            GetClientRect(h, &r);
            /* the shadow, its corner pixels left as they were */
            r.right--;
            r.bottom--;
            {
                HBRUSH ob = SelectObject(hdc, sysbrush[COLOR_BTNFACE]);
                PatBlt(hdc, r.right, 1, 1, r.bottom, PATCOPY);
                PatBlt(hdc, 1, r.bottom, r.right, 1, PATCOPY);
                SelectObject(hdc, ob);
            }
            FrameRect(hdc, &r, sysbrush[COLOR_WINDOWFRAME]);
            for (i = 0; i < m->count; i++) {
                LPMITEM it = item_at(m, i);
                draw_popup_item(hdc, m, it, (it->flags & MF_HILITE) != 0);
            }
            SelectObject(hdc, old);
        }
        EndPaint(h, &ps);
        return 0;
    }
    case WM_ERASEBKGND: {
        RECT r;
        GetClientRect(h, &r);
        r.right--;
        r.bottom--;
        FillRect((HDC)wp, &r, sysbrush[COLOR_MENU]);
        return 1;
    }
    case WM_MOUSEACTIVATE:
        return MA_NOACTIVATE;
    }
    return DefWindowProc(h, msg, wp, lp);
}

/* ---- the menu loop ---- */
typedef struct MLOOP {
    HWND   owner;               /* the window whose menu it is */
    HMENU  bar;                 /* its menu bar (or 0 for TrackPopupMenu) */
    BOOL   sysmenu;             /* the bar is the system menu */
    int    barsel;              /* selected bar item, -1 none */
    HMENU  popup;               /* open popup */
    HWND   hwndPopup;
    int    sel;
    UINT   result;              /* command chosen, 0 none */
    BOOL   resultsys;
    BOOL   done;
} MLOOP;

void sysbox_rect(PWND w, RECT *r);
extern HWND hwndSysMenuOpen;

static void invert_sysbox(MLOOP *l)
{
    PWND w = valid_wnd(l->owner);
    RECT r;
    HDC hdc;
    if (!w || !(w->style & WS_SYSMENU) || (w->style & WS_MINIMIZE))
        return;
    sysbox_rect(w, &r);
    hwndSysMenuOpen = hwndSysMenuOpen ? 0 : l->owner;   /* for repaints */
    hdc = get_dc(l->owner, TRUE, 0, 0);
    InvertRect(hdc, &r);
    release_dc(l->owner, hdc);
}

static void close_popup(MLOOP *l)
{
    if (l->sysmenu && l->hwndPopup)
        invert_sysbox(l);
    if (l->hwndPopup) {
        DestroyWindow(l->hwndPopup);
        l->hwndPopup = 0;
    }
    if (l->popup) {
        LPMENU m = valid_menu(l->popup);
        UINT i;
        if (m)
            for (i = 0; i < m->count; i++)
                item_at(m, i)->flags &= ~MF_HILITE;
    }
    l->popup = 0;
    l->sel = -1;
}

static void select_popup_item(MLOOP *l, int sel)
{
    LPMENU m = valid_menu(l->popup);
    HDC hdc;
    HFONT old;
    if (!m || sel == l->sel)
        return;
    hdc = GetDC(l->hwndPopup);
    old = SelectObject(hdc, GetStockObject(SYSTEM_FONT));
    if (l->sel >= 0 && (UINT)l->sel < m->count) {
        item_at(m, l->sel)->flags &= ~MF_HILITE;
        draw_popup_item(hdc, m, item_at(m, l->sel), FALSE);
    }
    l->sel = sel;
    if (sel >= 0 && (UINT)sel < m->count) {
        LPMITEM it = item_at(m, sel);
        it->flags |= MF_HILITE;
        draw_popup_item(hdc, m, it, TRUE);
        send_message(l->owner, WM_MENUSELECT, (it->flags & MF_POPUP) ? (UINT)it->id : it->id,
                     MAKELONG(it->flags, l->popup));
    }
    SelectObject(hdc, old);
    ReleaseDC(l->hwndPopup, hdc);
}

static void open_popup(MLOOP *l, HMENU popup, int x, int y)
{
    LPMENU m;
    if (!valid_menu(popup))
        return;
    send_message(l->owner, WM_INITMENUPOPUP, (WPARAM)popup, MAKELONG(l->barsel, l->sysmenu));
    if ((m = valid_menu(popup)) == 0)  /* (again: the owner may have changed it) */
        return;
    layout_popup(m);
    if (x + m->cx > sysmet[SM_CXSCREEN])
        x = sysmet[SM_CXSCREEN] - m->cx;
    if (y + m->cy > sysmet[SM_CYSCREEN])
        y = sysmet[SM_CYSCREEN] - m->cy;
    l->popup = popup;
    l->sel = -1;
    l->hwndPopup = create_window(0, MAKEINTRESOURCE(32768), NULL, WS_POPUP, x, y, m->cx + 1, m->cy + 1,
                                 l->owner, 0, hInstUser, NULL);
    if (l->hwndPopup) {
        SetWindowWord(l->hwndPopup, 0, (WORD)popup);
        set_window_pos(l->hwndPopup, HWND_TOP, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE |
                       SWP_SHOWWINDOW | SWP_NOACTIVATE);
        UpdateWindow(l->hwndPopup);
    }
}

static void bar_rect(MLOOP *l, int i, RECT *r)
{
    PWND w = PW(l->owner);
    LPMENU m = valid_menu(l->bar);
    *r = item_at(m, i)->rc;
    OffsetRect(r, w->rcClient.left, w->rcClient.top - m->cy - sysmet[SM_CYBORDER]);
}

static void select_bar(MLOOP *l, int i, BOOL open)
{
    LPMENU m = valid_menu(l->bar);
    HDC hdc;
    PWND w = PW(l->owner);
    close_popup(l);
    if (l->sysmenu) {
        if (open) {
            RECT r;
            GetWindowRect(l->owner, &r);
            /* on the caption's lower line (US4) */
            open_popup(l, l->bar, r.left + sysmet[SM_CXFRAME],
                       r.top + sysmet[SM_CYFRAME] - 2 + sysmet[SM_CYCAPTION]);
            if (l->hwndPopup)
                invert_sysbox(l);
        }
        return;
    }
    if (!m)
        return;
    hdc = get_dc(l->owner, TRUE, 0, 0);
    {
        HFONT old = SelectObject(hdc, GetStockObject(SYSTEM_FONT));
        int ox = w->rcClient.left - w->rcWindow.left;
        int oy = w->rcClient.top - w->rcWindow.top - m->cy - sysmet[SM_CYBORDER];
        if (l->barsel >= 0 && (UINT)l->barsel < m->count) {
            item_at(m, l->barsel)->flags &= ~MF_HILITE;
            draw_bar_item(hdc, item_at(m, l->barsel), ox, oy);
        }
        l->barsel = i;
        if (i >= 0) {
            LPMITEM it = item_at(m, i);
            it->flags |= MF_HILITE;
            draw_bar_item(hdc, it, ox, oy);
            send_message(l->owner, WM_MENUSELECT, (it->flags & MF_POPUP) ? (UINT)it->id : it->id,
                         MAKELONG(it->flags, l->bar));
        }
        SelectObject(hdc, old);
    }
    release_dc(l->owner, hdc);
    m = valid_menu(l->bar);             /* after WM_MENUSELECT */
    if (open && m && i >= 0 && (UINT)i < m->count && (item_at(m, i)->flags & MF_POPUP)) {
        RECT r;
        bar_rect(l, i, &r);
        open_popup(l, (HMENU)item_at(m, i)->id, r.left, r.bottom);   /* on the bar's line */
    }
}

static int mnemonic(LPMENU m, char c)
{
    UINT i;
    c = (char)(DWORD)AnsiUpper((LPSTR)MAKELP(0, (BYTE)c));
    for (i = 0; i < m->count; i++) {
        LPMITEM it = item_at(m, i);
        char buf[128], *p;
        text_of(it, buf, sizeof buf);
        for (p = buf; *p; p++)
            if (*p == '&' && p[1] && p[1] != '&') {
                if ((char)(DWORD)AnsiUpper((LPSTR)MAKELP(0, (BYTE)p[1])) == c)
                    return i;
                break;
            }
    }
    return -1;
}

static void choose(MLOOP *l, HMENU h, int i)
{
    LPMENU m = valid_menu(h);
    LPMITEM it;
    if (!m || i < 0 || (UINT)i >= m->count)
        return;
    it = item_at(m, i);
    if (it->flags & (MF_GRAYED | MF_DISABLED | MF_SEPARATOR))
        return;
    if (it->flags & MF_POPUP)
        return;
    l->result = it->id;
    l->resultsys = l->sysmenu;
    l->done = TRUE;
}

static int next_item(LPMENU m, int i, int dir)
{
    int n = m->count, k;
    for (k = 0; k < n; k++) {
        i = (i + dir + n) % n;
        if (!(item_at(m, i)->flags & MF_SEPARATOR))
            return i;
    }
    return -1;
}

static void menu_key(MLOOP *l, UINT vk)
{
    LPMENU bar = valid_menu(l->bar), pop = valid_menu(l->popup);
    switch (vk) {
    case VK_ESCAPE:
        if (l->popup && !l->sysmenu && bar) {
            close_popup(l);
        } else
            l->done = TRUE;
        break;
    case VK_LEFT:
    case VK_RIGHT:
        if (bar && !l->sysmenu)
            select_bar(l, next_item(bar, l->barsel < 0 ? 0 : l->barsel, vk == VK_LEFT ? -1 : 1),
                       l->popup != 0);
        break;
    case VK_DOWN:
    case VK_UP:
        if (!pop && bar && l->barsel >= 0 && !l->sysmenu)
            select_bar(l, l->barsel, TRUE);
        else if (pop)
            select_popup_item(l, next_item(pop, l->sel < 0 ? (vk == VK_DOWN ? -1 : 0) : l->sel,
                                           vk == VK_UP ? -1 : 1));
        break;
    case VK_RETURN:
        if (pop && l->sel >= 0)
            choose(l, l->popup, l->sel);
        else if (bar && l->barsel >= 0 && !l->sysmenu)
            select_bar(l, l->barsel, TRUE);
        break;
    }
}

static void menu_char(MLOOP *l, char c)
{
    LPMENU m = valid_menu(l->popup ? l->popup : l->bar);
    int i;
    if (!m)
        return;
    i = mnemonic(m, c);
    if (i < 0) {
        l->done = !l->popup;
        return;
    }
    if (l->popup) {
        select_popup_item(l, i);
        choose(l, l->popup, i);
    } else
        select_bar(l, i, TRUE);
}

/* the loop: messages for the menu are read here; others are passed on */
extern int menu_mode;

static void run_menu(MLOOP *l)
{
    MSG msg;
    HWND oldcap = GetCapture();
    /* the arrow while a menu is up, whatever the window under it chose
       (3.1: the arrow over Notepad's Help menu, opened from the keyboard
       with the pointer over the edit control; the menu class has no
       cursor, US2) */
    menu_mode++;
    SetCursor(load_icon_cursor(0, IDC_ARROW, TRUE));
    /* the caret goes on blinking (3.1: Notepad's caret seen both on and
       off while its Help menu is up) */
    while (!l->done) {
        if (!GetMessage(&msg, 0, 0, 0)) {
            PostQuitMessage(msg.wParam);
            break;
        }
        switch (msg.message) {
        case WM_KEYDOWN:
        case WM_SYSKEYDOWN:
            /* the characters for mnemonics come from here: without this a
               menu opened from the keyboard ignored its item letters */
            TranslateMessage(&msg);
            menu_key(l, msg.wParam);
            continue;
        case WM_CHAR:
        case WM_SYSCHAR:
            menu_char(l, (char)msg.wParam);
            continue;
        case WM_KEYUP:
        case WM_SYSKEYUP:
            continue;
        case WM_LBUTTONDBLCLK:
        case WM_NCLBUTTONDBLCLK:
            /* a double click on the system box while its menu is up: the
               menu ends, then SC_CLOSE (3.1: nothing of the menu stays).
               It went to DefWindowProc inside the loop, so the window
               closed - Program Manager's Exit dialog - under a menu that
               stayed up and live */
            if (l->sysmenu && valid_wnd(l->owner) &&
                (int)send_message(l->owner, WM_NCHITTEST, 0, MAKELONG(msg.pt.x, msg.pt.y)) == HTSYSMENU) {
                l->result = SC_CLOSE;
                l->resultsys = TRUE;
                l->done = TRUE;
                continue;
            }
            /* elsewhere: a press like any other */
            msg.message = msg.message == WM_LBUTTONDBLCLK ? WM_LBUTTONDOWN : WM_NCLBUTTONDOWN;
            /* fall through */
        case WM_LBUTTONDOWN:
        case WM_NCLBUTTONDOWN:
        case WM_LBUTTONUP:
        case WM_NCLBUTTONUP: {
            POINT pt = msg.pt;
            LPMENU pop = valid_menu(l->popup), bar = valid_menu(l->bar);
            if (pop && l->hwndPopup && PtInRect(&PW(l->hwndPopup)->rcWindow, pt)) {
                UINT i;
                pt.x -= PW(l->hwndPopup)->rcWindow.left;
                pt.y -= PW(l->hwndPopup)->rcWindow.top;
                for (i = 0; i < pop->count; i++)
                    if (PtInRect(&item_at(pop, i)->rc, pt)) {
                        select_popup_item(l, i);
                        if (msg.message == WM_LBUTTONUP || msg.message == WM_NCLBUTTONUP)
                            choose(l, l->popup, i);
                        break;
                    }
                continue;
            }
            if (bar && !l->sysmenu) {
                UINT i, n = bar->count;
                for (i = 0; i < n; i++) {
                    RECT r;
                    bar_rect(l, i, &r);
                    if (PtInRect(&r, pt)) {
                        if (msg.message == WM_LBUTTONDOWN || msg.message == WM_NCLBUTTONDOWN)
                            select_bar(l, i, TRUE);
                        else if (l->popup && l->sel < 0)
                            /* released on the bar item: the popup's first
                               item selected, as from the keyboard (3.1,
                               Program Manager's Help clicked) */
                            select_popup_item(l, 0);
                        break;
                    }
                }
                if (i < n)
                    continue;
            }
            if (msg.message == WM_LBUTTONDOWN || msg.message == WM_NCLBUTTONDOWN)
                l->done = TRUE;
            continue;
        }
        case WM_MOUSEMOVE:
        case WM_NCMOUSEMOVE:
            continue;
        }
        TranslateMessage(&msg);
        DispatchMessage(&msg);
        if (!valid_wnd(l->owner))
            break;
    }
    close_popup(l);
    if (!l->sysmenu && valid_menu(l->bar) && valid_wnd(l->owner))
        select_bar(l, -1, FALSE);
    if (valid_wnd(l->owner))
        send_message(l->owner, WM_MENUSELECT, 0, MAKELONG(0xFFFF, 0));
    if (oldcap)
        SetCapture(oldcap);
    menu_mode--;
    if (l->result && valid_wnd(l->owner))
        PostMessage(l->owner, l->resultsys ? WM_SYSCOMMAND : WM_COMMAND, l->result, 0);
}

/* DefWindowProc's SC_KEYMENU / SC_MOUSEMENU: lp is the key (0: Alt or F10
   alone; ' ': the system menu) */
void menu_track_bar(HWND h, char key)
{
    MLOOP l;
    PWND w = valid_wnd(h);
    LPMENU bar;
    if (!w)
        return;
    fmemset(&l, 0, sizeof l);
    l.owner = h;
    l.barsel = -1;
    l.sel = -1;
    l.bar = (HMENU)w->hMenu;
    bar = valid_menu(l.bar);
    /* Alt+Space, or Alt (F10) alone in a window without a menu bar: the
       system menu. Alt+letter without a menu bar used to open it as well,
       so Alt+H in a dialog or in Clock put up the system menu */
    if (key == ' ' || (!bar && !key && (w->style & WS_SYSMENU))) {
        l.bar = GetSystemMenu(h, FALSE);
        l.sysmenu = TRUE;
        sysmenu_states(w, l.bar);
        send_message(h, WM_INITMENU, (WPARAM)l.bar, 0);
        select_bar(&l, 0, TRUE);
        select_popup_item(&l, 0);       /* from the keyboard: the first item (US4) */
    } else if (bar) {
        send_message(h, WM_INITMENU, (WPARAM)l.bar, 0);
        if (key) {
            int i = (bar = valid_menu(l.bar)) != 0 ? mnemonic(bar, key) : -1;
            if (i < 0) {
                MessageBeep(0);
                return;
            }
            select_bar(&l, i, TRUE);
            if (l.popup)
                select_popup_item(&l, 0);
        } else
            select_bar(&l, 0, FALSE);
    } else {
        if (key)
            MessageBeep(0);
        return;
    }
    run_menu(&l);
}

BOOL WINAPI TrackPopupMenu(HMENU hm, UINT flags, int x, int y, int r, HWND h, const RECT __far *rc)
{
    MLOOP l;
    LPMENU m = valid_menu(hm);
    (void)r, (void)rc;
    if (!m || !valid_wnd(h))
        return FALSE;
    fmemset(&l, 0, sizeof l);
    l.owner = h;
    l.barsel = -1;
    layout_popup(m);
    if (flags & TPM_CENTERALIGN)
        x -= m->cx / 2;
    else if (flags & TPM_RIGHTALIGN)
        x -= m->cx;
    open_popup(&l, hm, x, y);
    run_menu(&l);
    return TRUE;
}

/* a click on the menu bar at pt (screen) */
void menu_track_point(HWND h, POINT pt)
{
    MLOOP l;
    PWND w = valid_wnd(h);
    LPMENU bar;
    UINT i;
    if (!w || !(bar = valid_menu((HMENU)w->hMenu)))
        return;
    fmemset(&l, 0, sizeof l);
    l.owner = h;
    l.barsel = -1;
    l.sel = -1;
    l.bar = (HMENU)w->hMenu;
    send_message(h, WM_INITMENU, (WPARAM)l.bar, 0);
    for (i = 0; (bar = valid_menu(l.bar)) != 0 && i < bar->count; i++) {
        RECT r;
        bar_rect(&l, i, &r);
        if (PtInRect(&r, pt)) {
            select_bar(&l, i, TRUE);
            break;
        }
    }
    if (l.barsel < 0)
        return;
    run_menu(&l);
}

void menu_track_sys(HWND h)
{
    MLOOP l;
    if (!valid_wnd(h))
        return;
    fmemset(&l, 0, sizeof l);
    l.owner = h;
    l.barsel = -1;
    l.sel = -1;
    l.bar = GetSystemMenu(h, FALSE);
    l.sysmenu = TRUE;
    sysmenu_states(PW(h), l.bar);
    send_message(h, WM_INITMENU, (WPARAM)l.bar, 0);
    select_bar(&l, 0, TRUE);
    run_menu(&l);
}
