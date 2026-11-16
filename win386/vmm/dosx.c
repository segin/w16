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

static u32 dta_sel, dta_off;            /* the program's DTA (1Ah) */
static int dta_set;

#define XB      (xbuf_lin())
#define XSEG    (info->xbuf_seg)

static u32 strlen0(const u8 *s, u32 max)
{
    u32 n = 0;
    while (n < max && s[n]) n++;
    return n;
}

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

/* an ASCIIZ string at sel:off into the buffer at offset at; returns its length */
static u32 copy_in_str(u32 sel, u32 off, u32 at)
{
    u8 *s = (u8 *)pm_lin(sel, off);
    u32 n = strlen0(s, 255);
    memcpy((void *)(XB + at), s, n);
    *(u8 *)(XB + at + n) = 0;
    return n;
}

void dos_pm(struct cregs *r)
{
    u32 ah = (AX(r) >> 8) & 0xFF, al = AX(r) & 0xFF;
    struct cregs v;

    switch (ah) {
    case 0x25:                          /* set the protected-mode vector */
        sysvm.pmvec_sel[al] = r->ds;
        sysvm.pmvec_off[al] = sysvm.client32 ? r->edx : DX(r);
        return;
    case 0x35:                          /* get it */
        r->es = sysvm.pmvec_sel[al];
        if (sysvm.client32) r->ebx = sysvm.pmvec_off[al];
        else SETW(r->ebx, sysvm.pmvec_off[al]);
        return;
    case 0x1A:                          /* set DTA: the program's, kept here */
        dta_sel = r->ds;
        dta_off = sysvm.client32 ? r->edx : DX(r);
        dta_set = 1;
        return;
    case 0x2F:
        if (!dta_set) break;            /* DOS's own (the PSP's) until set */
        r->es = dta_sel;
        if (sysvm.client32) r->ebx = dta_off;
        else SETW(r->ebx, dta_off);
        return;
    case 0x09: {                        /* print a $-terminated string */
        u8 *s = (u8 *)pm_lin(r->ds, r->edx);
        u32 n = 0;
        if (direct(r->ds)) break;
        while (n < xbuf_size() - 1 && s[n] != '$') n++;
        memcpy((void *)XB, s, n);
        *(u8 *)(XB + n) = '$';
        call21(r, XSEG, 0, 0, 0, 0, K_DX);
        return;
    }
    case 0x39: case 0x3A: case 0x3B: case 0x3C: case 0x3D: case 0x41:
    case 0x43: case 0x5B:               /* a path at DS:DX */
        if (direct(r->ds)) break;
        copy_in_str(r->ds, r->edx, 0);
        call21(r, XSEG, 0, 0, 0, 0, K_DX);
        return;
    case 0x5A: {                        /* temporary file: the path comes back */
        u32 n;
        if (direct(r->ds)) break;
        n = copy_in_str(r->ds, r->edx, 0);
        call21(r, XSEG, 0, 0, 0, 0, K_DX);
        if (!(r->eflags & EFL_CF))
            memcpy((void *)pm_lin(r->ds, r->edx), (void *)XB, n + 14);
        return;
    }
    case 0x6C:                          /* extended open: DS:SI */
        if (direct(r->ds)) break;
        copy_in_str(r->ds, r->esi, 0);
        call21(r, XSEG, 0, 0, 0, 0, K_SI);
        return;
    case 0x56:                          /* rename: DS:DX, ES:DI */
        if (direct(r->ds) && direct(r->es)) break;
        copy_in_str(r->ds, r->edx, 0);
        copy_in_str(r->es, r->edi, 256);
        call21(r, XSEG, XSEG, 0, 0, 256, K_DX | K_DI);
        return;
    case 0x47:                          /* current directory to DS:SI */
        if (direct(r->ds)) break;
        call21(r, XSEG, 0, 0, 0, 0, K_SI);
        if (!(r->eflags & EFL_CF)) memcpy((void *)pm_lin(r->ds, r->esi), (void *)XB, 64);
        return;
    case 0x60:                          /* truename: DS:SI to ES:DI */
        if (direct(r->ds) && direct(r->es)) break;
        copy_in_str(r->ds, r->esi, 0);
        call21(r, XSEG, XSEG, 0, 0, 256, K_SI | K_DI);
        if (!(r->eflags & EFL_CF)) memcpy((void *)pm_lin(r->es, r->edi), (void *)(XB + 256), 128);
        return;
    case 0x38:                          /* country information to DS:DX */
        if (DX(r) == 0xFFFF || direct(r->ds)) break;
        call21(r, XSEG, 0, 0, 0, 0, K_DX);
        if (!(r->eflags & EFL_CF)) memcpy((void *)pm_lin(r->ds, r->edx), (void *)XB, 34);
        return;
    case 0x3F: {                        /* read, in pieces */
        u32 want = CX(r), done = 0, got;
        u8 *dst = (u8 *)pm_lin(r->ds, r->edx);
        if (direct(r->ds)) break;
        do {
            u32 n = want - done > xbuf_size() ? xbuf_size() : want - done;
            struct cregs t = *r;
            SETW(t.eax, 0x3F00);
            SETW(t.ecx, n);
            call21(&t, XSEG, 0, 0, 0, 0, K_DX);
            if (t.eflags & EFL_CF) {
                if (!done) { *r = t; return; }
                break;
            }
            got = AX(&t);
            memcpy(dst + done, (void *)XB, got);
            done += got;
            if (got < n) break;
        } while (done < want);
        SETW(r->eax, done);
        r->eflags &= ~EFL_CF;
        return;
    }
    case 0x40: {                        /* write, in pieces (CX=0 truncates: one call) */
        u32 want = CX(r), done = 0, put;
        u8 *src = (u8 *)pm_lin(r->ds, r->edx);
        if (direct(r->ds)) break;
        do {
            u32 n = want - done > xbuf_size() ? xbuf_size() : want - done;
            struct cregs t = *r;
            memcpy((void *)XB, src + done, n);
            SETW(t.eax, 0x4000);
            SETW(t.ecx, n);
            call21(&t, XSEG, 0, 0, 0, 0, K_DX);
            if (t.eflags & EFL_CF) {
                if (!done) { *r = t; return; }
                break;
            }
            put = AX(&t);
            done += put;
            if (put < n) break;
        } while (done < want);
        SETW(r->eax, done);
        r->eflags &= ~EFL_CF;
        return;
    }
    case 0x44:                          /* IOCTL: the ones with a DS:DX buffer */
        if (direct(r->ds)) break;
        if (al == 2 || al == 3 || al == 4 || al == 5) {
            u32 n = CX(r) > xbuf_size() ? xbuf_size() : CX(r);
            if (al == 3 || al == 5) memcpy((void *)XB, (void *)pm_lin(r->ds, r->edx), n);
            call21(r, XSEG, 0, 0, 0, 0, K_DX);
            if ((al == 2 || al == 4) && !(r->eflags & EFL_CF))
                memcpy((void *)pm_lin(r->ds, r->edx), (void *)XB, AX(r));
            return;
        }
        if (al == 0x0C || al == 0x0D) {
            memcpy((void *)XB, (void *)pm_lin(r->ds, r->edx), 256);
            call21(r, XSEG, 0, 0, 0, 0, K_DX);
            memcpy((void *)pm_lin(r->ds, r->edx), (void *)XB, 256);
            return;
        }
        break;
    case 0x4E: case 0x4F: {             /* find first/next: through a DTA here */
        struct cregs t;
        u8 *dta = dta_set ? (u8 *)pm_lin(dta_sel, dta_off) : NULL;
        memset(&t, 0, sizeof(t));
        t.eax = 0x1A00;
        t.edx = 512;
        call21(&t, XSEG, 0, 512, 0, 0, K_DX);           /* DOS's DTA: buffer + 512 */
        if (dta) memcpy((void *)(XB + 512), dta, 43);
        if (ah == 0x4E) {
            copy_in_str(r->ds, r->edx, 0);
            call21(r, XSEG, 0, 0, 0, 0, K_DX);
        } else
            call21(r, 0, 0, 0, 0, 0, 0);
        if (dta) memcpy(dta, (void *)(XB + 512), 43);
        return;
    }
    case 0x11: case 0x12: {             /* FCB search: DS:DX, the result in the DTA */
        /* 44 bytes in and out (an extended FCB at most) and a DTA here, as
         * for 4Eh; w16's DOSX translates them too (TK36). File Manager
         * reads the volume label this way (extended FCB, attribute 08h):
         * reflected as they were, the label went to DOS's own DTA and
         * the drive line showed "C: [" with nothing after */
        struct cregs t;
        u8 *dta = dta_set ? (u8 *)pm_lin(dta_sel, dta_off) : NULL;
        u8 *fcb = (u8 *)pm_lin(r->ds, r->edx);
        memset(&t, 0, sizeof(t));
        t.eax = 0x1A00;
        t.edx = 512;
        call21(&t, XSEG, 0, 512, 0, 0, K_DX);
        if (dta) memcpy((void *)(XB + 512), dta, 44);
        memcpy((void *)XB, fcb, 44);
        call21(r, XSEG, 0, 0, 0, 0, K_DX);
        memcpy(fcb, (void *)XB, 44);
        if (dta) memcpy(dta, (void *)(XB + 512), 44);
        return;
    }
    case 0x50: {                        /* set PSP: a selector */
        int seg = sel_to_seg(BX(r));
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
    if (!n || n > xbuf_size()) { pm_v86_reflect(r, 0x10, 0); return; }
    if (in) memcpy((void *)XB, (void *)pm_lin(r->es, r->edx), n);
    v = pm;
    v.ss = 0; v.esp = 0;
    v.ds = 0; v.es = XSEG; v.fs = v.gs = 0;
    SETW(v.edx, 0);
    v86_int(&v, 0x10);
    *r = pm;
    if (out) memcpy((void *)pm_lin(r->es, r->edx), (void *)XB, n);
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
        pkt = (u8 *)pm_lin(r->ds, r->ebx);
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
    max = (xbuf_size() - 16) / SECTOR;          /* the packet at the end */
    if (!max) max = 1;
    do {
        n = count - done < max ? count - done : max;
        if (vec == 0x26) memcpy((void *)XB, (void *)pm_lin(bsel, boff + done * SECTOR), n * SECTOR);
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
        if (vec == 0x25) memcpy((void *)pm_lin(bsel, boff + done * SECTOR), (void *)XB, n * SECTOR);
        done += n;
    } while (done < count);
    *r = pm;
    r->eax = (r->eax & 0xFFFF0000u) | (v.eax & 0xFFFF);
    r->eflags = (r->eflags & ~0x8D5u) | (v.eflags & 0x8D5u);
}
