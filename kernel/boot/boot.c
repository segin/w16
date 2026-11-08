/*
 * boot.c - KRNL286.EXE bootstrap (real-mode MZ stub).
 *
 * 1. Find our own file name (the program path after the DOS environment).
 * 2. In real mode, read the NE header, the resident NE tables, every KERNEL
 *    segment and its relocation records into conventional memory.
 * 3. Switch to 16-bit protected mode through DPMI (INT 2Fh AX=1687h).
 * 4. Give every segment a descriptor, apply KERNEL's relocations, and call
 *    the KERNEL entry point (NE CS:IP) with ES:BX -> bootinfo.
 *
 * KERNEL must not import from other modules; it may only contain internal
 * and OS (floating-point) fixups.
 */
#include "w16/ne.h"
#include "regs.h"
#include "bootinfo.h"

#define MK_FP(s, o) ((void __far *)(((u32)(u16)(s) << 16) | (u16)(o)))

extern int __cdecl int21(regs16 *r);
extern int __cdecl int2f(regs16 *r);
extern int __cdecl int31(regs16 *r);
extern int __cdecl dpmi_switch(u16 entry_off, u16 entry_seg, u16 host_seg);
extern void __cdecl call_kernel(u16 cs, u16 ip, u16 bootinfo_off);
extern u16 __cdecl get_cs(void);
extern u16 __cdecl get_ds(void);
extern u16 __cdecl psp_seg;
extern u16 __cdecl psp_sel;

static regs16 r;
static bootinfo bi;
static ne_header h;
static u16 fh;
static u16 nepara;                      /* resident NE tables (real mode) */
static u16 rel_para[BOOT_MAXSEG];       /* relocation records, 0 = none */
static u16 rel_count[BOOT_MAXSEG];
static u16 scratch_sel, rel_sel;
static int in_pmode;

static u16 slen(const char *s)
{
    u16 n = 0;
    while (s[n])
        n++;
    return n;
}

static void say(const char *s)
{
    r.ax = 0x4000;
    r.bx = 1;
    r.cx = slen(s);
    r.dx = (u16)s;
    r.ds = r.es = get_ds();
    int21(&r);
}

/* INT 31h with null DS/ES: in protected mode, stale real-mode segment
   values in the register block would fault when loaded. */
static int i31(void)
{
    r.ds = r.es = 0;
    return int31(&r);
}

static void fail(const char *msg)
{
    say("w16 boot: ");
    say(msg);
    say("\r\n");
    r.ax = 0x4CFF;
    r.ds = r.es = get_ds();
    int21(&r);
}

/* ---- DOS file access (real mode) ---- */

static void dos_seek(u32 pos)
{
    r.ax = 0x4200;
    r.bx = fh;
    r.cx = (u16)(pos >> 16);
    r.dx = (u16)pos;
    if (int21(&r))
        fail("seek error");
}

/* Read n bytes (n <= 0xFFFF) to seg:off. */
static void dos_read(u16 seg, u16 off, u16 n)
{
    r.ax = 0x3F00;
    r.bx = fh;
    r.cx = n;
    r.dx = off;
    r.ds = seg;
    if (int21(&r) || r.ax != n)
        fail("read error");
}

/* Read up to 64K (len 0 means 64K) to seg:0. */
static void dos_read_big(u16 seg, u32 len)
{
    if (len > 0x8000UL) {
        dos_read(seg, 0, 0x8000);
        dos_read(seg + 0x800, 0, (u16)(len - 0x8000UL));
    } else if (len) {
        dos_read(seg, 0, (u16)len);
    }
}

static u16 dos_alloc(u32 bytes)
{
    u16 paras = (u16)((bytes + 15) >> 4);
    u16 i;
    r.ax = 0x4800;
    r.bx = paras ? paras : 1;
    if (int21(&r))
        fail("out of conventional memory");
    for (i = 0; i < paras; i++) {
        u16 __far *p = MK_FP(r.ax + i, 0);
        p[0] = p[1] = p[2] = p[3] = p[4] = p[5] = p[6] = p[7] = 0;
    }
    return r.ax;
}

static void find_own_path(void)
{
    u16 env = *(u16 __far *)MK_FP(psp_seg, 0x2C);
    const char __far *e = MK_FP(env, 0);
    u16 i = 0, n = 0;

    while (e[i] || e[i + 1])
        i++;
    i += 2;                             /* past the double NUL */
    if (*(const u16 __far *)(e + i) < 1)
        fail("no program path in environment (DOS 3+ required)");
    i += 2;                             /* past the string count */
    while (e[i] && n < BOOT_PATHMAX - 1)
        bi.path[n++] = e[i++];
    bi.path[n] = 0;
}

/* ---- Entry table lookup (for internal references to movable entries) ---- */

static int entry_lookup(const u8 __far *ne, u16 ordinal, u16 *segno, u16 *off)
{
    const u8 __far *p = ne + h.entry_off;
    const u8 __far *end = p + h.entry_len;
    u16 ord = 1;

    while (p < end && p[0]) {
        u8 count = p[0], type = p[1];
        p += 2;
        if (type == NEB_UNUSED) {
            ord += count;
            continue;
        }
        if (ordinal < ord)
            return 0;                   /* in a gap: not exported */
        if (ordinal < ord + count) {
            u16 k = ordinal - ord;
            if (type == NEB_MOVABLE) {
                const ne_entry_movable __far *m =
                    (const ne_entry_movable __far *)p + k;
                *segno = m->seg;
                *off = m->offset;
            } else if (type == NEB_CONSTANT) {
                return 0;
            } else {
                const ne_entry_fixed __far *f =
                    (const ne_entry_fixed __far *)p + k;
                *segno = type;
                *off = f->offset;
            }
            return 1;
        }
        p += (type == NEB_MOVABLE ? sizeof(ne_entry_movable)
                                  : sizeof(ne_entry_fixed)) * count;
        ord += count;
    }
    return 0;
}

/* ---- DPMI descriptor helpers (protected mode) ---- */

static void dpmi_fail(const char *what)
{
    say("DPMI error: ");
    fail(what);
}

static void set_desc(u16 sel, u32 base, u32 limit, int code)
{
    r.ax = 0x0007;
    r.bx = sel;
    r.cx = (u16)(base >> 16);
    r.dx = (u16)base;
    if (i31())
        dpmi_fail("set base");
    r.ax = 0x0008;
    r.bx = sel;
    r.cx = (u16)(limit >> 16);
    r.dx = (u16)limit;
    if (i31())
        dpmi_fail("set limit");
    r.ax = 0x0009;
    r.bx = sel;
    r.cx = (code ? 0x9A : 0x92) | (bi.cpl << 5);    /* present, R/W or X/R */
    if (i31())
        dpmi_fail("set access rights");
}

/* ---- Relocations ---- */

static void patch(u16 sel, const ne_reloc __far *rr, u16 tsel, u16 toff)
{
    u8 __far *p = MK_FP(sel, 0);
    u16 off = rr->offset;
    u8 src = rr->src & NERS_MASK;
    int add = (rr->flags & NERF_ADDITIVE) != 0;

    for (;;) {
        u16 __far *w = (u16 __far *)(p + off);
        u16 next = *w;
        switch (src) {
        case NERS_LOBYTE:
            p[off] = add ? (u8)(p[off] + toff) : (u8)toff;
            add = 1;                    /* no chain for byte fixups */
            break;
        case NERS_SEGMENT:
            *w = tsel;
            break;
        case NERS_OFFSET:
            *w = add ? *w + toff : toff;
            break;
        case NERS_FARPTR:
            w[0] = add ? w[0] + toff : toff;
            w[1] = tsel;
            break;
        default:
            fail("unsupported relocation type in KERNEL");
        }
        if (add || next == NE_CHAIN_END)
            break;
        off = next;
    }
}

static void relocate(u16 i)
{
    const u8 __far *ne = MK_FP(bi.ne_sel, 0);
    u16 k;

    set_desc(scratch_sel, (u32)bi.seg_para[i] << 4, bi.seg_size[i] - 1, 0);
    set_desc(rel_sel, (u32)rel_para[i] << 4, (u32)rel_count[i] * 8 - 1, 0);
    for (k = 0; k < rel_count[i]; k++) {
        const ne_reloc __far *rr = (const ne_reloc __far *)MK_FP(rel_sel, k * 8);
        u16 segno, toff;

        switch (rr->flags & NERF_TYPEMASK) {
        case NERF_INTERNAL:
            segno = rr->t1 & 0xFF;
            toff = rr->t2;
            if (segno == NE_MOVABLE_SEG && !entry_lookup(ne, rr->t2, &segno, &toff))
                fail("bad movable entry reference in KERNEL");
            if (segno < 1 || segno > bi.seg_count)
                fail("bad segment reference in KERNEL");
            patch(scratch_sel, rr, bi.seg_sel[segno - 1], toff);
            break;
        case NERF_OSFIXUP:
            break;                      /* KERNEL uses no floating point */
        default:
            fail("KERNEL must not import from other modules");
        }
    }
}

void __cdecl boot_main(void)
{
    u8 mz[64];
    u32 ne_base;
    u16 i, shift, desc, inc;
    u16 entry_off, entry_seg, host_paras, host_seg;

    bi.magic = BOOTINFO_MAGIC;
    bi.psp_seg = psp_seg;
    find_own_path();

    /* -- real mode: load everything -- */
    r.ax = 0x3D00;                      /* open, read-only */
    r.dx = (u16)bi.path;
    r.ds = get_ds();
    if (int21(&r))
        fail("cannot open KERNEL file");
    fh = r.ax;

    dos_read(get_ds(), (u16)mz, sizeof mz);
    if (*(u16 *)mz != MZ_MAGIC)
        fail("not an MZ file");
    ne_base = *(u32 *)(mz + MZ_LFANEW_OFF);
    dos_seek(ne_base);
    dos_read(get_ds(), (u16)&h, sizeof h);
    if (h.magic != NE_MAGIC)
        fail("no NE header");
    if (h.seg_count == 0 || h.seg_count > BOOT_MAXSEG)
        fail("too many KERNEL segments");
    if (h.cs == 0 || h.cs > h.seg_count)
        fail("bad KERNEL entry point");

    bi.ne_len = h.entry_off + h.entry_len;
    nepara = dos_alloc(bi.ne_len);
    dos_seek(ne_base);
    dos_read(nepara, 0, bi.ne_len);
    bi.seg_count = h.seg_count;
    shift = h.align_shift ? h.align_shift : 9;

    for (i = 0; i < h.seg_count; i++) {
        const ne_segment __far *s =
            (const ne_segment __far *)MK_FP(nepara, h.seg_off) + i;
        u32 size = s->min_alloc ? s->min_alloc : 0x10000UL;
        u32 flen = s->file_len ? s->file_len : 0x10000UL;

        if (s->sector == 0)
            flen = 0;
        if (flen > size)
            size = flen;
        if (i + 1 == h.auto_data) {
            size += (u32)h.heap_size + h.stack_size;
            if (size > 0x10000UL)
                size = 0x10000UL;
        }
        bi.seg_size[i] = size;
        bi.seg_para[i] = dos_alloc(size);
        if (flen) {
            dos_seek((u32)s->sector << shift);
            dos_read_big(bi.seg_para[i], flen);
        }
        if ((s->flags & NESEG_RELOCS) && flen) {
            u16 n;
            dos_seek(((u32)s->sector << shift) + flen);
            dos_read(get_ds(), (u16)&n, 2);
            if (n) {
                if (n > 0x1FFF)
                    fail("too many relocations");
                rel_count[i] = n;
                rel_para[i] = dos_alloc((u32)n * 8);
                dos_read(rel_para[i], 0, n * 8);
            }
        }
    }
    r.ax = 0x3E00;                      /* close */
    r.bx = fh;
    int21(&r);

    /* -- switch to protected mode -- */
    r.ax = 0x1687;
    int2f(&r);
    if (r.ax != 0)
        fail("no DPMI host found");
    entry_off = r.di;
    entry_seg = r.es;
    host_paras = r.si;
    host_seg = 0;
    if (host_paras) {
        r.ax = 0x4800;
        r.bx = host_paras;
        if (int21(&r))
            fail("no memory for DPMI host data");
        host_seg = r.ax;
    }
    if (dpmi_switch(entry_off, entry_seg, host_seg))
        fail("DPMI switch to protected mode failed");
    in_pmode = 1;
    bi.psp_sel = psp_sel;
    bi.cpl = get_cs() & 3;

    /* -- descriptors: one per segment, NE tables, two scratch -- */
    r.ax = 0x0000;
    r.cx = h.seg_count + 3;
    if (i31())
        dpmi_fail("allocate descriptors");
    desc = r.ax;
    r.ax = 0x0003;
    i31();
    inc = r.ax;

    for (i = 0; i < h.seg_count; i++)
        bi.seg_sel[i] = desc + i * inc;
    bi.ne_sel = desc + h.seg_count * inc;
    scratch_sel = bi.ne_sel + inc;
    rel_sel = scratch_sel + inc;
    set_desc(bi.ne_sel, (u32)nepara << 4, bi.ne_len - 1, 0);

    /* data descriptors first, so relocation works on writable segments */
    for (i = 0; i < h.seg_count; i++)
        set_desc(bi.seg_sel[i], (u32)bi.seg_para[i] << 4, bi.seg_size[i] - 1, 0);
    for (i = 0; i < h.seg_count; i++)
        if (rel_count[i])
            relocate(i);
    for (i = 0; i < h.seg_count; i++) {
        const ne_segment __far *s =
            (const ne_segment __far *)MK_FP(bi.ne_sel, h.seg_off) + i;
        if (!(s->flags & NESEG_DATA))
            set_desc(bi.seg_sel[i], (u32)bi.seg_para[i] << 4, bi.seg_size[i] - 1, 1);
    }
    r.ax = 0x0001;
    r.bx = rel_sel;
    i31();
    r.ax = 0x0001;
    r.bx = scratch_sel;
    i31();

    call_kernel(bi.seg_sel[h.cs - 1], h.ip, (u16)&bi);
    fail("KERNEL returned");
    (void)in_pmode;
}
