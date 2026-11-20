/*
 * wave.c - w16's SB16.DRV: wave output and input (wodMessage,
 * widMessage; drivers/sb16/README.md, ARCHITECTURE.md 24).
 *
 * The card plays and records from one DMA buffer in conventional memory,
 * auto-initialised, in two halves: the DSP interrupts after each half,
 * and the half just finished is filled from the queued headers (output)
 * or emptied into them (input) while the other plays. A half holds about
 * 1/16 s (256 to 4096 bytes). Output keeps two counts of the stream of
 * bytes the card plays (silence after the last header included): wpos,
 * filled so far, and dpos, played so far; each header's "reserved" field
 * gets the stream position after its last byte when it has been copied
 * for the last time (a loop's headers when the loop ends), and the header
 * is done when dpos has passed it. When nothing is left the card is
 * stopped after a half of silence.
 *
 * Sources: Multimedia Programmer's Guide ch. 5 (the wave functions, the
 * headers and their flags, loops, positions, WOM_* and WIM_* messages);
 * what MMSYSTEM sends and what Windows 3.1's SNDBLST2.DRV answers, as
 * observed (tests/mmsystem, ARCHITECTURE.md 23.2 and 24.1): a NULL or bad
 * format 32, a second open 4, PREPARE/UNPREPARE 8 (MMSYSTEM prepares), the
 * header's flags (INQUEUE while queued, DONE after, bit 31 while begun),
 * WOM_OPEN and WOM_CLOSE inside the open and close, done callbacks at
 * interrupt time and, from a reset, inside it (the headers not begun
 * first), a close with headers queued: 33 when paused, else after they
 * have played; GETPOS: size below 8 answered 1, NULL 0, samples for a
 * time format it does not keep (SB16.DRV keeps milliseconds too); wave
 * input: ADDBUFFER clears dwBytesRecorded and DONE, STOP keeps the queue,
 * RESET returns every header. The card: Creative's Sound Blaster Series
 * Hardware Programming Guide (DSP 4.xx: 41h/42h the output and input
 * rate, B6h/BEh 16-bit and C6h/CEh 8-bit auto-initialised output and
 * input with the FIFO, the mode byte: bit 4 signed, bit 5 stereo, the
 * length in samples less one; D0h/D5h pause, D4h/D6h continue).
 */
#include "sb16.h"

struct wdev {
    BYTE open, running, paused, sixteen, stereo;
    struct cbinfo cb;
    DWORD rate, avg;
    WORD align;
    LPWHDR head, tail;                  /* the queue */
    LPWHDR cur;                         /* output: the header being copied */
    DWORD curoff;                       /* and where in it */
    LPWHDR loopb;                       /* the loop's first header */
    DWORD loopsleft, passbytes;
    WORD half;                          /* bytes in a half of the buffer */
    BYTE khalf;                         /* the half the next interrupt ends */
    WORD hdata[2];                      /* output: the bytes of data in each */
    DWORD wpos, dpos;                   /* output: stream filled, played */
    DWORD pos;                          /* data played / recorded */
};
static struct wdev wo, wi;

#pragma pack(__push, 1)
struct woutcaps {
    WORD mid, pid, ver;
    char name[32];
    DWORD formats;
    WORD channels;
    DWORD support;
};
struct wincaps {
    WORD mid, pid, ver;
    char name[32];
    DWORD formats;
    WORD channels;
};
#pragma pack(__pop)
/* manufacturer and product IDs: Microsoft's for the Sound Blaster's wave
   devices (the SDK's MMSYSTEM.H, as SNDBLST2 reports them); every
   format of the SDK's list (1-4 M/S 08/16), two channels; volume, left
   and right, through the mixer */
static const struct woutcaps ocaps = { 1, 6, 0x0100, "Sound Blaster 16", 0x0FFFL, 2, 0x000CL };
static const struct wincaps icaps = { 1, 7, 0x0100, "Sound Blaster 16", 0x0FFFL, 2 };

void wave_init(void)
{
}

static WORD ch(struct wdev *w)
{
    return w->sixteen ? sb_dma16 : sb_dma8;
}

/* ---- the format: PCM, 1 or 2 channels, 8 or 16 bits, 5000-44100 Hz,
   nBlockAlign the size of a sample (SNDBLST2 refuses 0 there too) ---- */
static int fmtok(PCMFMT FAR *f)
{
    if (f->wFormatTag != 1)
        return 0;
    if (f->nChannels != 1 && f->nChannels != 2)
        return 0;
    if (f->wBitsPerSample != 8 && f->wBitsPerSample != 16)
        return 0;
    if (f->nSamplesPerSec < 5000 || f->nSamplesPerSec > 44100L)
        return 0;
    if (f->nBlockAlign != f->nChannels * (f->wBitsPerSample >> 3))
        return 0;
    return 1;
}

static void setfmt(struct wdev *w, PCMFMT FAR *f)
{
    w->sixteen = f->wBitsPerSample == 16;
    w->stereo = f->nChannels == 2;
    w->rate = f->nSamplesPerSec;
    w->align = f->nBlockAlign;
    w->avg = w->rate * w->align;
}

static WORD halfsize(struct wdev *w)
{
    DWORD n = (w->avg >> 4) & ~3L;
    if (n < 256)
        n = 256;
    if (n > DMABUF / 2)
        n = DMABUF / 2;
    return (WORD)n;
}

/* the card started on the buffer (both halves ready for output) */
static void hwstart(struct wdev *w, int rec)
{
    WORD len;
    dma_start(ch(w), dmaphys, w->half << 1, rec);
    dsp_write(rec ? 0x42 : 0x41);
    dsp_write((BYTE)(w->rate >> 8));
    dsp_write((BYTE)w->rate);
    if (w->sixteen)
        dsp_write(rec ? 0xBE : 0xB6);
    else
        dsp_write(rec ? 0xCE : 0xC6);
    dsp_write((BYTE)((w->sixteen ? 0x10 : 0) | (w->stereo ? 0x20 : 0)));
    len = (w->sixteen ? w->half >> 1 : w->half) - 1;
    dsp_write((BYTE)len);
    dsp_write((BYTE)(len >> 8));
    w->running = 1;
}

static void hwstop(struct wdev *w)
{
    dsp_reset();
    dma_stop(ch(w));
    dsp_write(0xD1);
    w->running = 0;
}

/* the bytes of the half under way that the card has done */
static WORD progress(struct wdev *w)
{
    WORD into = (w->half << 1) - dma_left(ch(w));
    if (w->khalf == 0)
        return into > w->half ? w->half : into;
    return into >= w->half ? into - w->half : w->half;
}

/* ---- output ---- */

/* the cursor past its header: back to the loop's start, or on */
static void advance(DWORD at)
{
    LPWHDR h = wo.cur, x;
    wo.curoff = 0;
    if ((h->dwFlags & WHDR_ENDLOOP) && wo.loopb) {
        if (wo.loopsleft && wo.passbytes) {
            wo.loopsleft--;
            wo.passbytes = 0;
            wo.cur = wo.loopb;
            return;
        }
        for (x = wo.loopb; x; x = x->lpNext) {
            x->reserved = at;
            if (x == h)
                break;
        }
        wo.loopb = 0;
    } else if (!wo.loopb)
        h->reserved = at;
    wo.cur = h->lpNext;
}

/* half K from the queue, silence after the last header */
static void fill(int k)
{
    LPSTR d = dmabuf + (k ? wo.half : 0);
    WORD n = 0, room = wo.half, c;
    LPWHDR h;
    DWORD left;
    while (wo.cur) {
        h = wo.cur;
        if (!(h->dwFlags & WHDR_STARTED)) {
            h->dwFlags |= WHDR_STARTED;
            if ((h->dwFlags & WHDR_BEGINLOOP) && !wo.loopb) {
                wo.loopb = h;
                wo.loopsleft = h->dwLoops > 1 ? h->dwLoops - 1 : 0;
                wo.passbytes = 0;
            }
        }
        left = h->dwBufferLength - wo.curoff;
        if (left && !room)
            break;
        c = left > room ? room : (WORD)left;
        if (c) {
            sb_hcopy(d + n, h->lpData, wo.curoff, c, 0);
            n += c;
            room -= c;
            wo.curoff += c;
            wo.passbytes += c;
        }
        if (wo.curoff >= h->dwBufferLength)
            advance(wo.wpos + n);
    }
    if (room) {
        BYTE s = wo.sixteen ? 0 : 0x80;
        for (c = n; c < wo.half; c++)
            d[c] = s;
    }
    wo.hdata[k] = n;
    wo.wpos += wo.half;
}

/* the headers played to the end, in order: done */
static void retire(void)
{
    LPWHDR h;
    WORD f;
    for (;;) {
        f = sb_cli();
        h = wo.head;
        if (!h || h == wo.cur || h == wo.loopb || h->reserved > wo.dpos) {
            sb_sti(f);
            return;
        }
        wo.head = h->lpNext;
        if (!wo.head)
            wo.tail = 0;
        sb_sti(f);
        h->lpNext = 0;
        h->dwFlags = (h->dwFlags & ~(WHDR_INQUEUE | WHDR_STARTED)) | WHDR_DONE;
        notify(&wo.cb, MM_WOM_DONE, (DWORD)h, 0);
    }
}

static void wo_start(void)
{
    wo.half = halfsize(&wo);
    wo.khalf = 0;
    wo.wpos = wo.dpos = 0;
    wo.cur = wo.head;
    wo.curoff = 0;
    fill(0);
    fill(1);
    hwstart(&wo, 0);
}

/* an output interrupt: the half done counted, the headers done returned,
   the half filled again; stopped after silence with nothing queued */
static void wo_irq(void)
{
    int k = wo.khalf;
    wo.khalf ^= 1;
    wo.dpos += wo.half;
    wo.pos += wo.hdata[k];
    retire();
    if (!wo.running)
        return;                         /* (a callback reset it) */
    fill(k);
    if (!wo.head && !wo.hdata[0] && !wo.hdata[1])
        hwstop(&wo);
}

static DWORD wo_write(LPWHDR h)
{
    WORD f;
    h->lpNext = 0;
    h->reserved = 0;
    h->dwFlags = (h->dwFlags | WHDR_INQUEUE) & ~(WHDR_DONE | WHDR_STARTED);
    f = sb_cli();
    if (wo.tail)
        wo.tail->lpNext = h;
    else
        wo.head = h;
    wo.tail = h;
    if (!wo.cur && wo.running)
        wo.cur = h;
    if (!wo.running && !wo.paused)
        wo_start();
    sb_sti(f);
    return 0;
}

/* everything queued returned (the headers not begun first, as SNDBLST2),
   the position 0 */
static void wo_reset(void)
{
    LPWHDR h, nx, a = 0, at = 0, b = 0, bt = 0;
    WORD f = sb_cli();
    if (wo.running)
        hwstop(&wo);
    h = wo.head;
    wo.head = wo.tail = wo.cur = wo.loopb = 0;
    wo.pos = 0;
    wo.paused = 0;
    sb_sti(f);
    for (; h; h = nx) {
        nx = h->lpNext;
        h->lpNext = 0;
        if (h->dwFlags & WHDR_STARTED) {
            if (bt)
                bt->lpNext = h;
            else
                b = h;
            bt = h;
        } else {
            if (at)
                at->lpNext = h;
            else
                a = h;
            at = h;
        }
    }
    if (at)
        at->lpNext = b;
    else
        a = b;
    for (h = a; h; h = nx) {
        nx = h->lpNext;
        h->lpNext = 0;
        h->dwFlags = (h->dwFlags & ~(WHDR_INQUEUE | WHDR_STARTED)) | WHDR_DONE;
        notify(&wo.cb, MM_WOM_DONE, (DWORD)h, 0);
    }
}

/* a close while headers are queued: 33 when paused; otherwise they play
   to the end first (as SNDBLST2: it waits), at most their length and a
   second */
static DWORD wo_close(void)
{
    LPWHDR h;
    DWORD bytes = 0, until;
    if (wo.head) {
        if (wo.paused || !wo.running)
            return WAVERR_STILLPLAYING;
        for (h = wo.head; h; h = h->lpNext)
            bytes += h->dwBufferLength;
        until = GetTickCount() + bytes * 1000 / wo.avg + 1000;
        while (wo.head && (long)(GetTickCount() - until) < 0)
            ;
        if (wo.head)
            return WAVERR_STILLPLAYING;
    }
    if (wo.running) {
        WORD f = sb_cli();
        hwstop(&wo);
        sb_sti(f);
    }
    wo.open = 0;
    notify(&wo.cb, MM_WOM_CLOSE, 0, 0);
    return 0;
}

/* a position: bytes, samples or milliseconds; size below 8 1, NULL 0
   (SNDBLST2's answers); other formats as samples */
static DWORD getpos(struct wdev *w, MMT FAR *t, DWORD size, int out)
{
    DWORD p;
    WORD f;
    if (size < sizeof(MMT))
        return MMSYSERR_ERROR;
    if (!t)
        return 0;
    f = sb_cli();
    p = w->pos;
    if (w->running && (out || wi.head)) {
        /* the half under way: what of it is data (output), or what a
           queued header will take of it (input: without one the bytes
           are not kept, and not counted, as SNDBLST2's) */
        WORD g = progress(w);
        if (out && g > wo.hdata[wo.khalf])
            g = wo.hdata[wo.khalf];
        if (!out && g > wi.head->dwBufferLength - wi.head->dwBytesRecorded)
            g = (WORD)(wi.head->dwBufferLength - wi.head->dwBytesRecorded);
        p += g;
    }
    sb_sti(f);
    switch (t->wType) {
    case TIME_BYTES:
        t->value = p;
        break;
    case TIME_MS:
        t->value = w->avg ? (p / w->avg) * 1000 + (p % w->avg) * 1000 / w->avg : 0;
        break;
    default:
        t->wType = TIME_SAMPLES;
        t->value = p / w->align;
        break;
    }
    return 0;
}

static DWORD wopen(struct wdev *w, WORD dev, DWORD FAR *user, WOPENDESC FAR *d, DWORD flags, WORD msg)
{
    if (dev)
        return MMSYSERR_BADDEVICEID;
    if (!d->lpFormat || !fmtok(d->lpFormat))
        return WAVERR_BADFORMAT;
    if (flags & WAVE_FORMAT_QUERY)
        return 0;
    if (!sb_ok)
        return MMSYSERR_NOTENABLED;
    if (wo.open || wi.open)
        return MMSYSERR_ALLOCATED;
    setfmt(w, d->lpFormat);
    w->cb.h = d->hWave;
    w->cb.cb = d->dwCallback;
    w->cb.inst = d->dwInstance;
    w->cb.flags = (HIWORD(flags) & 7) | DCB_NOSWITCH;
    w->head = w->tail = w->cur = w->loopb = 0;
    w->running = w->paused = 0;
    w->pos = 0;
    w->half = halfsize(w);
    w->open = 1;
    if (user)
        *user = (WORD)w;               /* its offset, as SNDBLST2 gives */
    notify(&w->cb, msg, 0, 0);
    return 0;
}

DWORD ENTRY WODMESSAGE(WORD dev, WORD msg, DWORD user, DWORD p1, DWORD p2)
{
    WORD f;
    switch (msg) {
    case WODM_GETNUMDEVS:
        return sb_ok ? 1 : 0;
    case WODM_GETDEVCAPS:
        if (dev)
            return MMSYSERR_BADDEVICEID;
        copycaps((LPVOID)p1, &ocaps, sizeof ocaps, p2);
        return 0;
    case WODM_OPEN:
        return wopen(&wo, dev, (DWORD FAR *)user, (WOPENDESC FAR *)p1, p2, MM_WOM_OPEN);
    case WODM_GETVOLUME:
        if (!sb_ok)
            return MMSYSERR_NOTENABLED;
        if (!p1)
            return MMSYSERR_INVALPARAM;
        *(DWORD FAR *)p1 = vol_get(VOL_WAVE);
        return 0;
    case WODM_SETVOLUME:
        if (!sb_ok)
            return MMSYSERR_NOTENABLED;
        vol_set(VOL_WAVE, p1);
        return 0;
    }
    if (!wo.open || user != (WORD)&wo)
        return MMSYSERR_NOTSUPPORTED;
    switch (msg) {
    case WODM_CLOSE:
        return wo_close();
    case WODM_WRITE:
        if (!(((LPWHDR)p1)->dwFlags & WHDR_PREPARED))
            return 34;                  /* WAVERR_UNPREPARED */
        return wo_write((LPWHDR)p1);
    case WODM_PAUSE:
        f = sb_cli();
        if (wo.running && !wo.paused)
            dsp_write(wo.sixteen ? 0xD5 : 0xD0);
        wo.paused = 1;
        sb_sti(f);
        return 0;
    case WODM_RESTART:
        f = sb_cli();
        if (wo.paused) {
            wo.paused = 0;
            if (wo.running)
                dsp_write(wo.sixteen ? 0xD6 : 0xD4);
            else if (wo.head)
                wo_start();
        }
        sb_sti(f);
        return 0;
    case WODM_RESET:
        wo_reset();
        return 0;
    case WODM_BREAKLOOP:
        f = sb_cli();
        if (wo.loopb)
            wo.loopsleft = 0;
        sb_sti(f);
        return 0;
    case WODM_GETPOS:
        return getpos(&wo, (MMT FAR *)p1, p2, 1);
    }
    return MMSYSERR_NOTSUPPORTED;       /* PREPARE, UNPREPARE, pitch, rate */
}

/* ---- input ---- */

/* N bytes recorded into the queued headers; a full one is done */
static void store(LPSTR s, WORD n)
{
    LPWHDR h;
    DWORD room;
    WORD c;
    while (n && (h = wi.head) != 0) {
        room = h->dwBufferLength - h->dwBytesRecorded;
        c = room > n ? n : (WORD)room;
        if (c) {
            sb_hcopy(s, h->lpData, h->dwBytesRecorded, c, 1);
            h->dwBytesRecorded += c;
            wi.pos += c;
            s += c;
            n -= c;
        }
        if (h->dwBytesRecorded >= h->dwBufferLength) {
            WORD f = sb_cli();
            wi.head = h->lpNext;
            if (!wi.head)
                wi.tail = 0;
            sb_sti(f);
            h->lpNext = 0;
            h->dwFlags = (h->dwFlags & ~WHDR_INQUEUE) | WHDR_DONE;
            notify(&wi.cb, MM_WIM_DATA, (DWORD)h, 0);
        }
    }
}

static void wi_irq(void)
{
    int k = wi.khalf;
    wi.khalf ^= 1;
    store(dmabuf + (k ? wi.half : 0), wi.half);
}

/* recording stopped: the part of the half under way kept */
static void wi_halt(void)
{
    WORD f = sb_cli(), g;
    if (!wi.running) {
        sb_sti(f);
        return;
    }
    g = progress(&wi);
    hwstop(&wi);
    sb_sti(f);
    if (g)
        store(dmabuf + (wi.khalf ? wi.half : 0), g);
}

/* the first header returned when it holds something (the SDK's
   waveInStop); RESET returns them all */
static void wi_return(int all)
{
    LPWHDR h;
    WORD f;
    for (;;) {
        f = sb_cli();
        h = wi.head;
        if (!h || (!all && !h->dwBytesRecorded)) {
            sb_sti(f);
            return;
        }
        wi.head = h->lpNext;
        if (!wi.head)
            wi.tail = 0;
        sb_sti(f);
        h->lpNext = 0;
        h->dwFlags = (h->dwFlags & ~WHDR_INQUEUE) | WHDR_DONE;
        notify(&wi.cb, MM_WIM_DATA, (DWORD)h, 0);
        if (!all)
            return;
    }
}

DWORD ENTRY WIDMESSAGE(WORD dev, WORD msg, DWORD user, DWORD p1, DWORD p2)
{
    LPWHDR h;
    WORD f;
    switch (msg) {
    case WIDM_GETNUMDEVS:
        return sb_ok ? 1 : 0;
    case WIDM_GETDEVCAPS:
        if (dev)
            return MMSYSERR_BADDEVICEID;
        copycaps((LPVOID)p1, &icaps, sizeof icaps, p2);
        return 0;
    case WIDM_OPEN:
        return wopen(&wi, dev, (DWORD FAR *)user, (WOPENDESC FAR *)p1, p2, MM_WIM_OPEN);
    }
    if (!wi.open || user != (WORD)&wi)
        return MMSYSERR_NOTSUPPORTED;
    switch (msg) {
    case WIDM_CLOSE:
        if (wi.head)
            return WAVERR_STILLPLAYING;
        wi_halt();
        wi.open = 0;
        notify(&wi.cb, MM_WIM_CLOSE, 0, 0);
        return 0;
    case WIDM_ADDBUFFER:
        h = (LPWHDR)p1;
        if (!(h->dwFlags & WHDR_PREPARED))
            return 34;
        h->lpNext = 0;
        h->dwBytesRecorded = 0;
        h->reserved = 0;
        h->dwFlags = (h->dwFlags | WHDR_INQUEUE) & ~WHDR_DONE;
        f = sb_cli();
        if (wi.tail)
            wi.tail->lpNext = h;
        else
            wi.head = h;
        wi.tail = h;
        sb_sti(f);
        return 0;
    case WIDM_START:
        f = sb_cli();
        if (!wi.running) {
            wi.half = halfsize(&wi);
            wi.khalf = 0;
            hwstart(&wi, 1);
        }
        sb_sti(f);
        return 0;
    case WIDM_STOP:
        wi_halt();
        wi_return(0);
        return 0;
    case WIDM_RESET:
        wi_halt();
        wi_return(1);
        wi.pos = 0;
        return 0;
    case WIDM_GETPOS:
        return getpos(&wi, (MMT FAR *)p1, p2, 0);
    }
    return MMSYSERR_NOTSUPPORTED;
}

/* an interrupt of the DMA channel of the given width */
void wave_irq(int sixteen)
{
    if (wo.running && wo.sixteen == (BYTE)sixteen)
        wo_irq();
    else if (wi.running && wi.sixteen == (BYTE)sixteen)
        wi_irq();
}
