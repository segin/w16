/*
 * tk53.c - what makes TOOLHELP's GlobalEntry say a block has a local heap
 * (GLOBALENTRY.wHeapPresent): a GlobalAlloc'd block made a heap with
 * LocalInit, then each word of its heap information block (the near
 * pointer at offset 6 of the segment) set to 0 in turn and GlobalEntry-
 * Handle asked again; and the information block itself, dumped. Run as
 * the shell under tests/dropin.sh (-r for Microsoft's KRNL286; -3 for
 * w16's KRNL386, -3 -r for Microsoft's): TOOLHELP is Microsoft's either
 * way. It looks for "LH" at 22h under KRNL286, at 28h under KRNL386.
 */
#include <windows.h>
#include <toolhelp.h>

#define TK_REF "tk53ref.h"         /* Microsoft's KRNL386 (dropin.sh -3 -r) */
#define TK_REF286 "tk53ref286.h"   /* where KRNL286 differs (dropin.sh -r) */
#include "tklog.h"

typedef BOOL (FAR PASCAL *PGEH)(GLOBALENTRY FAR *, HGLOBAL);

static char name[48];

static void nmx(const char *a, WORD v)
{
    int i = 0, k;
    while (*a)
        name[i++] = *a++;
    for (k = 12; k >= 0; k -= 4)
        name[i++] = "0123456789ABCDEF"[(v >> k) & 15];
    name[i] = 0;
}

int __cdecl tmain(void)
{
    HINSTANCE th;
    PGEH geh;
    GLOBALENTRY ge;
    HGLOBAL h;
    WORD sel, info, k, save;
    WORD FAR *p;
    out("TK53 start\r\n");
    th = LoadLibrary("TOOLHELP.DLL");
    obs("TOOLHELP loaded", th > HINSTANCE_ERROR);
    if (th <= HINSTANCE_ERROR)
        return 1;
    geh = (PGEH)GetProcAddress(th, "GlobalEntryHandle");
    ge.dwSize = sizeof ge;

    h = GlobalAlloc(GMEM_MOVEABLE, 0x1000);
    p = (WORD FAR *)GlobalLock(h);
    sel = SELECTOROF(p);
    obs("block entry, no heap", geh(&ge, h));
    obs("block heap present, no heap", ge.wHeapPresent);
    obs("LocalInit", LocalInit(sel, 0x10, 0xFF0));
    obs("block entry", geh(&ge, h));
    obs("block heap present", ge.wHeapPresent);
    obs("block flags", ge.wFlags);
    obs("block type", ge.wType);
    info = p[3];
    obs("info at", info);
    for (k = 0; k < 0x30; k += 2) {
        nmx("~info word ", k);
        obs(name, *(WORD FAR *)MAKELP(sel, info + k));
    }
    /* each word of the information block zeroed in turn */
    for (k = 0; k < 0x30; k += 2) {
        WORD FAR *w = (WORD FAR *)MAKELP(sel, info + k);
        save = *w;
        *w = 0;
        geh(&ge, h);
        *w = save;
        nmx("heap present, info word zeroed ", k);
        obs(name, ge.wHeapPresent);
    }
    /* the pointer at 6 */
    save = p[3];
    p[3] = 0;
    geh(&ge, h);
    p[3] = save;
    obs("heap present, pointer at 6 zeroed", ge.wHeapPresent);
    GlobalUnlock(h);
    GlobalFree(h);
    out("TK53 done\r\n");
    return 0;
}
