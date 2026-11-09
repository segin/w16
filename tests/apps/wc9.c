/*
 * wc9.c - WC9.EXE: how ExitWindowsExec reaches WIN.COM. Run as the shell
 * (SYSTEM.INI shell=C:\WC9.EXE): the first time (no C:\WC9.FLG) it creates
 * C:\WC9.FLG and calls ExitWindowsExec("C:\WC6.COM", "hello  world /x"),
 * so Windows ends, WIN.COM runs that program and then starts Windows again;
 * the second time it deletes the flag and returns, which ends Windows.
 * WC6.COM (tests/dos/wc6.asm) records its command tail and what WIN.COM's
 * INT 2Fh AX=4B20h returns. Black-box probe for w16's WIN.COM
 * (ARCHITECTURE.md 9.5); it notes what it did in C:\WC9.LOG.
 */
#include <windows.h>

#define TK_REF "wc9ref.h"
#include "tklog.h"

/* append a line to C:\WC9.LOG (TK.LOG starts afresh with every start) */
static void note(const char FAR *s)
{
    OFSTRUCT of;
    HFILE h = OpenFile("C:\\WC9.LOG", &of, OF_WRITE);
    WORD n = 0;
    if (h == HFILE_ERROR)
        h = _lcreat("C:\\WC9.LOG", 0);
    if (h == HFILE_ERROR)
        return;
    _llseek(h, 0, 2);
    while (s[n])
        n++;
    _lwrite(h, s, n);
    _lclose(h);
}

int __cdecl tmain(void)
{
    OFSTRUCT of;
    HFILE h;

    if (OpenFile("C:\\WC9.FLG", &of, OF_EXIST) != HFILE_ERROR) {
        OpenFile("C:\\WC9.FLG", &of, OF_DELETE);
        note("WC9 second start\r\n");
        return 0;
    }
    h = _lcreat("C:\\WC9.FLG", 0);
    if (h != HFILE_ERROR)
        _lclose(h);
    note("WC9 first start: ExitWindowsExec\r\n");
    ExitWindowsExec("C:\\WC6.COM", "hello  world /x");
    note("WC9 ExitWindowsExec returned\r\n");
    return 1;
}
