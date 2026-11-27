/* dosx.c - w16 VMM: INT 21h from protected mode (ARCHITECTURE.md 12.3).
 *
 * A protected-mode program under 386 Enhanced mode calls DOS with INT 21h
 * and protected-mode pointers. Functions whose pointers address memory a
 * V86 DOS cannot see are carried out through the transfer buffer in the
 * loader's data segment: parameters copied in, results copied out, reads
 * and writes in pieces. INT 21h 25h/35h set and get the protected-mode
 * vectors (as DPMI 0204h/0205h), the DTA is the program's own.
 *
 * Sources: MS-DOS Programmer's Reference (the INT 21h functions and which
 * registers carry pointers); DPMI 0.9 (what a host translates is the
 * host's choice: this list follows what KRNL386 is seen to call, see the
 * VMM trace, and grows with it).
 */
#include "vmm.h"

extern int dpmi_verbose;

#define dta_sel (cur_vm->dp.dta_sel)    /* the program's DTA (1Ah) */
#define dta_off (cur_vm->dp.dta_off)
#define dta_set (cur_vm->dp.dta_set)

#define XB      (xbuf_lin())
#define XSEG    (xbuf_seg())

/* Can V86 DOS address sel:off as it is? (conventional memory) */
static int direct(u32 sel)
{
    return sel_to_seg(sel) >= 0;
}

/* Run INT 21h with the client's registers, DS and ES given (segments);
 * the results come back except the registers in keep (bit mask: 1 EDX,
 * 2 ESI, 4 EDI) which the call used as pointers. */
#define K_DX 1
#define K_SI 2
#define K_DI 4
static struct cregs call21(struct cregs *r, u32 ds, u32 es, u32 dx, u32 si, u32 di, int keep)
{
    struct cregs v = *r, pm = *r;
    v.ss = 0; v.esp = 0;
    v.ds = ds; v.es = es; v.fs = v.gs = 0;
    if (keep & K_DX) v.edx = dx;
    if (keep & K_SI) v.esi = si;
    if (keep & K_DI) v.edi = di;
    v86_int(&v, 0x21);
    *r = pm;
    r->eax = v.eax; r->ebx = v.ebx; r->ecx = v.ecx;
    if (!(keep & K_DX)) r->edx = v.edx;
    if (!(keep & K_SI)) r->esi = v.esi;
    if (!(keep & K_DI)) r->edi = v.edi;
    r->eflags = (r->eflags & ~0x8D5u) | (v.eflags & 0x8D5u);
    return v;
}

/* A pointer the client passed that the VMM cannot use (cl_ptr): the call
 * fails with carry and AX 9 (UNVERIFIED: Windows 3.1 itself ended with
 * such pointers in the DPMI calls tried, VX11 runs P and S) */
static void bad_ptr(struct cregs *r)
{
    r->eflags |= EFL_CF;
    SETW(r->eax, 9);
}

/* an ASCIIZ string at sel:off into the buffer at offset at; its length,
 * or -1 when the client's memory there cannot be read */
static int copy_in_str(u32 sel, u32 off, u32 at)
{
    u8 *s;
    u32 n = 0, lin;
    s = cl_ptr(sel, off, 1, 0);
    if (!s) return -1;
    lin = (u32)s;
    for (;;) {
        u8 c;
        if (n > 0) {
            if (!cur_vm->client32 && ((off + n) & 0xFFFF) == 0) {
                s = cl_ptr(sel, 0, 1, 0);
                if (!s) return -1;
                lin = (u32)s;
            } else {
                lin++;
                if ((lin & 0xFFF) == 0 && !lin_user_ok(lin, 1, 0)) return -1;
            }
        }
        c = *(u8 *)lin;
        if (!c || n == 255) break;
        *(u8 *)(XB + at + n) = c;
        n++;
    }
    *(u8 *)(XB + at + n) = 0;
    if (dpmi_verbose) trace("{'%s'}", (char *)(XB + at));
    return (int)n;
}

/* the client's sel:off for n bytes (16-bit offsets that run past FFFFh
 * fail: the processor would not wrap them either) */
static u8 *cptr(struct cregs *r, u32 sel, u32 off, u32 n, int write)
{
    (void)r;
    if (!cur_vm->client32 && (off & 0xFFFF) + n > 0x10000) return NULL;
    return cl_ptr(sel, off, n, write);
}

/* DOS's current DTA (in V86 mode: INT 21h 2Fh) */
static void dos_dta(u32 *seg, u32 *off)
{
    struct cregs t;
    memset(&t, 0, sizeof(t));
    t.eax = 0x2F00;
    v86_int(&t, 0x21);
    *seg = t.es & 0xFFFF;
    *off = t.ebx & 0xFFFF;
}

static void set_dos_dta(u32 seg, u32 off)
{
    struct cregs t;
    memset(&t, 0, sizeof(t));
    t.eax = 0x1A00;
    t.ds = seg;
    t.edx = off;
    v86_int(&t, 0x21);
}

void dos_pm(struct cregs *r)
{
    u32 ah = (AX(r) >> 8) & 0xFF, al = AX(r) & 0xFF;
    struct cregs v;

    vm_dos_cwd();                       /* the VM's own current directory (vm.c) */

    if (dpmi_verbose) trace("[D21:%x %x %x %x es %x at %x:%x]", AX(r), BX(r), CX(r), DX(r), r->es, r->cs, r->eip);

    switch (ah) {
    case 0x25:                          /* set the protected-mode vector */
        cur_vm->pmvec_sel[al] = r->ds;
        cur_vm->pmvec_off[al] = cur_vm->client32 ? r->edx : DX(r);
        return;
    case 0x35:                          /* get it */
        r->es = cur_vm->pmvec_sel[al];
        if (cur_vm->client32) r->ebx = cur_vm->pmvec_off[al];
        else SETW(r->ebx, cur_vm->pmvec_off[al]);
        return;
    case 0x1A:                          /* set DTA: the program's, kept here */
        dta_sel = r->ds;
        dta_off = cur_vm->client32 ? r->edx : DX(r);
        dta_set = 1;
        return;
    case 0x2F:
        if (!dta_set) {                 /* DOS's own (the PSP's) until set */
            /* in a DOS box as a selector (Windows 3.1, DPMIPRB: the PSP's,
             * 9Fh, though the caller's ES mapped the same segment) */
            u32 seg, off, sel;
            if (cur_vm == &sysvm) break;
            dos_dta(&seg, &off);
            sel = seg_to_sel(seg, 0xF2);
            if (!sel) break;
            r->es = sel;
            if (cur_vm->client32) r->ebx = off; else SETW(r->ebx, off);
            return;
        }
        r->es = dta_sel;
        if (cur_vm->client32) r->ebx = dta_off;
        else SETW(r->ebx, dta_off);
        return;
    case 0x09: {                        /* print a $-terminated string */
        u8 *s;
        u32 n = 0;
        if (direct(r->ds)) break;
        while (n < xbuf_size() - 1 && (s = cl_ptr(r->ds, r->edx + n, 1, 0)) != NULL && *s != '$')
            *(u8 *)(XB + n++) = *s;
        *(u8 *)(XB + n) = '$';
        call21(r, XSEG, 0, 0, 0, 0, K_DX);
        return;
    }
    case 0x39: case 0x3A: case 0x3B: case 0x3C: case 0x3D: case 0x41:
    case 0x43: case 0x5B:               /* a path at DS:DX */
        if (direct(r->ds)) break;
        if (copy_in_str(r->ds, r->edx, 0) < 0) { bad_ptr(r); return; }
        call21(r, XSEG, 0, 0, 0, 0, K_DX);
        return;
    case 0x5A: {                        /* temporary file: the path comes back */
        int n;
        u8 *d;
        if (direct(r->ds)) break;
        n = copy_in_str(r->ds, r->edx, 0);
        if (n < 0 || (d = cptr(r, r->ds, r->edx, n + 14, 1)) == NULL) { bad_ptr(r); return; }
        call21(r, XSEG, 0, 0, 0, 0, K_DX);
        if (!(r->eflags & EFL_CF)) memcpy(d, (void *)XB, n + 14);
        return;
    }
    case 0x6C:                          /* extended open: DS:SI */
        if (direct(r->ds)) break;
        if (copy_in_str(r->ds, r->esi, 0) < 0) { bad_ptr(r); return; }
        call21(r, XSEG, 0, 0, 0, 0, K_SI);
        return;
    case 0x56:                          /* rename: DS:DX, ES:DI */
        if (direct(r->ds) && direct(r->es)) break;
        if (copy_in_str(r->ds, r->edx, 0) < 0 || copy_in_str(r->es, r->edi, 256) < 0) { bad_ptr(r); return; }
        call21(r, XSEG, XSEG, 0, 0, 256, K_DX | K_DI);
        return;
    case 0x47: {                        /* current directory to DS:SI */
        u8 *d;
        if (direct(r->ds)) break;
        if ((d = cptr(r, r->ds, r->esi, 64, 1)) == NULL) { bad_ptr(r); return; }
        call21(r, XSEG, 0, 0, 0, 0, K_SI);
        if (!(r->eflags & EFL_CF)) memcpy(d, (void *)XB, 64);
        if (dpmi_verbose) trace("{cwd '%s'}", (char *)XB);
        return;
    }
    case 0x60: {                        /* truename: DS:SI to ES:DI */
        u8 *d;
        if (direct(r->ds) && direct(r->es)) break;
        if (copy_in_str(r->ds, r->esi, 0) < 0 || (d = cptr(r, r->es, r->edi, 128, 1)) == NULL) { bad_ptr(r); return; }
        call21(r, XSEG, XSEG, 0, 0, 256, K_SI | K_DI);
        if (!(r->eflags & EFL_CF)) memcpy(d, (void *)(XB + 256), 128);
        return;
    }
    case 0x38: {                        /* country information to DS:DX */
        u8 *d;
        if (DX(r) == 0xFFFF || direct(r->ds)) break;
        if ((d = cptr(r, r->ds, r->edx, 34, 1)) == NULL) { bad_ptr(r); return; }
        call21(r, XSEG, 0, 0, 0, 0, K_DX);
        if (!(r->eflags & EFL_CF)) memcpy(d, (void *)XB, 34);
        return;
    }
    case 0x48: case 0x49: case 0x4A:    /* the host's memory (dpmi.c) */
        if (dpmi_dosmem(r)) return;
        break;
    case 0x3F: {                        /* read, in pieces */
        /* a 32-bit client's count is ECX and its result all of EAX
         * (Windows 3.1 in a DOS box, VXCD.386 under Visual C++'s
         * DOSXNT: 3F70h with EAX's high word 3 came back as EAX 2; DOSXNT
         * took w16's 00030002h for a short read and gave up on CL.EXE) */
        u32 want = cur_vm->client32 ? r->ecx : CX(r), done = 0, got;
        u8 *dst;
        if (direct(r->ds) && !cur_vm->client32) break;
        if ((dst = cptr(r, r->ds, r->edx, want, 1)) == NULL) { bad_ptr(r); return; }
        do {
            u32 n = want - done > xbuf_size() ? xbuf_size() : want - done;
            struct cregs t = *r;
            SETW(t.eax, 0x3F00);
            SETW(t.ecx, n);
            call21(&t, XSEG, 0, 0, 0, 0, K_DX);
            if (t.eflags & EFL_CF) {
                if (!done) { *r = t; if (cur_vm->client32) r->eax &= 0xFFFF; return; }
                break;
            }
            got = AX(&t);
            memcpy(dst + done, (void *)XB, got);
            done += got;
            if (got < n) break;
        } while (done < want);
        if (cur_vm->client32) r->eax = done; else SETW(r->eax, done);
        r->eflags &= ~EFL_CF;
        return;
    }
    case 0x40: {                        /* write, in pieces (CX=0 truncates: one call) */
        u32 want = cur_vm->client32 ? r->ecx : CX(r), done = 0, put;    /* (as 3Fh) */
        u8 *src;
        if (direct(r->ds) && !cur_vm->client32) break;
        if ((src = cptr(r, r->ds, r->edx, want, 0)) == NULL) { bad_ptr(r); return; }
        do {
            u32 n = want - done > xbuf_size() ? xbuf_size() : want - done;
            struct cregs t = *r;
            memcpy((void *)XB, src + done, n);
            SETW(t.eax, 0x4000);
            SETW(t.ecx, n);
            call21(&t, XSEG, 0, 0, 0, 0, K_DX);
            if (t.eflags & EFL_CF) {
                if (!done) { *r = t; if (cur_vm->client32) r->eax &= 0xFFFF; return; }
                break;
            }
            put = AX(&t);
            done += put;
            if (put < n) break;
        } while (done < want);
        if (cur_vm->client32) r->eax = done; else SETW(r->eax, done);
        r->eflags &= ~EFL_CF;
        return;
    }
    case 0x44:                          /* IOCTL: the ones with a DS:DX buffer */
        if (al == 0x0D && (CL(r) == 0x41 || CL(r) == 0x61)) {
            /* write/read track: the block (special, head, cylinder, first
             * sector, count, then a far pointer to the data) with its
             * pointer translated, the data through the buffer, 512 bytes a
             * sector (Windows 3.1 reads a track into a buffer outside DOS
             * memory this way: VX11 "io" on MS-DOS 4.00) */
            u8 *blk = cptr(r, r->ds, r->edx, 13, 1), *data;
            u32 n;
            if (!blk) { bad_ptr(r); return; }
            n = *(u16 *)(blk + 7) * 512u;
            if (n + 16 > xbuf_size()) { bad_ptr(r); return; }
            data = cl_ptr(*(u16 *)(blk + 11), *(u16 *)(blk + 9), n, CL(r) == 0x61);
            if (!data && n) { bad_ptr(r); return; }
            memcpy((void *)XB, blk, 13);
            *(u16 *)(XB + 9) = 16;
            *(u16 *)(XB + 11) = (u16)XSEG;
            if (CL(r) == 0x41) memcpy((void *)(XB + 16), data, n);
            call21(r, XSEG, 0, 0, 0, 0, K_DX);
            if (CL(r) == 0x61 && !(r->eflags & EFL_CF)) memcpy(data, (void *)(XB + 16), n);
            return;
        }
        if (direct(r->ds)) break;
        if (al == 2 || al == 3 || al == 4 || al == 5) {
            /* the count DOS gets is what the buffer here holds (it went
             * to DOS as the client's, and a read past 16 KB wrote past
             * the buffer) */
            u32 n = CX(r) > xbuf_size() ? xbuf_size() : CX(r), cx = CX(r);
            u8 *d = cptr(r, r->ds, r->edx, n, al == 2 || al == 4);
            if (!d) { bad_ptr(r); return; }
            if (al == 3 || al == 5) memcpy((void *)XB, d, n);
            SETW(r->ecx, n);
            call21(r, XSEG, 0, 0, 0, 0, K_DX);
            SETW(r->ecx, cx);
            if ((al == 2 || al == 4) && !(r->eflags & EFL_CF))
                memcpy(d, (void *)XB, AX(r) < n ? AX(r) : n);
            return;
        }
        if (al == 0x0C || al == 0x0D) {
            /* the parameter block, 256 bytes at most, in and out as it
             * was (the track I/O forms above) */
            u8 *d = cptr(r, r->ds, r->edx, 256, 1);
            if (!d) { bad_ptr(r); return; }
            memcpy((void *)XB, d, 256);
            call21(r, XSEG, 0, 0, 0, 0, K_DX);
            memcpy(d, (void *)XB, 256);
            return;
        }
        break;
    case 0x4E: case 0x4F: {             /* find first/next: through a DTA here */
        /* the program's DTA (1Ah), or, before it set one, DOS's own (the
         * PSP's), which DOS gets back afterwards: before, the search went
         * to the buffer and stayed there, and DOS was left pointing at it */
        u32 oseg = 0, ooff = 0;
        u8 *dta = NULL;
        int swap_dta = 0;
        if (dta_set) {
            dta = cl_ptr(dta_sel, dta_off, 43, 1);
            if (!dta) { bad_ptr(r); return; }
        }
        if (ah == 0x4E && copy_in_str(r->ds, r->edx, 0) < 0) { bad_ptr(r); return; }
        if (dta_set) {
            dos_dta(&oseg, &ooff);
            swap_dta = (oseg != XSEG || ooff != 512);
            if (swap_dta) set_dos_dta(XSEG, 512);       /* DOS's DTA: buffer + 512 */
            memcpy((void *)(XB + 512), dta, 43);
        }
        if (ah == 0x4E) call21(r, XSEG, 0, 0, 0, 0, K_DX);
        else call21(r, 0, 0, 0, 0, 0, 0);
        if (dta_set) {
            memcpy(dta, (void *)(XB + 512), 43);
            if (swap_dta) set_dos_dta(oseg, ooff);
        }
        return;
    }
    case 0x11: case 0x12: {             /* FCB search: DS:DX, the result in the DTA */
        /* the FCB (37 bytes, an extended one 44) in and out and a DTA
         * here, as for 4Eh; w16's DOSX translates them too (TK36). File
         * Manager reads the volume label this way (extended FCB,
         * attribute 08h): reflected as they were, the label went to
         * DOS's own DTA and the drive line showed "C: [" with nothing
         * after */
        u32 oseg = 0, ooff = 0, n;
        u8 *dta = NULL, *fcb = cptr(r, r->ds, r->edx, 1, 1);
        int swap_dta = 0;
        if (!fcb) { bad_ptr(r); return; }
        n = fcb[0] == 0xFF ? 44 : 37;
        if ((fcb = cptr(r, r->ds, r->edx, n, 1)) == NULL) { bad_ptr(r); return; }
        if (dta_set) {
            dta = cl_ptr(dta_sel, dta_off, n, 1);
            if (!dta) { bad_ptr(r); return; }
            dos_dta(&oseg, &ooff);
            swap_dta = (oseg != XSEG || ooff != 512);
            if (swap_dta) set_dos_dta(XSEG, 512);
            memcpy((void *)(XB + 512), dta, n);
        }
        memcpy((void *)XB, fcb, n);
        call21(r, XSEG, 0, 0, 0, 0, K_DX);
        memcpy(fcb, (void *)XB, n);
        if (dta_set) {
            memcpy(dta, (void *)(XB + 512), n);
            if (swap_dta) set_dos_dta(oseg, ooff);
        }
        return;
    }
    case 0x50: {                        /* set PSP: a selector */
        int seg = sel_to_seg(BX(r));
        if (dpmi_verbose) trace("{psp %x base %x seg %x}", BX(r), sel_base(BX(r)), seg);
        if (seg < 0) break;
        v = *r;
        SETW(v.ebx, seg);
        call21(&v, 0, 0, 0, 0, 0, 0);
        return;
    }
    case 0x26: case 0x55: {             /* create a PSP at DX: a selector */
        int seg = sel_to_seg(DX(r));
        if (seg < 0) break;
        v = *r;
        SETW(v.edx, seg);
        call21(&v, 0, 0, 0, 0, 0, 0);
        r->eax = v.eax;
        r->eflags = v.eflags;
        return;
    }
    case 0x51: case 0x62: {             /* get PSP: a selector back */
        u32 sel;
        call21(r, 0, 0, 0, 0, 0, 0);
        sel = seg_to_sel(BX(r), 0xF2);
        if (sel) SETW(r->ebx, sel);
        return;
    }
    }
    pm_v86_reflect(r, 0x21, 0);
}

/* INT 10h from protected mode: the palette functions with ES:DX buffers
 * (IBM VGA BIOS: 1002h 17 bytes in, 1009h 17 out, 1012h CX*3 in, 1017h CX*3
 * out); the rest goes to V86 mode as it is. */
void video_pm(struct cregs *r)
{
    u32 ax = AX(r), n = 0, in = 0, out = 0;
    struct cregs v, pm = *r;

    if (!direct(r->es)) {
        switch (ax) {
        case 0x1002: n = 17; in = 1; break;
        case 0x1009: n = 17; out = 1; break;
        case 0x1012: n = CX(r) * 3; in = 1; break;
        case 0x1017: n = CX(r) * 3; out = 1; break;
        }
    }
    if (!n) { pm_v86_reflect(r, 0x10, 0); return; }
    /* a count past the buffer (CX over 5461: there are 256 DAC entries)
     * or a buffer that is not there: nothing is done (before, the call
     * went to the BIOS with ES 0, which read or wrote the vector table) */
    if (n > xbuf_size() || !cptr(r, r->es, r->edx, n, out)) return;
    if (in) memcpy((void *)XB, cptr(r, r->es, r->edx, n, 0), n);
    v = pm;
    v.ss = 0; v.esp = 0;
    v.ds = 0; v.es = XSEG; v.fs = v.gs = 0;
    SETW(v.edx, 0);
    v86_int(&v, 0x10);
    *r = pm;
    if (out) memcpy(cptr(r, r->es, r->edx, n, 1), (void *)XB, n);
}

/* INT 25h/26h (absolute disk read/write) from protected mode. DS:BX is
 * the buffer (CX sectors from sector DX), or, with CX=FFFFh, a packet:
 * the first sector (dword), the count (word) and the buffer (far
 * pointer, a protected-mode one here). A buffer V86 DOS cannot see goes
 * through the transfer buffer, a piece at a time (each piece a call of
 * its own with the start sector moved on). The flags stay on the
 * client's stack (dpmi.c). Sources: MS-DOS Programmer's Reference (INT
 * 25h/26h, the >32 MB packet form). UNVERIFIED: sector sizes other than
 * 512 bytes (taken as 512 here), and how Windows 3.1 splits a large
 * transfer. */
#define SECTOR 512

void disk_pm(struct cregs *r, int vec)
{
    struct cregs v, pm = *r;
    u32 big = CX(r) == 0xFFFF, first, count, bsel, boff, done = 0, n, max;
    u8 *pkt;

    if (big) {
        if ((pkt = cptr(r, r->ds, r->ebx, 10, 0)) == NULL) { bad_ptr(r); return; }
        first = *(u32 *)pkt;
        count = *(u16 *)(pkt + 4);
        boff = *(u16 *)(pkt + 6);
        bsel = *(u16 *)(pkt + 8);
    } else {
        first = DX(r);
        count = CX(r);
        bsel = r->ds;
        boff = BX(r);
    }
    if (direct(bsel) && !big) { pm_v86_reflect(r, vec, 0); return; }
    /* the whole buffer the client's (a 16-bit offset past FFFFh would
     * wrap to the segment's start; UNVERIFIED what Windows 3.1 does) */
    if (!cptr(r, bsel, boff, count * SECTOR, vec == 0x25)) { bad_ptr(r); return; }
    max = (xbuf_size() - 16) / SECTOR;          /* the packet at the end */
    if (!max) max = 1;
    do {
        n = count - done < max ? count - done : max;
        if (vec == 0x26) memcpy((void *)XB, cl_ptr(bsel, boff + done * SECTOR, n * SECTOR, 0), n * SECTOR);
        v = pm;
        v.ss = 0; v.esp = 0;
        v.ds = XSEG; v.es = 0; v.fs = v.gs = 0;
        if (big) {
            u8 *p = (u8 *)(XB + max * SECTOR);
            *(u32 *)p = first + done;
            *(u16 *)(p + 4) = (u16)n;
            *(u16 *)(p + 6) = 0;
            *(u16 *)(p + 8) = (u16)XSEG;
            SETW(v.ebx, max * SECTOR);
        } else {
            SETW(v.ebx, 0);
            SETW(v.ecx, n);
            SETW(v.edx, first + done);
        }
        v86_int(&v, vec);
        if (v.eflags & EFL_CF) break;
        if (vec == 0x25) memcpy(cl_ptr(bsel, boff + done * SECTOR, n * SECTOR, 1), (void *)XB, n * SECTOR);
        done += n;
    } while (done < count);
    *r = pm;
    r->eax = (r->eax & 0xFFFF0000u) | (v.eax & 0xFFFF);
    r->eflags = (r->eflags & ~0x8D5u) | (v.eflags & 0x8D5u);
}
