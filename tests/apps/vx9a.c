/*
 * vx9a.c - VX9A.EXE: the Windows side of VX9 (tests/dos/vx9.asm, run by
 * tests/vx9run.sh as the shell): its own PDB as KERNEL built it (the
 * selector, the terminate address at 0Ah, the parent at 16h, the saved
 * stack at 2Eh), then TK51C.EXE (which only exits) started and waited
 * for, so that VX9.COM sees a task's exit reach DOS; then Exit Windows.
 */
#include <windows.h>

#define TK_REF "vx3ref.h"
#include "tklog.h"

static void pdb_fields(const char *name, WORD pdb)
{
    WORD FAR *p = (WORD FAR *)MAKELP(pdb, 0);
    out("OBS ~"); out(name); out("=sel:"); outhex(pdb);
    out(" term:"); outhex(p[0x0C / 2]); out(":"); outhex(p[0x0A / 2]);
    out(" parent:"); outhex(p[0x16 / 2]);
    out(" stack:"); outhex(p[0x30 / 2]); out(":"); outhex(p[0x2E / 2]);
    out("\r\n");
}

int __cdecl tmain(void)
{
    HMODULE hk = GetModuleHandle("KERNEL");
    WORD (FAR PASCAL *gcp)(void) = (WORD (FAR PASCAL *)(void))GetProcAddress(hk, MAKEINTRESOURCE(37));
    WORD pdb = gcp(), i;
    HINSTANCE h;
    pdb_fields("own_pdb", pdb);
    h = WinExec("C:\\TK51C.EXE", SW_SHOWNORMAL);
    out("OBS ~winexec="); outhex(h); out("\r\n");
    for (i = 0; i < 4000 && GetModuleHandle("TK51C"); i++)
        Yield();
    out("OBS ~gone="); outhex(i < 4000); out("\r\n");
    pdb_fields("own_pdb_after", pdb);
    ExitWindows(0, 0);
    return 0;
}
