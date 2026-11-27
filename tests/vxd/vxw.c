/* vxw.c - VXW.386, a probe VxD of w16's own (test only): the linear
 * address space's layout as the page directory shows it (ARCHITECTURE.md
 * 12.18). Loaded with "device=vxw.386" (init order: the last), it writes
 * to COM1 (DOSBox-X serial1=file), every line starting "VXW ", at
 * Sys_Critical_Init, Device_Init, Init_Complete, Sys_VM_Init and
 * System_Exit:
 *   the message, then the page directory's present entries as ranges of
 *   4 MB slots (first-last, in hex: slot n maps n * 400000h), and each
 *   range's flags (the entries' low 12 bits, of its first entry);
 *   _GetDemandPageInfo's six figures;
 *   at Device_Init and Init_Complete, the PageSwap device's documented
 *   services: PageSwap_Get_Version, PageSwap_Test_Create of 16 pages and
 *   of 100000h, PageSwap_Test_IO_Valid (the registers back).
 * Run once with and once without another device (W32S.386, Win32s's) under
 * Windows 3.1's WIN386: the ranges that differ are where that device put
 * what it made. The page directory is read through _MapPhysToLinear of
 * CR3 (the DDK's service; CR3 is the 386's). Nothing here comes from
 * Microsoft's code.
 */
#include "w16vxd.h"

#pragma data_seg("_TEXT", "CODE")

static vu32 inb(vu32 port);
static void outb(vu32 port, vu32 v);
static vu32 get_cr3(void);
#pragma aux inb = "in al, dx" "movzx eax, al" parm [edx] value [eax] modify exact [eax];
#pragma aux outb = "out dx, al" parm [edx] [eax] modify exact [];
#pragma aux get_cr3 = "mov eax, cr3" value [eax] modify exact [eax];

vu32 __cdecl vmm_map_phys(vu32 phys, vu32 n, vu32 flags);
vu32 __cdecl vmm_demand_info(vu32 info, vu32 flags);
void __cdecl pageswap_get_version(struct vregs *r);
void __cdecl pageswap_test_create(struct vregs *r);
void __cdecl pageswap_test_io_valid(struct vregs *r);
void __cdecl vmm_get_version(struct vregs *r);
void __cdecl vmm_get_sys_vm(struct vregs *r);
vu32 __cdecl vmm_sys_pages(vu32 flags);
vu32 __cdecl vmm_first_v86(void);
vu32 __cdecl vmm_last_v86(void);
static void regs(const char *what, struct vregs *r);

static void putc_(int c)
{
    int i;
    for (i = 0; i < 100000 && !(inb(0x3FD) & 0x20); i++) ;
    outb(0x3F8, c);
}
static void puts_(const char *s) { while (*s) putc_(*s++); }
static void hex(vu32 v, int n)
{
    while (n--) putc_("0123456789ABCDEF"[(v >> (n * 4)) & 15]);
}

static int detail;                      /* the tables too: at Device_Init */

/* the system's half of the address space (entries 200h up) in detail:
 * each present entry's value, the runs of present entries in its page
 * table (flags, the first physical page, physically contiguous), and
 * where the page directory and the tables themselves are mapped there */
static void tables(vu32 *pd)
{
    vu32 i, j, k, runs, *pt, cr3 = get_cr3() & ~0xFFFu;
    puts_("VXW  pdlin ");
    hex((vu32)pd, 8);
    puts_("\r\n");
    for (i = 0; i < 5; i++) {
        puts_("VXW  pde ");
        hex(i, 3);
        puts_(" = ");
        hex(pd[i], 8);
        puts_("\r\n");
    }
    for (i = 0x200; i < 1024; i++) {
        if (!(pd[i] & 1)) continue;
        pt = (vu32 *)vmm_map_phys(pd[i] & ~0xFFFu, 4096, 0);
        puts_("VXW  pde ");
        hex(i, 3);
        puts_(" = ");
        hex(pd[i], 8);
        puts_(" at ");
        hex((vu32)pt, 8);
        puts_("\r\n");
        if (!pt || pt == (vu32 *)0xFFFFFFFF) continue;
        for (j = 0, runs = 0; j < 1024 && runs < 24; j = k) {
            if (!pt[j]) { k = j + 1; continue; }
            for (k = j + 1; k < 1024 && pt[k] && (pt[k] & 0xFFF) == (pt[j] & 0xFFF)
                 && ((pt[k] & 1) == 0 || (pt[k] & ~0xFFFu) == (pt[j] & ~0xFFFu) + (k - j) * 4096); k++) ;
            puts_("VXW   pte ");
            hex(j, 3); putc_('-'); hex(k - 1, 3);
            puts_(" ");
            hex(pt[j], 8);
            puts_("\r\n");
            runs++;
        }
        for (j = 0; j < 1024; j++) {
            vu32 p = pt[j] & ~0xFFFu;
            if (!(pt[j] & 1)) continue;
            if (p == cr3) {
                puts_("VXW   the page directory at ");
                hex((i << 22) | (j << 12), 8);
                puts_("\r\n");
            }
            for (k = 0x200; k < 1024; k++)
                if ((pd[k] & 1) && (pd[k] & ~0xFFFu) == p) {
                    puts_("VXW   table ");
                    hex(k, 3);
                    puts_(" at ");
                    hex((i << 22) | (j << 12), 8);
                    puts_("\r\n");
                }
        }
    }
}

static void services(void)
{
    struct vregs v;
    vu32 i;
    for (i = 0; i < sizeof(v) / 4; i++) ((vu32 *)&v)[i] = 0;
    vmm_get_version(&v);
    regs("Get_VMM_Version", &v);
    for (i = 0; i < sizeof(v) / 4; i++) ((vu32 *)&v)[i] = 0;
    vmm_get_sys_vm(&v);
    regs("Get_Sys_VM_Handle", &v);
    puts_("VXW  sysvm high ");
    hex(((vu32 *)v.ebx)[1], 8);
    puts_("\r\n");
    puts_("VXW  _GetSysPageCount ");
    hex(vmm_sys_pages(0), 8);
    puts_(" _GetFirstV86Page ");
    hex(vmm_first_v86(), 8);
    puts_(" _GetLastV86Page ");
    hex(vmm_last_v86(), 8);
    puts_("\r\n");
}

static void dump(const char *what)
{
    vu32 *pd = (vu32 *)vmm_map_phys(get_cr3() & ~0xFFFu, 4096, 0);
    vu32 i, j, d[16];
    puts_("VXW ");
    puts_(what);
    puts_(" cr3 ");
    hex(get_cr3(), 8);
    puts_("\r\n");
    if (!pd || pd == (vu32 *)0xFFFFFFFF) return;
    for (i = 0; i < 1024; i = j) {
        if (!(pd[i] & 1)) { j = i + 1; continue; }
        for (j = i + 1; j < 1024 && (pd[j] & 1); j++) ;
        puts_("VXW  pde ");
        hex(i, 3); putc_('-'); hex(j - 1, 3);
        puts_(" flags ");
        hex(pd[i] & 0xFFF, 3);
        puts_("\r\n");
    }
    for (i = 0; i < 16; i++) d[i] = 0;
    vmm_demand_info((vu32)d, 0);
    puts_("VXW  demand");
    for (i = 0; i < 6; i++) { putc_(' '); hex(d[i], 8); }
    puts_("\r\n");
    if (detail) tables(pd);
}

static void regs(const char *what, struct vregs *r)
{
    puts_("VXW  ");
    puts_(what);
    puts_(" eax "); hex(r->eax, 8);
    puts_(" ebx "); hex(r->ebx, 8);
    puts_(" ecx "); hex(r->ecx, 8);
    puts_(" edx "); hex(r->edx, 8);
    puts_(" cf "); hex(r->eflags & 1, 1);
    puts_("\r\n");
}

static void clear(struct vregs *v)
{
    vu32 n;
    for (n = 0; n < sizeof(*v) / 4; n++) ((vu32 *)v)[n] = 0;
}

/* the PageSwap device's documented services (the DDK) */
static void pageswap(void)
{
    struct vregs v;
    clear(&v);
    pageswap_get_version(&v);
    regs("PageSwap_Get_Version", &v);
    clear(&v);
    v.ecx = 16;
    pageswap_test_create(&v);
    regs("PageSwap_Test_Create(16)", &v);
    clear(&v);
    v.ecx = 0x100000;
    pageswap_test_create(&v);
    regs("PageSwap_Test_Create(100000h)", &v);
    clear(&v);
    pageswap_test_io_valid(&v);
    regs("PageSwap_Test_IO_Valid", &v);
}

void __cdecl vxw_control(struct vregs *r)
{
    switch (r->eax) {
    case 0: dump("Sys_Critical_Init"); break;
    case 1: detail = 1; dump("Device_Init"); detail = 0; services(); pageswap(); break;
    case 2: dump("Init_Complete"); pageswap(); break;
    case 3: dump("Sys_VM_Init"); break;
    case 5: dump("System_Exit"); break;
    }
    r->eflags &= ~1u;
}
