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

/* the start of a VM control block (the DDK names its fields; the order
 * as observed) */
struct vm_cb {
    u32 status, high_linear, client_pointer, vmid;
};

#pragma pack()

extern struct ddb VMM_DDB;
#pragma aux VMM_DDB "*"           /* ddb.asm: no C decoration */
u32 __cdecl vxd_control(u32 proc, u32 msg, u32 ebx, u32 esi, u32 edx, u32 edi, u32 ebp);
extern u8 svc_common[];

#define SYS_CRITICAL_INIT   0x00
#define DEVICE_INIT         0x01
#define INIT_COMPLETE       0x02
#define SYS_VM_INIT         0x03
#define SYS_VM_TERMINATE    0x04
#define SYSTEM_EXIT         0x05
#define SYS_CRITICAL_EXIT   0x06
#define BEGIN_PM_APP        0x15
#define END_PM_APP          0x16

#define VMM_SERVICES        242     /* Windows 3.1's VMM (docs/win386-vxds.md) */

static struct ddb **devs;           /* in init order */
static struct ddb *find_dev(u32 id);
static u32 ndevs;
static struct vm_cb *sys_cb;
static struct client_regs *sys_client;
static u8 *sys_acb;                 /* the application control block (Begin_PM_App) */
static u32 *vmm_slots;              /* the VMM's service table: stub addresses */

u32 vm_handle(void)
{
    return (u32)sys_cb;
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

static void client_from(struct cregs *r)
{
    struct client_regs *c = sys_client;
    if (!r) return;
    c->edi = r->edi; c->esi = r->esi; c->ebp = r->ebp; c->ebx = r->ebx;
    c->edx = r->edx; c->ecx = r->ecx; c->eax = r->eax;
    c->eip = r->eip; c->cs = (u16)r->cs; c->eflags = r->eflags; c->esp = r->esp;
    c->ss = (u16)r->ss; c->es = (u16)r->es; c->ds = (u16)r->ds;
    c->fs = (u16)r->fs; c->gs = (u16)r->gs;
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
#define NBUILTIN 1
static void vtd_init(void);
static struct ddb vtd_ddb = {
    0, 0x030A, 0x0005, 3, 10, 0, { 'V', 'T', 'D', ' ', ' ', ' ', ' ', ' ' },
    0x14000000, 0, 0, 0, 0, 0, 0, 0, 8
};
#define VTD_FASTEST 1
#define VTD_SLOWEST 0x36
static u32 vtd_min_period = VTD_SLOWEST;   /* the shortest one asked for */
static u32 vtd_requests[16], vtd_nreq;

/* VTD takes IRQ 0 through VPICD (VPICD_Virtualize_IRQ), and its
 * VID_Hw_Int_Proc asks VPICD to simulate it into the System VM: the
 * System VM still sees every tick (the timer is not reprogrammed yet) */
extern u8 vtd_hw_entry[];
static struct vid vtd_vid;
static u32 vtd_irq;                     /* VPICD's handle */

static u32 svc_slot(u32 dev, u32 svc)
{
    struct ddb *d = find_dev(dev);
    if (!d || !d->svc_table || svc >= d->svc_count) return 0;
    return *(u32 *)(d->svc_table + svc * 4);
}

static void vtd_init(void)
{
    struct svcregs s;
    memset(&s, 0, sizeof(s));
    vtd_vid.irq = 0;
    vtd_vid.hw_int_proc = (u32)vtd_hw_entry;
    vtd_vid.iret_time_out = 500;
    s.edi = (u32)&vtd_vid;
    vmm_call_proc(svc_slot(3, 0x01), &s);     /* VPICD_Virtualize_IRQ */
    if (s.eflags & EFL_CF) panic("VTD: VPICD would not give IRQ 0");
    vtd_irq = s.eax;
}

/* VID_Hw_Int_Proc for IRQ 0 (vmmasm.asm calls it with the registers) */
void __cdecl vtd_hw(struct svcregs *r)
{
    struct svcregs s;
    memset(&s, 0, sizeof(s));
    s.eax = vtd_irq;
    s.ebx = (u32)sys_cb;
    vmm_call_proc(svc_slot(3, 0x02), &s);     /* VPICD_Set_Int_Request */
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
    case 0x0002:                        /* VTD_Get_Interrupt_Period */
        s->eax = VTD_SLOWEST;
        return;
    case 0x0003:                        /* VTD_Begin_Min_Int_Period */
        /* the period is noted but the timer is not reprogrammed yet: the
         * System VM keeps its 54 ms tick (UNVERIFIED what Windows 3.1
         * does to the VM's INT 08h rate when it speeds the timer up) */
        if (s->eax < VTD_FASTEST || s->eax > VTD_SLOWEST || vtd_nreq == 16) { s->eflags |= EFL_CF; return; }
        vtd_requests[vtd_nreq++] = s->eax;
        if (s->eax < vtd_min_period) vtd_min_period = s->eax;
        return;
    case 0x0004:                        /* VTD_End_Min_Int_Period */
        for (i = 0; i < vtd_nreq && vtd_requests[i] != s->eax; i++)
            ;
        if (i == vtd_nreq) { s->eflags |= EFL_CF; return; }
        vtd_requests[i] = vtd_requests[--vtd_nreq];
        vtd_min_period = VTD_SLOWEST;
        for (i = 0; i < vtd_nreq; i++)
            if (vtd_requests[i] < vtd_min_period) vtd_min_period = vtd_requests[i];
        return;
    }
    panic("VTD service %x is not implemented yet (called from %x)", svc, *(u32 *)(s + 1));
}

/* the devices: the VMM's own DDB and the loader's list, by init order
 * (equal orders: as loaded; UNVERIFIED tie-break), then the init messages */
void vxd_init(void)
{
    u32 n = 1 + NBUILTIN + info->vxd_count, i, j;
    u32 *list = (u32 *)(W386_VMM_LIN + info->vxd_list);

    devs = kmalloc(n * sizeof(*devs));
    sys_cb = kmalloc(4096);
    sys_client = kmalloc(sizeof(*sys_client));
    sys_acb = kmalloc(256);
    if (!devs || !sys_cb || !sys_client || !sys_acb) panic("no memory for the devices");
    /* the System VM's control block: its V86 memory is at linear 0 here
     * (Windows 3.1 maps it high, at 84000000h) */
    sys_cb->status = 0;
    sys_cb->high_linear = 0;
    sys_cb->client_pointer = (u32)sys_client;
    sys_cb->vmid = 1;
    vmm_slots = make_slots(1, VMM_SERVICES);
    devs[ndevs++] = &VMM_DDB;
    /* w16's own devices (C, in this module), each with its service table */
    vtd_ddb.svc_table = (u32)make_slots(5, vtd_ddb.svc_count);
    devs[ndevs++] = &vtd_ddb;
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
    vpicd->deliver = vmm_deliver;
    vpicd->sys_vm = (u32)sys_cb;
    vpicd->rm_base1 = info->pic1_base;
    vpicd->rm_base2 = info->pic2_base;
    broadcast(SYS_CRITICAL_INIT, info->tail_lin, 0, 0, 0);
    vtd_init();
    broadcast(DEVICE_INIT, info->tail_lin, 0, 0, 0);
    broadcast(INIT_COMPLETE, info->tail_lin, 0, 0, 0);
    broadcast(SYS_VM_INIT, info->tail_lin, 0, 0, 0);
}

/* KRNL386 entered protected mode (dpmi_switch) / ended (terminate_client) */
void vxd_pm_app(int begin, struct cregs *r)
{
    client_from(r);
    if (begin)
        broadcast(BEGIN_PM_APP, 0, sysvm.client32 ? 1 : 0, (u32)sys_acb, 0);
    else
        broadcast(END_PM_APP, 0, 0, (u32)sys_acb, 0);
}

/* Windows ends (exit_windows). UNVERIFIED: the order Windows 3.1 sends
 * these to several devices in (w16: the reverse of the init order). */
void vxd_exit(struct cregs *r)
{
    client_from(r);
    broadcast(SYS_VM_TERMINATE, 0, 0, 0, 1);
    broadcast(SYSTEM_EXIT, 0, 0, 0, 1);
    broadcast(SYS_CRITICAL_EXIT, 0, 0, 0, 1);
}

/* the System VM's client registers for a device's I/O handler (io.c):
 * filled from r; vxd_client_back puts them back */
struct client_regs *vxd_client(struct cregs *r)
{
    client_from(r);
    return sys_client;
}

static void client_to(struct cregs *r);
void vxd_client_back(struct cregs *r)
{
    client_to(r);
}

static void client_to(struct cregs *r)
{
    struct client_regs *c = sys_client;
    r->edi = c->edi; r->esi = c->esi; r->ebp = c->ebp; r->ebx = c->ebx;
    r->edx = c->edx; r->ecx = c->ecx; r->eax = c->eax;
    r->eip = c->eip; r->cs = c->cs; r->eflags = c->eflags; r->esp = c->esp;
    r->ss = c->ss; r->es = c->es; r->ds = c->ds; r->fs = c->fs; r->gs = c->gs;
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
    client_from(r);
    vxd_control(proc, r->eax, (u32)sys_cb, r->esi, r->edx, r->edi, (u32)sys_client);
    client_to(r);
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
    else if (dev != 1 && (d = find_dev(dev)) != NULL && svc < d->svc_count && d->svc_table)
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
        else n = kmalloc(l->node_size + 4);
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
 * segment registers taken as V86 segments). UNVERIFIED: protected-mode
 * nested execution (Begin_Nest_Exec in a PM client), which w16 runs in
 * V86 mode too ---- */
static int nest_level;

static int nest_service(u32 svc, struct svcregs *s)
{
    struct cregs v;
    struct client_regs *c = sys_client;
    switch (svc) {
    case 0x82:                          /* Begin_Nest_V86_Exec */
    case 0x83:                          /* Begin_Nest_Exec */
        nest_level++;
        return 1;
    case 0x84:                          /* Exec_Int: EAX the vector */
        memset(&v, 0, sizeof(v));
        v.eax = c->eax; v.ebx = c->ebx; v.ecx = c->ecx; v.edx = c->edx;
        v.esi = c->esi; v.edi = c->edi; v.ebp = c->ebp;
        v.ds = c->ds; v.es = c->es;
        v.eflags = c->eflags;
        v86_int(&v, s->eax & 0xFF);
        c->eax = v.eax; c->ebx = v.ebx; c->ecx = v.ecx; c->edx = v.edx;
        c->esi = v.esi; c->edi = v.edi; c->ebp = v.ebp;
        c->ds = (u16)v.ds; c->es = (u16)v.es;
        c->eflags = (c->eflags & ~0x8D5u) | (v.eflags & 0x8D5u);
        return 1;
    case 0x85:                          /* Resume_Exec: nothing is waiting here */
        return 1;
    case 0x86:                          /* End_Nest_Exec */
        if (nest_level) nest_level--;
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

/* ---- the VMM's services ---- */
void __cdecl svc_dispatch(struct svcregs *s)
{
    u32 dev = s->id >> 16, svc = s->id & 0xFFFF;

    s->eflags &= ~EFL_CF;
    if (dev == 1) switch (svc) {
    case 0x0000:                        /* Get_VMM_Version: 3.10, debug revision 0 */
        s->eax = (s->eax & 0xFFFF0000u) | 0x030A;
        s->ecx = 0;
        return;
    case 0x0001:                        /* Get_Cur_VM_Handle (one VM) */
    case 0x0003:                        /* Get_Sys_VM_Handle */
        s->ebx = (u32)sys_cb;
        return;
    case 0x0002:                        /* Test_Cur_VM_Handle */
    case 0x0004:                        /* Test_Sys_VM_Handle */
        ZF_SET(s, s->ebx == (u32)sys_cb);
        return;
    case 0x0005:                        /* Validate_VM_Handle: carry clear if valid */
        if (s->ebx != (u32)sys_cb) s->eflags |= EFL_CF;
        return;
    }
    if (dev == 1 && list_service(svc, s)) return;
    if (dev == 1 && io_service(svc, s)) return;
    if (dev == 1 && nest_service(svc, s)) return;
    if (dev == 5) { vtd_service(svc, s); return; }
    panic("VxD service %x of device %x is not implemented yet (called from %x)",
          svc, dev, *(u32 *)(s + 1));
}
