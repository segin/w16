/* vsd.c - w16's VSD, the virtual sound device (device 000Bh), a module
 * of WIN386.EXE's W3 table (ARCHITECTURE.md 12.8).
 *
 * Two services: VSD_Get_Version and VSD_Bell, the warning beep other
 * devices sound (VKD, REBOOT's screens, SHELL). The speaker itself is not
 * virtualised: as under Windows 3.1, every VM (the System VM's sound
 * driver, DOS boxes in front and behind) reaches the 8254's channel 2 and
 * port 61h directly, and the last to write wins. What VSD does for DOS
 * boxes is to silence a VM that is suspended: its speaker bits (port 61h
 * bits 0 and 1) are saved and cleared at VM_Suspend and set again at
 * VM_Resume.
 *
 * Observed under Windows 3.1 (tests/vsdprobe.sh: the probe VxD VXV.386,
 * the DOS program SPK.COM, DOSBox-X's sound recorded):
 * - VSD_Get_Version AX=0200h, carry clear (zero flag set); VSD_Bell
 *   leaves EAX, EBX, ECX, EDX as they were, carry and zero clear, and
 *   returns at once with port 61h's bits 0 and 1 set; the tone is
 *   892.7 Hz (an 8254 count of 1337) for 490-540 ms.
 * - Install_IO_Handler fails for ports 40h and 43h (VTD's) and succeeds
 *   for 42h and 61h: nothing traps the speaker's ports. A DOS box's tone
 *   sounds in front and, with background execution, behind (Windows or
 *   another box in front) without a break; a second box that plays
 *   reprograms channel 2 under the first and its end silences both; the
 *   System VM's MessageBeep (USER, MMSOUND.DRV) does the same to a box
 *   playing behind it.
 * - A full-screen box without background execution that is playing when
 *   Alt+Tab takes Windows to the front stops sounding at once; when it
 *   comes back to the front, its tone sounds again until it ends it.
 * Sources: the DDK's Virtual Device Adaptation Guide (VSD_Get_Version,
 * VSD_Bell: "a 1/2 second tone ... it returns immediately", the control
 * messages, Set_Global_Time_Out, Cancel_Time_Out, Allocate_Device_CB_Area);
 * the Intel 8254 data sheet and the IBM PC/AT technical reference (port
 * 61h: bit 0 timer 2's gate, bit 1 the speaker's data).
 * UNVERIFIED: a VSD_Bell while one is sounding (here the half second
 * starts again); whether Windows 3.1's VSD puts back the channel 2 count
 * and port 61h a DOS box had after a bell (here: see vsd_time_out); and
 * how it uses Call_When_Task_Switched (it links to it: the observed
 * behaviour needs no task-switch callback here).
 */
#include "w16vxd.h"

#define NULL ((void *)0)

void __cdecl vmm_set_global_time_out(struct vregs *r);
void __cdecl vmm_cancel_time_out(struct vregs *r);
vu32 __cdecl vmm_alloc_cb_area(vu32 size, vu32 flags);
extern char vsd_tmo[];                  /* vsdddb.asm: ENTRY for vsd_time_out */
#pragma aux vsd_tmo "*"

static vu32 inb(vu32 port);
static void outb(vu32 port, vu32 v);
#pragma aux inb = "in al, dx" "movzx eax, al" parm [edx] value [eax] modify exact [eax];
#pragma aux outb = "out dx, al" parm [edx] [eax] modify exact [];

#define SPK_BITS   0x03                 /* port 61h: timer 2 gate, speaker data */
#define BELL_COUNT 1337                 /* 892.7 Hz, as Windows 3.1's bell */
#define BELL_MS    500

static vu32 bell_tmo;                   /* the bell's time-out, 0 when silent */
static vu32 cb_off;                     /* each VM's saved speaker bits */
#define SAVED(vm) (*(vu32 *)((vm) + cb_off))

static void vregs_clear(struct vregs *r)
{
    vu32 *p = (vu32 *)r, i;
    for (i = 0; i < sizeof(*r) / 4; i++) p[i] = 0;
}

#pragma aux flags_save = "pushfd" "pop eax" "cli" value [eax] modify exact [eax];
static vu32 flags_save(void);
#pragma aux flags_back = "push eax" "popfd" parm [eax] modify exact [];
static void flags_back(vu32 f);

/* the bell's half second is over: the speaker off */
void __cdecl vsd_time_out(struct vregs *r)
{
    vu32 f = flags_save();
    bell_tmo = 0;
    outb(0x61, inb(0x61) & ~SPK_BITS);
    flags_back(f);
    r->eflags &= ~VF_CF;
}

/* VSD_Bell: channel 2 at the bell's count, the speaker on, and a global
 * time-out to end it; back at once */
static void bell(void)
{
    struct vregs s;
    vu32 f = flags_save();
    outb(0x43, 0xB6);                   /* channel 2, low then high byte, mode 3 */
    outb(0x42, BELL_COUNT & 0xFF);
    outb(0x42, BELL_COUNT >> 8);
    outb(0x61, inb(0x61) | SPK_BITS);
    if (bell_tmo) {                     /* one sounding: the half second again */
        vregs_clear(&s);
        s.esi = bell_tmo;
        vmm_cancel_time_out(&s);
        bell_tmo = 0;
    }
    vregs_clear(&s);
    s.eax = BELL_MS;
    s.esi = (vu32)vsd_tmo;
    vmm_set_global_time_out(&s);
    bell_tmo = s.esi;
    flags_back(f);
}

void __cdecl vsd_service(struct vregs *r)
{
    switch (r->arg) {
    case 0:                             /* VSD_Get_Version */
        r->eax = (r->eax & 0xFFFF0000u) | 0x0200;
        r->eflags = (r->eflags & ~VF_CF) | VF_ZF;
        return;
    case 1:                             /* VSD_Bell: nothing changed but the flags */
        bell();
        r->eflags &= ~(VF_CF | VF_ZF);
        return;
    }
    r->eflags |= VF_CF;
}

#define SYS_CRITICAL_INIT 0x00
#define CREATE_VM         0x07
#define VM_SUSPEND        0x0D
#define VM_RESUME         0x0E

void __cdecl vsd_control(struct vregs *r)
{
    vu32 v;
    r->eflags &= ~VF_CF;
    switch (r->eax) {
    case SYS_CRITICAL_INIT:
        cb_off = vmm_alloc_cb_area(4, 0);
        break;
    case CREATE_VM:
        if (cb_off) SAVED(r->ebx) = 0;
        break;
    case VM_SUSPEND:
        /* a suspended VM is not heard: its speaker bits kept and cleared
         * (Windows 3.1: a full-screen box stopped behind went silent) */
        if (!cb_off) break;
        v = inb(0x61);
        SAVED(r->ebx) = v & SPK_BITS;
        if (v & SPK_BITS) outb(0x61, v & ~SPK_BITS);
        break;
    case VM_RESUME:
        /* and heard again when it runs (Windows 3.1: its tone sounded
         * again when the box came back to the front) */
        if (!cb_off || !SAVED(r->ebx)) break;
        outb(0x61, inb(0x61) | SAVED(r->ebx));
        SAVED(r->ebx) = 0;
        break;
    }
}
