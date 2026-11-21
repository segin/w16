/*
 * reg.c - w16 REGEDIT: small helpers over SHELL's registration API
 * (RegOpenKey ... RegEnumKey, the SDK's documented functions; ARCHITECTURE.md
 * 13.2). A path here is relative to HKEY_CLASSES_ROOT without a leading
 * backslash ("MPlayer\shell"); "" is HKEY_CLASSES_ROOT itself.
 */
#include "regedit.h"
#include <shellapi.h>

LPSTR path_last(LPSTR path)
{
    LPSTR p = path, last = path;
    for (; *p; p = AnsiNext(p))
        if (*p == '\\')
            last = p + 1;
    return last;
}

LONG reg_get(LPCSTR path, LPSTR buf, LONG cb)
{
    LONG r;
    buf[0] = 0;
    r = RegQueryValue(HKEY_CLASSES_ROOT, (LPSTR)path, buf, &cb);
    if (r)
        buf[0] = 0;
    return r;
}

LONG reg_set(LPCSTR path, LPCSTR value)
{
    return RegSetValue(HKEY_CLASSES_ROOT, (LPSTR)path, REG_SZ, (LPSTR)value, lstrlen(value));
}

BOOL reg_exists(LPCSTR path)
{
    HKEY k;
    if (RegOpenKey(HKEY_CLASSES_ROOT, (LPSTR)path, &k))
        return FALSE;
    RegCloseKey(k);
    return TRUE;
}

LONG reg_create(LPCSTR path)
{
    HKEY k;
    LONG r = RegCreateKey(HKEY_CLASSES_ROOT, (LPSTR)path, &k);
    if (!r)
        RegCloseKey(k);
    return r;
}

LONG reg_delete(LPCSTR path)
{
    return RegDeleteKey(HKEY_CLASSES_ROOT, (LPSTR)path);
}

/* a copy of the subtree at from as to (made if need be): its value, then
   each subkey in RegEnumKey's order (newest first), recursively. The
   paths grow in one global buffer pair (bound: PATHMAX) */
static char cf[PATHMAX], ct[PATHMAX];

static LONG copy_rec(void)
{
    static char val[PATHMAX];
    HKEY k;
    LONG r;
    DWORD i;
    int lf = lstrlen(cf), lt = lstrlen(ct);
    if ((r = reg_create(ct)) != 0)
        return r;
    if (!reg_get(cf, val, sizeof val) && val[0])
        reg_set(ct, val);
    if (RegOpenKey(HKEY_CLASSES_ROOT, cf, &k))
        return 0;
    for (i = 0;; i++) {
        char name[128];
        if (RegEnumKey(k, i, name, sizeof name))
            break;
        if (lf + lstrlen(name) + 2 >= PATHMAX || lt + lstrlen(name) + 2 >= PATHMAX)
            continue;
        if (lf) lstrcat(cf, "\\");
        lstrcat(cf, name);
        lstrcat(ct, "\\");
        lstrcat(ct, name);
        r = copy_rec();
        cf[lf] = 0;
        ct[lt] = 0;
        if (r)
            break;
    }
    RegCloseKey(k);
    return r;
}

LONG reg_copy(LPCSTR from, LPCSTR to)
{
    LONG r;
    HKEY k;
    /* held open so that the whole copy reaches REG.DAT in one write */
    RegOpenKey(HKEY_CLASSES_ROOT, NULL, &k);
    lstrcpy(cf, from);
    lstrcpy(ct, to);
    r = copy_rec();
    RegCloseKey(k);
    return r;
}
