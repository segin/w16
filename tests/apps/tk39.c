/*
 * tk39.c - calls given a far pointer whose selector is not valid (one
 * allocated and freed again), to see which are caught by a module's
 * "__GP" table (tk38.c) and what they return then. Logs each return
 * value; on w16 an unrecovered fault ends the run with KERNEL's report.
 * Run as the shell (tests/dropin.sh, and -r for 3.1).
 */
#include <windows.h>

#define TK_REF "tk39ref.h"
#include "tklog.h"

int __cdecl tmain(void)
{
    WORD sel = AllocSelector(0);
    LPSTR bad;
    HDC dc;
    RECT r;
    char buf[64];
    DWORD d;
    int i;

    FreeSelector(sel);
    bad = (LPSTR)MAKELP(sel, 0);
    out("TK39 start\r\n");
    obs("bad selector", sel);
    i = lstrlen(bad);
    obs("lstrlen", i);
    buf[0] = 'A', buf[1] = 0;
    d = (DWORD)lstrcpy(buf, bad);
    obs("lstrcpy(buf, bad) is buf", d == (DWORD)(LPSTR)buf);
    obs("lstrcpy(buf, bad) hi", HIWORD(d));
    obs("lstrcpy(buf, bad) lo is buf's", LOWORD(d) == OFFSETOF((LPSTR)buf));
    obs("  buf[0]", (BYTE)buf[0]);
    buf[0] = 'A', buf[1] = 0;
    d = (DWORD)lstrcat(buf, bad);
    obs("lstrcat(buf, bad) is buf", d == (DWORD)(LPSTR)buf);
    obs("lstrcat(buf, bad) hi", HIWORD(d));
    obs("  buf[0]", (BYTE)buf[0]);
    obs("  buf[1]", (BYTE)buf[1]);
    d = (DWORD)lstrcpy(bad, "x");
    obs("lstrcpy(bad, x) hi", HIWORD(d));
    obs("lstrcpy(bad, x) lo", LOWORD(d));
    d = (DWORD)lstrcat(bad, "x");
    obs("lstrcat(bad, x) hi", HIWORD(d));
    obs("lstrcat(bad, x) lo", LOWORD(d));
    dc = GetDC(0);
    i = TextOut(dc, 0, 0, bad, 5);
    obs("TextOut", i);
    d = GetTextExtent(dc, bad, 5);
    obs("GetTextExtent lo", LOWORD(d));
    obs("GetTextExtent hi", HIWORD(d));
    SetRect(&r, 0, 0, 100, 100);
    i = DrawText(dc, bad, 5, &r, 0);
    obs("DrawText", i);
    i = ExtTextOut(dc, 0, 0, 0, NULL, bad, 5, NULL);
    obs("ExtTextOut", i);
    ReleaseDC(0, dc);
    i = GetWindowText(GetDesktopWindow(), bad, 10);
    obs("GetWindowText", i);
    i = wsprintf(buf, bad);
    obs("wsprintf", i);
    i = (int)FindWindow(bad, NULL);
    obs("FindWindow", i);
    i = (int)LoadString(GetModuleHandle("USER"), 1, bad, 10);
    obs("LoadString", i);
    out("TK39 done\r\n");
    return 0;
}
