/*
 * tk27.c - the mouse cursor: what LoadCursor(0, IDC_ARROW) gives (the
 * cursor block USER hands the display driver: hot spot, size, width in
 * bytes, planes, bits per pixel, then the masks), as words and a
 * checksum of the masks; and the same for IDC_WAIT. The cursor showed
 * garbled on w16. Run as the shell (tests/dropin.sh, and -r).
 */
#include <windows.h>

#define TK_REF "tk27ref.h"
#include "tklog.h"

extern WORD FAR PASCAL K_WAITEVENT(WORD htask);
extern int FAR PASCAL U_INITAPP(WORD hinst);

static void cursor(const char *what, LPCSTR id)
{
    HCURSOR h = LoadCursor(0, id);
    WORD FAR *p;
    WORD i, sum = 0, n;
    char nm[48];
    lstrcpy(nm, what);
    lstrcat(nm, " LoadCursor != 0");
    obs(nm, h != 0);
    if (!h)
        return;
    lstrcpy(nm, what);
    lstrcat(nm, " GlobalSize");
    obs(nm, (WORD)GlobalSize(h));
    p = (WORD FAR *)GlobalLock(h);
    if (!p)
        return;
    lstrcpy(nm, what);
    dump(nm, SELECTOROF(p), OFFSETOF(p), 8);
    n = (WORD)(GlobalSize(h) / 2);
    for (i = 6; i < n; i++)
        sum = sum * 31 + p[i];
    lstrcpy(nm, what);
    lstrcat(nm, " mask checksum");
    obs(nm, sum);
    GlobalUnlock(h);
}

int __cdecl tmain(void)
{
    out("TK27 start\r\n");
    K_WAITEVENT(0);
    U_INITAPP(init_regs[5]);
    cursor("arrow", IDC_ARROW);
    cursor("wait", IDC_WAIT);
    out("TK27 done, failures=");
    outhex((WORD)failures);
    out("\r\n");
    return failures;
}
