/* vkd.c - w16's VKD, the virtual keyboard device (device 000Dh), a module
 * of WIN386.EXE's W3 table (ARCHITECTURE.md 12.8).
 *
 * It takes IRQ 1 from VPICD at Sys_Critical_Init (Windows 3.1's VKD had
 * it by then: VXP), reads each byte from the keyboard controller (port
 * 60h), keeps the global shift state and looks for hot keys: a hot key's
 * press and release go to the device that defined it
 * (VKD_Define_Hot_Key: REBOOT's Ctrl+Alt+Del) and not to the VM; every
 * other byte goes to the keyboard owner, the System VM, as an interrupt,
 * whose handler (KEYBOARD.DRV's or the BIOS's) reads port 60h again: the
 * controller keeps the byte until it sends the next one. (Windows 3.1
 * traps port 60h for the VM instead; w16 cannot while the System VM's
 * protected-mode code runs with IOPL 3: ARCHITECTURE.md 12.8.)
 *
 * Observed under Windows 3.1 (VXP, VX3): VKD_Get_Version AX=0200h;
 * VKD_Get_Kbd_Owner the System VM; VKD_Peek_Msg_Key nothing (EAX=0, zero
 * flag); the PM API's function 0 AX=0200h.
 * Sources: the DDK's Virtual Device Adaptation Guide (the VKD services,
 * VKD_Define_Hot_Key's callback registers, the VPICD services); the IBM
 * PC keyboard scan codes (set 1, E0h prefixed extended keys).
 * UNVERIFIED: the numeric values of VKD_Define_Hot_Key's flags and shift
 * state bits in Windows 3.1 (w16's own below, used by w16's REBOOT), and
 * everything about pasting, forced keys and message mode.
 */
#include "w16vxd.h"

#define NULL ((void *)0)

void __cdecl vpicd_virtualize_irq(struct vregs *r);
void __cdecl vpicd_set_int_request(struct vregs *r);
void __cdecl vpicd_phys_eoi(struct vregs *r);
void __cdecl vxd_call_proc(vu32 proc, struct vregs *r);
extern char vkd_hw[];
#pragma aux vkd_hw "*"

static vu32 inb(vu32 port);
#pragma aux inb = "in al, dx" "movzx eax, al" parm [edx] value [eax] modify exact [eax];

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
};
#define NHOT 32
static struct hotkey hot[NHOT];

static struct vid vkd_vid;
static vu32 irq_handle, sys_vm;
static vu32 shift;          /* SS_* */
static int ext;             /* the last byte was E0h */

static void vregs_clear(struct vregs *r)
{
    vu32 *p = (vu32 *)r, i;
    for (i = 0; i < sizeof(*r) / 4; i++) p[i] = 0;
}

static void track_shift(vu32 code, int up)
{
    vu32 bit = 0;
    switch (code) {
    case 0x2A: case 0x36: bit = SS_SHIFT; break;
    case 0x1D: bit = SS_CTRL; break;        /* left, or right with E0h */
    case 0x38: bit = SS_ALT; break;         /* left, or right with E0h */
    }
    if (bit) shift = up ? (shift & ~bit) : (shift | bit);
}

/* a hot key for this byte? calls it; nonzero: the byte is the hot key's */
static int hot_key(vu32 code, int up)
{
    vu32 i;
    struct vregs r;
    for (i = 0; i < NHOT; i++) {
        struct hotkey *h = &hot[i];
        if (!h->used || h->scan != code) continue;
        if (h->type != 0xFF && h->type != (vu8)(ext ? 1 : 0)) continue;
        if (up) {
            if (!h->down) continue;
            h->down = 0;
        } else {
            if ((shift & (h->shift >> 16)) != (h->shift & 0xFFFF)) continue;
            h->down = 1;
        }
        if (h->flags & (up ? HK_RELEASE : HK_PRESS)) {
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

/* VID_Hw_Int_Proc for IRQ 1 */
void __cdecl vkd_hw_int(struct vregs *r)
{
    struct vregs s;
    vu32 b = inb(0x60), code = b & 0x7F;
    int up = (b & 0x80) != 0, mine = 0;

    if (b == 0xE0) ext = 1;
    else {
        mine = hot_key(code, up);
        track_shift(code, up);
        ext = 0;
    }
    vregs_clear(&s);
    s.eax = r->eax;
    s.ebx = sys_vm;
    if (mine) vpicd_phys_eoi(&s);           /* the VM does not see it */
    else vpicd_set_int_request(&s);
    r->eflags &= ~VF_CF;
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
        r->eax = (vu32)h;
        return;
    case 0x02:                              /* VKD_Remove_Hot_Key: EAX the handle */
        h = (struct hotkey *)r->eax;
        if (h >= hot && h < hot + NHOT) h->used = 0;
        return;
    case 0x03: case 0x04:                   /* VKD_Local_Enable/Disable_Hot_Key: one VM */
    case 0x06:                              /* VKD_Cancel_Hot_Key_State */
    case 0x09:                              /* VKD_Define_Paste_Mode */
    case 0x0B:                              /* VKD_Cancel_Paste */
    case 0x0E:                              /* VKD_Flush_Msg_Key_Queue */
        return;
    case 0x08:                              /* VKD_Get_Kbd_Owner */
        r->ebx = sys_vm;
        return;
    case 0x0C:                              /* VKD_Get_Msg_Key */
    case 0x0D:                              /* VKD_Peek_Msg_Key: no message mode yet */
        r->eax = 0;
        r->eflags |= VF_ZF;
        return;
    case 0x05:                              /* VKD_Reflect_Hot_Key */
    case 0x07:                              /* VKD_Force_Keys */
    case 0x0A:                              /* VKD_Start_Paste */
        /* putting keys into the VM needs port 60h trapped (not yet) */
        r->eflags |= VF_CF;
        return;
    }
    r->eflags |= VF_CF;
}

#define SYS_CRITICAL_INIT 0x00

void __cdecl vkd_control(struct vregs *r)
{
    struct vregs s;
    r->eflags &= ~VF_CF;
    if (r->eax == SYS_CRITICAL_INIT) {
        sys_vm = r->ebx;
        vregs_clear(&s);
        vkd_vid.irq = 1;
        vkd_vid.hw_int_proc = (vu32)vkd_hw;
        vkd_vid.iret_time_out = 500;
        s.edi = (vu32)&vkd_vid;
        vpicd_virtualize_irq(&s);
        if (s.eflags & VF_CF) r->eflags |= VF_CF;
        else irq_handle = s.eax;
    }
}

/* the PM API: function 0 AX=0200h (VX3); the others UNVERIFIED */
void __cdecl vkd_api(struct vregs *r)
{
    struct client_regs *c = (struct client_regs *)r->ebp;
    if ((c->eax & 0xFFFF) == 0) {
        c->eax = (c->eax & 0xFFFF0000u) | 0x0200;
        c->eflags &= ~VF_CF;
    } else
        c->eflags |= VF_CF;
}
