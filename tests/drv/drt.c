/*
 * drt.c - DRT.DLL, the installable driver the DRP probe opens (tests/drv/
 * drp.c; ARCHITECTURE.md 10.7). Its DriverProc logs every call it gets
 * (identifier, handle, message, both parameters, the DRV_OPEN string, the
 * task: the probe's own, another, or none known) to C:\DRP.LOG, the
 * probe's log, and answers as the SDK's example driver does (Programmer's
 * Reference Vol. 1, ch. 25), unless the probe told it to fail a message:
 * DRV_LOAD, DRV_ENABLE, DRV_DISABLE, DRV_FREE, DRV_CLOSE 1; DRV_OPEN
 * D0000000h + a count (the identifier later calls get); everything else
 * goes to DefDriverProc, whose result it logs and returns.
 */
#include <windows.h>

static const char logname[] = "C:\\DRP.LOG";
static char line[200];
static WORD opens;

int __cdecl libmain(void)
{
    return 1;
}

static void put(const char FAR *s, int n)
{
    HFILE f = _lopen(logname, OF_WRITE);
    if (f == HFILE_ERROR)
        f = _lcreat(logname, 0);
    else
        _llseek(f, 0, 2);
    _lwrite(f, s, n);
    _lclose(f);
}

LRESULT FAR PASCAL __loadds DriverProc(DWORD id, HDRVR h, UINT msg,
                                       LPARAM l1, LPARAM l2)      /* 1 */
{
    LRESULT r;
    int n, def = 0;
    HTASK t = GetCurrentTask();
    /* SYSTEM.INI [drt] fail= (the message answered 0) and task= (the
       probe's task), written by the probe (drp.c, DRT_SET) */
    WORD failmsg = GetPrivateProfileInt("drt", "fail", 0, "SYSTEM.INI");
    HTASK probe = (HTASK)GetPrivateProfileInt("drt", "task", 0, "SYSTEM.INI");
    switch (msg) {
    case DRV_LOAD: case DRV_ENABLE: case DRV_DISABLE: case DRV_FREE:
    case DRV_CLOSE:
        r = 1;
        break;
    case DRV_OPEN:
        r = 0xD0000000L + ++opens;
        break;
    case DRV_USER + 2: {
        /* GlobalAlloc(lParam1, 600h) from a DLL: the handle's low two bits,
           GlobalFlags in the high word (the probe's "galloc" section) */
        HGLOBAL g = GlobalAlloc((WORD)l1, 0x600);
        r = ((WORD)g & 3) | ((DWORD)GlobalFlags(g) << 16);
        GlobalFree(g);
        break; }
    default:
        r = DefDriverProc(id, h, msg, l1, l2);
        def = 1;
        break;
    }
    if (msg == failmsg)
        r = 0;
    if (msg == DRV_OPEN && l1)          /* a pointer: its string */
        n = wsprintf(line, "  DP id=%08lX h=%04X msg=%04X l1=\"%s\" l2=%08lX",
                     id, (WORD)h, msg,
                     IsBadStringPtr((LPCSTR)l1, 64) ? (LPSTR)"?" : (LPSTR)l1, l2);
    else
        n = wsprintf(line, "  DP id=%08lX h=%04X msg=%04X l1=%08lX l2=%08lX",
                     id, (WORD)h, msg, l1, l2);
    n += wsprintf(line + n, " %s=%08lX task=%s\r\n", def ? (LPSTR)"def" : (LPSTR)"ret",
                  r, !probe ? (LPSTR)"-" : t == probe ? (LPSTR)"probe" : (LPSTR)"other");
    put(line, n);
    return r;
}
