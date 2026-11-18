/*
 * mmc.h - the state MMC.DLL (tests/mmsystem/mmc.c, mmcs.asm) shares with
 * the MMSYSTEM probe MMP.EXE (tests/mmsystem/mmp.c).
 */
#ifndef MMC_H
#define MMC_H
#define MMC_NREC 40
struct mmcrec {                 /* one call of MMCCALLBACK */
    /* ES DS DI SI BP SP BX DX CX AX FLAGS BP0 RETIP RETCS, then the
     * arguments dw2 dw1 dwUser msg h (8 words) */
    WORD w[22];
    WORD task;                  /* GetCurrentTask() */
    WORD ss;                    /* the stack it ran on */
};
struct mmcstate {
    WORD nrec;                  /* calls recorded (more are counted) */
    struct mmcrec rec[MMC_NREC];
    WORD FAR *stk;              /* mmcs.asm's StackEnter record, 5 x 12 words */
    WORD FAR *n21, FAR *n41, FAR *ax21, FAR *ax41;
    WORD taskdone;              /* the mmTaskCreate task has finished */
    WORD taskblock;             /* it is in mmTaskBlock */
    WORD parent;                /* the probe's task */
};
typedef struct mmcstate FAR *(FAR PASCAL *MMCSTATEPROC)(void);
#endif
