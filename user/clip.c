/*
 * clip.c - the clipboard: OpenClipboard, CloseClipboard, EmptyClipboard,
 * SetClipboardData, GetClipboardData, the format enumeration, registered
 * formats and the viewer chain.
 *
 * Behaviour from the Windows 3.1 SDK's documentation of these functions
 * and messages (the published API): one window (or task) has the
 * clipboard open at a time; EmptyClipboard makes the opener the owner and
 * sends the old owner WM_DESTROYCLIPBOARD; SetClipboardData with a NULL
 * handle promises the data (delayed rendering): GetClipboardData then
 * sends the owner WM_RENDERFORMAT, and the owner's destruction
 * WM_RENDERALLFORMATS; CloseClipboard after a change sends the first
 * viewer WM_DRAWCLIPBOARD. Registered formats are atoms from C000h up, as
 * registered window messages. Windows Write needs the clipboard at start:
 * with the functions stubbed (returning 0) it ended at once, before its
 * window showed (RegisterClipboardFormat was the first stub it called:
 * [w16] userlog=1).
 *
 * UNVERIFIED: the CF_TEXT/CF_OEMTEXT conversions 3.1 offers are not made;
 * GetClipboardData and EnumClipboardFormats need the clipboard opened by
 * the caller's task.
 */
#include "user.h"

typedef struct {
    UINT   fmt;
    HANDLE h;                   /* 0: promised (delayed rendering) */
} CLIPFMT;

static CLIPFMT __near *clip_fmts;      /* LocalAlloc'd, clip_n used */
static WORD clip_n, clip_cap;
static HWND hwndClipOpen, hwndClipOwner, hwndClipViewer;
static HQUEUE hqClipOpen;
static BOOL clip_is_open, clip_changed;
static UINT clip_rendering;     /* the format WM_RENDERFORMAT asks for */
static BOOL clip_render_all;    /* in WM_RENDERALLFORMATS */

static CLIPFMT *find_fmt(UINT fmt)
{
    WORD i;
    for (i = 0; i < clip_n; i++)
        if (clip_fmts[i].fmt == fmt)
            return &clip_fmts[i];
    return NULL;
}

static BOOL opened_by_caller(void)
{
    return clip_is_open && hqClipOpen == cur_hq();
}

static void free_data(UINT fmt, HANDLE h)
{
    if (!h)
        return;
    switch (fmt) {
    case CF_BITMAP:
    case CF_DSPBITMAP:
    case CF_PALETTE:
        DeleteObject(h);
        break;
    case CF_METAFILEPICT:
    case CF_DSPMETAFILEPICT: {
        METAFILEPICT FAR *mp = (METAFILEPICT FAR *)GlobalLock(h);
        if (mp) {
            if (mp->hMF)
                DeleteMetaFile(mp->hMF);
            GlobalUnlock(h);
        }
        GlobalFree(h);
        break;
    }
    case CF_OWNERDISPLAY:
        break;
    default:
        if (fmt >= CF_PRIVATEFIRST && fmt <= CF_PRIVATELAST)
            break;              /* the owner frees these (WM_DESTROYCLIPBOARD) */
        if (fmt >= CF_GDIOBJFIRST && fmt <= CF_GDIOBJLAST)
            DeleteObject(h);
        else
            GlobalFree(h);
        break;
    }
}

BOOL WINAPI OpenClipboard(HWND h)
{
    if (clip_is_open)
        return FALSE;
    if (h && !valid_wnd(h))
        return FALSE;
    clip_is_open = TRUE;
    clip_changed = FALSE;
    hwndClipOpen = h;
    hqClipOpen = cur_hq();
    return TRUE;
}

BOOL WINAPI CloseClipboard(void)
{
    BOOL changed = clip_changed;
    if (!clip_is_open)
        return FALSE;
    clip_is_open = FALSE;
    clip_changed = FALSE;
    hwndClipOpen = 0;
    hqClipOpen = 0;
    if (changed && hwndClipViewer && valid_wnd(hwndClipViewer))
        send_message(hwndClipViewer, WM_DRAWCLIPBOARD, 0, 0);
    return TRUE;
}

BOOL WINAPI EmptyClipboard(void)
{
    WORD i;
    if (!opened_by_caller())
        return FALSE;
    if (hwndClipOwner && valid_wnd(hwndClipOwner))
        send_message(hwndClipOwner, WM_DESTROYCLIPBOARD, 0, 0);
    for (i = 0; i < clip_n; i++)
        free_data(clip_fmts[i].fmt, clip_fmts[i].h);
    clip_n = 0;
    hwndClipOwner = hwndClipOpen;
    clip_changed = TRUE;
    return TRUE;
}

HANDLE WINAPI SetClipboardData(UINT fmt, HANDLE h)
{
    CLIPFMT *f;
    /* the caller has it open, or the owner renders promised data */
    if (!opened_by_caller() && !clip_rendering && !clip_render_all)
        return 0;
    if (!fmt)
        return 0;
    f = find_fmt(fmt);
    if (f) {
        if (f->h && f->h != h)
            free_data(fmt, f->h);
    } else {
        if (clip_n == clip_cap) {
            WORD cap = clip_cap ? clip_cap * 2 : 8;
            CLIPFMT __near *n = clip_fmts
                ? (CLIPFMT __near *)LocalReAlloc((HLOCAL)clip_fmts, cap * sizeof(CLIPFMT), LMEM_MOVEABLE)
                : (CLIPFMT __near *)LocalAlloc(LMEM_FIXED, cap * sizeof(CLIPFMT));
            if (!n)
                return 0;
            clip_fmts = n;
            clip_cap = cap;
        }
        f = &clip_fmts[clip_n++];
        f->fmt = fmt;
    }
    f->h = h;
    if (clip_is_open)
        clip_changed = TRUE;
    return h;
}

HANDLE WINAPI GetClipboardData(UINT fmt)
{
    CLIPFMT *f;
    if (!opened_by_caller())
        return 0;
    f = find_fmt(fmt);
    if (!f)
        return 0;
    if (!f->h && hwndClipOwner && valid_wnd(hwndClipOwner)) {
        UINT was = clip_rendering;
        clip_rendering = fmt;
        send_message(hwndClipOwner, WM_RENDERFORMAT, fmt, 0);
        clip_rendering = was;
        f = find_fmt(fmt);
        if (!f)
            return 0;
    }
    return f->h;
}

int WINAPI CountClipboardFormats(void)
{
    return clip_n;
}

UINT WINAPI EnumClipboardFormats(UINT fmt)
{
    WORD i;
    if (!opened_by_caller())
        return 0;
    if (!fmt)
        return clip_n ? clip_fmts[0].fmt : 0;
    for (i = 0; i + 1 < clip_n; i++)
        if (clip_fmts[i].fmt == fmt)
            return clip_fmts[i + 1].fmt;
    return 0;
}

BOOL WINAPI IsClipboardFormatAvailable(UINT fmt)
{
    return find_fmt(fmt) != NULL;
}

int WINAPI GetPriorityClipboardFormat(UINT FAR *list, int n)
{
    int i;
    if (!clip_n)
        return 0;
    for (i = 0; i < n; i++)
        if (find_fmt(list[i]))
            return list[i];
    return -1;
}

HWND WINAPI GetClipboardOwner(void)
{
    return hwndClipOwner;
}

HWND WINAPI GetOpenClipboardWindow(void)
{
    return clip_is_open ? hwndClipOpen : 0;
}

UINT WINAPI RegisterClipboardFormat(LPCSTR name)
{
    return AddAtom(name);
}

int WINAPI GetClipboardFormatName(UINT fmt, LPSTR buf, int n)
{
    if (fmt < 0xC000)
        return 0;
    return GetAtomName((ATOM)fmt, buf, n);
}

HWND WINAPI SetClipboardViewer(HWND h)
{
    HWND old = hwndClipViewer;
    hwndClipViewer = h;
    /* UNVERIFIED: the new viewer is told at once */
    if (h && valid_wnd(h))
        send_message(h, WM_DRAWCLIPBOARD, 0, 0);
    return old;
}

HWND WINAPI GetClipboardViewer(void)
{
    return hwndClipViewer;
}

BOOL WINAPI ChangeClipboardChain(HWND remove, HWND next)
{
    HWND first = hwndClipViewer;
    if (remove == hwndClipViewer)
        hwndClipViewer = next;
    if (first && first != remove && valid_wnd(first))
        return (BOOL)send_message(first, WM_CHANGECBCHAIN, remove, MAKELONG(next, 0));
    return TRUE;
}

/* destroy_window: a window going away. The owner renders what it promised
   (WM_RENDERALLFORMATS) and stops owning; an opener's clipboard closes;
   a viewer at the head of the chain leaves it (UNVERIFIED: 3.1 leaves
   the chain to the application) */
void clip_window_destroyed(HWND h)
{
    if (h == hwndClipOwner) {
        WORD i;
        BOOL promised = FALSE;
        for (i = 0; i < clip_n; i++)
            if (!clip_fmts[i].h)
                promised = TRUE;
        if (promised) {
            clip_render_all = TRUE;
            send_message(h, WM_RENDERALLFORMATS, 0, 0);
            clip_render_all = FALSE;
        }
        /* what is still only promised can no longer be rendered */
        for (i = 0; i < clip_n; )
            if (!clip_fmts[i].h)
                clip_fmts[i] = clip_fmts[--clip_n];
            else
                i++;
        hwndClipOwner = 0;
    }
    if (clip_is_open && h == hwndClipOpen) {
        clip_is_open = FALSE;
        hwndClipOpen = 0;
        hqClipOpen = 0;
    }
    if (h == hwndClipViewer)
        hwndClipViewer = 0;
}
