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
 * A string with no NUL before the end of its segment, and a copy whose
 * source the copy itself overwrites (lstrcat(buf, buf)), would fault on
 * 3.1 too, which then returns 0 (UNVERIFIED: what it leaves in the
 * buffer; it copies up to the fault). w16 returned nothing: lstrlen
 * counted round a 64K segment for ever, the copies ran off the checked
 * length into KERNEL's own fault (KERNEL audit, H28, H29). Now they
 * return 0 there too; a copy whose source changed under it stops at
 * the checked length.
 */
#include "kernel.h"

u16 KAPI IsBadWritePtr(void FAR *p, u16 n);
extern u32 ptr_room(const void FAR *p, int write);          /* kmisc.c */

/* the length of s, if a NUL ends it inside its segment */
static int str_len(const char FAR *s, u16 *len)
{
    u32 rd = ptr_room(s, 0), i;
    for (i = 0; i < rd && i < 0xFFFFUL; i++)
        if (!s[(u16)i]) {
            *len = (u16)i;
            return 1;
        }
    return 0;
}

/* n + 1 bytes of s to d; 0 if the last one copied is not the NUL (s
   overlapped d and was overwritten on the way) */
static int str_copy(char FAR *d, const char FAR *s, u16 n)
{
    u16 i;
    for (i = 0; i <= n; i++)
        d[i] = s[i];
    return d[n] == 0;
}

char FAR * KAPI_ES lstrcpy(char FAR *d, const char FAR *s)      /* KERNEL.88 */
{
    char FAR *r = d;
    u16 n;
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
    if (!str_len(s, &n) || (u32)n + 1 > 0xFFFFUL || IsBadWritePtr(d, n + 1))
        return NULL;
    return str_copy(d, s, n) ? r : NULL;
}

char FAR * KAPI_ES lstrcat(char FAR *d, const char FAR *s)      /* KERNEL.89 */
{
    char FAR *r = d;
    u16 dn, sn;
    if (!d)
        return NULL;
    if (!str_len(d, &dn))
        return NULL;
    if (!s)
        return d;
    if (!str_len(s, &sn) || (u32)dn + sn + 1 > 0xFFFFUL || IsBadWritePtr(d, dn + sn + 1))
        return NULL;
    return str_copy(d + dn, s, sn) ? r : NULL;
}

u16 KAPI_ES lstrlen(const char FAR *s)                           /* KERNEL.90 */
{
    u16 n;
    return s && str_len(s, &n) ? n : 0;
}
