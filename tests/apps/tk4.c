/*
 * tk4.c - module and string API probe: GetModuleHandle, GetModuleFileName,
 * GetProcAddress, lstrcpy, lstrcat, lstrlen.
 *
 * Output and checks: see tklog.h; the reference values are in tk4ref.h.
 * The exit code is the number of failures. Calls that might fault on
 * Windows come last.
 */
#include <windows.h>

#define TK_REF "tk4ref.h"
#include "tklog.h"

extern WORD __cdecl get_ahincr(void);

static WORD hinst(void)
{
    return entry_regs[E_DS] & ~1u;
}

static WORD sel_of(const void FAR *p)
{
    return (WORD)((DWORD)p >> 16);
}

static WORD off_of(const void FAR *p)
{
    return (WORD)(DWORD)p;
}

static char upper(char c)
{
    return c >= 'a' && c <= 'z' ? (char)(c - 32) : c;
}

static int ends_with(const char FAR *s, const char *tail)
{
    WORD n = 0, t = 0;
    while (s[n])
        n++;
    while (tail[t])
        t++;
    if (t > n)
        return 0;
    while (t)
        if (upper(s[--n]) != tail[--t])
            return 0;
    return 1;
}

static HMODULE hkernel, hself;

static void probe_handles(void)
{
    hkernel = GetModuleHandle("KERNEL");
    hself = GetModuleHandle("TK4");
    obs_env("GetModuleHandle(KERNEL)", (WORD)hkernel);
    obs_env("GetModuleHandle(TK4)", (WORD)hself);
    obs("KERNEL found", hkernel != 0);
    obs("TK4 found", hself != 0);
    obs("hModule(TK4) & 1", (WORD)hself & 1);
    obs("hModule(KERNEL) & 1", (WORD)hkernel & 1);
    obs("hModule(TK4) != hInstance", (WORD)hself != hinst());
    if (hself)
        obs("[hModule(TK4):0]", *w_at((WORD)hself, 0));
    if (hkernel)
        obs("[hModule(KERNEL):0]", *w_at((WORD)hkernel, 0));
    obs("GetModuleHandle(kernel)==", GetModuleHandle("kernel") == hkernel);
    obs("GetModuleHandle(tk4)==", GetModuleHandle("tk4") == hself);
    obs("GetModuleHandle(NOSUCH)", (WORD)GetModuleHandle("NOSUCH"));
    obs("GetModuleHandle(TK4.EXE)==", GetModuleHandle("TK4.EXE") == hself);
    obs("GetModuleHandle(TK4.EXE)", GetModuleHandle("TK4.EXE") != 0);
    obs("GetModuleHandle(C:\\TK4.EXE)==", GetModuleHandle("C:\\TK4.EXE") == hself);
    obs("GetModuleHandle(TK4 )==", GetModuleHandle("TK4 ") == hself);
    obs("GetModuleHandle(MAKELP(0,hInstance))==",
        GetModuleHandle((LPCSTR)MAKELP(0, hinst())) == hself);
    obs("GetModuleHandle(MAKELP(0,hInstance|1))==",
        GetModuleHandle((LPCSTR)MAKELP(0, hinst() | 1)) == hself);
    obs("GetModuleHandle(MAKELP(0,hModule))==",
        GetModuleHandle((LPCSTR)MAKELP(0, (WORD)hself)) == hself);
}

static void probe_filename(void)
{
    char buf[128];
    int n, i;

    for (i = 0; i < 128; i++)
        buf[i] = 'x';
    n = GetModuleFileName((HINSTANCE)hinst(), buf, sizeof buf);
    obs("GetModuleFileName(hInstance) ret==strlen", n == lstrlen(buf));
    obs_str("GetModuleFileName(hInstance)", buf);
    obs("GetModuleFileName(hInstance) ends \\TK4.EXE", ends_with(buf, "\\TK4.EXE"));
    obs("GetModuleFileName(hInstance) has drive", buf[1] == ':' && buf[2] == '\\');
    obs("GetModuleFileName(hInstance) upper case", buf[0] >= 'A' && buf[0] <= 'Z');
    n = GetModuleFileName(hself, buf, sizeof buf);
    obs("GetModuleFileName(hModule) ret==strlen", n == lstrlen(buf));
    obs("GetModuleFileName(hModule) ends \\TK4.EXE", ends_with(buf, "\\TK4.EXE"));
    n = GetModuleFileName(hkernel, buf, sizeof buf);
    obs("GetModuleFileName(KERNEL) ret==strlen", n == lstrlen(buf));
    obs_str("GetModuleFileName(KERNEL)", buf);
    obs("GetModuleFileName(KERNEL) .EXE", ends_with(buf, ".EXE"));

    for (i = 0; i < 128; i++)
        buf[i] = 'x';
    n = GetModuleFileName(hself, buf, 6);
    obs("GetModuleFileName(n=6) ret", n);
    obs("GetModuleFileName(n=6) buf[4]", (BYTE)buf[4]);
    obs("GetModuleFileName(n=6) buf[5]", (BYTE)buf[5]);
    obs("GetModuleFileName(n=6) buf[6]", (BYTE)buf[6]);
    for (i = 0; i < 128; i++)
        buf[i] = 'x';
    n = GetModuleFileName(hself, buf, 1);
    obs("GetModuleFileName(n=1) ret", n);
    obs("GetModuleFileName(n=1) buf[0]", (BYTE)buf[0]);
    obs("GetModuleFileName(n=1) buf[1]", (BYTE)buf[1]);
    buf[0] = 'x';
    n = GetModuleFileName(hself, buf, 0);
    obs("GetModuleFileName(n=0) ret", n);
    obs("GetModuleFileName(n=0) buf[0]", (BYTE)buf[0]);
    buf[0] = 'x';
    n = GetModuleFileName((HINSTANCE)0x1234, buf, sizeof buf);
    obs("GetModuleFileName(bogus) ret", n);
    obs("GetModuleFileName(bogus) buf[0]", (BYTE)buf[0]);
}

typedef DWORD (FAR PASCAL *getversion_t)(void);

static void probe_procaddress(void)
{
    FARPROC byname, byord, p;

    byname = GetProcAddress(hkernel, "GETVERSION");
    byord = GetProcAddress(hkernel, (LPCSTR)MAKELP(0, 3));
    obs("GetProcAddress(GETVERSION)!=0", byname != NULL);
    obs("GetProcAddress(GETVERSION)==ordinal 3", byname == byord);
    if (byname)
        obs("GetProcAddress(GETVERSION)() == GetVersion()",
            ((getversion_t)byname)() == GetVersion());
    obs("GetProcAddress(GetVersion)==", GetProcAddress(hkernel, "GetVersion") == byname);
    obs("GetProcAddress(getversion)==", GetProcAddress(hkernel, "getversion") == byname);
    obs("GetProcAddress(ordinal 8003h)==", GetProcAddress(hkernel, (LPCSTR)MAKELP(0, 0x8003)) == byord);
    obs("GetProcAddress(#3)==", GetProcAddress(hkernel, "#3") == byord);
    obs("GetProcAddress(NOSUCHFUNC)", GetProcAddress(hkernel, "NOSUCHFUNC") != NULL);
    obs("GetProcAddress(ordinal 0)", GetProcAddress(hkernel, (LPCSTR)MAKELP(0, 0)) != NULL);
    obs("GetProcAddress(ordinal 27)", GetProcAddress(hkernel, (LPCSTR)MAKELP(0, 27)) != NULL);
    obs("GetProcAddress(ordinal 9999)", GetProcAddress(hkernel, (LPCSTR)MAKELP(0, 9999)) != NULL);
    obs("GetProcAddress(LSTRCPY)==ordinal 88",
        GetProcAddress(hkernel, "LSTRCPY") == GetProcAddress(hkernel, (LPCSTR)MAKELP(0, 88)));
    obs("GetProcAddress(LOCALALLOC)==ordinal 5",
        GetProcAddress(hkernel, "LOCALALLOC") == GetProcAddress(hkernel, (LPCSTR)MAKELP(0, 5)));
    obs("GetProcAddress(hInstance of KERNEL)",
        GetProcAddress((HINSTANCE)((WORD)hkernel | 1), "GETVERSION") == byname);
    p = GetProcAddress(hkernel, "__AHINCR");
    obs("GetProcAddress(__AHINCR) off==__AHINCR", off_of(p) == get_ahincr());
    obs("GetProcAddress(__AHINCR) sel", sel_of(p));
    p = GetProcAddress(hkernel, (LPCSTR)MAKELP(0, 114));
    obs("GetProcAddress(114) off==__AHINCR", off_of(p) == get_ahincr());
    obs("GetProcAddress(114) sel", sel_of(p));
    /* the program's own entry table: TK4 exports nothing */
    obs("GetProcAddress(TK4, ordinal 1)", GetProcAddress(hself, (LPCSTR)MAKELP(0, 1)) != NULL);
    obs("GetProcAddress(TK4, TK4)", GetProcAddress(hself, "TK4") != NULL);
}

static void probe_strings(void)
{
    char buf[32];
    LPSTR r;
    int i;

    obs("lstrlen(hello)", lstrlen("hello"));
    obs("lstrlen()", lstrlen(""));
    for (i = 0; i < 32; i++)
        buf[i] = 'x';
    r = lstrcpy(buf, "abc");
    obs("lstrcpy ret==dest", r == (LPSTR)buf);
    obs("lstrcpy copied", buf[0] == 'a' && buf[2] == 'c' && buf[3] == 0 && buf[4] == 'x');
    r = lstrcat(buf, "de");
    obs("lstrcat ret==dest", r == (LPSTR)buf);
    obs("lstrcat result", buf[3] == 'd' && buf[4] == 'e' && buf[5] == 0 && buf[6] == 'x');
    obs("lstrlen(abcde)", lstrlen(buf));
    r = lstrcat(buf, "");
    obs("lstrcat empty", r == (LPSTR)buf && lstrlen(buf) == 5);
    r = lstrcpy(buf, "");
    obs("lstrcpy empty", r == (LPSTR)buf && buf[0] == 0 && buf[1] == 'b');
    {
        static char big[300];
        for (i = 0; i < 299; i++)
            big[i] = 'y';
        big[299] = 0;
        obs("lstrlen(299)", lstrlen(big));
    }
    /* NULL arguments last: they may fault */
    out("TK4 null arguments\r\n");
    obs_env("lstrlen(NULL)", lstrlen(NULL));
    lstrcpy(buf, "abc");
    r = lstrcpy(buf, NULL);
    obs_env("lstrcpy(buf, NULL) buf[0]", (BYTE)buf[0]);
    obs_env("lstrcpy(buf, NULL) buf[1]", (BYTE)buf[1]);
    obs_env("lstrcpy(buf, NULL) buf[2]", (BYTE)buf[2]);
    obs_env("lstrcpy(buf, NULL) buf[3]", (BYTE)buf[3]);
    obs("lstrcpy(buf, NULL) ret==dest", r == (LPSTR)buf);
    r = lstrcpy(NULL, "x");
    obs("lstrcpy(NULL, x) ret", sel_of(r) | off_of(r));
    lstrcpy(buf, "abc");
    r = lstrcat(buf, NULL);
    obs("lstrcat(buf, NULL) ret==dest", r == (LPSTR)buf);
    obs_env("lstrcat(buf, NULL) buf[0]", (BYTE)buf[0]);
    obs_env("lstrcat(buf, NULL) buf[3]", (BYTE)buf[3]);
    obs_env("lstrcat(buf, NULL) buf[4]", (BYTE)buf[4]);
    obs("GetModuleHandle(NULL)", GetModuleHandle(NULL) != 0);
    obs("GetModuleHandle(NULL)==self", GetModuleHandle(NULL) == hself);
}

int __cdecl tmain(void)
{
    out("TK4 start\r\n");
    probe_handles();
    probe_filename();
    probe_procaddress();
    probe_strings();
    out("TK4 done, failures=");
    outhex((WORD)failures);
    out("\r\n");
    return failures;
}
