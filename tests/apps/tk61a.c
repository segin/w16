/*
 * tk61a.c - TK61A.EXE, the helper task of the freed-selector probe TK61
 * (tests/apps/tk61.c): allocates a fixed and a moveable block, tells TK61
 * their selectors (tk61.h), and ends with them still in FS and GS, as
 * QuickTime's Movie Player ends with one of its blocks in FS
 * (docs/notes/quicktime-handover.md). No log of its own.
 */
#include <windows.h>
#include "tk61.h"

extern WORD __cdecl init_regs[7];    /* ax bx cx dx si di es */
extern WORD __cdecl cur_cs(void);
extern WORD __cdecl cur_ds(void);
extern void __cdecl set_fsgs(WORD fs, WORD gs);
WORD use386;                         /* tk61s.asm */

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
    WORD ds, off, f, m, myds;
    tk61sh FAR *sh;
    HGLOBAL hf, hm;
    use386 = (GetWinFlags() & (WF_CPU386 | WF_CPU486)) != 0;
    ds = hex(&cmd);
    off = hex(&cmd);
    sh = (tk61sh FAR *)MAKELP(ds, off);
    hf = GlobalAlloc(GMEM_FIXED, 0x100);
    hm = GlobalAlloc(GMEM_MOVEABLE, 0x100);
    f = SELECTOROF(GlobalLock(hf));
    m = SELECTOROF(GlobalLock(hm));
    GlobalUnlock(hm);
    myds = cur_ds();
    sh->task = (WORD)GetCurrentTask();
    sh->fixed = f;
    sh->moveable = m;
    sh->ds = myds;
    sh->cs = cur_cs();
    sh->done = 1;
    set_fsgs(f, m);
    return 0;
}
