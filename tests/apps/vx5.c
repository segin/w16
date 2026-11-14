/*
 * vx5.c - VX5.EXE: what Windows 3.1's VxD protected-mode APIs return for
 * the calls Microsoft's drivers and KERNEL make while booting 386 Enhanced
 * mode (ARCHITECTURE.md 12.5). w16's VMM traced those calls with their
 * registers (vxdapi.c); VX5 replays each with the same registers, run as
 * the shell (tests/refrun.sh VX5.EXE /3) after the boot has made them
 * once, and logs every register and the carry flag back to TK.LOG:
 *   - VMD   AX=0100h BX=7404h CX=0071h DX=0500h (MOUSE.DRV, PS/2 mouse);
 *     twice; AX=0005h before and after (Ralf Brown's Interrupt List 61:
 *     the mouse port data). Run with w16's MOUSE.DRV in place of
 *     Microsoft's (REFRUN_FILES), VX5's first 0100h is the first one;
 *   - VDD   AX=000Ch BX=9B0Ah CX=3101h DX=0 SI=01F4h DI=0106h (VGA.DRV);
 *   - Reboot AX=0202h (RBIL: get KERNEL's Ctrl+Alt+Del handler, i.e. what
 *     KERNEL's AX=0201h set);
 * and the version (AX=0) of VMD and VDD, as VX3 does.
 */
#include <windows.h>

#define TK_REF "vx3ref.h"
#include "tklog.h"

extern void __cdecl __far vx_int2f(WORD __far *regs);
extern void __cdecl __far vx_call(void __far *entry, WORD __far *regs);

static void __far *entry(WORD id)
{
    WORD r[8];
    int i;
    for (i = 0; i < 8; i++) r[i] = 0;
    r[0] = 0x1684;
    r[1] = id;
    vx_int2f(r);
    out("OBS ~entry_"); outhex(id); out("="); outhex(r[6]); out(":"); outhex(r[5]); out("\r\n");
    if (!r[5] && !r[6]) return NULL;
    return (void __far *)MAKELONG(r[5], r[6]);
}

static void call(const char *name, void __far *e, WORD ax, WORD bx, WORD cx, WORD dx,
                 WORD si, WORD di)
{
    WORD r[8];
    int i;
    if (!e) return;
    for (i = 0; i < 8; i++) r[i] = 0;
    r[0] = ax; r[1] = bx; r[2] = cx; r[3] = dx; r[4] = si; r[5] = di;
    vx_call(e, r);
    out("OBS ~"); out(name); out("=ax:"); outhex(r[0]);
    out(" bx:"); outhex(r[1]); out(" cx:"); outhex(r[2]); out(" dx:"); outhex(r[3]);
    out(" si:"); outhex(r[4]); out(" di:"); outhex(r[5]); out(" es:"); outhex(r[6]);
    out(" cf:"); outhex(r[7] & 1); out("\r\n");
}

int __cdecl tmain(void)
{
    void __far *vmd, *vdd, *reboot;

    out("VX5 VxD API calls of the boot, replayed\r\n");
    vmd = entry(0x000C);
    vdd = entry(0x000A);
    reboot = entry(0x0009);
    call("vmd_0000", vmd, 0x0000, 0, 0, 0, 0, 0);
    call("vmd_0005", vmd, 0x0005, 0, 0, 0, 0, 0);
    call("vmd_0100", vmd, 0x0100, 0x7404, 0x0071, 0x0500, 0, 0);
    call("vmd_0100b", vmd, 0x0100, 0x7404, 0x0071, 0x0500, 0, 0);
    call("vmd_0005b", vmd, 0x0005, 0, 0, 0, 0, 0);
    call("vdd_0000", vdd, 0x0000, 0, 0, 0, 0, 0);
    call("vdd_000C", vdd, 0x000C, 0x9B0A, 0x3101, 0, 0x01F4, 0x0106);
    call("reboot_0202", reboot, 0x0202, 0, 0, 0, 0, 0);
    out("VX5 done\r\n");
    return 0;
}
