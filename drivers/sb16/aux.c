/*
 * aux.c - w16's SB16.DRV: the auxiliary devices (auxMessage) and the
 * volumes, through the SB16's mixer (drivers/sb16/README.md,
 * ARCHITECTURE.md 24).
 *
 * Five auxiliary devices, one per mixer pair: master, wave (the DSP's
 * "voice"), MIDI (the FM synthesiser), CD audio and line in; wave
 * output's volume (waveOutSetVolume) is the second.
 *
 * Sources: Multimedia Programmer's Guide ch. 5 (AUXCAPS, AUXCAPS_CDAUDIO,
 * AUXCAPS_AUXIN, AUXCAPS_VOLUME, AUXCAPS_LRVOLUME; a volume is a DWORD,
 * left in the low word, right in the high word, 0 to FFFFh); Creative's
 * Sound Blaster Series Hardware Programming Guide for the SB16's mixer
 * (registers 30h/31h master, 32h/33h voice, 34h/35h MIDI, 36h/37h CD,
 * 38h/39h line, left then right, the level in bits 7-3).
 */
#include "sb16.h"

static DWORD vols[NVOL];
static const BYTE regs[NVOL] = { 0x30, 0x32, 0x34, 0x36, 0x38 };

#pragma pack(__push, 1)
struct auxcaps {
    WORD mid, pid, ver;
    char name[32];
    WORD tech;
    DWORD support;
};
#pragma pack(__pop)
/* manufacturer 1 as the other devices; no product ID is assigned to
   these (0); volume, left and right */
static const struct auxcaps caps[NVOL] = {
    { 1, 0, 0x0100, "Sound Blaster 16 Master", 2, 3 },
    { 1, 0, 0x0100, "Sound Blaster 16 Wave", 2, 3 },
    { 1, 0, 0x0100, "Sound Blaster 16 MIDI", 2, 3 },
    { 1, 0, 0x0100, "Sound Blaster 16 CD Audio", 1, 3 },
    { 1, 0, 0x0100, "Sound Blaster 16 Line In", 2, 3 },
};

/* a 5-bit level as a 16-bit volume (the bits repeated) */
static WORD widen(BYTE r)
{
    WORD v = r >> 3;
    return (v << 11) | (v << 6) | (v << 1) | (v >> 4);
}

/* the levels the mixer has now */
void vol_init(void)
{
    int i;
    for (i = 0; i < NVOL; i++)
        vols[i] = MAKELONG(widen(mix_read(regs[i])), widen(mix_read((BYTE)(regs[i] + 1))));
}

DWORD vol_get(int which)
{
    return vols[which];
}

void vol_set(int which, DWORD v)
{
    vols[which] = v;
    mix_write(regs[which], (BYTE)((LOWORD(v) >> 11) << 3));
    mix_write((BYTE)(regs[which] + 1), (BYTE)((HIWORD(v) >> 11) << 3));
}

DWORD ENTRY AUXMESSAGE(WORD dev, WORD msg, DWORD user, DWORD p1, DWORD p2)
{
    (void)user;
    if (msg == AUXDM_GETNUMDEVS)
        return sb_ok ? NVOL : 0;
    if (msg != AUXDM_GETDEVCAPS && msg != AUXDM_GETVOLUME && msg != AUXDM_SETVOLUME)
        return MMSYSERR_NOTSUPPORTED;
    if (!sb_ok || dev >= NVOL)
        return MMSYSERR_BADDEVICEID;
    switch (msg) {
    case AUXDM_GETDEVCAPS:
        copycaps((LPVOID)p1, &caps[dev], sizeof caps[0], p2);
        return 0;
    case AUXDM_GETVOLUME:
        if (!p1)
            return MMSYSERR_INVALPARAM;
        *(DWORD FAR *)p1 = vols[dev];
        return 0;
    }
    vol_set(dev, p1);
    return 0;
}
