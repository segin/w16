/*
 * tk45.c - the interrupt controllers while Windows is idle (the w16 DOSX
 * input bug): started from WIN.INI load=, it waits for WM_TIMER (one a
 * second, GetMessage, so the system goes idle in between) and logs the
 * masks (ports 21h, A1h), the in-service and request registers of both
 * PICs and the BIOS tick count (0040:006C) each time, 40 times.
 * Compare tests/dropin.sh -r -x with -r.
 */
#include <windows.h>

#define TK_REF "tk45ref.h"
#include "tklog.h"

void _disable(void);
void _enable(void);
#pragma aux _disable = "cli";
#pragma aux _enable = "sti";
static BYTE inb(WORD port);
#pragma aux inb = "in al, dx" parm [dx] value [al];
static void outb(WORD port, BYTE v);
#pragma aux outb = "out dx, al" parm [dx] [al];

extern int FAR PASCAL U_INITAPP(WORD hinst);

static BYTE reg(WORD port, BYTE ocw3)
{
    BYTE v;
    _disable();
    outb(port, ocw3);
    v = inb(port);
    outb(port, 0x0A);
    _enable();
    return v;
}

int __cdecl tmain(void)
{
    WORD i, sel40 = (WORD)(DWORD)GetProcAddress(GetModuleHandle("KERNEL"), "__0040H");
    DWORD FAR *ticks = (DWORD FAR *)MAKELP(sel40, 0x6C);
    DWORD last = *ticks, t0 = GetTickCount();
    MSG m;
    out("TK45 start\r\n");
    U_INITAPP(init_regs[5]);
    SetTimer(0, 1, 1000, NULL);
    for (i = 0; i < 40; i++) {
        while (GetMessage(&m, 0, 0, 0) && m.message != WM_TIMER)
            DispatchMessage(&m);
        obs("t", i);
        obs("  ms", (WORD)(GetTickCount() - t0));
        obs("  BIOS ticks", (WORD)(*ticks - last));
        obs("  IMR1", inb(0x21));
        obs("  IMR2", inb(0xA1));
        obs("  ISR1", reg(0x20, 0x0B));
        obs("  ISR2", reg(0xA0, 0x0B));
        obs("  IRR1", inb(0x20));
        last = *ticks;
        t0 = GetTickCount();
    }
    KillTimer(0, 1);
    out("TK45 done\r\n");
    return 0;
}
