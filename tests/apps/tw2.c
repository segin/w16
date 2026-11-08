/*
 * tw2.c - Windows 2.x probe: does a 2.x-marked w16 program load and run?
 *
 * Imports only calls that exist in Windows 1.x/2.x (InitTask, WaitEvent,
 * InitApp, GetVersion, ExitWindows). Logs to TK.LOG, then ends the
 * Windows session so headless runs finish.
 */
#include <windows.h>

extern WORD __cdecl init_regs[7];       /* ax bx cx dx si di es */
extern void __cdecl log_write(const char FAR *buf, WORD len);
extern WORD FAR PASCAL WaitEvent(HTASK);
extern BOOL FAR PASCAL InitApp(HINSTANCE);

static void out(const char *s)
{
    WORD n = 0;
    while (s[n])
        n++;
    log_write(s, n);
}

static void outhex(WORD v)
{
    char b[7];
    int i;
    for (i = 3; i >= 0; i--) {
        b[i] = "0123456789ABCDEF"[v & 15];
        v >>= 4;
    }
    b[4] = '\r';
    b[5] = '\n';
    b[6] = 0;
    out(b);
}

int __cdecl tmain(void)
{
    DWORD v;
    out("TW2 start\r\n");
    WaitEvent(0);
    out("OBS InitApp=");
    outhex((WORD)InitApp((HINSTANCE)init_regs[5]));
    v = GetVersion();
    out("OBS GetVersion.lo=");
    outhex(LOWORD(v));
    out("OBS GetVersion.hi=");
    outhex(HIWORD(v));
    out("TW2 calling ExitWindows\r\n");
    ExitWindows(0, 0);
    out("TW2 ExitWindows returned\r\n");
    return 0;
}
