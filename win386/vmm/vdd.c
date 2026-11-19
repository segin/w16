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

/* each DOS VM's display state: its hardware state while another VM has
 * the display (kept with the VM's slot: vm_create reuses destroyed VMs);
 * while it is windowed, its video memory (A0000h-BFFFFh) is RAM of its
 * own (the text at B8000h, as the CPU sees it in text mode: character,
 * attribute), which the grabber reads through the VM's high linear
 * address, and what changed since the grabber last looked (Get_Mod,
 * Clear_Mod) */
#define TEXT_BYTES 4000                 /* 80 x 25 x 2 */
struct vsave {
    struct vsave *next;
    struct vm *vm;
    struct vga *g;
    int valid;
    int windowed;
    u32 vram[32];                       /* physical pages, A0000h-BFFFFh */
    u8 *shadow;                         /* the text at the last Clear_Mod */
    int shadow_ok;
    u16 last_cur;                       /* the cursor then (40:50h) */
    u8 *seen;                           /* the text Get_Mod reported on */
    int seen_ok;
    u16 seen_cur;
    int pending;                        /* a 41Ah event not answered yet */
    u8 *font;                           /* plane 2 for Get_Mem (ring 3) */
    u8 *plane1;                         /* Get_Mem's plane 1: a buffer of its own (UNVERIFIED use) */
    u32 *zero;
};
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

void page_free(u32 phys);

/* the VM's page-table entries for A0000h-BFFFFh: its RAM or the VGA */
static void map_video(struct vsave *d, int ram)
{
    u32 *pt = (u32 *)phys_to_lin(d->vm->ptab_phys), i;
    for (i = 0; i < 32; i++)
        pt[0xA0 + i] = ram ? (d->vram[i] | 7) : ((0xA0000 + i * 4096) | 7);
    set_cr3(get_cr3());
}

/* windowed: the saved planes become the text in RAM (odd/even: the
 * character at an even address is plane 0's byte there, the attribute
 * plane 1's) */
static void to_ram(struct vsave *d)
{
    u32 i, k;
    u8 *b8;
    if (d->windowed) return;
    for (i = 0; i < 32; i++) {
        d->vram[i] = page_alloc();
        if (!d->vram[i]) panic("no memory for a windowed DOS VM");
    }
    b8 = (u8 *)phys_to_lin(d->vram[0x18]);
    if (d->valid)
        for (i = 0; i < 8; i++) {
            u8 *pg = (u8 *)phys_to_lin(d->vram[0x18 + i]);
            for (k = 0; k < 4096; k += 2) {
                pg[k] = d->g->planes[0][i * 4096 + k];
                pg[k + 1] = d->g->planes[1][i * 4096 + k];
            }
        }
    (void)b8;
    if (!d->font) {
        d->font = kmalloc(8192);
        d->plane1 = kmalloc(8192);
        d->zero = kmalloc(1024);
        if (!d->font || !d->zero) panic("no memory for a windowed DOS VM");
        make_user((u32)d->font, 8192);
        make_user((u32)d->plane1, 8192);
        make_user((u32)d->zero, 1024);
        memset(d->plane1, 0, 8192);
        /* Get_Mem's last pointer: the DDK calls VDD_Get_Mem's result "the flat
         * address and allocation bitmap" of the video memory: every page
         * there (UNVERIFIED bit layout; zeros made the grabber paint black) */
        memset(d->zero, 0, 1024);
        d->zero[0] = d->zero[1] = d->zero[2] = d->zero[3] = 0xFFFFFFFFu;
    }
    if (d->valid) memcpy(d->font, d->g->planes[2], 8192);
    d->windowed = 1;
    d->shadow_ok = 0;
    d->seen_ok = 0;
    d->pending = 0;
    map_video(d, 1);
}

/* full screen again: the text back into the planes */
static void from_ram(struct vsave *d)
{
    u32 i, k;
    if (!d->windowed) return;
    for (i = 0; i < 8; i++) {
        u8 *pg = (u8 *)phys_to_lin(d->vram[0x18 + i]);
        for (k = 0; k < 4096; k += 2) {
            d->g->planes[0][i * 4096 + k] = pg[k];
            d->g->planes[1][i * 4096 + k] = pg[k + 1];
        }
    }
    map_video(d, 0);
    for (i = 0; i < 32; i++) page_free(d->vram[i]);
    d->windowed = 0;
}

/* SHELL: v windowed (its display is the grabber's) or full screen */
void vdd_window(struct vm *v, int on)
{
    struct vsave *d = save_of(v, 1);
    if (!d) return;
    trace("VDD: VM %d %s\n", v->id, on ? "windowed" : "full screen");
    if (on && v != owner) to_ram(d);
    /* off: the text goes back when the VM gets the display (vdd_focus) */
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
    } else if ((s = save_of(v, 0)) != NULL && s->valid) {
        from_ram(s);
        vga_restore(s->g);
    }
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
    if (s) {
        if (s->windowed) {
            u32 i;
            for (i = 0; i < 32; i++) page_free(s->vram[i]);
            s->windowed = 0;
        }
        s->valid = 0;
    }
    if (owner == v) owner = &sysvm;
}

/* a windowed VM's text and cursor (its BIOS data: 40:50h) */
static u8 *text_of(struct vsave *d)
{
    return (u8 *)(d->vm->cb->high_linear + 0xB8000);
}

static u16 cursor_of(struct vsave *d)
{
    return *(u16 *)vm_lin(d->vm, 0x450);
}

/* each timer tick: a windowed VM whose screen changed gets event 41Ah
 * (once until the grabber looks: Get_Mod / Clear_Mod) */
void vdd_tick(void)
{
    struct vsave *d;
    for (d = saves; d; d = d->next) {
        int mem, cur;
        if (!d->windowed || d->pending || !d->vm->id || d->vm->ended) continue;
        mem = !d->shadow_ok || memcmp(text_of(d), d->shadow, TEXT_BYTES);
        cur = cursor_of(d) != d->last_cur;
        if (mem || cur) {
            d->pending = 1;
            shell_display_changed(d->vm, cur && !mem);
        }
    }
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
    struct vm *v = vm_of(r->ebx);
    struct vsave *d = v ? save_of(v, 0) : NULL;
    u8 *b = (u8 *)pm_lin(r->es, r->edi);
    if (vm_trace) trace("(VDD %x)", AX(r));
    switch (AX(r)) {
    case 0x0001:                        /* Get_Mem: the planes' linear addresses */
        if (!d || !d->windowed) return 0;
        {
            u32 *m = (u32 *)b;
            m[0] = (u32)text_of(d);
            m[1] = (u32)d->plane1;
            m[2] = (u32)d->font;
            m[3] = 0;
            m[4] = 0;
            m[5] = m[6] = 0x1000;
            m[7] = 0x2000;
            m[8] = 0;
            m[9] = (u32)d->zero;
        }
        SETW(r->ecx, 0x28);
        r->eflags &= ~EFL_CF;
        return 1;
    case 0x0002:                        /* Get_State */
        if (!v) return 0;
        {
            u16 cur = *(u16 *)vm_lin(v, 0x450), shape = *(u16 *)vm_lin(v, 0x460);
            u32 i;
            memset(b, 0, 0x1C);
            b[0] = *(u8 *)vm_lin(v, 0x449);
            b[2] = 2;                   /* cursor tracking (Windows 3.1: 2) */
            b[3] = *(u8 *)vm_lin(v, 0x484) ? *(u8 *)vm_lin(v, 0x484) + 1 : 25;
            *(u16 *)(b + 4) = cur & 0xFF;
            *(u16 *)(b + 6) = cur >> 8;
            /* the cursor's first and last scan lines as the VGA has them
             * (CRTC 0Ah, 0Bh: Windows 3.1 0Dh, 0Eh for the MS-DOS Prompt,
             * whose BIOS data has the CGA-style 06h, 07h) */
            if (d && d->valid) {
                *(u16 *)(b + 8) = d->g->crtc[0x0A] & 0x1F;
                *(u16 *)(b + 10) = d->g->crtc[0x0B] & 0x1F;
            } else {
                *(u16 *)(b + 8) = shape >> 8;
                *(u16 *)(b + 10) = shape & 0xFF;
            }
            for (i = 0; i < 16; i++)
                b[12 + i] = d && d->valid ? d->g->attr[i] : (u8)(i < 8 ? (i == 6 ? 0x14 : i) : i + 0x30);
        }
        SETW(r->ecx, 0x1C);
        r->eflags &= ~EFL_CF;
        return 1;
    case 0x0003:                        /* Get_Mod: what changed since Clear_Mod */
        /* Windows 3.1's answers in text modes (VXS.386, typing into a
         * windowed MS-DOS Prompt): flags +0 (8 controller, 10h memory, 20h
         * cursor), count +2 always 1, a dword 0 at +4; the changed cells in
         * AL left, AH top, BL right, BH bottom, inclusive, the columns
         * counted from 1 and the rows from 0 ("C:\WINDOWS>" written on row
         * 0Eh: AX 0E01h, BX 0E0Bh); no change in memory: AX FFFFh, BX's low
         * word 0; the first answer after the window opens: everything (38h,
         * AX and BX FFFFh); ECX 5 with a memory change, else 4; EDX 0Ch */
        if (!d || !d->windowed) return 0;
        {
            u8 *t = text_of(d);
            u16 flags = 0;
            u32 i, l = 80, tp = 25, rt = 0, bt = 0;
            SETW(r->eax, 0xFFFF);
            SETW(r->ebx, 0);
            if (!d->shadow_ok) {
                flags = 0x38;
                SETW(r->ebx, 0xFFFF);
            } else {
                for (i = 0; i < TEXT_BYTES; i += 2)
                    if (t[i] != d->shadow[i] || t[i + 1] != d->shadow[i + 1]) {
                        u32 c = (i / 2) % 80, rw = (i / 2) / 80;
                        if (c < l) l = c;
                        if (c > rt) rt = c;
                        if (rw < tp) tp = rw;
                        if (rw > bt) bt = rw;
                    }
                if (l <= rt) {
                    flags |= 0x10;
                    SETW(r->eax, (tp << 8) | (l + 1));
                    SETW(r->ebx, (bt << 8) | (rt + 1));
                }
                if (cursor_of(d) != d->last_cur) flags |= 0x20;
            }
            *(u16 *)b = flags;
            *(u16 *)(b + 2) = 1;
            *(u32 *)(b + 4) = 0;
            SETW(r->ecx, (flags & 0x10) ? 5 : 4);
            /* what this answer covers: the VM runs on until Clear_Mod
             * (Windows 3.1 held it: UNVERIFIED how long), and a change made
             * meanwhile must come in the next answer, not be taken as seen */
            if (!d->seen) d->seen = kmalloc(TEXT_BYTES);
            if (d->seen) { memcpy(d->seen, t, TEXT_BYTES); d->seen_ok = 1; d->seen_cur = cursor_of(d); }
            if (vm_trace) trace("(MOD %x %x %x cur %x)", flags, AX(r), BX(r), cursor_of(d));
        }
        SETW(r->edx, 0x000C);
        r->eflags &= ~EFL_CF;
        return 1;
    case 0x0004:                        /* Clear_Mod */
        if (!d || !d->windowed) return 0;
        if (!d->shadow) d->shadow = kmalloc(TEXT_BYTES);
        if (d->shadow) {
            memcpy(d->shadow, d->seen_ok ? d->seen : text_of(d), TEXT_BYTES);
            d->shadow_ok = 1;
        }
        d->last_cur = d->seen_ok ? d->seen_cur : cursor_of(d);
        d->seen_ok = 0;
        d->pending = 0;
        r->eflags &= ~EFL_CF;
        return 1;
    case 0x0005:                        /* Free_Mem */
    case 0x0009:                        /* Unlock_App */
        if (d) d->pending = 0;
        r->eflags &= ~EFL_CF;
        return 1;
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
