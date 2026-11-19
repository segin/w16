/*
 * sbp.c - SBP.EXE, the Sound Blaster 16 driver's playing probe
 * (tests/sb16/sbaudio.sh; ARCHITECTURE.md 24.1). Run as the shell, it
 * reads C:\SBP.TXT, one command a line, logs to C:\SBP.LOG and returns,
 * which ends Windows (and with it DOSBox-X's sound capture):
 *   play FILE [N]       the WAV file through wave output device 0, cut
 *                       into N headers (default 4), all written at once;
 *                       waits for the N WOM_DONE (window callback); logs
 *                       the count, the time it took and the position in
 *                       bytes, samples and milliseconds
 *   loop FILE COUNT     the file as one header with WHDR_BEGINLOOP |
 *                       WHDR_ENDLOOP and dwLoops COUNT
 *   pause MS            nothing for MS milliseconds (messages pumped)
 *   vol L R             waveOutSetVolume(0, R << 16 | L), hex
 *   rec MS RATE BITS CH wave input device 0 for MS milliseconds into
 *                       headers of 1/4 s; logs the WIM_DATA count, the
 *                       bytes recorded and the samples' lowest and
 *                       highest values
 *   notes               MIDI output device 0: program 0 on channel 1,
 *                       the notes 60 64 67 72 69 65 62 each 300 ms with
 *                       100 ms between, then midiOutReset
 *   mci STRING          mciSendString(STRING) with the probe's window;
 *                       logs the answer and the returned string
 * Sources: the Multimedia Programmer's Guide (the wave, MIDI, MMIO and
 * MCI functions, the RIFF WAVE layout); the probe only calls them.
 */
#include <windows.h>
#include <mmsystem.h>

extern WORD __cdecl entry_regs[11];  /* start.asm: ax bx cx dx si di bp sp ds es ss */
static const char logname[] = "C:\\SBP.LOG";
static char line[400];
static HWND hwnd;
static WORD ndone, ndata;
static WAVEHDR FAR *lastdata;

static void put(const char FAR *s, int n)
{
    HFILE f = _lopen(logname, OF_WRITE);
    if (f == HFILE_ERROR)
        f = _lcreat(logname, 0);
    else
        _llseek(f, 0, 2);
    _lwrite(f, s, n);
    _lclose(f);
}

static void lg(const char *fmt, ...)
{
    int n = wvsprintf(line, fmt, (LPSTR)(&fmt + 1));
    line[n++] = '\r';
    line[n++] = '\n';
    put(line, n);
}

LRESULT FAR PASCAL __export SbpWndProc(HWND h, UINT m, WPARAM w, LPARAM l)
{
    switch (m) {
    case MM_WOM_DONE:
        ndone++;
        return 0;
    case MM_WIM_DATA:
        ndata++;
        lastdata = (WAVEHDR FAR *)l;
        return 0;
    }
    return DefWindowProc(h, m, w, l);
}

static void pump(void)
{
    MSG m;
    while (PeekMessage(&m, 0, 0, 0, PM_REMOVE)) {
        TranslateMessage(&m);
        DispatchMessage(&m);
    }
}

/* MS milliseconds, messages pumped; the largest reading of the clock
   counts (3.1's timeGetTime may step back: ARCHITECTURE.md 21.2) */
static void wait(DWORD ms)
{
    DWORD t0 = timeGetTime(), t, top = t0;
    for (;;) {
        pump();
        t = timeGetTime();
        if (t > top && t - top < 0x10000000L)
            top = t;
        if (top - t0 >= ms)
            return;
        Yield();
    }
}

/* until *p reaches n, at most MS milliseconds; the time it took */
static DWORD waitn(WORD *p, WORD n, DWORD ms)
{
    DWORD t0 = timeGetTime(), t, top = t0;
    while (*p < n) {
        pump();
        t = timeGetTime();
        if (t > top && t - top < 0x10000000L)
            top = t;
        if (top - t0 >= ms)
            break;
        Yield();
    }
    return top - t0;
}

/* a WAV file opened at its data: the format and the data's length */
static HMMIO openwav(LPCSTR name, PCMWAVEFORMAT *f, DWORD *len)
{
    HMMIO h = mmioOpen((LPSTR)name, NULL, MMIO_READ | MMIO_ALLOCBUF);
    MMCKINFO r, c;
    if (!h)
        return 0;
    r.fccType = mmioFOURCC('W', 'A', 'V', 'E');
    c.ckid = mmioFOURCC('f', 'm', 't', ' ');
    if (mmioDescend(h, &r, NULL, MMIO_FINDRIFF) || mmioDescend(h, &c, &r, MMIO_FINDCHUNK) ||
        mmioRead(h, (HPSTR)f, sizeof *f) != sizeof *f || mmioAscend(h, &c, 0)) {
        mmioClose(h, 0);
        return 0;
    }
    c.ckid = mmioFOURCC('d', 'a', 't', 'a');
    if (mmioDescend(h, &c, &r, MMIO_FINDCHUNK)) {
        mmioClose(h, 0);
        return 0;
    }
    *len = c.cksize;
    return h;
}

static void position(HWAVEOUT h)
{
    MMTIME b, s, m;
    b.wType = TIME_BYTES;
    s.wType = TIME_SAMPLES;
    m.wType = TIME_MS;
    waveOutGetPosition(h, &b, sizeof b);
    waveOutGetPosition(h, &s, sizeof s);
    waveOutGetPosition(h, &m, sizeof m);
    lg("  position: bytes %lu, samples %lu, ms %lu (type %u)", b.u.cb, s.u.sample, m.u.ms, m.wType);
}

#define MAXH 16

/* play FILE N, or (loops) the file as one looped header; each header's
   data a block of its own (below 64 KB) */
static void play(LPCSTR name, WORD n, DWORD loops)
{
    PCMWAVEFORMAT f;
    HGLOBAL hd[MAXH], hh;
    DWORD len, part, off, took, sz;
    WAVEHDR FAR *w;
    HWAVEOUT h;
    HMMIO m;
    UINT r;
    WORD i;
    m = openwav(name, &f, &len);
    if (!m) {
        lg("play %s: not a WAV file", name);
        return;
    }
    if (loops)
        n = 1;
    if (!n)
        n = 4;
    part = len / n / f.wf.nBlockAlign * f.wf.nBlockAlign;
    while (part > 60000L && n < MAXH) {
        n++;
        part = len / n / f.wf.nBlockAlign * f.wf.nBlockAlign;
    }
    lg("play %s: format %u %u %lu %u, %lu bytes, %u headers%s", name, f.wf.wFormatTag,
       f.wf.nChannels, f.wf.nSamplesPerSec, f.wBitsPerSample, len, n, (LPSTR)(loops ? ", looped" : ""));
    hh = GlobalAlloc(GMEM_MOVEABLE | GMEM_SHARE, n * sizeof(WAVEHDR));
    w = (WAVEHDR FAR *)GlobalLock(hh);
    for (i = 0, off = 0; i < n; i++) {
        sz = i == n - 1 ? len - off : part;
        hd[i] = GlobalAlloc(GMEM_MOVEABLE | GMEM_SHARE, sz + 16);
        w[i].lpData = GlobalLock(hd[i]);
        mmioRead(m, (HPSTR)w[i].lpData, sz);
        w[i].dwBufferLength = sz;
        w[i].dwFlags = loops ? WHDR_BEGINLOOP | WHDR_ENDLOOP : 0;
        w[i].dwLoops = loops;
        w[i].dwUser = i;
        off += sz;
    }
    mmioClose(m, 0);
    r = waveOutOpen(&h, 0, (LPWAVEFORMAT)&f, (DWORD)(WORD)hwnd, 0, CALLBACK_WINDOW);
    if (r)
        lg("  waveOutOpen = %u", r);
    else {
        for (i = 0; i < n; i++)
            waveOutPrepareHeader(h, &w[i], sizeof(WAVEHDR));
        ndone = 0;
        for (i = 0; i < n; i++)
            if ((r = waveOutWrite(h, &w[i], sizeof(WAVEHDR))) != 0)
                lg("  waveOutWrite(%u) = %u", i, r);
        took = waitn(&ndone, n, 60000L);
        lg("  done %u of %u, ~%lu ms", ndone, n, took);
        position(h);
        for (i = 0; i < n; i++)
            waveOutUnprepareHeader(h, &w[i], sizeof(WAVEHDR));
        r = waveOutClose(h);
        lg("  waveOutClose = %u", r);
        pump();
    }
    for (i = 0; i < n; i++) {
        GlobalUnlock(hd[i]);
        GlobalFree(hd[i]);
    }
    GlobalUnlock(hh);
    GlobalFree(hh);
}

static void rec(DWORD ms, DWORD rate, WORD bits, WORD ch)
{
    PCMWAVEFORMAT f;
    HWAVEIN h;
    HGLOBAL hb[4], hh;
    WAVEHDR FAR *w;
    DWORD size, total = 0, took, j;
    WORD i, n, lo = 0xFFFF, hi = 0;
    UINT r;
    f.wf.wFormatTag = WAVE_FORMAT_PCM;
    f.wf.nChannels = ch;
    f.wf.nSamplesPerSec = rate;
    f.wf.nBlockAlign = ch * (bits / 8);
    f.wf.nAvgBytesPerSec = rate * f.wf.nBlockAlign;
    f.wBitsPerSample = bits;
    size = f.wf.nAvgBytesPerSec / 4 / f.wf.nBlockAlign * f.wf.nBlockAlign;
    n = (WORD)((ms + 249) / 250);
    if (n > 4)
        n = 4;
    lg("rec %lu ms, %lu Hz, %u bits, %u channels: %u headers of %lu bytes", ms, rate, bits, ch, n, size);
    r = waveInOpen(&h, 0, (LPWAVEFORMAT)&f, (DWORD)(WORD)hwnd, 0, CALLBACK_WINDOW);
    if (r) {
        lg("  waveInOpen = %u", r);
        return;
    }
    hh = GlobalAlloc(GMEM_MOVEABLE | GMEM_SHARE, 4 * sizeof(WAVEHDR));
    w = (WAVEHDR FAR *)GlobalLock(hh);
    for (i = 0; i < n; i++) {
        hb[i] = GlobalAlloc(GMEM_MOVEABLE | GMEM_SHARE, size);
        w[i].lpData = GlobalLock(hb[i]);
        w[i].dwBufferLength = size;
        w[i].dwFlags = 0;
        waveInPrepareHeader(h, &w[i], sizeof(WAVEHDR));
        waveInAddBuffer(h, &w[i], sizeof(WAVEHDR));
    }
    ndata = 0;
    r = waveInStart(h);
    took = waitn(&ndata, n, ms + 5000);
    lg("  waveInStart = %u; WIM_DATA %u of %u, ~%lu ms", r, ndata, n, took);
    waveInStop(h);
    waveInReset(h);
    pump();
    for (i = 0; i < n; i++) {
        total += w[i].dwBytesRecorded;
        for (j = 0; j < w[i].dwBytesRecorded; j += f.wf.nBlockAlign) {
            WORD v = bits == 8 ? (BYTE)w[i].lpData[j]
                               : (WORD)(*(short FAR *)(w[i].lpData + j) + 0x8000);
            if (v < lo)
                lo = v;
            if (v > hi)
                hi = v;
        }
        waveInUnprepareHeader(h, &w[i], sizeof(WAVEHDR));
        GlobalUnlock(hb[i]);
        GlobalFree(hb[i]);
    }
    lg("  recorded %lu bytes, values %04X-%04X (offset binary)", total, lo, hi);
    lg("  waveInClose = %u", waveInClose(h));
    GlobalUnlock(hh);
    GlobalFree(hh);
}

static void notes(void)
{
    static const BYTE k[] = { 60, 64, 67, 72, 69, 65, 62 };
    HMIDIOUT h;
    UINT r;
    int i;
    r = midiOutOpen(&h, 0, 0, 0, 0);
    lg("notes: midiOutOpen = %u", r);
    if (r)
        return;
    midiOutShortMsg(h, 0x000000C0L);
    for (i = 0; i < sizeof k; i++) {
        midiOutShortMsg(h, 0x00640090L | ((DWORD)k[i] << 8));
        wait(300);
        midiOutShortMsg(h, 0x00000080L | ((DWORD)k[i] << 8));
        wait(100);
    }
    lg("  midiOutReset = %u", midiOutReset(h));
    lg("  midiOutClose = %u", midiOutClose(h));
}

static void mci(LPCSTR s)
{
    char ret[128];
    DWORD r;
    ret[0] = 0;
    r = mciSendString(s, ret, sizeof ret, hwnd);
    lg("mci \"%s\" = %lu \"%s\"", s, r, (LPSTR)ret);
    pump();
}

static DWORD num(char **p, int hex)
{
    DWORD v = 0;
    while (**p == ' ')
        (*p)++;
    for (;; (*p)++) {
        char c = **p;
        if (c >= '0' && c <= '9')
            v = v * (hex ? 16 : 10) + (c - '0');
        else if (hex && (c | 0x20) >= 'a' && (c | 0x20) <= 'f')
            v = v * 16 + ((c | 0x20) - 'a' + 10);
        else
            return v;
    }
}

static char *word(char **p)
{
    char *s;
    while (**p == ' ')
        (*p)++;
    s = *p;
    while (**p && **p != ' ')
        (*p)++;
    if (**p)
        *(*p)++ = 0;
    return s;
}

static int is(const char *a, const char *b)
{
    return !lstrcmpi(a, b);
}

int __cdecl __far tmain(void)
{
    HINSTANCE hi = (HINSTANCE)entry_regs[5];   /* DI: the instance */
    static char script[2048];
    WNDCLASS wc;
    HFILE f;
    int n;
    char *p, *e, *cmd;
    {
        typedef BOOL (FAR PASCAL *INITAPP)(HINSTANCE);
        INITAPP ia = (INITAPP)GetProcAddress(GetModuleHandle("USER"), MAKEINTRESOURCE(5));
        ia(hi);
    }
    wc.style = 0;
    wc.lpfnWndProc = SbpWndProc;
    wc.cbClsExtra = wc.cbWndExtra = 0;
    wc.hInstance = hi;
    wc.hIcon = 0;
    wc.hCursor = 0;
    wc.hbrBackground = 0;
    wc.lpszMenuName = NULL;
    wc.lpszClassName = "SbpWnd";
    RegisterClass(&wc);
    hwnd = CreateWindow("SbpWnd", "SBP", WS_OVERLAPPED, 0, 0, 100, 50, 0, 0, hi, NULL);
    f = _lopen("C:\\SBP.TXT", OF_READ);
    n = f == HFILE_ERROR ? 0 : _lread(f, script, sizeof script - 1);
    if (f != HFILE_ERROR)
        _lclose(f);
    script[n > 0 ? n : 0] = 0;
    lg("SBP start: waveOut %u waveIn %u midiOut %u aux %u", waveOutGetNumDevs(), waveInGetNumDevs(),
       midiOutGetNumDevs(), auxGetNumDevs());
    /* MMSYSTEM's start-up sound, if any, done first */
    wait(4000);
    for (p = script; *p; p = e) {
        for (e = p; *e && *e != '\r' && *e != '\n'; e++)
            ;
        if (*e)
            *e++ = 0;
        while (*e == '\r' || *e == '\n')
            e++;
        cmd = word(&p);
        if (is(cmd, "play")) {
            char *fn = word(&p);
            play(fn, (WORD)num(&p, 0), 0);
        } else if (is(cmd, "loop")) {
            char *fn = word(&p);
            play(fn, 1, num(&p, 0));
        } else if (is(cmd, "pause"))
            wait(num(&p, 0));
        else if (is(cmd, "vol")) {
            DWORD l = num(&p, 1), r = num(&p, 1);
            lg("vol %04lX %04lX: waveOutSetVolume = %u", l, r, waveOutSetVolume(0, (r << 16) | l));
        } else if (is(cmd, "rec")) {
            DWORD ms = num(&p, 0), rate = num(&p, 0);
            WORD bits = (WORD)num(&p, 0);
            rec(ms, rate, bits, (WORD)num(&p, 0));
        } else if (is(cmd, "notes"))
            notes();
        else if (is(cmd, "mci")) {
            while (*p == ' ')
                p++;
            mci(p);
        }
    }
    lg("SBP end");
    DestroyWindow(hwnd);
    return 0;
}
