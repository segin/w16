/* vxd.c - w16 VMM: the virtual devices (ARCHITECTURE.md 12.7). The VxDs
 * the loader placed after the VMM (SYSTEM.INI [386Enh]), their control
 * messages in init order, and the VxD dynamic links (INT 20h) to the
 * services of the VMM and of the devices.
 *
 * Sources: the Windows 3.1 DDK's Virtual Device Adaptation Guide
 * (.tools/docs/vdag31: Declare_Virtual_Device, the control messages,
 * VMMcall/VxDcall, Get_VMM_Version and the VM handle services,
 * Client_Reg_Struc, the control block); the message numbers, their order,
 * their registers and the services' results observed under Windows 3.1
 * with VXP.386 (tests/vxd/vxp.asm, tests/vxpprobe.sh):
 *   Sys_Critical_Init 0, Device_Init 1, Init_Complete 2, Sys_VM_Init 3
 *   (EBX the System VM's handle, ESI the command tail: a length byte and
 *   WIN386's tail), then Begin_PM_App 15h when KRNL386 enters protected
 *   mode (EDX 0 for a 16-bit client, EDI an application control block),
 *   End_PM_App 16h when it ends (EDI the same), Sys_VM_Terminate 4,
 *   System_Exit 5, Sys_Critical_Exit 6 at exit;
 *   Get_VMM_Version AX=030Ah ECX=0; Get_Cur_VM_Handle and
 *   Get_Sys_VM_Handle the same handle; the control block starts with the
 *   status (0), the high linear address, the client register pointer and
 *   the VM ID (1).
 */
#include "vmm.h"

#pragma pack(1)
struct ddb {
    u32 next;
    u16 sdk_version, device_id;
    u8 major, minor;
    u16 flags;
    char name[8];
    u32 init_order, control, v86_api, pm_api, v86_csip, pm_csip, ref_data;
    u32 svc_table, svc_count;
};

#pragma pack()

extern struct ddb VMM_DDB;
#pragma aux VMM_DDB "*"           /* ddb.asm: no C decoration */
u32 __cdecl vxd_control(u32 proc, u32 msg, u32 ebx, u32 esi, u32 edx, u32 edi, u32 ebp);
extern u8 svc_common[];


#define VMM_SERVICES        242     /* Windows 3.1's VMM (docs/win386-vxds.md) */

static struct ddb **devs;           /* in init order */
static struct ddb *find_dev(u32 id);
static u32 ndevs;
#define sys_cb (sysvm.cb)
#define sys_client (sysvm.client)
static u8 *sys_acb;                 /* the application control block (Begin_PM_App) */
static u32 *vmm_slots;              /* the VMM's service table: stub addresses */

u32 vm_handle(void)
{
    return VMH(cur_vm);
}

/* a stub per service: PUSH id; JMP svc_common */
static u32 *make_slots(u32 dev, u32 n)
{
    u32 *slots = kmalloc(n * 4), i;
    u8 *code = kmalloc(n * 10);
    if (!slots || !code) panic("no memory for a service table");
    for (i = 0; i < n; i++) {
        u8 *c = code + i * 10;
        c[0] = 0x68;
        *(u32 *)(c + 1) = (dev << 16) | i;
        c[5] = 0xE9;
        *(u32 *)(c + 6) = (u32)svc_common - (u32)(c + 10);
        slots[i] = (u32)c;
    }
    return slots;
}

static void client_fill(struct client_regs *c, struct cregs *r)
{
    if (!r) return;
    c->edi = r->edi; c->esi = r->esi; c->ebp = r->ebp; c->ebx = r->ebx;
    c->edx = r->edx; c->ecx = r->ecx; c->eax = r->eax;
    c->eip = r->eip; c->cs = (u16)r->cs; c->eflags = r->eflags; c->esp = r->esp;
    c->ss = (u16)r->ss; c->es = (u16)r->es; c->ds = (u16)r->ds;
    c->fs = (u16)r->fs; c->gs = (u16)r->gs;
}

/* a control message about VM v to every device, in init order (EBX v's
 * handle, EBP its client registers); nonzero if one refused (carry) */
u32 vxd_send(u32 msg, struct vm *v, u32 edx, u32 esi, u32 edi)
{
    u32 i, cf = 0;
    for (i = 0; i < ndevs; i++) {
        if (!devs[i] || !devs[i]->control) continue;
        if (vxd_control(devs[i]->control, msg, VMH(v), esi, edx, edi, (u32)v->client)) cf = 1;
    }
    if (msg == SET_DEVICE_FOCUS && (edx == 0 || edx == 0x000A)) vdd_focus(v);
    return cf;
}

/* the same in the reverse of the init order (UNVERIFIED which messages
 * Windows 3.1 sends so) */
void vxd_send_rev(u32 msg, struct vm *v, u32 edx, u32 esi, u32 edi)
{
    u32 i;
    for (i = ndevs; i-- > 0; ) {
        if (!devs[i] || !devs[i]->control) continue;
        vxd_control(devs[i]->control, msg, VMH(v), esi, edx, edi, (u32)v->client);
    }
}

/* send msg to every device, in init order (rev: the reverse); a device
 * that fails an init message (carry) is dropped */
static void broadcast(u32 msg, u32 esi, u32 edx, u32 edi, int rev)
{
    u32 i, j;
    for (j = 0; j < ndevs; j++) {
        i = rev ? ndevs - 1 - j : j;
        if (!devs[i] || !devs[i]->control) continue;
        if (vxd_control(devs[i]->control, msg, (u32)sys_cb, esi, edx, edi, (u32)sys_client)
            && msg <= INIT_COMPLETE) {
            trace("VMM: device %c%c%c%c%c%c%c%c failed message %x: not loaded\n",
                  devs[i]->name[0], devs[i]->name[1], devs[i]->name[2], devs[i]->name[3],
                  devs[i]->name[4], devs[i]->name[5], devs[i]->name[6], devs[i]->name[7], msg);
            devs[i] = 0;
        }
    }
}

/* w16's own VTD (device 0005h, init order 14000000h, 8 services: docs/
 * win386-vxds.md). Its services as Windows 3.1's answered VXP:
 * VTD_Get_Version AX=030Ah, the fastest period 1 ms in EBX, the slowest
 * 54 ms (36h) in ECX; VTD_Get_Interrupt_Period 54 ms. */
#define NBUILTIN 4
static void vtd_init(void);
/* w16's VDD (vdd.c, in this module): its DDB for other devices' dynamic
 * links (Windows 3.1's: "VDD", 000Ah, 2.00, init order 28000000h, 14
 * services); its control messages come from vxd_send (vdd_focus) */
/* w16's V86MMGR (v86mmgr.c): Windows 3.1's "V86MMGR", 0006h, 1.00, init
 * order A0000000h, 21 services */
static struct ddb v86mm_ddb = {
    0, 0x030A, 0x0006, 1, 0, 0, { 'V', '8', '6', 'M', 'M', 'G', 'R', ' ' },
    0xA0000000, 0, 0, 0, 0, 0, 0, 0, 21
};
static struct ddb vdd_ddb = {
    0, 0x030A, 0x000A, 2, 0, 0, { 'V', 'D', 'D', ' ', ' ', ' ', ' ', ' ' },
    0x28000000, 0, 0, 0, 0, 0, 0, 0, 14
};
/* w16's SHELL (shell.c) for other devices' links: Windows 3.1's "SHELL",
 * 0017h, init order B0000000h, 6 services (its API is vxdapi.c's) */
static struct ddb shell_ddb = {
    0, 0x030A, 0x0017, 3, 0, 0, { 'S', 'H', 'E', 'L', 'L', ' ', ' ', ' ' },
    0xB0000000, 0, 0, 0, 0, 0, 0, 0, 6
};
static struct ddb vtd_ddb = {
    0, 0x030A, 0x0005, 3, 10, 0, { 'V', 'T', 'D', ' ', ' ', ' ', ' ', ' ' },
    0x14000000, 0, 0, 0, 0, 0, 0, 0, 8
};
#define VTD_FASTEST 1
#define VTD_SLOWEST 0x36
static u32 vtd_min_period = VTD_SLOWEST;   /* the shortest one asked for */
static u32 vtd_requests[16], vtd_nreq;

/* VTD takes IRQ 0 through VPICD (VPICD_Virtualize_IRQ, then
 * VPICD_Physically_Unmask: the ticks keep coming whatever the VM masks),
 * ends each tick on the hardware at once (VPICD_Phys_EOI) and passes it
 * to the VMs whose own timer has run out (VPICD_Set_Int_Request); a VM's
 * EOI clears its request (VID_EOI_Proc).
 *
 * The 8254's channel 0 is virtual (ARCHITECTURE.md 12.14): ports 40h and
 * 43h are trapped (as Windows 3.1's VTD has them: VSD's probe could not
 * install a handler there, 12.8), each VM programs a timer of its own
 * (struct vtimer: the BIOS's 65536 counts, mode 3, until it writes one),
 * and the real channel 0 runs at the shortest period anyone needs: a
 * VM's timer or a device's VTD_Begin_Min_Int_Period. Each real interrupt
 * adds its counts to every VM's timer and to the system time, and a VM
 * gets a tick when its own period is over (one request at most: a VM
 * that has not taken the last loses the next, UNVERIFIED what Windows
 * 3.1 does). A read of port 40h gives the VM its own counter. Channels 1
 * and 2 (43h's control words for them, the speaker's 42h) go to the
 * hardware. Before, the ports were the hardware's: w16's TIMER.DRV set
 * the real timer to 1 ms, the VMM counted every interrupt as 54.9 ms
 * (time-outs, VPICD's IRET time-outs, the scheduler's slices all 55
 * times too fast), and a device's VTD_Begin_Min_Int_Period changed
 * nothing: Windows 3.1's VTDAPI.386, whose events are VMM time-outs, then
 * called them at most every 55 ms. */
extern u8 vtd_hw_entry[], vtd_eoi_entry[];
static struct vid vtd_vid;
static u32 vtd_irq;                     /* VPICD's handle */
#define PIT_HZ      1193182u            /* the 8254's input clock */
static u32 phys_count = 65536;          /* the real channel 0's period (counts) */
static u32 phys_ms = VTD_SLOWEST;       /* that in ms for VTD_Get_Interrupt_Period */
static u32 phys_skip;                   /* the next interrupt belongs to the old period */
static u32 tot_lo, tot_hi;              /* counts since the start (VTD_Get_Real_Time) */
static u32 sched_acc;                   /* counts toward the scheduler's next 54.9 ms */

static u32 svc_slot(u32 dev, u32 svc)
{
    struct ddb *d = find_dev(dev);
    if (!d || !d->svc_table || svc >= d->svc_count) return 0;
    return *(u32 *)(d->svc_table + svc * 4);
}

/* Release_Time_Slice called through the VMM's service table, so that a
 * device that hooked it (Hook_Device_Service) sees the call */
void vxd_release_time_slice(void)
{
    struct svcregs s;
    memset(&s, 0, sizeof(s));
    s.ebx = VMH(cur_vm);
    vmm_call_proc(vmm_slots[0x38], &s);
}

/* a device's service from the VMM's C code; 0 if it is not there */
int vxd_service_call(u32 dev, u32 svc, struct svcregs *s)
{
    u32 slot = svc_slot(dev, svc);
    if (!slot) return 0;
    vmm_call_proc(slot, s);
    return 1;
}

static void vpicd_call(u32 svc, u32 eax, u32 ebx)
{
    struct svcregs s;
    memset(&s, 0, sizeof(s));
    s.eax = eax;
    s.ebx = ebx;
    vmm_call_proc(svc_slot(3, svc), &s);
}

/* ---- the VMM's time-outs (Set_Global_Time_Out, Cancel_Time_Out; the
 * DDK's descriptions), looked at on every real timer interrupt (every
 * millisecond while someone has asked for VTD_Begin_Min_Int_Period 1);
 * the callback gets EBX the current VM, ECX the milliseconds since it was
 * due, EDX its reference data, EBP the client registers ---- */
struct tmo { struct tmo *next; u32 magic, due, proc, ref, tick; };
static u32 tmo_ticks;                       /* real timer interrupts so far */
#define TMO_MAGIC 0x4F4D4954u               /* "TIMO" */
static struct tmo *tmos;
static u32 sys_ms, sys_frac;                /* time since start: ms, and counts x 1000 */
u32 hb_tmo_set, hb_tmo_run, hb_ev_q, hb_ev_run, hb_nest;

static void tmo_tick(void)
{
    struct tmo **pp, *t;
    struct svcregs s;
    tmo_ticks++;
    for (pp = &tmos; (t = *pp) != NULL; ) {
        /* one set during this tick (by a callback here: a 0 ms time-out
         * set again each time ran for ever) waits for the next */
        if ((s32)(sys_ms - t->due) < 0 || t->tick == tmo_ticks) { pp = &t->next; continue; }
        *pp = t->next;                      /* done: unlinked before it runs */
        t->magic = 0;
        hb_tmo_run++;
        memset(&s, 0, sizeof(s));
        s.ebx = VMH(cur_vm);
        s.ecx = sys_ms - t->due;
        s.edx = t->ref;
        s.ebp = (u32)cur_vm->client;
        vmm_call_proc(t->proc, &s);
        kfree(t);
        pp = &tmos;                         /* the callback may have changed the list */
    }
}

/* the real channel 0: counts gone in its period (interrupts off) */
static u32 phys_elapsed(void)
{
    u32 c;
    if (phys_skip) return 0;                /* restarted; its old interrupt still waits */
    outb(0x43, 0x00);                       /* latch counter 0 */
    c = inb(0x40);
    c |= inb(0x40) << 8;
    if (!c) c = 65536;
    return c > phys_count ? 0 : phys_count - c;
}

/* IRQ 0 requested on the 8259 and not yet taken (its IRR; VPICD leaves
 * port 20h reading the IRR) */
static int irq0_waiting(void)
{
    outb(0x20, 0x0A);
    return inb(0x20) & 1;
}

static u32 vt_count(struct vm *v) { return v->vt.count ? v->vt.count : 65536; }

/* n counts have passed: the system time, the scheduler's ticks, every
 * VM's timer (a VM whose period is over gets its tick) */
static void vtd_counts(u32 n)
{
    struct vm *v;
    u32 q;
    if (!n) return;
    tot_lo += n;
    if (tot_lo < n) tot_hi++;
    sys_frac += n * 1000;
    q = sys_frac / PIT_HZ;
    sys_ms += q;
    sys_frac -= q * PIT_HZ;
    sched_acc += n;
    vm_tick_wakes(1);                   /* an idle VM's release penalty holds */
    for (v = vm_list; v; v = v->next) {
        u32 c = vt_count(v);
        if (v->ended) continue;
        v->vt.acc += n;
        if (v->vt.acc >= c) {
            v->vt.acc %= c;
            vpicd_call(0x02, vtd_irq, VMH(v));      /* VPICD_Set_Int_Request */
        }
    }
    vm_tick_wakes(0);
}

/* the period the real channel 0 should have: the shortest VM timer or
 * device minimum, at most the BIOS's 65536 counts, at least VTD's
 * fastest, 1 ms */
static u32 phys_wanted(void)
{
    struct vm *v;
    u32 c = 65536, m;
    for (v = vm_list; v; v = v->next)
        if (!v->ended && vt_count(v) < c) c = vt_count(v);
    if (vtd_min_period < VTD_SLOWEST) {
        m = vtd_min_period * PIT_HZ / 1000;
        if (m < c) c = m;
    }
    m = VTD_FASTEST * PIT_HZ / 1000;
    return c < m ? m : c;
}

/* the real channel 0 to count c from now (mode 2) */
static void phys_program(u32 c)
{
    phys_count = c;
    phys_ms = c >= 65536 ? VTD_SLOWEST : (c * 1000 + PIT_HZ / 2) / PIT_HZ;
    if (!phys_ms) phys_ms = 1;
    outb(0x43, 0x34);
    outb(0x40, c & 0xFF);
    outb(0x40, (c >> 8) & 0xFF);
    /* an interrupt of the old period already waiting is not counted again */
    phys_skip = irq0_waiting();
}

/* the same, what passed of the old period counted first */
static void phys_set(u32 c)
{
    vtd_counts(phys_elapsed());
    phys_program(c);
}

static void phys_update(void)
{
    u32 c = phys_wanted();
    if (c != phys_count) phys_set(c);
}

/* a VM's virtual counter now (8254: mode 3 counts down by two, twice a
 * period; the others once) */
static u32 vt_value(struct vm *v)
{
    u32 c = vt_count(v), e = (v->vt.acc + phys_elapsed()) % c, r;
    if (!v->vt.count || (v->vt.mode & 3) == 3) {
        r = c - ((2 * e) % c);
        r &= ~1u;
        if (!r) r = c & ~1u;
    } else
        r = c - e;
    return r & 0xFFFF;
}

/* a VM's IN or OUT on port 40h or 43h (io.c), a byte (wider accesses
 * come here byte by byte) */
u32 vtd_io(u32 port, u32 out, u32 data)
{
    struct vtimer *t = &cur_vm->vt;
    u32 b = data & 0xFF, rw = t->count ? t->rw : 3, c;
    if (port == 0x43) {
        if (!out) return data;          /* write-only (the 8254) */
        if ((b >> 6) != 0) {            /* channels 1 and 2, read-back: the hardware */
            outb(0x43, b);
            return data;
        }
        if (!(b & 0x30)) {              /* counter latch */
            if (!t->latched) {
                t->latch = (u16)vt_value(cur_vm);
                t->latched = rw == 3 ? 2 : 1;
            }
            return data;
        }
        if (!t->count) t->count = 65536;
        t->rw = (u8)((b >> 4) & 3);
        t->mode = (u8)((b >> 1) & 7);
        t->wstate = t->rstate = 0;
        t->latched = 0;
        return data;
    }
    if (out) {
        if (rw == 3 && !t->wstate) {
            t->wlow = (u8)b;
            t->wstate = 1;
            return data;
        }
        c = rw == 1 ? b : rw == 2 ? b << 8 : (t->wlow | (b << 8));
        t->wstate = 0;
        /* the count restarts: what passed so far counted with the old
         * one, the real timer started again with the new shortest */
        vtd_counts(phys_elapsed());
        if (!t->count) { t->rw = 3; t->mode = 3; }
        t->count = c ? c : 65536;
        t->acc = 0;
        phys_program(phys_wanted());
        return data;
    }
    if (t->latched) {
        u32 v = t->latch;
        t->latched--;
        if (rw == 2 || (rw == 3 && !t->latched)) return (data & ~0xFFu) | (v >> 8);
        return (data & ~0xFFu) | (v & 0xFF);
    }
    c = vt_value(cur_vm);
    if (rw == 1) return (data & ~0xFFu) | (c & 0xFF);
    if (rw == 2) return (data & ~0xFFu) | (c >> 8);
    t->rstate ^= 1;
    return (data & ~0xFFu) | (t->rstate ? (c & 0xFF) : (c >> 8));
}

static void vtd_init(void)
{
    struct svcregs s;
    memset(&s, 0, sizeof(s));
    vtd_vid.irq = 0;
    vtd_vid.hw_int_proc = (u32)vtd_hw_entry;
    vtd_vid.eoi_proc = (u32)vtd_eoi_entry;
    vtd_vid.iret_time_out = 500;
    s.edi = (u32)&vtd_vid;
    vmm_call_proc(svc_slot(3, 0x01), &s);     /* VPICD_Virtualize_IRQ */
    if (s.eflags & EFL_CF) panic("VTD: VPICD would not give IRQ 0");
    vtd_irq = s.eax;
    /* the real channel 0 in mode 2 (a count read back is linear), the
     * BIOS's period; the VMs' channel 0 trapped from now on */
    outb(0x43, 0x34);
    outb(0x40, 0);
    outb(0x40, 0);
    phys_count = 65536;
    io_trap_bit(0x40, 1);
    io_trap_bit(0x43, 1);
    vpicd_call(0x09, vtd_irq, 0);             /* VPICD_Physically_Unmask */
}

/* for the heartbeat: the time-outs waiting, the real period */
u32 vtd_heartbeat(u32 *n)
{
    struct tmo *t;
    *n = 0;
    for (t = tmos; t; t = t->next) (*n)++;
    return phys_count;
}

/* Get_Last_Updated_System_Time: as of the last tick */
static u32 clock_ms;                    /* the clock: the last tick's time, or a later
                                         * time Get_System_Time gave */
u32 vmm_last_time(void)
{
    return (s32)(clock_ms - sys_ms) > 0 ? clock_ms : sys_ms;
}

/* Get_System_Time (the DDK: to the millisecond, and it "must call the
 * timer device to update the clock"): the last tick's time plus what the
 * real channel 0 has counted since, never less than a time given before;
 * the clock time-outs are set from goes on to it */
u32 vmm_system_time(void)
{
    u32 t = sys_ms + (sys_frac + phys_elapsed() * 1000) / PIT_HZ;
    if ((s32)(t - clock_ms) < 0) t = clock_ms;
    clock_ms = t;
    return t;
}

static int tmo_service(u32 svc, struct svcregs *s)
{
    struct tmo *t, **pp;
    switch (svc) {
    case 0x3C:                              /* Set_Global_Time_Out: EAX ms, EDX ref, ESI proc -> ESI */
        t = kmalloc(sizeof(*t));
        if (!t) { s->esi = 0; return 1; }
        t->magic = TMO_MAGIC;
        t->tick = tmo_ticks;
        hb_tmo_set++;
        /* from the clock: the last tick, or the time Get_System_Time
         * gave since (not now: VTDAPI.386's 10 ms periodic event, set
         * again a little after each tick, came a tick late, every 20 ms;
         * VX16 tev 200 in 4 s, Windows 3.1 399). Not before what
         * Get_System_Time said: VTDAPI sets an MMSYSTEM event's time-out
         * from it; one that came a millisecond before the event's time
         * (from the last tick, when Get_System_Time had moved past it)
         * found the event not due, set no other, and Windows 3.1's
         * TIMER.DRV's events stopped for good: QuickTime's movie with
         * sound stopped at "play" (sbaudio.sh qtw16e, ARCHITECTURE.md
         * 12.14) */
        t->due = vmm_last_time() + s->eax;
        t->proc = s->esi;
        t->ref = s->edx;
        t->next = tmos;
        tmos = t;
        s->esi = (u32)t;
        return 1;
    case 0x3E:                              /* Cancel_Time_Out: ESI the handle (0: nothing) */
        for (pp = &tmos; (t = *pp) != NULL; pp = &t->next)
            if ((u32)t == s->esi) {
                *pp = t->next;
                t->magic = 0;
                kfree(t);
                break;
            }
        return 1;
    }
    return 0;
}

/* VID_Hw_Int_Proc for IRQ 0 (vmmasm.asm calls it with the registers) */
void __cdecl vtd_hw(struct svcregs *r)
{
    u32 n = 4;
    vpicd_call(0x04, vtd_irq, 0);             /* VPICD_Phys_EOI */
    if (phys_skip) phys_skip = 0;           /* the old period's: counted already */
    else vtd_counts(phys_count);
    tmo_tick();
    /* the scheduler's slices and the VDD's look at the screens: every
     * 54.9 ms, as the BIOS tick (a few at most at once) */
    while (sched_acc >= 65536 && n--) {
        sched_acc -= 65536;
        vm_tick();
    }
    if (sched_acc >= 65536) sched_acc %= 65536;
    phys_update();                          /* a VM that ended had the shortest */
    r->eflags &= ~EFL_CF;
}

/* VID_EOI_Proc for IRQ 0: the VM has ended the tick */
void __cdecl vtd_eoi(struct svcregs *r)
{
    vpicd_call(0x03, vtd_irq, r->ebx);        /* VPICD_Clear_Int_Request: EBX the VM */
    r->eflags &= ~EFL_CF;
}

static void vtd_service(u32 svc, struct svcregs *s)
{
    u32 i;
    switch (svc) {
    case 0x0000:                        /* VTD_Get_Version */
        s->eax = 0x030A;
        s->ebx = VTD_FASTEST;
        s->ecx = VTD_SLOWEST;
        return;
    case 0x0001:                        /* VTD_Update_System_Clock: kept at each tick */
    case 0x0005:                        /* VTD_Disable_Trapping, VTD_Enable_Trapping: */
    case 0x0006:                        /* UNVERIFIED what they change; nothing here */
        return;
    case 0x0007: {                      /* VTD_Get_Real_Time: EDX:EAX in the timer's */
        u32 e = phys_elapsed();         /* 1.19 MHz clocks (the DDK: 0.8 us) since the start */
        s->eax = tot_lo + e;
        s->edx = tot_hi + (s->eax < e);
        return;
    }
    case 0x0002:                        /* VTD_Get_Interrupt_Period */
        s->eax = phys_ms;
        return;
    case 0x0003:                        /* VTD_Begin_Min_Int_Period: the real */
        /* timer at least that fast from now (the VMs' ticks keep their rate) */
        if (s->eax < VTD_FASTEST || s->eax > VTD_SLOWEST || vtd_nreq == 16) { s->eflags |= EFL_CF; return; }
        vtd_requests[vtd_nreq++] = s->eax;
        if (s->eax < vtd_min_period) vtd_min_period = s->eax;
        phys_update();
        return;
    case 0x0004:                        /* VTD_End_Min_Int_Period */
        for (i = 0; i < vtd_nreq && vtd_requests[i] != s->eax; i++)
            ;
        if (i == vtd_nreq) { s->eflags |= EFL_CF; return; }
        vtd_requests[i] = vtd_requests[--vtd_nreq];
        vtd_min_period = VTD_SLOWEST;
        for (i = 0; i < vtd_nreq; i++)
            if (vtd_requests[i] < vtd_min_period) vtd_min_period = vtd_requests[i];
        phys_update();
        return;
    }
    panic("VTD service %x is not implemented yet (called from %x)", svc, *(u32 *)(s + 1));
}

/* VPICD's view of the VMs (w16vxd.h) */
static vu32 link_cur_vm(void)
{
    return VMH(cur_vm);
}

static void link_wake(vu32 h)
{
    struct vm *v = vm_of(h);
    if (v) vm_wake(v);
}

/* the devices: the VMM's own DDB and the loader's list, by init order
 * (equal orders: as loaded; UNVERIFIED tie-break), then the init messages */
/* VDMAD's link: a ring-0 linear address's physical one (~0: not
 * present), and physically contiguous pages for its buffer */
static u32 link_lin_to_phys(u32 lin)
{
    return lin_to_phys_x(lin);
}
static u32 link_alloc_run(u32 n, u32 align, u32 max_phys, u32 *phys)
{
    u32 p = page_alloc_run(n, align, max_phys);
    if (!p) return 0;
    *phys = p;
    return phys_to_lin(p);
}

void vxd_init(void)
{
    u32 n = 1 + NBUILTIN + info->vxd_count, i, j;
    u32 *list = (u32 *)(W386_VMM_LIN + info->vxd_list);

    devs = kmalloc(n * sizeof(*devs));
    sys_acb = kmalloc(256);
    if (!devs || !sys_acb) panic("no memory for the devices");
    /* the System VM's control block: vm.c (vm_sys_init) */
    vmm_slots = make_slots(1, VMM_SERVICES);
    devs[ndevs++] = &VMM_DDB;
    /* w16's own devices (C, in this module), each with its service table */
    vtd_ddb.svc_table = (u32)make_slots(5, vtd_ddb.svc_count);
    devs[ndevs++] = &vtd_ddb;
    vdd_ddb.svc_table = (u32)make_slots(0x0A, vdd_ddb.svc_count);
    devs[ndevs++] = &vdd_ddb;
    v86mm_ddb.svc_table = (u32)make_slots(0x06, v86mm_ddb.svc_count);
    devs[ndevs++] = &v86mm_ddb;
    shell_ddb.svc_table = (u32)make_slots(0x17, shell_ddb.svc_count);
    devs[ndevs++] = &shell_ddb;
    for (i = 0; i < info->vxd_count; i++) {
        struct ddb *d = (struct ddb *)list[i];
        devs[ndevs++] = d;
        trace("VMM: device %c%c%c%c%c%c%c%c, ID %x, init order %x\n",
              d->name[0], d->name[1], d->name[2], d->name[3], d->name[4], d->name[5],
              d->name[6], d->name[7], d->device_id, d->init_order);
    }
    for (i = 1; i < ndevs; i++) {       /* stable: by init order */
        struct ddb *d = devs[i];
        for (j = i; j > 0 && devs[j - 1]->init_order > d->init_order; j--)
            devs[j] = devs[j - 1];
        devs[j] = d;
    }
    /* DDB_Next: each device's DDB names the one after it in init order,
     * the last's is 0. Windows 3.1: VXB.386 (tests/vxd/vxm.c, SBPROBE),
     * loaded just before VSBD.386 with the same init order, found VSBD's
     * DDB as its DDB_Next; from VXB, with VSBD's after it, the chain had
     * 4 devices to its end (VXP.386 had found its DDB_Next set) */
    for (i = 0; i < ndevs; i++) devs[i]->next = i + 1 < ndevs ? (u32)devs[i + 1] : 0;
    /* VPICD (w16's module) owns the interrupts: without it nothing runs.
     * Windows 3.1 without *vpicd stops with this message (ARCHITECTURE.md
     * 12.2: device 0003h, service 0001h, VPICD_Virtualize_IRQ) */
    for (i = 0; i < ndevs; i++)
        if (devs[i]->device_id == 3 && devs[i]->ref_data
            && ((struct vpicd_link *)devs[i]->ref_data)->magic == VPICD_LINK_MAGIC)
            vpicd = (struct vpicd_link *)devs[i]->ref_data;
    if (!vpicd)
        fatal("Invalid VxD dynamic link call to device number 0003, service 0001.\n"
              "Your w16 configuration is invalid. Run the w16 Setup program\n"
              "again to correct this problem.\n");
    for (i = 0; i < ndevs; i++)                     /* VDMAD's link (w16vxd.h) */
        if (devs[i]->device_id == 4 && devs[i]->ref_data
            && ((struct vdmad_link *)devs[i]->ref_data)->magic == VDMAD_LINK_MAGIC) {
            struct vdmad_link *l = (struct vdmad_link *)devs[i]->ref_data;
            l->lin_to_phys = link_lin_to_phys;
            l->alloc_run = link_alloc_run;
            l->buf_kb = info->dma_buf_kb;
            l->buf_conv = (u32)info->dma_buf_seg << 4;
        }
    for (i = 0; i < ndevs; i++)                     /* REBOOT's link (w16vxd.h) */
        if (devs[i]->device_id == 9 && devs[i]->ref_data) reboot_link_of(devs[i]->ref_data);
    vpicd->deliver = vmm_deliver;
    vpicd->cur_vm = link_cur_vm;
    vpicd->wake = link_wake;
    vpicd->can_deliver = vmm_can_deliver;
    vpicd->sys_vm = (u32)sys_cb;
    vpicd->rm_base1 = info->pic1_base;
    vpicd->rm_base2 = info->pic2_base;
    broadcast(SYS_CRITICAL_INIT, info->tail_lin, 0, 0, 0);
    vtd_init();
    broadcast(DEVICE_INIT, info->tail_lin, 0, 0, 0);
    broadcast(INIT_COMPLETE, info->tail_lin, 0, 0, 0);
    broadcast(SYS_VM_INIT, info->tail_lin, 0, 0, 0);
    shell_init();
}

/* KRNL386 entered protected mode (dpmi_switch) / ended (terminate_client) */
void vxd_pm_app(int begin, struct cregs *r)
{
    client_fill(sys_client, r);
    if (begin)
        broadcast(BEGIN_PM_APP, 0, sysvm.client32 ? 1 : 0, (u32)sys_acb, 0);
    else
        broadcast(END_PM_APP, 0, 0, (u32)sys_acb, 0);
}

/* Windows ends (exit_windows). UNVERIFIED: the order Windows 3.1 sends
 * these to several devices in (w16: the reverse of the init order). */
void vxd_exit(struct cregs *r)
{
    client_fill(sys_client, r);
    broadcast(SYS_VM_TERMINATE, 0, 0, 0, 1);
    broadcast(SYSTEM_EXIT, 0, 0, 0, 1);
    broadcast(SYS_CRITICAL_EXIT, 0, 0, 0, 1);
}

/* a client register structure of its own for a device's I/O handler
 * (io.c), filled from r and put back after: the handler can make the VM
 * take an interrupt (VPICD_Set_Int_Request) whose handler traps again,
 * so a shared structure (sys_client) would be overwritten under it */
static void client_store(struct cregs *r, struct client_regs *c);
void vxd_client(struct cregs *r, struct client_regs *c)
{
    client_fill(c, r);
}

void vxd_client_back(struct cregs *r, struct client_regs *c)
{
    client_store(r, c);
}

static void client_store(struct cregs *r, struct client_regs *c)
{
    r->edi = c->edi; r->esi = c->esi; r->ebp = c->ebp; r->ebx = c->ebx;
    r->edx = c->edx; r->ecx = c->ecx; r->eax = c->eax;
    r->eip = c->eip; r->cs = c->cs; r->eflags = c->eflags; r->esp = c->esp;
    r->ss = c->ss; r->es = c->es; r->ds = c->ds; r->fs = c->fs; r->gs = c->gs;
}

/* a loaded device's V86 API procedure, or 0 */
u32 vxd_v86_api(u32 id)
{
    u32 i;
    for (i = 0; i < ndevs; i++)
        if (devs[i] && devs[i] != &VMM_DDB && devs[i]->device_id == id && devs[i]->v86_api)
            return devs[i]->v86_api;
    return 0;
}

/* a loaded device's protected-mode API procedure for INT 2Fh 1684h, or 0 */
u32 vxd_pm_api(u32 id)
{
    u32 i;
    for (i = 0; i < ndevs; i++)
        if (devs[i] && devs[i] != &VMM_DDB && devs[i]->device_id == id && devs[i]->pm_api)
            return devs[i]->pm_api;
    return 0;
}

/* call a device's API procedure (the DDK: EBX the VM's handle, EBP its
 * client registers, which the procedure reads and changes) for the
 * client in r */
void vxd_call_api(u32 proc, struct cregs *r)
{
    struct client_regs *c = cur_vm->client, outer = *c;
    struct vm *v = cur_vm;
    /* the VM's client registers (CB_Client_Pointer) are this call's while
     * it runs, and an outer call's again after it (an API called from an
     * interrupt's handler while another API's procedure waited) */
    client_fill(c, r);
    vxd_control(proc, r->eax, VMH(cur_vm), r->esi, r->edx, r->edi, (u32)c);
    client_store(r, c);
    *v->client = outer;
}

/* ---- dynamic links ---- */
static struct ddb *find_dev(u32 id)
{
    u32 i;
    for (i = 0; i < ndevs; i++)
        if (devs[i] && devs[i]->device_id == id) return devs[i];
    return NULL;
}

/* INT 20h in ring-0 code, then a dword: service number (bit 15: a jump)
 * and device ID. The six bytes become CALL/JMP DWORD PTR [slot], the
 * slot being the device's service table entry, and run again. */
void vxd_link(struct tframe *f)
{
    u8 *p = (u8 *)f->eip;
    u32 id = *(u32 *)p, dev = id >> 16, svc = id & 0x7FFF, slot = 0;
    struct ddb *d;

    if (dev == 1 && svc < VMM_SERVICES)
        slot = (u32)&vmm_slots[svc];
    else if (dev != 1 && (d = find_dev(dev)) != NULL && svc < d->svc_count && d->svc_table
             && *(u32 *)(d->svc_table + svc * 4))
        slot = d->svc_table + svc * 4;
    if (!slot && dev != 1) {
        /* a device that is not loaded (or a service it does not have):
         * VXQ.386 under Windows 3.1, linking to VPD (000Fh, not in 3.1) or
         * to 7FFFh from Sys_Critical_Init, went on as if the call had
         * returned, no message. The same here; the link is not made.
         * UNVERIFIED: the registers 3.1 leaves, and later calls */
        trace("VMM: dynamic link to device %x service %x from %x: not there, ignored\n",
              dev, svc, f->eip - 2);
        f->eip += 4;
        return;
    }
    if (!slot)
        /* Windows 3.1's message for a link it refuses (ARCHITECTURE.md
         * 12.2: the VMM's own link to VPICD when *vpicd is not loaded),
         * "w16" for "Windows" (ARCHITECTURE.md 3). UNVERIFIED that 3.1
         * refuses a VMM service number past its 242 this way */
        fatal("Invalid VxD dynamic link call to device number %04x, service %04x.\n"
              "Your w16 configuration is invalid. Run the w16 Setup program\n"
              "again to correct this problem.\n", dev, svc);
    p[-2] = 0xFF;
    p[-1] = (id & 0x8000) ? 0x25 : 0x15;
    *(u32 *)p = slot;
    f->eip -= 2;
}

#define ZF_SET(s, c) ((s)->eflags = (c) ? ((s)->eflags | EFL_ZF) : ((s)->eflags & ~EFL_ZF))

/* ---- the VMM's lists (List_Create ... List_Remove_First; the DDK's
 * descriptions). A node is the caller's NodeSize bytes after a hidden
 * link word. Freed nodes go to the list's own free pool. ---- */
struct vlist { u32 magic, node_size; u32 *head, *tail, *pool; };
#define LIST_MAGIC 0x5453494Cu          /* "LIST" */
#define NODE(n) ((u32 *)(n) - 1)        /* the link word before a node */

static struct vlist *list_of(u32 h)
{
    struct vlist *l = (struct vlist *)h;
    if (!l || l->magic != LIST_MAGIC) panic("bad list handle %x", h);
    return l;
}

static void list_unlink(struct vlist *l, u32 node)
{
    u32 *p = NULL, *q;
    for (q = l->head; q && q != NODE(node); p = q, q = (u32 *)*q)
        ;
    if (!q) return;
    if (p) *p = *q; else l->head = (u32 *)*q;
    if (l->tail == q) l->tail = p;
}

static int list_service(u32 svc, struct svcregs *s)
{
    struct vlist *l;
    u32 *n;
    switch (svc) {
    case 0x9B:                          /* List_Create: EAX flags, ECX node size -> ESI */
        l = kmalloc(sizeof(*l));
        if (!l) { s->eflags |= EFL_CF; return 1; }
        l->magic = LIST_MAGIC;
        l->node_size = s->ecx;
        s->esi = s->eax = (u32)l;
        return 1;
    case 0x9C:                          /* List_Destroy */
        l = list_of(s->esi);
        while ((n = l->head) != NULL) { l->head = (u32 *)*n; kfree(n); }
        while ((n = l->pool) != NULL) { l->pool = (u32 *)*n; kfree(n); }
        l->magic = 0;
        kfree(l);
        return 1;
    case 0x9D:                          /* List_Allocate -> EAX */
        l = list_of(s->esi);
        if ((n = l->pool) != NULL) l->pool = (u32 *)*n;
        else n = l->node_size < 0x10000000u ? kmalloc(l->node_size + 4) : NULL;
        if (!n) { s->eflags |= EFL_CF; return 1; }
        *n = 0;
        s->eax = (u32)(n + 1);
        return 1;
    case 0x9E:                          /* List_Attach: at the head */
        l = list_of(s->esi);
        n = NODE(s->eax);
        *n = (u32)l->head;
        l->head = n;
        if (!l->tail) l->tail = n;
        return 1;
    case 0x9F:                          /* List_Attach_Tail */
        l = list_of(s->esi);
        n = NODE(s->eax);
        *n = 0;
        if (l->tail) *l->tail = (u32)n; else l->head = n;
        l->tail = n;
        return 1;
    case 0xA0:                          /* List_Insert: EAX after ECX (0: head) */
        l = list_of(s->esi);
        n = NODE(s->eax);
        if (!s->ecx) { *n = (u32)l->head; l->head = n; if (!l->tail) l->tail = n; return 1; }
        *n = *NODE(s->ecx);
        *NODE(s->ecx) = (u32)n;
        if (l->tail == NODE(s->ecx)) l->tail = n;
        return 1;
    case 0xA1:                          /* List_Remove */
        l = list_of(s->esi);
        list_unlink(l, s->eax);
        return 1;
    case 0xA2:                          /* List_Deallocate: back to the pool */
        l = list_of(s->esi);
        n = NODE(s->eax);
        *n = (u32)l->pool;
        l->pool = n;
        return 1;
    case 0xA3:                          /* List_Get_First -> EAX, ZF if empty */
        l = list_of(s->esi);
        s->eax = l->head ? (u32)(l->head + 1) : 0;
        ZF_SET(s, !s->eax);
        return 1;
    case 0xA4:                          /* List_Get_Next */
        if (!s->eax) { ZF_SET(s, 1); return 1; }        /* no node: none after it */
        n = (u32 *)*NODE(s->eax);
        s->eax = n ? (u32)(n + 1) : 0;
        ZF_SET(s, !s->eax);
        return 1;
    case 0xA5:                          /* List_Remove_First */
        l = list_of(s->esi);
        n = l->head;
        if (n) { l->head = (u32 *)*n; if (l->tail == n) l->tail = NULL; }
        s->eax = n ? (u32)(n + 1) : 0;
        ZF_SET(s, !s->eax);
        return 1;
    }
    return 0;
}

/* ---- nested execution (the DDK's Begin_Nest_V86_Exec, Begin_Nest_Exec,
 * Exec_Int, Resume_Exec, End_Nest_Exec, Save/Restore_Client_State): a
 * device runs code in the System VM with the client registers (EBP's
 * Client_Reg_Struc, w16's sys_client) and gets them back. w16 runs it
 * synchronously in V86 mode on the VMM's reflection stack (the client's
 * segment registers taken as V86 segments); a far call in a
 * Begin_Nest_Exec block of a VM with a protected-mode program runs in
 * protected mode, on the host stack. UNVERIFIED: Exec_Int in such a block
 * (w16 runs it in V86 mode) ---- */
static int nest_level;

static u16 nest_args[16];
static int nest_nargs, nest_call, nest_pm;
static u32 nest_sel, nest_off;
static int nest_service(u32 svc, struct svcregs *s)
{
    struct cregs v;
    struct client_regs *c = cur_vm->client;
    switch (svc) {
    case 0x82:                          /* Begin_Nest_V86_Exec */
        nest_pm = 0;
        nest_level++;
        return 1;
    case 0x83:                          /* Begin_Nest_Exec: protected mode */
        /* when the VM runs a protected-mode program, whatever mode it was
         * interrupted in (the DDK) */
        nest_pm = cur_vm == &sysvm && sysvm.pm;
        nest_level++;
        return 1;
    case 0x84: {                        /* Exec_Int: EAX the vector */
        /* a VM in protected mode: its DS and ES are selectors, which V86
         * code gets as the segments they map in conventional memory (else
         * the transfer buffer's), and keeps when the call leaves them */
        int pm = !(c->eflags & EFL_VM) && cur_vm == &sysvm && sysvm.pm;
        int ds = pm ? sel_to_seg(c->ds) : (int)c->ds, es = pm ? sel_to_seg(c->es) : (int)c->es;
        if (ds < 0) ds = info->xbuf_seg;
        if (es < 0) es = info->xbuf_seg;
        memset(&v, 0, sizeof(v));
        v.eax = c->eax; v.ebx = c->ebx; v.ecx = c->ecx; v.edx = c->edx;
        v.esi = c->esi; v.edi = c->edi; v.ebp = c->ebp;
        v.ds = ds; v.es = es;
        v.eflags = c->eflags;
        v86_int(&v, s->eax & 0xFF);
        c->eax = v.eax; c->ebx = v.ebx; c->ecx = v.ecx; c->edx = v.edx;
        c->esi = v.esi; c->edi = v.edi; c->ebp = v.ebp;
        if (!pm || (int)(v.ds & 0xFFFF) != ds) c->ds = (u16)(pm ? seg_to_sel(v.ds & 0xFFFF, 0xF2) : v.ds);
        if (!pm || (int)(v.es & 0xFFFF) != es) c->es = (u16)(pm ? seg_to_sel(v.es & 0xFFFF, 0xF2) : v.es);
        c->eflags = (c->eflags & ~0x8D5u) | (v.eflags & 0x8D5u);
        return 1;
    }
    case 0x4D:                          /* Simulate_Push: EAX (a word: 16-bit clients) */
        if (nest_nargs < (int)(sizeof(nest_args) / sizeof(nest_args[0])))
            nest_args[nest_nargs++] = (u16)s->eax;
        return 1;
    case 0x48:                          /* Simulate_Far_Call: CX:EDX */
        nest_sel = s->ecx & 0xFFFF;
        nest_off = s->edx;
        nest_call = 1;
        return 1;
    case 0x85:                          /* Resume_Exec: a far call simulated in
                                         * protected mode runs now, to its return
                                         * (VTDAPI.386's timer callbacks: Begin_Nest_Exec,
                                         * Simulate_Push, Simulate_Far_Call, Resume_Exec,
                                         * End_Nest_Exec) */
        /* also when the VM was in V86 mode (a DOS or BIOS call, an
         * interrupt reflected there): Begin_Nest_Exec's call is in
         * protected mode all the same, with the DS it last had there.
         * Before, it was dropped ("not done"): under Windows 3.1's
         * TIMER.DRV and VTDAPI.386 an MMSYSTEM timer event lost so, when
         * the tick came during a DOS call, stopped QuickTime's movie with
         * sound at "play" for good (sbaudio.sh qtw16e, ARCHITECTURE.md
         * 12.14) */
        if (nest_call && (nest_pm || !(c->eflags & EFL_VM)) && cur_vm == &sysvm && sysvm.pm) {
            struct cregs in, out;
            int v86 = (c->eflags & EFL_VM) != 0;
            memset(&in, 0, sizeof(in));
            in.eax = c->eax; in.ebx = c->ebx; in.ecx = c->ecx; in.edx = c->edx;
            in.esi = c->esi; in.edi = c->edi; in.ebp = c->ebp;
            in.ds = v86 ? sysvm.last_ds : c->ds;
            in.es = v86 ? sysvm.last_ds : c->es;
            hb_nest++;
            dpmi_pm_farcall_regs(&in, nest_sel, nest_off, nest_args, nest_nargs, &out);
            c->eax = out.eax; c->ebx = out.ebx; c->ecx = out.ecx; c->edx = out.edx;
            c->esi = out.esi; c->edi = out.edi;
        } else if (nest_call)
            trace("VMM: Resume_Exec of %x:%x in V86 mode not done\n", nest_sel, nest_off);
        nest_call = 0;
        nest_nargs = 0;
        return 1;
    case 0x86:                          /* End_Nest_Exec */
        if (nest_level) nest_level--;
        nest_pm = 0;
        nest_call = 0;
        nest_nargs = 0;
        return 1;
    case 0x8D:                          /* Save_Client_State: EDI a buffer */
        memcpy((void *)s->edi, c, sizeof(*c));
        return 1;
    case 0x8E:                          /* Restore_Client_State: ESI a buffer */
        memcpy(c, (void *)s->esi, sizeof(*c));
        return 1;
    }
    return 0;
}

/* ---- Hook_V86_Int_Chain (the DDK): procedures called for a software
 * interrupt in V86 mode, the last installed first, with EAX the vector,
 * EBX the VM, EBP its client registers; carry clear: serviced, no
 * reflection. (Simulated hardware interrupts do not go through them here:
 * UNVERIFIED that any device w16 runs needs it) ---- */
struct v86hook { struct v86hook *next; u32 vec, proc; };
static struct v86hook *v86hooks;

int vxd_v86_int_hooks(struct cregs *r, int vec)
{
    struct v86hook *h;
    struct client_regs *c, outer;
    struct svcregs s;
    int done = 0;
    for (h = v86hooks; h && h->vec != (u32)vec; h = h->next)
        ;
    if (!h) return 0;
    c = cur_vm->client;
    outer = *c;
    client_fill(c, r);
    for (; h && !done; h = h->next) {
        if (h->vec != (u32)vec) continue;
        memset(&s, 0, sizeof(s));
        s.eax = vec;
        s.ebx = VMH(cur_vm);
        s.ebp = (u32)c;
        vmm_call_proc(h->proc, &s);
        done = !(s.eflags & EFL_CF);
    }
    client_store(r, c);
    *c = outer;
    return done;
}

static int v86hook_service(u32 svc, struct svcregs *s)
{
    struct v86hook *h;
    if (svc != 0x41) return 0;                  /* Hook_V86_Int_Chain: EAX, ESI */
    if (s->eax > 0xFF || !s->esi || (h = kmalloc(sizeof(*h))) == NULL) { s->eflags |= EFL_CF; return 1; }
    h->vec = s->eax;
    h->proc = s->esi;
    h->next = v86hooks;
    v86hooks = h;
    return 1;
}

/* ---- the profile services (SYSTEM.INI): Get_Profile_String (B3h),
 * Get_Next_Profile_String (B4h), Get_Profile_Decimal_Int (ABh),
 * Get_Profile_Hex_Int (AFh), Get_Profile_Boolean (B1h), from the DDK's
 * descriptions. The loader's copy of the file is copied once, each line
 * ended with a NUL, so that a value is a string the caller can keep; the
 * section NULL is [386Enh]; keys and sections ignore case; the first
 * entry of the key counts (Get_Next_Profile_String: the next) ---- */
static char *prof;
static u32 prof_len;

static int lc(int c) { return c >= 'A' && c <= 'Z' ? c + 32 : c; }
static int prof_eq(const char *a, const char *b, u32 n)
{
    while (n--) if (lc(*a++) != lc(*b++)) return 0;
    return 1;
}
static u32 slen(const char *s) { u32 n = 0; while (s[n]) n++; return n; }

static void prof_init(void)
{
    u32 i;
    if (prof || !info->ini_lin || !info->ini_len) return;
    prof = kmalloc(info->ini_len + 2);
    if (!prof) return;
    memcpy(prof, (void *)info->ini_lin, info->ini_len);
    prof_len = info->ini_len;
    prof[prof_len] = prof[prof_len + 1] = 0;
    for (i = 0; i < prof_len; i++) if (prof[i] == '\r' || prof[i] == '\n' || prof[i] == 0x1A) prof[i] = 0;
}

/* the value of the first entry key in section sec after position from
 * (0: the start), or NULL */
static char *prof_find(const char *sec, const char *key, char *from)
{
    u32 i = 0, kn = slen(key), sn = sec ? slen(sec) : 6;
    int in = 0;
    prof_init();
    if (!prof) return NULL;
    if (!sec) sec = "386Enh";
    while (i < prof_len) {
        char *l = prof + i, *p = l;
        u32 n = slen(l);
        i += n + 1;
        while (*p == ' ' || *p == '\t') p++;
        if (*p == '[') {
            in = prof_eq(p + 1, sec, sn) && p[1 + sn] == ']';
            continue;
        }
        if (!in || !prof_eq(p, key, kn)) continue;
        p += kn;
        while (*p == ' ' || *p == '\t') p++;
        if (*p != '=') continue;
        p++;
        while (*p == ' ' || *p == '\t') p++;
        if (from && p <= from) continue;
        return p;
    }
    return NULL;
}

static int prof_service(u32 svc, struct svcregs *s)
{
    char *v;
    u32 n = 0;
    int any = 0, ok = 1;
    switch (svc) {
    case 0xB3:                          /* Get_Profile_String: EDX default, ESI, EDI */
        v = prof_find((const char *)s->esi, (const char *)s->edi, NULL);
        if (v) s->edx = (u32)v; else s->eflags |= EFL_CF;
        return 1;
    case 0xB4:                          /* Get_Next_Profile_String: EDX the last, EDI */
        v = prof_find(NULL, (const char *)s->edi, (char *)s->edx);
        if (v) s->edx = (u32)v; else s->eflags |= EFL_CF;
        return 1;
    case 0xAB:                          /* Get_Profile_Decimal_Int: EAX default */
    case 0xAF:                          /* Get_Profile_Hex_Int */
    case 0xB1:                          /* Get_Profile_Boolean */
        v = prof_find((const char *)s->esi, (const char *)s->edi, NULL);
        s->eflags &= ~EFL_ZF;
        if (!v) { s->eflags |= EFL_CF; return 1; }
        if (!*v) { s->eflags |= EFL_ZF; return 1; }   /* there, no value */
        if (svc == 0xB1) {
            if (prof_eq(v, "yes", 3) || prof_eq(v, "on", 2) || prof_eq(v, "true", 4) || *v == '1') s->eax = ~0u;
            else if (prof_eq(v, "no", 2) || prof_eq(v, "off", 3) || prof_eq(v, "false", 5) || *v == '0') s->eax = 0;
            else s->eflags |= EFL_CF;
            return 1;
        }
        for (; *v; v++) {
            int d = *v >= '0' && *v <= '9' ? *v - '0'
                  : svc == 0xAF && lc(*v) >= 'a' && lc(*v) <= 'f' ? lc(*v) - 'a' + 10 : -1;
            if (d < 0) { if (*v != ' ' && *v != '\t' && *v != ';' && !(svc == 0xAF && lc(*v) == 'h')) ok = 0; break; }
            n = n * (svc == 0xAF ? 16 : 10) + d;
            any = 1;
        }
        if (any && ok) s->eax = n; else s->eflags |= EFL_CF;
        return 1;
    }
    return 0;
}

/* ---- the VMM's services ---- */
void __cdecl svc_dispatch(struct svcregs *s)
{
    u32 dev = s->id >> 16, svc = s->id & 0xFFFF;

    s->eflags &= ~EFL_CF;
    if (dev == 1 && vm_service(svc, s)) return;
    if (dev == 1) switch (svc) {
    case 0x0000:                        /* Get_VMM_Version: 3.10, debug revision 0 */
        s->eax = (s->eax & 0xFFFF0000u) | 0x030A;
        s->ecx = 0;
        return;
    case 0x001C: {                      /* Map_Flat: AH, AL offsets in Client_Reg_Struc (AL FFh: offset 0) */
        u8 *c = (u8 *)cur_vm->client;
        u32 seg = *(u16 *)(c + ((s->eax >> 8) & 0xFF));
        u32 off = (s->eax & 0xFF) == 0xFF ? 0 : *(u32 *)(c + (s->eax & 0xFF));
        if (cur_vm->client->eflags & EFL_VM) s->eax = ((seg & 0xFFFF) << 4) + (off & 0xFFFF);
        else if (!sel_present(seg)) s->eax = 0xFFFFFFFFu;  /* the DDK: -1 for an invalid selector */
        else s->eax = sel_base(seg) + (sysvm.client32 ? off : (off & 0xFFFF));
        return;
    }
    case 0x0093: {                      /* System_Control: EAX the message, EBX the VM */
        struct vm *v = vm_of(s->ebx);
        if (!v) v = cur_vm;
        if (vxd_send(s->eax, v, s->edx, s->esi, s->edi)) s->eflags |= EFL_CF;
        return;
    }
    case 0x0090: {                      /* Hook_Device_Service: EAX the service, ESI the hook */
        /* the DDK: the hook replaces the service in its table, ESI back
         * the procedure it replaced (the hook chains to it); carry for a
         * service that is not there. The calls already linked go through
         * the table's slot, so they reach the hook too */
        u32 hdev = s->eax >> 16, hsvc = s->eax & 0xFFFF, *slot = NULL, old;
        struct ddb *d;
        if (hdev == 1 && hsvc < VMM_SERVICES) slot = &vmm_slots[hsvc];
        else if (hdev != 1 && (d = find_dev(hdev)) != NULL && hsvc < d->svc_count && d->svc_table)
            slot = (u32 *)(d->svc_table + hsvc * 4);
        if (!slot || !*slot || !s->esi) { s->eflags |= EFL_CF; return; }
        old = *slot;
        *slot = s->esi;
        s->esi = old;
        return;
    }
    case 0x004C:                        /* Build_Int_Stack_Frame: CX:EDX the routine */
        if (!vmm_build_int(s->ecx & 0xFFFF, s->edx)) s->eflags |= EFL_CF;
        trace("VMM: Build_Int_Stack_Frame %x:%x%s\n", s->ecx & 0xFFFF, s->edx, (s->eflags & EFL_CF) ? " (not in PM)" : "");
        return;
    }
    if (dev == 1 && list_service(svc, s)) return;
    if (dev == 1 && prof_service(svc, s)) return;
    if (dev == 1 && io_service(svc, s)) return;
    if (dev == 1 && tmo_service(svc, s)) return;
    if (dev == 1 && nest_service(svc, s)) return;
    if (dev == 1 && v86hook_service(svc, s)) return;
    if (dev == 5) { vtd_service(svc, s); return; }
    if (dev == 0x06) { v86mm_service(svc, s); return; }
    if (dev == 0x17) {
        if (!shell_service(svc, s)) {
            trace("VMM: SHELL service %x is not implemented (called from %x)\n", svc, *(u32 *)(s + 1));
            s->eflags |= EFL_CF;
        }
        return;
    }
    if (dev == 0x0A) {
        /* (the rest of the VDD's services: carry, traced, UNVERIFIED) */
        if (!vdd_service(svc, s)) {
            trace("VMM: VDD service %x is not implemented (called from %x)\n", svc, *(u32 *)(s + 1));
            s->eflags |= EFL_CF;
        }
        return;
    }
    panic("VxD service %x of device %x is not implemented yet (called from %x)",
          svc, dev, *(u32 *)(s + 1));
}
