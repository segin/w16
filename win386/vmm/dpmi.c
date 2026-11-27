/* dpmi.c - w16 VMM: the DPMI 0.9 host for the System VM's protected-mode
 * program (KRNL386), ARCHITECTURE.md 12.3.
 *
 * Source: DPMI Specification 0.9 (INT 2Fh 1687h, the mode switch, INT 31h
 * functions 0000h-0A00h, interrupt and exception handling, the real-mode
 * register structure); Windows 3.1 DDK INT 2Fh 1684h/1686h; observation of
 * Windows 3.1 (ARCHITECTURE.md 12.2: selectors, version values).
 *
 * Stage 1: one client; protected mode runs at ring 3 with IOPL 0, as under
 * Windows 3.1 (VX6, ARCHITECTURE.md 12.3): CLI and STI trap and the VMM
 * sets the client's IF (vmm.c), POPF and IRET leave IF as it is, port I/O
 * goes through the TSS's I/O bitmap; interrupts the client has
 * not hooked are reflected to V86 mode, with DS and ES translated when
 * their selectors map conventional memory on a paragraph boundary (INT
 * 21h and INT 10h calls with other pointers: dosx.c).
 */
#include "vmm.h"

extern u32 pmbp_lin;
void page_free(u32 phys);

/* The host's state is the current VM's (struct dpmi_vm, vmm.h): each VM
 * has its own LDT, vectors, blocks and host stack. The System VM's LDT
 * hands out from index 11h (Windows 3.1: DS 8Fh); a DOS VM's from 10h, the
 * first being a 16-bit client's host stack (Windows 3.1 in a DOS box,
 * DPMIPRB: a 16-bit client gets DS 8Fh, a 32-bit one 87h) */
#define DP          (cur_vm->dp)
#define ldt_top     (DP.x_ldt_top)
#define ldt_alias   (DP.x_ldt_alias)
#define exc_last    (DP.x_exc_last)
#define segsels     (DP.x_segsels)
#define dosblks     (DP.x_dosblks)
#define env_seg     (DP.x_env_seg)
#define hstk_sel    (DP.x_hstk_sel)
#define hstk_lin    (DP.x_hstk_lin)
#define hstk_depth  (DP.x_hstk_depth)
#define hstk_low    (DP.x_hstk_low)
#define pmw_ss      (DP.x_pmw_ss)
#define pmw_sp      (DP.x_pmw_sp)
#define LDT_FIRST   (DP.ldt_first)

/* LDT ownership. KRNL386 takes every all-zero descriptor below the limit
 * of the LDT alias (the vendor API's selector) for its own free list, and
 * asks for 100h more selectors with 0000h while that limit is below F000h
 * (Pietrek, Windows Internals ch. 1-2). So here a descriptor is free if
 * and only if its 8 bytes are zero, and the LDT grows: ldt_top is the
 * size in use (the alias' limit + 1), raised 4 KB at a time when 0000h
 * needs room, up to the architecture's 64 KB. UNVERIFIED: Windows 3.1's
 * starting size and step. */
static void exc_default(struct cregs *r, int vec, u32 err);

extern int dpmi_verbose;

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

/* A selector's base; for one with no descriptor (an LDT index past the
 * LDT, a GDT index past the GDT) the VMM's own window, which no client
 * check lets through (lin_user_ok) */
u32 sel_base(u32 sel)
{
    if (sel & 4) return (sel & 0xFFFF) < ldt_limit + 1 ? desc_base(ldt_desc(sel)) : W386_VMM_LIN;
    if ((sel & ~3u) == (SEL_PMBP & ~3u)) return pmbp_lin;
    if ((sel & 0xFFFF) >> 3 >= GDT_ENTRIES) return W386_VMM_LIN;
    return desc_base(gdt + (sel & ~7u));
}

/* a selector with a present descriptor (Map_Flat's test) */
int sel_present(u32 sel)
{
    sel &= 0xFFFF;
    if (!(sel & ~3u)) return 0;
    if (sel & 4) return sel < ldt_limit + 1 && (ldt_desc(sel)[5] & 0x80);
    if ((sel & ~3u) == (SEL_PMBP & ~3u)) return 1;
    return (sel >> 3) < GDT_ENTRIES && (gdt[(sel & ~7u) + 5] & 0x80);
}

/* the D bit of a code segment, the B bit of a stack (bit 6 of byte 6) */
int seg_32(u32 sel)
{
    u8 *d;
    if (sel & 4) {
        if ((sel & 0xFFFF) >= ldt_limit + 1) return 0;
        d = ldt_desc(sel);
    } else {
        if ((sel & 0xFFFF) >> 3 >= GDT_ENTRIES) return 0;
        d = gdt + (sel & ~7u);
    }
    return (d[6] & 0x40) != 0;
}

/* The client's sel:off for len bytes as a linear address the VMM may use,
 * or NULL: the selector has a present data or code descriptor of DPL 3,
 * and ring 3 could reach every byte (lin_user_ok). The segment's limit is
 * not checked: Windows 3.1's host does not (VX11 run L: 000Bh wrote its 8
 * bytes past ES's limit). The offset is 16 bits for a 16-bit client. */
u8 *cl_ptr(u32 sel, u32 off, u32 len, int write)
{
    u8 *d;
    u32 lin;
    sel &= 0xFFFF;
    if (!(sel & ~3u)) return NULL;
    if (sel & 4) {
        if (sel >= ldt_limit + 1) return NULL;
        d = ldt_desc(sel);
    } else if ((sel & ~3u) == (SEL_PMBP & ~3u)) {
        return NULL;
    } else {
        if (sel >> 3 >= GDT_ENTRIES) return NULL;
        d = gdt + (sel & ~7u);
    }
    if ((d[5] & 0x90) != 0x90 || ((d[5] >> 5) & 3) != 3) return NULL;
    lin = desc_base(d) + (cur_vm->client32 ? off : (off & 0xFFFF));
    return lin_user_ok(lin, len, write) ? (u8 *)lin : NULL;
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
                if (dpmi_verbose && cur_vm != &sysvm) trace("{L%x+%d}", (i << 3) | 7, n);
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
struct segsel { struct segsel *next; u32 seg, sel, acc; };

static void segsels_drop(struct vm *v)
{
    while (v->dp.x_segsels) {
        struct segsel *n = v->dp.x_segsels->next;
        kfree(v->dp.x_segsels);
        v->dp.x_segsels = n;
    }
}

u32 seg_to_sel(u32 seg, u32 access)
{
    struct segsel *s;
    u32 sel;
    /* a DOS VM's client's own PSP: the selector the switch made (Windows
     * 3.1 in a DOS box, DPMIPRB: INT 21h 62h and 2Fh from protected mode
     * give 9Fh, the switch's ES) */
    if (cur_vm != &sysvm && cur_vm->pm && seg == cur_vm->psp_seg && DP.psp_sel && (access & 0xFE) == 0xF2)
        return DP.psp_sel;
    /* one kept is used only while its descriptor is still the one made
     * here: KRNL386 frees and reuses LDT entries by writing the LDT */
    for (s = segsels; s; s = s->next)
        if (s->seg == seg && (ldt_desc(s->sel)[5] & 0xFE) == (access & 0xFE)
            && desc_base(ldt_desc(s->sel)) == seg << 4 && *(u16 *)ldt_desc(s->sel) == 0xFFFF
            && !(ldt_desc(s->sel)[6] & 0x8F))
            return s->sel;
    for (s = segsels; s && (s->seg != seg || s->acc != (access & 0xFE)); s = s->next)
        ;
    sel = ldt_alloc(1);
    if (!sel) return 0;
    set_desc(ldt_desc(sel), seg << 4, 0xFFFF, access, 0);
    if (s) {                            /* the segment's entry, renewed */
        s->sel = sel;
        return sel;
    }
    s = kmalloc(sizeof(*s));
    if (!s) return sel;
    s->acc = access & 0xFE;
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
/* The stack's offsets are 32 bits when SS is a big segment (its B bit:
 * a 16-bit client's 32-bit stack, as the processor itself uses ESP
 * there); the words are the client's size */
static int stack_big(struct cregs *r)
{
    return cur_vm->client32 || seg_32(r->ss);
}

static u32 stack_lin(struct cregs *r)
{
    return sel_base(r->ss) + (stack_big(r) ? r->esp : (r->esp & 0xFFFF));
}

static void pm_push(struct cregs *r, u32 v)
{
    if (cur_vm->client32) {
        r->esp -= 4;
        *(u32 *)stack_lin(r) = v;
    } else {
        if (stack_big(r)) r->esp -= 2; else SETW(r->esp, r->esp - 2);
        *(u16 *)stack_lin(r) = (u16)v;
    }
}

static u32 pm_pop(struct cregs *r)
{
    u32 v;
    if (cur_vm->client32) {
        v = *(u32 *)stack_lin(r);
        r->esp += 4;
    } else {
        v = *(u16 *)stack_lin(r);
        if (stack_big(r)) r->esp += 2; else SETW(r->esp, r->esp + 2);
    }
    return v;
}

static void set_cf(struct cregs *r, int c)
{
    if (c) r->eflags |= EFL_CF;
    else r->eflags &= ~EFL_CF;
}

/* ---- start ---- */
/* The protected-mode vectors a client starts with: the host's default
 * handlers, or a device's (Set_PM_Int_Vector before Sys_VM_Init makes its
 * routine every VM's default: the DDK; Visual C++'s DOSXNT.386 hooks INT
 * 31h so) */
u32 pmdef_sel[256], pmdef_off[256];

/* the host stack: linear 300000h in every VM's own page table (below the
 * clients' memory, which starts at 4 MB and is shared) */
#define HSTK_LIN  0x00300000
/* Windows 3.1 (VX4): SS 0087h with SP 0FF6h at the handler's entry, i.e.
 * a 4 KB stack of the host's, the same whether the interrupt came in V86
 * mode or in protected mode */
#define HSTK_TOP  0x1000
#define HSTK_SIZE 0x2000                /* mapped (zeros) above the top too: VX4 reads 0 there under Windows 3.1 */

void dpmi_init(void)
{
    int i;

    for (i = 0; i < 256; i++) {
        pmdef_sel[i] = cur_vm->pmvec_sel[i] = SEL_PMBP;  /* the host's default handlers */
        pmdef_off[i] = cur_vm->pmvec_off[i] = i;
    }
    DP.ldt_first = 0x11;
    ldt_top = 0x1000;
    hstk_low = HSTK_TOP;
    /* a DOS VM's 32-bit client's host stack (its 16-bit clients' is an
     * LDT selector, as Windows 3.1's 87h) */
    set_desc(gdt + (SEL_HSTK32 & ~7u), HSTK_LIN, HSTK_SIZE - 1, 0xF2, 0x4);
}

/* vmm_switch: v's LDT is the one the processor uses (the System VM's, the
 * VMM's own, also for a DOS VM without a client) */
extern u8 *ldt_sys;
void dpmi_vm_in(struct vm *v)
{
    u8 *l = v->dp.ldt ? v->dp.ldt : ldt_sys;
    if (l == ldt) return;
    ldt = l;
    set_desc(gdt + SEL_LDT, (u32)ldt, ldt_limit, 0x82, 0);
    lldt(SEL_LDT);
}

/* A client inside another in the same VM (a DOS extender EXECing another
 * program that enters protected mode): the outer one's state, back when
 * the inner one ends. The inner client shares the LDT, which is put back
 * as it was, and the VM's vectors. */
struct dpmi_outer {
    struct dpmi_outer *next;
    struct cregs v86save;
    u16 psp_seg, o_env_seg, psp_sel;
    int client32;
    u32 dsel, o_ldt_top, ldt_first;
    u32 dta_sel, dta_off;
    int dta_set;
    u8 *ldt;                            /* a copy of the LDT's ldt_top bytes */
};

static void hstk_make(void);
static void *dp_alloc(u32 bytes);
static void dp_free(void *p, u32 bytes);
static void segsels_drop(struct vm *v);
static void dosblks_drop(struct vm *v);
static void mem_free_level(struct vm *v, u32 level);
static void rmcb_free_level(struct vm *v, u32 level);
static void vectors_default(struct vm *v)
{
    memcpy(v->pmvec_sel, pmdef_sel, sizeof(pmdef_sel));
    memcpy(v->pmvec_off, pmdef_off, sizeof(pmdef_off));
    memset(v->excvec_sel, 0, sizeof(v->excvec_sel));
    memset(v->excvec_off, 0, sizeof(v->excvec_off));
}

/* a client starts in a DOS VM: 0 if it cannot */
static int client_start(int c32)
{
    struct vm *v = cur_vm;
    if (!DP.level) {
        if (!DP.ldt) {
            DP.ldt = dp_alloc(65536);
            if (!DP.ldt) return 0;
        }
        memset(DP.ldt, 0, 65536);
        dpmi_vm_in(v);
        ldt_top = 0x1000;
        DP.ldt_first = 0x10;
        ldt_alias = 0;
        segsels_drop(v);                /* (their descriptors are gone with the LDT) */
        dosblks_drop(v);
        hstk_sel = 0;
        hstk_depth = 0;
        hstk_low = HSTK_TOP;
        DP.dta_set = 0;
        DP.wp_used = 0;
        vectors_default(v);
    } else {
        struct dpmi_outer *o;
        u8 *copy;
        /* Windows 3.1 (DPMIPRB N, O): a 16-bit client EXECs a 16-bit one,
         * which runs; a 32-bit one's switch fails */
        if (c32 != v->client32) return 0;
        o = kmalloc(sizeof(*o));
        copy = o ? dp_alloc(ldt_top) : NULL;
        if (!copy) { kfree(o); return 0; }
        memcpy(copy, ldt, ldt_top);
        o->ldt = copy;
        o->o_ldt_top = ldt_top;
        o->ldt_first = DP.ldt_first;
        o->v86save = v->v86save;
        o->psp_seg = v->psp_seg;
        o->o_env_seg = env_seg;
        o->psp_sel = DP.psp_sel;
        o->client32 = v->client32;
        o->dsel = v->dsel;
        o->dta_sel = DP.dta_sel; o->dta_off = DP.dta_off; o->dta_set = DP.dta_set;
        o->next = DP.outer;
        DP.outer = o;
        DP.dta_set = 0;
    }
    DP.level++;
    v->client32 = c32;
    return 1;
}


/* The mode switch: a far call from V86 mode to the 1687h entry. AX bit 0:
 * a 32-bit client; ES: host data (none asked). */
void dpmi_switch(struct cregs *r)
{
    u32 ip, cs, dsel, csel, ssel, psel, esel, psp, env, big;
    u8 *pspp;

    ip = v86_pop16(r);
    cs = v86_pop16(r);
    if (cur_vm != &sysvm) {
        if (!client_start(AX(r) & 1)) {
            trace("DPMI: VM %d: no memory for a client\n", cur_vm->id);
            r->eip = ip;
            r->cs = cs;
            r->eflags |= EFL_CF;
            return;
        }
        /* the first 16-bit client's host stack first: Windows 3.1's 87h */
        if (DP.level == 1 && !cur_vm->client32) hstk_make();
    } else {
        DP.level = 1;
        cur_vm->client32 = AX(r) & 1;
    }
    /* a DOS VM's 32-bit client: every data selector big (Windows 3.1 in
     * a DOS box, DPMIPRB: DS, SS, the PSP's and the environment's; DPMI
     * 0.9: "stack and data selectors for 32-bit programs will be 32-bit") */
    big = cur_vm != &sysvm && cur_vm->client32 ? 0x4 : 0;
    cur_vm->v86save = *r;
    cur_vm->v86save.eip = ip;
    cur_vm->v86save.cs = cs;
    psp = info->sda_lin ? *(u16 *)(info->sda_lin + 0x10) : 0;
    cur_vm->psp_seg = psp;
    /* Windows 3.1 gave DS 8Fh, CS 97h, the PSP 9Fh, the environment A7h (or
     * AFh) in that order; SS shared DS's selector when equal */
    dsel = ldt_alloc(1);
    set_desc(ldt_desc(dsel), (r->ds & 0xFFFF) << 4, 0xFFFF, 0xF2, big);
    csel = ldt_alloc(1);
    set_desc(ldt_desc(csel), cs << 4, 0xFFFF, 0xFA, 0);
    psel = ldt_alloc(1);
    set_desc(ldt_desc(psel), psp << 4, 0xFF, 0xF2, big);
    DP.psp_sel = (u16)psel;
    if ((r->ss & 0xFFFF) == (r->ds & 0xFFFF))
        ssel = dsel;
    else {
        ssel = ldt_alloc(1);
        set_desc(ldt_desc(ssel), (r->ss & 0xFFFF) << 4, 0xFFFF, 0xF2, cur_vm->client32 ? 0x4 : 0);
    }
    pspp = (u8 *)(psp << 4);
    env = *(u16 *)(pspp + 0x2C);
    env_seg = 0;
    if (psp && env) {
        env_seg = (u16)env;
        esel = ldt_alloc(1);
        set_desc(ldt_desc(esel), env << 4, 0xFFFF, 0xF2, big);
        *(u16 *)(pspp + 0x2C) = (u16)esel;
    }
    /* a nested 16-bit client: one more selector after the environment's
     * (Windows 3.1, DPMIPRB N: the inner client's next 0000h selector
     * came one later than w16's without it) */
    if (cur_vm != &sysvm && DP.level > 1 && !cur_vm->client32) {
        /* (its handlers still run on the outer client's, 87h: what the
         * selector is for is UNVERIFIED; it maps the host stack here) */
        u32 h = ldt_alloc(1);
        if (h) memcpy(ldt_desc(h), ldt_desc(hstk_sel), 8);
    }
    trace("DPMI: VM %d: %d-bit client (level %d), PSP %x, cs %x ds %x ss %x es %x\n", cur_vm->id,
          cur_vm->client32 ? 32 : 16, DP.level, psp, csel, dsel, ssel, psel);
    r->cs = csel;
    r->eip = ip;
    r->ds = dsel;
    r->ss = ssel;
    r->es = psel;
    r->fs = r->gs = 0;
    /* DPMI 0.9: a 32-bit client's ESP has its high word 0; Windows 3.1
     * leaves a 16-bit one's as it was in a DOS box (DPMIPRB) */
    if (cur_vm == &sysvm ? !cur_vm->client32 : cur_vm->client32) r->esp &= 0xFFFF;
    r->eflags = (r->eflags & ~(EFL_VM | EFL_CF | EFL_NT | EFL_IOPL)) | EFL_IF;
    cur_vm->dsel = dsel;
    cur_vm->pm = 1;
    vxd_pm_app(1, r);
}

/* ---- reflecting into V86 mode and back ---- */
int dpmi_verbose;

static int ps2_set_handler(struct cregs *r);

/* An interrupt from protected mode that the client has not hooked: run the
 * V86 handler with the client's registers; DS and ES become segments where
 * their selectors map conventional memory. Results come back, and segment
 * registers the V86 code changed come back as selectors. */
int pm_v86_reflect(struct cregs *r, int vec, int hw)
{
    struct cregs v, pm = *r;
    int ds, es;

    if (dpmi_verbose && !hw) trace("[R%x:%x %x %x %x]", vec, AX(r), BX(r), CX(r), DX(r));
    if (!hw && vec == 0x15 && AX(r) == 0xC207 && cur_vm == &sysvm) return ps2_set_handler(r);
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
    /* most calls do not use DS or ES at all: noted only in the detailed
     * trace (the serial port is slow, and the boot makes thousands) */
    if (dpmi_verbose && ds < 0 && (pm.ds & 4))
        trace("DPMI: INT %x AX=%x: DS %x not in conventional memory\n", vec, AX(r), pm.ds);
    if (dpmi_verbose && es < 0 && (pm.es & 4))
        trace("DPMI: INT %x AX=%x: ES %x not in conventional memory\n", vec, AX(r), pm.es);
    /* a selector V86 code cannot see becomes the transfer buffer's
     * segment, not 0: a call that writes through it (INT 21h 29h's FCB
     * at ES:DI, a BIOS state save) then writes there, not over the
     * interrupt vectors */
    v.ds = ds < 0 ? xbuf_seg() : ds;
    v.es = es < 0 ? xbuf_seg() : es;
    v.eflags = pm.eflags;
    v86_int_sim(&v, vec);
    if (dpmi_verbose && vec == 0x21 && ds >= 0) {
        u32 ah = (AX(&pm) >> 8) & 0xFF;
        if (ah == 0x3D || ah == 0x3C || ah == 0x4E || ah == 0x43 || ah == 0x5B || ah == 0x41)
            trace("{%s cf=%d ax=%x}", (char *)(((u32)ds << 4) + DX(&pm)), v.eflags & 1, v.eax & 0xFFFF);
        else if (ah == 0x6C)
            trace("{%s cf=%d ax=%x}", (char *)(((u32)ds << 4) + SI(&pm)), v.eflags & 1, v.eax & 0xFFFF);
    }
    *r = pm;
    r->eax = v.eax; r->ebx = v.ebx; r->ecx = v.ecx; r->edx = v.edx;
    r->esi = v.esi; r->edi = v.edi; r->ebp = v.ebp;
    r->eflags = (r->eflags & ~0x8D5u) | (v.eflags & 0x8D5u);    /* CF PF AF ZF SF OF */
    /* DOS functions that return no pointer keep the caller's segment
     * registers: MS-DOS 4.00 changes ES as a side effect in some (INT 21h
     * 3Eh, close, came back with another ES; KRNL386 then faulted with ES
     * 0 at 0117:A789 after closing BOOTLOG.TXT: the boot on the MS-DOS 4.00
     * image, INPUTRUN_HDD). The ones that return a pointer in DS or ES
     * (MS-DOS 4.00: 1Fh, 32h DS:BX the DPB, 34h ES:BX InDOS, 52h ES:BX the
     * list of lists, 5D06h DS:SI the swappable data area, 63h DS:SI the
     * lead-byte table) translate it as before */
    if (vec == 0x21) {
        u32 ah = (AX(&pm) >> 8) & 0xFF;
        if (ah != 0x1F && ah != 0x32 && ah != 0x34 && ah != 0x52 && ah != 0x5D && ah != 0x63)
            return 1;
    }
    if ((int)(v.ds & 0xFFFF) != (ds < 0 ? (int)xbuf_seg() : ds)) {
        u32 sel = seg_to_sel(v.ds & 0xFFFF, 0xF2);
        if (sel) r->ds = sel;
    }
    if ((int)(v.es & 0xFFFF) != (es < 0 ? (int)xbuf_seg() : es)) {
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
    struct rmregs *s = (struct rmregs *)cl_ptr(r->es, r->edi, sizeof(struct rmregs), 1);
    struct cregs v, pm = *r;
    u32 ncopy = CX(r) * 2, i;
    u8 *src;

    /* a structure the client cannot reach, or more words than the stack
     * they go to can take: the call fails (Windows 3.1 ends in both: VX11
     * runs P and S) */
    if (!s) { set_cf(r, 1); return; }
    src = (u8 *)stack_lin(r);
    if (ncopy && !lin_user_ok((u32)src, ncopy, 0)) { set_cf(r, 1); return; }
    if (!s->ss && !s->sp) {
        u32 low = (u32)cur_vm->refstk_top - (cur_vm == &sysvm ? W386_REFSTK_SIZE : DOSVM_REFSTK_SIZE) + 256;
        if ((u32)cur_vm->refsp < low || ncopy + 256 > (u32)cur_vm->refsp - low) { set_cf(r, 1); return; }
    }
    memset(&v, 0, sizeof(v));
    v.edi = s->edi; v.esi = s->esi; v.ebp = s->ebp; v.ebx = s->ebx;
    v.edx = s->edx; v.ecx = s->ecx; v.eax = s->eax;
    v.es = s->es; v.ds = s->ds; v.fs = s->fs; v.gs = s->gs;
    if (s->ss || s->sp) { v.ss = s->ss; v.esp = s->sp; }
    else { v.ss = cur_vm->refstk_seg; v.esp = cur_vm->refsp; }
    v.eflags = s->flags;
    /* parameters copied from the client's stack */
    for (i = ncopy; i > 0; i -= 2)
        v86_push16(&v, *(u16 *)(src + i - 2));
    if (dpmi_verbose) trace("[31:%x %x:%x]", fn, fn == 0x300 ? BX(r) & 0xFF : s->cs, s->ip);
    if (fn == 0x0300)
        v86_int_sim(&v, BX(r) & 0xFF);
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

/* ---- the client's linear address space: 4 MB up to the VMM's window,
 * in free ranges (first fit, merged when given back), so that what a
 * client frees is used again: Windows 3.1 gave a 64 KB block freed and
 * allocated again 300 times the same address each time (VX11 "mem") ---- */
struct lrange { struct lrange *next; u32 lin, size; };
static struct lrange *lfree;
static int lfree_init;

static void lin_setup(void)
{
    if (lfree_init) return;
    lfree_init = 1;
    lfree = kmalloc(sizeof(*lfree));
    if (!lfree) return;
    lfree->next = NULL;
    lfree->lin = 0x00400000;            /* client linear memory starts at 4 MB */
    lfree->size = W386_VMM_LIN - 0x00400000;
}

static u32 lin_alloc(u32 size)
{
    struct lrange **pp, *f;
    u32 lin;
    lin_setup();
    for (pp = &lfree; (f = *pp) != NULL; pp = &f->next)
        if (f->size >= size) {
            lin = f->lin;
            f->lin += size;
            f->size -= size;
            if (!f->size) { *pp = f->next; kfree(f); }
            return lin;
        }
    return 0;
}

/* the range lin, size taken if it is all free (a block growing in place) */
static int lin_take(u32 lin, u32 size)
{
    struct lrange **pp, *f, *n;
    lin_setup();
    for (pp = &lfree; (f = *pp) != NULL; pp = &f->next)
        if (lin >= f->lin && lin - f->lin < f->size) {
            if (size > f->size - (lin - f->lin)) return 0;
            if (lin + size < f->lin + f->size) {
                n = kmalloc(sizeof(*n));
                if (!n) return 0;
                n->lin = lin + size;
                n->size = f->lin + f->size - (lin + size);
                n->next = f->next;
                f->next = n;
            }
            f->size = lin - f->lin;
            if (!f->size) { *pp = f->next; kfree(f); }
            return 1;
        }
    return 0;
}

static void lin_free(u32 lin, u32 size)
{
    struct lrange **pp, *f, *n;
    if (!size) return;
    for (pp = &lfree; (f = *pp) != NULL && f->lin + f->size < lin; pp = &f->next)
        ;
    if (f && f->lin + f->size == lin) {             /* after f */
        f->size += size;
        if (f->next && f->lin + f->size == f->next->lin) {
            n = f->next;
            f->size += n->size;
            f->next = n->next;
            kfree(n);
        }
        return;
    }
    if (f && lin + size == f->lin) {                /* before f */
        f->lin = lin;
        f->size += size;
        return;
    }
    n = kmalloc(sizeof(*n));
    if (!n) return;                                 /* lost to the space: no harm */
    n->lin = lin;
    n->size = size;
    n->next = f;
    *pp = n;
}

/* pages lin .. lin+size-1 unmapped and their memory freed; a page table
 * left empty goes too */
static void pages_release(u32 lin, u32 size)
{
    u32 a, p, t, i, *pt;
    int need_flush = 0;
    for (a = lin; a < lin + size; a += 4096) {
        if (pager_drop(a)) continue;    /* in the paging file, or never used */
        p = lin_to_phys(a);
        if (!p) continue;
        if (map_page_noflush(a, 0, 0) == 2) need_flush = 1;
        page_free(p & ~0xFFFu);
    }
    for (t = lin >> 22; size && t <= (lin + size - 1) >> 22; t++) {
        if (t == 0 || t >= (W386_VMM_LIN >> 22)) continue;
        if (!(vmm_pdir_entry_get(t) & 1)) continue;
        pt = (u32 *)phys_to_lin(vmm_pdir_entry_get(t) & ~0xFFFu);
        for (i = 0; i < 1024 && !pt[i]; i++)
            ;
        if (i == 1024) {
            p = vmm_pdir_entry_get(t) & ~0xFFFu;
            vmm_pdir_entry(t, 0);
            need_flush = 1;
            page_free(p);
        }
    }
    if (need_flush) vmm_flush_tlb();
}

/* physical memory mapped with 0800h (kept: DPMI 0.9 has no call to free
 * a mapping); the same range asked for again gets the same address, as
 * under Windows 3.1 (VX11 "map": dev, dev2) */
struct physmap { struct physmap *next; u32 phys, size, lin; };
static struct physmap *physmaps;

/* ---- memory blocks (0501h) ---- */
struct memblk { struct memblk *next; u32 lin, size, handle; struct vm *vm; u32 level; };
static struct memblk *blocks;
static u32 next_handle = 1;

/* The most pages one block can have with f pages free: a page table (4 MB)
   of its own may be needed for every 1024 pages of it, and one more where
   it starts within a table (0500h's largest block, mem_alloc's check). */
static u32 mem_largest_pages(u32 f)
{
    u32 t = f / 1025 + 2;
    return f > t ? f - t : 0;
}

/* pages for lin .. lin+n pages; all or none */
static int pages_map(u32 lin, u32 n)
{
    u32 i, p;
    int need_flush = 0, mr;
    if ((mr = pager_map(lin, n)) >= 0) return mr;  /* with a paging file: pager.c */
    for (i = 0; i < n; i++) {
        p = page_alloc();
        if (!p || (mr = map_page_noflush(lin + i * 4096, p, 7)) == 0) {
            if (p) page_free(p);
            if (need_flush) vmm_flush_tlb();
            pages_release(lin, i * 4096);
            return 0;
        }
        if (mr == 2) need_flush = 1;
    }
    if (need_flush) vmm_flush_tlb();
    return 1;
}

static struct memblk *mem_alloc(u32 size)
{
    struct memblk *b;
    u32 n = size / 4096 + (size % 4096 != 0);
    /* the pages and their page tables free, and room in the client's
       linear space (a guard page after each block) */
    if (!size || n > mem_largest_pages(pager_avail())) return NULL;
    b = kmalloc(sizeof(*b));
    if (!b) return NULL;
    b->lin = lin_alloc(n * 4096 + 4096);
    if (!b->lin) { kfree(b); return NULL; }
    b->size = n * 4096;
    b->handle = next_handle++;
    b->vm = cur_vm;
    b->level = DP.level;
    if (!pages_map(b->lin, n)) {
        lin_free(b->lin, n * 4096 + 4096);
        kfree(b);
        return NULL;
    }
    b->next = blocks;
    blocks = b;
    return b;
}

static int mem_free(u32 handle)
{
    struct memblk **pp, *b;
    for (pp = &blocks; (b = *pp) != NULL; pp = &b->next)
        if (b->handle == handle && b->vm == cur_vm) {
            pages_release(b->lin, b->size);
            lin_free(b->lin, b->size + 4096);
            *pp = b->next;
            kfree(b);
            return 1;
        }
    return 0;
}

/* 0503h: smaller in place; larger in place when the addresses after it
 * are free, else moved (contents copied); the handle stays (Windows 3.1
 * kept both the address and the handle: VX11 "mem") */
static int mem_resize(struct memblk *o, u32 size)
{
    u32 n = size / 4096 + (size % 4096 != 0), m = o->size / 4096;
    struct memblk *b;
    if (!size) return 0;
    if (n <= m) {
        pages_release(o->lin + n * 4096, (m - n) * 4096);
        lin_free(o->lin + (n + 1) * 4096, (m - n) * 4096);
        o->size = n * 4096;
        return 1;
    }
    if (n - m <= mem_largest_pages(pager_avail())
        && lin_take(o->lin + (m + 1) * 4096, (n - m) * 4096)) {
        if (pages_map(o->lin + m * 4096, n - m)) {
            o->size = n * 4096;
            return 1;
        }
        lin_free(o->lin + (m + 1) * 4096, (n - m) * 4096);
    }
    b = mem_alloc(size);
    if (!b) return 0;
    memcpy((void *)b->lin, (void *)o->lin, o->size);
    pages_release(o->lin, o->size);
    lin_free(o->lin, o->size + 4096);
    o->lin = b->lin;
    o->size = b->size;
    blocks = b->next;                   /* b is first: mem_alloc put it there */
    kfree(b);
    return 1;
}

static struct memblk *mem_find(u32 handle)
{
    struct memblk *b;
    for (b = blocks; b; b = b->next)
        if (b->handle == handle && b->vm == cur_vm) return b;
    return NULL;
}

/* The host's own tables of a DOS VM (its LDT, a nested client's copy of
 * it, the callbacks): zeroed pages, ring 0 only, in the shared linear
 * space (kmalloc's blocks of more than a page come only from pages never
 * used, which a client that has taken all memory once leaves none of) */
static void *dp_alloc(u32 bytes)
{
    u32 n = (bytes + 4095) / 4096, lin, i, p;
    lin = lin_alloc(n * 4096);
    if (!lin) return NULL;
    for (i = 0; i < n; i++) {
        p = page_alloc();
        if (!p || !map_page(lin + i * 4096, p, 3)) {
            if (p) page_free(p);
            pages_release(lin, i * 4096);
            lin_free(lin, n * 4096);
            return NULL;
        }
    }
    return (void *)lin;
}

static void dp_free(void *ptr, u32 bytes)
{
    u32 n = (bytes + 4095) / 4096;
    if (!ptr) return;
    pages_release((u32)ptr, n * 4096);
    lin_free((u32)ptr, n * 4096);
}

/* a client's blocks given back when it ends (v's from level on; a VM
 * destroyed: all of them) */
static void mem_free_level(struct vm *v, u32 level)
{
    struct memblk **pp, *b;
    for (pp = &blocks; (b = *pp) != NULL; )
        if (b->vm == v && b->level >= level) {
            pages_release(b->lin, b->size);
            lin_free(b->lin, b->size + 4096);
            *pp = b->next;
            kfree(b);
        } else
            pp = &b->next;
}

/* ---- INT 21h 48h, 49h, 4Ah from protected mode: memory of the host's,
 * not DOS's (Windows 3.1, VX11 "d48": AH=48h from a program gives a
 * selector, though no DOS memory was left; the block's first selector
 * spans all of it, then one per further 64 KB, each FFFFh; FFFFh
 * paragraphs fail with AX 8 and BX FFFEh; 4Ah keeps the selector and
 * grows only within its selectors, at most (count * 1000h) - 1
 * paragraphs, else AX 8 and BX that; 49h frees the block and its
 * selectors). UNVERIFIED: the last selector's limit of a block that ends
 * inside it (FFFFh here too only when it is full), BX=0 (one paragraph
 * here) ---- */
struct dosblk { struct dosblk *next; u32 sel, tiles, handle; };

static u32 dosblk_largest(void)
{
    u32 n = mem_largest_pages(pager_avail());
    return n >= 0x1000 ? 0xFFFE : n * 0x100;
}

static void dosblk_descs(struct dosblk *k, u32 lin, u32 bytes)
{
    u32 i;
    for (i = 0; i < k->tiles; i++) {
        u32 left = bytes > i * 0x10000 ? bytes - i * 0x10000 : 0;
        u32 lim = i ? (left > 0x10000 ? 0xFFFF : (left ? left - 1 : 0)) : bytes - 1;
        set_desc(ldt_desc(k->sel + i * 8), lin + i * 0x10000, lim, 0xF2, 0);
    }
}

static void dosblks_drop(struct vm *v)
{
    while (v->dp.x_dosblks) {
        struct dosblk *n = v->dp.x_dosblks->next;
        kfree(v->dp.x_dosblks);
        v->dp.x_dosblks = n;
    }
}

static struct dosblk *dosblk_of(u32 sel)
{
    struct dosblk *k;
    for (k = dosblks; k; k = k->next)
        if ((k->sel & ~3u) == (sel & 0xFFFC)) return k;
    return NULL;
}

/* 1: done; 0: not one of these (a DOS block's selector: dosx.c goes on) */
int dpmi_dosmem(struct cregs *r)
{
    u32 ah = (AX(r) >> 8) & 0xFF, paras, bytes;
    struct dosblk *k, **pp;
    struct memblk *b;

    if (ah == 0x48) {
        paras = BX(r) ? BX(r) : 1;
        bytes = paras * 16;
        k = paras <= 0xFFFE ? kmalloc(sizeof(*k)) : NULL;
        if (k) {
            k->tiles = (bytes + 0xFFFF) >> 16;
            k->sel = ldt_alloc(k->tiles);
            b = k->sel ? mem_alloc(bytes) : NULL;
            if (b) {
                k->handle = b->handle;
                dosblk_descs(k, b->lin, bytes);
                k->next = dosblks;
                dosblks = k;
                SETW(r->eax, k->sel);
                set_cf(r, 0);
                return 1;
            }
            if (k->sel) { u32 i; for (i = 0; i < k->tiles; i++) ldt_free(k->sel + i * 8); }
            kfree(k);
        }
        set_cf(r, 1);
        SETW(r->eax, 8);
        SETW(r->ebx, dosblk_largest());
        return 1;
    }
    if ((k = dosblk_of(r->es)) == NULL) return 0;
    if (ah == 0x49) {
        u32 i;
        mem_free(k->handle);
        for (i = 0; i < k->tiles; i++) { ldt_free(k->sel + i * 8); sel_gone(r, k->sel + i * 8); }
        for (pp = &dosblks; *pp != k; pp = &(*pp)->next)
            ;
        *pp = k->next;
        kfree(k);
        set_cf(r, 0);
        return 1;
    }
    /* 4Ah */
    paras = BX(r) ? BX(r) : 1;
    if (paras > k->tiles * 0x1000 - 1) {
        set_cf(r, 1);
        SETW(r->eax, 8);
        SETW(r->ebx, k->tiles * 0x1000 - 1);
        return 1;
    }
    b = mem_find(k->handle);
    if (!b || !mem_resize(b, paras * 16)) {
        set_cf(r, 1);
        SETW(r->eax, 8);
        SETW(r->ebx, b ? b->size / 16 : 0);
        return 1;
    }
    dosblk_descs(k, b->lin, paras * 16);
    set_cf(r, 0);
    return 1;
}

/* ---- INT 31h ---- */
static u32 reg_ofs(struct cregs *r, u32 e)
{
    return cur_vm->client32 ? e : (e & 0xFFFF);
}

/* A selector freed: segment registers of the client's that hold it are
 * zeroed (Windows 3.1, VX11 run E: ES 0 after 0001h freed it), so the
 * return to the client cannot load a descriptor that is gone */
void sel_gone(struct cregs *r, u32 sel)
{
    sel &= 0xFFF8;
    if ((r->ds & 0xFFF8) == sel) r->ds = 0;
    if ((r->es & 0xFFF8) == sel) r->es = 0;
    if ((r->fs & 0xFFF8) == sel) r->fs = 0;
    if ((r->gs & 0xFFF8) == sel) r->gs = 0;
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
        if (!sel) {
            /* no selector: the block goes back to DOS */
            struct cregs f;
            memset(&f, 0, sizeof(f));
            f.eax = 0x4900;
            f.es = v.eax & 0xFFFF;
            v86_int(&f, 0x21);
            *r = pm;
            set_cf(r, 1);
            SETW(r->eax, 8);
            return;
        }
        set_desc(ldt_desc(sel), (v.eax & 0xFFFF) << 4, BX(r) ? BX(r) * 16 - 1 : 0, 0xF2, 0);
        SETW(r->eax, v.eax);
        SETW(r->edx, sel);
        return;
    }
    /* a selector that is not one (0 too): carry, AX as it was (Windows
     * 3.1, VX11 "dos": 0101h with DX=0) */
    seg = DX(r) ? sel_to_seg(DX(r)) : -1;
    if (seg < 0) { set_cf(r, 1); return; }
    v.es = seg;
    if (fn == 0x0101) v.eax = 0x4900;
    else { v.eax = 0x4A00; v.ebx = BX(r); }
    v86_int(&v, 0x21);
    *r = pm;
    if (v.eflags & EFL_CF) { set_cf(r, 1); SETW(r->eax, v.eax); SETW(r->ebx, v.ebx); return; }
    if (fn == 0x0101) { ldt_free(DX(r)); sel_gone(r, DX(r)); }
    else set_desc(ldt_desc(DX(r)), seg << 4, BX(r) ? BX(r) * 16 - 1 : 0, 0xF2, 0);
}

/* The host's real-mode entries other than the mode switch (0303h's
 * callbacks, 0305h's save/restore, 0306h's raw switch) are one V86
 * breakpoint byte, W386_BP_DPMIRM, reached through different segments:
 * entry k at (loader CS - 1 - k):(its offset + 16 (k + 1)) */
static u32 rm_seg(u32 k)
{
    return (u16)(info->ldr_cs - 1 - k);
}

static u32 rm_off(u32 k)
{
    return (u16)(info->bp_off + W386_BP_DPMIRM + 16 * (k + 1));
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
        /* none asked: carry, AX kept (Windows 3.1 in a DOS box, DPMIPRB) */
        if (!CX(r) && cur_vm != &sysvm) { set_cf(r, 1); break; }
        sel = ldt_alloc(CX(r) ? CX(r) : 1);
        if (!sel) { set_cf(r, 1); SETW(r->eax, 0x8011); break; }
        SETW(r->eax, sel);
        break;
    case 0x0001:                        /* free a descriptor */
        /* one free already, within the LDT: nothing to do, no carry
         * (Windows 3.1 in a DOS box, DPMIPRB) */
        if (cur_vm != &sysvm && (BX(r) & 4) && BX(r) < ldt_top && desc_free(BX(r) >> 3)) break;
        if (!sel_valid(BX(r))) { set_cf(r, 1); break; }
        ldt_free(BX(r));
        sel_gone(r, BX(r));
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
        /* past 1 MB the low 12 bits must be set (DPMI 0.9; Windows 3.1,
         * VX11 "lim": 00100000h fails with carry, AX kept, the limit
         * unchanged; before, it was rounded up silently) */
        if (v > 0xFFFFF && (v & 0xFFF) != 0xFFF) { set_cf(r, 1); break; }
        set_desc(d, desc_base(d), v, d[5], (d[6] >> 4) & 0x4);
        break;
    case 0x0009:                        /* set access rights */
        /* Windows 3.1 (VX11 "ar"): a system descriptor (a call gate, an
         * LDT, a TSS) or DPL 0, 1 or 2 fails with carry, AX as it was,
         * the descriptor unchanged; not present is taken */
        if (!sel_valid(BX(r)) || !(CL(r) & 0x10) || ((CL(r) >> 5) & 3) != 3) { set_cf(r, 1); break; }
        d = ldt_desc(BX(r));
        d[5] = (u8)CL(r);
        d[6] = (u8)((d[6] & 0x0F) | (CH(r) & 0xD0));
        break;
    case 0x000A:                        /* create a data alias */
        if (!(BX(r) & 4) || !sel_valid(BX(r))) { set_cf(r, 1); break; }
        sel = ldt_alloc(1);
        if (!sel) { set_cf(r, 1); break; }
        memcpy(ldt_desc(sel), ldt_desc(BX(r)), 8);
        ldt_desc(sel)[5] = 0xF2;
        /* big for a DOS VM's 32-bit client (Windows 3.1, DPMIPRB: CS's
         * alias, CS being 16-bit) */
        if (cur_vm != &sysvm && cur_vm->client32) ldt_desc(sel)[6] |= 0x40;
        SETW(r->eax, sel);
        break;
    case 0x000D:                        /* allocate a specific descriptor (04h-7Ch) */
        /* Windows 3.1 (DPMIPRB, 4Ch): no carry, AX kept */
        i = BX(r) >> 3;
        if (!(BX(r) & 4) || i >= 16 || !desc_free(i)) { set_cf(r, 1); break; }
        set_desc(ldt + i * 8, 0, 0, 0xF2, 0);
        break;
    case 0x000B:                        /* get descriptor */
        if (!sel_valid(BX(r)) || (d = cl_ptr(r->es, r->edi, 8, 1)) == NULL) { set_cf(r, 1); break; }
        memcpy(d, ldt_desc(BX(r)), 8);
        break;
    case 0x000C:                        /* set descriptor */
        /* the same checks as 0009h (Windows 3.1, VX11 "sd": a call gate,
         * DPL 0 data or code: carry) */
        if (!sel_valid(BX(r)) || (d = cl_ptr(r->es, r->edi, 8, 0)) == NULL
            || !(d[5] & 0x10) || ((d[5] >> 5) & 3) != 3) { set_cf(r, 1); break; }
        memcpy(ldt_desc(BX(r)), d, 8);
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
        if (cur_vm->excvec_sel[i]) {
            SETW(r->ecx, cur_vm->excvec_sel[i]);
            r->edx = cur_vm->client32 ? cur_vm->excvec_off[i] : ((r->edx & 0xFFFF0000u) | (u16)cur_vm->excvec_off[i]);
        } else {
            /* the host's default (dpmi_pmbp: PMBP_EXCDEF), which a
             * client's handler may also chain to with a far jump */
            SETW(r->ecx, SEL_PMBP);
            SETW(r->edx, PMBP_EXCDEF);
        }
        break;
    case 0x0203:                        /* set exception handler */
        i = BX(r) & 0xFF;
        if (i >= 32) { set_cf(r, 1); break; }
        cur_vm->excvec_sel[i] = CX(r);
        cur_vm->excvec_off[i] = reg_ofs(r, r->edx);
        if (CX(r) == SEL_PMBP) cur_vm->excvec_sel[i] = 0;
        break;
    case 0x0204:                        /* get PM interrupt vector */
        i = BX(r) & 0xFF;
        SETW(r->ecx, cur_vm->pmvec_sel[i]);
        if (cur_vm->client32) r->edx = cur_vm->pmvec_off[i];
        else SETW(r->edx, cur_vm->pmvec_off[i]);
        break;
    case 0x0205:                        /* set PM interrupt vector */
        i = BX(r) & 0xFF;
        cur_vm->pmvec_sel[i] = CX(r);
        cur_vm->pmvec_off[i] = reg_ofs(r, r->edx);
        break;
    case 0x0300: case 0x0301: case 0x0302:
        dpmi_simulate(r, fn);
        return;
    case 0x0303:                        /* allocate a real-mode callback */
        if (!DP.rmcb && (DP.rmcb = dp_alloc(DPMI_NRMCB * sizeof(*DP.rmcb))) == NULL) { set_cf(r, 1); break; }
        for (i = 0; i < DPMI_NRMCB && DP.rmcb[i].level; i++)
            ;
        if (i == DPMI_NRMCB || info->bp_off + W386_BP_DPMIRM + 16 * (W386_DPMIRM_CB + i + 1) > 0xFFFF) { set_cf(r, 1); break; }
        DP.rmcb[i].sel = r->ds & 0xFFFF;
        DP.rmcb[i].off = reg_ofs(r, r->esi);
        DP.rmcb[i].ssel = r->es & 0xFFFF;
        DP.rmcb[i].soff = reg_ofs(r, r->edi);
        DP.rmcb[i].level = DP.level ? DP.level : 1;
        SETW(r->ecx, rm_seg(W386_DPMIRM_CB + i));
        SETW(r->edx, rm_off(W386_DPMIRM_CB + i));
        break;
    case 0x0304:                        /* free one: carry, AX kept for one not allocated */
        i = (u16)(info->ldr_cs - 1 - CX(r)) - W386_DPMIRM_CB;
        if (i >= DPMI_NRMCB || !DP.rmcb || !DP.rmcb[i].level || DX(r) != rm_off(W386_DPMIRM_CB + i)) { set_cf(r, 1); break; }
        DP.rmcb[i].level = 0;
        break;
    case 0x0305:                        /* state save/restore: nothing the host needs saved */
        SETW(r->eax, 0x0024);           /* Windows 3.1's buffer size (DPMIPRB) */
        SETW(r->ebx, rm_seg(W386_DPMIRM_SAVE));
        SETW(r->ecx, rm_off(W386_DPMIRM_SAVE));
        SETW(r->esi, SEL_PMBP);
        if (cur_vm->client32) r->edi = PMBP_SAVE; else SETW(r->edi, PMBP_SAVE);
        break;
    case 0x0306:                        /* raw mode switches */
        SETW(r->ebx, rm_seg(W386_DPMIRM_RAW));
        SETW(r->ecx, rm_off(W386_DPMIRM_RAW));
        SETW(r->esi, SEL_PMBP);
        if (cur_vm->client32) r->edi = PMBP_RAW; else SETW(r->edi, PMBP_RAW);
        break;
    case 0x0400:                        /* version: Windows 3.1's values */
        SETW(r->eax, 0x005A);
        SETW(r->ebx, 0x0005);
        SETLO(r->ecx, 3);
        SETW(r->edx, 0x0870);
        break;
    case 0x0500: {                      /* free memory information (pager.c) */
        u32 *p = (u32 *)cl_ptr(r->es, r->edi, 48, 1);
        if (!p) { set_cf(r, 1); break; }
        pager_meminfo(p);
        break;
    }
    case 0x0501:                        /* allocate a memory block */
        /* size 0: carry, AX as it was (Windows 3.1, VX11 "mem") */
        if (!BX(r) && !CX(r)) { set_cf(r, 1); break; }
        b = mem_alloc((BX(r) << 16) | CX(r));
        if (!b) { set_cf(r, 1); SETW(r->eax, 0x8013); break; }
        SETW(r->ebx, b->lin >> 16); SETW(r->ecx, b->lin);
        SETW(r->esi, b->handle >> 16); SETW(r->edi, b->handle);
        break;
    case 0x0502:                        /* free a memory block */
        if (!mem_free((SI(r) << 16) | DI(r))) set_cf(r, 1);
        break;
    case 0x0503: {                      /* resize (mem_resize) */
        struct memblk *o = mem_find((SI(r) << 16) | DI(r));
        u32 size = (BX(r) << 16) | CX(r);
        if (!o || !size) { set_cf(r, 1); break; }
        if (!mem_resize(o, size)) { set_cf(r, 1); SETW(r->eax, 0x8013); break; }
        SETW(r->ebx, o->lin >> 16); SETW(r->ecx, o->lin);
        SETW(r->esi, o->handle >> 16); SETW(r->edi, o->handle);
        break;
    }
    case 0x0600: case 0x0601:           /* lock, unlock linear region (pager.c) */
        if (!pager_lock((BX(r) << 16) | CX(r), (SI(r) << 16) | DI(r), fn == 0x0600))
            { set_cf(r, 1); SETW(r->eax, 0x8013); }
        break;
    case 0x0602: case 0x0603:           /* real-mode memory: never paged */
    case 0x0702: case 0x0703:
        break;
    case 0x0604:
        SETW(r->ebx, 0); SETW(r->ecx, 4096);
        break;
    case 0x0800: {                      /* map physical memory */
        /* Windows 3.1 (VX11 "map"): below 1 MB, and a range that wraps
         * past 4 GB, fail with carry, AX as it was; at or above 1 MB any
         * address is mapped (its own memory too); the same range again
         * gets the same address; size 0 is taken */
        u32 phys = (BX(r) << 16) | CX(r), size = (SI(r) << 16) | DI(r), lin, off, span;
        struct physmap *m;
        if (phys < 0x100000 || size > 0xFFFFFFFFu - phys) { set_cf(r, 1); break; }
        for (m = physmaps; m; m = m->next)
            if (phys >= m->phys && phys - m->phys <= m->size && size <= m->size - (phys - m->phys)) break;
        if (m) {
            lin = m->lin + (phys - m->phys);
            SETW(r->ebx, lin >> 16); SETW(r->ecx, lin);
            break;
        }
        span = (phys & 0xFFF) + size;
        span = span > 0xFFFFF000u ? 0 : (span + 4095) & ~0xFFFu;
        m = kmalloc(sizeof(*m));
        lin = m && (span || !size) ? lin_alloc(span + 4096) : 0;
        if (!lin) { kfree(m); set_cf(r, 1); break; }
        {
            int need_flush = 0, mr;
            for (off = 0; off < span; off += 4096) {
                if ((mr = map_page_noflush(lin + off, (phys & ~0xFFFu) + off, 7)) == 0) break;
                if (mr == 2) need_flush = 1;
            }
            if (off < span) {
                while (off) {
                    off -= 4096;
                    if (map_page_noflush(lin + off, 0, 0) == 2) need_flush = 1;
                }
                if (need_flush) vmm_flush_tlb();
                lin_free(lin, span + 4096);
                kfree(m);
                set_cf(r, 1);
                break;
            }
            if (need_flush) vmm_flush_tlb();
        }
        m->phys = phys & ~0xFFFu;
        m->size = span;
        m->lin = lin;
        m->next = physmaps;
        physmaps = m;
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
    case 0x0B00:                        /* set a debug watchpoint: a handle (BX) */
        /* Windows 3.1 (DPMIPRB) takes one; w16 hands out the handle and
         * never triggers it (UNVERIFIED that 3.1's do) */
        for (i = 0; i < 4 && (DP.wp_used & (1u << i)); i++)
            ;
        if (i == 4) { set_cf(r, 1); break; }
        DP.wp_used |= 1u << i;
        SETW(r->ebx, i);
        break;
    case 0x0B01:                        /* clear one */
    case 0x0B02:                        /* its state: not triggered (AX 0) */
    case 0x0B03:                        /* reset it */
        if (BX(r) >= 4 || !(DP.wp_used & (1u << BX(r)))) { set_cf(r, 1); break; }
        if (fn == 0x0B01) DP.wp_used &= ~(1u << BX(r));
        if (fn == 0x0B02) SETW(r->eax, 0);
        break;
    case 0x0E00:                        /* the coprocessor's status (DPMI 1.0) */
        /* Windows 3.1 in a DOS box (DPMIPRB, DOSBox's 387): AX 0034h, a
         * 387 present, no emulation */
        SETW(r->eax, 0x0034);
        break;
    case 0x0E01:                        /* set coprocessor emulation: taken */
        break;
    default:
        /* carry, AX kept (Windows 3.1 in a DOS box, DPMIPRB: 0401h) */
        trace("DPMI: unsupported INT 31h AX=%x\n", fn);
        set_cf(r, 1);
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
    case 0x1000:
        /* SHARE's installation check: "installed" (AL FFh). Observed on
           Windows 3.1 in 386 Enhanced mode (tests/apps/tk70.c, a Windows
           program in the System VM, DOSBox without SHARE): AL FFh with
           Microsoft's WIN386, 0 with w16's before and in Standard mode.
           Microsoft's OLE 2 STORAGE.DLL asks it before it makes a
           compound file and fails with STG_E_SHAREREQUIRED without
           (StarOffice 3.1: its documents). UNVERIFIED: what 3.1 answers
           in a DOS box (w16: passed on to DOS) */
        SETLO(r->eax, 0xFF);
        return 1;
    case 0x1680:                        /* release time slice: AL=0 */
    case 0x1689:                        /* KERNEL's idle loop (Pietrek ch. 6): */
        SETLO(r->eax, 0);               /* the System VM's time goes to the others */
        if (AX(r) == 0x1680) vm_release();
        else if (cur_vm == &sysvm) vm_kernel_idle();
        else vm_idle();
        return 1;
    case 0x1681:                        /* begin/end critical section */
    case 0x1682:
        vm_crit(AX(r) == 0x1681);
        return 1;
    case 0x1683:
        SETW(r->ebx, cur_vm->id);
        SETLO(r->eax, 0);
        return 1;
    case 0x4003:                        /* the display driver's critical sections: */
    case 0x4004:                        /* noted by the VDD, passed on */
        vdd_int2f(r);
        return 0;
    case 0x1686:                        /* in protected mode */
        SETW(r->eax, 0);
        return 1;
    case 0x1684: {                      /* PM API entry (vxdapi.c) */
        u32 sel, off;
        vxdapi_get(r, &sel, &off);
        if (cur_vm->client32) r->edi = off; else SETW(r->edi, off);
        r->es = sel;
        return 1;
    }
    case 0x168A: {                      /* vendor entry: "MS-DOS" only */
        const char *p = (const char *)cl_ptr(r->ds, r->esi, 7, 0);
        if (p && p[0] == 'M' && p[1] == 'S' && p[2] == '-' && p[3] == 'D' && p[4] == 'O'
            && p[5] == 'S' && p[6] == 0) {
            SETLO(r->eax, 0);
            r->es = SEL_PMBP;
            if (cur_vm->client32) r->edi = PMBP_VENDOR;
            else SETW(r->edi, PMBP_VENDOR);
            return 1;
        }
        return 0;
    }
    }
    return 0;
}

/* ---- interrupts in protected mode ---- */
static void run_hw_handler(int vec, struct cregs *intr, u32 ccs, u32 cip, u32 cds, u32 cflags);
static int pmvec_default(int vec)
{
    return cur_vm->pmvec_sel[vec] == SEL_PMBP && cur_vm->pmvec_off[vec] == (u32)vec;
}

/* for the VMM's services (vxd.c: Get_PM_Int_Vector, Simulate_*) */
int dpmi_pmvec_default(int vec)
{
    return pmvec_default(vec);
}

void dpmi_client_push(struct cregs *r, u32 v)
{
    pm_push(r, v);
}

u32 dpmi_client_pop(struct cregs *r)
{
    return pm_pop(r);
}

u32 pm_lin(u32 sel, u32 off)
{
    return sel_base(sel) + (cur_vm->client32 ? off : (off & 0xFFFF));
}

/* INT 21h 4Ch, and 00h: KRNL386 ends Windows (File > Exit Windows) with
 * INT 21h AX=0001h (observed in w16's trace; Pietrek, Windows Internals
 * ch. 1, ExitKernel: "AH = 4CH, AX = exitCode"), DOS's old terminate, with
 * its own PSP current; and each task with AH=00h, AL the exit code, the
 * task's PDB current (task_exit). In V86 mode AH=00h would take the PSP
 * from the caller's CS, which is no PSP here: both become 4Ch (VX9 under
 * Windows 3.1: a task's 002Ah reached DOS as 4C2Ah, KRNL386's own exit as
 * 4C00h). */
static int is_exit(struct cregs *r)
{
    return (AX(r) >> 8) == 0x00 || (AX(r) >> 8) == 0x4C;
}

/* A task's exit: KRNL386 ends each task with INT 21h AH=00h (AL the exit
 * code) while the task's PDB is DOS's current PSP, so that DOS closes its
 * handles and tells the network, then carries on; DOS gets 4Ch. Observed with VX9
 * (tests/dos/vx9.asm, tests/vx9run.sh) under Windows 3.1: KERNEL builds
 * the PDB with the parent field (16h) a selector, its own PSP's, and the
 * terminate address (0Ah) inside KRNL386's real-mode code, as protected
 * mode sees them; but when the call reaches DOS in V86 mode the parent is
 * KRNL386's PSP segment and the terminate address is the VMM's return
 * breakpoint, the same address as the reflected INT 21h's return. DOS's
 * terminate then makes KRNL386's PSP current and "returns" through that
 * breakpoint, and KERNEL goes on (INT 21h 50h calls follow). Here the two
 * fields are set for the call and put back after it, and the caller gets
 * its own registers back: DOS's terminate loads registers from the
 * parent's saved stack, nothing the caller asked for (UNVERIFIED what
 * Windows 3.1 hands back; KERNEL does not use them, Pietrek, Windows
 * Internals ch. 3, ExitCall). */
static int task_exit(struct cregs *r)
{
    u16 pdb = info->sda_lin ? *(u16 *)(info->sda_lin + 0x10) : 0;
    u8 *p;
    u16 old_par, old_off, old_seg;
    int par;
    struct cregs v, pm = *r;

    if (!pdb || pdb == cur_vm->psp_seg) return 0;
    p = (u8 *)((u32)pdb << 4);
    old_off = *(u16 *)(p + 0x0A);
    old_seg = *(u16 *)(p + 0x0C);
    old_par = *(u16 *)(p + 0x16);
    par = sel_to_seg(old_par);
    trace("DPMI: task exit (INT 21h AX=%x), PDB %x, parent %x (%x)\n", AX(r), pdb, old_par, par);
    if (par > 0) *(u16 *)(p + 0x16) = (u16)par;
    *(u16 *)(p + 0x0A) = (u16)(info->bp_off + W386_BP_REFRET);
    *(u16 *)(p + 0x0C) = info->ldr_cs;
    v = pm;
    SETW(v.eax, 0x4C00 | (AX(r) & 0xFF));
    v.ss = 0; v.esp = 0;
    v.ds = v.es = v.fs = v.gs = 0;
    v86_int(&v, 0x21);
    *(u16 *)(p + 0x0A) = old_off;
    *(u16 *)(p + 0x0C) = old_seg;
    *(u16 *)(p + 0x16) = old_par;
    *r = pm;
    set_cf(r, 0);
    return 1;
}

/* A DOS VM's client ends (INT 21h 4Ch from protected mode): what it had
 * goes back (its memory blocks, DOS blocks of the host's, callbacks,
 * descriptors, vectors; the environment segment in its PSP), an outer
 * client's state comes back if it was a nested one, and DOS ends the
 * program in V86 mode, at the state the switch left (DPMI 0.9: "To
 * terminate the program, execute an Int 21h with AH=4Ch"). Windows 3.1
 * in a DOS box (DPMIPRB run twice in a box): the second run gets the same
 * selectors and free memory as the first. */
static void client_end(struct cregs *r)
{
    struct vm *v = cur_vm;
    struct cregs n = v->v86save;
    struct dpmi_outer *o;
    if ((AX(r) >> 8) == 0x00)
        SETW(r->eax, 0x4C00);
    trace("DPMI: VM %d: client ends (INT 21h AX=%x, level %d)\n", v->id, AX(r), DP.level);
    if (v->psp_seg && env_seg) *(u16 *)(((u32)v->psp_seg << 4) + 0x2C) = env_seg;
    vxd_pm_app(0, r);
    mem_free_level(v, DP.level);
    rmcb_free_level(v, DP.level);
    if ((o = DP.outer) != NULL) {
        /* back to the outer client */
        memcpy(ldt, o->ldt, o->o_ldt_top);
        if (ldt_top > o->o_ldt_top) memset(ldt + o->o_ldt_top, 0, ldt_top - o->o_ldt_top);
        ldt_top = o->o_ldt_top;
        DP.ldt_first = o->ldt_first;
        v->v86save = o->v86save;
        v->psp_seg = o->psp_seg;
        env_seg = o->o_env_seg;
        DP.psp_sel = o->psp_sel;
        v->client32 = o->client32;
        v->dsel = o->dsel;
        DP.dta_sel = o->dta_sel; DP.dta_off = o->dta_off; DP.dta_set = o->dta_set;
        /* the vectors are the VM's, and back to the defaults: Windows 3.1
         * (DPMIPRB N) lost the outer client's INT 61h handler when the
         * inner one ended */
        vectors_default(v);
        DP.outer = o->next;
        dp_free(o->ldt, o->o_ldt_top);
        kfree(o);
    } else {
        memset(ldt, 0, ldt_top);
        segsels_drop(v);
        dosblks_drop(v);
        ldt_alias = 0;
        hstk_sel = 0;
        hstk_depth = 0;
        hstk_low = HSTK_TOP;
        DP.psp_sel = 0;
        DP.dta_set = 0;
        DP.wp_used = 0;
        vectors_default(v);
        v->pm = 0;
    }
    DP.level--;
    n.eax = r->eax;
    n.ebx = r->ebx;
    n.eflags = (n.eflags & ~(EFL_NT)) | EFL_VM | EFL_IOPL | EFL_IF;
    *r = n;
    v86_reflect(r, 0x21);
}

/* a DOS VM is destroyed: whatever its clients had (vm.c, before its page
 * table goes) */
void dpmi_vm_destroy(struct vm *v)
{
    struct dpmi_outer *o;
    mem_free_level(v, 0);
    rmcb_free_level(v, 0);
    dosblks_drop(v);
    segsels_drop(v);
    while ((o = v->dp.outer) != NULL) {
        v->dp.outer = o->next;
        dp_free(o->ldt, o->o_ldt_top);
        kfree(o);
    }
    if (v->dp.hstk_own) {
        /* its host stack's pages, in its own page table */
        u32 *pt = (u32 *)phys_to_lin(v->ptab_phys), i;
        for (i = 0; i < HSTK_SIZE; i += 4096) {
            u32 e = pt[(HSTK_LIN + i) >> 12];
            if (e & 1) page_free(e & ~0xFFFu);
            pt[(HSTK_LIN + i) >> 12] = 0;
        }
    }
    if (ldt == v->dp.ldt) dpmi_vm_in(&sysvm);
    dp_free(v->dp.ldt, 65536);
    kfree(v->dp.acb);
    dp_free(v->dp.rmcb, DPMI_NRMCB * sizeof(*v->dp.rmcb));
    memset(&v->dp, 0, sizeof(v->dp));
    v->pm = 0;
}

void dpmi_close(struct cregs *r)
{
    if (cur_vm == &sysvm || !cur_vm->pm || dpmi_in_handler()) return;
    trace("DPMI: VM %d closing: its client ends\n", cur_vm->id);
    SETW(r->eax, 0x4CFF);
    client_end(r);
}

static void terminate_client(struct cregs *r)
{
    struct cregs v = cur_vm->v86save, p;
    if (cur_vm != &sysvm) { client_end(r); return; }
    if (task_exit(r)) return;
    if ((AX(r) >> 8) == 0x00)
        SETW(r->eax, 0x4C00);           /* DOS's AH=00h returns code 0 */
    trace("DPMI: client ends (INT 21h AX=%x, PSP %x)\n", AX(r),
          info->sda_lin ? *(u16 *)(info->sda_lin + 0x10) : 0);
    /* the client's own PSP current again first: KERNEL makes its tasks'
     * PDBs DOS's current PSP (INT 21h 50h), and DOS would end that one
     * and return to its parent, a selector (as w16's DOSX does) */
    memset(&p, 0, sizeof(p));
    p.eax = 0x5000;
    p.ebx = cur_vm->psp_seg;
    v86_int(&p, 0x21);
    /* DOS gets its environment segment back (the switch had put a
     * selector there, DPMI 0.9) */
    if (cur_vm->psp_seg && env_seg) *(u16 *)(((u32)cur_vm->psp_seg << 4) + 0x2C) = env_seg;
    vxd_pm_app(0, r);
    cur_vm->pm = 0;
    v.eax = r->eax;
    v.ebx = r->ebx;
    v.eflags = (v.eflags & ~(EFL_NT)) | EFL_VM | EFL_IOPL | EFL_IF;
    *r = v;
    v86_reflect(r, 0x21);
}

/* INT 25h and 26h (absolute disk read/write) return with the flags still
 * on the stack: DOS's interface, which the caller's POPF expects (seen:
 * File Manager's INT 25h, then LAHF, POPF; without the word POPF took the
 * caller's BP and set TF). The V86 call leaves its word on the reflection
 * stack; the client gets one on its own. */
static void disk_flags(struct cregs *r)
{
    pm_push(r, r->eflags & 0xFFFF & ~EFL_IOPL);
}

void dpmi_int(struct cregs *r, int vec, int hw)
{
    /* INT 31h: the host's own unless a virtual device has hooked it
     * (Set_PM_Int_Vector; Visual C++ 1.52's DOSXNT.386 does at
     * Sys_Critical_Init), or the client has with 0205h: then the hook
     * first, which chains to the default (dpmi_pmbp) */
    if (!hw && vec == 0x31 && pmvec_default(vec)) { dpmi_call(r); return; }
    /* INT 2Fh: the client's handlers first, the host's functions (16xxh)
     * where they chain to the default (dpmi_pmbp). Windows 3.1 (VX17,
     * tests/kidlerun.sh, ARCHITECTURE.md 12.15): a program's handler of
     * the protected-mode INT 2Fh saw each of KERNEL's 1689h idle calls (as
     * WISPy in Schulman's Undocumented Windows ch. 4); w16 answered them
     * before any handler */
    if (!hw && vec == 0x2F && pmvec_default(vec) && int2f_pm(r)) return;
    if (hw && !pmvec_default(vec)) {
        /* on the host's stack, as for an interrupt in V86 mode (VX4) */
        run_hw_handler(vec, r, r->cs, r->eip, r->ds, r->eflags);
        return;
    }
    if (!pmvec_default(vec)) {
        /* the client's handler: an interrupt frame on its stack. A
         * software interrupt keeps the caller's IF (VX6 under Windows 3.1:
         * the handler and the frame both show the caller's flags; with
         * IOPL 0 the handler's IRET could not set IF again anyway) */
        pm_push(r, r->eflags);
        pm_push(r, r->cs);
        pm_push(r, r->eip);
        r->eflags &= ~EFL_TF;
        r->cs = cur_vm->pmvec_sel[vec];
        r->eip = cur_vm->pmvec_off[vec];
        return;
    }
    if (!hw && vec == 0x21 && is_exit(r)) { terminate_client(r); return; }
    if (!hw && vec == 0x21) { dos_pm(r); return; }
    if (!hw && vec == 0x10) { video_pm(r); return; }
    /* INT 41h: the protected-mode debugger interface (AX=004Fh asks
     * whether one is there); no debugger: nothing happens, as without
     * one under Windows 3.1 (UNVERIFIED that Windows does exactly this) */
    if (!hw && vec == 0x41) return;
    if (!hw && (vec == 0x25 || vec == 0x26)) { disk_pm(r, vec); disk_flags(r); return; }
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
        r->eflags = (pm_pop(r) & 0xFFFF & ~EFL_IOPL) | (r->eflags & 0xFFFF0000u);
        if (index == 0x21 && is_exit(r)) { terminate_client(r); return; }
        if (index == 0x31) { dpmi_call(r); return; }
        if (index == 0x2F && int2f_pm(r)) return;
        if (index == 0x41) return;
        if (index == 0x21) { dos_pm(r); return; }
        if (index == 0x10) { video_pm(r); return; }
        /* the hardware interrupts' vectors (IRQ 0-7: 08h-0Fh, IRQ 8-15:
         * 70h-77h) are reflected with the client's selectors left out:
         * DS and ES hold whatever the interrupted code had */
        if (index == 0x25 || index == 0x26) { disk_pm(r, index); disk_flags(r); return; }
        pm_v86_reflect(r, index, (index >= 8 && index < 16) || (index >= 0x70 && index < 0x78));
        return;
    }
    if (index == PMBP_EXCDEF) {
        /* the default exception handler (0202h's answer) chained to by a
         * client's handler: the DPMI frame is on the stack as the client's
         * handler got it (our return address first); the exception is
         * handled as if the client had no handler */
        u32 err, ip, cs, fl, sp, ss;
        pm_pop(r); pm_pop(r);           /* the return address to PMBP_EXCRET */
        err = pm_pop(r);
        ip = pm_pop(r); cs = pm_pop(r); fl = pm_pop(r); sp = pm_pop(r); ss = pm_pop(r);
        r->eip = ip; r->cs = cs; r->eflags = (fl & 0xFFFF & ~EFL_IOPL) | (r->eflags & 0xFFFF0000u);
        r->esp = sp; r->ss = ss;
        exc_default(r, exc_last, err);
        return;
    }
    if (index >= PMBP_CB && index < PMBP_CB_END) {
        /* a virtual device's callback (Allocate_PM_Call_Back): the
         * registers as they are here, CS:IP the callback's own address;
         * the device moves them on (Simulate_Iret, Simulate_Far_Ret, a
         * jump to the vector it hooked) */
        vxd_pm_callback(r, (index - PMBP_CB) / 2);
        return;
    }
    if (index >= PMBP_API && index < PMBP_API_END) {
        u32 ip = pm_pop(r), cs = pm_pop(r);
        vxdapi_call(r, index, ip, cs);
        return;
    }
    if (index == PMBP_IRQRET) {
        exec_return(r);
        return;
    }
    if (index == PMBP_SAVE) {           /* 0305h's routine: called far */
        r->eip = pm_pop(r);
        r->cs = pm_pop(r);
        return;
    }
    if (index == PMBP_RAW) {
        /* 0306h, protected to real mode (jumped to): AX DS, CX ES, DX SS,
         * BX SP, SI CS, DI IP; FS and GS 0; the interrupt flag kept */
        struct cregs n = *r;
        if (vm_trace) trace("[PM2RM %x:%x ss:sp %x:%x]", SI(r), DI(r), DX(r), BX(r));
        n.ds = AX(r); n.es = CX(r); n.ss = DX(r); n.esp = BX(r);
        n.cs = SI(r); n.eip = DI(r);
        n.fs = n.gs = 0;
        n.eflags = (r->eflags & 0x0FD5 & ~EFL_TF) | (r->eflags & EFL_IF) | 2 | EFL_VM | EFL_IOPL;
        *r = n;
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
        r->eip = ip; r->cs = cs; r->eflags = fl & 0xFFFF & ~EFL_IOPL; r->esp = sp; r->ss = ss;
        return;
    }
    panic("unknown PM breakpoint %x", index);
}

/* ---- interrupts from V86 mode for protected-mode handlers ----
 * A hardware interrupt (and INT 1Ch/23h/24h) that comes while V86 code
 * runs goes to the client's protected-mode handler if it installed one
 * (DPMI 0.9), on a stack of the host's; the handler's IRET reaches the
 * IRQRET breakpoint and the V86 code goes on. */
/* While a handler runs on the host stack (hstk_depth > 0) another
 * interrupt can come: in its own code (interrupts enabled), or in V86 code
 * it called (a DOS or BIOS call, a chained interrupt). The new frame must
 * go below everything the running handlers have pushed: below the SP
 * where it was interrupted when that was on the host stack, else below
 * the lowest SP any of them had at a trap (hstk_low: the PM code that is
 * waiting for V86 code trapped to get there). Before, each new frame went
 * at the SP its outer handler had on entry, over what that handler had
 * pushed since. UNVERIFIED how Windows 3.1 nests; VX4 saw only the
 * outermost entry (SP 0FF6h). */
/* the stack the innermost running protected-mode code was on at its last
 * trap (while hstk_depth > 0): for an interrupt that comes in V86 code it
 * called, the stack it left from (nest_stack) */

static u32 hstk_mask(void)
{
    return cur_vm->client32 ? 0xFFFFFFFFu : 0xFFFFu;
}

/* every trap from protected mode (vmm.c): note how deep the host stack is */
void dpmi_note_pm(struct cregs *r)
{
    if (!hstk_sel) return;
    if (hstk_depth) {
        pmw_ss = r->ss & 0xFFFF;
        pmw_sp = r->esp & hstk_mask();
    }
    if ((r->ss & 0xFFFF) == hstk_sel) {
        if ((r->esp & hstk_mask()) < hstk_low)
            hstk_low = r->esp & hstk_mask();
    } else if (!hstk_depth)
        hstk_low = HSTK_TOP;            /* nothing runs on the host stack now */
}

/* a hardware interrupt's handler (or a routine run on the host stack) is
 * running in the VM */
int dpmi_in_handler(void)
{
    return hstk_depth != 0;
}

/* the VM's host stack: its pages (once per VM: a DOS VM's page table
 * came from the System VM's, whose stack it must not share) and the
 * client's selector: an LDT one, or for a DOS VM's 32-bit client the
 * GDT's big SEL_HSTK32 (Windows 3.1 gives a 32-bit client's DS 87h,
 * where a 16-bit one's host stack is) */
static void hstk_make(void)
{
    u32 i, pg;
    if (hstk_sel) return;
    hstk_lin = HSTK_LIN;
    if (!DP.hstk_own) {
        for (i = 0; i < HSTK_SIZE; i += 4096) {
            pg = page_alloc();
            if (!pg || !map_page(hstk_lin + i, pg, 7)) panic("no memory for the host stack");
        }
        DP.hstk_own = 1;
    }
    if (cur_vm != &sysvm && cur_vm->client32) {
        hstk_sel = SEL_HSTK32;
        return;
    }
    hstk_sel = ldt_alloc(1);
    set_desc(ldt_desc(hstk_sel), hstk_lin, HSTK_SIZE - 1, 0xF2, cur_vm->client32 ? 0x4 : 0);
}

/* where a new frame on the host stack starts; intr: the interrupted state */
static u32 hstk_base(struct cregs *intr)
{
    hstk_make();
    if (intr && !(intr->eflags & EFL_VM) && (intr->ss & 0xFFFF) == hstk_sel
        && (intr->esp & hstk_mask()) < hstk_low)
        hstk_low = intr->esp & hstk_mask();
    if (hstk_low < 0x100) panic("the host stack overflows (%d handlers)", hstk_depth);
    return hstk_low;
}

/* run p (a frame on the host stack built from hstk_base, or a nested
 * handler's on the interrupted code's own stack) to its return */
static void hstk_run(struct cregs *p)
{
    u32 saved_low = hstk_low, saved_ss = pmw_ss, saved_sp = pmw_sp;
    hstk_depth++;
    if ((p->ss & 0xFFFF) == hstk_sel)
        hstk_low = p->esp & hstk_mask();
    pmw_ss = p->ss & 0xFFFF;
    pmw_sp = p->esp & hstk_mask();
    exec_v86(p);
    hstk_depth--;
    hstk_low = saved_low;
    pmw_ss = saved_ss;
    pmw_sp = saved_sp;
}

/* Only the outermost handler gets the host stack. While a handler runs
 * (hstk_depth > 0), an interrupt gets its frame on the protected-mode
 * stack the VM is on: the interrupted code's SS:SP when it came in
 * protected mode, else (in V86 code a handler called: a DOS or BIOS call,
 * a chain to the host's handler) the stack that code left protected mode
 * from, at its last trap. VPICD's Begin_Use_Locked_PM_Stack switches
 * stacks on its first call only and counts the others (Virtual Device
 * Adaptation Guide), and VX4's nested tick under Windows 3.1's WIN386
 * (kinds 3 and 4, tests/dos/vx4.asm) shows it: a handler that has moved
 * to a stack of its own and enabled interrupts gets the next tick's frame
 * on that stack, and its words on the host stack stay. On the host stack
 * itself the frame still goes below everything noted there (hstk_base).
 * Returns 0 for the host stack. Before (2026-11-22) every nested frame
 * went on the host stack below hstk_low, the lowest SP of a trap there;
 * CLI and STI do not note it (cli_sti_fast), so the frame overwrote what
 * a handler had pushed before it moved: w16's TIMER.DRV's interrupt
 * handler, on MMSYSTEM's stack for a QuickTime callback, came back to
 * garbage (a #GP in TIMER:1 at 0723; and, with only interrupts in
 * protected mode moved, at 010E: a tick chained to the BIOS from
 * MMSYSTEM's stack, and an IRQ in that BIOS code; docs/notes/
 * quicktime-handover.md). */
static int nest_stack(struct cregs *intr, u32 *ss, u32 *sp)
{
    u32 s, p;
    if (!hstk_depth || !intr) return 0;
    if (!(intr->eflags & EFL_VM)) {
        s = intr->ss & 0xFFFF;
        p = intr->esp & hstk_mask();
    } else {
        s = pmw_ss;
        p = pmw_sp;
    }
    if (!s || s == hstk_sel) return 0;
    *ss = s;
    *sp = p;
    return 1;
}

/* Run the client's protected-mode handler of hardware interrupt vec on
 * the host's stack, as Windows 3.1 does (VX4, ARCHITECTURE.md 12.6): the
 * frame is IP, CS, FLAGS of a return into the host (the IRQRET
 * breakpoint), then the client's own CS:IP where it was interrupted (or,
 * for an interrupt in V86 mode, where it last left protected mode); DS
 * and ES are the client's DS; interrupts off. Synchronous: returns when
 * the handler has returned. intr: the interrupted state. A nested one's
 * frame may go on another stack (nest_stack). */
static void run_hw_handler(int vec, struct cregs *intr, u32 ccs, u32 cip, u32 cds, u32 cflags)
{
    struct cregs p;

    memset(&p, 0, sizeof(p));
    hstk_make();
    if (!nest_stack(intr, &p.ss, &p.esp)) {
        p.esp = hstk_base(intr);
        p.ss = hstk_sel;
    }
    p.ds = p.es = cds;
    pm_push(&p, ccs);
    pm_push(&p, cip);
    pm_push(&p, (cflags & 0x0FD5) | EFL_IF | 2);
    pm_push(&p, SEL_PMBP);
    pm_push(&p, PMBP_IRQRET);
    p.eflags = (cflags & 0x0FD5 & ~(EFL_TF | EFL_IF)) | 2;
    p.cs = cur_vm->pmvec_sel[vec];
    p.eip = cur_vm->pmvec_off[vec];
    hstk_run(&p);
}

/* INT vec for the VMM's devices in the System VM (the VDD's INT 2Fh
 * 4001h/4002h to the display driver): the client's protected-mode handler
 * if it has one, on the host stack with the registers of r (interrupts
 * on, as a software interrupt from code with interrupts on), else the
 * V86 handler; the results back in r. UNVERIFIED: the stack and segment
 * registers Windows 3.1 gives the handler. */
void dpmi_pm_int(int vec, struct cregs *r)
{
    struct cregs p;
    if (cur_vm != &sysvm || !cur_vm->pm || pmvec_default(vec)) {
        r->ss = 0; r->esp = 0;
        v86_int(r, vec);
        return;
    }
    memset(&p, 0, sizeof(p));
    p.esp = hstk_base(NULL);
    p.ss = hstk_sel;
    p.ds = p.es = cur_vm->last_ds;
    pm_push(&p, 0x0202);
    pm_push(&p, SEL_PMBP);
    pm_push(&p, PMBP_IRQRET);
    p.eax = r->eax; p.ebx = r->ebx; p.ecx = r->ecx; p.edx = r->edx;
    p.esi = r->esi; p.edi = r->edi; p.ebp = r->ebp;
    p.eflags = 0x0202;
    p.cs = cur_vm->pmvec_sel[vec];
    p.eip = cur_vm->pmvec_off[vec];
    hstk_run(&p);
    r->eax = p.eax; r->ebx = p.ebx; r->ecx = p.ecx; r->edx = p.edx;
    r->esi = p.esi; r->edi = p.edi;
    r->eflags = (r->eflags & ~0x8D5u) | (p.eflags & 0x8D5u);
}

/* a far call (PASCAL: the callee pops the words) to sel:off in the System
 * VM's protected mode, on the host stack, with n words of arguments
 * (args[0] pushed first); the registers it returns with in r */
/* as dpmi_pm_farcall, the registers (and DS, ES) from in */
void dpmi_pm_farcall_regs(const struct cregs *in, u32 sel, u32 off, const u16 *args, int n, struct cregs *r)
{
    struct cregs p;
    int i;
    p = *in;
    p.esp = hstk_base(NULL);
    p.ss = hstk_sel;
    for (i = 0; i < n; i++) pm_push(&p, args[i]);
    pm_push(&p, SEL_PMBP);
    pm_push(&p, PMBP_IRQRET);
    p.eflags = 0x0202;
    p.cs = sel;
    p.eip = off;
    hstk_run(&p);
    if (r) *r = p;
}

void dpmi_pm_farcall(u32 sel, u32 off, const u16 *args, int n, struct cregs *r)
{
    struct cregs p;
    int i;
    memset(&p, 0, sizeof(p));
    p.esp = hstk_base(NULL);
    p.ss = hstk_sel;
    p.ds = p.es = cur_vm->last_ds;
    for (i = 0; i < n; i++) pm_push(&p, args[i]);
    pm_push(&p, SEL_PMBP);
    pm_push(&p, PMBP_IRQRET);
    p.eflags = 0x0202;
    p.cs = sel;
    p.eip = off;
    hstk_run(&p);
    if (r) *r = p;
}

/* A hardware interrupt (and INT 1Ch/23h/24h) that comes while V86 code
 * runs goes to the client's protected-mode handler if it installed one
 * (DPMI 0.9). */
/* an interrupt frame on the client's stack and CS:IP at sel:off */
void dpmi_build_int(struct cregs *r, u32 sel, u32 off)
{
    pm_push(r, r->eflags & ~EFL_IOPL);
    pm_push(r, r->cs);
    pm_push(r, r->eip);
    /* IF stays as it was: with IOPL 0 the routine's IRET cannot set it
     * again, and the VM would go on with interrupts off (UNVERIFIED what
     * Windows 3.1 does; KERNEL's Ctrl+Alt+Del procedure is short) */
    r->eflags &= ~EFL_TF;
    r->cs = sel;
    r->eip = off;
    /* on the host stack: the frame is in use from now on (a hardware
     * handler's frame must go below it) */
    dpmi_note_pm(r);
}

int dpmi_v86_hook(struct cregs *r, int vec, int hw)
{
    if (!cur_vm->pm || pmvec_default(vec)) return 0;
    /* INT 1Ch/23h/24h the same way. UNVERIFIED: what Windows 3.1 gives
     * their handlers (VX4 looked at INT 08h only), and whether the flags
     * they return (INT 24h's AL, INT 23h's CF) come back to V86 mode */
    (void)hw;
    run_hw_handler(vec, r, cur_vm->last_cs, cur_vm->last_ip, cur_vm->last_ds, r->eflags);
    return 1;
}

/* ---- the PS/2 pointing device handler (INT 15h AX=C207h) ----
 * The BIOS calls the handler set with C207h far, from its IRQ 12 handler
 * in V86 mode, with four words on the stack (pushed in this order: the
 * status, the X data, the Y data, 0; IBM PS/2 BIOS interface) and pops them
 * itself after the handler's RETF. A protected-mode client (MOUSE.DRV for
 * a PS/2 mouse) passes a protected-mode ES:BX, which no V86 code can call,
 * so the BIOS gets a V86 breakpoint instead, and the breakpoint calls the
 * client's handler on the host's stack with the same four words, as the
 * hardware interrupt handlers are run (run_hw_handler). Under Windows 3.1
 * this is how MOUSE.DRV gets the mouse in 386 Enhanced mode (observed: the
 * pointer moves; with the BIOS given the bare selector it did not).
 * UNVERIFIED: the exact registers and stack Windows 3.1 gives the handler. */
static u32 ps2_sel, ps2_off;

static int ps2_set_handler(struct cregs *r)
{
    struct cregs v, pm = *r;

    memset(&v, 0, sizeof(v));
    v.eax = r->eax;
    v.eflags = pm.eflags;
    if ((r->es & 0xFFFF) || BX(r)) {
        ps2_sel = r->es & 0xFFFF;
        ps2_off = BX(r);
        v.es = info->ldr_cs;
        v.ebx = info->bp_off + W386_BP_PS2CB;
    }
    if (dpmi_verbose) trace("[PS2 handler %x:%x]", r->es, BX(r));
    v86_int(&v, 0x15);
    *r = pm;
    SETW(r->eax, v.eax);
    r->eflags = (r->eflags & ~0x8D5u) | (v.eflags & 0x8D5u);
    if (!((r->es & 0xFFFF) || BX(r))) ps2_sel = ps2_off = 0;
    return 1;
}

void dpmi_ps2cb(struct cregs *r)
{
    u16 *sp = (u16 *)v86_lin(r->ss, r->esp);
    u32 ip = sp[0], cs = sp[1], z = sp[2], y = sp[3], x = sp[4], status = sp[5];
    struct cregs p;

    /* the RETF back to the BIOS */
    r->eip = ip;
    r->cs = cs;
    SETW(r->esp, (u16)r->esp + 4);
    if (!cur_vm->pm || !ps2_sel) return;
    memset(&p, 0, sizeof(p));
    hstk_make();
    if (!nest_stack(r, &p.ss, &p.esp)) {
        p.esp = hstk_base(r);
        p.ss = hstk_sel;
    }
    p.ds = p.es = cur_vm->last_ds;
    pm_push(&p, status);
    pm_push(&p, x);
    pm_push(&p, y);
    pm_push(&p, z);
    pm_push(&p, SEL_PMBP);
    pm_push(&p, PMBP_IRQRET);
    p.eflags = (r->eflags & 0x0FD5 & ~(EFL_TF | EFL_IF)) | 2;
    p.cs = ps2_sel;
    p.eip = ps2_off;
    hstk_run(&p);
}

/* ---- real-mode callbacks (0303h), raw switches (0306h) ---- */
static void rmcb_free_level(struct vm *v, u32 level)
{
    u32 i;
    if (!v->dp.rmcb) return;
    for (i = 0; i < DPMI_NRMCB; i++)
        if (v->dp.rmcb[i].level && v->dp.rmcb[i].level >= level) v->dp.rmcb[i].level = 0;
}

/* V86 code reached something it cannot go on from (a callback freed, a
 * raw switch to bad selectors): the VM ends, as for a fault */
static void rm_bad(struct cregs *r, const char *what)
{
    trace("DPMI: VM %d: %s at %x:%x\n", cur_vm->id, what, r->cs, r->eip);
    if (cur_vm != &sysvm) vm_crashed(r);
    else panic("DPMI: %s at %x:%x", what, r->cs, r->eip);
}

#pragma pack(1)
struct rmregs2 {            /* (struct rmregs is dpmi_simulate's, below) */
    u32 edi, esi, ebp, res, ebx, edx, ecx, eax;
    u16 flags, es, ds, fs, gs, ip, cs, sp, ss;
};
#pragma pack()

/* A callback reached in V86 mode (DPMI 0.9, 11.4): the real-mode
 * registers into the client's structure, then its procedure in protected
 * mode on the host's locked stack with interrupts off, DS:(E)SI the
 * real-mode SS:SP, ES:(E)DI the structure; its IRET comes back here, and
 * real mode goes on from the structure ES:(E)DI then points at. */
static void rmcb_call(struct cregs *r, u32 n)
{
    struct rmregs2 *s;
    struct cregs p;
    u32 rsel;

    if (n >= DPMI_NRMCB || !DP.rmcb || !DP.rmcb[n].level || !cur_vm->pm) { rm_bad(r, "a real-mode callback not allocated"); return; }
    s = (struct rmregs2 *)cl_ptr(DP.rmcb[n].ssel, DP.rmcb[n].soff, sizeof(*s), 1);
    if (!s) { rm_bad(r, "a real-mode callback's structure"); return; }
    s->edi = r->edi; s->esi = r->esi; s->ebp = r->ebp; s->ebx = r->ebx;
    s->edx = r->edx; s->ecx = r->ecx; s->eax = r->eax;
    s->flags = (u16)(r->eflags & ~EFL_IOPL);
    s->es = (u16)r->es; s->ds = (u16)r->ds; s->fs = (u16)r->fs; s->gs = (u16)r->gs;
    s->ip = (u16)r->eip; s->cs = (u16)r->cs; s->sp = (u16)r->esp; s->ss = (u16)r->ss;
    /* DS for this call only (a nested client EXECed after callbacks ran
     * got the same selectors as under Windows 3.1 so: DPMIPRB N) */
    if ((rsel = ldt_alloc(1)) == 0) { rm_bad(r, "no selector for a callback"); return; }
    set_desc(ldt_desc(rsel), (r->ss & 0xFFFF) << 4, 0xFFFF, 0xF2, 0);
    memset(&p, 0, sizeof(p));
    hstk_make();
    if (!nest_stack(r, &p.ss, &p.esp)) {
        p.esp = hstk_base(r);
        p.ss = hstk_sel;
    }
    p.eax = r->eax; p.ebx = r->ebx; p.ecx = r->ecx; p.edx = r->edx; p.ebp = r->ebp;
    p.ds = rsel;
    p.esi = r->esp & 0xFFFF;
    p.es = DP.rmcb[n].ssel;
    p.edi = DP.rmcb[n].soff;
    pm_push(&p, (r->eflags & 0x0FD5 & ~EFL_TF) | 2);
    pm_push(&p, SEL_PMBP);
    pm_push(&p, PMBP_IRQRET);
    p.eflags = (r->eflags & 0x0FD5 & ~(EFL_TF | EFL_IF)) | 2;
    p.cs = DP.rmcb[n].sel;
    p.eip = DP.rmcb[n].off;
    hstk_run(&p);
    ldt_free(rsel);
    s = (struct rmregs2 *)cl_ptr(p.es, p.edi, sizeof(*s), 0);
    if (!s) { rm_bad(r, "a real-mode callback's structure on return"); return; }
    r->edi = s->edi; r->esi = s->esi; r->ebp = s->ebp; r->ebx = s->ebx;
    r->edx = s->edx; r->ecx = s->ecx; r->eax = s->eax;
    r->es = s->es; r->ds = s->ds; r->fs = s->fs; r->gs = s->gs;
    r->eip = s->ip; r->cs = s->cs; r->esp = s->sp; r->ss = s->ss;
    r->eflags = (r->eflags & 0xFFFF0000u) | (s->flags & 0x0FD5) | 2 | EFL_VM | EFL_IOPL;
}

/* a selector a raw switch may load (0 for DS and ES) */
static int raw_sel_ok(u32 sel, int code, int null_ok)
{
    u8 *d;
    sel &= 0xFFFF;
    if (!(sel & ~3u)) return null_ok;
    if ((sel & 3) != 3 || !sel_present(sel) || sel == SEL_PMBP) return 0;
    d = (sel & 4) ? ldt_desc(sel) : gdt + (sel & ~7u);
    if (!(d[5] & 0x10)) return 0;
    return code ? (d[5] & 0x08) != 0 : (d[5] & 0x08) == 0 || (d[5] & 0x02);
}

/* the breakpoint W386_BP_DPMIRM: which entry by the segment */
void dpmi_rm_entry(struct cregs *r)
{
    u32 k = (u16)(info->ldr_cs - 1 - r->cs);
    struct cregs n;
    if (k == W386_DPMIRM_SAVE) {        /* called far: return */
        r->eip = v86_pop16(r);
        r->cs = v86_pop16(r);
        return;
    }
    if (vm_trace) trace("[RM%x %x:%x]", k, r->cs, r->eip);
    if (k != W386_DPMIRM_RAW) { rmcb_call(r, k - W386_DPMIRM_CB); return; }
    /* 0306h, real to protected mode (jumped to): AX DS, CX ES, DX SS,
     * (E)BX (E)SP, SI CS, (E)DI (E)IP; FS and GS 0 */
    if (!cur_vm->pm || !raw_sel_ok(SI(r), 1, 0) || !raw_sel_ok(DX(r), 0, 0)
        || !raw_sel_ok(AX(r), 0, 1) || !raw_sel_ok(CX(r), 0, 1)) { rm_bad(r, "a raw switch to bad selectors"); return; }
    n = *r;
    n.ds = AX(r); n.es = CX(r); n.ss = DX(r); n.cs = SI(r);
    n.esp = cur_vm->client32 ? r->ebx : BX(r);
    n.eip = cur_vm->client32 ? r->edi : DI(r);
    n.fs = n.gs = 0;
    n.eflags = r->eflags & ~(EFL_VM | EFL_IOPL | EFL_NT | EFL_TF);
    *r = n;
}

void dpmi_exception(struct cregs *r, int vec, u32 err)
{
    if (vec < 32 && cur_vm->excvec_sel[vec]) {
        /* the DPMI 0.9 exception frame: return address, error code, IP,
         * CS, FLAGS, SP, SS (words for a 16-bit client), on the host's
         * locked stack (DPMI 0.9: exception handlers are called on a
         * locked stack the host provides), never the faulting code's:
         * KRNL386's ExitCall raises exception 6 on purpose to run the end
         * of a task on that stack (Pietrek, Windows Internals ch. 3) and
         * frees the task's own stack there, then makes an INT 21h; on the
         * task's stack the frame and that call's words went to freed
         * memory (resumed at 0:0 after the first task exit) */
        struct cregs o = *r;
        u32 base = hstk_base(r);
        if (dpmi_verbose || (vec != 0x0B && vec != 6)) trace("[EXC %x err %x at %x:%x cr2 %x ds %x es %x fs %x gs %x ss:sp %x:%x ax %x bx %x nest %d]", vec, err, r->cs, r->eip, get_cr2(), r->ds, r->es, r->fs, r->gs, r->ss, r->esp, r->eax, r->ebx, exec_depth());
        r->ss = hstk_sel;
        r->esp = base;
        pm_push(r, o.ss);
        pm_push(r, o.esp);
        pm_push(r, o.eflags);
        pm_push(r, o.cs);
        pm_push(r, o.eip);
        pm_push(r, err);
        pm_push(r, SEL_PMBP);
        pm_push(r, PMBP_EXCRET);
        r->cs = cur_vm->excvec_sel[vec];
        r->eip = cur_vm->excvec_off[vec];
        r->eflags &= ~(EFL_IF | EFL_TF);
        hstk_low = r->esp & hstk_mask();
        exc_last = vec;
        return;
    }
    exc_default(r, vec, err);
}

/* an exception no client handler takes */
static void exc_default(struct cregs *r, int vec, u32 err)
{
    if (vec == 1 && pmvec_default(1)) {
        /* a single step nobody's handler takes: TF off, the client goes
         * on. Seen right after KERNEL's Ctrl+Alt+Del procedure returned
         * (run with Build_Int_Stack_Frame): TF in the flags it returned
         * with. Who takes the step under Windows 3.1 is UNVERIFIED (RBIL:
         * Reboot 0204h sets/resets a protected-mode INT 01h handler) */
        trace("[EXC 1 at %x:%x: TF off]", r->cs, r->eip);
        r->eflags &= ~EFL_TF;
        return;
    }
    if (vec < 8 && !pmvec_default(vec)) {
        /* no exception handler: exceptions 0-7 go to the client's
         * protected-mode interrupt handler (DPMI 0.9) */
        if (dpmi_verbose) trace("[EXC %x -> INT %x at %x:%x]", vec, vec, r->cs, r->eip);
        pm_push(r, r->eflags & ~EFL_IOPL);
        pm_push(r, r->cs);
        pm_push(r, r->eip);
        r->eflags &= ~(EFL_IF | EFL_TF);
        r->cs = cur_vm->pmvec_sel[vec];
        r->eip = cur_vm->pmvec_off[vec];
        return;
    }
    if (cur_vm != &sysvm) {
        /* Windows 3.1 in a DOS box (DPMIPRB G16, G32): a general protection
         * fault with no handler ends the VM with WINOA386's "This
         * application has violated system integrity due to an invalid
         * general protection fault and will be terminated" */
        trace("DPMI: VM %d: exception %x err %x at %x:%x, no handler\n", cur_vm->id, vec, err, r->cs, r->eip);
        vm_crashed(r);
        return;
    }
    {   /* the descriptor the error code names; never code bytes (ARCHITECTURE.md 3) */
        u8 *d = ldt_desc(err);
        trace("desc(err) %x %x %x %x %x %x %x %x\n", d[0], d[1], d[2], d[3], d[4], d[5], d[6], d[7]);
    }
    panic("client exception %x err %x at %x:%x (eax %x ebx %x ds %x es %x fs %x gs %x ss:esp %x:%x cr2 %x)",
          vec, err, r->cs, r->eip, r->eax, r->ebx, r->ds, r->es, r->fs, r->gs, r->ss, r->esp, get_cr2());
}
