/*
 * mmdev.c - MMP.EXE's device sections (layer 3; tests/mmsystem/mmp.c,
 * ARCHITECTURE.md 23): the wave, MIDI, auxiliary and joystick functions,
 * the error texts, sndPlaySound and MessageBeep, as an application sees
 * them, and with the logging pass-through drivers MDS*.DRV in place
 * (tests/mmsystem/mds.c, mdl.c) what MMSYSTEM sends the device drivers
 * for each call: after every step the drivers' records ("  MD ...",
 * "  DCB ...") follow the step's line, then the callbacks MMC.DLL's
 * function got ("  cb ...") and the window and task messages that came.
 *
 * Sections (letters in C:\MMP.ARG):
 *   l  the drivers open now (USER's list)
 *   z  three seconds of yielding and messages
 *   n  the device counts, every DevCaps with good and bad IDs and sizes
 *   e  the four GetErrorText functions for every number 0-300 and sizes
 *   w  wave output: opens (formats, queries, callbacks, flags), headers,
 *      writing, position, pause, restart, reset, volume, pitch, rate,
 *      loops, GetID, Message, bad and stale handles
 *   i  wave input likewise
 *   m  MIDI output, and the MIDI mapper (ID FFFFh)
 *   j  MIDI input
 *   a  auxiliary devices
 *   y  joystick functions
 *   x  sndPlaySound: [sounds] names, files, memory images, the SND_*
 *      flags, stopping
 *   b  MessageBeep with each type (does it reach the wave driver?)
 *
 * Sources: the Multimedia Programmer's Guide chapters 4-6 (the functions,
 * structures, flags and messages); the probe only calls them.
 */
#include <windows.h>
#include <mmsystem.h>
#include <toolhelp.h>
#include "mmc.h"

extern void lg(const char *fmt, ...);
extern void put(const char FAR *s, int n);
extern char line[600];
extern struct mmcstate FAR *dev_st;
extern HWND dev_hwnd;
extern FARPROC dev_cb;
extern void dev_pump(void);

typedef void (FAR PASCAL *MLDUMP)(LPCSTR);
typedef void (FAR PASCAL *MLNAME)(DWORD, LPCSTR);
typedef void (FAR PASCAL *MLPROBE)(WORD, HTASK);
static MLDUMP mldump;
static MLNAME mlname;
static WORD cbmark, booted;
static HWND mmwnd;
static int quietwnd;            /* the boot: its order against the records is timing */
extern void windows(void);

extern WORD getss2(void);
#pragma aux getss2 = "mov ax, ss" value [ax];

/* ---- names: values the log shows by name, here and in MMSYSLOG's ---- */
static struct { DWORD v; char n[14]; } nm[60];
static int nnm;

static void name(DWORD v, const char *n)
{
    int i;
    for (i = 0; i < nnm; i++)
        if (!lstrcmp(nm[i].n, n))
            break;
    if (i == nnm && nnm < 60)
        nnm++;
    nm[i].v = v;
    lstrcpy(nm[i].n, n);
    if (mlname)
        mlname(v, n);
}

static const char *vs(DWORD v)
{
    static char b[8][20];
    static int k;
    char *r = b[k++ & 7];
    int i;
    for (i = 0; i < nnm; i++)
        if (nm[i].v == v && v)
            return lstrcpy(r, nm[i].n);
    wsprintf(r, "%08lX", v);
    return r;
}

static const char *hs(WORD v)
{
    static char b[8][20];
    static int k;
    char *r = b[k++ & 7];
    int i;
    for (i = 0; i < nnm; i++)
        if (nm[i].v == (DWORD)v && v)
            return lstrcpy(r, nm[i].n);
    wsprintf(r, "%04X", v);
    return r;
}

/* the drivers' records, the callbacks and the messages since the last */
static void flush(void)
{
    MSG m;
    if (mldump)
        mldump("C:\\MMP.LOG");
    while (cbmark < dev_st->nrec && cbmark < MMC_NREC) {
        struct mmcrec FAR *r = &dev_st->rec[cbmark];
        lg("  cb h %s msg %04X user %04X%04X dw1 %s dw2 %04X%04X%s", (LPSTR)hs(r->w[21]),
           r->w[20], r->w[19], r->w[18], (LPSTR)vs(MAKELONG(r->w[16], r->w[17])), r->w[15],
           r->w[14], (LPSTR)(r->ss == getss2() ? " (the probe's stack)" : ""));
        cbmark++;
    }
    if (dev_st->nrec >= MMC_NREC) {
        if (dev_st->nrec > MMC_NREC)
            lg("  cb (%u more)", dev_st->nrec - MMC_NREC);
        dev_st->nrec = 0;
        cbmark = 0;
    }
    while (PeekMessage(&m, 0, 0, 0, PM_REMOVE)) {
        if (!m.hwnd)
            lg("  task message %04X wParam %s lParam %s", m.message, (LPSTR)hs(m.wParam),
               (LPSTR)vs(m.lParam));
        else if (m.hwnd == dev_hwnd && m.message >= 0x3A0 && m.message < 0x3D0)
            lg("  window message %04X wParam %s lParam %s", m.message, (LPSTR)hs(m.wParam),
               (LPSTR)vs(m.lParam));
        else {
            /* MMSYSTEM's own window's messages, before it gets them */
            if (m.hwnd && m.hwnd == mmwnd && !quietwnd)
                lg("  to MMSYSTEM's window: %04X wParam %s lParam %s", m.message,
                   (LPSTR)hs(m.wParam), (LPSTR)vs(m.lParam));
            TranslateMessage(&m);
            DispatchMessage(&m);
        }
    }
}

/* waits (yielding) until *flags has bit, at most ms; then the records */
static void waitfor(DWORD FAR *flags, DWORD bit, DWORD ms)
{
    DWORD t = GetTickCount();
    MSG m;
    while (!(*flags & bit) && GetTickCount() - t < ms)
        PeekMessage(&m, 0, 0, 0, PM_NOREMOVE | PM_NOYIELD), Yield();
    if (!(*flags & bit))
        lg("  (timed out waiting for flag %04lX)", bit);
}

static void pause(DWORD ms)
{
    DWORD t = GetTickCount();
    while (GetTickCount() - t < ms)
        Yield();
}

static void fill(void FAR *p, int n, BYTE b)
{
    BYTE FAR *q = p;
    while (n--)
        *q++ = b;
}

static char *hexs(char *p, const void FAR *b, int n)
{
    const BYTE FAR *q = b;
    int i;
    for (i = 0; i < n; i++)
        p += wsprintf(p, "%02X", q[i]);
    return p;
}

/* ---- l: the drivers open now ---- */
static void listdrv(void)
{
    HDRVR h = 0;
    int k;
    char *p = line;
    p += wsprintf(p, "open drivers:");
    for (k = 0; k < 30; k++) {
        DRIVERINFOSTRUCT d;
        h = GetNextDriver(h, 0);
        if (!h)
            break;
        d.length = sizeof d;
        GetDriverInfo(h, &d);
        p += wsprintf(p, " %04X %s", (WORD)h, (LPSTR)d.szAliasName);
    }
    lg("%s", (LPSTR)line);
}

/* ---- n: counts and capabilities ---- */
typedef UINT (FAR PASCAL *CAPSFN)(UINT, void FAR *, UINT);

static void caps(const char *what, CAPSFN f, UINT n)
{
    static const UINT ids[] = { 0, 1, 2, 3, 0xFFFF, 0xFFFE, 0x8000 };
    static const UINT sizes[] = { 0, 1, 4, 8, 32, 48, 52, 80 };
    BYTE b[100];
    char *p;
    UINT r;
    int k, s;
    for (k = 0; k < sizeof ids / sizeof ids[0]; k++) {
        if (ids[k] > n + 1 && ids[k] < 0x8000)
            continue;
        fill(b, sizeof b, 0xEE);
        r = f(ids[k], b, 80);
        if (!r) {
            /* what a driver leaves in szPname after the name's end (MIDIMAP:
               its stack) is not MMSYSTEM's */
            int i, z = 0;
            for (i = 8; i < 40; i++) {
                if (z)
                    b[i] = 0;
                else if (!b[i])
                    z = 1;
            }
        }
        p = line;
        p += wsprintf(p, "%sGetDevCaps(%04X, size 80) = %u:", (LPSTR)what, ids[k], r);
        if (!r) {
            int len = 80;
            while (len > 0 && b[len - 1] == 0xEE)
                len--;
            p += wsprintf(p, " %d bytes ", len);
            p = hexs(p, b, len);
            p += wsprintf(p, " name \"%s\"", (LPSTR)(b + 6));
        }
        lg("%s", (LPSTR)line);
        flush();
    }
    if (!n)
        return;
    for (s = 0; s < sizeof sizes / sizeof sizes[0]; s++) {
        fill(b, sizeof b, 0xEE);
        r = f(0, b, sizes[s]);
        p = line;
        p += wsprintf(p, "%sGetDevCaps(0, size %u) = %u: ", (LPSTR)what, sizes[s], r);
        p = hexs(p, b, sizes[s] + 2 < 82 ? sizes[s] + 2 : 82);
        lg("%s", (LPSTR)line);
        flush();
    }
    /* (NULL and 0001:0000 are handed to the driver as they are; 3.1 was
       unstable afterwards, as after an error text into 0001:0000: not
       tried) */
}

static void sec_numbering(void)
{
    UINT nwo, nwi, nmo, nmi, nax, njoy;
    nwo = waveOutGetNumDevs();
    flush();
    nwi = waveInGetNumDevs();
    flush();
    nmo = midiOutGetNumDevs();
    flush();
    nmi = midiInGetNumDevs();
    flush();
    nax = auxGetNumDevs();
    flush();
    njoy = joyGetNumDevs();
    flush();
    lg("devices: waveOut %u waveIn %u midiOut %u midiIn %u aux %u joy %u", nwo, nwi, nmo, nmi, nax, njoy);
    lg("again: waveOut %u waveIn %u midiOut %u midiIn %u aux %u joy %u", waveOutGetNumDevs(),
       waveInGetNumDevs(), midiOutGetNumDevs(), midiInGetNumDevs(), auxGetNumDevs(), joyGetNumDevs());
    flush();
    caps("waveOut", (CAPSFN)waveOutGetDevCaps, nwo);
    caps("waveIn", (CAPSFN)waveInGetDevCaps, nwi);
    caps("midiOut", (CAPSFN)midiOutGetDevCaps, nmo);
    caps("midiIn", (CAPSFN)midiInGetDevCaps, nmi);
    caps("aux", (CAPSFN)auxGetDevCaps, nax);
    caps("joy", (CAPSFN)joyGetDevCaps, njoy ? njoy : 2);
    listdrv();
}

/* ---- e: error texts ---- */
typedef UINT (FAR PASCAL *ERRFN)(UINT, LPSTR, UINT);

static void errtext(const char *what, ERRFN f)
{
    char b[300], pb[300];
    UINT e, r, last = 0xFFFF, from = 0;
    char *p;
    pb[0] = 0;
    /* each number 0-300 and FFFF; a run of numbers with the same nonzero
       answer and the same buffer afterwards is one line */
    for (e = 0; e <= 301; e++) {
        UINT ee = e == 301 ? 0xFFFF : e;
        fill(b, sizeof b, 'x');
        b[299] = 0;
        r = f(ee, b, 299);
        if (last != 0xFFFF && (r != last || lstrcmp(b, pb))) {
            lg("%sGetErrorText(%u-%u) = %u, buffer \"%.130s\"", (LPSTR)what, from, e - 1, last,
               (LPSTR)pb);
            last = 0xFFFF;
        }
        if (r) {
            if (last == 0xFFFF) {
                from = ee;
                last = r;
                lstrcpy(pb, b);
            }
            if (e == 301)
                lg("%sGetErrorText(%u-%u) = %u, buffer \"%.130s\"", (LPSTR)what, from, ee, r,
                   (LPSTR)b);
            continue;
        }
        lg("%sGetErrorText(%u) = 0 \"%s\" (%d)", (LPSTR)what, ee, (LPSTR)b, lstrlen(b));
    }
    flush();
    {
        static const UINT sizes[] = { 0, 1, 2, 5, 10 };
        int k;
        for (k = 0; k < 5; k++) {
            fill(b, sizeof b, 'x');
            r = f(2, b, sizes[k]);
            p = line;
            p += wsprintf(p, "%sGetErrorText(2, size %u) = %u: ", (LPSTR)what, sizes[k], r);
            p = hexs(p, b, sizes[k] + 2);
            lg("%s", (LPSTR)line);
            fill(b, sizeof b, 'x');
            r = f(40, b, sizes[k]);
            p = line;
            p += wsprintf(p, "%sGetErrorText(40, size %u) = %u: ", (LPSTR)what, sizes[k], r);
            p = hexs(p, b, sizes[k] + 2);
            lg("%s", (LPSTR)line);
        }
        r = f(40, NULL, 128);
        lg("%sGetErrorText(40, NULL, 128) = %u", (LPSTR)what, r);
        r = f(2, NULL, 128);
        lg("%sGetErrorText(2, NULL, 128) = %u", (LPSTR)what, r);
    }
    flush();
}

static void sec_errors(void)
{
    char b[300];
    UINT r;
    errtext("waveOut", (ERRFN)waveOutGetErrorText);
    errtext("waveIn", (ERRFN)waveInGetErrorText);
    errtext("midiOut", (ERRFN)midiOutGetErrorText);
    errtext("midiIn", (ERRFN)midiInGetErrorText);
    /* the joystick and timer have no text function; mciGetErrorString
       is layer 4's */
    fill(b, sizeof b, 'x');
    r = waveOutGetErrorText(0, b, 300);
    lg("waveOutGetErrorText(0, size 300) = %u \"%s\"", r, (LPSTR)b);
}

/* ---- w: wave output ---- */
static PCMWAVEFORMAT fmt11, fmtbad;
static HGLOBAL hd1, hd2, hh;
static LPSTR data1, data2;
static WAVEHDR FAR *wh1, FAR *wh2;

static void setfmt(PCMWAVEFORMAT *f, WORD tag, WORD ch, DWORD rate, WORD bits)
{
    f->wf.wFormatTag = tag;
    f->wf.nChannels = ch;
    f->wf.nSamplesPerSec = rate;
    f->wf.nBlockAlign = ch * ((bits + 7) / 8);
    f->wf.nAvgBytesPerSec = rate * f->wf.nBlockAlign;
    f->wBitsPerSample = bits;
}

static void buffers(void)
{
    int i;
    if (hh)
        return;
    hd1 = GlobalAlloc(GMEM_MOVEABLE | GMEM_SHARE, 2205);
    hd2 = GlobalAlloc(GMEM_MOVEABLE | GMEM_SHARE, 1103);
    hh = GlobalAlloc(GMEM_MOVEABLE | GMEM_SHARE, 2 * sizeof(WAVEHDR) + 64);
    data1 = GlobalLock(hd1);
    data2 = GlobalLock(hd2);
    wh1 = (WAVEHDR FAR *)GlobalLock(hh);
    wh2 = wh1 + 1;
    for (i = 0; i < 2205; i++)          /* a 551 Hz square wave, 0.2 s */
        data1[i] = (i / 10) & 1 ? (char)0xA0 : (char)0x60;
    for (i = 0; i < 1103; i++)
        data2[i] = (i / 5) & 1 ? (char)0x90 : (char)0x70;
    name((DWORD)data1, "data1");
    name((DWORD)data2, "data2");
    name((DWORD)wh1, "hdr1");
    name((DWORD)wh2, "hdr2");
    name((DWORD)(LPVOID)&fmt11, "fmt");
    name((DWORD)dev_cb, "MMCCB");
    name((DWORD)(WORD)dev_hwnd, "hwnd");
    name((DWORD)(WORD)GetCurrentTask(), "task");
}

static void sethdr(WAVEHDR FAR *h, LPSTR d, DWORD len, DWORD flags, DWORD loops)
{
    h->lpData = d;
    h->dwBufferLength = len;
    h->dwBytesRecorded = 0;
    h->dwUser = 0x0BADC0DEL;
    h->dwFlags = flags;
    h->dwLoops = loops;
    h->lpNext = NULL;
    h->reserved = 0;
}

static const char *wflags(WAVEHDR FAR *h)
{
    static char b[4][80];
    static int k;
    char *r = b[k++ & 3];
    wsprintf(r, "flags %04lX rec %ld next %s res %s", h->dwFlags, h->dwBytesRecorded,
             (LPSTR)vs((DWORD)h->lpNext), (LPSTR)vs(h->reserved));
    return r;
}


static void wpos(HWAVEOUT h, int in)
{
    static const UINT types[] = { TIME_MS, TIME_SAMPLES, TIME_BYTES, TIME_SMPTE, TIME_MIDI, 7, 0 };
    MMTIME t;
    UINT r;
    int k;
    for (k = 0; k < 7; k++) {
        fill(&t, sizeof t, 0xEE);
        t.wType = types[k];
        r = in ? waveInGetPosition((HWAVEIN)h, &t, sizeof t)
               : waveOutGetPosition(h, &t, sizeof t);
        lg("  GetPosition(type %u) = %u: type %u value %08lX", types[k], r, t.wType, t.u.ms);
        flush();
    }
    fill(&t, sizeof t, 0xEE);
    t.wType = TIME_BYTES;
    r = in ? waveInGetPosition((HWAVEIN)h, &t, 4) : waveOutGetPosition(h, &t, 4);
    lg("  GetPosition(size 4) = %u: type %u value %08lX", r, t.wType, t.u.ms);
    r = in ? waveInGetPosition((HWAVEIN)h, NULL, sizeof t) : waveOutGetPosition(h, NULL, sizeof t);
    lg("  GetPosition(NULL) = %u", r);
    flush();
}

static void wopen(const char *what, UINT id, LPWAVEFORMAT f, DWORD cb, DWORD inst, DWORD flags,
                  HWAVEOUT FAR *ph, const char *hn)
{
    UINT r;
    HWAVEOUT h = (HWAVEOUT)0xEEEE;
    if (ph)
        *ph = h;
    r = waveOutOpen(ph ? ph : NULL, id, f, cb, inst, flags);
    if (ph && r == 0 && hn)
        name((DWORD)(WORD)*ph, hn);
    lg("waveOutOpen(%s) = %u, handle %s", (LPSTR)what, r, (LPSTR)(ph ? hs((WORD)*ph) : "-"));
    flush();
}

/* the header checks: prepare, unprepare and write (add) with bad
   headers, sizes and flags, nothing playing */
typedef UINT (FAR PASCAL *HDRFN)(HWAVEOUT, LPWAVEHDR, UINT);

static void hdrtry(const char *what, HDRFN f, HWAVEOUT h, LPWAVEHDR p, UINT size)
{
    UINT r = f(h, p, size);
    lg("  %s = %u, %s", (LPSTR)what, r, (LPSTR)(p ? wflags(p) : "-"));
    flush();
}

static void hdrmatrix(HWAVEOUT h, int in)
{
    static const DWORD fl[] = { WHDR_DONE, WHDR_INQUEUE, WHDR_BEGINLOOP | WHDR_ENDLOOP, 0x20,
                                0x100, 0x80000000L, WHDR_PREPARED, WHDR_PREPARED | WHDR_DONE,
                                WHDR_PREPARED | WHDR_INQUEUE, WHDR_PREPARED | 0x100 };
    HDRFN prep = in ? (HDRFN)waveInPrepareHeader : (HDRFN)waveOutPrepareHeader;
    HDRFN unprep = in ? (HDRFN)waveInUnprepareHeader : (HDRFN)waveOutUnprepareHeader;
    HDRFN wr = in ? (HDRFN)waveInAddBuffer : (HDRFN)waveOutWrite;
    char b[60];
    int k;
    UINT r;
    lg("header checks (%s):", (LPSTR)(in ? "waveIn" : "waveOut"));
    sethdr(wh1, data1, 1102, 0, 0);
    hdrtry("prepare(NULL)", prep, h, NULL, sizeof(WAVEHDR));
    hdrtry("unprepare(NULL)", unprep, h, NULL, sizeof(WAVEHDR));
    hdrtry("write(NULL)", wr, h, NULL, sizeof(WAVEHDR));
    hdrtry("prepare(size 0)", prep, h, wh1, 0);
    hdrtry("prepare(size 31)", prep, h, wh1, sizeof(WAVEHDR) - 1);
    hdrtry("prepare(size 33)", prep, h, wh1, sizeof(WAVEHDR) + 1);
    hdrtry("unprepare(size 31)", unprep, h, wh1, sizeof(WAVEHDR) - 1);
    hdrtry("write(size 31)", wr, h, wh1, sizeof(WAVEHDR) - 1);
    hdrtry("write(not prepared)", wr, h, wh1, sizeof(WAVEHDR));
    hdrtry("unprepare(not prepared)", unprep, h, wh1, sizeof(WAVEHDR));
    for (k = 0; k < sizeof fl / sizeof fl[0]; k++) {
        sethdr(wh1, data1, 1102, fl[k], 0);
        wsprintf(b, "prepare(flags %08lX)", fl[k]);
        hdrtry(b, prep, h, wh1, sizeof(WAVEHDR));
        if (wh1->dwFlags & WHDR_PREPARED) {
            wsprintf(b, "unprepare(flags %08lX)", wh1->dwFlags);
            hdrtry(b, unprep, h, wh1, sizeof(WAVEHDR));
        }
        sethdr(wh1, data1, 1102, fl[k], 0);
        wsprintf(b, "unprepare(flags %08lX)", fl[k]);
        hdrtry(b, unprep, h, wh1, sizeof(WAVEHDR));
    }
    sethdr(wh1, NULL, 1102, 0, 0);
    hdrtry("prepare(data NULL)", prep, h, wh1, sizeof(WAVEHDR));
    if (wh1->dwFlags & WHDR_PREPARED)
        hdrtry("unprepare(data NULL)", unprep, h, wh1, sizeof(WAVEHDR));
    sethdr(wh1, data1, 0, 0, 0);
    hdrtry("prepare(length 0)", prep, h, wh1, sizeof(WAVEHDR));
    if (wh1->dwFlags & WHDR_PREPARED)
        hdrtry("unprepare(length 0)", unprep, h, wh1, sizeof(WAVEHDR));
    sethdr(wh1, data1, 1102, 0, 0);
    hdrtry("prepare", prep, h, wh1, sizeof(WAVEHDR));
    hdrtry("prepare again", prep, h, wh1, sizeof(WAVEHDR));
    /* a prepared header with other flags given to write (nothing goes
       out: the header is taken back by a reset) */
    {
        static const DWORD wf[] = { WHDR_INQUEUE, 0x100, WHDR_DONE, 0x80000000L };
        for (k = 0; k < sizeof wf / sizeof wf[0]; k++) {
            wh1->dwFlags = WHDR_PREPARED | wf[k];
            wsprintf(b, "write(flags %08lX)", wh1->dwFlags);
            r = wr(h, wh1, sizeof(WAVEHDR));
            lg("  %s = %u, %s", (LPSTR)b, r, (LPSTR)wflags(wh1));
            flush();
            if (!r) {
                r = in ? waveInReset((HWAVEIN)h) : waveOutReset(h);
                waitfor(&wh1->dwFlags, WHDR_DONE, 3000);
                lg("  reset = %u, %s", r, (LPSTR)wflags(wh1));
                flush();
            }
        }
    }
    wh1->dwFlags = WHDR_PREPARED;
    hdrtry("unprepare", unprep, h, wh1, sizeof(WAVEHDR));
}

static void sec_waveout(void)
{
    HWAVEOUT h, h2;
    UINT r, id;
    DWORD v;
    UINT n = waveOutGetNumDevs();
    int k;
    buffers();
    lg("waveOutGetNumDevs = %u", n);
    flush();
    setfmt(&fmt11, WAVE_FORMAT_PCM, 1, 11025, 8);
    /* bad IDs, bad formats, queries */
    wopen("bad ID n", n, (LPWAVEFORMAT)&fmt11, 0, 0, 0, &h, 0);
    wopen("ID 7FFF", 0x7FFF, (LPWAVEFORMAT)&fmt11, 0, 0, 0, &h, 0);
    wopen("NULL format", 0, NULL, 0, 0, 0, &h, 0);
    {
        static const struct { WORD tag, ch; DWORD rate; WORD bits; } q[] = {
            { 1, 1, 11025, 8 }, { 1, 2, 22050, 8 }, { 1, 1, 44100, 8 }, { 1, 2, 44100, 16 },
            { 1, 1, 8000, 8 }, { 1, 1, 11025, 16 }, { 2, 1, 11025, 4 }, { 1, 3, 11025, 8 },
            { 1, 1, 11025, 12 }, { 1, 1, 0, 8 }, { 1, 1, 100000, 8 },
        };
        char b[60];
        for (k = 0; k < sizeof q / sizeof q[0]; k++) {
            setfmt(&fmtbad, q[k].tag, q[k].ch, q[k].rate, q[k].bits);
            wsprintf(b, "query %u %u %lu %u", q[k].tag, q[k].ch, q[k].rate, q[k].bits);
            wopen(b, 0, (LPWAVEFORMAT)&fmtbad, 0, 0, WAVE_FORMAT_QUERY, &h, 0);
            lstrcat(b, ", mapper");
            wopen(b, WAVE_MAPPER, (LPWAVEFORMAT)&fmtbad, 0, 0, WAVE_FORMAT_QUERY, &h, 0);
        }
        setfmt(&fmtbad, 1, 1, 11025, 8);
        fmtbad.wf.nBlockAlign = 0;
        wopen("query block align 0", 0, (LPWAVEFORMAT)&fmtbad, 0, 0, WAVE_FORMAT_QUERY, &h, 0);
        wopen("query, NULL handle pointer", 0, (LPWAVEFORMAT)&fmt11, 0, 0, WAVE_FORMAT_QUERY, NULL, 0);
        wopen("query, callback function", 0, (LPWAVEFORMAT)&fmt11, (DWORD)dev_cb, 0x5A5A0002L,
              WAVE_FORMAT_QUERY | CALLBACK_FUNCTION, &h, 0);
        wopen("query + allowsync", 0, (LPWAVEFORMAT)&fmt11, 0, 0, WAVE_FORMAT_QUERY | WAVE_ALLOWSYNC, &h, 0);
        wopen("flags 0004", 0, (LPWAVEFORMAT)&fmt11, 0, 0, 4, &h, 0);
        wopen("callback type 4", 0, (LPWAVEFORMAT)&fmt11, 0, 0, 0x00040000L | WAVE_FORMAT_QUERY, &h, 0);
        wopen("callback function NULL", 0, (LPWAVEFORMAT)&fmt11, 0, 0, CALLBACK_FUNCTION | WAVE_FORMAT_QUERY, &h, 0);
        wopen("callback window 0", 0, (LPWAVEFORMAT)&fmt11, 0, 0, CALLBACK_WINDOW | WAVE_FORMAT_QUERY, &h, 0);
        wopen("callback window 1234", 0, (LPWAVEFORMAT)&fmt11, 0x1234, 0, CALLBACK_WINDOW | WAVE_FORMAT_QUERY, &h, 0);
        wopen("callback function in data", 0, (LPWAVEFORMAT)&fmt11, (DWORD)(LPVOID)line, 0,
              CALLBACK_FUNCTION | WAVE_FORMAT_QUERY, &h, 0);
    }
    if (!n) {
        /* functions on handles with no device */
        lg("waveOutClose(1234) = %u", waveOutClose((HWAVEOUT)0x1234));
        lg("waveOutGetVolume(0) = %u", waveOutGetVolume(0, &v));
        return;
    }
    /* a callback function */
    wopen("function", 0, (LPWAVEFORMAT)&fmt11, (DWORD)dev_cb, 0x5A5A0001L, CALLBACK_FUNCTION, &h, "hwo");
    if (h == (HWAVEOUT)0xEEEE)
        return;
    wopen("again while open", 0, (LPWAVEFORMAT)&fmt11, 0, 0, 0, &h2, "hwo2");
    if (!waveOutGetID(h2, &id) && h2 != h)
        lg("waveOutClose(second) = %u", waveOutClose(h2)), flush();
    id = 0xEEEE;
    r = waveOutGetID(h, &id);
    lg("waveOutGetID = %u, %04X", r, id);
    r = waveOutGetID(h, NULL);
    lg("waveOutGetID(NULL) = %u", r);
    id = 0xEEEE;
    r = waveOutGetID((HWAVEOUT)0x1234, &id);
    lg("waveOutGetID(1234) = %u, %04X", r, id);
    r = waveOutGetID(0, &id);
    lg("waveOutGetID(0) = %u", r);
    r = waveOutGetID((HWAVEOUT)dev_hwnd, &id);
    lg("waveOutGetID(a window) = %u", r);
    flush();
    hdrmatrix(h, 0);
    sethdr(wh1, data1, 2205, 0, 0);
    sethdr(wh2, data2, 1103, 0, 0);
    r = waveOutPrepareHeader(h, wh1, sizeof(WAVEHDR));
    lg("waveOutPrepareHeader(1) = %u, %s", r, (LPSTR)wflags(wh1));
    r = waveOutPrepareHeader(h, wh2, sizeof(WAVEHDR));
    lg("waveOutPrepareHeader(2) = %u, %s", r, (LPSTR)wflags(wh2));
    flush();
    r = waveOutWrite(h, wh1, sizeof(WAVEHDR) - 1);
    lg("waveOutWrite(size 31) = %u, %s", r, (LPSTR)wflags(wh1));
    flush();
    r = waveOutWrite(h, NULL, sizeof(WAVEHDR));
    lg("waveOutWrite(NULL) = %u", r);
    flush();
    /* play both */
    r = waveOutWrite(h, wh1, sizeof(WAVEHDR));
    lg("waveOutWrite(1) = %u, %s", r, (LPSTR)wflags(wh1));
    r = waveOutWrite(h, wh2, sizeof(WAVEHDR));
    lg("waveOutWrite(2) = %u, %s", r, (LPSTR)wflags(wh2));
    r = waveOutUnprepareHeader(h, wh2, sizeof(WAVEHDR));
    lg("waveOutUnprepareHeader(2, queued) = %u, %s", r, (LPSTR)wflags(wh2));
    r = waveOutWrite(h, wh1, sizeof(WAVEHDR));
    lg("waveOutWrite(1 again, queued) = %u, %s", r, (LPSTR)wflags(wh1));
    flush();
    waitfor(&wh2->dwFlags, WHDR_DONE, 3000);
    lg("played: 1 %s; 2 %s", (LPSTR)wflags(wh1), (LPSTR)wflags(wh2));
    flush();
    wpos(h, 0);
    /* pause, restart, reset */
    r = waveOutPause(h);
    lg("waveOutPause = %u", r);
    flush();
    r = waveOutWrite(h, wh1, sizeof(WAVEHDR));
    lg("waveOutWrite(paused) = %u, %s", r, (LPSTR)wflags(wh1));
    flush();
    pause(300);
    lg("after 300 ms paused: %s", (LPSTR)wflags(wh1));
    r = waveOutPause(h);
    lg("waveOutPause again = %u", r);
    flush();
    r = waveOutRestart(h);
    lg("waveOutRestart = %u", r);
    flush();
    waitfor(&wh1->dwFlags, WHDR_DONE, 3000);
    lg("played: %s", (LPSTR)wflags(wh1));
    flush();
    r = waveOutRestart(h);
    lg("waveOutRestart (not paused) = %u", r);
    flush();
    r = waveOutWrite(h, wh1, sizeof(WAVEHDR));
    r = waveOutWrite(h, wh2, sizeof(WAVEHDR));
    lg("waveOutWrite(1, 2): %s; %s", (LPSTR)wflags(wh1), (LPSTR)wflags(wh2));
    r = waveOutReset(h);
    lg("waveOutReset (playing) = %u: %s; %s", r, (LPSTR)wflags(wh1), (LPSTR)wflags(wh2));
    flush();
    wpos(h, 0);
    r = waveOutReset(h);
    lg("waveOutReset (idle) = %u", r);
    flush();
    r = waveOutBreakLoop(h);
    lg("waveOutBreakLoop (idle) = %u", r);
    flush();
    /* loops */
    sethdr(wh1, data2, 1103, WHDR_BEGINLOOP | WHDR_ENDLOOP, 3);
    r = waveOutPrepareHeader(h, wh1, sizeof(WAVEHDR));
    lg("waveOutPrepareHeader(loop 3) = %u, %s", r, (LPSTR)wflags(wh1));
    r = waveOutWrite(h, wh1, sizeof(WAVEHDR));
    lg("waveOutWrite(loop 3) = %u, %s", r, (LPSTR)wflags(wh1));
    flush();
    waitfor(&wh1->dwFlags, WHDR_DONE, 3000);
    lg("played: %s", (LPSTR)wflags(wh1));
    flush();
    wh1->dwLoops = 1000;
    r = waveOutWrite(h, wh1, sizeof(WAVEHDR));
    lg("waveOutWrite(loop 1000) = %u, %s", r, (LPSTR)wflags(wh1));
    flush();
    pause(200);
    r = waveOutBreakLoop(h);
    lg("waveOutBreakLoop = %u", r);
    flush();
    waitfor(&wh1->dwFlags, WHDR_DONE, 3000);
    lg("played: %s", (LPSTR)wflags(wh1));
    flush();
    r = waveOutUnprepareHeader(h, wh1, sizeof(WAVEHDR));
    lg("waveOutUnprepareHeader(1) = %u, %s", r, (LPSTR)wflags(wh1));
    r = waveOutUnprepareHeader(h, wh2, sizeof(WAVEHDR));
    lg("waveOutUnprepareHeader(2) = %u, %s", r, (LPSTR)wflags(wh2));
    flush();
    /* volume, pitch, rate */
    v = 0xEEEEEEEEL;
    r = waveOutGetVolume(0, &v);
    lg("waveOutGetVolume(0) = %u, %08lX", r, v);
    flush();
    r = waveOutSetVolume(0, 0x80004000L);
    lg("waveOutSetVolume(0, 80004000) = %u", r);
    flush();
    v = 0xEEEEEEEEL;
    r = waveOutGetVolume(0, &v);
    lg("waveOutGetVolume(0) = %u, %08lX", r, v);
    flush();
    v = 0xEEEEEEEEL;
    r = waveOutGetVolume((UINT)h, &v);
    lg("waveOutGetVolume(the handle) = %u, %08lX", r, v);
    flush();
    r = waveOutSetVolume((UINT)h, 0xFFFFFFFFL);
    lg("waveOutSetVolume(the handle, FFFFFFFF) = %u", r);
    flush();
    r = waveOutGetVolume(n, &v);
    lg("waveOutGetVolume(n) = %u", r);
    r = waveOutGetVolume(0, NULL);
    lg("waveOutGetVolume(0, NULL) = %u", r);
    r = waveOutSetVolume(n, 0);
    lg("waveOutSetVolume(n) = %u", r);
    r = waveOutSetVolume(0xFFFF, 0);
    lg("waveOutSetVolume(FFFF) = %u", r);
    flush();
    v = 0xEEEEEEEEL;
    r = waveOutGetPitch(h, &v);
    lg("waveOutGetPitch = %u, %08lX", r, v);
    r = waveOutSetPitch(h, 0x00020000L);
    lg("waveOutSetPitch(2.0) = %u", r);
    v = 0xEEEEEEEEL;
    r = waveOutGetPlaybackRate(h, &v);
    lg("waveOutGetPlaybackRate = %u, %08lX", r, v);
    r = waveOutSetPlaybackRate(h, 0x00008000L);
    lg("waveOutSetPlaybackRate(0.5) = %u", r);
    r = waveOutGetPitch(h, NULL);
    lg("waveOutGetPitch(NULL) = %u", r);
    flush();
    /* Message */
    lg("waveOutMessage(h, 4000, 1, 2) = %08lX", waveOutMessage(h, 0x4000, 1, 2));
    flush();
    lg("waveOutMessage(h, 0003, 0, 0) = %08lX", waveOutMessage(h, 3, 0, 0));
    flush();
    lg("waveOutMessage(h, 0010, &v, 0) = %08lX", waveOutMessage(h, 16, (DWORD)(LPVOID)&v, 0));
    flush();
    lg("waveOutMessage(1234, 4000, 1, 2) = %08lX", waveOutMessage((HWAVEOUT)0x1234, 0x4000, 1, 2));
    lg("waveOutMessage(0, 4000, 1, 2) = %08lX", waveOutMessage(0, 0x4000, 1, 2));
    lg("waveOutMessage(ID 0, 4000) = %08lX", waveOutMessage((HWAVEOUT)0, 0x4000, 1, 2));
    flush();
    r = waveOutClose(h);
    lg("waveOutClose = %u", r);
    flush();
    /* the stale handle */
    lg("stale: Close %u Reset %u Pause %u Restart %u BreakLoop %u Write %u Prepare %u GetID %u",
       waveOutClose(h), waveOutReset(h), waveOutPause(h), waveOutRestart(h), waveOutBreakLoop(h),
       waveOutWrite(h, wh1, sizeof(WAVEHDR)), waveOutPrepareHeader(h, wh1, sizeof(WAVEHDR)),
       waveOutGetID(h, &id));
    flush();
    lg("handle 0: Close %u Reset %u Pause %u Write %u GetPitch %u", waveOutClose(0), waveOutReset(0),
       waveOutPause(0), waveOutWrite(0, wh1, sizeof(WAVEHDR)), waveOutGetPitch(0, &v));
    lg("a window as handle: Close %u Reset %u", waveOutClose((HWAVEOUT)dev_hwnd),
       waveOutReset((HWAVEOUT)dev_hwnd));
    flush();
    /* a window callback, a task callback, none, the mapper, sync */
    wopen("window", 0, (LPWAVEFORMAT)&fmt11, (DWORD)(WORD)dev_hwnd, 0x5A5A0003L, CALLBACK_WINDOW, &h, "hwo");
    if (!waveOutGetID(h, &id)) {
        sethdr(wh1, data2, 1103, 0, 0);
        waveOutPrepareHeader(h, wh1, sizeof(WAVEHDR));
        r = waveOutWrite(h, wh1, sizeof(WAVEHDR));
        lg("waveOutWrite = %u", r);
        flush();
        waitfor(&wh1->dwFlags, WHDR_DONE, 3000);
        pause(50);
        lg("played: %s", (LPSTR)wflags(wh1));
        flush();
        waveOutUnprepareHeader(h, wh1, sizeof(WAVEHDR));
        lg("waveOutClose = %u", waveOutClose(h));
        flush();
    }
    wopen("task", 0, (LPWAVEFORMAT)&fmt11, (DWORD)(WORD)GetCurrentTask(), 0x5A5A0004L, CALLBACK_TASK, &h, "hwo");
    if (!waveOutGetID(h, &id)) {
        lg("waveOutClose = %u", waveOutClose(h));
        flush();
    }
    wopen("none", 0, (LPWAVEFORMAT)&fmt11, 0, 0x5A5A0005L, CALLBACK_NULL, &h, "hwo");
    if (!waveOutGetID(h, &id)) {
        { UINT gr = waveOutGetID(h, &id); lg("waveOutGetID = %u, %04X", gr, id); }
        lg("waveOutClose = %u", waveOutClose(h));
        flush();
    }
    wopen("mapper, function", WAVE_MAPPER, (LPWAVEFORMAT)&fmt11, (DWORD)dev_cb, 0x5A5A0006L, CALLBACK_FUNCTION, &h, "hwo");
    if (!waveOutGetID(h, &id)) {
        id = 0xEEEE;
        { UINT gr = waveOutGetID(h, &id); lg("waveOutGetID = %u, %04X", gr, id); }
        lg("waveOutClose = %u", waveOutClose(h));
        flush();
    }
    wopen("allowsync", 0, (LPWAVEFORMAT)&fmt11, 0, 0, WAVE_ALLOWSYNC, &h, "hwo");
    if (!waveOutGetID(h, &id)) {
        lg("waveOutClose = %u", waveOutClose(h));
        flush();
    }
    setfmt(&fmtbad, 1, 2, 44100, 16);
    wopen("44100 16 stereo", 0, (LPWAVEFORMAT)&fmtbad, 0, 0, 0, &h, "hwo");
    if (!waveOutGetID(h, &id)) {
        lg("waveOutClose = %u", waveOutClose(h));
        flush();
    }
    wopen("44100 16 stereo, mapper", WAVE_MAPPER, (LPWAVEFORMAT)&fmtbad, 0, 0, 0, &h, "hwo");
    if (!waveOutGetID(h, &id)) {
        lg("waveOutClose = %u", waveOutClose(h));
        flush();
    }
    /* closing while a buffer plays */
    wopen("function", 0, (LPWAVEFORMAT)&fmt11, (DWORD)dev_cb, 0x5A5A0007L, CALLBACK_FUNCTION, &h, "hwo");
    if (!waveOutGetID(h, &id)) {
        sethdr(wh1, data1, 2205, 0, 0);
        waveOutPrepareHeader(h, wh1, sizeof(WAVEHDR));
        r = waveOutWrite(h, wh1, sizeof(WAVEHDR));
        lg("waveOutWrite = %u", r);
        r = waveOutClose(h);
        lg("waveOutClose(playing) = %u, %s", r, (LPSTR)wflags(wh1));
        flush();
        if (r) {
            lg("waveOutReset = %u", waveOutReset(h));
            lg("waveOutUnprepareHeader = %u", waveOutUnprepareHeader(h, wh1, sizeof(WAVEHDR)));
            lg("waveOutClose = %u", waveOutClose(h));
        } else {
            waitfor(&wh1->dwFlags, WHDR_DONE, 3000);
            lg("after: %s", (LPSTR)wflags(wh1));
        }
        flush();
    }
    /* last: the device stays open, the handle nowhere */
    wopen("NULL handle pointer", 0, (LPWAVEFORMAT)&fmt11, 0, 0, 0, NULL, 0);
}

/* ---- i: wave input ---- */
static void sec_wavein(void)
{
    HWAVEIN h;
    UINT r, id;
    UINT n = waveInGetNumDevs();
    buffers();
    lg("waveInGetNumDevs = %u", n);
    flush();
    setfmt(&fmt11, WAVE_FORMAT_PCM, 1, 11025, 8);
    h = (HWAVEIN)0xEEEE;
    r = waveInOpen(&h, n, (LPWAVEFORMAT)&fmt11, 0, 0, 0);
    lg("waveInOpen(bad ID) = %u", r);
    r = waveInOpen(&h, 0, (LPWAVEFORMAT)&fmt11, 0, 0, WAVE_FORMAT_QUERY);
    lg("waveInOpen(query) = %u", r);
    r = waveInOpen(&h, WAVE_MAPPER, (LPWAVEFORMAT)&fmt11, 0, 0, WAVE_FORMAT_QUERY);
    lg("waveInOpen(query, mapper) = %u", r);
    flush();
    if (!n)
        return;
    h = (HWAVEIN)0xEEEE;
    r = waveInOpen(&h, 0, (LPWAVEFORMAT)&fmt11, (DWORD)dev_cb, 0x5A5A0011L, CALLBACK_FUNCTION);
    if (!r)
        name((DWORD)(WORD)h, "hwi");
    lg("waveInOpen(function) = %u, %s", r, (LPSTR)hs((WORD)h));
    flush();
    if (r)
        return;
    id = 0xEEEE;
    { UINT gr = waveInGetID(h, &id); lg("waveInGetID = %u, %04X", gr, id); }
    hdrmatrix((HWAVEOUT)h, 1);
    sethdr(wh1, data1, 1102, 0, 0);
    sethdr(wh2, data2, 1103, 0, 0);
    r = waveInAddBuffer(h, wh1, sizeof(WAVEHDR));
    lg("waveInAddBuffer(not prepared) = %u, %s", r, (LPSTR)wflags(wh1));
    flush();
    r = waveInPrepareHeader(h, wh1, sizeof(WAVEHDR));
    lg("waveInPrepareHeader(1) = %u, %s", r, (LPSTR)wflags(wh1));
    r = waveInPrepareHeader(h, wh2, sizeof(WAVEHDR));
    lg("waveInPrepareHeader(2) = %u, %s", r, (LPSTR)wflags(wh2));
    flush();
    r = waveInAddBuffer(h, wh1, sizeof(WAVEHDR));
    lg("waveInAddBuffer(1) = %u, %s", r, (LPSTR)wflags(wh1));
    r = waveInAddBuffer(h, wh2, sizeof(WAVEHDR));
    lg("waveInAddBuffer(2) = %u, %s", r, (LPSTR)wflags(wh2));
    r = waveInUnprepareHeader(h, wh1, sizeof(WAVEHDR));
    lg("waveInUnprepareHeader(1, queued) = %u, %s", r, (LPSTR)wflags(wh1));
    flush();
    r = waveInStart(h);
    lg("waveInStart = %u", r);
    flush();
    waitfor(&wh2->dwFlags, WHDR_DONE, 3000);
    lg("recorded: 1 %s; 2 %s", (LPSTR)wflags(wh1), (LPSTR)wflags(wh2));
    flush();
    wpos((HWAVEOUT)h, 1);
    r = waveInStop(h);
    lg("waveInStop = %u", r);
    flush();
    r = waveInAddBuffer(h, wh1, sizeof(WAVEHDR));
    lg("waveInAddBuffer(1) = %u, %s", r, (LPSTR)wflags(wh1));
    flush();
    r = waveInClose(h);
    lg("waveInClose(buffer queued) = %u", r);
    flush();
    r = waveInReset(h);
    lg("waveInReset = %u, %s", r, (LPSTR)wflags(wh1));
    flush();
    wpos((HWAVEOUT)h, 1);
    r = waveInStart(h);
    lg("waveInStart (no buffers) = %u", r);
    flush();
    r = waveInStop(h);
    lg("waveInStop = %u", r);
    flush();
    r = waveInUnprepareHeader(h, wh1, sizeof(WAVEHDR));
    lg("waveInUnprepareHeader(1) = %u, %s", r, (LPSTR)wflags(wh1));
    r = waveInUnprepareHeader(h, wh2, sizeof(WAVEHDR));
    lg("waveInUnprepareHeader(2) = %u, %s", r, (LPSTR)wflags(wh2));
    flush();
    lg("waveInMessage(h, 4000, 1, 2) = %08lX", waveInMessage(h, 0x4000, 1, 2));
    flush();
    r = waveInClose(h);
    lg("waveInClose = %u", r);
    flush();
    lg("stale: Close %u Reset %u Start %u Stop %u AddBuffer %u GetID %u", waveInClose(h),
       waveInReset(h), waveInStart(h), waveInStop(h), waveInAddBuffer(h, wh1, sizeof(WAVEHDR)),
       waveInGetID(h, &id));
    flush();
    /* a wave output handle given to the input functions */
    {
        HWAVEOUT ho;
        if (!waveOutOpen(&ho, 0, (LPWAVEFORMAT)&fmt11, 0, 0, 0)) {
            name((DWORD)(WORD)ho, "hwo");
            lg("an output handle: waveInClose %u waveInReset %u waveInGetID %u",
               waveInClose((HWAVEIN)ho), waveInReset((HWAVEIN)ho), waveInGetID((HWAVEIN)ho, &id));
            lg("waveOutClose = %u", waveOutClose(ho));
            flush();
        }
    }
}

/* ---- m: MIDI output ---- */
static MIDIHDR FAR *mh;

static const char *mflags(MIDIHDR FAR *h)
{
    static char b[2][60];
    static int k;
    char *r = b[k++ & 1];
    wsprintf(r, "flags %04lX rec %ld next %s res %s", h->dwFlags, h->dwBytesRecorded,
             (LPSTR)vs((DWORD)h->lpNext), (LPSTR)vs(h->reserved));
    return r;
}

static void midiout(UINT id, const char *what)
{
    static BYTE sysex[] = { 0xF0, 0x7E, 0x7F, 0x09, 0x01, 0xF7 };
    static WORD patches[128], keys[128];
    HMIDIOUT h = (HMIDIOUT)0xEEEE;
    UINT r, gid;
    DWORD v;
    r = midiOutOpen(&h, id, (DWORD)dev_cb, 0x5A5A0021L, CALLBACK_FUNCTION);
    if (!r)
        name((DWORD)(WORD)h, "hmo");
    lg("midiOutOpen(%s, function) = %u, %s", (LPSTR)what, r, (LPSTR)hs((WORD)h));
    flush();
    if (r)
        return;
    gid = 0xEEEE;
    { UINT gr = midiOutGetID(h, &gid); lg("midiOutGetID = %u, %04X", gr, gid); }
    flush();
    r = midiOutShortMsg(h, 0x007F3C90L);
    lg("midiOutShortMsg(note on) = %u", r);
    flush();
    pause(100);
    r = midiOutShortMsg(h, 0x00003C80L);
    lg("midiOutShortMsg(note off) = %u", r);
    r = midiOutShortMsg(h, 0x00004090L);
    lg("midiOutShortMsg(note on, velocity 0) = %u", r);
    r = midiOutShortMsg(h, 0x00000040L);
    lg("midiOutShortMsg(running status 40 00) = %u", r);
    r = midiOutShortMsg(h, 0x000000F8L);
    lg("midiOutShortMsg(F8) = %u", r);
    flush();
    mh = (MIDIHDR FAR *)wh1;
    mh->lpData = (LPSTR)sysex;
    mh->dwBufferLength = sizeof sysex;
    mh->dwBytesRecorded = 0;
    mh->dwUser = 0x0BADC0DEL;
    mh->dwFlags = 0;
    mh->lpNext = NULL;
    mh->reserved = 0;
    name((DWORD)(LPVOID)sysex, "sysex");
    r = midiOutLongMsg(h, mh, sizeof(MIDIHDR));
    lg("midiOutLongMsg(not prepared) = %u, %s", r, (LPSTR)mflags(mh));
    flush();
    r = midiOutPrepareHeader(h, mh, sizeof(MIDIHDR) - 1);
    lg("midiOutPrepareHeader(size 27) = %u, %s", r, (LPSTR)mflags(mh));
    r = midiOutPrepareHeader(h, NULL, sizeof(MIDIHDR));
    lg("midiOutPrepareHeader(NULL) = %u", r);
    r = midiOutPrepareHeader(h, mh, sizeof(MIDIHDR));
    lg("midiOutPrepareHeader = %u, %s", r, (LPSTR)mflags(mh));
    flush();
    r = midiOutLongMsg(h, mh, sizeof(MIDIHDR));
    lg("midiOutLongMsg = %u, %s", r, (LPSTR)mflags(mh));
    flush();
    waitfor(&mh->dwFlags, MHDR_DONE, 2000);
    lg("sent: %s", (LPSTR)mflags(mh));
    flush();
    r = midiOutUnprepareHeader(h, mh, sizeof(MIDIHDR));
    lg("midiOutUnprepareHeader = %u, %s", r, (LPSTR)mflags(mh));
    r = midiOutUnprepareHeader(h, mh, sizeof(MIDIHDR));
    lg("midiOutUnprepareHeader again = %u, %s", r, (LPSTR)mflags(mh));
    flush();
    r = midiOutReset(h);
    lg("midiOutReset = %u", r);
    flush();
    v = 0xEEEEEEEEL;
    r = midiOutGetVolume(id, &v);
    lg("midiOutGetVolume(ID) = %u, %08lX", r, v);
    flush();
    r = midiOutSetVolume(id, 0x80008000L);
    lg("midiOutSetVolume(ID) = %u", r);
    flush();
    v = 0xEEEEEEEEL;
    r = midiOutGetVolume((UINT)h, &v);
    lg("midiOutGetVolume(handle) = %u, %08lX", r, v);
    flush();
    fill(patches, sizeof patches, 0);
    patches[0] = 0x0003;
    patches[1] = 0x0001;
    r = midiOutCachePatches(h, 0, patches, MIDI_CACHE_QUERY);
    lg("midiOutCachePatches(query) = %u, %04X %04X", r, patches[0], patches[1]);
    flush();
    r = midiOutCachePatches(h, 0, patches, MIDI_CACHE_ALL);
    lg("midiOutCachePatches(all) = %u", r);
    flush();
    r = midiOutCacheDrumPatches(h, 0, keys, MIDI_CACHE_ALL);
    lg("midiOutCacheDrumPatches(all) = %u", r);
    flush();
    r = midiOutCachePatches(h, 0, NULL, MIDI_CACHE_ALL);
    lg("midiOutCachePatches(NULL) = %u", r);
    r = midiOutCachePatches(h, 0, patches, 7);
    lg("midiOutCachePatches(flags 7) = %u", r);
    flush();
    lg("midiOutMessage(h, 4000, 1, 2) = %08lX", midiOutMessage(h, 0x4000, 1, 2));
    flush();
    r = midiOutClose(h);
    lg("midiOutClose = %u", r);
    flush();
    lg("stale: Close %u Reset %u ShortMsg %u GetID %u", midiOutClose(h), midiOutReset(h),
       midiOutShortMsg(h, 0x90), midiOutGetID(h, &gid));
    flush();
}

static void sec_midiout(void)
{
    UINT n = midiOutGetNumDevs(), r, k;
    HMIDIOUT h, h2;
    buffers();
    lg("midiOutGetNumDevs = %u", n);
    flush();
    h = (HMIDIOUT)0xEEEE;
    r = midiOutOpen(&h, n, 0, 0, 0);
    lg("midiOutOpen(bad ID) = %u", r);
    r = midiOutOpen(&h, 0, 0, 0, 0x00040000L);
    lg("midiOutOpen(callback type 4) = %u", r);
    r = midiOutOpen(&h, 0, 0, 0, WAVE_FORMAT_QUERY);
    lg("midiOutOpen(flag 1) = %u, %s", r, (LPSTR)hs((WORD)h));
    if (!r)
        lg("  closed: %u", midiOutClose(h));
    flush();
    for (k = 0; k < n; k++) {
        char b[20];
        wsprintf(b, "ID %u", k);
        midiout(k, b);
    }
    midiout(MIDI_MAPPER, "mapper");
    /* two opens at once */
    r = midiOutOpen(&h, MIDI_MAPPER, 0, 0, 0);
    if (!r)
        name((DWORD)(WORD)h, "hmo");
    lg("midiOutOpen(mapper) = %u, %s", r, (LPSTR)hs((WORD)h));
    flush();
    if (n) {
        r = midiOutOpen(&h2, 0, 0, 0, 0);
        if (!r)
            name((DWORD)(WORD)h2, "hmo2");
        lg("midiOutOpen(0 while the mapper has it) = %u, %s", r, (LPSTR)hs((WORD)h2));
        flush();
        if (!r)
            lg("midiOutClose(0) = %u", midiOutClose(h2));
    }
    if (!midiOutGetID(h, &k))
        lg("midiOutClose(mapper) = %u", midiOutClose(h));
    flush();
    r = midiOutOpen(&h, MIDI_MAPPER, (DWORD)(WORD)dev_hwnd, 0x5A5A0022L, CALLBACK_WINDOW);
    if (!r)
        name((DWORD)(WORD)h, "hmo");
    lg("midiOutOpen(mapper, window) = %u, %s", r, (LPSTR)hs((WORD)h));
    flush();
    if (!r) {
        lg("midiOutClose = %u", midiOutClose(h));
        flush();
    }
    r = midiOutOpen(&h, MIDI_MAPPER, (DWORD)(WORD)GetCurrentTask(), 0x5A5A0023L, CALLBACK_TASK);
    if (!r)
        name((DWORD)(WORD)h, "hmo");
    lg("midiOutOpen(mapper, task) = %u, %s", r, (LPSTR)hs((WORD)h));
    flush();
    if (!r) {
        lg("midiOutClose = %u", midiOutClose(h));
        flush();
    }
    {
        MIDIOUTCAPS c;
        fill(&c, sizeof c, 0xEE);
        r = midiOutGetDevCaps(MIDI_MAPPER, &c, sizeof c);
        lg("midiOutGetDevCaps(mapper) = %u, \"%s\"", r, (LPSTR)(r ? "" : c.szPname));
        flush();
    }
    /* last: the device stays open, the handle nowhere */
    r = midiOutOpen(NULL, 0, 0, 0, 0);
    lg("midiOutOpen(NULL handle pointer) = %u", r);
    flush();
}

/* ---- j: MIDI input ---- */
static void sec_midiin(void)
{
    static char buf[64];
    UINT n = midiInGetNumDevs(), r, id;
    HMIDIIN h = (HMIDIIN)0xEEEE;
    buffers();
    lg("midiInGetNumDevs = %u", n);
    flush();
    r = midiInOpen(&h, n, 0, 0, 0);
    lg("midiInOpen(bad ID) = %u", r);
    r = midiInOpen(&h, MIDI_MAPPER, 0, 0, 0);
    lg("midiInOpen(mapper ID) = %u", r);
    flush();
    if (!n)
        return;
    r = midiInOpen(&h, 0, (DWORD)dev_cb, 0x5A5A0031L, CALLBACK_FUNCTION);
    if (!r)
        name((DWORD)(WORD)h, "hmi");
    lg("midiInOpen(function) = %u, %s", r, (LPSTR)hs((WORD)h));
    flush();
    if (r)
        return;
    id = 0xEEEE;
    { UINT gr = midiInGetID(h, &id); lg("midiInGetID = %u, %04X", gr, id); }
    mh = (MIDIHDR FAR *)wh1;
    mh->lpData = buf;
    mh->dwBufferLength = sizeof buf;
    mh->dwBytesRecorded = 0;
    mh->dwUser = 0x0BADC0DEL;
    mh->dwFlags = 0;
    mh->lpNext = NULL;
    mh->reserved = 0;
    name((DWORD)(LPVOID)buf, "inbuf");
    r = midiInAddBuffer(h, mh, sizeof(MIDIHDR));
    lg("midiInAddBuffer(not prepared) = %u", r);
    r = midiInPrepareHeader(h, mh, sizeof(MIDIHDR));
    lg("midiInPrepareHeader = %u, %s", r, (LPSTR)mflags(mh));
    r = midiInAddBuffer(h, mh, sizeof(MIDIHDR));
    lg("midiInAddBuffer = %u, %s", r, (LPSTR)mflags(mh));
    flush();
    r = midiInStart(h);
    lg("midiInStart = %u", r);
    flush();
    pause(200);
    r = midiInStop(h);
    lg("midiInStop = %u, %s", r, (LPSTR)mflags(mh));
    flush();
    r = midiInClose(h);
    lg("midiInClose(buffer queued) = %u", r);
    flush();
    r = midiInReset(h);
    lg("midiInReset = %u, %s", r, (LPSTR)mflags(mh));
    flush();
    r = midiInUnprepareHeader(h, mh, sizeof(MIDIHDR));
    lg("midiInUnprepareHeader = %u, %s", r, (LPSTR)mflags(mh));
    lg("midiInMessage(h, 4000, 1, 2) = %08lX", midiInMessage(h, 0x4000, 1, 2));
    flush();
    r = midiInClose(h);
    lg("midiInClose = %u", r);
    flush();
    lg("stale: Close %u Reset %u Start %u", midiInClose(h), midiInReset(h), midiInStart(h));
    flush();
}

/* ---- a: auxiliary ---- */
static void sec_aux(void)
{
    UINT n = auxGetNumDevs(), r, k;
    DWORD v;
    lg("auxGetNumDevs = %u", n);
    flush();
    for (k = 0; k <= n && k < 4; k++) {
        v = 0xEEEEEEEEL;
        r = auxGetVolume(k, &v);
        lg("auxGetVolume(%u) = %u, %08lX", k, r, v);
        flush();
        r = auxSetVolume(k, 0x40008000L);
        lg("auxSetVolume(%u, 40008000) = %u", k, r);
        flush();
        v = 0xEEEEEEEEL;
        r = auxGetVolume(k, &v);
        lg("auxGetVolume(%u) = %u, %08lX", k, r, v);
        flush();
        lg("auxOutMessage(%u, 4000, 1, 2) = %08lX", k, auxOutMessage(k, 0x4000, 1, 2));
        flush();
    }
    lg("auxGetVolume(0, NULL) = %u", auxGetVolume(0, NULL));
    lg("auxGetVolume(FFFF) = %u", auxGetVolume(0xFFFF, &v));
    lg("auxOutMessage(FFFF, 4000) = %08lX", auxOutMessage(0xFFFF, 0x4000, 1, 2));
    flush();
}

/* ---- y: joystick ---- */
static void sec_joy(void)
{
    JOYINFO ji;
    UINT r, k, t;
    lg("joyGetNumDevs = %u", joyGetNumDevs());
    flush();
    for (k = 0; k < 3; k++) {
        fill(&ji, sizeof ji, 0xEE);
        r = joyGetPos(k, &ji);
        lg("joyGetPos(%u) = %u, %04X %04X %04X %04X", k, r, ji.wXpos, ji.wYpos, ji.wZpos, ji.wButtons);
        t = 0xEEEE;
        r = joyGetThreshold(k, &t);
        lg("joyGetThreshold(%u) = %u, %04X", k, r, t);
        r = joySetThreshold(k, 100);
        lg("joySetThreshold(%u, 100) = %u", k, r);
        t = 0xEEEE;
        r = joyGetThreshold(k, &t);
        lg("joyGetThreshold(%u) = %u, %04X", k, r, t);
        r = joySetCapture(dev_hwnd, k, 100, FALSE);
        lg("joySetCapture(%u) = %u", k, r);
        r = joyReleaseCapture(k);
        lg("joyReleaseCapture(%u) = %u", k, r);
        flush();
    }
    lg("joyGetPos(0, NULL) = %u", joyGetPos(0, NULL));
    lg("joySetCapture(NULL window) = %u", joySetCapture(0, 0, 100, FALSE));
    lg("joySetCapture(bad window) = %u", joySetCapture((HWND)0x1234, 0, 100, FALSE));
    flush();
}

/* ---- x: sndPlaySound ---- */
static void snd(const char *what, LPCSTR s, UINT f)
{
    DWORD t = GetTickCount();
    BOOL r = sndPlaySound(s, f);
    t = GetTickCount() - t;
    lg("sndPlaySound(%s, %04X) = %d", (LPSTR)what, f, r);
    lg("~ took %lu ms", t);
    flush();
}

/* the windows MMSYSTEM owns: its own window for sounds? */
extern WORD FAR PASCAL K_GETEXEPTR(WORD h);     /* KERNEL.133 */

BOOL FAR PASCAL __export mmwin(HWND h, LPARAM l)
{
    char c[40];
    WORD hi = GetWindowWord(h, GWW_HINSTANCE);
    (void)l;
    if (hi && K_GETEXEPTR(hi) == (WORD)GetModuleHandle("MMSYSTEM")) {
        GetClassName(h, c, sizeof c);
        lg("  window: class \"%s\", MMSYSTEM's, %s", (LPSTR)c,
           (LPSTR)(IsWindowVisible(h) ? "visible" : "hidden"));
        mmwnd = h;
        name((DWORD)(WORD)h, "mmwnd");
    }
    return TRUE;
}

void windows(void)
{
    FARPROC f = MakeProcInstance((FARPROC)mmwin, 0);
    EnumWindows((WNDENUMPROC)f, 0);
    FreeProcInstance(f);
}

static void sec_snd(void)
{
    HGLOBAL hm;
    LPSTR pm;
    HFILE fh;
    long len;
    lg("waveOutGetNumDevs = %u", waveOutGetNumDevs());
    windows();
    flush();
    snd("NULL", NULL, 0);
    windows();
    snd("SystemDefault, sync", "SystemDefault", SND_SYNC);
    windows();
    snd("SystemAsterisk, sync", "SystemAsterisk", SND_SYNC);
    snd("systemasterisk (case), sync", "systemasterisk", SND_SYNC);
    snd("NoSuchSound, sync", "NoSuchSound", SND_SYNC);
    snd("NoSuchSound, nodefault", "NoSuchSound", SND_SYNC | SND_NODEFAULT);
    snd("C:\\WINDOWS\\CHIMES.WAV, sync", "C:\\WINDOWS\\CHIMES.WAV", SND_SYNC);
    snd("chimes.wav (no path), sync", "chimes.wav", SND_SYNC);
    snd("tada (no extension), nodefault", "tada", SND_SYNC | SND_NODEFAULT);
    snd("C:\\NOFILE.WAV, nodefault", "C:\\NOFILE.WAV", SND_SYNC | SND_NODEFAULT);
    snd("C:\\WINDOWS\\WIN.INI (not a wave), nodefault", "C:\\WINDOWS\\WIN.INI", SND_SYNC | SND_NODEFAULT);
    snd("C:\\WINDOWS\\WIN.INI (not a wave)", "C:\\WINDOWS\\WIN.INI", SND_SYNC);
    snd("MmpBad (a [sounds] key, missing file), nodefault", "MmpBad", SND_SYNC | SND_NODEFAULT);
    snd("MmpBad (a [sounds] key, missing file)", "MmpBad", SND_SYNC);
    snd("MmpNoComma ([sounds] key, no comma)", "MmpNoComma", SND_SYNC | SND_NODEFAULT);
    snd("MmpSpaces ([sounds] key, spaces)", "MmpSpaces", SND_SYNC | SND_NODEFAULT);
    snd("MmpEmpty ([sounds] key, empty)", "MmpEmpty", SND_SYNC | SND_NODEFAULT);
    snd("empty string", "", SND_SYNC);
    snd("empty string, nodefault", "", SND_SYNC | SND_NODEFAULT);
    /* asynchronous */
    snd("SystemStart, async", "SystemStart", SND_ASYNC);
    windows();
    snd("SystemExit, async + nostop (one playing)", "SystemExit", SND_ASYNC | SND_NOSTOP);
    snd("SystemExit, sync + nostop (one playing)", "SystemExit", SND_SYNC | SND_NOSTOP);
    snd("SystemDefault, async (one playing)", "SystemDefault", SND_ASYNC);
    pause(1500);
    flush();
    windows();
    snd("SystemDefault, async + loop", "SystemDefault", SND_ASYNC | SND_LOOP);
    pause(800);
    flush();
    snd("NULL (stop)", NULL, 0);
    pause(300);
    flush();
    snd("SystemDefault, sync + loop", "SystemDefault", SND_SYNC | SND_LOOP);
    snd("SystemDefault, async + nostop (none playing)", "SystemDefault", SND_ASYNC | SND_NOSTOP);
    pause(1000);
    flush();
    snd("flags FF00", "SystemDefault", 0xFF00);
    /* when MMSYSTEM lets go of a sound that has ended: messages pumped
       for 6 s after an asynchronous one, its records as they come */
    snd("SystemDefault, async", "SystemDefault", SND_ASYNC);
    {
        DWORD t0 = GetTickCount(), last = 0;
        int k;
        for (k = 0; k < 60; k++) {
            DWORD t = GetTickCount();
            while (GetTickCount() - t < 100)
                Yield();
            flush();
            if (k == 59)
                lg("  %d s of messages", (k + 1) / 10);
        }
        (void)t0;
        (void)last;
    }
    /* memory images */
    fh = _lopen("C:\\WINDOWS\\DING.WAV", OF_READ);
    len = fh == HFILE_ERROR ? 0 : _llseek(fh, 0, 2);
    hm = GlobalAlloc(GMEM_MOVEABLE | GMEM_SHARE, len + 16);
    pm = GlobalLock(hm);
    if (fh != HFILE_ERROR) {
        _llseek(fh, 0, 0);
        _lread(fh, pm, (UINT)len);
        _lclose(fh);
    }
    lg("DING.WAV: %ld bytes", len);
    snd("memory, sync", pm, SND_MEMORY | SND_SYNC);
    snd("memory, async", pm, SND_MEMORY | SND_ASYNC);
    pause(1000);
    flush();
    pm[0] = 'X';
    snd("memory, not RIFF, nodefault", pm, SND_MEMORY | SND_SYNC | SND_NODEFAULT);
    snd("memory, not RIFF", pm, SND_MEMORY | SND_SYNC);
    snd("memory, NULL, nodefault", NULL, SND_MEMORY | SND_SYNC | SND_NODEFAULT);
    GlobalUnlock(hm);
    GlobalFree(hm);
    windows();
    flush();
}

/* ---- b: MessageBeep ---- */
static void sec_beep(void)
{
    static const UINT types[] = { 0, MB_ICONHAND, MB_ICONQUESTION, MB_ICONEXCLAMATION,
                                  MB_ICONASTERISK, 0x0050, 0xFFFF };
    int k;
    for (k = 0; k < sizeof types / sizeof types[0]; k++) {
        DWORD t = GetTickCount();
        MessageBeep(types[k]);
        lg("MessageBeep(%04X)", types[k]);
        lg("~ took %lu ms", GetTickCount() - t);
        flush();
        pause(1500);
        flush();
    }
}

void dev_sections(char c)
{
    HMODULE ml = GetModuleHandle("MMSYSLOG");
    if (ml && !mldump) {
        mldump = (MLDUMP)GetProcAddress(ml, "MLDUMP");
        mlname = (MLNAME)GetProcAddress(ml, "MLNAME");
        ((MLPROBE)GetProcAddress(ml, "MLPROBE"))(getss2(), GetCurrentTask());
    }
    cbmark = dev_st->nrec;
    if (!booted) {
        /* what happened at boot (the drivers opened, the start-up sound),
           once it is over: 4 s of messages */
        DWORD t0 = GetTickCount();
        booted = 1;
        lg("== boot");
        windows();
        quietwnd = 1;
        while (GetTickCount() - t0 < 4000) {
            flush();
            Yield();
        }
        flush();
        quietwnd = 0;
    }
    switch (c) {
    case 'l': listdrv(); break;
    case 'z': { DWORD t0 = GetTickCount(); lg("== 3 s of messages"); while (GetTickCount() - t0 < 3000) { flush(); Yield(); } } break;
    case 'n': lg("== numbering"); sec_numbering(); break;
    case 'e': lg("== errors"); sec_errors(); break;
    case 'w': lg("== waveout"); sec_waveout(); break;
    case 'i': lg("== wavein"); sec_wavein(); break;
    case 'm': lg("== midiout"); sec_midiout(); break;
    case 'j': lg("== midiin"); sec_midiin(); break;
    case 'a': lg("== aux"); sec_aux(); break;
    case 'y': lg("== joystick"); sec_joy(); break;
    case 'x': lg("== sndplaysound"); sec_snd(); break;
    case 'b': lg("== messagebeep"); sec_beep(); break;
    }
    flush();
}
