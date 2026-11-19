/*
 * tpc.c - TPC.DLL, the timer-event callback of the timer probe TPR.EXE
 * (tests/timer/tpr.c; ARCHITECTURE.md 21). The callback (tpcs.asm's
 * entry) records each call in this DLL's fixed data: the arguments, every
 * register at entry, the caller's code segment, timeGetTime, the BIOS
 * tick count, channel 0 of the 8254, the nesting depth and the current
 * task; and does what the probe set for its slot (tpc.h): kill its own
 * event, set another, wait with interrupts enabled (reentrancy), call
 * timeGetSystemTime or timeBeginPeriod. The SDK (Multimedia Programmer's
 * Guide ch. 6) says the callback must be in a DLL's fixed code and may
 * call only interrupt-callable functions: the time* functions are.
 */
#include <windows.h>
#include <mmsystem.h>
#include "tpc.h"

extern WORD __cdecl __far get0040(void);
extern WORD __cdecl __far pit0(void);
extern WORD __cdecl __far picisr(void);
extern WORD __cdecl __far getflags(void);
extern void __cdecl __far enable(void);

static struct tpcstate st;
extern DWORD __cdecl irqcnt;     /* tpcs.asm: INT 8 interrupts while hooked */

static int enh;          /* 386 Enhanced mode: the ports are virtual, not read */

int __cdecl libmain(void)
{
    enh = (GetWinFlags() & WF_ENHANCED) != 0;
    return 1;
}

struct tpcstate FAR * FAR PASCAL __loadds TpcState(void)
{
    return &st;
}

/* frame: ES DS DI SI BP SP BX DX CX AX flags bp ip cs dw2(2) dw1(2)
   user(2) msg id, as tpcs.asm pushed them */
void __cdecl __far tpc_cb(WORD __far *f)
{
    DWORD user = *(DWORD __far *)(f + 18);
    WORD s = (WORD)user;
    struct slot *sl;
    DWORD now = timeGetTime();
    WORD nth;
    if (s >= NSLOT)
        s = NSLOT - 1;
    sl = &st.slots[s];
    nth = ++sl->count;
    if (++st.depth > st.maxdepth)
        st.maxdepth = st.depth;
    if (st.depth > sl->maxdepth)
        sl->maxdepth = st.depth;
    if (nth == 1)
        sl->first = now;
    else {
        DWORD d = now - sl->last;
        if (nth == 2 || d < sl->minitv)
            sl->minitv = d;
        if (nth == 2 || d > sl->maxitv)
            sl->maxitv = d;
    }
    sl->last = now;
    if ((sl->mode & CB_REC) && st.n < NCB) {
        struct cbrec *r = &st.rec[st.n++];
        r->slot = s;
        r->id = f[21];
        r->msg = f[20];
        r->user = user;
        r->dw1 = *(DWORD __far *)(f + 16);
        r->dw2 = *(DWORD __far *)(f + 14);
        r->flags = f[10];
        r->ss = SELECTOROF(f);
        r->sp = OFFSETOF(f) + 24;             /* SP at entry: the return address */
        r->ds = f[1];
        r->es = f[0];
        r->retcs = f[13];
        r->ax = f[9];
        r->bx = f[6];
        r->cx = f[8];
        r->dx = f[7];
        r->si = f[3];
        r->di = f[2];
        r->ms = now;
        r->tick = *(WORD __far *)MAKELP(get0040(), 0x6C);
        r->pit = enh ? 0 : pit0();
        r->isr = enh ? 0 : picisr();
        r->depth = st.depth;
        r->task = (WORD)GetCurrentTask();
        r->nth = nth;
        r->irq = irqcnt;
    }
    if ((sl->mode & CB_KILL) && nth == sl->param)
        sl->res = timeKillEvent(f[21]);
    if (nth == 1) {
        if (sl->mode & CB_SET)
            sl->res = timeSetEvent(sl->param, 0, (LPTIMECALLBACK)TpcCallback,
                                   0x5A5A0000L | (s + 1), TIME_ONESHOT);
        if (sl->mode & CB_STIME) {
            MMTIME mt;
            int i;
            mt.wType = TIME_MS;
            sl->res = timeGetSystemTime(&mt, sizeof mt);
            for (i = 0; i < 8; i++)
                sl->mmt[i] = ((BYTE *)&mt)[i];
        }
        if (sl->mode & CB_BEGIN)
            sl->res = timeBeginPeriod(sl->param);
        if (sl->mode & CB_SPIN) {
            /* the time spent counts timeGetTime's steps of 0 to 10 ms only:
               in this loop a real step is 0 or 1 ms, and 3.1's clock steps
               55 ms back and later 55 ms on again (ARCHITECTURE.md 21.2),
               which as an unsigned difference ended the wait at once in
               some runs */
            DWORD p = now, t, el = 0;
            enable();
            while (el < sl->param) {
                t = timeGetTime();
                if (t - p <= 10)
                    el += t - p;
                p = t;
            }
        }
    }
    st.depth--;
}
