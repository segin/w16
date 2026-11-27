/*
 * tk55x.c - Windows ends without USER's ExitWindows: the shell calls
 * KERNEL's ExitKernel (KERNEL.2) itself, so the drivers are never
 * disabled (the PS/2 mouse stays on, with the BIOS's handler still the
 * one MOUSE.DRV gave it). Run as the shell under tests/dosstrun.sh, which
 * moves the mouse after Windows has ended and checks that DOS still
 * runs (boot-test case win386exit; ARCHITECTURE.md 12.10).
 */
#include <windows.h>

#define TK_REF "tk55ref.h"
#include "tklog.h"

int __cdecl tmain(void)
{
    void (FAR PASCAL *ek)(WORD) = (void (FAR PASCAL *)(WORD))
        GetProcAddress(GetModuleHandle("KERNEL"), MAKEINTRESOURCE(2));
    out("TK55X start, ExitKernel\r\n");
    if (ek)
        ek(0);
    out("TK55X ExitKernel returned\r\n");
    return 0;
}
