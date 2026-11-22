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
#define TEXT_MAX 16000                  /* up to 132 x 60 x 2: the text a window tracks */
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
    u32 *zero;                          /* Get_Mem's colour table (dac_table) */
    u32 mod_sum, mod_time;              /* VDD_Get_ModTime: the text's sum, when it changed */
    int mod_ok;
};
static struct vsave *saves;
static u32 mod_check(struct vsave *d);

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
    if (!s->g) { kfree(s); return NULL; }      /* (linked without it, it was found later) */
    s->valid = 0;
    s->next = saves;
    saves = s;
    return s;
}

/* the VGA's memory as the processor sees it, whichever VM runs: through
 * the System VM's high linear window, whose A0000h-BFFFFh is always the
 * VGA (a windowed DOS VM's own is RAM: saving from there with that VM
 * current saved its RAM as Windows' screen) */
static u8 *vga_mem(void)
{
    return (u8 *)(sysvm.cb->high_linear + 0xA0000);
}

static void vga_save(struct vga *g)
{
    u32 i, p;
    /* the CRTC and input status 1 at 3Bxh while the VGA is set for a
     * monochrome display (Misc Output bit 0 clear: mode 7), else 3Dxh */
    u32 crt;
    g->misc = inb(0x3CC);
    crt = (g->misc & 1) ? 0x3D4 : 0x3B4;
    g->seq_i = inb(0x3C4);
    g->gc_i = inb(0x3CE);
    g->crtc_i = inb(crt);
    inb(crt + 6);
    g->attr_i = inb(0x3C0);
    g->dac_mask = inb(0x3C6);
    g->dac_w = inb(0x3C8);
    for (i = 0; i < 5; i++) { outb(0x3C4, i); g->seq[i] = inb(0x3C5); }
    for (i = 0; i < 9; i++) { outb(0x3CE, i); g->gc[i] = inb(0x3CF); }
    for (i = 0; i < 25; i++) { outb(crt, i); g->crtc[i] = inb(crt + 1); }
    for (i = 0; i < 21; i++) { inb(crt + 6); outb(0x3C0, i | 0x20); g->attr[i] = inb(0x3C1); }
    inb(crt + 6);
    outb(0x3C7, 0);
    for (i = 0; i < 768; i++) g->dac[i] = inb(0x3C9);
    /* the planes: odd/even and chain 4 off, A0000h 64 KB, read mode 0 */
    outb(0x3C4, 4); outb(0x3C5, 0x06);
    outb(0x3CE, 5); outb(0x3CF, 0);
    outb(0x3CE, 6); outb(0x3CF, 0x05);
    for (p = 0; p < 4; p++) {
        outb(0x3CE, 4); outb(0x3CF, p);
        memcpy(g->planes[p], vga_mem(), 65536);
    }
}

static void vga_restore(struct vga *g)
{
    u32 i, p, crt;
    outb(0x3C4, 4); outb(0x3C5, 0x06);      /* not chain 4, not odd/even */
    outb(0x3CE, 5); outb(0x3CF, 0);         /* write mode 0 */
    outb(0x3CE, 1); outb(0x3CF, 0);         /* no set/reset */
    outb(0x3CE, 3); outb(0x3CF, 0);         /* no rotate, replace */
    outb(0x3CE, 8); outb(0x3CF, 0xFF);      /* every bit */
    outb(0x3CE, 6); outb(0x3CF, 0x05);      /* A0000h, 64 KB, graphics */
    for (p = 0; p < 4; p++) {
        outb(0x3C4, 2); outb(0x3C5, 1u << p);
        memcpy(vga_mem(), g->planes[p], 65536);
    }
    outb(0x3C2, g->misc);
    crt = (g->misc & 1) ? 0x3D4 : 0x3B4;
    outb(0x3C4, 0); outb(0x3C5, 1);         /* synchronous reset */
    for (i = 1; i < 5; i++) { outb(0x3C4, i); outb(0x3C5, g->seq[i]); }
    outb(0x3C4, 0); outb(0x3C5, g->seq[0] | 3);
    outb(crt, 0x11); outb(crt + 1, g->crtc[0x11] & 0x7F);
    for (i = 0; i < 25; i++) { outb(crt, i); outb(crt + 1, g->crtc[i]); }
    for (i = 0; i < 9; i++) { outb(0x3CE, i); outb(0x3CF, g->gc[i]); }
    inb(crt + 6);
    for (i = 0; i < 21; i++) { outb(0x3C0, i); outb(0x3C0, g->attr[i]); }
    outb(0x3C0, 0x20);
    outb(0x3C8, 0);
    for (i = 0; i < 768; i++) outb(0x3C9, g->dac[i]);
    outb(0x3C6, g->dac_mask);
    outb(0x3C8, g->dac_w);
    inb(crt + 6);
    outb(0x3C0, g->attr_i | 0x20);
    outb(crt, g->crtc_i);
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
    trace("VDD: INT 2Fh %x in the System VM (%x:%x)\n", (u32)arg, sysvm.pmvec_sel[0x2F], sysvm.pmvec_off[0x2F]);
    dpmi_pm_int(0x2F, &r);
}

/* INT 10h in a DOS VM that gets the display for the first time: its BIOS
 * sets the text mode its data area names. The data area is as it was when
 * Windows started (vm_snapshot), and WIN.COM's start-up logo (WIN /3 with
 * nothing but switches) had put the VGA in mode 12h then: a graphics mode
 * there means mode 3 (the user's report, 2026-11-21: plain DOSBox, the
 * tree's DOS Prompt stayed in 640x480 graphics, DOSBox's shell writing its
 * text there through the BIOS). Windows 3.1's MS-DOS Prompt starts in 80x25
 * text after its logo too. UNVERIFIED: what Windows 3.1's VDD does for a
 * VM started in mode 7 or with 43/50 rows */
static void bios_mode(struct vm *v, void *arg)
{
    struct cregs r;
    u8 mode = *(u8 *)0x449;
    u16 cur = *(u16 *)0x450;
    if (mode > 3 && mode != 7)
        mode = 3;
    memset(&r, 0, sizeof(r));
    r.eax = mode;
    r.eflags = EFL_IF;
    trace("VDD: VM %d's video mode %x\n", v->id, mode);
    v86_int(&r, 0x10);
    if (arg) {
        /* a VM that had run windowed (its text in RAM, arg): the text on
         * the screen again, and its cursor where it was */
        memcpy((void *)0xB8000, arg, TEXT_MAX);
        kfree(arg);
        memset(&r, 0, sizeof(r));
        r.eax = 0x0200;
        r.edx = cur;
        r.eflags = EFL_IF;
        v86_int(&r, 0x10);
    }
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

/* Get_Mem's last pointer: the VM's colours, a dword per DAC entry: its
 * number, then red, green, blue (6 bits). Windows 3.1 (VXS.386, the
 * windowed MS-DOS Prompt): 00 00 00 00, 01 00 00 2A, 02 00 2A 00 ...
 * 07 2A 2A 2A, 08 00 00 15 ..., the VGA's text-mode DAC; Microsoft's
 * grabber takes a cell's colour from the entry its palette register names
 * (FFh bytes there painted every colour white, zeros black). The first 64
 * entries are filled here (UNVERIFIED how many 3.1 keeps); from the VM's
 * saved DAC, or the VGA's text-mode colours for a VM that never had the
 * display */
static void dac_table(struct vsave *d)
{
    u32 i;
    u8 *t = (u8 *)d->zero;
    for (i = 0; i < 64; i++) {
        t[i * 4] = (u8)i;
        if (d->valid) {
            t[i * 4 + 1] = d->g->dac[i * 3] & 0x3F;
            t[i * 4 + 2] = d->g->dac[i * 3 + 1] & 0x3F;
            t[i * 4 + 3] = d->g->dac[i * 3 + 2] & 0x3F;
        } else {                        /* the EGA colour i: rgbRGB */
            t[i * 4 + 1] = (u8)(((i & 4) ? 0x2A : 0) + ((i & 0x20) ? 0x15 : 0));
            t[i * 4 + 2] = (u8)(((i & 2) ? 0x2A : 0) + ((i & 0x10) ? 0x15 : 0));
            t[i * 4 + 3] = (u8)(((i & 1) ? 0x2A : 0) + ((i & 0x08) ? 0x15 : 0));
        }
    }
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
        /* whole pages each, as ring 3 reads them: Get_Mem's colour table
         * had a slab block, and its page (other VMM data) went to ring 3 */
        u32 zp = page_alloc();
        d->font = kmalloc(8192);
        d->plane1 = kmalloc(8192);
        d->zero = zp ? (u32 *)phys_to_lin(zp) : NULL;
        if (!d->font || !d->plane1 || !d->zero) panic("no memory for a windowed DOS VM");
        make_user((u32)d->font, 8192);
        make_user((u32)d->plane1, 8192);
        make_user((u32)d->zero, 1024);
        memset(d->plane1, 0, 8192);
    }
    dac_table(d);
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
    if (on && !d->valid && !v->started) {
        /* windowed from the start (its PIF): its BIOS cannot set a mode on
         * the VGA, which is Windows'; its data area says 80x25 text (as
         * the grabber's first Get_State must see) and its RAM is blank
         * text (UNVERIFIED how Windows 3.1's VDD starts it; its window
         * shows the same text) */
        static const u8 bda[] = { 3, 80, 0, 0x00, 0x10, 0, 0 };
        u8 *t = (u8 *)phys_to_lin(d->vram[0x18]);
        u32 i;
        for (i = 0; i < sizeof(bda); i++) *(u8 *)vm_lin(v, 0x449 + i) = bda[i];
        for (i = 0; i < 16; i++) *(u8 *)vm_lin(v, 0x450 + i) = 0;
        *(u8 *)vm_lin(v, 0x462) = 0;
        *(u8 *)vm_lin(v, 0x463) = 0xD4;
        *(u8 *)vm_lin(v, 0x464) = 0x03;
        *(u8 *)vm_lin(v, 0x484) = 24;
        *(u8 *)vm_lin(v, 0x485) = 16;
        *(u8 *)vm_lin(v, 0x486) = 0;
        *(u8 *)vm_lin(v, 0x460) = 7;
        *(u8 *)vm_lin(v, 0x461) = 6;
        for (i = 0; i < 4096; i += 2) { t[i] = ' '; t[i + 1] = 7; }
    }
    /* off: the text goes back when the VM gets the display (vdd_focus) */
}

/* the display goes to v */
/* A switch away from Windows while its display driver is inside a
 * critical section (INT 2Fh 4003h) waits until it leaves it (4004h) or
 * 1000 ms have passed, whichever comes first (the DDAG, Enter Critical
 * Section: the VDD postpones saving the video state) */
static struct vm *pend;                 /* the VM the display waits to go to */
static u32 pend_ticks;
static void vdd_focus_now(struct vm *v);

static void pend_run(struct vm *sys, void *arg)
{
    struct vm *v = pend;
    (void)sys; (void)arg;
    if (!v) return;
    pend = NULL;
    vdd_focus_now(v);
}

void vdd_focus(struct vm *v)
{
    if (!owner) owner = &sysvm;
    if (v == owner) { pend = NULL; return; }
    if (owner == &sysvm && sys_crit) {
        if (!pend) pend_ticks = 19;     /* 1000 ms of 54.9 ms ticks */
        pend = v;
        trace("VDD: display driver in a critical section: the switch to VM %d waits\n", v->id);
        return;
    }
    pend = NULL;
    vdd_focus_now(v);
}

static void vdd_focus_now(struct vm *v)
{
    struct vsave *s;
    if (v == owner) return;
    trace("VDD: the display goes from VM %d to VM %d\n", owner->id, v->id);
    /* the VM that had it */
    if (owner == &sysvm) {
        /* (in another VM: when the System VM next runs, before its code) */
        if (cur_vm == &sysvm) notify_sys(&sysvm, (void *)0x4001);
        else vm_event(&sysvm, notify_sys, (void *)0x4001);
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
    } else if (s && s->windowed) {
        /* windowed since it started: no VGA state of its own yet. Its
         * BIOS sets the mode, then its text comes back (before, its video
         * memory stayed RAM: the screen showed nothing of it, and 128 KB
         * stayed taken) */
        u8 *t = kmalloc(TEXT_MAX);
        u32 i;
        if (t) {
            u32 k;
            for (k = 0; k < TEXT_MAX; k += 4096)
                memcpy(t + k, (void *)phys_to_lin(s->vram[0x18 + k / 4096]), TEXT_MAX - k < 4096 ? TEXT_MAX - k : 4096);
        }
        map_video(s, 0);
        for (i = 0; i < 32; i++) page_free(s->vram[i]);
        s->windowed = 0;
        vm_event(v, bios_mode, t);
    }
    else if (v->started)
        vm_event(v, bios_mode, NULL);
    /* a VM not started yet sets its mode at its VM_Init (vdd_vm_init) */
}

/* VM_Init of a DOS VM (in that VM) */
void vdd_vm_init(struct vm *v)
{
    struct vsave *d;
    if (v == owner) bios_mode(v, NULL);
    else if ((d = save_of(v, 0)) != NULL && d->windowed) {
        /* windowed from the start: its BIOS sets its text mode all the
         * same (the user's report, 2026-11-22: a PIF that starts windowed
         * gave a black window in plain DOSBox, with Microsoft's WINOA386
         * too). DOSBox's BIOS keeps the video mode in the emulator, not in
         * the VM's data area: Windows' mode 12h was still its mode, and the
         * VM's text went into graphics memory the grabber never reads. The
         * mode set writes the VM's own video memory (its RAM now) but the
         * real VGA's registers: Windows' VGA state is saved before and put
         * back after, and no other VM runs meanwhile */
        struct vga *g = kmalloc(sizeof(struct vga));
        if (!g) return;
        vga_save(g);
        v->crit++;
        bios_mode(v, NULL);
        v->crit--;
        vga_restore(g);
        kfree(g);
    }
}

void vdd_vm_destroy(struct vm *v)
{
    struct vsave *s;
    if (pend == v) pend = NULL;
    /* a VM that goes with the display: Windows gets it back */
    if (owner == v) vdd_focus(&sysvm);
    s = save_of(v, 0);
    if (s) {
        if (s->windowed) {
            u32 i;
            for (i = 0; i < 32; i++) page_free(s->vram[i]);
            s->windowed = 0;
        }
        s->valid = 0;
    }
}

/* a windowed VM's text and cursor (its BIOS data: 40:50h) */
static u8 *text_of(struct vsave *d)
{
    return (u8 *)(d->vm->cb->high_linear + 0xB8000);
}

/* the VM's text screen as its BIOS data says: columns (40:4Ah), rows
 * (40:84h + 1; 25 when it is 0), its bytes (character and attribute
 * each). Windows 3.1's window shows the 50 rows of an 80x50 screen and
 * the 40 columns of mode 1 (VX12 T and F); before, 80x25 was assumed */
static u32 text_bytes(struct vsave *d, u32 *cp, u32 *rp)
{
    u32 cols = *(u16 *)vm_lin(d->vm, 0x44A), rows = *(u8 *)vm_lin(d->vm, 0x484) + 1;
    if (cols < 1 || cols > 132) cols = 80;
    if (rows < 2 || rows > 60) rows = 25;
    if (cols * rows * 2 > TEXT_MAX) rows = TEXT_MAX / (cols * 2);
    if (cp) *cp = cols;
    if (rp) *rp = rows;
    return cols * rows * 2;
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
    if (pend && pend_ticks && !--pend_ticks) vm_event(&sysvm, pend_run, NULL);
    for (d = saves; d; d = d->next) if (d->vm->id && !d->vm->ended) mod_check(d);
    for (d = saves; d; d = d->next) {
        int mem, cur;
        if (!d->windowed || d->pending || !d->vm->id || d->vm->ended) continue;
        mem = !d->shadow_ok || memcmp(text_of(d), d->shadow, text_bytes(d, NULL, NULL));
        cur = cursor_of(d) != d->last_cur;
        if (mem || cur) {
            d->pending = shell_display_changed(d->vm, cur && !mem);
        }
    }
}

/* ---- the VDD's services for other devices (device 000Ah; the DDK's
 * order: VDD_Get_Version 0, ... VDD_Get_ModTime 5, ...; Windows 3.1's
 * VMPOLL calls service 5 and no other: docs/win386-services.md) ---- */

/* VDD_Get_ModTime (the DDK: the system time at the VM's last video
 * modification; VMPOLL tells an idle VM by it): the VM's text page and
 * cursor are summed when asked, and each timer tick for the VMs the VDD
 * keeps, and the time noted when the sum changes. UNVERIFIED: what
 * Windows 3.1's VDD counts as a modification (graphics modes here: the
 * same bytes at B8000h) */
static u32 mod_check(struct vsave *d)
{
    u32 n = text_bytes(d, NULL, NULL), i, h = cursor_of(d);
    u32 *p = (u32 *)text_of(d);
    for (i = 0; i < n / 4; i++) h = ((h << 5) | (h >> 27)) ^ p[i];
    if (!d->mod_ok || h != d->mod_sum) {
        /* a VM idle (released) whose screen changed runs again: Windows
         * 3.1 woke VX14 V within 5 ms of its release (VMPOLL) */
        if (d->mod_ok && d->vm->idle && d->vm != cur_vm) vm_wake(d->vm);
        d->mod_sum = h;
        d->mod_time = d->mod_ok ? vmm_system_time() : 0;
        d->mod_ok = 1;
    }
    return d->mod_time;
}

int vdd_service(u32 svc, struct svcregs *s)
{
    static const char name[8] = { 'V', 'I', 'D', 'E', 'O', 'V', 'G', 'A' };
    struct vm *v;
    struct vsave *d;
    switch (svc) {
    case 0x00:                          /* VDD_Get_Version: AX, ESI the 8-byte name */
        s->eax = (s->eax & 0xFFFF0000u) | 0x0200;   /* the DDB's 2.00 */
        s->esi = (u32)name;
        return 1;
    case 0x05:                          /* VDD_Get_ModTime: EBX the VM -> EAX */
        if ((v = vm_of(s->ebx)) == NULL || !v->id || (d = save_of(v, 1)) == NULL) { s->eax = 0; return 1; }
        s->eax = mod_check(d);
        return 1;
    }
    return 0;
}

/* INT 2Fh 4003h/4004h from the System VM (the display driver's critical
 * sections around its hardware access) */
void vdd_int2f(struct cregs *r)
{
    if (AX(r) == 0x4003) sys_crit++;
    else if (AX(r) == 0x4004 && sys_crit) {
        /* (calls not paired with a 4003h are ignored: the DDAG) */
        if (!--sys_crit && pend) vm_event(&sysvm, pend_run, NULL);
    }
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
    /* the caller's buffer (the grabber's), as long as Get_Mem's 28h
     * bytes: functions 1-3 fail without one */
    u8 *b = cl_ptr(r->es, r->edi, 0x28, 1);
    if (vm_trace) trace("(VDD %x)", AX(r));
    if (!b && AX(r) >= 1 && AX(r) <= 3) return 0;
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
            /* the text's size in whole pages: 1000h for 80x25, 2000h for
             * 80x50 (Windows 3.1, VXS.386 with VX12 T) */
            m[5] = m[6] = (text_bytes(d, NULL, NULL) + 0xFFF) & ~0xFFFu;
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
            u32 cols, rows, bytes = text_bytes(d, &cols, &rows);
            u32 i, l = cols, tp = rows, rt = 0, bt = 0;
            SETW(r->eax, 0xFFFF);
            SETW(r->ebx, 0);
            if (!d->shadow_ok) {
                flags = 0x38;
                SETW(r->ebx, 0xFFFF);
            } else {
                for (i = 0; i < bytes; i += 2)
                    if (t[i] != d->shadow[i] || t[i + 1] != d->shadow[i + 1]) {
                        u32 c = (i / 2) % cols, rw = (i / 2) / cols;
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
            if (!d->seen) d->seen = kmalloc(TEXT_MAX);
            if (d->seen) { memcpy(d->seen, t, TEXT_MAX); d->seen_ok = 1; d->seen_cur = cursor_of(d); }
            if (vm_trace) trace("(MOD %x %x %x cur %x)", flags, AX(r), BX(r), cursor_of(d));
        }
        SETW(r->edx, 0x000C);
        r->eflags &= ~EFL_CF;
        return 1;
    case 0x0004:                        /* Clear_Mod */
        if (!d || !d->windowed) return 0;
        if (!d->shadow) d->shadow = kmalloc(TEXT_MAX);
        if (d->shadow) {
            memcpy(d->shadow, d->seen_ok ? d->seen : text_of(d), TEXT_MAX);
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
        if ((r->es & ~3u) && cl_ptr(r->es, r->edi, 8, 1)) memcpy(cl_ptr(r->es, r->edi, 8, 1), "VIDEOVGA", 8);
        return 1;
    case 0x000C:
        SETW(r->eax, BX(r));
        r->eflags &= ~EFL_CF;
        return 1;
    }
    return 0;
}
