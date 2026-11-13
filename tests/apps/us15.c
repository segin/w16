/*
 * us15.c - probe US15: what each USER object costs, in which of USER's
 * heaps, and how many fit. Run as the shell on Windows 3.1
 * (tests/refrun.sh; REFRUN_KRNL286=1 for KRNL286) and with w16's USER
 * (DROPIN_FIXED=1 tests/userboot.sh -n -t 150 -s build/dos/US15.EXE).
 *
 * Observation only, through TOOLHELP (GlobalFirst/Next for the USER-owned
 * blocks with a local heap, LocalFirst/Next for their blocks: sizes,
 * flags, types), GetHeapSpaces (KERNEL.138), GetFreeSystemResources
 * (USER.284) and reading USER's data segment the way Spy-type tools do
 * (Schulman, Undocumented Windows, ch. 6, WND and CLASS: the layouts
 * tools rely on; checked here, not assumed).
 *
 *  - "cost" lines: for one object (a window, its text, scroll bars, a
 *    class and its name, extra bytes and menu name, a property, a hook, a
 *    timer, a CS_OWNDC/CS_CLASSDC DC, the DC cache, a registered message,
 *    a global atom, an edit control's and a list box's text), the bytes
 *    and blocks each heap gained: OBS ~cost <what>: <heap>=+bytes/+blocks
 *    with D USER's DGROUP, M the menu heap, S the menu-string heap, G the
 *    heap GlobalAddAtom grows, a/b/c other heaps of USER's.
 *  - "limit" lines: how many windows (plain, with 120-character titles),
 *    classes, properties (integer and string names), hooks and timers fit
 *    before USER refuses (each stops at a cap), with GFSR(2) and each
 *    heap's size/free/used every 32 objects (the resource curve).
 *  - the WND and CLASS fields a tool reads, against the documented calls.
 * Heap sizes and counts depend on what ran before, so costs and limits
 * are OBS ~ and compared by hand (ARCHITECTURE.md 10.4, US15).
 */
#include <windows.h>
#include <toolhelp.h>

#define TK_REF "us15ref.h"
#define TK_REF286 "us15ref286.h"
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
static HINSTANCE hi;

#define MAXH 10
typedef struct {
    HGLOBAL h;
    DWORD size;
    WORD freeb, nfree, usedb, nused;
    char name;
} HEAP;
static HEAP heaps[MAXH], prev[MAXH];
static int nheaps, nprev;
static char nextname = 'a';
static char name[80];

static void nm(const char *a, const char *b)
{
    int i = 0;
    while (*a && i < 70)
        name[i++] = *a++;
    while (*b && i < 78)
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

static int prev_of(int i)
{
    int j;
    for (j = 0; j < nprev; j++)
        if (prev[j].h == heaps[i].h)
            return j;
    return -1;
}

static int delta(int i)
{
    int j = prev_of(i);
    return j < 0 ? (int)heaps[i].usedb : (int)heaps[i].usedb - (int)prev[j].usedb;
}

static int dblocks(int i)
{
    int j = prev_of(i);
    return j < 0 ? (int)heaps[i].nused : (int)heaps[i].nused - (int)prev[j].nused;
}

/* the heap that grew most since the last scan, named c if not named yet */
static int grew_name(char c)
{
    int i, best = -1, bd = 0;
    for (i = 0; i < nheaps; i++)
        if (delta(i) > bd)
            bd = delta(i), best = i;
    if (best >= 0 && heaps[best].name == '?')
        heaps[best].name = c;
    return best;
}

static WORD dg_free(void)
{
    return getheapspaces ? LOWORD(getheapspaces(huser)) : 0;
}

static WORD dgfree0;

/* start a measurement */
static void begin(void)
{
    scan();
    dgfree0 = dg_free();
}

static void outsigned(int v)
{
    out(v < 0 ? "-" : "+");
    outhex((WORD)(v < 0 ? -v : v));
}

/* one cost line: every heap whose bytes or blocks in use changed; plus
   DGROUP's change as GetHeapSpaces sees it (works without TOOLHELP) */
static void cost(const char *what)
{
    int i;
    WORD f;
    scan();
    f = dg_free();
    for (i = 0; i < nheaps; i++)
        if (heaps[i].name == '?' && (delta(i) || dblocks(i)))
            heaps[i].name = nextname++;
    out("OBS ~cost ");
    out(what);
    out(":");
    for (i = 0; i < nheaps; i++) {
        char c[4];
        if (!delta(i) && !dblocks(i))
            continue;
        c[0] = ' ', c[1] = heaps[i].name, c[2] = '=', c[3] = 0;
        out(c);
        outsigned(delta(i));
        out("/");
        outsigned(dblocks(i));
    }
    out(" ghsD=");
    outsigned((int)(dgfree0 - f));
    out("\r\n");
}

/* the DGROUP bytes of the last cost line (TOOLHELP), for checks */
static int dcost(void)
{
    int i;
    for (i = 0; i < nheaps; i++)
        if (heaps[i].name == 'D')
            return delta(i);
    return 0;
}

/* a block in one of USER's heaps with this handle: the heap's index */
static int find_block(WORD handle, LOCALENTRY *out_le)
{
    LOCALENTRY le;
    BOOL more;
    int i;
    for (i = 0; i < nheaps; i++) {
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

/* the blocks of each heap by TOOLHELP type: count and bytes */
static void types(const char *tag)
{
    LOCALENTRY le;
    BOOL more;
    int i, t;
    WORD cnt[34], byt[34];
    for (i = 0; i < nheaps; i++) {
        char c[2];
        for (t = 0; t < 34; t++)
            cnt[t] = byt[t] = 0;
        le.dwSize = sizeof le;
        for (more = lfirst(&le, (WORD)heaps[i].h); more; more = lnext(&le)) {
            if (le.wFlags & LF_FREE)
                continue;
            t = le.wType < 33 ? le.wType : 33;
            cnt[t]++;
            byt[t] += le.wSize;
        }
        c[0] = heaps[i].name, c[1] = 0;
        out("OBS ~");
        out(tag);
        out(" heap ");
        out(c);
        out(" types:");
        for (t = 0; t < 34; t++)
            if (cnt[t]) {
                out(" ");
                outhex(t);
                out("=");
                outhex(cnt[t]);
                out("/");
                outhex(byt[t]);
            }
        out("\r\n");
    }
}

static void report(const char *tag)
{
    int i;
    out("OBS ~");
    out(tag);
    out(": GFSR(2)=");
    outhex(gfsr(2));
    out(" GFSR(1)=");
    outhex(gfsr(1));
    if (getheapspaces) {
        DWORD s = getheapspaces(huser);
        out(" ghsUSER=");
        outhex(LOWORD(s));
        out("/");
        outhex(HIWORD(s));
    }
    scan();
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
        out("#");
        outhex(heaps[i].nused);
    }
    out("\r\n");
}

/* one curve point: n, GFSR(2), each heap's size/16, free, used and
   GetHeapSpaces free/total */
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
    if (!nheaps && getheapspaces) {
        DWORD g = getheapspaces(huser);
        out(" ghsUSER=");
        outhex(LOWORD(g));
        out("/");
        outhex(HIWORD(g));
    }
    out("\r\n");
}

/* ---- reading USER's data segment (Schulman ch. 6) ---- */
static WORD uw(WORD off)
{
    return *(WORD FAR *)MAKELP(userds, off);
}

static DWORD ud(WORD off)
{
    return *(DWORD FAR *)MAKELP(userds, off);
}

static char cname[16];
static void numname(const char *pfx, WORD n)
{
    int i = 0, k;
    while (*pfx)
        cname[i++] = *pfx++;
    for (k = 12; k >= 0; k -= 4)
        cname[i++] = "0123456789ABCDEF"[(n >> k) & 15];
    cname[i] = 0;
}

static ATOM reg(const char *cls, UINT style, int clsextra, int wndextra, LPCSTR menu)
{
    WNDCLASS wc;
    wc.style = style;
    wc.lpfnWndProc = DefWindowProc;
    wc.cbClsExtra = clsextra;
    wc.cbWndExtra = wndextra;
    wc.hInstance = hi;
    wc.hIcon = 0;
    wc.hCursor = 0;
    wc.hbrBackground = 0;
    wc.lpszMenuName = menu;
    wc.lpszClassName = cls;
    return RegisterClass(&wc);
}

static HWND child(HWND top, const char *cls, LPCSTR text, DWORD style, WORD id)
{
    return CreateWindow(cls, text, WS_CHILD | style, 0, 0, 10, 10, top, (HMENU)id, hi, NULL);
}

BOOL FAR PASCAL __export __loadds hookproc(int code, WPARAM wp, LPARAM lp)
{
    (void)code, (void)wp, (void)lp;
    return 0;
}

static char longtext[130];

/* ---- the WND and CLASS fields ---- */
static void layout(HWND top)
{
    HWND w;
    WORD cls, ok;
    WNDCLASS wc;
    reg("US15L", CS_DBLCLKS, 6, 4, NULL);
    w = CreateWindow("US15L", "layout", WS_CHILD | WS_VISIBLE, 3, 4, 50, 60, top, (HMENU)0x1234, hi, NULL);
    obs("layout: window made", w != 0);
    if (!w)
        return;
    SetWindowWord(w, 0, 0xBEEF);
    SetClassWord(w, 2, 0xCAFE);
    obs("WND+00 hwndNext == GW_HWNDNEXT", uw((WORD)w + 0x00) == (WORD)GetWindow(w, GW_HWNDNEXT));
    obs("WND+02 hwndChild == GW_CHILD", uw((WORD)w + 0x02) == (WORD)GetWindow(w, GW_CHILD));
    obs("WND+04 hwndParent", uw((WORD)w + 0x04) == (WORD)top);
    obs("WND+06 hwndOwner (child)", uw((WORD)w + 0x06));
    {
        RECT r;
        GetWindowRect(w, &r);
        obs("WND+08 rcWindow", uw((WORD)w + 0x08) == (WORD)r.left && uw((WORD)w + 0x0A) == (WORD)r.top &&
            uw((WORD)w + 0x0C) == (WORD)r.right && uw((WORD)w + 0x0E) == (WORD)r.bottom);
        GetClientRect(w, &r);
        ClientToScreen(w, (POINT FAR *)&r.left);
        obs("WND+10 rcClient", uw((WORD)w + 0x10) == (WORD)r.left && uw((WORD)w + 0x12) == (WORD)r.top);
    }
    obs("WND+18 hmemTaskQ == GetTaskQueue", uw((WORD)w + 0x18) != 0);
    cls = uw((WORD)w + 0x1C);
    obs("WND+1E hInstance", uw((WORD)w + 0x1E) == (WORD)hi);
    obs("WND+20 lpfnWndProc", ud((WORD)w + 0x20) == (DWORD)GetWindowLong(w, GWL_WNDPROC));
    obs("WND+28 style", ud((WORD)w + 0x28) == (DWORD)GetWindowLong(w, GWL_STYLE));
    obs("WND+2C exstyle", ud((WORD)w + 0x2C) == (DWORD)GetWindowLong(w, GWL_EXSTYLE));
    obs("WND+30 id", uw((WORD)w + 0x30) == 0x1234);
    obs("WND+3C extra bytes", uw((WORD)w + 0x3C) == 0xBEEF);
    obs_env("WND+24 dwFlags lo", uw((WORD)w + 0x24));
    obs_env("WND+24 dwFlags hi", uw((WORD)w + 0x26));
    obs("WND+32 text block (with a title)", uw((WORD)w + 0x32) != 0);
    {
        LOCALENTRY le;
        int i;
        scan();
        i = find_block((WORD)w, &le);
        obs("WND block in DGROUP", i >= 0 && heaps[i].name == 'D');
        if (i >= 0) {
            obs("WND block size (extra 4)", le.wSize);
            obs("WND block flags", le.wFlags);
            obs("WND block type", le.wType);
            obs("WND handle==address", (WORD)le.hHandle == le.wAddress);
        }
        {
            WORD t = uw((WORD)w + 0x32);
            i = t ? find_block(t, &le) : -1;
            if (i < 0 && t) {
                /* a moveable block: the handle's entry points to it */
                obs("WND+32 text: a handle", 1);
            }
            obs("WND+32 text block in DGROUP", i >= 0 && heaps[i].name == 'D');
            if (i >= 0) {
                obs("WND+32 text block size (6 chars)", le.wSize);
                obs("WND+32 text block type", le.wType);
                obs("WND+32 text block flags", le.wFlags);
                obs_str("WND+32 text", (LPCSTR)MAKELP(userds, le.wAddress));
            }
        }
        obs("WND+1C class: a DGROUP block", (i = find_block(cls, &le)) >= 0 && heaps[i].name == 'D');
        if (i >= 0) {
            obs("CLASS block size (cls extra 6)", le.wSize);
            obs("CLASS block type", le.wType);
            obs("CLASS block flags", le.wFlags);
        }
    }
    ok = GetClassInfo(hi, "US15L", &wc);
    obs("CLASS+02 wSig", uw(cls + 0x02));
    obs("CLASS+04 atomCls == GetClassInfo's", uw(cls + 0x04) == ok);
    obs("CLASS+04 atomCls == GCW_ATOM", uw(cls + 0x04) == GetClassWord(w, -32));
    obs("CLASS+06 hDCE", uw(cls + 0x06));
    obs("CLASS+08 cClsWnds", uw(cls + 0x08));
    obs("CLASS+0A style", uw(cls + 0x0A));
    obs("CLASS+0C lpfnWndProc", ud(cls + 0x0C) == (DWORD)DefWindowProc);
    obs("CLASS+10 cbClsExtra", uw(cls + 0x10));
    obs("CLASS+12 cbWndExtra", uw(cls + 0x12));
    obs("CLASS+14 hInstance == hInstance", uw(cls + 0x14) == (WORD)hi);
    obs("CLASS+14 hInstance == GCW_HMODULE", uw(cls + 0x14) == GetClassWord(w, GCW_HMODULE));
    obs("CLASS+1C lpszMenuName lo", uw(cls + 0x1C));
    obs("CLASS+1E lpszMenuName hi", uw(cls + 0x1E));
    obs("CLASS+20 lpszClassName lo", uw(cls + 0x20));
    obs("CLASS+22 lpszClassName hi", uw(cls + 0x22));
    obs("CLASS+26 class extra word 1", uw(cls + 0x26) == 0xCAFE);
    obs("GetClassInfo lpszClassName is the name passed", wc.lpszClassName != 0);
    obs("GetClassInfo hInstance == hInstance", (WORD)wc.hInstance == (WORD)hi);
    {
        /* the next class: a near DGROUP pointer or 0 */
        WORD n = uw(cls), steps = 0;
        while (n && steps < 200) {
            n = uw(n);
            steps++;
        }
        obs_env("classes after US15L in the list", steps);
        obs("class list ends", steps < 200);
    }
    DestroyWindow(w);
    obs("CLASS+08 cClsWnds after DestroyWindow", uw(cls + 0x08));
    UnregisterClass("US15L", hi);
}

/* ---- one object at a time ---- */
static void costs(HWND top)
{
    HWND w, w2, ws[8];
    HDC dc[4];
    int i;
    char buf[40];
    ATOM a;

    reg("US15", 0, 0, 0, NULL);
    reg("US15X", 0, 0, 10, NULL);
    w = child(top, "US15", "", 0, 1);       /* warm up */
    DestroyWindow(w);

    begin();
    w = child(top, "US15", "", 0, 1);
    cost("window, no text, no extra");
    obs("window: DGROUP bytes = TOOLHELP block", dcost() > 0);
    begin();
    DestroyWindow(w);
    cost("DestroyWindow");

    begin();
    w = child(top, "US15X", "", 0, 1);
    cost("window, 10 extra bytes");
    DestroyWindow(w);

    begin();
    w = child(top, "US15", "0123456789", 0, 1);
    cost("window, 10-char title");
    begin();
    SetWindowText(w, "012345678901234567890123456789");
    cost("SetWindowText 10 to 30 chars");
    begin();
    SetWindowText(w, "");
    cost("SetWindowText to empty");
    begin();
    SetWindowText(w, "0123456789012345678901234567890123456789012345678901234567890123456789");
    cost("SetWindowText empty to 70 chars");
    begin();
    DestroyWindow(w);
    cost("DestroyWindow with 70-char text");

    begin();
    w = child(top, "US15", "", WS_VSCROLL | WS_HSCROLL, 1);
    cost("window with scroll bars");
    begin();
    SetScrollRange(w, SB_VERT, 0, 100, FALSE);
    SetScrollPos(w, SB_VERT, 10, FALSE);
    cost("SetScrollRange/Pos");
    DestroyWindow(w);

    begin();
    w = CreateWindow("US15", "", WS_POPUP | WS_SYSMENU | WS_CAPTION, 0, 0, 50, 50, top, NULL, hi, NULL);
    cost("popup with caption and system menu");
    begin();
    GetSystemMenu(w, FALSE);
    cost("GetSystemMenu(FALSE)");
    DestroyWindow(w);

    /* classes */
    begin();
    reg("ABCD", 0, 0, 0, NULL);
    cost("RegisterClass 4-char name");
    begin();
    UnregisterClass("ABCD", hi);
    cost("UnregisterClass");
    begin();
    reg("ABCDEFGHIJKLMNOPQRST", 0, 0, 0, NULL);
    cost("RegisterClass 20-char name");
    UnregisterClass("ABCDEFGHIJKLMNOPQRST", hi);
    begin();
    reg("ABCE", 0, 8, 0, NULL);
    cost("RegisterClass 8 class extra bytes");
    UnregisterClass("ABCE", hi);
    begin();
    reg("ABCF", 0, 0, 0, "MENUNAME12");
    cost("RegisterClass menu name 10 chars");
    UnregisterClass("ABCF", hi);
    begin();
    reg("ABCG", 0, 0, 0, MAKEINTRESOURCE(5));
    cost("RegisterClass integer menu name");
    UnregisterClass("ABCG", hi);
    begin();
    a = reg("ABCH", 0, 0, 0, NULL);
    obs_env("class atom", a);
    obs("class atom found by GlobalFindAtom", GlobalFindAtom("ABCH") == a);
    /* (3.1's RegisterClass returns 1, not the atom, to a program marked
       for 3.00 as this one is: US16) */
    obs_env("class atom >= C000h", a >= 0xC000u);
    UnregisterClass("ABCH", hi);

    /* properties */
    w = child(top, "US15", "", 0, 1);
    w2 = child(top, "US15", "", 0, 2);
    begin();
    SetProp(w, MAKEINTATOM(5), (HANDLE)1);
    cost("SetProp integer atom");
    begin();
    SetProp(w, MAKEINTATOM(6), (HANDLE)1);
    cost("SetProp 2nd integer atom");
    begin();
    SetProp(w, "PropName12", (HANDLE)1);
    cost("SetProp 10-char string");
    obs("SetProp string: GlobalFindAtom finds it", GlobalFindAtom("PropName12") != 0);
    begin();
    SetProp(w2, "PropName12", (HANDLE)1);
    cost("SetProp same string, another window");
    begin();
    SetProp(w, "PropName12", (HANDLE)2);
    cost("SetProp again (replace)");
    begin();
    RemoveProp(w, "PropName12");
    cost("RemoveProp string");
    begin();
    DestroyWindow(w2);
    cost("DestroyWindow with a string property");
    obs("GlobalFindAtom after the last window with the property went", GlobalFindAtom("PropName12") != 0);
    DestroyWindow(w);

    /* hooks */
    begin();
    {
        HHOOK hk = SetWindowsHookEx(WH_MSGFILTER, (HOOKPROC)hookproc, hi, GetCurrentTask());
        cost("SetWindowsHookEx");
        begin();
        UnhookWindowsHookEx(hk);
        cost("UnhookWindowsHookEx");
    }
    begin();
    SetWindowsHook(WH_MSGFILTER, (HOOKPROC)hookproc);
    cost("SetWindowsHook");
    UnhookWindowsHook(WH_MSGFILTER, (HOOKPROC)hookproc);

    /* timers */
    w = child(top, "US15", "", 0, 1);
    begin();
    SetTimer(w, 1, 30000, NULL);
    cost("SetTimer");
    begin();
    SetTimer(w, 2, 30000, NULL);
    cost("SetTimer 2nd");
    KillTimer(w, 1);
    KillTimer(w, 2);
    DestroyWindow(w);

    /* DCs */
    reg("US15O", CS_OWNDC, 0, 0, NULL);
    reg("US15C", CS_CLASSDC, 0, 0, NULL);
    begin();
    w = child(top, "US15O", "", 0, 1);
    cost("CS_OWNDC window");
    begin();
    DestroyWindow(w);
    cost("CS_OWNDC window destroyed");
    begin();
    w = child(top, "US15C", "", 0, 1);
    cost("CS_CLASSDC first window");
    begin();
    w2 = child(top, "US15C", "", 0, 2);
    cost("CS_CLASSDC second window");
    DestroyWindow(w2);
    DestroyWindow(w);
    for (i = 0; i < 8; i++)
        ws[i] = child(top, "US15", "", 0, i + 1);
    begin();
    for (i = 0; i < 4; i++)
        dc[i] = GetDC(ws[i]);
    cost("4 GetDCs at once");
    /* (3.1's cache has five DCs: a sixth GetDC at once ends Windows) */
    begin();
    for (i = 0; i < 4; i++)
        ReleaseDC(ws[i], dc[i]);
    cost("4 ReleaseDCs");
    for (i = 0; i < 8; i++)
        DestroyWindow(ws[i]);

    /* atoms */
    begin();
    a = RegisterWindowMessage("US15 registered message");
    cost("RegisterWindowMessage");
    grew_name('G');
    obs("RegisterWindowMessage == GlobalFindAtom", GlobalFindAtom("US15 registered message") == a);
    begin();
    GlobalAddAtom("US15 global atom 1");
    cost("GlobalAddAtom 18 chars");
    grew_name('G');
    begin();
    a = RegisterClipboardFormat("US15 clip format");
    cost("RegisterClipboardFormat");
    obs("RegisterClipboardFormat == GlobalFindAtom", GlobalFindAtom("US15 clip format") == a);

    /* controls */
    begin();
    w = CreateWindow("EDIT", "0123456789012345678901234567890123456789", WS_CHILD, 0, 0, 100, 20, top, (HMENU)7, hi, NULL);
    cost("EDIT with 40 chars");
    obs_env("EDIT: EM_GETHANDLE", (WORD)SendMessage(w, EM_GETHANDLE, 0, 0));
    DestroyWindow(w);
    begin();
    w = CreateWindow("LISTBOX", "", WS_CHILD | LBS_HASSTRINGS, 0, 0, 100, 100, top, (HMENU)8, hi, NULL);
    cost("LISTBOX");
    begin();
    for (i = 0; i < 10; i++)
        SendMessage(w, LB_ADDSTRING, 0, (LPARAM)(LPSTR)"0123456789");
    cost("LISTBOX 10 strings of 10");
    DestroyWindow(w);
    begin();
    w = CreateWindow("BUTTON", "Button", WS_CHILD | BS_PUSHBUTTON, 0, 0, 100, 20, top, (HMENU)9, hi, NULL);
    cost("BUTTON, 6-char title");
    DestroyWindow(w);
    begin();
    w = CreateWindow("COMBOBOX", "", WS_CHILD | CBS_DROPDOWNLIST, 0, 0, 100, 100, top, (HMENU)9, hi, NULL);
    cost("COMBOBOX dropdown list");
    DestroyWindow(w);
    (void)buf;
}

/* ---- how many fit ---- */
static HWND FAR *hw;
#define NMAX 16000

static void fill_windows(HWND top, const char *tag, LPCSTR text)
{
    WORD n;
    for (n = 0; n < NMAX; n++) {
        hw[n] = child(top, "US15", text, 0, n);
        if (!hw[n])
            break;
        if (n % 32 == 31)
            line(tag, n + 1);
    }
    nm(tag, ": count");
    obs_env(name, n);
    nm(tag, " full:");
    report(name);
    while (n)
        DestroyWindow(hw[--n]);
}

static void fill_classes(void)
{
    WORD n;
    for (n = 0; n < 4000; n++) {
        numname("K", n);
        if (!reg(cname, 0, 0, 0, NULL))
            break;
        if (n % 32 == 31)
            line("classes", n + 1);
    }
    obs_env("classes: count", n);
    report("classes full:");
    while (n) {
        numname("K", --n);
        UnregisterClass(cname, hi);
    }
}

static void fill_props(HWND top, BOOL str)
{
    HWND w = child(top, "US15", "", 0, 1);
    WORD n;
    for (n = 0; n < 8000; n++) {
        BOOL ok;
        if (str) {
            numname("P", n);
            ok = SetProp(w, cname, (HANDLE)n);
        } else
            ok = SetProp(w, MAKEINTATOM(n + 1), (HANDLE)n);
        if (!ok)
            break;
        if (n % 32 == 31)
            line(str ? "string props" : "atom props", n + 1);
    }
    obs_env(str ? "string props: count" : "atom props: count", n);
    if (n) {
        /* the last one set can be read back */
        if (str)
            numname("P", n - 1);
        /* (3.1 accepts string names after its atom heap is full but
           loses them: an OBS ~ for those) */
        (str ? obs_env : obs)(str ? "string props: the last one reads back" : "atom props: the last one reads back",
            GetProp(w, str ? (LPCSTR)cname : MAKEINTATOM(n)) == (HANDLE)(n - 1));
    }
    if (str) {
        /* how many read back (3.1 goes on accepting string names once
           its global atom heap is full, but loses them) */
        WORD i, ok = 0;
        for (i = 0; i < n; i++) {
            numname("P", i);
            ok += GetProp(w, cname) == (HANDLE)i;
        }
        obs_env("string props: read back", ok);
    }
    report(str ? "string props full:" : "atom props full:");
    while (n) {
        --n;
        if (str) {
            numname("P", n);
            RemoveProp(w, cname);
        } else
            RemoveProp(w, MAKEINTATOM(n + 1));
    }
    DestroyWindow(w);
}

static void fill_hooks(void)
{
    WORD n;
    HHOOK FAR *hk = (HHOOK FAR *)hw;
    for (n = 0; n < 8000; n++) {
        hk[n] = SetWindowsHookEx(WH_MSGFILTER, (HOOKPROC)hookproc, hi, GetCurrentTask());
        if (!hk[n])
            break;
        if (n % 32 == 31)
            line("hooks", n + 1);
    }
    obs_env("hooks: count", n);
    report("hooks full:");
    while (n)
        UnhookWindowsHookEx(hk[--n]);
}

static void fill_timers(HWND top)
{
    HWND w = child(top, "US15", "", 0, 1);
    WORD n;
    for (n = 0; n < 3000; n++) {
        if (!SetTimer(w, n + 1, 60000u, NULL))
            break;
        if (n % 32 == 31)
            line("timers", n + 1);
    }
    obs_env("timers: count", n);
    report("timers full:");
    while (n)
        KillTimer(w, n--);
    DestroyWindow(w);
}

static void fill_dlgs(HWND top)
{
    /* windows with 10 extra bytes and a 12-char text: a typical control */
    WORD n;
    for (n = 0; n < NMAX; n++) {
        hw[n] = child(top, "US15X", "Typical text", 0, n);
        if (!hw[n])
            break;
        if (n % 32 == 31)
            line("typical windows", n + 1);
    }
    obs_env("typical windows: count", n);
    report("typical windows full:");
    while (n)
        DestroyWindow(hw[--n]);
}

int __cdecl tmain(void)
{
    HINSTANCE th;
    SYSHEAPINFO si;
    HGLOBAL g;
    HWND top;
    int i;
    hi = (HINSTANCE)init_regs[5];
    WaitEvent(0);
    InitApp(hi);
    out("US15 start\r\n");
    huser = GetModuleHandle("USER");
    gfsr = (PGFSR)GetProcAddress(huser, MAKEINTRESOURCE(284));
    getheapspaces = (PGHS)GetProcAddress(GetModuleHandle("KERNEL"), MAKEINTRESOURCE(138));
    th = LoadLibrary("TOOLHELP.DLL");
    obs("TOOLHELP loaded", th > HINSTANCE_ERROR);
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
    obs("hUserSegment is USER's DGROUP", userds == (WORD)GetWindowWord(GetDesktopWindow(), GWW_HINSTANCE));
    report("start");
    {
        /* name the menu heaps */
        HMENU m;
        begin();
        m = CreatePopupMenu();
        scan();
        grew_name('M');
        AppendMenu(m, MF_STRING, 1, "0123456789");
        scan();
        for (i = 0; i < nheaps; i++)
            if (heaps[i].name == '?' && delta(i) > 0)
                heaps[i].name = 'S';
        DestroyMenu(m);
    }
    {
        /* the global atom table's heap */
        ATOM a;
        scan();
        a = GlobalAddAtom("US15 naming the atom heap, a long name to be sure");
        scan();
        grew_name('G');
        GlobalDeleteAtom(a);
    }
    report("named");
    types("start");
    for (i = 0; i < (int)sizeof longtext - 1; i++)
        longtext[i] = (char)('a' + i % 26);
    longtext[120] = 0;

    reg("US15", 0, 0, 0, NULL);
    top = CreateWindow("US15", "US15", WS_OVERLAPPED, 0, 0, 100, 100, NULL, NULL, hi, NULL);
    layout(top);
    costs(top);
    report("after costs");
    types("after costs");

    g = GlobalAlloc(GMEM_MOVEABLE, NMAX * sizeof(HWND));
    hw = (HWND FAR *)GlobalLock(g);
    fill_windows(top, "windows", "");
    report("windows freed");
    fill_windows(top, "titled windows", longtext);
    report("titled windows freed");
    fill_dlgs(top);
    fill_classes();
    report("classes freed");
    fill_props(top, FALSE);
    fill_props(top, TRUE);
    report("props freed");
    fill_hooks();
    report("hooks freed");
    fill_timers(top);
    report("timers freed");
    GlobalUnlock(g);
    GlobalFree(g);
    DestroyWindow(top);
    report("end");
    out("US15 end\r\n");
    return 0;
}
