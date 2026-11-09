/*
 * sh12.c - SH12: when SHELL copies WIN.INI's [embedding] section into a
 * registration database it made from nothing (SH2 without REG.DAT saw the
 * OLE classes appear midway). Run with REG.DAT deleted:
 *   tests/shellprobe.sh -m /S -p 'DEL C:\WINDOWS\REG.DAT' SH12
 * It asks for HKEY_CLASSES_ROOT\SoundRec after each kind of call, first
 * without dispatching messages, then while it does, and logs the first
 * step at which the key is there.
 */
#include <windows.h>

#define TK_REF "sh12ref.h"
#include "tklog.h"
#include "shprobe.h"

#define HKCR ((HKEY)HKEY_CLASSES_ROOT)

static DWORD start;
static int queries;

static int there(void)
{
    char b[40];
    LONG cb = sizeof b;
    queries++;
    return RegQueryValue(HKCR, "SoundRec", b, &cb) == ERROR_SUCCESS;
}

static void step(const char *what)
{
    static int seen;
    static WORD ntasks;
    if (GetNumTasks() != ntasks) {
        ntasks = GetNumTasks();
        out("tasks ");
        outhex(ntasks);
        out(" at ");
        out(what);
        out(GetModuleHandle("REGEDIT") ? " REGEDIT" : "");
        out(GetModuleHandle("OLECLI") ? " OLECLI" : "");
        out(GetModuleHandle("OLESVR") ? " OLESVR" : "");
        out("\r\n");
    }
    if (!seen && there()) {
        seen = 1;
        out("APPEARED after ");
        out(what);
        out(" ms=");
        outint((int)(GetTickCount() - start));
        out(" queries=");
        outint(queries);
        out("\r\n");
        snapshot("R01.DAT");
    }
}

int __cdecl tmain(void)
{
    HKEY h, h2;
    DWORD t0;
    WORD i;

    WaitEvent(0);
    InitApp((HINSTANCE)init_regs[5]);
    start = GetTickCount();
    out("SH12 [embedding]\r\n");
    snapshot("R00.DAT");
    obs("at.start", there());
    step("start");
    RegOpenKey(HKCR, NULL, &h);
    step("open root");
    RegCloseKey(h);
    step("close root");
    RegCreateKey(HKCR, "w16c", &h);
    step("create");
    RegCloseKey(h);
    step("close create");
    RegOpenKey(HKCR, "w16c", &h);
    RegOpenKey(HKCR, "w16c", &h2);
    step("two opens");
    RegSetValue(HKCR, "w16c\\x", REG_SZ, "y", 1);
    step("set with two open");
    RegDeleteKey(HKCR, "w16c\\x");
    step("delete with two open");
    RegCloseKey(h);
    step("close 1");
    RegCloseKey(h2);
    step("close 2");
    {   /* C:\SH12M.TXT picks how to wait: 1 queries without Yield, 2 Yield
           without queries, 3 neither; otherwise Yield and queries */
        char m = 0;
        HFILE f = _lopen("C:\\SH12M.TXT", OF_READ);
        if (f != HFILE_ERROR) {
            _lread(f, &m, 1);
            _lclose(f);
        }
        out("mode ");
        out(m == '1' ? "1" : m == '2' ? "2" : m == '3' ? "3" : "0");
        out("\r\n");
        t0 = GetTickCount();
        for (i = 0; GetTickCount() - t0 < 8000; i++) {
            if (m != '1' && m != '3')
                Yield();
            if (m != '2' && m != '3')
                step("8 s of waiting");
        }
        step("the wait");
    }
    pump(10000);
    step("10 s of messages");
    obs("at.end", there());
    snapshot("R02.DAT");
    out("SH12 done\r\n");
    return failures;
}
