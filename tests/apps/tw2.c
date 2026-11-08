/*
 * tw2.c - Windows 2.x probe: does a 2.x-marked w16 program load and run?
 *
 * Imports only calls that exist in Windows 1.x/2.x (InitTask, WaitEvent,
 * InitApp, GetVersion, CreateWindow, DestroyWindow, ExitWindows). Logs
 * to TK.LOG, then ends the Windows session so headless runs finish.
 * Also asks: does this Windows have the system MDI client class? (A
 * window of an unknown class cannot be created, so CreateWindow tells.)
 */
#include <windows.h>

extern WORD __cdecl init_regs[7];       /* ax bx cx dx si di es */
extern void __cdecl log_write(const char FAR *buf, WORD len);
extern WORD FAR PASCAL WaitEvent(HTASK);
extern BOOL FAR PASCAL InitApp(HINSTANCE);

/* the calls Program Manager's 3.x build uses, and a few alternatives */
static const struct { const char *mod, *name; WORD ord; } api[] = {
#include "tw2api.h"
};

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
    {
        CLIENTCREATESTRUCT cc;
        HWND w;
        cc.hWindowMenu = NULL;
        cc.idFirstChild = 500;
        w = CreateWindow("MDICLIENT", NULL, WS_POPUP, 0, 0, 10, 10, NULL, NULL,
                         (HINSTANCE)init_regs[5], (LPVOID)&cc);
        out("OBS MDICLIENT window created=");
        outhex(w != NULL);
        if (w)
            DestroyWindow(w);
    }
    /* which of them exist here: by name, and by the 3.1 ordinal */
    {
        int i;
        for (i = 0; i < (int)(sizeof api / sizeof api[0]); i++) {
            HINSTANCE h = GetModuleHandle(api[i].mod);
            out("API ");
            out(api[i].mod);
            out(" ");
            out(api[i].name);
            if (!h) {
                out(" nomodule\r\n");
                continue;
            }
            out(GetProcAddress(h, api[i].name) ? " name=1" : " name=0");
            out(api[i].ord && GetProcAddress(h, MAKEINTRESOURCE(api[i].ord)) ? " ord=1\r\n"
                                                                            : " ord=0\r\n");
        }
    }
    out("TW2 calling ExitWindows\r\n");
    ExitWindows(0, 0);
    out("TW2 ExitWindows returned\r\n");
    return 0;
}
