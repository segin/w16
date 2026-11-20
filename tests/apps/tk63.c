/*
 * tk63.c - how much memory Windows gives (boot-test case bigmem,
 * ARCHITECTURE.md 9.6 "More than 64 MB"): run as the shell, it writes
 * to TK.LOG, all values in KB (decimal):
 *   OBS ~mode=S|E           Standard or 386 Enhanced mode (GetWinFlags)
 *   OBS ~dpmi_largest=N     DPMI 0500h: the largest free block
 *   OBS ~dpmi_total=N       DPMI 0500h: total physical pages (-1: none given)
 *   OBS ~freespace=N        GetFreeSpace(0)
 *   OBS ~compact=N          GlobalCompact(0)
 *   OBS ~largest=N          the largest GlobalAlloc(GMEM_MOVEABLE) that
 *                           succeeds (64 KB steps up to KRNL386's
 *                           16 MB - 64 KB per block)
 *   OBS ~blocks=N           blocks of that size (at most 8 MB each) taken
 *                           one after another, until none is left
 *   OBS ~total=N            their sum
 *   PASS/FAIL touched       a dword written in every 4 KB page of every
 *                           block and read back
 *   OBS ~after=N            GetFreeSpace(0) after they are freed
 * then "TK63 end" and it ends, which ends Windows. The case compares the
 * numbers with the memory DOSBox-X was given (memsize).
 */
#include <windows.h>

#define TK_REF "tk63ref.h"
#include "tklog.h"

#define MAXBLK 160

static HGLOBAL blk[MAXBLK];

static void outdec(DWORD v)
{
    /* no long division: the probes link no C library */
    static const DWORD p10[] = { 1000000000UL, 100000000UL, 10000000UL,
        1000000UL, 100000UL, 10000UL, 1000UL, 100UL, 10UL, 1UL };
    char b[12];
    int i, n = 0;
    for (i = 0; i < 10; i++) {
        char c = '0';
        while (v >= p10[i]) {
            v -= p10[i];
            c++;
        }
        if (c != '0' || n || i == 9)
            b[n++] = c;
    }
    b[n] = 0;
    out(b);
}

static void obskb(const char *name, DWORD bytes)
{
    out("OBS ~");
    out(name);
    out("=");
    if (bytes == 0xFFFFFFFFUL)
        out("-1");
    else
        outdec(bytes >> 10);
    out("\r\n");
}

static DWORD mi[12];

static void dpmi_info(void)
{
    _asm {
        push es
        push di
        mov ax, ds
        mov es, ax
        lea di, mi
        mov ax, 0500h
        int 31h
        pop di
        pop es
    }
}

/* the largest moveable block GlobalAlloc gives, in 64 KB steps */
static DWORD largest_alloc(void)
{
    DWORD lo = 0, hi = 0xFF, mid;           /* 64 KB units */
    HGLOBAL h;
    while (lo < hi) {
        mid = (lo + hi + 1) >> 1;
        h = GlobalAlloc(GMEM_MOVEABLE, mid * 0x10000UL);
        if (h) {
            GlobalFree(h);
            lo = mid;
        } else
            hi = mid - 1;
    }
    return lo * 0x10000UL;
}

/* write (w) or check every 4 KB page of block h through selector s */
static int touch(HGLOBAL h, UINT s, int w, DWORD tag)
{
    DWORD base, size, off;
    void FAR *p = GlobalLock(h);
    if (!p)
        return 0;
    base = GetSelectorBase(SELECTOROF(p));
    size = GlobalSize(h);
    for (off = 0; off + 4 <= size; off += 0x1000) {
        DWORD FAR *d;
        SetSelectorBase(s, base + off);
        SetSelectorLimit(s, 0xFFF);
        d = (DWORD FAR *)((DWORD)s << 16);
        if (w)
            *d = tag ^ off;
        else if (*d != (tag ^ off)) {
            GlobalUnlock(h);
            return 0;
        }
    }
    GlobalUnlock(h);
    return 1;
}

int __cdecl tmain(void)
{
    DWORD big, total = 0, step;
    int n = 0, i, ok = 1;
    UINT s;

    out("TK63 start\r\n");
    out((GetWinFlags() & WF_ENHANCED) ? "OBS ~mode=E\r\n" : "OBS ~mode=S\r\n");
    dpmi_info();
    obskb("dpmi_largest", mi[0]);
    obskb("dpmi_total", mi[6] == 0xFFFFFFFFUL ? mi[6] : mi[6] << 12);
    obskb("freespace", GetFreeSpace(0));
    obskb("compact", GlobalCompact(0));
    big = largest_alloc();
    obskb("largest", big);
    /* blocks of up to 8 MB until GlobalAlloc fails, then smaller ones */
    step = big < 0x800000UL ? big : 0x800000UL;
    while (step >= 0x10000UL && n < MAXBLK) {
        HGLOBAL h = GlobalAlloc(GMEM_MOVEABLE, step);
        if (!h) {
            step >>= 1;
            continue;
        }
        blk[n++] = h;
        total += GlobalSize(h);
    }
    obskb("blocks", (DWORD)n << 10);
    obskb("total", total);
    s = AllocSelector(SELECTOROF((void FAR *)mi));
    if (!s)
        ok = 0;
    for (i = 0; ok && i < n; i++)
        ok = touch(blk[i], s, 1, 0x5A5A0000UL + i);
    for (i = 0; ok && i < n; i++)
        ok = touch(blk[i], s, 0, 0x5A5A0000UL + i);
    if (s)
        FreeSelector(s);
    check(ok, "touched");
    for (i = 0; i < n; i++)
        GlobalFree(blk[i]);
    obskb("after", GetFreeSpace(0));
    out("TK63 end\r\n");
    return 0;
}
