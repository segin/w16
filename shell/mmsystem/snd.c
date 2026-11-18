/*
 * snd.c - w16's MMSYSTEM.DLL, layer 3: sndPlaySound and MMSYSTEM's own
 * window (ARCHITECTURE.md 23.2, 23.3).
 *
 * Sources: the Multimedia Programmer's Guide ch. 4 ("Playing Waveform
 * Sounds": the names, WIN.INI [sounds], SystemDefault, the SND_ flags,
 * stopping with NULL); the RIFF WAVE layout from ch. 7 and 5; the rest
 * observed on Windows 3.1 with MMP.EXE's section x and the logging
 * drivers between MMSYSTEM and SNDBLST2.DRV (tests/mmsystem/mmdev.c;
 * docs/notes/mmsystem-handover.md):
 *   - MMSYSTEM has a hidden window of class "#42" (an integer atom) made
 *     at boot; a sound is played with waveOutOpen(WAVE_MAPPER, the
 *     format inside the file's image, that window, instance 0,
 *     CALLBACK_WINDOW | WAVE_ALLOWSYNC), one header over the whole data
 *     (WHDR_BEGINLOOP | WHDR_ENDLOOP and dwLoops FFFFFFFF for SND_LOOP),
 *     prepare, write; MM_WOM_DONE at the window unprepares and closes;
 *   - the file is read into one global block, the header in front of it
 *     (its dwUser the format's address); SND_MEMORY plays the caller's
 *     image where it is, with a header of its own;
 *   - the name is a [sounds] key first (the file before a comma, blanks
 *     trimmed), else a file found as OpenFile finds it; a sound that is
 *     missing or not a wave plays SystemDefault unless SND_NODEFAULT (a
 *     memory image that is not RIFF never does);
 *   - NULL or "" stops (reset, unprepare, close) and answers TRUE; every
 *     new sound stops the one before; SND_NOSTOP while one plays, SND_LOOP
 *     without SND_ASYNC and unknown flags answer FALSE without a sound;
 *   - SND_SYNC returns when the sound has played;
 *   - at boot MMSYSTEM plays SystemStart asynchronously.
 */
#include "mmsys.h"

#define SND_ASYNC       0x0001
#define SND_NODEFAULT   0x0002
#define SND_MEMORY      0x0004
#define SND_LOOP        0x0008
#define SND_NOSTOP      0x0010

#define MM_WOM_DONE     0x03BD
#define WAVE_ALLOWSYNC  0x0002
#define CALLBACK_WINDOW 0x00010000L
#define WHDR_DONE       0x00000001L
#define WHDR_BEGINLOOP  0x00000004L
#define WHDR_ENDLOOP    0x00000008L

typedef struct {
    LPSTR lpData;
    DWORD dwBufferLength, dwBytesRecorded, dwUser, dwFlags, dwLoops;
    LPVOID lpNext;
    DWORD reserved;
} SNDHDR;

/* dev.c's entries, called as an application would */
UINT MMAPI WAVEOUTOPEN(WORD FAR *ph, UINT id, LPVOID fmt, DWORD cb, DWORD inst, DWORD flags);
UINT MMAPI WAVEOUTCLOSE(WORD h);
UINT MMAPI WAVEOUTPREPAREHEADER(WORD h, LPVOID p, UINT size);
UINT MMAPI WAVEOUTUNPREPAREHEADER(WORD h, LPVOID p, UINT size);
UINT MMAPI WAVEOUTWRITE(WORD h, LPVOID p, UINT size);
UINT MMAPI WAVEOUTRESET(WORD h);

/* the block a file is read into: the header, room, then the file (3.1's
   data of a standard 44-byte file starts at 60h) */
#define PREFIX 0x34

static HWND sndwnd;
static WORD hwo;                        /* the sound playing (0: none) */
static HGLOBAL hblock;                  /* its block (the file, or a header) */
static SNDHDR FAR *hdr;

/* the end of a sound: the device closed; its block stays until the next
   sound or a stop (3.1's is still there when the sound has ended) */
static void finish(void)
{
    if (!hwo)
        return;
    WAVEOUTUNPREPAREHEADER(hwo, hdr, sizeof *hdr);
    WAVEOUTCLOSE(hwo);
    hwo = 0;
}

static void release(void)
{
    if (hblock) {
        GlobalUnlock(hblock);
        GlobalFree(hblock);
        hblock = 0;
    }
    hdr = 0;
}

static void stop(void)
{
    if (hwo) {
        /* a sound that has ended and waits for the timer is not reset */
        if (!(hdr && (hdr->dwFlags & WHDR_DONE)))
            WAVEOUTRESET(hwo);
        finish();
    }
    release();
}

LRESULT MMAPI mm_sndwndproc(HWND h, UINT msg, WPARAM wp, LPARAM lp)
{
    /* 3.1's (MMP section x, the messages its window gets): MM_WOM_DONE
       starts a timer (ID 1); the device is closed at a WM_TIMER that finds
       the sound playing done, and the timer killed; until then it ticks
       on, across new sounds. UNVERIFIED: the period (about 500 ms: no tick
       between two 0.5 s apart in MMP's pumping, one at the end) */
    if (msg == MM_WOM_DONE) {
        if (hwo && wp == hwo)
            SetTimer(h, 1, 500, NULL);
        return 0;
    }
    if (msg == WM_TIMER && wp == 1) {
        if (!hwo || (hdr && (hdr->dwFlags & WHDR_DONE))) {
            KillTimer(h, 1);
            finish();
        }
        return 0;
    }
    return DefWindowProc(h, msg, wp, lp);
}

void snd_init(void)
{
    WNDCLASS wc;
    if (sndwnd)
        return;
    wc.style = 0;
    wc.lpfnWndProc = (WNDPROC)mm_sndwndproc;
    wc.cbClsExtra = 0;
    wc.cbWndExtra = 0;
    wc.hInstance = mm_hinst;
    wc.hIcon = 0;
    wc.hCursor = 0;
    wc.hbrBackground = 0;
    wc.lpszMenuName = NULL;
    wc.lpszClassName = "#42";
    RegisterClass(&wc);
    sndwnd = CreateWindow("#42", NULL, WS_POPUP, 0, 0, 0, 0, 0, 0, mm_hinst, NULL);
}

/* the "fmt " and "data" chunks of a RIFF WAVE image of len bytes */
static BOOL parse(BYTE FAR *p, DWORD len, LPVOID FAR *fmt, LPSTR FAR *data, DWORD FAR *dlen)
{
    DWORD at = 12, n;
    *fmt = 0;
    *data = 0;
    if (len < 12 || *(DWORD FAR *)p != 0x46464952L || *(DWORD FAR *)(p + 8) != 0x45564157L)
        return FALSE;                   /* "RIFF" ... "WAVE" */
    while (at + 8 <= len) {
        DWORD id = *(DWORD FAR *)(p + (WORD)at);
        n = *(DWORD FAR *)(p + (WORD)at + 4);
        if (id == 0x20746D66L)          /* "fmt " */
            *fmt = p + (WORD)at + 8;
        else if (id == 0x61746164L) {   /* "data" */
            *data = (LPSTR)(p + (WORD)at + 8);
            *dlen = n;
            if (at + 8 + n > len)
                *dlen = len - at - 8;
        }
        at += 8 + ((n + 1) & ~1L);
    }
    return *fmt && *data;
}

/* a file's image read into a new block after PREFIX bytes; 0 when it is
   not there */
static HGLOBAL readfile(LPCSTR name, DWORD FAR *len)
{
    OFSTRUCT of;
    HFILE f = OpenFile(name, &of, OF_READ | OF_SHARE_DENY_NONE);
    HGLOBAL g;
    BYTE FAR *p;
    long n;
    if (f == HFILE_ERROR)
        return 0;
    n = _llseek(f, 0, 2);
    _llseek(f, 0, 0);
    if (n <= 0 || n > 0xFFF0L - PREFIX) {
        _lclose(f);
        return 0;
    }
    g = GlobalAlloc(GMEM_MOVEABLE | GMEM_SHARE, PREFIX + n);
    if (!g) {
        _lclose(f);
        return 0;
    }
    p = (BYTE FAR *)GlobalLock(g);
    if (_lread(f, p + PREFIX, (UINT)n) != (UINT)n) {
        GlobalUnlock(g);
        GlobalFree(g);
        _lclose(f);
        return 0;
    }
    _lclose(f);
    *len = n;
    return g;
}

/* a name: the [sounds] key's file, else the name itself */
static void soundfile(LPCSTR name, LPSTR file, int n)
{
    char v[144];
    LPSTR s, e;
    if (!GetProfileString("sounds", name, "", v, sizeof v) || !v[0]) {
        lstrcpyn(file, name, n);
        return;
    }
    for (s = v; *s == ' ' || *s == '\t'; s++)
        ;
    for (e = s; *e && *e != ','; e++)
        ;
    while (e > s && (e[-1] == ' ' || e[-1] == '\t'))
        e--;
    *e = 0;
    lstrcpyn(file, s, n);
}

static BOOL start(LPVOID fmt, LPSTR data, DWORD dlen, UINT flags)
{
    WORD h;
    if (WAVEOUTOPEN(&h, 0xFFFF, fmt, (DWORD)(WORD)sndwnd, 0,
                    CALLBACK_WINDOW | WAVE_ALLOWSYNC))
        return FALSE;
    hdr->lpData = data;
    hdr->dwBufferLength = dlen;
    hdr->dwUser = (DWORD)fmt;
    hdr->dwFlags = 0;
    hdr->dwLoops = 0;
    if (WAVEOUTPREPAREHEADER(h, hdr, sizeof *hdr)) {
        WAVEOUTCLOSE(h);
        return FALSE;
    }
    /* the loop is set after the preparation (3.1's driver sees flags 0 and
       dwLoops 0 at PREPARE) */
    if (flags & SND_LOOP) {
        hdr->dwFlags |= WHDR_BEGINLOOP | WHDR_ENDLOOP;
        hdr->dwLoops = 0xFFFFFFFFL;
    }
    if (WAVEOUTWRITE(h, hdr, sizeof *hdr)) {
        WAVEOUTUNPREPAREHEADER(h, hdr, sizeof *hdr);
        WAVEOUTCLOSE(h);
        return FALSE;
    }
    hwo = h;
    if (!(flags & SND_ASYNC)) {
        while (hwo == h && !(hdr->dwFlags & WHDR_DONE))
            Yield();
        finish();
    }
    return TRUE;
}

/* a file (or [sounds] key) */
static BOOL playname(LPCSTR name, UINT flags)
{
    char file[144];
    DWORD len, dlen;
    LPVOID fmt;
    LPSTR data;
    BYTE FAR *p;
    HGLOBAL g;
    soundfile(name, file, sizeof file);
    g = readfile(file, &len);
    if (!g)
        return FALSE;
    p = (BYTE FAR *)GlobalLock(g);
    if (!parse(p + PREFIX, len, &fmt, &data, &dlen)) {
        GlobalUnlock(g);
        GlobalFree(g);
        return FALSE;
    }
    hblock = g;
    hdr = (SNDHDR FAR *)p;
    if (!start(fmt, data, dlen, flags)) {
        hblock = 0;
        hdr = 0;
        GlobalUnlock(g);
        GlobalFree(g);
        return FALSE;
    }
    return TRUE;
}

UINT MMAPI WAVEOUTGETNUMDEVS(void);

BOOL MMAPI SNDPLAYSOUND(LPCSTR name, UINT flags)
{
    /* without a wave device: FALSE, whatever the arguments (NULL too) */
    if (!WAVEOUTGETNUMDEVS())
        return FALSE;
    if (!name || (!(flags & SND_MEMORY) && !*name)) {
        stop();
        return TRUE;
    }
    if (flags & ~(SND_ASYNC | SND_NODEFAULT | SND_MEMORY | SND_LOOP | SND_NOSTOP))
        return FALSE;
    if ((flags & SND_LOOP) && !(flags & SND_ASYNC))
        return FALSE;
    if ((flags & SND_NOSTOP) && hwo)
        return FALSE;
    stop();
    if (!sndwnd)
        snd_init();
    if (flags & SND_MEMORY) {
        LPVOID fmt;
        LPSTR data;
        DWORD dlen;
        /* the image's length is RIFF's own */
        if (!parse((BYTE FAR *)name, *(DWORD FAR *)(name + 4) + 8, &fmt, &data, &dlen))
            return FALSE;
        hblock = GlobalAlloc(GMEM_MOVEABLE | GMEM_SHARE, sizeof(SNDHDR));
        if (!hblock)
            return FALSE;
        hdr = (SNDHDR FAR *)GlobalLock(hblock);
        if (start(fmt, data, dlen, flags))
            return TRUE;
        GlobalUnlock(hblock);
        GlobalFree(hblock);
        hblock = 0;
        hdr = 0;
        return FALSE;
    }
    if (playname(name, flags))
        return TRUE;
    if (flags & SND_NODEFAULT)
        return FALSE;
    return playname("SystemDefault", flags);
}
