/*
 * tkwa.c - TKWA.DLL, which imports TKWB.DLL (tkwb.c): TK62 "wep" records
 * the order of the two WEPs when both go at once (the KERNEL audit's
 * H11). WA_SET(p) gives both the probe's buffer; its WEP appends 'A'.
 */
#include <windows.h>
#include "tkdll.h"

extern void FAR PASCAL WB_SET(char FAR *p);

static char FAR *log;

int __cdecl libmain(void)
{
    lib_heap();
    return 1;
}

void FAR PASCAL __loadds WA_SET(char FAR *p)                    /* 1 */
{
    log = p;
    WB_SET(p);
}

int FAR PASCAL __loadds WEP(int param)
{
    (void)param;
    if (log) {
        while (*log)
            log++;
        log[0] = 'A';
        log[1] = 0;
    }
    return 1;
}

WORD FAR PASCAL __loadds WA_INFO(WORD i)                      /* 3 */
{
    return lib_info(i);
}
