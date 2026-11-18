/*
 * mds.c - MDS*.DRV, the logging pass-through device drivers
 * (ARCHITECTURE.md 23.1). SYSTEM.INI [drivers] wave=mdsw1.drv puts one
 * where MMSYSTEM opens a wave (MIDI, auxiliary) driver; [mmslog]
 * MDSW1=sndblst2.drv names the real driver, which it loads as a library
 * the first time it is called. Every call of its DriverProc and of its
 * wodMessage, widMessage, modMessage, midMessage and auxMessage goes to
 * MMSYSLOG.DLL's MlCall (tests/mmsystem/mdl.c), which records it and
 * calls the real driver's entry of the same name. The variants differ
 * only in their module names and in which entries they export (the same
 * as the real driver's: tests/mmsystem/mdsw.lnk etc.), so that MMSYSTEM
 * finds what it would find in the real one. Black-box observation between
 * Microsoft's modules (ARCHITECTURE.md 3).
 */
#include <windows.h>
#include "mdl.h"

extern WORD __cdecl lib_regs[12];       /* dllstart.asm: DI = the instance */

static const char *const entries[MDK_NUM] = {
    "DriverProc", "wodMessage", "widMessage", "modMessage", "midMessage", "auxMessage"
};
static FARPROC procs[MDK_NUM];
static HINSTANCE real;
static WORD shimno, loaded;
static char me[10];

int __cdecl libmain(void)
{
    char path[128];
    int n, i, j;
    n = GetModuleFileName((HINSTANCE)lib_regs[5], path, sizeof path);
    for (i = n; i > 0 && path[i - 1] != '\\' && path[i - 1] != ':'; i--)
        ;
    for (j = 0; j < 9 && path[i + j] && path[i + j] != '.'; j++)
        me[j] = path[i + j];
    me[j] = 0;
    shimno = MlRegister(me);
    return 1;
}

static void load(void)
{
    char name[80];
    int k;
    if (loaded)
        return;
    loaded = 1;
    GetPrivateProfileString("mmslog", me, "", name, sizeof name, "SYSTEM.INI");
    real = LoadLibrary(name);
    if ((UINT)real < 32) {
        real = 0;
        return;
    }
    for (k = 0; k < MDK_NUM; k++)
        procs[k] = GetProcAddress(real, entries[k]);
}

DWORD __cdecl __far mds_entry(WORD kind, WORD flags, WORD retcs, void __far *args)
{
    load();
    return MlCall(shimno, kind, procs[kind], flags, retcs, args);
}
