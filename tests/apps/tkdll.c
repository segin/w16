/*
 * tkdll.c - TKDLL.DLL, the DLL TK5.EXE imports (by name and by ordinal).
 * It imports TKDLB.DLL itself, so loading TK5 loads both. See tkdll.h.
 */
#include <windows.h>
#include "tkdll.h"

extern WORD FAR PASCAL DLB_INITED(void);        /* by name */
extern WORD FAR PASCAL DLB_ID(void);            /* by ordinal */

static WORD dlb_inited_at_init, dlb_id, lalloc;

int __cdecl libmain(void)
{
    dlb_inited_at_init = DLB_INITED();
    dlb_id = DLB_ID();
    lib_heap();
    lalloc = LocalAlloc(LMEM_FIXED, 16);
    return 1;
}

/* 28: TKDLB's LibMain had run, 29: DLB_ID(), 30: LocalAlloc in LibMain */
WORD FAR PASCAL __loadds DLL_INFO(WORD i)                       /* 1 */
{
    switch (i) {
    case 28: return dlb_inited_at_init;
    case 29: return dlb_id;
    case 30: return lalloc;
    }
    return lib_info(i);
}

WORD FAR PASCAL __loadds DLL_ADD(WORD a, WORD b)                /* 2 */
{
    return a + b;
}

WORD FAR PASCAL __loadds DLL_SUB(WORD a, WORD b)                /* 3 */
{
    return a - b;
}

/* MakeProcInstance of one of our own exports, with our own instance */
DWORD FAR PASCAL __loadds DLL_MPI(void)                         /* 6 */
{
    return (DWORD)MakeProcInstance((FARPROC)DLL_ADD, (HINSTANCE)lib_regs[L_DI]);
}

DWORD FAR PASCAL __loadds DLL_SELFADDR(void)                    /* 7 */
{
    return (DWORD)(FARPROC)DLL_ADD;
}

WORD FAR PASCAL __loadds DLL_LALLOC(WORD n)                     /* 8 */
{
    return LocalAlloc(LMEM_FIXED, n);
}

WORD FAR PASCAL __loadds DLL_PEEK(WORD off)                     /* 9 */
{
    return *(WORD __near *)off;
}

int FAR PASCAL __loadds WEP(int param)
{
    (void)param;
    return 1;
}
