/*
 * tkwb.c - TKWB.DLL, imported by TKWA.DLL (tkwa.c): TK62 "wep" records
 * the order of the two WEPs when both go at once (the KERNEL audit's
 * H11). WB_SET(p) gives it the probe's buffer; its WEP appends 'B'.
 */
#include <windows.h>
#include "tkdll.h"

static char FAR *log;

int __cdecl libmain(void)
{
    lib_heap();
    return 1;
}

void FAR PASCAL __loadds WB_SET(char FAR *p)                    /* 1 */
{
    log = p;
}

int FAR PASCAL __loadds WEP(int param)
{
    (void)param;
    if (log) {
        while (*log)
            log++;
        log[0] = 'B';
        log[1] = 0;
    }
    return 1;
}

WORD FAR PASCAL __loadds WB_INFO(WORD i)                      /* 3 */
{
    return lib_info(i);
}
