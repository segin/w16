/* vmm.c - w16's Virtual Machine Manager: start-up, memory, the trap
 * dispatcher, the V86 monitor and leaving Windows (ARCHITECTURE.md 12.3).
 *
 * Stage 1 (what is here): one virtual machine, the System VM, running the
 * loader and the program it EXECs (KRNL386) in V86 mode with IOPL 3, and a
 * DPMI 0.9 host for that program (dpmi.c). No VxDs are loaded yet; the
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

/* ---- trace on a serial port (COM1; DOSBox-X: serial1=file file:...) ---- */
static void tchar(int c)
{
    int i;
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
    if (need > 4096) {
        u32 n = (need + 4095) / 4096, first = 0, i, p;
        for (i = 0; i < n; i++) {
            if (next_page + 4096 > info->xms_size) return NULL;
            p = info->xms_phys + next_page;
            next_page += 4096;
            nfree_pages--;
            if (!i) first = p;
        }
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
    u32 *b = (u32 *)p - 4, sz = b[0], cls = 0, s = 16;
    if (!p || sz > 4096) return;
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
struct execctx {
    u32 jb[6];
    struct execctx *prev;
    u32 saved_esp0;
    u16 saved_refsp;
    struct cregs res;
    struct cregs *v;
};
static struct execctx *cur_exec;
int exec_trace;

void exec_v86(struct cregs *v)
{
    /* after k_longjmp only memory (c, through EBP/ESP) is trustworthy:
     * scratch registers holding v or anything else are gone */
    volatile struct execctx c;
    c.v = v;
    c.prev = cur_exec;
    c.saved_esp0 = kesp0;
    c.saved_refsp = sysvm.refsp;
    if (!k_setjmp((u32 *)c.jb)) {
        cur_exec = (struct execctx *)&c;
        if ((u16)v->ss == info->refstk_seg && (u16)v->esp < sysvm.refsp)
            sysvm.refsp = (u16)v->esp;
        kesp0 = ((u32)&c - 512) & ~15u;
        tss_set_esp0(kesp0);
        sysvm.r = *v;
        if (exec_trace) trace("<%x %x:%x", v->eax & 0xFFFF, v->cs, v->eip);
        resume(&sysvm.r);
    }
    if (exec_trace) trace(">%x", c.res.eax & 0xFFFF);
    cur_exec = c.prev;
    kesp0 = c.saved_esp0;
    tss_set_esp0(kesp0);
    sysvm.refsp = c.saved_refsp;
    memcpy(c.v, (void *)&c.res, sizeof(struct cregs));
}

/* INT vec in V86 mode with registers *v (SS:SP 0: the reflection stack) */
void v86_int(struct cregs *v, int vec)
{
    u16 *ivt = (u16 *)0;
    if (!v->ss && !(v->esp & 0xFFFF)) {
        v->ss = info->refstk_seg;
        v->esp = sysvm.refsp;
    }
    v->eflags = (v->eflags & 0x0FD5) | EFL_VM | EFL_IOPL | 2;
    v86_push16(v, (u16)(v->eflags | EFL_IF));
    v86_push16(v, (u16)info->ldr_cs);
    v86_push16(v, (u16)(info->bp_off + W386_BP_REFRET));
    v->eip = ivt[vec * 2];
    v->cs = ivt[vec * 2 + 1];
    exec_v86(v);
}

/* a far call (iret: a far call to an IRET-ending routine) to v->cs:v->eip */
void v86_farcall(struct cregs *v, int iret)
{
    if (!v->ss && !(v->esp & 0xFFFF)) {
        v->ss = info->refstk_seg;
        v->esp = sysvm.refsp;
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
    case 0x1689:                        /* kernel idle (observed: AL=0) */
        SETLO(r->eax, 0);
        return 1;
    case 0x1681:                        /* begin/end critical section */
    case 0x1682:
        return 1;
    case 0x1683:                        /* current VM: the System VM */
        SETW(r->ebx, 1);
        SETLO(r->eax, 0);
        return 1;
    case 0x1684:                        /* V86 API entry: none (no VxDs yet) */
        SETW(r->edi, 0);
        r->es = 0;
        return 1;
    case 0x1687:                        /* DPMI: observed values (ARCHITECTURE.md 12.2) */
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
    info->exit_code = AX(r);
    info->ret_ss = (u16)r->ss;
    info->ret_sp = (u16)r->esp;
    pic_init(info->pic1_base, info->pic2_base);
    to_realmode(info->rm_tramp);
}

/* ---- the trap dispatcher ---- */
#define IRQ_BASE 0x50

static void software_int(struct cregs *r, int vec)
{
    if (r->eflags & EFL_VM) {
        if (vec == 0x2F && int2f_v86(r)) return;
        /* INT 1Ch, 23h, 24h go to a protected-mode handler (DPMI 0.9) */
        if ((vec == 0x1C || vec == 0x23 || vec == 0x24) && dpmi_v86_hook(r, vec, 0)) return;
        v86_reflect(r, vec);
    } else
        dpmi_int(r, vec, 0);
}

static void v86_breakpoint(struct cregs *r, u32 idx)
{
    switch (idx) {
    case W386_BP_EXIT:   exit_windows(r); break;
    case W386_BP_DPMI:   dpmi_switch(r); break;
    case W386_BP_REFRET:
        exec_return(r);
        if (0) {
            u16 *sp = (u16 *)v86_lin(r->ss, r->esp);
            panic("REFRET outside a nested call: %x:%x ss:sp %x:%x [%x %x %x %x] ax %x refsp %x",
                  r->cs, r->eip, r->ss, r->esp, sp[0], sp[1], sp[2], sp[3], r->eax, sysvm.refsp);
        }
        cur_exec->res = *r;
        k_longjmp(cur_exec->jb, 1);
        break;
    default:
        panic("unknown V86 breakpoint %d at %x:%x", idx, r->cs, r->eip);
    }
}

static u8 *code_ptr(struct cregs *r)
{
    if (r->eflags & EFL_VM) return (u8 *)v86_lin(r->cs, r->eip);
    return (u8 *)(sel_base(r->cs) + (sysvm.client32 ? r->eip : (r->eip & 0xFFFF)));
}

void __cdecl trap(struct tframe *f)
{
    /* the client's state is worked on in this frame's own copy: handlers
     * may run nested V86 code (exec_v86), whose traps use sysvm.r */
    struct cregs local;
    struct cregs *r = &local;
    u32 vec = f->vec;
    u8 *op;

    if (!(f->eflags & EFL_VM) && !(f->cs & 3)) {
        if (vec >= IRQ_BASE && vec < IRQ_BASE + 16) return;     /* not expected: IF=0 in the VMM */
        panic("exception %x err %x at %x:%x cr2 %x eax %x esi %x edi %x",
              vec, f->err, f->cs, f->eip, get_cr2(), f->eax, f->esi, f->edi);
    }
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
    if (vec >= IRQ_BASE && vec < IRQ_BASE + 16) {
        u32 irq = vec - IRQ_BASE;
        int v = irq < 8 ? 0x08 + irq : 0x70 + irq - 8;
        if (r->eflags & EFL_VM) {
            if (!dpmi_v86_hook(r, v, 1)) v86_reflect(r, v);
        } else
            dpmi_int(r, v, 1);
    } else if (vec == 13) {
        if (f->err & 2) {
            /* INT n through a DPL 0 gate: CD n, CC (INT 3) or CE (INTO) */
            op = code_ptr(r);
            r->eip += (op[0] == 0xCD) ? 2 : 1;
            software_int(r, f->err >> 3);
        } else {
            op = code_ptr(r);
            if (op[0] == 0xF4) {
                if (!(r->eflags & EFL_VM) && r->cs == SEL_PMBP)
                    dpmi_pmbp(r, r->eip);
                else
                    r->eip++;           /* HLT: carry on (idle) */
            } else if (r->eflags & EFL_VM)
                panic("V86 #GP at %x:%x op %x %x err %x", r->cs, r->eip, op[0], op[1], f->err);
            else
                dpmi_exception(r, 13, f->err);
        }
    } else if (vec == 6 && (r->eflags & EFL_VM)) {
        u32 lin = v86_lin(r->cs, r->eip);
        op = (u8 *)lin;
        if (op[0] == 0x63 && lin >= bp_lin && lin < bp_lin + info->bp_count)
            v86_breakpoint(r, lin - bp_lin);
        else
            v86_reflect(r, 6);
    } else if (vec < 32) {
        if (r->eflags & EFL_VM) {
            if (vec == 14) panic("V86 page fault at %x:%x cr2 %x", r->cs, r->eip, get_cr2());
            v86_reflect(r, vec);
        } else
            dpmi_exception(r, vec, f->err);
    } else
        software_int(r, vec);           /* through a DPL 3 gate */
    if (!(r->eflags & EFL_VM)) {
        /* catch a bad state before the processor does */
        u8 *d = (r->cs & 4) ? ldt + (r->cs & ~7u) : gdt + (r->cs & ~7u);
        if ((r->cs & 3) != 3 || !(d[5] & 0x80) || (d[5] & 0x18) != 0x18)
            panic("resuming PM at %x:%x with a bad CS (access %x) after vector %x err %x from %x:%x",
                  r->cs, r->eip, d[5], vec, f->err, f->cs, f->eip);
    }
    sysvm.r = local;
    resume(&sysvm.r);
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
    }
    { extern int dpmi_verbose; dpmi_verbose = exec_trace = info->trace; }
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
    pic_init(IRQ_BASE, IRQ_BASE + 8);
    dpmi_init();

    /* the System VM: the loader, back from pm_go, in V86 mode */
    memset(&sysvm.r, 0, sizeof(sysvm.r));
    sysvm.r.cs = info->v86_cs;
    sysvm.r.eip = info->v86_ip;
    sysvm.r.ss = info->v86_ss;
    sysvm.r.esp = info->v86_sp;
    sysvm.r.ds = info->v86_ds;
    sysvm.r.es = info->v86_es;
    sysvm.r.eflags = (info->v86_flags & 0xFFFF & ~EFL_NT) | EFL_VM | EFL_IOPL | EFL_IF | 2;
    sysvm.refsp = info->refstk_top;
    trace("VMM: System VM at %x:%x\n", sysvm.r.cs, sysvm.r.eip);
    resume(&sysvm.r);
}
