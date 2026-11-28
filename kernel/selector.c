/*
 * selector.c - KERNEL's selector services: AllocSelector,
 * AllocSelectorArray, FreeSelector, Get/SetSelectorBase and Limit,
 * SelectorAccessRights, PrestoChangoSelector, the aliases
 * (AllocDStoCSAlias, AllocCStoDSAlias, AllocAlias), LongPtrAdd,
 * GlobalDOSAlloc/GlobalDOSFree and GlobalPageLock/Unlock. (GlobalFix,
 * GlobalWire and GlobalHandleNoRIP are the global heap's: global.c.)
 *
 * Descriptors are managed through DPMI 0.9 (dpmi.c). Freed selectors go
 * where Windows 3.1 puts them (dpmi.c, TK61): back to the DPMI host at
 * once under KRNL286, onto KERNEL's own free list in the LDT under
 * KRNL386 (Pietrek, "Windows Internals", ch. 2).
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

/* kstr.asm's lstrcpy and the like have no stack to spare for a call:
   they read whether the 32-bit LSL can be used from a byte in their own
   code segment, written here once through a data descriptor over it. */
extern void __far str386(void);
#pragma aux str386 "_str386"

void str_init(void)
{
    void __far *p = (void __far *)str386;
    u16 a;
    if (!is386() || (a = dpmi_alloc_desc(1)) == 0)
        return;
    if (dpmi_set_desc(a, dpmi_get_base(FP_SEG(p)), (u32)FP_OFF(p), 0))
        *(u8 __far *)MK_FP(a, FP_OFF(p)) = 1;
    dpmi_free_desc(a);
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
#ifdef KRNL386
    /* observed (TK66 "sels"): KRNL386 keeps the other type bits, so a
       conforming code segment's data alias is expand-down (F6h), its code
       alias conforming (FEh) */
    if (code > 0)
        d.b[5] |= AR_CODE;
    else if (code == 0)
        d.b[5] = (d.b[5] & ~AR_CODE) | AR_RW;
#else
    /* observed (TK66 "sels"): KRNL286 gives a plain type, readable or
       writeable, accessed: a conforming code segment's aliases are F3h
       and FBh (KERNEL audit, medium 23; w16 kept the conforming bit) */
    if (code > 0)
        d.b[5] = (d.b[5] & 0xF0) | AR_CODE | AR_RW | AR_ACCESSED;
    else if (code == 0)
        d.b[5] = (d.b[5] & 0xF0) | AR_RW | AR_ACCESSED;
#endif
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
extern u16 __cdecl orc_n;       /* the alias oracle (below) */
static void orc_end(u16 sel);
static void es_zero(void);
#pragma aux es_zero = "push 0" "pop es" modify exact [es];

u16 KAPI FreeSelector(u16 sel)                                /* KERNEL.176 */
{
    u16 n = 1;
    if (!sel)
        return 0;
    /* (our privilege level, not just bit 0: a selector with another RPL
       missed its block and freed the descriptor under it; and a run of
       at most an LDT's worth: a 4 GB limit wrapped to 0. KERNEL audit,
       H18) */
    sel = (sel & ~3u) | cpl;
    if (orc_n)
        orc_end(sel);
    if ((u16)(GlobalHandle(sel) >> 16) == sel) {
        GlobalFree(sel);
    } else {
        if ((seg_rights(sel) & AR_PRESENT) && (seg_limit(sel) >> 16) < 0x2000UL)
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
#ifndef KRNL386
        /* a fresh AllocSelector(0) (w16's: not present, the AVL bit
           clear; a discarded block's has it set) becomes a usable
           segment under KRNL286 (TK62 "rights": readable once given
           data rights; KRNL386's stayed unusable: KERNEL audit, H16) */
        if (!(d.b[5] & AR_PRESENT) && !(d.b[6] & 0x10) && (rights & AR_SEG))
            d.b[5] |= AR_PRESENT;
#endif
        d.b[5] = (d.b[5] & (AR_PRESENT | AR_DPL | AR_ACCESSED)) | (u8)(rights & 0x1E);
        d.b[6] = (d.b[6] & 0x2F) | (u8)((rights >> 8) & 0xD0);
        put_desc(sel, &d);
    }
    return 0;
}

/* ---- the alias read-extent oracle ([w16] aliasoracle=1) ----
   An investigation aid for interoperability checks (ARCHITECTURE.md
   11.13), learnt at run time only: a data alias made of the code
   segment holding the procedure GetProcAddress last returned gets a limit
   just below that procedure's entry; kentry.asm's GP fault path raises the
   limit by a byte at each fault through the alias and restarts the
   instruction, so when the alias is freed its limit tells how far the
   check read (orc_ring: each fault's CS:IP). The entry's bytes before and
   after are logged too (whether the check wrote). */
extern u16 __cdecl orc_sel[8], orc_n, orc_ring[256 * 4], orc_pos;
extern void __far KLOADMODULE(void);
#pragma aux KLOADMODULE "*"
u16 orc_on;
u32 orc_proc;                   /* GetProcAddress's last result (module.c) */
static u32 orc_at[8];           /* the entry each alias was made for */

static void orc_line(const char *what, u16 a, u16 b, u16 c)
{
    kdbg_file("w16: oracle ");
    kdbg_file(what);
    kdbg_file(" ");
    kdbghex(a);
    kdbg_file(" ");
    kdbghex(b);
    kdbg_file(" ");
    kdbghex(c);
    kdbg_file("\r\n");
}

static void orc_bytes(const char *what, const u8 FAR *p)
{
    u16 i;
    kdbg_file("w16: oracle ");
    kdbg_file(what);
    for (i = 0; i < 16; i++) {
        kdbg_file(" ");
        kdbghex(p[i]);
    }
    kdbg_file("\r\n");
}

/* the module and segment number a selector belongs to, for the log */
static void orc_owner(u16 sel)
{
    module FAR *m;
    u16 i;
    for (m = module_first; m; m = m->next)
        for (i = 0; i < m->seg_count; i++)
            if (m->seg[i].sel && (m->seg[i].sel | 7) == (sel | 7)) {
                kdbg_file("w16: oracle  owner ");
                kdbg_file(m->name);
                kdbg_file(" seg ");
                kdbghex(i + 1);
                kdbg_file("\r\n");
                return;
            }
}

/* whether sel is one of KERNEL's code selectors (or the oracle's own
   over it): only w16's own code's bytes are ever logged, never another
   module's (the clean-room rule, ARCHITECTURE.md 3) */
extern u16 orc_lmsel;
static int orc_ours(u16 sel)
{
    module FAR *m;
    u16 i;
    if (orc_lmsel && (sel | 7) == (orc_lmsel | 7))
        return 1;
    for (m = module_first; m; m = m->next) {
        /* KERNEL, or a module whose description says it is w16's */
        const u8 FAR *d = m->nonres_sel ? (const u8 FAR *)MK_FP(m->nonres_sel, 0) : 0;
        if (m != module_first && !(d && d[0] > 4 && d[1] == 'w' && d[2] == '1' &&
                                   d[3] == '6' && d[4] == ' '))
            continue;
        for (i = 0; i < m->seg_count; i++)
            if (m->seg[i].sel && (m->seg[i].sel | 7) == (sel | 7))
                return 1;
    }
    return 0;
}

/* a watched selector: add it */
static void orc_add(u16 sel, u32 at)
{
    u16 i;
    for (i = 0; i < 8 && orc_sel[i]; i++)
        ;
    if (i == 8)
        return;
    orc_at[i] = at;
    orc_sel[i] = sel;
    orc_n++;
    orc_line("watch", sel, (u16)(at >> 16), (u16)at);
    if (orc_ours((u16)(at >> 16)))
        orc_bytes("before", (const u8 FAR *)at);
}

/* KERNEL.45 as GetProcAddress gives it with the oracle on: a code
   selector of its own over KERNEL's code, its limit below the entry, so
   that reads of the entry through it fault (module.c) */
u16 orc_lmsel;

static void orc_lm_init(void)
{
    void __far *p = (void __far *)KLOADMODULE;
    desc8 d;
    u16 a = dpmi_alloc_desc(1);
    if (!a)
        return;
    if (!get_desc(FP_SEG(p), &d)) {
        dpmi_free_desc(a);
        return;
    }
    d_set_limit(&d, (u32)FP_OFF(p) - 1);
    put_desc(a, &d);
    orc_lmsel = a;
    orc_add(a, (u32)p);
}

/* KERNEL's code segments and module table in 32-byte chunks, summed:
   phase 0 takes them, later phases log the chunks that changed (what
   a module patched in KERNEL) with their bytes now */
u16  KAPI GlobalAlloc(u16 flags, u32 size);
static u16 FAR *orc_sums;

void orc_snap(u16 phase)
{
    module FAR *m = module_first;
    u16 i, n = 0, ad = NE_HDR(m)->auto_data;
    if (!orc_on)
        return;
    if (!orc_sums) {
        u16 h = GlobalAlloc(0x0040, 9UL * 2048 * 2);
        if (!h)
            return;
        orc_sums = (u16 FAR *)MK_FP(h, 0);
    }
    for (i = 0; i <= m->seg_count && n < 9; i++, n++) {
        u16 sel = i < m->seg_count ? m->seg[i].sel : m->ne_sel, c;
        u16 lim;
        if (i + 1 == ad || !sel)
            continue;
        lim = sel_lim16(sel);
        for (c = 0; c < 2048 && (u32)c * 32 + 31 <= lim; c++) {
            const u8 FAR *p = (const u8 FAR *)MK_FP(sel, c * 32);
            u16 k, sum = 0;
            for (k = 0; k < 32; k++)
                sum = (u16)(sum * 31 + p[k]);
            if (phase && orc_sums[n * 2048 + c] != sum) {
                orc_line("changed", phase, sel, c * 32);
                orc_bytes("  now", p);
                orc_bytes("  now", p + 16);
            }
            orc_sums[n * 2048 + c] = sum;
        }
    }
}

/* a module table (w16's own data) before and after a hook ran: the
   bytes that changed go to the debuglog */
static u16 orc_mdb_copy;

void orc_mdb_snap(u16 sel)
{
    u16 n = sel_lim16(sel);
    if (!orc_on)
        return;
    if (!orc_mdb_copy && (orc_mdb_copy = GlobalAlloc(0, 65536UL - 16)) == 0)
        return;
    kmemcpy(MK_FP(orc_mdb_copy, 0), MK_FP(sel, 0), n > 0xFFE0U ? 0xFFE0U : n + 1);
}

void orc_mdb_diff(u16 sel)
{
    u16 n = sel_lim16(sel), i;
    const u8 FAR *a = (const u8 FAR *)MK_FP(orc_mdb_copy, 0);
    const u8 FAR *b = (const u8 FAR *)MK_FP(sel, 0);
    if (!orc_on || !orc_mdb_copy)
        return;
    if (n > 0xFFDF)
        n = 0xFFDF;
    {
        module FAR *m = module_first;
        int ours;
        while (m && m->ne_sel != sel)
            m = m->next;
        ours = m && m->seg_count && orc_ours(m->seg[0].sel);
        for (i = 0; i <= n; i++)       /* (another module's: where only) */
            if (a[i] != b[i])
                orc_line("mdb", i, ours ? a[i] : 0xFFFF, ours ? b[i] : 0xFFFF);
    }
}

/* [w16] aliasoracle: n bytes at a procedure of w16's own (never another
   module's) to the debuglog */
void orc_dump_ours(const char *what, u32 p, u16 n)
{
    u16 i;
    if (!orc_on || !p || !orc_ours((u16)(p >> 16)))
        return;
    kdbg_file("w16: oracle ");
    kdbg_file(what);
    for (i = 0; i < n; i++) {
        kdbg_file(i % 16 ? " " : "\r\n ");
        kdbghex(((const u8 FAR *)p)[i]);
    }
    kdbg_file("\r\n");
}

/* the boot is past the system driver: how far LoadModule's entry was
   read, and the selector made whole again for calls through it */
void orc_lm_done(void)
{
    desc8 d;
    if (!orc_lmsel)
        return;
    orc_end(orc_lmsel);
    if (get_desc(orc_lmsel, &d)) {
        d_set_limit(&d, 0xFFFFUL);
        put_desc(orc_lmsel, &d);
    }
}

static void orc_watch(u16 src, u16 dst)
{
    desc8 d;
    u16 off = FP_OFF(orc_proc);
    if (orc_on > 1) {
        orc_line("alias", src, dst, (u16)(orc_proc >> 16));
        orc_owner(src);
    }
    if (!orc_on || ((u16)(orc_proc >> 16) | 7) != (src | 7) || !off)
        return;
    if (!get_desc(dst, &d))
        return;
    d_set_limit(&d, (u32)off - 1);
    put_desc(dst, &d);
    orc_add(dst, orc_proc);
}

/* FreeSelector of a watched alias: how far it was read */
static void orc_end(u16 sel)
{
    u16 i, j;
    for (i = 0; i < 8; i++)
        if (orc_sel[i] && !((orc_sel[i] ^ sel) & ~3u))
            break;
    if (i == 8)
        return;
    orc_line("end", sel, (u16)seg_limit(sel), (u16)seg_limit(sel) - FP_OFF(orc_at[i]) + 1);
    for (j = 0; j < orc_pos; j++)
        if (!((orc_ring[j * 4] ^ sel) & ~3u))
            orc_line("fault", orc_ring[j * 4 + 1], orc_ring[j * 4 + 2], orc_ring[j * 4 + 3]);
    if (orc_ours((u16)(orc_at[i] >> 16)))
        orc_bytes("after", (const u8 FAR *)orc_at[i]);
    orc_sel[i] = 0;
    orc_n--;
}

/* [w16] aliasoracle: set up at boot (winboot.c) */
void orc_init(void)
{
    if (orc_on)
        orc_lm_init();
}

u16 KAPI PrestoChangoSelector(u16 src, u16 dst)               /* KERNEL.177 */
{
    desc8 d;
    if (!src || !seg_rights(src) || !get_desc(src, &d))
        return 0;
    d.b[5] ^= AR_CODE;
    if (!put_desc(dst, &d))
        return 0;
    orc_watch(src, dst);
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
    u16 a = sel ? copy_selector(sel | 1, 0) : 0;
    if (a)
        orc_watch(sel, a);
    return a;
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

static u16 gpagelock(u16 h)
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

u16 KAPI GlobalPageLock(u16 h)                                /* KERNEL.191 */
{
    u16 r = gpagelock(h);
    api_fs0(); api_es0();
    return r;
}

static u16 gpageunlock(u16 h)
{
#ifdef KRNL386
    return h ? gpage_lock(h, 0) : 0;
#else
    if (h && GlobalHandle(h))
        GlobalUnfix(h);
    return 0;
#endif
}

u16 KAPI GlobalPageUnlock(u16 h)                              /* KERNEL.192 */
{
    u16 r = gpageunlock(h);
    api_fs0(); api_es0();
    return r;
}
