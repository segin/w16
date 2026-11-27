/*
 * tk76.c - KERNEL's own exports by name: GetProcAddress on KERNEL's
 * module and instance handles, and an import by name. Windows 3.1's
 * GetProcAddress searches a module's resident names and then its
 * non-resident names (tests/apps/tk65.c, for a DLL); this probe asks it
 * of KERNEL itself, whose module w16's bootstrap builds without the
 * non-resident table. Microsoft's Setup toolkit (the "Microsoft Test
 * Driver" of Win32s 1.30c's Setup) binds its script's Declares with
 * GetProcAddress(KERNEL, "SETERRORMODE"); on w16 that gave 0 and Setup
 * stopped with "Bindtime error: 'SETERRORMODE' not found in specified
 * library".
 *
 * Each "gpa NAME" is 1 when GetProcAddress(hKernel, NAME) gives what
 * GetProcAddress(hKernel, MAKEINTRESOURCE(ordinal)) gives, 0 when it gives
 * 0, EEh for another address. The names are exports that w16's
 * kernel.lnk keeps non-resident, one resident (GETVERSION) as a control,
 * and spellings (mixed and lower case, "#107", unknown names).
 * "imp SETERRORMODE": TK76.EXE imports KERNEL's SETERRORMODE by name
 * (tk76.lnk); 1 when KERNEL bound it to ordinal 107's address.
 * "handle ...": which names and handles reach KERNEL (GetModuleHandle of
 * "KERNEL", of its file's name and path; LoadLibrary("KERNEL"), whose
 * instance handle GetProcAddress accepts as well).
 *
 * Runs in make test (tests/run-tests.sh) against tk76ref.h, made on
 * Windows 3.1 with tests/tk64ref.sh OUTDIR "S 3 286" TK76.
 */
#include <windows.h>

#define TK_REF "tk76ref.h"
#include "tklog.h"

extern UINT FAR PASCAL TK76SEM(UINT);   /* KERNEL.SETERRORMODE, by name */

static HMODULE hk;

static WORD which(FARPROC p, WORD ord)
{
    if (!p)
        return 0;
    return (DWORD)p == (DWORD)GetProcAddress(hk, MAKEINTRESOURCE(ord)) ? 1 : 0xEE;
}

static const struct {
    const char *name;
    WORD ord;
} q[] = {
    { "SETERRORMODE", 107 },
    { "SetErrorMode", 107 },
    { "seterrormode", 107 },
    { "#107", 107 },
    { "GETEXEVERSION", 105 },
    { "GETEXPWINVER", 167 },
    { "LOCALCOUNTFREE", 161 },
    { "ISBADREADPTR", 334 },
    { "IsBadStringPtr", 337 },
    { "LSTRCPYN", 353 },
    { "HMEMCPY", 348 },
    { "LOCALSHRINK", 121 },
    { "GETVERSION", 3 },
    { "GetVersion", 3 },
    { "SETERRORMOD", 107 },
    { "SETERRORMODEX", 107 },
    { "KERNEL", 107 },
};

int __cdecl tmain(void)
{
    char path[128], lab[48];
    const char *base;
    HINSTANCE hi;
    WORD i;

    out("TK76 start\r\n");
    hk = GetModuleHandle("KERNEL");
    obs("handle KERNEL", hk != 0);
    if (!hk)
        return failures + 1;
    for (i = 0; i < sizeof q / sizeof q[0]; i++) {
        lstrcpy(lab, "gpa ");
        lstrcat(lab, q[i].name);
        obs(lab, which(GetProcAddress(hk, q[i].name), q[i].ord));
    }
    obs("gpa kernel lower", which(GetProcAddress(GetModuleHandle("kernel"), "SetErrorMode"), 107));

    /* an import by name */
    obs("imp SETERRORMODE", which((FARPROC)TK76SEM, 107));
    {
        UINT old = TK76SEM(SEM_FAILCRITICALERRORS);
        obs("imp SETERRORMODE call", TK76SEM(old) == SEM_FAILCRITICALERRORS);
    }

    /* the handles that reach KERNEL */
    hi = LoadLibrary("KERNEL");
    obs("handle LoadLibrary KERNEL", hi > HINSTANCE_ERROR);
    if (hi > HINSTANCE_ERROR) {
        obs("handle LoadLibrary is module", (HMODULE)hi == hk);
        obs("gpa hInstance SETERRORMODE", which(GetProcAddress(hi, "SETERRORMODE"), 107));
        obs("gpa hInstance GETVERSION", which(GetProcAddress(hi, "GETVERSION"), 3));
        FreeLibrary(hi);
    }
    GetModuleFileName(hk, path, sizeof path);
    for (base = path, i = 0; path[i]; i++)
        if (path[i] == '\\' || path[i] == ':')
            base = path + i + 1;
    out("OBS ~kernel file=\"");
    out(path);
    out("\"\r\n");
    obs("handle file path", GetModuleHandle(path) == hk);
    obs("handle file name", GetModuleHandle(base) == hk);
    hi = LoadLibrary(base);
    obs("handle LoadLibrary file name", hi > HINSTANCE_ERROR && GetModuleHandle((LPCSTR)MAKELP(0, hi)) == hk);
    if (hi > HINSTANCE_ERROR) {
        obs("gpa file name SETERRORMODE", which(GetProcAddress(hi, "SETERRORMODE"), 107));
        FreeLibrary(hi);
    }
    out("TK76 end\r\n");
    return failures;
}
