/*
 * dx6.c - DX6.EXE: the descriptor tables once Windows is up. Run as the
 * shell (tests/dropin.sh -r -f build/dos/DX6.EXE=DX6.EXE -i
 * 'boot:shell=C:\DX6.EXE', with and without -x) it dumps the GDT through
 * Microsoft's DOSX's alias 000Bh (DPL 3; DX1) and the IDT through 0013h,
 * so the entries KERNEL and the drivers have changed show against the
 * tables DOSX made (DX1 gdtraw/idtraw lines). Black-box observation
 * (ARCHITECTURE.md section 3).
 */
#include <windows.h>

#define TK_REF "dx6ref.h"
#include "tklog.h"

static void dtab(const char *what, WORD sel, WORD from, WORD to)
{
    WORD i;
    for (i = from; i < to; i += 8) {
        WORD FAR *d = (WORD FAR *)MAKELP(sel, i);
        out("OBS ");
        out(what);
        out(" ");
        outhex(i);
        out("=");
        outhex(d[3]);
        outhex(d[2]);
        out(" ");
        outhex(d[1]);
        outhex(d[0]);
        out("\r\n");
    }
}

int __cdecl tmain(void)
{
    out("DX6 start\r\n");
    dtab("gdt", 0x000B, 0, 0x120);
    dtab("idt", 0x0013, 0, 0x800);
    out("DX6 done\r\n");
    return 0;
}
