/* w32s.c - w16's W32S, Win32s's device (002Dh): a replacement for Win32s
 * 1.30c's W32S.386 on w16's WIN386, a module of WIN386.EXE's W3 table
 * (ARCHITECTURE.md 12.20). The loader loads it for a SYSTEM.INI device=
 * line naming W32S.386 ([w16] W32SReplace=0: Microsoft's file instead).
 *
 * Win32s's 16-bit side (WIN32S16.DLL) and its 32-bit kernel (W32SKRNL.DLL,
 * through a 16-bit thunk) reach the device only through its PM API
 * (INT 2Fh AX=1684h BX=002Dh), the function in AX. What each function
 * takes and gives was observed under Windows 3.1 with Microsoft's
 * W32S.386 (VXY.386's API, page table, service and fault logs; black box:
 * registers, memory and the VMM-visible effects, never its code) and is
 * listed in ARCHITECTURE.md 12.20. This file answers that contract with
 * w16's own memory management:
 *   - Win32 memory lives in page tables of the device's own, in page
 *     directory entries past the VMM's window (the VMM's link, w16vxd.h),
 *     every committed page a physical page of the VMM's, present at once
 *     (Microsoft's maps them on first touch from pageable memory of the
 *     VMM's arena; w16's are not paged yet).
 *   - The flat selectors Win32 code runs with have base FFFF0000h (made
 *     by WIN32S16.DLL): a Win32 address is the linear one + 10000h, and
 *     the API's addresses are linear ones.
 *   - An image's sections are read from its file (DOS, nested execution
 *     in the System VM) when the client asks for them (function 02h);
 *     Microsoft's reads each page when it is first touched.
 * Functions not yet answered are logged (W32STrace) and fail with carry.
 */
#include "w16vxd.h"

#define NULL ((void *)0)

struct w32s_link w32s_link = { W32S_LINK_MAGIC };
#define L (&w32s_link)

void __cdecl vmm_begin_nest_exec(struct vregs *r);
void __cdecl vmm_exec_int(struct vregs *r);
void __cdecl vmm_end_nest_exec(struct vregs *r);
void __cdecl vmm_save_client_state(struct vregs *r);
void __cdecl vmm_restore_client_state(struct vregs *r);
void __cdecl vmm_hook_pm_fault(struct vregs *r);
void __cdecl vmm_get_profile_hex(struct vregs *r);
vu32 __cdecl vmm_heap_alloc(vu32 n, vu32 flags);
vu32 __cdecl vmm_heap_free(vu32 p, vu32 flags);
extern vu32 w32s_pf_prev[2];            /* w32sddb.asm */
extern vu32 w32s_pf_entry[2];

static void vclear(void *p, vu32 n)
{
    vu8 *d = p;
    while (n--) *d++ = 0;
}
static void vcopy(void *d, const void *s, vu32 n)
{
    vu8 *a = d;
    const vu8 *b = s;
    while (n--) *a++ = *b++;
}

/* ---- the trace ([w16] vmmtrace, [386Enh] W32STrace=1) ---- */
static vu32 trace_on;
static char tbuf[200];
static vu32 tlen;
static void tput(const char *s) { while (*s && tlen < sizeof tbuf - 2) tbuf[tlen++] = *s++; }
static void thex(vu32 v)
{
    int i;
    for (i = 28; i >= 0; i -= 4) if (tlen < sizeof tbuf - 2) tbuf[tlen++] = "0123456789ABCDEF"[(v >> i) & 15];
}
static void tend(void)
{
    tbuf[tlen++] = '\n';
    tbuf[tlen] = 0;
    if (trace_on) L->log(tbuf);
    tlen = 0;
}

/* ---- page tables ----
 * A page table entry of Win32s's memory: the 386's bits, and two of the
 * three the processor leaves to the system: W_RES the page belongs to a
 * reservation, W_COM it is committed (its frame is a physical page of
 * ours, also while not present: PAGE_NOACCESS). */
#define PAGE    4096u
#define PTE_P   0x001u
#define PTE_W   0x002u
#define PTE_U   0x004u
#define W_RES   0x200u
#define W_COM   0x400u
#define W_EXEC  0x800u          /* the protection was an EXECUTE one */
static vu8 owned_pde[1024];     /* the page directory entries we made */
static vu32 arena_pde;          /* where reservations without an address go */

static vu32 *pte_of(vu32 lin, int make)
{
    vu32 i = lin >> 22, pde = L->pdir_get(i), phys;
    if (!(pde & PTE_P)) {
        if (!make || i < 2) return NULL;
        phys = L->page_alloc();
        if (!phys) return NULL;
        L->pdir_set(i, phys | PTE_P | PTE_W | PTE_U);
        owned_pde[i] = 1;
        pde = L->pdir_get(i);
    } else if (!owned_pde[i])
        return NULL;                    /* the VMM's */
    return (vu32 *)L->phys_lin(pde & ~0xFFFu) + ((lin >> 12) & 0x3FF);
}

/* the PTE bits for a Win32 protection; *ok 0 for one not known */
static vu32 prot_bits(vu32 prot, int *ok)
{
    *ok = 1;
    switch (prot & 0xFF) {
    case 0x01: return 0;                                /* PAGE_NOACCESS */
    case 0x02: return PTE_P | PTE_U;                    /* PAGE_READONLY */
    case 0x04: return PTE_P | PTE_U | PTE_W;            /* PAGE_READWRITE */
    case 0x08: return PTE_P | PTE_U | PTE_W;            /* PAGE_WRITECOPY */
    case 0x10: return PTE_P | PTE_U | W_EXEC;           /* PAGE_EXECUTE */
    case 0x20: return PTE_P | PTE_U | W_EXEC;           /* PAGE_EXECUTE_READ */
    case 0x40: return PTE_P | PTE_U | PTE_W | W_EXEC;   /* PAGE_EXECUTE_READWRITE */
    case 0x80: return PTE_P | PTE_U | PTE_W | W_EXEC;   /* PAGE_EXECUTE_WRITECOPY */
    }
    *ok = 0;
    return 0;
}
/* the protection a PTE stands for */
static vu32 pte_prot(vu32 e)
{
    if (!(e & W_COM)) return 0;
    if (!(e & PTE_P)) return 0x01;
    if (e & W_EXEC) return e & PTE_W ? 0x40 : 0x20;
    return e & PTE_W ? 0x04 : 0x02;
}

/* ---- reservations ---- */
#define NREG 256
struct region { vu32 base, size, prot, sys; };
static struct region reg[NREG];
static vu32 nreg;

static struct region *region_of(vu32 lin)
{
    vu32 i;
    for (i = 0; i < nreg; i++)
        if (lin >= reg[i].base && lin - reg[i].base < reg[i].size) return &reg[i];
    return NULL;
}

/* is [lin, lin + n) free: no reservation, page tables ours or none */
static int range_free(vu32 lin, vu32 n)
{
    vu32 i, a;
    if (!n || lin + n < lin || lin < 0x400000u) return 0;
    for (i = 0; i < nreg; i++)
        if (lin < reg[i].base + reg[i].size && reg[i].base < lin + n) return 0;
    for (a = lin & ~0x3FFFFFu; a < lin + n; a += 0x400000u) {
        vu32 e = L->pdir_get(a >> 22);
        if ((e & PTE_P) && !owned_pde[a >> 22]) return 0;
        if (a + 0x400000u < a) break;
    }
    return 1;
}

/* a reservation of n bytes (64 KB granularity) at lin, or anywhere from
 * the arena (system ones from its upper part); 0 if none */
static vu32 reserve(vu32 lin, vu32 n, vu32 prot, vu32 sys)
{
    vu32 a, i, e;
    n = (n + 0xFFFFu) & ~0xFFFFu;
    if (!n || nreg == NREG) return 0;
    if (lin) {
        lin &= ~0xFFFFu;
        if (!range_free(lin, n)) return 0;
        a = lin;
    } else {
        a = (arena_pde << 22) + (sys ? 0x4000000u : 0);
        for (; ; a += 0x10000u) {
            if (a < (arena_pde << 22) || a >= 0xF0000000u) return 0;
            if (range_free(a, n)) break;
        }
    }
    for (i = 0; i < n; i += PAGE) {
        vu32 *p = pte_of(a + i, 1);
        if (!p) return 0;               /* (UNVERIFIED: no memory for a table) */
        *p = W_RES;
    }
    e = nreg++;
    reg[e].base = a;
    reg[e].size = n;
    reg[e].prot = prot;
    reg[e].sys = sys;
    return a;
}

/* commit [lin, lin + n) with prot: each page not committed gets a zeroed
 * physical page; committed ones keep theirs and get the protection. 0 if
 * a page is outside a reservation or memory ran out */
static int commit(vu32 lin, vu32 n, vu32 prot)
{
    vu32 a, end = (lin + n + PAGE - 1) & ~(PAGE - 1), bits;
    int ok;
    bits = prot_bits(prot, &ok);
    if (!ok) return 0;
    for (a = lin & ~(PAGE - 1); a < end; a += PAGE)
        if (!region_of(a)) return 0;
    for (a = lin & ~(PAGE - 1); a < end; a += PAGE) {
        vu32 *p = pte_of(a, 0), f;
        if (!p) return 0;
        if (*p & W_COM) f = *p & ~0xFFFu;
        else if (!(f = L->page_alloc())) return 0;
        *p = f | W_RES | W_COM | bits;
    }
    L->flush();
    return 1;
}

static void decommit(vu32 lin, vu32 n)
{
    vu32 a, end = (lin + n + PAGE - 1) & ~(PAGE - 1);
    for (a = lin & ~(PAGE - 1); a < end; a += PAGE) {
        vu32 *p = region_of(a) ? pte_of(a, 0) : NULL;
        if (!p) continue;
        if (*p & W_COM) L->page_free(*p & ~0xFFFu);
        *p = W_RES;
    }
    L->flush();
}

static int release(vu32 base)
{
    struct region *r = region_of(base);
    vu32 a;
    if (!r || r->base != base) return 0;
    decommit(r->base, r->size);
    for (a = r->base; a < r->base + r->size; a += PAGE) {
        vu32 *p = pte_of(a, 0);
        if (p) *p = 0;
    }
    *r = reg[--nreg];
    L->flush();
    return 1;
}

/* ---- VirtualAlloc (16-bit: function 1Dh; 32-bit: 06h) ----
 * flAllocationType: MEM_COMMIT 1000h, MEM_RESERVE 2000h, and 80000000h,
 * Win32s's own: the memory of the system (Microsoft's takes those pages
 * from the VMM's arena) */
static vu32 virtual_alloc(vu32 lin, vu32 n, vu32 type, vu32 prot)
{
    vu32 sys = (type & 0x80000000u) != 0, a;
    if (!n) return 0;
    if (type & 0x2000) {
        a = reserve(lin, n + (lin & 0xFFFFu), prot, sys);
        if (!a) return 0;
        if ((type & 0x1000) && !commit(lin ? lin : a, n, prot)) { release(a); return 0; }
        return lin ? lin & ~(PAGE - 1) : a;
    }
    if (type & 0x1000) {
        if (!lin) {
            a = reserve(0, n, prot, sys);
            if (!a) return 0;
            if (!commit(a, n, prot)) { release(a); return 0; }
            return a;
        }
        return commit(lin, n, prot) ? lin & ~(PAGE - 1) : 0;
    }
    return 0;
}

/* ---- module images (function 04h) and their sections (02h) ---- */
#define NIMG 32
struct image { vu32 base, size; char path[128]; };
static struct image img[NIMG];

static struct image *image_of(vu32 lin)
{
    vu32 i;
    for (i = 0; i < NIMG; i++)
        if (img[i].size && lin >= img[i].base && lin - img[i].base < img[i].size) return &img[i];
    return NULL;
}

/* ---- selectors: a selector's base from the GDT or the current LDT ---- */
static void sgdt_(void *p);
static vu32 sldt_(void);
#pragma aux sgdt_ = "sgdt [eax]" parm [eax] modify exact [];
#pragma aux sldt_ = "sldt ax" "movzx eax, ax" value [eax] modify exact [eax];
#pragma pack(1)
struct dtr { vu16 lim; vu32 base; };
#pragma pack()
static vu32 d_base(const vu32 *d) { return d[0] >> 16 | (d[1] & 0xFF) << 16 | (d[1] & 0xFF000000u); }

static vu32 sel_base(vu32 sel)
{
    struct dtr g;
    const vu32 *t;
    sgdt_(&g);
    t = (const vu32 *)g.base;
    if (sel & 4) {
        vu32 l = sldt_();
        t = (const vu32 *)d_base(t + (l >> 3 << 1));
    }
    return d_base(t + (sel >> 3 << 1));
}

/* ---- DOS from the device: nested execution in the System VM ----
 * The API call's client registers (cur) are the VM's: they are saved,
 * set for the call, and put back. DS and ES are given a GDT selector, so
 * that the VMM's Exec_Int hands V86 mode the transfer buffer's segment
 * (vxd.c); DX is an offset in it. */
static struct client_regs *cur;
static struct client_regs saved;
static vu32 cur_bx, cur_dx;             /* BX, DX of the last DOS call */

static vu32 dos(vu32 ax, vu32 bx, vu32 cx, vu32 dx, vu32 *out_ax)
{
    struct vregs v;
    vu32 cf;
    vclear(&v, sizeof v);
    v.edi = (vu32)&saved;
    vmm_save_client_state(&v);
    vclear(&v, sizeof v);
    vmm_begin_nest_exec(&v);
    cur->eax = ax;
    cur->ebx = bx;
    cur->ecx = cx;
    cur->edx = dx;
    cur->ds = cur->es = 0x10;
    cur->eflags &= ~VF_CF;
    vclear(&v, sizeof v);
    v.eax = 0x21;
    vmm_exec_int(&v);
    if (out_ax) *out_ax = cur->eax & 0xFFFF;
    cur_bx = cur->ebx & 0xFFFF;
    cur_dx = cur->edx & 0xFFFF;
    cf = cur->eflags & VF_CF;
    vclear(&v, sizeof v);
    vmm_end_nest_exec(&v);
    vclear(&v, sizeof v);
    v.esi = (vu32)&saved;
    vmm_restore_client_state(&v);
    return !cf;
}

/* bytes [off, off + n) of the open DOS handle h into linear address to */
static void handle_read(vu32 h, vu32 off, vu32 n, vu32 to)
{
    vu32 xb = L->xbuf_lin(), xs = L->xbuf_size(), chunk, got;
    if (xs < 0x400 || !dos(0x4200, h, off >> 16, off & 0xFFFF, NULL)) return;
    chunk = (xs - 0x200) & ~0x1FFu;
    if (chunk > 0x8000) chunk = 0x8000;
    while (n) {
        vu32 k = n < chunk ? n : chunk;
        if (!dos(0x3F00, h, k, 0x200, &got)) break;
        vcopy((void *)to, (const void *)(xb + 0x200), got);
        to += got;
        n -= got;
        if (got < k) break;
    }
}

/* file bytes [off, off + n) of path into linear address to: 1 if read
 * (a short file reads short; the rest stays as it was) */
static int file_read(const char *path, vu32 off, vu32 n, vu32 to)
{
    vu32 xb = L->xbuf_lin(), xs = L->xbuf_size(), h, got, chunk, i;
    if (!n) return 1;
    if (xs < 0x400) return 0;
    for (i = 0; path[i] && i < 0x1FF; i++) ((char *)xb)[i] = path[i];
    ((char *)xb)[i] = 0;
    if (!dos(0x3D40, 0, 0, 0, &h)) {    /* open: read, deny none */
        tput("W32S: cannot open ");
        tput(path);
        tend();
        return 0;
    }
    if (!dos(0x4200, h, off >> 16, off & 0xFFFF, NULL)) {
        dos(0x3E00, h, 0, 0, NULL);
        return 0;
    }
    chunk = (xs - 0x200) & ~0x1FFu;
    if (chunk > 0x8000) chunk = 0x8000;
    while (n) {
        vu32 k = n < chunk ? n : chunk;
        if (!dos(0x3F00, h, k, 0x200, &got)) break;
        vcopy((void *)to, (const void *)(xb + 0x200), got);
        to += got;
        n -= got;
        if (got < k) break;
    }
    dos(0x3E00, h, 0, 0, NULL);
    return 1;
}

/* ---- file mappings (functions 0Dh, 11h, 13h, 12h, 0Fh) ----
 * Windows 3.1's W32S.386 keeps a DOS handle of its own for a mapped file
 * (a duplicate, held in WIN32S16's DOS block as PSP) and reads a view's
 * pages from it when they are touched. w16's keeps the duplicate in the
 * PSP DOS has when the mapping is made (the program's), keyed by the
 * client's handle (ESI), and reads a view's bytes at once. */
#define NFILE 32
struct fobj { vu32 key, dos, size, view, vsize; };
static struct fobj fobj[NFILE];
static struct fobj *last_fobj;

static struct fobj *fobj_of(vu32 key)
{
    vu32 i;
    for (i = 0; i < NFILE; i++) if (fobj[i].key == key && fobj[i].dos) return &fobj[i];
    return NULL;
}

/* ---- the client's state ---- */
static vu32 k_entry, k_entry2, k_flatsel, k_proc, k_curtdb;  /* function 01h */
static vu32 dos_block;                  /* function 16h: WIN32S16's DOS block (seg:sel) */

static void trace_call(const char *what, const struct client_regs *c)
{
    if (!trace_on) return;
    tput("W32S ");
    tput(what);
    tput(" a "); thex(c->eax);
    tput(" b "); thex(c->ebx);
    tput(" c "); thex(c->ecx);
    tput(" d "); thex(c->edx);
    tput(" si "); thex(c->esi);
    tput(" di "); thex(c->edi);
    tput(" f "); thex(c->eflags);
    tend();
}

/* DOS's current PSP made the current task's (CurTDB through 01h's EDI,
 * the TDB's PDB at 60h, in conventional memory) for the client's DOS
 * handles, as it is on Windows 3.1 while the program runs (w16's KERNEL
 * sets it lazily, at the task's next INT 21h); the PSP before is
 * returned, to be put back. 0: not done */
static vu32 task_psp_in(void)
{
    vu32 a, tdb, pdb, base, old;
    if (!k_curtdb) return 0;
    a = sel_base(k_curtdb >> 16) + (k_curtdb & 0xFFFF);
    tdb = *(const vu16 *)a;
    if (!tdb) return 0;
    pdb = *(const vu16 *)(sel_base(tdb) + 0x60);
    base = sel_base(pdb);
    if (!pdb || base >= 0x100000 || (base & 15)) return 0;
    if (!dos(0x5100, 0, 0, 0, NULL)) return 0;
    old = cur_bx;
    dos(0x5000, base >> 4, 0, 0, NULL);
    return old;
}
static void task_psp_out(vu32 old)
{
    if (old) dos(0x5000, old, 0, 0, NULL);
}

#define CF_SET(c)   ((c)->eflags |= VF_CF)
#define CF_CLR(c)   ((c)->eflags &= ~VF_CF)
#define BLK(c)      ((vu32 *)(c)->edx)    /* a 32-bit call's block (linear) */
#define W32LIN(a)   ((a) - 0x10000u)      /* a Win32 address's linear one */

void __cdecl w32s_api(struct vregs *r)
{
    struct client_regs *c = (struct client_regs *)r->ebp;
    vu32 fn = c->eax & 0xFFFF, *b;
    cur = c;
    trace_call("in ", c);
    CF_CLR(c);
    switch (fn) {
    case 0x00:                          /* the version: 2.00 in AX's high word */
        L->ldt_user();                  /* (WIN32S16 reads the LDT next) */
        c->eax = 0x00C80000u;
        c->ebx = c->ecx = c->edx = 0;
        c->edi = 1;
        break;
    case 0x16:                          /* WIN32S16's DOS block: its size, then the block */
        if (!c->ebx) c->edx = 0x80;
        else dos_block = c->ebx;
        break;
    case 0x1A:                          /* the arena's base; EDX as Microsoft's */
        c->ecx = arena_pde << 22;
        c->edx = 0xFFFDFFFFu;
        break;
    case 0x1D:                          /* VirtualAlloc from 16-bit code: EDX SP:SS, a far
                                           pointer (offset high) to address, size, type,
                                           protection */
        b = (vu32 *)(sel_base(c->edx & 0xFFFF) + (c->edx >> 16));
        c->edx = virtual_alloc(b[0], b[1], b[2], b[3]);
        c->eax = c->edx ? 0 : 8;        /* (UNVERIFIED: the error code) */
        break;
    case 0x01:                          /* W32SKRNL's entries (all 0: it is gone) */
        k_entry = c->ebx;
        k_flatsel = c->ecx;
        k_entry2 = c->edx;
        k_proc = c->esi;
        k_curtdb = c->edi;
        c->eax = 0;
        break;
    case 0x06:                          /* VirtualAlloc: [1] address [2] size [3] type
                                           [4] protection, [5] the result */
        b = BLK(c);
        b[5] = virtual_alloc(b[1], b[2], b[3], b[4]);
        c->eax = 0;
        break;
    case 0x08: {                        /* VirtualProtect: [1] address [2] size [3] protection
                                           [4] where the old one goes (linear), [5] the result */
        vu32 a, end, *p, old = 0, bits;
        int ok;
        b = BLK(c);
        bits = prot_bits(b[3], &ok);
        end = (b[1] + b[2] + PAGE - 1) & ~(PAGE - 1);
        for (a = b[1] & ~(PAGE - 1); ok && a < end; a += PAGE) {
            p = region_of(a) ? pte_of(a, 0) : NULL;
            if (!p || !(*p & W_COM)) ok = 0;
        }
        if (ok) {
            for (a = b[1] & ~(PAGE - 1); a < end; a += PAGE) {
                p = pte_of(a, 0);
                if (a == (b[1] & ~(PAGE - 1))) old = pte_prot(*p);
                *p = (*p & ~(PTE_P | PTE_W | PTE_U | W_EXEC)) | bits;
            }
            L->flush();
            if (b[4]) *(vu32 *)b[4] = old;      /* (linear, as the block) */
        }
        b[5] = ok;
        c->eax = 0;
        break;
    }
    case 0x09: {                        /* VirtualQuery: [1] address [2] a MEMORY_BASIC_
                                           INFORMATION (linear) [3] its size; the result
                                           (bytes written) at [0]; Win32 addresses in it */
        vu32 a, *mbi, e, st, pr, run;
        struct region *g;
        b = BLK(c);
        a = b[1] & ~(PAGE - 1);
        mbi = (vu32 *)b[2];
        if (b[3] < 0x1C) { *(vu32 *)b[0] = 0; break; }
        g = region_of(a);
        if (!g) {
            /* free: up to the next reservation */
            vu32 i, next = 0xFFFF0000u;
            for (i = 0; i < nreg; i++)
                if (reg[i].base > a && reg[i].base < next) next = reg[i].base;
            mbi[0] = a + 0x10000u;
            mbi[1] = 0;
            mbi[2] = 0;
            mbi[3] = next - a;
            mbi[4] = 0x10000;           /* MEM_FREE */
            mbi[5] = 0x01;              /* PAGE_NOACCESS */
            mbi[6] = 0;
        } else {
            vu32 *p = pte_of(a, 0);
            e = p ? *p : 0;
            st = e & W_COM ? 0x1000 : 0x2000;
            pr = e & W_COM ? pte_prot(e) : 0;
            for (run = a; run < g->base + g->size; run += PAGE) {
                vu32 *q = pte_of(run, 0), f = q ? *q : 0;
                if ((f & W_COM ? 0x1000u : 0x2000u) != st || (f & W_COM ? pte_prot(f) : 0) != pr) break;
            }
            mbi[0] = a + 0x10000u;
            mbi[1] = g->base + 0x10000u;
            mbi[2] = g->prot;
            mbi[3] = run - a;
            mbi[4] = st;
            mbi[5] = pr;
            mbi[6] = image_of(a) ? 0x1000000u : 0x20000u;  /* MEM_IMAGE, MEM_PRIVATE */
        }
        *(vu32 *)b[0] = 0x1C;
        c->eax = 0;
        break;
    }
    case 0x07: {                        /* VirtualFree: [1] address [2] size [3] type
                                           (MEM_DECOMMIT 4000h, MEM_RELEASE 8000h); the
                                           result at [0] */
        vu32 ok = 0;
        b = BLK(c);
        if (b[3] & 0x8000) ok = release(b[1]);
        else if ((b[3] & 0x4000) && region_of(b[1])) { decommit(b[1], b[2]); ok = 1; }
        *(vu32 *)b[0] = ok;
        c->eax = 0;
        break;
    }
    case 0x05: {                        /* an image gone: EDX its linear base, EDI its number */
        struct image *m = image_of(c->edx);
        if (m) m->size = 0;
        c->eax = 1;
        break;
    }
    case 0x0D: {                        /* a file to map: ESI the client's handle for it,
                                           [1] the DOS handle; [0] -> 1 (made) */
        vu32 i, size = 0, dup = 0, ax, psp;
        b = BLK(c);
        for (i = 0; i < NFILE && fobj[i].dos; i++) ;
        psp = task_psp_in();
        if (i < NFILE && dos(0x4202, b[1], 0, 0, &ax)) {
            size = cur_dx << 16 | ax;
            dos(0x4200, b[1], 0, 0, NULL);
            if (dos(0x4500, b[1], 0, 0, &dup)) {
                fobj[i].key = c->esi;
                fobj[i].dos = dup;
                fobj[i].size = size;
                fobj[i].view = 0;
                last_fobj = &fobj[i];
            }
        }
        task_psp_out(psp);
        if (trace_on) {
            tput("W32S: file to map, DOS handle ");
            thex(b[1]);
            { vu32 k; tput(" S"); for (k = 0; k < 8; k++) { tput(" "); thex(((vu32 *)b[0])[k]); } }
            { vu32 k; tput(" B"); for (k = 2; k < 12; k++) { tput(" "); thex(b[k]); } }
            tput(" size ");
            thex(size);
            tput(" handle ");
            thex(dup);
            tend();
        }
        *(vu32 *)b[0] = dup != 0;
        c->eax = 0;
        break;
    }
    case 0x11: {                        /* a view of the file just made: [2] -> its address,
                                           [5] -> [2] its size, [3] its address (linear) */
        struct fobj *f = last_fobj;
        vu32 n, a = 0;
        b = BLK(c);
        if (f) {
            n = (f->size + PAGE - 1) & ~(PAGE - 1);
            if (!n) n = PAGE;
            a = virtual_alloc(0, n, 0x3000, 0x04);
            f->view = a;
            f->vsize = n;
        }
        *(vu32 *)b[2] = a;
        if (b[5]) { ((vu32 *)b[5])[2] = f ? f->vsize : 0; ((vu32 *)b[5])[3] = a; }
        c->eax = 0;
        break;
    }
    case 0x13: {                        /* the view's bytes from the file: ESI the client's
                                           handle, [14] the view (linear); [13] -> its size */
        struct fobj *f = fobj_of(c->esi);
        b = BLK(c);
        if (!f) f = last_fobj;
        if (f && f->view) {
            vu32 psp = task_psp_in();
            handle_read(f->dos, 0, f->size, f->view);
            task_psp_out(psp);
            b[13] = f->vsize;
        }
        c->eax = 0;
        break;
    }
    case 0x12: {                        /* a view gone: [2] its address (linear) */
        vu32 i;
        b = BLK(c);
        release(b[2]);
        for (i = 0; i < NFILE; i++) if (fobj[i].view == b[2]) fobj[i].view = 0;
        c->eax = 0;
        break;
    }
    case 0x0F: {                        /* a file mapping closed: [1] -> its client's handle
                                           (0 afterwards) */
        struct fobj *f;
        b = BLK(c);
        f = fobj_of(*(vu32 *)b[1]);
        if (f) {
            vu32 psp = task_psp_in();
            dos(0x3E00, f->dos, 0, 0, NULL);
            task_psp_out(psp);
            f->dos = 0;
            if (last_fobj == f) last_fobj = NULL;
        }
        *(vu32 *)b[1] = 0;
        c->eax = 0;
        break;
    }
    case 0x0B:                          /* (at a process's end: UNVERIFIED) */
    case 0x14:                          /* an exception's context (UNVERIFIED: nothing given yet) */
    case 0x15:
    case 0x1F:                          /* (once at a process's start) */
        c->eax = 0;
        break;
    case 0x0A:                          /* (no effect seen) */
        break;
    case 0x04: {                        /* an image: ESI its record (Win32: [1] the PE
                                           header, [3] the path, [5] the base, all Win32),
                                           EDI the PE header, ECX its number; EDX the
                                           process's record */
        vu32 i, j;
        const char *path;
        const vu32 *rec = (const vu32 *)W32LIN(c->esi);
        for (i = 0; i < NIMG && img[i].size; i++) ;
        if (i == NIMG) i = 0;
        img[i].base = W32LIN(rec[5]);
        img[i].size = *(const vu32 *)(W32LIN(c->edi) + 0x50);     /* SizeOfImage */
        path = (const char *)W32LIN(rec[3]);
        for (j = 0; j < sizeof img[i].path - 1 && path[j]; j++) img[i].path[j] = path[j];
        img[i].path[j] = 0;
        if (trace_on) {
            tput("W32S: image ");
            thex(img[i].base);
            tput(" size ");
            thex(img[i].size);
            tput(" ");
            tput(img[i].path);
            tend();
        }
        c->eax = i + 1;
        break;
    }
    case 0x02: {                        /* a section: EDX its linear address, ECX its virtual
                                           size, EDI its raw size, ESI its header (Win32) */
        struct image *m = image_of(c->edx);
        const vu32 *sh = (const vu32 *)W32LIN(c->esi);
        vu32 raw = c->edi;
        int why = !m ? 1 : !commit(c->edx, c->ecx > raw ? c->ecx : raw, 0x04) ? 2
                  : raw && !file_read(m->path, sh[5], raw, c->edx) ? 3 : 0;  /* PointerToRawData */
        if (why) {
            if (trace_on) {
                tput("W32S: section not loaded ");
                thex(why);
                tend();
            }
            c->eax = 1;
            CF_SET(c);
            break;
        }
        c->eax = 0;
        break;
    }
    default:
        trace_call("??? ", c);
        c->eax = 0;
        CF_SET(c);
        break;
    }
    trace_call("out", c);
}

/* faults: none taken yet (carry: on to the previous handler) */
/* Protected-mode faults (Hook_PM_Fault, w32sddb.asm's stubs: arg 0 the
 * page fault): a page fault on a reserved page of a reservation whose
 * next page up is committed commits it, with that page's protection, and
 * the client goes on: a thread's stack growing down (Windows 3.1's W32S.386
 * commits the reserved pages below a stack's committed top when they are
 * touched; a reservation with nothing committed faults: W32M). Anything
 * else goes on to the previous handler (carry). */
static vu32 get_cr2(void);
#pragma aux get_cr2 = "mov eax, cr2" value [eax] modify exact [eax];

void __cdecl w32s_fault(struct vregs *r)
{
    vu32 a = get_cr2() & ~(PAGE - 1), *p, *up;
    struct region *g;
    vu32 prot;
    r->eflags |= VF_CF;
    if (r->arg != 0) return;
    g = region_of(a);
    if (trace_on && g) {
        tput("W32S: fault at ");
        thex(get_cr2());
        tput(" eip ");
        thex(((struct client_regs *)r->ebp)->eip);
        tend();
    }
    if (!g) return;
    p = pte_of(a, 0);
    up = a + PAGE < g->base + g->size ? pte_of(a + PAGE, 0) : NULL;
    if (!p || (*p & W_COM)) return;
    if (up && (*up & W_COM))
        prot = pte_prot(*up) == 0x01 ? 0x04 : pte_prot(*up);    /* a stack's next page */
    else if ((g->prot & 0xFF) != 0x01)
        prot = g->prot;                 /* reserved with access: committed when touched */
    else
        return;
    if (!commit(a, PAGE, prot)) return;
    if (trace_on) {
        tput("W32S: stack page committed at ");
        thex(a);
        tend();
    }
    r->eflags &= ~VF_CF;
}

void __cdecl w32s_service(struct vregs *r)
{
    if (r->arg == 0) {                  /* (UNVERIFIED: Microsoft's services) the version */
        r->eax = 0x0200;
        r->eflags &= ~VF_CF;
    } else
        r->eflags |= VF_CF;
}

void __cdecl w32s_control(struct vregs *r)
{
    if (r->eax == DEVICE_INIT) {
        arena_pde = L->window_end_pde + 16;
        trace_on = L->trace != 0;
        tput("W32S: w16's device, arena at ");
        thex(arena_pde << 22);
        tend();
    }
    if (r->eax == INIT_COMPLETE) {
        struct vregs v;
        vclear(&v, sizeof v);
        v.eax = 0x0E;                   /* Hook_PM_Fault: page faults */
        v.esi = w32s_pf_entry[0];
        vmm_hook_pm_fault(&v);
        w32s_pf_prev[0] = (v.eflags & VF_CF) ? 0 : v.esi;
    }
    r->eflags &= ~VF_CF;
}

