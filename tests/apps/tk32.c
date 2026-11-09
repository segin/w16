/*
 * tk32.c - what KERNEL tells a task's signal procedure when the task
 * ends (the KERNEL -> USER contract: USER's SignalProc is the TDB's
 * signal procedure after InitApp). TK32 calls InitApp, installs a signal
 * procedure of its own with SetTaskSignalProc (logging BX, DI and its
 * five Pascal arguments, then chaining to the previous one, USER's), and
 * ends. Run as the shell (tests/dropin.sh, and -r for Windows 3.1);
 * the signal lines are the last in TK.LOG.
 */
#include <windows.h>

#define TK_REF "tk32ref.h"
#include "tklog.h"

extern WORD FAR PASCAL K_WAITEVENT(WORD htask);
extern int FAR PASCAL U_INITAPP(WORD hinst);
extern FARPROC FAR PASCAL K_SETTASKSIGNALPROC(WORD htask, FARPROC p);
extern WORD FAR PASCAL K_GETTASKQUEUE(WORD htask);

typedef void (FAR PASCAL *SIGPROC)(WORD, WORD, WORD, WORD, WORD);
static SIGPROC prev;
static WORD hinst_seen, task_seen;
WORD __cdecl sig_bx, sig_di;

/* the signal procedure; tk32s.asm's entry records BX and DI first */
void FAR PASCAL __loadds sig_c(WORD h, WORD code, WORD di, WORD inst, WORD queue)
{
    out("SIGNAL\r\n");
    obs("signal BX", sig_bx);
    obs("signal DI register", sig_di);
    obs("signal code", code);
    obs("signal h==task", h == task_seen);
    obs("signal arg3", di);
    obs("signal hInstance==ours", inst == hinst_seen);
    obs("signal queue==ours", queue == K_GETTASKQUEUE(0));
    obs_env("signal queue", queue);
    if (prev)
        prev(h, code, di, inst, queue);
    out("SIGNAL done\r\n");
}

extern void FAR sig_entry(void);        /* tk32s.asm */
#pragma aux sig_entry "*"

int __cdecl tmain(void)
{
    hinst_seen = init_regs[5];
    out("TK32 start\r\n");
    K_WAITEVENT(0);
    U_INITAPP(hinst_seen);
    task_seen = (WORD)GetCurrentTask();
    prev = (SIGPROC)K_SETTASKSIGNALPROC(0, (FARPROC)sig_entry);
    obs("previous signal proc != 0", prev != 0);
    obs("previous == USER.314",
        (FARPROC)prev == GetProcAddress(GetModuleHandle("USER"), MAKEINTRESOURCE(314)));
    out("TK32 exits\r\n");
    return 0;
}
