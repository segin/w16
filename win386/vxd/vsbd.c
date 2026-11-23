/* vsbd.c - w16's VSBD.386, the Sound Blaster's virtual device (device
 * 0445h, an external VxD: SYSTEM.INI [386Enh] device=vsbd.386), in the
 * place of Windows 3.1's VSBD.386 (ARCHITECTURE.md 24.6). It serves
 * w16's SB16.DRV and Windows 3.1's SNDBLST.DRV/SNDBLST2.DRV alike.
 *
 * The card belongs to one VM at a time. Its ports (the DSP's and mixer's,
 * base+0 to base+0Eh, and the FM's 388h-389h) are trapped in every VM but
 * the owner's; a VM that touches them while the card is nobody's becomes
 * the owner, one that touches them while another VM has it gets nothing
 * (reads FFh, writes dropped) and is told once with SHELL_Message: "Unable
 * to play sound - the Sound Blaster is in use by another application."
 * The card's IRQ is virtualised (VPICD) and goes to the owner. The System
 * VM's driver takes and gives back the card through the API (below)
 * around its use of it; a DOS VM keeps it until it ends. DMA is VDMAD's
 * default handling.
 *
 * The API (V86 and PM, one procedure; the client's DX the function, AX
 * the card's base port), as Windows 3.1's SNDBLST2.DRV calls Windows
 * 3.1's VSBD.386 (observed: tests/sbvxdprobe.sh, the probe VXB.386
 * between them, black box):
 *   1  take the card: AX=0, carry clear (8000h the first time); another
 *      VM has it: AX=2, carry set (SNDBLST2 then plays nothing, silently)
 *   2  give it back: AX=0
 *   3  the card: AX the DSP's version (0405h on DOSBox-X's SB16), BX 2,
 *      CH the IRQ, CL the DMA channel, DX the base port
 * SNDBLST2 takes the card around its enable (1, 3, 2) and around each
 * use of a device (1 at the open, 2 at the close).
 * Observed of Windows 3.1's VSBD.386 besides: it virtualises the IRQ
 * (VPICD_Get_IRQ_Complete_Status carry set for IRQ 5; physically masked
 * while nobody plays, unmasked while a box has it), traps 220h-22Eh,
 * 388h and 389h (not 22Fh, 38Ah, 38Bh, 330h, 331h); a box with the card
 * free plays; MessageBeep while a box has it is silent; a box that
 * touches the card while Windows plays gets the message box above
 * (caption the box's title, OK, an exclamation mark) and no card (SBD:
 * DSP not found).
 * Sources: the DDK's Virtual Device Adaptation Guide (VPICD_Virtualize_IRQ
 * and the VID procedures, Set/Clear_Int_Request, Phys_EOI, Physically_
 * Mask/Unmask, Install_IO_Handler, Enable/Disable_Local_Trapping, the
 * profile services, SHELL_Message); Creative's Sound Blaster Series
 * Hardware Programming Guide (the ports, the DSP's reset and E1h).
 * UNVERIFIED: VSBD's two services (here 0 the version, 1 nothing), API
 * function 0 and the meaning of BX; what Windows 3.1 does when the System
 * VM touches the ports without taking the card (here: as a box, it takes
 * it); the message's timing (here: the first refused access of a VM, once
 * until it gets the card).
 */
#include "w16vxd.h"

void __cdecl vmm_install_io(struct vregs *r);
void __cdecl vmm_enable_local(struct vregs *r);
void __cdecl vmm_disable_local(struct vregs *r);
void __cdecl vmm_get_sys_vm(struct vregs *r);
void __cdecl vmm_profile_hex(struct vregs *r);
void __cdecl vmm_profile_dec(struct vregs *r);
void __cdecl vpicd_virtualize(struct vregs *r);
void __cdecl vpicd_set_req(struct vregs *r);
void __cdecl vpicd_clear_req(struct vregs *r);
void __cdecl vpicd_phys_eoi(struct vregs *r);
void __cdecl vpicd_phys_mask(struct vregs *r);
void __cdecl vpicd_phys_unmask(struct vregs *r);
void __cdecl vpicd_status(struct vregs *r);
void __cdecl shell_message(struct vregs *r);
extern char vsbd_io[], vsbd_hw[], vsbd_eoi[], vsbd_mask[];
#pragma aux vsbd_io "*"
#pragma aux vsbd_hw "*"
#pragma aux vsbd_eoi "*"
#pragma aux vsbd_mask "*"

static vu32 inb(vu32 port);
static void outb(vu32 port, vu32 v);
#pragma aux inb = "in al, dx" "movzx eax, al" parm [edx] value [eax] modify exact [eax];
#pragma aux outb = "out dx, al" parm [edx] [eax] modify exact [];

static vu32 sb_port = 0x220, sb_irq = 7, sb_dma = 1, dsp_ver;
static vu32 sys_vm, owner, irq_h, taken_once, nocard;
static vu32 warned[16];                 /* VMs told already (until they get the card) */
static struct vid vid;

static void clr(struct vregs *r)
{
    vu32 *p = (vu32 *)r, i;
    for (i = 0; i < sizeof(*r) / 4; i++) p[i] = 0;
}

#define NPORTS 17
static vu32 port_n(vu32 i) { return i < 15 ? sb_port + i : 0x388 + (i - 15); }

static void trapping(vu32 vm, int on)
{
    struct vregs s;
    vu32 i;
    for (i = 0; i < NPORTS; i++) {
        clr(&s);
        s.ebx = vm;
        s.edx = port_n(i);
        if (on) vmm_enable_local(&s); else vmm_disable_local(&s);
    }
}

static void unwarn(vu32 vm)
{
    vu32 i;
    for (i = 0; i < 16; i++) if (warned[i] == vm) warned[i] = 0;
}

/* vm has the card now (0: nobody) */
static void set_owner(vu32 vm)
{
    struct vregs s;
    vu32 st;
    if (owner == vm) return;
    if (owner) trapping(owner, 1);
    owner = vm;
    if (vm) { trapping(vm, 0); unwarn(vm); }
    if (!irq_h) return;
    /* the 8259 follows the owner's mask (VPICD_Get_Complete_Status: 10h
     * virtually masked); nobody's: masked (Windows 3.1: status 4) */
    clr(&s); s.eax = irq_h; s.ebx = vm;
    if (vm) vpicd_status(&s);
    st = s.ecx;
    clr(&s); s.eax = irq_h;
    if (!vm || (st & 0x10)) vpicd_phys_mask(&s); else vpicd_phys_unmask(&s);
}

static const char msg_text[] =
    "Unable to play sound - the Sound Blaster is in use by another application.";

static void contention(vu32 vm)
{
    struct vregs s;
    vu32 i, f = 0;
    for (i = 0; i < 16; i++) {
        if (warned[i] == vm) return;
        if (!warned[i] && !f) f = i + 1;
    }
    if (f) warned[f - 1] = vm;
    clr(&s);
    s.eax = 0x0030;                     /* MB_OK | MB_ICONEXCLAMATION */
    s.ebx = vm;
    s.ecx = (vu32)msg_text;
    shell_message(&s);
}

/* a trapped port: EBX the VM, ECX the type, EDX the port, EAX the data */
void __cdecl vsbd_iohandler(struct vregs *r)
{
    vu32 vm = r->ebx, out = r->ecx & 4;
    if (!owner) set_owner(vm);
    if (owner != vm) {
        contention(vm);
        if (!out) r->eax |= 0xFF;
        return;
    }
    if (out) outb(r->edx, r->eax & 0xFF);
    else r->eax = (r->eax & ~0xFFu) | inb(r->edx);
}

/* the IRQ: to the owner (nobody's: the System VM) */
void __cdecl vsbd_hw_int(struct vregs *r)
{
    struct vregs s;
    clr(&s);
    s.eax = irq_h;
    s.ebx = owner ? owner : sys_vm;
    vpicd_set_req(&s);
}
void __cdecl vsbd_eoi_proc(struct vregs *r)
{
    struct vregs s;
    clr(&s); s.eax = irq_h; s.ebx = r->ebx;
    vpicd_phys_eoi(&s);
    clr(&s); s.eax = irq_h; s.ebx = r->ebx;
    vpicd_clear_req(&s);
}
/* the owner's mask is the card's (ECX 1: masked) */
void __cdecl vsbd_mask_change(struct vregs *r)
{
    struct vregs s;
    if (r->ebx != (owner ? owner : sys_vm)) return;
    clr(&s); s.eax = irq_h;
    if (r->ecx) vpicd_phys_mask(&s); else vpicd_phys_unmask(&s);
}

/* the API: EBX the VM, EBP the client's registers */
void __cdecl vsbd_api_proc(struct vregs *r)
{
    struct client_regs *c = (struct client_regs *)r->ebp;
    vu32 vm = r->ebx;
    c->eflags &= ~VF_CF;
    switch (c->edx & 0xFFFF) {
    case 0:                             /* (UNVERIFIED) the version */
        c->eax = (c->eax & 0xFFFF0000u) | 0x0300;
        return;
    case 1:                             /* take the card */
        if (nocard) { c->eax &= 0xFFFF0000u; return; }
        if (owner && owner != vm) {
            c->eax = (c->eax & 0xFFFF0000u) | 2;
            c->eflags |= VF_CF;
            return;
        }
        set_owner(vm);
        c->eax = (c->eax & 0xFFFF0000u) | (taken_once ? 0 : 0x8000);
        taken_once = 1;
        return;
    case 2:                             /* give it back */
        if (owner == vm) set_owner(0);
        c->eax &= 0xFFFF0000u;
        return;
    case 3:                             /* the card */
        c->eax = (c->eax & 0xFFFF0000u) | dsp_ver;
        c->ebx = (c->ebx & 0xFFFF0000u) | 2;
        c->ecx = (c->ecx & 0xFFFF0000u) | (sb_irq << 8) | sb_dma;
        c->edx = (c->edx & 0xFFFF0000u) | sb_port;
        return;
    }
    c->eflags |= VF_CF;
}

void __cdecl vsbd_service(struct vregs *r)
{
    r->eflags &= ~VF_CF;
    if (r->arg == 0) { r->eax = (r->eax & 0xFFFF0000u) | 0x0300; return; }
    r->eflags |= VF_CF;
}

/* the DSP's version (reset, E1h): 0 if there is none */
static vu32 dsp_version(void)
{
    vu32 i, v;
    outb(sb_port + 6, 1);
    for (i = 0; i < 1000; i++) inb(0x80 + 0);   /* a few microseconds */
    outb(sb_port + 6, 0);
    for (i = 0; i < 100000 && !(inb(sb_port + 0x0E) & 0x80); i++) ;
    if (inb(sb_port + 0x0A) != 0xAA) return 0;
    for (i = 0; i < 100000 && (inb(sb_port + 0x0C) & 0x80); i++) ;
    outb(sb_port + 0x0C, 0xE1);
    for (i = 0; i < 100000 && !(inb(sb_port + 0x0E) & 0x80); i++) ;
    v = inb(sb_port + 0x0A) << 8;
    for (i = 0; i < 100000 && !(inb(sb_port + 0x0E) & 0x80); i++) ;
    return v | inb(sb_port + 0x0A);
}

/* [SECTION] KEY= (hex or decimal); *found set when it is there */
static int found;
static vu32 profile(const char *sec, const char *key, vu32 def, int hex)
{
    struct vregs s;
    clr(&s);
    s.eax = def;
    s.esi = (vu32)sec;
    s.edi = (vu32)key;
    if (hex) vmm_profile_hex(&s); else vmm_profile_dec(&s);
    if (s.eflags & (VF_CF | VF_ZF)) return def;
    found = 1;
    return s.eax;
}

void __cdecl vsbd_control(struct vregs *r)
{
    struct vregs s;
    vu32 i;
    r->eflags &= ~VF_CF;
    switch (r->eax) {
    case DEVICE_INIT:
        clr(&s); vmm_get_sys_vm(&s); sys_vm = s.ebx;
        /* the card's settings: Windows 3.1's Sound Blaster drivers' section,
         * w16's SB16.DRV's */
        sb_port = profile("sndblst.drv", "port", sb_port, 1);
        sb_dma = profile("sndblst.drv", "dma", sb_dma, 0);
        sb_port = profile("sb16.drv", "port", sb_port, 1);
        sb_dma = profile("sb16.drv", "dma", sb_dma, 0);
        found = 0;
        sb_irq = profile("sndblst.drv", "int", sb_irq, 0);
        sb_irq = profile("sb16.drv", "int", sb_irq, 0);
        dsp_ver = dsp_version();
        if (!dsp_ver) {
            /* no card: nothing to share (the API lets every VM have it) */
            nocard = 1;
            break;
        }
        if (!found && dsp_ver >= 0x400) {
            /* the SB16's mixer says its IRQ (register 80h), as SB16.DRV
             * reads it */
            outb(sb_port + 4, 0x80);
            i = inb(sb_port + 5);
            sb_irq = (i & 2) ? 5 : (i & 4) ? 7 : (i & 1) ? 9 : (i & 8) ? 10 : sb_irq;
        }
        if (sb_irq == 2) sb_irq = 9;
        for (i = 0; i < NPORTS; i++) {
            clr(&s);
            s.edx = port_n(i);
            s.esi = (vu32)vsbd_io;
            vmm_install_io(&s);
        }
        vid.irq = (vu16)sb_irq;
        vid.options = 0;
        vid.hw_int_proc = (vu32)vsbd_hw;
        vid.eoi_proc = (vu32)vsbd_eoi;
        vid.mask_change_proc = (vu32)vsbd_mask;
        vid.iret_time_out = 500;
        clr(&s);
        s.edi = (vu32)&vid;
        vpicd_virtualize(&s);
        irq_h = (s.eflags & VF_CF) ? 0 : s.eax;
        if (irq_h) { clr(&s); s.eax = irq_h; vpicd_phys_mask(&s); }
        break;
    case DESTROY_VM:
        unwarn(r->ebx);
        if (owner == r->ebx) {
            owner = 0;                  /* (its trapping goes with it) */
            if (irq_h) { clr(&s); s.eax = irq_h; vpicd_phys_mask(&s); }
        }
        break;
    }
}
