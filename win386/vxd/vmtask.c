/* vmtask.c - VMTASK.386, w16's task-manager device (an external VxD:
 * SYSTEM.INI [386Enh] device=vmtask.386), the ring-0 half of the VM Task
 * Manager (VMTASK.EXE, shell/vmtask; ARCHITECTURE.md 15.12).
 *
 * A w16 extension: Windows 3.1 has nothing like it. It changes nothing
 * that anything else sees; it only watches. It counts, per virtual
 * machine, the time the VM has run (a Call_When_Task_Switched callback
 * and Get_System_Time), and the time the System VM spends idle (a
 * Call_When_Idle callback: the VMM calls it each time KERNEL's idle loop
 * says Windows has nothing to do, INT 2Fh AX=1689h, while every other VM
 * is idle too), and hands a snapshot of every VM to a program through
 * its API. It uses only services the Windows 3.1 DDK documents, and runs
 * under Microsoft's WIN386 as under w16's.
 *
 * The device ID, 5716h ("W" 57h, 16), is w16's own choice: the DDK has
 * new devices ask Microsoft for an ID, and w16 has none assigned. It is
 * clear of every ID of Windows 3.1's and WfW 3.11's devices (0001h-0027h,
 * 0442h, 0483h-048Bh; docs/win386-vxds.md) and of the block 0400h-05FFh
 * where Microsoft's assignments to others are known to lie (VSBD 0445h).
 * UNVERIFIED that no third-party VxD uses it: so the API's function 0
 * answers with a signature ("VMTK" in BX:CX) that a client checks.
 *
 * The API (V86 and protected mode, one procedure; INT 2Fh AX=1684h
 * BX=5716h gives its entry; the function in the client's AX; carry set
 * for an unknown function):
 *   0  version: AX 0100h (1.00), BX 564Dh and CX 544Bh ("VM", "TK"),
 *      DX the capabilities: bit 0 the VM list, bit 1 the idle time, bit 2
 *      SHELL_GetVMInfo's flags, bit 3 a V86 API as well.
 *   1  snapshot: ES:DI a buffer, CX its size in bytes. Filled with a
 *      header (struct vmt_head, 32 bytes) and one record per VM (struct
 *      vmt_vm, 48 bytes), the System VM first, then Get_Next_VM_Handle's
 *      order. Back: AX the number of VMs, CX the bytes written; carry set
 *      if not every VM's record fitted (the header always does, if CX is
 *      at least 32).
 *   2  the System VM's busy time: DX:AX the milliseconds it has run less
 *      those it was idle, CX:BX Get_System_Time (VMTASK.EXE asks at each
 *      Windows task switch, through TOOLHELP's NFY_TASKIN/NFY_TASKOUT, so
 *      that a task's time excludes idling and other VMs' slices).
 *   4  the loaded devices: ES:DI a buffer, CX its size; a word (the
 *      count) and 24 bytes each (name, ID, version, init order, service
 *      count, flags), the VMM first. See devices.
 *   (function 1's records are 64 bytes since 1.00's capability bit 5:
 *      the memory, _GetVMPgCount and V86MMGR_Get_EMS_XMS_Limits; a client
 *      steps by the header's record size)
 *   3  end a VM: DX:BX its handle; CX 0 Close_VM (SI the time-out in ms,
 *      0: 5 s, after which the VMM ends it by force), 1 Nuke_VM. Never
 *      the System VM: AX 1, carry set; AX 2 for a handle that is not a VM
 *      (Validate_VM_Handle). See end_vm.
 * The header and record layouts are VMTASK's own (shell/vmtask/vmtask.h
 * has the same).
 *
 * How the times are counted:
 *   - run time: at each task switch (EAX the VM switched from, EBX the
 *     one switched to, the DDK's Call_When_Task_Switched), the VM
 *     switched from gets the time since it was switched to, by
 *     VTD_Get_Real_Time (see add_span: not Get_System_Time). The VMM's own Get_VM_Exec_Time is only the current VM's: each
 *     record gives it as well for the VM current at the call (the System
 *     VM, when the call comes from Windows), FFFFFFFFh for the others.
 *   - idle time: each idle callback adds the time since the previous one
 *     when that was at most IDLE_GAP ms ago and no task switch came
 *     between: KERNEL's idle loop calls 1689h thousands of times a
 *     second (Windows 3.1: 15,600, ARCHITECTURE.md 12.15), so a run of
 *     callbacks covers the time Windows idled; a gap longer than that, or
 *     another VM run between, is work and is not counted. With a busy VM
 *     beside it Windows' 1689h gives up its slice instead (no callback):
 *     the processor is not idle then, and nothing is counted.
 *   - status: CB_VM_Status as it is, the priorities and share from
 *     Get_Time_Slice_Priority, the PIF's flags from SHELL_GetVMInfo
 *     (bit 2, 04h, windowed: observed, tests/vmtaskrun.sh).
 * Sources: the DDK's Virtual Device Adaptation Guide (.tools/docs/vdag31:
 * Call_When_Task_Switched, Call_When_Idle, Get_System_Time,
 * VTD_Get_Real_Time,
 * Get_VM_Exec_Time, Get_Sys_VM_Handle, Get_Next_VM_Handle,
 * Get_Cur_VM_Handle, Get_Execution_Focus, Get_Time_Slice_Priority,
 * Get_Time_Slice_Granularity, Map_Flat, Allocate_Device_CB_Area, the
 * control block's CB_VM_Status and CB_VMID, SHELL_GetVMInfo,
 * Declare_Virtual_Device on device IDs) and the DDAG's INT 2Fh 1684h.
 */
#include "w16vxd.h"

void __cdecl vmm_get_cur_vm(struct vregs *r);
void __cdecl vmm_get_sys_vm(struct vregs *r);
void __cdecl vmm_map_flat(struct vregs *r);
void __cdecl vmm_call_when_task_switched(struct vregs *r);
void __cdecl vmm_get_focus(struct vregs *r);
void __cdecl vmm_get_ts_priority(struct vregs *r);
void __cdecl vmm_get_ts_granularity(struct vregs *r);
void __cdecl vmm_call_when_idle(struct vregs *r);
void __cdecl vmm_get_next_vm(struct vregs *r);
void __cdecl vmm_get_system_time(struct vregs *r);
void __cdecl vmm_get_vm_exec_time(struct vregs *r);
vu32 __cdecl vmm_alloc_cb_area(vu32 size, vu32 flags);
void __cdecl shell_get_vm_info(struct vregs *r);
void __cdecl vtd_get_real_time(struct vregs *r);
void __cdecl vmm_validate_vm(struct vregs *r);
void __cdecl vmm_nuke_vm(struct vregs *r);
void __cdecl vmm_close_vm(struct vregs *r);
void __cdecl vmm_get_version(struct vregs *r);
void __cdecl v86mm_get_limits(struct vregs *r);
vu32 __cdecl vmm_vm_pgcount(vu32 vm, vu32 *outside);
extern struct vxd_ddb VMTASK_DDB;
#pragma aux VMTASK_DDB "*"
extern char vmtask_ts[], vmtask_idle[];
#pragma aux vmtask_ts "*"
#pragma aux vmtask_idle "*"

/* VTD_Get_Real_Time counts the 8254's input clock, 1,193,182 Hz (the DDK
 * says "0.8 microseconds"); 1193 a millisecond is 0.015 per cent off */
#define RT_PER_MS   1193
#define IDLE_GAP    (2 * RT_PER_MS)     /* callbacks closer than 2 ms are one idle run */
#define MAX_VMS     64                  /* a snapshot's records at most */
#define CAPS        0x007F             /* bit 4: function 3, end a VM; 5 memory; 6 function 4 */
#define MAX_DEVS    96                  /* function 4's records at most */

#pragma pack(1)
struct vmt_head {
    vu16 head_size, rec_size;           /* 32, 48 */
    vu16 vm_count, rec_count;           /* VMs there are, records written */
    vu32 sys_time;                      /* Get_System_Time */
    vu32 idle_ms;                       /* the System VM's idle time */
    vu32 idle_calls;                    /* idle callbacks */
    vu32 sys_vm, focus_vm;              /* Get_Sys_VM_Handle, Get_Execution_Focus */
    vu32 granularity;                   /* Get_Time_Slice_Granularity, ms */
};
struct vmt_vm {
    vu32 handle, id;                    /* the VM's handle, CB_VMID */
    vu32 status;                        /* CB_VM_Status */
    vu32 run_ms;                        /* its run time (task switches) */
    vu32 exec_ms;                       /* Get_VM_Exec_Time: the current VM's; else FFFFFFFFh */
    vu32 ts_flags;                      /* Get_Time_Slice_Priority: EAX */
    vu32 fg_prio, bg_prio;              /* ECX, EDX */
    vu32 cpu_pct;                       /* ESI: its share of the processor */
    vu32 shell_flags;                   /* SHELL_GetVMInfo (FFFFFFFFh: none) */
    vu32 created;                       /* Get_System_Time at Create_VM (0: at start) */
    vu32 runs;                          /* how often it was switched to */
    vu32 pages, pages_out;              /* _GetVMPgCount: EAX, EDX (4 KB pages) */
    vu32 ems_max, xms_max;              /* V86MMGR_Get_EMS_XMS_Limits: EDX, EDI (KB) */
};
/* function 4's records */
struct vmt_dev {
    char name[8];
    vu16 id;                            /* DDB_Req_Device_Number */
    vu8 major, minor;
    vu32 init_order;
    vu32 services;                      /* DDB_Service_Table_Size */
    vu32 flags;                         /* 1 V86 API, 2 PM API, 4 a service table, 80h the VMM (no DDB here) */
};
#pragma pack()

/* VTD_Get_Real_Time's 64-bit count */
struct rt { vu32 lo, hi; };

/* each VM's counts, in its control block (Allocate_Device_CB_Area) */
struct tvm {
    vu32 run_ms, run_rem;               /* run time until it was last switched from */
    struct rt since;                    /* when it was last switched to */
    vu32 created;                       /* Get_System_Time at Create_VM */
    vu32 runs;
};
/* (initialised data: an LE data object with no page of its own, all
 * BSS, stopped Windows 3.1's WIN386 at the start: a black screen, no
 * KERNEL; with one initialised word it loads) */
static vu32 caps = CAPS;
static vu32 cb_off, sys_vm, cur;        /* cur: the VM switched to last */
static vu32 idle_ms, idle_rem, idle_calls, idle_run;
static struct rt idle_last;
#define TV(vm) ((struct tvm *)((vm) + cb_off))
#define CB_STATUS(vm) (*(vu32 *)(vm))

/* EDX:EAX / ECX, the quotient (EDX below ECX) */
static vu32 udiv64(vu32 hi, vu32 lo, vu32 d);
#pragma aux udiv64 = "div ecx" parm [edx] [eax] [ecx] value [eax] modify exact [eax edx];

static void clr(struct vregs *r)
{
    vu32 *p = (vu32 *)r, i;
    for (i = 0; i < sizeof(*r) / 4; i++) p[i] = 0;
}

static vu32 now(void)
{
    struct vregs s;
    clr(&s);
    vmm_get_system_time(&s);
    return s.eax;
}

static void rt_now(struct rt *t)
{
    struct vregs s;
    clr(&s);
    vtd_get_real_time(&s);
    t->lo = s.eax;
    t->hi = s.edx;
}

/* the time from a to b (and *rem, the counts below a millisecond left
 * over before) into *ms; *rem gets what is left over now. Measured with
 * the real-time clock: Windows 3.1's Get_System_Time stood still through
 * the System VM's short runs beside a busy box (a tenth of a millisecond
 * each, just after a timer tick: 115 runs in 6 s counted 0 ms) */
static void add_span(const struct rt *a, const struct rt *b, vu32 *ms, vu32 *rem)
{
    vu32 lo = b->lo - a->lo, hi = b->hi - a->hi - (b->lo < a->lo), q;
    if ((int)hi < 0) return;            /* (never backwards) */
    lo += *rem;
    if (lo < *rem) hi++;
    if (hi >= RT_PER_MS) { *ms += 0xFFFFFFFFu; *rem = 0; return; }   /* (49 days) */
    q = udiv64(hi, lo, RT_PER_MS);
    *ms += q;
    *rem = lo - q * RT_PER_MS;
}

static void vm_reset(vu32 vm, vu32 t)
{
    struct tvm *v = TV(vm);
    v->run_ms = v->run_rem = 0;
    rt_now(&v->since);
    v->created = t;
    v->runs = 0;
}

/* Call_When_Task_Switched: EAX the VM switched from, EBX the one to */
void __cdecl vmtask_on_switch(struct vregs *r)
{
    struct rt t;
    rt_now(&t);
    if (cb_off) {
        if (r->eax) add_span(&TV(r->eax)->since, &t, &TV(r->eax)->run_ms, &TV(r->eax)->run_rem);
        if (r->ebx) {
            TV(r->ebx)->since = t;
            TV(r->ebx)->runs++;
        }
    }
    cur = r->ebx;
    idle_run = 0;                       /* another VM ran: an idle run ends */
}

/* Call_When_Idle: Windows and every other VM idle. Carry set: the next
 * callback is called too (the DDK) */
void __cdecl vmtask_on_idle(struct vregs *r)
{
    struct rt t;
    rt_now(&t);
    idle_calls++;
    if (idle_run && t.hi - idle_last.hi - (t.lo < idle_last.lo) == 0 && t.lo - idle_last.lo <= IDLE_GAP)
        add_span(&idle_last, &t, &idle_ms, &idle_rem);
    idle_last = t;
    idle_run = 1;
    r->eflags |= VF_CF;
}

/* vm's run time so far, with its current run if it is running */
static vu32 run_of(vu32 vm, const struct rt *t)
{
    struct tvm *v = TV(vm);
    vu32 ms = v->run_ms, rem = v->run_rem;
    if (vm == cur) add_span(&v->since, t, &ms, &rem);
    return ms;
}

/* the System VM's run time so far less its idle time */
static vu32 sys_busy(void)
{
    struct rt t;
    if (!sys_vm || !cb_off) return 0;
    rt_now(&t);
    return run_of(sys_vm, &t) - idle_ms;
}

static void record(struct vmt_vm *o, vu32 vm, const struct rt *t, vu32 curvm)
{
    struct vregs s;
    struct tvm *v = TV(vm);
    o->handle = vm;
    o->id = CB_VMID(vm);
    o->status = CB_STATUS(vm);
    o->run_ms = run_of(vm, t);
    o->created = v->created;
    o->runs = v->runs;
    o->exec_ms = 0xFFFFFFFFu;
    if (vm == curvm) {
        clr(&s);
        vmm_get_vm_exec_time(&s);
        o->exec_ms = s.eax;
    }
    clr(&s);
    s.ebx = vm;
    vmm_get_ts_priority(&s);
    o->ts_flags = s.eax;
    o->fg_prio = s.ecx & 0xFFFF;
    o->bg_prio = s.edx & 0xFFFF;
    o->cpu_pct = s.esi;
    o->pages = vmm_vm_pgcount(vm, &o->pages_out);
    clr(&s);
    s.ebx = vm;
    v86mm_get_limits(&s);
    o->ems_max = s.edx;
    o->xms_max = s.edi;
    o->shell_flags = 0xFFFFFFFFu;
    if (vm != sys_vm) {
        clr(&s);
        s.ebx = vm;
        shell_get_vm_info(&s);
        if (!(s.eflags & VF_CF)) o->shell_flags = s.eax;
    }
}

/* function 1: the snapshot into the client's ES:DI, CX bytes */
static void snapshot(struct client_regs *c)
{
    struct vregs s;
    struct vmt_head *h;
    vu8 *p;
    vu32 size = c->ecx & 0xFFFF, vm, curvm, n = 0, w = 0;
    struct rt t;
    clr(&s);
    s.eax = (0x38 << 8) | 0x00;         /* Client_ES : Client_DI */
    vmm_map_flat(&s);
    if (s.eax == 0xFFFFFFFFu || size < sizeof(struct vmt_head)) {
        c->eflags |= VF_CF;
        return;
    }
    h = (struct vmt_head *)s.eax;
    p = (vu8 *)(h + 1);
    rt_now(&t);
    clr(&s);
    vmm_get_cur_vm(&s);
    curvm = s.ebx;
    h->head_size = sizeof(struct vmt_head);
    h->rec_size = sizeof(struct vmt_vm);
    h->sys_time = now();
    h->idle_ms = idle_ms;
    h->idle_calls = idle_calls;
    h->sys_vm = sys_vm;
    clr(&s);
    vmm_get_focus(&s);
    h->focus_vm = s.ebx;
    clr(&s);
    vmm_get_ts_granularity(&s);
    h->granularity = s.eax;
    vm = sys_vm;
    do {
        if (sizeof(struct vmt_head) + (w + 1) * sizeof(struct vmt_vm) <= size) {
            record((struct vmt_vm *)p, vm, &t, curvm);
            p += sizeof(struct vmt_vm);
            w++;
        }
        n++;
        clr(&s);
        s.ebx = vm;
        vmm_get_next_vm(&s);
        vm = s.ebx;
    } while (vm && vm != sys_vm && n < MAX_VMS);
    h->vm_count = (vu16)n;
    h->rec_count = (vu16)w;
    c->eax = (c->eax & 0xFFFF0000u) | n;
    c->ecx = (c->ecx & 0xFFFF0000u) | (sizeof(struct vmt_head) + w * sizeof(struct vmt_vm));
    if (w < n) c->eflags |= VF_CF; else c->eflags &= ~VF_CF;
}

/* function 4: the loaded devices into ES:DI (CX bytes): a word, the
 * count, then a struct vmt_dev each: the VMM first (it has no DDB a
 * device can find: Get_VMM_Version's version, init order 0), then this
 * device and each after it by DDB_Next, which leads through the devices
 * in their init order (Windows 3.1 sets it so: VXB.386, ARCHITECTURE.md
 * 24.6); VMTASK's init order 0 puts every other device after it. AX the
 * count, CX the bytes; carry if not all fitted */
static void devices(struct client_regs *c)
{
    struct vregs s;
    struct vxd_ddb *d;
    struct vmt_dev *o;
    vu8 *base;
    vu32 size = c->ecx & 0xFFFF, n = 0, w = 0, i;
    clr(&s);
    s.eax = (0x38 << 8) | 0x00;
    vmm_map_flat(&s);
    if (s.eax == 0xFFFFFFFFu || size < 2) { c->eflags |= VF_CF; return; }
    base = (vu8 *)s.eax;
    o = (struct vmt_dev *)(base + 2);
    /* the VMM */
    if (2 + sizeof(*o) <= size) {
        clr(&s);
        vmm_get_version(&s);
        for (i = 0; i < 8; i++) o->name[i] = "VMM     "[i];
        o->id = 1;
        o->major = (vu8)((s.eax >> 8) & 0xFF);
        o->minor = (vu8)(s.eax & 0xFF);
        o->init_order = 0;
        o->services = 0;
        o->flags = 0x80;
        o++;
        w++;
    }
    n++;
    for (d = &VMTASK_DDB; d && n < MAX_DEVS; d = (struct vxd_ddb *)d->next) {
        n++;
        if (2 + (w + 1) * sizeof(*o) > size) continue;
        for (i = 0; i < 8; i++) o->name[i] = d->name[i];
        o->id = d->device_id;
        o->major = d->major;
        o->minor = d->minor;
        o->init_order = d->init_order;
        o->services = d->svc_count;
        o->flags = (d->v86_api ? 1 : 0) | (d->pm_api ? 2 : 0) | (d->svc_table ? 4 : 0);
        o++;
        w++;
    }
    *(vu16 *)base = (vu16)w;
    c->eax = (c->eax & 0xFFFF0000u) | n;
    c->ecx = (c->ecx & 0xFFFF0000u) | (2 + w * sizeof(*o));
    if (w < n) c->eflags |= VF_CF; else c->eflags &= ~VF_CF;
}

/* function 3: end the VM DX:BX. CX 0: Close_VM (SI the milliseconds it
 * may take before the VMM ends it by force; 0: 5000), 1: Nuke_VM. Never
 * the System VM. Back AX 0, carry clear; AX 1 the System VM, 2 not a VM
 * (carry set) */
static void end_vm(struct client_regs *c)
{
    struct vregs s;
    vu32 vm = ((c->edx & 0xFFFF) << 16) | (c->ebx & 0xFFFF);
    vu32 err = 0;
    clr(&s);
    s.ebx = vm;
    vmm_validate_vm(&s);
    if (!vm || (s.eflags & VF_CF)) err = 2;
    else if (vm == sys_vm) err = 1;
    if (err) {
        c->eax = (c->eax & 0xFFFF0000u) | err;
        c->eflags |= VF_CF;
        return;
    }
    clr(&s);
    s.ebx = vm;
    if ((c->ecx & 0xFFFF) == 1) vmm_nuke_vm(&s);
    else {
        s.eax = (c->esi & 0xFFFF) ? (c->esi & 0xFFFF) : 5000;
        vmm_close_vm(&s);
    }
    c->eax &= 0xFFFF0000u;
    c->eflags &= ~VF_CF;
}

/* the API: EBX the VM, EBP the client's registers */
void __cdecl vmtask_api_proc(struct vregs *r)
{
    struct client_regs *c = (struct client_regs *)r->ebp;
    vu32 t;
    if (!sys_vm || !cb_off) { c->eflags |= VF_CF; return; }
    switch (c->eax & 0xFFFF) {
    case 0:
        c->eax = (c->eax & 0xFFFF0000u) | 0x0100;
        c->ebx = (c->ebx & 0xFFFF0000u) | 0x564D;
        c->ecx = (c->ecx & 0xFFFF0000u) | 0x544B;
        c->edx = (c->edx & 0xFFFF0000u) | caps;
        c->eflags &= ~VF_CF;
        return;
    case 1:
        snapshot(c);
        return;
    case 2:
        t = sys_busy();
        c->eax = (c->eax & 0xFFFF0000u) | (t & 0xFFFF);
        c->edx = (c->edx & 0xFFFF0000u) | (t >> 16);
        t = now();
        c->ebx = (c->ebx & 0xFFFF0000u) | (t & 0xFFFF);
        c->ecx = (c->ecx & 0xFFFF0000u) | (t >> 16);
        c->eflags &= ~VF_CF;
        return;
    case 3:
        end_vm(c);
        return;
    case 4:
        devices(c);
        return;
    }
    c->eflags |= VF_CF;
}

void __cdecl vmtask_control(struct vregs *r)
{
    struct vregs s;
    vu32 t;
    r->eflags &= ~VF_CF;
    switch (r->eax) {
    case SYS_CRITICAL_INIT:
        cb_off = vmm_alloc_cb_area(sizeof(struct tvm), 0);
        break;
    case DEVICE_INIT:
        if (!cb_off) break;
        clr(&s);
        vmm_get_sys_vm(&s);
        sys_vm = cur = s.ebx;
        vm_reset(sys_vm, 0);
        clr(&s);
        s.esi = (vu32)vmtask_ts;
        vmm_call_when_task_switched(&s);
        clr(&s);
        s.esi = (vu32)vmtask_idle;
        vmm_call_when_idle(&s);
        break;
    case CREATE_VM:
        if (!cb_off) break;
        t = now();
        vm_reset(r->ebx, t);
        break;
    case DESTROY_VM:
        if (r->ebx == cur) cur = 0;
        break;
    }
}
