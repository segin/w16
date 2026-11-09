/* dpmi.c - w16 VMM: the DPMI 0.9 host for the System VM's protected-mode
 * program (KRNL386), ARCHITECTURE.md 12.3.
 *
 * Source: DPMI Specification 0.9 (INT 2Fh 1687h, the mode switch, INT 31h
 * functions 0000h-0A00h, interrupt and exception handling, the real-mode
 * register structure); Windows 3.1 DDK INT 2Fh 1684h/1686h; observation of
 * Windows 3.1 (ARCHITECTURE.md 12.2: selectors, version values).
 *
 * Stage 1: one client; protected mode runs at ring 3 with IOPL 3 (so CLI,
 * STI and port I/O act directly: one VM only); interrupts the client has
 * not hooked are reflected to V86 mode, with DS and ES translated when
 * their selectors map conventional memory on a paragraph boundary (no
 * buffer copying yet: ARCHITECTURE.md 12.4).
 */
#include "vmm.h"

extern u32 pmbp_lin;
void page_free(u32 phys);

#define LDT_FIRST 0x11          /* first index handed out (Windows 3.1: DS 8Fh) */

/* LDT ownership. KRNL386 takes every all-zero descriptor below the limit
 * of the LDT alias (the vendor API's selector) for its own free list, and
 * asks for 100h more selectors with 0000h while that limit is below F000h
 * (Pietrek, Windows Internals ch. 1-2). So here a descriptor is free if
 * and only if its 8 bytes are zero, and the LDT grows: ldt_top is the
 * size in use (the alias' limit + 1), raised 4 KB at a time when 0000h
 * needs room, up to the architecture's 64 KB. UNVERIFIED: Windows 3.1's
 * starting size and step. */
static u32 ldt_top = 0x1000;
static u32 ldt_alias;               /* the vendor API's LDT selector */

static u8 *ldt_desc(u32 sel)
{
    return ldt + (sel & ~7u);
}

static int desc_free(u32 i)
{
    u32 *d = (u32 *)(ldt + i * 8);
    return !d[0] && !d[1];
}

static int sel_valid(u32 sel)
{
    u32 i = sel >> 3;
    return (sel & 4) && i < 8192 && !desc_free(i);
}

u32 sel_base(u32 sel)
{
    if (sel & 4) return desc_base(ldt_desc(sel));
    if ((sel & ~3u) == (SEL_PMBP & ~3u)) return pmbp_lin;
    return desc_base(gdt + (sel & ~7u));
}

static void ldt_grow(void)
{
    if (ldt_top >= 0x10000) return;
    ldt_top += 0x1000;
    if (ldt_alias) set_desc(ldt_desc(ldt_alias), (u32)ldt, ldt_top - 1, 0xF2, 0);
}

/* Allocate n consecutive LDT selectors; 0 if none. */
static u32 ldt_alloc(u32 n)
{
    u32 i, j;
    for (;;) {
        for (i = LDT_FIRST; i + n <= ldt_top / 8; i++) {
            for (j = 0; j < n; j++)
                if (!desc_free(i + j)) break;
            if (j == n) {
                for (j = 0; j < n; j++)     /* data, present, DPL 3, base 0, limit 0 */
                    set_desc(ldt + (i + j) * 8, 0, 0, 0xF2, 0);
                return (i << 3) | 7;
            }
            i += j;
        }
        if (ldt_top >= 0x10000) return 0;
        ldt_grow();
    }
}

static void ldt_free(u32 sel)
{
    memset(ldt + (sel >> 3) * 8, 0, 8);
}

/* A selector for a real-mode segment (0002h): one per segment, kept. */
struct segsel { struct segsel *next; u32 seg, sel; };
static struct segsel *segsels;

u32 seg_to_sel(u32 seg, u32 access)
{
    struct segsel *s;
    u32 sel;
    for (s = segsels; s; s = s->next)
        if (s->seg == seg && (ldt_desc(s->sel)[5] & 0xFE) == (access & 0xFE)) return s->sel;
    sel = ldt_alloc(1);
    if (!sel) return 0;
    set_desc(ldt_desc(sel), seg << 4, 0xFFFF, access, 0);
    s = kmalloc(sizeof(*s));
    s->seg = seg;
    s->sel = sel;
    s->next = segsels;
    segsels = s;
    return sel;
}

/* The real-mode segment for a selector, or -1: base below 1 MB + 64 KB and
 * on a paragraph boundary. */
int sel_to_seg(u32 sel)
{
    u32 b;
    if (!sel) return 0;
    if (!(sel & 4) || !sel_valid(sel)) return -1;
    b = desc_base(ldt_desc(sel));
    if (b >= 0x110000 || (b & 15)) return -1;
    return b >> 4;
}

/* ---- the client's stack ---- */
static u32 stack_lin(struct cregs *r)
{
    return sel_base(r->ss) + (sysvm.client32 ? r->esp : (r->esp & 0xFFFF));
}

static void pm_push(struct cregs *r, u32 v)
{
    if (sysvm.client32) {
        r->esp -= 4;
        *(u32 *)stack_lin(r) = v;
    } else {
        SETW(r->esp, r->esp - 2);
        *(u16 *)stack_lin(r) = (u16)v;
    }
}

static u32 pm_pop(struct cregs *r)
{
    u32 v;
    if (sysvm.client32) {
        v = *(u32 *)stack_lin(r);
        r->esp += 4;
    } else {
        v = *(u16 *)stack_lin(r);
        SETW(r->esp, r->esp + 2);
    }
    return v;
}

static void set_cf(struct cregs *r, int c)
{
    if (c) r->eflags |= EFL_CF;
    else r->eflags &= ~EFL_CF;
}

/* ---- start ---- */
void dpmi_init(void)
{
    int i;

    for (i = 0; i < 256; i++) {
        sysvm.pmvec_sel[i] = SEL_PMBP;  /* the host's default handlers */
        sysvm.pmvec_off[i] = i;
    }
}

/* The mode switch: a far call from V86 mode to the 1687h entry. AX bit 0:
 * a 32-bit client; ES: host data (none asked). */
void dpmi_switch(struct cregs *r)
{
    u32 ip, cs, dsel, csel, ssel, psel, esel, psp, env;
    u8 *pspp;

    ip = v86_pop16(r);
    cs = v86_pop16(r);
    sysvm.client32 = AX(r) & 1;
    sysvm.v86save = *r;
    sysvm.v86save.eip = ip;
    sysvm.v86save.cs = cs;
    psp = info->sda_lin ? *(u16 *)(info->sda_lin + 0x10) : 0;
    sysvm.psp_seg = psp;
    /* Windows 3.1 gave DS 8Fh, CS 97h, the PSP 9Fh, the environment A7h (or
     * AFh) in that order; SS shared DS's selector when equal */
    dsel = ldt_alloc(1);
    set_desc(ldt_desc(dsel), (r->ds & 0xFFFF) << 4, 0xFFFF, 0xF2, 0);
    csel = ldt_alloc(1);
    set_desc(ldt_desc(csel), cs << 4, 0xFFFF, 0xFA, 0);
    psel = ldt_alloc(1);
    set_desc(ldt_desc(psel), psp << 4, 0xFF, 0xF2, 0);
    if ((r->ss & 0xFFFF) == (r->ds & 0xFFFF))
        ssel = dsel;
    else {
        ssel = ldt_alloc(1);
        set_desc(ldt_desc(ssel), (r->ss & 0xFFFF) << 4, 0xFFFF, 0xF2, sysvm.client32 ? 0x4 : 0);
    }
    pspp = (u8 *)(psp << 4);
    env = *(u16 *)(pspp + 0x2C);
    if (psp && env) {
        esel = ldt_alloc(1);
        set_desc(ldt_desc(esel), env << 4, 0xFFFF, 0xF2, 0);
        *(u16 *)(pspp + 0x2C) = (u16)esel;
    }
    trace("DPMI: %d-bit client, PSP %x, cs %x ds %x ss %x es %x\n",
          sysvm.client32 ? 32 : 16, psp, csel, dsel, ssel, psel);
    r->cs = csel;
    r->eip = ip;
    r->ds = dsel;
    r->ss = ssel;
    r->es = psel;
    r->fs = r->gs = 0;
    if (!sysvm.client32) r->esp &= 0xFFFF;
    r->eflags = (r->eflags & ~(EFL_VM | EFL_CF | EFL_NT)) | EFL_IOPL | EFL_IF;
    sysvm.dsel = dsel;
    sysvm.pm = 1;
}

/* ---- reflecting into V86 mode and back ---- */
int dpmi_verbose;

/* An interrupt from protected mode that the client has not hooked: run the
 * V86 handler with the client's registers; DS and ES become segments where
 * their selectors map conventional memory. Results come back, and segment
 * registers the V86 code changed come back as selectors. */
int pm_v86_reflect(struct cregs *r, int vec, int hw)
{
    struct cregs v, pm = *r;
    int ds, es;

    if (dpmi_verbose && !hw) trace("[R%x:%x]", vec, AX(r));
    v = pm;
    v.ss = 0; v.esp = 0;
    v.fs = v.gs = 0;
    if (hw) {
        v.ds = v.es = 0;
        v86_int(&v, vec);
        *r = pm;
        return 1;
    }
    ds = sel_to_seg(pm.ds);
    es = sel_to_seg(pm.es);
    if (ds < 0 && (pm.ds & 4))
        trace("DPMI: INT %x AX=%x: DS %x not in conventional memory\n", vec, AX(r), pm.ds);
    if (es < 0 && (pm.es & 4))
        trace("DPMI: INT %x AX=%x: ES %x not in conventional memory\n", vec, AX(r), pm.es);
    v.ds = ds < 0 ? 0 : ds;
    v.es = es < 0 ? 0 : es;
    v.eflags = pm.eflags;
    v86_int(&v, vec);
    *r = pm;
    r->eax = v.eax; r->ebx = v.ebx; r->ecx = v.ecx; r->edx = v.edx;
    r->esi = v.esi; r->edi = v.edi; r->ebp = v.ebp;
    r->eflags = (r->eflags & ~0x8D5u) | (v.eflags & 0x8D5u);    /* CF PF AF ZF SF OF */
    if ((int)(v.ds & 0xFFFF) != (ds < 0 ? 0 : ds)) {
        u32 sel = seg_to_sel(v.ds & 0xFFFF, 0xF2);
        if (sel) r->ds = sel;
    }
    if ((int)(v.es & 0xFFFF) != (es < 0 ? 0 : es)) {
        u32 sel = seg_to_sel(v.es & 0xFFFF, 0xF2);
        if (sel) r->es = sel;
    }
    return 1;
}

#pragma pack(1)
struct rmregs {             /* DPMI real-mode register structure */
    u32 edi, esi, ebp, res, ebx, edx, ecx, eax;
    u16 flags, es, ds, fs, gs, ip, cs, sp, ss;
};
#pragma pack()

/* DPMI 0300h-0302h: run real-mode code with the client's register structure */
static void dpmi_simulate(struct cregs *r, int fn)
{
    u32 lin = sel_base(r->es) + (sysvm.client32 ? r->edi : DI(r));
    struct rmregs *s = (struct rmregs *)lin;
    struct cregs v, pm = *r;
    u32 ncopy = CX(r) * 2, i;
    u8 *src;

    memset(&v, 0, sizeof(v));
    v.edi = s->edi; v.esi = s->esi; v.ebp = s->ebp; v.ebx = s->ebx;
    v.edx = s->edx; v.ecx = s->ecx; v.eax = s->eax;
    v.es = s->es; v.ds = s->ds; v.fs = s->fs; v.gs = s->gs;
    if (s->ss || s->sp) { v.ss = s->ss; v.esp = s->sp; }
    else { v.ss = info->refstk_seg; v.esp = sysvm.refsp; }
    v.eflags = s->flags;
    /* parameters copied from the client's stack */
    src = (u8 *)(stack_lin(r));
    for (i = ncopy; i > 0; i -= 2)
        v86_push16(&v, *(u16 *)(src + i - 2));
    if (dpmi_verbose) trace("[31:%x %x:%x]", fn, fn == 0x300 ? BX(r) & 0xFF : s->cs, s->ip);
    if (fn == 0x0300)
        v86_int(&v, BX(r) & 0xFF);
    else {
        v.cs = s->cs;
        v.eip = s->ip;
        v86_farcall(&v, fn == 0x0302);
    }
    s->edi = v.edi; s->esi = v.esi; s->ebp = v.ebp; s->ebx = v.ebx;
    s->edx = v.edx; s->ecx = v.ecx; s->eax = v.eax;
    s->flags = (u16)v.eflags;
    s->es = (u16)v.es; s->ds = (u16)v.ds; s->fs = (u16)v.fs; s->gs = (u16)v.gs;
    *r = pm;
    set_cf(r, 0);
}

/* ---- memory blocks (0501h) ---- */
struct memblk { struct memblk *next; u32 lin, size, handle; };
static struct memblk *blocks;
static u32 next_lin = 0x00400000;      /* client linear memory starts at 4 MB */
static u32 next_handle = 1;

static struct memblk *mem_alloc(u32 size)
{
    struct memblk *b;
    u32 n = (size + 4095) / 4096, i, p;
    if (!size || n > pages_free()) return NULL;
    b = kmalloc(sizeof(*b));
    if (!b) return NULL;
    b->lin = next_lin;
    b->size = n * 4096;
    b->handle = next_handle++;
    for (i = 0; i < n; i++) {
        p = page_alloc();
        if (!p || !map_page(b->lin + i * 4096, p, 7)) return NULL;
    }
    next_lin += b->size + 4096;         /* a guard page between blocks */
    b->next = blocks;
    blocks = b;
    return b;
}

static int mem_free(u32 handle)
{
    struct memblk **pp, *b;
    u32 i, p;
    for (pp = &blocks; (b = *pp) != NULL; pp = &b->next)
        if (b->handle == handle) {
            for (i = 0; i < b->size; i += 4096) {
                p = lin_to_phys(b->lin + i);
                map_page(b->lin + i, 0, 0);
                if (p) page_free(p & ~0xFFFu);
            }
            *pp = b->next;
            kfree(b);
            return 1;
        }
    return 0;
}

static struct memblk *mem_find(u32 handle)
{
    struct memblk *b;
    for (b = blocks; b; b = b->next)
        if (b->handle == handle) return b;
    return NULL;
}

/* ---- INT 31h ---- */
static u32 reg_ofs(struct cregs *r, u32 e)
{
    return sysvm.client32 ? e : (e & 0xFFFF);
}

/* 0100h-0102h: DOS memory through INT 21h 48h/49h/4Ah in V86 mode; the
 * block gets one selector (64 KB or less; larger blocks would need
 * several: ARCHITECTURE.md 12.4) */
static void dos_alloc(struct cregs *r, u32 fn)
{
    struct cregs v, pm = *r;
    int seg;
    u32 sel;

    memset(&v, 0, sizeof(v));
    if (fn == 0x0100) {
        v.eax = 0x4800; v.ebx = BX(r);
        v86_int(&v, 0x21);
        *r = pm;
        if (v.eflags & EFL_CF) { set_cf(r, 1); SETW(r->eax, v.eax); SETW(r->ebx, v.ebx); return; }
        sel = ldt_alloc(1);
        if (!sel) { set_cf(r, 1); SETW(r->eax, 8); return; }
        set_desc(ldt_desc(sel), (v.eax & 0xFFFF) << 4, BX(r) ? BX(r) * 16 - 1 : 0, 0xF2, 0);
        SETW(r->eax, v.eax);
        SETW(r->edx, sel);
        return;
    }
    seg = sel_to_seg(DX(r));
    if (seg < 0) { set_cf(r, 1); SETW(r->eax, 9); return; }
    v.es = seg;
    if (fn == 0x0101) v.eax = 0x4900;
    else { v.eax = 0x4A00; v.ebx = BX(r); }
    v86_int(&v, 0x21);
    *r = pm;
    if (v.eflags & EFL_CF) { set_cf(r, 1); SETW(r->eax, v.eax); SETW(r->ebx, v.ebx); return; }
    if (fn == 0x0101) ldt_free(DX(r));
    else set_desc(ldt_desc(DX(r)), seg << 4, BX(r) * 16 - 1, 0xF2, 0);
}

static void dpmi_call(struct cregs *r)
{
    u32 fn = AX(r), sel, i, v;
    u8 *d;
    struct memblk *b;

    set_cf(r, 0);
    if (dpmi_verbose) trace("[31:%x bx=%x cx=%x dx=%x]", fn, BX(r), CX(r), DX(r));
    switch (fn) {
    case 0x0000:                        /* allocate LDT descriptors */
        sel = ldt_alloc(CX(r) ? CX(r) : 1);
        if (!sel) { set_cf(r, 1); SETW(r->eax, 0x8011); break; }
        SETW(r->eax, sel);
        break;
    case 0x0001:                        /* free a descriptor */
        if (!sel_valid(BX(r))) { set_cf(r, 1); break; }
        ldt_free(BX(r));
        break;
    case 0x0002:                        /* segment to descriptor */
        sel = seg_to_sel(BX(r), 0xF2);
        if (!sel) { set_cf(r, 1); break; }
        SETW(r->eax, sel);
        break;
    case 0x0004:                        /* lock/unlock a selector (Windows' own; reserved in 0.9): */
    case 0x0005:                        /* nothing is paged out, so nothing to do */
        break;
    case 0x0003:                        /* selector increment */
        SETW(r->eax, 8);
        break;
    case 0x0006:                        /* get base */
        if (!sel_valid(BX(r))) { set_cf(r, 1); break; }
        v = desc_base(ldt_desc(BX(r)));
        SETW(r->ecx, v >> 16);
        SETW(r->edx, v);
        break;
    case 0x0007:                        /* set base */
        if (!sel_valid(BX(r))) { set_cf(r, 1); break; }
        d = ldt_desc(BX(r));
        v = (CX(r) << 16) | DX(r);
        d[2] = (u8)v; d[3] = (u8)(v >> 8); d[4] = (u8)(v >> 16); d[7] = (u8)(v >> 24);
        break;
    case 0x0008:                        /* set limit */
        if (!sel_valid(BX(r))) { set_cf(r, 1); break; }
        d = ldt_desc(BX(r));
        v = (CX(r) << 16) | DX(r);
        set_desc(d, desc_base(d), v, d[5], (d[6] >> 4) & 0x4);
        break;
    case 0x0009:                        /* set access rights */
        if (!sel_valid(BX(r))) { set_cf(r, 1); break; }
        d = ldt_desc(BX(r));
        d[5] = (u8)(CL(r) | 0x60);      /* DPL 3 */
        d[6] = (u8)((d[6] & 0x0F) | (CH(r) & 0xD0));
        break;
    case 0x000A:                        /* create a data alias */
        if (!(BX(r) & 4) || !sel_valid(BX(r))) { set_cf(r, 1); break; }
        sel = ldt_alloc(1);
        if (!sel) { set_cf(r, 1); break; }
        memcpy(ldt_desc(sel), ldt_desc(BX(r)), 8);
        ldt_desc(sel)[5] = 0xF2;
        SETW(r->eax, sel);
        break;
    case 0x000B:                        /* get descriptor */
        if (!sel_valid(BX(r))) { set_cf(r, 1); break; }
        memcpy((void *)(sel_base(r->es) + reg_ofs(r, r->edi)), ldt_desc(BX(r)), 8);
        break;
    case 0x000C:                        /* set descriptor */
        if (!sel_valid(BX(r))) { set_cf(r, 1); break; }
        memcpy(ldt_desc(BX(r)), (void *)(sel_base(r->es) + reg_ofs(r, r->edi)), 8);
        ldt_desc(BX(r))[5] |= 0x60;
        break;
    case 0x0100: case 0x0101: case 0x0102:
        dos_alloc(r, fn);
        break;
    case 0x0200:                        /* get real-mode vector */
        SETW(r->ecx, ((u16 *)0)[(BX(r) & 0xFF) * 2 + 1]);
        SETW(r->edx, ((u16 *)0)[(BX(r) & 0xFF) * 2]);
        break;
    case 0x0201:                        /* set real-mode vector */
        ((u16 *)0)[(BX(r) & 0xFF) * 2 + 1] = CX(r);
        ((u16 *)0)[(BX(r) & 0xFF) * 2] = DX(r);
        break;
    case 0x0202:                        /* get exception handler */
        i = BX(r) & 0xFF;
        if (i >= 32) { set_cf(r, 1); break; }
        if (sysvm.excvec_sel[i]) {
            SETW(r->ecx, sysvm.excvec_sel[i]);
            r->edx = sysvm.client32 ? sysvm.excvec_off[i] : ((r->edx & 0xFFFF0000u) | (u16)sysvm.excvec_off[i]);
        } else {
            SETW(r->ecx, SEL_PMBP);
            SETW(r->edx, PMBP_EXCRET);  /* the host's default: terminate */
        }
        break;
    case 0x0203:                        /* set exception handler */
        i = BX(r) & 0xFF;
        if (i >= 32) { set_cf(r, 1); break; }
        sysvm.excvec_sel[i] = CX(r);
        sysvm.excvec_off[i] = reg_ofs(r, r->edx);
        if (CX(r) == SEL_PMBP) sysvm.excvec_sel[i] = 0;
        break;
    case 0x0204:                        /* get PM interrupt vector */
        i = BX(r) & 0xFF;
        SETW(r->ecx, sysvm.pmvec_sel[i]);
        if (sysvm.client32) r->edx = sysvm.pmvec_off[i];
        else SETW(r->edx, sysvm.pmvec_off[i]);
        break;
    case 0x0205:                        /* set PM interrupt vector */
        i = BX(r) & 0xFF;
        sysvm.pmvec_sel[i] = CX(r);
        sysvm.pmvec_off[i] = reg_ofs(r, r->edx);
        break;
    case 0x0300: case 0x0301: case 0x0302:
        dpmi_simulate(r, fn);
        return;
    case 0x0400:                        /* version: Windows 3.1's values */
        SETW(r->eax, 0x005A);
        SETW(r->ebx, 0x0005);
        SETLO(r->ecx, 3);
        SETW(r->edx, 0x0870);
        break;
    case 0x0500: {                      /* free memory information */
        u32 *p = (u32 *)(sel_base(r->es) + reg_ofs(r, r->edi));
        u32 f = pages_free();
        for (i = 0; i < 12; i++) p[i] = 0xFFFFFFFF;
        p[0] = f * 4096;                /* largest free block */
        p[1] = f;                       /* maximum unlocked page allocation */
        p[2] = f;                       /* maximum locked page allocation */
        p[3] = f;                       /* linear address space, pages */
        p[4] = f;                       /* total unlocked pages */
        p[5] = f;                       /* free pages */
        p[6] = info->xms_size / 4096;   /* total physical pages */
        p[7] = f;                       /* free linear address space, pages */
        p[8] = 0;                       /* paging file size, pages: none yet */
        break;
    }
    case 0x0501:                        /* allocate a memory block */
        b = mem_alloc((BX(r) << 16) | CX(r));
        if (!b) { set_cf(r, 1); SETW(r->eax, 0x8013); break; }
        SETW(r->ebx, b->lin >> 16); SETW(r->ecx, b->lin);
        SETW(r->esi, b->handle >> 16); SETW(r->edi, b->handle);
        break;
    case 0x0502:                        /* free a memory block */
        if (!mem_free((SI(r) << 16) | DI(r))) set_cf(r, 1);
        break;
    case 0x0503: {                      /* resize: a new block, contents copied */
        struct memblk *o = mem_find((SI(r) << 16) | DI(r));
        u32 size = (BX(r) << 16) | CX(r);
        if (!o) { set_cf(r, 1); break; }
        b = mem_alloc(size);
        if (!b) { set_cf(r, 1); SETW(r->eax, 0x8013); break; }
        memcpy((void *)b->lin, (void *)o->lin, o->size < b->size ? o->size : b->size);
        mem_free(o->handle);
        SETW(r->ebx, b->lin >> 16); SETW(r->ecx, b->lin);
        SETW(r->esi, b->handle >> 16); SETW(r->edi, b->handle);
        break;
    }
    case 0x0600: case 0x0601: case 0x0602: case 0x0603:
    case 0x0702: case 0x0703:
        break;                          /* nothing is paged out yet */
    case 0x0604:
        SETW(r->ebx, 0); SETW(r->ecx, 4096);
        break;
    case 0x0800: {                      /* map physical memory */
        u32 phys = (BX(r) << 16) | CX(r), size = (SI(r) << 16) | DI(r), lin, off;
        if (phys < 0x100000) { SETW(r->ebx, phys >> 16); SETW(r->ecx, phys); break; }
        lin = next_lin;
        for (off = 0; off < size + (phys & 0xFFF); off += 4096)
            if (!map_page(lin + off, (phys & ~0xFFFu) + off, 7)) { set_cf(r, 1); return; }
        next_lin += ((size + (phys & 0xFFF) + 4095) & ~0xFFFu) + 4096;
        lin += phys & 0xFFF;
        SETW(r->ebx, lin >> 16); SETW(r->ecx, lin);
        break;
    }
    case 0x0900:                        /* virtual interrupts: disable, get old */
        SETLO(r->eax, (r->eflags & EFL_IF) ? 1 : 0);
        r->eflags &= ~EFL_IF;
        break;
    case 0x0901:
        SETLO(r->eax, (r->eflags & EFL_IF) ? 1 : 0);
        r->eflags |= EFL_IF;
        break;
    case 0x0902:
        SETLO(r->eax, (r->eflags & EFL_IF) ? 1 : 0);
        break;
    case 0x0A00:                        /* vendor API: "MS-DOS" */
        set_cf(r, 1);
        break;
    default:
        trace("DPMI: unsupported INT 31h AX=%x\n", fn);
        set_cf(r, 1);
        SETW(r->eax, 0x8001);
        break;
    }
}

/* ---- INT 2Fh in protected mode ---- */
int int2f_pm(struct cregs *r)
{
    if (dpmi_verbose && (AX(r) >> 8) == 0x16 && AX(r) != 0x1689 && AX(r) != 0x1680)
        trace("[2F:%x bx=%x]", AX(r), BX(r));
    switch (AX(r)) {
    case 0x1600:
        SETW(r->eax, 0x0A03);
        return 1;
    case 0x1680:
    case 0x1689:
        SETLO(r->eax, 0);
        return 1;
    case 0x1683:
        SETW(r->ebx, 1);
        SETLO(r->eax, 0);
        return 1;
    case 0x1686:                        /* in protected mode */
        SETW(r->eax, 0);
        return 1;
    case 0x1684:                        /* PM API entry: none yet */
        SETW(r->edi, 0);
        r->es = 0;
        return 1;
    case 0x168A: {                      /* vendor entry: "MS-DOS" only */
        const char *p = (const char *)(sel_base(r->ds) + reg_ofs(r, r->esi));
        if (p[0] == 'M' && p[1] == 'S' && p[2] == '-' && p[3] == 'D' && p[4] == 'O'
            && p[5] == 'S' && p[6] == 0) {
            SETLO(r->eax, 0);
            r->es = SEL_PMBP;
            if (sysvm.client32) r->edi = PMBP_VENDOR;
            else SETW(r->edi, PMBP_VENDOR);
            return 1;
        }
        return 0;
    }
    }
    return 0;
}

/* ---- interrupts in protected mode ---- */
static int pmvec_default(int vec)
{
    return sysvm.pmvec_sel[vec] == SEL_PMBP && sysvm.pmvec_off[vec] == (u32)vec;
}

u32 pm_lin(u32 sel, u32 off)
{
    return sel_base(sel) + (sysvm.client32 ? off : (off & 0xFFFF));
}

static void terminate_client(struct cregs *r)
{
    struct cregs v = sysvm.v86save;
    trace("DPMI: client ends (INT 21h AX=%x)\n", AX(r));
    sysvm.pm = 0;
    v.eax = r->eax;
    v.ebx = r->ebx;
    v.eflags = (v.eflags & ~(EFL_NT)) | EFL_VM | EFL_IOPL | EFL_IF;
    *r = v;
    v86_reflect(r, 0x21);
}

void dpmi_int(struct cregs *r, int vec, int hw)
{
    if (!hw && vec == 0x31) { dpmi_call(r); return; }
    if (!hw && vec == 0x2F && int2f_pm(r)) return;
    if (!pmvec_default(vec)) {
        /* the client's handler: an interrupt frame on its stack */
        pm_push(r, r->eflags);
        pm_push(r, r->cs);
        pm_push(r, r->eip);
        r->eflags &= ~(EFL_IF | EFL_TF);
        r->cs = sysvm.pmvec_sel[vec];
        r->eip = sysvm.pmvec_off[vec];
        return;
    }
    if (!hw && vec == 0x21 && (AX(r) >> 8) == 0x4C) { terminate_client(r); return; }
    if (!hw && vec == 0x21) { dos_pm(r); return; }
    if (!hw && vec == 0x10) { video_pm(r); return; }
    /* INT 41h: the protected-mode debugger interface (AX=004Fh asks
     * whether one is there); no debugger: nothing happens, as without
     * one under Windows 3.1 (UNVERIFIED that Windows does exactly this) */
    if (!hw && vec == 0x41) return;
    pm_v86_reflect(r, vec, hw);
}

/* A breakpoint in the host's ring-3 segment (a HLT executed there). */
void dpmi_pmbp(struct cregs *r, u32 index)
{
    if (index < 256) {
        /* the default handler of INT index, reached by the client chaining
         * to it: its interrupt frame is on the stack */
        r->eip = pm_pop(r);
        r->cs = pm_pop(r);
        r->eflags = (pm_pop(r) & 0xFFFF) | (r->eflags & 0xFFFF0000u);
        r->eflags |= EFL_IOPL;
        if (index == 0x21 && (AX(r) >> 8) == 0x4C) { terminate_client(r); return; }
        if (index == 0x31) { dpmi_call(r); return; }
        if (index == 0x2F && int2f_pm(r)) return;
        if (index == 0x41) return;
        if (index == 0x21) { dos_pm(r); return; }
        if (index == 0x10) { video_pm(r); return; }
        pm_v86_reflect(r, index, index >= 8 && index < 16);
        return;
    }
    if (index == PMBP_IRQRET) {
        exec_return(r);
        return;
    }
    if (index == PMBP_VENDOR) {
        /* the "MS-DOS" vendor extension (Pietrek, Windows Internals ch. 1:
         * KRNL386 calls it with AX=0100h and expects the selector of a
         * writable alias of the LDT in AX, carry clear). Called far. */
        u32 ip = pm_pop(r), cs = pm_pop(r);
        set_cf(r, 1);
        if (AX(r) == 0x0100) {
            if (!ldt_alias) {
                ldt_alias = ldt_alloc(1);
                set_desc(ldt_desc(ldt_alias), (u32)ldt, ldt_top - 1, 0xF2, 0);
                make_user((u32)ldt, ldt_limit + 1);
            }
            SETW(r->eax, ldt_alias);
            set_cf(r, 0);
        } else
            trace("DPMI: vendor call AX=%x\n", AX(r));
        r->eip = ip;
        r->cs = cs;
        return;
    }
    if (index == PMBP_EXCRET) {
        /* back from a client's exception handler: the DPMI frame */
        u32 err, ip, cs, fl, sp, ss;
        err = pm_pop(r);                /* the handler's RETF took our address */
        ip = pm_pop(r); cs = pm_pop(r); fl = pm_pop(r); sp = pm_pop(r); ss = pm_pop(r);
        (void)err;
        r->eip = ip; r->cs = cs; r->eflags = (fl & 0xFFFF) | EFL_IOPL; r->esp = sp; r->ss = ss;
        return;
    }
    panic("unknown PM breakpoint %x", index);
}

/* ---- interrupts from V86 mode for protected-mode handlers ----
 * A hardware interrupt (and INT 1Ch/23h/24h) that comes while V86 code
 * runs goes to the client's protected-mode handler if it installed one
 * (DPMI 0.9), on a stack of the host's; the handler's IRET reaches the
 * IRQRET breakpoint and the V86 code goes on. */
static u32 hstk_sel, hstk_lin, hstk_sp;
#define HSTK_SIZE 0x4000

int dpmi_v86_hook(struct cregs *r, int vec, int hw)
{
    struct cregs p;
    u32 saved_sp;

    if (!sysvm.pm || pmvec_default(vec)) return 0;
    if (!hstk_sel) {
        u32 i, pg;
        hstk_lin = 0x00300000;          /* below the client's blocks (4 MB up) */
        for (i = 0; i < HSTK_SIZE; i += 4096) {
            pg = page_alloc();
            if (!pg || !map_page(hstk_lin + i, pg, 7)) return 0;
        }
        hstk_sel = ldt_alloc(1);
        set_desc(ldt_desc(hstk_sel), hstk_lin, HSTK_SIZE - 1, 0xF2, sysvm.client32 ? 0x4 : 0);
        hstk_sp = HSTK_SIZE;
    }
    (void)hw;
    memset(&p, 0, sizeof(p));
    p.ss = hstk_sel;
    p.esp = hstk_sp;
    p.ds = p.es = sysvm.dsel;
    p.eflags = (r->eflags & 0x0FD5 & ~EFL_TF) | EFL_IOPL | 2;
    p.eflags |= EFL_IF;
    pm_push(&p, p.eflags);
    pm_push(&p, SEL_PMBP);
    pm_push(&p, PMBP_IRQRET);
    p.eflags &= ~EFL_IF;
    p.cs = sysvm.pmvec_sel[vec];
    p.eip = sysvm.pmvec_off[vec];
    saved_sp = hstk_sp;
    hstk_sp = p.esp & (sysvm.client32 ? 0xFFFFFFFFu : 0xFFFFu);
    exec_v86(&p);
    hstk_sp = saved_sp;
    /* flags the handler changed (CF etc. for INT 23h/24h) */
    if (!hw) r->eflags = (r->eflags & ~0x8D5u) | (p.eflags & 0x8D5u);
    return 1;
}

void dpmi_exception(struct cregs *r, int vec, u32 err)
{
    if (vec < 32 && sysvm.excvec_sel[vec]) {
        /* the DPMI 0.9 exception frame: return address, error code, IP,
         * CS, FLAGS, SP, SS (words for a 16-bit client) */
        struct cregs o = *r;
        if (dpmi_verbose) trace("[EXC %x err %x at %x:%x cr2 %x]", vec, err, r->cs, r->eip, get_cr2());
        pm_push(r, o.ss);
        pm_push(r, o.esp);
        pm_push(r, o.eflags);
        pm_push(r, o.cs);
        pm_push(r, o.eip);
        pm_push(r, err);
        pm_push(r, SEL_PMBP);
        pm_push(r, PMBP_EXCRET);
        r->cs = sysvm.excvec_sel[vec];
        r->eip = sysvm.excvec_off[vec];
        r->eflags &= ~(EFL_IF | EFL_TF);
        return;
    }
    {
        u8 *c = (u8 *)(sel_base(r->cs) + (sysvm.client32 ? r->eip : (r->eip & 0xFFFF)));
        u8 *d = ldt_desc(err);
        trace("code %x %x %x %x %x %x  desc(err) %x %x %x %x %x %x %x %x\n", c[0], c[1], c[2], c[3], c[4], c[5],
              d[0], d[1], d[2], d[3], d[4], d[5], d[6], d[7]);
    }
    panic("client exception %x err %x at %x:%x (eax %x ebx %x ds %x es %x fs %x gs %x ss:esp %x:%x cr2 %x)",
          vec, err, r->cs, r->eip, r->eax, r->ebx, r->ds, r->es, r->fs, r->gs, r->ss, r->esp, get_cr2());
}
