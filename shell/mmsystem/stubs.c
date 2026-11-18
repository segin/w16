/*
 * stubs.c - w16's MMSYSTEM.DLL: the exports of the layers not written
 * yet (ARCHITECTURE.md 23; docs/notes/mmsystem-handover.md has the
 * plan). Each takes the arguments the Multimedia Programmer's Guide gives
 * it (a Pascal function pops them) and answers as when no device or
 * driver is there. Generated once from a table; edited by hand since.
 */
#include "mmsys.h"

BOOL MMAPI SNDPLAYSOUND(DWORD a0, WORD a1)
{
    (void)a0;
    (void)a1;
    return FALSE;
}

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
    (void)a0;
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

UINT MMAPI MIDIOUTGETNUMDEVS(void)
{
    return 0;
}

UINT MMAPI MIDIOUTGETDEVCAPS(WORD a0, DWORD a1, WORD a2)
{
    (void)a0;
    (void)a1;
    (void)a2;
    return MMSYSERR_BADDEVICEID;
}

UINT MMAPI MIDIOUTGETERRORTEXT(WORD a0, DWORD a1, WORD a2)
{
    (void)a0;
    (void)a1;
    (void)a2;
    return MMSYSERR_BADERRNUM;
}

UINT MMAPI MIDIOUTOPEN(DWORD a0, WORD a1, DWORD a2, DWORD a3, DWORD a4)
{
    (void)a0;
    (void)a1;
    (void)a2;
    (void)a3;
    (void)a4;
    return MMSYSERR_BADDEVICEID;
}

UINT MMAPI MIDIOUTCLOSE(WORD a0)
{
    (void)a0;
    return MMSYSERR_INVALHANDLE;
}

UINT MMAPI MIDIOUTPREPAREHEADER(WORD a0, DWORD a1, WORD a2)
{
    (void)a0;
    (void)a1;
    (void)a2;
    return MMSYSERR_INVALHANDLE;
}

UINT MMAPI MIDIOUTUNPREPAREHEADER(WORD a0, DWORD a1, WORD a2)
{
    (void)a0;
    (void)a1;
    (void)a2;
    return MMSYSERR_INVALHANDLE;
}

UINT MMAPI MIDIOUTSHORTMSG(WORD a0, DWORD a1)
{
    (void)a0;
    (void)a1;
    return MMSYSERR_INVALHANDLE;
}

UINT MMAPI MIDIOUTLONGMSG(WORD a0, DWORD a1, WORD a2)
{
    (void)a0;
    (void)a1;
    (void)a2;
    return MMSYSERR_INVALHANDLE;
}

UINT MMAPI MIDIOUTRESET(WORD a0)
{
    (void)a0;
    return MMSYSERR_INVALHANDLE;
}

UINT MMAPI MIDIOUTGETVOLUME(WORD a0, DWORD a1)
{
    (void)a0;
    (void)a1;
    return MMSYSERR_BADDEVICEID;
}

UINT MMAPI MIDIOUTSETVOLUME(WORD a0, DWORD a1)
{
    (void)a0;
    (void)a1;
    return MMSYSERR_BADDEVICEID;
}

UINT MMAPI MIDIOUTCACHEPATCHES(WORD a0, WORD a1, DWORD a2, WORD a3)
{
    (void)a0;
    (void)a1;
    (void)a2;
    (void)a3;
    return MMSYSERR_INVALHANDLE;
}

UINT MMAPI MIDIOUTCACHEDRUMPATCHES(WORD a0, WORD a1, DWORD a2, WORD a3)
{
    (void)a0;
    (void)a1;
    (void)a2;
    (void)a3;
    return MMSYSERR_INVALHANDLE;
}

UINT MMAPI MIDIOUTGETID(WORD a0, DWORD a1)
{
    (void)a0;
    (void)a1;
    return MMSYSERR_INVALHANDLE;
}

DWORD MMAPI MIDIOUTMESSAGE(WORD a0, WORD a1, DWORD a2, DWORD a3)
{
    (void)a0;
    (void)a1;
    (void)a2;
    (void)a3;
    return MMSYSERR_INVALHANDLE;
}

UINT MMAPI MIDIINGETNUMDEVS(void)
{
    return 0;
}

UINT MMAPI MIDIINGETDEVCAPS(WORD a0, DWORD a1, WORD a2)
{
    (void)a0;
    (void)a1;
    (void)a2;
    return MMSYSERR_BADDEVICEID;
}

UINT MMAPI MIDIINGETERRORTEXT(WORD a0, DWORD a1, WORD a2)
{
    (void)a0;
    (void)a1;
    (void)a2;
    return MMSYSERR_BADERRNUM;
}

UINT MMAPI MIDIINOPEN(DWORD a0, WORD a1, DWORD a2, DWORD a3, DWORD a4)
{
    (void)a0;
    (void)a1;
    (void)a2;
    (void)a3;
    (void)a4;
    return MMSYSERR_BADDEVICEID;
}

UINT MMAPI MIDIINCLOSE(WORD a0)
{
    (void)a0;
    return MMSYSERR_INVALHANDLE;
}

UINT MMAPI MIDIINPREPAREHEADER(WORD a0, DWORD a1, WORD a2)
{
    (void)a0;
    (void)a1;
    (void)a2;
    return MMSYSERR_INVALHANDLE;
}

UINT MMAPI MIDIINUNPREPAREHEADER(WORD a0, DWORD a1, WORD a2)
{
    (void)a0;
    (void)a1;
    (void)a2;
    return MMSYSERR_INVALHANDLE;
}

UINT MMAPI MIDIINADDBUFFER(WORD a0, DWORD a1, WORD a2)
{
    (void)a0;
    (void)a1;
    (void)a2;
    return MMSYSERR_INVALHANDLE;
}

UINT MMAPI MIDIINSTART(WORD a0)
{
    (void)a0;
    return MMSYSERR_INVALHANDLE;
}

UINT MMAPI MIDIINSTOP(WORD a0)
{
    (void)a0;
    return MMSYSERR_INVALHANDLE;
}

UINT MMAPI MIDIINRESET(WORD a0)
{
    (void)a0;
    return MMSYSERR_INVALHANDLE;
}

UINT MMAPI MIDIINGETID(WORD a0, DWORD a1)
{
    (void)a0;
    (void)a1;
    return MMSYSERR_INVALHANDLE;
}

DWORD MMAPI MIDIINMESSAGE(WORD a0, WORD a1, DWORD a2, DWORD a3)
{
    (void)a0;
    (void)a1;
    (void)a2;
    (void)a3;
    return MMSYSERR_INVALHANDLE;
}

UINT MMAPI AUXGETNUMDEVS(void)
{
    return 0;
}

UINT MMAPI AUXGETDEVCAPS(WORD a0, DWORD a1, WORD a2)
{
    (void)a0;
    (void)a1;
    (void)a2;
    return MMSYSERR_BADDEVICEID;
}

UINT MMAPI AUXGETVOLUME(WORD a0, DWORD a1)
{
    (void)a0;
    (void)a1;
    return MMSYSERR_BADDEVICEID;
}

UINT MMAPI AUXSETVOLUME(WORD a0, DWORD a1)
{
    (void)a0;
    (void)a1;
    return MMSYSERR_BADDEVICEID;
}

DWORD MMAPI AUXOUTMESSAGE(WORD a0, WORD a1, DWORD a2, DWORD a3)
{
    (void)a0;
    (void)a1;
    (void)a2;
    (void)a3;
    return MMSYSERR_BADDEVICEID;
}

UINT MMAPI WAVEOUTGETNUMDEVS(void)
{
    return 0;
}

UINT MMAPI WAVEOUTGETDEVCAPS(WORD a0, DWORD a1, WORD a2)
{
    (void)a0;
    (void)a1;
    (void)a2;
    return MMSYSERR_BADDEVICEID;
}

UINT MMAPI WAVEOUTGETERRORTEXT(WORD a0, DWORD a1, WORD a2)
{
    (void)a0;
    (void)a1;
    (void)a2;
    return MMSYSERR_BADERRNUM;
}

UINT MMAPI WAVEOUTOPEN(DWORD a0, WORD a1, DWORD a2, DWORD a3, DWORD a4, DWORD a5)
{
    (void)a0;
    (void)a1;
    (void)a2;
    (void)a3;
    (void)a4;
    (void)a5;
    return MMSYSERR_BADDEVICEID;
}

UINT MMAPI WAVEOUTCLOSE(WORD a0)
{
    (void)a0;
    return MMSYSERR_INVALHANDLE;
}

UINT MMAPI WAVEOUTPREPAREHEADER(WORD a0, DWORD a1, WORD a2)
{
    (void)a0;
    (void)a1;
    (void)a2;
    return MMSYSERR_INVALHANDLE;
}

UINT MMAPI WAVEOUTUNPREPAREHEADER(WORD a0, DWORD a1, WORD a2)
{
    (void)a0;
    (void)a1;
    (void)a2;
    return MMSYSERR_INVALHANDLE;
}

UINT MMAPI WAVEOUTWRITE(WORD a0, DWORD a1, WORD a2)
{
    (void)a0;
    (void)a1;
    (void)a2;
    return MMSYSERR_INVALHANDLE;
}

UINT MMAPI WAVEOUTPAUSE(WORD a0)
{
    (void)a0;
    return MMSYSERR_INVALHANDLE;
}

UINT MMAPI WAVEOUTRESTART(WORD a0)
{
    (void)a0;
    return MMSYSERR_INVALHANDLE;
}

UINT MMAPI WAVEOUTRESET(WORD a0)
{
    (void)a0;
    return MMSYSERR_INVALHANDLE;
}

UINT MMAPI WAVEOUTGETPOSITION(WORD a0, DWORD a1, WORD a2)
{
    (void)a0;
    (void)a1;
    (void)a2;
    return MMSYSERR_INVALHANDLE;
}

UINT MMAPI WAVEOUTGETPITCH(WORD a0, DWORD a1)
{
    (void)a0;
    (void)a1;
    return MMSYSERR_INVALHANDLE;
}

UINT MMAPI WAVEOUTSETPITCH(WORD a0, DWORD a1)
{
    (void)a0;
    (void)a1;
    return MMSYSERR_INVALHANDLE;
}

UINT MMAPI WAVEOUTGETVOLUME(WORD a0, DWORD a1)
{
    (void)a0;
    (void)a1;
    return MMSYSERR_BADDEVICEID;
}

UINT MMAPI WAVEOUTSETVOLUME(WORD a0, DWORD a1)
{
    (void)a0;
    (void)a1;
    return MMSYSERR_BADDEVICEID;
}

UINT MMAPI WAVEOUTGETPLAYBACKRATE(WORD a0, DWORD a1)
{
    (void)a0;
    (void)a1;
    return MMSYSERR_INVALHANDLE;
}

UINT MMAPI WAVEOUTSETPLAYBACKRATE(WORD a0, DWORD a1)
{
    (void)a0;
    (void)a1;
    return MMSYSERR_INVALHANDLE;
}

UINT MMAPI WAVEOUTBREAKLOOP(WORD a0)
{
    (void)a0;
    return MMSYSERR_INVALHANDLE;
}

UINT MMAPI WAVEOUTGETID(WORD a0, DWORD a1)
{
    (void)a0;
    (void)a1;
    return MMSYSERR_INVALHANDLE;
}

DWORD MMAPI WAVEOUTMESSAGE(WORD a0, WORD a1, DWORD a2, DWORD a3)
{
    (void)a0;
    (void)a1;
    (void)a2;
    (void)a3;
    return MMSYSERR_INVALHANDLE;
}

UINT MMAPI WAVEINGETNUMDEVS(void)
{
    return 0;
}

UINT MMAPI WAVEINGETDEVCAPS(WORD a0, DWORD a1, WORD a2)
{
    (void)a0;
    (void)a1;
    (void)a2;
    return MMSYSERR_BADDEVICEID;
}

UINT MMAPI WAVEINGETERRORTEXT(WORD a0, DWORD a1, WORD a2)
{
    (void)a0;
    (void)a1;
    (void)a2;
    return MMSYSERR_BADERRNUM;
}

UINT MMAPI WAVEINOPEN(DWORD a0, WORD a1, DWORD a2, DWORD a3, DWORD a4, DWORD a5)
{
    (void)a0;
    (void)a1;
    (void)a2;
    (void)a3;
    (void)a4;
    (void)a5;
    return MMSYSERR_BADDEVICEID;
}

UINT MMAPI WAVEINCLOSE(WORD a0)
{
    (void)a0;
    return MMSYSERR_INVALHANDLE;
}

UINT MMAPI WAVEINPREPAREHEADER(WORD a0, DWORD a1, WORD a2)
{
    (void)a0;
    (void)a1;
    (void)a2;
    return MMSYSERR_INVALHANDLE;
}

UINT MMAPI WAVEINUNPREPAREHEADER(WORD a0, DWORD a1, WORD a2)
{
    (void)a0;
    (void)a1;
    (void)a2;
    return MMSYSERR_INVALHANDLE;
}

UINT MMAPI WAVEINADDBUFFER(WORD a0, DWORD a1, WORD a2)
{
    (void)a0;
    (void)a1;
    (void)a2;
    return MMSYSERR_INVALHANDLE;
}

UINT MMAPI WAVEINSTART(WORD a0)
{
    (void)a0;
    return MMSYSERR_INVALHANDLE;
}

UINT MMAPI WAVEINSTOP(WORD a0)
{
    (void)a0;
    return MMSYSERR_INVALHANDLE;
}

UINT MMAPI WAVEINRESET(WORD a0)
{
    (void)a0;
    return MMSYSERR_INVALHANDLE;
}

UINT MMAPI WAVEINGETPOSITION(WORD a0, DWORD a1, WORD a2)
{
    (void)a0;
    (void)a1;
    (void)a2;
    return MMSYSERR_INVALHANDLE;
}

UINT MMAPI WAVEINGETID(WORD a0, DWORD a1)
{
    (void)a0;
    (void)a1;
    return MMSYSERR_INVALHANDLE;
}

DWORD MMAPI WAVEINMESSAGE(WORD a0, WORD a1, DWORD a2, DWORD a3)
{
    (void)a0;
    (void)a1;
    (void)a2;
    (void)a3;
    return MMSYSERR_INVALHANDLE;
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

UINT MMAPI MMIOOPEN(DWORD a0, DWORD a1, DWORD a2)
{
    (void)a0;
    (void)a1;
    (void)a2;
    return 0;
}

UINT MMAPI MMIOCLOSE(WORD a0, WORD a1)
{
    (void)a0;
    (void)a1;
    return MMIOERR_BASE + 4;
}

LONG MMAPI MMIOREAD(WORD a0, DWORD a1, DWORD a2)
{
    (void)a0;
    (void)a1;
    (void)a2;
    return -1;
}

LONG MMAPI MMIOWRITE(WORD a0, DWORD a1, DWORD a2)
{
    (void)a0;
    (void)a1;
    (void)a2;
    return -1;
}

LONG MMAPI MMIOSEEK(WORD a0, DWORD a1, WORD a2)
{
    (void)a0;
    (void)a1;
    (void)a2;
    return -1;
}

UINT MMAPI MMIOGETINFO(WORD a0, DWORD a1, WORD a2)
{
    (void)a0;
    (void)a1;
    (void)a2;
    return MMSYSERR_INVALHANDLE;
}

UINT MMAPI MMIOSETINFO(WORD a0, DWORD a1, WORD a2)
{
    (void)a0;
    (void)a1;
    (void)a2;
    return MMSYSERR_INVALHANDLE;
}

UINT MMAPI MMIOSETBUFFER(WORD a0, DWORD a1, DWORD a2, WORD a3)
{
    (void)a0;
    (void)a1;
    (void)a2;
    (void)a3;
    return MMSYSERR_INVALHANDLE;
}

UINT MMAPI MMIOFLUSH(WORD a0, WORD a1)
{
    (void)a0;
    (void)a1;
    return MMSYSERR_INVALHANDLE;
}

UINT MMAPI MMIOADVANCE(WORD a0, DWORD a1, WORD a2)
{
    (void)a0;
    (void)a1;
    (void)a2;
    return MMSYSERR_INVALHANDLE;
}

DWORD MMAPI MMIOSTRINGTOFOURCC(DWORD a0, WORD a1)
{
    (void)a0;
    (void)a1;
    return 0;
}

DWORD MMAPI MMIOINSTALLIOPROC(DWORD a0, DWORD a1, DWORD a2)
{
    (void)a0;
    (void)a1;
    (void)a2;
    return 0;
}

LONG MMAPI MMIOSENDMESSAGE(WORD a0, WORD a1, DWORD a2, DWORD a3)
{
    (void)a0;
    (void)a1;
    (void)a2;
    (void)a3;
    return 0;
}

UINT MMAPI MMIODESCEND(WORD a0, DWORD a1, DWORD a2, WORD a3)
{
    (void)a0;
    (void)a1;
    (void)a2;
    (void)a3;
    return MMIOERR_BASE + 9;
}

UINT MMAPI MMIOASCEND(WORD a0, DWORD a1, WORD a2)
{
    (void)a0;
    (void)a1;
    (void)a2;
    return MMIOERR_BASE + 6;
}

UINT MMAPI MMIOCREATECHUNK(WORD a0, DWORD a1, WORD a2)
{
    (void)a0;
    (void)a1;
    (void)a2;
    return MMIOERR_BASE + 6;
}

UINT MMAPI MMIORENAME(DWORD a0, DWORD a1, DWORD a2, DWORD a3)
{
    (void)a0;
    (void)a1;
    (void)a2;
    (void)a3;
    return MMIOERR_BASE + 1;
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
