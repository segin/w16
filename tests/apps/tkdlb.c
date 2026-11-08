/*
 * tkdlb.c - TKDLB.DLL, imported by TKDLL.DLL (so it is loaded as a
 * dependency of a dependency of TK5.EXE). See tkdll.h.
 */
#include <windows.h>
#include "tkdll.h"

static WORD inited;

int __cdecl libmain(void)
{
    lib_heap();
    inited = 1;
    return 1;
}

WORD FAR PASCAL __loadds DLB_INITED(void)                       /* 1 */
{
    return inited;
}

WORD FAR PASCAL __loadds DLB_ID(void)                           /* 2 */
{
    return 0xB0B;
}

WORD FAR PASCAL __loadds DLB_INFO(WORD i)                       /* 3 */
{
    return lib_info(i);
}
