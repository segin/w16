/*
 * tk80.c - how KERNEL starts a 32-bit program (a PE file) when Win32s'
 * loader, W32SYS.DLL, is there. Run as the shell with TK80D.DLL
 * (tests/apps/tk80d.c) as W32SYS.DLL in the system directory and PE.EXE
 * (tests/scripts/runassocfiles.py) in C:\. Pietrek (Windows Internals,
 * ch. 3, ExecPE) has KERNEL's EXEC load W32SYS.DLL and go on in its
 * third entry with the command line and nCmdShow on the stack; TK80D logs
 * what that entry is given ("E" lines). TK80 logs, to C:\TK80.LOG:
 *   A v      WinExec("C:\PE.EXE one  two", SW_SHOWMINIMIZED)
 *   B v      WinExec("PE", SW_SHOW) (C:\ current)
 *   C v      LoadModule("C:\PE.EXE", a parameter block of its own)
 *   M h u    W32SYS's module handle and usage after them (0 0: none)
 *   T done   (and C:\TK80.END for the test to stop)
 */
#include <windows.h>

static void put(const char *s)
{
    HFILE f = _lopen("C:\\TK80.LOG", OF_READWRITE);
    int n = 0;
    if (f == HFILE_ERROR)
        f = _lcreat("C:\\TK80.LOG", 0);
    if (f == HFILE_ERROR)
        return;
    _llseek(f, 0, 2);
    while (s[n])
        n++;
    _lwrite(f, s, n);
    _lclose(f);
}

static void puthex(char tag, WORD v, WORD v2, int two)
{
    char b[16];
    int i, n = 0;
    b[n++] = tag;
    b[n++] = ' ';
    for (i = 12; i >= 0; i -= 4)
        b[n++] = "0123456789ABCDEF"[(v >> i) & 15];
    if (two) {
        b[n++] = ' ';
        for (i = 12; i >= 0; i -= 4)
            b[n++] = "0123456789ABCDEF"[(v2 >> i) & 15];
    }
    b[n++] = '\r';
    b[n++] = '\n';
    b[n] = 0;
    put(b);
}

static BYTE tail[] = "\x05 abc\r";
static WORD show[2] = { 2, SW_SHOWNOACTIVATE };

int __cdecl tmain(void)
{
    struct { WORD env; LPSTR cmd; LPVOID show; DWORD res; } lp;
    HMODULE h;

    put("T start\r\n");
    puthex('A', WinExec("C:\\PE.EXE one  two", SW_SHOWMINIMIZED), 0, 0);
    puthex('B', WinExec("PE", SW_SHOW), 0, 0);
    lp.env = 0;
    lp.cmd = (LPSTR)tail;
    lp.show = show;
    lp.res = 0;
    puthex('C', LoadModule("C:\\PE.EXE", &lp), 0, 0);
    h = GetModuleHandle("W32SYS");
    puthex('M', (WORD)h, h ? GetModuleUsage(h) : 0, 1);
    put("T done\r\n");
    _lclose(_lcreat("C:\\TK80.END", 0));
    return 0;
}
