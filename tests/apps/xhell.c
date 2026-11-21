/*
 * xhell.c - XHELL.DLL, a logging stand-in between a program and SHELL's
 * registration functions (probe RE15, ARCHITECTURE.md 15.8): a scratch
 * copy of the program gets its SHELL imports renamed to XHELL
 * (tools/nerename.py), and each call of RegOpenKey ... RegEnumKey is
 * written to C:\XHELL.LOG with its arguments and result, then passed on to
 * SHELL. Black-box observation of the calls a program makes, as US1 does
 * for USER (ARCHITECTURE.md 10); the program itself is not looked into.
 * DragAcceptFiles, DragQueryFile, DragFinish and ShellAbout pass through.
 */
#include <windows.h>
#include <shellapi.h>

int FAR PASCAL ShellAbout(HWND, LPCSTR, LPCSTR, HICON);

static void logline(LPCSTR s)
{
    OFSTRUCT of;
    HFILE f = OpenFile("C:\\XHELL.LOG", &of, OF_WRITE);
    if (f == HFILE_ERROR)
        f = OpenFile("C:\\XHELL.LOG", &of, OF_CREATE | OF_WRITE);
    if (f == HFILE_ERROR)
        return;
    _llseek(f, 0, 2);
    _lwrite(f, s, lstrlen(s));
    _lwrite(f, "\r\n", 2);
    _lclose(f);
}

static LPCSTR str(LPCSTR s)
{
    return s ? s : "(null)";
}

LONG FAR PASCAL __loadds XRegOpenKey(HKEY k, LPCSTR sub, HKEY FAR *r)
{
    char b[300];
    LONG v = RegOpenKey(k, (LPSTR)sub, r);
    wsprintf(b, "RegOpenKey %08lX \"%.200s\" -> %ld %08lX", k, str(sub), v, v ? 0L : *r);
    logline(b);
    return v;
}

LONG FAR PASCAL __loadds XRegCreateKey(HKEY k, LPCSTR sub, HKEY FAR *r)
{
    char b[300];
    LONG v = RegCreateKey(k, (LPSTR)sub, r);
    wsprintf(b, "RegCreateKey %08lX \"%.200s\" -> %ld %08lX", k, str(sub), v, v ? 0L : *r);
    logline(b);
    return v;
}

LONG FAR PASCAL __loadds XRegCloseKey(HKEY k)
{
    char b[80];
    LONG v = RegCloseKey(k);
    wsprintf(b, "RegCloseKey %08lX -> %ld", k, v);
    logline(b);
    return v;
}

LONG FAR PASCAL __loadds XRegDeleteKey(HKEY k, LPCSTR sub)
{
    char b[300];
    LONG v = RegDeleteKey(k, (LPSTR)sub);
    wsprintf(b, "RegDeleteKey %08lX \"%.200s\" -> %ld", k, str(sub), v);
    logline(b);
    return v;
}

LONG FAR PASCAL __loadds XRegSetValue(HKEY k, LPCSTR sub, DWORD t, LPCSTR d, DWORD cb)
{
    char b[400];
    LONG v = RegSetValue(k, (LPSTR)sub, t, (LPSTR)d, cb);
    wsprintf(b, "RegSetValue %08lX \"%.150s\" %ld \"%.150s\" %ld -> %ld", k, str(sub), t, str(d), cb, v);
    logline(b);
    return v;
}

LONG FAR PASCAL __loadds XRegQueryValue(HKEY k, LPCSTR sub, LPSTR d, LONG FAR *cb)
{
    char b[400];
    LONG in = cb ? *cb : -1, v = RegQueryValue(k, (LPSTR)sub, d, cb);
    wsprintf(b, "RegQueryValue %08lX \"%.150s\" %ld -> %ld \"%.150s\" %ld", k, str(sub), in, v,
             (LPSTR)(v || !d ? "" : d), cb ? *cb : -1L);
    logline(b);
    return v;
}

LONG FAR PASCAL __loadds XRegEnumKey(HKEY k, DWORD i, LPSTR d, DWORD cb)
{
    char b[300];
    LONG v = RegEnumKey(k, i, d, cb);
    wsprintf(b, "RegEnumKey %08lX %ld %ld -> %ld \"%.150s\"", k, i, cb, v, (LPSTR)(v ? "" : d));
    logline(b);
    return v;
}

void FAR PASCAL __loadds XDragAcceptFiles(HWND w, BOOL f)
{
    DragAcceptFiles(w, f);
}

UINT FAR PASCAL __loadds XDragQueryFile(HDROP h, UINT i, LPSTR s, UINT cb)
{
    return DragQueryFile(h, i, s, cb);
}

void FAR PASCAL __loadds XDragFinish(HDROP h)
{
    DragFinish(h);
}

int FAR PASCAL __loadds XShellAbout(HWND w, LPCSTR a, LPCSTR o, HICON i)
{
    return ShellAbout(w, a, o, i);
}

/* the entry routine (tests/apps/dllstart.asm) */
int __cdecl __far libmain(void)
{
    return 1;
}

int FAR PASCAL __loadds WEP(int param)
{
    (void)param;
    return 1;
}
