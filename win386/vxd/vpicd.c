/* vpicd.c - w16's VPICD, the virtual programmable interrupt controller
 * device (device 0003h), a module of its own in WIN386.EXE's W3 table
 * (ARCHITECTURE.md 12.8).
 *
 * It owns both 8259s: it moves their vectors to 50h/58h at
 * Sys_Critical_Init and back at Sys_Critical_Exit, and every hardware
 * interrupt the VMM takes comes here (the private link in w16vxd.h). An
 * IRQ a device has virtualised (VPICD_Virtualize_IRQ) goes to its
 * VID_Hw_Int_Proc; the others, and requests the devices set
 * (VPICD_Set_Int_Request), are simulated into the System VM, the only VM
 * so far, through the VMM.
 *
 * Stage 1 limits (ARCHITECTURE.md 12.8): no virtual PIC per VM yet. The
 * System VM's protected-mode code runs with IOPL 3, so its port 20h/21h/
 * A0h/A1h accesses (EOI, masks) reach the 8259s themselves and cannot be
 * trapped; so a simulated interrupt is ended by the VM's own EOI on the
 * hardware, VID_EOI_Proc, VID_Mask_Change_Proc and VID_IRET_Proc are not
 * called, and a request is taken as delivered when it is simulated (edge,
 * not the DDK's level-triggered virtual PIC).
 *
 * Sources: the Windows 3.1 DDK's Virtual Device Adaptation Guide (.tools/
 * docs/vdag31: the VPICD services, VPICD_IRQ_Descriptor, the VID_*
 * procedures); the 8259A data sheet; what VXP.386 saw under Windows 3.1
 * (tests/vxpprobe.sh): Get_Version AX=030Ah EBX=1 ECX=0Fh;
 * Get_IRQ_Complete_Status carry set for an IRQ a device has (IRQ 0, 1),
 * clear for one nobody has (5: ECX=4, physically masked; 12 before the
 * mouse device took it: ECX=0); Convert_IRQ_To_Int 1 -> 9, 12 -> 74h, 16
 * carry set; Convert_Int_To_IRQ 74h and 21h carry set.
 */
#include "w16vxd.h"

#define NULL ((void *)0)
#define NIRQ 16

/* the VxD runtime and the dynamic links (vpicdddb.asm, vxdrt.asm) */
void __cdecl vxd_call_proc(vu32 proc, struct vregs *r);

static void outb(vu32 port, vu32 v);
static vu32 inb(vu32 port);
#pragma aux outb = "out dx, al" parm [edx] [eax] modify exact [];
#pragma aux inb = "in al, dx" "movzx eax, al" parm [edx] value [eax] modify exact [eax];

/* an IRQ a device virtualised; its handle is the address of this */
struct virq {
    vu32 magic;
    struct vid d;
    vu32 irq;
    int requested;          /* VPICD_Set_Int_Request for the System VM */
    int eoi_owed;           /* VPICD_Phys_EOI during VID_Hw_Int_Proc */
};
#define VIRQ_MAGIC 0x51524956u      /* "VIRQ" */
static struct virq virqs[NIRQ];
static struct virq *owner[NIRQ];
static vu32 hw_procs[8], n_hw_procs;   /* VPICD_Call_When_Hw_Int */
static int in_hw;                       /* in hw_int, for the IRQ in_irq */
static vu32 in_irq;
static vu32 sys_vm;
static vu8 masks1, masks2;              /* the IMRs before Windows */
static vu32 pending;                    /* requests not yet simulated (bit per IRQ) */

static void hw_int(vu32 irq);
static void resume(void);
struct vpicd_link vpicd_link = { VPICD_LINK_MAGIC, hw_int, resume };

/* ---- the 8259s ---- */
static void pic_program(vu32 b1, vu32 b2)
{
    vu32 m1 = inb(0x21), m2 = inb(0xA1);
    outb(0x20, 0x11); outb(0xA0, 0x11);     /* ICW1: edge, cascade, ICW4 */
    outb(0x21, b1);   outb(0xA1, b2);       /* ICW2: vector bases */
    outb(0x21, 4);    outb(0xA1, 2);        /* ICW3: slave on IRQ 2 */
    outb(0x21, 1);    outb(0xA1, 1);        /* ICW4: 8086 mode */
    outb(0x21, m1);   outb(0xA1, m2);
}

/* a specific EOI for irq (and the cascade for the slave's) */
static void phys_eoi(vu32 irq)
{
    if (irq >= 8) {
        outb(0xA0, 0x60 | (irq - 8));
        outb(0x20, 0x62);
    } else
        outb(0x20, 0x60 | irq);
}

static int phys_masked(vu32 irq)
{
    return irq < 8 ? (inb(0x21) >> irq) & 1 : (inb(0xA1) >> (irq - 8)) & 1;
}

static void phys_mask(vu32 irq, int on)
{
    vu32 port = irq < 8 ? 0x21 : 0xA1, bit = 1u << (irq & 7), m = inb(port);
    outb(port, on ? (m | bit) : (m & ~bit));
}

static int phys_request(vu32 irq)
{
    vu32 port = irq < 8 ? 0x20 : 0xA0, v;
    outb(port, 0x0A);                       /* OCW3: read the IRR */
    v = inb(port);
    return (v >> (irq & 7)) & 1;
}

static int phys_in_service(vu32 irq)
{
    vu32 port = irq < 8 ? 0x20 : 0xA0, v;
    outb(port, 0x0B);                       /* OCW3: read the ISR */
    v = inb(port);
    outb(port, 0x0A);
    return (v >> (irq & 7)) & 1;
}

/* the System VM's vector for an IRQ: the real-mode PIC bases (DPMI 0.9:
 * the VM's IRQs are INT 08h-0Fh, 70h-77h; DPMI 0400h reports 08h/70h) */
static vu32 irq_vector(vu32 irq)
{
    return irq < 8 ? vpicd_link.rm_base1 + irq : vpicd_link.rm_base2 + irq - 8;
}

/* ---- status bits (Get_Status / Get_Complete_Status). UNVERIFIED: their
 * values in Windows 3.1, except as VXP suggests: Phys_Mask 4 (IRQ 5,
 * masked, gave ECX=4) and Phys_Req 40h (IRQ 0 gave 40h: the tick pending
 * in the IRR, presumably) ---- */
#define ST_IRET_PENDING 0x01
#define ST_IN_SERVICE   0x02
#define ST_PHYS_MASK    0x04
#define ST_PHYS_IN_SERV 0x08
#define ST_VIRT_MASK    0x10
#define ST_VIRT_REQ     0x20
#define ST_PHYS_REQ     0x40
#define ST_VIRT_DEV_REQ 0x80

static vu32 irq_status(vu32 irq, struct virq *caller)
{
    vu32 s = 0;
    if (phys_masked(irq)) s |= ST_PHYS_MASK;
    if (phys_in_service(irq)) s |= ST_PHYS_IN_SERV;
    if (phys_request(irq)) s |= ST_PHYS_REQ;
    if (pending & (1u << irq)) s |= ST_VIRT_REQ;
    if (caller && caller->requested) s |= ST_VIRT_DEV_REQ;
    return s;
}

static struct virq *handle(vu32 h)
{
    struct virq *v = (struct virq *)h;
    if (v < virqs || v >= virqs + NIRQ || v->magic != VIRQ_MAGIC) return NULL;
    return v;
}

/* ---- simulating interrupts into the VM ---- */
static void simulate(vu32 irq)
{
    struct virq *v = owner[irq];
    struct vregs r;
    if (v && v->d.virt_int_proc) {          /* VID_Virt_Int_Proc: about to be simulated */
        r.eax = (vu32)v; r.ebx = sys_vm; r.eflags = 0;
        vxd_call_proc(v->d.virt_int_proc, &r);
    }
    /* cleared first: the VM's handler runs inside deliver, and the traps
     * on the way must not simulate the same request again */
    pending &= ~(1u << irq);
    if (!vpicd_link.deliver(irq_vector(irq)))
        pending |= 1u << irq;           /* the VM has interrupts off: later */
}

/* the VMM: a hardware interrupt on irq (interrupts off) */
static void hw_int(vu32 irq)
{
    struct virq *v = owner[irq];
    struct vregs r;
    vu32 i;

    for (i = 0; i < n_hw_procs; i++) {      /* VPICD_Call_When_Hw_Int */
        r.eax = irq; r.ebx = sys_vm; r.eflags = 0;
        vxd_call_proc(hw_procs[i], &r);
    }
    if (!v) {
        /* nobody's: to the System VM, whose own EOI ends it */
        pending |= 1u << irq;
        simulate(irq);
        return;
    }
    in_hw = 1;
    in_irq = irq;
    v->eoi_owed = 0;
    r.eax = (vu32)v; r.ebx = sys_vm; r.eflags = 0;
    vxd_call_proc(v->d.hw_int_proc, &r);
    in_hw = 0;
    if (pending & (1u << irq)) {
        /* requested: the VM's handler ends it on the hardware */
        simulate(irq);
    } else if (v->eoi_owed)
        phys_eoi(irq);
}

/* the VMM is about to resume the VM: requests set outside a hardware
 * interrupt are simulated now (if the VM takes interrupts) */
static void resume(void)
{
    vu32 irq;
    if (!pending) return;
    for (irq = 0; irq < NIRQ; irq++)
        if (pending & (1u << irq)) simulate(irq);
}

/* ---- the services ---- */
void __cdecl vpicd_service(struct vregs *r)
{
    struct virq *v;
    vu32 irq;

    r->eflags &= ~VF_CF;
    switch (r->arg) {
    case 0x00:                              /* VPICD_Get_Version */
        r->eax = 0x030A;
        r->ebx = 1;                         /* observed; UNVERIFIED meaning (flags) */
        r->ecx = NIRQ - 1;                  /* the highest IRQ */
        return;
    case 0x01: {                            /* VPICD_Virtualize_IRQ: EDI the descriptor */
        struct vid *d = (struct vid *)r->edi;
        irq = d->irq;
        if (irq >= NIRQ || irq == 2 || owner[irq] || !d->hw_int_proc) { r->eflags |= VF_CF; return; }
        v = &virqs[irq];
        v->magic = VIRQ_MAGIC;
        v->d = *d;
        v->irq = irq;
        v->requested = 0;
        owner[irq] = v;
        r->eax = (vu32)v;
        return;
    }
    case 0x02:                              /* VPICD_Set_Int_Request: EAX handle, EBX VM */
        if ((v = handle(r->eax)) == NULL) return;
        v->requested = 1;
        pending |= 1u << v->irq;
        return;                             /* simulated after the Hw_Int_Proc or at resume */
    case 0x03:                              /* VPICD_Clear_Int_Request */
        if ((v = handle(r->eax)) == NULL) return;
        v->requested = 0;
        pending &= ~(1u << v->irq);
        return;
    case 0x04:                              /* VPICD_Phys_EOI: EAX handle */
        if ((v = handle(r->eax)) == NULL) return;
        if (in_hw && in_irq == v->irq) v->eoi_owed = 1;
        else phys_eoi(v->irq);
        return;
    case 0x05:                              /* VPICD_Get_Complete_Status: EAX handle -> ECX */
    case 0x06:                              /* VPICD_Get_Status (UNVERIFIED: the same bits) */
        if ((v = handle(r->eax)) == NULL) { r->ecx = 0; return; }
        r->ecx = irq_status(v->irq, v);
        return;
    case 0x07:                              /* VPICD_Test_Phys_Request: carry = the IRR bit */
        if ((v = handle(r->eax)) != NULL && phys_request(v->irq)) r->eflags |= VF_CF;
        return;
    case 0x08:                              /* VPICD_Physically_Mask */
    case 0x09:                              /* VPICD_Physically_Unmask */
        if ((v = handle(r->eax)) != NULL) phys_mask(v->irq, r->arg == 0x08);
        return;
    case 0x0A:                              /* VPICD_Set_Auto_Masking: nothing to do yet */
        return;
    case 0x0B:                              /* VPICD_Get_IRQ_Complete_Status: EAX IRQ */
        irq = r->eax;
        if (irq >= NIRQ) { r->eflags |= VF_CF; r->ecx = 0; return; }
        r->ecx = irq_status(irq, NULL);
        if (owner[irq] || irq == 2) r->eflags |= VF_CF;
        return;
    case 0x0C:                              /* VPICD_Convert_Handle_To_IRQ */
        if ((v = handle(r->eax)) == NULL) { r->eflags |= VF_CF; return; }
        r->esi = v->irq;                    /* UNVERIFIED register */
        r->eax = v->irq;
        return;
    case 0x0D:                              /* VPICD_Convert_IRQ_To_Int: EAX IRQ, EBX VM */
        if (r->eax >= NIRQ) { r->eflags |= VF_CF; return; }
        r->eax = irq_vector(r->eax);
        return;
    case 0x0E:                              /* VPICD_Convert_Int_To_IRQ: EAX vector */
        /* Windows 3.1 refused 74h (IRQ 12's vector in the System VM) and
         * 21h. UNVERIFIED which vectors it maps: w16 maps VPICD's own
         * hardware vectors (50h-5Fh) and nothing else */
        if (r->eax >= VPICD_BASE1 && r->eax < VPICD_BASE1 + NIRQ && r->eax != VPICD_BASE1 + 2) {
            r->eax -= VPICD_BASE1;
            return;
        }
        r->eflags |= VF_CF;
        return;
    case 0x0F:                              /* VPICD_Call_When_Hw_Int: ESI the procedure */
        if (n_hw_procs == sizeof(hw_procs) / sizeof(hw_procs[0])) { r->eflags |= VF_CF; return; }
        hw_procs[n_hw_procs++] = r->esi;
        return;
    case 0x10:                              /* VPICD_Force_Default_Owner */
    case 0x11:                              /* VPICD_Force_Default_Behavior */
    case 0x12:                              /* VPICD_Auto_Mask_At_Inst_Swap */
    case 0x13:                              /* VPICD_Begin_Inst_Page_Swap */
    case 0x14:                              /* VPICD_End_Inst_Page_Swap */
        return;                             /* one VM, no instance swapping: nothing to do */
    }
    r->eflags |= VF_CF;
}

/* ---- the control procedure ---- */
#define SYS_CRITICAL_INIT 0x00
#define SYS_CRITICAL_EXIT 0x06

void __cdecl vpicd_control(struct vregs *r)
{
    r->eflags &= ~VF_CF;
    switch (r->eax) {
    case SYS_CRITICAL_INIT:
        sys_vm = r->ebx;
        masks1 = inb(0x21);
        masks2 = inb(0xA1);
        pic_program(VPICD_BASE1, VPICD_BASE2);
        break;
    case SYS_CRITICAL_EXIT:
        pic_program(vpicd_link.rm_base1, vpicd_link.rm_base2);
        outb(0x21, masks1);
        outb(0xA1, masks2);
        break;
    }
}

/* the PM (and V86) API: function 0, the version (VX3 under Windows 3.1:
 * AX=030Ah) */
void __cdecl vpicd_api(struct vregs *r)
{
    struct client_regs *c = (struct client_regs *)r->ebp;
    if ((c->eax & 0xFFFF) == 0) {
        c->eax = (c->eax & 0xFFFF0000u) | 0x030A;
        c->eflags &= ~VF_CF;
    } else
        c->eflags |= VF_CF;
}
