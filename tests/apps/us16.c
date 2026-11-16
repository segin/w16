/*
 * us16.c - probe US16: what RegisterClass returns. Built twice, US16.EXE
 * (the linker's expected Windows version, 3.00) and US16A.EXE (marked 3.10
 * with tools/nemark.py); each names its observations after the version
 * it was marked with. Run as the shell on Windows 3.1 (tests/refrun.sh)
 * and with w16's USER (DROPIN_FIXED=1 tests/userboot.sh -n -t 40 -s ...).
 *
 * The 3.1 SDK declares ATOM RegisterClass; Pietrek (Windows Internals,
 * ch. 4) says its result is "either 0, 1, or the atom handle of the class
 * name"; US15 (marked 3.00) got 1. Observed for a string name, a long one,
 * "#nnnnn", an integer atom, CS_GLOBALCLASS, a local class named like a
 * system class, and a second registration, against GetClassInfo's result
 * and GetClassWord(GCW_ATOM).
 */
#include <windows.h>

#define TK_REF "us16ref.h"
#include "tklog.h"

extern WORD FAR PASCAL WaitEvent(HTASK);
extern BOOL FAR PASCAL InitApp(HINSTANCE);

static HINSTANCE hi;
static char pfx[8] = "3.00: ";
static char name[80];

static void nm(const char *b)
{
    int i = 0;
    const char *a = pfx;
    while (*a)
        name[i++] = *a++;
    while (*b && i < 78)
        name[i++] = *b++;
    name[i] = 0;
}

static ATOM reg(LPCSTR cls, UINT style)
{
    WNDCLASS wc;
    wc.style = style;
    wc.lpfnWndProc = DefWindowProc;
    wc.cbClsExtra = 0;
    wc.cbWndExtra = 0;
    wc.hInstance = hi;
    wc.hIcon = 0;
    wc.hCursor = 0;
    wc.hbrBackground = 0;
    wc.lpszMenuName = NULL;
    wc.lpszClassName = cls;
    return RegisterClass(&wc);
}

static void one(const char *what, LPCSTR cls, UINT style)
{
    ATOM r = reg(cls, style);
    WNDCLASS wc;
    ATOM info = (ATOM)GetClassInfo(hi, cls, &wc);
    HWND w = CreateWindow(cls, "", WS_POPUP, 0, 0, 10, 10, NULL, NULL, hi, NULL);
    WORD gcw = w ? GetClassWord(w, -32) : 0;   /* GCW_ATOM */
    char t[64];
    int i = 0;
    while (*what)
        t[i++] = *what++;
    t[i] = 0;
    lstrcpy(t + i, " returns nonzero");
    nm(t);
    obs(name, r != 0);
    lstrcpy(t + i, " returns 1");
    nm(t);
    obs(name, r == 1);
    lstrcpy(t + i, " returns GetClassInfo's");
    nm(t);
    obs(name, r == info);
    lstrcpy(t + i, " GetClassInfo == GCW_ATOM");
    nm(t);
    obs(name, info == gcw);
    lstrcpy(t + i, " value");
    nm(t);
    obs_env(name, r);
    lstrcpy(t + i, " GetClassInfo value");
    nm(t);
    obs_env(name, info);
    if (w)
        DestroyWindow(w);
}

int __cdecl tmain(void)
{
    char path[128];
    int i, n;
    hi = (HINSTANCE)init_regs[5];
    WaitEvent(0);
    InitApp(hi);
    out("US16 start\r\n");
    n = GetModuleFileName(hi, path, sizeof path);
    for (i = 0; i < n; i++)
        if (path[i] == 'A' && path[i + 1] == '.')
            lstrcpy(pfx, "3.10: ");
    out(pfx);
    out("\r\n");
    one("string name", "US16CLASS", 0);
    one("20-char name", "ABCDEFGHIJKLMNOPQRST", 0);
    one("#12345 name", "#12345", 0);
    one("integer atom name", MAKEINTRESOURCE(12346), 0);
    one("CS_GLOBALCLASS", "US16GLOBAL", CS_GLOBALCLASS);
    one("local Button", "Button", 0);
    nm("again: returns 0");
    obs(name, reg("US16CLASS", 0) == 0);
    nm("Unregister then again: returns nonzero");
    UnregisterClass("US16CLASS", hi);
    obs(name, reg("US16CLASS", 0) != 0);
    out("US16 end\r\n");
    return 0;
}
