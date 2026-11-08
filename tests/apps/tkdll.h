/*
 * tkdll.h - shared by the test DLLs (tkdll.c, tkdlb.c, tkid.c): what the
 * entry code in dllstart.asm recorded, as words a probe reads through the
 * DLL's xxx_INFO export.
 *
 * Info index:
 *   0-11   registers at the entry point: ax bx cx dx si di bp sp ds es ss cs
 *   12-19  instance data words DS:0000-000E at entry
 *   20-23  bytes ES:SI[0..7] at entry (as words)
 *   24     number of bytes copied from ES:SI (FFFFh: ES:SI not readable)
 *   25     times the entry point ran
 *   26     DS now
 *   27     pHeap (DS:0006) now
 *   28-    DLL-specific (see the DLL)
 */
extern WORD __cdecl lib_regs[12];
extern WORD __cdecl lib_inst[8];
extern BYTE __cdecl lib_cmd[8];
extern WORD __cdecl lib_cmdlen;
extern WORD __cdecl lib_calls;

#define L_CX    2
#define L_DI    5
#define L_DS    8

static WORD cur_ds(void);
#pragma aux cur_ds = "mov ax, ds" value [ax];
static WORD ds_limit(void);
#pragma aux ds_limit = ".286p" "mov dx, ds" "lsl ax, dx" value [ax] modify [dx];

static WORD lib_info(WORD i)
{
    if (i < 12)
        return lib_regs[i];
    if (i < 20)
        return lib_inst[i - 12];
    if (i < 24)
        return ((WORD *)lib_cmd)[i - 20];
    switch (i) {
    case 24: return lib_cmdlen;
    case 25: return lib_calls;
    case 26: return cur_ds();
    case 27: return *(WORD __near *)6;
    case 31: return ds_limit();
    }
    return 0xFFFF;
}

/* KERNEL does not make a DLL's local heap (observed on Windows 3.1:
   DS:0006 is 0 at the entry point); like a task's, it goes in the last
   CX bytes of DGROUP. */
static void lib_heap(void)
{
    if (!*(WORD __near *)6 && lib_regs[L_CX])   /* near: DGROUP */
        LocalInit(cur_ds(), 0, lib_regs[L_CX]);
}
