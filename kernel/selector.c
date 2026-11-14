/*
 * selector.c - KERNEL's selector services: AllocSelector,
 * AllocSelectorArray, FreeSelector, Get/SetSelectorBase and Limit,
 * SelectorAccessRights, PrestoChangoSelector, the aliases
 * (AllocDStoCSAlias, AllocCStoDSAlias, AllocAlias), LongPtrAdd,
 * GlobalDOSAlloc/GlobalDOSFree and GlobalPageLock/Unlock. (GlobalFix,
 * GlobalWire and GlobalHandleNoRIP are the global heap's: global.c.)
 *
 * Descriptors are managed through DPMI 0.9 (dpmi.c). Windows 3.1's KERNEL
 * keeps its own free list in the LDT (Pietrek, "Windows Internals"); w16
 * allocates each selector from the DPMI host, which a DPMI client cannot
 * tell apart except by the selector values.
 *
 * Behaviour observed on Windows 3.1 with tests/apps/tk17.c, KRNL286 (which
 * w16 follows where it differs from KRNL386) and KRNL386 in Standard and
 * 386 Enhanced mode; ARCHITECTURE.md 9.2 has the full list:
 *  - AllocSelector(0) gives a selector the CPU does not accept (Windows
 *    marks it in use with a system type, 0Fh, at DPL 0) until it is given
 *    access rights. DPMI cannot make such a descriptor, so w16 gives a
 *    not-present data segment at our privilege level.
 *  - AllocSelector(sel) copies sel's descriptor into (limit >> 16) + 1
 *    selectors __AHINCR apart, each with base + 64K * i and the remaining
 *    limit (limit - 64K * i).
 *  - AllocSelectorArray(n) gives n present, writeable data selectors
 *    (F3h) with base 0 and (KRNL286) limit 0.
 *  - FreeSelector returns 0 and frees (limit >> 16) + 1 selectors of a
 *    valid selector, one of any other; the selector of a block frees the
 *    block.
 *  - Get/SetSelectorBase work on the LDT slot sel & ~7, whatever the TI
 *    and RPL bits; SetSelectorBase returns sel, SetSelectorLimit 0.
 *  - SelectorAccessRights(sel, 0, x): (descriptor byte 6 & D0h) << 8 |
 *    (byte 5 & 1Eh), 0 if LAR fails. With any other op (1, also 2) it
 *    sets byte 5's bits 1Eh and byte 6's D0h from x, keeping the present
 *    bit, DPL and the accessed bit; returns 0.
 *  - The aliases copy the descriptor and set (DStoCS) or clear (CStoDS,
 *    with the writeable bit set) the code bit; AllocDStoCSAlias also does
 *    GlobalFix on the block (a moveable block's GlobalFlags lock count
 *    stays 1 after the alias is freed). Aliases are not blocks:
 *    GlobalHandle(alias) = 0.
 *  - PrestoChangoSelector(src, dst) copies src into dst with the code bit
 *    toggled and returns dst (KRNL386: dst | 1); 0 when src is not valid.
 */
#include "kernel.h"

#define AR_PRESENT  0x80
#define AR_DPL      0x60
#define AR_SEG      0x10        /* code or data, not a system descriptor */
#define AR_CODE     0x08
#define AR_RW       0x02        /* data: writeable; code: readable */
#define AR_ACCESSED 0x01

/* ---- descriptor access ---- */

static u16 cpu386 = 0xFFFF;     /* 1 on a 386 or later (32-bit LSL/LAR) */

static int is386(void)
{
    if (cpu386 == 0xFFFF)
        cpu386 = dpmi_cpu_type() >= 3;
    return cpu386;
}

/* 32-bit LSL (386): limit in DX:AX, 0 if LSL fails */
extern u32 lsl32(u16 sel);
#pragma aux lsl32 = \
    0x66 0x33 0xC0              /* xor eax, eax */          \
    0x66 0x0F 0xB7 0xD3         /* movzx edx, bx */         \
    0x66 0x0F 0x03 0xC2         /* lsl eax, edx */          \
    0x66 0x8B 0xD0              /* mov edx, eax */          \
    0x66 0xC1 0xEA 0x10         /* shr edx, 16 */           \
    parm [bx] value [dx ax] modify exact [ax dx];

/* 16-bit LSL (286) */
extern u16 lsl16(u16 sel);
#pragma aux lsl16 = \
    "xor ax, ax"                                            \
    0x0F 0x03 0xC3              /* lsl ax, bx */            \
    parm [bx] value [ax] modify exact [ax];

/* 32-bit LAR (386): descriptor bytes 5 (low) and 6 (high, limit nibble
   cleared) or 0 if LAR fails */
extern u16 lar32(u16 sel);
#pragma aux lar32 = \
    0x66 0x33 0xC0              /* xor eax, eax */          \
    0x66 0x0F 0xB7 0xD3         /* movzx edx, bx */         \
    0x66 0x0F 0x02 0xC2         /* lar eax, edx */          \
    0x66 0x25 0x00 0xFF 0xF0 0x00   /* and eax, 00F0FF00h */\
    0x66 0xC1 0xE8 0x08         /* shr eax, 8 */            \
    parm [bx] value [ax] modify exact [ax dx];

/* 16-bit LAR (286): byte 5 << 8, 0 if LAR fails */
extern u16 lar16(u16 sel);
#pragma aux lar16 = \
    "xor ax, ax"                                            \
    0x0F 0x02 0xC3              /* lar ax, bx */            \
    parm [bx] value [ax] modify exact [ax];

/* The segment limit as LSL gives it (0 when LSL fails). */
static u32 seg_limit(u16 sel)
{
    return is386() ? lsl32(sel) : lsl16(sel);
}

/* The highest 16-bit offset sel reaches: its limit, FFFFh where the
   limit goes on past 64K (the first selector of a huge block on a 386:
   a 16-bit LSL gives only the low word, which made offsets in the first
   64K of a block over 64K look bad, KERNEL audit #6); 0 when LSL fails */
u16 sel_lim16(u16 sel)
{
    u32 l = seg_limit(sel);
    return l > 0xFFFFUL ? 0xFFFF : (u16)l;
}

/* LAR's view: access byte in the low byte, the flags nibble of byte 6
   (G, D, AVL) in bits 12-15; 0 when LAR fails. */
static u16 seg_rights(u16 sel)
{
    return is386() ? lar32(sel) : (lar16(sel) >> 8);
}

/* The LDT selector Windows uses for sel: Windows indexes its LDT with
   sel & ~7 (observed: GetSelectorBase(sel | 3) and (sel & ~7) agree). */
static u16 ldt_sel(u16 sel)
{
    return (sel & ~7u) | 4 | cpl;
}

typedef struct { u8 b[8]; } desc8;

static int get_desc(u16 sel, desc8 *d)
{
    return dpmi_get_ldt(ldt_sel(sel), d->b);
}

static int put_desc(u16 sel, const desc8 *d)
{
    return dpmi_set_ldt(ldt_sel(sel), d->b);
}

static u32 d_base(const desc8 *d)
{
    return d->b[2] | ((u32)d->b[3] << 8) | ((u32)d->b[4] << 16) | ((u32)d->b[7] << 24);
}

static void d_set_base(desc8 *d, u32 base)
{
    d->b[2] = (u8)base;
    d->b[3] = (u8)(base >> 8);
    d->b[4] = (u8)(base >> 16);
    d->b[7] = (u8)(base >> 24);
}

/* byte-granular limit; above 1 MB as page granular, as the CPU needs */
static void d_set_limit(desc8 *d, u32 lim)
{
    if (lim > 0xFFFFFUL) {
        lim >>= 12;
        d->b[6] |= 0x80;
    } else {
        d->b[6] &= 0x7F;
    }
    d->b[0] = (u8)lim;
    d->b[1] = (u8)(lim >> 8);
    d->b[6] = (d->b[6] & 0xF0) | (u8)((lim >> 16) & 0x0F);
}

/* Fill n selectors from `first` (AHINCR apart) from descriptor d: tile i
   gets base + 64K * i and limit - 64K * i. */
static int fill_tiles(u16 first, u16 n, const desc8 *d, u32 limit)
{
    desc8 t;
    u16 i;
    u32 base = d_base(d);
    for (i = 0; i < n; i++) {
        t = *d;
        d_set_base(&t, base + ((u32)i << 16));
        d_set_limit(&t, limit - ((u32)i << 16));
        if (!put_desc(first + i * sel_inc, &t))
            return 0;
    }
    return 1;
}

static void free_run(u16 first, u16 n)
{
    u16 i;
    for (i = 0; i < n; i++)
        dpmi_free_desc(first + i * sel_inc);
}

/* A fresh selector that is not usable yet (see the top of this file). */
static u16 raw_selector(void)
{
    u16 s = dpmi_alloc_desc(1);
    if (s)
        dpmi_set_rights(s, (cpl << 5) | AR_SEG | AR_RW);   /* not present */
    return s;
}

/* A copy of sel's descriptor in a new selector run; code: -1 keep, 0
   data (writeable), 1 code. */
static u16 copy_selector(u16 sel, int code)
{
    desc8 d;
    u32 lim;
    u16 n, s;
    if (!seg_rights(sel) || !get_desc(sel, &d))
        return 0;
    lim = seg_limit(sel);
    n = code < 0 ? (u16)(lim >> 16) + 1 : 1;
    if (code > 0)
        d.b[5] |= AR_CODE;
    else if (code == 0)
        d.b[5] = (d.b[5] & ~AR_CODE) | AR_RW;
    s = dpmi_alloc_desc(n);
    if (!s)
        return 0;
    if (!fill_tiles(s, n, &d, lim)) {
        free_run(s, n);
        return 0;
    }
    return s;
}

/* ---- API ---- */

u16 KAPI AllocSelector(u16 sel)                               /* KERNEL.175 */
{
    if (sel && seg_rights(sel))
        return copy_selector(sel | 1, -1);
    return raw_selector();
}

/* Observed: returns 0. The selector of a global block frees the block
   (GlobalSize gives 0 afterwards). Freeing a free selector also returns
   0, but corrupted Windows' free list (it handed the selector out twice);
   w16 leaves it to the DPMI host. */
static void es_zero(void);
#pragma aux es_zero = "push 0" "pop es" modify exact [es];

u16 KAPI FreeSelector(u16 sel)                                /* KERNEL.176 */
{
    u16 n = 1;
    if (!sel)
        return 0;
    sel |= 1;
    if ((u16)(GlobalHandle(sel) >> 16) == sel) {
        GlobalFree(sel);
    } else {
        if (seg_rights(sel) & AR_PRESENT)
            n = (u16)(seg_limit(sel) >> 16) + 1;
        free_run(sel, n);
    }
    /* observed (tests/apps/tk17.c): called with the selector in ES, 3.1
       returns ES = 0; w16 gave it back the dead selector (KERNEL audit
       #8). Its second selector of a huge block is freed too (3.1 the
       same: the block keeps its size, GlobalFree still succeeds) */
    es_zero();
    return 0;
}

u16 KAPI AllocSelectorArray(u16 n)                            /* KERNEL.206 */
{
    u16 s, i;
    if (!n)
        return 0;                       /* UNVERIFIED: not probed */
    s = dpmi_alloc_desc(n);
    if (!s)
        return 0;
    for (i = 0; i < n; i++) {
        if (!dpmi_set_desc(s + i * sel_inc, 0, 0, 0)) {
            free_run(s, n);
            return 0;
        }
    }
    return s;
}

u32 KAPI_ES GetSelectorBase(u16 sel)                             /* KERNEL.186 */
{
    desc8 d;
    return get_desc(sel, &d) ? d_base(&d) : 0;
}

u16 KAPI SetSelectorBase(u16 sel, u32 base)                   /* KERNEL.187 */
{
    dpmi_set_base(ldt_sel(sel), base);
    return sel;
}

/* KRNL286 reads the descriptor's limit (observed: a free selector gave
   its free-list word), KRNL386 uses LSL (0 when that fails). w16 reads the
   descriptor; the granularity bit counts. */
#ifdef KRNL386
extern u32 __cdecl k386_lsl(u16 sel);           /* k386.asm */

u32 KAPI GetSelectorLimit(u16 sel)                            /* KERNEL.188 */
{
    return sel ? k386_lsl(sel) : 0;
}
#else
u32 KAPI GetSelectorLimit(u16 sel)                            /* KERNEL.188 */
{
    desc8 d;
    u32 lim;
    if (!sel || !get_desc(sel, &d))
        return 0;
    lim = d.b[0] | ((u32)d.b[1] << 8) | ((u32)(d.b[6] & 0x0F) << 16);
    if (d.b[6] & 0x80)
        lim = (lim << 12) | 0xFFF;
    return lim;
}
#endif

/* Observed: SetSelectorLimit(sel, n) stores n's low 20 bits as a byte
   limit (FFFFFFFFh gives FFFFFh, 100000h gives 0), whatever is
   granularity; returns 0. */
u16 KAPI SetSelectorLimit(u16 sel, u32 limit)                 /* KERNEL.189 */
{
    desc8 d;
    if (get_desc(sel, &d)) {
        limit &= 0xFFFFFUL;
        d.b[0] = (u8)limit;
        d.b[1] = (u8)(limit >> 8);
        d.b[6] = (d.b[6] & 0xF0) | (u8)(limit >> 16);
        put_desc(sel, &d);
    }
    return 0;
}

u16 KAPI SelectorAccessRights(u16 sel, u16 op, u16 rights)    /* KERNEL.196 */
{
    desc8 d;
    u16 ar = seg_rights(sel);
    if (op == 0)
        return (ar & 0xD000) | (ar & 0x1E);
    if (get_desc(sel, &d)) {           /* any other op sets (observed: 2) */
        d.b[5] = (d.b[5] & (AR_PRESENT | AR_DPL | AR_ACCESSED)) | (u8)(rights & 0x1E);
        d.b[6] = (d.b[6] & 0x2F) | (u8)((rights >> 8) & 0xD0);
        put_desc(sel, &d);
    }
    return 0;
}

u16 KAPI PrestoChangoSelector(u16 src, u16 dst)               /* KERNEL.177 */
{
    desc8 d;
    if (!src || !seg_rights(src) || !get_desc(src, &d))
        return 0;
    d.b[5] ^= AR_CODE;
    if (!put_desc(dst, &d))
        return 0;
#ifdef KRNL386
    return dst | 1;                     /* observed (tk17.c) */
#else
    return dst;                         /* KRNL286; KRNL386: dst | 1 */
#endif
}

u16 KAPI AllocDStoCSAlias(u16 sel)                            /* KERNEL.171 */
{
    u16 a;
    if (!sel)
        return 0;
    if (!seg_rights(sel | 1))
        return raw_selector();          /* observed: not 0; UNVERIFIED what
                                           Windows copies from a free slot */
    a = copy_selector(sel | 1, 1);
    if (a)
        GlobalFix(sel);                 /* observed: a moveable block's
                                           lock count stays 1 */
    return a;
}

u16 KAPI AllocCStoDSAlias(u16 sel)                            /* KERNEL.170 */
{
    return sel ? copy_selector(sel | 1, 0) : 0;
}

/* Observed: the same as AllocCStoDSAlias, but at another address (the
   compiler would merge two identical bodies, hence the call). */
u16 KAPI AllocAlias(u16 sel)                                  /* KERNEL.172 */
{
    return AllocCStoDSAlias(sel);
}

/* Observed: the selector's base grows by add plus the offset's paragraph
   part (offset & FFF0h), and the result is sel:(offset & 0Fh). KRNL286
   returns 0 and changes nothing when add is 1 MB or more (FFFFFh worked,
   100000h did not; "negative" adds fail too); UNVERIFIED whether add or
   add plus the offset part is limited. (KRNL386 took up to FF0000h and
   failed for 7FFF0000h.) Only the one selector changes (UNVERIFIED: tiled
   selectors; the probe of one stopped Windows). */
u32 KAPI LongPtrAdd(u32 p, u32 add)                           /* KERNEL.180 */
{
    u16 sel = (u16)(p >> 16), off = (u16)p;
    u32 base = GetSelectorBase(sel), inc = add + (off & 0xFFF0u);
#ifdef KRNL386
    /* KRNL386: FF0000h worked, 7FFF0000h did not (tk17.c, TK17A: 1 MB
       steps up to E00000h); UNVERIFIED where between: 16 MB here */
    if (add > 0xFFFFFFUL || inc < add)
        return 0;
#else
    if (add > 0xFFFFFUL || inc < add)
        return 0;
#endif
    if (!dpmi_set_base(ldt_sel(sel), base + inc))
        return 0;
    return ((u32)sel << 16) | (off & 0x000F);
}

/* Observed: the handle is the selector itself (odd); the block is in
   conventional memory, base = paragraph << 4; GlobalSize rounds to 32;
   a block over 64K is tiled as GlobalAlloc's; the memory is not cleared
   (w16 clears it); GlobalDOSAlloc(0) and of 1 MB give 0. */
u32 KAPI GlobalDOSAlloc(u32 size)                             /* KERNEL.184 */
{
    u16 seg, sel;
    sel = galloc_dos(size, task_pdb(), &seg);
    return sel ? ((u32)seg << 16) | sel : 0;
}

/* Observed: returns 0 (also for 0). */
u16 KAPI GlobalDOSFree(u16 sel)                               /* KERNEL.185 */
{
    if (sel)
        GlobalFree(sel);
    return 0;
}

/* ---- page locks ----
   Observed with KRNL286 (tests/apps/tk17.c): GlobalPageLock adds a
   GlobalFix lock (GlobalFlags of a moveable block shows it) and returns
   1, for any block (0 for 0); GlobalPageUnlock takes it off and returns 0.
   (KRNL386 returns a page-lock count: 1, 2, then 1, 0.) DOSX does not
   page, so nothing is page-locked (UNVERIFIED: DPMI 0004h/0600h). */

u16 KAPI GlobalPageLock(u16 h)                                /* KERNEL.191 */
{
#ifdef KRNL386
    return h ? gpage_lock(h, 1) : 0;    /* global386.c */
#else
    if (!h || !GlobalHandle(h))
        return 0;
    GlobalFix(h);
    return 1;
#endif
}

u16 KAPI GlobalPageUnlock(u16 h)                              /* KERNEL.192 */
{
#ifdef KRNL386
    return h ? gpage_lock(h, 0) : 0;
#else
    if (h && GlobalHandle(h))
        GlobalUnfix(h);
    return 0;
#endif
}
