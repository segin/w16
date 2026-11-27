/* pager.c - w16 VMM: demand paging to the paging (swap) file, the
 * PageFile and PageSwap devices' side of the VMM (ARCHITECTURE.md 12.17).
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
static u32 resident;                    /* those of them in physical memory */
static u32 eager_min;                   /* free pages above which new ones are mapped at once */
static u32 hand = CLIENT_LO;            /* the clock hand (a linear address) */
static u32 page_ins, page_outs;
#define WIN_MAX 4                       /* pages in one DOS call (swap_io) */
static u32 win_lin, nwin;

/* page locks: lin page -> count, for the pages a client locked */
struct lock { struct lock *next; u32 page, count; };
static struct lock *locks[64];

static u32 *pte_of(u32 lin)
{
    u32 pde;
    /* only the clients' memory is the pager's: a V86 page table's
     * entries (a VM's first megabyte, its high linear window) keep the
     * DDK's page type in the same three bits (memsvc.c) */
    if (lin < CLIENT_LO || lin >= CLIENT_HI) return NULL;
    pde = vmm_pdir_entry_get(lin >> 22);
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
    {
        u32 b = (u32)info->xbuf_seg << 4, end = b + info->xbuf_size;
        win_lin = (b + 4095) & ~4095u;
        nwin = end > win_lin ? (end - win_lin) >> 12 : 0;
        if (nwin > WIN_MAX) nwin = WIN_MAX;
    }
    eager_min = f / 2 > 4 * RESERVE ? f / 2 : 4 * RESERVE;   /* (half the memory: the rest for pages touched later) */
    if (!info->swap_pages || info->swap_handle == 0xFFFF || !nwin) return;
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
    if (slots_used >= nslots) return ~0u;
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
    /* (the next search starts at a free slot: with the file full, a
     * linear search from an old hint cost more than the DOS call) */
    slot_hint = s;
}

/* The window the file is read and written through: the page-aligned pages
 * inside the loader's transfer buffer (info->xbuf_seg, 16 KB, in the
 * loader's memory, which every VM maps to the same physical pages).
 * For a transfer the current VM's page table entries of the window are
 * pointed at the pages being read or written, so DOS reads and writes
 * them where they are, and set back after: what the buffer holds (a DOS
 * call from protected mode may be using it when a page fault comes) is
 * never touched, and a DOS VM's own, smaller buffer is not used. Up to
 * that many pages go in one DOS call (one page per call made paging a
 * program larger than the memory too slow: bigmem e128) */

/* n pages (phys[]) of the file from slot on, in or out */
static int swap_io(u32 slot, const u32 *phys, u32 n, int write)
{
    u32 *pt = (u32 *)phys_to_lin(cur_vm->ptab_phys) + (win_lin >> 12);
    u32 old[WIN_MAX], i;
    struct cregs v;
    u16 *cur_psp = info->sda_lin ? (u16 *)(info->sda_lin + 0x10) : NULL;
    u16 oldpsp = 0;
    u32 off = slot << 12;
    int ok;

    if (!n || n > nwin) return 0;
    for (i = 0; i < n; i++) {
        old[i] = pt[i];
        pt[i] = phys[i] | 7;
    }
    vmm_flush_tlb();
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
        v.ecx = n << 12;
        v.ds = win_lin >> 4;
        v86_int(&v, 0x21);
        ok = !(v.eflags & EFL_CF) && (v.eax & 0xFFFF) == (n << 12);
    }
    if (cur_psp) *cur_psp = oldpsp;
    for (i = 0; i < n; i++) pt[i] = old[i];
    vmm_flush_tlb();
    return ok;
}

static struct lock *lock_find(u32 page)
{
    struct lock *k;
    for (k = locks[(page >> 12) & 63]; k; k = k->next)
        if (k->page == page) return k;
    return NULL;
}

/* n free slots in a row (~0: none) */
static u32 slot_run(u32 n)
{
    u32 i, s, k;
    if (n == 1) return slot_alloc();
    if (nslots - slots_used < n) return ~0u;
    /* (a short search only: the caller writes one page at a time
     * without a run) */
    for (i = 0; i < nslots && i < 64; i++) {
        s = (slot_hint + i) % nslots;
        if (s + n > nslots) continue;
        for (k = 0; k < n && !(slotmap[(s + k) >> 3] & (1 << ((s + k) & 7))); k++)
            ;
        if (k < n) { i += k; continue; }
        for (k = 0; k < n; k++) slotmap[(s + k) >> 3] |= (u8)(1 << ((s + k) & 7));
        slots_used += n;
        slot_hint = s + n;
        return s;
    }
    return ~0u;
}

/* every 1024 pages in or out: the counts and the BIOS tick (the trace) */
static void pager_progress(void)
{
    trace("VMM: paging: %x out, %x in, %x slots used, %x free, tick %x\n", page_outs, page_ins,
          slots_used, pages_free(), *(u32 *)0x46C);
}

/* pages i .. i+n of an eviction written from slot on: those not taken
 * back meanwhile are in the file now and their physical pages free; the
 * rest go back as they were (the write failed) or keep what they have */
struct victim { u32 *pte, orig, phys, tv; };

static u32 evict_done(struct victim *v, u32 n, u32 slot, int ok)
{
    u32 i, got = 0;
    for (i = 0; i < n; i++) {
        if (*v[i].pte != v[i].tv) {     /* taken back while it was written */
            slot_free(slot + i);
            continue;
        }
        if (!ok) {
            slot_free(slot + i);
            *v[i].pte = v[i].orig;
            continue;
        }
        *v[i].pte = ((slot + i) << 12) | PTE_SWAPPED | PTE_PAGEABLE | (v[i].orig & 6);
        page_free(v[i].phys);
        if ((++page_outs & 0x3FF) == 0) pager_progress();
        if (resident) resident--;
        got++;
    }
    return got;
}

/* pageable pages out to the file, up to the window's size in one DOS
 * call (a run of free slots; one at a time without one): their physical
 * pages are free after. The number written */
static u32 evict(void)
{
    struct victim v[WIN_MAX];
    u32 n, e, k = 0, i, slot, got = 0, limit = 2 * ((CLIENT_HI - CLIENT_LO) >> 12);
    u32 phys[WIN_MAX], *pte;
    for (n = 0; n < limit && k < nwin; n++, hand += 4096) {
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
        v[k].pte = pte;
        v[k].orig = e;
        v[k].phys = phys[k] = e & ~0xFFFu;
        v[k].tv = v[k].phys | PTE_TRANSIT | PTE_PAGEABLE | (e & 6);
        *pte = v[k].tv;
        k++;
    }
    if (!k) return 0;
    vmm_flush_tlb();
    slot = slot_run(k);
    if (slot != ~0u) {
        int ok = swap_io(slot, phys, k, 1);
        got = evict_done(v, k, slot, ok);
        if (!ok) trace("VMM: the paging file could not be written (slots %x-%x)\n", slot, slot + k - 1);
    } else {
        for (i = 0; i < k; i++) {
            slot = slot_alloc();
            if (slot == ~0u) {
                if (*v[i].pte == v[i].tv) *v[i].pte = v[i].orig;
                continue;
            }
            got += evict_done(&v[i], 1, slot, swap_io(slot, &phys[i], 1, 1));
        }
    }
    vmm_flush_tlb();
    return got;
}

/* a physical page for a client's pageable memory, paging another out if
 * none is free */
u32 pager_page(void)
{
    if (nslots)
        while (pages_free() <= RESERVE && evict())
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
        resident++;
        return 1;
    }
    {
        /* the pages after it that went out with it (the slots after its
         * own) come back in the same DOS call */
        u32 *ptes[WIN_MAX], ev[WIN_MAX], ph[WIN_MAX], k, n, i, slot = e >> 12;
        ptes[0] = pte; ev[0] = e; ph[0] = phys;
        for (n = 1; n < nwin; n++) {
            u32 *q = pte_of(lin + n * 4096), f;
            if (!q || ((lin + n * 4096) & ~0xFFFu) >= CLIENT_HI) break;
            f = *q;
            if ((f & (1 | PTE_SWAPPED | PTE_PAGEABLE | PTE_TRANSIT)) != (PTE_SWAPPED | PTE_PAGEABLE)
                || (f >> 12) != slot + n) break;
            ptes[n] = q; ev[n] = f;
        }
        for (k = 1; k < n; k++)
            if ((ph[k] = page_alloc()) == 0) break;
        n = k;
        for (k = 1; k < n && *ptes[k] == ev[k]; k++)    /* (still out) */
            ;
        for (i = k; i < n; i++) page_free(ph[i]);
        n = k;
        if (*pte != e) {                /* read in meanwhile (a nested fault) */
            for (i = 0; i < n; i++) page_free(ph[i]);
            return 1;
        }
        if (!swap_io(slot, ph, n, 0)) {
            for (i = 0; i < n; i++) page_free(ph[i]);
            trace("VMM: the paging file could not be read (slot %x at %x)\n", slot, lin);
            return 0;
        }
        for (i = n; i--; ) {             /* (the last first: the hint is the run's start) */
            if (*ptes[i] != ev[i]) {    /* (in meanwhile) */
                page_free(ph[i]);
                continue;
            }
            /* the faulting page accessed, the others not: the clock takes
             * those first if they are not used */
            *ptes[i] = ph[i] | PTE_PAGEABLE | (ev[i] & 6) | 1 | (i ? 0 : 0x20);
            slot_free(slot + i);
            if ((++page_ins & 0x3FF) == 0) pager_progress();
            resident++;
        }
    }
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
    if (resident) resident--;           /* present: the caller frees it */
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
            while (i--) {
                pte = pte_of(lin + i * 4096);
                if (pte && (*pte & 1)) { page_free(*pte & ~0xFFFu); resident--; }
                if (pte) *pte = 0;
                committed--;
            }
            return 0;
        }
        pte = pte_of(lin + i * 4096);
        *pte = PTE_PAGEABLE | 6;
        /* while memory is plentiful the page is there at once, as before
         * the paging file (a fault for every first touch slowed the boot
         * and the timing tests); else it comes when first touched */
        if (pages_free() > eager_min) {
            u32 p = page_alloc();
            if (p) { *pte = p | PTE_PAGEABLE | 7; resident++; }
        }
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
    unlocked = f + resident;
    if (nslots) {
        /* the free linear space less one, but no more than the memory and
         * the file can hold: 3.1 gave 258,964 KB with 128 MB (ARCHITECTURE.md
         * 9.6), and its 0501h of more than memory and a 2 MB file failed */
        u32 c = pager_avail(), t = c / 1025 + 2;
        p[1] = linfree ? linfree - 1 : 0;
        if (p[1] > (c > t ? c - t : 0)) p[1] = c > t ? c - t : 0;
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
