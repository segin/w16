/*
 * dev.c - w16's MMSYSTEM.DLL, layer 3: the wave, MIDI and auxiliary
 * devices and their drivers (ARCHITECTURE.md 23.2, 23.3).
 *
 * Sources: the Multimedia Programmer's Guide ch. 5 (the functions, the
 * structures, the flags, the error numbers); the driver messages' numbers
 * from Wine's mmddk.h of before 2002 (X11 licence); everything else
 * observed on Windows 3.1 with MMP.EXE's device sections and the logging
 * pass-through drivers MDS*.DRV between MMSYSTEM and Windows 3.1's own
 * SNDBLST2.DRV, MSADLIB.DRV and MIDIMAP.DRV (tests/mmsystem/mmdev.c,
 * mds.c, mdl.c; docs/notes/mmsystem-handover.md):
 *   - the drivers are opened at boot, one OpenDriver per [drivers] key
 *     and direction (wave: wodMessage, then wave again for widMessage;
 *     midi: modMessage, midMessage; aux: auxMessage); an open whose entry
 *     the driver lacks is closed again; each entry gets DRVM_INIT, then
 *     its GETNUMDEVS; "midimapper" last, only when there are MIDI output
 *     devices;
 *   - the IDs follow the keys' order; FFFFh is the mapper (MIDI out: the
 *     midimapper driver; wave: each device in turn);
 *   - the checks MMSYSTEM makes itself and their answers (flags 10,
 *     callbacks 11, IDs 2 or 6, handles 5, headers 11, 33, 34, 64, 65),
 *     and what it hands the driver unchanged;
 *   - a driver's PREPARE/UNPREPARE answered MMSYSERR_NOTSUPPORTED: MMSYSTEM
 *     sets or clears WHDR_PREPARED itself.
 * The callbacks (WOM_*, WIM_*, MOM_*, MIM_*) are the drivers' own
 * DriverCallback calls; MMSYSTEM is not between them.
 */
#include "mmsys.h"

typedef DWORD (FAR PASCAL *MSGPROC)(UINT, UINT, DWORD, DWORD, DWORD);

#define C_WOD 0
#define C_WID 1
#define C_MOD 2
#define C_MID 3
#define C_AUX 4
#define NCLS  5

/* the driver messages (Wine's mmddk.h; seen in the records) */
#define DRVM_INIT               100
#define WODM_GETNUMDEVS         3
#define WODM_GETDEVCAPS         4
#define WODM_OPEN               5
#define WODM_CLOSE              6
#define WODM_PREPARE            7
#define WODM_UNPREPARE          8
#define WODM_WRITE              9
#define WODM_PAUSE              10
#define WODM_RESTART            11
#define WODM_RESET              12
#define WODM_GETPOS             13
#define WODM_GETPITCH           14
#define WODM_SETPITCH           15
#define WODM_GETVOLUME          16
#define WODM_SETVOLUME          17
#define WODM_GETPLAYBACKRATE    18
#define WODM_SETPLAYBACKRATE    19
#define WODM_BREAKLOOP          20
#define WIDM_GETNUMDEVS         50
#define WIDM_GETDEVCAPS         51
#define WIDM_OPEN               52
#define WIDM_CLOSE              53
#define WIDM_PREPARE            54
#define WIDM_UNPREPARE          55
#define WIDM_ADDBUFFER          56
#define WIDM_START              57
#define WIDM_STOP               58
#define WIDM_RESET              59
#define WIDM_GETPOS             60
#define MODM_GETNUMDEVS         1
#define MODM_GETDEVCAPS         2
#define MODM_OPEN               3
#define MODM_CLOSE              4
#define MODM_PREPARE            5
#define MODM_UNPREPARE          6
#define MODM_DATA               7
#define MODM_LONGDATA           8
#define MODM_RESET              9
#define MODM_GETVOLUME          10
#define MODM_SETVOLUME          11
#define MODM_CACHEPATCHES       12
#define MODM_CACHEDRUMPATCHES   13
#define MIDM_GETNUMDEVS         53
#define MIDM_GETDEVCAPS         54
#define MIDM_OPEN               55
#define MIDM_CLOSE              56
#define MIDM_PREPARE            57
#define MIDM_UNPREPARE          58
#define MIDM_ADDBUFFER          59
#define MIDM_START              60
#define MIDM_STOP               61
#define MIDM_RESET              62
#define AUXDM_GETNUMDEVS        3
#define AUXDM_GETDEVCAPS        4
#define AUXDM_GETVOLUME         5
#define AUXDM_SETVOLUME         6

#define WAVERR_UNPREPARED       34
#define WAVERR_STILLPLAYING     33
#define MIDIERR_UNPREPARED      64
#define MIDIERR_STILLPLAYING    65

#define WAVE_FORMAT_QUERY       0x0001
#define WAVE_ALLOWSYNC          0x0002
#define CALLBACK_WINDOW         1
#define CALLBACK_FUNCTION       3

#define WHDR_DONE       0x00000001L
#define WHDR_PREPARED   0x00000002L
#define WHDR_INQUEUE    0x00000010L
#define MHDR_PREPARED   0x00000002L
#define MHDR_INQUEUE    0x00000004L

#define MAXDRV 10

struct mmdrv {
    HDRVR h;
    MSGPROC proc;
    WORD n;
};

static struct mmdrv drvs[NCLS][MAXDRV];
static WORD ndrv[NCLS];
static struct mmdrv mapper;             /* the midimapper's modMessage */

static const char *const entries[NCLS] = {
    "wodMessage", "widMessage", "modMessage", "midMessage", "auxMessage"
};
static const UINT numdevs[NCLS] = {
    WODM_GETNUMDEVS, WIDM_GETNUMDEVS, MODM_GETNUMDEVS, MIDM_GETNUMDEVS, AUXDM_GETNUMDEVS
};

/* one [drivers] key for one entry */
static void loadone(LPCSTR key, int cls, struct mmdrv *d)
{
    HDRVR h = OpenDriver(key, NULL, 0);
    MSGPROC p;
    if (!h)
        return;
    p = (MSGPROC)GetProcAddress(GetDriverModuleHandle(h), entries[cls]);
    if (!p) {
        CloseDriver(h, 0, 0);
        return;
    }
    d->h = h;
    d->proc = p;
    p(0, DRVM_INIT, 0, 0, 0);
    d->n = (WORD)p(0, numdevs[cls], 0, 0, 0);
}

/* KEY, KEY1 ... KEY9, a gap in the numbers skipped (MMP's multi case:
   wave2 without wave1 is loaded, wave10 is not) */
static void loadkeys(LPCSTR base, int c1, int c2)
{
    char key[16];
    int k;
    for (k = 0; k <= 9; k++) {
        if (k)
            wsprintf(key, "%s%d", (LPSTR)base, k);
        else
            lstrcpy(key, base);
        if (ndrv[c1] < MAXDRV) {
            loadone(key, c1, &drvs[c1][ndrv[c1]]);
            if (drvs[c1][ndrv[c1]].h)
                ndrv[c1]++;
        }
        if (c2 >= 0 && ndrv[c2] < MAXDRV) {
            loadone(key, c2, &drvs[c2][ndrv[c2]]);
            if (drvs[c2][ndrv[c2]].h)
                ndrv[c2]++;
        }
    }
}

static WORD total(int cls)
{
    WORD i, n = 0;
    for (i = 0; i < ndrv[cls]; i++)
        n += drvs[cls][i].n;
    return n;
}

void dev_load(void)
{
    loadkeys("wave", C_WOD, C_WID);
    loadkeys("midi", C_MOD, C_MID);
    if (total(C_MOD)) {
        struct mmdrv in;
        in.h = 0;
        loadone("midimapper", C_MOD, &mapper);
        loadone("midimapper", C_MID, &in);     /* MIDIMAP has none: closed */
    }
    loadkeys("aux", C_AUX, -1);
    /* the interrupt stack the drivers' callbacks run on: 3.1's exists
       with a wave driver loaded (MMP: a DriverCallback function of
       SNDBLST2's is not on the caller's stack); UNVERIFIED: when it is made */
    if (ndrv[C_WOD] || ndrv[C_WID] || ndrv[C_MOD] || ndrv[C_MID] || ndrv[C_AUX])
        mm_makestack();
}

/* the driver and its own device number for an ID */
static struct mmdrv *find(int cls, UINT id, WORD *dev)
{
    WORD i;
    for (i = 0; i < ndrv[cls]; i++) {
        if (id < drvs[cls][i].n) {
            *dev = id;
            return &drvs[cls][i];
        }
        id -= drvs[cls][i].n;
    }
    return 0;
}

/* ---- handles: blocks in MMSYSTEM's local heap, on a list ---- */
struct mmh {
    WORD sig;
    struct mmh __near *next;
    WORD cls;
    struct mmdrv *d;
    WORD dev;                           /* the driver's device number */
    WORD id;                            /* the ID it was opened with */
    DWORD user;                         /* the driver's instance */
};
#define MMH_SIG 0x484D
static struct mmh __near *handles;

static struct mmh __near *newh(int cls, struct mmdrv *d, WORD dev, WORD id)
{
    struct mmh __near *h = (struct mmh __near *)LocalAlloc(LMEM_FIXED | LMEM_ZEROINIT, sizeof *h);
    if (!h)
        return 0;
    h->sig = MMH_SIG;
    h->cls = cls;
    h->d = d;
    h->dev = dev;
    h->id = id;
    h->next = handles;
    handles = h;
    return h;
}

static void freeh(struct mmh __near *h)
{
    struct mmh __near *q;
    if (handles == h)
        handles = h->next;
    else
        for (q = handles; q; q = q->next)
            if (q->next == h) {
                q->next = h->next;
                break;
            }
    h->sig = 0;
    LocalFree((HLOCAL)h);
}

static struct mmh __near *geth(WORD hv, int cls)
{
    struct mmh __near *h;
    for (h = handles; h; h = h->next)
        if ((WORD)h == hv)
            break;
    if (h && h->cls != cls)
        h = 0;
    return h;
}

static DWORD msg(struct mmh __near *h, UINT m, DWORD p1, DWORD p2)
{
    return h->d->proc(h->dev, m, h->user, p1, p2);
}

/* a callback the caller gives: 11 when its type is not 0-3, its window
   is not one or its function not code (all before anything else) */
extern WORD __cdecl __far mm_iscode(WORD sel);  /* mmfix.asm */

static int badcallback(DWORD cb, DWORD flags)
{
    switch ((WORD)(HIWORD(flags) & 7)) {
    case 0:
    case 2:                             /* UNVERIFIED: a task is not checked */
        return 0;
    case CALLBACK_WINDOW:
        return !IsWindow((HWND)LOWORD(cb));
    case CALLBACK_FUNCTION:
        return !mm_iscode(HIWORD(cb));
    }
    return 1;
}

/* ---- the common parts ---- */
static UINT getnum(int cls)
{
    return total(cls);
}

static UINT caps(int cls, UINT m, UINT id, LPVOID p, UINT size)
{
    WORD dev;
    struct mmdrv *d;
    if (id == 0xFFFF) {
        if (cls != C_MOD || !mapper.h)
            return MMSYSERR_NODRIVER;
        if (!size)
            return 0;
        return (UINT)mapper.proc(0, m, 0, (DWORD)p, size);
    }
    d = find(cls, id, &dev);
    if (!d)
        return MMSYSERR_BADDEVICEID;
    if (!size)
        return 0;
    return (UINT)d->proc(dev, m, 0, (DWORD)p, size);
}

static UINT close(int cls, WORD hv, UINT m)
{
    struct mmh __near *h = geth(hv, cls);
    UINT r;
    if (!h)
        return MMSYSERR_INVALHANDLE;
    r = (UINT)msg(h, m, 0, 0);
    if (!r)
        freeh(h);
    return r;
}

static UINT simple(int cls, WORD hv, UINT m)
{
    struct mmh __near *h = geth(hv, cls);
    if (!h)
        return MMSYSERR_INVALHANDLE;
    return (UINT)msg(h, m, 0, 0);
}

static UINT getid(int cls, WORD hv, UINT FAR *pid)
{
    struct mmh __near *h = geth(hv, cls);
    if (!h)
        return MMSYSERR_INVALHANDLE;
    if (pid)
        *pid = h->id;
    return 0;
}

static DWORD message(int cls, WORD hv, UINT m, DWORD p1, DWORD p2)
{
    struct mmh __near *h = geth(hv, cls);
    if (!h)
        return 0;                       /* 3.1's answer to a bad handle */
    return msg(h, m, p1, p2);
}

/* the headers: WAVEHDR (32 bytes) and MIDIHDR (28); dwFlags at 16.
   Observed (MMP's header checks, waveOut and waveIn): a NULL header or
   another size: 11; flags other than WHDR_DONE..WHDR_INQUEUE and bit 31:
   11; PREPARE of a prepared header: 0 without the driver, otherwise the
   flags but the loop bits (wave out only) are cleared before the driver
   is asked; MMSYSTEM's own preparation (the driver's 8) fails with 7 for
   a NULL lpData; UNPREPARE: queued 33, not prepared 0 without the
   driver; WRITE/ADDBUFFER: queued 33, not prepared 34 (64 MIDI); WRITE
   clears WHDR_DONE before the driver, ADDBUFFER leaves it. UNVERIFIED for MIDI: the flag checks (the
   same rule with the MHDR_ flags assumed). */
struct hdr {
    LPSTR data;
    DWORD len, rec, user, flags;
};

struct hdrrule {
    UINT size;
    DWORD valid, keep, inq;
    UINT busy, unprep;
    DWORD clear;                        /* cleared before WRITE/ADDBUFFER */
};

/* UNVERIFIED for MIDI: which flags are cleared (output as wave output,
   input as wave input assumed) */
static const struct hdrrule wout = { 32, 0x8000001FL, 0x0000000CL, WHDR_INQUEUE,
                                     WAVERR_STILLPLAYING, WAVERR_UNPREPARED, WHDR_DONE };
static const struct hdrrule win = { 32, 0x8000001FL, 0, WHDR_INQUEUE, WAVERR_STILLPLAYING,
                                    WAVERR_UNPREPARED, 0 };
static const struct hdrrule mout = { 28, 0x80000007L, 0, MHDR_INQUEUE, MIDIERR_STILLPLAYING,
                                     MIDIERR_UNPREPARED, WHDR_DONE };
static const struct hdrrule min = { 28, 0x80000007L, 0, MHDR_INQUEUE, MIDIERR_STILLPLAYING,
                                    MIDIERR_UNPREPARED, 0 };

static UINT badhdr(struct hdr FAR *p, UINT size, const struct hdrrule *r)
{
    if (!p || size != r->size)
        return MMSYSERR_INVALPARAM;
    if (p->flags & ~r->valid)
        return MMSYSERR_INVALPARAM;
    return 0;
}

static UINT prepare(int cls, WORD hv, struct hdr FAR *p, UINT size, UINT m,
                    const struct hdrrule *rl)
{
    struct mmh __near *h = geth(hv, cls);
    UINT r;
    if (!h)
        return MMSYSERR_INVALHANDLE;
    if ((r = badhdr(p, size, rl)) != 0)
        return r;
    if (p->flags & WHDR_PREPARED)
        return 0;
    p->flags &= rl->keep;
    r = (UINT)msg(h, m, (DWORD)p, size);
    if (r == MMSYSERR_NOTSUPPORTED) {
        if (!SELECTOROF(p->data))
            return MMSYSERR_NOMEM;
        p->flags |= WHDR_PREPARED;
        r = 0;
    }
    return r;
}

static UINT unprepare(int cls, WORD hv, struct hdr FAR *p, UINT size, UINT m,
                      const struct hdrrule *rl)
{
    struct mmh __near *h = geth(hv, cls);
    UINT r;
    if (!h)
        return MMSYSERR_INVALHANDLE;
    if ((r = badhdr(p, size, rl)) != 0)
        return r;
    if (p->flags & rl->inq)
        return rl->busy;
    if (!(p->flags & WHDR_PREPARED))
        return 0;
    r = (UINT)msg(h, m, (DWORD)p, size);
    if (r == MMSYSERR_NOTSUPPORTED) {
        p->flags &= ~WHDR_PREPARED;
        r = 0;
    }
    return r;
}

static UINT write(int cls, WORD hv, struct hdr FAR *p, UINT size, UINT m,
                  const struct hdrrule *rl)
{
    struct mmh __near *h = geth(hv, cls);
    UINT r;
    if (!h)
        return MMSYSERR_INVALHANDLE;
    if ((r = badhdr(p, size, rl)) != 0)
        return r;
    if (p->flags & rl->inq)
        return rl->busy;
    if (!(p->flags & WHDR_PREPARED))
        return rl->unprep;
    p->flags &= ~rl->clear;
    return (UINT)msg(h, m, (DWORD)p, size);
}

/* ---- wave ---- */
struct waveopendesc {
    WORD hWave;
    LPVOID lpFormat;
    DWORD dwCallback;
    DWORD dwInstance;
};

static UINT wopen(int cls, WORD FAR *ph, UINT id, LPVOID fmt, DWORD cb, DWORD inst,
                  DWORD flags)
{
    struct waveopendesc desc;
    struct mmdrv *d;
    struct mmh __near *h = 0;
    DWORD user = 0;
    WORD dev;
    UINT r, i, n;
    if (LOWORD(flags) & ~(WAVE_FORMAT_QUERY | WAVE_ALLOWSYNC))
        return MMSYSERR_INVALFLAG;
    if (badcallback(cb, flags))
        return MMSYSERR_INVALPARAM;
    if (!(flags & WAVE_FORMAT_QUERY) && ph)
        *ph = 0;
    if (id == 0xFFFF) {
        /* the mapper: each device in turn until one takes the format */
        n = total(cls);
        r = MMSYSERR_BADDEVICEID;
        for (i = 0; i < n; i++) {
            r = wopen(cls, ph, i, fmt, cb, inst, flags);
            if (!r)
                break;
        }
        return r;
    }
    d = find(cls, id, &dev);
    if (!d)
        return MMSYSERR_BADDEVICEID;
    desc.hWave = 0;
    desc.lpFormat = fmt;
    desc.dwCallback = cb;
    desc.dwInstance = inst;
    if (!(flags & WAVE_FORMAT_QUERY)) {
        h = newh(cls, d, dev, id);
        if (!h)
            return MMSYSERR_NOMEM;
        desc.hWave = (WORD)h;
    }
    r = (UINT)d->proc(dev, cls == C_WOD ? WODM_OPEN : WIDM_OPEN, (DWORD)(LPVOID)&user,
                      (DWORD)(LPVOID)&desc, flags);
    if (h) {
        if (r)
            freeh(h);
        else {
            h->user = user;
            if (ph)
                *ph = (WORD)h;
        }
    }
    return r;
}

UINT MMAPI WAVEOUTGETNUMDEVS(void)
{
    return getnum(C_WOD);
}

UINT MMAPI WAVEINGETNUMDEVS(void)
{
    return getnum(C_WID);
}

UINT MMAPI WAVEOUTGETDEVCAPS(UINT id, LPVOID p, UINT size)
{
    return caps(C_WOD, WODM_GETDEVCAPS, id, p, size);
}

UINT MMAPI WAVEINGETDEVCAPS(UINT id, LPVOID p, UINT size)
{
    return caps(C_WID, WIDM_GETDEVCAPS, id, p, size);
}

UINT MMAPI WAVEOUTOPEN(WORD FAR *ph, UINT id, LPVOID fmt, DWORD cb, DWORD inst, DWORD flags)
{
    return wopen(C_WOD, ph, id, fmt, cb, inst, flags);
}

UINT MMAPI WAVEINOPEN(WORD FAR *ph, UINT id, LPVOID fmt, DWORD cb, DWORD inst, DWORD flags)
{
    return wopen(C_WID, ph, id, fmt, cb, inst, flags);
}

UINT MMAPI WAVEOUTCLOSE(WORD h)
{
    return close(C_WOD, h, WODM_CLOSE);
}

UINT MMAPI WAVEINCLOSE(WORD h)
{
    return close(C_WID, h, WIDM_CLOSE);
}

UINT MMAPI WAVEOUTPREPAREHEADER(WORD h, LPVOID p, UINT size)
{
    return prepare(C_WOD, h, p, size, WODM_PREPARE, &wout);
}

UINT MMAPI WAVEINPREPAREHEADER(WORD h, LPVOID p, UINT size)
{
    return prepare(C_WID, h, p, size, WIDM_PREPARE, &win);
}

UINT MMAPI WAVEOUTUNPREPAREHEADER(WORD h, LPVOID p, UINT size)
{
    return unprepare(C_WOD, h, p, size, WODM_UNPREPARE, &wout);
}

UINT MMAPI WAVEINUNPREPAREHEADER(WORD h, LPVOID p, UINT size)
{
    return unprepare(C_WID, h, p, size, WIDM_UNPREPARE, &win);
}

UINT MMAPI WAVEOUTWRITE(WORD h, LPVOID p, UINT size)
{
    return write(C_WOD, h, p, size, WODM_WRITE, &wout);
}

UINT MMAPI WAVEINADDBUFFER(WORD h, LPVOID p, UINT size)
{
    return write(C_WID, h, p, size, WIDM_ADDBUFFER, &win);
}

UINT MMAPI WAVEOUTPAUSE(WORD h)
{
    return simple(C_WOD, h, WODM_PAUSE);
}

UINT MMAPI WAVEOUTRESTART(WORD h)
{
    return simple(C_WOD, h, WODM_RESTART);
}

UINT MMAPI WAVEOUTRESET(WORD h)
{
    return simple(C_WOD, h, WODM_RESET);
}

UINT MMAPI WAVEOUTBREAKLOOP(WORD h)
{
    return simple(C_WOD, h, WODM_BREAKLOOP);
}

UINT MMAPI WAVEINSTART(WORD h)
{
    return simple(C_WID, h, WIDM_START);
}

UINT MMAPI WAVEINSTOP(WORD h)
{
    return simple(C_WID, h, WIDM_STOP);
}

UINT MMAPI WAVEINRESET(WORD h)
{
    return simple(C_WID, h, WIDM_RESET);
}

static UINT getpos(int cls, WORD hv, LPVOID p, UINT size, UINT m)
{
    struct mmh __near *h = geth(hv, cls);
    if (!h)
        return MMSYSERR_INVALHANDLE;
    return (UINT)msg(h, m, (DWORD)p, size);
}

UINT MMAPI WAVEOUTGETPOSITION(WORD h, LPVOID p, UINT size)
{
    return getpos(C_WOD, h, p, size, WODM_GETPOS);
}

UINT MMAPI WAVEINGETPOSITION(WORD h, LPVOID p, UINT size)
{
    return getpos(C_WID, h, p, size, WIDM_GETPOS);
}

static UINT hval(WORD hv, UINT m, DWORD v)
{
    struct mmh __near *h = geth(hv, C_WOD);
    if (!h)
        return MMSYSERR_INVALHANDLE;
    return (UINT)msg(h, m, v, 0);
}

UINT MMAPI WAVEOUTGETPITCH(WORD h, DWORD FAR *p)
{
    return hval(h, WODM_GETPITCH, (DWORD)p);
}

UINT MMAPI WAVEOUTSETPITCH(WORD h, DWORD v)
{
    return hval(h, WODM_SETPITCH, v);
}

UINT MMAPI WAVEOUTGETPLAYBACKRATE(WORD h, DWORD FAR *p)
{
    return hval(h, WODM_GETPLAYBACKRATE, (DWORD)p);
}

UINT MMAPI WAVEOUTSETPLAYBACKRATE(WORD h, DWORD v)
{
    return hval(h, WODM_SETPLAYBACKRATE, v);
}

/* the volume functions take a device ID (a handle is a bad ID) */
static UINT idval(int cls, UINT id, UINT m, DWORD v)
{
    WORD dev;
    struct mmdrv *d;
    if (id == 0xFFFF)
        return MMSYSERR_NODRIVER;
    d = find(cls, id, &dev);
    if (!d)
        return MMSYSERR_BADDEVICEID;
    return (UINT)d->proc(dev, m, 0, v, 0);
}

UINT MMAPI WAVEOUTGETVOLUME(UINT id, DWORD FAR *p)
{
    return idval(C_WOD, id, WODM_GETVOLUME, (DWORD)p);
}

UINT MMAPI WAVEOUTSETVOLUME(UINT id, DWORD v)
{
    return idval(C_WOD, id, WODM_SETVOLUME, v);
}

UINT MMAPI WAVEOUTGETID(WORD h, UINT FAR *p)
{
    return getid(C_WOD, h, p);
}

UINT MMAPI WAVEINGETID(WORD h, UINT FAR *p)
{
    return getid(C_WID, h, p);
}

DWORD MMAPI WAVEOUTMESSAGE(WORD h, UINT m, DWORD p1, DWORD p2)
{
    return message(C_WOD, h, m, p1, p2);
}

DWORD MMAPI WAVEINMESSAGE(WORD h, UINT m, DWORD p1, DWORD p2)
{
    return message(C_WID, h, m, p1, p2);
}

/* ---- MIDI ---- */
struct midiopendesc {
    WORD hMidi;
    DWORD dwCallback;
    DWORD dwInstance;
};

static UINT mopen(int cls, WORD FAR *ph, UINT id, DWORD cb, DWORD inst, DWORD flags)
{
    struct midiopendesc desc;
    struct mmdrv *d;
    struct mmh __near *h;
    WORD dev = 0;
    UINT r;
    if (ph)
        *ph = 0;
    if (LOWORD(flags))
        return MMSYSERR_INVALFLAG;
    if (badcallback(cb, flags))
        return MMSYSERR_INVALPARAM;
    if (id == 0xFFFF) {
        if (cls != C_MOD || !mapper.h)
            return MMSYSERR_NODRIVER;
        d = &mapper;
    } else {
        d = find(cls, id, &dev);
        if (!d)
            return MMSYSERR_BADDEVICEID;
    }
    h = newh(cls, d, dev, id);
    if (!h)
        return MMSYSERR_NOMEM;
    desc.hMidi = (WORD)h;
    desc.dwCallback = cb;
    desc.dwInstance = inst;
    r = (UINT)d->proc(dev, cls == C_MOD ? MODM_OPEN : MIDM_OPEN, (DWORD)(LPVOID)&h->user,
                      (DWORD)(LPVOID)&desc, flags);
    if (r)
        freeh(h);
    else if (ph)
        *ph = (WORD)h;
    return r;
}

UINT MMAPI MIDIOUTGETNUMDEVS(void)
{
    return getnum(C_MOD);
}

UINT MMAPI MIDIINGETNUMDEVS(void)
{
    return getnum(C_MID);
}

UINT MMAPI MIDIOUTGETDEVCAPS(UINT id, LPVOID p, UINT size)
{
    return caps(C_MOD, MODM_GETDEVCAPS, id, p, size);
}

UINT MMAPI MIDIINGETDEVCAPS(UINT id, LPVOID p, UINT size)
{
    return caps(C_MID, MIDM_GETDEVCAPS, id, p, size);
}

UINT MMAPI MIDIOUTOPEN(WORD FAR *ph, UINT id, DWORD cb, DWORD inst, DWORD flags)
{
    return mopen(C_MOD, ph, id, cb, inst, flags);
}

UINT MMAPI MIDIINOPEN(WORD FAR *ph, UINT id, DWORD cb, DWORD inst, DWORD flags)
{
    return mopen(C_MID, ph, id, cb, inst, flags);
}

UINT MMAPI MIDIOUTCLOSE(WORD h)
{
    return close(C_MOD, h, MODM_CLOSE);
}

UINT MMAPI MIDIINCLOSE(WORD h)
{
    return close(C_MID, h, MIDM_CLOSE);
}

UINT MMAPI MIDIOUTPREPAREHEADER(WORD h, LPVOID p, UINT size)
{
    return prepare(C_MOD, h, p, size, MODM_PREPARE, &mout);
}

UINT MMAPI MIDIINPREPAREHEADER(WORD h, LPVOID p, UINT size)
{
    return prepare(C_MID, h, p, size, MIDM_PREPARE, &min);
}

UINT MMAPI MIDIOUTUNPREPAREHEADER(WORD h, LPVOID p, UINT size)
{
    return unprepare(C_MOD, h, p, size, MODM_UNPREPARE, &mout);
}

UINT MMAPI MIDIINUNPREPAREHEADER(WORD h, LPVOID p, UINT size)
{
    return unprepare(C_MID, h, p, size, MIDM_UNPREPARE, &min);
}

UINT MMAPI MIDIOUTLONGMSG(WORD h, LPVOID p, UINT size)
{
    return write(C_MOD, h, p, size, MODM_LONGDATA, &mout);
}

UINT MMAPI MIDIINADDBUFFER(WORD h, LPVOID p, UINT size)
{
    return write(C_MID, h, p, size, MIDM_ADDBUFFER, &min);
}

UINT MMAPI MIDIOUTSHORTMSG(WORD hv, DWORD m)
{
    struct mmh __near *h = geth(hv, C_MOD);
    if (!h)
        return MMSYSERR_INVALHANDLE;
    return (UINT)msg(h, MODM_DATA, m, 0);
}

UINT MMAPI MIDIOUTRESET(WORD h)
{
    return simple(C_MOD, h, MODM_RESET);
}

UINT MMAPI MIDIINSTART(WORD h)
{
    return simple(C_MID, h, MIDM_START);
}

UINT MMAPI MIDIINSTOP(WORD h)
{
    return simple(C_MID, h, MIDM_STOP);
}

UINT MMAPI MIDIINRESET(WORD h)
{
    return simple(C_MID, h, MIDM_RESET);
}

UINT MMAPI MIDIOUTGETVOLUME(UINT id, DWORD FAR *p)
{
    if (id == 0xFFFF && mapper.h)
        return (UINT)mapper.proc(0, MODM_GETVOLUME, 0, (DWORD)p, 0);
    return idval(C_MOD, id, MODM_GETVOLUME, (DWORD)p);
}

UINT MMAPI MIDIOUTSETVOLUME(UINT id, DWORD v)
{
    if (id == 0xFFFF && mapper.h)
        return (UINT)mapper.proc(0, MODM_SETVOLUME, 0, v, 0);
    return idval(C_MOD, id, MODM_SETVOLUME, v);
}

/* the patches: the caller's array and MAKELONG(flags, bank) */
static UINT cache(WORD hv, UINT bank, LPVOID p, UINT flags, UINT m)
{
    struct mmh __near *h = geth(hv, C_MOD);
    if (!h)
        return MMSYSERR_INVALHANDLE;
    return (UINT)msg(h, m, (DWORD)p, MAKELONG(flags, bank));
}

UINT MMAPI MIDIOUTCACHEPATCHES(WORD h, UINT bank, LPVOID p, UINT flags)
{
    return cache(h, bank, p, flags, MODM_CACHEPATCHES);
}

UINT MMAPI MIDIOUTCACHEDRUMPATCHES(WORD h, UINT patch, LPVOID p, UINT flags)
{
    return cache(h, patch, p, flags, MODM_CACHEDRUMPATCHES);
}

UINT MMAPI MIDIOUTGETID(WORD h, UINT FAR *p)
{
    return getid(C_MOD, h, p);
}

UINT MMAPI MIDIINGETID(WORD h, UINT FAR *p)
{
    return getid(C_MID, h, p);
}

DWORD MMAPI MIDIOUTMESSAGE(WORD h, UINT m, DWORD p1, DWORD p2)
{
    return message(C_MOD, h, m, p1, p2);
}

DWORD MMAPI MIDIINMESSAGE(WORD h, UINT m, DWORD p1, DWORD p2)
{
    return message(C_MID, h, m, p1, p2);
}

/* ---- auxiliary ---- */
UINT MMAPI AUXGETNUMDEVS(void)
{
    return getnum(C_AUX);
}

UINT MMAPI AUXGETDEVCAPS(UINT id, LPVOID p, UINT size)
{
    return caps(C_AUX, AUXDM_GETDEVCAPS, id, p, size);
}

UINT MMAPI AUXGETVOLUME(UINT id, DWORD FAR *p)
{
    return idval(C_AUX, id, AUXDM_GETVOLUME, (DWORD)p);
}

UINT MMAPI AUXSETVOLUME(UINT id, DWORD v)
{
    return idval(C_AUX, id, AUXDM_SETVOLUME, v);
}

DWORD MMAPI AUXOUTMESSAGE(UINT id, UINT m, DWORD p1, DWORD p2)
{
    WORD dev;
    struct mmdrv *d;
    if (id == 0xFFFF)
        return MMSYSERR_NODRIVER;
    d = find(C_AUX, id, &dev);
    if (!d)
        return MMSYSERR_BADDEVICEID;
    return d->proc(dev, m, 0, p1, p2);
}

/* ---- error texts (MMSYSTEM's string table: string n for error n) ----
   Observed (MMP section e): size 0: 0 and nothing written; NULL: 9; a
   number without a text: "" and 9; otherwise the text, cut to size - 1
   characters, and 0. UNVERIFIED: an unwritable buffer (3.1 answered 0 to
   0001:0000 and was unstable afterwards; w16 writes nothing). */
static UINT errtext(UINT err, LPSTR p, UINT size, UINT lo, UINT hi)
{
    if (!size)
        return 0;
    if (!p)
        return MMSYSERR_BADERRNUM;
    if (IsBadWritePtr(p, size))
        return 0;
    if (!(err <= MMSYSERR_INVALPARAM || (err >= lo && err <= hi))) {
        *p = 0;
        return MMSYSERR_BADERRNUM;
    }
    LoadString(mm_hinst, err, p, size);
    return 0;
}

UINT MMAPI WAVEOUTGETERRORTEXT(UINT err, LPSTR p, UINT size)
{
    return errtext(err, p, size, 32, 35);
}

UINT MMAPI WAVEINGETERRORTEXT(UINT err, LPSTR p, UINT size)
{
    return errtext(err, p, size, 32, 35);
}

UINT MMAPI MIDIOUTGETERRORTEXT(UINT err, LPSTR p, UINT size)
{
    return errtext(err, p, size, 64, 69);
}

UINT MMAPI MIDIINGETERRORTEXT(UINT err, LPSTR p, UINT size)
{
    return errtext(err, p, size, 64, 69);
}
