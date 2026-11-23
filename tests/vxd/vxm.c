/* vxm.c - VXM.386, a probe VxD for the Virtual DMA Device (VDMAD, device
 * 0004h; ARCHITECTURE.md 12.8), w16's own, test only. Loaded with
 * "device=vxm.386" under Windows 3.1's WIN386 or w16's, it writes one line
 * per result to COM1 (DOSBox-X serial1=file), every line starting "VXM ":
 *   Device_Init: VDMAD_Get_Version; VDMAD_Virtualize_Channel for
 *     channels 1 and 5 (its callback below), 1 again, 2 (the floppy's),
 *     4 (the cascade) and 8; VDMAD_Get_EISA_Adr_Mode for channels 0-7;
 *     built with RESERVE, VDMAD_Reserve_Buffer_Space (8 pages, no limit).
 *   Init_Complete: VDMAD_Get_Version again (the buffer exists by then);
 *     Install_IO_Handler for the 8237s' ports, the page registers and a
 *     few others (carry: someone traps the port; a port it gets is passed
 *     to the hardware unchanged); the services called in the System VM:
 *     Lock/Unlock_DMA_Region (conventional memory, a 64 KB and a 128 KB
 *     boundary, the probe's own memory), Request_Buffer, Copy_To/From_
 *     Buffer (and their errors), Release_Buffer, Disable/Enable_
 *     Translation, Get/Set_Virt_State, Get/Set_Region_Info, Scatter_Lock
 *     and Scatter_Unlock.
 *   The channel callback (channels 1 and 5, any VM): the virtual state as
 *     the VM left it (Get_Virt_State), then VDMAD_Default_Handler (as
 *     the DDK's devices chain), then the region (Get_Region_Info) and
 *     the physical 8237's address, count and page for the channel:
 *     what the default handler programmed.
 * Sources: the Windows 3.1 DDK's Virtual Device Adaptation Guide (the
 * VDMAD services, Install_IO_Handler, the control messages); the Intel
 * 8237A data sheet and the IBM PC/AT technical reference (the ports: 00h-
 * 0Fh the first controller, C0h-DFh the second, the page registers 81h-
 * 8Fh). Nothing here comes from Microsoft's code.
 */
#include <stdarg.h>
#include "w16vxd.h"

/* everything in the one object of code (Windows 3.1's WIN386 does not
 * load this probe with a second object for its data) */
#pragma data_seg("_TEXT", "CODE")

static vu32 inb(vu32 port);
static void outb(vu32 port, vu32 v);
#pragma aux inb = "in al, dx" "movzx eax, al" parm [edx] value [eax] modify exact [eax];
#pragma aux outb = "out dx, al" parm [edx] [eax] modify exact [];

void __cdecl vmm_install_io(struct vregs *r);
void __cdecl vmm_get_sys_vm(struct vregs *r);
#define D(n) void __cdecl vdmad_##n(struct vregs *r);
D(0) D(1) D(2) D(3) D(4) D(5) D(6) D(7) D(8) D(9) D(10) D(11) D(12) D(13)
D(14) D(15) D(16) D(17) D(18) D(19) D(20) D(21) D(22) D(23)
extern char vxm_cb[], vxm_io[];
#pragma aux vxm_cb "*"
#pragma aux vxm_io "*"

/* ---- COM1 ---- */
static void putc_(int c)
{
    int i;
    for (i = 0; i < 100000 && !(inb(0x3FD) & 0x20); i++) ;
    outb(0x3F8, c);
}
static void puts_(const char *s) { while (*s) putc_(*s++); }
static void hex(vu32 v, int n)
{
    int i;
    for (i = n - 1; i >= 0; i--) putc_("0123456789ABCDEF"[(v >> (i * 4)) & 15]);
}
static vu32 lines = 1;            /* (initialised data: BSS is not zeroed by every loader) */
static void LOG(const char *f, ...)
{
    va_list a;
    if (lines > 3001) return;
    va_start(a, f);
    lines++;
    puts_("VXM ");
    for (; *f; f++) {
        if (*f == '%') {
            f++;
            if (*f == 'b') hex(va_arg(a, vu32), 2);
            else if (*f == 'w') hex(va_arg(a, vu32), 4);
            else hex(va_arg(a, vu32), 8);
        } else putc_(*f);
    }
    putc_('\r'); putc_('\n');
    va_end(a);
}

static vu32 cur_ebp = 1;                /* the client's registers of the call under way */
static void clr(struct vregs *r)
{
    vu32 *p = (vu32 *)r, i;
    for (i = 0; i < sizeof(*r) / 4; i++) p[i] = 0;
    r->ebp = cur_ebp;
}
#define CF(r) ((r)->eflags & VF_CF ? 1u : 0u)
#define ZF(r) ((r)->eflags & VF_ZF ? 1u : 0u)

static vu32 sys_vm = 1, h1 = 1, h5 = 1;
static vu8 area[0x2000];                /* the probe's own memory (VMM heap) */
#pragma pack(1)
static struct { vu32 size, off; vu16 seg, id, avail, used; vu32 e[16]; } dds;
#pragma pack()

static vu32 vmid(vu32 vm) { return vm ? CB_VMID(vm) : 0; }

/* the physical 8237 registers of channel ch: address, count, page */
static const vu8 pagereg[8] = { 0x87, 0x83, 0x81, 0x82, 0x8F, 0x8B, 0x89, 0x8A };
static void phys_regs(vu32 ch, vu32 *adr, vu32 *cnt, vu32 *pg)
{
    vu32 base = ch < 4 ? ch * 2 : 0xC0 + (ch - 4) * 4, step = ch < 4 ? 1 : 2;
    outb(ch < 4 ? 0x0C : 0xD8, 0);
    *adr = inb(base); *adr |= inb(base) << 8;
    *cnt = inb(base + step); *cnt |= inb(base + step) << 8;
    *pg = inb(pagereg[ch]);
}

/* the channel callback: EAX the DMA handle, EBX the VM */
void __cdecl vxm_callback(struct vregs *r)
{
    struct vregs s;
    vu32 h = r->eax, vm = r->ebx, ch = h == h1 ? 1 : h == h5 ? 5 : 9, a, c, p;
    cur_ebp = r->ebp;
    clr(&s); s.eax = h; s.ebx = vm; s.ebp = r->ebp;
    vdmad_4(&s);
    LOG("cb ch %b vm %b virt esi %x ecx %x dl %b dh %b",
        ch, vmid(vm), s.esi, s.ecx, s.edx & 0xFF, (s.edx >> 8) & 0xFF);
    /* VDMAD_Default_Handler (as the DDK's devices jump to it: every
     * register as the callback got them, EBP the client's registers) */
    s = *r;
    vdmad_18(&s);
    clr(&s); s.eax = h; s.ebp = r->ebp;
    vdmad_2(&s);
    if (ch < 8) phys_regs(ch, &a, &c, &p); else a = c = p = 0;
    LOG("  after default: region id %b locked %b lin %x size %x; phys adr %w cnt %w page %b",
        s.ebx & 0xFF, (s.ebx >> 8) & 0xFF, s.esi, s.ecx, a, c, p);
}

/* a port the probe got: to the hardware unchanged (bytes; others noted) */
void __cdecl vxm_iohandler(struct vregs *r)
{
    vu32 t = r->ecx & 0xFF;
    if (t == 0) r->eax = (r->eax & ~0xFFu) | inb(r->edx);
    else if (t == 4) outb(r->edx, r->eax & 0xFF);
    else LOG("io type %x port %x not passed on", r->ecx, r->edx);
}

void __cdecl vxm_timeout(struct vregs *r) { (void)r; }

static void get_version(const char *when)
{
    struct vregs s;
    clr(&s);
    vdmad_0(&s);
    LOG("Get_Version %x: eax %x ecx %x cf %b", *(vu32 *)when, s.eax, s.ecx, CF(&s));
}

static void virtualize(vu32 ch, vu32 cb, vu32 *h)
{
    struct vregs s;
    clr(&s); s.eax = ch; s.esi = cb;
    vdmad_1(&s);
    LOG("Virtualize_Channel %b: cf %b eax %x", ch, CF(&s), s.eax);
    if (h && !CF(&s)) *h = s.eax;
}

static void lock(vu32 lin, vu32 n, vu32 dl)
{
    struct vregs s;
    clr(&s); s.esi = lin; s.ecx = n; s.edx = dl; s.eax = 0xEEEEEEEE;
    vdmad_9(&s);
    LOG("Lock_DMA_Region %x %x dl %b: cf %b al %b ecx %x edx %x",
        lin, n, dl, CF(&s), s.eax & 0xFF, s.ecx, s.edx);
    if (!CF(&s)) {
        clr(&s); s.esi = lin; s.ecx = n;
        vdmad_10(&s);
        LOG("  Unlock_DMA_Region: cf %b", CF(&s));
    }
}

static const vu16 ports[] = {
    0x00, 0x01, 0x02, 0x03, 0x04, 0x07, 0x08, 0x09, 0x0A, 0x0B, 0x0C, 0x0D,
    0x0E, 0x0F, 0x80, 0x81, 0x82, 0x83, 0x84, 0x87, 0x88, 0x89, 0x8A, 0x8B,
    0x8F, 0xC0, 0xC2, 0xC4, 0xC6, 0xCE, 0xD0, 0xD2, 0xD4, 0xD6, 0xD8, 0xDA,
    0xDC, 0xDE, 0xDF, 0x10, 0x18, 0x40A, 0x40B, 0x4D6, 0x481, 0x48B, 0xFFFF
};

static void services(void)
{
    struct vregs s;
    vu32 i, id, bufsz, ok;
    vu8 *b = area;

    clr(&s);
    vdmad_0(&s);
    bufsz = s.ecx;
    /* ports */
    for (i = 0; ports[i] != 0xFFFF; i++) {
        clr(&s); s.edx = ports[i]; s.esi = (vu32)vxm_io;
        vmm_install_io(&s);
        LOG("Install_IO_Handler %w: cf %b", ports[i], CF(&s));
    }
    /* locking */
    lock(0x90000, 0x2000, 0);
    lock(0x9F000, 0x2000, 1);
    lock(0x9F000, 0x2000, 2);
    lock(0x8F000, 0x2000, 1);
    lock(0x8F000, 0x2000, 2);
    lock(0xB8000, 0x1000, 0);
    lock(CB_HIGH_LINEAR(sys_vm) + 0x90000, 0x2000, 0);
    lock((vu32)area, sizeof area, 0);
    lock((vu32)area, 0x100, 0);
    /* the buffer */
    for (i = 0; i < 0x1000; i++) b[i] = (vu8)(i * 7 + 3);
    clr(&s); s.esi = (vu32)b; s.ecx = 0x1000; s.eax = 0xEEEEEEEE;
    vdmad_14(&s);
    id = s.ebx;
    LOG("Request_Buffer %x %x: cf %b al %b ebx %x edx %x", (vu32)b, 0x1000, CF(&s), s.eax & 0xFF, s.ebx, s.edx);
    clr(&s); s.esi = (vu32)b; s.ecx = 0x1000; s.eax = 0xEEEEEEEE;
    vdmad_14(&s);
    LOG("Request_Buffer again: cf %b al %b ebx %x", CF(&s), s.eax & 0xFF, s.ebx);
    clr(&s); s.ebx = id; s.esi = (vu32)b; s.edi = 0; s.ecx = 0x1000; s.eax = 0xEEEEEEEE;
    vdmad_16(&s);
    LOG("Copy_To_Buffer: cf %b al %b", CF(&s), s.eax & 0xFF);
    for (i = 0; i < 0x1000; i++) b[i] = 0;
    clr(&s); s.ebx = id; s.esi = (vu32)b; s.edi = 0; s.ecx = 0x1000; s.eax = 0xEEEEEEEE;
    vdmad_17(&s);
    for (ok = 1, i = 0; i < 0x1000; i++) if (b[i] != (vu8)(i * 7 + 3)) ok = 0;
    LOG("Copy_From_Buffer: cf %b al %b data back %b", CF(&s), s.eax & 0xFF, ok);
    clr(&s); s.ebx = id; s.esi = (vu32)b; s.edi = 0x800; s.ecx = 0x1000; s.eax = 0xEEEEEEEE;
    vdmad_16(&s);
    LOG("Copy_To_Buffer edi 800 ecx 1000: cf %b al %b", CF(&s), s.eax & 0xFF);
    clr(&s); s.ebx = id; s.esi = (vu32)b; s.edi = 0; s.ecx = bufsz + 1; s.eax = 0xEEEEEEEE;
    vdmad_16(&s);
    LOG("Copy_To_Buffer ecx size+1: cf %b al %b", CF(&s), s.eax & 0xFF);
    clr(&s); s.ebx = id + 5; s.esi = (vu32)b; s.edi = 0; s.ecx = 0x10; s.eax = 0xEEEEEEEE;
    vdmad_16(&s);
    LOG("Copy_To_Buffer id+5: cf %b al %b", CF(&s), s.eax & 0xFF);
    clr(&s); s.ebx = id + 5; s.esi = (vu32)b; s.edi = 0; s.ecx = 0x10; s.eax = 0xEEEEEEEE;
    vdmad_17(&s);
    LOG("Copy_From_Buffer id+5: cf %b al %b", CF(&s), s.eax & 0xFF);
    clr(&s); s.ebx = id;
    vdmad_15(&s);
    LOG("Release_Buffer: cf %b", CF(&s));
    clr(&s); s.ebx = id;
    vdmad_15(&s);
    LOG("Release_Buffer again: cf %b", CF(&s));
    clr(&s); s.esi = (vu32)b; s.ecx = bufsz; s.eax = 0xEEEEEEEE;
    vdmad_14(&s);
    LOG("Request_Buffer size %x: cf %b al %b ebx %x", bufsz, CF(&s), s.eax & 0xFF, s.ebx);
    if (!CF(&s)) { id = s.ebx; clr(&s); s.ebx = id; vdmad_15(&s); }
    clr(&s); s.esi = (vu32)b; s.ecx = bufsz + 1; s.eax = 0xEEEEEEEE;
    vdmad_14(&s);
    LOG("Request_Buffer size+1: cf %b al %b ebx %x", CF(&s), s.eax & 0xFF, s.ebx);
    if (!CF(&s)) { id = s.ebx; clr(&s); s.ebx = id; vdmad_15(&s); }
    /* translation */
    clr(&s); s.eax = h1; s.ebx = sys_vm;
    vdmad_19(&s);
    LOG("Disable_Translation: cf %b zf %b", CF(&s), ZF(&s));
    clr(&s); s.eax = h1; s.ebx = sys_vm;
    vdmad_4(&s);
    LOG("  Get_Virt_State: esi %x ecx %x edx %x", s.esi, s.ecx, s.edx);
    clr(&s); s.eax = h1; s.ebx = sys_vm;
    vdmad_19(&s);
    LOG("Disable_Translation 2: cf %b zf %b", CF(&s), ZF(&s));
    clr(&s); s.eax = h1; s.ebx = sys_vm;
    vdmad_20(&s);
    LOG("Enable_Translation: cf %b zf %b", CF(&s), ZF(&s));
    clr(&s); s.eax = h1; s.ebx = sys_vm;
    vdmad_20(&s);
    LOG("Enable_Translation 2: cf %b zf %b", CF(&s), ZF(&s));
    clr(&s); s.eax = h1; s.ebx = sys_vm;
    vdmad_20(&s);
    LOG("Enable_Translation 3: cf %b zf %b", CF(&s), ZF(&s));
    /* virtual state and region */
    {
        vu32 esi, ecx, edx;
        clr(&s); s.eax = h1; s.ebx = sys_vm;
        vdmad_4(&s);
        esi = s.esi; ecx = s.ecx; edx = s.edx;
        LOG("Get_Virt_State: esi %x ecx %x edx %x cf %b", esi, ecx, edx, CF(&s));
        clr(&s); s.eax = h1; s.ebx = sys_vm; s.esi = 0x12345; s.ecx = 0x777; s.edx = 0x0059;
        vdmad_5(&s);
        clr(&s); s.eax = h1; s.ebx = sys_vm;
        vdmad_4(&s);
        LOG("  after Set_Virt_State 12345 777 0059: esi %x ecx %x edx %x", s.esi, s.ecx, s.edx);
        clr(&s); s.eax = h1; s.ebx = sys_vm; s.esi = 0x12345; s.ecx = 0x777; s.edx = 0x0156;
        vdmad_5(&s);
        clr(&s); s.eax = h1; s.ebx = sys_vm;
        vdmad_4(&s);
        LOG("  after Set_Virt_State 12345 777 0156: esi %x ecx %x edx %x", s.esi, s.ecx, s.edx);
        clr(&s); s.eax = h1; s.ebx = sys_vm; s.esi = esi; s.ecx = ecx; s.edx = edx;
        vdmad_5(&s);
    }
    {
        vu32 ebx, esi, ecx;
        clr(&s); s.eax = h1;
        vdmad_2(&s);
        ebx = s.ebx; esi = s.esi; ecx = s.ecx;
        LOG("Get_Region_Info: ebx %x esi %x ecx %x", ebx, esi, ecx);
        clr(&s); s.eax = h1; s.ebx = 0x0107; s.esi = 0x90000; s.ecx = 0x100; s.edx = 0x90000;
        vdmad_3(&s);
        clr(&s); s.eax = h1;
        vdmad_2(&s);
        LOG("  after Set_Region_Info 0107 90000 100: ebx %x esi %x ecx %x edx %x", s.ebx, s.esi, s.ecx, s.edx);
        clr(&s); s.eax = h1; s.ebx = ebx; s.esi = esi; s.ecx = ecx;
        vdmad_3(&s);
    }
    /* scatter lock */
    for (i = 0; i < 2; i++) {
        vu32 j;
        dds.size = 0x3000; dds.off = 0x8F800; dds.seg = 0; dds.id = 0; dds.avail = 8; dds.used = 0;
        for (j = 0; j < 16; j++) dds.e[j] = 0xEEEEEEEE;
        clr(&s); s.ebx = sys_vm; s.eax = i; s.edi = (vu32)&dds; s.esi = 0xEEEEEEEE;
        vdmad_11(&s);
        LOG("Scatter_Lock al %b: cf %b zf %b edx %x esi %x size %x used %w e %x %x %x %x %x %x",
            i, CF(&s), ZF(&s), s.edx, s.esi, dds.size, dds.used, dds.e[0], dds.e[1], dds.e[2], dds.e[3], dds.e[4], dds.e[5]);
        clr(&s); s.ebx = sys_vm; s.eax = i; s.edi = (vu32)&dds;
        vdmad_12(&s);
        LOG("  Scatter_Unlock: cf %b", CF(&s));
    }
    /* the physical state of channels 1 and 5 now */
    {
        vu32 a, c, p;
        phys_regs(1, &a, &c, &p);
        LOG("phys ch 1: adr %w cnt %w page %b", a, c, p);
        phys_regs(5, &a, &c, &p);
        LOG("phys ch 5: adr %w cnt %w page %b", a, c, p);
    }
}

void __cdecl vxm_control(struct vregs *r)
{
    struct vregs s;
    vu32 i;
    r->eflags &= ~VF_CF;
    cur_ebp = r->ebp;
    switch (r->eax) {
    case SYS_CRITICAL_INIT:
        clr(&s); vmm_get_sys_vm(&s); sys_vm = s.ebx;
        get_version("sci ");
        break;
    case DEVICE_INIT:
        get_version("di  ");
        virtualize(1, (vu32)vxm_cb, &h1);
        virtualize(5, (vu32)vxm_cb, &h5);
        virtualize(1, (vu32)vxm_cb, 0);
        virtualize(2, (vu32)vxm_cb, 0);
        virtualize(4, (vu32)vxm_cb, 0);
        virtualize(8, (vu32)vxm_cb, 0);
        for (i = 0; i < 8; i++) {
            clr(&s); s.eax = i; s.ebx = h1; s.ecx = 0xEEEEEEEE;
            vdmad_21(&s);
            LOG("Get_EISA_Adr_Mode %b: ecx %x cf %b", i, s.ecx, CF(&s));
        }
#ifdef RESERVE
        clr(&s); s.eax = 8; s.ecx = 0;
        vdmad_13(&s);
        LOG("Reserve_Buffer_Space 8 0: cf %b", CF(&s));
#endif
        break;
    case INIT_COMPLETE:
        get_version("ic  ");
        services();
        break;
    case SYS_VM_INIT:
        get_version("svi ");
        break;
    case CREATE_VM:
        LOG("Create_VM %b", vmid(r->ebx));
        break;
    case DESTROY_VM:
        LOG("Destroy_VM %b", vmid(r->ebx));
        break;
    case SYSTEM_EXIT:
        get_version("exit");
        break;
    }
}
