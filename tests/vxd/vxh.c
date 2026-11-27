/* vxh.c - VXH.386, a probe VxD of w16's own (test only) for the VMM's
 * memory, paging and page-hooking services (docs/notes/
 * win386-memsvc-audit.md, ARCHITECTURE.md 12.19). Loaded with
 * "device=vxh.386" (init order: the last) under Windows 3.1's WIN386 or
 * w16's, it writes one line per result to COM1 (DOSBox-X serial1=file),
 * every line starting "VXH ". A token starting '~' is a value that
 * depends on the WIN386's own layout (an address, a handle, a count of
 * free pages): tests/memsvc.sh leaves those out when it compares.
 *   Device_Init: _GetFirstV86Page, _GetLastV86Page, the global V86 page
 *     assignments; a page to hook (the first one nobody assigned in
 *     B0h-B7h, else C8h-EFh): _Assign_Device_V86_Pages, Hook_V86_Page
 *     (and its errors); _PageAllocate of each kind and flag, _PageGetSizeAddr,
 *     _PageReAllocate, _PageLock/_PageUnLock and their counts, _PageFree,
 *     _PageCheckLinRange, _LinPageLock/_LinPageUnLock, _CopyPageTable,
 *     _MapPhysToLinear, _GetNulPageHandle, _SelectorMapFlat,
 *     _TestGlobalV86Mem, the V86 data areas, _Allocate_Device_CB_Area,
 *     _AddFreePhysPage, Hook_V86_Fault (fault 06h).
 *   Init_Complete: _ModifyPageBits and its errors on the System VM, the
 *     System VM's first megabyte as _CopyPageTable gives it (runs of
 *     entries: present, writable, user, the page's own physical address),
 *     _GetV86PageableArray/_SetResetV86Pageable, PageLockedIfDP.
 *   VM_Init of a DOS VM: the hooked page's entry in it (then made not
 *     present with _ModifyPageBits, PG_HOOKED), local assignments.
 *   The hooked page's callback: EAX the page, EBX the VM: logs, then maps
 *     a PG_HOOKED page of the VM's own (its ID in every byte) with
 *     _MapIntoV86.
 *   The V86 API (PGT.COM, tests/dos/pgt.asm, in DOS boxes): the services
 *     in a DOS VM (Client_AX the function: the list in vxh_api).
 *   Destroy_VM: the VM's page freed.
 * Sources: the Windows 3.1 DDK's Virtual Device Adaptation Guide (the
 * memory management services, Hook_V86_Page, Hook_V86_Fault,
 * Map_Lin_To_VM_Addr; the flag values those services take, as the DDK's
 * VMM.INC defines them for a VxD's source); the Intel 80386 manual (page
 * table entries). Nothing here comes from Microsoft's code.
 */
#include <stdarg.h>
#include "w16vxd.h"

#pragma data_seg("_TEXT", "CODE")

static vu32 inb(vu32 port);
static void outb(vu32 port, vu32 v);
static vu32 get_cs(void);
static vu32 get_ds(void);
#pragma aux inb = "in al, dx" "movzx eax, al" parm [edx] value [eax] modify exact [eax];
#pragma aux outb = "out dx, al" parm [edx] [eax] modify exact [];
#pragma aux get_cs = "mov eax, cs" value [eax] modify exact [eax];
#pragma aux get_ds = "mov eax, ds" value [eax] modify exact [eax];

/* the DDK's flag values (VMM.INC) */
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
#define PageSetV86Pageable   0x00000200u
#define PageClearV86Pageable 0x00000400u
#define PageMarkPageOut 0x00002000u
#define PageDiscard     0x00010000u
#define PagePDPQueryDirty 0x00020000u
#define PageMapFreePhysReg 0x00040000u
#define P_PRES  1u
#define P_WRITE 2u
#define P_USER  4u
#define P_ACC   0x20u
#define P_DIRTY 0x40u
#define GVDAParaAlign   0x00000004u
#define GVDAPageAlign   0x00000008u
#define GVDAZeroInit    0x00000200u
#define GVDAInquire     0x00000800u

vu32 vxh_edx = 1, vxh_fl = 1;           /* (CCALL: EDX and the flags after a call) */
vu32 vxh_ud_prev = 1;
#define C(x) vu32 __cdecl x
C(_PageAllocate)(vu32, vu32, vu32, vu32, vu32, vu32, vu32, vu32);
C(_PageReAllocate)(vu32, vu32, vu32);
C(_PageFree)(vu32, vu32);
C(_PageLock)(vu32, vu32, vu32, vu32);
C(_PageUnLock)(vu32, vu32, vu32, vu32);
C(_PageGetSizeAddr)(vu32, vu32);
C(_PageGetAllocInfo)(vu32);
C(_GetFreePageCount)(vu32);
C(_GetSysPageCount)(vu32);
C(_GetVMPgCount)(vu32, vu32);
C(_MapIntoV86)(vu32, vu32, vu32, vu32, vu32, vu32);
C(_PhysIntoV86)(vu32, vu32, vu32, vu32, vu32);
C(_TestGlobalV86Mem)(vu32, vu32, vu32);
C(_ModifyPageBits)(vu32, vu32, vu32, vu32, vu32, vu32, vu32);
C(_CopyPageTable)(vu32, vu32, vu32, vu32);
C(_LinMapIntoV86)(vu32, vu32, vu32, vu32, vu32);
C(_LinPageLock)(vu32, vu32, vu32);
C(_LinPageUnLock)(vu32, vu32, vu32);
C(_SetResetV86Pageable)(vu32, vu32, vu32, vu32);
C(_GetV86PageableArray)(vu32, vu32, vu32);
C(_PageCheckLinRange)(vu32, vu32, vu32);
C(_PageOutDirtyPages)(vu32, vu32);
C(_PageDiscardPages)(vu32, vu32, vu32, vu32);
C(_GetNulPageHandle)(void);
C(_GetFirstV86Page)(void);
C(_MapPhysToLinear)(vu32, vu32, vu32);
C(_SelectorMapFlat)(vu32, vu32, vu32);
C(_GetDemandPageInfo)(vu32, vu32);
C(_GetSetPageOutCount)(vu32, vu32);
C(_Assign_Device_V86_Pages)(vu32, vu32, vu32, vu32);
C(_DeAssign_Device_V86_Pages)(vu32, vu32, vu32, vu32);
C(_Get_Device_V86_Pages_Array)(vu32, vu32, vu32);
C(_Allocate_Device_CB_Area)(vu32, vu32);
C(_Allocate_Global_V86_Data_Area)(vu32, vu32);
C(_Allocate_Temp_V86_Data_Area)(vu32, vu32);
C(_Free_Temp_V86_Data_Area)(void);
C(_AddFreePhysPage)(vu32, vu32, vu32);
C(_GetLastV86Page)(void);
#define R(x) void __cdecl x(struct vregs *)
R(Get_Cur_VM_Handle); R(Get_Sys_VM_Handle); R(Map_Lin_To_VM_Addr);
R(Get_System_Time); R(Hook_V86_Page); R(Hook_V86_Fault); R(Get_Profile_Decimal_Int);
extern char vxh_hook[], vxh_hook2[], vxh_ud[];
#pragma aux vxh_hook "*"
#pragma aux vxh_hook2 "*"
#pragma aux vxh_ud "*"

/* ---- COM1 ---- */
static void putc_(int c)
{
    int i;
    for (i = 0; i < 100000 && !(inb(0x3FD) & 0x20); i++) ;
    outb(0x3F8, c);
}
static void hex(vu32 v, int n)
{
    int i;
    for (i = n - 1; i >= 0; i--) putc_("0123456789ABCDEF"[(v >> (i * 4)) & 15]);
}
static vu32 lines = 1;
/* %x 8 hex digits, %w 4, %b 2, %s a string */
static void LOG(const char *f, ...)
{
    va_list a;
    const char *s;
    if (lines > 4001) return;
    va_start(a, f);
    lines++;
    putc_('V'); putc_('X'); putc_('H'); putc_(' ');
    for (; *f; f++) {
        if (*f == '%') {
            f++;
            if (*f == 'b') hex(va_arg(a, vu32), 2);
            else if (*f == 'w') hex(va_arg(a, vu32), 4);
            else if (*f == 's') for (s = va_arg(a, const char *); *s; s++) putc_(*s);
            else hex(va_arg(a, vu32), 8);
        } else putc_(*f);
    }
    putc_('\r'); putc_('\n');
    va_end(a);
}

static void clr(struct vregs *r)
{
    vu32 *p = (vu32 *)r, i;
    for (i = 0; i < sizeof(*r) / 4; i++) p[i] = 0;
}
#define CF(r) ((r)->eflags & VF_CF ? 1u : 0u)
static vu32 nz(vu32 v) { return v != 0; }

/* ---- state (initialised: BSS is not zeroed by every loader) ---- */
static vu32 sys_vm = 1, hpage = 1, first_pg = 1, last_pg = 1;
static volatile vu32 hooks = 1;
static vu32 skip = 1;               /* [386Enh] VXHSkip: groups left out (a debugging aid) */
static vu32 quiet = 1, blk_sys = 1, blk_sys_lin = 1;
static vu32 vmblk[64] = { 1 };          /* each VM's PG_HOOKED page (by VM ID) */
static vu32 vmblk_lin[64] = { 1 };
static vu32 maplk[64] = { 1 };          /* each VM's _MapIntoV86 page */
static vu32 pt[0x110] = { 1 };          /* a _CopyPageTable buffer */
static vu8 arr[64] = { 1 };
static vu32 hook2_calls = 1;

static vu32 vmid(vu32 vm) { return vm ? CB_VMID(vm) : 0; }
static vu32 cur_vm(void) { struct vregs s; clr(&s); Get_Cur_VM_Handle(&s); return s.ebx; }

/* one page table entry as the DDK describes it: present, writable, user,
 * accessed, dirty; the system bits (9-11: the page's type under Windows
 * 3.1) and the physical page left out of the comparison */
static void pte(const char *what, vu32 e)
{
    LOG("%s p%b w%b u%b a%b d%b ~t%b ~ph%x", what, e & 1, (e >> 1) & 1, (e >> 2) & 1,
        (e >> 5) & 1, (e >> 6) & 1, (e >> 9) & 7, e >> 12);
}
/* the same for a page whose accessed and dirty bits come from what ran
 * before (the BIOS, DOS, Windows): those left out */
static void ptex(const char *what, vu32 e)
{
    LOG("%s p%b w%b u%b ~a%b ~d%b ~t%b ~ph%x", what, e & 1, (e >> 1) & 1, (e >> 2) & 1,
        (e >> 5) & 1, (e >> 6) & 1, (e >> 9) & 7, e >> 12);
}
static vu32 pte_of(vu32 lin_pg)
{
    vu32 e = 0xEEEEEEEE;
    if (!_CopyPageTable(lin_pg, 1, (vu32)&e, 0)) return 0xEEEEEEEE;
    return e;
}

/* the 110h bits of an assignment array as ranges of pages */
static void pages_array_n(const char *what, vu32 n)
{
    vu32 i, j;
    for (i = 0; i < n; i = j) {
        if (!(arr[i >> 3] & (1 << (i & 7)))) { j = i + 1; continue; }
        for (j = i + 1; j < n && (arr[j >> 3] & (1 << (j & 7))); j++) ;
        LOG("%s assigned %w-%w", what, i, j - 1);
    }
}
#define pages_array(w) pages_array_n(w, 0x110)

/* a run-length picture of page table entries 0-10Fh: present, writable,
 * user, and whether the page is its own physical page (identity) */
static void ptab_runs(const char *what, vu32 base)
{
    vu32 i, j, k, ok;
    for (i = 0; i < 0x110; i++) pt[i] = 0xEEEEEEEE;
    ok = _CopyPageTable(base, 0x110, (vu32)pt, 0);
    LOG("%s _CopyPageTable %b", what, nz(ok));
    for (i = 0; i < 0x110; i = j) {
        k = pt[i] & 7;
        for (j = i + 1; j < 0x110 && (pt[j] & 7) == k
             && ((pt[j] >> 12) == j) == ((pt[i] >> 12) == i); j++) ;
        LOG("~%s-pages=%w-%w ~pwu=%b ~ident=%b", what, i, j - 1, k, (pt[i] >> 12) == i);
    }
}

/* ---- the hooked page ---- */
static void fill(vu32 lin, vu32 v)
{
    vu32 i;
    for (i = 0; i < 1024; i++) ((vu32 *)lin)[i] = v * 0x01010101u;
}

/* the VM's own page for the hooked page: PG_HOOKED, made at its first
 * fault, its ID in every byte */
static vu32 vm_page(vu32 vm)
{
    vu32 id = vmid(vm) & 63, h;
    if (vmblk[id]) return vmblk[id];
    h = _PageAllocate(1, PG_HOOKED, vm, 0, 0, 0, 0, PageZeroInit);
    LOG("hook: _PageAllocate(1, PG_HOOKED, VM %b) %b ~edx=%x", id, nz(h), vxh_edx);
    if (!h) return 0;
    vmblk[id] = h;
    vmblk_lin[id] = vxh_edx;
    fill(vxh_edx, 0x40 + id);
    return h;
}

/* Hook_V86_Page's callback: EAX the page, EBX the VM, EBP not a client */
void __cdecl vxh_hook_proc(struct vregs *r)
{
    vu32 h, ok;
    hooks++;
    if (!quiet)
        LOG("hook: page %w vm %b cur %b", r->eax, vmid(r->ebx), vmid(cur_vm()) == vmid(r->ebx));
    h = vm_page(r->ebx);
    ok = h ? _MapIntoV86(h, r->ebx, r->eax, 1, 0, 0) : 0;
    if (!quiet) {
        LOG("hook: _MapIntoV86 %b", nz(ok));
        pte("hook: after map", pte_of((CB_HIGH_LINEAR(r->ebx) >> 12) + r->eax));
    }
}

/* a second page hooked (the hooks' errors): never expected to be called */
void __cdecl vxh_hook_proc2(struct vregs *r)
{
    hook2_calls++;
    LOG("hook2: page %w vm %b", r->eax, vmid(r->ebx));
}

/* Hook_V86_Fault 06h: PGT's invalid opcode 0Fh FFh 'W' 'H' is taken and
 * skipped; others go down the chain */
static vu32 ud_seen = 1;
vu32 __cdecl vxh_ud_check(vu32 vm, struct client_regs *c)
{
    vu8 *p = (vu8 *)(((vu32)c->cs << 4) + (c->eip & 0xFFFF));
    if (p[0] != 0x0F || p[1] != 0xFF || p[2] != 'W' || p[3] != 'H') return 0;
    ud_seen++;
    LOG("ud: V86 fault 06h vm %b at the probe's opcode", vmid(vm));
    c->eip += 4;
    return 1;
}

/* ---- Device_Init ---- */
static vu32 blk_a = 1, blk_b = 1, blk_c = 1, freed = 1;

static void alloc_tests(void)
{
    vu32 h, lin, phys, n, i, ok, s0, s1, nul;
    vu32 *p;

    s0 = _GetSysPageCount(0);
    LOG("~_GetSysPageCount=%x", s0);
    LOG("~_GetFreePageCount=%x ~lockable=%x", _GetFreePageCount(0), vxh_edx);
    LOG("~_PageGetAllocInfo=%x ~lockable=%x", _PageGetAllocInfo(0), vxh_edx);

    /* a plain PG_SYS block */
    h = _PageAllocate(1, PG_SYS, 0, 0, 0, 0, 0, 0);
    lin = vxh_edx;
    LOG("_PageAllocate(1, PG_SYS, 0) h %b lin %b aligned %b ~h=%x ~lin=%x", nz(h), nz(lin),
        (lin & 0xFFF) == 0, h, lin);
    blk_a = h;
    if (lin) ((vu32 *)lin)[0] = 0x12345678;
    n = _PageGetSizeAddr(h, 0);
    LOG("_PageGetSizeAddr %x same lin %b", n, vxh_edx == lin);
    pte("PG_SYS page", pte_of(lin >> 12));

    /* zeroed and locked */
    h = _PageAllocate(3, PG_SYS, 0, 0, 0, 0, 0, PageZeroInit | PageLocked);
    lin = vxh_edx;
    blk_b = h;
    for (i = 0, ok = 1, p = (vu32 *)lin; lin && i < 3 * 1024; i++) if (p[i]) ok = 0;
    LOG("_PageAllocate(3, PG_SYS, ZeroInit|Locked) h %b zero %b", nz(h), ok);
    s1 = _GetSysPageCount(0);
    LOG("_GetSysPageCount grew by %x", s1 - s0);
    /* lock counts: 1 from PageLocked */
    LOG("_PageLock(3 at 0) %b", nz(_PageLock(h, 3, 0, 0)));
    LOG("_PageLock(4 at 0) %b", nz(_PageLock(h, 4, 0, 0)));
    LOG("_PageLock(1 at 3) %b", nz(_PageLock(h, 1, 3, 0)));
    LOG("_PageUnLock(3 at 0) %b", nz(_PageUnLock(h, 3, 0, 0)));
    LOG("_PageUnLock(3 at 0) %b", nz(_PageUnLock(h, 3, 0, 0)));
    LOG("_PageUnLock(3 at 0) a third time %b", nz(_PageUnLock(h, 3, 0, 0)));
    LOG("_PageUnLock(1 at 2) %b", nz(_PageUnLock(h, 1, 2, 0)));
    LOG("_PageLock(1 at 1) %b", nz(_PageLock(h, 1, 1, 0)));
    LOG("_PageUnLock(1 at 1) %b", nz(_PageUnLock(h, 1, 1, 0)));
    LOG("_PageCheckLinRange(3-page block, 3) %x", _PageCheckLinRange(lin >> 12, 3, 0));
    LOG("_PageCheckLinRange(3-page block + 1, 2) %x", _PageCheckLinRange((lin >> 12) + 1, 2, 0));
    LOG("_LinPageLock(3) %b", nz(_LinPageLock(lin >> 12, 3, 0)));
    LOG("_LinPageUnLock(3) %b", nz(_LinPageUnLock(lin >> 12, 3, 0)));
    LOG("_LinPageUnLock(3) again %b", nz(_LinPageUnLock(lin >> 12, 3, 0)));
    LOG("_PageCheckLinRange(7FFFF000h, 1) %x", _PageCheckLinRange(0x7FFFF, 1, 0));
    LOG("_PageCheckLinRange(page 0, 1) %x", _PageCheckLinRange(0, 1, 0));

    /* unlocked: pages present only once touched? */
    h = _PageAllocate(2, PG_SYS, 0, 0, 0, 0, 0, 0);
    lin = vxh_edx;
    pte("unlocked block page 0 before touch", pte_of(lin >> 12));
    if (lin) ((vu32 *)lin)[0] = 1;
    pte("unlocked block page 0 after touch", pte_of(lin >> 12));
    LOG("_PageFree(unlocked) %b", nz(_PageFree(h, 0)));

    /* aligned, contiguous, below 16 MB */
    phys = 0xEEEEEEEE;
    h = _PageAllocate(2, PG_SYS, 0, 0x0F, 0, 0x1000, (vu32)&phys, PageUseAlign | PageContig | PageFixed);
    lin = vxh_edx;
    blk_c = h;
    LOG("_PageAllocate(2, Align 64K, below 16MB, Contig|Fixed) h %b phys 64K %b below %b ~phys=%x",
        nz(h), (phys & 0xFFFF) == 0, phys < 0x1000000, phys);
    if (h) {
        LOG("  page 0 at phys %b page 1 contiguous %b", (pte_of(lin >> 12) & ~0xFFFu) == phys,
            (pte_of((lin >> 12) + 1) & ~0xFFFu) == phys + 0x1000);
        LOG("  _PageReAllocate(aligned) %b", nz(_PageReAllocate(h, 3, 0)));
        LOG("  _PageLock(fixed) %b _PageUnLock(fixed) %b", nz(_PageLock(h, 2, 0, 0)), nz(_PageUnLock(h, 2, 0, 0)));
        LOG("  _PageUnLock(fixed) again %b", nz(_PageUnLock(h, 2, 0, 0)));
    }
    phys = 0xEEEEEEEE;
    h = _PageAllocate(1, PG_SYS, 0, 0, 0x110, 0x1000, (vu32)&phys, PageUseAlign | PageFixed);
    LOG("_PageAllocate(1, UseAlign|Fixed, min 110h) h %b phys set %b", nz(h), phys != 0xEEEEEEEE);
    phys = 0xEEEEEEEE;
    h = _PageAllocate(1, PG_SYS, 0, 0, 0, 0x1000, (vu32)&phys, PageUseAlign);
    LOG("_PageAllocate(1, UseAlign without Fixed) h %b phys set %b", nz(h), phys != 0xEEEEEEEE);
    /* (below 1 MB, maxPhys 100h: Windows 3.1 took a page of the System
     * VM's conventional memory, 9Eh, out of it, and Windows ended before
     * KRNL386 ran: not tried) */
    h = _PageAllocate(1, PG_SYS, 0, 0, 0, 0, 0, PageContig);
    LOG("_PageAllocate(1, Contig without UseAlign) h %b", nz(h));
    if (h) _PageFree(h, 0);

    /* the errors */
    h = _PageAllocate(0, PG_SYS, 0, 0, 0, 0, 0, 0);
    LOG("_PageAllocate(0 pages) %x %x", h, vxh_edx);
    h = _PageAllocate(1, PG_VM, 0, 0, 0, 0, 0, 0);
    LOG("_PageAllocate(PG_VM, VM 0) h %b", nz(h));
    if (h) _PageFree(h, 0);
    h = _PageAllocate(1, PG_VM, sys_vm, 0, 0, 0, 0, 0);
    LOG("_PageAllocate(PG_VM, System VM) h %b", nz(h));
    if (h) LOG("  _PageFree %b", nz(_PageFree(h, 0)));
    h = _PageAllocate(1, PG_SYS, sys_vm, 0, 0, 0, 0, 0);
    LOG("_PageAllocate(PG_SYS, System VM) h %b", nz(h));
    if (h) _PageFree(h, 0);
    h = _PageAllocate(1, PG_HOOKED, sys_vm, 0, 0, 0, 0, 0);
    LOG("_PageAllocate(PG_HOOKED, System VM) h %b", nz(h));
    if (h) _PageFree(h, 0);
    h = _PageAllocate(1, 5, 0, 0, 0, 0, 0, 0);
    LOG("_PageAllocate(type 5) h %b", nz(h));
    if (h) _PageFree(h, 0);
    /* (a bad VM handle ended Windows 3.1: not tried) */
    h = _PageAllocate(0x100000, PG_SYS, 0, 0, 0, 0, 0, 0);
    LOG("_PageAllocate(100000h pages) h %b", nz(h));
    if (h) _PageFree(h, 0);
    h = _PageAllocate(1, PG_SYS, 0, 0, 0, 0, 0, PageLockedIfDP);
    LOG("_PageAllocate(LockedIfDP) at Device_Init h %b", nz(h));
    if (h) _PageFree(h, 0);

    /* the free physical region */
    h = _PageAllocate(4, PG_SYS, 0, 0, 0, 0, 0, PageMapFreePhysReg);
    lin = vxh_edx;
    LOG("_PageAllocate(4, MapFreePhysReg) h %b", nz(h));
    if (h) {
        pte("  free phys region page 0", pte_of(lin >> 12));
        LOG("  _PageGetSizeAddr %x", _PageGetSizeAddr(h, 0));
        LOG("  _PageFree %b", nz(_PageFree(h, 0)));
    }

    /* reallocation: contents kept, new pages zeroed, the old handle */
    h = _PageReAllocate(blk_a, 3, PageZeroInit);
    lin = vxh_edx;
    LOG("_PageReAllocate(1 -> 3, ZeroInit) h %b ~same=%b", nz(h), h == blk_a);
    if (h) {
        LOG("  kept %b new zero %b size %x", ((vu32 *)lin)[0] == 0x12345678,
            ((vu32 *)lin)[1024] == 0 && ((vu32 *)lin)[2047] == 0 && ((vu32 *)lin)[3071] == 0,
            _PageGetSizeAddr(h, 0));
        if (h != blk_a) LOG("  old handle's size %x", _PageGetSizeAddr(blk_a, 0));
        ((vu32 *)lin)[2048] = 0x5A5A5A5A;
        n = _PageReAllocate(h, 2, PageZeroReInit);
        LOG("  _PageReAllocate(3 -> 2, ZeroReInit) h %b zero %b", nz(n), n && ((vu32 *)vxh_edx)[0] == 0);
        if (n) h = n;
        blk_a = h;
        n = _PageReAllocate(h, 0, 0);
        LOG("  _PageReAllocate(0 pages) %x", n);
    }

    /* freeing */
    h = _PageAllocate(1, PG_SYS, 0, 0, 0, 0, 0, PageFixed);
    LOG("_PageFree(fixed) %b", nz(_PageFree(h, 0)));
    freed = h;
    h = _PageAllocate(1, PG_SYS, 0, 0, 0, 0, 0, PageLocked);
    LOG("_PageFree(locked) %b", nz(_PageFree(h, 0)));
    nul = _GetNulPageHandle();
    LOG("_GetNulPageHandle %b ~h=%x", nz(nul), nul);
    LOG("  _PageGetSizeAddr(nul) %x lin %b", _PageGetSizeAddr(nul, 0), nz(vxh_edx));
    LOG("  _PageLock(nul) %b", nz(_PageLock(nul, 1, 0, 0)));

    /* a PG_SYS page for _LinMapIntoV86 later */
    blk_sys = _PageAllocate(1, PG_SYS, 0, 0, 0, 0, 0, PageLocked | PageZeroInit);
    blk_sys_lin = vxh_edx;
    if (blk_sys_lin) fill(blk_sys_lin, 0x5C);
}

static void map_tests(void)
{
    vu32 a, b, s;
    a = _MapPhysToLinear(0xA0000, 0x10000, 0);
    LOG("_MapPhysToLinear(A0000h, 64K) ok %b phys A0 %b phys AF %b ~lin=%x", a != 0xFFFFFFFF,
        (pte_of(a >> 12) & ~0xFFFu) == 0xA0000, (pte_of((a >> 12) + 15) & ~0xFFFu) == 0xAF000, a);
    b = _MapPhysToLinear(0xA8000, 0x100, 0);
    LOG("_MapPhysToLinear(A8000h) = first + 8000h %b", b == a + 0x8000);
    b = _MapPhysToLinear(0x100000, 0x1000, 0);
    LOG("_MapPhysToLinear(100000h) ok %b phys %b", b != 0xFFFFFFFF, (pte_of(b >> 12) & ~0xFFFu) == 0x100000);
    b = _MapPhysToLinear(0xFFFFF000, 0x2000, 0);
    LOG("_MapPhysToLinear(FFFFF000h, 2000h) %x", b);
    b = _MapPhysToLinear(0x12345, 0, 0);
    LOG("_MapPhysToLinear(12345h, 0) ok %b ~lin=%x", b != 0xFFFFFFFF, b);
    s = get_cs();
    LOG("_SelectorMapFlat(CS) %x", _SelectorMapFlat(sys_vm, s, 0));
    LOG("_SelectorMapFlat(DS, high word set) %x", _SelectorMapFlat(sys_vm, get_ds() | 0xABCD0000u, 0));
    LOG("_SelectorMapFlat(0) %x", _SelectorMapFlat(sys_vm, 0, 0));
    LOG("_SelectorMapFlat(FFF8h) %x", _SelectorMapFlat(sys_vm, 0xFFF8, 0));
    /* (VM 0 with an LDT selector hung Windows 3.1: not tried) */
    LOG("_CopyPageTable(7FF00000h) %b", nz(_CopyPageTable(0x7FF00, 1, (vu32)pt, 0)));
}

static void tgv(const char *what, vu32 lin, vu32 n)
{
    LOG("_TestGlobalV86Mem(%s) %x", what, _TestGlobalV86Mem(lin, n, 0));
}

static void global_tests(void)
{
    tgv("IVT", 0, 0x400);
    tgv("500h", 0x500, 0x10);
    tgv("below the first page", (first_pg - 1) << 12, 0x1000);
    tgv("the first page", first_pg << 12, 0x1000);
    tgv("across the first page", (first_pg << 12) - 0x10, 0x20);
    tgv("the last page", last_pg << 12, 0x1000);
    tgv("A0000h", 0xA0000, 0x1000);
    tgv("B8000h", 0xB8000, 0x1000);
    tgv("F0000h", 0xF0000, 0x1000);
    tgv("100000h", 0x100000, 0x1000);
    tgv("110000h", 0x110000, 0x1000);
    tgv("0 bytes", 0x500, 0);
}

static void v86area_tests(void)
{
    vu32 a, t, f;
    a = _Allocate_Device_CB_Area(16, 0);
    LOG("_Allocate_Device_CB_Area(16) %b dword %b ~off=%x", nz(a), (a & 3) == 0, a);
    a = _Allocate_Device_CB_Area(0x100000, 0);
    LOG("_Allocate_Device_CB_Area(1 MB) %x", a);
    t = _Allocate_Temp_V86_Data_Area(0x100, 0);
    LOG("_Allocate_Temp_V86_Data_Area(100h) %b below 1MB %b para %b zero %b ~a=%x", nz(t), t < 0x100000,
        (t & 15) == 0, t && ((vu32 *)t)[0] == 0 && ((vu32 *)t)[63] == 0, t);
    LOG("  again %x", _Allocate_Temp_V86_Data_Area(0x100, 0));
    LOG("  _Allocate_Global_V86_Data_Area meanwhile %x", _Allocate_Global_V86_Data_Area(0x10, 0));
    LOG("  _Free_Temp_V86_Data_Area %b", nz(_Free_Temp_V86_Data_Area()));
    LOG("  _Free_Temp_V86_Data_Area again %b", nz(_Free_Temp_V86_Data_Area()));
    f = _GetFirstV86Page();
    t = _Allocate_Global_V86_Data_Area(0, GVDAInquire | GVDAParaAlign);
    LOG("_Allocate_Global_V86_Data_Area(Inquire, para) ~size=%x", t);
    a = _Allocate_Global_V86_Data_Area(0x30, GVDAParaAlign | GVDAZeroInit);
    LOG("_Allocate_Global_V86_Data_Area(30h, para, zero) %b para %b zero %b ~below=%b ~moved=%b ~a=%x",
        nz(a), (a & 15) == 0, a && ((vu32 *)a)[0] == 0 && ((vu32 *)a)[11] == 0,
        a < (_GetFirstV86Page() << 12), _GetFirstV86Page() != f, a);
    a = _Allocate_Global_V86_Data_Area(0x1000, GVDAPageAlign | GVDAZeroInit);
    LOG("_Allocate_Global_V86_Data_Area(1000h, page) %b page %b below the first page %b first page moved %b ~a=%x",
        nz(a), (a & 0xFFF) == 0, a < (_GetFirstV86Page() << 12), _GetFirstV86Page() != f, a);
    LOG("  _TestGlobalV86Mem(it) %x", _TestGlobalV86Mem(a, 0x1000, 0));
    first_pg = _GetFirstV86Page();
}

static void choose_page(void)
{
    struct vregs s;
    vu32 i, ok;
    for (i = 0; i < sizeof(arr); i++) arr[i] = 0xEE;
    ok = _Get_Device_V86_Pages_Array(0, (vu32)arr, 0);
    LOG("_Get_Device_V86_Pages_Array(global) %b", nz(ok));
    pages_array("global");
    LOG("_Assign_Device_V86_Pages(C8h) %b", nz(_Assign_Device_V86_Pages(0xC8, 1, 0, 0)));
    LOG("_Assign_Device_V86_Pages(10Fh, 2) %b", nz(_Assign_Device_V86_Pages(0x10F, 2, 0, 0)));
    /* (9Fh, the last V86 page, assigned globally: Windows 3.1 ended
     * before KRNL386 ran; not tried) */
    LOG("_Assign_Device_V86_Pages(local, System VM) at Device_Init %b",
        nz(_Assign_Device_V86_Pages(0xC8, 1, sys_vm, 0)));
    /* the page to hook: the first from B0h, then from C8h, that nobody
     * hooked (Hook_V86_Page succeeds) */
    for (i = 0xB0; i < 0xF0; i++) {
        if (i == 0xB8) i = 0xC8;
        clr(&s); s.eax = i; s.esi = (vu32)vxh_hook;
        Hook_V86_Page(&s);
        if (!CF(&s)) break;
        LOG("Hook_V86_Page(%w) cf 1: hooked already", i);
    }
    hpage = i;
    LOG("the page hooked %w", hpage);
    ptex("the page after the hook", pte_of(hpage));
    LOG("_Assign_Device_V86_Pages(it) %b", nz(_Assign_Device_V86_Pages(hpage, 1, 0, 0)));
    clr(&s); s.eax = hpage; s.esi = (vu32)vxh_hook2;
    Hook_V86_Page(&s);
    LOG("Hook_V86_Page(it) again cf %b", CF(&s));
    clr(&s); s.eax = 0xA0; s.esi = (vu32)vxh_hook2;
    Hook_V86_Page(&s);
    LOG("Hook_V86_Page(A0h) cf %b", CF(&s));
    clr(&s); s.eax = 0x50; s.esi = (vu32)vxh_hook2;
    Hook_V86_Page(&s);
    LOG("Hook_V86_Page(50h) cf %b", CF(&s));
    clr(&s); s.eax = 0x110; s.esi = (vu32)vxh_hook2;
    Hook_V86_Page(&s);
    LOG("Hook_V86_Page(110h) cf %b", CF(&s));
    for (i = 0; i < sizeof(arr); i++) arr[i] = 0;
    _Get_Device_V86_Pages_Array(0, (vu32)arr, 0);
    pages_array("global now");
}

static void device_init(void)
{
    struct vregs s;
    vu32 i;
    first_pg = _GetFirstV86Page();
    last_pg = _GetLastV86Page();
    LOG("Device_Init ~_GetFirstV86Page=%x _GetLastV86Page %x", first_pg, last_pg);
    LOG("_AddFreePhysPage(50h) %x", _AddFreePhysPage(0x50, 1, 0));
    choose_page();
    if (!(skip & 64)) alloc_tests();
    map_tests();
    if (!(skip & 2)) v86area_tests();
    global_tests();
    clr(&s); s.eax = 6; s.esi = (vu32)vxh_ud;
    Hook_V86_Fault(&s);
    vxh_ud_prev = CF(&s) ? 0 : s.esi;
    LOG("Hook_V86_Fault(06h) cf %b prev %b", CF(&s), nz(s.esi));
    clr(&s); s.eax = 2; s.esi = (vu32)vxh_ud;
    Hook_V86_Fault(&s);
    LOG("Hook_V86_Fault(02h) cf %b", CF(&s));
    /* (3.1 took 50h, beyond the DDK's 4Fh: the previous handler is put
     * back at once, before any interrupt can come) */
    clr(&s); s.eax = 0x50; s.esi = (vu32)vxh_ud;
    Hook_V86_Fault(&s);
    i = CF(&s);
    if (!i) { vu32 p = s.esi; clr(&s); s.eax = 0x50; s.esi = p; Hook_V86_Fault(&s); }
    LOG("Hook_V86_Fault(50h) cf %b", i);
    clr(&s); s.eax = 0xFF; s.esi = (vu32)vxh_ud;
    Hook_V86_Fault(&s);
    i = CF(&s);
    if (!i) { vu32 p = s.esi; clr(&s); s.eax = 0xFF; s.esi = p; Hook_V86_Fault(&s); }
    LOG("Hook_V86_Fault(FFh) cf %b", i);
    for (i = 0; i < 64; i++) vmblk[i] = maplk[i] = 0;
    hooks = 0;
    quiet = 0;
    hook2_calls = 0;
    ud_seen = 0;
}

/* ---- Init_Complete ---- */
static void init_complete(void)
{
    vu32 h, e, i;
    LOG("Init_Complete ~_GetFirstV86Page=%x _GetLastV86Page %x", _GetFirstV86Page(), _GetLastV86Page());
    h = _PageAllocate(1, PG_SYS, 0, 0, 0, 0, 0, PageLockedIfDP);
    LOG("_PageAllocate(LockedIfDP) %b", nz(h));
    if (h) {
        LOG("  _PageLock(LockedIfDP) %b _PageUnLock(LockedIfDP) %b", nz(_PageLock(h, 1, 0, PageLockedIfDP)),
            nz(_PageUnLock(h, 1, 0, PageLockedIfDP)));
        LOG("  _PageFree %b", nz(_PageFree(h, 0)));
    }
    /* _ModifyPageBits on the hooked page in the System VM */
    e = pte_of(hpage);
    ptex("System VM's hooked page", e);
    if (!(skip & 8)) {
    LOG("_ModifyPageBits(AND with other bits clear) %b", nz(_ModifyPageBits(sys_vm, hpage, 1, 0xFFFFFF00u, 0, PG_IGNORE, 0)));
    LOG("_ModifyPageBits(OR 40h) %b", nz(_ModifyPageBits(sys_vm, hpage, 1, ~0u, 0x40, PG_IGNORE, 0)));
    LOG("_ModifyPageBits(type 1) %b", nz(_ModifyPageBits(sys_vm, hpage, 1, ~0u, 0, PG_SYS, 0)));
    LOG("_ModifyPageBits(page 110h) %b", nz(_ModifyPageBits(sys_vm, 0x110, 1, ~0u, 0, PG_IGNORE, 0)));
    LOG("_ModifyPageBits(page 0) %b", nz(_ModifyPageBits(sys_vm, 0, 1, ~0u, 0, PG_IGNORE, 0)));
    LOG("_ModifyPageBits(clear W, PG_IGNORE) %b", nz(_ModifyPageBits(sys_vm, hpage, 1, ~P_WRITE, 0, PG_IGNORE, 0)));
    pte("  then", pte_of(hpage));
    LOG("_ModifyPageBits(clear W, PG_HOOKED) %b", nz(_ModifyPageBits(sys_vm, hpage, 1, ~P_WRITE, 0, PG_HOOKED, 0)));
    pte("  then", pte_of(hpage));
    LOG("_ModifyPageBits(set W, PG_IGNORE) %b", nz(_ModifyPageBits(sys_vm, hpage, 1, ~0u, P_WRITE, PG_IGNORE, 0)));
    pte("  then", pte_of(hpage));
    LOG("_ModifyPageBits(set P) %b", nz(_ModifyPageBits(sys_vm, hpage, 1, ~0u, P_PRES, PG_IGNORE, 0)));
    pte("  then", pte_of(hpage));
    }
    /* the System VM's first megabyte */
    ptab_runs("sysvm", 0);
    LOG("CB_High_Linear(System VM) = _CopyPageTable's %b",
        _CopyPageTable(CB_HIGH_LINEAR(sys_vm) >> 12, 0x110, (vu32)pt, 0) && pt[0x10] == pte_of(0x10));
    /* the pageable array */
    for (i = 0; i < sizeof(arr); i++) arr[i] = 0xEE;
    LOG("_GetV86PageableArray(System VM) %b", nz(_GetV86PageableArray(sys_vm, (vu32)arr, 0)));
    pages_array_n("pageable", 0x100);
    if (!(skip & 4)) LOG("_SetResetV86Pageable(set, first page, 1) %b", nz(_SetResetV86Pageable(sys_vm, first_pg, 1, PageSetV86Pageable)));
    for (i = 0; i < sizeof(arr); i++) arr[i] = 0;
    _GetV86PageableArray(sys_vm, (vu32)arr, 0);
    for (e = 0, i = 0; i < 0x100; i++) if (arr[i >> 3] & (1 << (i & 7))) e++;
    LOG("  pageable: the first page %b, pages %x", (arr[first_pg >> 3] >> (first_pg & 7)) & 1, e);
    if (!(skip & 4)) LOG("_SetResetV86Pageable(clear) %b", nz(_SetResetV86Pageable(sys_vm, first_pg, 1, PageClearV86Pageable)));
    LOG("_PageOutDirtyPages(query) ~%x", _PageOutDirtyPages(0, PagePDPQueryDirty));
    LOG("_GetSetPageOutCount(get) ~%x", _GetSetPageOutCount(0, 1));
    LOG("~_GetVMPgCount(SystemVM)=%x ~%x", _GetVMPgCount(sys_vm, 0), vxh_edx);
}

/* ---- a DOS VM ---- */
static void vm_init(vu32 vm)
{
    vu32 hl = CB_HIGH_LINEAR(vm) >> 12, ok;
    LOG("VM_Init vm %b", vmid(vm));
    ptex("  the hooked page in it", pte_of(hl + hpage));
    ok = _ModifyPageBits(vm, hpage, 1, ~P_PRES, 0, PG_HOOKED, 0);
    LOG("  _ModifyPageBits(not present, PG_HOOKED) %b", nz(ok));
    pte("  then", pte_of(hl + hpage));
    LOG("  _Assign_Device_V86_Pages(local %w) %b", hpage + 1, nz(_Assign_Device_V86_Pages(hpage + 1, 1, vm, 0)));
    LOG("  _Assign_Device_V86_Pages(local, the global one) %b", nz(_Assign_Device_V86_Pages(hpage, 1, vm, 0)));
    for (ok = 0; ok < sizeof(arr); ok++) arr[ok] = 0xEE;
    LOG("  _Get_Device_V86_Pages_Array(it) %b", nz(_Get_Device_V86_Pages_Array(vm, (vu32)arr, 0)));
    pages_array("  local");
    LOG("  _DeAssign_Device_V86_Pages(local) %b", nz(_DeAssign_Device_V86_Pages(hpage + 1, 1, vm, 0)));
    LOG("  _DeAssign_Device_V86_Pages(local) again %b", nz(_DeAssign_Device_V86_Pages(hpage + 1, 1, vm, 0)));
}

static void destroy_vm(vu32 vm)
{
    vu32 id = vmid(vm) & 63;
    LOG("Destroy_VM vm %b hooked page %b", id, vmblk[id] != 0);
    if (vmblk[id]) LOG("  _PageFree %b", nz(_PageFree(vmblk[id], 0)));
    if (maplk[id]) LOG("  _PageFree(mapped) %b", nz(_PageFree(maplk[id], 0)));
    vmblk[id] = maplk[id] = 0;
}

/* ---- the V86 API: AX the function ----
 *  0  the VM: its handle checks, the page table, _TestGlobalV86Mem, counts
 *  1  PGT's report: BX a code, CX, DX values
 *  2  the hooked page's entry (after PGT read and wrote it)
 *  3  the hooked page not present again (DX 1: quietly, for timing)
 *  4  the hooked page read-only (P_WRITE clear, PG_HOOKED)
 *  5  a PG_VM page of the VM's own (9Ah in every byte) at page CX (_MapIntoV86)
 *  6  the nul page at page CX
 *  7  _LinMapIntoV86 of the probe's PG_SYS page (5Ch) at page CX; of a V86 page
 *  8  CB_High_Linear: 5Eh written at page CX + 10h through it; PGT's 4F0h
 *  9  Map_Lin_To_VM_Addr of page CX + 123h
 * 10  _PhysIntoV86 of the VM's PG_VM page at page CX; _ModifyPageBits after
 * 11  timing: quiet hooks on (DX 1) or off (0) and the count
 * 12  ring 0: the hooked page (not present) read through the VM's high
 *     linear address
 */
void __cdecl vxh_api(struct vregs *r)
{
    struct client_regs *c = (struct client_regs *)r->ebp;
    vu32 vm = r->ebx, id = vmid(vm) & 63, fn = c->eax & 0xFFFF, cx = c->ecx & 0xFFFF;
    vu32 hl = CB_HIGH_LINEAR(vm), h, ok, e;
    struct vregs s;
    c->eflags &= ~1u;
    switch (fn) {
    case 0:
        LOG("api vm %b: ~_GetFirstV86Page=%x _GetLastV86Page %x high linear = Get_Cur_VM_Handle's %b",
            id, _GetFirstV86Page(), _GetLastV86Page(), cur_vm() == vm);
        LOG("~_GetVMPgCount=%x ~%x", _GetVMPgCount(vm, 0), vxh_edx);
        ptab_runs("dosvm", 0);
        LOG("_CopyPageTable(high linear) = (0) %b",
            _CopyPageTable(hl >> 12, 0x110, (vu32)pt, 0) && pt[0x10] == pte_of(0x10) && pt[0x50] == pte_of(0x50));
        tgv("IVT", 0, 0x400);
        tgv("the first page", _GetFirstV86Page() << 12, 0x1000);
        tgv("the last page", _GetLastV86Page() << 12, 0x1000);
        tgv("B8000h", 0xB8000, 0x1000);
        for (e = 0; e < sizeof(arr); e++) arr[e] = 0xEE;
        LOG("_GetV86PageableArray(it) %b", nz(_GetV86PageableArray(vm, (vu32)arr, 0)));
        pages_array_n("pageable", 0x100);
        c->ebx = (c->ebx & 0xFFFF0000u) | hpage;
        break;
    case 1:
        if ((c->ebx & 0xFFFF) >= 0x80) LOG("pgt %w: ~%w ~%w", c->ebx & 0xFFFF, cx, c->edx & 0xFFFF);
        else LOG("pgt %w: %w %w", c->ebx & 0xFFFF, cx, c->edx & 0xFFFF);
        break;
    case 2:
        pte("api: the hooked page", pte_of(hpage));
        break;
    case 3:
        ok = _ModifyPageBits(vm, hpage, 1, ~P_PRES, 0, PG_HOOKED, 0);
        if (!(c->edx & 0xFFFF)) {
            LOG("api: _ModifyPageBits(not present) %b", nz(ok));
            pte("api: then", pte_of(hpage));
        }
        break;
    case 4:
        ok = _ModifyPageBits(vm, hpage, 1, ~P_WRITE, 0, PG_HOOKED, 0);
        LOG("api: _ModifyPageBits(read-only) %b", nz(ok));
        pte("api: then", pte_of(hpage));
        break;
    case 5:
        h = maplk[id];
        if (!h) {
            h = _PageAllocate(1, PG_VM, vm, 0, 0, 0, 0, PageZeroInit);
            LOG("api: _PageAllocate(1, PG_VM) %b", nz(h));
            if (!h) break;
            maplk[id] = h;
            fill(vxh_edx, 0x9A);
        }
        LOG("api: _MapIntoV86(PGT's page) ~page=%w %b", cx, nz(_MapIntoV86(h, vm, cx, 1, 0, 0)));
        pte("api: then", pte_of(cx));
        LOG("api: _MapIntoV86(page 0Fh) %b (10Fh, 2) %b (offset past) %b",
            nz(_MapIntoV86(h, vm, 0x0F, 1, 0, 0)), nz(_MapIntoV86(h, vm, 0x10F, 2, 0, 0)),
            nz(_MapIntoV86(h, vm, cx, 1, 1, 0)));
        break;
    case 6:
        ok = _MapIntoV86(_GetNulPageHandle(), vm, cx, 1, 0, 0);
        LOG("api: _MapIntoV86(nul, PGT's page) ~page=%w %b", cx, nz(ok));
        pte("api: then", pte_of(cx));
        break;
    case 7:
        ok = _LinMapIntoV86(blk_sys_lin >> 12, vm, cx, 1, 0);
        LOG("api: _LinMapIntoV86(PG_SYS page) eax %x edx = the page %b", ok, vxh_edx == cx);
        pte("api: then", pte_of(cx));
        ok = _LinMapIntoV86(0x50, vm, cx, 1, 0);
        LOG("api: _LinMapIntoV86(page 50h) eax %x edx %x", ok, vxh_edx);
        ok = _LinMapIntoV86((hl >> 12) + 0x50, vm, cx + 1, 1, 0);
        LOG("api: _LinMapIntoV86(own high linear 50h, at the next page) eax %x ~edx=%x same memory %b", ok, vxh_edx,
            (pte_of(cx + 1) & ~0xFFFu) == (pte_of(0x50) & ~0xFFFu));
        break;
    case 8:
        *(vu8 *)(hl + (cx << 12) + 0x10) = 0x5E;
        e = CB_HIGH_LINEAR(sys_vm);
        LOG("api: high linear 4F0h: own %w System VM's same %b", *(vu16 *)(hl + 0x4F0),
            *(vu16 *)(e + 0x4F0) == *(vu16 *)(hl + 0x4F0));
        LOG("api: high linear, PGT's page: System VM's same memory %b",
            (pte_of((e >> 12) + cx) & ~0xFFFu) == (pte_of((hl >> 12) + cx) & ~0xFFFu));
        break;
    case 9:
        clr(&s); s.eax = (cx << 12) + 0x123; s.ecx = 0xFF; s.ebp = r->ebp;
        Map_Lin_To_VM_Addr(&s);
        LOG("api: Map_Lin_To_VM_Addr(PGT's page + 123h) cf %b cx = page:12h %b edx %x", CF(&s),
            (s.ecx & 0xFFFF) == (cx << 8) + 0x12, s.edx);
        clr(&s); s.eax = 0x123456; s.ecx = 0xFF; s.ebp = r->ebp;
        Map_Lin_To_VM_Addr(&s);
        LOG("api: Map_Lin_To_VM_Addr(123456h) cf %b ~cx=%w ~edx=%x", CF(&s), s.ecx & 0xFFFF, s.edx);
        break;
    case 10:
        h = maplk[id];
        e = 0;
        if (h) { _PageGetSizeAddr(h, 0); e = pte_of(vxh_edx >> 12); }
        ok = h ? _PhysIntoV86(e >> 12, vm, cx, 1, 0) : 0;
        LOG("api: _PhysIntoV86(PGT's page) ~page=%w %b", cx, nz(ok));
        pte("api: then", pte_of(cx));
        LOG("api: _PhysIntoV86(page 0Fh) %b", nz(_PhysIntoV86(e >> 12, vm, 0x0F, 1, 0)));
        break;
    case 11:
        quiet = c->edx & 1;
        LOG("api: quiet %b hooks so far ~%x", quiet, hooks);
        break;
    case 12:
        ok = _ModifyPageBits(vm, hpage, 1, ~P_PRES, 0, PG_HOOKED, 0);
        LOG("api: ring 0 read of the hooked page through the high linear address (not present %b)", nz(ok));
        e = hooks;
        ok = *(volatile vu8 *)(hl + (hpage << 12));
        LOG("api: read %b hooked %x", ok, hooks - e);
        break;
    default:
        c->eflags |= 1;
    }
}

void __cdecl vxh_control(struct vregs *r)
{
    struct vregs s;
    r->eflags &= ~VF_CF;
    switch (r->eax) {
    case SYS_CRITICAL_INIT:
        clr(&s); Get_Sys_VM_Handle(&s); sys_vm = s.ebx;
        clr(&s); s.eax = 0; s.esi = 0; s.edi = (vu32)"VXHSkip";
        Get_Profile_Decimal_Int(&s);
        skip = s.eax;
        if (skip) LOG("~VXHSkip=%x", skip);
        break;
    case DEVICE_INIT:
        device_init();
        break;
    case INIT_COMPLETE:
        init_complete();
        break;
    case VM_INIT:
        vm_init(r->ebx);
        break;
    case DESTROY_VM:
        destroy_vm(r->ebx);
        break;
    case SYSTEM_EXIT:
        LOG("System_Exit hooks ~%x second callback %x V86 fault hook %x", hooks, hook2_calls, ud_seen);
        /* last: bad handles (Windows 3.1 does not check them: one ended it) */
        LOG("_PageFree(freed) next");
        /* (3.1 freed it again and gave its size: it does not check a
         * handle; w16 does) */
        LOG("_PageFree(freed) ~%b", nz(_PageFree(freed, 0)));
        LOG("_PageGetSizeAddr(freed) ~%x ~%x", _PageGetSizeAddr(freed, 0), vxh_edx);
        LOG("_PageFree(0) next");
        LOG("_PageFree(0) %b", nz(_PageFree(0, 0)));
        break;
    }
}
