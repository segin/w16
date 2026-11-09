/*
 * ocsrv.c - OCSRV.EXE: a launch recorder for OLE servers (probe OC3,
 * ARCHITECTURE.md 16). Registered as a class's StdFileEditing server in a
 * scratch copy's registration database, it logs the command line and the
 * show command the OLE client library starts it with (InitTask's ES:BX and
 * DX), then starts the real server (C:\WINDOWS\<argument of OCSRV.TXT>)
 * with the same command line and show command and exits after a moment.
 */
#include <windows.h>

extern WORD __cdecl init_regs[7];       /* ax bx cx dx si di es */
extern WORD FAR PASCAL WaitEvent(HTASK);
extern BOOL FAR PASCAL InitApp(HINSTANCE);
void FAR PASCAL SpyLog(const char FAR *s);

static char line[400];
static WORD lp_;

static void put(const char FAR *s)
{
    while (*s && lp_ < sizeof line - 4)
        line[lp_++] = *s++;
}

static void puthex(WORD v)
{
    int i;
    for (i = 3; i >= 0; i--)
        line[lp_++] = "0123456789ABCDEF"[(v >> (i * 4)) & 15];
}

int __cdecl tmain(void)
{
    BYTE FAR *cmd = (BYTE FAR *)MAKELP(init_regs[6], init_regs[1]);
    static char real[200], run[400];
    WORD i, n = 0;
    HFILE f;
    MSG m;
    DWORD end;

    WaitEvent(0);
    InitApp((HINSTANCE)init_regs[5]);
    put("OCSRV cmdline=\"");
    for (i = 0; cmd[i] && i < 300; i++) {
        if (cmd[i] < 32 || cmd[i] == '"' || cmd[i] == '\\') {
            put("\\x");
            puthex(cmd[i]);
        } else {
            line[lp_++] = cmd[i];
        }
    }
    put("\" show=");
    puthex(init_regs[3]);
    put("\r\n");
    line[lp_] = 0;
    SpyLog(line);
    f = _lopen("C:\\OCSRV.TXT", OF_READ);
    if (f != HFILE_ERROR) {
        n = _lread(f, real, sizeof real - 1);
        _lclose(f);
    }
    while (n && (real[n - 1] == '\r' || real[n - 1] == '\n' || real[n - 1] == ' '))
        n--;
    real[n] = 0;
    if (n) {
        lstrcpy(run, real);
        lstrcat(run, " ");
        lstrcat(run, (LPCSTR)cmd);
        WinExec(run, init_regs[3]);
    }
    end = GetTickCount() + 500;
    while ((long)(GetTickCount() - end) < 0) {
        while (PeekMessage(&m, NULL, 0, 0, PM_REMOVE))
            DispatchMessage(&m);
        Yield();
    }
    return 0;
}
