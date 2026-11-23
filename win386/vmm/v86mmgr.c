/* v86mmgr.c - w16's V86MMGR (device 0006h; ARCHITECTURE.md 12.13): the DOS
 * VMs' XMS driver and LIM EMS 4.0 driver, backed by the VMM's own pages,
 * the A20 line and the HMA per VM, and V86MMGR's services for devices.
 * Like VTD, the VDD and SHELL it is part of the VMM module, with a DDB of
 * its own for the dynamic links (vxd.c): Windows 3.1's "V86MMGR", 0006h,
 * init order A0000000h, 21 services.
 *
 * Observed under Windows 3.1 (VX15.COM in a DOS box, tests/v86mmrun.sh;
 * VXT.386's V86MMGR lines; DOSBox and DOSBox-X, 16 MB):
 * XMS (INT 2Fh 4300h AL=80h, 4310h the entry; every PIF):
 *   - 00h: AX 0200h (XMS 2.00), BX 0205h, DX 1 (the HMA exists).
 *   - 08h: AX the largest block, DX the total, both the PIF's XMS limit
 *     (1,024 KB by default; 256 with 256) less what is allocated, BL 0;
 *     with a KB Required of 256 and a limit of 512: 256/512, after 128 KB
 *     allocated 128/384; with no limit the largest 2,048 and a total that
 *     counts the swap file (w16 has none: both what memory there is).
 *   - 01h: 91h (the HMA in use: DOSBox's own was) whatever the PIF says;
 *     02h then succeeds once, then 93h.
 *   - 03h-07h: the A20 query gives 0 at first; local and global enable
 *     and disable succeed and the query follows them.
 *   - 09h: 64 KB, 0 KB succeed; FFFFh KB: A0h. 32 handles per VM (1 KB
 *     blocks until A1h). 0Eh: AX 1, BH the lock count, BL the free
 *     handles (31 with one block), DX the size.
 *   - 0Bh: moves to and from conventional memory; an odd length is
 *     accepted; a source offset whose end passes the block's: A7h.
 *   - 0Ch (lock): always AX 0, BL ADh (lock failed); 0Dh: AAh.
 *   - 0Fh: resizes (128 KB); 0Ah frees, and a second 0Ah on the same
 *     handle succeeds again; a limit of 0: 09h gives A0h (but 0 KB works).
 *   - 10h (UMB): BL B1h, DX unchanged.
 * EMS (INT 67h; with DOSBox's own EMS loaded before Windows, its frame
 * E000h; none without it, nor with NoEMMDriver=on; EMMPageFrame=D000
 * without DOSBox's gave a driver that failed 41h with 81h):
 *   - 40h AH 0; 41h BX E000h; 42h BX free, DX total pages: the PIF's EMS
 *     limit / 16 (64 by default, 16 with 256 KB, 0 with 0: 43h then 87h);
 *     46h AL 40h (LIM 4.0); 4Bh BX 1 (handle 0) at first.
 *   - 43h with 4 pages: a handle; 44h maps, the frame shows the pages;
 *     44h with BX FFFFh unmaps; a logical page past the handle's: 8Ah; a
 *     physical page past 3: 8Bh; 4Ch the pages; 47h saves, again 8Dh;
 *     48h restores; 51h resizes; 5301h/5300h name it; 5801h CX 4 and
 *     5800h E000/0, E400/1, E800/2, EC00/3: only the frame; 5901h BX the
 *     free and DX the total raw pages; 45h frees, again 83h; 43h with 0
 *     pages 89h, with 7FFFh 87h; 1-page handles until 88h (64).
 * V86MMGR services: Get_Version EAX 030Ah; Get_Mapping_Info ECX 0300h;
 * Get_Xlat_Buff_State for the System VM EAX 0A00h, ECX and EDX 2000h;
 * Get_EMS_XMS_Limits: the System VM 0/800h/0/800h/1, a DOS VM the PIF's
 * required and limit in KB (FFFFFFFCh for no limit), ECX 1; GetPgStatus
 * 80h for the frame's pages, 100h for the HMA's (100h-10Fh).
 * Sources: the XMS 2.0/3.0 and LIM EMS 4.0 specifications (functions,
 * registers, error codes); the DDK's Virtual Device Adaptation Guide
 * (V86MMGR's services); Intel 80386 Programmer's Reference Manual (paging).
 */
#include "vmm.h"

#define XMS_HANDLES     32
#define EMS_HANDLES     255
#define EMS_PAGES_MAX   2048            /* per handle (32 MB): w16's own bound */

struct emb {                            /* an XMS block */
    int known, used;
    u32 kb, n;                          /* size, VMM pages */
    u32 *pg;                            /* their physical addresses */
};
struct eh {                             /* an EMS handle */
    int used;
    u32 n;                              /* 16 KB pages */
    u32 *pg;                            /* 4 VMM pages each */
    char name[8];
    int saved;
    s32 save[4];
};
struct mmvm {
    u32 xms_req, xms_lim, ems_req, ems_lim; /* KB; lim FFFFFFFCh: none */
    u32 xms_used, ems_used;             /* KB, EMS pages */
    struct emb emb[XMS_HANDLES];
    struct eh eh[EMS_HANDLES];
    s32 map[4];                         /* per frame page: handle << 16 | page, -1 none */
    u32 frame_pte[16];                  /* the frame's PTEs as the VM had them */
    u32 hma_pte[16];                    /* the HMA's */
    u32 *hma_pg;                        /* its own HMA's pages, or NULL (a global HMA) */
    int hma;                            /* 0 free, 1 in use, 2 the VM's */
    int a20_global, a20_local;          /* XMS 03h-06h */
    int wrap;                           /* the HMA mapped onto 0-FFFFh now */
    int a20_fixed;                      /* A20 was on at the start: it stays on */
};

static u32 *get_pages(u32 n);
static void free_pages(u32 *pg, u32 n);
static int hma_inherit = 1;             /* the HMA's state a new VM starts with */
static u32 sys_lim = 0x800;             /* Get_EMS_XMS_Limits for the System VM */
static u32 xbuf_seg_set, xbuf_free_set, xbuf_size_set;   /* Set_Xlat_Buff_State */

static u32 *ptab(struct vm *v)
{
    return (u32 *)phys_to_lin(v->ptab_phys);
}

static u32 kb_round(u32 kb)
{
    return kb == 0xFFFF ? 0xFFFFFFFCu : (kb + 3) & ~3u;
}

/* ---- per VM ---- */
void v86mm_vm_create(struct vm *v, const u8 *b)
{
    struct mmvm *m = kmalloc(sizeof(*m));
    u32 i, *pt;
    v->mm = m;
    if (!m) return;
    memset(m, 0, sizeof(*m));
    /* SHELL function 3's block: +22h EMS limit, +24h required, +26h XMS
     * limit, +28h required (the PIF's 386 data from +08h) */
    m->ems_lim = b ? kb_round(*(u16 *)(b + 0x22)) : 1024;
    m->ems_req = b ? kb_round(*(u16 *)(b + 0x24)) : 0;
    m->xms_lim = b ? kb_round(*(u16 *)(b + 0x26)) : 1024;
    m->xms_req = b ? kb_round(*(u16 *)(b + 0x28)) : 0;
    if (info->ems_size && (m->ems_lim == 0xFFFFFFFCu || m->ems_lim > info->ems_size))
        m->ems_lim = kb_round(info->ems_size);
    /* the HMA in use at first: Windows 3.1's boxes got 91h for 01h, then
     * 02h succeeded once (DOSBox's HMA was free when WIN386 started:
     * UNVERIFIED who holds it; w16's loader notes it in hma_used); of three
     * boxes started together only the first found it so, the others free
     * (02h: 93h; MEMLOAD C). UNVERIFIED: whether it is the first box of the
     * session or the first while the first still runs */
    m->hma = hma_inherit;
    hma_inherit = 0;
    /* A20 on when Windows started (DOSBox-X: its DOS in the HMA, a
     * global user): Windows 3.1's XMS said A20 was on at every query,
     * whatever 03h-06h did, and the HMA stays mapped (the DDK: the A20
     * state of a global HMA user is global); with A20 off then (plain
     * DOSBox) the query followed the VM's calls. UNVERIFIED that this is
     * what 3.1 goes by */
    m->a20_fixed = info->dos_high;
    for (i = 0; i < 4; i++) m->map[i] = -1;
    pt = ptab(v);
    if (info->ems_frame)
        for (i = 0; i < 16; i++) m->frame_pte[i] = pt[(info->ems_frame >> 8) + i];
    for (i = 0; i < 16; i++) m->hma_pte[i] = pt[0x100 + i];
    /* no global HMA user (A20 off when Windows started): the VM's HMA is
     * its own, 64 KB of pages starting as the HMA was (the DDK: the HMA is
     * local unless a global user has it; two boxes using it at once must
     * not see each other's data: MEMLOAD C) */
    if (!info->dos_high && (m->hma_pg = get_pages(16)) != NULL)
        for (i = 0; i < 16; i++) {
            memcpy((void *)phys_to_lin(m->hma_pg[i]), (void *)phys_to_lin(m->hma_pte[i] & ~0xFFFu), 4096);
            m->hma_pte[i] = m->hma_pg[i] | 7;
            pt[0x100 + i] = m->hma_pte[i];
        }
    m->eh[0].used = 1;                  /* handle 0: the system's, no pages */
    /* no EMS in the VMs but a manager loaded before Windows (NoEMMDriver):
     * its INT 67h is not the VM's (Windows 3.1: no EMMXXXX0 at the
     * vector's segment in the box) */
    if (!info->ems_frame) {
        u32 *vec = (u32 *)vm_lin(v, 0x67 * 4);
        if ((*vec >> 16) && !memcmp((void *)(((*vec >> 16) << 4) + 0x0A), "EMMXXXX0", 8))
            *vec = ((u32)info->ldr_cs << 16) | (info->bp_off + W386_BP_NOEMS);
    }
}

static void free_pages(u32 *pg, u32 n)
{
    u32 i;
    for (i = 0; i < n; i++) if (pg[i]) page_free(pg[i]);
}

void v86mm_vm_destroy(struct vm *v)
{
    struct mmvm *m = v->mm;
    u32 i;
    if (!m) return;
    for (i = 0; i < XMS_HANDLES; i++)
        if (m->emb[i].used) { free_pages(m->emb[i].pg, m->emb[i].n); kfree(m->emb[i].pg); }
    for (i = 0; i < EMS_HANDLES; i++)
        if (m->eh[i].used && m->eh[i].pg) { free_pages(m->eh[i].pg, m->eh[i].n * 4); kfree(m->eh[i].pg); }
    if (m->hma_pg) { free_pages(m->hma_pg, 16); kfree(m->hma_pg); }
    kfree(m);
    v->mm = NULL;
}

/* n pages into a new array (all or none) */
static u32 *get_pages(u32 n)
{
    u32 *pg, i;
    if (n > pages_free()) return NULL;
    pg = kmalloc((n ? n : 1) * 4);
    if (!pg) return NULL;
    for (i = 0; i < n; i++)
        if ((pg[i] = page_alloc()) == 0) { free_pages(pg, i); kfree(pg); return NULL; }
    return pg;
}

/* resize an array of pages from n to nn (kept pages first) */
static u32 *resize_pages(u32 *pg, u32 n, u32 nn)
{
    u32 *np, i;
    if (nn <= n) {
        free_pages(pg + nn, n - nn);
        return pg;
    }
    if (nn - n > pages_free()) return NULL;
    np = kmalloc(nn * 4);
    if (!np) return NULL;
    for (i = 0; i < n; i++) np[i] = pg[i];
    for (; i < nn; i++)
        if ((np[i] = page_alloc()) == 0) { free_pages(np + n, i - n); kfree(np); return NULL; }
    kfree(pg);
    return np;
}

/* ---- A20 and the HMA: the VM's page table at 100000h-10FFFFh is the
 * HMA (A20 on) or the first 64 KB again (off) ---- */
static void a20_apply(struct vm *v)
{
    struct mmvm *m = v->mm;
    u32 *pt = ptab(v), i;
    int on = m->a20_fixed || m->a20_global || m->a20_local > 0;
    /* (the HMA stays mapped until a program turns A20 off: Windows 3.1's
     * query said off at first, but DOS kept in the HMA would not survive
     * a wrap the VM never asked for: UNVERIFIED) */
    if (!on && !m->wrap) {
        for (i = 0; i < 16; i++) pt[0x100 + i] = pt[i];
        m->wrap = 1;
    } else if (on && m->wrap) {
        for (i = 0; i < 16; i++) pt[0x100 + i] = m->hma_pte[i];
        m->wrap = 0;
    }
    if (v == cur_vm) vmm_flush_tlb();
}

/* ---- XMS ---- */
static u32 xms_free_kb(struct mmvm *m)
{
    u32 phys = pages_free() * 4, left;
    left = m->xms_lim == 0xFFFFFFFCu ? phys : (m->xms_lim > m->xms_used ? m->xms_lim - m->xms_used : 0);
    return left < phys ? left : phys;
}

static struct emb *emb_of(struct mmvm *m, u32 h)
{
    if (h < 1 || h > XMS_HANDLES || !m->emb[h - 1].known) return NULL;
    return &m->emb[h - 1];
}

/* the linear address of a move's end: handle 0 a seg:off in the VM, else
 * a block and an offset (each page through the VMM's window); NULL if
 * the range is not all there. The copy goes page by page (cb) */
static int xmove(struct mmvm *m, u32 len, u32 sh, u32 so, u32 dh, u32 doff, u32 *bl)
{
    struct emb *s = NULL, *d = NULL;
    u32 done = 0;
    if (sh && ((s = emb_of(m, sh)) == NULL || !s->used)) { *bl = 0xA3; return 0; }
    if (dh && ((d = emb_of(m, dh)) == NULL || !d->used)) { *bl = 0xA5; return 0; }
    if (s && so >= s->kb * 1024 && len) { *bl = 0xA4; return 0; }
    if (d && doff >= d->kb * 1024 && len) { *bl = 0xA6; return 0; }
    if ((s && so + len > s->kb * 1024) || (d && doff + len > d->kb * 1024)) { *bl = 0xA7; return 0; }
    if (!sh) so = ((so >> 16) << 4) + (so & 0xFFFF);
    if (!dh) doff = ((doff >> 16) << 4) + (doff & 0xFFFF);
    while (done < len) {
        u32 a = so + done, b = doff + done, n = len - done, k;
        u8 *pa, *pb;
        if (s) { k = 4096 - (a & 0xFFF); if (n > k) n = k; pa = (u8 *)phys_to_lin(s->pg[a >> 12]) + (a & 0xFFF); }
        else pa = (u8 *)a;
        if (d) { k = 4096 - (b & 0xFFF); if (n > k) n = k; pb = (u8 *)phys_to_lin(d->pg[b >> 12]) + (b & 0xFFF); }
        else pb = (u8 *)b;
        if ((!s && (a + n > 0x110000)) || (!d && (b + n > 0x110000))) { *bl = 0xA7; return 0; }
        memcpy(pb, pa, n);
        done += n;
    }
    return 1;
}

#define XOK(r)      SETW((r)->eax, 1), SETLO((r)->ebx, 0)
#define XERR(r, e)  SETW((r)->eax, 0), SETLO((r)->ebx, (e))

/* the XMS entry (W386_BP_XMS): AH the function (XMS 2.0) */
void v86mm_xms(struct cregs *r)
{
    struct mmvm *m = cur_vm->mm;
    struct emb *e;
    u32 i, h, kb, n;
    if (!m) { XERR(r, 0x80); return; }
    switch ((r->eax >> 8) & 0xFF) {
    case 0x00:                          /* version 2.00, revision 2.05, the HMA exists */
        SETW(r->eax, 0x0200);
        SETW(r->ebx, 0x0205);
        SETW(r->edx, 1);
        return;
    case 0x01:                          /* request the HMA */
        if (m->hma) { XERR(r, 0x91); return; }
        m->hma = 2;
        XOK(r);
        return;
    case 0x02:                          /* release it */
        if (!m->hma) { XERR(r, 0x93); return; }
        m->hma = 0;
        XOK(r);
        return;
    case 0x03: m->a20_global = 1; a20_apply(cur_vm); XOK(r); return;
    case 0x04: m->a20_global = 0; a20_apply(cur_vm); XOK(r); return;
    case 0x05: m->a20_local++; a20_apply(cur_vm); XOK(r); return;
    case 0x06: if (m->a20_local > 0) m->a20_local--; a20_apply(cur_vm); XOK(r); return;
    case 0x07:
        SETW(r->eax, (m->a20_fixed || m->a20_global || m->a20_local > 0) ? 1 : 0);
        SETLO(r->ebx, 0);
        return;
    case 0x08: {                        /* free memory: the largest, the total */
        u32 f = xms_free_kb(m), big = f;
        /* (3.1: with KB Required set, the largest is what is left of it:
         * 256 of 512, 128 after 128 KB; UNVERIFIED past it) */
        if (m->xms_req && m->xms_used < m->xms_req && m->xms_req - m->xms_used < big)
            big = m->xms_req - m->xms_used;
        if (f > 0xFFFF) f = 0xFFFF;
        if (big > 0xFFFF) big = 0xFFFF;
        SETW(r->eax, big);
        SETW(r->edx, f);
        SETLO(r->ebx, 0);
        return;
    }
    case 0x09:                          /* allocate DX KB */
        kb = DX(r);
        SETW(r->edx, 0);                /* (no handle: DX 0, as 3.1) */
        if (kb > xms_free_kb(m)) { XERR(r, 0xA0); return; }
        for (i = 0; i < XMS_HANDLES && m->emb[i].used; i++)
            ;
        if (i == XMS_HANDLES) { XERR(r, 0xA1); return; }
        n = (kb + 3) / 4;
        e = &m->emb[i];
        if ((e->pg = get_pages(n)) == NULL) { XERR(r, 0xA0); return; }
        e->known = e->used = 1;
        e->kb = kb;
        e->n = n;
        m->xms_used += kb;
        XOK(r);
        SETW(r->edx, i + 1);
        return;
    case 0x0A:                          /* free: a freed handle again is no error (3.1) */
        if ((e = emb_of(m, DX(r))) == NULL) { XERR(r, 0xA2); return; }
        if (e->used) {
            free_pages(e->pg, e->n);
            kfree(e->pg);
            m->xms_used -= e->kb;
            e->used = 0;
            e->pg = NULL;
        }
        XOK(r);
        return;
    case 0x0B: {                        /* move: DS:SI the structure */
        u8 *p = (u8 *)v86_lin(r->ds, SI(r));
        u32 bl = 0;
        if (xmove(m, *(u32 *)p, *(u16 *)(p + 4), *(u32 *)(p + 6), *(u16 *)(p + 10), *(u32 *)(p + 12), &bl))
            SETW(r->eax, 1);            /* (BL as it was: 3.1) */
        else
            XERR(r, bl);
        return;
    }
    case 0x0C:                          /* lock: Windows 3.1 refuses (ADh) */
        if ((e = emb_of(m, DX(r))) == NULL || !e->used) { XERR(r, 0xA2); return; }
        XERR(r, 0xAD);
        return;
    case 0x0D:                          /* unlock: never locked */
        if ((e = emb_of(m, DX(r))) == NULL || !e->used) { XERR(r, 0xA2); return; }
        XERR(r, 0xAA);
        return;
    case 0x0E:                          /* information: locks, free handles, size */
        if ((e = emb_of(m, DX(r))) == NULL || !e->used) { XERR(r, 0xA2); return; }
        for (h = 0, i = 0; i < XMS_HANDLES; i++) if (!m->emb[i].used) h++;
        SETW(r->eax, 1);
        SETW(r->ebx, h);                /* BH 0 locks, BL the free handles */
        SETW(r->edx, e->kb);
        return;
    case 0x0F: {                        /* resize to BX KB */
        u32 *np;
        if ((e = emb_of(m, DX(r))) == NULL || !e->used) { XERR(r, 0xA2); return; }
        kb = BX(r);
        if (kb > e->kb && kb - e->kb > xms_free_kb(m)) { XERR(r, 0xA0); return; }
        n = (kb + 3) / 4;
        if ((np = resize_pages(e->pg, e->n, n)) == NULL) { XERR(r, 0xA0); return; }
        e->pg = np;
        m->xms_used = m->xms_used - e->kb + kb;
        e->kb = kb;
        e->n = n;
        XOK(r);
        return;
    }
    case 0x10:                          /* UMBs: none (DX as it was, as 3.1) */
        XERR(r, 0xB1);
        return;
    case 0x11:
        XERR(r, 0xB2);
        return;
    }
    XERR(r, 0x80);                      /* 12h, the 3.0 functions (88h...): a 2.00 driver */
}

/* ---- EMS ---- */
static int ems_on(struct mmvm *m)
{
    return m && info->ems_frame;
}

static u32 ems_free_pages(struct mmvm *m)
{
    u32 phys = pages_free() / 4, left;
    left = m->ems_lim == 0xFFFFFFFCu ? phys : (m->ems_lim / 16 > m->ems_used ? m->ems_lim / 16 - m->ems_used : 0);
    return left < phys ? left : phys;
}

/* the free pages 42h and 5901h show: Windows 3.1 left the PIF's EMS
 * Required out (limit 256 KB, required 128: 8 of 16 free, 4 after 4
 * allocated) though a VM could still allocate up to its limit (16 1-page
 * handles). UNVERIFIED why */
static u32 ems_free_shown(struct mmvm *m)
{
    u32 f = ems_free_pages(m), r = m->ems_req / 16;
    if (m->ems_lim == 0xFFFFFFFCu) return f;
    return f > r ? f - r : 0;
}

static u32 ems_total_pages(struct mmvm *m)
{
    u32 t = m->ems_lim == 0xFFFFFFFCu ? pages_free() / 4 + m->ems_used : m->ems_lim / 16;
    return t;
}

/* frame page p (0-3) shows handle h's logical page l, or nothing (h < 0) */
static void ems_map_noflush(struct vm *v, u32 p, s32 h, u32 l)
{
    struct mmvm *m = v->mm;
    u32 *pt = ptab(v), base = (info->ems_frame >> 8) + p * 4, k;
    for (k = 0; k < 4; k++)
        pt[base + k] = h < 0 ? m->frame_pte[p * 4 + k] : (m->eh[h].pg[l * 4 + k] | 7);
    m->map[p] = h < 0 ? -1 : (h << 16) | l;
}

static void ems_map(struct vm *v, u32 p, s32 h, u32 l)
{
    ems_map_noflush(v, p, h, l);
    if (v == cur_vm) vmm_flush_tlb();
}

static struct eh *eh_of(struct mmvm *m, u32 h)
{
    return h < EMS_HANDLES && m->eh[h].used ? &m->eh[h] : NULL;
}

/* a handle's pages from n to nn */
static int eh_resize(struct mmvm *m, struct eh *e, u32 nn)
{
    u32 *np;
    if (nn > EMS_PAGES_MAX) return 0x87;
    if (nn > e->n && nn - e->n > ems_free_pages(m)) return nn - e->n > ems_total_pages(m) ? 0x87 : 0x88;
    if (!e->pg && nn) {
        if ((e->pg = get_pages(nn * 4)) == NULL) return 0x88;
    } else if (e->pg) {
        if ((np = resize_pages(e->pg, e->n * 4, nn * 4)) == NULL) return 0x88;
        e->pg = np;
    }
    m->ems_used = m->ems_used - e->n + nn;
    e->n = nn;
    return 0;
}

/* pages unmapped that a resize or free took away */
static void eh_unmap_from(struct vm *v, u32 h, u32 from)
{
    u32 p;
    int changed = 0;
    for (p = 0; p < 4; p++)
        if (v->mm->map[p] >= 0 && (u32)v->mm->map[p] >> 16 == h && ((u32)v->mm->map[p] & 0xFFFF) >= from) {
            ems_map_noflush(v, p, -1, 0);
            changed = 1;
        }
    if (changed && v == cur_vm) vmm_flush_tlb();
}

#define EAH(r, v)   SETHI((r)->eax, (v))

static int ems_alloc(struct cregs *r, struct mmvm *m, u32 n, int raw)
{
    u32 h;
    int e;
    if (!ems_total_pages(m) || n > ems_total_pages(m)) { EAH(r, 0x87); return 1; }   /* (3.1: 87h before 89h with a limit of 0) */
    if (!n && !raw) { EAH(r, 0x89); return 1; }
    if (n > ems_total_pages(m)) { EAH(r, 0x87); return 1; }
    if (n > ems_free_pages(m)) { EAH(r, 0x88); return 1; }
    for (h = 1; h < EMS_HANDLES && m->eh[h].used; h++)
        ;
    if (h == EMS_HANDLES) { EAH(r, 0x85); return 1; }
    memset(&m->eh[h], 0, sizeof(m->eh[h]));
    if ((e = eh_resize(m, &m->eh[h], n)) != 0) { EAH(r, e); return 1; }
    m->eh[h].used = 1;
    SETW(r->edx, h);
    EAH(r, 0);
    return 1;
}

/* INT 67h in a DOS VM with EMS: 1 if done */
int v86mm_ems(struct cregs *r)
{
    struct vm *v = cur_vm;
    struct mmvm *m = v->mm;
    struct eh *e;
    u32 i, p, l, h, al = r->eax & 0xFF;
    u8 *buf;
    if (!ems_on(m)) return 0;
    switch ((r->eax >> 8) & 0xFF) {
    case 0x40: EAH(r, 0); return 1;                     /* status */
    case 0x41: SETW(r->ebx, info->ems_frame); EAH(r, 0); return 1;
    case 0x42:                                          /* free, total pages */
        SETW(r->ebx, ems_free_shown(m));
        SETW(r->edx, ems_total_pages(m));
        EAH(r, 0);
        return 1;
    case 0x43: return ems_alloc(r, m, BX(r), 0);
    case 0x44:                                          /* map AL physical <- BX logical */
        if ((e = eh_of(m, DX(r))) == NULL) { EAH(r, 0x83); return 1; }
        if (al > 3) { EAH(r, 0x8B); return 1; }
        if (BX(r) == 0xFFFF) ems_map(v, al, -1, 0);
        else if (BX(r) >= e->n) { EAH(r, 0x8A); return 1; }
        else ems_map(v, al, DX(r), BX(r));
        EAH(r, 0);
        return 1;
    case 0x45:                                          /* free */
        h = DX(r);
        if ((e = eh_of(m, h)) == NULL) { EAH(r, 0x83); return 1; }
        if (e->saved) { EAH(r, 0x86); return 1; }
        eh_unmap_from(v, h, 0);
        eh_resize(m, e, 0);
        if (e->pg) kfree(e->pg);
        e->pg = NULL;
        if (h) e->used = 0;
        EAH(r, 0);
        return 1;
    case 0x46: SETW(r->eax, 0x0040); return 1;          /* LIM 4.0 */
    case 0x47:                                          /* save the map for DX */
        if ((e = eh_of(m, DX(r))) == NULL) { EAH(r, 0x83); return 1; }
        if (e->saved) { EAH(r, 0x8D); return 1; }
        for (p = 0; p < 4; p++) e->save[p] = m->map[p];
        e->saved = 1;
        EAH(r, 0);
        return 1;
    case 0x48:                                          /* restore it */
        if ((e = eh_of(m, DX(r))) == NULL) { EAH(r, 0x83); return 1; }
        if (!e->saved) { EAH(r, 0x8E); return 1; }
        for (p = 0; p < 4; p++)
            ems_map_noflush(v, p, e->save[p] < 0 || !eh_of(m, (u32)e->save[p] >> 16) ? -1 : (s32)((u32)e->save[p] >> 16),
                    (u32)e->save[p] & 0xFFFF);
        if (v == cur_vm) vmm_flush_tlb();
        e->saved = 0;
        EAH(r, 0);
        return 1;
    case 0x4B:                                          /* handles open */
        for (h = 0, i = 0; i < EMS_HANDLES; i++) if (m->eh[i].used) h++;
        SETW(r->ebx, h);
        EAH(r, 0);
        return 1;
    case 0x4C:
        if ((e = eh_of(m, DX(r))) == NULL) { EAH(r, 0x83); return 1; }
        SETW(r->ebx, e->n);
        EAH(r, 0);
        return 1;
    case 0x4D:                                          /* ES:DI: handle, pages each */
        buf = (u8 *)v86_lin(r->es, DI(r));
        for (h = 0, i = 0; i < EMS_HANDLES; i++)
            if (m->eh[i].used) { *(u16 *)(buf + h * 4) = (u16)i; *(u16 *)(buf + h * 4 + 2) = (u16)m->eh[i].n; h++; }
        SETW(r->ebx, h);
        EAH(r, 0);
        return 1;
    case 0x4E:                                          /* the whole map (16 bytes) */
    case 0x4F: {                                        /* partial: done as the whole */
        u32 get, set;
        if ((((r->eax >> 8) & 0xFF) == 0x4E && al == 3) || (((r->eax >> 8) & 0xFF) == 0x4F && al == 2)) {
            SETLO(r->eax, 16);                          /* the size */
            EAH(r, 0);
            return 1;
        }
        if (al > 2 || (((r->eax >> 8) & 0xFF) == 0x4F && al > 1)) { EAH(r, 0x8F); return 1; }
        get = al == 0 || al == 2;
        set = al == 1 || al == 2;
        if (get) {
            buf = (u8 *)v86_lin(r->es, DI(r));
            for (p = 0; p < 4; p++) *(s32 *)(buf + p * 4) = m->map[p];
        }
        if (set) {
            buf = (u8 *)v86_lin(r->ds, SI(r));
            for (p = 0; p < 4; p++) {
                s32 x = *(s32 *)(buf + p * 4);
                ems_map_noflush(v, p, x < 0 || !eh_of(m, (u32)x >> 16) || ((u32)x & 0xFFFF) >= m->eh[(u32)x >> 16].n
                        ? -1 : (s32)((u32)x >> 16), (u32)x & 0xFFFF);
            }
            if (v == cur_vm) vmm_flush_tlb();
        }
        EAH(r, 0);
        return 1;
    }
    case 0x50: {                                        /* map several: DS:SI CX pairs */
        int changed = 0;
        if ((e = eh_of(m, DX(r))) == NULL) { EAH(r, 0x83); return 1; }
        if (al > 1) { EAH(r, 0x8F); return 1; }
        buf = (u8 *)v86_lin(r->ds, SI(r));
        for (i = 0; i < CX(r); i++) {
            l = *(u16 *)(buf + i * 4);
            p = *(u16 *)(buf + i * 4 + 2);
            if (al == 1) p = (p - info->ems_frame) >> 10;   /* a segment */
            if (p > 3) { if (changed && v == cur_vm) vmm_flush_tlb(); EAH(r, 0x8B); return 1; }
            if (l == 0xFFFF) { ems_map_noflush(v, p, -1, 0); changed = 1; }
            else if (l >= e->n) { if (changed && v == cur_vm) vmm_flush_tlb(); EAH(r, 0x8A); return 1; }
            else { ems_map_noflush(v, p, DX(r), l); changed = 1; }
        }
        if (changed && v == cur_vm) vmm_flush_tlb();
        EAH(r, 0);
        return 1;
    }
    case 0x51: {                                        /* resize to BX pages */
        int err;
        if ((e = eh_of(m, DX(r))) == NULL) { EAH(r, 0x83); return 1; }
        eh_unmap_from(v, DX(r), BX(r));
        if ((err = eh_resize(m, e, BX(r))) != 0) { EAH(r, err); return 1; }
        SETW(r->ebx, e->n);
        EAH(r, 0);
        return 1;
    }
    case 0x52:                                          /* attributes: not supported */
        /* (3.1: 5200h AH 91h; UNVERIFIED 5201h, 5202h) */
        EAH(r, al > 2 ? 0x8F : 0x91);
        return 1;
    case 0x53:                                          /* the handle's name */
        if ((e = eh_of(m, DX(r))) == NULL) { EAH(r, 0x83); return 1; }
        if (al == 0) memcpy((void *)v86_lin(r->es, DI(r)), e->name, 8);
        else if (al == 1) {
            char nm[8];
            memcpy(nm, (void *)v86_lin(r->ds, SI(r)), 8);
            for (i = 0; i < EMS_HANDLES; i++)
                if (m->eh[i].used && &m->eh[i] != e && !memcmp(m->eh[i].name, nm, 8)
                    && memcmp(nm, "\0\0\0\0\0\0\0\0", 8)) { EAH(r, 0xA1); return 1; }
            memcpy(e->name, nm, 8);
        } else { EAH(r, 0x8F); return 1; }
        EAH(r, 0);
        return 1;
    case 0x54:                                          /* the handle directory */
        if (al == 0) {
            buf = (u8 *)v86_lin(r->es, DI(r));
            for (h = 0, i = 0; i < EMS_HANDLES; i++)
                if (m->eh[i].used) { *(u16 *)(buf + h * 10) = (u16)i; memcpy(buf + h * 10 + 2, m->eh[i].name, 8); h++; }
            SETLO(r->eax, h);
        } else if (al == 1) {
            u8 *nm = (u8 *)v86_lin(r->ds, SI(r));
            if (!memcmp(nm, "\0\0\0\0\0\0\0\0", 8)) { EAH(r, 0xA1); return 1; }
            for (i = 0; i < EMS_HANDLES; i++)
                if (m->eh[i].used && !memcmp(m->eh[i].name, nm, 8)) break;
            if (i == EMS_HANDLES) { EAH(r, 0xA0); return 1; }
            SETW(r->edx, i);
        } else if (al == 2) SETW(r->ebx, EMS_HANDLES);
        else { EAH(r, 0x8F); return 1; }
        EAH(r, 0);
        return 1;
    case 0x57: {                                        /* move/exchange a region: DS:SI */
        u8 *d = (u8 *)v86_lin(r->ds, SI(r));
        u32 len = *(u32 *)d, k;
        u8 *src, *dst;
        if (al > 1) { EAH(r, 0x8F); return 1; }
        for (k = 0; k < len; k++) {
            u8 *ends[2];
            u32 side;
            for (side = 0; side < 2; side++) {
                u8 *q = d + 4 + side * 7;
                u32 type = q[0], hh = *(u16 *)(q + 1), off = *(u16 *)(q + 3), seg = *(u16 *)(q + 5);
                if (type == 0) ends[side] = (u8 *)(seg * 16 + off + k);
                else {
                    u32 a = off + k, pg = seg + (a >> 14);
                    if ((e = eh_of(m, hh)) == NULL) { EAH(r, 0x83); return 1; }
                    if (pg >= e->n) { EAH(r, 0x93); return 1; }
                    a &= 0x3FFF;
                    ends[side] = (u8 *)phys_to_lin(e->pg[pg * 4 + (a >> 12)]) + (a & 0xFFF);
                }
            }
            src = ends[0];
            dst = ends[1];
            if (al == 0) *dst = *src;
            else { u8 t = *dst; *dst = *src; *src = t; }
        }
        EAH(r, 0);
        return 1;
    }
    case 0x58:                                          /* the mappable pages: the frame */
        if (al == 1) { SETW(r->ecx, 4); EAH(r, 0); return 1; }
        if (al) { EAH(r, 0x8F); return 1; }
        buf = (u8 *)v86_lin(r->es, DI(r));
        for (p = 0; p < 4; p++) {
            *(u16 *)(buf + p * 4) = (u16)(info->ems_frame + p * 0x400);
            *(u16 *)(buf + p * 4 + 2) = (u16)p;
        }
        SETW(r->ecx, 4);
        EAH(r, 0);
        return 1;
    case 0x59:
        if (al == 1) {                                  /* raw pages: free, total */
            SETW(r->ebx, ems_free_pages(m));
            SETW(r->edx, ems_total_pages(m));
            EAH(r, 0);
            return 1;
        }
        if (al == 0) {                                  /* hardware: 16 KB pages, no alternate sets */
            buf = (u8 *)v86_lin(r->es, DI(r));
            *(u16 *)buf = 0x0400;
            *(u16 *)(buf + 2) = 0;
            *(u16 *)(buf + 4) = 16;
            *(u16 *)(buf + 6) = 0;
            *(u16 *)(buf + 8) = 0;
            EAH(r, 0);
            return 1;
        }
        EAH(r, 0x8F);
        return 1;
    case 0x5A: return ems_alloc(r, m, BX(r), 1);        /* raw pages: 0 allowed */
    case 0x5C: EAH(r, 0); return 1;                     /* prepare for a warm boot */
    }
    EAH(r, 0x84);                                       /* not here (55h, 56h, 5Bh, 5Dh...) */
    return 1;
}

/* INT 2Fh 43xxh in a DOS VM: 1 if answered */
int v86mm_int2f(struct cregs *r)
{
    if (cur_vm == &sysvm || !cur_vm->mm) return 0;
    if (AX(r) == 0x4300) { SETLO(r->eax, 0x80); return 1; }
    if (AX(r) == 0x4310) {
        r->es = info->ldr_cs;
        SETW(r->ebx, info->bp_off + W386_BP_XMS);
        return 1;
    }
    return 0;
}

/* ---- V86MMGR's services (the DDK's descriptions; numbers in the DDK's
 * order: 0 Get_Version, 1 Allocate_V86_Pages, 2 Set_EMS_XMS_Limits,
 * 3 Get_EMS_XMS_Limits, 4 Set_Mapping_Info, 5 Get_Mapping_Info,
 * 6 Xlat_API, 7 Load_Client_Ptr, 8 Allocate_Buffer, 9 Free_Buffer,
 * 0Ah Get_Xlat_Buff_State, 0Bh Set_Xlat_Buff_State, 0Ch Get_VM_Flat_Sel,
 * 0Dh Map_Pages, 0Eh Free_Page_Map_Region, 0Fh LocalGlobalReg,
 * 10h GetPgStatus, 11h SetLocalA20, 12h ResetBasePages, 13h
 * SetAvailMapPgs) ---- */
int v86mm_service(u32 svc, struct svcregs *s)
{
    struct vm *v;
    struct mmvm *m;
    switch (svc) {
    case 0x00:                          /* EAX 030Ah, carry clear (3.1, VXT) */
        s->eax = 0x030A;
        return 1;
    case 0x01:                          /* Allocate_V86_Pages: the VM's memory is the VMM's (vm.c) */
        s->eax = (u32)info->mem_top >> 8;
        return 1;
    case 0x02:                          /* Set_EMS_XMS_Limits: EAX, EDX EMS, ESI, EDI XMS (KB) */
        if ((v = vm_of(s->ebx)) == NULL || v == &sysvm || (m = v->mm) == NULL) { s->eflags |= EFL_CF; return 1; }
        if (s->eax != 0xFFFFFFFFu || s->edx != 0xFFFFFFFFu) { m->ems_req = s->eax & ~3u; m->ems_lim = s->edx & ~3u; }
        if (s->esi != 0xFFFFFFFFu || s->edi != 0xFFFFFFFFu) { m->xms_req = s->esi & ~3u; m->xms_lim = s->edi & ~3u; }
        return 1;
    case 0x03:                          /* Get_EMS_XMS_Limits */
        if ((v = vm_of(s->ebx)) == NULL) { s->eflags |= EFL_CF; return 1; }
        if (v == &sysvm || (m = v->mm) == NULL) {
            s->eax = 0; s->edx = sys_lim; s->esi = 0; s->edi = sys_lim;
        } else {
            s->eax = m->ems_req; s->edx = m->ems_lim; s->esi = m->xms_req; s->edi = m->xms_lim;
        }
        s->ecx = 1;
        return 1;
    case 0x04:                          /* Set_Mapping_Info: noted, nothing to reserve */
        return 1;
    case 0x05:                          /* Get_Mapping_Info: 3.1's (CH 3, CL 0) */
        s->ecx = 0x0300;
        return 1;
    case 0x0A:                          /* Get_Xlat_Buff_State: the VMM's transfer buffer */
        s->eax = xbuf_seg_set ? xbuf_seg_set : info->xbuf_seg;
        s->ecx = xbuf_seg_set ? xbuf_free_set : xbuf_size();
        s->edx = xbuf_seg_set ? xbuf_size_set : xbuf_size();
        return 1;
    case 0x0B:                          /* Set_Xlat_Buff_State */
        xbuf_seg_set = s->eax & 0xFFFF;
        xbuf_free_set = s->ecx;
        xbuf_size_set = s->edx;
        return 1;
    case 0x0F:                          /* LocalGlobalReg: no UMBs, every region local */
        s->eax = 0;
        return 1;
    case 0x10: {                        /* GetPgStatus: ECX the page */
        u32 pg = s->ecx;
        s->eax = 0;
        if (info->ems_frame && pg >= (u32)info->ems_frame >> 8 && pg < ((u32)info->ems_frame >> 8) + 16) s->eax = 0x80;
        else if (pg >= 0x100 && pg < 0x110) s->eax = 0x100;
        return 1;
    }
    case 0x11:                          /* SetLocalA20: A20 is each VM's here already */
        return 1;
    }
    /* Xlat_API (the script's encoding is V86MMGR.INC's, not documented),
     * the buffer and mapper services: carry, traced. UNVERIFIED */
    trace("VMM: V86MMGR service %x is not implemented (called from %x)\n", svc, *(u32 *)(s + 1));
    s->eflags |= EFL_CF;
    return 1;
}
