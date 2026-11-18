/*
 * stubs.c - w16's MMSYSTEM.DLL: the exports of the layers not written
 * yet (ARCHITECTURE.md 23; docs/notes/mmsystem-handover.md has the
 * plan). Each takes the arguments the Multimedia Programmer's Guide gives
 * it (a Pascal function pops them) and answers as when no device or
 * driver is there. Generated once from a table; edited by hand since.
 */
#include "mmsys.h"

UINT MMAPI JOYGETNUMDEVS(void)
{
    return 0;
}

UINT MMAPI JOYGETDEVCAPS(WORD a0, DWORD a1, WORD a2)
{
    (void)a0;
    (void)a1;
    (void)a2;
    return MMSYSERR_NODRIVER;
}

UINT MMAPI JOYGETPOS(WORD a0, DWORD a1)
{
    (void)a0;
    (void)a1;
    return MMSYSERR_NODRIVER;
}

UINT MMAPI JOYGETTHRESHOLD(WORD a0, DWORD a1)
{
    (void)a0;
    (void)a1;
    return MMSYSERR_NODRIVER;
}

UINT MMAPI JOYRELEASECAPTURE(WORD a0)
{
    (void)a0;
    return MMSYSERR_NODRIVER;
}

UINT MMAPI JOYSETCAPTURE(WORD a0, WORD a1, WORD a2, WORD a3)
{
    if (!IsWindow((HWND)a0))
        return JOYERR_PARMS;           /* before the driver (3.1: MMP section y) */
    (void)a1;
    (void)a2;
    (void)a3;
    return MMSYSERR_NODRIVER;
}

UINT MMAPI JOYSETTHRESHOLD(WORD a0, WORD a1)
{
    (void)a0;
    (void)a1;
    return MMSYSERR_NODRIVER;
}

DWORD MMAPI MCISENDCOMMAND(WORD a0, WORD a1, DWORD a2, DWORD a3)
{
    (void)a0;
    (void)a1;
    (void)a2;
    (void)a3;
    return MCIERR_BASE + 1;
}

DWORD MMAPI MCISENDSTRING(DWORD a0, DWORD a1, WORD a2, WORD a3)
{
    (void)a0;
    (void)a1;
    (void)a2;
    (void)a3;
    return MCIERR_BASE + 7;
}

UINT MMAPI MCIGETDEVICEID(DWORD a0)
{
    (void)a0;
    return 0;
}

UINT MMAPI MCILOADCOMMANDRESOURCE(WORD a0, DWORD a1, WORD a2)
{
    (void)a0;
    (void)a1;
    (void)a2;
    return 0xFFFF;
}

BOOL MMAPI MCIGETERRORSTRING(DWORD a0, DWORD a1, WORD a2)
{
    (void)a0;
    (void)a1;
    (void)a2;
    return FALSE;
}

BOOL MMAPI MCISETDRIVERDATA(WORD a0, DWORD a1)
{
    (void)a0;
    (void)a1;
    return FALSE;
}

DWORD MMAPI MCIGETDRIVERDATA(WORD a0)
{
    (void)a0;
    return 0;
}

UINT MMAPI MCIDRIVERYIELD(WORD a0)
{
    (void)a0;
    return 0;
}

BOOL MMAPI MCIDRIVERNOTIFY(WORD a0, WORD a1, WORD a2)
{
    (void)a0;
    (void)a1;
    (void)a2;
    return FALSE;
}

BOOL MMAPI MCIEXECUTE(DWORD a0)
{
    (void)a0;
    return FALSE;
}

BOOL MMAPI MCIFREECOMMANDRESOURCE(WORD a0)
{
    (void)a0;
    return FALSE;
}

BOOL MMAPI MCISETYIELDPROC(WORD a0, DWORD a1, DWORD a2)
{
    (void)a0;
    (void)a1;
    (void)a2;
    return FALSE;
}

UINT MMAPI MCIGETDEVICEIDFROMELEMENTID(DWORD a0, DWORD a1)
{
    (void)a0;
    (void)a1;
    return 0;
}

DWORD MMAPI MCIGETYIELDPROC(WORD a0, DWORD a1)
{
    (void)a0;
    (void)a1;
    return 0;
}

UINT MMAPI MCIGETCREATORTASK(WORD a0)
{
    (void)a0;
    return 0;
}


















/* Not documented; how many bytes of arguments each pops was measured on
   Windows 3.1 (tests/mmsystem/mmp.c, section pops) */
UINT MMAPI MMDRVINSTALL(WORD a0, DWORD a1, WORD a2)
{
    (void)a0;
    (void)a1;
    (void)a2;
    return 0;
}

UINT MMAPI JOYSETCALIBRATION(WORD a0, DWORD a1, DWORD a2, DWORD a3, DWORD a4,
                             DWORD a5, DWORD a6)
{
    (void)a0;
    (void)a1;
    (void)a2;
    (void)a3;
    (void)a4;
    (void)a5;
    (void)a6;
    return MMSYSERR_NODRIVER;
}

/* UNVERIFIED: 3.1's faults with ten zero words (MMP, pops) */
DWORD MMAPI MCIPARSECOMMAND(WORD a0, DWORD a1, DWORD a2, DWORD a3, WORD a4)
{
    (void)a0;
    (void)a1;
    (void)a2;
    (void)a3;
    (void)a4;
    return 0;
}
