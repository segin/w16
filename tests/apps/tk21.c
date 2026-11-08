/*
 * tk21.c - the system timer: run as the shell (tests/dropin.sh, -r for
 * Windows 3.1), it asks SYSTEM.DRV for a timer (CreateSystemTimer,
 * SYSTEM.2) whose procedure counts calls, waits about a second (BIOS
 * ticks) and logs the count; then the same with GetSystemMsecCount
 * (SYSTEM.6). To compare how interrupts reach drivers under w16's KERNEL
 * and Windows'.
 */
#include <windows.h>

#define TK_REF "tk21ref.h"
#include "tklog.h"

extern WORD FAR PASCAL S_CREATESYSTEMTIMER(WORD ms, FARPROC proc);
extern WORD FAR PASCAL S_KILLSYSTEMTIMER(WORD id);
extern DWORD FAR PASCAL S_GETSYSTEMMSECCOUNT(void);
extern WORD __cdecl get_0040h(void);

static volatile WORD calls;

void FAR __loadds timer_proc(void)
{
    calls++;
}

int __cdecl tmain(void)
{
    volatile DWORD FAR *ticks = (volatile DWORD FAR *)MAKELP(get_0040h(), 0x6C);
    DWORD t0, m0, m1;
    WORD id;

    out("TK21 start\r\n");
    id = S_CREATESYSTEMTIMER(55, (FARPROC)timer_proc);
    obs("CreateSystemTimer != 0", id != 0);
    m0 = S_GETSYSTEMMSECCOUNT();
    t0 = *ticks;
    while (*ticks - t0 < 18)
        ;
    m1 = S_GETSYSTEMMSECCOUNT();
    obs("BIOS ticks advanced", 1);
    obs("timer calls in 18 ticks >= 10", calls >= 10);
    obs_env("timer calls", calls);
    obs("msec count advanced >= 500", m1 - m0 >= 500);
    obs_env("msec delta", (WORD)(m1 - m0));
    if (id)
        S_KILLSYSTEMTIMER(id);
    out("TK21 done, failures=");
    outhex((WORD)failures);
    out("\r\n");
    return failures;
}
