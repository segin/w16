/*
 * ext.c - w16 File Manager: the extension interface (WINFILE.INI
 * [AddOns], FMExtensionProc, the FMEVENT_* events and the FM_* messages).
 *
 * The protocol is the 3.1 SDK's (Programmer's Reference vol. 1 ch. 16,
 * vol. 3 ch. 7: facts only); what the SDK leaves out was observed on 3.1's
 * File Manager with w16's own probe extension FMX.DLL (tests/apps/fmx.c,
 * obs WF15-WF19, scenario WF14; docs/notes/winfile-handover.md):
 *
 * - The events: FMEVENT_LOAD 100, FMEVENT_UNLOAD 101, FMEVENT_INITMENU 102,
 *   FMEVENT_USER_REFRESH 103, FMEVENT_SELCHANGE 104; nothing else (no
 *   help strings for the status bar in 3.1). The messages: FM_GETFOCUS
 *   WM_USER + 0x200, FM_GETDRIVEINFO + 0x201, FM_GETSELCOUNT + 0x202,
 *   FM_GETSELCOUNTLFN + 0x203, FM_GETFILESEL + 0x204, FM_GETFILESELLFN
 *   + 0x205, FM_REFRESH_WINDOWS + 0x206, FM_RELOAD_EXTENSIONS + 0x207
 *   (FMX's scan of WM_USER + 0x1F0 ... 0x21F: no others answer).
 *   MENU_TEXT_LEN is 40 (the delta at offset 46 of FMS_LOAD).
 * - Start-up: after the frame is made (not yet shown, no directory window
 *   yet), the [AddOns] keys in the file's order: LoadLibrary of the value
 *   (a missing file: nothing said, nothing counted), GetProcAddress
 *   "FMExtensionProc", FMEVENT_LOAD with the frame and an FMS_LOAD whose
 *   wMenuDelta is 600 for the first extension loaded, 700 for the second
 *   ... (100 more each; the other members are whatever was there). An
 *   answer of 0, a menu of 0 or a dwSize that is not the structure's:
 *   FreeLibrary, no FMEVENT_UNLOAD, the delta given to the next one. At
 *   most 5 extensions: after the fifth no more keys are read (the 3.1
 *   SDK: "up to five"; WF16, WF17).
 * - The menu: every item's ID (in submenus too) gets the delta added, in
 *   the extension's own menu, and loses its state (a grayed item shows
 *   enabled: WF14); the popup goes in the menu bar with szMenuName before
 *   Window, in load order.
 * - FMEVENT_INITMENU (lParam MAKELONG(delta, hMenu)) when the extension's
 *   popup opens (not for its submenus); a command: the item's own ID
 *   (1..99) as wMsg, lParam 0 - after File Manager's own commands (the
 *   fifth extension's item 3, ID 1003, is Window > Refresh) but before
 *   the Window menu's list of windows (its item 6, ID 1006, goes to it,
 *   not to the first window: WF19); F5 / Window > Refresh, also in
 *   Search Results: FMEVENT_USER_REFRESH to each, lParam 0; a selection
 *   change in the file list or in Search Results made by the user (not a
 *   new directory, not the tree): FMEVENT_SELCHANGE to each, lParam 0.
 * - Exit and FM_RELOAD_EXTENSIONS: to each in load order FMEVENT_UNLOAD
 *   with hwnd 0 and lParam 0, then FreeLibrary; on a reload the menus go
 *   and [AddOns] is read again.
 * - The queries: see ext_message.
 * - Files made or changed (KERNEL's FileCdr, main.c): a change in a
 *   directory no window shows makes FM_GETDRIVEINFO give FFFFFFFFh free
 *   bytes until the drive is read again (F5, a directory read: WF21); an
 *   automatic reread sends no event (WF21's log as 3.1's); Search Results
 *   asks to be updated (WF20; WF14, WF17 first runs).
 */
#include "winfile.h"

#define FMEVENT_LOAD            100
#define FMEVENT_UNLOAD          101
#define FMEVENT_INITMENU        102
#define FMEVENT_USER_REFRESH    103
#define FMEVENT_SELCHANGE       104

#define FMFOCUS_DIR             1
#define FMFOCUS_TREE            2
#define FMFOCUS_DRIVES          3
#define FMFOCUS_SEARCH          4

#define MENU_TEXT_LEN           40
#define EXT_MAX                 5       /* 3.1's: no more are loaded */
#define EXT_DELTA0              600     /* the first one's delta */
#define EXT_DELTASTEP           100

typedef struct {
    DWORD dwSize;
    char  szMenuName[MENU_TEXT_LEN];
    HMENU hMenu;
    WORD  wMenuDelta;
} FMS_LOAD, FAR *LPFMS_LOAD;

typedef struct {
    WORD  wTime, wDate;
    DWORD dwSize;
    BYTE  bAttr;
    char  szName[260];
} FMS_GETFILESEL, FAR *LPFMS_GETFILESEL;

typedef struct {
    DWORD dwTotalSpace, dwFreeSpace;
    char  szPath[260];
    char  szVolume[14];
    char  szShare[128];
} FMS_GETDRIVEINFO, FAR *LPFMS_GETDRIVEINFO;

typedef DWORD (FAR PASCAL *FMEXTPROC)(HWND, WORD, LONG);

typedef struct {
    HINSTANCE lib;
    FMEXTPROC proc;
    HMENU menu;
    WORD  delta;
} EXT;

static EXT ext[EXT_MAX];
static int g_next;                      /* extensions loaded */

/* every item of m and its submenus: the ID plus the delta */
static void renumber(HMENU m, WORD delta)
{
    int i, n = GetMenuItemCount(m);
    for (i = 0; i < n; i++) {
        HMENU sub = GetSubMenu(m, i);
        UINT st;
        char t[128];
        LPSTR text = t;
        HGLOBAL big = NULL;
        int size = sizeof t;
        if (sub) {
            renumber(sub, delta);
            continue;
        }
        st = GetMenuState(m, i, MF_BYPOSITION);
        if (st & MF_SEPARATOR)
            continue;
        /* the text whatever its length (a longer buffer until it fits) */
        while (GetMenuString(m, i, text, size, MF_BYPOSITION) >= size - 1 && size < 0x4000) {
            HGLOBAL nb = GlobalAlloc(GMEM_MOVEABLE, size * 2);
            if (!nb)
                break;
            if (big) {
                GlobalUnlock(big);
                GlobalFree(big);
            }
            big = nb;
            size *= 2;
            text = GlobalLock(big);
        }
        /* the item's state goes: 3.1 shows a grayed item enabled after
           this (WF14) */
        ModifyMenu(m, i, MF_BYPOSITION | MF_STRING, GetMenuItemID(m, i) + delta, text);
        if (big) {
            GlobalUnlock(big);
            GlobalFree(big);
        }
    }
}

/* the menu bar's position of the Window menu with n extensions' menus
   before it */
static int window_pos(void)
{
    HWND a = (HWND)LOWORD(SendMessage(g_mdi, WM_MDIGETACTIVE, 0, 0));
    /* a maximised child's system menu takes the first place */
    return MENU_WINDOW + g_next + (a && IsZoomed(a) ? 1 : 0);
}

static BOOL load_one(LPCSTR path, FMS_LOAD FAR *ld)
{
    HINSTANCE h;
    FMEXTPROC proc;
    EXT *e = &ext[g_next];
    UINT mode = SetErrorMode(SEM_NOOPENFILEERRORBOX);
    h = LoadLibrary(path);
    SetErrorMode(mode);
    if ((UINT)h < 32)
        return FALSE;
    proc = (FMEXTPROC)GetProcAddress(h, "FMExtensionProc");
    if (!proc) {
        FreeLibrary(h);
        return FALSE;
    }
    ld->wMenuDelta = EXT_DELTA0 + EXT_DELTASTEP * g_next;
    if (!proc(g_frame, FMEVENT_LOAD, (LONG)ld) || !ld->hMenu ||
        ld->dwSize != sizeof(FMS_LOAD)) {
        FreeLibrary(h);
        return FALSE;
    }
    e->lib = h;
    e->proc = proc;
    e->menu = ld->hMenu;
    e->delta = ld->wMenuDelta;
    renumber(e->menu, e->delta);
    InsertMenu(GetMenu(g_frame), window_pos(), MF_BYPOSITION | MF_POPUP,
               (UINT)e->menu, ld->szMenuName);
    g_next++;
    return TRUE;
}

/* [AddOns], in the file's order, until 5 are loaded */
void ext_load(void)
{
    static char keys[1024];
    char path[PATHLEN];
    FMS_LOAD ld;                        /* one for all, as 3.1's (WF16) */
    LPSTR k;
    HGLOBAL hk = NULL;
    LPSTR all = keys;
    int n = sizeof keys;
    /* the key list as long as it is (no bound of ours below the API's
       int) */
    for (;;) {
        HGLOBAL nk;
        int got = GetPrivateProfileString("AddOns", NULL, "", all, n, INI_FILE);
        if (got < n - 2 || n >= 0x4000)
            break;
        nk = GlobalAlloc(GMEM_MOVEABLE, n * 2);
        if (!nk)
            break;
        if (hk) {
            GlobalUnlock(hk);
            GlobalFree(hk);
        }
        hk = nk;
        n *= 2;
        all = GlobalLock(hk);
    }
    {
        LPBYTE b = (LPBYTE)&ld;
        int i;
        for (i = 0; i < (int)sizeof ld; i++)
            b[i] = 0;
    }
    for (k = all; *k && g_next < EXT_MAX; k += lstrlen(k) + 1) {
        GetPrivateProfileString("AddOns", k, "", path, sizeof path, INI_FILE);
        if (path[0])
            load_one(path, &ld);
    }
    if (hk) {
        GlobalUnlock(hk);
        GlobalFree(hk);
    }
}

/* to each, in load order: FMEVENT_UNLOAD (hwnd 0), FreeLibrary */
void ext_unload(BOOL menus)
{
    int i;
    for (i = 0; i < g_next; i++) {
        ext[i].proc(NULL, FMEVENT_UNLOAD, 0);
        if (menus) {
            HMENU bar = GetMenu(g_frame);
            int j, n = GetMenuItemCount(bar);
            for (j = 0; j < n; j++)
                if (GetSubMenu(bar, j) == ext[i].menu) {
                    RemoveMenu(bar, j, MF_BYPOSITION);
                    break;
                }
        }
        FreeLibrary(ext[i].lib);
    }
    g_next = 0;
}

void ext_reload(void)
{
    ext_unload(TRUE);
    ext_load();
    DrawMenuBar(g_frame);
}

static void notify(WORD msg)
{
    int i;
    for (i = 0; i < g_next; i++)
        ext[i].proc(g_frame, msg, 0);
}

void ext_selchange(void)
{
    notify(FMEVENT_SELCHANGE);
}

void ext_refresh(void)
{
    notify(FMEVENT_USER_REFRESH);
}

/* WM_INITMENUPOPUP: an extension's own popup */
void ext_initmenu(HMENU m)
{
    int i;
    for (i = 0; i < g_next; i++)
        if (ext[i].menu == m)
            ext[i].proc(g_frame, FMEVENT_INITMENU, MAKELONG(ext[i].delta, (WORD)m));
}

/* WM_COMMAND: an extension's item (its delta + 1 ... + 99) */
BOOL ext_command(WPARAM id)
{
    int i;
    for (i = 0; i < g_next; i++)
        if (id > ext[i].delta && id < ext[i].delta + EXT_DELTASTEP) {
            ext[i].proc(g_frame, (WORD)(id - ext[i].delta), 0);
            return TRUE;
        }
    return FALSE;
}

/* ---- the queries ---- */

static HWND active_child(void)
{
    return (HWND)LOWORD(SendMessage(g_mdi, WM_MDIGETACTIVE, 0, 0));
}

/* the selected files of the active window: the file list's (not "..")
   or Search Results'; i-th into f (the last one if fewer), returns how
   many there are */
static int selected(int want, LPFMS_GETFILESEL f)
{
    HWND a = active_child();
    LPDIRWIN d = dirwin_of(a);
    int n, i, k = 0;
    if (is_search(a))
        return search_sel(want, f ? f->szName : NULL, f ? (LPVOID)f : NULL);
    if (!d)
        return 0;
    n = (int)SendMessage(d->dirlb, LB_GETCOUNT, 0, 0);
    for (i = 0; i < n; i++) {
        LPDENTRY e;
        if (SendMessage(d->dirlb, LB_GETSEL, i, 0) <= 0)
            continue;
        e = dir_item(d, i);
        if (!e || e->pic == BM_PARENT)
            continue;
        if (f && k <= want) {
            f->wTime = e->time;
            f->wDate = e->date;
            f->dwSize = e->size;
            f->bAttr = e->attr;
            full_path(d, e->name, f->szName);
        }
        k++;
    }
    return k;
}

/* The FM_* messages, as 3.1 answers them (WF14, WF15, WF17, WF19), for
   the active MDI window:
   FM_GETFOCUS: 1 the file list, 2 the tree, 3 the drive bar, 4 Search
   Results. FM_GETSELCOUNT(LFN): the file list's selected entries but
   ".." (whichever pane has the focus), or Search Results'. FM_GETFILESEL
   (LFN): the wParam-th of them: time, date, size (a directory's as DOS
   gives it), attributes, the full path in capitals; past the end the
   last one, and the count returned instead of wParam; none selected:
   nothing written, 0. FM_GETDRIVEINFO: 1; total and free bytes, the
   window's directory (Search Results: the one it started from), the
   volume label, share "". FM_REFRESH_WINDOWS: wParam 0 the active
   window read again, else every one; no FMEVENT_USER_REFRESH (its answer
   means nothing). FM_RELOAD_EXTENSIONS: ext_reload, the menu bar laid
   out again (3.1's wraps then as it does not at start: WF14 S07). */
LRESULT ext_message(UINT msg, WPARAM wp, LPARAM lp)
{
    HWND a = active_child();
    LPDIRWIN d = dirwin_of(a);
    switch (msg) {
    case FM_GETFOCUS: {
        HWND f = GetFocus();
        if (is_search(a))
            return FMFOCUS_SEARCH;
        if (!d)
            return 0;
        if (f == d->treelb)
            return FMFOCUS_TREE;
        if (f == d->drives)
            return FMFOCUS_DRIVES;
        return FMFOCUS_DIR;
    }
    case FM_GETSELCOUNT:
    case FM_GETSELCOUNTLFN:
        return selected(-1, NULL);
    case FM_GETFILESEL:
    case FM_GETFILESELLFN: {
        int n = selected((int)wp, (LPFMS_GETFILESEL)lp);
        /* the index asked, but not past the count (WF15, WF17) */
        return (int)wp < n ? wp : n;
    }
    case FM_GETDRIVEINFO: {
        LPFMS_GETDRIVEINFO g = (LPFMS_GETDRIVEINFO)lp;
        DWORD fr, tot;
        int drive;
        LPCSTR path;
        if (is_search(a)) {
            path = search_start();
            drive = path[0] - 'A';
        } else if (d) {
            path = d->path;
            drive = d->drive;
        } else {
            return 1;
        }
        if (dos_freespace(drive + 1, &fr, &tot) != 0)
            fr = tot = 0;
        else if (g_free_unknown & (1UL << drive))
            fr = 0xFFFFFFFFUL;          /* a change seen through FileCdr in
                                           a directory no window shows,
                                           until the drive is read again
                                           (WF21; 3.1's cache) */
        g->dwTotalSpace = tot;
        g->dwFreeSpace = fr;
        s_cpy(g->szPath, path, sizeof g->szPath);
        volume_label(drive, g->szVolume, sizeof g->szVolume);
        g->szShare[0] = 0;
        return 1;
    }
    case FM_REFRESH_WINDOWS:
        if (wp) {
            HWND w;
            for (w = GetWindow(g_mdi, GW_CHILD); w; w = GetWindow(w, GW_HWNDNEXT))
                if (dirwin_of(w))
                    treewin_command(dirwin_of(w), IDM_REFRESH);
        } else if (d) {
            treewin_command(d, IDM_REFRESH);
        }
        return 0;
    case FM_RELOAD_EXTENSIONS:
        ext_reload();
        return 0;
    }
    return 0;
}
