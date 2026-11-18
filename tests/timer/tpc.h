/*
 * tpc.h - the state TPC.DLL's timer callback (tests/timer/tpc.c,
 * tpcs.asm) keeps for the probe TPR.EXE (tests/timer/tpr.c); TpcState()
 * returns it.
 */
#define NSLOT 16
#define NCB 520

/* a slot's mode bits (dwUser's low word is the slot, the high word 5A5Ah) */
#define CB_REC   1      /* record each call (else count it only) */
#define CB_KILL  2      /* timeKillEvent(own id) at call number param */
#define CB_SET   4      /* first call: timeSetEvent(param, 0, cb, next slot, oneshot) */
#define CB_SPIN  8      /* first call: interrupts on, wait param ms */
#define CB_STIME 16     /* first call: timeGetSystemTime into the slot */
#define CB_BEGIN 32     /* first call: timeBeginPeriod(param), result in res */

struct cbrec {
    WORD slot, id, msg, flags, ss, sp, ds, es, retcs, ax, bx, cx, dx, si, di;
    DWORD user, dw1, dw2, ms;
    WORD tick, pit, depth, task, nth;
    DWORD irq;
    WORD isr;
};

struct slot {
    WORD mode, param, count, id, res, maxdepth;
    DWORD first, last, minitv, maxitv;
    BYTE mmt[8];
};

struct tpcstate {
    WORD n, depth, maxdepth, tpcds;
    struct slot slots[NSLOT];
    struct cbrec rec[NCB];
};

struct tpcstate FAR * FAR PASCAL TpcState(void);
void FAR PASCAL TpcHook(BOOL on);
DWORD FAR PASCAL TpcIrq(void);
WORD FAR * FAR PASCAL TpcArm(void);
WORD FAR * FAR PASCAL TpcStack(void);
void FAR PASCAL TpcCallback(UINT id, UINT msg, DWORD user, DWORD dw1, DWORD dw2);
