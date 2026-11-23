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
        u32 run;
        spt = (u32 *)phys_to_lin(sysvm.ptab_phys);
        memcpy(pt, spt, 4096);
        /* physically contiguous when it can be, as Windows 3.1 gives a
         * DOS VM its memory (VDS, INT 4Bh 8103h in a box: three pages
         * locked without a buffer at consecutive physical addresses):
         * DMA to a box's buffer then needs no copy (VDMAD) */
        run = page_alloc_run((priv_end - priv_start) >> 12, 0, 0);
        for (a = priv_start; a < priv_end; a += 4096) {
            u32 pg = run ? run + (a - priv_start) : page_alloc();
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
    v->exclusive = v->background = 0;
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

/* The conventional memory a DOS VM's program gets: the PIF's KB Desired
 * (SHELL function 3's block +1Ah). Windows 3.1 (tests/pifread.sh, WC1.COM's
 * PSP:2 under Microsoft's WIN386): the top of memory is the VM's free
 * memory's start plus that many KB (64 KB: 1C00h, 100 KB: 2500h, from
 * 0C00h), at most 640 KB's top; FFFFh (-1) all of it; 0 gave 4 KB. Here
 * the free arena after the stub's 8 KB is cut there: an MCB of the
 * system's (owner 8) at the limit holds the rest, so the arena's chain
 * stays as DOS made it. UNVERIFIED: 1-3 KB, and what KB Required does
 * (a VM that cannot get it: 3.1's message). */
int vm_set_memory(struct vm *v, u32 kb)
{
    u32 mcb = (u32)info->free_mcb << 4, start, end, lim, size;
    u8 m[16];
    if (kb == 0xFFFF || !info->free_mcb) return 0;
    if (kb < 4) kb = 4;
    if (*(u8 *)vm_lin(v, mcb) != 'M' && *(u8 *)vm_lin(v, mcb) != 'Z') return 0;
    size = *(u16 *)vm_lin(v, mcb + 3);
    start = (u32)info->free_mcb + 1;
    end = start + size;                 /* the paragraph after the free arena */
    /* the stub's block (its MCB is the free one's), then the program's:
     * the program sees as much as under 3.1 (tests/pifread.sh -W edmema:
     * top - PSP FB4h for 64 KB, as 3.1's 1C00h - 0C4Ch; the stub's
     * block ends a paragraph before DOSVM_BLOCK) */
    lim = start + DOSVM_BLOCK - 1 + kb * 64;
    if (lim + 1 >= end) return 0;
    memset(m, 0, sizeof(m));
    m[0] = *(u8 *)vm_lin(v, mcb);       /* the new MCB ends the chain if the old did */
    *(u16 *)(m + 1) = 8;
    *(u16 *)(m + 3) = (u16)(end - lim - 1);
    memcpy(m + 8, "SC", 2);
    vm_poke(v, lim << 4, m, 16);
    m[0] = 'M';
    vm_poke(v, mcb, m, 1);
    size = lim - start;
    vm_poke(v, mcb + 3, &size, 2);
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
    v86mm_vm_destroy(v);
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

/* Time-slice scheduling (the DDK's Time-Slice Scheduler Services; what
 * Windows 3.1 does observed with VXT.386, tests/vxd/vxt.asm, and VX13,
 * tests/vmpollrun.sh: ARCHITECTURE.md 12.12). A VM is scheduled when it
 * can run and has the execution focus or runs in the background (the
 * PIF's Background; the System VM always): Windows 3.1's
 * Get_Time_Slice_Info counted the System VM and Background DOS VMs, and
 * a DOS VM without Background got no time while Windows had the focus */
static int scheduled(struct vm *v)
{
    if (!runnable(v)) return 0;
    return v == vm_focus || v == &sysvm || v->background || v->closing || v->ended;
}

/* CB_VM_Status (the DDK's names; the values as Windows 3.1's control
 * blocks showed them to VXT.386: the System VM 7062h, F062h while it
 * idles, a Background DOS VM 4002h; Suspended and Not_Executeable
 * UNVERIFIED) */
#define VMSTAT_EXCLUSIVE    0x0001
#define VMSTAT_BACKGROUND   0x0002
#define VMSTAT_SUSPENDED    0x0008
#define VMSTAT_NOT_EXEC     0x0010
#define VMSTAT_PM_EXEC      0x0020
#define VMSTAT_PM_APP       0x0040
#define VMSTAT_HIGH_PRI_BACK 0x0200
#define VMSTAT_TS_SCHED     0x4000
#define VMSTAT_IDLE         0x8000
#define VMSTAT_SETTABLE     (VMSTAT_EXCLUSIVE | VMSTAT_BACKGROUND | VMSTAT_HIGH_PRI_BACK)

static void status_of(struct vm *v)
{
    u32 st = v->cb->status & ~(VMSTAT_EXCLUSIVE | VMSTAT_BACKGROUND | VMSTAT_SUSPENDED
                               | VMSTAT_NOT_EXEC | VMSTAT_PM_EXEC | VMSTAT_PM_APP
                               | VMSTAT_TS_SCHED | VMSTAT_IDLE);
    if (v->exclusive) st |= VMSTAT_EXCLUSIVE;
    if (v->background || v == &sysvm) st |= VMSTAT_BACKGROUND;
    if (v->suspended || v->susp_count) st |= VMSTAT_SUSPENDED;
    if (v->ended) st |= VMSTAT_NOT_EXEC;
    if (v == &sysvm && sysvm.pm) {
        st |= VMSTAT_PM_APP;
        if (v == cur_vm ? !(cur_vm->r.eflags & EFL_VM) : !(v->r.eflags & EFL_VM)) st |= VMSTAT_PM_EXEC;
    }
    if (scheduled(v)) st |= VMSTAT_TS_SCHED;
    if (v->idle) st |= VMSTAT_IDLE;
    v->cb->status = st;
}

static void status_all(void)
{
    struct vm *v;
    for (v = vm_list; v; v = v->next) status_of(v);
}

/* ---- callbacks: Call_When_Task_Switched, Call_When_Idle,
 * Call_When_Not_Critical ---- */
struct vmcb { struct vmcb *next; u32 proc, ref; };
static struct vmcb *ts_cbs, *idle_cbs, *notcrit_cbs;

static void add_cb(struct vmcb **list, u32 proc, u32 ref, int at_end)
{
    struct vmcb *c = kmalloc(sizeof(*c)), **pp = list;
    if (!c) return;
    c->proc = proc;
    c->ref = ref;
    c->next = NULL;
    if (at_end) while (*pp) pp = &(*pp)->next;
    else c->next = *pp;
    *pp = c;
}

/* the System VM idles: when every other scheduled VM idles too, the idle
 * callbacks in the order installed until one clears carry (the DDK) */
static void system_idle(void)
{
    struct vm *v;
    struct vmcb *c;
    struct svcregs s;
    for (v = vm_list; v; v = v->next)
        if (v != &sysvm && scheduled(v) && !v->idle) return;
    for (c = idle_cbs; c; c = c->next) {
        memset(&s, 0, sizeof(s));
        s.ebx = VMH(&sysvm);
        s.ebp = (u32)sysvm.client;
        s.eflags = EFL_CF;
        vmm_call_proc(c->proc, &s);
        if (!(s.eflags & EFL_CF)) break;
    }
}

/* the current VM has nothing to do: HLT (an interrupt ends it: the next
 * timer tick at the latest), KERNEL's idle call (INT 2Fh 1689h) */
void vm_idle(void)
{
    struct vm *v = cur_vm;
    v->idle = 1;
    v->idle_until = vmm_system_time();
    resched = 1;
    if (v == &sysvm) system_idle();
}

/* the current VM releases its time slice (INT 2Fh 1680h,
 * Release_Time_Slice, VMPOLL's detection): it gets no more time until an
 * interrupt of its own or Wake_Up_VM wakes it. Windows 3.1 (VXT.386): a
 * Background DOS VM released so ran again only every ~7 s (IDLE_WAKE),
 * the timer's ticks did not wake it. The VM with the focus, the System VM
 * and a high-priority background VM: the next tick wakes them (the DDK:
 * the VM with the focus loses nothing; UNVERIFIED: its "-500 ms" for a
 * background VM, which 3.1's ~7 s does not show) */
void vm_release(void)
{
    struct vm *v = cur_vm;
    vm_idle();
    if (v != vm_focus && v != &sysvm && !(v->cb->status & VMSTAT_HIGH_PRI_BACK))
        v->idle_until += IDLE_WAKE;
}

/* an interrupt or event for v: it may run again. The VM running now keeps
 * the rest of its time slice unless it idles: a timer tick wakes every VM,
 * and switching at once took a busy DOS VM off inside its timer
 * interrupt's handler every tick; back, it always had the next tick's
 * interrupt pending, which kept its keyboard's (IRQ 1, lower priority)
 * waiting: in plain DOSBox, whose BIOS waits for a key in a loop without
 * HLT, keys typed in a full-screen MS-DOS Prompt came seconds late or not
 * at all. The timer's own requests (vm_tick_wakes) wake an idle VM only
 * once its release penalty is over */
static int tick_waking;

void vm_wake(struct vm *v)
{
    if (tick_waking && v->idle && (s32)(vmm_system_time() - v->idle_until) < 0) return;
    v->idle = 0;
    if (v != cur_vm && runnable(v) && (cur_vm->idle || !cur_vm->slice || !runnable(cur_vm)))
        resched = 1;
}

void vm_tick_wakes(int on)
{
    tick_waking = on;
}

void vm_tick(void)
{
    struct vm *v;
    u32 now = vmm_system_time();
    for (v = vm_list; v; v = v->next)
        if (v->idle && (s32)(now - v->idle_until) >= 0) v->idle = 0;
    vdd_tick();
    status_all();
    if (cur_vm->slice) cur_vm->slice--;
    if (!cur_vm->slice && vm_list->next) resched = 1;
}

void vm_set_focus(struct vm *v)
{
    vm_focus = v;
    resched = 1;
}

/* the next VM to run: a boosted VM (Adjust_Exec_Priority: the highest
 * execution priority above the current VM's, the DDK); else, after the
 * current one in the list, the first scheduled VM that is not idle; else
 * the current one if it can run; else the System VM */
static struct vm *pick(void)
{
    struct vm *v = cur_vm, *b = NULL;
    if (boost) {
        v = boost;
        boost = NULL;
        if (runnable(v) || v == &sysvm) return v;
        v = cur_vm;
    }
    for (v = vm_list; v; v = v->next)
        if (v != cur_vm && runnable(v) && v->exec_prio > cur_vm->exec_prio
            && (!b || v->exec_prio > b->exec_prio)) b = v;
    if (b) return b;
    if (cur_vm->exec_prio > 0 && runnable(cur_vm) && !cur_vm->idle) return cur_vm;
    v = cur_vm;
    /* SYSTEM.INI [386Enh] WinExclusive: while Windows has the focus no
     * other VM runs (Control Panel's "Exclusive in Foreground") */
    if (info->win_exclusive && vm_focus == &sysvm && runnable(&sysvm)) return &sysvm;
    /* a DOS VM whose PIF says Exclusive, full screen with the focus: the
     * others wait (UNVERIFIED in a window, where w16 does not apply it) */
    if (vm_focus != &sysvm && vm_focus->exclusive && runnable(vm_focus)) return vm_focus;
    do {
        v = v->next ? v->next : vm_list;
        if (scheduled(v) && !v->idle) return v;
    } while (v != cur_vm);
    if (runnable(cur_vm)) return cur_vm;
    for (v = vm_list; v; v = v->next)
        if (scheduled(v)) return v;
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

/* the switch's bookkeeping: execution times, the Call_When_Task_Switched
 * callbacks (EAX the old VM, EBX the new, the DDK) */
static void task_switch(struct vm *o, struct vm *n)
{
    struct vmcb *c;
    struct svcregs s;
    u32 now = vmm_system_time();
    o->exec_ms += now - o->run_since;
    n->run_since = now;
    status_of(o);
    status_of(n);
    for (c = ts_cbs; c; c = c->next) {
        memset(&s, 0, sizeof(s));
        s.eax = VMH(o);
        s.ebx = VMH(n);
        vmm_call_proc(c->proc, &s);
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
    task_switch(cur_vm, n);
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
    shell_vm_init(cur_vm);
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

/* ---- the critical section (Begin_Critical_Section and the rest, the
 * DDK): one owner, a claim count; the owner gets a priority boost while
 * it holds it (CRIT_BOOST, w16's value: above any device boost w16's
 * devices give), and another VM that claims it waits for the owner ---- */
#define CRIT_BOOST 0x100000
static struct vm *crit_owner;
static u32 crit_count;

static void crit_claim(u32 n)
{
    /* the owner runs until it releases (the claiming VM is taken up
     * here again when it is switched to) */
    while (crit_owner && crit_owner != cur_vm && runnable(crit_owner)) {
        struct vm *o = crit_owner;
        task_switch(cur_vm, o);
        vmm_switch(o);
    }
    if (!crit_owner || !runnable(crit_owner)) {
        if (crit_owner && crit_owner != cur_vm) crit_owner->exec_prio -= CRIT_BOOST;
        if (crit_owner != cur_vm) cur_vm->exec_prio += CRIT_BOOST;
        crit_owner = cur_vm;
        crit_count = 0;
    }
    crit_count += n;
}

static void crit_release(u32 n)
{
    struct vmcb *c;
    struct svcregs s;
    if (crit_owner != cur_vm || !n) return;
    if (n < crit_count) { crit_count -= n; return; }
    crit_count = 0;
    crit_owner = NULL;
    cur_vm->exec_prio -= CRIT_BOOST;
    resched = 1;
    /* the most recent Call_When_Not_Critical callback, removed as called */
    if ((c = notcrit_cbs) != NULL) {
        notcrit_cbs = c->next;
        memset(&s, 0, sizeof(s));
        s.ebx = VMH(cur_vm);
        s.edx = c->ref;
        s.ebp = (u32)cur_vm->client;
        vmm_call_proc(c->proc, &s);
        kfree(c);
    }
}

/* INT 2Fh AX=1681h/1682h from a VM: Begin/End Critical Section (the DDK:
 * no other VM runs until it ends it, but for hardware interrupts) */
void vm_crit(int begin)
{
    if (begin) crit_claim(1);
    else crit_release(1);
}

/* Get_Time_Slice_Priority's share: the VM's priority (the foreground one
 * with the focus) of the sum of the scheduled VMs' (Windows 3.1, VXT.386:
 * the System VM with the focus 50, each of two Background DOS VMs 25, at
 * 100/50 each) */
static u32 cpu_percent(struct vm *v)
{
    struct vm *w;
    u32 sum = 0;
    if (!scheduled(v)) return 0;
    if (vm_focus->exclusive && vm_focus != &sysvm) return v == vm_focus ? 100 : 0;
    for (w = vm_list; w; w = w->next)
        if (scheduled(w)) sum += w == vm_focus ? w->prio_fg : w->prio_bg;
    return sum ? (v == vm_focus ? v->prio_fg : v->prio_bg) * 100 / sum : 0;
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
    case 0x002F:                        /* Crash_Cur_VM: the current VM ends (SHELL's
                                         * "violated system integrity"); the
                                         * System VM's end is Windows' */
        if (cur_vm == &sysvm) panic("Crash_Cur_VM in the System VM (called from %x)", *(u32 *)(s + 1));
        trace("VMM: VM %d: Crash_Cur_VM (called from %x)\n", cur_vm->id, *(u32 *)(s + 1));
        cur_vm->ended = 1;
        shell_vm_crashed(cur_vm);
        resched = 1;
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
        vm_release();
        return 1;
    case 0x0039:                        /* Wake_Up_VM */
        if ((v = vm_of(s->ebx)) != NULL) vm_wake(v);
        return 1;
    case 0x001E:                        /* Adjust_Exec_Priority: EAX the boost, EBX the VM */
        if ((v = vm_of(s->ebx)) == NULL) return 1;
        v->exec_prio += (s32)s->eax;
        if (((s32)s->eax < 0 && v == cur_vm) || ((s32)s->eax > 0 && v != cur_vm && v->exec_prio > cur_vm->exec_prio))
            resched = 1;
        return 1;
    case 0x001F:                        /* Begin_Critical_Section: ECX flags */
        crit_claim(1);
        return 1;
    case 0x0022:                        /* Claim_Critical_Section: EAX claims, ECX flags */
        if (s->eax) crit_claim(s->eax);
        return 1;
    case 0x0020:                        /* End_Critical_Section */
        crit_release(1);
        return 1;
    case 0x0023:                        /* Release_Critical_Section: ECX claims */
        if (s->ecx) crit_release(s->ecx);
        return 1;
    case 0x0021:                        /* End_Crit_And_Suspend: carry if it could not suspend */
        crit_release(crit_owner == cur_vm ? crit_count : 0);
        if (cur_vm == &sysvm) { s->eflags |= EFL_CF; return 1; }
        if (!cur_vm->susp_count++ && !cur_vm->suspended) vxd_send(VM_SUSPEND, cur_vm, 0, 0, 0);
        resched = 1;
        return 1;
    case 0x0024:                        /* Call_When_Not_Critical: ESI, EDX ref */
        add_cb(&notcrit_cbs, s->esi, s->edx, 0);
        return 1;
    case 0x0029:                        /* Get_Crit_Section_Status */
    case 0x00F1:                        /* Get_Crit_Status_No_Block */
        s->ecx = crit_owner ? crit_count : 0;
        s->ebx = VMH(crit_owner ? crit_owner : cur_vm);
        if (cur_vm->exec_prio >= CRIT_BOOST) s->eflags |= EFL_CF;
        return 1;
    case 0x002A:                        /* Call_When_Task_Switched: ESI */
        add_cb(&ts_cbs, s->esi, 0, 1);
        return 1;
    case 0x0032:                        /* Get_Time_Slice_Priority: EBX the VM */
        if ((v = vm_of(s->ebx)) == NULL) { s->eflags |= EFL_CF; return 1; }
        status_all();
        s->eax = v->cb->status & VMSTAT_SETTABLE;
        s->ecx = v->prio_fg & 0xFFFF;
        s->edx = v->prio_bg & 0xFFFF;
        s->esi = cpu_percent(v);
        return 1;
    case 0x0033:                        /* Set_Time_Slice_Priority: EAX flags, EBX, ECX, EDX */
        if ((v = vm_of(s->ebx)) == NULL || !s->ecx || !s->edx || s->ecx > 10000 || s->edx > 10000) {
            s->eflags |= EFL_CF;
            return 1;
        }
        v->prio_fg = s->ecx;
        v->prio_bg = s->edx;
        v->exclusive = (s->eax & VMSTAT_EXCLUSIVE) != 0;
        if (v != &sysvm) v->background = (s->eax & VMSTAT_BACKGROUND) != 0;
        v->cb->status = (v->cb->status & ~VMSTAT_HIGH_PRI_BACK) | (s->eax & VMSTAT_HIGH_PRI_BACK);
        status_of(v);
        resched = 1;
        return 1;
    case 0x0034:                        /* Get_Time_Slice_Granularity: EAX ms */
        s->eax = info->min_slice ? info->min_slice : 20;
        return 1;
    case 0x0035:                        /* Set_Time_Slice_Granularity: EAX ms */
        if (s->eax) info->min_slice = (u16)(s->eax > 10000 ? 10000 : s->eax);
        return 1;
    case 0x0036: {                      /* Get_Time_Slice_Info: EAX scheduled, EBX current, ECX idle */
        u32 n = 0, i = 0;
        for (v = vm_list; v; v = v->next)
            if (scheduled(v)) { n++; if (v->idle) i++; }
        s->eax = n;
        s->ebx = VMH(cur_vm);
        s->ecx = i;
        return 1;
    }
    case 0x0037:                        /* Adjust_Execution_Time: EAX ms (signed), EBX */
        /* (a slice of ticks here: the milliseconds in whole ticks, at
         * least the tick it is in) */
        if ((v = vm_of(s->ebx)) == NULL || !scheduled(v)) return 1;
        if ((s32)s->eax > 0) v->slice += (s->eax + 54) / 55;
        else if (v->slice > (u32)(-(s32)s->eax) / 55 + 1) v->slice -= (u32)(-(s32)s->eax) / 55;
        else v->slice = 1;
        return 1;
    case 0x003A:                        /* Call_When_Idle: ESI */
        add_cb(&idle_cbs, s->esi, 0, 1);
        return 1;
    case 0x003F:                        /* Get_System_Time */
        s->eax = vmm_system_time();
        return 1;
    case 0x0040:                        /* Get_VM_Exec_Time: the current VM's */
        s->eax = cur_vm->exec_ms + (vmm_system_time() - cur_vm->run_since);
        return 1;
    case 0x00CF:                        /* Get_Last_Updated_System_Time */
        s->eax = vmm_last_time();
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
