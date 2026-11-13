/*
 * vx3.c - VX3.EXE: the VxD protected-mode API entries as a Windows program
 * sees them under 386 Enhanced mode (ARCHITECTURE.md 12.5). Run as the
 * shell (tests/refrun.sh VX3.EXE /3) under Windows 3.1's WIN386 and under
 * w16's; logs to TK.LOG:
 *   - INT 2Fh AX=1684h for every device ID 0000h-0040h and the external
 *     ones known: which have an entry, and where (selector:offset);
 *   - for each entry, function 0 (AX=0, the DDK's convention for "get
 *     version"): every register and the carry flag back.
 * Only function 0 is called: the others change state (the display, the
 * mouse, the reboot handler) and are recorded from the host side instead
 * (w16's VMM traces every call USER and the drivers make, vxdapi.c).
 */
#include <windows.h>

#define TK_REF "vx3ref.h"
#include "tklog.h"

extern void __cdecl __far vx_int2f(WORD __far *regs);
extern void __cdecl __far vx_call(void __far *entry, WORD __far *regs);

static const WORD xids[] = { 0x0442, 0x0483, 0x0484, 0x0486, 0x048B, 0 };

static void one(WORD id)
{
    WORD r[8], e[2];
    char name[24];
    static const char hx[] = "0123456789ABCDEF";
    int i;

    for (i = 0; i < 8; i++) r[i] = 0;
    r[0] = 0x1684;
    r[1] = id;
    vx_int2f(r);
    if (!r[5] && !r[6]) return;
    e[0] = r[5];
    e[1] = r[6];
    name[0] = 'a'; name[1] = 'p'; name[2] = 'i'; name[3] = '_';
    for (i = 0; i < 4; i++) name[4 + i] = hx[(id >> (12 - 4 * i)) & 15];
    name[8] = 0;
    out("OBS ~"); out(name); out("="); outhex(e[1]); out(":"); outhex(e[0]); out("\r\n");
    for (i = 0; i < 8; i++) r[i] = 0;
    vx_call((void __far *)MAKELONG(e[0], e[1]), r);
    out("OBS "); out(name); out("_fn0=ax:"); outhex(r[0]);
    out(" bx:"); outhex(r[1]); out(" cx:"); outhex(r[2]); out(" dx:"); outhex(r[3]);
    out(" si:"); outhex(r[4]); out(" di:"); outhex(r[5]); out(" cf:"); outhex(r[7] & 1);
    out("\r\n");
}

int __cdecl tmain(void)
{
    WORD id;
    int i;

    out("VX3 VxD PM API entries\r\n");
    for (id = 0; id <= 0x40; id++)
        one(id);
    for (i = 0; xids[i]; i++)
        one(xids[i]);
    out("VX3 done\r\n");
    return 0;
}
