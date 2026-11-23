/* vdmad.c - w16's VDMAD, the virtual DMA device (device 0004h), a module
 * of WIN386.EXE's W3 table (ARCHITECTURE.md 12.8).
 *
 * The two 8237 DMA controllers. VDMAD traps their ports (00h-0Fh, C0h-
 * DEh) and the page registers of the eight channels (87h 83h 81h 82h,
 * 8Fh 8Bh 89h 8Ah) in every VM, and gives each VM a controller of its
 * own (in its control block): base and current address and count, page,
 * mode, mask and request of each channel, the flip-flops, the status's
 * terminal-count bits. A VM's writes change its virtual state; when a
 * channel's state changes the channel's owner is called (the device that
 * took it with VDMAD_Virtualize_Channel, else VDMAD's default handler),
 * as the DDK describes. The default handler programs the physical
 * channel for the VM's transfer when the VM unmasks it: the address the
 * VM gave is an address in the VM's first megabyte, at the VM's high
 * linear address (CB_High_Linear) in ring 0; if its pages are physically
 * contiguous, within one DMA page (64 KB, or 128 KB for channels 5-7)
 * and below 16 MB the transfer goes there, else through VDMAD's buffer
 * (copied in before a transfer from memory, out after one to memory).
 * Masking the channel stops it. A VM reading back an address or count of
 * a channel running for it gets the physical controller's progress
 * translated back into its own addresses; the status's terminal counts
 * are kept for the VM whose transfer it was.
 *
 * The Virtual DMA Services (INT 4Bh, AH=81h) for V86 code, with the BIOS
 * data byte 40:7Bh's bit 5 set to say they are there.
 *
 * Sources: the Windows 3.1 DDK's Virtual Device Adaptation Guide (.tools/
 * docs/vdag31: the VDMAD services, their registers and error codes, the
 * channel callback, Install_IO_Handler, Hook_V86_Int_Chain); the Intel
 * 8237A data sheet and the IBM PC/AT technical reference (the ports, the
 * flip-flop, mode, mask, request and status registers, the page
 * registers; channels 5-7 count words and address words); Microsoft's
 * Virtual DMA Services specification (INT 4Bh: the functions, the DDS,
 * the flags, the error codes). Observed under Windows 3.1 (tests/
 * vdmadprobe.sh: SBD.COM in a DOS box, VXM.386):
 * - VDS: 40:7Bh = 28h in a box (bit 5); 8102h AX=0100h BX=0003h
 *   CX=0200h SI:DI the buffer's size (16 KB with no DMABufferSize)
 *   DX=0; a box's three pages locked with no buffer at consecutive
 *   physical addresses (its memory is contiguous); on success AX is
 *   left as it was; Request_Buffer (8107h) gives buffer ID 1, a second
 *   one fails with AL=6, a second release with AL=0Ah, an Enable
 *   Translation without a Disable with AL=0Eh, an unknown function
 *   with AL=0Fh; Scatter_Lock (8105h) fills the table with (address,
 *   size) pairs and the number used.
 * - SB DMA in a box: single-cycle and auto-initialised 8-bit transfers
 *   play from the box's own memory (the halves it refills during an
 *   auto-initialised transfer are heard); a 16-bit transfer of 44100
 *   bytes whose pages cross a 128 KB physical boundary ends the box
 *   ("This application has violated system integrity"): too big for the
 *   16 KB buffer.
 * UNVERIFIED: see ARCHITECTURE.md 12.8 VDMAD.
 */
#include "w16vxd.h"

#define NULL ((void *)0)

/* the VMM (vdmadddb.asm) */
void __cdecl vxd_call_proc(vu32 proc, struct vregs *r);
void __cdecl vmm_install_io(struct vregs *r);
void __cdecl vmm_get_sys_vm(struct vregs *r);
void __cdecl vmm_get_cur_vm(struct vregs *r);
void __cdecl vmm_crash_cur_vm(struct vregs *r);
void __cdecl vmm_hook_v86_int(struct vregs *r);
vu32 __cdecl vmm_alloc_cb_area(vu32 size, vu32 flags);
extern char vdmad_io[], vdmad_def[], vdmad_vds[];
#pragma aux vdmad_io "*"
#pragma aux vdmad_def "*"
#pragma aux vdmad_vds "*"

static vu32 inb(vu32 port);
static void outb(vu32 port, vu32 v);
#pragma aux inb = "in al, dx" "movzx eax, al" parm [edx] value [eax] modify exact [eax];
#pragma aux outb = "out dx, al" parm [edx] [eax] modify exact [];

struct vdmad_link vdmad_link = { VDMAD_LINK_MAGIC };

/* DMA error codes (the DDK's, the VDS's) */
#define DMA_NOT_CONTIGUOUS  1
#define DMA_NOT_ALIGNED     2
#define DMA_LOCK_FAILED     3
#define DMA_BUFFER_TOO_SMALL 5
#define DMA_BUFFER_IN_USE   6
#define DMA_INVALID_REGION  7
#define DMA_INVALID_BUFFER  0x0A
#define DMA_COPY_OUT_RANGE  0x0B
#define DMA_INVALID_CHANNEL 0x0C
#define DMA_DISABLE_OVERFLOW 0x0D
#define DMA_DISABLE_UNDERFLOW 0x0E
#define DMA_FUNC_UNSUPPORTED 0x0F

/* the mode byte's flags in a virtual state (the 8237's channel bits are
 * not part of it: the DDK's DMA_masked and DMA_requested take them) */
#define DMA_MASKED      0x01
#define DMA_REQUESTED   0x02
#define MODE_TYPE       0x0C            /* 04h write (to memory), 08h read (from memory) */
#define MODE_WRITE      0x04
#define MODE_READ       0x08
#define MODE_AUTO       0x10

#define ISA_LIMIT 0x1000000u            /* 24 address lines */

static void memcpy_(void *d, const void *s, vu32 n)
{
    vu8 *dd = d; const vu8 *ss = s;
    while (n--) *dd++ = *ss++;
}
static void vregs_clear(struct vregs *r)
{
    vu32 *p = (vu32 *)r, i;
    for (i = 0; i < sizeof(*r) / 4; i++) p[i] = 0;
}

/* ---- the channels ---- */
static const vu8 pageport[8] = { 0x87, 0x83, 0x81, 0x82, 0x8F, 0x8B, 0x89, 0x8A };
#define WORDCH(c) ((c) >= 4)
#define ADRPORT(c) ((c) < 4 ? (c) * 2 : 0xC0 + ((c) - 4) * 4)
#define CNTPORT(c) ((c) < 4 ? (c) * 2 + 1 : 0xC2 + ((c) - 4) * 4)
#define MASKPORT(c) ((c) < 4 ? 0x0A : 0xD4)
#define MODEPORT(c) ((c) < 4 ? 0x0B : 0xD6)
#define FFPORT(c) ((c) < 4 ? 0x0C : 0xD8)
#define STATPORT(c) ((c) < 4 ? 0x08 : 0xD0)

/* a channel: its owner, its region (the services' Set/Get_Region_Info),
 * the transfer programmed physically */
struct chan {
    vu32 ch;
    vu32 owned, cb;                     /* Virtualize_Channel's callback (0: none) */
    vu32 buf_id, locked, lin, size, phys;   /* the region */
    vu32 run_vm;                        /* the VM whose transfer it is (0: none) */
    vu32 run_mode;                      /* its mode */
    vu32 tc;                            /* a terminal count seen for it */
    vu32 virt_base;                     /* the VM's address register when programmed */
    vu32 ext;                           /* EISA extended mode (0-3) */
};
static struct chan chans[8];
#define HANDLE(c) ((vu32)&chans[c])
static struct chan *chan_of(vu32 h)
{
    vu32 i;
    for (i = 0; i < 8; i++) if (h == HANDLE(i)) return &chans[i];
    return NULL;
}

/* a VM's controllers (its control block) */
struct vch {
    vu16 base_adr, base_cnt, cur_adr, cur_cnt;
    vu8 page, mode, mask, req, disable;
    vu8 dh;                             /* Get_Virt_State's DH (below) */
    vu8 set;                            /* Set_Virt_State's lin, bytes hold */
    vu32 lin, bytes;
};
struct vdma {
    vu8 ff[2];                          /* the flip-flops */
    vu8 tc[2];                          /* terminal counts not yet read (status) */
    vu8 cmd[2], temp[2];
    struct vch c[8];
};
static vu32 cb_off;
#define VD(vm) ((struct vdma *)((vm) + cb_off))

static vu32 sys_vm;
static int inited;                      /* Init_Complete done: the buffer exists */

/* ---- the buffer ---- */
static vu32 buf_size, buf_max = ISA_LIMIT, buf_lin, buf_phys, buf_used;
static vu32 buf_want;                   /* Reserve_Buffer_Space's largest */
#define BUF_ID 1

static void buffer_alloc(void)
{
    vu32 kb = vdmad_link.buf_kb ? vdmad_link.buf_kb : 16, n, phys;
    buf_size = kb * 1024;
    if (buf_want > buf_size) buf_size = buf_want;
    buf_size = (buf_size + 4095) & ~4095u;
    if (buf_size > 0x10000) buf_size = 0x10000;
    if (vdmad_link.buf_conv && kb * 1024 >= buf_size) {
        buf_phys = vdmad_link.buf_conv;
        buf_lin = buf_phys;             /* the first megabyte: the same in every VM */
        return;
    }
    n = buf_size >> 12;
    /* within one 64 KB DMA page: aligned to the buffer's size rounded
     * up to a power of two */
    {
        vu32 al = 1;
        while (al < n) al <<= 1;
        buf_lin = vdmad_link.alloc_run ? vdmad_link.alloc_run(n, al, buf_max, &phys) : 0;
    }
    if (!buf_lin) { buf_size = 0; return; }
    buf_phys = phys;
}

/* ---- memory ---- */
static vu32 phys_of(vu32 lin)
{
    return vdmad_link.lin_to_phys ? vdmad_link.lin_to_phys(lin) : ~0u;
}

/* how many bytes of [lin, lin+n) are physically contiguous; *ph the first's
 * physical address (~0: not present) */
static vu32 contiguous(vu32 lin, vu32 n, vu32 *ph)
{
    vu32 p0 = phys_of(lin), done, p;
    *ph = p0;
    if (p0 == ~0u) return 0;
    done = 4096 - (lin & 0xFFF);
    while (done < n) {
        p = phys_of(lin + done);
        if (p != p0 + done) break;
        done += 4096;
    }
    return done < n ? done : n;
}

/* VDMAD_Lock_DMA_Region: CF and AL, ECX the lockable bytes, EDX phys */
static int lock_region(vu32 lin, vu32 n, vu32 align, vu32 *ecx, vu32 *edx, vu32 *err)
{
    vu32 ph, c = contiguous(lin, n, &ph), lim;
    *edx = ph;
    if (!c) { *ecx = 0; *err = DMA_LOCK_FAILED; return 0; }
    /* (Windows 3.1: a region that is not contiguous says so, even when
     * it also crosses the boundary where it stops being contiguous) */
    if (c < n) { *ecx = c; *err = DMA_NOT_CONTIGUOUS; return 0; }
    if (align == 1 || align == 2) {
        vu32 b = align == 1 ? 0x10000 : 0x20000;
        lim = b - (ph & (b - 1));       /* to the next boundary */
        if (n > lim) { *ecx = lim; *err = DMA_NOT_ALIGNED; return 0; }
    }
    *ecx = n;
    return 1;
}

/* ---- the physical controller ---- */
static void phys_mask(vu32 c, int on)
{
    if (c == 4) return;                 /* the cascade stays as it is */
    outb(MASKPORT(c), (on ? 4 : 0) | (c & 3));
}

/* program channel c for a transfer of size bytes at phys, mode m */
static void phys_program(vu32 c, vu32 phys, vu32 size, vu32 m)
{
    vu32 a, n;
    phys_mask(c, 1);
    outb(MODEPORT(c), (m & ~3u) | (c & 3));
    outb(FFPORT(c), 0);
    if (WORDCH(c)) { a = (phys >> 1) & 0xFFFF; n = (size >> 1) - 1; }
    else { a = phys & 0xFFFF; n = size - 1; }
    outb(ADRPORT(c), a & 0xFF);
    outb(ADRPORT(c), a >> 8);
    outb(CNTPORT(c), n & 0xFF);
    outb(CNTPORT(c), (n >> 8) & 0xFF);
    outb(pageport[c], (phys >> 16) & (WORDCH(c) ? 0xFE : 0xFF));
}

/* the physical controller's current address and count of channel c */
static void phys_current(vu32 c, vu32 *adr, vu32 *cnt)
{
    vu32 a1, a2, n1, n2, i;
    for (i = 0; i < 3; i++) {           /* a stable pair: read twice */
        outb(FFPORT(c), 0);
        a1 = inb(ADRPORT(c)); a1 |= inb(ADRPORT(c)) << 8;
        n1 = inb(CNTPORT(c)); n1 |= inb(CNTPORT(c)) << 8;
        outb(FFPORT(c), 0);
        a2 = inb(ADRPORT(c)); a2 |= inb(ADRPORT(c)) << 8;
        n2 = inb(CNTPORT(c)); n2 |= inb(CNTPORT(c)) << 8;
        if (a1 == a2 && n1 == n2) break;
    }
    *adr = a2; *cnt = n2;
}

/* the physical status of controller k: its terminal counts kept */
static vu32 phys_status(vu32 k)
{
    vu32 s = inb(k ? 0xD0 : 0x08), i;
    for (i = 0; i < 4; i++)
        if (s & (1u << i)) chans[k * 4 + i].tc = 1;
    return s;
}

/* ---- the virtual state as the services see it ---- */
/* the VM's address of channel c's transfer (bytes, in its first megabyte)
 * and its size in bytes */
/* (the current address and count: Windows 3.1's virtual state follows a
 * transfer the VM has read the progress of, or masked: VXM.386 saw the
 * address and count where an auto-initialised transfer had got to) */
static vu32 virt_addr(struct vch *v, vu32 c)
{
    if (WORDCH(c)) return ((vu32)(v->page & 0xFE) << 16) | ((vu32)v->cur_adr << 1);
    return ((vu32)v->page << 16) | v->cur_adr;
}
static vu32 virt_size(struct vch *v, vu32 c)
{
    return (((vu32)v->cur_cnt + 1) & 0x1FFFF) << (WORDCH(c) ? 1 : 0);
}
/* the virtual state's address (the high linear address, or with
 * translation disabled the physical one) and size */
static vu32 vstate_lin(vu32 vm, struct vch *v, vu32 c)
{
    if (v->set) return v->lin;
    return v->disable ? virt_addr(v, c) : CB_HIGH_LINEAR(vm) + virt_addr(v, c);
}
static vu32 vstate_bytes(struct vch *v, vu32 c)
{
    return v->set ? v->bytes : virt_size(v, c);
}
static vu32 virt_mode(struct vch *v)
{
    return (v->mode & ~3u) | (v->mask ? DMA_MASKED : 0) | (v->req ? DMA_REQUESTED : 0);
}

/* ---- a transfer's end: the buffer copied out, unlocked, released ---- */
static void finish(struct chan *ch, int copy)
{
    if (ch->buf_id == BUF_ID && buf_used) {
        if (copy && (ch->run_mode & MODE_TYPE) == MODE_WRITE && ch->size <= buf_size)
            memcpy_((void *)ch->lin, (void *)buf_lin, ch->size);
        buf_used = 0;
    }
    ch->buf_id = 0;
    ch->locked = 0;
    ch->lin = ch->size = ch->phys = 0;
    ch->run_vm = 0;
}

/* the default handler: the physical channel follows the VM's */
static void default_handler(vu32 h, vu32 vm)
{
    struct chan *ch = chan_of(h);
    struct vch *v;
    vu32 c, lin, size, ph, e, cnt, mode;
    struct vregs r;
    if (!ch || !vm) return;
    c = ch->ch;
    v = &VD(vm)->c[c];
    mode = virt_mode(v);
    if (mode & DMA_MASKED) {
        /* the VM's own transfer stops (a transfer to memory through the
         * buffer: what came is copied out); another VM's masking its
         * channel touches nobody else's transfer */
        if (ch->run_vm == vm) {
            phys_mask(c, 1);
            finish(ch, 1);
        }
        return;
    }
    if (ch->run_vm) {                   /* the transfer under way ends: the last unmask wins */
        phys_mask(c, 1);
        finish(ch, 1);
    }
    size = vstate_bytes(v, c);
    lin = vstate_lin(vm, v, c);
    if (v->disable) {                   /* the VM programs physical addresses */
        ch->buf_id = 0; ch->locked = 0; ch->lin = lin; ch->size = size; ch->phys = lin;
    } else {
        if (lock_region(lin, size, WORDCH(c) ? 2 : 1, &cnt, &ph, &e) && ph + size <= buf_max) {
            /* (Windows 3.1's region: BH FFh when locked) */
            ch->buf_id = 0; ch->locked = 0xFF; ch->lin = lin; ch->size = size; ch->phys = ph;
        } else if (size <= buf_size && !buf_used) {
            buf_used = 1;
            ch->buf_id = BUF_ID; ch->locked = 0; ch->lin = lin; ch->size = size; ch->phys = buf_phys;
            if ((mode & MODE_TYPE) == MODE_READ)
                memcpy_((void *)buf_lin, (void *)lin, size);
        } else {
            /* nowhere to put it: the VM is ended (Windows 3.1: "This
             * application has violated system integrity") */
            vregs_clear(&r);
            r.ebx = vm;
            vmm_crash_cur_vm(&r);
            return;
        }
    }
    ch->run_vm = vm;
    ch->run_mode = mode;
    ch->tc = 0;
    ch->virt_base = v->cur_adr;
    phys_program(c, ch->phys, size, mode);
    if (v->req) outb(c < 4 ? 0x09 : 0xD2, 4 | (c & 3));
    phys_mask(c, 0);
}

/* the VM's state of channel c changed: its owner is told */
static void changed(vu32 vm, vu32 c, vu32 ebp)
{
    struct vregs r;
    struct chan *ch = &chans[c];
    if (c == 4) return;                 /* the cascade */
    if (ch->owned && !ch->cb) return;   /* the owner programs it itself */
    vregs_clear(&r);
    r.eax = HANDLE(c);
    r.ebx = vm;
    r.ebp = ebp;
    if (ch->owned) vxd_call_proc(ch->cb, &r);
    else default_handler(HANDLE(c), vm);
}

/* ---- the ports ---- */
#define IO_OUTPUT 0x04

/* the address the VM reads back of channel c: its own while the channel
 * runs for it, from the physical controller's progress */
static void readback(vu32 vm, vu32 c, vu32 *adr, vu32 *cnt)
{
    struct vch *v = &VD(vm)->c[c];
    struct chan *ch = &chans[c];
    if (ch->run_vm == vm && c != 4) {
        vu32 pa, pc, base = WORDCH(c) ? (ch->phys >> 1) & 0xFFFF : ch->phys & 0xFFFF;
        phys_current(c, &pa, &pc);
        *adr = (ch->virt_base + ((pa - base) & 0xFFFF)) & 0xFFFF;
        *cnt = pc;
        v->cur_adr = (vu16)*adr;        /* the virtual state follows */
        v->cur_cnt = (vu16)*cnt;
        return;
    }
    *adr = v->cur_adr;
    *cnt = v->cur_cnt;
}

/* a channel running for vm: its virtual registers where it has got to */
static void sync(vu32 vm, vu32 c)
{
    vu32 a, n;
    if (chans[c].run_vm == vm && c != 4) readback(vm, c, &a, &n);
}

static vu32 port_in(vu32 vm, vu32 port)
{
    struct vdma *d = VD(vm);
    vu32 k = port >= 0xC0, c, i, adr, cnt, v;
    if (port >= 0x80 && port <= 0x8F) {
        for (c = 0; c < 8; c++) if (pageport[c] == port) return d->c[c].page;
        return inb(port);
    }
    if (k) i = (port - 0xC0) >> 1; else i = port;
    if (i < 8) {                        /* address, count */
        c = k * 4 + (i >> 1);
        readback(vm, c, &adr, &cnt);
        v = (i & 1) ? cnt : adr;
        v = d->ff[k] ? v >> 8 : v & 0xFF;
        d->ff[k] ^= 1;
        return v & 0xFF;
    }
    switch (i) {
    case 8: {                           /* status: terminal counts and requests */
        vu32 s = phys_status(k), out = 0;
        /* (Windows 3.1: the second controller's status read 00h in a box
         * all through a 16-bit auto-initialised transfer: no requests, no
         * terminal counts; UNVERIFIED for a single-cycle one) */
        if (k) s &= 0x0F;
        for (c = 0; c < 4; c++) {
            struct chan *ch = &chans[k * 4 + c];
            if (ch->run_vm == vm) {
                if (ch->tc) {
                    if (!k || !(ch->run_mode & MODE_AUTO)) d->tc[k] |= 1u << c;
                    ch->tc = 0;
                    if (!(ch->run_mode & MODE_AUTO)) {
                        /* a single transfer has ended: the VM's
                         * registers as the controller left them */
                        readback(vm, k * 4 + c, &adr, &cnt);
                        d->c[k * 4 + c].cur_adr = (vu16)adr;
                        d->c[k * 4 + c].cur_cnt = 0xFFFF;
                        finish(ch, 1);
                    }
                }
                out |= s & (0x10u << c);
            }
        }
        out |= d->tc[k];
        d->tc[k] = 0;
        return out;
    }
    case 13:                            /* temporary register */
        return d->temp[k];
    }
    return 0xFF;
}

static void port_out(vu32 vm, vu32 port, vu32 val, vu32 ebp)
{
    struct vdma *d = VD(vm);
    vu32 k = port >= 0xC0, c, i;
    val &= 0xFF;
    if (port >= 0x80 && port <= 0x8F) {
        for (c = 0; c < 8; c++)
            if (pageport[c] == port) { d->c[c].page = (vu8)val; d->c[c].set = 0; return; }
        outb(port, val);                /* the spare page registers, port 80h */
        return;
    }
    if (k) i = (port - 0xC0) >> 1; else i = port;
    if (i < 8) {                        /* address, count: base and current */
        struct vch *v;
        c = k * 4 + (i >> 1);
        v = &d->c[c];
        v->set = 0;
        if (i & 1) {
            v->base_cnt = d->ff[k] ? (v->base_cnt & 0xFF) | (val << 8) : (v->base_cnt & 0xFF00) | val;
            v->cur_cnt = v->base_cnt;
        } else {
            v->base_adr = d->ff[k] ? (v->base_adr & 0xFF) | (val << 8) : (v->base_adr & 0xFF00) | val;
            v->cur_adr = v->base_adr;
        }
        d->ff[k] ^= 1;
        return;
    }
    switch (i) {
    case 8:                             /* command */
        d->cmd[k] = (vu8)val;
        return;
    case 9:                             /* request */
        c = k * 4 + (val & 3);
        d->c[c].req = (val >> 2) & 1;
        changed(vm, c, ebp);
        return;
    case 10:                            /* single mask: the owner is told even
                                         * when it does not change (Windows 3.1) */
        c = k * 4 + (val & 3);
        sync(vm, c);
        d->c[c].mask = (val >> 2) & 1;
        changed(vm, c, ebp);
        return;
    case 11:                            /* mode */
        c = k * 4 + (val & 3);
        d->c[c].mode = (vu8)val;
        d->c[c].dh |= 0x04;             /* (Windows 3.1's DH: see vm_init) */
        return;
    case 12:                            /* clear the flip-flop */
        d->ff[k] = 0;
        return;
    case 13:                            /* master clear: all masked */
        d->ff[k] = 0;
        d->tc[k] = 0;
        for (c = k * 4; c < k * 4 + 4; c++) {
            sync(vm, c);
            d->c[c].mask = 1;
            d->c[c].req = 0;
            changed(vm, c, ebp);
        }
        return;
    case 14:                            /* clear the mask register */
    case 15:                            /* write all mask bits */
        for (c = k * 4; c < k * 4 + 4; c++) {
            vu32 m = i == 14 ? 0 : (val >> (c & 3)) & 1;
            sync(vm, c);
            d->c[c].mask = (vu8)m;
            changed(vm, c, ebp);
        }
        return;
    }
}

/* the I/O handler: EBX the VM, ECX the type, EDX the port, EAX the data */
void __cdecl vdmad_iohandler(struct vregs *r)
{
    if ((r->ecx & 0x18) == 0) {
        if (r->ecx & IO_OUTPUT) port_out(r->ebx, r->edx & 0xFFFF, r->eax, r->ebp);
        else r->eax = (r->eax & ~0xFFu) | port_in(r->ebx, r->edx & 0xFFFF);
        return;
    }
    /* words and dwords: byte by byte */
    {
        vu32 n = (r->ecx & 0x10) ? 4 : 2, i, v = 0;
        for (i = 0; i < n; i++) {
            if (r->ecx & IO_OUTPUT) port_out(r->ebx, (r->edx + i) & 0xFFFF, r->eax >> (i * 8), r->ebp);
            else v |= port_in(r->ebx, (r->edx + i) & 0xFFFF) << (i * 8);
        }
        if (!(r->ecx & IO_OUTPUT)) r->eax = n == 4 ? v : (r->eax & ~0xFFFFu) | v;
    }
}

/* VDMAD_Default_Handler as a procedure (vdmad_def): EAX, EBX */
void __cdecl vdmad_default(struct vregs *r)
{
    default_handler(r->eax, r->ebx);
}

/* ---- the services ---- */
#define CF(r) ((r)->eflags |= VF_CF)
static void ok(struct vregs *r) { r->eflags &= ~VF_CF; }
static void fail(struct vregs *r, vu32 e) { r->eflags |= VF_CF; r->eax = (r->eax & ~0xFFu) | e; }

static int buf_check(struct vregs *r)
{
    if (r->ebx != BUF_ID || !buf_used) { fail(r, DMA_INVALID_BUFFER); return 0; }
    if (r->edi + r->ecx > buf_size || r->edi + r->ecx < r->edi) { fail(r, DMA_COPY_OUT_RANGE); return 0; }
    return 1;
}

/* Scatter_Lock: the extended DDS at EDI (size, offset (the linear
 * address), selector, buffer ID, entries available and used, then the
 * table); AL bit 0 the page table entries instead of regions */
#pragma pack(1)
struct dds { vu32 size, off; vu16 seg, id, avail, used; vu32 e[1]; };
#pragma pack()
static void scatter_lock(struct vregs *r, struct dds *d, vu32 lin)
{
    vu32 n = d->size, used = 0, need = 0, done = 0, a = lin, ph, c;
    if (r->eax & 1) {                   /* page table entries */
        vu32 first = lin & ~0xFFFu, last = (lin + n + 4095) & ~0xFFFu, p;
        for (p = first; p < last; p += 4096) {
            ph = phys_of(p);
            if (need < d->avail) { d->e[need] = ph == ~0u ? 0 : ((ph & ~0xFFFu) | 1); used++; }
            need++;
        }
        r->esi = lin & 0xFFF;
        done = used == need ? n : used * 4096 - (lin & 0xFFF);
    } else {
        r->esi = 0;
        while (done < n) {
            c = contiguous(a, n - done, &ph);
            if (!c) break;
            if (need < d->avail) { d->e[need * 2] = ph; d->e[need * 2 + 1] = c; used++; done += c; }
            need++;
            a += c;
            if (need >= d->avail && need > used) {
                /* count what is left */
                while (a < lin + n && (c = contiguous(a, lin + n - a, &ph)) != 0) { need++; a += c; }
                break;
            }
        }
    }
    d->used = (vu16)used;
    r->edx = need;
    ok(r);
    if (done >= n) r->eflags |= VF_ZF; else { r->eflags &= ~VF_ZF; d->size = done; }
}

void __cdecl vdmad_service(struct vregs *r)
{
    struct chan *ch;
    vu32 cnt, ph, e;
    ok(r);
    switch (r->arg) {
    case 0x00:                          /* VDMAD_Get_Version: Windows 3.1's
                                         * AX=030Ah, ECX 0 (also after Init_Complete) */
        r->eax = (r->eax & 0xFFFF0000u) | 0x030A;
        r->ecx = 0;
        return;
    case 0x01:                          /* VDMAD_Virtualize_Channel: EAX, ESI; one
                                         * taken (or the cascade): carry, EAX its handle */
        if (r->eax > 7) { CF(r); return; }
        if (r->eax == 4 || chans[r->eax].owned) { r->eax = HANDLE(r->eax); CF(r); return; }
        chans[r->eax].owned = 1;
        chans[r->eax].cb = r->esi;
        r->eax = HANDLE(r->eax);
        return;
    case 0x02:                          /* VDMAD_Get_Region_Info */
        if ((ch = chan_of(r->eax)) == NULL) { CF(r); return; }
        r->ebx = (r->ebx & 0xFFFF0000u) | ((ch->locked & 0xFF) << 8) | (ch->buf_id & 0xFF);
        r->esi = ch->lin;
        r->ecx = ch->size;
        return;
    case 0x03:                          /* VDMAD_Set_Region_Info */
        if ((ch = chan_of(r->eax)) == NULL) { CF(r); return; }
        ch->buf_id = r->ebx & 0xFF;
        ch->locked = (r->ebx >> 8) & 0xFF;
        ch->lin = r->esi;
        ch->size = r->ecx;
        ch->phys = r->edx;
        return;
    case 0x04: {                        /* VDMAD_Get_Virt_State */
        struct vch *v;
        if ((ch = chan_of(r->eax)) == NULL || !r->ebx) { CF(r); return; }
        v = &VD(r->ebx)->c[ch->ch];
        r->esi = vstate_lin(r->ebx, v, ch->ch);
        r->ecx = vstate_bytes(v, ch->ch);
        r->edx = (r->edx & 0xFFFF0000u) | ((vu32)v->dh << 8) | virt_mode(v);
        return;
    }
    case 0x05: {                        /* VDMAD_Set_Virt_State */
        struct vch *v;
        vu32 a, c;
        if ((ch = chan_of(r->eax)) == NULL || !r->ebx) { CF(r); return; }
        c = ch->ch;
        v = &VD(r->ebx)->c[c];
        a = v->disable ? r->esi : r->esi - CB_HIGH_LINEAR(r->ebx);
        if (WORDCH(c)) { v->base_adr = (vu16)(a >> 1); v->page = (vu8)((a >> 16) & 0xFE); v->base_cnt = (vu16)((r->ecx >> 1) - 1); }
        else { v->base_adr = (vu16)a; v->page = (vu8)(a >> 16); v->base_cnt = (vu16)(r->ecx - 1); }
        v->cur_adr = v->base_adr; v->cur_cnt = v->base_cnt;
        v->mode = (vu8)((r->edx & 0xFC) | (c & 3));
        v->mask = r->edx & DMA_MASKED ? 1 : 0;
        v->req = r->edx & DMA_REQUESTED ? 1 : 0;
        v->dh = (vu8)(r->edx >> 8);
        /* Get_Virt_State gives back what was set (Windows 3.1) */
        v->set = 1; v->lin = r->esi; v->bytes = r->ecx;
        return;
    }
    case 0x06:                          /* VDMAD_Set_Phys_State: EAX, EBX, DL */
        if ((ch = chan_of(r->eax)) == NULL) { CF(r); return; }
        ch->run_vm = r->ebx;
        ch->run_mode = r->edx & 0xFF;
        ch->tc = 0;
        phys_program(ch->ch, ch->phys, ch->size, r->edx & 0xFF);
        return;
    case 0x07:                          /* VDMAD_Mask_Channel */
        if ((ch = chan_of(r->eax)) == NULL) { CF(r); return; }
        phys_mask(ch->ch, 1);
        return;
    case 0x08:                          /* VDMAD_UnMask_Channel */
        if ((ch = chan_of(r->eax)) == NULL) { CF(r); return; }
        ch->run_vm = r->ebx;
        phys_mask(ch->ch, 0);
        return;
    case 0x09:                          /* VDMAD_Lock_DMA_Region: ESI, ECX, DL;
                                         * a failure leaves EDX, a success AL */
        if (!lock_region(r->esi, r->ecx, r->edx & 0xFF, &cnt, &ph, &e)) {
            fail(r, e);
            r->ecx = cnt;
            return;
        }
        r->ecx = cnt;
        r->edx = ph;
        return;
    case 0x0A:                          /* VDMAD_Unlock_DMA_Region */
    case 0x17:                          /* VDMAD_Unlock_DMA_Region_No_Dirty */
        if (phys_of(r->esi) == ~0u) CF(r);  /* (nothing pages here: no counts) */
        return;
    case 0x0B:                          /* VDMAD_Scatter_Lock: EBX, AL, EDI */
        scatter_lock(r, (struct dds *)r->edi, ((struct dds *)r->edi)->off);
        return;
    case 0x0C:                          /* VDMAD_Scatter_Unlock */
        return;
    case 0x0D:                          /* VDMAD_Reserve_Buffer_Space: EAX pages, ECX max */
        if (inited) { CF(r); return; }
        if (r->eax * 4096 > buf_want) buf_want = r->eax * 4096;
        if (r->ecx && r->ecx < buf_max) buf_max = r->ecx;
        return;
    case 0x0E:                          /* VDMAD_Request_Buffer: ESI, ECX */
        if (r->ecx > buf_size || !buf_size) { fail(r, DMA_BUFFER_TOO_SMALL); return; }
        if (buf_used) { fail(r, DMA_BUFFER_IN_USE); return; }
        buf_used = 1;
        r->ebx = BUF_ID;
        r->edx = buf_phys;
        return;
    case 0x0F:                          /* VDMAD_Release_Buffer: EBX */
        if (r->ebx != BUF_ID || !buf_used) { CF(r); return; }
        buf_used = 0;
        return;
    case 0x10:                          /* VDMAD_Copy_To_Buffer: EBX, ESI, EDI, ECX */
        if (buf_check(r)) memcpy_((void *)(buf_lin + r->edi), (void *)r->esi, r->ecx);
        return;
    case 0x11:                          /* VDMAD_Copy_From_Buffer */
        if (buf_check(r)) memcpy_((void *)r->esi, (void *)(buf_lin + r->edi), r->ecx);
        return;
    case 0x12:                          /* VDMAD_Default_Handler: EAX, EBX */
        default_handler(r->eax, r->ebx);
        return;
    case 0x13:                          /* VDMAD_Disable_Translation: EAX, EBX */
    case 0x14: {                        /* VDMAD_Enable_Translation */
        struct vch *v;
        if ((ch = chan_of(r->eax)) == NULL || !r->ebx) { CF(r); return; }
        v = &VD(r->ebx)->c[ch->ch];
        if (r->arg == 0x13) {
            if (v->disable == 0xFF) { CF(r); return; }
            v->disable++;
            return;
        }
        if (!v->disable) { CF(r); return; }
        v->disable--;
        /* Windows 3.1: zero set when the count reaches 0 (translation on) */
        if (v->disable) r->eflags &= ~VF_ZF; else r->eflags |= VF_ZF;
        return;
    }
    case 0x15:                          /* VDMAD_Get_EISA_Adr_Mode: EAX the channel */
        if (r->eax > 7) { CF(r); return; }
        r->ecx = (r->ecx & ~0xFFu) | chans[r->eax].ext;
        return;
    case 0x16:                          /* VDMAD_Set_EISA_Adr_Mode: EAX, CL */
        if (r->eax > 7) { CF(r); return; }
        chans[r->eax].ext = r->ecx & 3;
        return;
    }
    CF(r);
}

/* ---- the Virtual DMA Services: INT 4Bh in V86 mode (Hook_V86_Int_Chain:
 * EAX the interrupt, EBX the VM, EBP the client's registers; carry clear
 * when it is handled) ---- */
static vu32 vlin(vu32 vm, vu32 seg, vu32 off)
{
    return CB_HIGH_LINEAR(vm) + ((seg & 0xFFFF) << 4) + (off & 0xFFFF);
}
static void vds_err(struct client_regs *c, vu32 e)
{
    c->eax = (c->eax & ~0xFFu) | e;
    c->eflags |= VF_CF;
}
void __cdecl vdmad_vds_hook(struct vregs *r)
{
    struct client_regs *c = (struct client_regs *)r->ebp;
    vu32 vm = r->ebx, fn, cnt, ph, e;
    struct dds *d;
    if (((c->eax >> 8) & 0xFF) != 0x81) { r->eflags |= VF_CF; return; }   /* not ours */
    r->eflags &= ~VF_CF;
    fn = c->eax & 0xFF;
    c->eflags &= ~VF_CF;
    d = (struct dds *)vlin(vm, c->es, c->edi);
    switch (fn) {
    case 0x02:                          /* get version */
        c->eax = (c->eax & 0xFFFF0000u) | 0x0100;
        c->ebx = (c->ebx & 0xFFFF0000u) | 0x0003;
        c->ecx = (c->ecx & 0xFFFF0000u) | 0x0200;
        c->esi = (c->esi & 0xFFFF0000u) | (buf_size >> 16);
        c->edi = (c->edi & 0xFFFF0000u) | (buf_size & 0xFFFF);
        c->edx &= 0xFFFF0000u;
        return;
    case 0x03: {                        /* lock DMA region: DX flags */
        vu32 lin = vlin(vm, d->seg, d->off), fl = c->edx, al = (fl & 0x10) ? 1 : (fl & 0x20) ? 2 : 0;
        d->id = 0;
        if (lock_region(lin, d->size, al, &cnt, &ph, &e) && ph + d->size <= ISA_LIMIT) {
            *(vu32 *)&d->avail = ph;
            return;
        }
        if (fl & 4) {                   /* no buffer: the error, the lockable size */
            d->size = cnt;
            vds_err(c, e);
            return;
        }
        if (d->size > buf_size) { vds_err(c, DMA_BUFFER_TOO_SMALL); return; }
        if (buf_used) { vds_err(c, DMA_BUFFER_IN_USE); return; }
        buf_used = 1;
        d->id = BUF_ID;
        *(vu32 *)&d->avail = buf_phys;
        if (fl & 2) memcpy_((void *)buf_lin, (void *)lin, d->size);
        return;
    }
    case 0x04:                          /* unlock: DX bit 1 copies out */
        if (d->id) {
            if (d->id != BUF_ID || !buf_used) { vds_err(c, DMA_INVALID_BUFFER); return; }
            if ((c->edx & 2) && d->size <= buf_size)
                memcpy_((void *)vlin(vm, d->seg, d->off), (void *)buf_lin, d->size);
            buf_used = 0;
        }
        return;
    case 0x05: {                        /* scatter/gather lock */
        struct vregs s;
        vregs_clear(&s);
        s.eax = (c->edx >> 6) & 1;      /* bit 6: page table entries */
        scatter_lock(&s, d, vlin(vm, d->seg, d->off));
        if (s.eax & 1) c->ebx = (c->ebx & 0xFFFF0000u) | (s.esi & 0xFFFF);
        return;
    }
    case 0x06:                          /* scatter/gather unlock */
        return;
    case 0x07:                          /* request DMA buffer: DX bit 1 copies in */
        if (d->size > buf_size || !buf_size) { vds_err(c, DMA_BUFFER_TOO_SMALL); return; }
        if (buf_used) { vds_err(c, DMA_BUFFER_IN_USE); return; }
        buf_used = 1;
        d->id = BUF_ID;
        *(vu32 *)&d->avail = buf_phys;
        if (c->edx & 2) memcpy_((void *)buf_lin, (void *)vlin(vm, d->seg, d->off), d->size);
        return;
    case 0x08:                          /* release DMA buffer: DX bit 1 copies out */
        if (d->id != BUF_ID || !buf_used) { vds_err(c, DMA_INVALID_BUFFER); return; }
        if ((c->edx & 2) && d->size <= buf_size)
            memcpy_((void *)vlin(vm, d->seg, d->off), (void *)buf_lin, d->size);
        buf_used = 0;
        return;
    case 0x09:                          /* copy into the buffer: BX:CX its offset */
    case 0x0A: {                        /* copy out of it */
        vu32 off = ((c->ebx & 0xFFFF) << 16) | (c->ecx & 0xFFFF);
        if (d->id != BUF_ID || !buf_used) { vds_err(c, DMA_INVALID_BUFFER); return; }
        if (off + d->size > buf_size) { vds_err(c, DMA_COPY_OUT_RANGE); return; }
        if (fn == 9) memcpy_((void *)(buf_lin + off), (void *)vlin(vm, d->seg, d->off), d->size);
        else memcpy_((void *)vlin(vm, d->seg, d->off), (void *)(buf_lin + off), d->size);
        return;
    }
    case 0x0B:                          /* disable DMA translation: BX the channel */
    case 0x0C: {                        /* enable it */
        vu32 ch = c->ebx & 0xFFFF;
        struct vch *v;
        if (ch > 7) { vds_err(c, DMA_INVALID_CHANNEL); return; }
        v = &VD(vm)->c[ch];
        if (fn == 0x0B) {
            if (v->disable == 0xFF) { vds_err(c, DMA_DISABLE_OVERFLOW); return; }
            v->disable++;
            return;
        }
        if (!v->disable) { vds_err(c, DMA_DISABLE_UNDERFLOW); return; }
        v->disable--;
        if (v->disable) c->eflags &= ~VF_ZF; else c->eflags |= VF_ZF;
        return;
    }
    }
    vds_err(c, DMA_FUNC_UNSUPPORTED);
}

/* ---- the control messages ---- */
static const vu16 ports[] = {
    0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07,
    0x08, 0x09, 0x0A, 0x0B, 0x0C, 0x0D, 0x0E, 0x0F,
    0xC0, 0xC2, 0xC4, 0xC6, 0xC8, 0xCA, 0xCC, 0xCE,
    0xD0, 0xD2, 0xD4, 0xD6, 0xD8, 0xDA, 0xDC, 0xDE,
    0x81, 0x82, 0x83, 0x87, 0x89, 0x8A, 0x8B, 0xFFFF     /* (not 8Fh, the cascade's:
                                                           Windows 3.1 leaves it) */
};

static void vm_init(vu32 vm)
{
    struct vdma *d = VD(vm);
    vu32 c;
    vu8 *p = (vu8 *)d;
    for (c = 0; c < sizeof(*d); c++) p[c] = 0;
    /* Windows 3.1's first state of a channel (Get_Virt_State): masked,
     * mode 40h (single), count 0; DH 40h for channels 5-7, and 04h added
     * once the VM has written the channel's mode (UNVERIFIED what these
     * two bits are) */
    for (c = 0; c < 8; c++) {
        d->c[c].mask = 1;
        d->c[c].mode = 0x40 | (c & 3);
        d->c[c].dh = WORDCH(c) ? 0x40 : 0;
    }
    d->c[4].mask = 0;                   /* the cascade: open, as the BIOS leaves it */
}

void __cdecl vdmad_control(struct vregs *r)
{
    struct vregs s;
    vu32 i;
    r->eflags &= ~VF_CF;
    switch (r->eax) {
    case SYS_CRITICAL_INIT:
        vregs_clear(&s);
        vmm_get_sys_vm(&s);
        sys_vm = s.ebx;
        cb_off = vmm_alloc_cb_area(sizeof(struct vdma), 0);
        if (!cb_off) { r->eflags |= VF_CF; return; }
        for (i = 0; i < 8; i++) {
            chans[i].ch = i;
            chans[i].ext = WORDCH(i) ? 1 : 0;
        }
        vm_init(sys_vm);
        /* the buffer now: Windows 3.1's is there for the devices'
         * Init_Complete (VXM.386 had it then: Request_Buffer gave ID 1),
         * which comes before VDMAD's */
        buffer_alloc();
        for (i = 0; ports[i] != 0xFFFF; i++) {
            vregs_clear(&s);
            s.edx = ports[i];
            s.esi = (vu32)vdmad_io;
            vmm_install_io(&s);
        }
        vregs_clear(&s);
        s.eax = 0x4B;
        s.esi = (vu32)vdmad_vds;
        vmm_hook_v86_int(&s);
        /* the Virtual DMA Services are there: 40:7Bh bit 5, and bit 3
         * (Windows 3.1's boxes read 28h there; the VDS specification:
         * INT 4Bh is chained) */
        *(vu8 *)0x47B |= 0x28;
        break;
    case INIT_COMPLETE:
        /* a larger buffer for what devices reserved (the first one stays
         * unused) */
        if (buf_want > buf_size && !buf_used) buffer_alloc();
        inited = 1;
        break;
    case CREATE_VM:
        vm_init(r->ebx);
        break;
    case DESTROY_VM:
        for (i = 0; i < 8; i++)
            if (chans[i].run_vm == r->ebx) {
                phys_mask(i, 1);
                finish(&chans[i], 0);
            }
        break;
    case SYSTEM_EXIT:
        for (i = 0; i < 8; i++)
            if (chans[i].run_vm) { phys_mask(i, 1); finish(&chans[i], 0); }
        break;
    }
}
