/*
 * tm1.c - probe TM1: how the Task List is started (ARCHITECTURE.md 15.5).
 *
 * Installed as SYSTEM.INI [boot] taskman.exe=TM1.EXE, or as TASKMAN.EXE
 * itself, in a scratch copy of the reference install. Each start appends
 * to C:\TM1.LOG: the command line, nCmdShow, hPrevInstance, the pointer's
 * place, the active and focus windows, and every top-level window in Z
 * order (handle, class, title, style, extended style, owner, task,
 * IsWinOldApTask, last active popup, rectangle); then it ends. With "/s" on its
 * command line it stays (a dialog-less wait of 5 s, messages pumped) so
 * that a second start finds it running.
 */
#include <windows.h>

extern BOOL FAR PASCAL IsWinOldApTask(HTASK);

static HFILE logf = HFILE_ERROR;

static void out(const char FAR *s)
{
    if (logf != HFILE_ERROR)
        _lwrite(logf, s, lstrlen(s));
}

static void logwin(HWND h)
{
    char b[200], cls[40], title[80];
    HTASK t = GetWindowTask(h);
    GetClassName(h, cls, sizeof cls);
    title[0] = 0;
    GetWindowText(h, title, sizeof title);
    wsprintf(b, "  %04X %-10s st=%08lX ex=%08lX own=%04X task=%04X woa=%d vis=%d ico=%d en=%d pop=%04X \"%s\"\r\n",
             h, (LPSTR)cls, GetWindowLong(h, GWL_STYLE), GetWindowLong(h, GWL_EXSTYLE),
             GetWindow(h, GW_OWNER), t, IsWinOldApTask(t) ? 1 : 0, IsWindowVisible(h) ? 1 : 0,
             IsIconic(h) ? 1 : 0, IsWindowEnabled(h) ? 1 : 0, GetLastActivePopup(h), (LPSTR)title);
    out(b);
    {
        RECT r;
        GetWindowRect(h, &r);
        wsprintf(b, "        at %d,%d %dx%d cs=%04X\r\n", r.left, r.top, r.right - r.left, r.bottom - r.top,
                 GetClassWord(h, GCW_STYLE));
        out(b);
    }
}

int PASCAL WinMain(HINSTANCE hi, HINSTANCE hp, LPSTR cmd, int show)
{
    char b[160];
    POINT pt;
    HWND h;
    (void)hi;
    logf = _lopen("C:\\TM1.LOG", OF_WRITE);
    if (logf == HFILE_ERROR)
        logf = _lcreat("C:\\TM1.LOG", 0);
    else
        _llseek(logf, 0, 2);
    GetCursorPos(&pt);
    wsprintf(b, "TM1 cmd=\"%s\" show=%d prev=%04X cursor=%d,%d active=%04X focus=%04X capture=%04X\r\n",
             cmd, show, hp, pt.x, pt.y, GetActiveWindow(), GetFocus(), GetCapture());
    out(b);
    for (h = GetWindow(GetDesktopWindow(), GW_CHILD); h; h = GetWindow(h, GW_HWNDNEXT))
        logwin(h);
    out("end\r\n");
    _lclose(logf);
    if (cmd[0] == '/' && (cmd[1] == 's' || cmd[1] == 'S')) {
        DWORD t0 = GetTickCount();
        MSG m;
        while (GetTickCount() - t0 < 5000)
            while (PeekMessage(&m, 0, 0, 0, PM_REMOVE))
                DispatchMessage(&m);
    }
    return 0;
}
