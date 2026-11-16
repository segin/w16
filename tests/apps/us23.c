/*
 * us23.c - probe US23: the system menu's cost in USER's menu heaps, and
 * the shrinking of USER's heaps. Run as the shell on Windows 3.1
 * (REFRUN_FILES=build/dos/US23A.EXE tests/refrun.sh) and with w16's USER
 * (DROPIN_FIXED=1 tests/userboot.sh -n -t 120 -f build/dos/US23A.EXE=US23A.EXE
 * -s build/dos/US23.EXE).
 *
 * Pietrek (Windows Internals): USER loads one system-menu template at
 * start-up (LoadWindows), and when a task ends normally with more than
 * 4096 bytes free in DGROUP it shrinks DGROUP to what is in use + 400h
 * and both menu heaps (LocalShrink(h, 0)). Observed here:
 *  - what windows with WS_SYSMENU and GetSystemMenu(h, FALSE/TRUE) add to
 *    the menu heaps, whether two windows share a handle, the items
 *  - the heaps' sizes after menus and windows are made and destroyed by
 *    this task (which goes on), and by US23A (which then ends)
 *  - LocalShrink on this program's own heap: what it returns and leaves
 * Heap sizes depend on the boot; most values are OBS ~ and compared by
 * hand, deltas are plain.
 */
#include <windows.h>
#include <toolhelp.h>

#define TK_REF "us23ref.h"
#define TK_REF286 "us23ref286.h"
#include "tklog.h"

extern WORD FAR PASCAL WaitEvent(HTASK);
extern BOOL FAR PASCAL InitApp(HINSTANCE);
typedef UINT (FAR PASCAL *P0)(void);
typedef UINT (FAR PASCAL *PU)(UINT);
static P0 LocalCountFree_, LocalHeapSize_;      /* KERNEL.161, 162 */
static PU gfsr;                                 /* USER.284 */
#define LocalCountFree() LocalCountFree_()
#define LocalHeapSize() LocalHeapSize_()
#define GetFreeSystemResources(n) gfsr(n)

typedef BOOL (FAR PASCAL *P1)(void FAR *);
typedef BOOL (FAR PASCAL *P2)(void FAR *, WORD);
static P2 gfirst, gnext, lfirst;
static P1 lnext, sysheapinfo;
static HMODULE huser;
static WORD dgroup, mheap, sheap;      /* global handles of USER's heaps */
static char name[80];

static void nm(const char *a, const char *b)
{
    lstrcpy(name, a);
    lstrcat(name, b);
}

/* a heap's block size and bytes in use (TOOLHELP) */
static DWORD heap_size(WORD h)
{
    GLOBALENTRY ge;
    BOOL more;
    ge.dwSize = sizeof ge;
    for (more = gfirst(&ge, GLOBAL_ALL); more; more = gnext(&ge, GLOBAL_ALL))
        if ((WORD)ge.hBlock == h)
            return ge.dwBlockSize;
    return 0;
}

static WORD heap_used(WORD h)
{
    LOCALENTRY le;
    BOOL more;
    WORD n = 0;
    le.dwSize = sizeof le;
    for (more = lfirst(&le, h); more; more = lnext(&le))
        if (!(le.wFlags & LF_FREE))
            n += le.wSize;
    return n;
}

/* the USER-owned heaps other than DGROUP; the menu heap is the one a
   CreateMenu grows, the string heap the one a text item grows */
static void find_heaps(void)
{
    GLOBALENTRY ge;
    BOOL more;
    WORD h[4], u[4], n = 0, i;
    HMENU m;
    ge.dwSize = sizeof ge;
    for (more = gfirst(&ge, GLOBAL_ALL); more; more = gnext(&ge, GLOBAL_ALL))
        if (ge.wHeapPresent && ge.hOwner == huser && (WORD)ge.hBlock != dgroup && n < 4)
            h[n++] = (WORD)ge.hBlock;
    for (i = 0; i < n; i++)
        u[i] = heap_used(h[i]);
    m = CreatePopupMenu();
    for (i = 0; i < n; i++)
        if (heap_used(h[i]) > u[i])
            mheap = h[i], u[i] = heap_used(h[i]);
    AppendMenu(m, MF_STRING, 1, "0123456789");
    for (i = 0; i < n; i++)
        if (h[i] != mheap && heap_used(h[i]) > u[i])
            sheap = h[i];
    DestroyMenu(m);
    obs("menu heap found", mheap != 0);
    obs("string heap found", sheap != 0);
}

static WORD um, us, ud;

static void mark(void)
{
    um = heap_used(mheap);
    us = heap_used(sheap);
    ud = heap_used(dgroup);
}

static void deltas(const char *what)
{
    nm(what, ": menu heap bytes");
    obs(name, heap_used(mheap) - um);
    nm(what, ": string heap bytes");
    obs(name, heap_used(sheap) - us);
    mark();
}

static void sizes(const char *what)
{
    nm(what, ": DGROUP size/16");
    obs_env(name, (WORD)(heap_size(dgroup) >> 4));
    nm(what, ": menu heap size/16");
    obs_env(name, (WORD)(heap_size(mheap) >> 4));
    nm(what, ": string heap size/16");
    obs_env(name, (WORD)(heap_size(sheap) >> 4));
}

static DWORD sz0[3];

static void sizes_were(void)
{
    sz0[0] = heap_size(dgroup);
    sz0[1] = heap_size(mheap);
    sz0[2] = heap_size(sheap);
}

/* plain: did each heap get smaller, larger or stay since sizes_were */
static void changed(const char *what)
{
    static const char *const hn[3] = { ": DGROUP ", ": menu heap ", ": string heap " };
    DWORD now[3];
    int i;
    now[0] = heap_size(dgroup);
    now[1] = heap_size(mheap);
    now[2] = heap_size(sheap);
    for (i = 0; i < 3; i++) {
        nm(what, hn[i]);
        lstrcat(name, "grew(1)/shrank(2)");
        obs(name, now[i] > sz0[i] ? 1 : now[i] < sz0[i] ? 2 : 0);
    }
    sizes(what);
}

/* the highest blocks in use in a heap (what keeps it from shrinking) */
static void top_blocks(const char *what, WORD h)
{
    LOCALENTRY le;
    BOOL more;
    WORD a[3] = { 0, 0, 0 }, z[3] = { 0, 0, 0 }, f[3] = { 0, 0, 0 }, i;
    le.dwSize = sizeof le;
    for (more = lfirst(&le, h); more; more = lnext(&le))
        if (!(le.wFlags & LF_FREE)) {
            a[0] = a[1], z[0] = z[1], f[0] = f[1];
            a[1] = a[2], z[1] = z[2], f[1] = f[2];
            a[2] = le.wAddress, z[2] = le.wSize, f[2] = le.wFlags;
        }
    for (i = 0; i < 3; i++) {
        wsprintf(name, "%s: block %d from the top at/size/flags", (LPSTR)what, 2 - i);
        out("OBS ~");
        out(name);
        out("=");
        outhex(a[i]);
        out("/");
        outhex(z[i]);
        out("/");
        outhex(f[i]);
        out("\r\n");
    }
}

static void items(const char *what, HMENU m)
{
    int i, n = GetMenuItemCount(m);
    char buf[64];
    nm(what, ": items");
    obs(name, n);
    for (i = 0; i < n; i++) {
        wsprintf(buf, "%s item %d", (LPSTR)what, i);
        lstrcpy(name, buf);
        GetMenuString(m, i, buf, sizeof buf, MF_BYPOSITION);
        obs_str(name, buf);
        wsprintf(name, "%s item %d id", (LPSTR)what, i);
        obs(name, GetMenuItemID(m, i));
    }
}

static void run_helper(const char *cmd)
{
    MSG msg;
    DWORD t;
    UINT r = WinExec(cmd, SW_SHOWMINNOACTIVE);
    obs("WinExec(US23A) ok", r > 32);
    t = GetTickCount();
    while (GetModuleHandle("US23A") && GetTickCount() - t < 60000UL)
        if (PeekMessage(&msg, 0, 0, 0, PM_REMOVE))
            DispatchMessage(&msg);
        else
            Yield();
    obs("US23A ended", GetModuleHandle("US23A") == 0);
}

static HMENU hm[1500];
static HWND hw[400];

static void own_shrink(void)
{
    HLOCAL a, b, x;
    WORD ds = (WORD)(DWORD)GlobalHandle(SELECTOROF((void FAR *)&a));
    DWORD s0 = GlobalSize(ds), s1;
    UINT r;
    obs_env("own heap: GlobalSize", (WORD)s0);
    obs_env("own heap: LocalHeapSize", LocalHeapSize());
    obs_env("own heap: LocalCountFree", LocalCountFree());
    r = LocalShrink(0, 0);
    obs_env("own heap, nothing to give back: LocalShrink(0, 0)", r);
    obs("own heap, nothing to give back: GlobalSize the same", GlobalSize(ds) == s0);
    /* grown by two 16K blocks, both freed */
    a = LocalAlloc(LMEM_FIXED, 0x4000);
    b = LocalAlloc(LMEM_FIXED, 0x4000);
    obs("own heap: grown", a && b && GlobalSize(ds) > s0);
    s1 = GlobalSize(ds);
    obs_env("own heap grown: GlobalSize", (WORD)s1);
    LocalFree(a);
    LocalFree(b);
    obs("own heap, freed: GlobalSize the same", GlobalSize(ds) == s1);
    obs_env("own heap freed: LocalHeapSize", LocalHeapSize());
    obs_env("own heap freed: LocalCountFree", LocalCountFree());
    r = LocalShrink(0, 0x2000);
    obs_env("own heap: LocalShrink(0, 2000h)", r);
    obs_env("own heap: GlobalSize after LocalShrink(0, 2000h)", (WORD)GlobalSize(ds));
    obs_env("own heap: LocalHeapSize after", LocalHeapSize());
    r = LocalShrink(0, 0);
    obs_env("own heap: LocalShrink(0, 0)", r);
    obs_env("own heap: GlobalSize after LocalShrink(0, 0)", (WORD)GlobalSize(ds));
    obs_env("own heap: LocalHeapSize after", LocalHeapSize());
    obs("own heap: back to its first size", GlobalSize(ds) == s0);
    /* a fixed block after the freed space */
    a = LocalAlloc(LMEM_FIXED, 0x4000);
    x = LocalAlloc(LMEM_FIXED, 0x10);
    LocalFree(a);
    r = LocalShrink(0, 0);
    obs_env("own heap, fixed block at the end: LocalShrink(0, 0)", r);
    obs_env("own heap, fixed block at the end: GlobalSize", (WORD)GlobalSize(ds));
    LocalFree(x);
    LocalShrink(0, 0);
    /* a moveable block after the freed space */
    a = LocalAlloc(LMEM_FIXED, 0x4000);
    x = LocalAlloc(LMEM_MOVEABLE, 0x10);
    LocalFree(a);
    r = LocalShrink(0, 0);
    obs_env("own heap, moveable block at the end: LocalShrink(0, 0)", r);
    obs_env("own heap, moveable block at the end: GlobalSize", (WORD)GlobalSize(ds));
    obs("own heap, moveable block at the end: back to its first size", GlobalSize(ds) == s0);
    LocalFree(x);
}

int __cdecl tmain(void)
{
    HINSTANCE hi = (HINSTANCE)init_regs[5];
    HINSTANCE th;
    SYSHEAPINFO si;
    WNDCLASS wc;
    HWND h1, h2, c1;
    HMENU s1, s2, sc;
    int i;
    WaitEvent(0);
    InitApp(hi);
    out("US23 start\r\n");
    huser = GetModuleHandle("USER");
    gfsr = (PU)GetProcAddress(huser, MAKEINTRESOURCE(284));
    LocalCountFree_ = (P0)GetProcAddress(GetModuleHandle("KERNEL"), MAKEINTRESOURCE(161));
    LocalHeapSize_ = (P0)GetProcAddress(GetModuleHandle("KERNEL"), MAKEINTRESOURCE(162));
    th = LoadLibrary("TOOLHELP.DLL");
    obs("TOOLHELP loaded", th > HINSTANCE_ERROR);
    if (th <= HINSTANCE_ERROR)
        return 1;
    sysheapinfo = (P1)GetProcAddress(th, "SystemHeapInfo");
    gfirst = (P2)GetProcAddress(th, "GlobalFirst");
    gnext = (P2)GetProcAddress(th, "GlobalNext");
    lfirst = (P2)GetProcAddress(th, "LocalFirst");
    lnext = (P1)GetProcAddress(th, "LocalNext");
    si.dwSize = sizeof si;
    sysheapinfo(&si);
    dgroup = LOWORD(GlobalHandle((WORD)si.hUserSegment));
    find_heaps();
    sizes("start");

    /* ---- the system menu ---- */
    wc.style = 0;
    wc.lpfnWndProc = DefWindowProc;
    wc.cbClsExtra = wc.cbWndExtra = 0;
    wc.hInstance = hi;
    wc.hIcon = 0;
    wc.hCursor = 0;
    wc.hbrBackground = (HBRUSH)(COLOR_WINDOW + 1);
    wc.lpszMenuName = NULL;
    wc.lpszClassName = "US23";
    RegisterClass(&wc);
    mark();
    h1 = CreateWindow("US23", "1", WS_OVERLAPPEDWINDOW, 0, 0, 200, 150, NULL, NULL, hi, NULL);
    deltas("CreateWindow(WS_SYSMENU)");
    s1 = GetSystemMenu(h1, FALSE);
    deltas("GetSystemMenu(1, FALSE)");
    obs("GetSystemMenu(1, FALSE) a menu", IsMenu(s1));
    obs("GetSystemMenu(1, FALSE) again: same", GetSystemMenu(h1, FALSE) == s1);
    deltas("GetSystemMenu(1, FALSE) again");
    items("system menu", s1);
    h2 = CreateWindow("US23", "2", WS_OVERLAPPEDWINDOW, 20, 20, 200, 150, NULL, NULL, hi, NULL);
    mark();
    s2 = GetSystemMenu(h2, FALSE);
    deltas("GetSystemMenu(2, FALSE)");
    obs("GetSystemMenu(2, FALSE) differs from 1's", s2 != s1);
    obs("GetSystemMenu(1, TRUE)", (WORD)GetSystemMenu(h1, TRUE));
    deltas("GetSystemMenu(1, TRUE)");
    obs("GetSystemMenu(1, TRUE): the copy is gone", !IsMenu(s1));
    s1 = GetSystemMenu(h1, FALSE);
    deltas("GetSystemMenu(1, FALSE) after TRUE");
    c1 = CreateWindow("US23", "c", WS_CHILD | WS_CAPTION | WS_SYSMENU | WS_VISIBLE, 0, 0, 100, 80, h1, (HMENU)5, hi, NULL);
    mark();
    sc = GetSystemMenu(c1, FALSE);
    deltas("GetSystemMenu(child, FALSE)");
    items("child's system menu", sc);
    DestroyWindow(h2);
    deltas("DestroyWindow(2)");
    DestroyWindow(h1);
    deltas("DestroyWindow(1 and its child)");

    /* the same nine items from a menu template (LoadMenuIndirect), and
       with AppendMenu, for what the copies cost */
    {
        static const char *const t[9] = { "&Restore", "&Move", "&Size", "Mi&nimize",
            "Ma&ximize", "", "&Close\tAlt+F4", "", "S&witch To...\tCtrl+Esc" };
        static const WORD ids[9] = { SC_RESTORE, SC_MOVE, SC_SIZE, SC_MINIMIZE,
            SC_MAXIMIZE, 0, SC_CLOSE, 0, SC_TASKLIST };
        static BYTE tpl[256];
        BYTE *p = tpl + 4;
        HMENU m;
        for (i = 0; i < 9; i++) {
            *(WORD *)p = (WORD)(i == 8 ? MF_END : 0);
            *(WORD *)(p + 2) = ids[i];
            lstrcpy((LPSTR)(p + 4), t[i]);
            p += 4 + lstrlen(t[i]) + 1;
        }
        mark();
        m = LoadMenuIndirect(tpl);
        deltas("LoadMenuIndirect(9 items)");
        DestroyMenu(m);
        mark();
        m = CreatePopupMenu();
        for (i = 0; i < 9; i++)
            if (*t[i])
                AppendMenu(m, MF_STRING, ids[i], t[i]);
            else
                AppendMenu(m, MF_SEPARATOR, 0, NULL);
        deltas("CreatePopupMenu + 9 AppendMenu");
        DestroyMenu(m);
        mark();
    }

    /* ---- shrinking ---- */
    sizes_were();
    for (i = 0; i < 1500; i++) {
        hm[i] = CreatePopupMenu();
        AppendMenu(hm[i], MF_STRING, 1, "0123456789012345678901234567890123456789");
    }
    changed("1500 menus made");
    for (i = 0; i < 1500; i++)
        DestroyMenu(hm[i]);
    h1 = CreateWindow("US23", "", WS_OVERLAPPED, 0, 0, 50, 50, NULL, NULL, hi, NULL);
    for (i = 0; i < 400; i++)
        hw[i] = CreateWindow("US23", "", WS_CHILD, 0, 0, 5, 5, h1, (HMENU)i, hi, NULL);
    changed("1500 menus destroyed, 400 windows made");
    DestroyWindow(h1);
    changed("all destroyed (this task goes on)");
    sizes_were();
    run_helper("C:\\US23A.EXE 0");
    changed("US23A that does nothing ended");
    top_blocks("DGROUP after US23A ended", dgroup);
    sizes_were();
    run_helper("C:\\US23A.EXE 1");
    changed("US23A that made and destroyed menus and windows ended");
    obs("GetFreeSystemResources(2) after", GetFreeSystemResources(2) >= 80);
    obs_env("GetFreeSystemResources(2)", GetFreeSystemResources(2));
    {
        /* LocalShrink on the menu heap from here, the way USER calls it */
        WORD sel = HIWORD(GlobalHandle(mheap));
        HMENU m;
        for (i = 0; i < 1500; i++)
            hm[i] = CreatePopupMenu();
        for (i = 0; i < 1500; i++)
            DestroyMenu(hm[i]);
        m = CreatePopupMenu();          /* (something in use, at a low address) */
        sizes_were();
        obs_env("menu heap: LocalShrink(sel, 0)", LocalShrink((HLOCAL)sel, 0));
        changed("menu heap after LocalShrink(sel, 0)");
        DestroyMenu(m);
    }
    own_shrink();
    out("US23 end\r\n");
    return 0;
}
