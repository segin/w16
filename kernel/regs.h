/*
 * regs.h - register block used to issue software interrupts from C
 * (see INTCALL in kernel/inc/intcall.inc) and by register-based entry
 * points such as InitTask.
 *
 * The field order is ABI between C and the assembly helpers: the asm code
 * builds this structure by pushing FLAGS, ES, DS, DI, SI, DX, CX, BX, AX.
 */
#ifndef W16_REGS_H
#define W16_REGS_H

#include "w16/types.h"

typedef struct regs16 {
    u16 ax, bx, cx, dx, si, di, ds, es, flags;
} regs16;

#define FLAG_CARRY 0x0001

#endif
