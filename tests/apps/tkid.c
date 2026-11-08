/*
 * tkid.c - TKID.TPL, the template TK5.EXE copies into several directories
 * (patching the byte after "TKID#") to see where LoadLibrary looks first.
 * Built also as:
 *   TKBAD.DLL    (TK_BAD)    its initialisation routine fails (returns 0)
 *   TKNODEP.DLL  (TK_NODEP)  imports from a module that does not exist
 * See tkdll.h.
 */
#include <windows.h>
#include "tkdll.h"

static char id_mark[] = "TKID#\1";
static WORD FAR *wep_ptr;

#ifdef TK_NODEP
extern WORD FAR PASCAL NOFUNC(void);
#endif

int __cdecl libmain(void)
{
    lib_heap();
#ifdef TK_BAD
    return 0;
#else
    return 1;
#endif
}

WORD FAR PASCAL __loadds ID_GET(void)                           /* 1 */
{
#ifdef TK_NODEP
    if (!id_mark[0])
        return NOFUNC();
#endif
    return (BYTE)id_mark[5];
}

/* WEP writes 100h | its argument to *p */
void FAR PASCAL __loadds ID_SETWEP(WORD FAR *p)                 /* 2 */
{
    wep_ptr = p;
}

WORD FAR PASCAL __loadds ID_INFO(WORD i)                        /* 3 */
{
    return lib_info(i);
}

int FAR PASCAL __loadds WEP(int param)
{
    if (wep_ptr)
        *wep_ptr = 0x100 | (WORD)param;
    return 1;
}
