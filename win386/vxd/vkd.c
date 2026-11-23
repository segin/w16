/* vkd.c - w16's VKD, the virtual keyboard device (device 000Dh), a module
 * of WIN386.EXE's W3 table (ARCHITECTURE.md 12.8).
 *
 * It takes IRQ 1 from VPICD at Sys_Critical_Init (Windows 3.1's VKD had
 * it by then: VXP), reads each byte from the keyboard controller (port
 * 60h), keeps the global shift state and looks for hot keys: a hot key's
 * press and release go to the device that defined it
 * (VKD_Define_Hot_Key: REBOOT's Ctrl+Alt+Del; unless the hot key is
 * disabled in the keyboard's VM, VKD_Local_Disable_Hot_Key) and not to
 * the VM. Every other byte goes into the keyboard queue of the VM with
 * the keyboard focus (Set_Device_Focus for VKD, or for every device: the
 * keyboard owner). Each VM sees a controller of its own: VKD traps
 * ports 60h and 64h (Install_IO_Handler), presents the queue's bytes one
 * at a time in its virtual output buffer (status bit 0, and a request on
 * IRQ 1 while it is full), and passes commands (writes) and the pointing
 * device's bytes (status bit 5) to the real controller. So keys can be
 * put into the VM: VKD_Force_Keys, VKD_Reflect_Hot_Key.
 *
 * Observed under Windows 3.1 (VXP, VX3): VKD_Get_Version AX=0200h;
 * VKD_Get_Kbd_Owner the System VM; VKD_Peek_Msg_Key nothing (EAX=0, zero
 * flag); the PM API's function 0 AX=0200h.
 * Sources: the DDK's Virtual Device Adaptation Guide (the VKD services,
 * VKD_Define_Hot_Key's callback registers, the VPICD services); the IBM
 * PC keyboard scan codes (set 1, E0h prefixed extended keys).
 * UNVERIFIED: the numeric values of VKD_Define_Hot_Key's flags and shift
 * state bits in Windows 3.1 (w16's own below, used by w16's REBOOT);
 * VKD_Reflect_Hot_Key's shift-state keystrokes (not made here); the queue's
 * size (Force_Keys reports an overflow, as the DDK says it can); pasting
 * and message mode (not yet: they come with DOS boxes).
 */
#include "w16vxd.h"

#define NULL ((void *)0)

void __cdecl vpicd_virtualize_irq(struct vregs *r);
void __cdecl vpicd_set_int_request(struct vregs *r);
void __cdecl vpicd_phys_eoi(struct vregs *r);
void __cdecl vpicd_clear_int_request(struct vregs *r);
void __cdecl vpicd_physically_unmask(struct vregs *r);
void __cdecl vmm_install_io_handler(struct vregs *r);
void __cdecl vmm_simulate_io(struct vregs *r);
extern char vkd_io60[], vkd_io64[];
#pragma aux vkd_io60 "*"
#pragma aux vkd_io64 "*"
extern char vkd_eoi[], vkd_io92[];
#pragma aux vkd_io92 "*"
#pragma aux vkd_eoi "*"
void __cdecl vxd_call_proc(vu32 proc, struct vregs *r);
vu32 __cdecl vmm_alloc_cb_area(vu32 size, vu32 flags);
void __cdecl vmm_validate_vm(struct vregs *r);
extern char vkd_hw[];
#pragma aux vkd_hw "*"

static vu32 inb(vu32 port);
static void outb(vu32 port, vu32 v);
#pragma aux inb = "in al, dx" "movzx eax, al" parm [edx] value [eax] modify exact [eax];
#pragma aux outb = "out dx, al" parm [edx] [eax] modify exact [];

/* the global shift state (w16's bits; the DDK names SS_Shift, SS_Ctrl,
 * SS_Alt and others without values) */
#define SS_SHIFT  0x0001
#define SS_CTRL   0x0002
#define SS_ALT    0x0004
/* VKD_Define_Hot_Key's CL (w16's values) */
#define HK_PRESS   0x01
#define HK_RELEASE 0x02
#define HK_REPEAT  0x04
/* AH: 0 normal, 1 extended, 0FFh either */

struct hotkey {
    vu32 used;
    vu8 scan, type, flags, down;
    vu32 shift;             /* high word mask, low word compare */
    vu32 proc, ref;
    vu32 off[8];            /* a bit per VM ID: disabled there (Local_Disable) */
};
#define NHOT 32
static struct hotkey hot[NHOT];

static struct vid vkd_vid;
static vu32 irq_handle, sys_vm;
static vu32 kfocus;         /* the VM with the keyboard (Set_Device_Focus) */
static vu32 shift;          /* SS_*: from phys_mods (set_shift) */
static int ext;             /* the last byte was E0h, held back (key_byte) */
static int e1_left;         /* bytes of an E1h sequence (Pause) still to come */

static void vregs_clear(struct vregs *r)
{
    vu32 *p = (vu32 *)r, i;
    for (i = 0; i < sizeof(*r) / 4; i++) p[i] = 0;
}

static vu32 phys_mods;      /* the shift keys down on the keyboard (MOD_* bits, mod_bit) */

/* the global shift state from the keys down: Shift while either Shift is
 * down, and so on (before, letting go of one of two held Shifts cleared
 * it; E0h 2Ah, the fake shift of the extended keys, set it; E1h 1Dh in
 * Pause's sequence was taken for Ctrl) */
static void set_shift(void)
{
    shift = ((phys_mods & 0x03) ? SS_SHIFT : 0) | ((phys_mods & 0x0C) ? SS_CTRL : 0)
          | ((phys_mods & 0x30) ? SS_ALT : 0);
}

/* a hot key for this byte? calls it; nonzero: the byte is the hot key's */
static int hot_off(struct hotkey *h, vu32 vm)
{
    vu32 id = CB_VMID(vm);
    return id < 256 && (h->off[id >> 5] >> (id & 31)) & 1;
}

static int hot_key(vu32 code, int up)
{
    vu32 i;
    struct vregs r;
    for (i = 0; i < NHOT; i++) {
        struct hotkey *h = &hot[i];
        if (!h->used || h->scan != code) continue;
        if (!up && hot_off(h, kfocus)) continue;
        if (h->type != 0xFF && h->type != (vu8)(ext ? 1 : 0)) continue;
        if (up) {
            if (!h->down) continue;
            h->down = 0;
        } else if (h->down) {
            /* the keyboard's repeat of a hot key held down: its callback
             * only when it asked for repeats (the DDK's CallOnRepeat) */
            if (!(h->flags & HK_REPEAT) || !h->proc) return 1;
        } else {
            if ((shift & (h->shift >> 16)) != (h->shift & 0xFFFF)) continue;
            h->down = 1;
        }
        if ((h->flags & (up ? HK_RELEASE : HK_PRESS)) && h->proc) {
            vregs_clear(&r);
            r.eax = code | ((up ? 1 : 0) << 8);
            r.ebx = (vu32)h;
            r.ecx = shift;
            r.edx = h->ref;
            vxd_call_proc(h->proc, &r);
        }
        return 1;
    }
    return 0;
}

/* ---- each VM's keyboard (in its control block): a queue, and the byte
 * the VM's controller holds (its output buffer) ---- */
#define QSIZE 256
struct kq {
    vu8 queue[QSIZE];
    vu32 q_head, q_len;
    vu32 cur;               /* the VM's port 60h */
    int obf;                /* the VM's output buffer full (status bit 0) */
    vu32 mods;              /* the shift keys the VM was given down (MOD_*) */
    int ext;                /* the last byte it was given was E0h */
};
static vu32 cb_off;
#define KQ(vm) ((struct kq *)((vm) + cb_off))

static void request(vu32 vm, int on)
{
    struct vregs s;
    vregs_clear(&s);
    s.eax = irq_handle;
    s.ebx = vm;
    if (on) vpicd_set_int_request(&s);
    else vpicd_clear_int_request(&s);
}

/* the shift keys one by one (MOD_*: left and right Shift, Ctrl, Alt),
 * from a byte and whether E0h came before it; 0 for other keys (E0h 2Ah
 * and E0h 36h are the extended keys' fake shifts) */
static vu32 mod_bit(vu32 code, int e0)
{
    switch (code) {
    case 0x2A: return e0 ? 0 : 0x01;
    case 0x36: return e0 ? 0 : 0x02;
    case 0x1D: return e0 ? 0x08 : 0x04;
    case 0x38: return e0 ? 0x20 : 0x10;
    }
    return 0;
}
static const vu8 mod_code[6] = { 0x2A, 0x36, 0x1D, 0x1D, 0x38, 0x38 };

/* a byte of the keyboard's (or forced as if from it): the shift keys
 * down; e0: E0h came before it */
static void track_mods(vu32 b, int e0)
{
    vu32 m;
    if (e1_left) { e1_left--; return; }
    m = mod_bit(b & 0x7F, e0);
    if (m) phys_mods = (b & 0x80) ? (phys_mods & ~m) : (phys_mods | m);
    set_shift();
}

static void present(vu32 vm);

static int q_put(vu32 vm, vu32 b)
{
    struct kq *k = KQ(vm);
    vu32 m;
    if (k->q_len == QSIZE) return 0;
    k->queue[(k->q_head + k->q_len++) % QSIZE] = (vu8)b;
    if (b == 0xE0) { k->ext = 1; return 1; }
    m = mod_bit(b & 0x7F, k->ext);
    k->ext = 0;
    if (m) k->mods = (b & 0x80) ? (k->mods & ~m) : (k->mods | m);
    return 1;
}

/* a VM that gets the keyboard: the shift keys it was given down that are
 * up now (released while another VM had the keyboard: Alt after
 * Alt+Enter, Alt+Tab) are released in it too. The DDK describes VKD
 * bringing a VM to the global shift state with simulated keystrokes
 * (VKD_Reflect_Hot_Key); under Windows 3.1 a DOS box made a window with
 * Alt+Enter takes letters as letters, not Alt+letters (observed:
 * tests/scripts/input/dosbox386w.txt). UNVERIFIED: that Windows 3.1 does
 * it here, and whether it also presses keys held down now */
static void sync_mods(vu32 vm)
{
    struct kq *k = KQ(vm);
    vu32 up = k->mods & ~phys_mods, i;
    for (i = 0; i < 6; i++) {
        if (!(up & (1u << i))) continue;
        if (i == 3 || i == 5) q_put(vm, 0xE0);
        q_put(vm, mod_code[i] | 0x80);
    }
    present(vm);
}

/* the next byte into the VM's output buffer, with IRQ 1 requested (a
 * level: the request goes when the VM reads it) */
static void present(vu32 vm)
{
    struct kq *k = KQ(vm);
    if (k->obf || !k->q_len) return;
    k->cur = k->queue[k->q_head];
    k->q_head = (k->q_head + 1) % QSIZE;
    k->q_len--;
    k->obf = 1;
    request(vm, 1);
}

/* a byte from the keyboard: a hot key's, or the VM's */
static void key_byte(vu32 b)
{
    vu32 code = b & 0x7F;
    int up = (b & 0x80) != 0, mine = 0;
    if (b == 0xE0) {
        /* held until the next byte is known not to be a hot key's: an
         * extended hot key's E0h is the hot key's (Windows 3.1: neither a
         * DOS VM, VX12 K, nor the System VM, VX11 K, gets the E0h of
         * Ctrl+Alt+grey Del) */
        if (ext == 1) q_put(kfocus, 0xE0);
        ext = 1;
        return;
    }
    if (b == 0xE1) e1_left = 2;             /* Pause: E1h 1Dh 45h, E1h 9Dh C5h */
    else {
        if (!e1_left) mine = hot_key(code, up);    /* with the shift state before this byte */
        track_mods(b, ext);
    }
    if (!mine) {
        if (ext == 1) q_put(kfocus, 0xE0);  /* the one held back */
        q_put(kfocus, b);
    }
    ext = 0;
}

/* VID_Hw_Int_Proc for IRQ 1: the byte is taken from the controller now
 * and the IRQ ended (VPICD_Phys_EOI): the VM gets it from the queue */
void __cdecl vkd_hw_int(struct vregs *r)
{
    struct vregs s;
    vu32 st = inb(0x64);
    if ((st & 0x21) == 0x01)                /* a keyboard byte (not the mouse's, not none) */
        key_byte(inb(0x60));
    vregs_clear(&s);
    s.eax = r->eax;
    vpicd_phys_eoi(&s);
    present(kfocus);
    r->eflags &= ~VF_CF;
}

/* VID_EOI_Proc for IRQ 1: the VM's handler is done; the next byte */
void __cdecl vkd_eoi_proc(struct vregs *r)
{
    if (KQ(r->ebx)->obf)                    /* not read: no second interrupt for it */
        request(r->ebx, 0);
    present(r->ebx);
    r->eflags &= ~VF_CF;
}

/* the VM's port 60h (EAX data, ECX the I/O type: Byte_Input 0, Byte_Output
 * 4, as observed with VXI; others through Simulate_IO) */
/* the controller command a VM wrote last (port 64h): D1h, write the
 * output port, takes its data byte at 60h */
static vu32 cmd_vm, cmd_d1;

void __cdecl vkd_port60(struct vregs *r)
{
    vu32 st;
    struct kq *k = KQ(r->ebx);
    if (r->ecx & ~4u) { vmm_simulate_io(r); return; }
    if (r->ecx & 4) {                       /* a command's data: the keyboard's */
        vu32 v = r->eax & 0xFF;
        /* the output port's A20 line stays on: the VMM and every VM's
         * memory are above 1 MB, and with A20 off they would fold onto
         * low memory (a DOS program's HIMEM-style A20 switch in a DOS
         * box ended Windows); the reset line is left as it is written.
         * UNVERIFIED: Windows 3.1 keeps A20 per VM (the DDK's V86MMGR) */
        if (cmd_d1 && cmd_vm == r->ebx) v |= 0x02;
        cmd_d1 = 0;
        outb(0x60, v);
        return;
    }
    st = inb(0x64);
    if (!k->obf && (st & 0x21) == 0x21) {   /* the pointing device's byte */
        r->eax = (r->eax & ~0xFFu) | inb(0x60);
        return;
    }
    r->eax = (r->eax & ~0xFFu) | k->cur;
    if (k->obf) {
        k->obf = 0;
        request(r->ebx, 0);
    }
}

/* the VM's port 64h: the status with the VM's output buffer; commands to
 * the controller. A VM polling the status with interrupts off gets the
 * controller's byte too (the replies to its commands) */
void __cdecl vkd_port64(struct vregs *r)
{
    vu32 st;
    struct kq *k = KQ(r->ebx);
    if (r->ecx & ~4u) { vmm_simulate_io(r); return; }
    if (r->ecx & 4) {
        vu32 v = r->eax & 0xFF;
        cmd_d1 = v == 0xD1;
        cmd_vm = r->ebx;
        /* F0h-FFh pulse output port lines low for the bits clear: never
         * A20's (bit 1; the reset line's FEh goes as it is) */
        if (v >= 0xF0) v |= 0x02;
        outb(0x64, v);
        return;
    }
    st = inb(0x64);
    if (r->ebx == kfocus && !k->obf && !k->q_len && (st & 0x21) == 0x01) {
        key_byte(inb(0x60));
        st = inb(0x64);
    }
    present(r->ebx);
    st &= ~0x01u;
    /* the VM's own output buffer first: while it holds a keyboard byte
     * the status says so (OBF, AUXB clear), as an 8042 shows the byte in
     * its buffer; a mouse byte in the real controller shows (OBF and
     * AUXB) once the VM has read it. Before, AUXB came from the real
     * controller even then, and a BIOS IRQ 12 handler took the keyboard
     * byte for the mouse's (UNVERIFIED against Windows 3.1: the two never
     * met in a probe) */
    if (k->obf) st = (st & ~0x20u) | 0x01;
    else if (st & 0x20) st |= 0x01;
    r->eax = (r->eax & ~0xFFu) | st;
}

/* port 92h (the system control port: bit 1 A20, bit 0 a fast reset):
 * A20 stays on, as for the 8042's output port (a DOS box switching it off
 * there ended Windows 3.1 too, VX12 B: w16 goes on instead); the rest
 * goes to the port as written */
void __cdecl vkd_port92(struct vregs *r)
{
    if (r->ecx & ~4u) { vmm_simulate_io(r); return; }
    if (r->ecx & 4) outb(0x92, (r->eax & 0xFF) | 0x02);
    else r->eax = (r->eax & ~0xFFu) | inb(0x92);
}

void __cdecl vkd_service(struct vregs *r)
{
    vu32 i;
    struct hotkey *h;

    r->eflags &= ~VF_CF;
    switch (r->arg) {
    case 0x00:                              /* VKD_Get_Version */
        r->eax = 0x0200;
        return;
    case 0x01:                              /* VKD_Define_Hot_Key */
        for (i = 0; i < NHOT && hot[i].used; i++)
            ;
        if (i == NHOT) { r->eflags |= VF_CF; return; }
        h = &hot[i];
        h->used = 1;
        h->scan = (vu8)r->eax;
        h->type = (vu8)(r->eax >> 8);
        h->shift = r->ebx;
        h->flags = (vu8)r->ecx;
        h->proc = r->esi;
        h->ref = r->edx;
        h->down = 0;
        for (i = 0; i < 8; i++) h->off[i] = 0;
        r->eax = (vu32)h;
        return;
    case 0x02:                              /* VKD_Remove_Hot_Key: EAX the handle */
        h = (struct hotkey *)r->eax;
        if (h >= hot && h < hot + NHOT) h->used = 0;
        return;
    case 0x03: case 0x04:                   /* VKD_Local_Enable/Disable_Hot_Key: EAX, EBX the VM */
        h = (struct hotkey *)r->eax;
        if (h >= hot && h < hot + NHOT && CB_VMID(r->ebx) < 256) {
            vu32 id = CB_VMID(r->ebx);
            if (r->arg == 0x04) h->off[id >> 5] |= 1u << (id & 31);
            else h->off[id >> 5] &= ~(1u << (id & 31));
        }
        return;
    case 0x06:                              /* VKD_Cancel_Hot_Key_State */
    case 0x09:                              /* VKD_Define_Paste_Mode */
    case 0x0B:                              /* VKD_Cancel_Paste */
    case 0x0E:                              /* VKD_Flush_Msg_Key_Queue */
        return;
    case 0x08:                              /* VKD_Get_Kbd_Owner */
        r->ebx = kfocus;
        return;
    case 0x0C:                              /* VKD_Get_Msg_Key */
    case 0x0D:                              /* VKD_Peek_Msg_Key: no message mode yet */
        r->eax = 0;
        r->eflags |= VF_ZF;
        return;
    case 0x05:                              /* VKD_Reflect_Hot_Key: EAX the hot key */
        h = (struct hotkey *)r->eax;
        if (h < hot || h >= hot + NHOT || !h->used) return;
        /* its press to the VM EBX names (the DDK; the keyboard's VM if 0)
         * and its release, when it comes */
        {
            vu32 vm = r->ebx ? r->ebx : kfocus;
            if (h->type == 1) q_put(vm, 0xE0);
            q_put(vm, h->scan);
            h->down = 0;
            present(vm);
        }
        return;
    case 0x07: {                            /* VKD_Force_Keys: ESI scan codes, ECX count */
        /* as if they came from the keyboard: the shift keys' state
         * follows them (REBOOT gives back the Ctrl and Alt releases its
         * screen read) */
        int fe0 = 0;
        for (i = 0; i < r->ecx; i++) {
            vu8 b = ((vu8 *)r->esi)[i];
            if (!q_put(kfocus, b)) {
                r->ecx -= i;                /* the ones that did not fit */
                r->eflags |= VF_CF;
                present(kfocus);
                return;
            }
            if (b == 0xE0) { fe0 = 1; continue; }
            track_mods(b, fe0);
            fe0 = 0;
        }
        present(kfocus);
        return;
    }
    case 0x0A:                              /* VKD_Start_Paste: not yet (DOS boxes) */
        r->eflags |= VF_CF;
        return;
    }
    r->eflags |= VF_CF;
}

#define VKD_ID            0x000D

void __cdecl vkd_control(struct vregs *r)
{
    struct vregs s;
    r->eflags &= ~VF_CF;
    if (r->eax == DESTROY_VM) {
        /* its ID is the next VM's: no hot key stays disabled for it, and
         * the keyboard is not left with a VM that is gone */
        vu32 id = CB_VMID(r->ebx), i;
        if (id < 256)
            for (i = 0; i < NHOT; i++) hot[i].off[id >> 5] &= ~(1u << (id & 31));
        if (kfocus == r->ebx) { kfocus = sys_vm; sync_mods(kfocus); }
        if (cmd_vm == r->ebx) cmd_d1 = 0;
        return;
    }
    if (r->eax == SET_DEVICE_FOCUS && (r->edx == 0 || r->edx == VKD_ID)) {
        /* the keyboard to VM EBX: what is queued for the other stays
         * there (UNVERIFIED) */
        kfocus = r->ebx;
        sync_mods(kfocus);
        return;
    }
    if (r->eax == SYS_CRITICAL_INIT) {
        sys_vm = r->ebx;
        kfocus = sys_vm;
        cb_off = vmm_alloc_cb_area(sizeof(struct kq), 0);
        vregs_clear(&s);
        vkd_vid.irq = 1;
        vkd_vid.hw_int_proc = (vu32)vkd_hw;
        vkd_vid.eoi_proc = (vu32)vkd_eoi;
        vkd_vid.iret_time_out = 500;
        s.edi = (vu32)&vkd_vid;
        vpicd_virtualize_irq(&s);
        if (s.eflags & VF_CF) r->eflags |= VF_CF;
        else {
            /* the hot keys come whatever the VM masks (UNVERIFIED: what
             * Windows 3.1's VKD does with the physical mask) */
            irq_handle = s.eax;
            vregs_clear(&s);
            s.eax = irq_handle;
            vpicd_physically_unmask(&s);
        }
        /* the VM's controller ports */
        vregs_clear(&s);
        s.esi = (vu32)vkd_io60;
        s.edx = 0x60;
        vmm_install_io_handler(&s);
        vregs_clear(&s);
        s.esi = (vu32)vkd_io64;
        s.edx = 0x64;
        vmm_install_io_handler(&s);
        vregs_clear(&s);
        s.esi = (vu32)vkd_io92;
        s.edx = 0x92;
        vmm_install_io_handler(&s);
    }
}

/* a VM handle (VMM Validate_VM_Handle) */
static int valid_vm(vu32 vm)
{
    struct vregs s;
    vregs_clear(&s);
    s.ebx = vm;
    vmm_validate_vm(&s);
    return !(s.eflags & VF_CF);
}

/* the PM API: function 0 AX=0200h (VX3); the others UNVERIFIED. A VM
 * handle that is not one fails (carry; UNVERIFIED what Windows 3.1 does) */
void __cdecl vkd_api(struct vregs *r)
{
    struct client_regs *c = (struct client_regs *)r->ebp;
    if ((c->eax & 0xFFFF) == 0) {
        c->eax = (c->eax & 0xFFFF0000u) | 0x0200;
        c->eflags &= ~VF_CF;
    } else if ((c->eax & 0xFFFF) == 1 && c->ebx && valid_vm(c->ebx)) {
        /* 1: a key into VM EBX, CH the scan code, CL the count (WINOA386
         * passes on the keys its window gets for a DOS VM that does not
         * have the keyboard, e.g. Tab's or Esc's release after a switch;
         * Windows 3.1: AX back 1, carry clear) */
        vu32 n = c->ecx & 0xFF, b = (c->ecx >> 8) & 0xFF;
        if (!n) n = 1;
        while (n--) q_put(c->ebx, b);
        present(c->ebx);
        c->eflags &= ~VF_CF;
    } else
        c->eflags |= VF_CF;
}
