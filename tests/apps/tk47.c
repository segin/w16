/*
 * tk47.c - how the DOS extender passes on INT 15h C207h when ES:BX is a
 * code selector of a block below 1 MB, as MOUSE.DRV gives it (in a w16
 * boot 07AFh:04BCh, the selector's base 1FED0h, protected-mode code
 * there): copies a handler (tk47s.asm) into a GlobalDOSAlloc block, gives
 * C207h a code alias of it, and once a second for 30 seconds logs how
 * often the handler ran in real mode and in protected mode, the words of
 * the last call and its SS. Started from WIN.INI load= under
 * tests/inputrun.sh (the mouse then moves); afterwards it gives
 * MOUSE.DRV's handler back (the address C207h had: not known, so the
 * mouse stays off). Compare -r -X build/dos/DOSX.EXE with -r.
 */
#include <windows.h>

#define TK_REF "tk47ref.h"
#include "tklog.h"

extern BYTE __far __cdecl h47_code[];
extern WORD __far __cdecl h47_len;
extern WORD __cdecl __far c207(WORD sel, WORD off);
extern int FAR PASCAL U_INITAPP(WORD hinst);

int __cdecl tmain(void)
{
    DWORD d = GlobalDosAlloc(256);
    WORD sel = LOWORD(d), csel, i, r;
    WORD FAR *w = (WORD FAR *)MAKELP(sel, 0);
    BYTE FAR *b = (BYTE FAR *)MAKELP(sel, 0);
    BYTE FAR *code = (BYTE FAR *)h47_code;
    MSG m;
    U_INITAPP(init_regs[5]);
    out("TK47 start\r\n");
    obs("block", sel != 0);
    if (!sel)
        return 1;
    for (i = 0; i < 32; i++)
        b[i] = 0;
    for (i = 0; i < h47_len; i++)
        b[32 + i] = code[i];
    w[0] = sel;
    csel = AllocDStoCSAlias(sel);
    obs("code alias", csel != 0);
    obs("block segment", HIWORD(d));
    r = c207(csel, 32);
    obs("C207h AH", r & 0xFF);
    obs("C207h CF", r >> 8);
    SetTimer(0, 1, 1000, NULL);
    for (i = 0; i < 30; i++) {
        while (GetMessage(&m, 0, 0, 0) && m.message != WM_TIMER)
            DispatchMessage(&m);
        obs("t", i);
        obs("  real-mode calls", w[1]);
        obs("  protected-mode calls", w[2]);
        if (w[1] || w[2]) {
            obs("  w0", w[3]);
            obs("  w1", w[4]);
            obs("  w2", w[5]);
            obs("  w3", w[6]);
            obs("  SS is ours", w[7] == SELECTOROF((void FAR *)&i));
            obs("  SS is the block's segment", w[7] == HIWORD(d));
        }
    }
    KillTimer(0, 1);
    c207(0, 0);
    out("TK47 done\r\n");
    return 0;
}
