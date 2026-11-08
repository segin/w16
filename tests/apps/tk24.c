/*
 * tk24.c - register-level KERNEL services USER relies on: GetTaskQueueDS
 * and GetTaskQueueES (KERNEL.118, 119). Called with known values in every
 * register (tk24s.asm, carry set), after InitApp has made a queue; logs
 * which registers they change. Run as the shell (tests/dropin.sh, and -r
 * for Windows 3.1).
 */
#include <windows.h>

#define TK_REF "tk24ref.h"
#include "tklog.h"

extern WORD FAR PASCAL K_WAITEVENT(WORD htask);
extern WORD FAR PASCAL K_GETTASKQUEUE(WORD htask);
extern int FAR PASCAL U_INITAPP(WORD hinst);
extern WORD __cdecl q_regs[10];
extern void __cdecl __far q_call(int es_version);

static const char *const names[10] = {
    "AX", "BX", "CX", "DX", "SI", "DI", "BP", "DS", "ES", "FLAGS&1"
};
static const WORD before[7] = { 0x1111, 0x2222, 0x3333, 0x4444, 0x5555, 0x6666, 0x7777 };

static void show(const char *what)
{
    WORD q = K_GETTASKQUEUE(0), i;
    char n[40];
    for (i = 0; i < 7; i++) {
        lstrcpy(n, what);
        lstrcat(n, " ");
        lstrcat(n, names[i]);
        lstrcat(n, " kept");
        obs(n, q_regs[i] == before[i]);
        if (q_regs[i] != before[i]) {
            lstrcpy(n, what);
            lstrcat(n, " ");
            lstrcat(n, names[i]);
            lstrcat(n, "==queue");
            obs(n, q_regs[i] == q);
        }
    }
    lstrcpy(n, what);
    lstrcat(n, " DS==queue");
    obs(n, q_regs[7] == q);
    lstrcpy(n, what);
    lstrcat(n, " ES==queue");
    obs(n, q_regs[8] == q);
    lstrcpy(n, what);
    lstrcat(n, " carry");
    obs(n, q_regs[9] & 1);
}

int __cdecl tmain(void)
{
    out("TK24 start\r\n");
    K_WAITEVENT(0);
    obs("InitApp != 0", U_INITAPP(init_regs[5]) != 0);
    obs("queue != 0", K_GETTASKQUEUE(0) != 0);
    q_call(0);
    show("GetTaskQueueDS");
    q_call(1);
    show("GetTaskQueueES");
    out("TK24 done, failures=");
    outhex((WORD)failures);
    out("\r\n");
    return failures;
}
