/*
 * sn1.c - SN1: every function the sound driver (module SOUND, SYSTEM.INI
 * [boot] sound.drv=) exports, called from a program and logged
 * (drivers/sound/README.md). Run as the shell (tests/soundprobe.sh), once
 * with Windows 3.1's MMSOUND.DRV and once with w16's; the two TK.LOGs must
 * be identical except the lines and fields marked "~". The sound itself
 * (DOSBox-X's audio capture of the whole run) is compared by the script.
 *
 * Every call goes through kb_callx (kb1s.asm): marker values in every
 * register, all registers and the bytes taken off the stack reported. A
 * wrong guess of an argument list here cannot corrupt the probe's stack.
 * Only values are logged: the one pointer a function returns
 * (GetThresholdEvent) is compared as a pointer and its documented int read.
 *
 * Parts ([w16] sn1parts=ABCDEF in SYSTEM.INI picks them):
 *   A  which ordinals exist
 *   B  each entry's stack use and registers, before OpenSound and after
 *   C  the API's results over its inputs: OpenSound/CloseSound twice,
 *      queue sizes, notes, accents, envelopes, noise, frequencies,
 *      thresholds, counts, the states WaitSoundState takes
 *   D  playing: queued notes started, the count of notes left and the
 *      speaker's port state against the BIOS tick count, StopSound
 *   E  the beeps: DoBeep, MessageBeep with each type, separated by
 *      silences (the capture shows them)
 *   F  a melody at several tempos, then WEP
 */
#include <windows.h>

#define TK_REF "sn1ref.h"
#include "tklog.h"

extern int FAR PASCAL U_INITAPP(WORD hinst);
extern WORD FAR PASCAL K_GETEXEPTR(WORD h);
extern WORD __cdecl get_0040h(void);
extern void __cdecl __far kb_callx(FARPROC f, WORD FAR *args, WORD FAR *out);
extern WORD __cdecl __far spk_state(void);
extern WORD __cdecl __far pit2_count(void);
extern void __cdecl __far spk_on(WORD divisor);
extern void __cdecl __far spk_off(void);
extern WORD __cdecl __far pit0_count(void);
extern WORD __cdecl __far pit0_status(void);

static WORD args[16];
static WORD drvds;                      /* the driver's data selector */
static WORD rr[20];
static FARPROC p[32];
static char line[300];
static char lb[2048];
static WORD lbn;
static volatile DWORD FAR *ticks;
static char parts[16];

static void flush(void)
{
    if (lbn)
        log_write(lb, lbn);
    lbn = 0;
}

static void put(const char FAR *s)
{
    while (*s) {
        if (lbn == sizeof lb)
            flush();
        lb[lbn++] = *s++;
    }
}

static int part(char c)
{
    char *s;
    for (s = parts; *s; s++)
        if (*s == c)
            return 1;
    return 0;
}

/* call ordinal ord with up to 5 word arguments (Pascal order: a first);
   n the number given */
static int callw(int ord, int n, int a, int b, int c, int d, int e)
{
    int v[5], i;
    for (i = 0; i < 16; i++)
        args[i] = 0xC0DE;
    v[0] = a; v[1] = b; v[2] = c; v[3] = d; v[4] = e;
    for (i = 0; i < n; i++)
        args[16 - n + i] = v[i];
    kb_callx(p[ord], args, rr);
    return (int)rr[2];
}

/* the registers a call left, as one line: AX compared (but for DoBeep,
   whose AX is what its loop left: 0000, 0101, 0505 on 3.1), the others
   only where 3.1 keeps them all the time (SI, DI, DS, BP, the flags); BX,
   CX, DX, ES are logged "~" (3.1's changes them in some calls) */
static void regline(const char *what)
{
    const char *k;
    int junk = 0;
    for (k = what; *k; k++)
        if (k[0] == 'D' && k[1] == 'o' && k[2] == 'B')
            junk = 1;
    /* CloseSound with the device closed: AX is the driver's own data
       selector on 3.1 (logged as such by closeds()) */
    if (what[0] == 'C' && what[5] == 'S' && (what[11] == 'c' || what[11] == 'a'))
        junk = 1;
    wsprintf(line, "OBS regs %s: popped=%u %sAX=%04X ~DX=%04X ~BX=%s ~CX=%s ~DX=%s ~ES=%s SI=%u DI=%u DS=%u BP=%u IF=%u DF=%u\r\n",
             (LPSTR)what, rr[0], (LPSTR)(junk ? "~" : ""), rr[2], rr[5],
             (LPSTR)(rr[3] == 0x2222 ? "kept" : "changed"), (LPSTR)(rr[4] == 0x3333 ? "kept" : "changed"),
             (LPSTR)(rr[5] == 0x4444 ? "kept" : "changed"),
             (LPSTR)(rr[9] == SELECTOROF((void FAR *)rr) ? "kept" : "changed"),
             rr[6] == 0x5555, rr[7] == 0x6666,
             rr[8] == SELECTOROF((void FAR *)rr),
             rr[10] == OFFSETOF((void FAR *)rr), (rr[1] >> 9) & 1, (rr[1] >> 10) & 1);
    put(line);
}

static void res(const char *what, int v)
{
    wsprintf(line, "OBS %s = %d\r\n", (LPSTR)what, v);
    put(line);
}

/* the speaker as the ports show it: 61h's two low bits, channel 2's mode
   and access mode (the OUT pin and the count are timing: "~") */
static void spk(const char *what)
{
    WORD s = spk_state();
    wsprintf(line, "OBS spk %s: 61h&3=%u mode=%u rw=%u ~out=%u ~61h=%02X ~count=%04X\r\n", (LPSTR)what,
             s & 3, (s >> 9) & 7, (s >> 12) & 3, (s >> 15) & 1, s & 0xFF, pit2_count());
    put(line);
}

static int waitticks(WORD n);
static void tickedge(void)
{
    DWORD t = *ticks;
    while (*ticks == t)
        ;
}

static int waitticks(WORD n)
{
    DWORD t0 = *ticks, spin = 0;
    while (*ticks - t0 < n)
        if (++spin > 60000000L)
            return 0;
    return 1;
}

/* ---- markers on the capture's time line: 4 ticks of silence, then n
   tones of 2983 Hz (divisor 400), 2 ticks each with 2 ticks between ---- */
static void marker(int n)
{
    int i;
    waitticks(4);
    for (i = 0; i < n; i++) {
        spk_on(400);
        waitticks(2);
        spk_off();
        waitticks(2);
    }
    waitticks(2);
}

/* ---- the helper: SNH.EXE (SN1.EXE under another module name, made
   by tests/soundprobe.sh), started by part Q with "H" on its command
   line, makes the calls that may not return. Each line goes to
   C:\SN1H.LOG at once (opened, appended, closed), so part Q can read how
   far it got while it waits in the driver. ---- */
static int strchr_(const char *s, char c)
{
    for (; *s; s++)
        if (*s == c)
            return 1;
    return 0;
}

static void hput(const char *s)
{
    HFILE h = _lopen("C:\\SN1H.LOG", OF_WRITE);
    if (h == HFILE_ERROR)
        h = _lcreat("C:\\SN1H.LOG", 0);
    if (h == HFILE_ERROR)
        return;
    _llseek(h, 0, 2);
    _lwrite(h, s, lstrlen(s));
    _lclose(h);
}

/* [w16] sn1hq: the queue size; sn1hlen: the notes' length (4: quarter
   notes); sn1hstart: StartSound first */
static int helper(void)
{
    int r, n, q, len, st;
    DWORD t0, t1;
    q = GetPrivateProfileInt("w16", "sn1hq", 192, "SYSTEM.INI");
    len = GetPrivateProfileInt("w16", "sn1hlen", 4, "SYSTEM.INI");
    st = GetPrivateProfileInt("w16", "sn1hstart", 0, "SYSTEM.INI");
    r = callw(1, 0, 0, 0, 0, 0, 0);
    wsprintf(line, "H OpenSound = %d\r\n", r);
    hput(line);
    r = callw(3, 2, 1, q, 0, 0, 0);
    wsprintf(line, "H SetVoiceQueueSize(1,%d) = %d\r\n", q, r);
    hput(line);
    if (GetPrivateProfileInt("w16", "sn1hacc", 1, "SYSTEM.INI")) {
        r = callw(5, 5, 1, 120, 128, 0, 0);
        wsprintf(line, "H SetVoiceAccent(1,120,128,0,0) = %d\r\n", r);
        hput(line);
    }
    if (st) {
        r = callw(9, 0, 0, 0, 0, 0, 0);
        wsprintf(line, "H StartSound = %d\r\n", r);
        hput(line);
    }
    t1 = *ticks;
    for (n = 0; n < 300; n++) {
        wsprintf(line, "H note %d\r\n", n);
        if (n % 10 == 0)
            hput(line);
        t0 = *ticks;
        r = callw(4, 4, 1, 37, len, 0, 0);
        if (r || *ticks - t0 > 1) {
            wsprintf(line, "H note %d = %d after %lu ticks, count %d\r\n", n, r,
                     *ticks - t0, callw(13, 1, 1, 0, 0, 0, 0));
            hput(line);
            if (r)
                break;
        }
    }
    wsprintf(line, "H %d notes queued, ~%lu ticks, count %d\r\n", n, *ticks - t1, callw(13, 1, 1, 0, 0, 0, 0));
    hput(line);
    r = callw(2, 0, 0, 0, 0, 0, 0);
    wsprintf(line, "H CloseSound = %d\r\nH done\r\n", r);
    hput(line);
    return 0;
}

/* ---- A: the ordinals ---- */
static void partA(HMODULE hs)
{
    int i;
    put("OBS ordinals:");
    for (i = 1; i < 32; i++)
        if (p[i]) {
            wsprintf(line, " %d", i);
            put(line);
        }
    put("\r\n");
    (void)hs;
    flush();
}

/* ---- B: stack use and registers ---- */
static void partB(void)
{
    int i;
    /* before OpenSound: every entry with harmless arguments */
    callw(15, 0, 0, 0, 0, 0, 0); regline("GetThresholdStatus closed");
    callw(13, 1, 1, 0, 0, 0, 0); regline("CountVoiceNotes(1) closed");
    callw(3, 2, 1, 192, 0, 0, 0); regline("SetVoiceQueueSize(1,192) closed");
    callw(4, 4, 1, 37, 4, 0, 0); regline("SetVoiceNote(1,37,4,0) closed");
    callw(5, 5, 1, 120, 128, 0, 0); regline("SetVoiceAccent(1,120,128,0,0) closed");
    callw(6, 3, 1, 0, 0, 0, 0); regline("SetVoiceEnvelope(1,0,0) closed");
    callw(7, 2, 0, 0, 0, 0, 0); regline("SetSoundNoise(0,0) closed");
    callw(8, 4, 1, 440, 0, 10, 0); regline("SetVoiceSound(1,440<<16,10) closed");
    callw(9, 0, 0, 0, 0, 0, 0); regline("StartSound closed");
    callw(10, 0, 0, 0, 0, 0, 0); regline("StopSound closed");
    callw(11, 1, 0, 0, 0, 0, 0); regline("WaitSoundState(0) closed");
    callw(12, 0, 0, 0, 0, 0, 0); regline("SyncAllVoices closed");
    callw(14, 0, 0, 0, 0, 0, 0); regline("GetThresholdEvent closed");
    callw(16, 2, 1, 0, 0, 0, 0); regline("SetVoiceThreshold(1,0) closed");
    callw(2, 0, 0, 0, 0, 0, 0); regline("CloseSound closed");
    spk("closed");
    flush();
    /* open */
    callw(1, 0, 0, 0, 0, 0, 0); regline("OpenSound");
    callw(1, 0, 0, 0, 0, 0, 0); regline("OpenSound again");
    callw(15, 0, 0, 0, 0, 0, 0); regline("GetThresholdStatus");
    callw(13, 1, 1, 0, 0, 0, 0); regline("CountVoiceNotes(1)");
    callw(3, 2, 1, 192, 0, 0, 0); regline("SetVoiceQueueSize(1,192)");
    callw(4, 4, 1, 37, 4, 0, 0); regline("SetVoiceNote(1,37,4,0)");
    callw(5, 5, 1, 120, 128, 0, 0); regline("SetVoiceAccent(1,120,128,0,0)");
    callw(6, 3, 1, 0, 0, 0, 0); regline("SetVoiceEnvelope(1,0,0)");
    callw(7, 2, 0, 0, 0, 0, 0); regline("SetSoundNoise(0,0)");
    callw(8, 4, 1, 440, 0, 10, 0); regline("SetVoiceSound(1,440<<16,10)");
    callw(13, 1, 1, 0, 0, 0, 0); regline("CountVoiceNotes(1) 2 queued");
    callw(12, 0, 0, 0, 0, 0, 0); regline("SyncAllVoices");
    callw(14, 0, 0, 0, 0, 0, 0); regline("GetThresholdEvent");
    drvds = rr[5];
    callw(16, 2, 1, 0, 0, 0, 0); regline("SetVoiceThreshold(1,0)");
    callw(10, 0, 0, 0, 0, 0, 0); regline("StopSound");
    callw(11, 1, 0, 0, 0, 0, 0); regline("WaitSoundState(0) stopped");
    callw(13, 1, 1, 0, 0, 0, 0); regline("CountVoiceNotes(1) after StopSound");
    spk("open");
    callw(2, 0, 0, 0, 0, 0, 0); regline("CloseSound");
    callw(2, 0, 0, 0, 0, 0, 0); regline("CloseSound again");
    wsprintf(line, "OBS ~CloseSound again: AX the driver's data selector %u (3.1's: no; its AX is not the selector GetThresholdEvent gives)\r\n", rr[2] == drvds);
    put(line);
    for (i = 0; i < 3; i++) {
        waitticks(6);
        callw(17, 0, 0, 0, 0, 0, 0); regline("DoBeep");
        spk("after DoBeep");
        flush();
    }
    waitticks(10);
    flush();
}

/* ---- C: results over the inputs ---- */
static void partC(void)
{
    static const int qs[] = { 0, 1, 2, 3, 4, 5, 6, 7, 8, 16, 31, 32, 33, 64, 100, 192, 193,
                              256, 1024, 4096, 16384, 32767, -1, -192 };
    static const int vs[] = { -1, 0, 1, 2, 3, 4, 5, 255, 256 };
    int i, j, r;
    char w[80];

    r = callw(1, 0, 0, 0, 0, 0, 0); res("C OpenSound", r);
    r = callw(1, 0, 0, 0, 0, 0, 0); res("C OpenSound again", r);
    r = callw(2, 0, 0, 0, 0, 0, 0); res("C CloseSound", r);
    r = callw(1, 0, 0, 0, 0, 0, 0); res("C OpenSound after Close", r);
    /* voices */
    for (i = 0; i < sizeof vs / sizeof vs[0]; i++) {
        wsprintf(w, "C CountVoiceNotes(%d)", vs[i]);
        r = callw(13, 1, vs[i], 0, 0, 0, 0); res(w, r);
        /* only voice 1 is written to: with others Windows 3.1's driver
           went on returning 0, and the probe later hung (its memory
           overwritten, UNVERIFIED) */
        if (vs[i] != 1)
            continue;
        wsprintf(w, "C SetVoiceQueueSize(%d,192)", vs[i]);
        r = callw(3, 2, vs[i], 192, 0, 0, 0); res(w, r);
        wsprintf(w, "C SetVoiceNote(%d,37,4,0)", vs[i]);
        r = callw(4, 4, vs[i], 37, 4, 0, 0); res(w, r);
        wsprintf(w, "C SetVoiceAccent(%d,120,128,0,0)", vs[i]);
        r = callw(5, 5, vs[i], 120, 128, 0, 0); res(w, r);
        wsprintf(w, "C SetVoiceSound(%d,440,10)", vs[i]);
        r = callw(8, 4, vs[i], 440, 0, 10, 0); res(w, r);
        wsprintf(w, "C SetVoiceEnvelope(%d,0,0)", vs[i]);
        r = callw(6, 3, vs[i], 0, 0, 0, 0); res(w, r);
        wsprintf(w, "C SetVoiceThreshold(%d,1)", vs[i]);
        r = callw(16, 2, vs[i], 1, 0, 0, 0); res(w, r);
        wsprintf(w, "C CountVoiceNotes(%d) after", vs[i]);
        r = callw(13, 1, vs[i], 0, 0, 0, 0); res(w, r);
    }
    flush();
    callw(10, 0, 0, 0, 0, 0, 0);
    /* queue sizes (how many notes then fit: part Q) */
    for (i = 0; i < sizeof qs / sizeof qs[0]; i++) {
        callw(10, 0, 0, 0, 0, 0, 0);
        wsprintf(w, "C SetVoiceQueueSize(1,%d)", qs[i]);
        r = callw(3, 2, 1, qs[i], 0, 0, 0); res(w, r);
    }
    flush();
    callw(10, 0, 0, 0, 0, 0, 0);
    r = callw(3, 2, 1, 4096, 0, 0, 0); res("C SetVoiceQueueSize(1,4096) for the rest", r);
    /* notes: value 0..90, lengths, dots */
    for (i = -2; i <= 90; i++) {
        callw(10, 0, 0, 0, 0, 0, 0);
        callw(3, 2, 1, 4096, 0, 0, 0);
        r = callw(4, 4, 1, i, 4, 0, 0);
        wsprintf(line, "OBS C SetVoiceNote(1,%d,4,0) = %d count %d\r\n", i, r, callw(13, 1, 1, 0, 0, 0, 0));
        put(line);
    }
    flush();
    for (i = -1; i <= 260; i++) {
        callw(10, 0, 0, 0, 0, 0, 0);
        r = callw(4, 4, 1, 37, i, 0, 0);
        if (i > 70 && i < 250 && i != 128 && i != 255 && i != 256)
            continue;
        wsprintf(line, "OBS C SetVoiceNote(1,37,%d,0) = %d count %d\r\n", i, r, callw(13, 1, 1, 0, 0, 0, 0));
        put(line);
    }
    flush();
    for (i = -1; i <= 9; i++) {
        callw(10, 0, 0, 0, 0, 0, 0);
        r = callw(4, 4, 1, 37, 4, i, 0);
        wsprintf(line, "OBS C SetVoiceNote(1,37,4,%d) = %d count %d\r\n", i, r, callw(13, 1, 1, 0, 0, 0, 0));
        put(line);
        r = callw(4, 4, 1, 37, 1, i, 0);
        wsprintf(line, "OBS C SetVoiceNote(1,37,1,%d) = %d\r\n", i, r);
        put(line);
        r = callw(4, 4, 1, 37, 64, i, 0);
        wsprintf(line, "OBS C SetVoiceNote(1,37,64,%d) = %d\r\n", i, r);
        put(line);
    }
    flush();
    /* accents */
    {
        static const int tempo[] = { -1, 0, 1, 31, 32, 33, 120, 255, 256, 1000 };
        static const int vol[] = { -1, 0, 1, 128, 255, 256 };
        static const int mode[] = { -1, 0, 1, 2, 3, 4 };
        static const int pitch[] = { -1, 0, 1, 83, 84, 85, 255 };
        for (i = 0; i < sizeof tempo / sizeof tempo[0]; i++) {
            callw(10, 0, 0, 0, 0, 0, 0);
            r = callw(5, 5, 1, tempo[i], 128, 0, 0);
            wsprintf(line, "OBS C SetVoiceAccent(1,%d,128,0,0) = %d count %d\r\n", tempo[i], r, callw(13, 1, 1, 0, 0, 0, 0));
            put(line);
        }
        for (i = 0; i < sizeof vol / sizeof vol[0]; i++) {
            r = callw(5, 5, 1, 120, vol[i], 0, 0);
            wsprintf(line, "OBS C SetVoiceAccent(1,120,%d,0,0) = %d\r\n", vol[i], r);
            put(line);
        }
        for (i = 0; i < sizeof mode / sizeof mode[0]; i++) {
            r = callw(5, 5, 1, 120, 128, mode[i], 0);
            wsprintf(line, "OBS C SetVoiceAccent(1,120,128,%d,0) = %d\r\n", mode[i], r);
            put(line);
        }
        for (i = 0; i < sizeof pitch / sizeof pitch[0]; i++) {
            r = callw(5, 5, 1, 120, 128, 0, pitch[i]);
            wsprintf(line, "OBS C SetVoiceAccent(1,120,128,0,%d) = %d\r\n", pitch[i], r);
            put(line);
        }
    }
    flush();
    /* envelopes and noise: anything */
    for (i = -1; i <= 16; i += 1) {
        r = callw(6, 3, 1, i, i, 0, 0);
        wsprintf(line, "OBS C SetVoiceEnvelope(1,%d,%d) = %d\r\n", i, i, r);
        put(line);
    }
    for (i = -1; i <= 8; i++)
        for (j = 0; j < 3; j++) {
            r = callw(7, 2, i, j * 100, 0, 0, 0);
            wsprintf(line, "OBS C SetSoundNoise(%d,%d) = %d\r\n", i, j * 100, r);
            put(line);
        }
    flush();
    /* frequencies and durations */
    {
        static const int fr[] = { 0, 1, 18, 19, 20, 21, 30, 35, 36, 37, 100, 440, 1000, 5000, 10000,
                                  20000, 32767, (int)32768U, (int)40000U, -1 };
        static const int du[] = { -1, 0, 1, 2, 18, 100, 255, 256, 1000, 32767 };
        callw(10, 0, 0, 0, 0, 0, 0);
        for (i = 0; i < sizeof fr / sizeof fr[0]; i++) {
            callw(10, 0, 0, 0, 0, 0, 0);
            r = callw(8, 4, 1, fr[i], 0, 10, 0);
            wsprintf(line, "OBS C SetVoiceSound(1,%d<<16,10) = %d count %d\r\n", fr[i], r, callw(13, 1, 1, 0, 0, 0, 0));
            put(line);
            r = callw(8, 4, 1, fr[i], 0x8000, 10, 0);
            wsprintf(line, "OBS C SetVoiceSound(1,%d.5,10) = %d\r\n", fr[i], r);
            put(line);
        }
        for (i = 0; i < sizeof du / sizeof du[0]; i++) {
            callw(10, 0, 0, 0, 0, 0, 0);
            r = callw(8, 4, 1, 440, 0, du[i], 0);
            wsprintf(line, "OBS C SetVoiceSound(1,440,%d) = %d count %d\r\n", du[i], r, callw(13, 1, 1, 0, 0, 0, 0));
            put(line);
        }
    }
    flush();
    /* threshold and states */
    callw(10, 0, 0, 0, 0, 0, 0);
    for (i = -1; i <= 3; i++) {
        r = callw(16, 2, 1, i, 0, 0, 0);
        wsprintf(line, "OBS C SetVoiceThreshold(1,%d) = %d status %d\r\n", i, r, callw(15, 0, 0, 0, 0, 0, 0));
        put(line);
    }
    {
        DWORD ev;
        int FAR *pe;
        callw(14, 0, 0, 0, 0, 0, 0);
        ev = MAKELONG(rr[2], rr[5]);
        pe = (int FAR *)ev;
        wsprintf(line, "OBS C GetThresholdEvent: null %u ~%04X:%04X\r\n", ev == 0, HIWORD(ev), LOWORD(ev));
        put(line);
        if (ev) {
            wsprintf(line, "OBS C *GetThresholdEvent() = %d\r\n", *pe);
            put(line);
        }
    }
    flush();
    /* WaitSoundState with nothing playing: 0 and values outside the
       documented ones (S_THRESHOLD and S_ALLTHRESHOLD would wait for a
       threshold that never comes: never returned on Windows 3.1's
       driver, part C's first runs) */
    {
        static const int ws[] = { 0, 3, 4, -1 };
        for (i = 0; i < 4; i++) {
            callw(10, 0, 0, 0, 0, 0, 0);
            r = callw(11, 1, ws[i], 0, 0, 0, 0);
            wsprintf(line, "OBS C WaitSoundState(%d) = %d\r\n", ws[i], r);
            put(line);
            flush();
        }
    }
    r = callw(12, 0, 0, 0, 0, 0, 0); res("C SyncAllVoices", r);
    r = callw(9, 0, 0, 0, 0, 0, 0); res("C StartSound (empty)", r);
    r = callw(10, 0, 0, 0, 0, 0, 0); res("C StopSound", r);
    r = callw(2, 0, 0, 0, 0, 0, 0); res("C CloseSound", r);
    r = callw(2, 0, 0, 0, 0, 0, 0);
    wsprintf(line, "OBS ~C CloseSound again: the driver's data selector %u\r\n", (WORD)r == drvds);
    put(line);
    flush();
}

/* ---- D: playing ---- */
/* wait until CountVoiceNotes(1) is n or less (at most 400 BIOS ticks) */
static void countdown(int n)
{
    DWORD t0 = *ticks;
    while (callw(13, 1, 1, 0, 0, 0, 0) > n && *ticks - t0 < 400)
        ;
}

static void watch(const char *what, int maxticks)
{
    DWORD t0 = *ticks, last = 0xFFFFFFFFL;
    int lastn = -99, n;
    WORD lasts = 0xFFFF, s;
    while (*ticks - t0 < (DWORD)maxticks) {
        n = callw(13, 1, 1, 0, 0, 0, 0);
        s = spk_state() & 0x0E03;
        if (n != lastn || s != lasts) {
            /* (the speaker off with a count left: the moment between the
               last note's end and the count's going to 0, seen now and
               then with both drivers: not compared) */
            wsprintf(line, "OBS %sD %s: tick ~%lu count %d 61h&3=%u mode=%u\r\n",
                     (LPSTR)((s & 3) == 0 && n > 0 ? "~" : ""), (LPSTR)what, *ticks - t0, n,
                     s & 3, (s >> 9) & 7);
            put(line);
            lastn = n, lasts = s;
        }
        if (*ticks != last)
            last = *ticks;
    }
    flush();
}

static void partD(void)
{
    int r, i;
    DWORD t0;
    marker(3);
    r = callw(1, 0, 0, 0, 0, 0, 0); res("D OpenSound", r);
    r = callw(3, 2, 1, 1024, 0, 0, 0); res("D SetVoiceQueueSize(1,1024)", r);
    /* tempo 120, quarter notes: 500 ms each */
    r = callw(5, 5, 1, 120, 128, 0, 0); res("D accent 120 normal", r);
    for (i = 0; i < 4; i++) {
        r = callw(4, 4, 1, 37 + 4 * i, 4, 0, 0);
        wsprintf(line, "OBS D note %d = %d\r\n", 37 + 4 * i, r);
        put(line);
    }
    spk("D queued");
    r = callw(13, 1, 1, 0, 0, 0, 0); res("D count before start", r);
    t0 = *ticks;
    r = callw(9, 0, 0, 0, 0, 0, 0);
    wsprintf(line, "OBS D StartSound = %d, ~%lu ticks in it\r\n", r, *ticks - t0);
    put(line);
    watch("playing", 60);
    t0 = *ticks;
    r = callw(11, 1, 0, 0, 0, 0, 0);
    wsprintf(line, "OBS D WaitSoundState(0) = %d ~%lu ticks\r\n", r, *ticks - t0);
    put(line);
    /* StopSound in the middle (a marker first: the time between these
       groups is BIOS ticks, whose phase is the run's) */
    marker(1);
    for (i = 0; i < 4; i++)
        callw(4, 4, 1, 49, 4, 0, 0);
    callw(9, 0, 0, 0, 0, 0, 0);
    /* (until the second note has started: BIOS ticks while notes play
       come at the driver's pace, and their phase is the run's) */
    countdown(2);
    r = callw(13, 1, 1, 0, 0, 0, 0); res("D count once the second of 4 notes plays", r);
    r = callw(10, 0, 0, 0, 0, 0, 0); res("D StopSound", r);
    spk("D stopped");
    r = callw(13, 1, 1, 0, 0, 0, 0); res("D count after StopSound", r);
    watch("after stop", 10);
    /* WaitSoundState(0) while playing */
    marker(2);
    for (i = 0; i < 3; i++)
        callw(4, 4, 1, 45, 8, 0, 0);
    callw(9, 0, 0, 0, 0, 0, 0);
    t0 = *ticks;
    r = callw(11, 1, 0, 0, 0, 0, 0);
    wsprintf(line, "OBS D WaitSoundState(0) playing = %d ~%lu ticks; count %d\r\n", r, *ticks - t0, callw(13, 1, 1, 0, 0, 0, 0));
    put(line);
    watch("after wait", 30);
    /* a frequency */
    marker(3);
    callw(8, 4, 1, 1000, 0, 18, 0);
    callw(8, 4, 1, 0, 0, 9, 0);
    callw(8, 4, 1, 500, 0, 18, 0);
    callw(9, 0, 0, 0, 0, 0, 0);
    watch("SetVoiceSound", 50);
    r = callw(2, 0, 0, 0, 0, 0, 0); res("D CloseSound", r);
    spk("D closed");
    flush();
}

/* ---- E: the beeps ---- */
static void partE(void)
{
    static const WORD types[] = { 0, MB_ICONHAND, MB_ICONQUESTION, MB_ICONEXCLAMATION, MB_ICONASTERISK, 0xFFFF };
    int i;
    BOOL b = 2;
    marker(4);
    SystemParametersInfo(SPI_GETBEEP, 0, &b, 0);
    wsprintf(line, "OBS E SPI_GETBEEP %d\r\n", b);
    put(line);
    waitticks(10);
    for (i = 0; i < 3; i++) {
        callw(17, 0, 0, 0, 0, 0, 0);
        regline("E DoBeep");
        waitticks(8);
    }
    for (i = 0; i < sizeof types / sizeof types[0]; i++) {
        MessageBeep(types[i]);
        wsprintf(line, "OBS E MessageBeep(%04X)\r\n", types[i]);
        put(line);
        spk("E after MessageBeep");
        waitticks(8);
    }
    /* DoBeep while the device is open */
    callw(1, 0, 0, 0, 0, 0, 0);
    callw(17, 0, 0, 0, 0, 0, 0);
    regline("E DoBeep while open");
    waitticks(8);
    MessageBeep(0);
    put("OBS E MessageBeep(0) while open\r\n");
    waitticks(8);
    callw(2, 0, 0, 0, 0, 0, 0);
    flush();
}

/* ---- F: a melody at tempos, then WEP ---- */
static void partF(void)
{
    static const int tempo[] = { 255, 120, 32 };
    int i, j, r;
    callw(1, 0, 0, 0, 0, 0, 0);
    callw(3, 2, 1, 1024, 0, 0, 0);
    for (j = 0; j < 3; j++) {
        marker(j + 1);
        callw(5, 5, 1, tempo[j], 128, j % 3, 0);
        for (i = 0; i < 8; i++)
            callw(4, 4, 1, 25 + 2 * i, 8, i & 1, 0);
        callw(4, 4, 1, 0, 8, 0, 0);     /* a rest */
        callw(9, 0, 0, 0, 0, 0, 0);
        r = callw(11, 1, 0, 0, 0, 0, 0);
        wsprintf(line, "OBS F tempo %d: WaitSoundState(0) = %d\r\n", tempo[j], r);
        put(line);
        watch("F", 200);
    }
    callw(2, 0, 0, 0, 0, 0, 0);
    callw(18, 1, 0, 0, 0, 0, 0);
    regline("F WEP(0)");
    flush();
}

/* ---- G: what times the notes: channel 0's largest count (its reload
   value) over 3 ticks, idle and while notes play ---- */
static WORD pit0max(WORD n)
{
    DWORD t0 = *ticks;
    WORD m = 0, c;
    while (*ticks - t0 < n) {
        c = pit0_count();
        if (c > m)
            m = c;
    }
    return m;
}

static void partG(void)
{
    WORD m;
    int i;
    marker(3);
    m = pit0max(3);
    wsprintf(line, "OBS G channel 0 reload idle: %s ~%04X\r\n", (LPSTR)(m > 0xF000 ? "65536" : "less"), m);
    put(line);
    callw(1, 0, 0, 0, 0, 0, 0);
    callw(5, 5, 1, 120, 128, 0, 0);
    for (i = 0; i < 4; i++)
        callw(4, 4, 1, 37, 4, 0, 0);
    m = pit0max(3);
    wsprintf(line, "OBS G channel 0 reload queued, not started: %s ~%04X\r\n", (LPSTR)(m > 0xF000 ? "65536" : "less"), m);
    put(line);
    callw(9, 0, 0, 0, 0, 0, 0);
    m = pit0max(3);
    wsprintf(line, "OBS G channel 0 reload playing: %s ~%04X, status %02X\r\n", (LPSTR)(m > 0xF000 ? "65536" : "less"), m,
             pit0_status() & 0x3F);
    put(line);
    callw(11, 1, 0, 0, 0, 0, 0);
    m = pit0max(3);
    wsprintf(line, "OBS G channel 0 reload after: %s ~%04X\r\n", (LPSTR)(m > 0xF000 ? "65536" : "less"), m);
    put(line);
    callw(2, 0, 0, 0, 0, 0, 0);
    m = pit0max(3);
    wsprintf(line, "OBS G channel 0 reload closed: %s ~%04X\r\n", (LPSTR)(m > 0xF000 ? "65536" : "less"), m);
    put(line);
    /* the BIOS's ticks while notes play: two whole notes legato at tempo
       32 (1500 driver ticks each, 7.5 s); the BIOS ticks counted from the
       first's start (the count going to 1) to the end (0): 15 s */
    marker(1);
    callw(1, 0, 0, 0, 0, 0, 0);
    callw(5, 5, 1, 32, 128, 1, 0);
    callw(4, 4, 1, 37, 1, 0, 0);
    callw(4, 4, 1, 49, 1, 0, 0);
    {
        DWORD ts = *ticks;
        callw(9, 0, 0, 0, 0, 0, 0);
        countdown(1);
        wsprintf(line, "OBS ~G BIOS ticks from StartSound to the first note: %lu\r\n", *ticks - ts);
        put(line);
    }
    {
        DWORD t0 = *ticks;
        WORD bt;
        countdown(0);
        bt = (WORD)(*ticks - t0);
        wsprintf(line, "OBS G BIOS ticks during 15 s of notes: %u0 or so (~%u)\r\n", (bt + 5) / 10, bt);
        put(line);
    }
    callw(2, 0, 0, 0, 0, 0, 0);
    flush();
}

/* ---- H: the sound, for the capture: groups of notes, each group
   played to its end (WaitSoundState(S_QUEUEEMPTY)) and followed by a
   marker of its number (tests/soundprobe.sh lines the two recordings up
   by the markers) ---- */
static void play(int grp)
{
    int r;
    r = callw(9, 0, 0, 0, 0, 0, 0);
    wsprintf(line, "OBS H%d StartSound = %d count %d\r\n", grp, r, callw(13, 1, 1, 0, 0, 0, 0));
    put(line);
    r = callw(11, 1, 0, 0, 0, 0, 0);
    wsprintf(line, "OBS H%d WaitSoundState(0) = %d count %d\r\n", grp, r, callw(13, 1, 1, 0, 0, 0, 0));
    put(line);
    flush();
    waitticks(3);
    marker(grp % 4 + 1);
}

static void note(int grp, int v, int len, int dots)
{
    int r = callw(4, 4, 1, v, len, dots, 0);
    if (r) {
        wsprintf(line, "OBS H%d SetVoiceNote(1,%d,%d,%d) = %d\r\n", grp, v, len, dots, r);
        put(line);
    }
}

static void accent(int grp, int tempo, int vol, int mode, int pitch)
{
    int r = callw(5, 5, 1, tempo, vol, mode, pitch);
    if (r) {
        wsprintf(line, "OBS H%d SetVoiceAccent(1,%d,%d,%d,%d) = %d\r\n", grp, tempo, vol, mode, pitch, r);
        put(line);
    }
}

static void partH(void)
{
    static const int lens[] = { 1, 2, 4, 8, 16, 32, 64 };
    static const int tempos[] = { 32, 33, 60, 90, 100, 150, 200, 254, 255 };
    int i, r;
    r = callw(1, 0, 0, 0, 0, 0, 0);
    wsprintf(line, "OBS H OpenSound = %d\r\n", r);
    put(line);
    r = callw(3, 2, 1, 4096, 0, 0, 0);
    wsprintf(line, "OBS H SetVoiceQueueSize(1,4096) = %d\r\n", r);
    put(line);
    marker(1);
    /* 1: every note, sixteenths, staccato, tempo 120 */
    accent(1, 120, 128, 2, 0);
    for (i = 1; i <= 84; i++)
        note(1, i, 16, 0);
    play(1);
    /* 2: the lengths, normal, tempo 120, a rest after each */
    accent(2, 120, 128, 0, 0);
    for (i = 0; i < sizeof lens / sizeof lens[0]; i++) {
        note(2, 37, lens[i], 0);
        note(2, 0, 8, 0);
    }
    play(2);
    /* 3: dots */
    for (i = 0; i <= 3; i++) {
        note(3, 37, 4, i);
        note(3, 0, 8, 0);
    }
    note(3, 37, 16, 1);
    note(3, 0, 8, 0);
    play(3);
    /* 4: the modes: two notes each, a rest after */
    for (i = 0; i <= 2; i++) {
        accent(4, 120, 128, i, 0);
        note(4, 37, 4, 0);
        note(4, 41, 4, 0);
        note(4, 0, 4, 0);
    }
    play(4);
    /* 5: tempos */
    for (i = 0; i < sizeof tempos / sizeof tempos[0]; i++) {
        accent(5, tempos[i], 128, 0, 0);
        note(5, 49, 4, 0);
        note(5, 0, 4, 0);
    }
    play(5);
    /* 6: pitch (transposition) and volume */
    accent(6, 120, 128, 2, 12);
    note(6, 25, 8, 0);
    accent(6, 120, 128, 2, 1);
    note(6, 25, 8, 0);
    accent(6, 120, 128, 2, 83);
    note(6, 1, 8, 0);
    note(6, 2, 8, 0);
    accent(6, 120, 0, 2, 0);
    note(6, 37, 8, 0);
    accent(6, 120, 255, 2, 0);
    note(6, 37, 8, 0);
    accent(6, 120, 1, 2, 0);
    note(6, 37, 8, 0);
    play(6);
    /* 7: SetVoiceSound: frequencies (Hz in the high word, a fraction
       in the low) and durations */
    {
        static const WORD fq[][2] = { { 100, 0 }, { 440, 0 }, { 440, 0x8000 }, { 1000, 0 },
                                     { 3000, 0 }, { 18, 0 }, { 0, 0 } };
        static const int du[] = { 1, 2, 3, 5, 10, 18 };
        for (i = 0; i < sizeof fq / sizeof fq[0]; i++) {
            r = callw(8, 4, 1, fq[i][0], fq[i][1], 20, 0);
            wsprintf(line, "OBS H7 SetVoiceSound(1,%u.%04X,20) = %d\r\n", fq[i][0], fq[i][1], r);
            put(line);
            note(7, 0, 16, 0);      /* (a rest long enough to part the tones on the recording) */
        }
        for (i = 0; i < sizeof du / sizeof du[0]; i++) {
            r = callw(8, 4, 1, 1000, 0, du[i], 0);
            wsprintf(line, "OBS H7 SetVoiceSound(1,1000,%d) = %d\r\n", du[i], r);
            put(line);
            note(7, 0, 16, 0);
        }
    }
    play(7);
    /* 8: notes and SetVoiceSound mixed, an accent between */
    accent(8, 200, 128, 0, 0);
    note(8, 30, 8, 0);
    callw(8, 4, 1, 700, 0, 4, 0);
    note(8, 32, 8, 0);
    accent(8, 100, 128, 1, 0);
    note(8, 34, 8, 0);
    note(8, 35, 8, 0);
    play(8);
    r = callw(2, 0, 0, 0, 0, 0, 0);
    wsprintf(line, "OBS H CloseSound = %d\r\n", r);
    put(line);
    flush();
}

/* ---- P: the ports while notes play: 61h's low bits and channel 2's
   mode and count, sampled as fast as the probe can for a while; each
   change of (61h & 3, mode, the count's range) is logged with the
   sample's time in channel 0 counts (~: the host's timing) ---- */
static void partP(void)
{
    static WORD buf[3000];
    int n = 0, i;
    DWORD t0;
    WORD last = 0xFFFF;
    marker(1);
    callw(1, 0, 0, 0, 0, 0, 0);
    callw(5, 5, 1, 120, 128, 0, 0);
    note(10, 37, 8, 0);
    note(10, 0, 8, 0);
    callw(5, 5, 1, 120, 128, 2, 0);
    note(10, 41, 8, 0);
    callw(8, 4, 1, 1000, 0, 6, 0);
    callw(9, 0, 0, 0, 0, 0, 0);
    t0 = *ticks;
    /* windows of 256 samples: the largest count seen in a window is
       about channel 2's divisor (mode 3 counts down from it by 2) */
    /* until the speaker is off and nothing is left (BIOS ticks are not
       the measure: their phase against the driver's is the run's) */
    while (*ticks - t0 < 40 && n < 3000 - 3) {
        WORD s = 0, c, k, wmax = 0, j;
        if ((spk_state() & 3) == 0 && callw(13, 1, 1, 0, 0, 0, 0) == 0)
            break;
        for (j = 0; j < 256; j++) {
            s = spk_state();
            c = pit2_count();
            if (c > wmax)
                wmax = c;
        }
        /* quiet (an ultrasonic divisor: below 64) or a tone; the divisor
           itself is logged "~" (the window can straddle a change) */
        (void)j;
        k = (s & 3) | ((s >> 9) & 7) << 2 | (wmax < 64 ? 0 : 1) << 5;
        if (k != last) {
            buf[n++] = k;
            buf[n++] = wmax;
            buf[n++] = (WORD)(*ticks - t0);
            last = k;
        }
    }
    for (i = 0; i < n; i += 3) {
        WORD k = buf[i];
        wsprintf(line, "OBS P 61h&3=%u mode=%u %s ~divisor=%u ~tick=%u\r\n", k & 3, (k >> 2) & 7,
                 (LPSTR)(k >> 5 ? "tone" : "quiet"), buf[i + 1], buf[i + 2]);
        put(line);
    }
    callw(11, 1, 0, 0, 0, 0, 0);
    callw(2, 0, 0, 0, 0, 0, 0);
    spk("P closed");
    flush();
}

/* ---- T: timing: each group legato notes alternating between C5 and C6
   (37, 49), so that every item's start shows on the recording: the
   lengths at tempo 120, the tempos with quarter notes, rests between,
   the modes' gaps, SetVoiceSound's durations ---- */
static void tgroup(int grp)
{
    callw(9, 0, 0, 0, 0, 0, 0);
    callw(11, 1, 0, 0, 0, 0, 0);
    waitticks(2);
    marker(grp % 4 + 1);
}

static void partT(void)
{
    static const int lens[] = { 1, 2, 3, 4, 5, 6, 8, 12, 16, 24, 32, 64 };
    static const int tempos[] = { 32, 33, 45, 60, 77, 90, 100, 111, 120, 150, 177, 200, 233, 254, 255 };
    int i, k, g = 0;
    callw(1, 0, 0, 0, 0, 0, 0);
    callw(3, 2, 1, 4096, 0, 0, 0);
    marker(1);
    for (i = 0; i < sizeof lens / sizeof lens[0]; i++) {
        accent(11, 120, 128, 1, 0);
        for (k = 0; k < (lens[i] < 4 ? 3 : 6); k++)
            note(11, k & 1 ? 49 : 37, lens[i], 0);
        tgroup(++g);
    }
    for (i = 0; i < sizeof tempos / sizeof tempos[0]; i++) {
        accent(12, tempos[i], 128, 1, 0);
        for (k = 0; k < 4; k++)
            note(12, k & 1 ? 49 : 37, 4, 0);
        tgroup(++g);
    }
    /* rests: note, rest, note, rest ... */
    for (i = 0; i < 4; i++) {
        static const int rl[] = { 4, 8, 16, 2 };
        accent(13, 120, 128, 1, 0);
        for (k = 0; k < 3; k++) {
            note(13, 37, 4, 0);
            note(13, 0, rl[i], 0);
        }
        note(13, 49, 4, 0);
        tgroup(++g);
    }
    /* the modes, quarter notes at 120 and 77 */
    for (i = 0; i < 6; i++) {
        accent(14, i < 3 ? 120 : 77, 128, i % 3, 0);
        for (k = 0; k < 4; k++)
            note(14, k & 1 ? 49 : 37, 4, 0);
        tgroup(++g);
    }
    /* SetVoiceSound: durations 1-20, 500 and 1000 Hz alternating */
    /* (durations 1-4, one and two ticks, were measured too, on 3.1's: the
       recording breaks such short tones up differently each run, so they
       are not played here) */
    for (i = 5; i <= 20; i += 3) {
        for (k = 0; k < 6; k++)
            callw(8, 4, 1, k & 1 ? 1000 : 500, 0, i, 0);
        tgroup(++g);
    }
    /* SetVoiceSound frequencies, 40 ticks each, a rest between */
    {
        static const WORD fq[] = { 37, 40, 55, 100, 220, 261, 262, 440, 523, 1000, 1500,
                                   2000, 3000, 4000, 5000 };
        for (i = 0; i < sizeof fq / sizeof fq[0]; i++) {
            callw(8, 4, 1, fq[i], 0, 80, 0);
            callw(8, 4, 1, 0, 0, 20, 0);
        }
        tgroup(++g);
    }
    callw(2, 0, 0, 0, 0, 0, 0);
    flush();
}

/* ---- U: the normal and staccato modes over tempos and lengths, notes
   alternating C5/C6, four a group ---- */
static void partU(void)
{
    static const int tempos[] = { 255, 200, 150, 100, 60, 32 };
    static const int lens[] = { 16, 8, 4 };
    int i, j, m, k, g = 0;
    callw(1, 0, 0, 0, 0, 0, 0);
    callw(3, 2, 1, 4096, 0, 0, 0);
    marker(1);
    for (m = 0; m <= 2; m += 2)
        for (i = 0; i < sizeof tempos / sizeof tempos[0]; i++)
            for (j = 0; j < sizeof lens / sizeof lens[0]; j++) {
                accent(15, tempos[i], 128, m, 0);
                for (k = 0; k < 4; k++)
                    note(15, k & 1 ? 49 : 37, lens[j], 0);
                tgroup(++g);
            }
    callw(2, 0, 0, 0, 0, 0, 0);
    flush();
}

/* ---- V: normal mode, quarter and eighth notes over many tempos ---- */
static void partV(void)
{
    int t, j, k, g = 0;
    callw(1, 0, 0, 0, 0, 0, 0);
    callw(3, 2, 1, 4096, 0, 0, 0);
    marker(1);
    for (t = 35; t <= 255; t += 11)
        for (j = 4; j <= 8; j += 4) {
            accent(16, t, 128, 0, 0);
            for (k = 0; k < 3; k++)
                note(16, k & 1 ? 49 : 37, j, 0);
            tgroup(++g);
        }
    callw(2, 0, 0, 0, 0, 0, 0);
    flush();
}

/* ---- W: the BIOS's ticks around StartSound: from StartSound to the
   first note's start and to the end, with no quiet left over (after a
   legato note), with 13 ticks (after a normal quarter at 120) and with
   101 (after a quarter rest at 120: 100 + 1), and the BIOS ticks while
   nothing plays as a check ---- */
static void wcase(const char *what, int last)
{
    DWORD ts, t1;
    /* the last item before: sets the quiet left over */
    callw(5, 5, 1, 120, 128, last, 0);
    if (last == 3) {
        callw(5, 5, 1, 120, 128, 1, 0);
        note(17, 37, 16, 0);
        note(17, 0, 4, 0);
    } else
        note(17, 37, 16, 0);
    callw(9, 0, 0, 0, 0, 0, 0);
    callw(11, 1, 0, 0, 0, 0, 0);
    waitticks(3);
    callw(5, 5, 1, 32, 128, 1, 0);
    note(17, 49, 2, 0);                 /* 3.75 s */
    note(17, 37, 2, 0);
    tickedge();
    ts = *ticks;
    callw(9, 0, 0, 0, 0, 0, 0);
    countdown(1);
    t1 = *ticks;
    countdown(0);
    wsprintf(line, "OBS ~W %s: BIOS ticks to the first note %lu, to the end %lu\r\n", (LPSTR)what,
             t1 - ts, *ticks - ts);
    put(line);
    flush();
}

static void partW(void)
{
    DWORD t0;
    callw(1, 0, 0, 0, 0, 0, 0);
    tickedge();
    t0 = *ticks;
    waitticks(100);
    wsprintf(line, "OBS W idle check: %lu\r\n", *ticks - t0);
    put(line);
    wcase("after legato", 1);
    wcase("after normal", 0);
    wcase("after a rest", 3);
    callw(2, 0, 0, 0, 0, 0, 0);
}

/* ---- S: state: notes added while playing, StartSound twice, StopSound
   and CloseSound while playing, WaitSoundState's states, thresholds ---- */
static void partS(void)
{
    int r, i;
    DWORD t0;
    marker(2);
    callw(1, 0, 0, 0, 0, 0, 0);
    callw(5, 5, 1, 120, 128, 0, 0);
    note(9, 37, 4, 0);
    note(9, 41, 4, 0);
    r = callw(9, 0, 0, 0, 0, 0, 0);
    wsprintf(line, "OBS S StartSound = %d count %d\r\n", r, callw(13, 1, 1, 0, 0, 0, 0));
    put(line);
    note(9, 45, 4, 0);
    wsprintf(line, "OBS S a note added while playing: count %d\r\n", callw(13, 1, 1, 0, 0, 0, 0));
    put(line);
    t0 = *ticks;
    r = callw(11, 1, 0, 0, 0, 0, 0);
    wsprintf(line, "OBS S WaitSoundState(0) = %d after ~%lu ticks, count %d\r\n", r, *ticks - t0, callw(13, 1, 1, 0, 0, 0, 0));
    put(line);
    waitticks(4);
    wsprintf(line, "OBS S count later %d\r\n", callw(13, 1, 1, 0, 0, 0, 0));
    put(line);
    r = callw(9, 0, 0, 0, 0, 0, 0);
    wsprintf(line, "OBS S StartSound again = %d count %d\r\n", r, callw(13, 1, 1, 0, 0, 0, 0));
    put(line);
    callw(11, 1, 0, 0, 0, 0, 0);
    flush();
    marker(3);
    /* StartSound twice in a row */
    note(9, 49, 8, 0);
    note(9, 50, 8, 0);
    callw(9, 0, 0, 0, 0, 0, 0);
    note(9, 51, 8, 0);
    r = callw(9, 0, 0, 0, 0, 0, 0);
    wsprintf(line, "OBS S StartSound while playing = %d count %d\r\n", r, callw(13, 1, 1, 0, 0, 0, 0));
    put(line);
    callw(11, 1, 0, 0, 0, 0, 0);
    flush();
    marker(1);
    /* thresholds */
    for (i = 0; i <= 3; i++)
        note(9, 37 + i, 16, 0);
    r = callw(16, 2, 1, 2, 0, 0, 0);
    wsprintf(line, "OBS S SetVoiceThreshold(1,2) = %d status %d\r\n", r, callw(15, 0, 0, 0, 0, 0, 0));
    put(line);
    callw(9, 0, 0, 0, 0, 0, 0);
    {
        int last = -99, st;
        t0 = *ticks;
        while (*ticks - t0 < 30) {
            int n;
            /* the status first: the bit is set as the count comes down to
               the threshold, so the count read after it is that or less
               (read before it, a tick could come between: seen with both
               drivers) */
            st = callw(15, 0, 0, 0, 0, 0, 0);
            n = callw(13, 1, 1, 0, 0, 0, 0);
            /* each count, and the count when the status bit is seen */
            if (n != last) {
                wsprintf(line, "OBS S count %d\r\n", n);
                put(line);
                last = n;
            }
            if (st) {
                wsprintf(line, "OBS S status set, count %d\r\n", n);
                put(line);
            }
        }
        wsprintf(line, "OBS S status after %d, again %d\r\n", callw(15, 0, 0, 0, 0, 0, 0), callw(15, 0, 0, 0, 0, 0, 0));
        put(line);
    }
    marker(2);
    for (i = 0; i <= 3; i++)
        note(9, 37 + i, 16, 0);
    callw(16, 2, 1, 2, 0, 0, 0);
    callw(9, 0, 0, 0, 0, 0, 0);
    flush();
    t0 = *ticks;
    r = callw(11, 1, 1, 0, 0, 0, 0);
    wsprintf(line, "OBS S WaitSoundState(S_THRESHOLD) = %d count %d\r\n", r, callw(13, 1, 1, 0, 0, 0, 0));
    put(line);
    /* WaitSoundState(S_ALLTHRESHOLD) here never returned on Windows
       3.1's driver (its count was 2, the threshold 2): not called */
    callw(11, 1, 0, 0, 0, 0, 0);
    flush();
    marker(2);
    /* CloseSound while playing */
    for (i = 0; i <= 3; i++)
        note(9, 37 + i, 4, 0);
    callw(9, 0, 0, 0, 0, 0, 0);
    countdown(2);
    r = callw(2, 0, 0, 0, 0, 0, 0);
    wsprintf(line, "OBS S CloseSound while playing = %d\r\n", r);
    put(line);
    spk("S closed while playing");
    flush();
    marker(3);
    /* SyncAllVoices: a sync mark in the queue? */
    callw(1, 0, 0, 0, 0, 0, 0);
    note(9, 37, 8, 0);
    r = callw(12, 0, 0, 0, 0, 0, 0);
    wsprintf(line, "OBS S SyncAllVoices = %d count %d\r\n", r, callw(13, 1, 1, 0, 0, 0, 0));
    put(line);
    note(9, 41, 8, 0);
    callw(9, 0, 0, 0, 0, 0, 0);
    callw(11, 1, 0, 0, 0, 0, 0);
    callw(2, 0, 0, 0, 0, 0, 0);
    flush();
    marker(4);
}

/* ---- SNDREC.DRV in front (tests/apps/sndrec.asm): its records, one line
   each: the caller's module, the call, its arguments (as many words as
   it took off), AX and DX out ---- */
static const char *const snames[] = { "", "OpenSound", "CloseSound", "SetVoiceQueueSize",
    "SetVoiceNote", "SetVoiceAccent", "SetVoiceEnvelope", "SetSoundNoise",
    "SetVoiceSound", "StartSound", "StopSound", "WaitSoundState", "SyncAllVoices",
    "CountVoiceNotes", "GetThresholdEvent", "GetThresholdStatus",
    "SetVoiceThreshold", "DoBeep", "WEP" };

static void records(HMODULE hs)
{
    FARPROC plog = GetProcAddress(hs, MAKEINTRESOURCE(200));
    BYTE FAR *h;
    WORD n, i, k, rs;
    char name[80];
    if (!plog)
        return;
    h = (BYTE FAR *)((DWORD (FAR PASCAL *)(void))plog)();
    n = *(WORD FAR *)h;
    rs = *(WORD FAR *)(h + 4);
    wsprintf(line, "REC %u calls, %u kept\r\n", *(WORD FAR *)(h + 6), n);
    put(line);
    for (i = 0; i < n; i++) {
        WORD FAR *r = (WORD FAR *)(h + 16 + i * rs);
        WORD ord = r[0], pop = r[18], m;
        name[0] = 0;
        m = K_GETEXEPTR(r[7]);
        if (m)
            GetModuleFileName((HMODULE)m, name, sizeof name);
        for (k = lstrlen(name); k > 0 && name[k - 1] != '\\'; k--)
            ;
        wsprintf(line, "REC %s: %s(", (LPSTR)(name + k), (LPSTR)(ord <= 18 ? snames[ord] : "?"));
        put(line);
        /* the arguments: the words taken off, first-pushed first */
        for (k = pop / 2; k > 0; k--) {
            wsprintf(line, k > 1 ? "%04X, " : "%04X", r[8 + k - 1]);
            put(line);
        }
        /* (DX only where it is a result, GetThresholdEvent's; AX not for
           DoBeep, whose AX is what its loop left: "~", not compared) */
        wsprintf(line, ") = %sAX=%04X %sDX=%04X, %u bytes off\r\n", (LPSTR)(ord == 17 ? "~" : ""), r[16],
                 (LPSTR)(ord == 14 ? "" : "~"), r[17], pop);
        put(line);
    }
    flush();
}

/* ---- X: experiments ---- */
static void partX(void)
{
    static const WORD dob[] = { 0, 1, 2, 0x10, 0x30, 0x40, 0xFFFF };
    int i;
    marker(1);
    for (i = 0; i < sizeof dob / sizeof dob[0]; i++) {
        DWORD t0 = *ticks;
        callw(17, 1, dob[i], 0, 0, 0, 0);
        wsprintf(line, "OBS X DoBeep(%04X) ~ticks %lu\r\n", dob[i], *ticks - t0);
        put(line);
        regline("X DoBeep");
        waitticks(6);
    }
    flush();
    marker(2);
    marker(3);
}

/* ---- Q: the queue's capacity, in SNH (a full queue may make a call
   wait for room): for each set-up the helper queues quarter notes until
   a call fails or waits; this task waits for it, then (if it is still
   waiting) StopSound, then CloseSound, each followed by a wait; the
   helper's log is copied into this one ---- */
static void helperlog(void)
{
    HFILE h = _lopen("C:\\SN1H.LOG", OF_READ);
    static char buf[1024];
    int n, i, j = 0;
    char l[120];
    if (h == HFILE_ERROR) {
        put("OBS Q no helper log\r\n");
        return;
    }
    while ((n = _lread(h, buf, sizeof buf)) > 0)
        for (i = 0; i < n; i++) {
            if (buf[i] == '\r')
                continue;
            if (buf[i] == '\n' || j == sizeof l - 1) {
                l[j] = 0;
                j = 0;
                /* the progress lines are not compared (timing) */
                if (l[0] == 'H' && l[1] == ' ' && l[2] == 'n' && !strchr_(l, '='))
                    continue;
                put("OBS Q ");
                put(l);
                put("\r\n");
                continue;
            }
            l[j++] = buf[i];
        }
    _lclose(h);
}

static int helper_running(void)
{
    return GetModuleHandle("SNH") != 0;
}

static void qrun(const char *q, const char *len, const char *start, const char *acc)
{
    DWORD t0;
    WORD h;
    OFSTRUCT of;
    OpenFile("C:\\SN1H.LOG", &of, OF_DELETE);
    WritePrivateProfileString("w16", "sn1hq", q, "SYSTEM.INI");
    WritePrivateProfileString("w16", "sn1hlen", len, "SYSTEM.INI");
    WritePrivateProfileString("w16", "sn1hstart", start, "SYSTEM.INI");
    WritePrivateProfileString("w16", "sn1hacc", acc, "SYSTEM.INI");
    wsprintf(line, "OBS Q queue %s, length %s, start %s, accent %s\r\n", (LPSTR)q, (LPSTR)len,
             (LPSTR)start, (LPSTR)acc);
    put(line);
    h = WinExec("C:\\SNH.EXE H", SW_SHOWNORMAL);
    if (h <= 32) {
        wsprintf(line, "OBS Q helper not started: %u\r\n", h);
        put(line);
        return;
    }
    t0 = *ticks;
    while (helper_running() && *ticks - t0 < 60)
        Yield();
    if (helper_running()) {
        put("OBS Q helper still waiting after 60 ticks\r\n");
        wsprintf(line, "OBS Q StopSound from here = %d\r\n", callw(10, 0, 0, 0, 0, 0, 0));
        put(line);
        t0 = *ticks;
        while (helper_running() && *ticks - t0 < 60)
            Yield();
    }
    if (helper_running()) {
        put("OBS Q helper still waiting after StopSound\r\n");
        wsprintf(line, "OBS Q CloseSound from here = %d\r\n", callw(2, 0, 0, 0, 0, 0, 0));
        put(line);
        t0 = *ticks;
        while (helper_running() && *ticks - t0 < 60)
            Yield();
    }
    if (helper_running())
        put("OBS Q helper still waiting after CloseSound\r\n");
    helperlog();
    flush();
}

/* [w16] sn1qlist: the set-ups, "queue,length,start,accent" separated
   by ";" (some hang Windows 3.1's driver for good: tests/soundprobe.sh
   runs those alone) */
static void partQ(void)
{
    static char list[200];
    char f[4][12];
    char *c;
    int k, j;
    GetPrivateProfileString("w16", "sn1qlist", "192,4,0,1;192,4,1,1;192,64,1,1;192,4,0,0;12,4,0,0;1024,4,0,1",
                            list, sizeof list, "SYSTEM.INI");
    marker(4);
    c = list;
    while (*c) {
        for (k = 0; k < 4; k++) {
            j = 0;
            while (*c && *c != ',' && *c != ';' && j < 11)
                f[k][j++] = *c++;
            f[k][j] = 0;
            if (*c == ',')
                c++;
        }
        if (*c == ';')
            c++;
        qrun(f[0], f[1], f[2], f[3]);
    }
    marker(4);
}

int __cdecl tmain(void)
{
    HMODULE hs;
    WORD i;

    U_INITAPP(SELECTOROF((void FAR *)&i));
    ticks = (volatile DWORD FAR *)MAKELP(get_0040h(), 0x6C);
    if (*(char FAR *)MAKELP(init_regs[6], init_regs[1]) == 'H') {   /* the command line (InitTask: ES:BX) */
        hs = GetModuleHandle("SOUND");
        for (i = 1; i < 32; i++)
            p[i] = GetProcAddress(hs, MAKEINTRESOURCE(i));
        return helper();
    }
    GetPrivateProfileString("w16", "sn1parts", "ABCDEF", parts, sizeof parts, "SYSTEM.INI");
    out("SN1 start\r\n");
    hs = GetModuleHandle("SOUND");
    wsprintf(line, "OBS SOUND loaded: %u\r\n", hs != 0);
    put(line);
    for (i = 1; i < 32; i++)
        p[i] = hs ? GetProcAddress(hs, MAKEINTRESOURCE(i)) : 0;
    spk("start");
    flush();
    if (part('A'))
        partA(hs);
    if (part('B'))
        partB();
    if (part('C'))
        partC();
    if (part('D'))
        partD();
    if (part('E'))
        partE();
    if (part('F'))
        partF();
    if (part('G'))
        partG();
    if (part('H'))
        partH();
    if (part('S'))
        partS();
    if (part('P'))
        partP();
    if (part('T'))
        partT();
    if (part('U'))
        partU();
    if (part('V'))
        partV();
    if (part('W'))
        partW();
    if (part('Q'))
        partQ();
    if (part('X'))
        partX();
    flush();
    records(hs);
    out("SN1 done\r\n");
    return 0;
}
