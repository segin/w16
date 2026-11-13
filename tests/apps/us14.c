/*
 * us14.c - probe US14: where USER keeps menus, and what they cost in
 * "free system resources". Run as the shell on Windows 3.1
 * (tests/refrun.sh) and with w16's USER (DROPIN_FIXED=1 tests/userboot.sh -n
 * -t 120 -s: the probe runs about a minute).
 *
 * Pietrek (Windows Internals, ch. 1, LoadWindows) and Schulman
 * (Undocumented Windows, ch. 6, "USER Heaps") say 3.1's USER keeps menu
 * structures in a local heap of their own and menu item strings in a
 * second one, each in a GlobalAlloc'd segment, apart from USER's DGROUP.
 * Observed here through TOOLHELP (GlobalFirst/Next, LocalFirst/Next,
 * SystemHeapInfo) and GetFreeSystemResources (USER.284):
 *  - the local heaps in USER-owned segments, their sizes and free bytes
 *  - which heap gains a block for CreateMenu, for an item, for its text;
 *    the sizes of those blocks
 *  - how many menus, items and strings fit before USER refuses, and the
 *    percentages along the way
 * Heap sizes and counts depend on what ran before, so most values are
 * printed as OBS ~ and compared by hand. Block sizes as TOOLHELP gives
 * them differ under KRNL286 (us14ref286.h).
 *
 * Found on 3.1: GetFreeSystemResources' percentage of a heap is
 * GetHeapSpaces' free and total bytes, each in whole kilobytes, free * 100
 * / total; GFSR_USERRESOURCES is the least of USER's DGROUP, menu and
 * menu-string heaps, GFSR_SYSTEMRESOURCES the lower of USER's and GDI's,
 * other arguments give 0 (the probe counts the values that do not fit).
 */
#include <windows.h>
#include <toolhelp.h>

#define TK_REF "us14ref.h"
#define TK_REF286 "us14ref286.h"
#include "tklog.h"

extern WORD FAR PASCAL WaitEvent(HTASK);
extern BOOL FAR PASCAL InitApp(HINSTANCE);

typedef BOOL (FAR PASCAL *P1)(void FAR *);
typedef BOOL (FAR PASCAL *P2)(void FAR *, WORD);
typedef UINT (FAR PASCAL *PGFSR)(UINT);
typedef DWORD (FAR PASCAL *PGHS)(HMODULE);

static P1 sysheapinfo;
static P2 gfirst, gnext, lfirst;
static P1 lnext;
static PGFSR gfsr;
static PGHS getheapspaces;
static HMODULE huser;
static WORD userds, dgroup;

#define MAXH 8
typedef struct {
    HGLOBAL h;
    DWORD size;
    WORD freeb, nfree, usedb, nused;
    char name;          /* D: USER's DGROUP, M: menus, S: strings, ?: other */
} HEAP;
static HEAP heaps[MAXH], prev[MAXH];
static int nheaps, nprev;
static char name[64];

static void nm(const char *a, char c, const char *b)
{
    int i = 0;
    while (*a)
        name[i++] = *a++;
    if (c)
        name[i++] = c;
    while (*b)
        name[i++] = *b++;
    name[i] = 0;
}

static void scan(void)
{
    GLOBALENTRY ge;
    LOCALENTRY le;
    BOOL more, lm;
    int i, j;
    for (i = 0; i < nheaps; i++)
        prev[i] = heaps[i];
    nprev = nheaps;
    nheaps = 0;
    ge.dwSize = sizeof ge;
    for (more = gfirst(&ge, GLOBAL_ALL); more; more = gnext(&ge, GLOBAL_ALL)) {
        HEAP *hp;
        if (!ge.wHeapPresent || nheaps >= MAXH)
            continue;
        if (ge.hOwner != huser && (WORD)ge.hBlock != dgroup)
            continue;
        hp = &heaps[nheaps++];
        hp->h = ge.hBlock;
        hp->size = ge.dwBlockSize;
        hp->freeb = hp->nfree = hp->usedb = hp->nused = 0;
        hp->name = (WORD)ge.hBlock == dgroup ? 'D' : '?';
        for (j = 0; j < nprev; j++)
            if (prev[j].h == hp->h && prev[j].name != '?')
                hp->name = prev[j].name;
        le.dwSize = sizeof le;
        for (lm = lfirst(&le, (WORD)ge.hBlock); lm; lm = lnext(&le)) {
            if (le.wFlags & LF_FREE) {
                hp->freeb += le.wSize;
                hp->nfree++;
            } else {
                hp->usedb += le.wSize;
                hp->nused++;
            }
        }
    }
}

static int delta(int i);

/* the heap holding a live block with this handle: its index, -1 none.
   Heaps that grew since the last scan are looked in first: heaps made
   alike have blocks at the same offsets (w16's USER has more heaps than
   3.1's, several made the same way as the menu heaps) */
static int find_block(WORD handle, LOCALENTRY *out_le)
{
    LOCALENTRY le;
    BOOL more;
    int i, pass;
    for (pass = 0; pass < 2; pass++)
        for (i = 0; i < nheaps; i++) {
            if (!pass && delta(i) <= 0)
                continue;
            le.dwSize = sizeof le;
            for (more = lfirst(&le, (WORD)heaps[i].h); more; more = lnext(&le))
                if (!(le.wFlags & LF_FREE) && (WORD)le.hHandle == handle) {
                    if (out_le)
                        *out_le = le;
                    return i;
                }
        }
    return -1;
}

/* bytes in use in heap i now, less at the last scan */
static int delta(int i)
{
    int j;
    for (j = 0; j < nprev; j++)
        if (prev[j].h == heaps[i].h)
            return (int)heaps[i].usedb - (int)prev[j].usedb;
    return 0;
}

/* the heap whose bytes in use grew most since the last scan */
static int grew(void)
{
    int i, best = -1, bd = 0;
    for (i = 0; i < nheaps; i++)
        if (delta(i) > bd)
            bd = delta(i), best = i;
    return best;
}

static void report(const char *tag0)
{
    char tag[40];
    int i;
    lstrcpy(tag, tag0);
    nm(tag, 0, " GFSR(0)");
    obs_env(name, gfsr(0));
    nm(tag, 0, " GFSR(1)");
    obs_env(name, gfsr(1));
    nm(tag, 0, " GFSR(2)");
    obs_env(name, gfsr(2));
    if (getheapspaces) {
        DWORD s = getheapspaces(huser);
        nm(tag, 0, " GetHeapSpaces(USER) free");
        obs_env(name, LOWORD(s));
        nm(tag, 0, " GetHeapSpaces(USER) total");
        obs_env(name, HIWORD(s));
    }
    scan();
    for (i = 0; i < nheaps; i++) {
        nm(tag, heaps[i].name, " handle");
        obs_env(name, (WORD)heaps[i].h);
        nm(tag, heaps[i].name, " size/16");
        obs_env(name, (WORD)(heaps[i].size >> 4));
        nm(tag, heaps[i].name, " free");
        obs_env(name, heaps[i].freeb);
        nm(tag, heaps[i].name, " used");
        obs_env(name, heaps[i].usedb);
    }
}

/* the percentage of a heap as US14 found GetFreeSystemResources counts
   it on 3.1: GetHeapSpaces' free and total in whole kilobytes */
static WORD pct(WORD h)
{
    DWORD g = getheapspaces((HMODULE)h);
    WORD t = HIWORD(g) >> 10;
    return t ? (LOWORD(g) >> 10) * 100 / t : 0;   /* at most 63 * 100 */
}

static WORD misfit[3], fits, hgdi;

/* GetFreeSystemResources against that rule: USER the least of DGROUP's,
   the menu heap's and the string heap's (after a scan) */
static void model(void)
{
    WORD u = 0xFFFF, g = pct(hgdi), i, found = 0;
    for (i = 0; i < nheaps; i++)
        if (heaps[i].name == 'D' || heaps[i].name == 'M' || heaps[i].name == 'S') {
            WORD p = pct((WORD)heaps[i].h | 1);
            if (p < u)
                u = p;
            found++;
        }
    if (found != 3)
        return;         /* (TOOLHELP did not show the three heaps) */
    misfit[0] += gfsr(0) != (u < g ? u : g);
    misfit[1] += gfsr(1) != g;
    misfit[2] += gfsr(2) != u;
    fits++;
}

/* one line: n, GFSR(2), then size/16, free and used bytes of each heap */
static void line(const char *what, WORD n)
{
    int i;
    WORD p = gfsr(2);
    scan();
    out("OBS ~");
    out(what);
    out(" ");
    outhex(n);
    out(": GFSR(2)=");
    outhex(p);
    for (i = 0; i < nheaps; i++) {
        char c[3];
        c[0] = ' ', c[1] = heaps[i].name, c[2] = 0;
        out(c);
        out("=");
        outhex((WORD)(heaps[i].size >> 4));
        out("/");
        outhex(heaps[i].freeb);
        out("/");
        outhex(heaps[i].usedb);
        if (getheapspaces) {
            DWORD g = getheapspaces((HMODULE)((WORD)heaps[i].h | 1));
            out(" ghs=");
            outhex(LOWORD(g));
            out("/");
            outhex(HIWORD(g));
        }
    }
    out("\r\n");
    model();
}

static HMENU FAR *hm;
#define NMAX 20000

static void fill_menus(void)
{
    WORD n;
    for (n = 0; n < NMAX; n++) {
        hm[n] = CreateMenu();
        if (!hm[n])
            break;
        if (n % 16 == 15)
            line("menus", n + 1);
    }
    obs_env("CreateMenu until it fails: menus", n);
    report("menus full:");
    while (n)
        DestroyMenu(hm[--n]);
    report("menus freed:");
}

static char text[200];

static void fill_items(const char *tag, UINT flags, WORD textlen)
{
    HMENU m = CreatePopupMenu();
    WORD n, i;
    for (i = 0; i < textlen; i++)
        text[i] = (char)('a' + i % 26);
    text[textlen] = 0;
    for (n = 0; n < NMAX; n++) {
        text[0] = (char)('A' + n % 26);
        if (!AppendMenu(m, flags, n + 1, text))
            break;
        if (n % 16 == 15)
            line(tag, n + 1);
    }
    nm(tag, 0, ": items");
    obs_env(name, n);
    obs_env("  GetMenuItemCount", GetMenuItemCount(m));
    nm(tag, 0, " full:");
    report(name);
    DestroyMenu(m);
    nm(tag, 0, " freed:");
    report(name);
}

static void fill_windows(HINSTANCE hi)
{
    WNDCLASS wc;
    HWND top, w;
    WORD n;
    wc.style = 0;
    wc.lpfnWndProc = DefWindowProc;
    wc.cbClsExtra = 0;
    wc.cbWndExtra = 0;
    wc.hInstance = hi;
    wc.hIcon = 0;
    wc.hCursor = 0;
    wc.hbrBackground = 0;
    wc.lpszMenuName = NULL;
    wc.lpszClassName = "US14";
    RegisterClass(&wc);
    top = CreateWindow("US14", "US14", WS_OVERLAPPED, 0, 0, 100, 100, NULL, NULL, hi, NULL);
    for (n = 0; n < 4000; n++) {
        w = CreateWindow("US14", "", WS_CHILD, 0, 0, 10, 10, top, (HMENU)n, hi, NULL);
        if (!w)
            break;
        if (n % 16 == 15)
            line("windows", n + 1);
    }
    obs_env("CreateWindow until it fails: windows", n);
    report("windows full:");
    DestroyWindow(top);
    report("windows freed:");
}

int __cdecl tmain(void)
{
    HINSTANCE hi = (HINSTANCE)init_regs[5];
    HINSTANCE th;
    SYSHEAPINFO si;
    LOCALENTRY le;
    HMENU a, b;
    HGLOBAL g;
    int i;
    WaitEvent(0);
    InitApp(hi);
    out("US14 start\r\n");
    huser = GetModuleHandle("USER");
    hgdi = GetModuleHandle("GDI");
    gfsr = (PGFSR)GetProcAddress(huser, MAKEINTRESOURCE(284));
    getheapspaces = (PGHS)GetProcAddress(GetModuleHandle("KERNEL"), MAKEINTRESOURCE(138));
    th = LoadLibrary("TOOLHELP.DLL");
    obs("TOOLHELP loaded", th > HINSTANCE_ERROR);
    obs("GetFreeSystemResources found", gfsr != 0);
    if (th <= HINSTANCE_ERROR || !gfsr)
        return 1;
    sysheapinfo = (P1)GetProcAddress(th, "SystemHeapInfo");
    gfirst = (P2)GetProcAddress(th, "GlobalFirst");
    gnext = (P2)GetProcAddress(th, "GlobalNext");
    lfirst = (P2)GetProcAddress(th, "LocalFirst");
    lnext = (P1)GetProcAddress(th, "LocalNext");
    si.dwSize = sizeof si;
    obs("SystemHeapInfo", sysheapinfo(&si));
    userds = (WORD)si.hUserSegment;
    dgroup = LOWORD(GlobalHandle(userds));
    obs_env("GlobalHandle(hUserSegment)", dgroup);
    obs_env("SystemHeapInfo wUserFreePercent", si.wUserFreePercent);
    obs_env("SystemHeapInfo wGDIFreePercent", si.wGDIFreePercent);
    obs_env("SystemHeapInfo hUserSegment", (WORD)si.hUserSegment);
    obs_env("GetWindowWord(desktop, GWW_HINSTANCE)", GetWindowWord(GetDesktopWindow(), GWW_HINSTANCE));
    obs("SystemHeapInfo wUserFreePercent==GFSR(2)", si.wUserFreePercent == gfsr(2));
    obs("SystemHeapInfo wGDIFreePercent==GFSR(1)", si.wGDIFreePercent == gfsr(1));
    report("start:");
    /* the heaps of USER, other than DGROUP: how many */
    {
        int n = 0;
        for (i = 0; i < nheaps; i++)
            n += heaps[i].name != 'D';
        /* 3.1: three, a 100h-byte one besides the menu heaps (not
           known what it is; w16's USER does not have it) */
        obs_env("USER heaps besides DGROUP", n);
        n = 0;
        for (i = 0; i < nheaps; i++)
            n += heaps[i].name == 'D';
        obs("USER DGROUP heaps found", n);
    }

    /* one menu: which heap, which block */
    scan();
    a = CreateMenu();
    scan();
    i = find_block((WORD)a, &le);
    obs("CreateMenu ok", a != 0);
    obs_env("CreateMenu handle", (WORD)a);
    obs("CreateMenu: block in a USER heap", i >= 0);
    if (i >= 0) {
        if (heaps[i].name == '?')
            heaps[i].name = 'M';
        obs("CreateMenu: in DGROUP", heaps[i].name == 'D');
        obs("CreateMenu: block size", le.wSize);
        obs("CreateMenu: block flags", le.wFlags);
        obs("CreateMenu: handle==address", (WORD)le.hHandle == le.wAddress);
        obs_env("CreateMenu: heap segment size", (WORD)heaps[i].size);
    }
    i = grew();
    obs("CreateMenu: the heap that grew is the menu's", i >= 0 && heaps[i].name == 'M');
    b = CreatePopupMenu();
    scan();
    i = find_block((WORD)b, &le);
    obs("CreatePopupMenu: same heap", i >= 0 && heaps[i].name == 'M');
    obs("CreatePopupMenu: block size", i >= 0 ? le.wSize : 0);
    DestroyMenu(b);

    /* an item without text (a separator): the item array */
    scan();
    AppendMenu(a, MF_SEPARATOR, 0, NULL);
    scan();
    i = grew();
    obs("separator: grew the menu heap", i >= 0 && heaps[i].name == 'M');
    obs("separator: bytes", i >= 0 ? delta(i) : 0);
    AppendMenu(a, MF_SEPARATOR, 0, NULL);
    scan();
    i = grew();
    obs("2nd separator: grew the menu heap", i >= 0 && heaps[i].name == 'M');
    obs("2nd separator: bytes", i >= 0 ? delta(i) : 0);
    /* an item with text */
    AppendMenu(a, MF_STRING, 1, "0123456789");
    scan();
    {
        int s = -1, m = -1, d = -1;
        for (i = 0; i < nheaps; i++) {
            int dd = delta(i);
            if (heaps[i].name == 'M')
                m = dd;
            else if (heaps[i].name == 'D')
                d = dd;
            else if (dd > 0 && heaps[i].name == '?') {
                heaps[i].name = 'S';
                s = dd;
            } else if (heaps[i].name == 'S')
                s = dd;
        }
        obs("string item: menu heap bytes", m);
        obs("string item (10 chars): string heap bytes", s);
        obs("string item: DGROUP bytes", d);
    }
    AppendMenu(a, MF_STRING, 2, "0123456789012");
    scan();
    for (i = 0; i < nheaps; i++)
        if (heaps[i].name == 'S')
            obs("string item (13 chars): string heap bytes", delta(i));
    ModifyMenu(a, 3, MF_BYPOSITION | MF_STRING, 2, "0");
    scan();
    for (i = 0; i < nheaps; i++)
        if (heaps[i].name == 'S')
            obs("ModifyMenu to 1 char: string heap bytes", delta(i));
    DestroyMenu(a);
    scan();
    for (i = 0; i < nheaps; i++)
        if (heaps[i].name != '?') {
            nm("DestroyMenu: bytes back in ", heaps[i].name, "");
            obs(name, -delta(i));
        }
    report("named:");

    g = GlobalAlloc(GMEM_MOVEABLE, NMAX * sizeof(HMENU));
    hm = (HMENU FAR *)GlobalLock(g);
    fill_menus();
    fill_items("separators", MF_SEPARATOR, 0);
    fill_items("short items", MF_STRING, 4);
    fill_items("long items", MF_STRING, 120);
    GlobalUnlock(g);
    GlobalFree(g);
    report("end:");
    fill_windows(hi);
    obs("GetFreeSystemResources checked against the 1K rule", fits > 100);
    obs("GFSR(0) misfits", misfit[0]);
    obs("GFSR(1) misfits", misfit[1]);
    obs("GFSR(2) misfits", misfit[2]);
    obs("GFSR(3)==GFSR(0)", gfsr(3) == gfsr(0));
    obs_env("GFSR(3)", gfsr(3));
    obs_env("GFSR(FFFF)", gfsr(0xFFFF));
    out("US14 end\r\n");
    return 0;
}
