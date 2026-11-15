/*
 * sh7.c - SH7: DoEnvironmentSubst and FindEnvironmentString (the DOS
 * environment: tests/shellprobe.sh -p 'SET W16A=alpha' ... SH7).
 */
#include <windows.h>

#define TK_REF "sh7ref.h"
#include "tklog.h"
#include "shprobe.h"

static char buf[300];

static void subst(const char *tag, const char *s, UINT cb)
{
    char n[64];
    DWORD r;
    WORD i;
    for (i = 0; i < sizeof buf; i++)
        buf[i] = 'Z';
    lstrcpy(buf, s);
    r = DoEnvironmentSubst(buf, cb);
    suffix(n, tag, ".lo");
    obs(n, LOWORD(r));
    suffix(n, tag, ".hi");
    obs(n, HIWORD(r));
    obs_s(tag, buf);
    suffix(n, tag, ".after");
    buf[sizeof buf - 1] = 0;
    obs(n, (BYTE)buf[lstrlen(s) + 1]);
}

static void findenv(const char *tag, const char *name)
{
    char n[64];
    LPSTR p;
    lstrcpy(buf, name);
    p = FindEnvironmentString(buf);
    obs(tag, p ? 1 : 0);
    if (p) {
        LPSTR env = GetDOSEnvironment();
        suffix(n, tag, ".text");
        obs_s(n, p);
        suffix(n, tag, ".seg");
        obs(n, SELECTOROF(p) == SELECTOROF(env));
        /* where in its entry it points: the name, or past '=' */
        suffix(n, tag, ".prev");
        obs(n, OFFSETOF(p) ? (BYTE)p[-1] : 0);
    }
}

int __cdecl tmain(void)
{
    WaitEvent(0);
    InitApp((HINSTANCE)init_regs[5]);
    out("SH7 environment\r\n");
    subst("s.plain", "no vars here", sizeof buf);
    subst("s.a", "%W16A%", sizeof buf);
    subst("s.a.mid", "x%W16A%y%W16B%z", sizeof buf);
    subst("s.lower", "%w16a%", sizeof buf);
    subst("s.missing", "a%W16NONE%b", sizeof buf);
    subst("s.pct2", "100%%", sizeof buf);
    subst("s.pct1", "100%", sizeof buf);
    subst("s.open", "x%W16A", sizeof buf);
    subst("s.empty.var", "%%x", sizeof buf);
    subst("s.windir", "%windir%", sizeof buf);
    subst("s.WINDIR", "%WINDIR%", sizeof buf);
    subst("s.path", "%PATH%", sizeof buf);
    subst("s.small", "%W16A%%W16A%", 8);
    subst("s.small2", "x%W16A%", 6);
    subst("s.exact", "%W16A%", 6);
    subst("s.exact1", "%W16A%", 5);
    subst("s.zero", "%W16A%", 0);
    subst("s.empty", "", sizeof buf);
    subst("s.long", "%W16L%", sizeof buf);
    subst("s.long.small", "%W16L%", 40);
    subst("s.eqval", "%W16E%", sizeof buf);
    subst("s.space", "% W16A%", sizeof buf);
    subst("s.twice", "%W16A%%W16A%", sizeof buf);
    findenv("e.a", "W16A");
    findenv("e.lower", "w16a");
    findenv("e.windir", "windir");
    findenv("e.WINDIR", "WINDIR");
    findenv("e.path", "PATH");
    findenv("e.missing", "W16NONE");
    findenv("e.prefix", "W16");
    findenv("e.eq", "W16A=");
    findenv("e.empty", "");
    out("SH7 done\r\n");
    return failures;
}
