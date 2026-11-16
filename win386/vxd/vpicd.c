/* vpicd.c - w16's VPICD, the virtual programmable interrupt controller
 * device (device 0003h), a module of its own in WIN386.EXE's W3 table
 * (ARCHITECTURE.md 12.8).
 *
 * The 8259s. VPICD owns them: it moves their vectors to 50h/58h at
 * Sys_Critical_Init and back at Sys_Critical_Exit, and every hardware
 * interrupt the VMM takes comes here (the private link in w16vxd.h). As
 * the DDK says of Windows 3.1's ("VPICD does not prioritize hardware
 * interrupts"), a hardware interrupt is ended on the 8259 at once and its
 * IRQ physically masked while it is "physically in service", until
 * VPICD_Phys_EOI. An IRQ a device has virtualised (VPICD_Virtualize_IRQ)
 * then goes to its VID_Hw_Int_Proc; one nobody has gets VPICD's default
 * handling: a request into the System VM, and VPICD_Phys_EOI when the VM
 * ends it.
 *
 * The virtual PIC. The System VM (the only VM so far) has a PIC of its
 * own: an IMR, an IRR (the requests devices set with
 * VPICD_Set_Int_Request, level-triggered: a request stays until
 * VPICD_Clear_Int_Request) and an ISR, programmed and read through ports
 * 20h, 21h, A0h and A1h, which the VMM traps (Install_Mult_IO_Handlers)
 * and hands here: ICW1-ICW4 (the VM's vector bases), OCW1 (the mask:
 * VID_Mask_Change_Proc for a change), OCW2 (EOI, non-specific or
 * specific: the ISR bit cleared and VID_EOI_Proc called), OCW3 (which of
 * IRR and ISR a read gives). A request is simulated into the VM, the
 * highest priority first (0, 1, 8-15 on the cascade, 3-7), when the VM
 * has interrupts on, the IRQ is not masked and nothing of equal or higher
 * priority is in service; VID_Virt_Int_Proc comes first; the VM's
 * handler's IRET calls VID_IRET_Proc, or the descriptor's time-out does
 * (carry set) if the IRET has not come by then (a VMM global time-out).
 * Physical masking follows the VM's mask (auto-masking, the default)
 * unless the IRQ's device used VPICD_Physically_Mask/Unmask.
 *
 * Sources: the Windows 3.1 DDK's Virtual Device Adaptation Guide (.tools/
 * docs/vdag31: the VPICD services, VPICD_IRQ_Descriptor, the VID_*
 * procedures, Set_Global_Time_Out); the 8259A data sheet (ICWs, OCWs,
 * priorities, the cascade). Observed under Windows 3.1:
 *   VXP (tests/vxpprobe.sh): Get_Version AX=030Ah EBX=1 ECX=0Fh;
 *   Get_IRQ_Complete_Status carry set for an IRQ a device has (IRQ 0, 1),
 *   clear for one nobody has (5: ECX=4, physically masked; 12 before the
 *   mouse device took it: ECX=0); Convert_IRQ_To_Int 1 -> 9, 12 -> 74h, 16
 *   carry set; Convert_Int_To_IRQ 74h and 21h carry set.
 *   VXI and VX7 (tests/vxobs.sh io: a probe device virtualising IRQ 10,
 *   every VID procedure its own): Get_Complete_Status after
 *   Set_Int_Request A0h (Virt_Dev_Req 80h, Virt_Req 20h), in
 *   VID_Virt_Int_Proc A2h (In_Service 2), in VID_EOI_Proc A1h (In_Service
 *   gone, IRET_Pending 1), after Clear_Int_Request and Phys_EOI 1, after
 *   the IRET 0; masking the IRQ calls VID_Mask_Change_Proc with ECX 1
 *   (unmasking: 0) and status 14h (Virt_Mask 10h, Phys_Mask 4: auto-
 *   masked); the order is Virt_Int_Proc, EOI_Proc, IRET_Proc (carry
 *   clear); with the handler waiting a second, VID_IRET_Proc comes with
 *   carry set before the EOI (status A2h), and not again at the IRET.
 *   The VM's handler reads the slave's IRR 0 (in service is not
 *   requested), its ISR 04h, the master's ISR 04h (the cascade), and the
 *   ISR 0 after its EOI.
 * UNVERIFIED: Phys_In_Serv's value (8 here) and Phys_Req's (40h: VXP's
 * IRQ 0 status suggests it); IRET_Pending for a device with no
 * VID_IRET_Proc (not tracked here); the masks the 8259s get at exit (here:
 * the System VM's).
 */
#include "w16vxd.h"

#define NULL ((void *)0)
#define NIRQ 16

/* the VxD runtime and the dynamic links (vpicdddb.asm, vxdrt.asm) */
void __cdecl vxd_call_proc(vu32 proc, struct vregs *r);
void __cdecl vmm_install_mult_io(struct vregs *r);
void __cdecl vmm_simulate_io(struct vregs *r);
void __cdecl vmm_set_global_time_out(struct vregs *r);
void __cdecl vmm_cancel_time_out(struct vregs *r);
extern vu8 vpicd_io_table[];
extern char vpicd_tmo[];                /* ENTRY: the IRET time-out */
#pragma aux vpicd_tmo "*"

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
    int phys_mode;          /* 0 auto-masking, 1 physically masked, 2 unmasked */
};
#define VIRQ_MAGIC 0x51524956u      /* "VIRQ" */
static struct virq virqs[NIRQ];
static struct virq *owner[NIRQ];
static vu32 hw_procs[8], n_hw_procs;   /* VPICD_Call_When_Hw_Int */
static vu32 sys_vm;

/* the hardware side */
static vu32 phys_isr;                   /* physically in service: masked until Phys_EOI */
static vu32 def_req;                    /* VPICD's own requests (nobody's IRQs) */
static vu32 dos_masks;                  /* the 8259s' masks before Windows */
static int vm_ready;                    /* Sys_VM_Init done: the VM's PIC set up */

/* the System VM's virtual PIC */
static struct vpic {
    vu32 imr, isr;                      /* bit per IRQ, the slave's in 8-15 */
    vu32 base[2];                       /* ICW2: the vector bases */
    int icw_step[2], icw4[2], single[2];
    int read_isr[2];                    /* OCW3: reads give the ISR */
} vp;

/* IRETs being waited for (a device's VID_IRET_Proc), innermost last */
#define NIRET 32
static struct { vu32 irq, tmo; int timed_out; } irets[NIRET];
static int n_irets;

static void hw_int(vu32 irq);
static void resume(void);
static void iret_done(void);
struct vpicd_link vpicd_link = { VPICD_LINK_MAGIC, hw_int, resume, iret_done };

static void vregs_clear(struct vregs *r)
{
    vu32 *p = (vu32 *)r, i;
    for (i = 0; i < sizeof(*r) / 4; i++) p[i] = 0;
}

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

static int phys_request(vu32 irq)
{
    vu32 port = irq < 8 ? 0x20 : 0xA0, v;
    outb(port, 0x0A);                       /* OCW3: read the IRR */
    v = inb(port);
    return (v >> (irq & 7)) & 1;
}

/* the 8259s' masks from the state: an IRQ is masked while physically in
 * service, when its device masked it, or (auto-masking) when the VM has;
 * the cascade (IRQ 2) is left open */
static void phys_update(void)
{
    vu32 m = 0, irq;
    for (irq = 0; irq < NIRQ; irq++) {
        struct virq *v = owner[irq];
        int on;
        if (irq == 2) continue;
        if (phys_isr & (1u << irq)) on = 1;
        else if (v && v->phys_mode == 1) on = 1;
        else if (v && v->phys_mode == 2) on = 0;
        else on = ((vm_ready ? vp.imr : dos_masks) >> irq) & 1;
        if (on) m |= 1u << irq;
    }
    outb(0x21, m & 0xFF);
    outb(0xA1, (m >> 8) & 0xFF);
}

/* the VM's vector for an IRQ (its ICW2 bases; DPMI 0400h reports 08h/70h) */
static vu32 irq_vector(vu32 irq)
{
    return irq < 8 ? vp.base[0] + irq : vp.base[1] + irq - 8;
}

static int requested(vu32 irq)
{
    return owner[irq] ? owner[irq]->requested : (def_req >> irq) & 1;
}

/* ---- status bits (Get_Status / Get_Complete_Status; observed: VXI) ---- */
#define ST_IRET_PENDING 0x01
#define ST_IN_SERVICE   0x02
#define ST_PHYS_MASK    0x04
#define ST_PHYS_IN_SERV 0x08            /* UNVERIFIED value */
#define ST_VIRT_MASK    0x10
#define ST_VIRT_REQ     0x20
#define ST_PHYS_REQ     0x40            /* UNVERIFIED value */
#define ST_VIRT_DEV_REQ 0x80

static int iret_pending(vu32 irq)
{
    int i;
    for (i = 0; i < n_irets; i++)
        if (irets[i].irq == irq && !irets[i].timed_out) return 1;
    return 0;
}

static vu32 irq_status(vu32 irq, struct virq *caller)
{
    vu32 s = 0;
    if (iret_pending(irq)) s |= ST_IRET_PENDING;
    if (vp.isr & (1u << irq)) s |= ST_IN_SERVICE;
    if (phys_masked(irq)) s |= ST_PHYS_MASK;
    if (phys_isr & (1u << irq)) s |= ST_PHYS_IN_SERV;
    if (vp.imr & (1u << irq)) s |= ST_VIRT_MASK;
    if (requested(irq)) s |= ST_VIRT_REQ;
    if (phys_request(irq)) s |= ST_PHYS_REQ;
    if (caller && caller->requested) s |= ST_VIRT_DEV_REQ;
    return s;
}

static struct virq *handle(vu32 h)
{
    struct virq *v = (struct virq *)h;
    if (v < virqs || v >= virqs + NIRQ || v->magic != VIRQ_MAGIC) return NULL;
    return v;
}

static void call_vid(vu32 proc, struct virq *v, vu32 ecx, vu32 flags)
{
    struct vregs r;
    if (!proc) return;
    vregs_clear(&r);
    r.eax = (vu32)v;
    r.ebx = sys_vm;
    r.ecx = ecx;
    r.eflags = flags;
    vxd_call_proc(proc, &r);
}

/* ---- simulating interrupts into the VM ---- */

/* priority order: IRQ 0, 1, the cascade (8-15), 3-7 */
static const vu8 prio[15] = { 0, 1, 8, 9, 10, 11, 12, 13, 14, 15, 3, 4, 5, 6, 7 };

/* the IRQ to simulate next, or -1 */
static int next_irq(void)
{
    vu32 i, irq;
    for (i = 0; i < 15; i++) {
        irq = prio[i];
        if (irq == 8 && (vp.isr & 4)) return -1;    /* the cascade in service */
        if (vp.isr & (1u << irq)) return -1;        /* equal or higher in service */
        if (irq >= 8 && (vp.imr & 4)) continue;     /* the cascade masked */
        if (requested(irq) && !(vp.imr & (1u << irq))) return (int)irq;
    }
    return -1;
}

/* simulate what can be; called with the VM where it was interrupted */
static void deliver_pending(void)
{
    int irq;
    while ((irq = next_irq()) >= 0) {
        struct virq *v = owner[irq];
        int want_iret = v && v->d.iret_proc;
        if (!vpicd_link.can_deliver()) return;          /* interrupts off */
        vp.isr |= 1u << irq;
        if (irq >= 8) vp.isr |= 4;
        if (v) call_vid(v->d.virt_int_proc, v, 0, 0);
        if (want_iret && n_irets < NIRET) {
            struct vregs r;
            irets[n_irets].irq = irq;
            irets[n_irets].timed_out = 0;
            irets[n_irets].tmo = 0;
            if (v->d.iret_time_out) {
                vregs_clear(&r);
                r.eax = v->d.iret_time_out;
                r.edx = (vu32)&irets[n_irets];
                r.esi = (vu32)vpicd_tmo;
                vmm_set_global_time_out(&r);
                irets[n_irets].tmo = r.esi;
            }
            n_irets++;
        } else
            want_iret = 0;
        vpicd_link.deliver(irq_vector(irq), want_iret);
    }
}

/* the VMM: the IRET of a simulated interrupt that asked for it */
static void iret_done(void)
{
    struct vregs r;
    vu32 irq;
    int timed_out;
    if (!n_irets) return;
    n_irets--;
    irq = irets[n_irets].irq;
    timed_out = irets[n_irets].timed_out;
    if (irets[n_irets].tmo) {
        vregs_clear(&r);
        r.esi = irets[n_irets].tmo;
        vmm_cancel_time_out(&r);
    }
    if (!timed_out && owner[irq])
        call_vid(owner[irq]->d.iret_proc, owner[irq], 0, 0);     /* carry clear */
}

/* the IRET time-out (VMM global time-out: EDX our entry) */
void __cdecl vpicd_time_out(struct vregs *r)
{
    int i;
    for (i = 0; i < n_irets; i++)
        if ((vu32)&irets[i] == r->edx && !irets[i].timed_out) {
            vu32 irq = irets[i].irq;
            irets[i].timed_out = 1;
            irets[i].tmo = 0;
            if (owner[irq]) call_vid(owner[irq]->d.iret_proc, owner[irq], 0, VF_CF);
            return;
        }
}

/* the VM ended irq (an EOI on its virtual PIC) */
static void virt_eoi(vu32 irq)
{
    struct virq *v = owner[irq];
    vp.isr &= ~(1u << irq);
    if (irq == 2) return;
    if (v) {
        if (v->d.eoi_proc) call_vid(v->d.eoi_proc, v, 0, 0);
        return;
    }
    /* VPICD's own: the request done, the IRQ open again */
    def_req &= ~(1u << irq);
    phys_isr &= ~(1u << irq);
    phys_update();
}

/* the VMM: a hardware interrupt on irq (interrupts off) */
static void hw_int(vu32 irq)
{
    struct virq *v = owner[irq];
    struct vregs r;
    vu32 i;

    /* ended on the 8259 now, masked until VPICD_Phys_EOI */
    phys_isr |= 1u << irq;
    phys_update();
    phys_eoi(irq);
    for (i = 0; i < n_hw_procs; i++) {      /* VPICD_Call_When_Hw_Int */
        vregs_clear(&r);
        r.eax = irq; r.ebx = sys_vm;
        vxd_call_proc(hw_procs[i], &r);
    }
    if (v) call_vid(v->d.hw_int_proc, v, 0, 0);
    else def_req |= 1u << irq;              /* to the System VM */
    deliver_pending();
}

/* the VMM is about to resume the VM */
static void resume(void)
{
    deliver_pending();
}

/* ---- the VM's ports 20h, 21h, A0h, A1h (trapped by the VMM) ---- */
#define IO_OUTPUT 0x04
#define IO_SIZE   0x18
#define IO_STRING 0x20

static vu32 lowest(vu32 bits)
{
    vu32 i;
    for (i = 0; i < 8; i++) if (bits & (1u << i)) return i;
    return 8;
}

static void pic_command(int c, vu32 v)
{
    vu32 shift = c ? 8 : 0, i;
    if (v & 0x10) {                         /* ICW1 */
        vp.icw_step[c] = 1;
        vp.icw4[c] = v & 1;
        vp.single[c] = (v >> 1) & 1;
        vp.imr &= ~(0xFFu << shift);
        vp.isr &= ~(0xFFu << shift);
        vp.read_isr[c] = 0;
        return;
    }
    if (v & 0x08) {                         /* OCW3 */
        if (v & 2) vp.read_isr[c] = v & 1;
        return;
    }
    switch (v >> 5) {                       /* OCW2 */
    case 1: case 5:                         /* non-specific EOI (rotating too) */
        i = lowest((vp.isr >> shift) & 0xFF);
        if (i < 8) virt_eoi(i + shift);
        break;
    case 3: case 7:                         /* specific EOI */
        virt_eoi((v & 7) + shift);
        break;
    }
}

static void pic_data(int c, vu32 v)
{
    vu32 shift = c ? 8 : 0, old, irq;
    switch (vp.icw_step[c]) {
    case 1:                                 /* ICW2 */
        vp.base[c] = v & 0xF8;
        vp.icw_step[c] = vp.single[c] ? (vp.icw4[c] ? 3 : 0) : 2;
        return;
    case 2:                                 /* ICW3 */
        vp.icw_step[c] = vp.icw4[c] ? 3 : 0;
        return;
    case 3:                                 /* ICW4 */
        vp.icw_step[c] = 0;
        return;
    }
    old = vp.imr;                           /* OCW1 */
    vp.imr = (vp.imr & ~(0xFFu << shift)) | ((v & 0xFF) << shift);
    phys_update();
    for (irq = shift; irq < shift + 8; irq++)
        if (((old ^ vp.imr) >> irq) & 1 && owner[irq])
            call_vid(owner[irq]->d.mask_change_proc, owner[irq], (vp.imr >> irq) & 1, 0);
}

static vu32 pic_read(int c, int data)
{
    vu32 shift = c ? 8 : 0, v;
    if (data) return (vp.imr >> shift) & 0xFF;
    if (vp.read_isr[c]) return (vp.isr >> shift) & 0xFF;
    {   /* the IRR: requested, not in service; the master's bit 2 the slave's line */
        vu32 irq, irr = 0;
        for (irq = 0; irq < NIRQ; irq++)
            if (requested(irq) && !(vp.isr & (1u << irq))) irr |= 1u << irq;
        if ((irr >> 8) & ~(vp.imr >> 8) & 0xFF) irr |= 4;
        v = (irr >> shift) & 0xFF;
    }
    return v;
}

void __cdecl vpicd_io(struct vregs *r)
{
    int c = (r->edx & 0x80) != 0, data = r->edx & 1;
    if (r->ecx & (IO_SIZE | IO_STRING)) {
        vmm_simulate_io(r);                 /* comes back byte by byte */
        return;
    }
    if (r->ecx & IO_OUTPUT) {
        if (data) pic_data(c, r->eax & 0xFF);
        else pic_command(c, r->eax & 0xFF);
    } else
        r->eax = (r->eax & ~0xFFu) | pic_read(c, data);
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
        v->requested = (def_req >> irq) & 1;
        v->phys_mode = 0;
        def_req &= ~(1u << irq);
        owner[irq] = v;
        phys_update();
        r->eax = (vu32)v;
        return;
    }
    case 0x02:                              /* VPICD_Set_Int_Request: EAX handle, EBX VM */
        if ((v = handle(r->eax)) == NULL) return;
        v->requested = 1;
        deliver_pending();                  /* now, if the VM can take it */
        return;
    case 0x03:                              /* VPICD_Clear_Int_Request */
        if ((v = handle(r->eax)) == NULL) return;
        v->requested = 0;
        return;
    case 0x04:                              /* VPICD_Phys_EOI: EAX handle */
        if ((v = handle(r->eax)) == NULL) return;
        phys_isr &= ~(1u << v->irq);
        phys_update();
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
    case 0x0A:                              /* VPICD_Set_Auto_Masking */
        if ((v = handle(r->eax)) == NULL) return;
        v->phys_mode = r->arg == 0x08 ? 1 : r->arg == 0x09 ? 2 : 0;
        phys_update();
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
#define SYS_VM_INIT       0x03
#define SYS_CRITICAL_EXIT 0x06

void __cdecl vpicd_control(struct vregs *r)
{
    struct vregs s;
    r->eflags &= ~VF_CF;
    switch (r->eax) {
    case SYS_CRITICAL_INIT:
        sys_vm = r->ebx;
        /* the 8259s keep their masks until the System VM is set up */
        dos_masks = inb(0x21) | (inb(0xA1) << 8);
        vp.base[0] = vpicd_link.rm_base1;
        vp.base[1] = vpicd_link.rm_base2;
        pic_program(VPICD_BASE1, VPICD_BASE2);
        phys_update();
        vregs_clear(&s);
        s.edi = (vu32)vpicd_io_table;
        vmm_install_mult_io(&s);
        break;
    case SYS_VM_INIT:
        /* the VM's PIC gets the masks the real one had (VX6, VX7: the
         * System VM's program reads them back). Under Windows 3.1 VXP saw
         * IRQ 5 physically masked but not virtually at Sys_Critical_Init
         * (status 4): the VM's masks come later (UNVERIFIED: when) */
        vp.imr = dos_masks;
        vm_ready = 1;
        phys_update();
        break;
    case SYS_CRITICAL_EXIT:
        /* the real-mode bases, and the System VM's masks (UNVERIFIED) */
        pic_program(vpicd_link.rm_base1, vpicd_link.rm_base2);
        outb(0x21, vp.imr & 0xFF);
        outb(0xA1, (vp.imr >> 8) & 0xFF);
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
