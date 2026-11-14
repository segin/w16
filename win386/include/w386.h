/* w386.h - the start-up block w16's WIN386 loader hands to its VMM.
 *
 * Shared by the real-mode loader (16-bit, win386/ldr) and the VMM (32-bit,
 * win386/vmm). This is w16's own interface between its two halves, not a
 * Windows structure: nothing outside WIN386.EXE sees it. Layout is packed,
 * fields sized explicitly so both compilers agree.
 */
#ifndef W386_H
#define W386_H

#if defined(__386__)
typedef unsigned char  w_u8;
typedef unsigned short w_u16;
typedef unsigned int   w_u32;
#else
typedef unsigned char  w_u8;
typedef unsigned short w_u16;
typedef unsigned long  w_u32;
#endif

#define W386_MAGIC      0x36383357UL    /* "W386" */
#define W386_VMM_LIN    0x80000000UL    /* linear base of the VMM window */

/* Breakpoints: ARPL (63h) bytes in the loader's code segment. In V86 mode
 * ARPL is an invalid opcode, so executing one traps to the VMM, which tells
 * them apart by linear address. Index of each: */
#define W386_BP_EXIT    0       /* the loader: leave Windows (AX = exit code) */
#define W386_BP_DPMI    1       /* the DPMI mode-switch entry (INT 2Fh 1687h) */
#define W386_BP_REFRET  2       /* end of a reflected interrupt/call from PM */
#define W386_BP_PS2CB   3       /* the PS/2 pointing device handler (INT 15h C207h) of a PM client */
#define W386_BP_FIRSTFREE 4     /* real-mode callbacks and V86 API entries */

#pragma pack(1)
struct w386_info {
    w_u32 magic;
    w_u32 xms_phys;     /* physical address of the locked XMS block */
    w_u32 xms_size;     /* its size in bytes */
    w_u32 pt_count;     /* page tables mapping the window (4 MB each) */
    w_u32 img_off;      /* the VMM image's offset in the window */
    w_u32 img_size;     /* bytes, page aligned */
    w_u32 entry_lin;    /* linear address of the VMM entry */
    w_u32 info_lin;     /* linear address of this block */
    w_u16 ldr_cs;       /* the loader's code segment (real mode) */
    w_u16 ldr_ds;       /* its data segment */
    w_u16 v86_ip, v86_cs, v86_sp, v86_ss;   /* where the System VM starts */
    w_u16 v86_ds, v86_es;
    w_u32 v86_flags;
    w_u16 bp_off;       /* breakpoint table: ldr_cs:bp_off, bp_count bytes */
    w_u16 bp_count;
    w_u16 rm_tramp;     /* offset in ldr_cs of the return-to-real-mode code */
    w_u16 refstk_seg;   /* the V86 stack for interrupts reflected from PM */
    w_u16 refstk_top;
    w_u16 psp;          /* the loader's PSP */
    w_u16 exit_code;    /* set by the VMM when Windows ends */
    w_u16 ret_ss, ret_sp;  /* V86 stack at the exit breakpoint */
    w_u32 conv_free;    /* free conventional memory when the VMM started */
    w_u8  pic1_base, pic2_base;     /* real-mode PIC bases (08h, 70h) */
    w_u32 log_port;     /* serial port for the VMM's trace (0: none) */
    w_u32 mem_kb;       /* extended memory reported by XMS (KB) */
    w_u16 pmbp_off;     /* PM breakpoints: HLT bytes at ldr_ds:pmbp_off */
    w_u16 pmbp_count;
    w_u32 sda_lin;      /* DOS swappable data area (current PSP at +10h) */
    w_u16 xbuf_seg;     /* transfer buffer for DOS calls from PM (real mode) */
    w_u16 xbuf_size;    /* bytes */
    w_u32 trace;        /* SYSTEM.INI [w16] vmmtrace=1: detailed VMM trace */
    w_u32 vxd_count;    /* VxDs loaded after the VMM (SYSTEM.INI [386Enh]) */
    w_u32 vxd_list;     /* window offset of their DDBs' linear addresses */
    w_u32 tail_lin;     /* WIN386's command tail (PSP:80h), linear */
};
#pragma pack()

#endif
