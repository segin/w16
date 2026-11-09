/* vmm.h - w16's Virtual Machine Manager (WIN386.EXE, ring 0, 32-bit flat).
 * ARCHITECTURE.md 12. Sources: Intel 80386 Programmer's Reference Manual
 * (protected mode, paging, V86 mode, the TSS and IDT), DPMI 0.9, the
 * Windows 3.1 DDK documentation of INT 2Fh 16xxh.
 */
#ifndef VMM_H
#define VMM_H

#include "w386.h"

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
#define SEL_FLAT3   0x43    /* ring 3 data, flat (the host's view, for clients) */
#define GDT_ENTRIES 9

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

/* The virtual machine (only the System VM so far). */
struct vm {
    struct cregs r;         /* the current state */
    int pm;                 /* a DPMI client exists */
    int client32;           /* it is a 32-bit client */
    struct nest *nest;      /* protected-mode states waiting for V86 code */
    struct cregs v86save;   /* the V86 state at the mode switch */
    u16 refsp;              /* the reflection stack pointer (info->refstk_seg) */
    u16 psp_seg;
    u32 dsel;               /* the client's first DS selector */
    u32 pmvec_sel[256];     /* protected-mode interrupt vectors (0205h) */
    u32 pmvec_off[256];
    u32 excvec_sel[32];     /* exception handlers (0203h) */
    u32 excvec_off[32];
};

extern struct w386_info *info;
extern struct vm sysvm;
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
void *kmalloc(u32 size);
void kfree(void *p);
u32 page_alloc(void);               /* a physical page, zeroed; 0 if none */
u32 pages_free(void);
u32 phys_to_lin(u32 phys);
int map_page(u32 lin, u32 phys, u32 flags);
u32 lin_to_phys(u32 lin);
void make_user(u32 lin, u32 size);
void memset(void *d, int c, u32 n);
void memcpy(void *d, const void *s, u32 n);
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
void exec_return(struct cregs *r);         /* that breakpoint: back to exec_v86's caller */
void v86_int(struct cregs *v, int vec);    /* INT vec in V86 mode, synchronously */
void v86_farcall(struct cregs *v, int iret);
u32 xbuf_lin(void);                        /* the transfer buffer */
u32 xbuf_size(void);

/* dosx.c: INT 21h from protected mode */
void dos_pm(struct cregs *r);
void video_pm(struct cregs *r);

/* vxdapi.c */
void vxdapi_get(struct cregs *r, u32 *sel, u32 *off);
void vxdapi_call(struct cregs *r, u32 index, u32 retip, u32 retcs);
int sel_to_seg(u32 sel);
u32 seg_to_sel(u32 seg, u32 access);
u32 pm_lin(u32 sel, u32 off);

/* dpmi.c */
void dpmi_init(void);
void dpmi_switch(struct cregs *r);                 /* the V86 entry breakpoint */
void dpmi_int(struct cregs *r, int vec, int hw);   /* INT n / IRQ in protected mode */
void dpmi_pmbp(struct cregs *r, u32 index);        /* a PM breakpoint (HLT) */
void dpmi_exception(struct cregs *r, int vec, u32 err);
u32 sel_base(u32 sel);
int pm_v86_reflect(struct cregs *r, int vec, int hw);
int int2f_pm(struct cregs *r);
int dpmi_v86_hook(struct cregs *r, int vec, int hw);  /* PM handler for a V86 interrupt? */

/* the PM breakpoints: HLT bytes in a ring-3 code segment (SEL_PMBP) */
#define PMBP_DEFAULT    0       /* 0-255: the default handler for INT n */
#define PMBP_EXCRET     256     /* return from a client's exception handler */
#define PMBP_VENDOR     257     /* the "MS-DOS" vendor entry (INT 2Fh 168Ah) */
#define PMBP_IRQRET     258     /* end of a hardware interrupt run in PM for V86 */
#define PMBP_API        259     /* 259..: a VxD's PM API entry (1684h), by device */
#define PMBP_COUNT      320

#endif
