/* io.c - w16 VMM: I/O port trapping (ARCHITECTURE.md 12.3). Devices
 * install a handler per port (Install_IO_Handler, Install_Mult_IO_
 * Handlers); the port's bit in the TSS's I/O bitmap makes an IN, OUT, INS
 * or OUTS to it from V86 mode or from a protected-mode client (IOPL 0)
 * raise #GP; the instruction is decoded and the handler called; then the
 * VM goes on after the instruction.
 *
 * Sources: the Windows 3.1 DDK's Virtual Device Adaptation Guide
 * (.tools/docs/vdag31: Install_IO_Handler, Install_Mult_IO_Handlers and
 * the VxD_IO table, Simulate_IO, Enable/Disable_Global/Local_Trapping, the
 * handler's registers: EBX the VM, ECX the type, EDX the port, EBP the
 * client registers, EAX the data); the Intel 80386 manual (the I/O
 * permission bitmap, the instructions). What Windows 3.1 does, observed
 * with VXI.386 and VX7 (tests/vxobs.sh io): the types are Byte_Input 0,
 * Byte_Output 4, Word_Input 8, Word_Output 0Ch, Dword_Input 10h,
 * Dword_Output 14h, String_IO 20h and Rep_IO 40h added for INS/OUTS with
 * the segment (V86) or selector (PM) of the string in the high word (ES
 * for INS, DS for OUTS), Client_ECX the count and Client_EIP already
 * past the instruction; IN AL,imm8 gives the same as IN AL,DX; a second handler for a
 * port fails (carry); Simulate_IO given a string calls the port's handler
 * once per element with the element's plain type, Client_ECX already 0,
 * and advances DI or SI. UNVERIFIED: Addr_32_IO (80h here) and Reverse_IO
 * (100h here), and what Simulate_IO does with a plain word or dword (here:
 * byte by byte, each byte port's handler or the hardware) and with a
 * plain byte (here: the hardware).
 */
#include "vmm.h"

#define IO_BYTE     0x00
#define IO_OUTPUT   0x04
#define IO_WORD     0x08
#define IO_DWORD    0x10
#define IO_STRING   0x20
#define IO_REP      0x40
#define IO_ADDR32   0x80                /* UNVERIFIED value */
#define IO_REVERSE  0x100               /* UNVERIFIED value */
#define IO_SIZE     0x18

/* a port's handler; trapping is on (global) unless a VM is on its list
 * of local exceptions (Disable_Local_Trapping) */
struct loc { struct loc *next; u32 vm; };
struct ioh { u32 port, proc; int on; struct loc *off; };
static struct ioh *iohs;
static u32 n_iohs, max_iohs;

static struct ioh *find(u32 port)
{
    u32 i;
    for (i = 0; i < n_iohs; i++)
        if (iohs[i].port == port) return &iohs[i];
    return NULL;
}

static int install(u32 port, u32 proc)
{
    if (port > 0xFFFF || find(port)) return 0;
    if (n_iohs == max_iohs) {           /* no fixed limit (ZOI): grow */
        u32 n = max_iohs ? max_iohs * 2 : 32;
        struct ioh *t = kmalloc(n * sizeof(*t));
        if (!t) return 0;
        if (iohs) { memcpy(t, iohs, n_iohs * sizeof(*t)); kfree(iohs); }
        iohs = t;
        max_iohs = n;
    }
    iohs[n_iohs].port = port;
    iohs[n_iohs].proc = proc;
    iohs[n_iohs].on = 1;
    iohs[n_iohs].off = NULL;
    n_iohs++;
    io_trap_bit(port, 1);
    return 1;
}

/* is the port trapped in VM vm (a handle) */
static int trapped(struct ioh *h, u32 vm)
{
    struct loc *l;
    if (!h->on) return 0;
    for (l = h->off; l; l = l->next)
        if (l->vm == vm) return 0;
    return 1;
}

static void set_trapping(u32 port, int on)
{
    struct ioh *h = find(port);
    if (!h) return;                     /* the DDK: only for an installed port */
    h->on = on;
    io_trap_bit(port, trapped(h, vm_handle()));
}

/* Enable/Disable_Local_Trapping: for VM vm only */
static void set_local(u32 port, u32 vm, int on)
{
    struct ioh *h = find(port);
    struct loc **pp, *l;
    if (!h) return;
    for (pp = &h->off; (l = *pp) != NULL; pp = &l->next)
        if (l->vm == vm) break;
    if (on && l) { *pp = l->next; kfree(l); }
    if (!on && !l && (l = kmalloc(sizeof(*l))) != NULL) { l->vm = vm; l->next = h->off; h->off = l; }
    if (vm == vm_handle()) io_trap_bit(port, trapped(h, vm));
}

/* another VM runs now: its ports */
void io_vm_switch(struct vm *v)
{
    u32 i;
    for (i = 0; i < n_iohs; i++) io_trap_bit(iohs[i].port, trapped(&iohs[i], VMH(v)));
}

/* a VM is gone: its exceptions too (control blocks are used again) */
void io_vm_gone(struct vm *v)
{
    u32 i;
    for (i = 0; i < n_iohs; i++) set_local(iohs[i].port, VMH(v), 1);
}

static u32 dispatch(u32 port, u32 type, u32 data, struct client_regs *c);

/* Simulate_IO: EAX data, ECX type, EDX port, EBP client registers */
static u32 simulate(u32 port, u32 type, u32 data, struct client_regs *c)
{
    u32 size = type & IO_SIZE, out = type & IO_OUTPUT, n, i, step, count;
    u32 *idx, mask, seg, base;

    if (type & IO_STRING) {
        u32 elem = type & (IO_SIZE | IO_OUTPUT);
        mask = (type & IO_ADDR32) ? 0xFFFFFFFFu : 0xFFFFu;
        step = size == IO_DWORD ? 4 : size == IO_WORD ? 2 : 1;
        seg = type >> 16;
        base = (c->eflags & EFL_VM) ? (seg & 0xFFFF) << 4 : sel_base(seg);
        idx = out ? &c->esi : &c->edi;
        count = 1;
        if (type & IO_REP) {
            count = c->ecx & mask;
            c->ecx &= ~mask;            /* observed: 0 before the elements */
        }
        for (i = 0; i < count; i++) {
            u32 off = *idx & mask;
            u8 *p = (u8 *)(base + off);
            if (out) {
                u32 v = step == 4 ? *(u32 *)p : step == 2 ? *(u16 *)p : *p;
                dispatch(port, elem, v, c);
            } else {
                u32 v = dispatch(port, elem, 0, c);
                if (step == 4) *(u32 *)p = v;
                else if (step == 2) *(u16 *)p = (u16)v;
                else *p = (u8)v;
            }
            off = (type & IO_REVERSE) ? off - step : off + step;
            *idx = (*idx & ~mask) | (off & mask);
        }
        return data;
    }
    if (size == IO_BYTE) {
        if (out) { outb(port, data); return data; }
        return (data & ~0xFFu) | inb(port);
    }
    /* a word or a dword: byte by byte (UNVERIFIED) */
    n = size == IO_DWORD ? 4 : 2;
    if (out) {
        for (i = 0; i < n; i++) dispatch(port + i, IO_BYTE | IO_OUTPUT, data >> (8 * i), c);
        return data;
    }
    {
        u32 v = 0;
        for (i = 0; i < n; i++) v |= (dispatch(port + i, IO_BYTE, 0, c) & 0xFF) << (8 * i);
        if (n == 2) v |= data & 0xFFFF0000u;
        return v;
    }
}

/* one access: the port's handler if it is trapped, else Simulate_IO */
static u32 dispatch(u32 port, u32 type, u32 data, struct client_regs *c)
{
    struct ioh *h = find(port);
    struct svcregs s;
    if (!h || !trapped(h, vm_handle())) return simulate(port, type, data, c);
    memset(&s, 0, sizeof(s));
    s.eax = data;
    s.ebx = vm_handle();
    s.ecx = type;
    s.edx = port;
    s.ebp = (u32)c;
    s.eflags = 2;
    vmm_call_proc(h->proc, &s);
    return s.eax;
}

/* An IN, OUT, INS or OUTS from the VM that raised #GP: is it to a trapped
 * port? Then the handler does it, and the VM goes on after it. */
u32 io_traps;                           /* for the heartbeat (vmm.c) */

int io_trap(struct cregs *r, u8 *op)
{
    u32 n = 0, opsize32, addr32, rep = 0, seg = 0, type, port, len, data;
    struct client_regs cr, *c = &cr;
    struct cregs *held;
    int v86 = (r->eflags & EFL_VM) != 0;

    opsize32 = addr32 = v86 ? 0 : sysvm.client32;
    for (;; n++) {
        if (n >= 14) return 0;
        if (op[n] == 0x66) opsize32 = !opsize32;
        else if (op[n] == 0x67) addr32 = !addr32;
        else if (op[n] == 0xF2 || op[n] == 0xF3) rep = 1;
        else if (op[n] == 0x26) seg = r->es;
        else if (op[n] == 0x2E) seg = r->cs;
        else if (op[n] == 0x36) seg = r->ss;
        else if (op[n] == 0x3E) seg = r->ds;
        else if (op[n] == 0x64) seg = r->fs;
        else if (op[n] == 0x65) seg = r->gs;
        else break;
    }
    switch (op[n]) {
    case 0xE4: case 0xE5: case 0xE6: case 0xE7:
        port = op[n + 1];
        len = n + 2;
        break;
    case 0xEC: case 0xED: case 0xEE: case 0xEF:
    case 0x6C: case 0x6D: case 0x6E: case 0x6F:
        port = r->edx & 0xFFFF;
        len = n + 1;
        break;
    default:
        return 0;
    }
    type = (op[n] & 1) ? (opsize32 ? IO_DWORD : IO_WORD) : IO_BYTE;
    if (op[n] == 0xE6 || op[n] == 0xE7 || op[n] == 0xEE || op[n] == 0xEF
        || op[n] == 0x6E || op[n] == 0x6F)
        type |= IO_OUTPUT;
    if (op[n] >= 0x6C && op[n] <= 0x6F) {
        type |= IO_STRING;
        if (rep) type |= IO_REP;
        if (addr32) type |= IO_ADDR32;
        if (r->eflags & EFL_DF) type |= IO_REVERSE;
        if (!(type & IO_OUTPUT)) seg = r->es;       /* INS: always ES */
        else if (!seg) seg = r->ds;
        type |= (seg & 0xFFFF) << 16;
        /* REP with a count of 0 does nothing */
        if (rep && !((addr32 ? r->ecx : (r->ecx & 0xFFFF)))) {
            r->eip += len;
            return 1;
        }
    }
    /* past the instruction first: Windows 3.1's handlers see Client_EIP
     * after it (VXI: IN AL,DX at 1A6h gave 1A7h, IN EAX,DX 2 bytes on) */
    if (v86 || !sysvm.client32) r->eip = (r->eip & 0xFFFF0000u) | (u16)(r->eip + len);
    else r->eip += len;
    io_traps++;
    vxd_client(r, c);
    held = vmm_hold_delivery(NULL);
    data = dispatch(port, type, r->eax, c);
    vmm_hold_delivery(held);
    if (!(type & (IO_OUTPUT | IO_STRING))) {
        u32 size = type & IO_SIZE;
        if (size == IO_BYTE) c->eax = (c->eax & ~0xFFu) | (data & 0xFF);
        else if (size == IO_WORD) c->eax = (c->eax & ~0xFFFFu) | (data & 0xFFFF);
        else c->eax = data;
    }
    if (vm_trace && cur_vm != &sysvm && (port == 0x60 || port == 0x64 || port == 0x20 || port == 0x21))
        trace("<%x%c%x>", port, (type & IO_OUTPUT) ? '=' : ':', (type & IO_OUTPUT) ? r->eax & 0xFF : c->eax & 0xFF);
    vxd_client_back(r, c);
    return 1;
}

/* the VMM's I/O services */
int io_service(u32 svc, struct svcregs *s)
{
    switch (svc) {
    case 0x94:                          /* Simulate_IO */
        s->eax = simulate(s->edx, s->ecx, s->eax, (struct client_regs *)s->ebp);
        return 1;
    case 0x95: {                        /* Install_Mult_IO_Handlers: EDI the table */
        u8 *t = (u8 *)s->edi;
        u32 n = *(u16 *)t, i;
        for (i = 0; i < n; i++) {
            u8 *e = t + 2 + i * 6;      /* a word port, a dword procedure */
            if (!install(*(u16 *)e, *(u32 *)(e + 2))) {
                s->edx = *(u16 *)e;
                s->eflags |= EFL_CF;
                return 1;
            }
        }
        return 1;
    }
    case 0x96:                          /* Install_IO_Handler: ESI procedure, EDX port */
        if (!install(s->edx, s->esi)) s->eflags |= EFL_CF;
        return 1;
    case 0x97:                          /* Enable_Global_Trapping: EDX */
        set_trapping(s->edx, 1);
        return 1;
    case 0x98:                          /* Enable_Local_Trapping: EBX the VM, EDX */
        set_local(s->edx, s->ebx, 1);
        return 1;
    case 0x99:                          /* Disable_Global_Trapping */
        set_trapping(s->edx, 0);
        return 1;
    case 0x9A:                          /* Disable_Local_Trapping */
        set_local(s->edx, s->ebx, 0);
        return 1;
    }
    return 0;
}
