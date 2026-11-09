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

static u32 ldt_used[8192 / 32];

static u8 *ldt_desc(u32 sel)
{
    return ldt + (sel & ~7u);
}

static int sel_valid(u32 sel)
{
    u32 i = sel >> 3;
    return (sel & 4) && i < 8192 && (ldt_used[i / 32] & (1u << (i % 32)));
}

u32 sel_base(u32 sel)
{
    if (sel & 4) return desc_base(ldt_desc(sel));
    if ((sel & ~3u) == (SEL_PMBP & ~3u)) return pmbp_lin;
    return desc_base(gdt + (sel & ~7u));
}

/* Allocate n consecutive LDT selectors; 0 if none. */
static u32 ldt_alloc(u32 n)
{
    u32 i, j;
    for (i = LDT_FIRST; i + n <= 8192; i++) {
        for (j = 0; j < n; j++)
            if (ldt_used[(i + j) / 32] & (1u << ((i + j) % 32))) break;
        if (j == n) {
            for (j = 0; j < n; j++) {
                ldt_used[(i + j) / 32] |= 1u << ((i + j) % 32);
                /* data, present, DPL 3, base 0, limit 0 */
                set_desc(ldt + (i + j) * 8, 0, 0, 0xF2, 0);
            }
            return (i << 3) | 7;
        }
    }
    return 0;
}

static void ldt_free(u32 sel)
{
    u32 i = sel >> 3;
    ldt_used[i / 32] &= ~(1u << (i % 32));
    memset(ldt + i * 8, 0, 8);
}

/* A selector for a real-mode segment (0002h): one per segment, kept. */
struct segsel { struct segsel *next; u32 seg, sel; };
static struct segsel *segsels;

static u32 seg_to_sel(u32 seg, u32 access)
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
static int sel_to_seg(u32 sel)
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

    memset(ldt_used, 0, sizeof(ldt_used));
    ldt_used[0] = 0x0001FFFF;           /* 0-10h reserved */
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
    sysvm.pm = 1;
}

/* ---- reflecting into V86 mode and back ---- */
static int to_v86_seg(u32 sel, u32 *seg)
{
    int s = sel_to_seg(sel);
    if (s < 0) { *seg = 0; return 0; }
    *seg = s;
    return 1;
}

/* Run the V86 handler of vec (or, for NEST_0300, the prepared state) for
 * the protected-mode state in *r; dpmi_refret comes back. */
static void nest_v86(struct cregs *r, int kind, struct cregs *v)
{
    struct nest *n = kmalloc(sizeof(*n));
    if (!n) panic("out of memory (nest)");
    n->pm = *r;
    n->kind = kind;
    n->prev = sysvm.nest;
    n->v86sp = sysvm.refsp;
    sysvm.nest = n;
    *r = *v;
}

int pm_v86_reflect(struct cregs *r, int vec, int hw)
{
    struct cregs v;
    u16 *ivt = (u16 *)0;

    v = *r;
    if (hw) {
        v.ds = v.es = 0;
    } else {
        if (!to_v86_seg(r->ds, &v.ds) && (r->ds & ~3u))
            trace("DPMI: INT %x AX=%x: DS %x not in conventional memory\n", vec, AX(r), r->ds);
        if (!to_v86_seg(r->es, &v.es) && (r->es & ~3u))
            trace("DPMI: INT %x AX=%x: ES %x not in conventional memory\n", vec, AX(r), r->es);
    }
    v.fs = v.gs = 0;
    v.ss = info->refstk_seg;
    v.esp = sysvm.refsp;
    v.eflags = (r->eflags & 0xFFFF & ~(EFL_NT)) | EFL_VM | EFL_IOPL;
    /* an IRET frame back to the REFRET breakpoint */
    v86_push16(&v, (u16)v.eflags);
    v86_push16(&v, (u16)info->ldr_cs);
    v86_push16(&v, (u16)(info->bp_off + W386_BP_REFRET));
    v.eflags &= ~(EFL_IF | EFL_TF);
    v.eip = ivt[vec * 2];
    v.cs = ivt[vec * 2 + 1];
    nest_v86(r, hw ? NEST_HWINT : NEST_INT, &v);
    sysvm.refsp = (u16)v.esp;
    return 1;
}

#pragma pack(1)
struct rmregs {             /* DPMI real-mode register structure */
    u32 edi, esi, ebp, res, ebx, edx, ecx, eax;
    u16 flags, es, ds, fs, gs, ip, cs, sp, ss;
};
#pragma pack()

void dpmi_refret(struct cregs *r)
{
    struct nest *n = sysvm.nest;
    struct cregs v = *r;
    if (!n) panic("REFRET without a nested call");
    sysvm.nest = n->prev;
    sysvm.refsp = n->v86sp;
    *r = n->pm;
    if (n->kind == NEST_INT) {
        u32 sel;
        r->eax = v.eax; r->ebx = v.ebx; r->ecx = v.ecx; r->edx = v.edx;
        r->esi = v.esi; r->edi = v.edi; r->ebp = v.ebp;
        r->eflags = (r->eflags & ~0x8D5u) | (v.eflags & 0x8D5u);    /* OF DF? no: CF PF AF ZF SF OF */
        /* segment registers the V86 code changed come back as selectors */
        if (sel_to_seg(r->ds) != (int)(v.ds & 0xFFFF)) {
            sel = seg_to_sel(v.ds & 0xFFFF, 0xF2);
            if (sel) r->ds = sel;
        }
        if (sel_to_seg(r->es) != (int)(v.es & 0xFFFF)) {
            sel = seg_to_sel(v.es & 0xFFFF, 0xF2);
            if (sel) r->es = sel;
        }
    } else if (n->kind == NEST_0300) {
        struct rmregs *s = (struct rmregs *)n->regstruct;
        s->edi = v.edi; s->esi = v.esi; s->ebp = v.ebp; s->ebx = v.ebx;
        s->edx = v.edx; s->ecx = v.ecx; s->eax = v.eax;
        s->flags = (u16)v.eflags;
        s->es = (u16)v.es; s->ds = (u16)v.ds; s->fs = (u16)v.fs; s->gs = (u16)v.gs;
        set_cf(r, 0);
    }
    kfree(n);
}

/* DPMI 0300h-0302h: run real-mode code with the client's register structure */
static void dpmi_simulate(struct cregs *r, int fn)
{
    u32 lin = sel_base(r->es) + (sysvm.client32 ? r->edi : DI(r));
    struct rmregs *s = (struct rmregs *)lin;
    struct cregs v;
    u16 *ivt = (u16 *)0;
    u32 ncopy = CX(r) * 2, i;
    u8 *src;

    memset(&v, 0, sizeof(v));
    v.edi = s->edi; v.esi = s->esi; v.ebp = s->ebp; v.ebx = s->ebx;
    v.edx = s->edx; v.ecx = s->ecx; v.eax = s->eax;
    v.es = s->es; v.ds = s->ds; v.fs = s->fs; v.gs = s->gs;
    if (s->ss || s->sp) { v.ss = s->ss; v.esp = s->sp; }
    else { v.ss = info->refstk_seg; v.esp = sysvm.refsp; }
    v.eflags = (s->flags & 0x0FD5) | EFL_VM | EFL_IOPL | EFL_IF | 2;
    /* parameters copied from the client's stack */
    src = (u8 *)(stack_lin(r));
    for (i = ncopy; i > 0; i -= 2)
        v86_push16(&v, *(u16 *)(src + i - 2));
    if (fn == 0x0300 || fn == 0x0302)
        v86_push16(&v, (u16)v.eflags);
    v86_push16(&v, (u16)info->ldr_cs);
    v86_push16(&v, (u16)(info->bp_off + W386_BP_REFRET));
    if (fn == 0x0300) {
        v.eip = ivt[BX(r) & 0xFF] * 1;
        v.eip = ivt[(BX(r) & 0xFF) * 2];
        v.cs = ivt[(BX(r) & 0xFF) * 2 + 1];
        v.eflags &= ~(EFL_IF | EFL_TF);
    } else {
        v.eip = s->ip;
        v.cs = s->cs;
    }
    nest_v86(r, NEST_0300, &v);
    sysvm.nest->regstruct = lin;
    if (!(s->ss || s->sp)) sysvm.refsp = (u16)(v.esp - 0);
    /* the parameters stay on the V86 stack: the callee (or the caller's
     * convention) handles them; REFRET restores the reflection stack */
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

static void dos_alloc(struct cregs *r, u32 fn)
{
    /* run INT 21h 48h/49h/4Ah in V86 mode; the result arrives in REFRET.
     * Done synchronously through a nested call is not possible here, so
     * the client's registers are prepared for INT 21h and the selector work
     * happens afterwards: stage 1 keeps it simple and fails instead. */
    (void)fn;
    trace("DPMI: 01%02xh not implemented yet\n", fn & 0xFF);
    set_cf(r, 1);
    SETW(r->eax, 8);
}

static void dpmi_call(struct cregs *r)
{
    u32 fn = AX(r), sel, i, v;
    u8 *d;
    struct memblk *b;

    set_cf(r, 0);
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
    case 0x168A:                        /* vendor entry: not offered yet */
        return 1;
    }
    return 0;
}

/* ---- interrupts in protected mode ---- */
static int pmvec_default(int vec)
{
    return sysvm.pmvec_sel[vec] == SEL_PMBP && sysvm.pmvec_off[vec] == (u32)vec;
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
        pm_v86_reflect(r, index, index >= 8 && index < 16);
        return;
    }
    if (index == PMBP_EXCRET) {
        /* back from a client's exception handler: the DPMI frame */
        u32 err, ip, cs, fl, sp, ss;
        pm_pop(r); pm_pop(r);           /* our return address */
        err = pm_pop(r);
        ip = pm_pop(r); cs = pm_pop(r); fl = pm_pop(r); sp = pm_pop(r); ss = pm_pop(r);
        (void)err;
        r->eip = ip; r->cs = cs; r->eflags = (fl & 0xFFFF) | EFL_IOPL; r->esp = sp; r->ss = ss;
        return;
    }
    panic("unknown PM breakpoint %x", index);
}

void dpmi_exception(struct cregs *r, int vec, u32 err)
{
    if (vec < 32 && sysvm.excvec_sel[vec]) {
        /* the DPMI 0.9 exception frame: return address, error code, IP,
         * CS, FLAGS, SP, SS (words for a 16-bit client) */
        struct cregs o = *r;
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
    panic("client exception %x err %x at %x:%x (eax %x ds %x es %x ss:esp %x:%x cr2 %x)",
          vec, err, r->cs, r->eip, r->eax, r->ds, r->es, r->ss, r->esp, get_cr2());
}
