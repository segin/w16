/*
 * tk62a.c - TK62A.EXE, the helper task of TK62 (tests/apps/tk62.c, the
 * KERNEL audit's regressions): it ends with a free block in front of its
 * own block in a piece of the heap that holds nothing else, so that
 * freeing its blocks at its end empties that piece (the audit's C1:
 * KERNEL read the merged free arena after the piece was given back).
 *
 * A block of almost 1 MB gets a piece of its own (no other free space is
 * that large); shrunk to 100h bytes, it leaves the rest of the piece
 * free, and the next block of almost 1 MB goes there, right after it.
 * The first is then freed. No log of its own: tk62.h.
 * With "L" after the address it only counts its turns (Yield after each)
 * until TK62 sets stop: TK62 "lock" (LockCurrentTask).
 */
#include <windows.h>
#include "tk62.h"

extern WORD __cdecl init_regs[7];    /* ax bx cx dx si di es */

static WORD hex(const char FAR **p)
{
    WORD v = 0;
    const char FAR *s = *p;
    while (*s == ' ')
        s++;
    for (;;) {
        char c = *s;
        if (c >= '0' && c <= '9')
            v = v * 16 + (c - '0');
        else if (c >= 'A' && c <= 'F')
            v = v * 16 + (c - 'A' + 10);
        else
            break;
        s++;
    }
    *p = s;
    return v;
}

int __cdecl tmain(void)
{
    const char FAR *cmd = MAKELP(init_regs[6], init_regs[1]);
    WORD ds, off;
    tk62sh FAR *sh;
    HGLOBAL h1, h2;
    DWORD b1, b2;
    ds = hex(&cmd);
    off = hex(&cmd);
    sh = (tk62sh FAR *)MAKELP(ds, off);
    sh->task = (WORD)GetCurrentTask();
    while (*cmd == ' ')
        cmd++;
    if (*cmd == 'L') {
        while (!sh->stop) {
            sh->count++;
            Yield();
        }
        sh->done = 1;
        return 0;
    }
    h1 = GlobalAlloc(GMEM_FIXED, 0xFFF00UL);
    sh->big = h1 != 0;
    if (h1 && GlobalReAlloc(h1, 0x100, 0)) {
        h2 = GlobalAlloc(GMEM_FIXED, 0xFFE00UL);
        if (h2) {
            b1 = GetSelectorBase(SELECTOROF(GlobalLock(h1)));
            b2 = GetSelectorBase(SELECTOROF(GlobalLock(h2)));
            GlobalUnlock(h1);
            GlobalUnlock(h2);
            sh->hit = b2 > b1 && b2 - b1 < 0x1000;
        }
    }
    if (h1)
        GlobalFree(h1);
    sh->done = 1;
    return 0;               /* h2 is freed at the task's end */
}
