/*
 * tk3.c - local heap probe (LocalInit .. LocalCompact, InitTask's heap).
 *
 * Output (to TK.LOG, see start.asm), one item per line:
 *   PASS <name> / FAIL <name>   checks of behaviour observed on Windows 3.1
 *   OBS <name>=<value>          raw observations; run this program on both
 *                               w16 and Windows 3.1 (tests/refrun.sh) and
 *                               compare these lines
 *   OBS ~<name>=<value>         a value that depends on the program's
 *                               layout or the environment (addresses)
 *   OBS <name>@<off>=w w w ...  memory dumps (words, DS-relative offsets)
 * The exit code is the number of failures.
 *
 * Every plain OBS whose name is in tk3ref.h is also checked against the
 * value observed on Windows 3.1 (Standard and 386 Enhanced mode gave the
 * same values); tk3ref.h is generated from a reference run with
 * tools/obsref.py. Layout-dependent values (OBS ~) are checked relative to
 * each other in the code instead.
 *
 * The program makes no heap calls before these tests, so the task's local
 * heap is in the state InitTask left it in.
 */
#include <windows.h>

extern WORD __cdecl entry_regs[11];   /* ax bx cx dx si di bp sp ds es ss */
extern WORD __cdecl init_regs[7];    /* ax bx cx dx si di es */
extern void __cdecl log_write(const char FAR *buf, WORD len);

#define E_BX 1
#define E_CX 2
#define E_SP 7
#define E_DS 8

static int failures;

static void out(const char *s)
{
    WORD n = 0;
    while (s[n])
        n++;
    log_write(s, n);
}

static void outhex(WORD v)
{
    char b[5];
    int i;
    for (i = 3; i >= 0; i--) {
        b[i] = "0123456789ABCDEF"[v & 15];
        v >>= 4;
    }
    b[4] = 0;
    out(b);
}

static void check(int ok, const char *name)
{
    out(ok ? "PASS " : "FAIL ");
    out(name);
    out("\r\n");
    if (!ok)
        failures++;
}

typedef struct { const char *name; WORD value; } refval;
static const refval ref[] = {
#include "tk3ref.h"
    { 0, 0 }
};

static int same(const char *a, const char *b)
{
    while (*a && *a == *b) {
        a++;
        b++;
    }
    return *a == *b;
}

static void obs(const char *name, WORD v)
{
    WORD i;
    out("OBS ");
    out(name);
    out("=");
    outhex(v);
    out("\r\n");
    for (i = 0; ref[i].name; i++)
        if (same(ref[i].name, name)) {
            if (v != ref[i].value) {
                out("FAIL ");
                out(name);
                out(" (Windows 3.1: ");
                outhex(ref[i].value);
                out(")\r\n");
                failures++;
            }
            break;
        }
}

/* an address or other layout-dependent value: not in tk3ref.h */
static void obs_env(const char *name, WORD v)
{
    out("OBS ~");
    out(name);
    out("=");
    outhex(v);
    out("\r\n");
}

/* name with a 4-digit hex suffix: "prefix(xxxx)" */
static void obsn(const char *prefix, WORD n, WORD v)
{
    char name[64];
    WORD i = 0, d;
    while (*prefix && i < sizeof name - 7)
        name[i++] = *prefix++;
    name[i++] = '(';
    for (d = 0; d < 4; d++)
        name[i++] = "0123456789ABCDEF"[(n >> (12 - 4 * d)) & 15];
    name[i++] = ')';
    name[i] = 0;
    obs(name, v);
}

static WORD FAR *w_at(WORD seg, WORD off)
{
    return (WORD FAR *)MAKELP(seg, off);
}

static void dump(const char *name, WORD seg, WORD off, WORD nwords)
{
    WORD i;
    for (i = 0; i < nwords; i++) {
        if ((i & 7) == 0) {
            if (i)
                out("\r\n");
            out("OBS ");
            out(name);
            out("@");
            outhex(off + 2 * i);
            out("=");
        } else {
            out(" ");
        }
        outhex(*w_at(seg, off + 2 * i));
    }
    out("\r\n");
}

static WORD ds(void)
{
    return entry_regs[E_DS];
}

static WORD hinst(void)
{
    return entry_regs[E_DS] & ~1u;
}

/* ---- the heap InitTask made ---- */
static void probe_initial(void)
{
    WORD FAR *inst = w_at(ds(), 0);
    WORD i, ph;

    obs("entry.BX(stack)", entry_regs[E_BX]);
    obs("entry.CX(heap)", entry_regs[E_CX]);
    obs_env("entry.SP", entry_regs[E_SP]);
    obs_env("GlobalSize(hInstance)", (WORD)GlobalSize(hinst()));
    for (i = 0; i < 8; i++)
        if (i == 0 || i == 1 || i == 2 || i == 4)
            obsn("inst", 2 * i, inst[i]);
        else
            obs_env(i == 3 ? "inst(0006)" : i == 5 ? "inst(000A)" :
                    i == 6 ? "inst(000C)" : "inst(000E)", inst[i]);
    ph = inst[3];
    obs_env("pHeap-entry.SP", ph - entry_regs[E_SP]);
    /* from the end of the stack through the heap header */
    dump("initial", ds(), entry_regs[E_SP], 0x40);
    if (ph > entry_regs[E_SP] + 0x80)
        dump("pheap", ds(), ph - 0x10, 0x30);
    dump("segend", ds(), (WORD)GlobalSize(hinst()) - 0x20, 0x10);
    obs("LocalCompact(0) initial", LocalCompact(0));
    check(ph == (WORD)GlobalSize(hinst()) - entry_regs[E_CX] + 0x10,
          "heap is the last CX bytes of DGROUP, pHeap at its start + 10h");
    check(*w_at(ds(), ph + 6) == ph - 0x10 &&
          *w_at(ds(), ph + 0x0A) == (WORD)GlobalSize(hinst()) - 0x0C,
          "heap info: first and last sentinel");
}

/* ---- fixed and moveable blocks ---- */
static void probe_blocks(void)
{
    HLOCAL h1, h2;
    WORD p2, i;

    h1 = LocalAlloc(LMEM_FIXED, 10);
    obs_env("fixed h", (WORD)h1);
    obs_env("fixed h-entry.SP", (WORD)h1 - entry_regs[E_SP]);
    dump("fixed", ds(), (WORD)h1 - 8, 16);
    obs("fixed LocalSize", LocalSize(h1));
    obs("fixed LocalFlags", LocalFlags(h1));
    obs_env("fixed LocalLock", (WORD)LocalLock(h1));
    obs("fixed LocalFlags(locked)", LocalFlags(h1));
    obs("fixed LocalUnlock", LocalUnlock(h1));
    obs("fixed LocalUnlock again", LocalUnlock(h1));
    obs_env("fixed LocalHandle", (WORD)LocalHandle((UINT)h1));
    obs("fixed LocalHandle(h+2)", (WORD)LocalHandle((UINT)h1 + 2));
    check((WORD)h1 == *w_at(ds(), 6) + 0x30, "first fixed block follows the heap info");
    check((WORD)LocalLock(h1) == (WORD)h1 && LocalHandle((UINT)h1) == h1,
          "fixed: handle == pointer == LocalLock == LocalHandle");

    h2 = LocalAlloc(LMEM_MOVEABLE, 10);
    obs_env("moveable h", (WORD)h2);
    obs_env("moveable h-entry.SP", (WORD)h2 - entry_regs[E_SP]);
    dump("mhandle", ds(), (WORD)h2 - 8, 8);
    obs("moveable LocalFlags", LocalFlags(h2));
    dump("info(moveable)", ds(), *w_at(ds(), 6), 0x16);
    p2 = (WORD)LocalLock(h2);
    obs_env("moveable LocalLock", p2);
    obs("moveable LocalLock-h", p2 - (WORD)h2);
    dump("mhandle(locked)", ds(), (WORD)h2 - 8, 8);
    dump("mblock", ds(), p2 - 8, 16);
    obs("moveable LocalFlags(locked)", LocalFlags(h2));
    LocalLock(h2);
    obs("moveable LocalFlags(locked 2)", LocalFlags(h2));
    obs("moveable LocalSize", LocalSize(h2));
    obs_env("moveable LocalHandle(p)", (WORD)LocalHandle(p2));
    obs("moveable LocalHandle(h)", (WORD)LocalHandle((UINT)h2));
    check(((WORD)h2 & 3) == 2 && *(WORD NEAR *)h2 == p2 && *(WORD NEAR *)(p2 - 2) == (WORD)h2,
          "moveable: handle entry -> data, data - 2 -> handle");
    check(p2 == (WORD)GlobalSize(hinst()) - 0x16, "moveable block at the top of the heap");
    check(LocalHandle(p2) == h2, "LocalHandle(moveable data) == handle");
    LocalLock(h2);
    obs("moveable LocalUnlock 3->", LocalUnlock(h2));
    obs("moveable LocalUnlock 1", LocalUnlock(h2));
    obs("moveable LocalUnlock 2", LocalUnlock(h2));
    obs("moveable LocalUnlock 3", LocalUnlock(h2));
    obs("moveable LocalFlags(unlocked)", LocalFlags(h2));
    for (i = 0; i < 300; i++)
        LocalLock(h2);
    obs("moveable LocalFlags(300 locks)", LocalFlags(h2));
    for (i = 0; i < 300; i++)
        LocalUnlock(h2);
    obs("moveable LocalFlags(300 unlocks)", LocalFlags(h2));

    obs("LocalFree(fixed)", (WORD)LocalFree(h1));
    obs("LocalFree(moveable)", (WORD)LocalFree(h2));
    dump("mhandle(freed)", ds(), (WORD)h2 - 8, 8);
    obs("LocalFree(0)", (WORD)LocalFree(0));
    dump("info(freed)", ds(), *w_at(ds(), 6), 0x16);
    obs("LocalCompact(0) after free", LocalCompact(0));
}

/* ---- sizes, spacing ---- */
static void probe_sizes(void)
{
    static const WORD sizes[] = { 0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 15, 16, 17,
                                  31, 32, 33, 100 };
    HLOCAL h[sizeof sizes / sizeof sizes[0]];
    WORD k, n = sizeof sizes / sizeof sizes[0];

    for (k = 0; k < n; k++) {
        h[k] = LocalAlloc(LMEM_FIXED, sizes[k]);
        obsn("fixed LocalSize", sizes[k], LocalSize(h[k]));
        if (k)
            if (k == 1)
                obs_env("fixed spacing after(0000)", (WORD)h[k] - (WORD)h[k - 1]);
            else
                obsn("fixed spacing after", sizes[k - 1], (WORD)h[k] - (WORD)h[k - 1]);
    }
    obs("fixed(0) h", (WORD)h[0]);
    for (k = 0; k < n; k++)
        LocalFree(h[k]);
    for (k = 0; k < n; k++) {
        h[k] = LocalAlloc(LMEM_MOVEABLE, sizes[k]);
        obsn("moveable LocalSize", sizes[k], LocalSize(h[k]));
        if (k)
            obsn("moveable handle spacing", k, (WORD)h[k] - (WORD)h[k - 1]);
    }
    obs_env("moveable(0) h", (WORD)h[0]);
    check(((WORD)h[0] & 3) == 2 && *(WORD NEAR *)h[0] == 0, "LocalAlloc(moveable, 0): handle, no data");
    obs("moveable(0) LocalFlags", LocalFlags(h[0]));
    obs("moveable(0) LocalLock", (WORD)LocalLock(h[0]));
    dump("moveable(0) handle", ds(), (WORD)h[0], 2);
    for (k = 0; k < n; k++)
        obsn("moveable(n) LocalFree", k, (WORD)LocalFree(h[k]));
}

/* ---- discardable blocks ---- */
static void probe_discard(void)
{
    HLOCAL h;
    WORD p;

    h = LocalAlloc(LMEM_MOVEABLE | LMEM_DISCARDABLE, 10);
    obs_env("discardable h", (WORD)h);
    obs("discardable LocalFlags", LocalFlags(h));
    dump("dhandle", ds(), (WORD)h, 2);
    p = (WORD)LocalLock(h);
    obs("discardable LocalFlags(locked)", LocalFlags(h));
    dump("dhandle(locked)", ds(), (WORD)h, 2);
    dump("dblock", ds(), p - 4, 4);
    obs("discardable LocalUnlock", LocalUnlock(h));
    obs("LocalDiscard ret==h", LocalReAlloc(h, 0, LMEM_MOVEABLE) == h);
    obs("discarded LocalFlags", LocalFlags(h));
    obs("discarded LocalSize", LocalSize(h));
    obs("discarded LocalLock", (WORD)LocalLock(h));
    dump("dhandle(discarded)", ds(), (WORD)h, 2);
    obs("discarded ReAlloc(20) ret==h", LocalReAlloc(h, 20, LMEM_MOVEABLE) == h);
    obs("discarded ReAlloc LocalFlags", LocalFlags(h));
    obs("discarded ReAlloc LocalSize", LocalSize(h));
    LocalFree(h);

    /* plain moveable: can it be discarded? */
    h = LocalAlloc(LMEM_MOVEABLE, 10);
    obs("LocalDiscard(nondiscardable) ret==h", LocalReAlloc(h, 0, LMEM_MOVEABLE) == h);
    obs("nondiscardable LocalFlags", LocalFlags(h));
    obs("nondiscardable LocalSize", LocalSize(h));
    LocalFree(h);

    /* LMEM_MODIFY */
    h = LocalAlloc(LMEM_MOVEABLE, 10);
    obs("ReAlloc(MODIFY|DISCARDABLE) ret==h",
        LocalReAlloc(h, 0, LMEM_MODIFY | LMEM_DISCARDABLE | LMEM_MOVEABLE) == h);
    obs("MODIFY LocalFlags", LocalFlags(h));
    LocalFree(h);
    h = LocalAlloc(LMEM_FIXED, 10);
    obs("fixed ReAlloc(MODIFY|MOVEABLE) ret==h", LocalReAlloc(h, 0, LMEM_MODIFY | LMEM_MOVEABLE) == h);
    obs("fixed MODIFY LocalFlags", LocalFlags(h));
    LocalFree(h);
}

/* ---- ZEROINIT and leftover contents ---- */
static void probe_zero(void)
{
    HLOCAL h;
    BYTE NEAR *p;
    WORD i, nz;

    h = LocalAlloc(LMEM_FIXED, 32);
    p = (BYTE NEAR *)h;
    for (i = 0; i < 32; i++)
        p[i] = 0xAA;
    LocalFree(h);
    h = LocalAlloc(LMEM_FIXED, 32);
    obs("realloc same address", (BYTE NEAR *)h == p);
    for (nz = i = 0; i < 32; i++)
        nz += ((BYTE NEAR *)h)[i] != 0;
    obs("fixed nonzero bytes (no ZEROINIT)", nz);
    for (i = 0; i < 32; i++)
        ((BYTE NEAR *)h)[i] = 0xAA;
    LocalFree(h);
    h = LocalAlloc(LPTR, 32);
    for (nz = i = 0; i < 32; i++)
        nz += ((BYTE NEAR *)h)[i] != 0;
    obs("LPTR nonzero bytes", nz);
    check(nz == 0, "LMEM_ZEROINIT block is zero");
    LocalFree(h);
}

/* ---- LocalReAlloc ---- */
static void probe_realloc(void)
{
    HLOCAL a, b, m, r;
    WORD i, ok, p, q;

    a = LocalAlloc(LMEM_FIXED, 16);
    for (i = 0; i < 16; i++)
        ((BYTE NEAR *)a)[i] = (BYTE)i;
    r = LocalReAlloc(a, 8, 0);
    obs("fixed shrink 16->8 same", r == a);
    obs("fixed shrink LocalSize", LocalSize(r));
    r = LocalReAlloc(a, 64, 0);
    obs("fixed grow 8->64 (free after) same", r == a);
    obs_env("fixed grow ret", (WORD)r);
    obs("fixed grow LocalSize", LocalSize(a));
    b = LocalAlloc(LMEM_FIXED, 16);
    obs("obstacle after a", (WORD)b - (WORD)a);
    r = LocalReAlloc(a, 200, 0);
    obs("fixed grow blocked, flags 0", (WORD)r);
    obs("fixed grow blocked LocalSize", LocalSize(a));
    r = LocalReAlloc(a, 200, LMEM_MOVEABLE);
    obs("fixed grow blocked, LMEM_MOVEABLE moved", r != a && r != 0);
    obs("fixed grow blocked, LMEM_MOVEABLE ret-a", (WORD)r - (WORD)a);
    if (r) {
        ok = 1;
        for (i = 0; i < 8; i++)
            if (((BYTE NEAR *)r)[i] != (BYTE)i)
                ok = 0;
        obs("fixed moved contents kept", ok);
        obs("fixed moved LocalSize", LocalSize(r));
        obs("old a LocalSize", LocalSize(a));
        a = r;
    }
    LocalFree(a);
    LocalFree(b);

    m = LocalAlloc(LMEM_MOVEABLE, 16);
    p = (WORD)LocalLock(m);
    for (i = 0; i < 16; i++)
        ((BYTE NEAR *)p)[i] = (BYTE)(i + 1);
    b = LocalAlloc(LMEM_FIXED, 16);
    obs("m obstacle-p", (WORD)b - p);
    r = LocalReAlloc(m, 300, LMEM_MOVEABLE);
    obs("moveable grow while locked ret==h", r == m);
    obs("moveable grow while locked LocalSize", LocalSize(m));
    obs("moveable grow while locked LocalFlags", LocalFlags(m));
    obs("moveable grow while locked [h]-p", *(WORD NEAR *)m - p);
    LocalUnlock(m);
    r = LocalReAlloc(m, 300, LMEM_MOVEABLE);
    obs("moveable grow unlocked ret==h", r == m);
    q = (WORD)LocalLock(m);
    obs("moveable grow moved", q != p);
    obs("moveable grow q-p", q - p);
    ok = 1;
    for (i = 0; i < 16; i++)
        if (((BYTE NEAR *)q)[i] != (BYTE)(i + 1))
            ok = 0;
    obs("moveable moved contents kept", ok);
    obs("moveable grown LocalSize", LocalSize(m));
    LocalUnlock(m);
    p = LocalSize(m);
    r = LocalReAlloc(m, 400, LMEM_MOVEABLE | LMEM_ZEROINIT);
    q = (WORD)LocalLock(m);
    ok = 1;
    for (i = p; i < 400; i++)
        if (((BYTE NEAR *)q)[i])
            ok = 0;
    obs("ReAlloc ZEROINIT grown part zero", ok);
    LocalUnlock(m);
    r = LocalReAlloc(m, 10, 0);
    obs("moveable shrink flags 0 ret==h", r == m);
    obs("moveable shrink LocalSize", LocalSize(m));
    LocalFree(m);
    LocalFree(b);
}

/* ---- large requests, growth, compaction ---- */
static void probe_limits(void)
{
    HLOCAL h, g;
    WORD gs = (WORD)GlobalSize(hinst());

    obs("LocalCompact(0)", LocalCompact(0));
    obs("LocalCompact(FFFF)", LocalCompact(0xFFFF));
    h = LocalAlloc(LMEM_FIXED, 3000);
    obs("LocalAlloc 3000 > heap", (WORD)h != 0);
    obs_env("GlobalSize(hInstance) after 3000", (WORD)GlobalSize(hinst()));
    obs("GlobalSize grew by", (WORD)GlobalSize(hinst()) - gs);
    obs("LocalCompact(0) after 3000", LocalCompact(0));
    dump("segend2", ds(), (WORD)GlobalSize(hinst()) - 0x20, 0x10);
    g = LocalAlloc(LMEM_FIXED, 0xFFF0);
    obs("LocalAlloc FFF0", (WORD)g);
    g = LocalAlloc(LMEM_MOVEABLE, 0xFFF0);
    obs("LocalAlloc moveable FFF0", (WORD)g);
    obs("LocalFree(3000)", (WORD)LocalFree(h));
    obs("LocalCompact(0) end", LocalCompact(0));
    obs_env("GlobalSize(hInstance) end", (WORD)GlobalSize(hinst()));
    obs("LocalFree(bogus 1235)", (WORD)LocalFree((HLOCAL)0x1235));
    obs("LocalSize(bogus 1235)", LocalSize((HLOCAL)0x1235));
}

/* ---- LocalInit on a global block ---- */
static void probe_init(void)
{
    HGLOBAL g;
    WORD sel, r;

    g = GlobalAlloc(GMEM_FIXED | GMEM_ZEROINIT, 512);
    sel = (WORD)((DWORD)GlobalLock(g) >> 16);
    r = LocalInit(sel, 16, 511);
    obs("LocalInit(sel,16,511)", r);
    obs("LocalInit pHeap", *w_at(sel, 6));
    dump("linit", sel, 0, 0x40);
    dump("linit.end", sel, 0x1E0, 0x10);
    GlobalUnlock(g);
    GlobalFree(g);

    g = GlobalAlloc(GMEM_FIXED | GMEM_ZEROINIT, 512);
    sel = (WORD)((DWORD)GlobalLock(g) >> 16);
    r = LocalInit(sel, 0, 256);
    obs("LocalInit(sel,0,256)", r);
    obs("LocalInit0 pHeap", *w_at(sel, 6));
    dump("linit0", sel, 0, 0x40);
    dump("linit0.end", sel, 0xE0, 0x30);
    GlobalUnlock(g);
    GlobalFree(g);
}

extern DWORD __cdecl call_ds(WORD ds, void FAR *fn, WORD nargs, WORD a1, WORD a2, WORD a3);

/* ---- moveable placement and compaction ---- */
static void probe_compact(void)
{
    HLOCAL m1, m2, m3, f;
    WORD before = LocalCompact(0);

    m1 = LocalAlloc(LMEM_MOVEABLE, 40);
    m2 = LocalAlloc(LMEM_MOVEABLE, 40);
    m3 = LocalAlloc(LMEM_MOVEABLE, 40);
    obs("m1 data-GlobalSize", *(WORD NEAR *)m1 - (WORD)GlobalSize(hinst()));
    obs("m2 data-m1", *(WORD NEAR *)m2 - *(WORD NEAR *)m1);
    obs("m3 data-m1", *(WORD NEAR *)m3 - *(WORD NEAR *)m1);
    f = LocalAlloc(LMEM_FIXED, 40);
    obs("fixed f-pHeap", (WORD)f - *w_at(ds(), 6));
    LocalFree(m2);
    obs("m3 data-m1 after free m2", *(WORD NEAR *)m3 - *(WORD NEAR *)m1);
    obs("LocalCompact(0) hole", LocalCompact(0));
    obs("m3 data-m1 after compact", *(WORD NEAR *)m3 - *(WORD NEAR *)m1);
    obs("m1 data-GlobalSize after compact", *(WORD NEAR *)m1 - (WORD)GlobalSize(hinst()));
    m2 = LocalAlloc(LMEM_MOVEABLE, 40);
    obs("m2 again data-m1", *(WORD NEAR *)m2 - *(WORD NEAR *)m1);
    LocalFree(m2);
    LocalLock(m1);
    obs("LocalCompact(0) m1 locked", LocalCompact(0));
    obs("m3 data-m1 m1 locked", *(WORD NEAR *)m3 - *(WORD NEAR *)m1);
    LocalUnlock(m1);
    /* a fixed request bigger than any hole: compaction moves moveables */
    LocalFree(f);
    LocalFree(m1);
    LocalFree(m3);
    obs("LocalCompact(0) all freed", LocalCompact(0));
    obs("LocalCompact(0) before", before);
    m1 = LocalAlloc(LMEM_MOVEABLE | LMEM_DISCARDABLE, 100);
    obs("discardable m1 data-GlobalSize", *(WORD NEAR *)m1 - (WORD)GlobalSize(hinst()));
    obs("LocalCompact(0) with discardable", LocalCompact(0));
    obs("discardable after LocalCompact(0) flags", LocalFlags(m1));
    obs("LocalCompact(FFFF) with discardable", LocalCompact(0xFFFF));
    obs("discardable after LocalCompact(FFFF) flags", LocalFlags(m1));
    LocalFree(m1);
    dump("info(end)", ds(), *w_at(ds(), 6), 0x16);
}

/* ---- heap growth and LocalInit details on other segments ---- */
static void probe_other(void)
{
    static const WORD sizes[] = { 100, 300, 400, 401, 402, 404, 1000, 2000 };
    static const WORD starts[] = { 16, 17, 18, 19, 20 };
    HGLOBAL g;
    WORD sel, k, h;

    for (k = 0; k < sizeof sizes / sizeof sizes[0]; k++) {
        g = GlobalAlloc(GMEM_MOVEABLE | GMEM_ZEROINIT, 512);
        sel = (WORD)((DWORD)GlobalLock(g) >> 16);
        LocalInit(sel, 16, 511);
        obsn("grow: LocalCompact before", sizes[k],
             (WORD)call_ds(sel, (void FAR *)LocalCompact, 1, 0, 0, 0));
        h = (WORD)call_ds(sel, (void FAR *)LocalAlloc, 2, LMEM_FIXED, sizes[k], 0);
        obsn("grow: LocalAlloc", sizes[k], h);
        obsn("grow: GlobalSize", sizes[k], (WORD)GlobalSize(g));
        obsn("grow: LocalCompact after", sizes[k],
             (WORD)call_ds(sel, (void FAR *)LocalCompact, 1, 0, 0, 0));
        obsn("grow: last", sizes[k], *w_at(sel, *w_at(sel, 6) + 0x0A));
        if (k == 0)
            dump("grow100.info", sel, *w_at(sel, 6), 0x16);
        h = (WORD)call_ds(sel, (void FAR *)LocalAlloc, 2, LMEM_MOVEABLE, sizes[k], 0);
        obsn("grow: moveable LocalAlloc", sizes[k], h);
        obsn("grow: moveable GlobalSize", sizes[k], (WORD)GlobalSize(g));
        GlobalUnlock(g);
        GlobalFree(g);
    }
    /* a fixed segment */
    g = GlobalAlloc(GMEM_FIXED | GMEM_ZEROINIT, 512);
    sel = (WORD)((DWORD)GlobalLock(g) >> 16);
    LocalInit(sel, 16, 511);
    h = (WORD)call_ds(sel, (void FAR *)LocalAlloc, 2, LMEM_FIXED, 1000, 0);
    obs("grow fixed seg: LocalAlloc 1000", h);
    obs("grow fixed seg: GlobalSize", (WORD)GlobalSize(g));
    GlobalUnlock(g);
    GlobalFree(g);

    for (k = 0; k < sizeof starts / sizeof starts[0]; k++) {
        g = GlobalAlloc(GMEM_FIXED | GMEM_ZEROINIT, 512);
        sel = (WORD)((DWORD)GlobalLock(g) >> 16);
        obsn("LocalInit(start,509)", starts[k], LocalInit(sel, starts[k], 509));
        obsn("LocalInit pHeap", starts[k], *w_at(sel, 6));
        obsn("LocalInit first", starts[k], *w_at(sel, *w_at(sel, 6) + 6));
        obsn("LocalInit last", starts[k], *w_at(sel, *w_at(sel, 6) + 0x0A));
        obsn("LocalInit minsize", starts[k], *w_at(sel, *w_at(sel, 6) + 0x26));
        GlobalUnlock(g);
        GlobalFree(g);
    }
}

static WORD new_heap(HGLOBAL *g)
{
    WORD sel;
    *g = GlobalAlloc(GMEM_MOVEABLE | GMEM_ZEROINIT, 512);
    sel = (WORD)((DWORD)GlobalLock(*g) >> 16);
    LocalInit(sel, 16, 511);
    return sel;
}

static void drop_heap(HGLOBAL g)
{
    GlobalUnlock(g);
    GlobalFree(g);
}

#define LA(sel, f, n) ((WORD)call_ds(sel, (void FAR *)LocalAlloc, 2, f, n, 0))
#define LC(sel, n) ((WORD)call_ds(sel, (void FAR *)LocalCompact, 1, n, 0, 0))

/* ---- heap growth amounts, handle tables, odd requests ---- */
static void probe_more(void)
{
    HGLOBAL g;
    WORD sel, n, h, i, ph, t1, t2;
    HLOCAL m[40];

    /* growth: one fixed request bigger than the free space */
    for (n = 1000; n < 1032; n++) {
        sel = new_heap(&g);
        h = LA(sel, LMEM_FIXED, n);
        obsn("sweep fixed GlobalSize", n, h ? (WORD)GlobalSize(g) : 0);
        drop_heap(g);
    }
    /* growth: moveable request with the handle table already there */
    for (n = 1000; n < 1032; n += 2) {
        sel = new_heap(&g);
        LA(sel, LMEM_MOVEABLE, 2);
        obsn("sweep moveable free before", n, LC(sel, 0));
        h = LA(sel, LMEM_MOVEABLE, n);
        obsn("sweep moveable GlobalSize", n, h ? (WORD)GlobalSize(g) : 0);
        drop_heap(g);
    }
    /* growth with plenty of free space but just not enough */
    sel = new_heap(&g);
    obs("sweep big-free before", LC(sel, 0));
    h = LA(sel, LMEM_FIXED, LC(sel, 0) + 2);
    obs("sweep big-free LocalAlloc", h);
    obs("sweep big-free GlobalSize", (WORD)GlobalSize(g));
    obs("sweep big-free after", LC(sel, 0));
    drop_heap(g);

    /* the dislevel/ncompact bytes (+0E) */
    sel = new_heap(&g);
    ph = *w_at(sel, 6);
    obs("+0E after LocalInit", *w_at(sel, ph + 0x0E));
    LC(sel, 0);
    obs("+0E after LocalCompact(0)", *w_at(sel, ph + 0x0E));
    LA(sel, LMEM_FIXED, 10);
    obs("+0E after LocalAlloc fixed", *w_at(sel, ph + 0x0E));
    LC(sel, 0);
    obs("+0E after LocalCompact(0) 2", *w_at(sel, ph + 0x0E));
    LC(sel, 0xFFFF);
    obs("+0E after LocalCompact(FFFF)", *w_at(sel, ph + 0x0E));
    LA(sel, LMEM_FIXED, 10);
    obs("+0E after LocalAlloc fixed 2", *w_at(sel, ph + 0x0E));
    drop_heap(g);

    /* handle table overflow, in the task's own heap */
    ph = *w_at(ds(), 6);
    t1 = *w_at(ds(), ph + 0x14);
    obs_env("ht: htable before", t1);
    obs_env("ht: hfree before", *w_at(ds(), ph + 0x16));
    obs("ht: htable before-pHeap", t1 - ph);
    for (i = 0; i < 40; i++)
        m[i] = LocalAlloc(LMEM_MOVEABLE, 2);
    t2 = *w_at(ds(), ph + 0x14);
    obs_env("ht: htable after 40", t2);
    obs_env("ht: hfree after 40", *w_at(ds(), ph + 0x16));
    obs("ht: table2 count", *w_at(ds(), t2));
    obs("ht: table2 link==table1", *w_at(ds(), t2 + 2 + 4 * 0x20) == t1);
    obs("ht: hfree after 40-table2", *w_at(ds(), ph + 0x16) - t2);
    obs("ht: handle 32-table2", (WORD)m[32] - t2);
    obs("ht: items", *w_at(ds(), ph + 4));
    for (i = 0; i < 40; i++)
        obsn("ht: handle-first", i, (WORD)m[i] - (WORD)m[0]);
    dump("ht: table2", ds(), *w_at(ds(), ph + 0x14) - 4, 0x10);
    dump("ht: table2 end", ds(), *w_at(ds(), ph + 0x14) + 0x7C, 0x6);
    for (i = 0; i < 40; i++)
        LocalFree(m[i]);
    obs_env("ht: htable freed", *w_at(ds(), ph + 0x14));
    obs_env("ht: hfree freed", *w_at(ds(), ph + 0x16));
    obs("ht: htable freed==table2", *w_at(ds(), ph + 0x14) == t2);
    obs("ht: hfree freed==last freed", *w_at(ds(), ph + 0x16) == (WORD)m[39]);
    obs("ht: items freed", *w_at(ds(), ph + 4));

    /* odd requests */
    h = (WORD)LocalAlloc(LMEM_MOVEABLE, 10);
    LocalLock((HLOCAL)h);
    obs("discard locked: ret", (WORD)LocalReAlloc((HLOCAL)h, 0, LMEM_MOVEABLE));
    obs("discard locked: flags", LocalFlags((HLOCAL)h));
    obs("LocalFree(locked): ret", (WORD)LocalFree((HLOCAL)h));
    obs("LocalFree(locked): [h+2]", *w_at(ds(), h + 2));
    obs("LocalFlags(freed)", LocalFlags((HLOCAL)h));
    obs("LocalLock(freed)", (WORD)LocalLock((HLOCAL)h));
    obs("LocalSize(freed)", LocalSize((HLOCAL)h));
    obs("LocalFree(freed)", (WORD)LocalFree((HLOCAL)h));
    h = (WORD)LocalAlloc(LMEM_MOVEABLE, 10);
    obs("moveable ReAlloc(h,0,0)", (WORD)LocalReAlloc((HLOCAL)h, 0, 0));
    obs("moveable ReAlloc(h,0,0) flags", LocalFlags((HLOCAL)h));
    obs("moveable ReAlloc(h,0,0) size", LocalSize((HLOCAL)h));
    LocalReAlloc((HLOCAL)h, 0, LMEM_MOVEABLE);
    obs("discarded ReAlloc(h,8,0) ret==h", (WORD)LocalReAlloc((HLOCAL)h, 8, 0) == h);
    obs("discarded ReAlloc(h,8,0) flags", LocalFlags((HLOCAL)h));
    obs("discarded ReAlloc(h,8,0) size", LocalSize((HLOCAL)h));
    LocalFree((HLOCAL)h);
    h = (WORD)LocalAlloc(LMEM_FIXED, 10);
    obs("fixed ReAlloc(h,0,0)", (WORD)LocalReAlloc((HLOCAL)h, 0, 0));
    obs("fixed ReAlloc(h,0,0) size", LocalSize((HLOCAL)h));
    obs("fixed ReAlloc(h,0,MOVEABLE)", (WORD)LocalReAlloc((HLOCAL)h, 0, LMEM_MOVEABLE));
    obs("fixed ReAlloc(h,0,MOVEABLE) size", LocalSize((HLOCAL)h));
    LocalFree((HLOCAL)h);
    h = (WORD)LocalAlloc(LMEM_FIXED | LMEM_DISCARDABLE, 10);
    obs("fixed|DISCARDABLE flags", LocalFlags((HLOCAL)h));
    obs("fixed|DISCARDABLE h&3", h & 3);
    LocalFree((HLOCAL)h);
    h = (WORD)LocalAlloc(LMEM_MOVEABLE | 0x0100, 10);
    obs("moveable|0100 flags", LocalFlags((HLOCAL)h));
    LocalFree((HLOCAL)h);
    h = (WORD)LocalAlloc(LMEM_MOVEABLE | LMEM_DISCARDABLE, 10);
    LocalLock((HLOCAL)h);
    obs("LocalCompact(FFFF) discardable locked", LocalCompact(0xFFFF));
    obs("discardable locked flags after", LocalFlags((HLOCAL)h));
    LocalUnlock((HLOCAL)h);
    LocalFree((HLOCAL)h);
    h = (WORD)LocalAlloc(LMEM_MOVEABLE, 10);
    i = *(WORD NEAR *)h;
    obs("ReAlloc moveable 10->11 same place", *(WORD NEAR *)LocalReAlloc((HLOCAL)h, 11, 0) == i);
    obs("ReAlloc moveable 10->20 flags 0 ret", (WORD)LocalReAlloc((HLOCAL)h, 20, 0) == h);
    obs("ReAlloc moveable 10->20 moved", *(WORD NEAR *)h - i);
    LocalFree((HLOCAL)h);
}

int __cdecl tmain(void)
{
    out("TK3 start\r\n");
    probe_initial();
    probe_blocks();
    probe_sizes();
    probe_discard();
    probe_zero();
    probe_realloc();
    probe_limits();
    probe_init();
    probe_compact();
    probe_other();
    probe_more();
    out("TK3 done, failures=");
    outhex((WORD)failures);
    out("\r\n");
    return failures;
}
