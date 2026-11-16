/*
 * misc.c - w16 SHELL.DLL: initialisation, WEP, and the exports nothing
 * in Windows 3.1 imports (docs/exportdb: SHELL.32, 100, 104-128).
 *
 * WCI (32) and HERETHARBETYGARS (100) are internal to SHELL (Schulman et
 * al., Undocumented Windows, ch. 2, list them as SHELL importing itself;
 * 100 is the About box's hidden credits). The unnamed ordinals 104-128
 * are code, except 107, 121, 127 and 128, which are in data segments
 * (NE entry table, docs/exportdb). w16 gives each a stub that returns 0,
 * and data words for the data ones. UNVERIFIED: what any of them does.
 */
#include "shell.h"

HINSTANCE g_inst;

int __cdecl shell_init(WORD hinst)
{
    g_inst = (HINSTANCE)hinst;
    reg_init();
    hook_init();
    return 1;
}

int SHAPI WEP(int exiting)
{
    (void)exiting;
    return 1;
}

LONG SHAPI WCI(void) { return 0; }
LONG SHAPI HERETHARBETYGARS(void) { return 0; }

#define STUB(n) LONG SHAPI SHELL_##n(void) { return 0; }
STUB(104) STUB(105) STUB(106) STUB(108) STUB(109) STUB(110) STUB(111)
STUB(112) STUB(113) STUB(114) STUB(115) STUB(116) STUB(117) STUB(118)
STUB(119) STUB(120) STUB(122) STUB(123) STUB(124) STUB(125) STUB(126)

WORD SHELL_107[2], SHELL_121[2], SHELL_127[2], SHELL_128[2];
