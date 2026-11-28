/* vmm.h - w16's Virtual Machine Manager (WIN386.EXE, ring 0, 32-bit flat).
 * ARCHITECTURE.md 12. Sources: Intel 80386 Programmer's Reference Manual
 * (protected mode, paging, V86 mode, the TSS and IDT), DPMI 0.9, the
 * Windows 3.1 DDK documentation of INT 2Fh 16xxh.
 */
#ifndef VMM_H
#define VMM_H

#include "w386.h"
#include "w16vxd.h"

typedef unsigned char  u8;
typedef unsigned short u16;
typedef unsigned int   u32;
typedef signed int     s32;

#define NULL ((void *)0)

/* GDT selectors (VMM's own) */
#define SEL_CODE    0x08    /* ring 0 code, flat */
#define SEL_DATA    0x10    /* ring 0 data, flat */
#define SEL_TSS     0x18
#define SEL_RMCODE  0x20    /* 16-bit code at the loader's segment (exit) */
#define SEL_RMDATA  0x28    /* 16-bit data, 64 KB (exit) */
#define SEL_LDT     0x30
#define SEL_PMBP    0x3B    /* ring 3 code: PM breakpoints (HLT bytes) */
#define SEL_BIOSDATA 0x40   /* ring 3 data: the BIOS data area (40:0), as Windows 3.1's */
#define SEL_HSTK32  0x83    /* ring 3 data, big: a DOS VM's 32-bit client's host stack (dpmi.c) */
#define GDT_ENTRIES 17          /* up to SEL_HSTK32 (index 16) */

#define EFL_CF  0x0001
#define EFL_ZF  0x0040
#define EFL_TF  0x0100
#define EFL_IF  0x0200
#define EFL_DF  0x0400
#define EFL_IOPL 0x3000
#define EFL_NT  0x4000
#define EFL_RF  0x10000
#define EFL_VM  0x20000

/* The trap frame the stubs build (vmmasm.asm), lowest address first. */
struct tframe {
    u32 gs, fs, es, ds;
    u32 edi, esi, ebp, esp0, ebx, edx, ecx, eax;
    u32 vec, err;
    u32 eip, cs, eflags, esp, ss;       /* esp, ss: from ring 3/V86 only */
    u32 v86es, v86ds, v86fs, v86gs;     /* from V86 only */
};

/* A client's registers (V86 or protected mode), as a VM is resumed. */
struct cregs {
    u32 edi, esi, ebp, ebx, edx, ecx, eax;
    u32 eip, eflags, esp;
    u32 cs, ss, ds, es, fs, gs;
};
/* register access helpers */
#define AX(r)  ((u16)(r)->eax)
#define BX(r)  ((u16)(r)->ebx)
#define CX(r)  ((u16)(r)->ecx)
#define DX(r)  ((u16)(r)->edx)
#define SI(r)  ((u16)(r)->esi)
#define DI(r)  ((u16)(r)->edi)
#define CL(r)  ((u8)(r)->ecx)
#define CH(r)  ((u8)((r)->ecx >> 8))
#define SETW(reg, v) ((reg) = ((reg) & 0xFFFF0000u) | ((u16)(v)))
#define SETLO(reg, v) ((reg) = ((reg) & 0xFFFFFF00u) | ((u8)(v)))
#define SETHI(reg, v) ((reg) = ((reg) & 0xFFFF00FFu) | ((u32)(u8)(v) << 8))

/* a saved protected-mode state while V86 code runs for it */
struct nest {
    struct nest *prev;
    struct cregs pm;        /* the protected-mode state to come back to */
    u32 kind;               /* NEST_* */
    u32 regstruct;          /* 0300h-0302h: linear address of the client's structure */
    u16 v86sp;              /* the reflection stack before this level */
};
#define NEST_INT    1       /* a software interrupt reflected from PM: copy regs back */
#define NEST_HWINT  2       /* a hardware interrupt: restore regs unchanged */
#define NEST_0300   3       /* DPMI 0300h-0302h */

/* the start of a VM control block, whose address is the VM's handle (the
 * DDK names CB_VM_Status, CB_High_Linear, CB_Client_Pointer, CB_VMID; the
 * order as observed with VXP). Devices get areas after VM_CB_RESERVED
 * (_Allocate_Device_CB_Area) */
#pragma pack(1)
struct vm_cb {
    u32 status, high_linear, client_pointer, vmid;
    struct vm *vm;          /* w16's own: the VMM's state of the VM */
};
#pragma pack()
#define VM_CB_SIZE      8192
#define VM_CB_RESERVED  0x100

/* A VM's DPMI host (dpmi.c, ARCHITECTURE.md 12.16): the System VM's
 * client is KRNL386; a DOS VM's are its DOS extenders, nested ones too
 * (a client that EXECs another) */
#define DPMI_NRMCB 1024         /* real-mode callbacks per VM (0303h; Windows 3.1 in a box: 1024 at least) */
struct dpmi_outer;
struct dpmi_rmcb { u32 sel, off, ssel, soff, level; };   /* the procedure, the structure, the client (0: free) */
struct segsel;
struct dosblk;
struct dpmi_vm {
    u8 *ldt;                    /* a DOS VM's LDT (NULL: the VMM's own, the System VM's) */
    u32 ldt_first;              /* the first index 0000h hands out */
    u32 x_ldt_top, x_ldt_alias;
    struct segsel *x_segsels;     /* 0002h's selectors */
    struct dosblk *x_dosblks;     /* INT 21h 48h's blocks */
    struct dpmi_outer *outer;   /* the state of the clients a nested one runs inside */
    u32 level;                  /* clients running (1: one, 2: one nested in it...) */
    u32 x_hstk_sel, x_hstk_lin, x_hstk_depth, x_hstk_low, x_pmw_ss, x_pmw_sp;
    int hstk_own;               /* a DOS VM's host stack: pages of its own */
    int x_exc_last;
    u16 x_env_seg, psp_sel;
    u32 dta_sel, dta_off;       /* the client's DTA (INT 21h 1Ah: dosx.c) */
    int dta_set;
    struct dpmi_rmcb *rmcb;     /* 0303h's callbacks (DPMI_NRMCB, made at the first) */
    u32 wp_used;                /* 0B00h's watchpoints in use (bits) */
    u32 term;                   /* the client's PSP terminate address (0Ah) before the host's */
    void *exec_mark;            /* the VM's nested calls when the client started (exec_unwind) */
    int stale;                  /* the last client ended by DOS: its descriptors are still there */
    u8 *acb;                    /* the application control block (Begin_PM_App) */
};

/* The virtual machines: the System VM (sysvm), whose DPMI client is
 * KRNL386, and the DOS VMs (vm.c). */
/* a VM's channel 0 of the 8254 (VTD, vxd.c); all 0: the BIOS's (65536
 * counts, mode 3, both bytes) */
struct vtimer {
    u32 count;              /* its period in counts (0: not written yet) */
    u32 acc;                /* counts since its period began */
    u16 latch;              /* a latched count */
    u8 mode, rw;            /* the control word's mode and access (1 low, 2 high, 3 both) */
    u8 wstate, wlow;        /* a count's low byte written, waiting for the high */
    u8 rstate, latched;     /* the next byte read is the high one; latched bytes left */
};

struct vm {
    struct cregs r;         /* the current state */
    int pm;                 /* a DPMI client exists */
    int client32;           /* it is a 32-bit client */
    struct nest *nest;      /* protected-mode states waiting for V86 code */
    struct cregs v86save;   /* the V86 state at the mode switch */
    u16 refsp;              /* the reflection stack pointer (info->refstk_seg) */
    u16 psp_seg;
    u32 dsel;               /* the client's first DS selector */
    u32 last_cs, last_ip, last_ds;  /* where the client last left protected mode */
    u32 pmvec_sel[256];     /* protected-mode interrupt vectors (0205h) */
    u32 pmvec_off[256];
    u32 excvec_sel[32];     /* exception handlers (0203h) */
    u32 excvec_off[32];
    /* every VM (vm.c) */
    struct vm *next;        /* the VM list, in creation order */
    u32 id;                 /* the VM ID: 1 the System VM */
    struct vm_cb *cb;       /* its control block: the handle */
    struct client_regs *client;
    u32 ptab_phys;          /* the page table of linear 0-4 MB (its first MB) */
    u32 *own;               /* a DOS VM's private pages (physical, one per page of vm.c's private range): freed with it, whatever a device mapped there since */
    u8 assigned[36];        /* memsvc.c: V86 pages assigned to a device in this VM only (_Assign_Device_V86_Pages) */
    u8 pageable[32];        /* memsvc.c: _SetResetV86Pageable's pages */
    u32 kstack_top;         /* its ring-0 stack (the System VM: kstack) */
    u32 kjb[6];             /* its ring-0 context while another VM runs */
    int started;            /* has run (kjb holds its context) */
    u32 kesp0;              /* saved while not current: */
    struct cregs *hw_r;
    void *exec;
    u16 refstk_seg, refstk_top;     /* the V86 stack of the VMM's nested calls */
    u8 *inst;               /* its instance data while another VM runs */
    int crit;               /* in a DOS critical section (INT 2Ah 80h/81h) */
    int indos;              /* inside an INT 21h call the VMM made (v86_int) */
    int ndos;               /* V86 INT 21h calls under way (vm_int21): their */
    u16 dos_ss[8], dos_sp[8];   /* return frames, to see one abandoned */
    u32 susp_count;         /* Suspend_VM's count (the services) */
    u32 gen;                /* which VM this slot holds now (vm_create) */
    int closing;            /* its programs are being ended (vm_close) */
    u32 nuke_at;            /* Close_VM: ended by force at this system time (0: never) */
    u16 closing_psp;
    int idle;               /* HLT, INT 2Fh 1680h: no time until an interrupt */
    int suspended;          /* Suspend_VM, or not executable any more */
    u32 slice;              /* ticks left in its time slice */
    u32 prio_fg, prio_bg;   /* time-slice priorities (PIF, SYSTEM.INI) */
    int exclusive;          /* PIF: no other VM runs while it has the focus full screen */
    int background;         /* PIF: it runs on full screen without the focus */
    u32 exec_ms;            /* Get_VM_Exec_Time: the time it has run */
    u32 run_since;          /* when it last started running (vmm_system_time) */
    s32 exec_prio;          /* Adjust_Exec_Priority's boosts */
    u32 idle_until;         /* idle (released): ticks do not wake it before this */
    struct mmvm *mm;        /* V86MMGR's: its XMS, EMS, A20 (DOS VMs) */
    u32 exit_code;          /* its program's exit code (VM stub) */
    int ended;              /* its program has ended */
    struct vm *free_next;   /* destroyed, kept for the next VM */
    struct vtimer vt;       /* its timer (VTD) */
    char cwd[68];           /* its current drive and directory, "C:\X" (vm_dos_cwd) */
    struct dpmi_vm dp;      /* its DPMI host (dpmi.c) */
};

extern struct w386_info *info;
extern struct vm sysvm;
extern struct vm *cur_vm;           /* the VM running (vm.c) */
extern struct vm *vm_list;
extern u32 bp_lin;              /* linear address of the V86 breakpoints */

/* vmmasm.asm */
void __cdecl resume(struct cregs *r);           /* does not return */
void __cdecl outb(u32 port, u32 v);
u32 __cdecl inb(u32 port);
void __cdecl lgdt(void *pseudo);
void __cdecl lidt(void *pseudo);
void __cdecl lldt(u32 sel);
void __cdecl ltr(u32 sel);
void __cdecl to_realmode(u32 tramp_off);        /* does not return */
u32 __cdecl get_cr0(void);
u32 __cdecl get_cr2(void);
u32 __cdecl get_cr3(void);
void __cdecl set_cr3(u32);
void __cdecl hlt(void);
int __cdecl k_setjmp(u32 *jb);
void __cdecl k_longjmp(u32 *jb, int v);
extern u32 kesp0;
extern u32 trap_stubs[256];
extern u8 kstack_top[];

/* vmm.c */
void trace(const char *fmt, ...);
void panic(const char *fmt, ...);
void fatal(const char *fmt, ...);   /* end Windows with a message */
void *kmalloc(u32 size);
void kfree(void *p);
u32 page_alloc(void);               /* a physical page, zeroed; 0 if none */
u32 page_alloc_run(u32 n, u32 align, u32 max_phys);    /* contiguous pages, or 0 */
u32 page_alloc_run_phase(u32 n, u32 phase);           /* the same, the first at phase mod 64 KB */
u32 pages_free(void);
u32 v86mm_pages(struct vm *v);      /* v86mmgr.c: a VM's pages beyond its first MB */
void page_free(u32 phys);
u32 phys_to_lin(u32 phys);
int map_page(u32 lin, u32 phys, u32 flags);
int map_page_noflush(u32 lin, u32 phys, u32 flags);
u32 lin_to_phys(u32 lin);
u32 lin_to_phys_x(u32 lin);          /* ~0: not present */
int lin_user_ok(u32 lin, u32 len, int write);   /* ring 3 could reach it all */
u32 vmm_pdir_entry(u32 i, u32 v);   /* set a page directory entry; the old one */
u32 vmm_pdir_entry_get(u32 i);
void make_user(u32 lin, u32 size);
void memset(void *d, int c, u32 n);
void memcpy(void *d, const void *s, u32 n);
int memcmp(const void *a, const void *b, u32 n);
u32 v86_lin(u32 seg, u32 off);
void v86_push16(struct cregs *r, u16 v);
u16 v86_pop16(struct cregs *r);
void v86_reflect(struct cregs *r, int vec);
void set_desc(u8 *d, u32 base, u32 limit, u32 access, u32 flags);
u32 desc_base(const u8 *d);
u32 desc_limit(const u8 *d);
extern u8 gdt[];
extern u8 *ldt;
extern u32 ldt_limit;

void exec_v86(struct cregs *v);            /* run client code until REFRET/IRQRET */
void exec_return(struct cregs *r);
int exec_depth(void);         /* that breakpoint: back to exec_v86's caller */
void *exec_mark(void);                     /* the current VM's nested calls now */
void exec_unwind(void *mark);              /* those made since mark dropped (DOS ended their program) */
void v86_int(struct cregs *v, int vec);    /* INT vec in V86 mode, synchronously */
void v86_int_sim(struct cregs *v, int vec);    /* the same, the devices' V86 hooks first */
void v86_farcall(struct cregs *v, int iret);
u32 xbuf_lin(void);                        /* the transfer buffer (the current VM's) */
u32 xbuf_seg(void);
u32 xbuf_size(void);

/* dosx.c: INT 21h from protected mode */
void dos_pm(struct cregs *r);
void video_pm(struct cregs *r);
void disk_pm(struct cregs *r, int vec);

/* vxdapi.c */
void vxdapi_get(struct cregs *r, u32 *sel, u32 *off);
void vxdapi_call(struct cregs *r, u32 index, u32 retip, u32 retcs);
void vxdapi_again(struct cregs *r);         /* the call made again: a device waits */
int sel_to_seg(u32 sel);
u32 seg_to_sel(u32 seg, u32 access);
u32 pm_lin(u32 sel, u32 off);

/* a VMM service's registers (vmmasm.asm svc_common: PUSHAD, PUSHFD) */
#pragma pack(1)
struct svcregs {
    u32 edi, esi, ebp, esp0, ebx, edx, ecx, eax;
    u32 eflags, id;
};
#pragma pack()
u32 __cdecl vmm_call_proc(u32 proc, struct svcregs *r);

/* vxd.c */
void vxd_client(struct cregs *r, struct client_regs *c);
void vxd_client_back(struct cregs *r, struct client_regs *c);
void vxd_init(void);
const char *vmm_svc_name(u32 svc);  /* a VMM service's DDK name */
const char *vxd_owner(u32 addr);    /* the device whose image holds addr */
int vmm_running(void);              /* vmm.c: the System VM has run */
void vmm_sample(void);              /* vmm.c: [w16] vmmtrace=1, where the VM runs */
extern struct vpicd_link *vpicd;
int vmm_deliver(u32 vec, int iret);
int vmm_can_deliver(void);
int vmm_build_int(u32 sel, u32 off);
struct cregs *vmm_hold_delivery(struct cregs *r);
void vxd_pm_app(int begin, struct cregs *r);
void vxd_exit(struct cregs *r);
void vxd_link(struct tframe *f);
int vxd_step(struct tframe *f);      /* [w16] vmmtrace=4 */
void vxd_pm_fault(struct cregs *r, int vec, u32 err);
int vxd_v86_fault(struct cregs *r, int vec, u32 err);  /* Hook_V86_Fault's chain: 1 if a device took it */
int vxd_vmm_fault(void *frame, int vec);                 /* Hook_VMM_Fault's */
int vxd_watch_fault(u32 vec, u32 *eflags, u32 eip);    /* [w16] vmmtrace=3 */   /* Hook_PM_Fault's chain, then DPMI */
u32 vm_handle(void);
u32 vxd_pm_api(u32 id);
void vxd_call_api(u32 proc, struct cregs *r);

/* vm.c: the VMs, their memory, the scheduler */
void vm_sys_init(void);             /* the System VM: control block, memory map */
void vm_snapshot(void);             /* the instance data new VMs start with */
struct vm *vm_create(void);         /* a DOS VM, not started */
int vm_set_program(struct vm *v, const char *path, const u8 *tail, const char *dir);
void vm_set_message(struct vm *v, const char *msg, u32 n);   /* for standard output first */
int vm_set_memory(struct vm *v, u32 kb);    /* conventional memory: the PIF's KB Desired */
void vm_destroy(struct vm *v);
struct vm *vm_of(u32 handle);       /* NULL if not a VM */
void vm_end_trap(struct cregs *r);  /* at the end of a trap: switch VMs? */
void vm_idle(void);                 /* the current VM has nothing to do */
void vm_wake(struct vm *v);         /* an interrupt for v */
void vm_tick(void);                 /* a timer tick */
void vm_tick_wakes(int on);
void vm_crit(int begin);            /* INT 2Fh 1681h/1682h */         /* VTD's requests: wakes that respect a release's penalty */
void vm_release(void);              /* Release_Time_Slice, INT 2Fh 1680h */
void vm_kernel_idle(void);          /* the System VM's INT 2Fh 1689h */
void vxd_release_time_slice(void);  /* Release_Time_Slice through its slot */
#define IDLE_WAKE 7000              /* ms: a released Background VM runs again (Windows 3.1: ~7 s) */
u32 vmm_system_time(void);          /* vxd.c: Get_System_Time, ms */
u32 vmm_last_time(void);            /* Get_Last_Updated_System_Time */
u32 vtd_io(u32 port, u32 out, u32 data);    /* vxd.c: a VM's byte at port 40h or 43h */
/* the heartbeat's counts of timing work since the last (vxd.c, vm.c):
 * time-outs set and run, device events queued and run, nested far calls */
extern u32 hb_tmo_set, hb_tmo_run, hb_ev_q, hb_ev_run, hb_nest;
u32 vtd_heartbeat(u32 *tmos);       /* the real timer's period in counts */
u32 vm_events_waiting(void);
void vm_set_focus(struct vm *v);    /* the execution focus */
extern struct vm *vm_focus;
int vm_service(u32 svc, struct svcregs *s);
void vm_exited(struct cregs *r);    /* the VM stub's VMEXIT breakpoint */
void vm_start(struct cregs *r);     /* a new VM's VMSTART breakpoint */
u32 vm_lin(struct vm *v, u32 lin);  /* v's address lin (first MB) for the VMM */
void vm_int2a(struct cregs *r);     /* DOS's critical sections */
extern int vm_trace;                /* [w16] vmmtrace=2 */
int vm_dos_call(u32 ax);            /* an INT 21h that returns: the VM is in DOS until it does */
void vm_int21(struct cregs *r);     /* V86 INT 21h: through the INT21RET breakpoint */
void vm_dos_cwd(void);              /* before a VM's INT 21h: its own current directory */
void vm_int21_ret(struct cregs *r);
#define VMH(v) ((u32)(v)->cb)
/* a DOS VM's first 8 KB of memory (the loader's VM stub, ldrasm.asm): its
 * PSP, the parameters, a message, the VMM's reflection stack, the stub's
 * stack */
#define DOSVM_PARAMS        0x100
#define DOSVM_BLOCK         0x200       /* paragraphs the stub takes (ldrasm.asm VS_BLOCK) */
#define DOSVM_MSGLEN        0x3FE
#define DOSVM_MSG           0x400
#define DOSVM_MSG_MAX       0x800
#define DOSVM_REFSTK_TOP    0x1800
#define DOSVM_REFSTK_SIZE   0xC00
#define DOSVM_STACK_TOP     0x1FFE

u32 vxd_send(u32 msg, struct vm *v, u32 edx, u32 esi, u32 edi);   /* carry: a device refused */
void vxd_send_rev(u32 msg, struct vm *v, u32 edx, u32 esi, u32 edi);
int vxd_service_call(u32 dev, u32 svc, struct svcregs *s);

void vm_event(struct vm *v, void (*fn)(struct vm *, void *), void *arg);   /* run in v, soon */
void io_vm_gone(struct vm *v);
void dpmi_pm_int(int vec, struct cregs *r);
void dpmi_pm_farcall(u32 sel, u32 off, const u16 *args, int n, struct cregs *r);
void dpmi_pm_farcall_regs(const struct cregs *in, u32 sel, u32 off, const u16 *args, int n, struct cregs *r);

/* shell.c, vdd.c: w16's SHELL and VDD (devices in the VMM module) */
void shell_init(void);
void shell_api(struct cregs *r);
int shell_service(u32 svc, struct svcregs *r);   /* device 0017h's services */
void shell_vm_ended(struct vm *v);
void shell_vm_init(struct vm *v);   /* VM_Init done: VMPOLL from the PIF */
void shell_vm_crashed(struct vm *v);
void shell_vm_nuked(struct vm *v);
void shell_vm_closing(struct vm *v);
struct reboot_link *reboot_link_of(u32 ref);
void vm_crashed(struct cregs *r);
void vm_close(struct vm *v);
int vxd_v86_int_hooks(struct cregs *r, int vec);
u32 vxdapi_v86_get(u32 id);                 /* a V86 API entry's breakpoint, or 0 */
void vxdapi_v86_call(struct cregs *r, u32 idx);
u32 vxd_v86_api(u32 id);                    /* a loaded device's V86 API procedure */    /* Hook_V86_Int_Chain's procedures */
int vdd_api(struct cregs *r);
int vdd_service(u32 svc, struct svcregs *s);   /* the VDD's services (device 000Ah) */
/* v86mmgr.c: the DOS VMs' XMS and EMS (device 0006h) */
void v86mm_vm_create(struct vm *v, const u8 *block);
void v86mm_vm_destroy(struct vm *v);
void v86mm_xms(struct cregs *r);            /* the XMS entry's breakpoint */
int v86mm_ems(struct cregs *r);             /* INT 67h */
int v86mm_int2f(struct cregs *r);           /* INT 2Fh 4300h/4310h */
int v86mm_service(u32 svc, struct svcregs *s);
void vmm_flush_tlb(void);
void vdd_int2f(struct cregs *r);
void vdd_focus(struct vm *v);       /* the display goes to v */
void vdd_vm_init(struct vm *v);
void vdd_vm_destroy(struct vm *v);
void vdd_window(struct vm *v, int on);   /* SHELL: windowed or full screen */
void vdd_tick(void);
int shell_display_changed(struct vm *v, int cursor);
#define SEL_FLAT7B  0x7B            /* SHELL's flat selector (its function 9; GDT index 15) */

/* pager.c: demand paging to the paging file (ARCHITECTURE.md 12.17) */
void pager_init(void);
int pager_on(void);
u32 pager_file_pages(void);
u32 pager_avail(void);                  /* pages a client may still be given */
int pager_map(u32 lin, u32 n);          /* new pageable pages; -1: no paging file */
int pager_drop(u32 lin);                /* a pageable page goes; 1: nothing left to free */
int pager_fault(u32 lin);               /* a page fault: 1 if the page is there now */
int pager_present(u32 lin);             /* bring lin's page in if it is the pager's */
int pager_lock(u32 lin, u32 size, int lock);
void pager_meminfo(u32 *p);             /* DPMI 0500h */
u32 pager_page(void);
int pager_service(u32 dev, u32 svc, struct svcregs *s);
void pager_stats(u32 *ins, u32 *outs, u32 *used);

/* memsvc.c: the memory and page-hooking services (ARCHITECTURE.md 12.19) */
int memsvc_service(u32 svc, struct svcregs *s);
int memsvc_page_fault(u32 lin, u32 err, struct vm *in);   /* a hooked V86 page or a block's: 1 if mapped now */
int memsvc_hooked(u32 page);                            /* a V86 page some device hooked */
u32 vm_first_v86_page(void);                            /* vm.c */
u32 vm_last_v86_page(void);
u32 vm_global_v86_alloc(u32 n, u32 align, int inquire);
u32 vm_temp_v86_area(u32 n);
int vm_add_instance(u32 lin, u32 len);
int vm_instance_range(u32 lin, u32 len);
u32 dpmi_ldt_sel(u32 base, u32 limit);                  /* dpmi.c */                /* 1 if it holds instance data */

/* io.c: I/O port trapping */
void io_trap_bit(u32 port, int on);                 /* vmm.c: the TSS's I/O bitmap */
int io_trap(struct cregs *r, u8 *op);               /* IN/OUT/INS/OUTS that faulted */
int io_service(u32 svc, struct svcregs *s);
extern u32 io_traps;

/* dpmi.c */
void dpmi_init(void);
void dpmi_vm_in(struct vm *v);                      /* vmm_switch: v's LDT */
void dpmi_vm_destroy(struct vm *v);                 /* vm.c: a client's memory left */
void dpmi_close(struct cregs *r);                   /* vm.c: a closing DOS VM's client ends */
void dpmi_rm_entry(struct cregs *r);                /* W386_BP_DPMIRM: callbacks, raw switch, state save */
extern u32 pmdef_sel[256], pmdef_off[256];          /* the PM vectors a new client starts with */
void dpmi_switch(struct cregs *r);                 /* the V86 entry breakpoint */
void dpmi_int(struct cregs *r, int vec, int hw);   /* INT n / IRQ in protected mode */
void dpmi_pmbp(struct cregs *r, u32 index);        /* a PM breakpoint (HLT) */
void dpmi_exception(struct cregs *r, int vec, u32 err);
u32 sel_base(u32 sel);
int seg_32(u32 sel);
int sel_present(u32 sel);                /* a code segment's D bit, a stack's B bit */
u8 *cl_ptr(u32 sel, u32 off, u32 len, int write);   /* the client's sel:off, or NULL */
void sel_gone(struct cregs *r, u32 sel);            /* freed: out of r's segment registers */
int dpmi_dosmem(struct cregs *r);   /* INT 21h 48h, 49h, 4Ah from protected mode */
int pm_v86_reflect(struct cregs *r, int vec, int hw);
int int2f_pm(struct cregs *r);
int dpmi_v86_hook(struct cregs *r, int vec, int hw);  /* PM handler for a V86 interrupt? */
void dpmi_ps2cb(struct cregs *r);
void dpmi_build_int(struct cregs *r, u32 sel, u32 off);
int dpmi_pmvec_default(int vec);                    /* INT vec's PM vector is the host's */
void dpmi_client_push(struct cregs *r, u32 v);      /* on the client's PM stack (its word size) */
u32 dpmi_client_pop(struct cregs *r);
void vxd_pm_callback(struct cregs *r, u32 n);       /* vxd.c: Allocate_PM_Call_Back's n-th */
int dpmi_in_handler(void);
void dpmi_note_pm(struct cregs *r);                /* every trap from PM */                  /* the BIOS called the PS/2 handler */

/* the PM breakpoints: HLT bytes in a ring-3 code segment (SEL_PMBP) */
#define PMBP_DEFAULT    0       /* 0-255: the default handler for INT n */
#define PMBP_EXCRET     256     /* return from a client's exception handler */
#define PMBP_VENDOR     257     /* the "MS-DOS" vendor entry (INT 2Fh 168Ah) */
#define PMBP_IRQRET     258     /* end of a hardware interrupt run in PM for V86 */
#define PMBP_API        259     /* 259..318: a VxD's PM API entry (1684h), by device */
#define PMBP_API_END    319
#define PMBP_EXCDEF     319     /* the default exception handler 0202h hands out */
#define PMBP_CB         320     /* 320..447: callbacks (Allocate_PM_Call_Back), 2 bytes apart */
#define PMBP_CB_END     448
#define PMBP_RAW        448     /* 0306h: the protected-to-real entry */
#define PMBP_SAVE       449     /* 0305h: save/restore state (nothing to save) */
#define PMBP_COUNT      456

#endif
