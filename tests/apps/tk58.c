/*
 * tk58.c - the global heap of Windows 3.1's KRNL386, black-box: the
 * Burgermaster's header, the arenas, the selector table, and what each
 * allocation does to them; TOOLHELP's view of the same blocks. The
 * counterpart of TK19 (KRNL286's heap, tests/apps/tk19.c).
 *
 * Layouts probed: Pietrek, "Windows Internals" (1993), ch. 2, Tables 2-2
 * to 2-4 (the 386 HeapInfo/GlobalInfo and the KRNL386 arena) and Schulman
 * et al., "Undocumented Windows" (1992), ch. 5 (KRNL386 Global Arena,
 * THHOOK). Run as the shell: tests/refrun.sh TK58.EXE /S and /3 (KRNL386
 * in both modes on a 386); tests/dropin.sh -3 for w16's KRNL386. Under a
 * KRNL286 heap (Burgermaster word 6 a selector of limit 0Fh) it only says
 * so.
 *
 * The Burgermaster is more than 64 KB under KRNL386 (THHOOK 18h, the
 * selector table's offset, is a dword above 10000h), so arenas are read
 * through a scratch selector based at the Burgermaster's base + offset.
 */
#include <windows.h>
#include <toolhelp.h>

#define TK_REF "tk58ref.h"
#include "tklog.h"

extern DWORD FAR PASCAL K_GLOBALMASTERHANDLE(void);
extern DWORD FAR PASCAL K_GETSELECTORBASE(WORD s);
extern WORD  FAR PASCAL K_SETSELECTORBASE(WORD s, DWORD b);
extern WORD  FAR PASCAL K_SETSELECTORLIMIT(WORD s, DWORD l);
extern WORD  FAR PASCAL K_ALLOCSELECTOR(WORD s);
extern WORD  FAR PASCAL K_FREESELECTOR(WORD s);
extern WORD  FAR PASCAL K_GLOBALFIX(WORD h);
extern WORD  FAR PASCAL K_GLOBALUNFIX(WORD h);
extern WORD  FAR PASCAL K_GLOBALPAGELOCK(WORD h);
extern WORD  FAR PASCAL K_GLOBALPAGEUNLOCK(WORD h);
extern DWORD FAR PASCAL K_GETCURRENTPDB(void);
extern DWORD FAR PASCAL K_GLOBALDOSALLOC(DWORD n);

typedef BOOL (FAR PASCAL *PGEH)(GLOBALENTRY FAR *, HGLOBAL);

static WORD bm, sc, pdb;
static DWORD bmbase;
static WORD FAR *th;            /* THHOOK */

/* the KRNL386 arena (Pietrek, Table 2-4) */
typedef struct {
    DWORD next, prev, addr, size;
    WORD handle, owner;
    BYTE count, pglock, flags, selcount;
    DWORD lprev, lnext;
} ar386;

static void rd(DWORD off, void FAR *dst, WORD n)
{
    K_SETSELECTORBASE(sc, bmbase + off);
    hmemcpy(dst, MAKELP(sc, 0), n);
}

static DWORD rd32(DWORD off)
{
    DWORD v;
    rd(off, &v, 4);
    return v;
}

static WORD rd16(DWORD off)
{
    WORD v;
    rd(off, &v, 2);
    return v;
}

static void arena(DWORD off, ar386 *a)
{
    rd(off, a, sizeof *a);
}

static DWORD seltab_start(void) { return *(DWORD FAR *)&th[0x0C]; }

/* the selector table entry of selector s (Pietrek: (s & ~7) >> 1) */
static DWORD seltab(WORD s)
{
    return rd32(seltab_start() + ((s & ~7) >> 1));
}

static const char *nm(const char *p, const char *s)
{
    static char b[80];
    WORD i = 0;
    while (*p && i < 40)
        b[i++] = *p++;
    b[i++] = ' ';
    while (*s && i < sizeof b - 1)
        b[i++] = *s++;
    b[i] = 0;
    return b;
}

static void obs32(const char *name, DWORD v)
{
    char n[64];
    lstrcpy(n, name);
    obs(nm(n, "high"), HIWORD(v));
    obs(nm(n, "low"), LOWORD(v));
}

/* the arena of a block, by its handle: what each field says */
static void show(const char *what, HGLOBAL h)
{
    ar386 a;
    char n[64];
    WORD sel = h ? (WORD)h | 1 : 0;
    DWORD off = h ? seltab(sel) : 0;
    lstrcpy(n, what);
    obs(nm(n, "handle odd"), (WORD)h & 1);
    obs_env(nm(n, "handle"), (WORD)h);
    obs(nm(n, "GlobalHandle(sel)==h"), LOWORD(GlobalHandle(sel)) == (WORD)h);
    obs32(nm(n, "GlobalSize"), GlobalSize(h));
    obs(nm(n, "GlobalFlags"), GlobalFlags(h));
    obs(nm(n, "seltab entry nonzero"), off != 0);
    if (!off)
        return;
    if (GlobalFlags(h) & GMEM_DISCARDED) {
        /* no arena: the entry is the owner */
        obs(nm(n, "seltab entry==PDB"), off == pdb);
        return;
    }
    arena(off, &a);
    obs(nm(n, "arena offset<10000h"), off < 0x10000UL);
    obs(nm(n, "arena offset mod 20h"), (WORD)(off & 0x1F));
    obs(nm(n, "arena handle==h"), a.handle == (WORD)h);
    obs(nm(n, "arena handle odd"), a.handle & 1);
    obs(nm(n, "arena owner==PDB"), a.owner == pdb);
    obs_env(nm(n, "arena owner"), a.owner);
    obs(nm(n, "arena address==base"), a.addr == K_GETSELECTORBASE(sel));
    obs(nm(n, "arena size==GlobalSize"), a.size == GlobalSize(h));
    obs32(nm(n, "arena size"), a.size);
    obs(nm(n, "arena count"), a.count);
    obs(nm(n, "arena pglock"), a.pglock);
    obs(nm(n, "arena flags"), a.flags);
    obs(nm(n, "arena selcount"), a.selcount);
    obs(nm(n, "arena lprev==0"), a.lprev == 0);
    obs(nm(n, "arena lnext==0"), a.lnext == 0);
    obs(nm(n, "arena next.prev==it"), rd32(a.next + 4) == off);
    obs(nm(n, "arena prev.next==it"), rd32(a.prev) == off);
    obs(nm(n, "next arena address==address+size"), rd32(a.next + 8) == a.addr + a.size);
}

/* the whole list: links, address order, the sentinels, the kinds of
   arena, the free list (from the first arena's 1Ch) and the LRU ring */
static void walk(const char *tag)
{
    DWORD first = rd32(6), last = rd32(0x0A), off = first, prev = first;
    DWORD f, lru;
    WORD n = 0, nfree = 0, nnt = 0, nlru = 0, nfl = 0;
    WORD ok_link = 1, ok_adj = 1, ok_order = 1, ok_size = 1, ok_free = 1, ok_off = 1;
    ar386 a, b;
    char t[48];
    while (n < 8000) {
        arena(off, &a);
        n++;
        if (off & 0x1F)
            ok_off = 0;
        if (a.prev != prev && off != first)
            ok_link = 0;
        if (a.size & 0x1F)
            ok_size = 0;
        if (a.owner == 0xFFFC)
            nnt++;
        else if (a.owner == 0 && a.handle == 0)
            nfree++;
        if (a.next == off)
            break;
        arena(a.next, &b);
        if (b.addr < a.addr)
            ok_order = 0;
        if (a.owner != 0xFFFC && b.owner != 0xFFFC && b.addr != a.addr + a.size)
            ok_adj = 0;
        if (a.owner == 0 && a.handle == 0 && b.owner == 0 && b.handle == 0)
            ok_free = 0;
        prev = off;
        off = a.next;
    }
    lstrcpy(t, tag);
    obs(nm(t, "walk ends at last"), off == last);
    obs(nm(t, "count==BM count"), n == rd16(4));
    obs(nm(t, "links"), ok_link);
    obs(nm(t, "arenas 20h-aligned"), ok_off);
    obs(nm(t, "address order"), ok_order);
    obs(nm(t, "adjacent"), ok_adj);
    obs(nm(t, "sizes mod 20h"), ok_size);
    obs(nm(t, "no two free in a row"), ok_free);
    obs(nm(t, "free==BM free count"), nfree == rd16(0x2E));
    obs_env(nm(t, "arenas"), n);
    obs_env(nm(t, "not there"), nnt);
    /* the free list: first arena's 1Ch onwards */
    arena(first, &a);
    f = a.lnext;
    while (f && f != last && nfl < 8000) {
        arena(f, &b);
        if (b.owner || b.handle)
            break;
        nfl++;
        f = b.lnext;
    }
    obs(nm(t, "free list ends at last"), f == last);
    obs(nm(t, "free list count==BM free count"), nfl == rd16(0x2E));
    /* the LRU ring from BM 20h */
    lru = rd32(0x20);
    if (lru) {
        f = lru;
        do {
            arena(f, &b);
            nlru++;
            f = b.lnext;
        } while (f != lru && nlru < 8000);
    }
    obs(nm(t, "LRU ring count==BM LRU count"), nlru == rd16(0x24));
}

static void header(void)
{
    ar386 a;
    DWORD first = rd32(6), last = rd32(0x0A);
    obs("BM check", rd16(0));
    obs("BM freeze", rd16(2));
    obs32("BM first", first);
    obs("BM last>first", last > first);
    obs("BM ncompact/dislevel", rd16(0x0E));
    obs32("BM distotal", rd32(0x10));
    obs("BM htable", rd16(0x14));
    obs("BM hfree", rd16(0x16));
    obs("BM hdelta", rd16(0x18));
    obs("BM hexpand", rd16(0x1A));
    obs("BM pstats", rd16(0x1C));
    obs("BM lrulock", rd16(0x1E));
    obs("BM 30h", rd16(0x30));
    obs("BM 32h", rd16(0x32));
    obs("BM 34h", rd16(0x34));
    obs("BM 36h", rd16(0x36));
    obs("BM 38h", rd16(0x38));
    obs("BM 3Ah", rd16(0x3A));
    obs("BM 3Ch", rd16(0x3C));
    obs("BM 3Eh", rd16(0x3E));
    obs("BM 40h", rd16(0x40));
    obs("BM 44h", rd16(0x44));
    obs_env("BM count", rd16(4));
    obs_env("BM LRU count", rd16(0x24));
    obs_env("BM free count", rd16(0x2E));
    obs_env("BM reserve low", LOWORD(rd32(0x26)));
    obs_env("BM reserve high", HIWORD(rd32(0x26)));
    obs_env("BM disfence low", LOWORD(rd32(0x2A)));
    obs_env("BM disfence_hi", rd16(0x42));
    obs_env("THHOOK SelTableStart high", HIWORD(seltab_start()));
    obs_env("THHOOK SelTableStart low", LOWORD(seltab_start()));
    obs("THHOOK SelTableLen", th[0x0B]);
    obs("seltab end == BM end", seltab_start() + th[0x0B] == GetSelectorLimit(bm) + 1);
    obs("BM limit>FFFFh", GetSelectorLimit(bm) > 0xFFFFUL);
    obs_env("BM limit high", HIWORD(GetSelectorLimit(bm)));
    obs_env("BM limit low", LOWORD(GetSelectorLimit(bm)));
    obs("BM handle==selector", LOWORD(K_GLOBALMASTERHANDLE()) == bm);
    obs("BM seltab entry", seltab(bm) != 0);
    arena(first, &a);
    obs("first sentinel prev==itself", a.prev == first);
    obs("first sentinel owner", a.owner);
    obs("first sentinel handle", a.handle);
    obs32("first sentinel size", a.size);
    obs("first sentinel count", a.count);
    obs("first sentinel flags", a.flags);
    obs("first sentinel lprev==itself", a.lprev == first);
    obs32("first arena offset", first);
    arena(last, &a);
    obs("last sentinel next==itself", a.next == last);
    obs("last sentinel owner", a.owner);
    obs("last sentinel handle", a.handle);
    obs32("last sentinel size", a.size);
    obs("last sentinel count", a.count);
    obs("last sentinel lnext==itself", a.lnext == last);
    /* the arena after the first sentinel */
    arena(rd32(first), &a);
    obs_env("second arena owner", a.owner);
    obs("second arena handle==BM", a.handle == bm);
    obs("second arena address==BM base", a.addr == bmbase);
    obs("BM arena (seltab) == second", seltab(bm) == rd32(first));
    {
        ar386 b;
        arena(seltab(bm), &b);
        obs("BM arena owner", b.owner);
        obs("BM arena flags", b.flags);
        obs("BM arena size==limit+1", b.size == GetSelectorLimit(bm) + 1);
    }
}

static void toolhelp(const char *what, HGLOBAL h)
{
    static HINSTANCE thl;
    static PGEH geh;
    GLOBALENTRY ge;
    ar386 a;
    char n[64];
    if (!thl) {
        thl = LoadLibrary("TOOLHELP.DLL");
        if (thl > HINSTANCE_ERROR)
            geh = (PGEH)GetProcAddress(thl, "GlobalEntryHandle");
    }
    if (!geh)
        return;
    ge.dwSize = sizeof ge;
    lstrcpy(n, what);
    obs(nm(n, "TOOLHELP entry"), geh(&ge, h));
    arena(seltab((WORD)h | 1), &a);
    obs(nm(n, "TOOLHELP address==arena"), ge.dwAddress == a.addr);
    obs(nm(n, "TOOLHELP size==arena"), ge.dwBlockSize == a.size);
    obs(nm(n, "TOOLHELP hBlock==h"), ge.hBlock == h);
    obs(nm(n, "TOOLHELP wcLock"), ge.wcLock);
    obs(nm(n, "TOOLHELP wcPageLock"), ge.wcPageLock);
    obs(nm(n, "TOOLHELP wFlags"), ge.wFlags);
    obs(nm(n, "TOOLHELP wType"), ge.wType);
}

int __cdecl tmain(void)
{
    DWORD gm = K_GLOBALMASTERHANDLE();
    HGLOBAL hf1, hf0, hm, hd, hhuge, hdos;
    DWORD dos;
    WORD r1, r2;

    out("TK58 start\r\n");
    bm = HIWORD(gm);
    pdb = LOWORD(K_GETCURRENTPDB());
    th = (WORD FAR *)GetProcAddress(GetModuleHandle("KERNEL"), MAKEINTRESOURCE(332));
    if (GetSelectorLimit(*(WORD FAR *)MAKELP(bm, 6)) == 0xF &&
        *(WORD FAR *)MAKELP(bm, 8) != 0) {
        out("TK58: a KRNL286 heap, nothing probed\r\n");
        return 0;
    }
    bmbase = K_GETSELECTORBASE(bm);
    sc = K_ALLOCSELECTOR(bm);
    K_SETSELECTORLIMIT(sc, 0xFFFF);
    header();
    walk("start");

    hf1 = GlobalAlloc(GMEM_FIXED, 1);
    show("fixed 1", hf1);
    toolhelp("fixed 1", hf1);
    hf0 = GlobalAlloc(GMEM_FIXED, 0);
    obs("fixed 0 nonzero", hf0 != 0);
    if (hf0)
        show("fixed 0", hf0);
    hm = GlobalAlloc(GMEM_MOVEABLE, 100);
    show("moveable 100", hm);
    GlobalLock(hm);
    show("moveable 100 locked", hm);
    GlobalUnlock(hm);
    hd = GlobalAlloc(GMEM_MOVEABLE | GMEM_DISCARDABLE, 0x1000);
    show("discardable 1000h", hd);
    obs("BM LRU chain==discardable", rd32(0x20) == seltab((WORD)hd | 1));
    GlobalLock(hd);
    show("discardable 1000h locked", hd);
    toolhelp("discardable 1000h locked", hd);
    GlobalUnlock(hd);
    walk("allocated");

    r1 = K_GLOBALFIX(hf1);
    show("fixed 1 GlobalFix", hf1);
    K_GLOBALUNFIX(hf1);
    r1 = K_GLOBALPAGELOCK(hm);
    r2 = K_GLOBALPAGELOCK(hm);
    obs("GlobalPageLock moveable 1", r1);
    obs("GlobalPageLock moveable 2", r2);
    show("moveable 100 pagelocked twice", hm);
    toolhelp("moveable 100 pagelocked twice", hm);
    obs("GlobalPageUnlock 1", K_GLOBALPAGEUNLOCK(hm));
    obs("GlobalPageUnlock 2", K_GLOBALPAGEUNLOCK(hm));
    show("moveable 100 pageunlocked", hm);

    GlobalDiscard(hd);
    obs("discarded GlobalFlags", GlobalFlags(hd));
    obs("discarded seltab entry==PDB", seltab((WORD)hd | 1) == pdb);
    obs_env("discarded seltab entry", LOWORD(seltab((WORD)hd | 1)));
    obs("discarded LRU count==0", rd16(0x24) == 0 || rd32(0x20) != 0);
    walk("discarded");

    hhuge = GlobalAlloc(GMEM_MOVEABLE, 0x31230UL);
    show("huge 31230h", hhuge);
    toolhelp("huge 31230h", hhuge);
    obs("huge tile 1 seltab", LOWORD(seltab(((WORD)hhuge | 1) + 8)) != 0);
    obs("huge tile 1 seltab==arena", seltab(((WORD)hhuge | 1) + 8) == seltab((WORD)hhuge | 1));

    dos = K_GLOBALDOSALLOC(0x100);
    hdos = (HGLOBAL)LOWORD(dos);
    show("DOS 100h", hdos);

    GlobalFree(hhuge);
    GlobalFree(hdos);
    GlobalFree(hd);
    GlobalFree(hm);
    if (hf0)
        GlobalFree(hf0);
    GlobalFree(hf1);
    walk("freed");
    K_FREESELECTOR(sc);
    out("TK58 done\r\n");
    return 0;
}
