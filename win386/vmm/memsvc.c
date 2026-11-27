/* memsvc.c - w16 VMM: the memory, paging and page-hooking services a
 * virtual device calls (ARCHITECTURE.md 12.19; the audit's findings,
 * service by service: docs/notes/win386-memsvc-audit.md).
 *
 * Sources: the Windows 3.1 DDK's Virtual Device Adaptation Guide (the
 * memory management services, Hook_V86_Page, the V86 data areas,
 * Map_Lin_To_VM_Addr; the flag values as the DDK's VMM.INC defines them
 * for a VxD's source); the Intel 80386 manual (page tables); Windows 3.1's
 * answers to the probe VxD VXH.386 (tests/vxd/vxh.c, tests/memsvc.sh),
 * black box: what the services give back and what the page tables then
 * hold, never its code.
 *
 * w16's VMM is laid out differently from Microsoft's (12.17): the VMM's
 * window at 80000000h maps all of its memory (linear = physical + a
 * constant), the clients' memory starts at 4 MB, and each VM's first
 * megabyte is its own page table, also mapped at its high linear address
 * (CB_High_Linear, C0000000h + ID x 4 MB). A device sees the DDK's
 * behaviour on that layout:
 *  - Blocks (_PageAllocate): pages of linear space of their own after the
 *    window (vxd.c's device-memory area), present at once when locked or
 *    fixed, else when first touched (a page fault gives a zeroed page),
 *    as under 3.1; PageUseAlign blocks are physically contiguous pages of
 *    the window. A handle is the VMM's record of the block. Blocks are
 *    never paged out (w16 pages the clients' memory only).
 *  - V86 pages: _MapIntoV86, _PhysIntoV86, _LinMapIntoV86 and
 *    _ModifyPageBits change the VM's page table (its high linear window
 *    with it); an entry they write keeps the DDK's page type in bits 9-11
 *    (PG_VM 0, PG_SYS 1, PG_HOOKED 7: what 3.1's entries hold there).
 *  - Hook_V86_Page: a page fault on a hooked page of the first megabyte
 *    (V86 code, a protected-mode client, the VMM or a device; through the
 *    VM's high linear address too) calls the device's procedure, which
 *    maps something there, and the access is made again.
 */
#include "vmm.h"

/* the DDK's flags (VMM.INC) */
#define PG_VM           0
#define PG_SYS          1
#define PG_HOOKED       7
#define PG_IGNORE       0xFFFFFFFFu
#define PageZeroInit    0x00000001u
#define PageUseAlign    0x00000002u
#define PageContig      0x00000004u
#define PageFixed       0x00000008u
#define PageZeroReInit  0x00000020u
#define PageNoCopy      0x00000040u
#define PageLocked      0x00000080u
#define PageLockedIfDP  0x00000100u
#define PageSetV86Pageable     0x00000200u
#define PageClearV86Pageable   0x00000400u
#define PageSetV86IntsLocked   0x00000800u
#define PageClearV86IntsLocked 0x00001000u
#define PageMarkPageOut 0x00002000u
#define PageDiscard     0x00010000u
#define PagePDPQueryDirty 0x00020000u
#define PageMapFreePhysReg 0x00040000u
#define P_PRES  1u
#define P_WRITE 2u
#define P_USER  4u
#define P_ACC   0x20u
#define P_DIRTY 0x40u
#define P_TYPE(t) (((t) & 7) << 9)
#define GVDAWordAlign   0x00000001u
#define GVDADWordAlign  0x00000002u
#define GVDAParaAlign   0x00000004u
#define GVDAPageAlign   0x00000008u
#define GVDAInstance    0x00000100u
#define GVDAZeroInit    0x00000200u
#define GVDAReclaim     0x00000400u
#define GVDAInquire     0x00000800u

#define V86_PAGES 0x110                 /* the V86 address space, with the HMA */

u32 vmm_lin_reserve(u32 n);             /* vxd.c: linear space after the window */
extern u8 *ldt_sys;                     /* vmm.c: the System VM's LDT */

/* ---- blocks ---- */
#define PGH_MAGIC 0x48475057u           /* "WPGH" */
#define H_FIXED     1u                  /* PageFixed: always locked */
#define H_WINDOW    2u                  /* PageUseAlign: contiguous pages of the window */
#define H_FREEPHYS  4u                  /* PageMapFreePhysReg: no pages of its own */
struct pgh {
    u32 magic;
    struct pgh *next;                   /* every live block */
    u32 lin, n, type, vm, flags;
    u8 *lk;                             /* a lock count per page */
};
static struct pgh *blocks;
static u32 sys_pages;                   /* PG_SYS pages allocated */

/* freed linear ranges for reuse (first fit) */
struct lfree { struct lfree *next; u32 lin, n; };
static struct lfree *lfrees;

static u32 lin_alloc(u32 n)
{
    struct lfree **pp, *f;
    u32 lin;
    for (pp = &lfrees; (f = *pp) != NULL; pp = &f->next)
        if (f->n >= n) {
            lin = f->lin;
            f->lin += n * 4096;
            f->n -= n;
            if (!f->n) { *pp = f->next; kfree(f); }
            return lin;
        }
    return vmm_lin_reserve(n);
}

static void lin_release(u32 lin, u32 n)
{
    struct lfree *f = kmalloc(sizeof(*f));
    if (!f) return;
    f->lin = lin;
    f->n = n;
    f->next = lfrees;
    lfrees = f;
}

static u32 *pte_at(u32 lin)
{
    u32 pde = vmm_pdir_entry_get(lin >> 22);
    if (!(pde & 1)) return NULL;
    return (u32 *)phys_to_lin(pde & ~0xFFFu) + ((lin >> 12) & 0x3FF);
}

static struct pgh *pgh_of(u32 h)
{
    struct pgh *p;
    for (p = blocks; p; p = p->next)
        if ((u32)p == h && p->magic == PGH_MAGIC) return p;
    return NULL;
}

static struct pgh *block_at(u32 lin)
{
    struct pgh *p;
    for (p = blocks; p; p = p->next)
        if (lin >= p->lin && lin - p->lin < p->n * 4096) return p;
    return NULL;
}

/* page i of the block present (a zeroed page if it was not); its
 * physical address, 0 if there is no memory */
static u32 block_page(struct pgh *h, u32 i)
{
    u32 *e, p;
    if (h->flags & H_WINDOW) return lin_to_phys(h->lin) + i * 4096;
    e = pte_at(h->lin + i * 4096);
    if (e && (*e & 1)) return *e & ~0xFFFu;
    if (h->flags & H_FREEPHYS) return 0;
    p = page_alloc();
    if (!p || !map_page(h->lin + i * 4096, p, P_PRES | P_WRITE | P_USER | P_TYPE(h->type))) {
        if (p) page_free(p);
        return 0;
    }
    return p;
}

/* a block's pages (not present: they come at first touch, demand_fault) */
static void block_drop_pages(struct pgh *h)
{
    u32 i, *e;
    if (h->flags & H_WINDOW) {
        u32 p = lin_to_phys(h->lin);
        for (i = 0; i < h->n; i++) page_free(p + i * 4096);
        return;
    }
    for (i = 0; i < h->n; i++) {
        e = pte_at(h->lin + i * 4096);
        if (!e) continue;
        if ((*e & 1) && !(h->flags & H_FREEPHYS)) page_free(*e & ~0xFFFu);
        *e = 0;
    }
    vmm_flush_tlb();
}

static void block_free(struct pgh *h)
{
    struct pgh **pp;
    for (pp = &blocks; *pp && *pp != h; pp = &(*pp)->next)
        ;
    if (*pp) *pp = h->next;
    block_drop_pages(h);
    if (!(h->flags & H_WINDOW)) lin_release(h->lin, h->n);
    if (h->type == PG_SYS && !(h->flags & H_FREEPHYS)) sys_pages -= h->n;
    h->magic = 0;
    kfree(h->lk);
    kfree(h);
}

/* is the MS-DOS pager there (PageLockedIfDP locks then) */
static int dos_pager(void)
{
    return pager_on() && info->swap_type == 2;
}

static struct pgh *block_new(u32 n, u32 type, u32 vm, u32 flags, u32 align, u32 minp, u32 maxp, u32 *physp)
{
    struct pgh *h;
    u32 i, lock;
    if (!n || n > 0x40000) return NULL;
    h = kmalloc(sizeof(*h));
    if (!h) return NULL;
    h->lk = kmalloc(n);
    if (!h->lk) { kfree(h); return NULL; }
    h->magic = PGH_MAGIC;
    h->n = n;
    h->type = type;
    h->vm = vm;
    h->flags = 0;
    lock = (flags & (PageLocked | PageFixed)) || ((flags & PageLockedIfDP) && dos_pager());
    if (flags & PageFixed) h->flags |= H_FIXED;
    if (flags & PageUseAlign) {
        u32 p = page_alloc_run(n, align, maxp);
        if (!p || p < minp) {
            if (p) for (i = 0; i < n; i++) page_free(p + i * 4096);
            kfree(h->lk); kfree(h);
            return NULL;
        }
        h->flags |= H_WINDOW;
        h->lin = phys_to_lin(p);
        if (physp) *physp = p;
    } else {
        h->lin = lin_alloc(n);
        if (!h->lin) { kfree(h->lk); kfree(h); return NULL; }
        if (flags & PageMapFreePhysReg) h->flags |= H_FREEPHYS;
        for (i = 0; i < n; i++) {
            /* the page table for it, the entry not present */
            /* (not present, writable, user and the type as 3.1's entries
             * for a page not touched yet, VXH) */
            if (!map_page_noflush(h->lin + i * 4096, 0x1000, 0)) break;
            *pte_at(h->lin + i * 4096) = P_WRITE | P_USER | P_TYPE(type);
        }
        if (i < n) { lin_release(h->lin, n); kfree(h->lk); kfree(h); return NULL; }
    }
    h->next = blocks;
    blocks = h;
    for (i = 0; i < n; i++) h->lk[i] = (u8)lock;
    if (lock && !(h->flags & (H_WINDOW | H_FREEPHYS)))
        for (i = 0; i < n; i++)
            if (!block_page(h, i)) { block_free(h); return NULL; }
    if (type == PG_SYS && !(h->flags & H_FREEPHYS)) sys_pages += n;
    return h;
}

/* a ring-0 page fault on a block's page not there yet: a zeroed page */
static int demand_fault(u32 lin)
{
    struct pgh *h = lin >= W386_VMM_LIN ? block_at(lin) : NULL;
    u32 *e;
    if (!h || (h->flags & (H_WINDOW | H_FREEPHYS))) return 0;
    e = pte_at(lin);
    if (e && (*e & 1)) return 0;
    return block_page(h, (lin - h->lin) >> 12) != 0;
}

/* the system nul page (_GetNulPageHandle): one page, mapped for every
 * page a device gives back. A block of its own that _PageFree refuses */
static struct pgh *nul_h;
static u32 nul_phys;

static struct pgh *nul_handle(void)
{
    if (!nul_h) {
        nul_h = block_new(1, PG_SYS, 0, PageFixed | PageLocked, 1, 0, 0, NULL);
        if (nul_h) nul_phys = block_page(nul_h, 0);
    }
    return nul_h;
}

static int vm_ok(u32 vm) { return vm_of(vm) != NULL; }

/* _PageAllocate(nPages, pType, VM, AlignMask, minPhys, maxPhys, PhysAddr,
 * flags) -> EAX handle, EDX ring-0 address; both 0 on error. Windows 3.1
 * (VXH): PG_VM and PG_HOOKED need a VM, PG_SYS none (else 0); another type
 * fails; PageUseAlign needs PageFixed; PageContig alone is ignored; a
 * free physical region (PageMapFreePhysReg) was refused at Device_Init */
static void page_allocate(u32 *a, struct svcregs *s)
{
    u32 n = a[0], type = a[1], vm = a[2], flags = a[7], align = 1, minp = 0, maxp = 0, phys = 0;
    struct pgh *h;
    s->eax = s->edx = 0;
    if (type == PG_SYS ? vm != 0 : (type != PG_VM && type != PG_HOOKED) || !vm_ok(vm)) return;
    if (flags & PageMapFreePhysReg) return;
    if (flags & PageUseAlign) {
        if (!(flags & PageFixed) || vmm_running()) return;
        align = a[3] + 1;
        minp = a[4] << 12;
        maxp = a[5] ? a[5] << 12 : 0;
    }
    h = block_new(n, type, vm, flags, align, minp, maxp, &phys);
    if (!h) return;
    if ((flags & PageUseAlign) && a[6]) *(u32 *)a[6] = phys;
    s->eax = (u32)h;
    s->edx = h->lin;
}

/* _PageReAllocate(hMem, nPages, flags): a new block of the old one's
 * kind, the old pages copied (not with PageNoCopy), new pages zeroed
 * (PageZeroInit; w16's always are), all of them with PageZeroReInit; the
 * old pages freed. 3.1 kept the handle (VXH) and reallocated a
 * PageUseAlign block too (the DDK says it fails): so here */
static void page_reallocate(u32 *a, struct svcregs *s)
{
    struct pgh *h = pgh_of(a[0]), *nh;
    u32 n = a[1], flags = a[2], i, keep, nf;
    s->eax = s->edx = 0;
    if (!h || !n || h == nul_h || (h->flags & H_FREEPHYS)) return;
    nf = (h->flags & H_FIXED) ? PageFixed : 0;
    for (i = 0; i < h->n; i++) if (h->lk[i]) nf |= PageLocked;
    nf |= flags & (PageLocked | PageLockedIfDP);
    nh = block_new(n, h->type, h->vm, nf, 1, 0, 0, NULL);
    if (!nh) return;
    keep = h->n < n ? h->n : n;
    if (!(flags & (PageNoCopy | PageZeroReInit)))
        for (i = 0; i < keep; i++) {
            u32 *e = (h->flags & H_WINDOW) ? NULL : pte_at(h->lin + i * 4096);
            if (e && !(*e & 1)) continue;           /* never touched: zeros */
            memcpy((void *)(nh->lin + i * 4096), (void *)(h->lin + i * 4096), 4096);
        }
    for (i = 0; i < keep; i++) nh->lk[i] = h->lk[i];
    /* h takes the new pages, nh the old ones, and nh goes */
    {
        u32 t;
        u8 *k;
        t = h->lin; h->lin = nh->lin; nh->lin = t;
        t = h->n; h->n = nh->n; nh->n = t;
        t = h->flags; h->flags = nh->flags; nh->flags = t;
        k = h->lk; h->lk = nh->lk; nh->lk = k;
    }
    block_free(nh);
    s->eax = (u32)h;
    s->edx = h->lin;
}

/* _PageLock / _PageUnLock (hMem, nPages, PageOff, flags): a count per
 * page; past the block's end fails. 3.1 (VXH) let an unlock of pages that
 * were not locked succeed, and a fixed block's lock and unlock both */
static u32 page_lock(u32 *a, int lock)
{
    struct pgh *h = pgh_of(a[0]);
    u32 n = a[1], off = a[2], flags = a[3], i;
    if (!h || off + n > h->n || off + n < off) return 0;
    if ((flags & PageLockedIfDP) && !dos_pager()) return 1;
    if (h->flags & (H_FIXED | H_WINDOW)) return 1;
    for (i = off; i < off + n; i++) {
        if (lock) {
            if (!(h->flags & H_FREEPHYS) && !block_page(h, i)) return 0;
            if (h->lk[i] < 255) h->lk[i]++;
        } else {
            if (h->lk[i]) h->lk[i]--;
            if (!h->lk[i] && (flags & PageMarkPageOut)) {
                u32 *e = pte_at(h->lin + i * 4096);
                if (e) *e &= ~P_ACC;
            }
        }
    }
    if (!lock) vmm_flush_tlb();
    return 1;
}

/* ---- V86 pages ---- */
static u32 *vm_ptab(struct vm *v)
{
    return (u32 *)phys_to_lin(v->ptab_phys);
}

/* Hook_V86_Page's procedures, by page. Pages w16's own devices map
 * themselves (the VDD's A0h-BFh, V86MMGR's EMS page frame) count as
 * hooked: under 3.1 the VDD has A0h and B0h-B7h hooked (VXH) */
static u32 hook_proc[V86_PAGES];

static int vmm_owns_page(u32 pg)
{
    if (pg >= 0xA0 && pg < 0xC0) return 1;
    if (pg >= 0x100) return 1;          /* the HMA: V86MMGR's (UNVERIFIED for 3.1) */
    if (info->ems_frame && pg >= ((u32)info->ems_frame >> 8) && pg < ((u32)info->ems_frame >> 8) + 16) return 1;
    return 0;
}

int memsvc_hooked(u32 page)
{
    return page < V86_PAGES && hook_proc[page] != 0;
}

/* the global assignments (_Assign_Device_V86_Pages with VM 0); a VM's own
 * are in struct vm. Windows 3.1 had A0h-10Fh assigned by the time the
 * last device's Device_Init came (VXH): here every page above the last
 * V86 page is, from the start. A new DOS VM's own array had every page
 * (0-10Fh) assigned at its VM_Init, and a device could take one back
 * (_DeAssign_Device_V86_Pages) and assign it again: vm.c starts it so */
static u8 g_assigned[36];
static int g_assigned_init;

static u8 *assigned_of(u32 vm)
{
    struct vm *v;
    if (!g_assigned_init) {
        u32 i;
        g_assigned_init = 1;
        for (i = vm_last_v86_page() + 1; i < V86_PAGES; i++) g_assigned[i >> 3] |= (u8)(1 << (i & 7));
    }
    if (!vm) return g_assigned;
    v = vm_of(vm);
    return v ? v->assigned : NULL;
}

#define BIT(a, i) ((a)[(i) >> 3] & (1 << ((i) & 7)))

static u32 assign(u32 *a, int on)
{
    u32 pg = a[0], n = a[1], vm = a[2], i;
    u8 *arr;
    struct vm *w;
    if (!n || pg >= V86_PAGES || n > V86_PAGES - pg) return 0;
    if ((vm || !on) && !vmm_running()) return 0;       /* local ones, and taking back: once initialised */
    arr = assigned_of(vm);
    if (!arr) return 0;
    for (i = pg; i < pg + n; i++) {
        if (on) {
            if (BIT(g_assigned, i)) return 0;
            if (vm && BIT(arr, i)) return 0;
            if (!vm) for (w = vm_list; w; w = w->next) if (BIT(w->assigned, i)) return 0;
        } else if (!BIT(arr, i)) return 0;
    }
    for (i = pg; i < pg + n; i++)
        if (on) arr[i >> 3] |= (u8)(1 << (i & 7));
        else arr[i >> 3] &= (u8)~(1 << (i & 7));
    return 1;
}

/* entries of v's page table for V86 pages pg .. pg+n: phys[i] (or the
 * pages from first on), present, writable, user, accessed and dirty
 * clear (the DDK), the type in bits 9-11 */
static void v86_map(struct vm *v, u32 pg, u32 n, u32 first, u32 type)
{
    u32 *pt = vm_ptab(v), i;
    for (i = 0; i < n; i++)
        pt[pg + i] = ((first + i * 4096) & ~0xFFFu) | P_PRES | P_WRITE | P_USER | P_TYPE(type);
    vmm_flush_tlb();
}

/* a V86 range the mapping services take: the DDK says 10h-10Fh, but 3.1
 * mapped page 0Fh too (VXH); not past 10Fh, not across the first V86
 * page */
static int v86_range_ok(u32 pg, u32 n)
{
    u32 first = vm_first_v86_page();
    if (!n || pg >= V86_PAGES || n > V86_PAGES - pg) return 0;
    if (pg < first && pg + n > first) return 0;
    return 1;
}

/* a page fault on a hooked V86 page: in VM `in`'s first megabyte at its
 * page pg (the faulting address in the current VM's first megabyte, or
 * in a VM's high linear window); the device's procedure is called (EAX
 * the page, EBX the VM, interrupts off) and maps the page; 1: the access
 * can be made again. A block's page not there yet (demand_fault) too */
static u32 hook_last_lin, hook_last_count;

/* entry e allows the access the fault's error code describes */
static int usable(u32 e, u32 err)
{
    return (e & P_PRES) && (!(err & 2) || (e & P_WRITE)) && (!(err & 4) || (e & P_USER));
}

int memsvc_page_fault(u32 lin, u32 err, struct vm *in)
{
    struct vm *v = NULL;
    u32 pg = 0, *pt, e;
    struct svcregs s;
    if (lin < V86_PAGES * 4096) { v = in ? in : cur_vm; pg = lin >> 12; }
    else if (lin >= 0xC0000000u) {
        struct vm *w;
        u32 id = (lin - 0xC0000000u) >> 22;
        for (w = vm_list; w && w->id != id; w = w->next)
            ;
        if (w && ((lin >> 12) & 0x3FF) < V86_PAGES) { v = w; pg = (lin >> 12) & 0x3FF; }
    } else if (lin >= W386_VMM_LIN)
        return demand_fault(lin);
    if (!v || !hook_proc[pg]) return 0;
    pt = vm_ptab(v);
    e = pt[pg];
    if (lin != hook_last_lin) { hook_last_lin = lin; hook_last_count = 0; }
    if (!usable(e, err)) {
        memset(&s, 0, sizeof(s));
        s.eax = pg;
        s.ebx = VMH(v);
        vmm_call_proc(hook_proc[pg], &s);
        e = pt[pg];
    }
    vmm_flush_tlb();
    /* a procedure that maps nothing: the fault again and again, and after
     * a few the VM ends (the DDK: map the page or end the VM) */
    if (usable(e, err)) hook_last_count = 0;
    else if (++hook_last_count > 4) { hook_last_lin = 0; return 0; }
    return 1;
}

/* the physical page of ring-0 linear page lpg for _LinMapIntoV86 (a
 * block's page brought in, a client's from the paging file): ~0 if none;
 * *type the page's DDK type */
static u32 lin_page_phys(u32 lpg, u32 *type)
{
    u32 lin = lpg << 12, e;
    struct pgh *h = lin >= W386_VMM_LIN ? block_at(lin) : NULL;
    *type = PG_SYS;
    if (h) {
        *type = h->type;
        e = block_page(h, (lin - h->lin) >> 12);
        return e ? e : ~0u;
    }
    if (lin >= 0x00400000u && lin < W386_VMM_LIN) {
        *type = PG_VM;
        if (!pager_present(lin)) return ~0u;
    }
    if (lin >= 0xC0000000u) *type = PG_VM;
    e = lin_to_phys_x(lin);
    return e == ~0u ? ~0u : e & ~0xFFFu;
}

/* _ModifyPageBits(VM, VMLinPgNum, nPages, bitAND, bitOR, pType, flags),
 * as Windows 3.1 answered VXH: only P_PRES, P_WRITE and P_USER may be
 * cleared or (but P_PRES) set, the type PG_HOOKED or PG_IGNORE, the pages
 * from the VM's first V86 page to 10Fh; accessed and dirty cleared */
static u32 modify_page_bits(u32 *a)
{
    struct vm *v = vm_of(a[0]);
    u32 pg = a[1], n = a[2], and = a[3], or = a[4], type = a[5], i, *pt;
    if (!v || !n || pg < vm_first_v86_page() || pg >= V86_PAGES || n > V86_PAGES - pg) return 0;
    if ((and | 7u) != ~0u || (or & ~6u)) return 0;
    if (type != PG_HOOKED && type != PG_IGNORE) return 0;
    pt = vm_ptab(v);
    for (i = pg; i < pg + n; i++) {
        u32 e = (pt[i] & and) | or;
        e &= ~(P_ACC | P_DIRTY);
        if (type == PG_HOOKED) e = (e & ~P_TYPE(7)) | P_TYPE(PG_HOOKED);
        pt[i] = e;
    }
    vmm_flush_tlb();
    return 1;
}

/* _TestGlobalV86Mem(VMLinAddr, nBytes, flags): 3.1 (VXH) called the
 * pages below the first V86 page global, instanced (3) where the range
 * holds instance data (the interrupt vectors, once Windows ran: not at
 * Device_Init), the VM's own pages local, every page above the last V86
 * page to 10Fh global, the rest local; a range of both mixed (2) */
static u32 test_global(u32 lin, u32 n)
{
    u32 first = vm_first_v86_page(), last = vm_last_v86_page(), pg, end, g = 0, l = 0;
    if (!n) n = 1;
    end = lin + n - 1;
    if (end < lin) return 0;
    for (pg = lin >> 12; pg <= end >> 12; pg++) {
        if (pg < first || (pg > last && pg < V86_PAGES)) g = 1; else l = 1;
        if (g && l) return 2;
    }
    if (g && vm_instance_range(lin, n)) return 3;
    return g ? 1 : 0;
}

/* _SelectorMapFlat(VM, Selector, flags): the descriptor's base; -1 for
 * the null selector, one past the table or not present */
static u32 selector_map_flat(u32 vm, u32 sel)
{
    u8 *d;
    sel &= 0xFFFF;
    if (!(sel & ~3u)) return ~0u;
    if (sel & 4) {
        struct vm *v = vm_of(vm);
        u8 *l;
        if (!v) return ~0u;
        l = v->dp.ldt ? v->dp.ldt : ldt_sys;
        if (sel > ldt_limit) return ~0u;
        d = l + (sel & ~7u);
    } else {
        if ((sel >> 3) >= GDT_ENTRIES) return ~0u;
        d = gdt + (sel & ~7u);
    }
    if (!(d[5] & 0x80)) return ~0u;
    return desc_base(d);
}

/* _CopyPageTable(LinPgNum, nPages, PageBuf, flags): pages 0-10Fh the
 * current VM's; 0 if a page directory entry on the way is not present */
static u32 copy_page_table(u32 pg, u32 n, u32 *buf)
{
    u32 i, *e;
    for (i = 0; i < n; i++) {
        if (pg + i > 0xFFFFF) return 0;
        e = pte_at((pg + i) << 12);
        if (!e) return 0;
        buf[i] = *e;
    }
    return 1;
}

/* the V86 pageable arrays (_SetResetV86Pageable): kept and given back;
 * w16 never pages a VM's first megabyte, so they change nothing else */
static u32 set_reset_pageable(u32 *a)
{
    struct vm *v = vm_of(a[0]);
    u32 pg = a[1], n = a[2], f = a[3], i;
    if (!v) return 0;
    if (f & (PageSetV86IntsLocked | PageClearV86IntsLocked)) return 1;
    if (!(f & (PageSetV86Pageable | PageClearV86Pageable))) return 0;
    if (pg < vm_first_v86_page() || pg > 0x100 || n > 0x100 - pg) return 0;
    for (i = pg; i < pg + n && i < 0x100; i++)
        if (f & PageSetV86Pageable) v->pageable[i >> 3] |= (u8)(1 << (i & 7));
        else v->pageable[i >> 3] &= (u8)~(1 << (i & 7));
    return 1;
}

/* _PageDiscardPages(LinPgNum, VM, nPages, flags): the clients' pageable
 * pages lose their accessed bit (and dirty with PageDiscard); w16 writes
 * every page it evicts, so a discarded one still comes back as it was
 * (UNVERIFIED that any device relies on its contents going) */
static u32 discard_pages(u32 *a)
{
    u32 pg = a[0], n = a[2], f = a[3], i, *e;
    if (pg < V86_PAGES || (pg << 12) >= 0xC0000000u) return vm_of(a[1]) != NULL;
    for (i = 0; i < n; i++) {
        e = pte_at((pg + i) << 12);
        if (e && (*e & 1)) *e &= ~(P_ACC | ((f & PageDiscard) ? P_DIRTY : 0));
    }
    vmm_flush_tlb();
    return 1;
}

/* Map_Lin_To_VM_Addr: EAX linear, ECX limit -> CX:EDX (V86: a segment
 * and offset; protected mode: a selector of the VM's LDT, offset 0) */
static int map_lin_to_vm(struct svcregs *s)
{
    struct client_regs *c = cur_vm->client;
    if (c->eflags & EFL_VM) {
        if (s->eax >= 0x110000) return 0;
        s->ecx = (s->ecx & 0xFFFF0000u) | (s->eax >> 4);
        s->edx = s->eax & 15;
        return 1;
    }
    {
        /* a selector made before for the same base and limit in this VM
         * is given again (the DDK: a device never frees one) */
        static struct { u32 vm, base, limit, sel; } made[32];
        static u32 nmade;
        u32 i, sel, lim = s->ecx;
        for (i = 0; i < nmade; i++)
            if (made[i].vm == VMH(cur_vm) && made[i].base == s->eax && made[i].limit == lim
                && selector_map_flat(VMH(cur_vm), made[i].sel) == s->eax) break;   /* (still there) */
        if (i < nmade) sel = made[i].sel;
        else {
            sel = dpmi_ldt_sel(s->eax, lim);
            if (!sel) return 0;
            if (nmade < sizeof(made) / sizeof(made[0])) {
                made[nmade].vm = VMH(cur_vm);
                made[nmade].base = s->eax;
                made[nmade].limit = lim;
                made[nmade++].sel = sel;
            }
        }
        s->ecx = (s->ecx & 0xFFFF0000u) | sel;
        s->edx = 0;
        return 1;
    }
}

/* ---- the services; C convention, the arguments after the return
 * address; 1 if one of these ---- */
static u32 temp_area;
static u32 page_out_count = 1;

int memsvc_service(u32 svc, struct svcregs *s)
{
    u32 *a = (u32 *)(s + 1) + 1;
    struct pgh *h;
    struct vm *v;
    switch (svc) {
    case 0x53: page_allocate(a, s); break;                  /* _PageAllocate */
    case 0x54: page_reallocate(a, s); break;                /* _PageReAllocate */
    case 0x55:                                              /* _PageFree(hMem, flags) */
        h = pgh_of(a[0]);
        s->eax = h && h != nul_h;
        if (s->eax) block_free(h);
        break;
    case 0x56: s->eax = page_lock(a, 1); break;             /* _PageLock */
    case 0x57: s->eax = page_lock(a, 0); break;             /* _PageUnLock */
    case 0x58:                                              /* _PageGetSizeAddr(hMem, flags) */
        /* (3.1 gave the nul page's size as FFFFFFFFh, VXH) */
        h = pgh_of(a[0]);
        s->eax = !h ? 0 : h == nul_h ? 0xFFFFFFFFu : h->n;
        s->edx = h ? h->lin : 0;
        break;
    case 0x59: {                                            /* _PageGetAllocInfo(flags) */
        u32 m[12];
        pager_meminfo(m);
        s->eax = m[7];                  /* the free linear space */
        s->edx = m[5] > 7 ? m[5] - 7 : 0;   /* what could be locked: free physical pages */
        break;
    }
    case 0x5B:                                              /* _GetSysPageCount(flags) */
        s->eax = (info->img_size >> 12) + sys_pages;
        break;
    case 0x5D: {                    /* _MapIntoV86(hMem, VM, VMLinPgNum, nPages, PageOff, flags) */
        u32 i, *pt;
        h = a[0] == (u32)nul_handle() ? nul_h : pgh_of(a[0]);
        v = vm_of(a[1]);
        s->eax = 0;
        if (!h || !v || !v86_range_ok(a[2], a[3])) break;
        if (h != nul_h && (a[4] + a[3] > h->n || a[4] + a[3] < a[4])) break;
        pt = vm_ptab(v);
        for (i = 0; i < a[3]; i++) {
            u32 p = h == nul_h ? nul_phys : block_page(h, a[4] + i);
            if (!p) break;
            pt[a[2] + i] = p | P_PRES | P_WRITE | P_USER | P_TYPE(h->type);
        }
        vmm_flush_tlb();
        s->eax = i == a[3];
        if (info->trace == 1)
            trace("VMM: _MapIntoV86(%x, VM %d, %x, %x, %x) by %s\n", a[0], v->id, a[2], a[3], a[4],
                  vxd_owner(*(u32 *)(s + 1)));
        break;
    }
    case 0x5E:                      /* _PhysIntoV86(PhysPage, VM, VMLinPgNum, nPages, flags) */
        v = vm_of(a[1]);
        s->eax = 0;
        if (!v || !v86_range_ok(a[2], a[3])) break;
        v86_map(v, a[2], a[3], a[0] << 12, PG_SYS);
        s->eax = 1;
        break;
    case 0x5F: s->eax = test_global(a[0], a[1]); break;     /* _TestGlobalV86Mem */
    case 0x60: s->eax = modify_page_bits(a); break;         /* _ModifyPageBits */
    case 0x61: s->eax = copy_page_table(a[0], a[1], (u32 *)a[2]); break;   /* _CopyPageTable */
    case 0x62: {                    /* _LinMapIntoV86(HLinPgNum, VM, VMLinPgNum, nPages, flags) */
        u32 i, p, t, *pt;
        v = vm_of(a[1]);
        s->eax = 0;
        if (!v) break;
        /* (3.1, VXH: EAX FFFFFFFFh, EDX the V86 page; a V86 page given
         * comes back as it is) */
        if (a[0] <= 0x100) { s->eax = ~0u; s->edx = a[0]; break; }
        if (!v86_range_ok(a[2], a[3])) break;
        pt = vm_ptab(v);
        for (i = 0; i < a[3]; i++) {
            if ((p = lin_page_phys(a[0] + i, &t)) == ~0u) break;
            pt[a[2] + i] = p | P_PRES | P_WRITE | P_USER | P_TYPE(t);
        }
        vmm_flush_tlb();
        if (i == a[3]) { s->eax = ~0u; s->edx = a[2]; }
        break;
    }
    case 0x63:                                              /* _LinPageLock(HLinPgNum, nPages, flags) */
    case 0x64: {                                            /* _LinPageUnLock */
        u32 lin = a[0] << 12, i;
        s->eax = 1;
        if (lin >= 0x00400000u && lin < W386_VMM_LIN) {
            if ((a[2] & PageLockedIfDP) && !dos_pager()) break;
            s->eax = pager_lock(lin, a[1] << 12, svc == 0x63);
        } else if (svc == 0x63 && (h = block_at(lin)) != NULL)
            for (i = 0; i < a[1] && block_at(lin + i * 4096) == h; i++)
                if (!block_page(h, (lin - h->lin) / 4096 + i)) { s->eax = 0; break; }
        break;
    }
    case 0x65: s->eax = set_reset_pageable(a); break;       /* _SetResetV86Pageable */
    case 0x66:                                              /* _GetV86PageableArray(VM, ArrayBuf, flags) */
        v = vm_of(a[0]);
        s->eax = v != NULL;
        if (v) memcpy((void *)a[1], v->pageable, 32);
        break;
    case 0x67: s->eax = a[1]; break;                        /* _PageCheckLinRange: 3.1 gave nPages (VXH) */
    case 0x68: {                                            /* _PageOutDirtyPages(nPages, flags) */
        /* the clients' dirty pageable pages, counted (PagePDPQueryDirty);
         * w16 writes a page when it evicts it, not ahead: none flushed */
        u32 lin, n = 0, *e;
        s->eax = 0;
        if (!(a[1] & PagePDPQueryDirty)) break;
        for (lin = 0x00400000u; lin < W386_VMM_LIN; lin += 4096) {
            if (!(vmm_pdir_entry_get(lin >> 22) & 1)) { lin |= 0x3FFFFFu; lin -= 4095; continue; }
            e = pte_at(lin);
            if ((*e & (1 | 0x400 | P_DIRTY)) == (1 | 0x400 | P_DIRTY)) n++;
        }
        s->eax = n;
        break;
    }
    case 0x69: s->eax = discard_pages(a); break;            /* _PageDiscardPages */
    case 0x6A: s->eax = (u32)nul_handle(); break;           /* _GetNulPageHandle */
    case 0x6B: s->eax = vm_first_v86_page(); break;         /* _GetFirstV86Page */
    case 0x6E: s->eax = selector_map_flat(a[0], a[1]); break;   /* _SelectorMapFlat */
    case 0x70:                                              /* _GetSetPageOutCount(NewCount, flags) */
        if (a[1] & 1) s->eax = page_out_count; else page_out_count = a[0];
        break;
    case 0x71:                                              /* Hook_V86_Page: EAX the page, ESI */
        /* 3.1 (VXH): pages above the last V86 page, below 110h; carry
         * for one hooked already (its VDD's A0h, B0h-B7h) */
        if (s->eax <= vm_last_v86_page() || s->eax >= V86_PAGES || !s->esi
            || hook_proc[s->eax] || vmm_owns_page(s->eax)) { s->eflags |= EFL_CF; break; }
        hook_proc[s->eax] = s->esi;
        break;
    case 0x72: s->eax = assign(a, 1); break;                /* _Assign_Device_V86_Pages */
    case 0x73: s->eax = assign(a, 0); break;                /* _DeAssign_Device_V86_Pages */
    case 0x74: {                                            /* _Get_Device_V86_Pages_Array(VM, ArrayBuf, flags) */
        u8 *arr = assigned_of(a[0]);
        s->eax = arr != NULL;
        if (arr) memcpy((void *)a[1], arr, 36);
        break;
    }
    case 0xA8: {                    /* _Allocate_Global_V86_Data_Area(nBytes, flags) */
        u32 f = a[1], al = (f & GVDAPageAlign) ? 4096 : (f & GVDAParaAlign) ? 16
                       : (f & GVDADWordAlign) ? 4 : (f & GVDAWordAlign) ? 2 : 1;
        s->eax = 0;
        if (temp_area || vmm_running()) break;
        s->eax = vm_global_v86_alloc(a[0], al, (f & GVDAInquire) != 0);
        if (!s->eax || (f & GVDAInquire)) break;
        if (f & GVDAZeroInit) memset((void *)s->eax, 0, a[0]);
        if ((f & GVDAInstance) && !vm_add_instance(s->eax, a[0])) s->eax = 0;
        break;
    }
    case 0xA9:                                              /* _Allocate_Temp_V86_Data_Area(nBytes, flags) */
        s->eax = temp_area || vmm_running() ? 0 : vm_temp_v86_area(a[0]);
        if (s->eax) temp_area = s->eax;
        break;
    case 0xAA:                                              /* _Free_Temp_V86_Data_Area */
        s->eax = temp_area != 0;
        temp_area = 0;
        break;
    case 0xD2: s->eax = 0; break;   /* _AddFreePhysPage: w16 manages its own block only: none added */
    case 0xD4:                                              /* _SetLastV86Page(PgNum, flags): V86MMGR's */
        s->eax = 0;
        break;
    case 0xD5: s->eax = vm_last_v86_page(); break;          /* _GetLastV86Page */
    case 0x1D:                                              /* Map_Lin_To_VM_Addr */
        if (!map_lin_to_vm(s)) s->eflags |= EFL_CF;
        break;
    default:
        return 0;
    }
    return 1;
}

