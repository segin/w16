/* pager.c - w16 VMM: demand paging to the paging (swap) file, the
 * PageFile and PageSwap devices' side of the VMM (ARCHITECTURE.md 12.16).
 *
 * The loader (win386/ldr/swap.c) makes the paging file before Windows
 * starts, as SYSTEM.INI's [386Enh] keys say, and leaves it open (a handle
 * of its own PSP, not inherited); info->swap_pages is its size in pages
 * (the most the file may hold), 0 when there is none (Paging=no, or no
 * room on the disk).
 *
 * Which memory is paged: the pages of the protected-mode clients' DPMI
 * memory blocks (0501h, 0503h; dpmi.c marks their page table entries
 * PTE_PAGEABLE). Under Windows 3.1 a client's memory is limited by the
 * paging file, not by the physical memory (VX18: 0501h of more than the
 * physical memory succeeds and reads back what was written, and 0500h's
 * figures follow the file's size): every page a client has is counted
 * against the file (commit), and the physical pages hold the ones in use.
 * When no physical page is free, the clock hand looks for a pageable page
 * that has not been used since its last pass (the accessed bit), writes it
 * to a free slot of the file and gives its physical page to the caller.
 * Touching a page that is out raises a page fault (in the client, or in
 * the VMM or a VxD working on the client's memory), which reads it back.
 *
 * Page table entries (bits 9-11 are the system's, Intel 386 manual):
 *   present:     PTE_PAGEABLE for the pages that may go to the file
 *   not present: PTE_SWAPPED, the slot in bits 12-31 (the page is in the
 *                file); PTE_TRANSIT, the physical page in bits 12-31 (it is
 *                being written out: a fault then takes it back as it is)
 * DPMI 0600h/0601h lock and unlock pages (a count per page, kept here).
 *
 * The file is read and written with DOS (INT 21h 42h, 3Fh, 40h, run in the
 * current VM with the loader's PSP current) through the first 4 KB of the
 * transfer buffer, whose contents are saved on the ring-0 stack around
 * each transfer: a page fault can come in the middle of a DOS call from
 * protected mode that is using the buffer. The DDK's MS-DOS pager (page
 * file type 2: PageSwap_Get_Version).
 */
#include "vmm.h"

#define PTE_SWAPPED     0x200u
#define PTE_PAGEABLE    0x400u
#define PTE_TRANSIT     0x800u
#define CLIENT_LO       0x00400000u     /* the clients' linear memory: 4 MB .. */
#define CLIENT_HI       W386_VMM_LIN

static u8 *slotmap;                     /* a bit per page of the file: in use */
static u32 nslots, slots_used, slot_hint;
static u32 committed;                   /* pageable pages the clients have */
static u32 hand = CLIENT_LO;            /* the clock hand (a linear address) */
static u32 page_ins, page_outs;

/* page locks: lin page -> count, for the pages a client locked */
struct lock { struct lock *next; u32 page, count; };
static struct lock *locks[64];

static u32 *pte_of(u32 lin)
{
    u32 pde = vmm_pdir_entry_get(lin >> 22);
    if (!(pde & 1)) return NULL;
    return (u32 *)phys_to_lin(pde & ~0xFFFu) + ((lin >> 12) & 0x3FF);
}

/* pages the VMM itself keeps free (page tables, its heap, devices) */
#define RESERVE 64

static u32 lin_total;                   /* the linear address space, pages (0500h) */
static u32 commit_max;                  /* the most pageable pages the clients may have */

void pager_init(void)
{
    u32 f = pages_free();
    /* Windows 3.1's linear address space grew with [386Enh]
     * PageOverCommit: 0500h's linear space was 39FCh pages with the
     * default 4 and 1EDCh with 2 (16 MB, VX18), a multiple of the
     * physical memory and a constant part. UNVERIFIED: what that
     * multiple is of; here of the free pages when the VMM starts */
    lin_total = (info->overcommit ? info->overcommit : 4) * f;
    if (!info->swap_pages || info->swap_handle == 0xFFFF) return;
    nslots = info->swap_pages;
    /* (the file's size: never more than the linear address space; under
     * Windows 3.1 0500h's paging file size was the linear space's without
     * MaxPagingFileSize, VX18) */
    if (nslots > lin_total) nslots = lin_total;
    slotmap = kmalloc((nslots + 7) / 8);
    if (!slotmap) { nslots = 0; return; }
    memset(slotmap, 0, (nslots + 7) / 8);
    /* what the clients can have: the physical memory and the file (with
     * a 2 MB file Windows 3.1 refused 0501h of the physical memory and 1
     * MB more; with its default file it gave it, VX18) */
    commit_max = (f > 2 * RESERVE ? f - 2 * RESERVE : 0) + nslots;
    trace("VMM: paging file %x pages (type %x), linear space %x pages, commit %x\n",
          nslots, info->swap_type, lin_total, commit_max);
}

int pager_on(void) { return nslots != 0; }
u32 pager_file_pages(void) { return nslots; }

/* the pages one more block may have (before its page tables): with a
 * paging file what may still be committed, else the free physical pages */
u32 pager_avail(void)
{
    if (!nslots) return pages_free();
    return commit_max > committed ? commit_max - committed : 0;
}

static u32 slot_alloc(void)
{
    u32 i, s;
    for (i = 0; i < nslots; i++) {
        s = (slot_hint + i) % nslots;
        if (!(slotmap[s >> 3] & (1 << (s & 7)))) {
            slotmap[s >> 3] |= (u8)(1 << (s & 7));
            slots_used++;
            slot_hint = s + 1;
            return s;
        }
    }
    return ~0u;
}

static void slot_free(u32 s)
{
    if (s >= nslots || !(slotmap[s >> 3] & (1 << (s & 7)))) return;
    slotmap[s >> 3] &= (u8)~(1 << (s & 7));
    slots_used--;
}

/* one page of the file in or out, at the VMM's address buf */
static int swap_io(u32 slot, u32 buf, int write)
{
    u8 save[4096];
    u8 *xb = (u8 *)xbuf_lin();
    struct cregs v;
    u16 *cur_psp = info->sda_lin ? (u16 *)(info->sda_lin + 0x10) : NULL;
    u16 oldpsp = 0;
    u32 off = slot << 12;
    int ok;

    memcpy(save, xb, 4096);
    if (write) memcpy(xb, (void *)buf, 4096);
    if (cur_psp) { oldpsp = *cur_psp; *cur_psp = info->psp; }
    memset(&v, 0, sizeof(v));
    v.eax = 0x4200;
    v.ebx = info->swap_handle;
    v.ecx = off >> 16;
    v.edx = off & 0xFFFF;
    v86_int(&v, 0x21);
    ok = !(v.eflags & EFL_CF);
    if (ok) {
        memset(&v, 0, sizeof(v));
        v.eax = write ? 0x4000 : 0x3F00;
        v.ebx = info->swap_handle;
        v.ecx = 4096;
        v.ds = info->xbuf_seg;
        v86_int(&v, 0x21);
        ok = !(v.eflags & EFL_CF) && (v.eax & 0xFFFF) == 4096;
    }
    if (cur_psp) *cur_psp = oldpsp;
    if (ok && !write) memcpy((void *)buf, xb, 4096);
    memcpy(xb, save, 4096);
    return ok;
}

static struct lock *lock_find(u32 page)
{
    struct lock *k;
    for (k = locks[(page >> 12) & 63]; k; k = k->next)
        if (k->page == page) return k;
    return NULL;
}

/* one pageable page out to the file: its physical page is free after */
static int evict_one(void)
{
    u32 n, *pte, e, phys, slot, limit = 2 * ((CLIENT_HI - CLIENT_LO) >> 12);
    for (n = 0; n < limit; n++, hand += 4096) {
        if (hand < CLIENT_LO || hand >= CLIENT_HI) hand = CLIENT_LO;
        pte = pte_of(hand);
        if (!pte) {                     /* no page table: the next 4 MB */
            hand = (hand | 0x3FFFFFu) - 4095;
            n += 1023;
            continue;
        }
        e = *pte;
        if ((e & (1 | PTE_PAGEABLE)) != (1 | PTE_PAGEABLE)) continue;
        if (e & 0x20) { *pte = e & ~0x20u; continue; }      /* used: a second chance */
        if (lock_find(hand)) continue;
        slot = slot_alloc();
        if (slot == ~0u) return 0;
        phys = e & ~0xFFFu;
        *pte = phys | PTE_TRANSIT | PTE_PAGEABLE | (e & 6);
        vmm_flush_tlb();
        if (!swap_io(slot, phys_to_lin(phys), 1)) {
            slot_free(slot);
            if (*pte == (phys | PTE_TRANSIT | PTE_PAGEABLE | (e & 6))) *pte = e;
            vmm_flush_tlb();
            trace("VMM: the paging file could not be written (slot %x)\n", slot);
            return 0;
        }
        if (*pte != (phys | PTE_TRANSIT | PTE_PAGEABLE | (e & 6))) {
            slot_free(slot);            /* taken back while it was written */
            continue;
        }
        *pte = (slot << 12) | PTE_SWAPPED | PTE_PAGEABLE | (e & 6);
        page_free(phys);
        page_outs++;
        hand += 4096;
        return 1;
    }
    return 0;
}

/* a physical page for a client's pageable memory, paging another out if
 * none is free */
u32 pager_page(void)
{
    if (nslots)
        while (pages_free() <= RESERVE && evict_one())
            ;
    return page_alloc();
}

/* a page fault at lin: 1 if the page is back (the access can be made
 * again), 0 if it is not one of the pager's */
int pager_fault(u32 lin)
{
    u32 *pte = pte_of(lin), e, phys;
    if (!pte) return 0;
    e = *pte;
    if (e & 1) return 0;
    if (e & PTE_TRANSIT) {              /* being written out: back as it is */
        *pte = (e & ~(PTE_TRANSIT | 0xFFu)) | (e & 6) | 1;
        return 1;
    }
    if (!(e & PTE_PAGEABLE)) return 0;
    phys = pager_page();
    if (!phys) return 0;
    if (!(e & PTE_SWAPPED)) {           /* not used yet: a zeroed page */
        if (*pte != e) { page_free(phys); return 1; }
        *pte = phys | PTE_PAGEABLE | (e & 6) | 1;
        return 1;
    }
    if (!swap_io(e >> 12, phys_to_lin(phys), 0)) {
        page_free(phys);
        trace("VMM: the paging file could not be read (slot %x at %x)\n", e >> 12, lin);
        return 0;
    }
    if (*pte != e) {                    /* read in meanwhile (a nested fault) */
        page_free(phys);
        return 1;
    }
    *pte = phys | PTE_PAGEABLE | (e & 6) | 1;
    slot_free(e >> 12);
    page_ins++;
    return 1;
}

/* the page table entry of a pageable page that goes away (the block is
 * freed): its slot is free again. 1 if it was in the file */
int pager_drop(u32 lin)
{
    u32 *pte = pte_of(lin), e;
    struct lock **pp, *k;
    if (!pte) return 0;
    e = *pte;
    if (!(e & PTE_PAGEABLE)) return 0;
    if (committed) committed--;
    for (pp = &locks[(lin >> 12) & 63]; (k = *pp) != NULL; pp = &k->next)
        if (k->page == (lin & ~0xFFFu)) { *pp = k->next; kfree(k); break; }
    if (!(e & 1) && (e & PTE_SWAPPED)) {
        slot_free(e >> 12);
        *pte = 0;
        return 1;
    }
    if (!(e & 1) && (e & PTE_TRANSIT)) {
        *pte = (e & ~(PTE_TRANSIT | 0xFFu)) | (e & 6) | 1;  /* its write is dropped when it ends */
        return 0;
    }
    if (!(e & 1)) {                     /* never used */
        *pte = 0;
        return 1;
    }
    return 0;
}

/* a client's new pages lin .. lin+n pages (DPMI 0501h, 0503h): with a
 * paging file each is pageable and not there until it is used (a zeroed
 * page then); 0 if they may not be committed (nothing done). Without one:
 * -1, the caller maps physical pages as before */
int pager_map(u32 lin, u32 n)
{
    u32 i, *pte;
    if (!nslots) return -1;
    if (n > pager_avail()) return 0;
    for (i = 0; i < n; i++) {
        if (!map_page_noflush(lin + i * 4096, 0x1000, 0)) {     /* the page table */
            while (i--) { pte = pte_of(lin + i * 4096); if (pte) *pte = 0; committed--; }
            return 0;
        }
        pte = pte_of(lin + i * 4096);
        *pte = PTE_PAGEABLE | 6;
        committed++;
    }
    vmm_flush_tlb();
    return 1;
}

/* Is lin's page one that is out (in the file or on its way)? Then it is
 * brought in: the VMM's checks of a client's pointers (lin_user_ok) */
int pager_present(u32 lin)
{
    u32 *pte = pte_of(lin);
    if (!pte || (*pte & 1)) return 1;
    if (!(*pte & PTE_PAGEABLE)) return 0;
    return pager_fault(lin);
}

/* DPMI 0600h/0601h: lock or unlock lin .. lin+size (the pages in it the
 * pager owns are brought in and counted) */
int pager_lock(u32 lin, u32 size, int lock)
{
    u32 a, end;
    struct lock *k;
    if (!size) return 1;
    end = (lin + size - 1) & ~0xFFFu;
    for (a = lin & ~0xFFFu; ; a += 4096) {
        u32 *pte = pte_of(a);
        if (pte && (*pte & PTE_PAGEABLE)) {
            if (lock) {
                if (!pager_present(a)) return 0;
                k = lock_find(a);
                if (!k && (k = kmalloc(sizeof(*k))) != NULL) {
                    k->page = a; k->count = 0;
                    k->next = locks[(a >> 12) & 63];
                    locks[(a >> 12) & 63] = k;
                }
                if (k) k->count++;
            } else if ((k = lock_find(a)) != NULL && k->count && !--k->count) {
                struct lock **pp;
                for (pp = &locks[(a >> 12) & 63]; *pp != k; pp = &(*pp)->next)
                    ;
                *pp = k->next;
                kfree(k);
            }
        }
        if (a == end) break;
    }
    return 1;
}

/* DPMI 0500h's figures (as Windows 3.1's, VX18: the largest block and the
 * most unlocked pages one less than the free linear space with a paging
 * file, the free physical pages less 7 without; the most locked pages 7
 * less than the unlocked ones) */
void pager_meminfo(u32 *p)
{
    u32 f = pages_free(), i, unlocked, linfree;
    for (i = 0; i < 12; i++) p[i] = 0xFFFFFFFF;
    linfree = lin_total > committed ? lin_total - committed : 0;
    unlocked = f + (committed > slots_used ? committed - slots_used : 0);
    if (nslots) {
        p[1] = linfree ? linfree - 1 : 0;
        p[2] = unlocked > 7 ? unlocked - 7 : 0;
    } else {
        u32 t = f / 1025 + 2;           /* page tables (dpmi.c mem_largest_pages) */
        p[1] = p[2] = f > t ? f - t : 0;
        unlocked = f;
    }
    p[0] = p[1] * 4096;
    p[3] = lin_total;
    p[4] = unlocked;
    p[5] = f;
    p[6] = info->xms_size / 4096;
    p[7] = linfree;
    p[8] = nslots;
}

void pager_stats(u32 *ins, u32 *outs, u32 *used)
{
    *ins = page_ins;
    *outs = page_outs;
    *used = slots_used;
}

/* The services (the DDK's Virtual Device Adaptation Guide):
 *   VMM _GetFreePageCount (5Ah): EAX the free pages, EDX the pages that
 *     could be allocated locked
 *   VMM _GetDemandPageInfo (6Fh): a DemandInfoStruc
 *   PageSwap (0007h) PageSwap_Get_Version: EAX the version (0200h as
 *     3.1 gave it, VXW), ECX the file's largest size in pages, EBX the type (1
 *     none, 2 MS-DOS), carry clear; PageSwap_Test_Create: carry if ECX
 *     pages could not be allocated; PageSwap_Test_IO_Valid: carry if
 *     paging must be avoided (no file)
 * 1 if done. */
int pager_service(u32 dev, u32 svc, struct svcregs *s)
{
    u32 *arg = (u32 *)(s + 1) + 1;      /* a C-convention service's arguments */
    u32 m[12];
    if (dev == 1 && svc != 0x5A && svc != 0x6F) return 0;
    if (dev != 1 && (dev != 7 || svc > 2)) return 0;
    pager_meminfo(m);
    if (dev == 1) {
        if (svc == 0x5A) {              /* _GetFreePageCount(flags) */
            s->eax = m[5];
            s->edx = m[2];
        } else {                        /* _GetDemandPageInfo(DemandInfo, flags) */
            u32 *d = (u32 *)arg[0];
            if (!d) return 1;
            memset(d, 0, 16 * 4);
            d[0] = m[3];                /* DILin_Total_Count */
            d[1] = m[6];                /* DIPhys_Count */
            d[2] = m[5];                /* DIFree_Count */
            d[3] = m[4];                /* DIUnlock_Count */
            d[4] = 0;                   /* DILinear_Base_Addr: UNVERIFIED (0: not one range) */
            d[5] = m[7];                /* DILin_Total_Free */
        }
        return 1;
    }
    switch (svc) {
    case 0:                             /* PageSwap_Get_Version */
        s->eax = 0x0200;                /* Windows 3.1's (VXW), not the DDB's 2.10 */
        s->ecx = nslots;
        s->ebx = nslots ? info->swap_type : 1;  /* (all of EBX: 2 under 3.1, VXW) */
        s->eflags &= ~EFL_CF;
        break;
    case 1:                             /* PageSwap_Test_Create(ECX pages, EAX flags) */
        if (s->ecx <= pager_avail()) s->eflags &= ~EFL_CF;
        else s->eflags |= EFL_CF;
        break;
    case 2:                             /* PageSwap_Test_IO_Valid */
        if (nslots) s->eflags &= ~EFL_CF;
        else s->eflags |= EFL_CF;
        break;
    }
    return 1;
}
