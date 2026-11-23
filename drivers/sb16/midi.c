/*
 * midi.c - w16's SB16.DRV: MIDI output and input through the card's
 * MPU-401 port in UART mode (modMessage, midMessage; drivers/sb16/
 * README.md, ARCHITECTURE.md 24). The OPL3 FM synthesiser is not driven
 * (no FM device: README).
 *
 * Sources: Multimedia Programmer's Guide ch. 5 (the MIDI functions, short
 * messages packed in a DWORD with the status byte lowest, MIDIHDR,
 * MOM_* and MIM_* messages, time stamps in milliseconds from midiInStart);
 * the MIDI 1.0 specification's message lengths, running status and
 * Channel Mode message 123 (All Notes Off); what Windows 3.1's
 * SNDBLST2.DRV answers, as observed (tests/mmsystem, ARCHITECTURE.md 23.2
 * and 24.1): the open leaves the instance alone, MOM_OPEN and MOM_CLOSE
 * inside the open and close, a long message sent before LONGDATA returns
 * with DONE set and MOM_DONE inside the call, PREPARE/UNPREPARE and
 * volume 8, a second open 4; input: ADDBUFFER sets INQUEUE, STOP keeps
 * the queue, RESET returns every buffer with MIM_LONGDATA and the time
 * since the start, a close with buffers queued 65. The MPU-401: Roland's
 * MPU-401 command set as the SB16 has it (Creative's Hardware Programming
 * Guide: data port base, status/command port base+1; status bit 6 clear
 * when it takes a byte, bit 7 clear when it has one; command FFh reset
 * and 3Fh UART mode, each acknowledged with FEh).
 */
#include "sb16.h"
#include <conio.h>

static struct {
    BYTE open, rstat;                   /* running status sent last */
    struct cbinfo cb;
    BYTE on[16][16];                    /* the notes on, per channel */
} mo;

static struct {
    BYTE open, started, stat, need, have, d1, sysex;
    struct cbinfo cb;
    LPMHDR head, tail;
    DWORD t0;
} mi;

#pragma pack(__push, 1)
struct moutcaps {
    WORD mid, pid, ver;
    char name[32];
    WORD tech, voices, notes, chmask;
    DWORD support;
};
struct mincaps {
    WORD mid, pid, ver;
    char name[32];
};
#pragma pack(__pop)
/* Microsoft's IDs for the Sound Blaster's MIDI port (as SNDBLST2 reports
   them); a MIDI port (MOD_MIDIPORT), all 16 channels, no volume */
static const struct moutcaps ocaps = { 1, 3, 0x0100, "Sound Blaster 16 MIDI", 1, 0, 0, 0xFFFF, 0 };
static const struct mincaps icaps = { 1, 4, 0x0100, "Sound Blaster 16 MIDI" };

/* ---- the MPU-401 ---- */
static int mpu_out(BYTE b)
{
    WORD i;
    for (i = 0; i < 0x4000; i++)
        if (!(inp(sb_mpu + 1) & 0x40)) {
            outp(sb_mpu, b);
            return 1;
        }
    return 0;
}

/* a command, 1 when it was acknowledged (FEh) */
static int mpu_cmd(BYTE c)
{
    WORD i;
    for (i = 0; i < 0x4000 && (inp(sb_mpu + 1) & 0x40); i++)
        ;
    outp(sb_mpu + 1, c);
    for (i = 0; i < 0x4000; i++)
        if (!(inp(sb_mpu + 1) & 0x80) && inp(sb_mpu) == 0xFE)
            return 1;
    return 0;
}

/* reset (twice: in UART mode the first is not acknowledged), then UART
   mode */
int mpu_reset(void)
{
    if (!mpu_cmd(0xFF) && !mpu_cmd(0xFF))
        return 0;
    return mpu_cmd(0x3F);
}

/* the data bytes after a status byte */
static BYTE datalen(BYTE s)
{
    if (s < 0xC0 || (s >= 0xE0 && s < 0xF0))
        return 2;
    if (s < 0xE0)
        return 1;
    if (s == 0xF1 || s == 0xF3)
        return 1;
    if (s == 0xF2)
        return 2;
    return 0;
}

/* a short message: running status kept for channel messages; a DWORD
   whose low byte is not a status byte continues the last one (as the SDK
   allows) */
static void shortmsg(DWORD m)
{
    BYTE s = (BYTE)m, n, d[2], i;
    WORD f = sb_cli();
    if (s >= 0xF8) {
        mpu_out(s);
        sb_sti(f);
        return;
    }
    if (s >= 0x80) {
        if (s < 0xF0) {
            if (s != mo.rstat)
                mpu_out(s);
            mo.rstat = s;
        } else {
            mpu_out(s);
            mo.rstat = 0;
        }
        d[0] = (BYTE)(m >> 8);
        d[1] = (BYTE)(m >> 16);
    } else {
        if (!mo.rstat) {
            sb_sti(f);
            return;
        }
        s = mo.rstat;
        d[0] = (BYTE)m;
        d[1] = (BYTE)(m >> 8);
    }
    n = datalen(s);
    for (i = 0; i < n; i++)
        mpu_out(d[i] & 0x7F);
    if ((s & 0xF0) == 0x90 && (d[1] & 0x7F))
        mo.on[s & 15][(d[0] & 0x7F) >> 3] |= (BYTE)(1 << (d[0] & 7));
    else if ((s & 0xF0) == 0x80 || (s & 0xF0) == 0x90)
        mo.on[s & 15][(d[0] & 0x7F) >> 3] &= (BYTE)~(1 << (d[0] & 7));
    sb_sti(f);
}

/* a reset: a note off for every note on, and All Notes Off on every
   channel */
static void allnotesoff(void)
{
    BYTE c, k;
    WORD f;
    for (c = 0; c < 16; c++) {
        for (k = 0; k < 128; k++)
            if (mo.on[c][k >> 3] & (1 << (k & 7))) {
                f = sb_cli();
                mpu_out((BYTE)(0x80 | c));
                mpu_out(k);
                mpu_out(0);
                sb_sti(f);
            }
        f = sb_cli();
        mpu_out((BYTE)(0xB0 | c));
        mpu_out(123);
        mpu_out(0);
        sb_sti(f);
        for (k = 0; k < 16; k++)
            mo.on[c][k] = 0;
    }
    mo.rstat = 0;
}

DWORD ENTRY MODMESSAGE(WORD dev, WORD msg, DWORD user, DWORD p1, DWORD p2)
{
    LPMHDR h;
    DWORD i;
    WORD f;
    MOPENDESC FAR *d;
    (void)user;
    switch (msg) {
    case MODM_GETNUMDEVS:
        return mpu_ok ? 1 : 0;
    case MODM_GETDEVCAPS:
        if (dev)
            return MMSYSERR_BADDEVICEID;
        copycaps((LPVOID)p1, &ocaps, sizeof ocaps, p2);
        return 0;
    case MODM_OPEN:
        if (dev)
            return MMSYSERR_BADDEVICEID;
        if (!mpu_ok)
            return MMSYSERR_NOTENABLED;
        if (mo.open)
            return MMSYSERR_ALLOCATED;
        d = (MOPENDESC FAR *)p1;
        mo.cb.h = d->hMidi;
        mo.cb.cb = d->dwCallback;
        mo.cb.inst = d->dwInstance;
        mo.cb.flags = (HIWORD(p2) & 7) | DCB_NOSWITCH;
        mo.rstat = 0;
        mo.open = 1;
        notify(&mo.cb, MM_MOM_OPEN, 0, 0);
        return 0;
    }
    if (!mo.open)
        return MMSYSERR_NOTSUPPORTED;
    switch (msg) {
    case MODM_CLOSE:
        mo.open = 0;
        notify(&mo.cb, MM_MOM_CLOSE, 0, 0);
        return 0;
    case MODM_DATA:
        shortmsg(p1);
        return 0;
    case MODM_LONGDATA:
        h = (LPMHDR)p1;
        if (!(h->dwFlags & MHDR_PREPARED))
            return MIDIERR_UNPREPARED;
        for (i = 0; i < h->dwBufferLength; i++) {
            BYTE b;
            sb_hcopy((LPSTR)&b, h->lpData, i, 1, 0);
            f = sb_cli();
            mpu_out(b);
            sb_sti(f);
        }
        mo.rstat = 0;
        h->dwFlags = (h->dwFlags & ~MHDR_INQUEUE) | MHDR_DONE;
        notify(&mo.cb, MM_MOM_DONE, (DWORD)h, 0);
        return 0;
    case MODM_RESET:
        allnotesoff();
        return 0;
    }
    return MMSYSERR_NOTSUPPORTED;       /* PREPARE, UNPREPARE, volume, patches */
}

/* ---- input (at interrupt time: sb16a.asm's handler queues the bytes,
   sb_work hands them here) ---- */

/* the first long buffer back with what it holds */
static void longdone(DWORD t)
{
    LPMHDR h = mi.head;
    WORD f;
    if (!h)
        return;
    f = sb_cli();
    mi.head = h->lpNext;
    if (!mi.head)
        mi.tail = 0;
    sb_sti(f);
    h->lpNext = 0;
    h->dwFlags = (h->dwFlags & ~MHDR_INQUEUE) | MHDR_DONE;
    notify(&mi.cb, MM_MIM_LONGDATA, (DWORD)h, t);
}

static void sysexbyte(BYTE b, DWORD t)
{
    LPMHDR h = mi.head;
    if (!h)
        return;                         /* no buffer: the byte is lost */
    if (h->dwBytesRecorded < h->dwBufferLength) {
        sb_hcopy((LPSTR)&b, h->lpData, h->dwBytesRecorded, 1, 1);
        h->dwBytesRecorded++;
    }
    if (b == 0xF7 || h->dwBytesRecorded >= h->dwBufferLength)
        longdone(t);
}

static void inbyte(BYTE b)
{
    DWORD t = TIMEGETTIME() - mi.t0;
    if (b >= 0xF8) {                    /* real time: at once, alone */
        notify(&mi.cb, MM_MIM_DATA, b, t);
        return;
    }
    if (mi.sysex) {
        if (b < 0x80 || b == 0xF7) {
            sysexbyte(b, t);
            if (b == 0xF7)
                mi.sysex = 0;
            return;
        }
        mi.sysex = 0;                   /* ended by another status */
        if (mi.head && mi.head->dwBytesRecorded)
            longdone(t);
    }
    if (b == 0xF0) {
        mi.sysex = 1;
        mi.stat = 0;
        sysexbyte(b, t);
        return;
    }
    if (b >= 0x80) {
        mi.stat = b;
        mi.need = datalen(b);
        mi.have = 0;
        if (!mi.need) {
            notify(&mi.cb, MM_MIM_DATA, b, t);
            mi.stat = 0;
        }
        return;
    }
    if (!mi.stat)
        return;                         /* data without a status */
    if (!mi.have) {
        mi.d1 = b;
        mi.have = 1;
        if (mi.need == 2)
            return;
        notify(&mi.cb, MM_MIM_DATA, mi.stat | ((DWORD)b << 8), t);
    } else
        notify(&mi.cb, MM_MIM_DATA, mi.stat | ((DWORD)mi.d1 << 8) | ((DWORD)b << 16), t);
    mi.have = 0;
    if (mi.stat >= 0xF0)
        mi.stat = 0;
}

void midi_input(void)
{
    BYTE b;
    while (mpuqh != mpuqt) {
        b = mpuq[mpuqh];
        mpuqh = (mpuqh + 1) & 63;
        if (mi.open && mi.started)
            inbyte(b);
    }
}

DWORD ENTRY MIDMESSAGE(WORD dev, WORD msg, DWORD user, DWORD p1, DWORD p2)
{
    LPMHDR h;
    WORD f;
    MOPENDESC FAR *d;
    (void)user;
    switch (msg) {
    case MIDM_GETNUMDEVS:
        return mpu_ok ? 1 : 0;
    case MIDM_GETDEVCAPS:
        if (dev)
            return MMSYSERR_BADDEVICEID;
        copycaps((LPVOID)p1, &icaps, sizeof icaps, p2);
        return 0;
    case MIDM_OPEN:
        if (dev)
            return MMSYSERR_BADDEVICEID;
        if (!mpu_ok)
            return MMSYSERR_NOTENABLED;
        if (mi.open)
            return MMSYSERR_ALLOCATED;
        d = (MOPENDESC FAR *)p1;
        mi.cb.h = d->hMidi;
        mi.cb.cb = d->dwCallback;
        mi.cb.inst = d->dwInstance;
        mi.cb.flags = (HIWORD(p2) & 7) | DCB_NOSWITCH;
        mi.head = mi.tail = 0;
        mi.started = mi.sysex = mi.stat = 0;
        mi.t0 = TIMEGETTIME();
        mi.open = 1;
        notify(&mi.cb, MM_MIM_OPEN, 0, 0);
        return 0;
    }
    if (!mi.open)
        return MMSYSERR_NOTSUPPORTED;
    switch (msg) {
    case MIDM_CLOSE:
        if (mi.head)
            return MIDIERR_STILLPLAYING;
        f = sb_cli();
        mi.open = mi.started = 0;
        sb_sti(f);
        notify(&mi.cb, MM_MIM_CLOSE, 0, 0);
        return 0;
    case MIDM_ADDBUFFER:
        h = (LPMHDR)p1;
        if (!(h->dwFlags & MHDR_PREPARED))
            return MIDIERR_UNPREPARED;
        h->lpNext = 0;
        h->dwBytesRecorded = 0;
        h->dwFlags = (h->dwFlags | MHDR_INQUEUE) & ~MHDR_DONE;
        f = sb_cli();
        if (mi.tail)
            mi.tail->lpNext = h;
        else
            mi.head = h;
        mi.tail = h;
        sb_sti(f);
        return 0;
    case MIDM_START:
        f = sb_cli();
        if (!mi.started) {
            mi.t0 = TIMEGETTIME();
            mi.stat = mi.sysex = mi.have = 0;
            mi.started = 1;
        }
        sb_sti(f);
        return 0;
    case MIDM_STOP:
        f = sb_cli();
        mi.started = 0;
        sb_sti(f);
        if (mi.sysex && mi.head && mi.head->dwBytesRecorded)
            longdone(TIMEGETTIME() - mi.t0);
        mi.sysex = 0;
        return 0;
    case MIDM_RESET:
        {
            LPMHDR list, next;
            f = sb_cli();
            mi.started = 0;
            mi.sysex = 0;
            list = mi.head;
            mi.head = mi.tail = 0;
            sb_sti(f);
            while (list) {
                next = list->lpNext;
                list->lpNext = 0;
                list->dwFlags = (list->dwFlags & ~MHDR_INQUEUE) | MHDR_DONE;
                notify(&mi.cb, MM_MIM_LONGDATA, (DWORD)list, TIMEGETTIME() - mi.t0);
                list = next;
            }
        }
        return 0;
    }
    return MMSYSERR_NOTSUPPORTED;
}
