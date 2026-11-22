/* vmm.c - w16's Virtual Machine Manager: start-up, memory, the trap
 * dispatcher, the V86 monitor and leaving Windows (ARCHITECTURE.md 12.3).
 *
 * Stage 1 (what is here): one virtual machine, the System VM, running the
 * loader and the program it EXECs (KRNL386) in V86 mode with IOPL 3, and a
 * DPMI 0.9 host for that program (dpmi.c), whose protected-mode code runs
 * with IOPL 0 (pm_cli_sti). No VxDs are loaded yet; the
 * plan for them is in ARCHITECTURE.md 12.4.
 *
 * Sources: Intel 80386 Programmer's Reference Manual; 8259A data sheet
 * (initialisation words); Windows 3.1 DDK, INT 2Fh 1600h-1689h.
 */
#include "vmm.h"

struct w386_info *info;
struct vm sysvm;
u32 bp_lin;
u32 pmbp_lin;

/* ---- descriptor tables ---- */
u8 gdt[GDT_ENTRIES * 8];
static u8 idt[256 * 8];
#define LDT_MAX 8192                    /* the architecture's limit: 64 KB of descriptors */
static u8 ldt_store[LDT_MAX * 8];
u8 *ldt = ldt_store;
u32 ldt_limit = LDT_MAX * 8 - 1;

#pragma pack(1)
static struct {
    u32 link, esp0, ss0, esp1, ss1, esp2, ss2, cr3, eip, eflags;
    u32 eax, ecx, edx, ebx, esp, ebp, esi, edi;
    u32 es, cs, ss, ds, fs, gs, ldtr;
    u16 trap, iobase;
    u8 iomap[8192 + 1];                 /* every port allowed; FFh ends it */
} tss;
static struct { u16 limit; u32 base; } gdtr, idtr;
#pragma pack()

/* trap port (1) or let it through (0): its bit in the I/O bitmap */
void io_trap_bit(u32 port, int on)
{
    if (port > 0xFFFF) return;
    if (on) tss.iomap[port >> 3] |= (u8)(1 << (port & 7));
    else tss.iomap[port >> 3] &= (u8)~(1 << (port & 7));
}

static void tss_set_esp0(u32 v)
{
    tss.esp0 = v;
}

void set_desc(u8 *d, u32 base, u32 limit, u32 access, u32 flags)
{
    if (limit > 0xFFFFF) {
        limit >>= 12;
        flags |= 0x8;                   /* granularity: 4 KB */
    }
    d[0] = (u8)limit;
    d[1] = (u8)(limit >> 8);
    d[2] = (u8)base;
    d[3] = (u8)(base >> 8);
    d[4] = (u8)(base >> 16);
    d[5] = (u8)access;
    d[6] = (u8)(((limit >> 16) & 0x0F) | (flags << 4));
    d[7] = (u8)(base >> 24);
}

u32 desc_base(const u8 *d)
{
    return d[2] | (d[3] << 8) | (d[4] << 16) | ((u32)d[7] << 24);
}

u32 desc_limit(const u8 *d)
{
    u32 l = d[0] | (d[1] << 8) | ((d[6] & 0x0F) << 16);
    if (d[6] & 0x80) l = (l << 12) | 0xFFF;
    return l;
}

static void set_gate(int vec, u32 handler, int dpl)
{
    u8 *g = idt + vec * 8;
    g[0] = (u8)handler;
    g[1] = (u8)(handler >> 8);
    g[2] = (u8)SEL_CODE;
    g[3] = 0;
    g[4] = 0;
    g[5] = (u8)(0x8E | (dpl << 5));     /* present, 32-bit interrupt gate */
    g[6] = (u8)(handler >> 16);
    g[7] = (u8)(handler >> 24);
}

/* ---- small library ---- */
void memset(void *d, int c, u32 n)
{
    u8 *p = d;
    while (n--) *p++ = (u8)c;
}

void memcpy(void *d, const void *s, u32 n)
{
    u8 *p = d;
    const u8 *q = s;
    while (n--) *p++ = *q++;
}

int memcmp(const void *a, const void *b, u32 n)
{
    const u8 *p = a, *q = b;
    for (; n--; p++, q++)
        if (*p != *q) return *p < *q ? -1 : 1;
    return 0;
}

/* ---- trace on a serial port (COM1; DOSBox-X: serial1=file file:...) ---- */
static void pic_init(u32 b1, u32 b2);
static char *tbuf;                     /* fatal(): its message goes here */
static u32 tbuf_len;

static void tchar(int c)
{
    int i;
    if (tbuf) {
        if (tbuf_len < W386_FATAL_SIZE - 1) tbuf[tbuf_len++] = (char)c;
        tbuf[tbuf_len] = 0;
        return;
    }
    if (!info || !info->log_port) return;
    for (i = 0; i < 100000 && !(inb(info->log_port + 5) & 0x20); i++)
        ;
    outb(info->log_port, c);
}

static void tnum(u32 v, int base, int width, int pad)
{
    char buf[12];
    int n = 0;
    do {
        buf[n++] = "0123456789ABCDEF"[v % base];
        v /= base;
    } while (v);
    while (n < width--) tchar(pad);
    while (n) tchar(buf[--n]);
}

static void vtrace(const char *fmt, u32 *ap)
{
    for (; *fmt; fmt++) {
        int width = 0, pad = ' ';
        if (*fmt != '%') {
            if (*fmt == '\n') tchar('\r');
            tchar(*fmt);
            continue;
        }
        fmt++;
        if (!*fmt) break;
        if (*fmt == '0') pad = '0';
        while (*fmt >= '0' && *fmt <= '9') width = width * 10 + *fmt++ - '0';
        switch (*fmt) {
        case 'x': tnum(*ap++, 16, width, pad); break;
        case 'd': tnum(*ap++, 10, width, pad); break;
        case 'c': tchar(*ap++); break;
        case 's': { const char *s = (const char *)*ap++; while (*s) tchar(*s++); } break;
        default:  tchar(*fmt); break;
        }
    }
}

void trace(const char *fmt, ...)
{
    vtrace(fmt, (u32 *)&fmt + 1);
}

/* Windows ends with a message, as Windows 3.1's WIN386 does for an
 * invalid dynamic link (ARCHITECTURE.md 12.2): before the System VM has
 * run, back to the loader, which prints it and exits; later (UNVERIFIED
 * what Windows 3.1 does then) the VMM stops with it in the trace. */
static int vm_started;

void fatal(const char *fmt, ...)
{
    trace("VMM: fatal: ");
    vtrace(fmt, (u32 *)&fmt + 1);
    if (!vm_started && info->fatal_lin) {
        tbuf = (char *)info->fatal_lin;
        tbuf_len = 0;
        vtrace(fmt, (u32 *)&fmt + 1);
        tbuf = 0;
        info->exit_code = W386_FATAL;
        info->ret_ss = info->v86_ss;    /* pm_go's own frame */
        info->ret_sp = info->v86_sp;
        pic_init(info->pic1_base, info->pic2_base);
        to_realmode(info->rm_tramp);
    }
    for (;;) hlt();
}

void panic(const char *fmt, ...)
{
    trace("VMM panic: ");
    vtrace(fmt, (u32 *)&fmt + 1);
    trace("\n");
    for (;;) hlt();
}

/* ---- memory: the window (all of the XMS block) ---- */
/* Window offset w is at linear W386_VMM_LIN + w, physical xms_phys + w. */
static u32 next_page;                   /* window offset of the next never-used page */
static u32 free_list;                   /* freed pages (linked through their first word) */
static u32 nfree_pages;

u32 phys_to_lin(u32 phys)
{
    if (phys < 0x110000) return phys;   /* identity mapped */
    return W386_VMM_LIN + (phys - info->xms_phys);
}

u32 page_alloc(void)
{
    u32 phys;
    if (free_list) {
        phys = free_list;
        free_list = *(u32 *)phys_to_lin(phys);
    } else if (next_page + 4096 <= info->xms_size) {
        phys = info->xms_phys + next_page;
        next_page += 4096;
    } else
        return 0;
    nfree_pages--;
    memset((void *)phys_to_lin(phys), 0, 4096);
    return phys;
}

void page_free(u32 phys)
{
    *(u32 *)phys_to_lin(phys) = free_list;
    free_list = phys;
    nfree_pages++;
}

u32 pages_free(void)
{
    return nfree_pages;
}

static u32 *pdir(void)
{
    return (u32 *)W386_VMM_LIN;         /* window offset 0 */
}

static void flush_tlb(void)
{
    set_cr3(get_cr3());
}

/* Map one page; flags: 1 present, 2 writable, 4 user. */
int map_page(u32 lin, u32 phys, u32 flags)
{
    u32 *pd = pdir(), *pt;
    u32 pde = pd[lin >> 22];
    if (!(pde & 1)) {
        u32 p = page_alloc();
        if (!p) return 0;
        pde = p | (lin < W386_VMM_LIN ? 7 : 3);
        pd[lin >> 22] = pde;
    }
    pt = (u32 *)phys_to_lin(pde & ~0xFFFu);
    pt[(lin >> 12) & 0x3FF] = phys ? ((phys & ~0xFFFu) | flags) : 0;
    flush_tlb();
    return 1;
}

/* Let ring 3 reach VMM memory (the LDT, for the vendor API's alias). */
void make_user(u32 lin, u32 size)
{
    u32 a, *pd = pdir(), *pt;
    for (a = lin & ~0xFFFu; a < lin + size; a += 4096) {
        if (!(pd[a >> 22] & 1)) continue;
        pd[a >> 22] |= 4;
        pt = (u32 *)phys_to_lin(pd[a >> 22] & ~0xFFFu);
        pt[(a >> 12) & 0x3FF] |= 4;
    }
    flush_tlb();
}

/* Could ring 3 reach every byte of lin .. lin+len-1 (present, user, and
 * writable when write)? The VMM's own accesses to a client's memory go
 * through this first, so that a bad pointer fails the call instead of
 * faulting in ring 0 (where Windows 3.1 itself ends: probe VX11 runs P
 * and S, docs/notes/win386-audit-triage.md). */
int lin_user_ok(u32 lin, u32 len, int write)
{
    u32 a, end, pde, pte, need = write ? 7 : 5;
    if (!len) return 1;
    end = lin + len - 1;
    if (end < lin) return 0;                    /* wraps */
    for (a = lin & ~0xFFFu; ; a += 4096) {
        pde = pdir()[a >> 22];
        if ((pde & need) != need) return 0;
        pte = ((u32 *)phys_to_lin(pde & ~0xFFFu))[(a >> 12) & 0x3FF];
        if ((pte & need) != need) return 0;
        if (a >= (end & ~0xFFFu)) break;
    }
    return 1;
}

u32 lin_to_phys(u32 lin)
{
    u32 pde = pdir()[lin >> 22], pte;
    if (!(pde & 1)) return 0;
    pte = ((u32 *)phys_to_lin(pde & ~0xFFFu))[(lin >> 12) & 0x3FF];
    if (!(pte & 1)) return 0;
    return (pte & ~0xFFFu) | (lin & 0xFFF);
}

/* kmalloc: power-of-two size classes from 16 bytes to a page; larger
 * blocks are whole pages in the window (contiguous, never freed back to
 * the class lists). The block size sits in a 16-byte header. */
static u32 *klists[9];                  /* 16 .. 4096 */
static u32 kbump, kbump_end;

void *kmalloc(u32 size)
{
    u32 need = size + 16, cls = 0, sz = 16, *b;
    if (size > info->xms_size) return NULL;     /* (and no wrap below) */
    if (need > 4096) {
        u32 n = (need + 4095) / 4096, first;
        /* contiguous pages from the never-used end; all or none */
        if (n > (info->xms_size - next_page) / 4096) return NULL;
        first = info->xms_phys + next_page;
        next_page += n * 4096;
        nfree_pages -= n;
        b = (u32 *)phys_to_lin(first);
        memset(b, 0, n * 4096);
        b[0] = n * 4096;
        return b + 4;
    }
    while (sz < need) { sz <<= 1; cls++; }
    if (klists[cls]) {
        b = klists[cls];
        klists[cls] = (u32 *)b[1];
    } else {
        if (kbump + sz > kbump_end) {
            u32 p = page_alloc();
            if (!p) return NULL;
            kbump = phys_to_lin(p);
            kbump_end = kbump + 4096;
        }
        b = (u32 *)kbump;
        kbump += sz;
    }
    memset(b, 0, sz);
    b[0] = sz;
    return b + 4;
}

void kfree(void *p)
{
    u32 *b, sz, cls = 0, s = 16;
    if (!p) return;
    b = (u32 *)p - 4;
    sz = b[0];
    if (sz > 4096) {
        /* whole pages: each back to the free list */
        u32 i, phys = info->xms_phys + ((u32)b - W386_VMM_LIN);
        for (i = 0; i < sz / 4096; i++) page_free(phys + i * 4096);
        return;
    }
    while (s < sz) { s <<= 1; cls++; }
    b[1] = (u32)klists[cls];
    klists[cls] = b;
}

/* ---- V86 helpers ---- */
u32 v86_lin(u32 seg, u32 off)
{
    return ((seg & 0xFFFF) << 4) + (off & 0xFFFF);
}

void v86_push16(struct cregs *r, u16 v)
{
    u16 sp = (u16)(r->esp - 2);
    *(u16 *)v86_lin(r->ss, sp) = v;
    SETW(r->esp, sp);
}

u16 v86_pop16(struct cregs *r)
{
    u16 sp = (u16)r->esp;
    u16 v = *(u16 *)v86_lin(r->ss, sp);
    SETW(r->esp, sp + 2);
    return v;
}

/* An interrupt in V86 mode, as the processor does it in real mode. */
void v86_reflect(struct cregs *r, int vec)
{
    u16 *ivt = (u16 *)0;
    v86_push16(r, (u16)r->eflags);
    v86_push16(r, (u16)r->cs);
    v86_push16(r, (u16)r->eip);
    r->eflags &= ~(EFL_IF | EFL_TF);
    r->eip = ivt[vec * 2];
    r->cs = ivt[vec * 2 + 1];
}

/* ---- nested V86 execution ----
 * exec_v86 runs V86 code for the VMM (for an interrupt reflected from
 * protected mode, a DPMI 0300h call, a DOS call made for the client) and
 * returns when it reaches the REFRET breakpoint: the ring-0 stack in use
 * moves below this frame while the V86 code runs, so traps during it
 * (interrupts, nested calls) work as usual. Depth is limited only by the
 * ring-0 stack. */
static struct cregs *hw_r;              /* the VM's state VPICD delivers into (trap()) */

struct execctx {
    u32 jb[6];
    struct cregs *saved_hw_r;           /* trap() states end with longjmp: put back */
    struct execctx *prev;
    u32 saved_esp0;
    u16 saved_refsp;
    struct cregs res;
    struct cregs *v;
};
static struct execctx *cur_exec;
int exec_trace;
int vm_trace;                           /* [w16] vmmtrace=2: the DOS VMs' trace */

void exec_v86(struct cregs *v)
{
    /* after k_longjmp only memory (c, through EBP/ESP) is trustworthy:
     * scratch registers holding v or anything else are gone */
    volatile struct execctx c;
    c.v = v;
    c.prev = cur_exec;
    c.saved_esp0 = kesp0;
    c.saved_refsp = cur_vm->refsp;
    c.saved_hw_r = hw_r;
    if (!k_setjmp((u32 *)c.jb)) {
        cur_exec = (struct execctx *)&c;
        if ((u16)v->ss == cur_vm->refstk_seg && (u16)v->esp < cur_vm->refsp)
            cur_vm->refsp = (u16)v->esp;
        kesp0 = ((u32)&c - 512) & ~15u;
        tss_set_esp0(kesp0);
        cur_vm->r = *v;
        if (exec_trace) trace("<%x %x:%x", v->eax & 0xFFFF, v->cs, v->eip);
        resume(&cur_vm->r);
    }
    if (exec_trace) trace(">%x", c.res.eax & 0xFFFF);
    cur_exec = c.prev;
    hw_r = c.saved_hw_r;
    kesp0 = c.saved_esp0;
    tss_set_esp0(kesp0);
    cur_vm->refsp = c.saved_refsp;
    memcpy(c.v, (void *)&c.res, sizeof(struct cregs));
}

/* the reflection stack's lowest safe SP (W386_REFSTK_SIZE bytes below its top) */
static void refstk_check(void)
{
    if (cur_vm->refsp < cur_vm->refstk_top - (cur_vm == &sysvm ? W386_REFSTK_SIZE : DOSVM_REFSTK_SIZE) + 256)
        panic("the V86 reflection stack overflows (VM %d, sp %x, %d nested calls)", cur_vm->id, cur_vm->refsp, exec_depth());
}

/* INT vec in V86 mode with registers *v (SS:SP 0: the reflection stack) */
void v86_int(struct cregs *v, int vec)
{
    u16 *ivt = (u16 *)0;
    if (!v->ss && !(v->esp & 0xFFFF)) {
        refstk_check();
        v->ss = cur_vm->refstk_seg;
        v->esp = cur_vm->refsp;
    }
    /* as the processor's INT: the caller's flags in the frame, the
     * handler entered with IF and TF clear (Windows 3.1, VX11 "ifl": a
     * real-mode handler reached through 0300h or from protected mode
     * sees IF 0 whatever the caller's IF, and 0300h hands back the
     * structure's flags as the handler's IRET restored them) */
    v->eflags = (v->eflags & 0x0FD5) | EFL_VM | EFL_IOPL | 2;
    v86_push16(v, (u16)(v->eflags & ~EFL_IOPL));
    v86_push16(v, (u16)info->ldr_cs);
    v86_push16(v, (u16)(info->bp_off + W386_BP_REFRET));
    v->eflags &= ~(EFL_IF | EFL_TF);
    v->eip = ivt[vec * 2];
    v->cs = ivt[vec * 2 + 1];
    if (vec == 0x21 && vm_dos_call(v->eax)) {
        struct vm *m = cur_vm;          /* the same VM when it returns */
        m->indos++;
        exec_v86(v);
        if (m->indos > 0) m->indos--;
        return;
    }
    exec_v86(v);
}

/* a far call (iret: a far call to an IRET-ending routine) to v->cs:v->eip */
void v86_farcall(struct cregs *v, int iret)
{
    if (!v->ss && !(v->esp & 0xFFFF)) {
        refstk_check();
        v->ss = cur_vm->refstk_seg;
        v->esp = cur_vm->refsp;
    }
    v->eflags = (v->eflags & 0x0FD5) | EFL_VM | EFL_IOPL | EFL_IF | 2;
    if (iret) v86_push16(v, (u16)v->eflags);
    v86_push16(v, (u16)info->ldr_cs);
    v86_push16(v, (u16)(info->bp_off + W386_BP_REFRET));
    exec_v86(v);
}

int exec_depth(void)
{
    int n = 0;
    struct execctx *c;
    for (c = cur_exec; c; c = c->prev) n++;
    return n;
}

void exec_return(struct cregs *r)
{
    if (!cur_exec) panic("return breakpoint outside a nested call at %x:%x", r->cs, r->eip);
    cur_exec->res = *r;
    k_longjmp(cur_exec->jb, 1);
}

u32 xbuf_lin(void) { return (u32)info->xbuf_seg << 4; }
u32 xbuf_size(void) { return info->xbuf_size; }

/* ---- INT 2Fh in V86 mode: the Windows functions (DDK INT 2Fh 16xxh) ---- */
static int int2f_v86(struct cregs *r)
{
    switch (AX(r)) {
    case 0x1600:                        /* Enhanced mode installed: 3.10 */
        SETW(r->eax, 0x0A03);
        return 1;
    case 0x1680:                        /* release time slice: AL=0 */
        vm_release();
        SETLO(r->eax, 0);
        return 1;
    case 0x1689:                        /* kernel idle (observed: AL=0): the */
        SETLO(r->eax, 0);               /* System VM releases its time slice */
        if (cur_vm == &sysvm) vm_idle();
        return 1;
    case 0x1681:                        /* begin/end critical section (the DDK) */
    case 0x1682:
        vm_crit(AX(r) == 0x1681);
        return 1;
    case 0x1683:                        /* the current VM's ID (the System VM: 1) */
        SETW(r->ebx, cur_vm->id);
        SETLO(r->eax, 0);
        return 1;
    case 0x1684: {                      /* V86 API entry (vxdapi.c) */
        u32 idx = vxdapi_v86_get(BX(r));
        SETW(r->edi, idx ? info->bp_off + idx : 0);
        r->es = idx ? info->ldr_cs : 0;
        return 1;
    }
    case 0x1687:                        /* DPMI: observed values (ARCHITECTURE.md 12.2) */
        /* the System VM's only: a DOS VM has no DPMI host yet (UNVERIFIED
         * what Windows 3.1 offers there; its DOS boxes run DPMI programs) */
        if (cur_vm != &sysvm) return 0;
        SETW(r->eax, 0);
        SETW(r->ebx, 1);                /* 32-bit clients supported */
        SETLO(r->ecx, 3);               /* 386 */
        SETW(r->edx, 0x005A);           /* 0.90 */
        SETW(r->esi, 0);                /* no host data needed */
        r->es = info->ldr_cs;
        SETW(r->edi, info->bp_off + W386_BP_DPMI);
        return 1;
    }
    return 0;
}

/* ---- leaving Windows ---- */
static void pic_init(u32 b1, u32 b2)
{
    u32 m1 = inb(0x21), m2 = inb(0xA1);
    outb(0x20, 0x11); outb(0xA0, 0x11);     /* ICW1: edge, cascade, ICW4 */
    outb(0x21, b1);   outb(0xA1, b2);       /* ICW2: vector bases */
    outb(0x21, 4);    outb(0xA1, 2);        /* ICW3: slave on IRQ 2 */
    outb(0x21, 1);    outb(0xA1, 1);        /* ICW4: 8086 mode */
    outb(0x21, m1);   outb(0xA1, m2);
}

static void exit_windows(struct cregs *r)
{
    trace("VMM: exit, code %x\n", AX(r) & 0xFF);
    vxd_exit(r);
    info->exit_code = AX(r);
    info->ret_ss = (u16)r->ss;
    info->ret_sp = (u16)r->esp;
    if (!vpicd) pic_init(info->pic1_base, info->pic2_base);    /* else VPICD did, at Sys_Critical_Exit */
    to_realmode(info->rm_tramp);
}

/* ---- hardware interrupts: w16's VPICD module owns them (vxd.c finds
 * it, w16vxd.h); the VMM hands each one over and simulates into the VM
 * what VPICD asks for, where the VM was interrupted ---- */
#define IRQ_BASE VPICD_BASE1
struct vpicd_link *vpicd;

/* io.c: no interrupt is simulated while a device's I/O handler runs (the
 * VM's state is in the handler's client registers then); the trap's end
 * (VPICD's resume) delivers what it set */
struct cregs *vmm_hold_delivery(struct cregs *r)
{
    struct cregs *o = hw_r;
    hw_r = r;
    return o;
}

/* Build_Int_Stack_Frame (vxd.c) for a device called during a trap (a
 * time-out, a hot key): the VM takes an interrupt to sel:off (an interrupt
 * frame on its stack, TF cleared). The Reboot device's only use: KERNEL's
 * Ctrl+Alt+Del procedure, which tells from where the VM was interrupted
 * whether the current task responds. Observed under w16 (CS:IP only):
 * run where the VM was idle (KERNEL's idle loop, its HLT), the procedure
 * set the sanity byte every time; run elsewhere (another place in KERNEL,
 * an application, a driver's interrupt handler) it often did not, and
 * REBOOT showed "busy or unstable" in about half the plain-DOSBox runs.
 * So the frame waits for the VM's next idle HLT in protected mode
 * (vmm_idle_hlt); a second call for the same routine while it waits (the
 * VM has not idled: a task that does not respond) makes it at once, where
 * the VM is, if that is protected mode outside a hardware interrupt's
 * handler, else at the next such trap. UNVERIFIED how Windows 3.1 times
 * it. */
static u32 bint_sel, bint_off, bint_now;    /* a frame not yet made */

static void build_int_now(struct cregs *r)
{
    trace("VMM: Build_Int_Stack_Frame at %x:%x fl %x ss:sp %x:%x ds %x\n", r->cs, r->eip, r->eflags, r->ss, r->esp, r->ds);
    dpmi_build_int(r, bint_sel, bint_off);
    bint_sel = 0;
}

static int bint_ok(struct cregs *r)
{
    return bint_sel && cur_vm == &sysvm && sysvm.pm && !(r->eflags & EFL_VM) && !dpmi_in_handler();
}

int vmm_build_int(u32 sel, u32 off)
{
    struct cregs *r = hw_r;
    if (!r || cur_vm != &sysvm || !sysvm.pm) return 0;
    bint_now = bint_sel == sel && bint_off == off;
    bint_sel = sel;
    bint_off = off;
    if (bint_now && bint_ok(r)) build_int_now(r);
    return 1;
}

/* at the end of each trap: a frame due now */
static void vmm_build_int_pending(struct cregs *r)
{
    if (bint_now && bint_ok(r)) build_int_now(r);
}

/* the VM's HLT in protected mode (idle): a waiting frame is made here */
static void vmm_idle_hlt(struct cregs *r)
{
    if (bint_ok(r)) build_int_now(r);
}

int vmm_can_deliver(void)
{
    return hw_r && (hw_r->eflags & EFL_IF);
}

/* iret: VPICD wants to know when the handler returns. In protected mode,
 * and for a protected-mode handler of an interrupt in V86 mode, the
 * handler runs to its return before this returns; a V86 handler gets a
 * second frame above the interrupt's own, whose IRET reaches the IRQIRET
 * breakpoint (v86_breakpoint), which then does the first frame's IRET. */
int vmm_deliver(u32 vec, int iret)
{
    struct cregs *r = hw_r;
    if (vm_trace && cur_vm != &sysvm) trace("(I%x%s)", vec, r && (r->eflags & EFL_IF) ? "" : "-");
    if (!r || !(r->eflags & EFL_IF)) return 0;
    if (r->eflags & EFL_VM) {
        if (cur_vm == &sysvm && dpmi_v86_hook(r, vec, 1)) {
            if (iret) vpicd->iret();
        } else {
            v86_reflect(r, vec);
            if (iret) {
                v86_push16(r, (u16)r->eflags);
                v86_push16(r, (u16)info->ldr_cs);
                v86_push16(r, (u16)(info->bp_off + W386_BP_IRQIRET));
            }
        }
    } else {
        dpmi_int(r, vec, 1);
        if (iret) vpicd->iret();
    }
    return 1;
}

/* ---- switching VMs (vm.c decides when). Each VM has a ring-0 stack of
 * its own: a VM's traps, the nested V86 calls the VMM makes in it and the
 * devices' procedures run there, so a VM can be left at the end of any
 * trap, however deep, and taken up again later where it was. The first
 * megabyte is the VM's own page table (linear 0-4 MB), and its instance
 * data (vm.c) is copied in. ---- */
void vm_inst_out(struct vm *v);
void vm_inst_in(struct vm *v);
void io_vm_switch(struct vm *v);

void vmm_switch(struct vm *n)
{
    struct vm *o = cur_vm;
    o->kesp0 = kesp0;
    o->hw_r = hw_r;
    o->exec = cur_exec;
    if (vm_trace) trace("{%d>%d}", o->id, n->id);
    if (k_setjmp(o->kjb)) return;       /* back in o: its globals are in place */
    vm_inst_out(o);
    cur_vm = n;
    pdir()[0] = n->ptab_phys | 7;
    flush_tlb();
    vm_inst_in(n);
    io_vm_switch(n);
    kesp0 = n->kesp0;
    tss_set_esp0(kesp0);
    hw_r = n->hw_r;
    cur_exec = n->exec;
    if (n->started) k_longjmp(n->kjb, 1);
    n->started = 1;
    resume(&n->r);                      /* a new VM's first instruction */
}

/* the trap's state for VPICD while vm.c delivers what came for a VM
 * that was switched out */
struct cregs *vmm_trap_state(struct cregs *r)
{
    struct cregs *o = hw_r;
    hw_r = r;
    return o;
}

u32 vmm_pdir_entry_get(u32 i)
{
    return pdir()[i];
}

u32 vmm_pdir_entry(u32 i, u32 v)        /* set a page directory entry; the old one */
{
    u32 o = pdir()[i];
    pdir()[i] = v;
    flush_tlb();
    return o;
}

/* ---- the trap dispatcher ---- */


/* traps since the last heartbeat, by kind (the heartbeat lists them):
 * 0-255 software interrupts by vector (V86 "v", PM "p" below), and */
#define TK_CLISTI   512                 /* PM CLI/STI (IOPL 0) */
#define TK_HLT      513                 /* HLT outside the breakpoints */
#define TK_PMBP     514                 /* PM breakpoints (HLT at SEL_PMBP) */
#define TK_IO       515                 /* trapped ports */
#define TK_V86BP    516                 /* V86 breakpoints (ARPL) */
#define TK_EXC      517                 /* other exceptions */
#define TK_N        518
static u32 tk_count[TK_N];

static void software_int(struct cregs *r, int vec)
{
    tk_count[(vec & 0xFF) + ((r->eflags & EFL_VM) ? 0 : 256)]++;
    if (vm_trace && cur_vm != &sysvm) trace("[%d:%x/%x %x:%x]", cur_vm->id, vec, r->eax & 0xFFFF, r->cs, r->eip);
    if ((r->eflags & EFL_VM) && vxd_v86_int_hooks(r, vec)) return;  /* a device serviced it */
    if (r->eflags & EFL_VM) {
        if (vec == 0x2F && int2f_v86(r)) return;
        if (vec == 0x2A) vm_int2a(r);   /* DOS's critical sections: noted, then on to DOS's handler */
        if (vec == 0x21) { vm_int21(r); return; }
        /* INT 1Ch, 23h, 24h go to a protected-mode handler (DPMI 0.9) */
        if ((vec == 0x1C || vec == 0x23 || vec == 0x24) && cur_vm == &sysvm && dpmi_v86_hook(r, vec, 0)) return;
        v86_reflect(r, vec);
    } else
        dpmi_int(r, vec, 0);
}

static void v86_breakpoint(struct cregs *r, u32 idx)
{
    switch (idx) {
    case W386_BP_EXIT:   exit_windows(r); break;
    case W386_BP_DPMI:   dpmi_switch(r); break;
    case W386_BP_PS2CB:  dpmi_ps2cb(r); break;
    case W386_BP_VMEXIT: vm_exited(r); break;
    case W386_BP_VMSTART: vm_start(r); break;
    case W386_BP_INT21RET: vm_int21_ret(r); break;
    case W386_BP_IRQIRET:               /* the interrupted code's own frame */
        r->eip = v86_pop16(r);
        r->cs = v86_pop16(r);
        r->eflags = (r->eflags & 0xFFFF0000u) | v86_pop16(r) | EFL_IOPL;
        vpicd->iret();
        break;
    case W386_BP_REFRET:
        exec_return(r);
        if (0) {
            u16 *sp = (u16 *)v86_lin(r->ss, r->esp);
            panic("REFRET outside a nested call: %x:%x ss:sp %x:%x [%x %x %x %x] ax %x refsp %x",
                  r->cs, r->eip, r->ss, r->esp, sp[0], sp[1], sp[2], sp[3], r->eax, cur_vm->refsp);
        }
        cur_exec->res = *r;
        k_longjmp(cur_exec->jb, 1);
        break;
    default:
        if (idx >= W386_BP_V86API && idx < W386_BP_V86API + W386_NV86API) {
            u32 ip = v86_pop16(r), cs = v86_pop16(r);   /* the far call's return */
            r->eip = ip;
            r->cs = cs;
            vxdapi_v86_call(r, idx);
            break;
        }
        panic("unknown V86 breakpoint %d at %x:%x", idx, r->cs, r->eip);
    }
}

/* CLI and STI from protected mode, where the client runs with IOPL 0 as
 * under Windows 3.1 (VX6, ARCHITECTURE.md 12.3): they raise #GP and the
 * VMM sets the client's IF, which is the VM's interrupt flag: PUSHF shows
 * it, POPF and IRET cannot change it (the processor ignores IF there at
 * CPL > IOPL), hardware interrupts reach the VM only while it is set
 * (vmm_deliver), and a request VPICD holds is delivered at the next STI.
 * V86 code keeps IOPL 3 (VX6: Windows 3.1's V86 flags are 3287h, and a
 * V86 CLI clears the real IF). Prefixes before the opcode are skipped. */
static int pm_cli_sti(struct cregs *r, u8 *op)
{
    u32 n = 0;
    while (n < 14 && (op[n] == 0x66 || op[n] == 0x67 || op[n] == 0x26 || op[n] == 0x2E
                      || op[n] == 0x36 || op[n] == 0x3E || op[n] == 0x64 || op[n] == 0x65
                      || op[n] == 0xF2 || op[n] == 0xF3))
        n++;
    if (op[n] == 0xFA) r->eflags &= ~EFL_IF;
    else if (op[n] == 0xFB) r->eflags |= EFL_IF;
    else return 0;
    r->eip = seg_32(r->cs) ? r->eip + n + 1 : ((r->eip & 0xFFFF0000u) | (u16)(r->eip + n + 1));
    return 1;
}

/* The instruction at CS:EIP, or NULL when its first 15 bytes (the most
 * an instruction has) are not all readable by the client: CS's own
 * D bit gives the offset's size (a 16-bit client can run 32-bit code
 * segments: Win32s, Watcom's extender for Windows) */
static u8 *code_ptr(struct cregs *r)
{
    u32 lin;
    if (r->eflags & EFL_VM) return (u8 *)v86_lin(r->cs, r->eip);
    lin = sel_base(r->cs) + (seg_32(r->cs) ? r->eip : (r->eip & 0xFFFF));
    return lin_user_ok(lin, 15, 0) ? (u8 *)lin : NULL;
}

/* vmmasm.asm's #GP fast path: may an STI that enables the client's
 * interrupts go on without the trap's work (nothing for VPICD to deliver
 * to the VM, no Build_Int_Stack_Frame frame waiting)? */
int __cdecl sti_ok(void)
{
    return vpicd && vpicd->pending && !vpicd->pending() && !bint_sel;
}

static int cli_sti_fast(struct tframe *f)
{
    u32 cs = f->cs & 0xFFFF, lin, big = seg_32(cs);
    u8 op;
    lin = sel_base(cs) + (big ? f->eip : (f->eip & 0xFFFF));
    if (!lin_user_ok(lin, 1, 0)) return 0;
    op = *(u8 *)lin;
    if (op == 0xFA) f->eflags &= ~EFL_IF;
    else if (op == 0xFB) {
        if (!(f->eflags & EFL_IF) && (!vpicd || !vpicd->pending || vpicd->pending() || bint_sel)) return 0;
        f->eflags |= EFL_IF;
    } else return 0;
    f->eip = big ? f->eip + 1 : ((f->eip & 0xFFFF0000u) | (u16)(f->eip + 1));
    tk_count[TK_CLISTI]++;
    return 1;
}

/* A heartbeat in the trace every 20 s of timer ticks (BIOS rate): where the
 * System VM is, how deep the nested calls go and how many of each
 * hardware interrupt came, so that a hang shows where it is. */
static u32 irq_count[16], hb_ticks, hb_n;

static void heartbeat(struct cregs *r)
{
    int i;
    hb_ticks = 0;
    trace("VMM: heartbeat %d in VM %d at %s %x:%x fl %x, depth %d, PM last %x:%x, irqs",
          ++hb_n, cur_vm->id, (r->eflags & EFL_VM) ? "V86" : "PM", r->cs, r->eip, r->eflags,
          exec_depth(), sysvm.last_cs, sysvm.last_ip);
    for (i = 0; i < 16; i++) if (irq_count[i]) trace(" %d:%d", i, irq_count[i]);
    trace(", PIC masks %x %x, trapped I/O %d, traps", inb(0x21), inb(0xA1), io_traps);
    for (i = 0; i < TK_N; i++)
        if (tk_count[i]) {
            if (i < 512) trace(" %c%x:%d", i < 256 ? 'v' : 'p', i & 0xFF, tk_count[i]);
            else trace(" %s:%d", i == TK_CLISTI ? "clisti" : i == TK_HLT ? "hlt" : i == TK_PMBP ? "pmbp"
                       : i == TK_IO ? "io" : i == TK_V86BP ? "v86bp" : "exc", tk_count[i]);
            tk_count[i] = 0;
        }
    if (r->eflags & EFL_VM) {
        /* registers and stack words only: never code bytes (ARCHITECTURE.md 3) */
        u16 *sp = (u16 *)v86_lin(r->ss, r->esp);
        trace(", ss:sp %x:%x", r->ss, r->esp & 0xFFFF);
        for (i = 0; i < 8; i++) trace(" %04x", sp[i]);
        trace(", ax %x bx %x cx %x dx %x ds %x es %x", r->eax, r->ebx, r->ecx, r->edx, r->ds, r->es);
    }
    trace("\n");
}

void __cdecl trap(struct tframe *f)
{
    /* the client's state is worked on in this frame's own copy: handlers
     * may run nested V86 code (exec_v86), whose traps use sysvm.r */
    struct cregs local;
    struct cregs *r = &local, *saved_hw_r;
    u32 vec = f->vec;
    u8 *op;

    if (!(f->eflags & EFL_VM) && !(f->cs & 3)) {
        if (vec >= IRQ_BASE && vec < IRQ_BASE + 16) {
            /* an outside VxD's own code with interrupts on (its STI: the
             * VMM's and w16's devices' code never runs so, their entries
             * clear IF): the interrupt is VPICD's now, as under Windows
             * 3.1, whose VMM takes hardware interrupts whenever IF is set
             * (the DDK). Before, it was dropped without an EOI, and the
             * 8259 kept that IRQ and every lower one from then on */
            if (vpicd) {
                irq_count[vec - IRQ_BASE]++;
                vpicd->hw_int(vec - IRQ_BASE);
            }
            return;
        }
        if (vec == 0x20) { vxd_link(f); return; }               /* a VxD's dynamic link */
        panic("exception %x err %x at %x:%x cr2 %x eax %x esi %x edi %x",
              vec, f->err, f->cs, f->eip, get_cr2(), f->eax, f->esi, f->edi);
    }
    /* CLI and STI from a protected-mode client (IOPL 0, 12.3), the
     * commonest trap of all (some 9,000 a second while Windows passes
     * messages): done here, without the rest of a trap's work, when
     * nothing can follow from it: CLI always; STI when interrupts were on
     * already or VPICD has nothing the VM would take and no frame waits
     * (Build_Int_Stack_Frame). A VM switch the scheduler wants then waits
     * for the next trap (the next tick at the latest). Before, each one
     * cost the whole trap path, and Windows passed a tenth of the messages
     * a second it passes under Windows 3.1's WIN386 (VX13, ARCHITECTURE.md
     * 12.12) */
    if (vec == 13 && !f->err && (f->cs & 3) == 3 && !(f->eflags & EFL_VM) && cli_sti_fast(f)) return;
    r->edi = f->edi; r->esi = f->esi; r->ebp = f->ebp; r->ebx = f->ebx;
    r->edx = f->edx; r->ecx = f->ecx; r->eax = f->eax;
    r->eip = f->eip; r->eflags = f->eflags; r->esp = f->esp;
    r->cs = f->cs & 0xFFFF; r->ss = f->ss & 0xFFFF;
    if (f->eflags & EFL_VM) {
        r->ds = f->v86ds & 0xFFFF; r->es = f->v86es & 0xFFFF;
        r->fs = f->v86fs & 0xFFFF; r->gs = f->v86gs & 0xFFFF;
    } else {
        r->ds = f->ds & 0xFFFF; r->es = f->es & 0xFFFF;
        r->fs = f->fs & 0xFFFF; r->gs = f->gs & 0xFFFF;
    }

    if (!(r->eflags & EFL_VM) && r->cs != SEL_PMBP) {
        sysvm.last_cs = r->cs;
        sysvm.last_ip = r->eip;
        sysvm.last_ds = r->ds;
    }
    /* also at the breakpoints: a handler that chains with PUSHF, CALL FAR
     * has its own words below the frame */
    if (!(r->eflags & EFL_VM)) dpmi_note_pm(r);
    /* V86 code running on the reflection stack (a reflected interrupt, a
     * DOS or BIOS call made for the client): an interrupt that is then
     * reflected again must build its frame below where that code is, not
     * at the stack's start for this nesting level (the same as the host
     * stack, dpmi.c). Before, the new frame overwrote the interrupted
     * code's stack now and then: V86 code resumed at a garbage CS:IP, an
     * invalid opcode reflected to the BIOS for ever (seen in about 1 in 7
     * boots under load, while fonts loaded). */
    if ((r->eflags & EFL_VM) && (u16)r->ss == cur_vm->refstk_seg && (u16)r->esp < cur_vm->refsp)
        cur_vm->refsp = (u16)r->esp;
    /* the state VPICD simulates interrupts into, for the whole trap: a
     * device called during it (an I/O handler, a hot key) may set a
     * request, which must go to this state (taken now if it has
     * interrupts on, else at its STI), never to an outer one */
    saved_hw_r = hw_r;
    hw_r = r;
    if (vec >= IRQ_BASE && vec < IRQ_BASE + 16) {
        u32 irq = vec - IRQ_BASE;
        irq_count[irq]++;
        if (vm_trace && irq == 1) trace("(K%d)", cur_vm->id);
        if (irq == 0 && ++hb_ticks >= 182 * 2) heartbeat(r);
        vpicd->hw_int(irq);
    } else if (vec == 13 && !(r->eflags & EFL_VM) && (op = code_ptr(r)) == NULL) {
        dpmi_exception(r, 13, f->err);  /* code the VMM cannot read: the client's fault */
    } else if (vec == 13) {
        if (f->err & 2) {
            /* INT n through a DPL 0 gate: CD n, CC (INT 3) or CE (INTO) */
            op = code_ptr(r);
            r->eip += (op[0] == 0xCD) ? 2 : 1;
            software_int(r, f->err >> 3);
        } else {
            op = code_ptr(r);
            if (op[0] == 0xF4) {
                if (!(r->eflags & EFL_VM) && r->cs == SEL_PMBP) {
                    tk_count[TK_PMBP]++;
                    dpmi_pmbp(r, r->eip);
                } else {
                    tk_count[TK_HLT]++;
                    r->eip++;           /* HLT: carry on (idle) */
                    if (!(r->eflags & EFL_VM)) vmm_idle_hlt(r);
                    /* another VM may run meanwhile. Windows 3.1 does not
                     * give up a VM's slice at a HLT in V86 mode: DOSBox-X's
                     * shell, which halts between its keyboard polls, got
                     * its full share there (VX13, ARCHITECTURE.md 12.12);
                     * w16 does, but with [w16] V86HltYield=0 */
                    if (!(r->eflags & EFL_VM) || !info->hlt_stock) vm_idle();
                }
            } else if (!f->err && io_trap(r, op))
                tk_count[TK_IO]++;      /* a trapped port */
            else if ((r->eflags & EFL_VM) && cur_vm != &sysvm)
                vm_crashed(r);          /* a DOS VM's: that VM ends (vm.c) */
            else if (r->eflags & EFL_VM)
                panic("V86 #GP at %x:%x err %x", r->cs, r->eip, f->err);
            else if (!f->err && pm_cli_sti(r, op))
                tk_count[TK_CLISTI]++;
            else
                { tk_count[TK_EXC]++; dpmi_exception(r, 13, f->err); }
        }
    } else if (vec == 6 && (r->eflags & EFL_VM)) {
        u32 lin = v86_lin(r->cs, r->eip);
        op = (u8 *)lin;
        if (op[0] == 0x63 && lin >= bp_lin && lin < bp_lin + info->bp_count) {
            tk_count[TK_V86BP]++;
            v86_breakpoint(r, lin - bp_lin);
        }
        else
            v86_reflect(r, 6);
    } else if (vec < 32) {
        if (r->eflags & EFL_VM) {
            if (vec == 14 && cur_vm != &sysvm) vm_crashed(r);
            else if (vec == 14) panic("V86 page fault at %x:%x cr2 %x", r->cs, r->eip, get_cr2());
            else
            v86_reflect(r, vec);
        } else
            { tk_count[TK_EXC]++; dpmi_exception(r, vec, f->err); }
    } else
        software_int(r, vec);           /* through a DPL 3 gate */
    if (!(r->eflags & EFL_VM)) {
        /* catch a bad state before the processor does (a GDT index past
         * the table: its first descriptor, the null one, fails the test) */
        u8 *d = (r->cs & 4) ? ldt + (r->cs & ~7u)
              : ((r->cs >> 3) < GDT_ENTRIES ? gdt + (r->cs & ~7u) : gdt);
        if ((r->cs & 3) != 3 || !(d[5] & 0x80) || (d[5] & 0x18) != 0x18
            || (!(d[5] & 4) && ((d[5] >> 5) & 3) != 3))
            panic("resuming PM at %x:%x with a bad CS (access %x) after vector %x err %x from %x:%x",
                  r->cs, r->eip, d[5], vec, f->err, f->cs, f->eip);
    }
    vmm_build_int_pending(r);
    if (vpicd) vpicd->resume();         /* virtual interrupts VPICD has pending */
    hw_r = saved_hw_r;
    vm_end_trap(&local);
    cur_vm->r = local;
    resume(&cur_vm->r);
}

/* ---- start-up ---- */
void __cdecl vmm_main(struct w386_info *inf)
{
    int i;

    info = inf;
    if (info->log_port) {
        outb(info->log_port + 3, 0x80);
        outb(info->log_port + 0, 1);    /* 115200 */
        outb(info->log_port + 1, 0);
        outb(info->log_port + 3, 0x03);
        outb(info->log_port + 1, 0);
        outb(info->log_port + 4, 0x03);  /* DTR, RTS: DOSBox's nullmodem sends then */
    }
    { extern int dpmi_verbose; dpmi_verbose = exec_trace = info->trace == 1; }
    vm_trace = info->trace == 2;
    trace("w16 VMM: %d KB at %x, image %x+%x\n", info->xms_size >> 10, info->xms_phys,
          info->img_off, info->img_size);
    next_page = info->img_off + info->img_size;
    nfree_pages = (info->xms_size - next_page) / 4096;
    bp_lin = v86_lin(info->ldr_cs, info->bp_off);
    pmbp_lin = v86_lin(info->ldr_ds, info->pmbp_off);

    /* GDT */
    memset(gdt, 0, sizeof(gdt));
    set_desc(gdt + SEL_CODE, 0, 0xFFFFFFFF, 0x9A, 0x4);
    set_desc(gdt + SEL_DATA, 0, 0xFFFFFFFF, 0x92, 0x4);
    set_desc(gdt + SEL_TSS, (u32)&tss, sizeof(tss) - 1, 0x89, 0);
    set_desc(gdt + SEL_RMCODE, (u32)info->ldr_cs << 4, 0xFFFF, 0x9A, 0);
    set_desc(gdt + SEL_RMDATA, 0, 0xFFFF, 0x92, 0);
    set_desc(gdt + SEL_LDT, (u32)ldt, ldt_limit, 0x82, 0);
    set_desc(gdt + (SEL_PMBP & ~7), pmbp_lin, info->pmbp_count - 1, 0xFA, 0);
    set_desc(gdt + (SEL_FLAT3 & ~7), 0, 0xFFFFFFFF, 0xF2, 0x4);
    set_desc(gdt + (SEL_FLAT7B & ~7), 0, 0xFFFFFFFF, 0xF2, 0x4);   /* SHELL's (Windows 3.1: 7Bh) */
    gdtr.limit = sizeof(gdt) - 1;
    gdtr.base = (u32)gdt;
    lgdt(&gdtr);
    /* IDT: DPL 0 for the exceptions and the hardware interrupts (an INT n
     * there from V86 mode or ring 3 then raises #GP and is told apart),
     * DPL 3 for the rest */
    for (i = 0; i < 256; i++)
        set_gate(i, trap_stubs[i], (i < 32 || (i >= IRQ_BASE && i < IRQ_BASE + 16)) ? 0 : 3);
    idtr.limit = sizeof(idt) - 1;
    idtr.base = (u32)idt;
    lidt(&idtr);
    /* TSS: the ring-0 stack, every I/O port allowed */
    memset(&tss, 0, sizeof(tss));
    tss.esp0 = (u32)kstack_top;
    kesp0 = tss.esp0;
    tss.ss0 = SEL_DATA;
    tss.iobase = (u16)((u8 *)tss.iomap - (u8 *)&tss);
    tss.iomap[8192] = 0xFF;
    ltr(SEL_TSS);
    memset(ldt, 0, LDT_MAX * 8);
    lldt(SEL_LDT);
    /* the 8259s are VPICD's (it moves them to 50h/58h at Sys_Critical_Init) */
    vm_sys_init();
    dpmi_init();
    vxd_init();
    vm_snapshot();

    /* the System VM: the loader, back from pm_go, in V86 mode */
    memset(&sysvm.r, 0, sizeof(sysvm.r));
    sysvm.r.cs = info->v86_cs;
    sysvm.r.eip = info->v86_ip;
    sysvm.r.ss = info->v86_ss;
    sysvm.r.esp = info->v86_sp;
    sysvm.r.ds = info->v86_ds;
    sysvm.r.es = info->v86_es;
    sysvm.r.eflags = (info->v86_flags & 0xFFFF & ~EFL_NT) | EFL_VM | EFL_IOPL | EFL_IF | 2;
    trace("VMM: System VM at %x:%x\n", sysvm.r.cs, sysvm.r.eip);
    vm_started = 1;
    resume(&sysvm.r);
}
