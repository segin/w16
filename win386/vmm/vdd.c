/* vdd.c - w16 VMM: the virtual display device (VDD, 000Ah; Windows
 * 3.1's W3 module VDDVGA), part of the VMM module for now (ARCHITECTURE.md
 * 12.10).
 *
 * The display belongs to one VM at a time (Set_Device_Focus for VDD, or
 * for all devices): the System VM (Windows) or a full-screen DOS VM. When
 * it changes hands the VGA's registers, its four 64 KB planes and the DAC
 * are saved for the VM that had it and restored (or, for a VM that never
 * had it, its BIOS sets its video mode, INT 10h in that VM, as at its
 * VM_Init) for the VM that gets it. Windows' display driver is told with
 * INT 2Fh 4001h (Notify Background Switch) before its screen goes and
 * 4002h (Notify Foreground Switch) when it is back, run in the System VM
 * (the DDAG: the VDD calls them there). A full-screen DOS VM that loses
 * the display does not run (SHELL suspends it, as Windows 3.1's did with
 * the MS-DOS Prompt's settings); windowed DOS VMs (the grabber) are the
 * next step.
 *
 * Sources: the Windows 3.1 DDK: the Display Driver Adaptation Guide
 * (.tools/docs/ddag31: INT 2Fh 4000h-4007h), the Virtual Device
 * Adaptation Guide (vdag31: the VDD services, Set_Device_Focus); IBM VGA
 * register descriptions (Sequencer, Graphics Controller, CRTC, Attribute
 * Controller, DAC; the planes read with Read Map Select and written with
 * Map Mask, odd/even and chain 4 off). Observed under Windows 3.1 with
 * VXS.386 (the WIN386 handover note): the order of the control messages,
 * the VDD's API function 0 ("VIDEOVGA", AX=030Ah) and 000Ch (AX = BX).
 */
#include "vmm.h"

struct vga {
    u8 seq[5], gc[9], crtc[25], attr[21], misc, dac[768];
    u8 seq_i, gc_i, crtc_i, attr_i, dac_mask, dac_w;
    u8 planes[4][65536];
};

static struct vm *owner;                /* the VM the display belongs to */
static struct vga *saved_sys;           /* Windows' screen while a DOS VM has it */
static int sys_crit;                    /* INT 2Fh 4003h/4004h nesting (the display driver) */

/* each DOS VM's saved screen: kept with the VM's slot (vm_create reuses
 * destroyed VMs) */
struct vsave { struct vsave *next; struct vm *vm; struct vga *g; int valid; };
static struct vsave *saves;

static struct vsave *save_of(struct vm *v, int make)
{
    struct vsave *s;
    for (s = saves; s; s = s->next)
        if (s->vm == v) return s;
    if (!make) return NULL;
    s = kmalloc(sizeof(*s));
    if (!s) return NULL;
    s->vm = v;
    s->g = kmalloc(sizeof(struct vga));
    s->valid = 0;
    s->next = saves;
    saves = s;
    return s->g ? s : NULL;
}

static void vga_save(struct vga *g)
{
    u32 i, p;
    g->seq_i = inb(0x3C4);
    g->gc_i = inb(0x3CE);
    g->crtc_i = inb(0x3D4);
    inb(0x3DA);
    g->attr_i = inb(0x3C0);
    g->dac_mask = inb(0x3C6);
    g->dac_w = inb(0x3C8);
    g->misc = inb(0x3CC);
    for (i = 0; i < 5; i++) { outb(0x3C4, i); g->seq[i] = inb(0x3C5); }
    for (i = 0; i < 9; i++) { outb(0x3CE, i); g->gc[i] = inb(0x3CF); }
    for (i = 0; i < 25; i++) { outb(0x3D4, i); g->crtc[i] = inb(0x3D5); }
    for (i = 0; i < 21; i++) { inb(0x3DA); outb(0x3C0, i | 0x20); g->attr[i] = inb(0x3C1); }
    inb(0x3DA);
    outb(0x3C7, 0);
    for (i = 0; i < 768; i++) g->dac[i] = inb(0x3C9);
    /* the planes: odd/even and chain 4 off, A0000h 64 KB, read mode 0 */
    outb(0x3C4, 4); outb(0x3C5, 0x06);
    outb(0x3CE, 5); outb(0x3CF, 0);
    outb(0x3CE, 6); outb(0x3CF, 0x05);
    for (p = 0; p < 4; p++) {
        outb(0x3CE, 4); outb(0x3CF, p);
        memcpy(g->planes[p], (void *)0xA0000, 65536);
    }
}

static void vga_restore(struct vga *g)
{
    u32 i, p;
    outb(0x3C4, 4); outb(0x3C5, 0x06);      /* not chain 4, not odd/even */
    outb(0x3CE, 5); outb(0x3CF, 0);         /* write mode 0 */
    outb(0x3CE, 1); outb(0x3CF, 0);         /* no set/reset */
    outb(0x3CE, 3); outb(0x3CF, 0);         /* no rotate, replace */
    outb(0x3CE, 8); outb(0x3CF, 0xFF);      /* every bit */
    outb(0x3CE, 6); outb(0x3CF, 0x05);      /* A0000h, 64 KB, graphics */
    for (p = 0; p < 4; p++) {
        outb(0x3C4, 2); outb(0x3C5, 1u << p);
        memcpy((void *)0xA0000, g->planes[p], 65536);
    }
    outb(0x3C2, g->misc);
    outb(0x3C4, 0); outb(0x3C5, 1);         /* synchronous reset */
    for (i = 1; i < 5; i++) { outb(0x3C4, i); outb(0x3C5, g->seq[i]); }
    outb(0x3C4, 0); outb(0x3C5, g->seq[0] | 3);
    outb(0x3D4, 0x11); outb(0x3D5, g->crtc[0x11] & 0x7F);
    for (i = 0; i < 25; i++) { outb(0x3D4, i); outb(0x3D5, g->crtc[i]); }
    for (i = 0; i < 9; i++) { outb(0x3CE, i); outb(0x3CF, g->gc[i]); }
    inb(0x3DA);
    for (i = 0; i < 21; i++) { outb(0x3C0, i); outb(0x3C0, g->attr[i]); }
    outb(0x3C0, 0x20);
    outb(0x3C8, 0);
    for (i = 0; i < 768; i++) outb(0x3C9, g->dac[i]);
    outb(0x3C6, g->dac_mask);
    outb(0x3C8, g->dac_w);
    inb(0x3DA);
    outb(0x3C0, g->attr_i | 0x20);
    outb(0x3D4, g->crtc_i);
    outb(0x3CE, g->gc_i);
    outb(0x3C4, g->seq_i);
}

/* INT 2Fh AX=ax in the System VM, where Windows' display driver hooks it
 * in protected mode (dpmi.c runs the client's handler) */
static void notify_sys(struct vm *v, void *arg)
{
    struct cregs r;
    (void)v;
    memset(&r, 0, sizeof(r));
    r.eax = (u32)arg;
    trace("VDD: INT 2Fh %x in the System VM\n", (u32)arg);
    dpmi_pm_int(0x2F, &r);
}

/* INT 10h AX=0003h in a DOS VM that gets the display for the first time:
 * its BIOS sets the mode its data area names (UNVERIFIED: what Windows
 * 3.1's VDD does for a new VM) */
static void bios_mode(struct vm *v, void *arg)
{
    struct cregs r;
    u8 mode = *(u8 *)0x449;
    (void)arg;
    memset(&r, 0, sizeof(r));
    r.eax = mode;
    r.eflags = EFL_IF;
    trace("VDD: VM %d's video mode %x\n", v->id, mode);
    v86_int(&r, 0x10);
}

/* the display goes to v */
void vdd_focus(struct vm *v)
{
    struct vsave *s;
    if (!owner) owner = &sysvm;
    if (v == owner) return;
    trace("VDD: the display goes from VM %d to VM %d\n", owner->id, v->id);
    /* the VM that had it */
    if (owner == &sysvm) {
        if (cur_vm == &sysvm) notify_sys(&sysvm, (void *)0x4001);
        if (!saved_sys) saved_sys = kmalloc(sizeof(struct vga));
        if (saved_sys) vga_save(saved_sys);
    } else if ((s = save_of(owner, 1)) != NULL) {
        vga_save(s->g);
        s->valid = 1;
    }
    /* the VM that gets it */
    owner = v;
    if (v == &sysvm) {
        if (saved_sys) vga_restore(saved_sys);
        vm_event(&sysvm, notify_sys, (void *)0x4002);
    } else if ((s = save_of(v, 0)) != NULL && s->valid)
        vga_restore(s->g);
    else if (v->started)
        vm_event(v, bios_mode, NULL);
    /* a VM not started yet sets its mode at its VM_Init (vdd_vm_init) */
}

/* VM_Init of a DOS VM (in that VM) */
void vdd_vm_init(struct vm *v)
{
    if (v == owner) bios_mode(v, NULL);
}

void vdd_vm_destroy(struct vm *v)
{
    struct vsave *s = save_of(v, 0);
    if (s) s->valid = 0;
    if (owner == v) owner = &sysvm;
}

/* INT 2Fh 4003h/4004h from the System VM (the display driver's critical
 * sections around its hardware access) */
void vdd_int2f(struct cregs *r)
{
    if (AX(r) == 0x4003) sys_crit++;
    else if (AX(r) == 0x4004 && sys_crit) sys_crit--;
}

/* The VDD's protected-mode API (vxdapi.c): function 0 the version (030Ah)
 * and the display's name, 8 bytes at ES:DI ("VIDEOVGA", Windows 3.1 with
 * VDDVGA; the grabber VGA.3GR checks it and otherwise WINOA386 says "386
 * System display type mismatch."); 000Ch (VGA.DRV: BX=9B0Ah CX=3101h
 * DX=0 SI=01F4h DI=0106h) returns AX = BX, carry clear (VX5, UNVERIFIED
 * what it does). The grabber's functions (Get_Mem ...) come with windowed
 * DOS VMs. */
int vdd_api(struct cregs *r)
{
    switch (AX(r)) {
    case 0x0000:
        SETW(r->eax, 0x030A);
        r->eflags &= ~EFL_CF;
        if (r->es & ~3u) memcpy((void *)(sel_base(r->es) + DI(r)), "VIDEOVGA", 8);
        return 1;
    case 0x000C:
        SETW(r->eax, BX(r));
        r->eflags &= ~EFL_CF;
        return 1;
    }
    return 0;
}
