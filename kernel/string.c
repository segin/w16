/*
 * string.c - KERNEL's string APIs.
 *
 * Observed on Windows 3.1 (tests/apps/tk4.c, Standard and 386 Enhanced):
 * lstrcpy and lstrcat return the destination, lstrcpy(NULL, s) returns
 * NULL. A NULL far pointer did not fault there: it behaved like a small
 * scratch string (lstrlen(NULL) = 2, lstrcpy(buf, NULL) copied two odd
 * bytes, and after lstrcpy(NULL, "x") lstrcat(buf, NULL) appended "x").
 * w16 does not copy that: NULL reads as an empty string and is never
 * written. UNVERIFIED: whether any program depends on the Windows quirk.
 */
#include "kernel.h"

char FAR * KAPI_ES lstrcpy(char FAR *d, const char FAR *s)      /* KERNEL.88 */
{
    char FAR *r = d;
    if (!d)
        return NULL;
    if (!s) {
        *d = 0;
        return d;
    }
    while ((*d++ = *s++) != 0)
        ;
    return r;
}

char FAR * KAPI_ES lstrcat(char FAR *d, const char FAR *s)      /* KERNEL.89 */
{
    char FAR *r = d;
    if (!d)
        return NULL;
    if (!s)
        return d;
    while (*d)
        d++;
    while ((*d++ = *s++) != 0)
        ;
    return r;
}

u16 KAPI_ES lstrlen(const char FAR *s)                           /* KERNEL.90 */
{
    return s ? kstrlen(s) : 0;
}
