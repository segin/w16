/*
 * tk12.c - KERNEL resource services: FindResource, LoadResource,
 * LockResource, FreeResource, SizeofResource, AccessResource,
 * AllocResource, SetResourceHandler, and the resource table in the module
 * table (offset 24h of the hModule's segment, in the NE file's format with
 * the handler, handle and usage words filled in).
 *
 * The resources are in tk12.rc; TK12.EXE is linked with alignment 16, so
 * the resource shift is 4 and lengths are rounded to 16 bytes. The table
 * holds, in this order: type 10 (RT_RCDATA) ids 1-6, NAMED, OTHER; type
 * MYTYPE ids 7, LOWER; type 300 id 1.
 *
 * Output and checks: see tklog.h; reference values in tk12ref.h. Calls
 * that might fault on Windows come last.
 */
#include <windows.h>

#define TK_REF "tk12ref.h"
#include "tklog.h"

extern int __cdecl dos_int21(WORD FAR *r);

#define STEP(s) out("-- " s "\r\n")

typedef HGLOBAL (FAR PASCAL *hdlrfn)(HGLOBAL, HINSTANCE, HRSRC);

static WORD hm;         /* our hModule */
static WORD hi;         /* our hInstance */
static WORD rt;         /* the resource table in the module table */
static char nm[96];

static WORD W(WORD sel, WORD off)
{
    return *w_at(sel, off);
}

static BYTE B(WORD sel, WORD off)
{
    return *(BYTE FAR *)MAKELP(sel, off);
}

static const char *cat(const char *a, const char *b)
{
    WORD i = 0;
    while (*a && i < sizeof nm - 2)
        nm[i++] = *a++;
    nm[i++] = ' ';
    while (*b && i < sizeof nm - 1)
        nm[i++] = *b++;
    nm[i] = 0;
    return nm;
}

static int same_bytes(const char FAR *a, const char *b)
{
    while (*b)
        if (*a++ != *b++)
            return 0;
    return *a == 0;
}

static int starts(const char FAR *a, const char *b)
{
    while (*b)
        if (*a++ != *b++)
            return 0;
    return 1;
}

/* NAMEINFO k of type entry t (0-based, in table order) */
static WORD type_at(WORD t)
{
    WORD p = rt + 2;
    while (t--)
        p += 8 + 12 * W(hm, p + 2);
    return p;
}

static WORD ni(WORD t, WORD k)
{
    return type_at(t) + 8 + 12 * k;
}

/* ---- the resource handler ---- */

static FARPROC prev_handler;
static WORD h_calls, h_mem, h_inst, h_rsrc, h_ret, h_fail;

HGLOBAL __far __pascal __loadds my_handler(HGLOBAL mem, HINSTANCE inst, HRSRC res)
{
    HGLOBAL r = 0;
    h_calls++;
    h_mem = (WORD)mem;
    h_inst = (WORD)inst;
    h_rsrc = (WORD)res;
    if (!h_fail && prev_handler)
        r = ((hdlrfn)prev_handler)(mem, inst, res);
    h_ret = (WORD)r;
    return r;
}

/* ---- DOS file access for AccessResource's handle ---- */

static WORD dos(WORD ax, WORD bx, WORD cx, WORD dx, void FAR *buf, WORD *carry)
{
    WORD r[8];
    r[0] = ax;
    r[1] = bx;
    r[2] = cx;
    r[3] = dx;
    r[4] = r[5] = 0;
    r[6] = buf ? SELECTOROF(buf) : 0;
    r[7] = 0;
    if (buf)
        r[3] = OFFSETOF(buf);
    *carry = (WORD)dos_int21(r);
    if (ax == 0x4201)
        return r[0] | (r[3] ? 0x8000 : 0);      /* low word; flag a high word */
    return r[0];
}

/* ---- the table as it is ---- */

static const char *const rname[11] = {
    "R10.1", "R10.2", "R10.3", "R10.4", "R10.5", "R10.6", "R10.NAMED",
    "R10.OTHER", "RMYTYPE.7", "RMYTYPE.LOWER", "R300.1"
};

static WORD ni_n(WORD n)
{
    return n < 8 ? ni(0, n) : n < 10 ? ni(1, n - 8) : ni(2, 0);
}

static void dump_ni(const char *when, WORD n)
{
    WORD e = ni_n(n), h = W(hm, e + 8);
    char t[48];
    WORD i = 0;
    const char *s = rname[n];
    while (*s)
        t[i++] = *s++;
    t[i++] = ' ';
    while (*when)
        t[i++] = *when++;
    t[i] = 0;
    obs(cat(t, "flags"), W(hm, e + 4));
    obs(cat(t, "handle!=0"), h != 0);
    obs_env(cat(t, "handle"), h);
    obs(cat(t, "usage"), W(hm, e + 10));
    if (h) {
        obs(cat(t, "handle odd"), h & 1);
        obs(cat(t, "GlobalFlags"), GlobalFlags((HGLOBAL)h));
        obs(cat(t, "GlobalSize"), (WORD)GlobalSize((HGLOBAL)h));
    }
}

static void table(void)
{
    WORD t, p, n;
    obs("rsrctab==segtab+10*segments", rt == W(hm, 0x22) + 10 * W(hm, 0x1C));
    obs("rsrctab shift", W(hm, rt));
    for (t = 0; t < 3; t++) {
        p = type_at(t);
        obsn("type id", t, W(hm, p));
        obsn("type count", t, W(hm, p + 2));
        obsn("type handler!=0", t, W(hm, p + 4) || W(hm, p + 6));
        obsn("type handler==type 0's", t,
             W(hm, p + 4) == W(hm, rt + 6) && W(hm, p + 6) == W(hm, rt + 8));
        obs_env("type handler off", W(hm, p + 4));
        obs_env("type handler seg", W(hm, p + 6));
    }
    p = type_at(3);
    obs("table end type id", W(hm, p));
    obs("after table: name length", B(hm, p + 2));
    for (n = 0; n < 11; n++) {
        WORD e = ni_n(n);
        obs_env(cat(rname[n], "offset"), W(hm, e));
        obs(cat(rname[n], "length"), W(hm, e + 2));
        obs(cat(rname[n], "id"), (W(hm, e + 6) & 0x8000) ? W(hm, e + 6) : 0);
    }
    for (n = 0; n < 11; n++)
        dump_ni("start", n);
}

/* ---- FindResource ---- */

static void find(const char *what, LPCSTR name, LPCSTR type, WORD expect)
{
    HRSRC r = FindResource((HINSTANCE)hi, name, type);
    obs(cat("Find", what), expect ? (WORD)r == expect : (WORD)r);
}

static void finds(void)
{
    WORD k;
    find("1 RCDATA", MAKEINTRESOURCE(1), RT_RCDATA, ni(0, 0));
    find("6 RCDATA", MAKEINTRESOURCE(6), RT_RCDATA, ni(0, 5));
    obs("Find by hModule", (WORD)FindResource((HINSTANCE)hm, MAKEINTRESOURCE(2), RT_RCDATA) == ni(0, 1));
    find("\"#1\" RCDATA", "#1", RT_RCDATA, ni(0, 0));
    find("\"#3\" \"#10\"", "#3", "#10", ni(0, 2));
    find("\"#003\" \"#010\"", "#003", "#010", ni(0, 2));
    find("\"1\" RCDATA", "1", RT_RCDATA, 0);
    find("\"#\" RCDATA", "#", RT_RCDATA, 0);
    find("\"#1x\" RCDATA", "#1x", RT_RCDATA, 0);
    find("\"#65537\" RCDATA", "#65537", RT_RCDATA, 0);
    find("NAMED", "NAMED", RT_RCDATA, ni(0, 6));
    find("named", "named", RT_RCDATA, ni(0, 6));
    find("Other", "Other", RT_RCDATA, ni(0, 7));
    find("OTHE", "OTHE", RT_RCDATA, 0);
    find("OTHERS", "OTHERS", RT_RCDATA, 0);
    find("7 MYTYPE", MAKEINTRESOURCE(7), "MYTYPE", ni(1, 0));
    find("7 mytype", MAKEINTRESOURCE(7), "mytype", ni(1, 0));
    find("\"#7\" \"MyType\"", "#7", "MyType", ni(1, 0));
    find("LOWER MYTYPE", "LOWER", "MYTYPE", ni(1, 1));
    find("1 300", MAKEINTRESOURCE(1), MAKEINTRESOURCE(300), ni(2, 0));
    find("1 \"#300\"", MAKEINTRESOURCE(1), "#300", ni(2, 0));
    /* missing */
    find("99 RCDATA", MAKEINTRESOURCE(99), RT_RCDATA, 0);
    find("1 type 11", MAKEINTRESOURCE(1), MAKEINTRESOURCE(11), 0);
    find("NOPE RCDATA", "NOPE", RT_RCDATA, 0);
    find("1 NOTYPE", MAKEINTRESOURCE(1), "NOTYPE", 0);
    find("NAMED MYTYPE", "NAMED", "MYTYPE", 0);
    find("7 RCDATA", MAKEINTRESOURCE(7), RT_RCDATA, 0);
    find("0 RCDATA", MAKEINTRESOURCE(0), RT_RCDATA, 0);
    find("1 type 0", MAKEINTRESOURCE(1), MAKEINTRESOURCE(0), 0);
    find("\"\" RCDATA", "", RT_RCDATA, 0);
    for (k = 0; k < 11; k++)
        obs(cat(rname[k], "hrsrc - rsrctab"), ni_n(k) - rt);
}

/* ---- SizeofResource ---- */

static void sizes(void)
{
    WORD n;
    for (n = 0; n < 11; n++) {
        DWORD s = SizeofResource((HINSTANCE)hi, (HRSRC)ni_n(n));
        obs(cat(rname[n], "Sizeof"), (WORD)s);
        obs(cat(rname[n], "Sizeof high"), (WORD)(s >> 16));
    }
    obs("Sizeof by hModule", (WORD)SizeofResource((HINSTANCE)hm, (HRSRC)ni(0, 0)));
}

/* ---- AccessResource ---- */

static void access(void)
{
    WORD e = ni(0, 0), shift = W(hm, rt), cf, pos, n, fh, fh2;
    char buf[8];
    int i;

    fh = AccessResource((HINSTANCE)hi, (HRSRC)e);
    obs("Access handle!=-1", fh != 0xFFFF);
    obs_env("Access handle", fh);
    if (fh == 0xFFFF)
        return;
    pos = dos(0x4201, fh, 0, 0, NULL, &cf);
    obs("Access position==offset<<shift", !cf && pos == W(hm, e) << shift);
    n = dos(0x3F00, fh, 6, 0, (void FAR *)buf, &cf);
    obs("Access read", cf ? 0xE000 | n : n);
    obs("Access data", same_bytes(buf, "Hello"));
    /* write the byte back that is there: does the handle allow writing? */
    dos(0x4200, fh, 0, W(hm, e) << shift, NULL, &cf);
    n = dos(0x4000, fh, 1, 0, (void FAR *)buf, &cf);
    obs("Access write", cf ? 0xE000 | n : n);
    fh2 = AccessResource((HINSTANCE)hi, (HRSRC)ni(0, 1));
    obs("Access again: another handle", fh2 != fh && fh2 != 0xFFFF);
    pos = dos(0x4201, fh2, 0, 0, NULL, &cf);
    obs("Access again position==offset<<shift", !cf && pos == W(hm, ni(0, 1)) << shift);
    pos = dos(0x4201, fh, 0, 0, NULL, &cf);
    obs("Access first position - offset<<shift", cf ? 0xE000 : pos - (W(hm, e) << shift));
    dos(0x3E00, fh2, 0, 0, NULL, &cf);
    obs("Access close", cf);
    dos(0x3E00, fh, 0, 0, NULL, &cf);
    for (i = 0; i < 8; i++)
        buf[i] = 0;
    fh = AccessResource((HINSTANCE)hm, (HRSRC)ni(1, 1));
    obs("Access by hModule", fh != 0xFFFF);
    if (fh != 0xFFFF) {
        dos(0x3F00, fh, 7, 0, (void FAR *)buf, &cf);
        obs("Access MYTYPE LOWER data", same_bytes(buf, "Mytype "));
        dos(0x3E00, fh, 0, 0, NULL, &cf);
    }
}

/* ---- AllocResource ---- */

static void alloc1(const char *what, WORD e, DWORD n)
{
    WORD before = W(hm, e + 8), ubefore = W(hm, e + 10);
    HGLOBAL h = AllocResource((HINSTANCE)hi, (HRSRC)e, n);
    char t[48];
    WORD i = 0;
    while (*what)
        t[i++] = *what++;
    t[i] = 0;
    obs(cat(t, "!=0"), h != 0);
    if (!h)
        return;
    obs(cat(t, "odd"), (WORD)h & 1);
    obs(cat(t, "GlobalSize"), (WORD)GlobalSize(h));
    obs(cat(t, "GlobalFlags"), GlobalFlags(h));
    obs(cat(t, "table handle unchanged"), W(hm, e + 8) == before);
    obs(cat(t, "table usage unchanged"), W(hm, e + 10) == ubefore);
    obs(cat(t, "Lock!=NULL"), GlobalLock(h) != NULL);
    obs(cat(t, "GlobalFlags locked"), GlobalFlags(h));
    GlobalUnlock(h);
    obs(cat(t, "FreeResource"), FreeResource(h));
    obs(cat(t, "GlobalFlags freed"), GlobalFlags(h));
}

static void allocs(void)
{
    alloc1("Alloc R10.1 0", ni(0, 0), 0);
    alloc1("Alloc R10.1 100", ni(0, 0), 100);
    alloc1("Alloc R10.1 1", ni(0, 0), 1);
    alloc1("Alloc R10.6 10", ni(0, 5), 10);
    alloc1("Alloc R10.6 2000", ni(0, 5), 2000);
    alloc1("Alloc R10.3 0", ni(0, 2), 0);       /* moveable */
    alloc1("Alloc R10.2 0", ni(0, 1), 0);       /* fixed, preloaded */
    alloc1("Alloc R300.1 0", ni(2, 0), 0);
}

/* ---- LoadResource / LockResource / FreeResource ---- */

static void life(WORD n, const char *text)
{
    WORD e = ni_n(n), h, h2, h3;
    const char FAR *p;
    const char *tag = rname[n];
    char t[48];
    WORD i = 0;

    h = (WORD)LoadResource((HINSTANCE)hi, (HRSRC)e);
    obs(cat(tag, "Load!=0"), h != 0);
    obs(cat(tag, "Load==table handle"), h == W(hm, e + 8));
    while (*tag)
        t[i++] = *tag++;
    t[i] = 0;
    dump_ni("loaded", n);
    if (!h)
        return;
    h2 = (WORD)LoadResource((HINSTANCE)hm, (HRSRC)e);
    obs(cat(t, "Load again same"), h2 == h);
    obs(cat(t, "usage after 2 loads"), W(hm, e + 10));
    p = LockResource((HGLOBAL)h);
    obs(cat(t, "Lock!=NULL"), p != NULL);
    obs(cat(t, "Lock sel==handle|1"), SELECTOROF(p) == (h | 1));
    obs(cat(t, "Lock offset"), OFFSETOF(p));
    obs(cat(t, "data"), p && starts(p, text));
    obs(cat(t, "GlobalFlags locked"), GlobalFlags((HGLOBAL)h));
    LockResource((HGLOBAL)h);
    obs(cat(t, "GlobalFlags locked twice"), GlobalFlags((HGLOBAL)h));
    obs(cat(t, "usage after locks"), W(hm, e + 10));
    obs(cat(t, "Unlock"), GlobalUnlock((HGLOBAL)h));
    obs(cat(t, "Unlock 2"), GlobalUnlock((HGLOBAL)h));
    obs(cat(t, "Unlock 3"), GlobalUnlock((HGLOBAL)h));
    obs(cat(t, "Free 1==handle"), (WORD)FreeResource((HGLOBAL)h) == h);
    dump_ni("freed once", n);
    obs(cat(t, "Free 2"), FreeResource((HGLOBAL)h));
    obs(cat(t, "after 2 frees: GlobalFlags"), GlobalFlags((HGLOBAL)h));
    obs(cat(t, "after 2 frees: GlobalSize"), (WORD)GlobalSize((HGLOBAL)h));
    obs(cat(t, "after 2 frees: GlobalHandle==handle"), (WORD)GlobalHandle(h | 1) == h);
    dump_ni("freed twice", n);
    h3 = (WORD)LoadResource((HINSTANCE)hi, (HRSRC)e);
    obs(cat(t, "reload!=0"), h3 != 0);
    obs(cat(t, "reload same handle"), h3 == h);
    p = LockResource((HGLOBAL)h3);
    obs(cat(t, "reload data"), p && starts(p, text));
    GlobalUnlock((HGLOBAL)h3);
    dump_ni("reloaded", n);
    obs(cat(t, "Free 3"), FreeResource((HGLOBAL)h3));
    dump_ni("freed thrice", n);
    obs(cat(t, "Free 4 (usage 0)"), FreeResource((HGLOBAL)h3));
    dump_ni("end", n);
}

/* ---- SetResourceHandler ---- */

static void handlers(void)
{
    WORD t0 = type_at(0), e, h, h2;
    FARPROC p, q;
    DWORD before = MAKELONG(W(hm, t0 + 4), W(hm, t0 + 6));
    const char FAR *d;

    p = (FARPROC)SetResourceHandler((HINSTANCE)hi, RT_RCDATA, (RSRCHDLRPROC)my_handler);
    prev_handler = p;
    obs("SetHandler prev==table", (DWORD)p == before);
    obs("SetHandler table==new", W(hm, t0 + 4) == OFFSETOF(my_handler) &&
        W(hm, t0 + 6) == SELECTOROF(my_handler));
    obs("SetHandler other types unchanged",
        W(hm, type_at(1) + 4) == LOWORD(before) && W(hm, type_at(1) + 6) == HIWORD(before));
    q = (FARPROC)SetResourceHandler((HINSTANCE)hi, "NOTYPE", (RSRCHDLRPROC)my_handler);
    obs("SetHandler missing type", (WORD)(DWORD)q | (WORD)((DWORD)q >> 16));
    q = (FARPROC)SetResourceHandler((HINSTANCE)hm, "#300", (RSRCHDLRPROC)my_handler);
    obs("SetHandler \"#300\" prev==default", (DWORD)q == before);
    q = (FARPROC)SetResourceHandler((HINSTANCE)hm, "#300", (RSRCHDLRPROC)p);
    obs("SetHandler \"#300\" back", (DWORD)q == (DWORD)(FARPROC)my_handler);
    q = (FARPROC)SetResourceHandler((HINSTANCE)hi, "mytype", (RSRCHDLRPROC)p);
    obs("SetHandler mytype prev==default", (DWORD)q == before);

    /* a resource not loaded yet: R10.5 */
    e = ni(0, 4);
    h_calls = 0;
    h = (WORD)LoadResource((HINSTANCE)hi, (HRSRC)e);
    obs("Handler: calls on Load", h_calls);
    obs("Handler: hMem", h_mem);
    obs("Handler: hInstance==hInstance", h_inst == hi);
    obs("Handler: hInstance==hModule", h_inst == hm);
    obs_env("Handler: hInstance", h_inst);
    obs("Handler: hRsrc==hrsrc", h_rsrc == e);
    obs("Handler: result==Load", h_ret == h);
    obs("Handler: table handle==Load", W(hm, e + 8) == h);
    obs("Handler: usage", W(hm, e + 10));
    h_calls = 0;
    LoadResource((HINSTANCE)hi, (HRSRC)e);
    obs("Handler: calls on second Load", h_calls);
    d = LockResource((HGLOBAL)h);
    obs("Handler: calls on Lock", h_calls);
    obs("Handler: data", d && same_bytes(d, "Default options."));
    GlobalUnlock((HGLOBAL)h);
    /* discard it: GlobalDiscard */
    h_calls = 0;
    obs("Handler: GlobalDiscard", (WORD)GlobalReAlloc((HGLOBAL)h, 0, GMEM_MOVEABLE) == h);
    obs("Handler: GlobalFlags discarded", GlobalFlags((HGLOBAL)h));
    obs("Handler: GlobalSize discarded", (WORD)GlobalSize((HGLOBAL)h));
    obs("Handler: usage discarded", W(hm, e + 10));
    obs("Handler: table handle discarded", W(hm, e + 8) == h);
    obs("Handler: calls on discard", h_calls);
    obs("Handler: table flags discarded", W(hm, e + 4));
    d = LockResource((HGLOBAL)h);
    obs("Handler: calls on Lock of discarded", h_calls);
    obs("Handler: hMem==handle (Lock)", h_mem == h);
    obs("Handler: hRsrc (Lock)", h_rsrc == e);
    obs("Handler: hInstance (Lock)==hModule", h_inst == hm);
    obs("Handler: Lock of discarded", d != NULL);
    obs("Handler: Lock of discarded sel==handle|1", SELECTOROF(d) == (h | 1));
    obs("Handler: Lock of discarded data", d && same_bytes(d, "Default options."));
    obs("Handler: GlobalFlags reloaded", GlobalFlags((HGLOBAL)h));
    GlobalUnlock((HGLOBAL)h);
    h_calls = 0;
    GlobalReAlloc((HGLOBAL)h, 0, GMEM_MOVEABLE);
    {
        WORD r = (WORD)LoadResource((HINSTANCE)hi, (HRSRC)e);
        obs("Handler: calls on Load of discarded", h_calls);
        obs("Handler: hMem==handle (Load)", h_mem == h);
        obs("Handler: Load of discarded==handle", r == h);
        obs("Handler: usage after Load of discarded", W(hm, e + 10));
        obs("Handler: GlobalFlags after Load of discarded", GlobalFlags((HGLOBAL)h));
    }
    /* a failing handler when a discarded resource is locked */
    h_calls = 0;
    h_fail = 1;
    d = LockResource((HGLOBAL)h);
    h_fail = 0;
    obs("Handler failing on Lock: calls", h_calls);
    obs("Handler failing on Lock: result", d != NULL);
    obs("Handler failing on Lock: GlobalFlags", GlobalFlags((HGLOBAL)h));
    obs("Handler failing on Lock: table handle", W(hm, e + 8) == h);
    obs("Handler failing on Lock: table flags", W(hm, e + 4));
    obs("Handler failing on Lock: usage", W(hm, e + 10));
    FreeResource((HGLOBAL)h);
    FreeResource((HGLOBAL)h);
    FreeResource((HGLOBAL)h);
    obs("Handler: usage after frees", W(hm, e + 10));
    obs("Handler: table handle after frees", W(hm, e + 8) == h);
    obs("Handler: GlobalFlags after frees", GlobalFlags((HGLOBAL)h));
    FreeResource((HGLOBAL)h);
    obs("Handler: table handle after a free at usage 0", W(hm, e + 8) == h);
    obs("Handler: GlobalFlags after a free at usage 0", GlobalFlags((HGLOBAL)h));
    /* not discardable, not loaded: R10.3 (moveable), R10.2 (fixed) */
    e = ni(0, 2);
    h_calls = 0;
    h = (WORD)LoadResource((HINSTANCE)hi, (HRSRC)e);
    obs("Handler: calls on Load of R10.3", h_calls);
    obs("Handler: hMem (R10.3)", h_mem);
    obs("Handler: hInstance==hModule (R10.3)", h_inst == hm);
    obs("Handler: hInstance==hInstance (R10.3)", h_inst == hi);
    obs_env("Handler: hInstance (R10.3)", h_inst);
    obs("Handler: hRsrc (R10.3)", h_rsrc == e);
    obs("Handler: result==Load (R10.3)", h_ret == h);
    h_calls = 0;
    LockResource((HGLOBAL)h);
    obs("Handler: calls on Lock of R10.3", h_calls);
    GlobalUnlock((HGLOBAL)h);
    FreeResource((HGLOBAL)h);
    e = ni(0, 1);
    h_calls = 0;
    h = (WORD)LoadResource((HINSTANCE)hi, (HRSRC)e);
    obs("Handler: calls on Load of R10.2", h_calls);
    obs("Handler: hMem (R10.2)", h_mem);
    obs("Handler: result==Load (R10.2)", h_ret == h);
    FreeResource((HGLOBAL)h);
    /* failing handler on a non-discardable resource: R10.3 again */
    e = ni(0, 2);
    h_calls = 0;
    h_fail = 1;
    h = (WORD)LoadResource((HINSTANCE)hi, (HRSRC)e);
    h_fail = 0;
    obs("Handler failing (R10.3): calls", h_calls);
    obs("Handler failing (R10.3): Load", h);
    obs("Handler failing (R10.3): usage", W(hm, e + 10));
    obs("Handler failing (R10.3): table handle", W(hm, e + 8));
    obs("Handler failing (R10.3): table flags", W(hm, e + 4));
    /* a handler that fails: R10.OTHER */
    e = ni(0, 7);
    h_calls = 0;
    h_fail = 1;
    h = (WORD)LoadResource((HINSTANCE)hi, (HRSRC)e);
    h_fail = 0;
    obs("Handler failing: calls", h_calls);
    obs("Handler failing: hMem", h_mem);
    obs("Handler failing: Load!=0", h != 0);
    obs("Handler failing: usage", W(hm, e + 10));
    obs("Handler failing: table handle==Load", W(hm, e + 8) == h);
    /* preloaded R10.4: handler called by Load? */
    e = ni(0, 3);
    h_calls = 0;
    h = (WORD)LoadResource((HINSTANCE)hi, (HRSRC)e);
    obs("Handler: calls on Load of preloaded", h_calls);
    FreeResource((HGLOBAL)h);
    q = (FARPROC)SetResourceHandler((HINSTANCE)hi, RT_RCDATA, (RSRCHDLRPROC)p);
    obs("SetHandler restore returns ours", (DWORD)q == (DWORD)(FARPROC)my_handler);
    /* the default handler called directly with hMem 0 */
    e = ni(0, 7);
    obs("Default handler(0): usage before", W(hm, e + 10));
    obs("Default handler(0): table handle before!=0", W(hm, e + 8) != 0);
    obs("Default handler(0): table flags before", W(hm, e + 4));
    h2 = W(hm, e + 8);
    h = (WORD)((hdlrfn)p)(0, (HINSTANCE)hm, (HRSRC)e);
    obs("Default handler(0): !=0", h != 0);
    obs("Default handler(0): ==old table handle", h == h2);
    obs("Default handler(0): table handle==result", W(hm, e + 8) == h);
    obs("Default handler(0): usage", W(hm, e + 10));
    obs("Default handler(0): table flags", W(hm, e + 4));
    obs("Default handler(0): GlobalFlags unlocked", GlobalFlags((HGLOBAL)h));
    obs("Default handler(0): GlobalSize", (WORD)GlobalSize((HGLOBAL)h));
    if (h) {
        d = GlobalLock((HGLOBAL)h);
        obs("Default handler(0): data", d && same_bytes(d, "Other."));
        obs("Default handler(0): GlobalFlags", GlobalFlags((HGLOBAL)h));
        GlobalUnlock((HGLOBAL)h);
        if (h != h2)
            GlobalFree((HGLOBAL)h);
    }
    FreeResource((HGLOBAL)h2);
    FreeResource((HGLOBAL)h2);
    obs("Default handler(0): end table handle", W(hm, e + 8));
}

/* ---- names compared against a lower-case table entry ---- */

static void lower_names(void)
{
    WORD e = ni(1, 1), off = rt + W(hm, e + 6), i;
    BYTE n = B(hm, off);
    for (i = 1; i <= n; i++)
        *(BYTE FAR *)MAKELP(hm, off + i) |= 0x20;
    find("table lower: LOWER", "LOWER", "MYTYPE", ni(1, 1));
    find("table lower: lower", "lower", "MYTYPE", ni(1, 1));
    find("table lower: Lower", "Lower", "MYTYPE", ni(1, 1));
    for (i = 1; i <= n; i++)
        *(BYTE FAR *)MAKELP(hm, off + i) &= ~0x20;
}

int __cdecl tmain(void)
{
    WORD h;

    out("TK12 start\r\n");
    hi = entry_regs[E_DS] & ~1u;
    hm = (WORD)GetModuleHandle("TK12");
    rt = W(hm, 0x24);

    STEP("table");
    table();
    STEP("find");
    finds();
    STEP("sizes");
    sizes();
    STEP("access");
    access();
    STEP("alloc");
    allocs();
    STEP("life");
    life(0, "Hello");
    life(1, "Preloaded resource, fixed.");
    life(2, "Moveable, not discardable.");
    life(3, "Preloaded, discardable.");
    life(5, "0123456789");
    life(6, "Named resource.");
    life(10, "Type 300.");
    STEP("handlers");
    handlers();
    STEP("names");
    lower_names();

    STEP("edge cases");
    obs("Load(hrsrc 0)", (WORD)LoadResource((HINSTANCE)hi, 0));
    obs("Sizeof(hrsrc 0)", (WORD)SizeofResource((HINSTANCE)hi, 0));
    obs("Lock(0)", LockResource(0) != NULL);
    obs("Free(0)", FreeResource(0));
    h = (WORD)GlobalAlloc(GMEM_MOVEABLE, 100);
    obs("Free(GlobalAlloc block)", FreeResource((HGLOBAL)h));
    obs("Free(GlobalAlloc block): GlobalFlags", GlobalFlags((HGLOBAL)h));
    obs("Free(GlobalAlloc block): GlobalSize", (WORD)GlobalSize((HGLOBAL)h));
    obs("Find(NULL, 1, RCDATA)", (WORD)FindResource(NULL, MAKEINTRESOURCE(1), RT_RCDATA));

    out("TK12 done, failures=");
    outhex((WORD)failures);
    out("\r\n");
    return failures;
}
