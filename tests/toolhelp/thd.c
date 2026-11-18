/*
 * thd.c - THD.DLL, the library the TOOLHELP.DLL probe loads and frees
 * (tests/toolhelp/thp.c): two code segments and a DGROUP, all PRELOAD, so
 * that Windows 3.1 notifies the same segments w16 does (w16 loads every
 * segment with its module).
 */
#include <windows.h>

int __cdecl __far libmain(void)
{
    return 1;
}

#pragma code_seg("THD2_TEXT")
static WORD calls;

void __far __pascal __loadds __export THDFAR(void)
{
    calls++;
}
