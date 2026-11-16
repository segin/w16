/*
 * tk49.c - writes out MS1.COM's log (tests/dos/ms1.asm: the PS/2
 * pointing-device calls, INT 74h and the handler's calls in real mode
 * while Windows booted). Started from WIN.INI load= after the seconds
 * given in [w16] tk49wait (default 40) of waiting with GetMessage, it
 * logs every record: kind, AX, BX, CX/ES, flags, INT 15h depth, IVT 74h
 * segment, sequence. Run with MS1 loaded before WIN (a batch file).
 */
#include <windows.h>

#define TK_REF "tk49ref.h"
#include "tklog.h"

extern int FAR PASCAL U_INITAPP(WORD hinst);

int __cdecl tmain(void)
{
    WORD ivtsel = (WORD)(DWORD)GetProcAddress(GetModuleHandle("KERNEL"), "__0000H");
    WORD FAR *ivt = (WORD FAR *)MAKELP(ivtsel, 0);
    WORD off = ivt[0x66 * 2], seg = ivt[0x66 * 2 + 1];
    WORD sel, i, n, wait;
    WORD FAR *h;
    MSG m;
    char line[96];
    U_INITAPP(init_regs[5]);
    out("TK49 start\r\n");
    wait = GetPrivateProfileInt("w16", "tk49wait", 40, "SYSTEM.INI");
    SetTimer(0, 1, 1000, NULL);
    for (i = 0; i < wait; i++)
        while (GetMessage(&m, 0, 0, 0) && m.message != WM_TIMER)
            DispatchMessage(&m);
    KillTimer(0, 1);
    sel = AllocSelector(SELECTOROF((void FAR *)&i));
    SetSelectorBase(sel, (DWORD)seg << 4);
    SetSelectorLimit(sel, 0xFFFF);
    h = (WORD FAR *)MAKELP(sel, off);
    n = h[0];
    obs("records", n);
    for (i = 0; i < n; i++) {
        WORD FAR *r = (WORD FAR *)MAKELP(sel, h[10] + i * 16);
        wsprintf(line, "R %u %04X %04X %04X fl=%04X d=%u ivt74=%04X #%u\r\n",
                 r[0], r[1], r[2], r[3], r[4], r[5], r[6], r[7]);
        out(line);
    }
    out("TK49 done\r\n");
    return 0;
}
