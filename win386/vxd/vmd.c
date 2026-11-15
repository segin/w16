/* vmd.c - w16's VMD, the virtual mouse device (device 000Ch), a module
 * of WIN386.EXE's W3 table (ARCHITECTURE.md 12.8).
 *
 * It takes the PS/2 mouse's IRQ 12 from VPICD and passes every mouse
 * interrupt to the System VM, the mouse's owner (one VM so far), where the
 * BIOS's IRQ 12 handler and MOUSE.DRV's PS/2 handler (INT 15h C207h,
 * bridged into protected mode by the VMM's DPMI host) take it as before.
 *
 * Observed under Windows 3.1 (VXP, VX5; ARCHITECTURE.md 12.8):
 * VMD_Get_Version AX=0300h, carry clear; VMD_Get_Mouse_Owner EBX=0 at
 * Sys_Critical_Init and the System VM at System_Exit; VMD_Set_Mouse_Type
 * with EAX=-74h (INT 74h) ECX=4 gives EAX=1 (could not virtualise),
 * carry set; VPICD shows IRQ 12 not virtualised at Sys_Critical_Init and
 * virtualised at System_Exit. The PM API: function 0 AX=0300h; MOUSE.DRV's
 * AX=0100h (BX=7404h CX=0071h DX=0500h) fails, carry set, every register
 * unchanged, also as the first call.
 * UNVERIFIED: at which message Windows 3.1's VMD takes IRQ 12 (w16:
 * Device_Init), serial and bus mice, what the other API functions do.
 * Sources: the DDK's Virtual Device Adaptation Guide (VMD_Get_Version,
 * VMD_Set_Mouse_Type, VMD_Get_Mouse_Owner, the VPICD services).
 */
#include "w16vxd.h"

#define NULL ((void *)0)

void __cdecl vpicd_virtualize_irq(struct vregs *r);
void __cdecl vpicd_set_int_request(struct vregs *r);
void __cdecl vpicd_clear_int_request(struct vregs *r);
void __cdecl vpicd_phys_eoi(struct vregs *r);
void __cdecl vpicd_physically_unmask(struct vregs *r);
extern char vmd_eoi[];
#pragma aux vmd_eoi "*"
void __cdecl vpicd_convert_int_to_irq(struct vregs *r);
extern char vmd_hw[];                   /* vmdddb.asm: ENTRY for vmd_hw_int */
#pragma aux vmd_hw "*"

static struct vid vmd_vid;
static vu32 irq_handle;                 /* VPICD's, 0 until IRQ 12 is ours */
static vu32 sys_vm, owner;

static void vregs_clear(struct vregs *r)
{
    vu32 *p = (vu32 *)r, i;
    for (i = 0; i < sizeof(*r) / 4; i++) p[i] = 0;
}

/* VPICD_Virtualize_IRQ for irq; 0 if VPICD refused */
static vu32 take_irq(vu32 irq)
{
    struct vregs r;
    vregs_clear(&r);
    vmd_vid.irq = (vu16)irq;
    vmd_vid.options = 0;
    vmd_vid.hw_int_proc = (vu32)vmd_hw;
    vmd_vid.eoi_proc = (vu32)vmd_eoi;
    vmd_vid.iret_time_out = 500;
    r.edi = (vu32)&vmd_vid;
    vpicd_virtualize_irq(&r);
    if (r.eflags & VF_CF) return 0;
    /* physically open whatever the VM masks: Windows 3.1 showed IRQ 12
     * not physically masked (VXP's status 0 at System_Exit) after
     * MOUSE.DRV had masked it in the VM (UNVERIFIED that this is how) */
    irq = r.eax;
    vregs_clear(&r);
    r.eax = irq;
    vpicd_physically_unmask(&r);
    return irq;
}

/* VID_EOI_Proc: the VM's handler is done; the next mouse byte may come */
void __cdecl vmd_eoi_proc(struct vregs *r)
{
    struct vregs s;
    vregs_clear(&s);
    s.eax = r->eax;
    s.ebx = r->ebx;
    vpicd_clear_int_request(&s);
    vpicd_phys_eoi(&s);
    r->eflags &= ~VF_CF;
}

/* VID_Hw_Int_Proc: the mouse's interrupt goes to its owner */
void __cdecl vmd_hw_int(struct vregs *r)
{
    struct vregs s;
    vregs_clear(&s);
    s.eax = r->eax;
    s.ebx = owner ? owner : sys_vm;
    vpicd_set_int_request(&s);
    r->eflags &= ~VF_CF;
}

void __cdecl vmd_service(struct vregs *r)
{
    struct vregs s;
    vu32 irq;

    r->eflags &= ~VF_CF;
    switch (r->arg) {
    case 0:                                 /* VMD_Get_Version */
        r->eax = 0x0300;
        return;
    case 1:                                 /* VMD_Set_Mouse_Type: EAX IRQ or -vector, ECX type */
        irq = r->eax;
        if ((int)irq < 0) {
            vregs_clear(&s);
            s.eax = -(int)irq;
            vpicd_convert_int_to_irq(&s);
            if (s.eflags & VF_CF) { r->eax = 1; r->eflags |= VF_CF; return; }
            irq = s.eax;
        }
        if (irq_handle) { r->eax = 0; r->eflags |= VF_CF; return; }    /* already virtualised */
        irq_handle = take_irq(irq);
        if (!irq_handle) { r->eax = 1; r->eflags |= VF_CF; return; }
        owner = sys_vm;
        return;
    case 2:                                 /* VMD_Get_Mouse_Owner */
        r->ebx = owner;
        return;
    }
    r->eflags |= VF_CF;
}

#define SYS_CRITICAL_INIT 0x00
#define DEVICE_INIT       0x01

void __cdecl vmd_control(struct vregs *r)
{
    r->eflags &= ~VF_CF;
    switch (r->eax) {
    case SYS_CRITICAL_INIT:
        sys_vm = r->ebx;
        break;
    case DEVICE_INIT:
        /* the PS/2 mouse's IRQ (UNVERIFIED: the message; Windows 3.1's VMD
         * had not taken it at Sys_Critical_Init but had by System_Exit) */
        if (!irq_handle && (irq_handle = take_irq(12)) != 0)
            owner = sys_vm;
        break;
    }
}

/* the PM (and V86) API */
void __cdecl vmd_api(struct vregs *r)
{
    struct client_regs *c = (struct client_regs *)r->ebp;
    switch (c->eax & 0xFFFF) {
    case 0x0000:                            /* version */
        c->eax = (c->eax & 0xFFFF0000u) | 0x0300;
        c->eflags &= ~VF_CF;
        return;
    case 0x0100:                            /* MOUSE.DRV's: fails under Windows 3.1 too */
    default:
        c->eflags |= VF_CF;
        return;
    }
}
