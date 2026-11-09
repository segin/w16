/*
 * tk28.c - cursors from a program's resources, PRELOAD FIXED and
 * LOADONCALL DISCARDABLE (tk28.rc): the block LoadCursor gives, after
 * InitApp has installed USER's cursor handler for the program. The
 * display driver's system cursors are PRELOAD FIXED, and w16 handed them
 * to USER as raw DIBs (TK27). Run as the shell (tests/dropin.sh, -r).
 */
#include <windows.h>

#define TK_REF "tk28ref.h"
#include "tklog.h"

extern WORD FAR PASCAL K_WAITEVENT(WORD htask);
extern int FAR PASCAL U_INITAPP(WORD hinst);

static void cursor(const char *what, WORD hinst, LPCSTR id, LPCSTR gid)
{
    HCURSOR h;
    HRSRC r;
    HGLOBAL g;
    WORD FAR *p;
    char nm[48];
    /* the RT_CURSOR resource itself, before LoadCursor */
    r = FindResource(hinst, id, RT_CURSOR);
    lstrcpy(nm, what);
    lstrcat(nm, " FindResource(RT_CURSOR) != 0");
    obs(nm, r != 0);
    if (r) {
        g = LoadResource(hinst, r);
        p = (WORD FAR *)LockResource(g);
        lstrcpy(nm, what);
        lstrcat(nm, " LoadResource raw");
        if (p)
            dump(nm, SELECTOROF(p), OFFSETOF(p), 6);
        lstrcpy(nm, what);
        lstrcat(nm, " LoadResource GlobalSize");
        obs(nm, (WORD)GlobalSize(g));
        GlobalUnlock(g);
    }
    h = LoadCursor(hinst, gid);
    lstrcpy(nm, what);
    lstrcat(nm, " LoadCursor != 0");
    obs(nm, h != 0);
    if (!h)
        return;
    lstrcpy(nm, what);
    lstrcat(nm, " LoadCursor GlobalSize");
    obs(nm, (WORD)GlobalSize(h));
    p = (WORD FAR *)GlobalLock(h);
    lstrcpy(nm, what);
    lstrcat(nm, " LoadCursor");
    if (p)
        dump(nm, SELECTOROF(p), OFFSETOF(p), 6);
    GlobalUnlock(h);
}

int __cdecl tmain(void)
{
    WORD hinst = init_regs[5];
    out("TK28 start\r\n");
    K_WAITEVENT(0);
    U_INITAPP(hinst);
    /* wrc numbers the RT_CURSOR resources 1, 2 for groups 10, 20 */
    cursor("preload fixed", hinst, MAKEINTRESOURCE(1), MAKEINTRESOURCE(10));
    cursor("loadoncall discardable", hinst, MAKEINTRESOURCE(2), MAKEINTRESOURCE(20));
    out("TK28 done, failures=");
    outhex((WORD)failures);
    out("\r\n");
    return failures;
}
