/*
 * k386c.c - KRNL386 only (built with -DKRNL386): what KERNEL does in 386
 * Enhanced mode, under WIN386's VMM, that it does not do under DOSX.
 *
 * Observed on Windows 3.1 with tests/apps/tk57.c (TK57 as the shell,
 * tests/refrun.sh TK57.EXE /3 and /S; Microsoft's KRNL386):
 *  - INT 2Fh 1600h gives AX=0A03h in 386 Enhanced mode (1600h unchanged
 *    under DOSX); GetWinFlags is 0C29h there (WF_ENHANCED and WF_PAGING
 *    in place of WF_STANDARD) and 0419h in Standard mode, on DOSBox's 486
 *    with a coprocessor.
 *  - WF_PAGING and the processor: Microsoft's KRNL386 on w16's WIN386
 *    (whose DPMI 0400h gives CL=3 and whose 0500h reports no paging file)
 *    gave 0425h: WF_CPU386 from the DPMI host's processor type, and no
 *    WF_PAGING; on Microsoft's WIN386 (CL=4, a paging file of 39FCh pages
 *    in 0500h's 20h) 0C29h. So WF_PAGING follows 0500h's paging file
 *    (UNVERIFIED that it is that field and not another 0500h value).
 *  - The Reboot device's API (INT 2Fh 1684h BX=0009h; Ralf Brown's
 *    Interrupt List 61: 0201h sets KERNEL's Ctrl+Alt+Del procedure ES:DI
 *    and a byte DS:SI, 0202h gives the procedure back) returns from 0202h
 *    a procedure in a KERNEL code segment: KERNEL called 0201h. Pietrek
 *    (ch. 3, InitTask) has it done at the first task's InitTask, in
 *    Enhanced mode only (KRebootInit); w16's WIN386 saw the 0201h call
 *    during the boot (ARCHITECTURE.md 12.5).
 * The byte and the procedure: k386.asm (KERNEL sets the byte to 1 at
 * every task switch and idle pass: "whenever it runs").
 * DOS programs: WINOA386.MOD and the 386 grabber (loader.c, TK57 "mod").
 * UNVERIFIED: the call Pietrek
 * describes when the last task ends (ExitCall) and Reboot 0100h for
 * ExitWindows(EW_REBOOTSYSTEM) (ExitKernel).
 */
#include "kernel.h"

#ifdef KRNL386

extern int __cdecl int2f_pm(regs16 FAR *r);                 /* k386.asm */
extern int __cdecl vxd_call(u32 entry, regs16 FAR *r);
extern void __far reboot_proc(void);
#pragma aux reboot_proc "*"
extern u8 __cdecl k386_sane;            /* k386.asm: Reboot 0201h's byte */

u16 k386_enhanced;              /* 386 Enhanced mode: running under WIN386 */
u16 k386_paging;                /* the VMM pages to a paging file */
static int reboot_done;

/* At start-up: which mode (INT 2Fh 1600h, the DDK's "Enhanced Windows
   installation check": AL 0 or 80h none, 01h or FFh Windows/386 2.x,
   else the major version) */
void k386_init(void)
{
    regs16 r;
    u8 al;
    kmemset(&r, 0, sizeof r);
    r.ax = 0x1600;
    int2f_pm(&r);
    al = (u8)r.ax;
    k386_enhanced = (al & 0x7F) != 0 && al != 0x01 && al != 0xFF;
    if (k386_enhanced) {
        /* DPMI 0500h: 20h, the paging file's size in pages (-1: none
           reported) */
        static u32 info[12];
        kmemset(info, 0xFF, sizeof info);
        kmemset(&r, 0, sizeof r);
        r.ax = 0x0500;
        r.di = FP_OFF((void FAR *)info);
        r.es = FP_SEG((void FAR *)info);
        if (!dpmi31(&r))
            k386_paging = info[8] != 0 && info[8] != 0xFFFFFFFFUL;
    }
}

/* A VxD's protected-mode API entry (INT 2Fh 1684h), or 0 */
static u32 vxd_entry(u16 id)
{
    regs16 r;
    kmemset(&r, 0, sizeof r);
    r.ax = 0x1684;
    r.bx = id;
    int2f_pm(&r);
    return ((u32)r.es << 16) | r.di;
}

/* The first task's InitTask (task.c): in Enhanced mode, KERNEL's
   Ctrl+Alt+Del procedure to the Reboot device (0201h) */
void k386_first_task(void)
{
    regs16 r;
    u32 e;
    if (reboot_done || !k386_enhanced)
        return;
    reboot_done = 1;
    e = vxd_entry(0x0009);
    if (!e)
        return;
    kmemset(&r, 0, sizeof r);
    r.ax = 0x0201;
    r.es = FP_SEG((void __far *)reboot_proc);
    r.di = FP_OFF((void __far *)reboot_proc);
    r.ds = FP_SEG((void FAR *)&k386_sane);
    r.si = FP_OFF((void FAR *)&k386_sane);
    vxd_call(e, &r);
}

#endif
