/*
 * tk65d.c - TK65D.DLL, the DLL TK65 (tk65.c) asks GetProcAddress about:
 * exports whose names are in upper, mixed and lower case, in the
 * resident and the non-resident name table (tk65d.lnk). Each returns
 * its ordinal.
 */
#include <windows.h>

int __cdecl libmain(void)
{
    return 1;
}

WORD FAR PASCAL F1(void) { return 1; }
WORD FAR PASCAL F2(void) { return 2; }
WORD FAR PASCAL F3(void) { return 3; }
WORD FAR PASCAL F4(void) { return 4; }
WORD FAR PASCAL F5(void) { return 5; }
WORD FAR PASCAL F6(void) { return 6; }
WORD FAR PASCAL F8(void) { return 8; }
WORD FAR PASCAL F9(void) { return 9; }
WORD FAR PASCAL F10(void) { return 10; }
WORD FAR PASCAL F11(void) { return 11; }
WORD FAR PASCAL F12(void) { return 12; }

int FAR PASCAL WEP(int param)                                 /* 7 */
{
    (void)param;
    return 1;
}
