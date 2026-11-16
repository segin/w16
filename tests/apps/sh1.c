/*
 * sh1.c - SH1: SHELL's registration database API (RegOpenKey, RegCreateKey,
 * RegCloseKey, RegDeleteKey, RegSetValue, RegQueryValue, RegEnumKey) and
 * what it does to REG.DAT. Run as the shell (tests/refrun.sh, both modes)
 * on Windows 3.1 and with w16's SHELL.DLL dropped in (tests/shellprobe.sh).
 *
 * Every return code, returned string and length is an OBS; REG.DAT is
 * copied to C:\R<nn>.DAT after the steps that may change it, so that the
 * file format can be read off by diffing (tests/shellprobe.sh -g fetches
 * them). Black-box: only the documented API and the data file.
 */
#include <windows.h>

#define TK_REF "sh1ref.h"
#include "tklog.h"
#include "shprobe.h"

#define HKCR ((HKEY)HKEY_CLASSES_ROOT)

static char buf[1200];
static char big[1100];

static void fill(void)
{
    WORD i;
    for (i = 0; i < 64; i++)
        buf[i] = 'X';
    buf[64] = 0;
}

/* RegQueryValue(k, sub) with a buffer of cb bytes: result, cb, text */
static void query(const char *name, HKEY k, LPCSTR sub, LONG cb)
{
    char n[64];
    LONG r;
    fill();
    r = RegQueryValue(k, sub, buf, &cb);
    obs_l(name, r);
    suffix(n, name, ".cb");
    obs_l(n, cb);
    suffix(n, name, ".text");
    obs_s(n, buf);
}

static void enumkey(const char *name, HKEY k, DWORD i, DWORD cb)
{
    char n[64];
    LONG r;
    fill();
    r = RegEnumKey(k, i, buf, cb);
    obs_l(name, r);
    suffix(n, name, ".text");
    obs_s(n, buf);
}

int __cdecl tmain(void)
{
    HKEY h1 = 0, h2 = 0, h3 = 0, h4 = 0, h5 = 0;
    LONG r;
    WORD i;

    WaitEvent(0);
    InitApp((HINSTANCE)init_regs[5]);
    out("SH1 registration database\r\n");
    snapshot("R00.DAT");

    /* ---- reading the stock database ---- */
    r = RegOpenKey(HKCR, ".txt", &h1);
    obs_l("open.txt", r);
    obs_l("open.txt.h", h1);
    query("q.h.null", h1, NULL, sizeof buf);
    query("q.h.empty", h1, "", sizeof buf);
    query("q.root.txt", HKCR, ".txt", sizeof buf);
    query("q.root.TXT", HKCR, ".TXT", sizeof buf);
    query("q.cb4", HKCR, ".txt", 4);
    query("q.cb1", HKCR, ".txt", 1);
    query("q.cb0", HKCR, ".txt", 0);
    query("q.cb8", HKCR, ".txt", 8);
    query("q.novalue", HKCR, "txtfile\\shell", sizeof buf);
    query("q.missing", HKCR, "w16nokey", sizeof buf);
    query("q.root", HKCR, NULL, sizeof buf);
    query("q.root.empty", HKCR, "", sizeof buf);
    query("q.hkey0", 0, ".txt", sizeof buf);
    query("q.deep", HKCR, "txtfile\\shell\\open\\command", sizeof buf);
    query("q.lead", HKCR, "\\.txt", sizeof buf);
    query("q.trail", HKCR, "txtfile\\", sizeof buf);
    query("q.double", HKCR, "txtfile\\\\shell", sizeof buf);
    query("q.badh", (HKEY)0x1234, ".txt", sizeof buf);
    query("q.classes", HKCR, ".classes", sizeof buf);

    r = RegOpenKey(HKCR, "txtfile", &h2);
    obs_l("open.txtfile", r);
    obs_l("open.txtfile.h", h2);
    query("q.rel", h2, "shell\\open\\command", sizeof buf);
    r = RegOpenKey(HKCR, "txtfile", &h3);
    obs_l("open.again", r);
    obs_l("open.again.h", h3);
    r = RegOpenKey(h2, NULL, &h4);
    obs_l("open.null", r);
    obs_l("open.null.h", h4);
    r = RegOpenKey(h2, "", &h5);
    obs_l("open.empty", r);
    obs_l("open.empty.h", h5);
    h4 = 0x5555;
    r = RegOpenKey(HKCR, "w16nokey", &h4);
    obs_l("open.missing", r);
    obs_l("open.missing.h", h4);
    r = RegOpenKey(0, ".txt", &h4);
    obs_l("open.hkey0", r);
    obs_l("open.hkey0.h", h4);
    obs_l("close.hkey0", RegCloseKey(h4));
    r = RegOpenKey(HKCR, NULL, &h4);
    obs_l("open.root", r);
    obs_l("open.root.h", h4);
    obs_l("close.root", RegCloseKey(h4));
    obs_l("close.h1", RegCloseKey(h1));
    obs_l("close.h1.again", RegCloseKey(h1));
    obs_l("close.h3", RegCloseKey(h3));
    obs_l("close.h5", RegCloseKey(h5));
    obs_l("close.bad", RegCloseKey((HKEY)0x1234));
    obs_l("close.hkcr", RegCloseKey(HKCR));
    obs_l("close.0", RegCloseKey(0));
    r = RegOpenKey(HKCR, ".ini", &h1);
    obs_l("open.reuse", r);
    obs_l("open.reuse.h", h1);
    obs_l("close.reuse", RegCloseKey(h1));
    query("q.closed", h1, NULL, sizeof buf);

    /* ---- enumeration ---- */
    for (i = 0; i < 40; i++) {
        r = RegEnumKey(HKCR, i, buf, sizeof buf);
        if (r != ERROR_SUCCESS)
            break;
        obsn("enum.root", i, strsum(buf, slen(buf)));
        obs_str("enum.root.name", buf);
    }
    obs("enum.root.count", i);
    obs_l("enum.root.end", r);
    enumkey("enum.big", HKCR, 1000, sizeof buf);
    enumkey("enum.cb3", HKCR, 0, 3);
    enumkey("enum.cb0", HKCR, 0, 0);
    enumkey("enum.h", h2, 0, sizeof buf);
    enumkey("enum.h.1", h2, 1, sizeof buf);
    enumkey("enum.bad", (HKEY)0x1234, 0, sizeof buf);
    enumkey("enum.0", 0, 0, sizeof buf);
    obs_l("close.h2", RegCloseKey(h2));
    snapshot("R01.DAT");

    /* ---- creating keys ---- */
    r = RegCreateKey(HKCR, "w16sh1\\a\\b", &h1);
    obs_l("create.ab", r);
    obs_l("create.ab.h", h1);
    snapshot("R02.DAT");
    obs_l("create.ab.close", RegCloseKey(h1));
    snapshot("R03.DAT");
    r = RegCreateKey(HKCR, "w16sh1", &h1);
    obs_l("create.exist", r);
    r = RegSetValue(HKCR, "w16sh1", REG_SZ, "Value One", 9);
    obs_l("set.one", r);
    snapshot("R04.DAT");
    query("q.one", HKCR, "w16sh1", sizeof buf);
    r = RegSetValue(HKCR, "w16sh1", REG_SZ, "Value Two", 3);
    obs_l("set.cb3", r);
    query("q.cb3", HKCR, "w16sh1", sizeof buf);
    r = RegSetValue(HKCR, "w16sh1", REG_SZ, "Value Three", 0);
    obs_l("set.cb0", r);
    query("q.cb0v", HKCR, "w16sh1", sizeof buf);
    r = RegSetValue(HKCR, "w16sh1", 2, "binary", 6);
    obs_l("set.type2", r);
    query("q.type2", HKCR, "w16sh1", sizeof buf);
    r = RegSetValue(HKCR, "w16sh1", 0, "zero", 4);
    obs_l("set.type0", r);
    query("q.type0", HKCR, "w16sh1", sizeof buf);
    r = RegSetValue(h1, NULL, REG_SZ, "via handle", 10);
    obs_l("set.hnull", r);
    query("q.hnull", h1, NULL, sizeof buf);
    r = RegSetValue(h1, "", REG_SZ, "via empty", 9);
    obs_l("set.hempty", r);
    query("q.hempty", h1, NULL, sizeof buf);
    r = RegSetValue(HKCR, "w16sh1\\c", REG_SZ, "new key c", 9);
    obs_l("set.newkey", r);
    query("q.newkey", HKCR, "w16sh1\\c", sizeof buf);
    r = RegSetValue(HKCR, "w16sh1\\d", REG_SZ, "txtfile", 7);
    obs_l("set.shared", r);
    r = RegSetValue(HKCR, "w16sh1\\e", REG_SZ, "", 0);
    obs_l("set.empty", r);
    query("q.empty", HKCR, "w16sh1\\e", sizeof buf);
    r = RegSetValue(HKCR, "w16sh1\\A", REG_SZ, "upper A", 7);
    obs_l("set.caseA", r);
    query("q.casea", HKCR, "w16sh1\\a", sizeof buf);
    r = RegSetValue(HKCR, NULL, REG_SZ, "root value", 10);
    obs_l("set.root", r);
    query("q.rootv", HKCR, NULL, sizeof buf);
    r = RegSetValue((HKEY)0x1234, NULL, REG_SZ, "bad", 3);
    obs_l("set.badh", r);
    snapshot("R05.DAT");
    for (i = 0; i < 12; i++) {
        r = RegEnumKey(h1, i, buf, sizeof buf);
        if (r != ERROR_SUCCESS)
            break;
        obsn("enum.w16", i, strsum(buf, slen(buf)));
        obs_str("enum.w16.name", buf);
    }
    obs("enum.w16.count", i);
    obs_l("enum.w16.end", r);

    /* sizes: a long name and a long value */
    for (i = 0; i < 1000; i++)
        big[i] = (char)('a' + i % 26);
    big[1000] = 0;
    r = RegSetValue(HKCR, "w16sh1\\long", REG_SZ, big, 1000);
    obs_l("set.long", r);
    {
        LONG cb = sizeof buf;
        r = RegQueryValue(HKCR, "w16sh1\\long", buf, &cb);
        obs_l("q.long", r);
        obs_l("q.long.cb", cb);
        obs("q.long.sum", strsum(buf, slen(buf)));
    }
    big[300] = 0;
    r = RegCreateKey(HKCR, big, &h2);
    obs_l("create.longname", r);
    obs_l("create.longname.close", RegCloseKey(h2));
    query("q.longname", HKCR, big, sizeof buf);
    r = RegCreateKey(HKCR, "w16sh1\\\xE9t\xE9", &h2);  /* e acute */
    obs_l("create.ansi", r);
    obs_l("create.ansi.close", RegCloseKey(h2));
    r = RegCreateKey(HKCR, "w16sh1\\x\\\\y", &h2);
    obs_l("create.double", r);
    obs_l("create.double.close", RegCloseKey(h2));
    r = RegCreateKey(HKCR, "w16sh1\\z\\", &h2);
    obs_l("create.trail", r);
    obs_l("create.trail.close", RegCloseKey(h2));
    r = RegCreateKey(HKCR, NULL, &h2);
    obs_l("create.null", r);
    obs_l("create.null.h", h2);
    obs_l("create.null.close", RegCloseKey(h2));
    r = RegCreateKey(HKCR, "", &h2);
    obs_l("create.empty", r);
    obs_l("create.empty.close", RegCloseKey(h2));
    snapshot("R06.DAT");

    /* ---- deleting ---- */
    r = RegOpenKey(HKCR, "w16sh1\\a\\b", &h2);
    obs_l("open.b", r);
    r = RegDeleteKey(HKCR, "w16sh1\\a");
    obs_l("del.a", r);
    query("q.del.b", HKCR, "w16sh1\\a\\b", sizeof buf);
    query("q.del.a", HKCR, "w16sh1\\a", sizeof buf);
    query("q.del.openh", h2, NULL, sizeof buf);
    r = RegSetValue(h2, NULL, REG_SZ, "after delete", 12);
    obs_l("set.deleted", r);
    obs_l("close.deleted", RegCloseKey(h2));
    r = RegDeleteKey(HKCR, "w16sh1\\a");
    obs_l("del.again", r);
    r = RegDeleteKey(h1, "c");
    obs_l("del.rel", r);
    r = RegDeleteKey(h1, NULL);
    obs_l("del.self", r);
    query("q.del.self", HKCR, "w16sh1", sizeof buf);
    r = RegDeleteKey(HKCR, NULL);
    obs_l("del.rootnull", r);
    r = RegDeleteKey((HKEY)0x1234, "x");
    obs_l("del.badh", r);
    obs_l("close.h1.del", RegCloseKey(h1));
    snapshot("R07.DAT");
    RegDeleteKey(HKCR, "w16sh1");
    RegDeleteKey(HKCR, big);
    snapshot("R08.DAT");
    r = RegSetValue(HKCR, "w16sh1\\again", REG_SZ, "reused", 6);
    obs_l("set.reuse", r);
    snapshot("R09.DAT");
    RegDeleteKey(HKCR, "w16sh1");
    RegSetValue(HKCR, NULL, REG_SZ, "", 0);
    snapshot("R10.DAT");

    out("SH1 done\r\n");
    return failures;
}
