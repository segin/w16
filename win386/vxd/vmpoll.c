/* vmpoll.c - w16's VMPOLL, the idle-detection device (device 0018h), a
 * module of WIN386.EXE's W3 table (ARCHITECTURE.md 12.12).
 *
 * A DOS program waiting for a key often polls the keyboard (INT 16h
 * AH=01h, "is there a key?") in a loop; each such VM would take its whole
 * time slice from Windows and the other VMs. VMPOLL sees the polls (a V86
 * hook of INT 16h) and, when a VM has done nothing but poll for a while,
 * releases its time slice (Release_Time_Slice) and keeps releasing it at
 * each poll until it has a key or its screen changes.
 *
 * Observed under Windows 3.1 (VXT.386, tests/vxd/vxt.asm; VX13/VX14,
 * tests/vmpollrun.sh; DOSBox-X at a fixed 30,000 cycles, windowed DOS VMs
 * with Background on, Windows with the focus):
 *   - VMPoll_Get_Version: EAX = 030Ah, carry clear.
 *   - SHELL calls VMPoll_Get_Version and VMPoll_Enable_Disable (AL 1 when
 *     the PIF's Detect Idle Time is on, AL 0 when off, EBX the VM) for
 *     each new VM while it handles VM_Init.
 *   - A VM polling INT 16h AH=01h in a tight loop (VX14 P), polling with
 *     INT 2Fh 1680h between (R) or with INT 21h AH=0Bh (D): released after
 *     3,546 polls (21 ms) the first time, then it runs once every ~7 s
 *     (Wake_Up_VM, not from VMPOLL's code), polls once and is released
 *     again; its control block shows VMStat_Idle. Windows kept 99.5, 98
 *     and 96 per cent of its speed with 1, 2 and 4 such boxes.
 *   - The same polling with Detect Idle Time off, a VM waiting inside the
 *     BIOS's INT 16h AH=00h (DOSBox's BIOS loops there without a trap: W),
 *     COMMAND.COM at DOSBox-X's prompt (12 polls and INT 28h a second),
 *     and a VM polling with a delay of 4,000 loop iterations between polls
 *     (Q) are never released: each gets its full share (Windows at 100/50
 *     priorities: 67, 49, 33 per cent with 1, 2, 4 boxes).
 *   - A VM that polls and writes to its screen at each poll (V) is
 *     released too, but woken again within 5 ms, and released after half
 *     as many polls (1,773) each time.
 * w16's rule, an approximation (UNVERIFIED how 3.1 counts; it may
 * calibrate its count to the processor's speed): a poll that finds no
 * key and comes within TIGHT_GAP (about 250 us) of the VM's previous one
 * adds the gap to the VM's polling time, a later one halves it (the VM
 * did something else between); a switch to another VM in between counts
 * neither. When the polling time reaches DETECT (20 ms) the VM is idle:
 * its time slice is released at once and at every poll after, until a
 * poll finds a key, the VM reads one (AH=00h/10h), its screen changes
 * (VDD_Get_ModTime; then it keeps half its polling time, as 3.1's V), or
 * VMPoll_Reset_Detection. The ~7 s wake is the VMM's (vm.c).
 * Sources: the DDK's Virtual Device Adaptation Guide (VMPoll_Get_Version,
 * VMPoll_Enable_Disable, VMPoll_Reset_Detection, VDD_Get_ModTime,
 * Release_Time_Slice, VTD_Get_Real_Time, Hook_V86_Int_Chain); the BIOS
 * keyboard buffer's head and tail at 40:1Ah and 40:1Ch (IBM's published
 * BIOS data area).
 */
#include "w16vxd.h"

vu32 __cdecl vmm_alloc_cb_area(vu32 size, vu32 flags);
void __cdecl vmm_hook_v86_int(struct vregs *r);
void __cdecl vmm_release_time_slice(struct vregs *r);
void __cdecl vmm_get_sys_vm(struct vregs *r);
void __cdecl vmm_call_when_task_switched(struct vregs *r);
void __cdecl vmm_get_next_vm(struct vregs *r);
void __cdecl vtd_get_real_time(struct vregs *r);
void __cdecl vdd_get_modtime(struct vregs *r);
extern char vmpoll_i16[], vmpoll_ts[];
#pragma aux vmpoll_i16 "*"
#pragma aux vmpoll_ts "*"

#define TIGHT_GAP   300                 /* VTD real-time units (0.838 us): ~250 us */
#define DETECT      23864               /* 20 ms */

struct pvm {
    vu32 enabled;                       /* VMPoll_Enable_Disable for this VM */
    vu32 idle;                          /* found idle: released at each poll */
    vu32 have_last, last;               /* the time of its previous poll */
    vu32 switched;                      /* another VM ran since */
    vu32 polling;                       /* tight polling so far (real-time units) */
    vu32 idle_mod;                      /* VDD_Get_ModTime when it became idle */
};
static vu32 cb_off, sys_vm, global_on = 1;
#define PV(vm) ((struct pvm *)((vm) + cb_off))

static void vregs_clear(struct vregs *r)
{
    vu32 *p = (vu32 *)r, i;
    for (i = 0; i < sizeof(*r) / 4; i++) p[i] = 0;
}

static vu32 now(void)
{
    struct vregs r;
    vregs_clear(&r);
    vtd_get_real_time(&r);
    return r.eax;
}

static vu32 modtime(vu32 vm)
{
    struct vregs r;
    vregs_clear(&r);
    r.ebx = vm;
    vdd_get_modtime(&r);
    return r.eax;
}

static void release(void)
{
    struct vregs r;
    vregs_clear(&r);
    vmm_release_time_slice(&r);
}

static void reset(struct pvm *p)
{
    p->idle = 0;
    p->polling = 0;
    p->have_last = 0;
}

/* a poll that found no key */
static void poll(vu32 vm, struct pvm *p)
{
    vu32 t = now();
    if (p->idle) {
        if (modtime(vm) == p->idle_mod) { release(); return; }
        p->idle = 0;                    /* its screen changed: half its time kept */
        p->polling = DETECT / 2;
        p->have_last = 0;
    }
    if (p->have_last && !p->switched) {
        if (t - p->last <= TIGHT_GAP) p->polling += t - p->last;
        else p->polling /= 2;
    }
    p->switched = 0;
    p->have_last = 1;
    p->last = t;
    if (p->polling >= DETECT) {
        p->idle = 1;
        p->idle_mod = modtime(vm);
        release();
    }
}

/* Hook_V86_Int_Chain's procedure for INT 16h: EBX the VM, EBP its client
 * registers; passed on (carry) always */
void __cdecl vmpoll_int16(struct vregs *r)
{
    struct client_regs *c = (struct client_regs *)r->ebp;
    vu32 vm = r->ebx, ah = (c->eax >> 8) & 0xFF;
    struct pvm *p;
    int key;
    r->eflags |= VF_CF;
    if (vm == sys_vm || !cb_off) return;
    p = PV(vm);
    /* the VM's own BIOS data (it is the current VM): a key waits when the
     * buffer's head and tail differ */
    key = *(volatile vu16 *)0x41A != *(volatile vu16 *)0x41C;
    if (ah == 0x01 || ah == 0x11) {
        if (key) reset(p);
        else if (p->enabled && global_on) poll(vm, p);
    } else if (ah == 0x00 || ah == 0x10) {
        if (key) reset(p);
    }
}

/* Call_When_Task_Switched: EAX the VM that ran, EBX the next */
void __cdecl vmpoll_switched(struct vregs *r)
{
    if (cb_off && r->eax && r->eax != sys_vm) PV(r->eax)->switched = 1;
}

void __cdecl vmpoll_service(struct vregs *r)
{
    struct pvm *p;
    switch (r->arg) {
    case 0x00:                          /* VMPoll_Get_Version: EAX 030Ah */
        r->eax = 0x030A;
        r->eflags &= ~VF_CF;
        return;
    case 0x01:                          /* VMPoll_Enable_Disable: AL, EBX (0: all VMs) */
        if (!r->ebx) { global_on = (r->eax & 0xFF) != 0; return; }
        if (!cb_off) return;
        p = PV(r->ebx);
        p->enabled = (r->eax & 0xFF) != 0;
        if (!p->enabled) reset(p);
        return;
    case 0x02:                          /* VMPoll_Reset_Detection: EBX */
        if (cb_off && r->ebx) reset(PV(r->ebx));
        return;
    }
}


void __cdecl vmpoll_control(struct vregs *r)
{
    struct vregs s;
    struct pvm *p;
    r->eflags &= ~VF_CF;
    switch (r->eax) {
    case SYS_CRITICAL_INIT:
        sys_vm = r->ebx;
        cb_off = vmm_alloc_cb_area(sizeof(struct pvm), 0);
        return;
    case DEVICE_INIT:
        vregs_clear(&s);
        s.eax = 0x16;
        s.esi = (vu32)vmpoll_i16;
        vmm_hook_v86_int(&s);
        vregs_clear(&s);
        s.esi = (vu32)vmpoll_ts;
        vmm_call_when_task_switched(&s);
        return;
    case CREATE_VM:
        /* detection on until SHELL says what the PIF says (UNVERIFIED
         * Windows 3.1's default for a VM SHELL does not set) */
        if (!cb_off) return;
        p = PV(r->ebx);
        reset(p);
        p->switched = 0;
        p->enabled = 1;
        return;
    }
}
