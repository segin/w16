/*
 * tk54.c - LocalNotify's procedure and LocalShrink, observed: a local heap
 * in a GlobalAlloc'd moveable block with a notify procedure of ours that
 * records each call (wMsg, hMem, wArg) and answers 0 or 1; requests that
 * need the heap to grow (LN_OUTOFMEM), discardable blocks in the way
 * (LN_DISCARD), compaction moving blocks (LN_MOVE); KERNEL's own procedure
 * put back; LocalShrink on a grown heap and on our own. Run by make test
 * and on Windows 3.1 (tests/refrun.sh, REFRUN_KRNL286=1 for KRNL286).
 */
#include <windows.h>

#define TK_REF "tk54ref.h"
#define TK_REF286 "tk54ref286.h"
#include "tklog.h"

extern DWORD __cdecl call_ds(WORD ds, void FAR *fn, WORD nargs, WORD a1, WORD a2, WORD a3);

#define NCALL 16
static WORD ncalls, cmsg[NCALL], ch[NCALL], carg[NCALL];
static WORD answer;     /* the first call's answer; later calls get 0
                           (a 1 every time made KERNEL ask for ever: 3.1
                           hung, it retries while the procedure says 1) */

BOOL FAR PASCAL __loadds notifyproc(WORD msg, HANDLE h, WORD arg)
{
    if (ncalls < NCALL) {
        cmsg[ncalls] = msg;
        ch[ncalls] = (WORD)h;
        carg[ncalls] = arg;
    }
    ncalls++;
    if (ncalls == 1)
        return answer;
    return 0;
}

static char nm[64];
static const char *cat(const char *a, const char *b)
{
    int i = 0;
    while (*a)
        nm[i++] = *a++;
    while (*b)
        nm[i++] = *b++;
    nm[i] = 0;
    return nm;
}

#define LA(sel, f, n) ((WORD)call_ds(sel, (void FAR *)LocalAlloc, 2, f, n, 0))
#define LF(sel, h) ((WORD)call_ds(sel, (void FAR *)LocalFree, 1, h, 0, 0))
#define LC(sel, n) ((WORD)call_ds(sel, (void FAR *)LocalCompact, 1, n, 0, 0))
#define LFL(sel, h) ((WORD)call_ds(sel, (void FAR *)LocalFlags, 1, h, 0, 0))
#define LL(sel, h) ((WORD)call_ds(sel, (void FAR *)LocalLock, 1, h, 0, 0))
#define LU(sel, h) ((WORD)call_ds(sel, (void FAR *)LocalUnlock, 1, h, 0, 0))

static DWORD set_notify(WORD sel, FARPROC p)
{
    static FARPROC ln;
    if (!ln)
        ln = GetProcAddress(GetModuleHandle("KERNEL"), MAKEINTRESOURCE(14));
    return call_ds(sel, (void FAR *)ln, 2, SELECTOROF(p), OFFSETOF(p), 0);
}

static HGLOBAL g;

static WORD new_heap(WORD size)
{
    WORD sel;
    g = GlobalAlloc(GMEM_MOVEABLE | GMEM_ZEROINIT, size);
    sel = SELECTOROF(GlobalLock(g));
    LocalInit(sel, 16, size - 1);
    return sel;
}

static void drop_heap(void)
{
    GlobalUnlock(g);
    GlobalFree(g);
}

/* the calls recorded since ncalls was cleared, under the name t */
static char tname[48];
static void report(const char *t0)
{
    WORD i;
    const char *t = tname;
    for (i = 0; t0[i] && i < 47; i++)
        tname[i] = t0[i];
    tname[i] = 0;
    obs(cat(t, ": calls"), ncalls);
    for (i = 0; i < ncalls && i < 4; i++) {
        obsn(cat(t, ": wMsg"), i, cmsg[i]);
        obsn(cat(t, ": wArg"), i, carg[i]);
    }
}

static void probe_outofmem(void)
{
    WORD sel, h, k;
    DWORD prev;
    for (k = 0; k < 2; k++) {
        const char *t = k ? "oom answer 1" : "oom answer 0";
        sel = new_heap(512);
        prev = set_notify(sel, (FARPROC)notifyproc);
        obs(cat(t, ": prev nonzero"), prev != 0);
        answer = k;
        ncalls = 0;
        h = LA(sel, LMEM_FIXED, 1000);
        obs(cat(t, ": LocalAlloc 1000 ok"), h != 0);
        obs(cat(t, ": GlobalSize after"), (WORD)GlobalSize(g));
        report(t);
        drop_heap();
    }
    /* KERNEL's procedure put back: the heap grows */
    sel = new_heap(512);
    prev = set_notify(sel, (FARPROC)notifyproc);
    set_notify(sel, (FARPROC)prev);
    ncalls = 0;
    h = LA(sel, LMEM_FIXED, 1000);
    obs("oom default: LocalAlloc 1000 ok", h != 0);
    obs("oom default: GlobalSize after", (WORD)GlobalSize(g));
    obs("oom default: our calls", ncalls);
    drop_heap();
}

static void probe_discard(void)
{
    WORD sel, d[3], i, k, h;
    for (k = 0; k < 2; k++) {
        const char *t = k ? "discard answer 1" : "discard answer 0";
        sel = new_heap(1024);
        set_notify(sel, (FARPROC)notifyproc);
        for (i = 0; i < 3; i++)
            d[i] = LA(sel, LMEM_MOVEABLE | LMEM_DISCARDABLE, 200);
        answer = k;
        ncalls = 0;
        obs(cat(t, ": LocalCompact(FFFF)"), LC(sel, 0xFFFF));
        report(t);
        obs(cat(t, ": first hMem is a block"), ncalls && (ch[0] == d[0] || ch[0] == d[1] || ch[0] == d[2]));
        for (i = 0; i < 3; i++)
            obsn(cat(t, ": discarded"), i, (LFL(sel, d[i]) & LMEM_DISCARDED) != 0);
        /* a request that needs their room, the heap's block unable to grow
           (locked: GlobalReAlloc of a locked moveable block fails) */
        ncalls = 0;
        h = LA(sel, LMEM_FIXED, 700);
        obs(cat(t, ": LocalAlloc 700 ok"), h != 0);
        report(cat(t, " 700"));
        drop_heap();
    }
}

static void probe_move(void)
{
    WORD sel, a, b, c, pb;
    sel = new_heap(1024);
    set_notify(sel, (FARPROC)notifyproc);
    a = LA(sel, LMEM_MOVEABLE, 100);
    b = LA(sel, LMEM_MOVEABLE, 100);
    c = LA(sel, LMEM_FIXED, 100);
    pb = LL(sel, b);
    LU(sel, b);
    LF(sel, a);
    answer = 1;
    ncalls = 0;
    obs("move: LocalCompact(0)", LC(sel, 0));
    {
        WORD nb = LL(sel, b);
        obs("move: b moved", nb != pb);
        LU(sel, b);
        report("move");
        obs("move: hMem==b", ncalls ? ch[0] == b : 0xFFFF);
        obs("move: wArg==new address", ncalls ? carg[0] == nb : 0xFFFF);
        obs("move: wArg==old address", ncalls ? carg[0] == pb : 0xFFFF);
    }
    (void)c;
    drop_heap();
}

static void probe_shrink(void)
{
    WORD sel, h, r;
    sel = new_heap(512);
    h = LA(sel, LMEM_FIXED, 3000);
    obs("shrink: grown GlobalSize", (WORD)GlobalSize(g));
    LF(sel, h);
    r = LocalShrink((HLOCAL)sel, 0);
    obs("shrink: LocalShrink(sel, 0)", r);
    obs("shrink: GlobalSize after", (WORD)GlobalSize(g));
    {
        WORD ph = *(WORD FAR *)MAKELP(sel, 6);
        obs("shrink: last-first",
            *(WORD FAR *)MAKELP(sel, ph + (k286() ? 8 : 0x0A)) - *(WORD FAR *)MAKELP(sel, ph + 6));
    }
    h = LA(sel, LMEM_FIXED, 3000);
    LF(sel, h);
    r = LocalShrink((HLOCAL)sel, 0x400);
    obs("shrink: LocalShrink(sel, 400h)", r);
    obs("shrink: GlobalSize after 400h", (WORD)GlobalSize(g));
    h = LA(sel, LMEM_FIXED, 200);
    r = LocalShrink((HLOCAL)sel, 0);
    obs("shrink: with a block, LocalShrink(sel, 0)", r);
    obs("shrink: with a block, GlobalSize", (WORD)GlobalSize(g));
    drop_heap();
}

int __cdecl tmain(void)
{
    out("TK54 start\r\n");
    probe_outofmem();
    probe_discard();
    probe_move();
    probe_shrink();
    out("TK54 done, failures=");
    outhex((WORD)failures);
    out("\r\n");
    return failures;
}
