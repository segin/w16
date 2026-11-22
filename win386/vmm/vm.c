/* vm.c - w16 VMM: the virtual machines (ARCHITECTURE.md 12.10). The
 * System VM (sysvm: the loader, then KRNL386 and Windows) and the DOS VMs
 * the SHELL device creates for the MS-DOS Prompt and other DOS programs:
 * their memory, instance data, creation and destruction, and the
 * scheduler that gives them time.
 *
 * Memory. Linear 0-4 MB is each VM's own page table (vmm.c switches the
 * page directory's first entry): below the first page of the free memory
 * Windows found (DOS, its drivers and TSRs, the WIN386 loader) every VM
 * sees the same physical pages; from there to the top of conventional
 * memory a DOS VM has pages of its own (the System VM keeps the real
 * ones); video memory, the ROMs and the HMA are shared. Each VM's first
 * megabyte is also mapped at a high linear address of its own (its
 * control block's CB_High_Linear: the page table again, at C0000000h + ID
 * x 4 MB), where devices and the System VM's grabber reach a VM that is
 * not running. Instance data (the DDK's word for data DOS and the BIOS
 * keep once but each VM needs a copy of): the interrupt vectors and the
 * BIOS data area (0-4FFh), DOS's swappable data area (INT 21h 5D06h:
 * current PSP, DTA, InDOS...) and the bytes between the free memory's MCB
 * and the first private page, and after the last one; they are copied
 * out and in at each switch. A new VM starts with them as they were when
 * Windows started (vm_snapshot).
 *
 * A DOS VM starts at the loader's VM stub (ldrasm.asm), which takes 4 KB
 * of the VM's free memory as a PSP of its own, EXECs the program SHELL
 * named (vm_set_program writes its path, command tail and directory
 * there) and reports its end at the VMEXIT breakpoint (vm_exited).
 *
 * The scheduler. A VM runs until its time slice is over (timer ticks: its
 * priority / 50, the foreground priority for the VM with the execution
 * focus, scaled by SYSTEM.INI [386Enh] MinTimeslice / 20 ms, at least one
 * tick; the System VM's priorities are WinTimeslice's; UNVERIFIED how
 * Windows 3.1 turns WinTimeSlice, MinTimeslice and the PIF's priorities
 * into time: w16's defaults keep the slices it had), it idles (HLT, INT 2Fh 1680h,
 * Release_Time_Slice) or another VM gets an interrupt while it idles. A
 * VM inside one of DOS's critical sections (INT 2Ah 80h/81h, which DOS
 * makes around its shared tables) is not taken off then. Every tick wakes
 * every VM. Switches happen at the end of a trap (vmm.c vmm_switch).
 *
 * Sources: the Windows 3.1 DDK's Virtual Device Adaptation Guide (.tools/
 * docs/vdag31: the VM control block's fields, Create_VM ... Destroy_VM,
 * the scheduler and VM services, instance data, _DOSMGR_Exec_VM); MS-DOS
 * 4.00 (INT 21h 5D06h, the MCB layout, INT 2Ah 80h/81h/82h); Intel 80386
 * Programmer's Reference Manual (paging). Observed under Windows 3.1 with
 * VXS.386 (tests/vxd/vxs.asm, tests/vxsrun.sh): the WIN386 handover note.
 */
#include "vmm.h"

struct vm *cur_vm = &sysvm;
struct vm *vm_list = &sysvm;
struct vm *vm_focus = &sysvm;
static struct vm *vm_pool;              /* destroyed VMs, kept with their memory */
static u32 cb_next = VM_CB_RESERVED;    /* _Allocate_Device_CB_Area */

#define HIGH_LIN(id)    (0xC0000000u + (id) * 0x400000u)
#define HIGH_PDE(id)    (0x300 + (id))
#define KSTACK_SIZE     16384
#define MAX_VM_ID       255             /* the high linear windows: C0000000h-FFFFFFFFh */

struct vmev { struct vmev *next; struct vm *vm; void (*fn)(struct vm *, void *); void *arg; };
static struct vmev *events;             /* VM events (vm_event) */
static struct vm *boost;                /* a VM with events: next to run */
static int resched;

void vmm_switch(struct vm *n);
u32 vmm_pdir_entry(u32 i, u32 v);
struct cregs *vmm_trap_state(struct cregs *r);
void page_free(u32 phys);

/* ---- instance data ---- */
struct irange { u32 lin, len; };
static struct irange irng[8];
static u32 n_irng, inst_size;
static u8 *inst_snap;                   /* as at Windows' start */
static u32 priv_start, priv_end;        /* the private pages (linear, 4 KB aligned) */

static void add_range(u32 lin, u32 len)
{
    if (!len || n_irng == sizeof(irng) / sizeof(irng[0])) return;
    irng[n_irng].lin = lin;
    irng[n_irng].len = len;
    n_irng++;
    inst_size += len;
}

static void inst_copy(u8 *buf, int out)
{
    u32 i, o = 0;
    for (i = 0; i < n_irng; i++) {
        if (out) memcpy(buf + o, (void *)irng[i].lin, irng[i].len);
        else memcpy((void *)irng[i].lin, buf + o, irng[i].len);
        o += irng[i].len;
    }
}

void vm_inst_out(struct vm *v) { if (v->inst) inst_copy(v->inst, 1); }
void vm_inst_in(struct vm *v) { if (v->inst) inst_copy(v->inst, 0); }

/* where the VMM reaches v's address lin (below 1 MB + 64 KB) now */
u32 vm_lin(struct vm *v, u32 lin)
{
    u32 i, o = 0;
    if (v == cur_vm) return lin;
    for (i = 0; i < n_irng; i++) {
        if (lin >= irng[i].lin && lin < irng[i].lin + irng[i].len)
            return (u32)v->inst + o + (lin - irng[i].lin);
        o += irng[i].len;
    }
    return HIGH_LIN(v->id) + lin;
}

static void vm_poke(struct vm *v, u32 lin, const void *src, u32 n)
{
    const u8 *s = src;
    while (n--) *(u8 *)vm_lin(v, lin++) = *s++;
}

/* ---- the System VM ---- */
void vm_sys_init(void)
{
    sysvm.id = 1;
    sysvm.cb = kmalloc(VM_CB_SIZE);
    sysvm.client = kmalloc(sizeof(struct client_regs));
    if (!sysvm.cb || !sysvm.client) panic("no memory for the System VM");
    sysvm.ptab_phys = vmm_pdir_entry(0, 0) & ~0xFFFu;
    vmm_pdir_entry(0, sysvm.ptab_phys | 7);
    vmm_pdir_entry(HIGH_PDE(1), sysvm.ptab_phys | 7);
    sysvm.cb->status = 0;
    sysvm.cb->high_linear = HIGH_LIN(1);
    sysvm.cb->client_pointer = (u32)sysvm.client;
    sysvm.cb->vmid = 1;
    sysvm.cb->vm = &sysvm;
    sysvm.started = 1;
    sysvm.refstk_seg = info->refstk_seg;
    sysvm.refstk_top = info->refstk_top;
    sysvm.refsp = info->refstk_top;
    /* SYSTEM.INI [386Enh] WinTimeSlice's default (UNVERIFIED that w16
     * should read it; the DDK's priorities are 1-10000) */
    /* SYSTEM.INI [386Enh] WinTimeslice (the loader reads it; 100,50 when
     * absent, as Windows 3.1's Control Panel shows) */
    sysvm.prio_fg = info->win_fg ? info->win_fg : 100;
    sysvm.prio_bg = info->win_bg ? info->win_bg : 50;
    sysvm.slice = 2;
}

/* the instance data new VMs start with: Windows' start (after the
 * devices' initialisation, before KRNL386 runs) */
void vm_snapshot(void)
{
    u32 mcb = (u32)info->free_mcb << 4, top = (u32)info->mem_top << 4;
    /* the first free arena from there: the loader's own allocations (its
     * far heap) can follow its program block (MS-DOS 4.00 MCB: 'M' or 'Z',
     * the owner PSP at +1, 0 when free, the size in paragraphs at +3) */
    while (mcb && mcb < top && *(u8 *)mcb == 'M' && *(u16 *)(mcb + 1))
        mcb += ((u32)*(u16 *)(mcb + 3) + 1) << 4;
    if (mcb >= top || (*(u8 *)mcb != 'M' && *(u8 *)mcb != 'Z') || *(u16 *)(mcb + 1)) mcb = 0;
    info->free_mcb = (u16)(mcb >> 4);
    add_range(0, 0x500);
    if (info->sda_lin && info->sda_size) add_range(info->sda_lin, info->sda_size);
    if (info->free_mcb && top > mcb + 0x2000) {
        /* the MCB's paragraph is instance data even when the arena
         * starts on a page (a page of its own then started there, zeros:
         * every new VM lost its free MCB) */
        priv_start = (mcb + 0x10 + 0xFFF) & ~0xFFFu;
        priv_end = top & ~0xFFFu;
        add_range(mcb, priv_start - mcb);
        if (top > priv_end) add_range(priv_end, top - priv_end);
    }
    inst_snap = kmalloc(inst_size);
    sysvm.inst = kmalloc(inst_size);
    if (!inst_snap || !sysvm.inst) panic("no memory for the instance data");
    inst_copy(inst_snap, 1);
    trace("VMM: instance data %d bytes in %d ranges, DOS VM memory %x-%x (MCB %x)\n",
          inst_size, n_irng, priv_start, priv_end, info->free_mcb);
}

/* ---- the VM list ---- */
struct vm *vm_of(u32 h)
{
    struct vm *v;
    for (v = vm_list; v; v = v->next)
        if (VMH(v) == h) return v;
    return NULL;
}

static u32 new_id(void)
{
    u32 id;
    struct vm *v;
    for (id = 2; id <= MAX_VM_ID; id++) {
        for (v = vm_list; v && v->id != id; v = v->next)
            ;
        if (!v) return id;
    }
    return 0;
}

/* a DOS VM, set up but not started (no control message yet) */
struct vm *vm_create(void)
{
    struct vm *v, **pp;
    u32 id = new_id(), *pt, *spt, a, p;
    u8 *cb, *inst, *kst;
    struct client_regs *cl;

    if (!id || !priv_start) return NULL;
    if (vm_pool) {
        v = vm_pool;
        vm_pool = v->free_next;
        cb = (u8 *)v->cb; cl = v->client; inst = v->inst; kst = (u8 *)v->kstack_top - KSTACK_SIZE;
    } else {
        v = kmalloc(sizeof(*v));
        cb = kmalloc(VM_CB_SIZE);
        cl = kmalloc(sizeof(*cl));
        inst = kmalloc(inst_size);
        kst = kmalloc(KSTACK_SIZE);
        if (!v || !cb || !cl || !inst || !kst) {
            kfree(v); kfree(cb); kfree(cl); kfree(inst); kfree(kst);
            return NULL;
        }
    }
    memset(v, 0, sizeof(*v));
    memset(cb, 0, VM_CB_SIZE);
    memset(cl, 0, sizeof(*cl));
    v->cb = (struct vm_cb *)cb;
    v->client = cl;
    v->inst = inst;
    v->kstack_top = ((u32)kst + KSTACK_SIZE) & ~15u;
    v->kesp0 = v->kstack_top;
    v->id = id;
    /* the page table: the System VM's, with pages of its own for the
     * private range; short of memory, the VM is not made (its blocks
     * kept for the next, as a destroyed VM's) */
    p = page_alloc();
    pt = p ? (u32 *)phys_to_lin(p) : NULL;
    if (pt) {
        spt = (u32 *)phys_to_lin(sysvm.ptab_phys);
        memcpy(pt, spt, 4096);
        for (a = priv_start; a < priv_end; a += 4096) {
            u32 pg = page_alloc();
            if (!pg) break;
            pt[a >> 12] = pg | 7;
        }
    }
    if (!pt || a < priv_end) {
        if (pt) {
            u32 b;
            for (b = priv_start; b < a; b += 4096) page_free(pt[b >> 12] & ~0xFFFu);
            page_free(p);
        }
        v->id = 0;
        v->cb = (struct vm_cb *)cb; v->client = cl; v->inst = inst;
        v->free_next = vm_pool;
        vm_pool = v;
        return NULL;
    }
    {
        static u32 gens;
        v->gen = ++gens;
    }
    v->ptab_phys = p;
    vmm_pdir_entry(HIGH_PDE(id), p | 7);
    v->cb->high_linear = HIGH_LIN(id);
    v->cb->client_pointer = (u32)cl;
    v->cb->vmid = id;
    v->cb->vm = v;
    memcpy(inst, inst_snap, inst_size);
    v->prio_fg = 100;
    v->prio_bg = 50;
    /* the VM stub's registers: DS = SS = the first paragraph after the
     * free memory's MCB, where the stub's 4 KB will be */
    v->refstk_seg = info->free_mcb + 1;
    v->refstk_top = DOSVM_REFSTK_TOP;
    v->refsp = DOSVM_REFSTK_TOP;
    v->r.cs = info->ldr_cs;
    v->r.eip = info->bp_off + W386_BP_VMSTART;
    v->suspended = 1;                   /* until SHELL gives it the focus (Resume_VM) */
    v->r.ds = v->r.es = v->r.ss = v->refstk_seg;
    v->r.esp = DOSVM_STACK_TOP;
    v->r.eflags = EFL_VM | EFL_IOPL | EFL_IF | 2;
    for (pp = &vm_list; *pp; pp = &(*pp)->next)
        ;
    *pp = v;
    trace("VMM: VM %d created, handle %x\n", id, VMH(v));
    return v;
}

/* the program the VM stub EXECs: path (ASCIIZ), tail (a length byte, the
 * text, CR), the start-up directory ("C:\WINDOWS", or empty) */
int vm_set_program(struct vm *v, const char *path, const u8 *tail, const char *dir)
{
    u32 base = ((u32)v->refstk_seg << 4) + DOSVM_PARAMS, n;
    u8 b[0x220];
    memset(b, 0, sizeof(b));
    for (n = 0; path[n] && n < 0x7F; n++) b[n] = path[n];
    n = tail ? tail[0] : 0;
    if (n > 0x7E) n = 0x7E;
    b[0x80] = (u8)n;
    if (tail) memcpy(b + 0x81, tail + 1, n);
    b[0x81 + n] = 0x0D;
    /* the drive (0 = A:) and the directory */
    if (dir && dir[0] && dir[1] == ':') {
        b[0x100] = (u8)((dir[0] | 0x20) - 'a');
        for (n = 0; dir[n] && n < 0x7F; n++) b[0x101 + n] = dir[n];
    } else {
        b[0x100] = 2;                   /* C: (UNVERIFIED default) */
    }
    /* the EXEC parameter block: environment 0 (the parent's), the tail,
     * the two FCBs (blank) */
    *(u16 *)(b + 0x180) = 0;
    *(u16 *)(b + 0x182) = DOSVM_PARAMS + 0x80;
    *(u16 *)(b + 0x184) = v->refstk_seg;
    *(u16 *)(b + 0x186) = DOSVM_PARAMS + 0x200;
    *(u16 *)(b + 0x188) = v->refstk_seg;
    *(u16 *)(b + 0x18A) = DOSVM_PARAMS + 0x210;
    *(u16 *)(b + 0x18C) = v->refstk_seg;
    memset(b + 0x201, ' ', 11);
    memset(b + 0x211, ' ', 11);
    vm_poke(v, base, b, sizeof(b));
    vm_set_message(v, "", 0);
    return 1;
}

/* a message the VM stub writes to standard output before the program */
void vm_set_message(struct vm *v, const char *msg, u32 n)
{
    u32 seg = (u32)v->refstk_seg << 4;
    u16 len;
    if (n > DOSVM_MSG_MAX) n = DOSVM_MSG_MAX;
    len = (u16)n;
    vm_poke(v, seg + DOSVM_MSG, msg, n);
    vm_poke(v, seg + DOSVM_MSGLEN, &len, 2);
}

void vm_destroy(struct vm *v)
{
    struct vm **pp;
    u32 *pt, a;
    if (v == &sysvm || v == cur_vm) return;
    for (pp = &vm_list; *pp && *pp != v; pp = &(*pp)->next)
        ;
    if (!*pp) return;
    *pp = v->next;
    pt = (u32 *)phys_to_lin(v->ptab_phys);
    for (a = priv_start; a < priv_end; a += 4096)
        if (pt[a >> 12] & 1) page_free(pt[a >> 12] & ~0xFFFu);
    vmm_pdir_entry(HIGH_PDE(v->id), 0);
    page_free(v->ptab_phys);
    io_vm_gone(v);
    {   /* its events */
        struct vmev **ep, *e;
        for (ep = &events; (e = *ep) != NULL; )
            if (e->vm == v) { *ep = e->next; kfree(e); } else ep = &e->next;
        if (boost == v) boost = NULL;
    }
    if (vm_focus == v) vm_focus = &sysvm;
    trace("VMM: VM %d destroyed\n", v->id);
    v->id = 0;
    v->cb->vm = NULL;
    v->free_next = vm_pool;
    vm_pool = v;
}

/* ---- the scheduler ---- */

static int runnable(struct vm *v)
{
    return !v->suspended && !v->susp_count && !v->ended;
}

/* V86 INT 21h calls whose return frame is gone without the call having
 * returned through it: a program ended inside DOS, Ctrl+C at COMMAND.COM's
 * prompt (its handler starts the prompt again on a fresh stack). They no
 * longer count: the VM kept the processor for good otherwise (the
 * scheduler leaves a VM in DOS running). The frame is gone when the VM's
 * stack, the same segment, is above it */
static void dos_frames_check(struct cregs *r)
{
    struct vm *v = cur_vm;
    while (v->ndos && (r->eflags & EFL_VM) && (u16)r->ss == v->dos_ss[v->ndos - 1]
           && (u16)r->esp >= (u16)(v->dos_sp[v->ndos - 1] + 6))
        v->ndos--;
}

static u32 slice_of(struct vm *v)
{
    u32 p = v == vm_focus ? v->prio_fg : v->prio_bg;
    u32 ms = info->min_slice ? info->min_slice : 20;
    /* priority / 50 ticks at MinTimeslice 20 ms, in proportion to it */
    p = p * ms / (50 * 20);
    return p ? p : 1;
}

void vm_idle(void)
{
    cur_vm->idle = 1;
    resched = 1;
}

/* an interrupt or event for v: it may run again. The VM running now keeps
 * the rest of its time slice unless it idles: a timer tick wakes every VM,
 * and switching at once took a busy DOS VM off inside its timer
 * interrupt's handler every tick; back, it always had the next tick's
 * interrupt pending, which kept its keyboard's (IRQ 1, lower priority)
 * waiting: in plain DOSBox, whose BIOS waits for a key in a loop without
 * HLT, keys typed in a full-screen MS-DOS Prompt came seconds late or not
 * at all */
void vm_wake(struct vm *v)
{
    v->idle = 0;
    if (v != cur_vm && runnable(v) && (cur_vm->idle || !cur_vm->slice || !runnable(cur_vm)))
        resched = 1;
}

void vm_tick(void)
{
    struct vm *v;
    for (v = vm_list; v; v = v->next) v->idle = 0;
    vdd_tick();
    if (cur_vm->slice) cur_vm->slice--;
    if (!cur_vm->slice && vm_list->next) resched = 1;
}

void vm_set_focus(struct vm *v)
{
    vm_focus = v;
    resched = 1;
}

/* the next VM to run: after the current one in the list, the first that
 * can run and is not idle; else the current one if it can run; else the
 * System VM */
static struct vm *pick(void)
{
    struct vm *v = cur_vm;
    if (boost) {
        v = boost;
        boost = NULL;
        if (runnable(v) || v == &sysvm) return v;
        v = cur_vm;
    }
    /* SYSTEM.INI [386Enh] WinExclusive: while Windows has the focus no
     * other VM runs (Control Panel's "Exclusive in Foreground") */
    if (info->win_exclusive && vm_focus == &sysvm && runnable(&sysvm)) return &sysvm;
    do {
        v = v->next ? v->next : vm_list;
        if (runnable(v) && !v->idle) return v;
    } while (v != cur_vm);
    if (runnable(cur_vm)) return cur_vm;
    for (v = vm_list; v; v = v->next)
        if (runnable(v)) return v;
    return &sysvm;
}

/* ---- ending a DOS VM ---- */

/* A fault in a DOS VM's V86 code (a #GP): that VM ends, Windows goes on.
 * Windows 3.1 (VX12 G, VXSH.386): Suspend_VM, VM_Suspend, VM_Not_
 * Executeable with EDX 1, the devices' focus and the execution focus to
 * the System VM, SHELL's event 41Bh with wParam 4 to WINOA386, which
 * shows "This application has violated system integrity due to an invalid
 * general protection fault and will be terminated." and destroys the VM
 * (SHELL 4) */
void vm_crashed(struct cregs *r)
{
    struct vm *v = cur_vm;
    trace("VMM: VM %d: a fault at %x:%x: the VM ends\n", v->id, r->cs, r->eip);
    v->ended = 1;
    shell_vm_crashed(v);
    resched = 1;
}

/* Closing a DOS VM (Ctrl+Alt+Del's Enter): its programs end with INT 21h
 * 4CFFh, each in turn, until the VM stub's (Windows 3.1: AX=4CFFh in the
 * VM after Close_VM_Notify, VXSH.386); the stub then ends the VM as usual
 * (vm_exited) */
void vm_close(struct vm *v)
{
    v->closing = 1;
    v->idle = 0;
    if (v->suspended && !v->ended) { v->suspended = 0; vm_wake(v); }
}

/* at the end of each trap in a closing VM in V86 mode: a program other
 * than the VM stub's own is sent to the loader's vm_close */
static void close_step(struct cregs *r)
{
    struct vm *v = cur_vm;
    u16 psp;
    if (v->ended || !(r->eflags & EFL_VM)) return;
    if (info->sda_lin) psp = *(u16 *)(info->sda_lin + 0x10);
    else {                                  /* (a DOS without 5D06h: INT 21h 51h, in the VM) */
        struct cregs t;
        memset(&t, 0, sizeof(t));
        t.eax = 0x5100;
        v86_int(&t, 0x21);
        psp = (u16)t.ebx;
    }
    if (psp == v->refstk_seg) return;               /* the stub's: it ends the VM */
    if ((u16)r->cs == info->ldr_cs && (u16)r->eip >= info->vmclose_off
        && (u16)r->eip < info->vmclose_off + 16) return;
    /* (inside DOS too, as Windows 3.1, whose AX=4CFFh came while
     * COMMAND.COM waited for a line: DOS's terminate drops the call it
     * was in) */
    if (psp == v->closing_psp) return;      /* sent there already: DOS has it now */
    trace("VMM: VM %d: program %x ended (closing)\n", v->id, psp);
    v->closing_psp = psp;
    r->cs = info->ldr_cs;
    r->eip = info->vmclose_off;
    r->eflags |= EFL_IF;
    v->ndos = 0;
    v->crit = 0;
}

/* ---- VM events: a device's procedure run in a given VM (the DDK's
 * Schedule_VM_Event, Call_Priority_VM_Event): at the end of one of its
 * traps, where it can run code in the VM (nested execution). In the
 * System VM only when its program has a protected mode, interrupts
 * on and no hardware interrupt's handler runs (in V86 mode too: the
 * callback runs on the host stack as an interrupt's handler would; where a callback into
 * Windows is safe; UNVERIFIED Windows 3.1's exact conditions) ---- */
void vm_event(struct vm *v, void (*fn)(struct vm *, void *), void *arg)
{
    struct vmev *e = kmalloc(sizeof(*e)), **pp;
    if (!e) return;
    e->vm = v;
    e->fn = fn;
    e->arg = arg;
    e->next = NULL;
    for (pp = &events; *pp; pp = &(*pp)->next)
        ;
    *pp = e;
    v->idle = 0;
    if (v != cur_vm) { boost = v; resched = 1; }
}

int dpmi_in_handler(void);

static int events_ok(struct cregs *r)
{
    if (cur_vm != &sysvm) return 1;
    return sysvm.pm && (r->eflags & EFL_IF) && !dpmi_in_handler();
}

static void run_events(struct cregs *r)
{
    struct vmev **pp, *e;
    struct cregs *o;
    for (pp = &events; (e = *pp) != NULL; ) {
        if (e->vm != cur_vm || !events_ok(r)) { pp = &e->next; continue; }
        *pp = e->next;
        o = vmm_trap_state(r);
        e->fn(e->vm, e->arg);
        vmm_trap_state(o);
        kfree(e);
        pp = &events;
    }
}

void vm_end_trap(struct cregs *r)
{
    struct vm *n;
    struct cregs *o;
    if (events) run_events(r);
    if (cur_vm->closing) close_step(r);
    if (!resched) return;
    if (cur_vm->ndos) dos_frames_check(r);
    if ((cur_vm->crit > 0 || cur_vm->indos > 0 || cur_vm->ndos > 0) && runnable(cur_vm) && !cur_vm->idle) return;
    resched = 0;
    n = pick();
    if (n == cur_vm) {
        if (!cur_vm->slice) cur_vm->slice = slice_of(cur_vm);
        return;
    }
    n->slice = slice_of(n);
    cur_vm->r = *r;
    vmm_switch(n);
    /* back in this VM: what VPICD has for it now, and its events */
    o = vmm_trap_state(r);
    if (vpicd) vpicd->resume();
    vmm_trap_state(o);
    if (events) run_events(r);
}

/* a new VM's first instruction is the VMSTART breakpoint: its
 * VM_Critical_Init and VM_Init (the DDK: sent in the new VM, before it
 * runs; Windows 3.1 sent them after VM_Resume), then the VM stub */
void vm_start(struct cregs *r)
{
    struct cregs *o;
    trace("VMM: VM %d starts\n", cur_vm->id);
    o = vmm_trap_state(r);
    vxd_send(VM_CRITICAL_INIT, cur_vm, 0, 0, 0);
    vxd_send(VM_INIT, cur_vm, 0, 0, 0);
    vdd_vm_init(cur_vm);
    vmm_trap_state(o);
    r->cs = info->ldr_cs;
    r->eip = info->vmstub_off;
}

/* INT 2Ah from V86 code: DOS's critical sections (MS-DOS: AH=80h begin,
 * 81h end, 82h end them all) */
void vm_int2a(struct cregs *r)
{
    switch ((r->eax >> 8) & 0xFF) {
    case 0x80: cur_vm->crit++; break;
    case 0x81: if (cur_vm->crit) cur_vm->crit--; break;
    case 0x82: cur_vm->crit = 0; break;
    }
}

/* A VM inside a DOS call keeps the processor until the call returns or
 * the VM idles (DOS waiting for a key calls INT 28h, the BIOS halts): no
 * other VM enters DOS halfway through its call. MS-DOS keeps the state of
 * a call in its swappable data area (instance data here) and marks what
 * must not be interrupted with INT 2Ah 80h/81h; DOSBox's built-in DOS
 * keeps it in the emulator (its transfer buffer for INT 21h 40h, its
 * console's state), and a DOS VM's write to the screen came out with the
 * bytes of a file Windows read meanwhile (rows of garbage at the top of
 * the MS-DOS Prompt, about 1 run in 3). Windows 3.1 on the same DOS did not
 * show it (UNVERIFIED how it avoids it: its time slices are longer; the
 * DDK's critical section, Begin_Critical_Section, is the same idea). The
 * calls that do not return are left out: EXEC (4Bh: the child runs inside
 * it), terminate (00h, 31h, 4Ch) */
int vm_dos_call(u32 ax)
{
    u32 ah = (ax >> 8) & 0xFF;
    return ah != 0x00 && ah != 0x31 && ah != 0x4B && ah != 0x4C;
}

/* INT 21h from V86 code: on to DOS with a second frame above the
 * caller's, whose IRET reaches the INT21RET breakpoint */
void vm_int21(struct cregs *r)
{
    u16 fl = (u16)r->eflags;
    if (!vm_dos_call(r->eax)) { v86_reflect(r, 0x21); return; }
    v86_reflect(r, 0x21);
    v86_push16(r, fl);
    v86_push16(r, (u16)info->ldr_cs);
    v86_push16(r, (u16)(info->bp_off + W386_BP_INT21RET));
    dos_frames_check(r);
    if (cur_vm->ndos < 8) {
        cur_vm->dos_ss[cur_vm->ndos] = (u16)r->ss;
        cur_vm->dos_sp[cur_vm->ndos] = (u16)r->esp;
        cur_vm->ndos++;
    }
}

/* DOS has returned: the flags it returned, the caller's CS:IP */
void vm_int21_ret(struct cregs *r)
{
    r->eip = v86_pop16(r);
    r->cs = v86_pop16(r);
    v86_pop16(r);
    if (cur_vm->ndos > 0) cur_vm->ndos--;
}

/* the VM stub's VMEXIT breakpoint: the VM's program has ended (AX its
 * exit code, carry: the EXEC failed with DOS's error AX). The VM runs no
 * more; SHELL ends it as Windows 3.1's does (shell.c) */
void vm_exited(struct cregs *r)
{
    cur_vm->exit_code = AX(r) | ((r->eflags & EFL_CF) ? 0x10000u : 0);
    cur_vm->ended = 1;
    trace("VMM: VM %d's program ended, code %x%s\n", cur_vm->id, AX(r),
          (r->eflags & EFL_CF) ? " (EXEC failed)" : "");
    shell_vm_ended(cur_vm);
    resched = 1;
}

/* ---- the VMM's VM services (the DDK's descriptions) ---- */
int vm_service(u32 svc, struct svcregs *s)
{
    struct vm *v;
    switch (svc) {
    case 0x0001:                        /* Get_Cur_VM_Handle */
        s->ebx = VMH(cur_vm);
        return 1;
    case 0x0002:                        /* Test_Cur_VM_Handle: ZF */
        if (s->ebx == VMH(cur_vm)) s->eflags |= EFL_ZF; else s->eflags &= ~EFL_ZF;
        return 1;
    case 0x0003:                        /* Get_Sys_VM_Handle */
        s->ebx = VMH(&sysvm);
        return 1;
    case 0x0004:                        /* Test_Sys_VM_Handle */
        if (s->ebx == VMH(&sysvm)) s->eflags |= EFL_ZF; else s->eflags &= ~EFL_ZF;
        return 1;
    case 0x0005:                        /* Validate_VM_Handle: carry if not */
        if (!vm_of(s->ebx)) s->eflags |= EFL_CF;
        return 1;
    case 0x002B:                        /* Suspend_VM */
        /* the DDK: a count; VM_Suspend to the devices when the VM was
         * not suspended already; not the System VM (SHELL's own
         * suspending of full-screen VMs is separate: shell.c) */
        if ((v = vm_of(s->ebx)) == NULL || v == &sysvm) { s->eflags |= EFL_CF; return 1; }
        if (!v->susp_count++ && !v->suspended) vxd_send(VM_SUSPEND, v, 0, 0, 0);
        if (v == cur_vm) resched = 1;
        return 1;
    case 0x002C:                        /* Resume_VM */
    case 0x002D:                        /* No_Fail_Resume_VM */
        /* the count down; at 0 VM_Resume, which a device may refuse
         * (Resume_VM then fails, the count 1) */
        if ((v = vm_of(s->ebx)) == NULL || !v->susp_count) { s->eflags |= EFL_CF; return 1; }
        if (--v->susp_count) return 1;
        if (!v->suspended && vxd_send(VM_RESUME, v, 0, 0, 0) && svc == 0x002C) {
            v->susp_count = 1;
            s->eflags |= EFL_CF;
            return 1;
        }
        vm_wake(v);
        return 1;
    case 0x0030:                        /* Get_Execution_Focus */
        s->ebx = VMH(vm_focus);
        return 1;
    case 0x0031:                        /* Set_Execution_Focus */
        if ((v = vm_of(s->ebx)) == NULL) { s->eflags |= EFL_CF; return 1; }
        vm_set_focus(v);
        return 1;
    case 0x0038:                        /* Release_Time_Slice */
        vm_idle();
        return 1;
    case 0x0039:                        /* Wake_Up_VM */
        if ((v = vm_of(s->ebx)) != NULL) vm_wake(v);
        return 1;
    case 0x003B:                        /* Get_Next_VM_Handle: the list is circular */
        v = vm_of(s->ebx);
        v = v && v->next ? v->next : vm_list;
        s->ebx = VMH(v);
        return 1;
    case 0x00A7: {                      /* _Allocate_Device_CB_Area(size, flags): C, EAX the offset */
        u32 *args = (u32 *)(s + 1) + 1, n = (args[0] + 3) & ~3u;
        if (args[0] > VM_CB_SIZE || n > VM_CB_SIZE - cb_next) { s->eax = 0; return 1; }
        s->eax = cb_next;
        cb_next += n;
        return 1;
    }
    }
    return 0;
}
