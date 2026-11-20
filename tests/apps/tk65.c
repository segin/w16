/*
 * tk65.c - GetProcAddress by name, and imports by name: which spellings
 * find which exports. Observed on Windows 3.1 (tests/tk64ref.sh), checked
 * against w16's KERNEL in make test.
 *
 * TK65D.DLL (tk65d.c, tk65d.lnk) exports, each function returning its
 * ordinal:
 *   1 UPPERRES  2 MixedRes  3 lowerres  7 WEP  8 <C4h>UML   (resident)
 *   10 <8Ah>X  11 <9Fh>Y  12 <D7h>Z                         (resident)
 *   4 UPPERNR   5 MixedNr   6 lowernr   9 <E4h>UML          (non-resident)
 * (C4h and E4h: A and a with umlaut in Windows' character set; 8Ah S with
 * caron, 9Fh Y with diaeresis, D7h the multiplication sign.)
 *  - "gpa NAME": GetProcAddress(TK65D, NAME) for upper, lower and mixed
 *    spellings of each, "#n" strings, MAKEINTRESOURCE ordinals, NULL,
 *    the module name; the value is the ordinal of what it found (as
 *    GetProcAddress by ordinal gives it), 0 for nothing, EEh for an
 *    address that is none of them.
 *  - "own ...": GetProcAddress with hInstance 0 (Pietrek, Windows
 *    Internals ch. 8: the current task's module) and with its own
 *    hInstance, for TK65.EXE's export TK65OWN (1).
 *  - "imp n": TK65In.DLL (tk65i.c) imports TK65D's entry by a name (the
 *    list below); loaded, and the address KERNEL put there, as above.
 *
 * Runs in make test beside TK60 (tests/run-tests.sh), the DLLs in the
 * Windows directory; the reference run: tests/tk64ref.sh.
 */
#include <windows.h>

#define TK_REF "tk65ref.h"
#include "tklog.h"

#define NORD 13                 /* ordinals 1-12 */
static DWORD byord[NORD];

WORD FAR PASCAL TK65OWN(void)                                  /* 1 */
{
    return 0x65;
}

/* the ordinal whose address p is; 0 for NULL, EEh for none */
static WORD which(FARPROC p)
{
    WORD i;
    if (!p)
        return 0;
    for (i = 1; i < NORD; i++)
        if ((DWORD)p == byord[i])
            return i;
    return 0xEE;
}

static const struct {
    const char *label, *name;
} q[] = {
    { "UPPERRES", "UPPERRES" },
    { "upperres", "upperres" },
    { "UpperRes", "UpperRes" },
    { "MixedRes", "MixedRes" },
    { "MIXEDRES", "MIXEDRES" },
    { "mixedres", "mixedres" },
    { "mIXEDrES", "mIXEDrES" },
    { "lowerres", "lowerres" },
    { "LOWERRES", "LOWERRES" },
    { "LowerRes", "LowerRes" },
    { "UPPERNR", "UPPERNR" },
    { "uppernr", "uppernr" },
    { "UpperNr", "UpperNr" },
    { "MixedNr", "MixedNr" },
    { "MIXEDNR", "MIXEDNR" },
    { "mixednr", "mixednr" },
    { "lowernr", "lowernr" },
    { "LOWERNR", "LOWERNR" },
    { "WEP", "WEP" },
    { "wep", "wep" },
    { "C4 UML", "\xC4UML" },
    { "E4 UML", "\xE4UML" },
    { "E4 uml", "\xE4uml" },
    { "C4 uml", "\xC4uml" },
    { "9A x", "\x9Ax" },
    { "FF y", "\xFFy" },
    { "F7 z", "\xF7z" },
    { "8A X", "\x8AX" },
    { "UPPERRES blank", "UPPERRES " },
    { "UPPERRE", "UPPERRE" },
    { "module name", "TK65D" },
    { "module name lower", "tk65d" },
    { "empty", "" },
    { "#1", "#1" },
    { "#01", "#01" },
    { "#5", "#5" },
    { "#9", "#9" },
    { "#0", "#0" },
    { "#", "#" },
    { "#12", "#12" },
    { "#13", "#13" },
    { "#1x", "#1x" },
    { "#x1", "#x1" },
    { "# 1", "# 1" },
    { "#+1", "#+1" },
    { "#-1", "#-1" },
    { "##1", "##1" },
    { "#65537", "#65537" },
    { "#32769", "#32769" },
    { "#00000000000000000002", "#00000000000000000002" },
    { "x#1", "x#1" },
};

/* TK65In.DLL's import (the Makefile's TK65IMP list, in order) */
static const char *const imp[] = {
    "MixedRes", "MIXEDRES", "upperres", "MixedNr", "mixednr", "uppernr",
    "UPPERRES", "lowerres", "#4"
};

int __cdecl tmain(void)
{
    HINSTANCE h, hi;
    HMODULE hm;
    WORD i;
    char name[48];
    out("TK65 start\r\n");
    h = LoadLibrary("TK65D.DLL");
    obs("TK65D loaded", h > HINSTANCE_ERROR);
    if (h <= HINSTANCE_ERROR)
        return failures + 1;
    for (i = 1; i < NORD; i++)
        byord[i] = (DWORD)GetProcAddress(h, MAKEINTRESOURCE(i));
    for (i = 1; i < NORD; i++)
        obsn("by ordinal", i, byord[i] != 0);

    for (i = 0; i < sizeof q / sizeof q[0]; i++) {
        lstrcpy(name, "gpa ");
        lstrcat(name, q[i].label);
        obs(name, which(GetProcAddress(h, q[i].name)));
    }
    obs("gpa NULL", which(GetProcAddress(h, NULL)));
    obs("gpa ord 2", which(GetProcAddress(h, MAKEINTRESOURCE(2))));
    obs("gpa ord 8002h", which(GetProcAddress(h, MAKEINTRESOURCE(0x8002))));
    obs("gpa ord 0", which(GetProcAddress(h, MAKEINTRESOURCE(0))));
    obs("gpa ord 13", which(GetProcAddress(h, MAKEINTRESOURCE(13))));
    {
        /* past the entry table: what comes back */
        DWORD v = (DWORD)GetProcAddress(h, MAKEINTRESOURCE(13));
        obs_env("gpa ord 13 selector", HIWORD(v));
        obs_env("gpa ord 13 offset", LOWORD(v));
        obs_env("gpa ord 12 selector", HIWORD(byord[12]));
        obs_env("gpa ord 12 offset", LOWORD(byord[12]));
        v = (DWORD)GetProcAddress(h, MAKEINTRESOURCE(200));
        obs_env("gpa ord 200 selector", HIWORD(v));
        obs_env("gpa ord 200 offset", LOWORD(v));
    }
    hm = GetModuleHandle("TK65D");
    obs("gpa hModule upperres", which(GetProcAddress(hm, "upperres")));
    obs("gpa hModule MixedRes", which(GetProcAddress(hm, "MixedRes")));

    /* a program's own export */
    {
        DWORD own = (DWORD)(FARPROC)TK65OWN;
        HINSTANCE self = (HINSTANCE)(entry_regs[E_DS] & ~1u);
        obs("own 0 TK65OWN", (DWORD)GetProcAddress(0, "TK65OWN") == own);
        obs("own 0 tk65own", (DWORD)GetProcAddress(0, "tk65own") == own);
        obs("own 0 #1", (DWORD)GetProcAddress(0, "#1") == own);
        obs("own 0 ord 1", (DWORD)GetProcAddress(0, MAKEINTRESOURCE(1)) == own);
        obs("own 0 nonzero", GetProcAddress(0, "TK65OWN") != 0);
        obs("own hInstance", GetProcAddress(self, "TK65OWN") != 0);
    }

    /* imports by name */
    for (i = 0; i < sizeof imp / sizeof imp[0]; i++) {
        char lab[40];
        lstrcpy(name, "TK65I1.DLL");
        name[5] = (char)('1' + i);
        hi = LoadLibrary(name);
        lstrcpy(lab, "imp ");
        lstrcat(lab, imp[i]);
        lstrcat(lab, ": loaded");
        obs(lab, hi > HINSTANCE_ERROR ? 1 : (WORD)hi);
        if (hi > HINSTANCE_ERROR) {
            DWORD (FAR PASCAL *get)(void) =
                (DWORD (FAR PASCAL *)(void))GetProcAddress(hi, "GETIMP");
            lstrcpy(lab, "imp ");
            lstrcat(lab, imp[i]);
            obs(lab, get ? which((FARPROC)get()) : 0xFFFF);
            FreeLibrary(hi);
        }
    }
    FreeLibrary(h);
    out("TK65 end\r\n");
    return failures;
}
