/*
 * win.c - windows: creation, destruction, the window tree, window data.
 *
 * Each window is a WND in USER's local heap followed by its cbWndExtra
 * bytes; the HWND is its near offset (Pietrek, Windows Internals, ch. 4).
 * Siblings are linked through hwndNext in Z order, topmost first; the
 * desktop is the root of the top-level windows.
 */
#include "user.h"

HWND hwndDesktop;

/* CS_BYTEALIGNCLIENT: the client area's left edge goes to the nearest
   multiple of 8 on screen (probe US5: asked for x 10 and 333, Windows 3.1
   put such windows at 12 and 332, their client areas at 16 and 336) */
LRESULT WINAPI ListBoxWndProc(HWND h, UINT msg, WPARAM wp, LPARAM lp);
LRESULT WINAPI ComboLBoxWndProc(HWND h, UINT msg, WPARAM wp, LPARAM lp);
BOOL no_byte_align;             /* set around Tile/Cascade's moves (mdi.c) */
void byte_align(PWND w, BOOL made)
{
    int dx;
    WORD cs = ((PCLS)w->cls)->style;
    if (no_byte_align)
        return;
    /* CS_BYTEALIGNWINDOW only when the window is made, not when it is
       moved (US18 on 3.1: a dialog-class window made at x 221 went to 224,
       moved to 213 and to 230 it stayed there; Terminal centres its
       dialogs at 220) */
    if (!made)
        cs &= ~CS_BYTEALIGNWINDOW;
    /* not an icon (3.1: Notepad's, CS_BYTEALIGNCLIENT, minimised at 21
       like the rest; w16 had it at 24) */
    /* a child only by CS_BYTEALIGNWINDOW, as it is made (US33 dlgf: child
       dialogs, their left edges on screen at multiples of 8: 11 and 12
       pixels into a client area at x 304 made 8 and 16) */
    if (w->style & WS_CHILD)
        cs &= CS_BYTEALIGNWINDOW;
    if (!(cs & (CS_BYTEALIGNCLIENT | CS_BYTEALIGNWINDOW)) || (w->style & WS_MINIMIZE))
        return;
    /* CS_BYTEALIGNWINDOW: the window's left edge (dialogs, #32770: US4's
       message boxes are at 136, 168, 120 where centring gives 137, 169,
       122) */
    /* nearest multiple of 8 also left of the screen: (x + 4) & ~7 (C's
       division rounds towards zero, which put a window dragged partly off
       the left edge 8 pixels right of where Windows 3.1 puts it) */
    /* a CS_BYTEALIGNCLIENT window being made: up to the next multiple
       (3.1: Notepad made at 22,22 by CW_USEDEFAULT, its client at 26, came
       at 28, client 32: boot-test case taskman; at 0 it comes at 4) */
    if ((cs & CS_BYTEALIGNCLIENT) && made)
        dx = ((w->rcClient.left + 7) & ~7) - w->rcClient.left;
    else if (cs & CS_BYTEALIGNCLIENT)
        dx = ((w->rcClient.left + 4) & ~7) - w->rcClient.left;
    else
        dx = ((w->rcWindow.left + 4) & ~7) - w->rcWindow.left;
    if (dx) {
        OffsetRect(&w->rcWindow, dx, 0);
        OffsetRect(&w->rcClient, dx, 0);
    }
}
void free_place(HWND h);
void minimize_new(HWND h);
static void free_window(PWND w);
void free_props(PWND w);
void free_scroll(PWND w);
void purge_window_msgs(HQUEUE hq, HWND h);
void free_window_dcs(HWND h);
extern LRESULT WINAPI __loadds ComboBoxWndProc(HWND, UINT, WPARAM, LPARAM);

/* ---- where a window's WND is ----
   Normally (the layout 3.1's tools know, Schulman ch. 6) the WND and its
   extra bytes are one fixed block of USER's DGROUP and the HWND is its
   offset: PW(h) is DGROUP:h.
   [w16] ExtendedUserHeap=1 (SYSTEM.INI, read at start-up; off by default)
   moves them out: the HWND is still the offset of a fixed DGROUP block,
   but a 4-byte stub {heap number with STUB_SIG, offset}, and the WND
   follows a word holding the HWND in a block of one of up to NWNDHEAPS
   window heaps (local heaps in 64K global blocks of their own, made whole
   so their selectors never change). A window then costs DGROUP the stub
   alone, as Windows 95 did, and thousands fit; but a program that reads
   USER's data directly (Spy-type tools, desktop replacements, debuggers,
   some hook DLLs; TOOLHELP's heap walk shows stubs where WNDs were) does
   not find the WND at DGROUP:HWND. Everything else reaches windows through
   PW/HW/valid_wnd, the one code path both layouts share. */
#define NWNDHEAPS 16
#define STUB_SIG  0x5700        /* 'W' in the high byte, the heap's number low */
typedef struct STUB {
    WORD sig;
    WORD off;                   /* the window heap's block: the HWND, then the WND */
} STUB;
static UHEAP uhWnd[NWNDHEAPS];
static int nwndheaps;
static WND dummy_wnd;           /* PW of a handle that is no window (ext mode) */

static BOOL add_wnd_heap(void)
{
    HGLOBAL g;
    WORD sel;
    if (nwndheaps >= NWNDHEAPS)
        return FALSE;
    g = GlobalAlloc(GMEM_MOVEABLE | GMEM_ZEROINIT, 0xFFF0);
    if (!g)
        return FALSE;
    sel = HIWORD(GlobalHandle((UINT)g));
    if (!LocalInit(sel, 0x12, 0xFFEF)) {
        GlobalFree(g);
        return FALSE;
    }
    uhWnd[nwndheaps].h = g;
    uhWnd[nwndheaps].sel = sel;
    nwndheaps++;
    return TRUE;
}

PWND wnd_of(WORD h)
{
    STUB __near *st;
    if (!h)
        return 0;
    if (!ext_user_heap)
        return (PWND)MK_FP(get_ds(), h);
    st = (STUB __near *)h;
    if ((st->sig & 0xFF00) != STUB_SIG || (st->sig & 0xFF) >= nwndheaps)
        return (PWND)(WND __near *)&dummy_wnd;
    return (PWND)MK_FP(uhWnd[st->sig & 0xFF].sel, st->off + 2);
}

HWND hwnd_of(PWND w)
{
    if (!w)
        return 0;
    return ext_user_heap ? (HWND)((WORD __far *)w)[-1] : (HWND)FP_OFF(w);
}

/* a new window's block of size bytes, zeroed: its handle, 0 if no room */
static HWND alloc_wnd(WORD size)
{
    STUB __near *st;
    WORD i, off = 0;
    if (!ext_user_heap)
        return (HWND)LocalAlloc(LPTR, size);
    st = (STUB __near *)LocalAlloc(LPTR, sizeof(STUB));
    if (!st)
        return 0;
    for (i = 0; !off; i++) {
        if (i == nwndheaps && !add_wnd_heap())
            break;
        off = uh_alloc(&uhWnd[i], LMEM_FIXED | LMEM_ZEROINIT, size + 2);
    }
    if (!off) {
        LocalFree((HLOCAL)st);
        return 0;
    }
    i--;
    *(WORD __far *)MK_FP(uhWnd[i].sel, off) = (WORD)st;
    st->sig = STUB_SIG | i;
    st->off = off;
    return (HWND)(WORD)st;
}

static void free_wnd(HWND h)
{
    STUB __near *st = (STUB __near *)(WORD)h;
    if (ext_user_heap)
        uh_free(&uhWnd[st->sig & 0xFF], st->off);
    LocalFree((HLOCAL)h);
}

/* Fast near validation of a fixed block in USER's DGROUP local heap.
   Returns its usable size (> 0), or 0 if not a valid busy fixed block.
   Matches KERNEL's local.c (both KRNL286 and KRNL386 heap info layouts)
   without cross-module calls into KERNEL. */
static WORD local_fixed_size(WORD p)
{
    static BYTE last_off;       /* 0: detect, 8: KRNL286, 10: KRNL386, 0xFF: fallback */
    WORD info, first, last, a, pv, nx;
    if (!p || (p & 3))
        return 0;
    info = *(WORD __near *)6;
    if (!last_off) {
        if (info && !(info & 3) && info >= 0x10) {
            WORD ia = info - 4;
            WORD isz = *(WORD __near *)(ia + 2) - ia;
            if (isz >= 0x2E && *(WORD __near *)(info + 0x28) == 0x484C)
                last_off = 0x0A;        /* KRNL386: LI_LAST = 0x0A, LI_MAGIC = 0x28 */
            else if (isz >= 0x28 && *(WORD __near *)(info + 0x22) == 0x484C)
                last_off = 0x08;        /* KRNL286: LI_LAST = 0x08, LI_MAGIC = 0x22 */
            else
                last_off = 0xFF;
        } else {
            last_off = 0xFF;
        }
    }
    if (last_off == 0xFF || !info)
        return LocalSize((HLOCAL)p);
    first = *(WORD __near *)(info + 0x06);
    last = *(WORD __near *)(info + last_off);
    a = p - 4;
    if (a <= first || a >= last)
        return 0;
    pv = *(WORD __near *)a;
    if ((pv & 3) != 1)                  /* LA_BUSY | !LA_MOVEABLE */
        return 0;
    pv &= ~3;
    nx = *(WORD __near *)(a + 2);
    if (nx <= a || nx > last || pv >= a || pv < first)
        return 0;
    if (*(WORD __near *)(pv + 2) != a || (*(WORD __near *)nx & ~3) != a)
        return 0;
    return nx - a - 4;
}

PWND valid_wnd(HWND h)
{
    PWND w;
    WORD sz;
    if (!h || ((WORD)h & 3))
        return 0;
    /* a live window is a fixed local block of ours that points to a class */
    sz = local_fixed_size((WORD)h);
    if (sz < (ext_user_heap ? sizeof(STUB) : sizeof(WND)))
        return 0;
    if (ext_user_heap) {
        STUB __near *st = (STUB __near *)(WORD)h;
        UHEAP *hp;
        if ((st->sig & 0xFF00) != STUB_SIG || (st->sig & 0xFF) >= nwndheaps || (st->off & 1))
            return 0;
        hp = &uhWnd[st->sig & 0xFF];
        if (uh_size(hp, st->off) < sizeof(WND) + 2 || *(WORD __far *)MK_FP(hp->sel, st->off) != (WORD)h)
            return 0;
    }
    w = PW(h);
    if (!w->cls || (w->flags & 0x8000))
        return 0;
    /* ... a class: a fixed block of DGROUP's heap with the class signature
       (any block of WND's size with a word at 1Ch passed before: USER audit,
       item 9). A class with windows is never freed (UnregisterClass and
       the module's end leave it), so a window's class is a live one. */
    if (local_fixed_size(w->cls) < sizeof(CLS) || ((PCLS)w->cls)->sig != CLS_SIG)
        return 0;
    return w;
}

HWND top_level(HWND h)
{
    PWND w = valid_wnd(h);
    while (w && (w->style & WS_CHILD) && w->hwndParent)
        w = PW(w->hwndParent);
    return HW(w);
}

void unlink_window(PWND w)
{
    PWND p = PW(w->hwndParent);
    HWND __far *link;
    if (!p)
        return;
    for (link = &p->hwndChild; *link; link = &PW(*link)->hwndNext)
        if (*link == HW(w)) {
            *link = w->hwndNext;
            break;
        }
    w->hwndNext = 0;
}

/* after: HWND_TOP (0), HWND_BOTTOM (1) or a sibling */
void link_window(PWND w, HWND after)
{
    PWND p = PW(w->hwndParent);
    HWND __far *link;
    if (!p)
        return;
    if (after == HWND_TOP) {
        /* a top-level window that is not topmost goes under the topmost
           ones (WS_EX_TOPMOST: SetWindowPos HWND_TOPMOST, as Windows
           3.1's Task List makes itself) */
        link = &p->hwndChild;
        if (HW(p) == hwndDesktop && !(w->exstyle & WS_EX_TOPMOST))
            while (*link && *link != HW(w) && (PW(*link)->exstyle & WS_EX_TOPMOST))
                link = &PW(*link)->hwndNext;
        w->hwndNext = *link;
        *link = HW(w);
        return;
    }
    for (link = &p->hwndChild; *link; link = &PW(*link)->hwndNext)
        if (after != HWND_BOTTOM && *link == after) {
            link = &PW(*link)->hwndNext;
            break;
        }
    w->hwndNext = *link;
    *link = HW(w);
}

BOOL is_visible(PWND w)
{
    for (; w; w = PW(w->hwndParent))
        if (!(w->style & WS_VISIBLE))
            return FALSE;
    return TRUE;
}

/* window text: a block of a string heap (uheap.c; 3.1 keeps it out of
   DGROUP too: US15), WND+32 its handle, 0 for none (an empty text frees
   it, as 3.1's does), the heap's number in the flags. FALSE: no room for
   it (3.1's CreateWindow fails then: US15) */
#define TEXTPTR(w)  ((LPCSTR)UH_PTR(&uhStrs[TEXTHEAP(w)], (w)->text))
static BOOL set_text(PWND w, LPCSTR s)
{
    WORD heap, t = 0;
    if (s && *s && !(t = uh_text(s, &heap)))
        return FALSE;
    if (w->text)
        uh_free(&uhStrs[TEXTHEAP(w)], w->text);
    w->text = t;
    w->flags = (w->flags & ~WF_TEXTHEAP) | (t ? (DWORD)heap << 16 : 0);
    return TRUE;
}

/* WM_GETMINMAXINFO's defaults (3.1 SDK; the maximised size covers the
   frame off screen) */
void minmax(PWND w, MINMAXINFO __far *mm)
{
    int cxf = sysmet[SM_CXFRAME], cyf = sysmet[SM_CYFRAME];
    if (!(w->style & WS_THICKFRAME)) {
        cxf = (w->style & WS_DLGFRAME) || (w->exstyle & WS_EX_DLGMODALFRAME) ?
              sysmet[SM_CXDLGFRAME] : sysmet[SM_CXBORDER];
        cyf = (w->style & WS_DLGFRAME) || (w->exstyle & WS_EX_DLGMODALFRAME) ?
              sysmet[SM_CYDLGFRAME] : sysmet[SM_CYBORDER];
    }
    mm->ptMaxSize.x = sysmet[SM_CXSCREEN] + 2 * cxf;
    mm->ptMaxSize.y = sysmet[SM_CYSCREEN] + 2 * cyf;
    mm->ptMaxPosition.x = -cxf;
    mm->ptMaxPosition.y = -cyf;
    mm->ptMinTrackSize.x = sysmet[SM_CXMINTRACK];
    mm->ptMinTrackSize.y = sysmet[SM_CYMINTRACK];
    /* a child or popup without a caption: its frame (TK75 on 3.1: a
       popup and a child with a thick frame alone, made 30 x 10, stayed
       so; sized to 20 x 5 they came out 20 x 8. UNVERIFIED: the width,
       here the frame's as the height) */
    if ((w->style & (WS_CHILD | WS_POPUP)) && (w->style & WS_CAPTION) != WS_CAPTION) {
        mm->ptMinTrackSize.x = 2 * cxf;
        mm->ptMinTrackSize.y = 2 * cyf;
    }
    mm->ptMaxTrackSize.x = mm->ptMaxSize.x;
    mm->ptMaxTrackSize.y = mm->ptMaxSize.y;
}

/* CW_USEDEFAULT positions: UNVERIFIED cascade (Windows 3.1 staggers new
   overlapped windows by the caption height; the exact sequence is to be
   observed) */
static int cascade;

HWND create_window(DWORD exstyle, LPCSTR clsname, LPCSTR title, DWORD style,
                   int x, int y, int cx, int cy, HWND parent, HMENU menu,
                   HINSTANCE hinst, void __far *param)
{
    PCLS c = find_class(clsname, hinst);
    PWND w, pw;
    HWND h;
    CREATESTRUCT cs;
    MINMAXINFO mm;
    LONG r;
    BOOL show = FALSE;
    int sw = SW_SHOW;

    if (!c) {
        ulog("CreateWindow: no class ");
        if (!ISATOM(clsname))
            ulog(clsname);
        ulogx("", LOWORD((DWORD)clsname));
        return 0;
    }
    pw = parent ? valid_wnd(parent) : (PWND)0;
    if ((style & WS_CHILD) && !pw)
        return 0;
    h = alloc_wnd(sizeof(WND) + c->cbWndExtra);
    if (!h)
        return 0;
    w = PW(h);
    w->cls = (WORD)c;
    c->cwnd++;
    w->hInstance = hinst;
    w->lpfnWndProc = c->lpfnWndProc;
    w->exstyle = exstyle;
    w->hq = hwndDesktop ? cur_hq() : 0;
    if (!(style & WS_CHILD) && !(style & WS_POPUP))
        style |= WS_CAPTION | WS_CLIPSIBLINGS;  /* overlapped (3.1 SDK) */
    if (style & WS_CHILD) {
        /* a CS_PARENTDC class's child (the system controls, or a class of
           the program's) drops WS_CLIPSIBLINGS unless its parent clips
           its children; the combo box keeps it (US33 cs on 3.1: BUTTON,
           STATIC, EDIT, LISTBOX, SCROLLBAR made with it read back without,
           COMBOBOX and a class without CS_PARENTDC with it, any of them
           in a WS_CLIPCHILDREN parent with it; SetWindowLong keeps it) */
        if ((c->style & CS_PARENTDC) && pw && !(pw->style & WS_CLIPCHILDREN) &&
            c->lpfnWndProc != (WNDPROC)ComboBoxWndProc)
            style &= ~WS_CLIPSIBLINGS;
        w->hwndParent = parent;
        w->hMenu = (WORD)menu;
    } else {
        w->hwndParent = hwndDesktop;
        style |= WS_CLIPSIBLINGS;               /* top-level windows clip */
        if (pw)
            w->hwndOwner = top_level(parent);
        w->hMenu = (WORD)menu;
        if (!menu && c->lpszMenuName)
            w->hMenu = (WORD)LoadMenu(hinst, class_menu_name(c));
    }
    if ((style & WS_VISIBLE) && hwndDesktop) {
        show = TRUE;
        style &= ~WS_VISIBLE;
    }
    w->style = style;
    if (title && !ISATOM(title) && !set_text(w, title)) {
        /* the class's menu too (USER audit, item 19) */
        if (!(style & WS_CHILD) && !menu && w->hMenu)
            DestroyMenu((HMENU)w->hMenu);
        c->cwnd--;
        free_wnd(h);
        return 0;
    }

    /* default position and size */
    if (!(style & (WS_CHILD | WS_POPUP))) {
        if (x == CW_USEDEFAULT) {
            /* (22 on VGA: on 3.1 the second window, after Notepad at 0,
               came at 22,22: Clock under TM2, ARCHITECTURE.md 15.5) */
            int step = sysmet[SM_CYCAPTION] + 2 * sysmet[SM_CYBORDER];
            x = y = (cascade++ % 8) * step;
            if (y + step > sysmet[SM_CYSCREEN] / 2)
                cascade = 0;
            if (cy != CW_USEDEFAULT && cy)
                sw = cy;                /* y of CW_USEDEFAULT: the show command */
            if (cx == CW_USEDEFAULT) {
                /* the screen's width less a frame, down to the row of
                   icons, and with CS_BYTEALIGNCLIENT a client area a
                   whole number of 8 pixels wide (started first on 3.1 on
                   640x480: Terminal 636x408 at 0,0; Notepad, whose class
                   has CS_BYTEALIGNCLIENT, 632x408, then moved to x 4.
                   UNVERIFIED for later, cascaded ones, and whether the
                   client width is aligned in other cases too) */
                int fx = (style & WS_THICKFRAME) ? sysmet[SM_CXFRAME] : sysmet[SM_CXBORDER];
                cx = sysmet[SM_CXSCREEN] - x - sysmet[SM_CXFRAME];
                cy = sysmet[SM_CYSCREEN] - y - sysmet[SM_CYICONSPACING];
                if (c->style & CS_BYTEALIGNCLIENT)
                    cx -= (cx - 2 * fx) % 8;
            }
        } else if (cx == CW_USEDEFAULT) {
            cx = sysmet[SM_CXSCREEN] * 3 / 4;
            cy = sysmet[SM_CYSCREEN] * 3 / 4;
        }
    } else {
        if (x == CW_USEDEFAULT)
            x = y = 0;
        if (cx == CW_USEDEFAULT)
            cx = cy = 0;
    }
    if (pw && (style & WS_CHILD)) {
        x += pw->rcClient.left;
        y += pw->rcClient.top;
    }
    SetRect(&w->rcWindow, x, y, x + cx, y + cy);
    w->rcClient = w->rcWindow;
    if (w->hwndParent)
        link_window(w, (style & WS_CHILD) ? HWND_BOTTOM : HWND_TOP);
    else if (!hwndDesktop)
        hwndDesktop = h;

    /* WH_CBT's HCBT_CREATEWND before any message (US30 cbt on 3.1: C3
       first; also for the icon title window); nonzero: no window */
    if (hwndDesktop && h != hwndDesktop) {
        CREATESTRUCT ccs;
        struct { CREATESTRUCT __far *lpcs; HWND hwndInsertAfter; } cw;
        ccs.lpCreateParams = param;
        ccs.hInstance = hinst;
        ccs.hMenu = menu;
        ccs.hwndParent = parent;
        ccs.cy = cy;
        ccs.cx = cx;
        ccs.y = y;
        ccs.x = x;
        ccs.style = style;
        ccs.lpszName = title;
        ccs.lpszClass = clsname;
        ccs.dwExStyle = exstyle;
        cw.lpcs = &ccs;
        cw.hwndInsertAfter = (style & WS_CHILD) ? (HWND)HWND_BOTTOM : (HWND)HWND_TOP;
        if (cbt_hook(HCBT_CREATEWND, (WPARAM)h, (LPARAM)(void __far *)&cw)) {
            if ((w = valid_wnd(h)) != 0) {
                unlink_window(w);
                free_window(w);
            }
            return 0;
        }
        if (!(w = valid_wnd(h)))
            return 0;
    }

    /* WM_GETMINMAXINFO for overlapped and thick-frame windows (US29 nc
       on 3.1: not for a popup without a thick frame, with a caption or a
       maximise box or not; w16 sent it to every popup) */
    if (!(style & (WS_CHILD | WS_POPUP)) || (style & WS_THICKFRAME)) {
        minmax(w, &mm);
        send_message(h, WM_GETMINMAXINFO, 0, (LPARAM)(MINMAXINFO __far *)&mm);
        /* every message from here on can destroy the window, or its
           parent: looked up again after each (USER audit, item 10) */
        if (!(w = valid_wnd(h)))
            return 0;
        if (cx > mm.ptMaxTrackSize.x)
            cx = mm.ptMaxTrackSize.x;
        if (cy > mm.ptMaxTrackSize.y)
            cy = mm.ptMaxTrackSize.y;
        /* and no smaller than the minimum tracking size (TK75 on 3.1:
           an overlapped window and an MDI child with a caption and a
           thick frame made 30 x 10 or 0 x 0 came out 102 x 26; a popup or
           plain child with a thick frame and no caption stayed 30 x 10) */
        if (cx < mm.ptMinTrackSize.x)
            cx = mm.ptMinTrackSize.x;
        if (cy < mm.ptMinTrackSize.y)
            cy = mm.ptMinTrackSize.y;
        if (cx < 0)
            cx = 0;
        if (cy < 0)
            cy = 0;
        w->rcWindow.right = w->rcWindow.left + cx;
        w->rcWindow.bottom = w->rcWindow.top + cy;
    }

    cs.lpCreateParams = param;
    cs.hInstance = hinst;
    cs.hMenu = menu;
    cs.hwndParent = parent;
    cs.cy = cy;
    cs.cx = cx;
    pw = parent ? valid_wnd(parent) : (PWND)0;
    cs.y = (style & WS_CHILD) && pw ? y - pw->rcClient.top : y;
    cs.x = (style & WS_CHILD) && pw ? x - pw->rcClient.left : x;
    cs.style = style;
    cs.lpszName = title;
    cs.lpszClass = clsname;
    cs.dwExStyle = exstyle;

    r = send_message(h, WM_NCCREATE, 0, (LPARAM)(CREATESTRUCT __far *)&cs);
    if (!(w = valid_wnd(h)))
        return 0;
    if (!r) {
        /* never made: WM_NCDESTROY alone (US29 nc on 3.1: WM_NCCREATE,
           WM_NCDESTROY; w16 sent WM_DESTROY too: USER audit, item 18) */
        send_message(h, WM_NCDESTROY, 0, 0);
        if ((w = valid_wnd(h)) != 0) {
            purge_window_msgs(w->hq, h);
            unlink_window(w);
            free_window(w);
        }
        return 0;
    }
    calc_client(w);
    byte_align(w, TRUE);
    r = send_message(h, WM_CREATE, 0, (LPARAM)(CREATESTRUCT __far *)&cs);
    if (!(w = valid_wnd(h)))
        return 0;
    if (r == -1) {
        destroy_window(h);
        return 0;
    }
    w->flags |= WF_CREATED;
    /* an overlapped (top-level, not popup) window gets WM_SIZE and
       WM_MOVE when it is first shown (3.1 trace: Notepad's main window
       got none in CreateWindow; its WM_SIZE handler places its edit
       control, which did not exist yet, so w16's early WM_SIZE left the
       edit at 0,0 instead of 8,2); a child or a popup gets them right
       after WM_CREATE, hidden or visible (TK71 on 3.1: WM_NCCREATE,
       WM_NCCALCSIZE, WM_CREATE, WM_SIZE, WM_MOVE, then WM_PARENTNOTIFY
       and the show). w16 held them back for every hidden window, and
       StarOffice's StarView, whose MDI children and controls take their
       size from WM_SIZE, faulted later in its own message 046Eh. */
    /* (w16's list boxes, combo boxes' lists among them, still get theirs
       at the first show: their whole-item height worked out from an
       at-create WM_SIZE used the system font's items before the dialog
       font came, 98 high where 3.1's is 92: boot-test case combo.
       UNVERIFIED what 3.1's list box does with that WM_SIZE) */
    if (!(style & (WS_CHILD | WS_POPUP)) || w->lpfnWndProc == (WNDPROC)ListBoxWndProc ||
        w->lpfnWndProc == (WNDPROC)ComboLBoxWndProc)
        w->flags |= WF_SENDSIZE;
    else {
        send_message(h, WM_SIZE, SIZE_RESTORED,
                     MAKELONG(w->rcClient.right - w->rcClient.left,
                              w->rcClient.bottom - w->rcClient.top));
        if (!(w = valid_wnd(h)))
            return 0;
        {
            int mx = w->rcClient.left, my = w->rcClient.top;
            pw = parent ? valid_wnd(parent) : (PWND)0;
            if ((style & WS_CHILD) && pw) {
                mx -= pw->rcClient.left;
                my -= pw->rcClient.top;
            }
            send_message(h, WM_MOVE, 0, MAKELONG(mx, my));
        }
    }
    /* WS_MINIMIZE: to its icon's place now (US24; user/pos.c) */
    if ((style & (WS_MINIMIZE | WS_CHILD)) == WS_MINIMIZE && valid_wnd(h))
        minimize_new(h);
    if (!(w = valid_wnd(h)))
        return 0;
    if ((style & WS_CHILD) && !(exstyle & WS_EX_NOPARENTNOTIFY) && valid_wnd(parent))
        send_message(parent, WM_PARENTNOTIFY, WM_CREATE, MAKELONG(h, w->hMenu));
    if (show && valid_wnd(h)) {
        if (style & WS_MINIMIZE)
            sw = SW_SHOWMINIMIZED;
        else if (style & WS_MAXIMIZE)
            sw = SW_SHOWMAXIMIZED;
        ShowWindow(h, sw);
    }
    return valid_wnd(h) ? h : 0;
}

/* a bad class name or title: 0, no window (3.1, US27) */
HWND WINAPI CreateWindow(LPCSTR cls, LPCSTR title, DWORD style, int x, int y,
                         int cx, int cy, HWND parent, HMENU menu, HINSTANCE hinst,
                         void __far *param)
{
    if (GP_BAD(cls) || GP_BAD(title))
        return 0;
    return create_window(0, cls, title, style, x, y, cx, cy, parent, menu, hinst, param);
}

HWND WINAPI CreateWindowEx(DWORD ex, LPCSTR cls, LPCSTR title, DWORD style, int x, int y,
                           int cx, int cy, HWND parent, HMENU menu, HINSTANCE hinst,
                           void __far *param)
{
    if (GP_BAD(cls) || GP_BAD(title))
        return 0;
    return create_window(ex, cls, title, style, x, y, cx, cy, parent, menu, hinst, param);
}

static void free_window(PWND w)
{
    HWND h = HW(w);
    kill_window_timers(h);
    if (w->hrgnUpdate) {
        if ((WORD)w->hrgnUpdate > 1)
            delete_private(w->hrgnUpdate);
        w->hrgnUpdate = 0;
    }
    if (w->flags & WF_INPAINT) {
        w->flags &= ~WF_INPAINT;
        dec_paint(w->hq);
    }
    if (w->text)
        uh_free(&uhStrs[TEXTHEAP(w)], w->text);
    if (!(w->style & WS_CHILD) && w->hMenu)
        DestroyMenu((HMENU)w->hMenu);
    if (w->hSysMenu)
        DestroyMenu((HMENU)w->hSysMenu);
    free_props(w);
    free_scroll(w);
    /* its DCs: not released ones back to the cache, reset (US29 dc on
       3.1: the next GetDC had none of the pen or mode left), its own
       one freed. Never called before: a DC not released stayed out for
       good, and a CS_OWNDC window's DC went to the next window given the
       same handle (USER audit, item 14) */
    free_window_dcs(h);
    ((PCLS)w->cls)->cwnd--;
    w->cls = 0;
    w->flags |= 0x8000;
    free_wnd(h);
}

static void destroy_tree(HWND h)
{
    PWND w = valid_wnd(h);
    HWND c;
    if (!w)
        return;
    w->flags |= WF_DESTROYING;
    clip_window_destroyed(h);       /* clip.c: WM_RENDERALLFORMATS first */
    {
        void savebits_forget(HWND h);
        savebits_forget(h);
    }
    send_message(h, WM_DESTROY, 0, 0);
    while ((w = valid_wnd(h)) != 0 && (c = w->hwndChild) != 0) {
        PWND cw = valid_wnd(c);
        if (!cw)
            break;
        destroy_tree(c);
        if (valid_wnd(c)) {             /* still there: unlink it anyway */
            unlink_window(PW(c));
        }
    }
    if ((w = valid_wnd(h)) == 0)
        return;
    if (w->flags & WF_DLG) {
        void dlg_free_editseg(HWND h);
        dlg_free_editseg(h);
    }
    free_place(h);
    send_message(h, WM_NCDESTROY, 0, 0);
    if ((w = valid_wnd(h)) == 0)
        return;
    purge_window_msgs(w->hq, h);
    unlink_window(w);
    free_window(w);
}

/* A window whose destruction will never finish: its task ended inside
   it (a WM_DESTROY, say). Out of the tree and freed, with its children,
   and without messages: the window procedures' task is gone. */
void destroy_abandoned(HWND h)
{
    PWND w = valid_wnd(h);
    HWND c;
    if (!w)
        return;
    w->flags |= WF_DESTROYING;
    while ((w = valid_wnd(h)) != 0 && (c = w->hwndChild) != 0) {
        destroy_abandoned(c);
        if (valid_wnd(c))
            unlink_window(PW(c));
    }
    if ((w = valid_wnd(h)) == 0)
        return;
    if (h == hwndCapture)
        hwndCapture = 0;
    if (h == hwndFocus)
        hwndFocus = 0;
    if (h == hwndActive)
        hwndActive = 0;
    if (h == hwndActivePrev)    /* (its handle can come back as another window) */
        hwndActivePrev = 0;
    free_place(h);
    purge_window_msgs(w->hq, h);
    unlink_window(w);
    free_window(w);
}

/* the window activated when the active one h goes: the first visible,
   enabled top-level window that is not h, one h owns, or an icon's title
   (US30 show on 3.1: with an icon above the next open window, the icon
   became active; w16 activated the icon's title window, and when it did
   so before the destruction, as 3.1 does, Task List's End Task and
   tiling went wrong: boot test taskman) */
static HWND next_after_destroy(HWND h)
{
    HWND t;
    for (t = PW(hwndDesktop)->hwndChild; t; t = PW(t)->hwndNext) {
        PWND tw = PW(t);
        if (t != h && (!h || tw->hwndOwner != h) && (tw->style & WS_VISIBLE) &&
            !(tw->style & WS_DISABLED) && ((PCLS)tw->cls)->atom != 32772)
            return t;
    }
    return 0;
}

BOOL destroy_window(HWND h)
{
    PWND w = valid_wnd(h), p;
    HWND parent, owned, n;
    RECT rc;
    BOOL wasvis, hadfocus;
    if (!w || h == hwndDesktop)
        return FALSE;
    if (w->flags & WF_DESTROYING)
        return TRUE;
    /* WH_CBT (US30 cbt on 3.1: C4 for the window and for its owned
       windows, not for its children); nonzero: it stays */
    if (cbt_hook(HCBT_DESTROYWND, (WPARAM)h, 0) || !(w = valid_wnd(h)))
        return FALSE;
    p = PW(w->hwndParent);
    parent = w->hwndParent;
    if ((w->style & WS_CHILD) && !(w->exstyle & WS_EX_NOPARENTNOTIFY) && p)
        send_message(parent, WM_PARENTNOTIFY, WM_DESTROY, MAKELONG(h, w->hMenu));
    if (!(w = valid_wnd(h)))
        return TRUE;
    /* being destroyed from here on: a DestroyWindow of it from inside
       (an owned window's WM_DESTROY destroying its owner) returns at
       once. 3.1 starts over then, and over again for ever: US29 dw, the
       owned window got WM_DESTROY until the stack ran out. */
    w->flags |= WF_DESTROYING;
    /* owned windows go first, in z-order (US29 dw.owner on 3.1: W2 W82
       O2 O82). The list is looked at again from the top after each, as
       a WM_DESTROY can change it, but a window already being destroyed is
       passed over: it stayed linked, and the loop came back to it for
       ever (USER audit, item 4) */
    if (!(w->style & WS_CHILD)) {
        for (owned = PW(hwndDesktop)->hwndChild; owned; owned = n) {
            PWND ow = PW(owned);
            n = ow->hwndNext;
            if (ow->hwndOwner == h && !(ow->flags & WF_DESTROYING)) {
                destroy_window(owned);
                n = PW(hwndDesktop)->hwndChild;
            }
        }
    }
    if (!(w = valid_wnd(h)))
        return TRUE;
    /* (3.1 activates the next window before the window goes - US30 cbt:
       its ACTIVATE names the dying window as the active one - where w16
       does it afterwards; done here, with the same choice of window, Task
       List's End Task and tiling went wrong: boot test taskman. Left:
       docs/notes/user-audit-triage.md, in git history) */
    wasvis = is_visible(w);
    rc = w->rcWindow;
    hadfocus = hwndFocus && (hwndFocus == h || IsChild(h, hwndFocus));
    if (wasvis)
        set_window_pos(h, 0, 0, 0, 0, 0, SWP_HIDEWINDOW | SWP_NOACTIVATE | SWP_NOMOVE |
                       SWP_NOSIZE | SWP_NOZORDER);
    if (!(w = valid_wnd(h)))
        return TRUE;
    if (hwndCapture && (h == hwndCapture || IsChild(h, hwndCapture)))
        ReleaseCapture();       /* the input queue goes back with it */
    destroy_tree(h);
    if (hwndFocus && !valid_wnd(hwndFocus))
        hwndFocus = 0;
    /* a child that had the focus leaves it to its parent (TK75 on 3.1:
       an MDI child destroyed with WM_MDIDESTROY, the focus then on the
       MDI client; the client destroyed, on its frame. w16 left the focus
       nowhere) */
    if (hadfocus && !hwndFocus && parent && (p = valid_wnd(parent)) != 0 &&
        parent != hwndDesktop && !(p->flags & WF_DESTROYING) && is_visible(p))
        set_focus(parent);
    if (hwndActive && !valid_wnd(hwndActive)) {
        hwndActive = 0;
        {
            HWND t = next_after_destroy(0);
            if (t)
                activate_window(t, FALSE);
        }
    }
    (void)rc;
    return TRUE;
}

BOOL WINAPI DestroyWindow(HWND h)
{
    return destroy_window(h);
}

BOOL WINAPI IsWindow(HWND h)
{
    return valid_wnd(h) != 0;
}

HWND WINAPI GetDesktopWindow(void)
{
    return hwndDesktop;
}

HWND WINAPI GetParent(HWND h)
{
    PWND w = valid_wnd(h);
    if (!w)
        return 0;
    if (w->style & WS_CHILD)
        return w->hwndParent;
    return w->hwndOwner;
}

HWND WINAPI SetParent(HWND h, HWND parent)
{
    PWND w = valid_wnd(h);
    HWND old;
    BOOL vis;
    if (!w)
        return 0;
    if (!parent)
        parent = hwndDesktop;
    if (!valid_wnd(parent))
        return 0;
    /* not the desktop, and not under itself or its own descendant: 3.1
       ends there (US29 sd: SetParent of the desktop; ss: of a window to
       itself, which it does, GetParent then the window, and hangs; sc: to
       its child, done too, a cycle) and w16 looped for ever (USER audit,
       item 13); refused here */
    if (h == hwndDesktop || parent == h || IsChild(h, parent))
        return 0;
    old = w->hwndParent;
    vis = (w->style & WS_VISIBLE) != 0;
    if (vis)
        ShowWindow(h, SW_HIDE);
    {
        int dx = PW(parent)->rcClient.left - PW(old)->rcClient.left;
        int dy = PW(parent)->rcClient.top - PW(old)->rcClient.top;
        unlink_window(w);
        w->hwndParent = parent;
        link_window(w, HWND_TOP);
        OffsetRect(&w->rcWindow, dx, dy);
        OffsetRect(&w->rcClient, dx, dy);
    }
    invalidate_dce_cache();
    if (vis)
        ShowWindow(h, SW_SHOWNA);
    return old;
}

BOOL WINAPI IsChild(HWND parent, HWND h)
{
    PWND w = valid_wnd(h);
    while (w && (w->style & WS_CHILD)) {
        if (w->hwndParent == parent)
            return TRUE;
        w = valid_wnd(w->hwndParent);
    }
    return FALSE;
}

HWND WINAPI GetWindow(HWND h, UINT cmd)
{
    PWND w = valid_wnd(h);
    HWND r;
    if (!w)
        return 0;
    switch (cmd) {
    case GW_HWNDFIRST:
        return w->hwndParent ? PW(w->hwndParent)->hwndChild : 0;
    case GW_HWNDLAST:
        if (!w->hwndParent)
            return 0;
        for (r = HW(w); PW(r)->hwndNext; r = PW(r)->hwndNext)
            ;
        return r;
    case GW_HWNDNEXT:
        return w->hwndNext;
    case GW_HWNDPREV:
        if (!w->hwndParent)
            return 0;
        r = PW(w->hwndParent)->hwndChild;
        if (r == h)
            return 0;
        while (r && PW(r)->hwndNext != h)
            r = PW(r)->hwndNext;
        return r;
    case GW_OWNER:
        return w->hwndOwner;
    case GW_CHILD:
        return w->hwndChild;
    }
    return 0;
}

HWND WINAPI GetNextWindow(HWND h, UINT cmd)
{
    return GetWindow(h, cmd == GW_HWNDPREV ? GW_HWNDPREV : GW_HWNDNEXT);
}

HWND WINAPI GetTopWindow(HWND h)
{
    PWND w = valid_wnd(h ? h : hwndDesktop);
    return w ? w->hwndChild : 0;
}

BOOL WINAPI IsWindowVisible(HWND h)
{
    PWND w = valid_wnd(h);
    return w && is_visible(w);
}

BOOL WINAPI IsWindowEnabled(HWND h)
{
    PWND w = valid_wnd(h);
    return w && !(w->style & WS_DISABLED);
}

BOOL WINAPI EnableWindow(HWND h, BOOL enable)
{
    PWND w = valid_wnd(h);
    BOOL was;
    if (!w)
        return FALSE;
    was = (w->style & WS_DISABLED) != 0;
    if (enable && was) {
        w->style &= ~WS_DISABLED;
        send_message(h, WM_ENABLE, TRUE, 0);
    } else if (!enable && !was) {
        send_message(h, WM_CANCELMODE, 0, 0);
        w->style |= WS_DISABLED;
        /* the window itself loses the focus; a child of it keeps it
           (US39 dis on 3.1: A disabled with the focus on its child C,
           GetFocus still answered C) */
        if (h == hwndFocus)
            set_focus(0);
        send_message(h, WM_ENABLE, FALSE, 0);
    }
    return was;
}

BOOL WINAPI IsIconic(HWND h)
{
    PWND w = valid_wnd(h);
    return w && (w->style & WS_MINIMIZE);
}

BOOL WINAPI IsZoomed(HWND h)
{
    PWND w = valid_wnd(h);
    return w && (w->style & WS_MAXIMIZE);
}

/* ---- window text ---- */
/* the count is a WORD: 32768 or more is a count, not a negative number,
   and 0 copies nothing yet still empties the buffer (probe US36 on 3.1:
   51193 characters copied for 51194, NUL at buf[0] for 0) */
int WINAPI GetWindowText(HWND h, LPSTR buf, int n)
{
    if (!valid_wnd(h))
        return 0;
    buf[0] = 0;
    if (!n)
        return 0;
    return (int)send_message(h, WM_GETTEXT, (WORD)n, (LPARAM)buf);
}

int WINAPI GetWindowTextLength(HWND h)
{
    if (!valid_wnd(h))
        return 0;
    return (int)send_message(h, WM_GETTEXTLENGTH, 0, 0);
}

/* a bad string: the text as it was (3.1) */
void WINAPI SetWindowText(HWND h, LPCSTR s)
{
    if (GP_BAD(s))
        return;
    if (valid_wnd(h))
        send_message(h, WM_SETTEXT, 0, (LPARAM)s);
}

/* DefWindowProc's part; a bad buffer (WM_GETTEXT) or string (WM_SETTEXT):
   0, the text as it was (3.1) */
LONG def_gettext(PWND w, WORD n, LPSTR buf)
{
    /* n a WORD (US36: 32768 to 65535 copy the text) */
    if (!n || GP_BAD(buf))
        return 0;
    if (!w->text) {
        buf[0] = 0;
        return 0;
    }
    lstr_cpyn(buf, TEXTPTR(w), n);
    return lstr_len(buf);
}

LONG def_gettextlength(PWND w)
{
    return w->text ? lstr_len(TEXTPTR(w)) : 0;
}

BOOL def_settext(PWND w, LPCSTR s)
{
    if (GP_BAD(s))
        return FALSE;
    return set_text(w, s);
}

/* ---- window words and longs ---- */
static void __far *extra_field(PWND w, int i, WORD size)
{
    if (i >= 0 && i + size <= (WORD)((PCLS)w->cls)->cbWndExtra)
        return WND_EXTRA(w) + i;
    return 0;
}

WORD WINAPI GetWindowWord(HWND h, int i)
{
    PWND w = valid_wnd(h);
    WORD __far *p;
    if (!w)
        return 0;
    switch (i) {
    case GWW_HINSTANCE: return (WORD)w->hInstance;
    case GWW_HWNDPARENT: return (w->style & WS_CHILD) ? (WORD)w->hwndParent : (WORD)w->hwndOwner;
    case GWW_ID: return w->hMenu;
    }
    p = extra_field(w, i, 2);
    return p ? *p : 0;
}

WORD WINAPI SetWindowWord(HWND h, int i, WORD v)
{
    PWND w = valid_wnd(h);
    WORD old, __far *p;
    if (!w)
        return 0;
    switch (i) {
    case GWW_HINSTANCE: old = (WORD)w->hInstance; w->hInstance = (HINSTANCE)v; return old;
    case GWW_ID: old = w->hMenu; w->hMenu = v; return old;
    case GWW_HWNDPARENT:
        if (w->style & WS_CHILD)
            return 0;
        old = (WORD)w->hwndOwner;
        w->hwndOwner = (HWND)v;
        return old;
    }
    p = extra_field(w, i, 2);
    if (!p)
        return 0;
    old = *p;
    *p = v;
    return old;
}

LONG WINAPI GetWindowLong(HWND h, int i)
{
    PWND w = valid_wnd(h);
    LONG __far *p;
    if (!w)
        return 0;
    switch (i) {
    case GWL_WNDPROC: return (LONG)w->lpfnWndProc;
    case GWL_STYLE: return w->style;
    case GWL_EXSTYLE: return w->exstyle;
    }
    p = extra_field(w, i, 4);
    return p ? *p : 0;
}

LONG WINAPI SetWindowLong(HWND h, int i, LONG v)
{
    PWND w = valid_wnd(h);
    LONG old, __far *p;
    if (!w)
        return 0;
    switch (i) {
    case GWL_WNDPROC: old = (LONG)w->lpfnWndProc; w->lpfnWndProc = (WNDPROC)v; return old;
    case GWL_STYLE:
        old = w->style;
        w->style = v;
        if ((old ^ v) & (WS_VISIBLE | WS_CLIPCHILDREN | WS_CLIPSIBLINGS | WS_MINIMIZE))
            invalidate_dce_cache();
        return old;
    case GWL_EXSTYLE: old = w->exstyle; w->exstyle = v; return old;
    }
    p = extra_field(w, i, 4);
    if (!p)
        return 0;
    old = *p;
    *p = v;
    return old;
}

/* ---- enumeration and search ----
   The windows are listed first and the callback called for each one still
   a window: one it destroys is passed over, one it moves comes once (US29
   en on 3.1: E4 E3 E2 E1 with E3 destroyed and E1 brought to the top at
   E4 gave 4 2 1; children depth first, 1 2 3 g 4 with 2 destroyed and 4
   brought up at 1 gave 1 3 g 4). w16 walked the live list, so a callback
   that reordered or destroyed windows could repeat or end the walk
   (USER audit, item 11). The list is a global block (it can be long). */
static void list_add(HWND __far *l, WORD *n, HWND h)
{
    if (l)
        l[*n] = h;
    (*n)++;
}

static void list_children(HWND parent, HWND __far *l, WORD *n)
{
    HWND h;
    for (h = PW(parent)->hwndChild; h; h = PW(h)->hwndNext) {
        list_add(l, n, h);
        list_children(h, l, n);
    }
}

/* top-level windows (of queue hq, if not 0), or every descendant of
   parent: a locked block and the count; NULL if none or no memory */
static HWND __far *list_windows(HWND parent, HQUEUE hq, WORD *n)
{
    HWND h, __far *l = 0;
    HGLOBAL g;
    int pass;
    for (pass = 0; pass < 2; pass++) {
        *n = 0;
        if (parent)
            list_children(parent, l, n);
        else
            for (h = PW(hwndDesktop)->hwndChild; h; h = PW(h)->hwndNext)
                if (!hq || PW(h)->hq == hq)
                    list_add(l, n, h);
        if (pass || !*n)
            break;
        g = GlobalAlloc(GMEM_MOVEABLE, (DWORD)*n * sizeof(HWND));
        if (!g) {
            *n = 0;
            return 0;
        }
        l = (HWND __far *)GlobalLock(g);
    }
    return *n ? l : 0;
}

static void list_free(HWND __far *l)
{
    HGLOBAL g;
    if (!l)
        return;
    g = (HGLOBAL)LOWORD(GlobalHandle(SELECTOROF(l)));
    GlobalUnlock(g);
    GlobalFree(g);
}

static BOOL enum_list(HWND __far *l, WORD n, WNDENUMPROC f, LPARAM lp)
{
    WORD i;
    BOOL r = TRUE;
    for (i = 0; i < n; i++)
        if (valid_wnd(l[i]) && !((BOOL (__far __pascal *)(HWND, LPARAM))f)(l[i], lp)) {
            r = FALSE;
            break;
        }
    list_free(l);
    return r;
}

BOOL WINAPI EnumWindows(WNDENUMPROC f, LPARAM lp)
{
    WORD n;
    HWND __far *l = list_windows(0, 0, &n);
    return enum_list(l, n, f, lp);
}

BOOL WINAPI EnumChildWindows(HWND parent, WNDENUMPROC f, LPARAM lp)
{
    WORD n;
    HWND __far *l;
    if (!valid_wnd(parent))
        return TRUE;
    l = list_windows(parent, 0, &n);
    return enum_list(l, n, f, lp);
}

BOOL WINAPI EnumTaskWindows(HTASK t, WNDENUMPROC f, LPARAM lp)
{
    WORD n;
    HQUEUE hq = K_GETTASKQUEUE(t);
    HWND __far *l = hq ? list_windows(0, hq, &n) : 0;
    if (!l)
        return TRUE;
    return enum_list(l, n, f, lp);
}

/* A bad class name or title: 0 (3.1, US27). The windows' titles are
   compared through an 80-byte buffer, so a title of 80 characters or more
   is never found (US29 fw on 3.1: 79 found, 80, 100, 127, 150 not; w16's
   buffer was 128 bytes) */
HWND WINAPI FindWindow(LPCSTR cls, LPCSTR title)
{
    HWND h;
    char buf[80];
    PCLS c;
    ATOM a = 0;
    if (GP_BAD(cls) || GP_BAD(title))
        return 0;
    c = cls ? find_class(cls, 0) : (PCLS)0;
    if (cls) {
        if (ISATOM(cls))
            a = LOWORD((DWORD)cls);
        else
            a = c ? c->atom : FindAtom(cls);
        if (!a)
            return 0;
    }
    for (h = PW(hwndDesktop)->hwndChild; h; h = PW(h)->hwndNext) {
        PWND w = PW(h);
        if (cls && ((PCLS)w->cls)->atom != a)
            continue;
        if (title) {
            def_gettext(w, sizeof buf, buf);
            if (lstrcmpi(buf, title))
                continue;
        }
        return h;
    }
    return 0;
}

HTASK WINAPI GetWindowTask(HWND h)
{
    PWND w = valid_wnd(h);
    return w && w->hq ? q_of(w->hq)->task : 0;
}

HWND WINAPI GetLastActivePopup(HWND h)
{
    PWND w = valid_wnd(h);
    if (!w)
        return 0;
    return w->hwndLastActive && valid_wnd(w->hwndLastActive) ? w->hwndLastActive : h;
}

/* ---- coordinates ----
   A caller's POINTs and RECTs through gp_copy (gp.asm): a bad one is left
   as it was, the part of one below a segment's limit changed (3.1, US27) */
static void offset_points(POINT __far *p, UINT n, int dx, int dy)
{
    POINT a;
    WORD k;
    while (n--) {
        k = gp_copy(&a, p, sizeof a) & ~1;
        a.x += dx;
        a.y += dy;
        if (gp_copy(p, &a, k) != sizeof a)
            return;
        p++;
    }
}

void WINAPI ClientToScreen(HWND h, POINT __far *p)
{
    PWND w = valid_wnd(h);
    if (w)
        offset_points(p, 1, w->rcClient.left, w->rcClient.top);
}

void WINAPI ScreenToClient(HWND h, POINT __far *p)
{
    PWND w = valid_wnd(h);
    if (w)
        offset_points(p, 1, -w->rcClient.left, -w->rcClient.top);
}

void WINAPI MapWindowPoints(HWND from, HWND to, POINT __far *p, UINT n)
{
    int dx = 0, dy = 0;
    PWND w;
    if (from && (w = valid_wnd(from)) != 0)
        dx += w->rcClient.left, dy += w->rcClient.top;
    if (to && (w = valid_wnd(to)) != 0)
        dx -= w->rcClient.left, dy -= w->rcClient.top;
    offset_points(p, n, dx, dy);
}

void WINAPI GetWindowRect(HWND h, RECT __far *r)
{
    PWND w = valid_wnd(h);
    if (w)
        gp_copy(r, &w->rcWindow, sizeof(RECT));
}

void WINAPI GetClientRect(HWND h, RECT __far *r)
{
    PWND w = valid_wnd(h);
    if (w)
        SetRect(r, 0, 0, w->rcClient.right - w->rcClient.left,
                w->rcClient.bottom - w->rcClient.top);
}

static HWND child_from_point(HWND parent, POINT pt, BOOL skiphidden)
{
    HWND h;
    for (h = PW(parent)->hwndChild; h; h = PW(h)->hwndNext) {
        PWND w = PW(h);
        if (skiphidden && !(w->style & WS_VISIBLE))
            continue;
        if (PtInRect(&w->rcWindow, pt))
            return h;
    }
    return 0;
}

/* the window under pt below `above' (a window WM_NCHITTEST called
   HTTRANSPARENT): the next sibling under it in z-order that contains pt,
   descended into as WindowFromPoint does; none, the parent */
HWND window_below(HWND above, POINT pt)
{
    PWND w = valid_wnd(above);
    HWND h;
    if (!w)
        return 0;
    for (h = w->hwndNext; h; h = PW(h)->hwndNext) {
        PWND s = PW(h);
        if ((s->style & WS_VISIBLE) && PtInRect(&s->rcWindow, pt))
            break;
    }
    if (!h)
        return w->hwndParent ? w->hwndParent : hwndDesktop;
    for (;;) {
        PWND c = PW(h);
        HWND d;
        if ((c->style & WS_DISABLED) || (c->style & WS_MINIMIZE) || !PtInRect(&c->rcClient, pt))
            return h;
        d = child_from_point(h, pt, TRUE);
        if (!d)
            return h;
        h = d;
    }
}

HWND WINAPI WindowFromPoint(POINT pt)
{
    HWND h = hwndDesktop, c;
    for (;;) {
        PWND w = PW(h);
        if (h != hwndDesktop && ((w->style & WS_DISABLED) || (w->style & WS_MINIMIZE)))
            return h;
        if (h != hwndDesktop && !PtInRect(&w->rcClient, pt))
            return h;
        c = child_from_point(h, pt, TRUE);
        if (!c)
            return h;
        h = c;
    }
}

HWND WINAPI ChildWindowFromPoint(HWND parent, POINT pt)
{
    PWND w = valid_wnd(parent);
    HWND c;
    if (!w)
        return 0;
    pt.x += w->rcClient.left;
    pt.y += w->rcClient.top;
    if (!PtInRect(&w->rcWindow, pt))
        return 0;
    c = child_from_point(parent, pt, FALSE);
    return c ? c : parent;
}

/* ---- properties ----
   As 3.1's (US15): a window's properties are one block, WND+36 its
   handle, an entry of an atom and the data per property (4 bytes; 3.1's
   grew by 4 a property), string names global atoms (3.1: GlobalFindAtom
   finds a property's name while a window has it and not after). w16 keeps
   the block in the object heap (uheap.c), not DGROUP. Entries with atom 0
   are unused (a block can be larger than asked for). Atoms from C000h up
   are names this list holds a reference to. */
typedef struct PROP { ATOM atom; HANDLE data; } PROP;
#define PROPS(w)    ((PROP __far *)UH_PTR(&uhObj, (w)->props))

static UINT nprops(PWND w)
{
    return w->props ? uh_size(&uhObj, w->props) / sizeof(PROP) : 0;
}

static ATOM prop_atom(LPCSTR name, BOOL add)
{
    if (ISATOM(name)) {
        ATOM a = LOWORD((DWORD)name);
        char b[64];
        /* a string atom given as a number: one more reference, so that
           every entry from C000h up holds one */
        if (add && a >= 0xC000u && ga_name(a, b, sizeof b))
            return ga_add(b);
        return a;
    }
    return add ? ga_add(name) : ga_find(name);
}

static void prop_unref(ATOM a)
{
    if (a >= 0xC000u)
        ga_delete(a);
}

/* the entry's index, -1 none */
static int find_prop(PWND w, ATOM a)
{
    UINT i, n = nprops(w);
    PROP __far *p = PROPS(w);
    if (!a)
        return -1;
    for (i = 0; i < n; i++)
        if (p[i].atom == a)
            return i;
    return -1;
}

/* A bad name: FALSE, 0 (3.1, US27; GetProp and RemoveProp too). A name
   that runs past its segment's end: TRUE, nothing set (3.1: KERNEL's
   atom code faults on it and recovers, and SetProp still returns TRUE;
   UNVERIFIED what 3.1 stores then) */
BOOL WINAPI SetProp(HWND h, LPCSTR name, HANDLE data)
{
    PWND w = valid_wnd(h);
    PROP __far *p;
    UINT i, n;
    ATOM a;
    WORD b;
    if (!w || GP_BAD(name))
        return FALSE;
    if (!ISATOM(name) && HIWORD(gp_strlen(name, 0xFFFF)))
        return TRUE;
    i = find_prop(w, prop_atom(name, FALSE));
    if (i != (UINT)-1) {
        PROPS(w)[i].data = data;
        return TRUE;
    }
    a = prop_atom(name, TRUE);
    if (!a)
        return FALSE;
    n = nprops(w);
    p = PROPS(w);
    for (i = 0; i < n && p[i].atom; i++)
        ;
    if (i == n) {
        b = uh_realloc(&uhObj, w->props, (n + 1) * sizeof(PROP));
        if (!b) {
            prop_unref(a);
            return FALSE;
        }
        w->props = b;
    }
    p = PROPS(w);
    p[i].atom = a;
    p[i].data = data;
    return TRUE;
}

HANDLE WINAPI GetProp(HWND h, LPCSTR name)
{
    PWND w = GP_BAD(name) ? 0 : valid_wnd(h);
    int i = w ? find_prop(w, prop_atom(name, FALSE)) : -1;
    return i >= 0 ? PROPS(w)[i].data : 0;
}

/* entry i goes: the last one takes its place, the block shrinks */
static void drop_prop(PWND w, UINT i)
{
    UINT n = nprops(w);
    PROP __far *p = PROPS(w);
    prop_unref(p[i].atom);
    while (n && !p[n - 1].atom)
        n--;
    if (n && i < n - 1)
        p[i] = p[n - 1];
    n--;
    if (!n) {
        uh_free(&uhObj, w->props);
        w->props = 0;
    } else {
        WORD b;
        p[n].atom = 0;
        b = uh_realloc(&uhObj, w->props, n * sizeof(PROP));   /* smaller: in place */
        if (b)
            w->props = b;
    }
}

HANDLE WINAPI RemoveProp(HWND h, LPCSTR name)
{
    PWND w = GP_BAD(name) ? 0 : valid_wnd(h);
    int i = w ? find_prop(w, prop_atom(name, FALSE)) : -1;
    HANDLE d;
    if (i < 0)
        return 0;
    d = PROPS(w)[i].data;
    drop_prop(w, i);
    return d;
}

/* DestroyWindow: the properties left go, their names' references with
   them */
void free_props(PWND w)
{
    UINT i, n = nprops(w);
    for (i = 0; i < n; i++)
        prop_unref(PROPS(w)[i].atom);
    uh_free(&uhObj, w->props);
    w->props = 0;
}

/* newest first; the procedure may remove the property it is given (each
   entry is read again after the call: nothing in the heap is held) */
int WINAPI EnumProps(HWND h, PROPENUMPROC f)
{
    PWND w = valid_wnd(h);
    int r = -1;
    UINT i;
    char name[64];
    if (!w)
        return -1;
    for (i = nprops(w); i-- > 0;) {
        PROP pp;
        if (!(w = valid_wnd(h)) || i >= nprops(w))
            break;
        pp = PROPS(w)[i];
        if (!pp.atom)
            continue;
        if (pp.atom >= 0xC000u) {
            ga_name(pp.atom, name, sizeof name);
            r = ((int (__far __pascal *)(HWND, LPCSTR, HANDLE))f)(h, name, pp.data);
        } else
            r = ((int (__far __pascal *)(HWND, LPCSTR, HANDLE))f)(h, MAKEINTATOM(pp.atom), pp.data);
        if (!r)
            break;
    }
    return r;
}
