/*
 * tk65i.c - TK65I1.DLL ... TK65I9.DLL: each imports one entry of
 * TK65D.DLL by a name (the Makefile gives each its own: TK65 in
 * tests/apps/tk65.c lists them) and returns the address KERNEL fixed up
 * for it from GETIMP, never calling it.
 */
#include <windows.h>

extern void FAR PASCAL TKIMP(void);

int __cdecl libmain(void)
{
    return 1;
}

DWORD FAR PASCAL GETIMP(void)                                  /* 1 */
{
    return (DWORD)(void FAR *)TKIMP;
}
