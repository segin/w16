/*
 * emu.h - WIN87EM's x87 emulator (emu.c) and its interface to
 * win87em.asm.
 */
#ifndef W16_EMU_H
#define W16_EMU_H

typedef unsigned char  u8;
typedef unsigned short u16;
typedef unsigned long  u32;
typedef short          s16;
typedef long           s32;

#define FAR __far
#ifndef NULL
#define NULL 0
#endif

/* the registers win87em.asm saves on an INT 34h-3Eh (emu_common) */
typedef struct emu_frame {
    u16 es, ds, di, si, bp, sp, bx, dx, cx, ax;
    u16 intno;
    u16 ip, cs, flags;
} emu_frame;

/* __FPMATH registers (fpm_emu): in and out */
typedef struct fpm_regs {
    u16 ax, dx, flags;
} fpm_regs;

/* win87em.asm */
extern u16 __cdecl user_cw, fn12, em_acc;
extern u32 __cdecl handler;
extern void __cdecl __far call_handler(u16 code, u16 app_ds);
#pragma aux call_handler modify [ax bx cx dx es]

void __cdecl __far emu_reset(void);
void __cdecl __far emu_entry(emu_frame FAR *f);
void __cdecl __far emu_fpmath(u16 fn, fpm_regs FAR *r);
void __cdecl __far emu_save(void FAR *buf);
void __cdecl __far emu_restore(void FAR *buf);

#endif
