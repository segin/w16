/*
 * sh2.c - SH2: more of the registration database (after SH1): what a new
 * session does to REG.DAT, the limits on key names, which characters a
 * key name may hold, how strings are shared, odd key handles, and the
 * table's growth. Run as the shell (tests/shellprobe.sh SH2), also with
 * REG.DAT deleted first (-p 'DEL C:\WINDOWS\REG.DAT') to see the file
 * SHELL makes from nothing. REG.DAT is copied to C:\R<nn>.DAT.
 */
#include <windows.h>

#define TK_REF "sh2ref.h"
#include "tklog.h"
#include "shprobe.h"

#define HKCR ((HKEY)HKEY_CLASSES_ROOT)

static char buf[600], name[600];

static void query(const char *nm, HKEY k, LPCSTR sub)
{
    char n[64];
    LONG r, cb = sizeof buf;
    buf[0] = 'X';
    buf[1] = 0;
    r = RegQueryValue(k, sub, buf, &cb);
    obs_l(nm, r);
    suffix(n, nm, ".text");
    obs_s(n, buf);
}

/* create w16b\<name of len chars of c> */
static LONG create_len(WORD len, char c)
{
    HKEY h;
    LONG r;
    WORD i;
    lstrcpy(name, "w16b\\");
    for (i = 0; i < len; i++)
        name[5 + i] = c;
    name[5 + len] = 0;
    r = RegCreateKey(HKCR, name, &h);
    if (r == ERROR_SUCCESS)
        RegCloseKey(h);
    return r;
}

static const WORD lens[] = { 1, 30, 31, 32, 33, 62, 63, 64, 65, 79, 80, 81, 126, 127,
                             128, 129, 254, 255, 256, 257, 400, 0 };

int __cdecl tmain(void)
{
    HKEY h, h2;
    LONG r;
    WORD i, c;
    char key[16];

    WaitEvent(0);
    InitApp((HINSTANCE)init_regs[5]);
    out("SH2 registration database (2)\r\n");
    snapshot("R00.DAT");

    /* a new session's first change */
    r = RegCreateKey(HKCR, "w16b", &h);
    obs_l("create.w16b", r);
    obs_l("close.w16b", RegCloseKey(h));
    snapshot("R01.DAT");

    /* name lengths: one component */
    for (i = 0; lens[i]; i++)
        obsn("len", lens[i], (WORD)create_len(lens[i], 'n'));
    /* path lengths: many short components */
    for (i = 1; i <= 40; i++) {
        WORD k;
        lstrcpy(name, "w16b\\p");
        for (k = 1; k < i; k++)
            lstrcat(name, "\\abcdefghi");
        r = RegCreateKey(HKCR, name, &h);
        obsn("path", i, (WORD)r);
        if (r == ERROR_SUCCESS)
            RegCloseKey(h);
        else
            break;
    }
    query("q.deeppath", HKCR, name);
    snapshot("R02.DAT");

    /* characters in key names */
    for (c = 1; c < 256; c++) {
        lstrcpy(key, "w16b\\c?x");
        key[6] = (char)c;
        r = RegCreateKey(HKCR, key, &h);
        if (r == ERROR_SUCCESS)
            RegCloseKey(h);
        obsn("char", c, (WORD)r);
    }
    /* characters in values */
    for (c = 1; c < 256; c += 15) {
        lstrcpy(key, "v?w");
        key[1] = (char)c;
        r = RegSetValue(HKCR, "w16b\\val", REG_SZ, key, 3);
        obsn("vchar", c, (WORD)r);
    }
    query("q.vchar", HKCR, "w16b\\val");
    RegDeleteKey(HKCR, "w16b\\val");
    snapshot("R03.DAT");
    RegDeleteKey(HKCR, "w16b");
    snapshot("R04.DAT");

    /* string sharing: case */
    RegSetValue(HKCR, "w16b\\PBRUSH", REG_SZ, "TXTFILE", 7);
    RegSetValue(HKCR, "w16b\\txtfile", REG_SZ, "PBrush", 6);
    RegSetValue(HKCR, "w16b\\pbrush", REG_SZ, "pbrush", 6);
    query("q.case1", HKCR, "w16b\\PBRUSH");
    query("q.case2", HKCR, "w16b\\pbrush");
    snapshot("R05.DAT");
    for (i = 0; i < 4; i++) {
        r = RegEnumKey(HKCR, i, buf, sizeof buf);
        obsn("enumroot", i, (WORD)r);
        obs_str("enumroot.name", buf);
        obsn("enumroot.sum", i, strsum(buf, slen(buf)));
    }
    r = RegOpenKey(HKCR, "w16b", &h);
    for (i = 0; i < 4; i++) {
        r = RegEnumKey(h, i, buf, sizeof buf);
        obsn("enumb", i, (WORD)r);
        obs_str("enumb.name", buf);
        obsn("enumb.sum", i, strsum(buf, slen(buf)));
    }
    r = RegEnumKey(h, 0x10000L, buf, sizeof buf);
    obs_l("enum.hiidx", r);
    RegCloseKey(h);

    /* odd handles */
    query("q.classes.txt", 0, ".classes\\.txt");
    query("q.hi2", (HKEY)MAKELONG(0x2C, 2), NULL);
    query("q.hi0", (HKEY)MAKELONG(0x2C, 0), NULL);
    query("q.node0", (HKEY)MAKELONG(0, 1), ".classes\\.txt");
    query("q.node27", (HKEY)MAKELONG(0x27, 1), ".txt");
    query("q.hkcr.null", HKCR, NULL);
    query("q.big", (HKEY)MAKELONG(0x7000, 1), NULL);
    query("q.ffff", (HKEY)0xFFFFFFFFL, NULL);
    r = RegOpenKey(HKCR, "w16b", &h);
    obs_l("open.h", r);
    obs_l("open.h.val", h);
    RegCloseKey(h);

    /* growth: many keys at once */
    RegCreateKey(HKCR, "w16b\\many", &h);
    for (i = 0; i < 40; i++) {
        lstrcpy(key, "k00");
        key[1] = (char)('0' + i / 10);
        key[2] = (char)('0' + i % 10);
        r = RegSetValue(h, key, REG_SZ, key, 3);
        if (r)
            obsn("many", i, (WORD)r);
        if (i == 10)
            snapshot("R06.DAT");          /* h still open: not written */
    }
    RegCloseKey(h);
    snapshot("R07.DAT");
    r = RegOpenKey(HKCR, "w16b", &h);
    r = RegOpenKey(HKCR, "w16b", &h2);
    RegDeleteKey(HKCR, "w16b\\many");
    RegCloseKey(h);
    snapshot("R08.DAT");                  /* one handle still open */
    RegCloseKey(h2);
    snapshot("R09.DAT");
    RegDeleteKey(HKCR, "w16b");
    snapshot("R10.DAT");
    out("SH2 done\r\n");
    return failures;
}
