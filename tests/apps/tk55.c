/*
 * tk55.c - the shell that ends at once with exit code 5: what Windows
 * does when its last task ends (Pietrek: KERNEL's ExitCall calls USER's
 * ExitWindows then). Run as the shell under tests/dropin.sh with a batch
 * file around WIN that records WIN's ERRORLEVEL and the video mode after
 * it; BOOTLOG.TXT shows whether USER's ExitWindows ran (INIT=TSRQuery).
 */
#include <windows.h>

#define TK_REF "tk55ref.h"
#include "tklog.h"

int __cdecl tmain(void)
{
    out("TK55 start, ending with 5\r\n");
    return 5;
}
