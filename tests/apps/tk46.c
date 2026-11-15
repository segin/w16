/*
 * tk46.c - how the DOS extender passes on INT 15h C207h (set the PS/2
 * pointing-device handler) when ES:BX is a protected-mode address, as
 * MOUSE.DRV gives it: started from WIN.INI load= under tests/inputrun.sh
 * (a click to capture the mouse, then motion), it installs its own
 * handler (tk46s.asm; the mouse then no longer reaches MOUSE.DRV) and,
 * once a second for 30 seconds, logs how often it was called and the
 * state of the last call: the four words the BIOS passes, the return
 * address, SS:SP, DS, ES and the flags.
 * Compare tests/inputrun.sh -r -X build/dos/DOSX.EXE with -r.
 */
#include <windows.h>

#define TK_REF "tk46ref.h"
#include "tklog.h"

extern WORD __cdecl p_calls, p_rec[12];
extern WORD __cdecl __far ps2_set(void);
extern int FAR PASCAL U_INITAPP(WORD hinst);

int __cdecl tmain(void)
{
    WORD i, r;
    MSG m;
    char n[16];
    U_INITAPP(init_regs[5]);
    out("TK46 start\r\n");
    GlobalFix((HGLOBAL)GlobalHandle(SELECTOROF((void FAR *)ps2_set)));
    r = ps2_set();
    obs("C207h AH", r & 0xFF);
    obs("C207h CF", r >> 8);
    SetTimer(0, 1, 1000, NULL);
    for (i = 0; i < 30; i++) {
        while (GetMessage(&m, 0, 0, 0) && m.message != WM_TIMER)
            DispatchMessage(&m);
        obs("t", i);
        obs("  calls", p_calls);
        if (p_calls) {
            WORD k;
            for (k = 0; k < 4; k++) {
                wsprintf(n, "  w%u", k);
                obs(n, p_rec[k]);
            }
            obs("  ret CS is a selector", (p_rec[5] & 3) == 3);
            obs("  SS is ours", p_rec[6] == SELECTOROF((void FAR *)&i));
            obs("  DS is ours", p_rec[8] == SELECTOROF((void FAR *)&i));
            obs("  flags IF", (p_rec[10] >> 9) & 1);
            obs("  flags IOPL", (p_rec[10] >> 12) & 3);
        }
    }
    KillTimer(0, 1);
    out("TK46 done\r\n");
    return 0;
}
