/*
 * bootinfo.h - hand-off from the KRNL286.EXE real-mode bootstrap (the MZ
 * stub, kernel/boot/) to the protected-mode KERNEL module.
 *
 * Internal to w16; not part of the Windows ABI. On entry to KERNEL
 * (NE CS:IP), ES:BX points to this structure.
 */
#ifndef W16_BOOTINFO_H
#define W16_BOOTINFO_H

#include "w16/types.h"

#define BOOTINFO_MAGIC  0x3157          /* "W1" */
#define BOOT_MAXSEG     16              /* KERNEL segments supported */
#define BOOT_PATHMAX    80

typedef struct bootinfo {
    u16 magic;                  /* BOOTINFO_MAGIC */
    u16 psp_sel;                /* PSP selector (from the DPMI switch) */
    u16 psp_seg;                /* PSP real-mode segment */
    u16 cpl;                    /* privilege level the client runs at */
    u16 ne_sel;                 /* data selector: resident NE tables copy */
    u16 ne_len;                 /* bytes in that copy */
    u16 seg_count;              /* KERNEL segments */
    u16 seg_sel[BOOT_MAXSEG];   /* selector of each segment (code = exec) */
    u16 seg_para[BOOT_MAXSEG];  /* real-mode paragraph of each segment */
    u32 seg_size[BOOT_MAXSEG];  /* bytes allocated for each segment */
    char path[BOOT_PATHMAX];    /* full path of KRNL286.EXE */
} bootinfo;

#endif
