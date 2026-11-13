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
 *
 * A pointer with a selector that is not valid (TK39, 3.1: allocated and
 * freed) makes them return 0 (lstrcpy and lstrcat: AX 0, DX not
 * meaningful) and change nothing: 3.1's KERNEL lets the access fault
 * and recovers through its "__GP" table (fault.c). w16 checks first.
 * UNVERIFIED: a string running off the end of its segment (3.1 would
 * have copied up to the fault).
 */
#include "kernel.h"

u16 KAPI IsBadStringPtr(const char FAR *p, u16 max);
u16 KAPI IsBadWritePtr(void FAR *p, u16 n);

char FAR * KAPI_ES lstrcpy(char FAR *d, const char FAR *s)      /* KERNEL.88 */
{
    char FAR *r = d;
    if (!d)
        return NULL;
    if (!s) {
        if (IsBadWritePtr(d, 1))
            return NULL;
        *d = 0;
        return d;
    }
    /* lengths in 32 bits: a 64K string's length + 1 wrapped to 0, which
       IsBadWritePtr passed (KERNEL audit, low) */
    if (IsBadStringPtr(s, 0xFFFF) || (u32)kstrlen(s) + 1 > 0xFFFFUL ||
        IsBadWritePtr(d, kstrlen(s) + 1))
        return NULL;
    while ((*d++ = *s++) != 0)
        ;
    return r;
}

char FAR * KAPI_ES lstrcat(char FAR *d, const char FAR *s)      /* KERNEL.89 */
{
    char FAR *r = d;
    if (!d)
        return NULL;
    if (IsBadStringPtr(d, 0xFFFF))
        return NULL;
    if (!s)
        return d;
    if (IsBadStringPtr(s, 0xFFFF) ||
        (u32)kstrlen(d) + kstrlen(s) + 1 > 0xFFFFUL ||
        IsBadWritePtr(d, kstrlen(d) + kstrlen(s) + 1))
        return NULL;
    while (*d)
        d++;
    while ((*d++ = *s++) != 0)
        ;
    return r;
}

u16 KAPI_ES lstrlen(const char FAR *s)                           /* KERNEL.90 */
{
    return s && !IsBadStringPtr(s, 0xFFFF) ? kstrlen(s) : 0;
}
