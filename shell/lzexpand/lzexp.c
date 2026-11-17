/*
 * lzexp.c - w16's LZEXPAND.DLL: reading files compressed by COMPRESS.EXE
 * (ARCHITECTURE.md, LZEXPAND).
 *
 * Sources: Windows 3.1 SDK, Programmer's Reference Vol. 1 ch. 9, Vol. 2
 * (the ten functions); the publicly documented SZDD format; and black-box
 * observation of 3.1's LZEXPAND with tests/lzexpand/lzp.c, which is where
 * everything the SDK does not say comes from: the handle values, the
 * order slots are taken in, LZOpenFile's second try with the compressed
 * name, GetExpandedName's case rule, how LZRead expands (in passes, see
 * lzexpand.h) and what it does at the end of the data and with matches
 * longer than COMPRESS writes, LZCopy's results.
 */
#include "lzexpand.h"

static HGLOBAL slots[LZ_SLOTS];         /* the compressed files open */
static int lastslot = -1;               /* the slot taken last */

static const BYTE magic[8] = { 'S', 'Z', 'D', 'D', 0x88, 0xF0, 0x27, 0x33 };

/* ---- small helpers (no C runtime) ---- */
static int same(const BYTE FAR *a, const BYTE FAR *b, int n)
{
    while (n--)
        if (*a++ != *b++)
            return 0;
    return 1;
}

static long hdr_size(const BYTE FAR *h)
{
    return (long)h[10] | ((long)h[11] << 8) | ((long)h[12] << 16) | ((long)h[13] << 24);
}

/* ---- the decoder ---- */
static void dec_reset(LZS FAR *s)
{
    WORD i;
    for (i = 0; i < RING; i++)
        s->ring[i] = ' ';
    s->count = 0;
    s->passA = 0;
    s->rpos = RING_START;
    s->inoff = HDR_SIZE;
    s->inlen = 0;
    s->inpos = 0;
    s->nflags = 0;
    s->pend = 0;
    s->shortnow = 0;
    s->needseek = 1;
}

/* the next byte of compressed data, or -1 at its end. The file is read
   in s->inmax pieces; its pointer is set only when expanding starts from
   the beginning, after that each piece is read from wherever the pointer
   is (observed: a caller's _llseek between reads changes what follows) */
static int getb(LZS FAR *s)
{
    if (s->inpos >= s->inlen) {
        UINT n;
        s->inoff += s->inlen;
        s->inpos = 0;
        s->inlen = 0;
        if (s->needseek) {
            s->needseek = 0;
            if (_llseek(s->dos, s->inoff, 0) == -1L)
                return -1;
        }
        n = _lread(s->dos, s->inp, s->inmax);
        if (n == 0 || n == (UINT)HFILE_ERROR)
            return -1;
        /* a file that ends within its first piece: the pass ends with
           this item (observed; not when a later piece is the last) */
        if (n < s->inmax && s->inoff == HDR_SIZE)
            s->shortnow = 1;
        s->inlen = n;
    }
    return s->inp[s->inpos++];
}

/* read the next item into s->pend: 0, or IT_END at the end of the data
   before an item, IT_CUT inside a match */
#define IT_END 1
#define IT_CUT 2
static int item(LZS FAR *s)
{
    int b, hi;
    if (s->pend)
        return 0;
    if (s->nflags == 0) {
        b = getb(s);
        if (b < 0)
            return IT_END;
        s->flags = (BYTE)b;
        s->nflags = 8;
    }
    b = getb(s);
    if (b < 0)
        return IT_END;
    if (s->flags & 1) {
        s->plen = 1;
        s->plit = (BYTE)b;
    } else {
        hi = getb(s);
        if (hi < 0)
            return IT_CUT;
        s->plen = (BYTE)((hi & 15) + 3);
        s->poff = (WORD)b | ((WORD)(hi & 0xF0) << 4);
    }
    s->flags >>= 1;
    s->nflags--;
    s->pend = 1;
    return 0;
}

/* put the pending item into the ring */
static void put(LZS FAR *s)
{
    WORD i, p;
    if (s->plen == 1) {
        s->ring[s->rpos] = s->plit;
        s->rpos = (s->rpos + 1) & (RING - 1);
    } else {
        p = s->poff;
        for (i = 0; i < s->plen; i++) {
            s->ring[s->rpos] = s->ring[p];
            s->rpos = (s->rpos + 1) & (RING - 1);
            p = (p + 1) & (RING - 1);
        }
    }
    s->count += s->plen;
    s->pend = 0;
}

/* the expanded byte at position p (still in the ring) */
#define RINGAT(s, p) ((s)->ring[((WORD)(p) + RING_START) & (RING - 1)])

/* ---- the handle table ---- */
static LZS FAR *lock(HFILE h, int *err)
{
    LZS FAR *s;
    if (!slots[h - LZ_BASE]) {
        *err = LZERROR_GLOBLOCK;
        return 0;
    }
    s = (LZS FAR *)GlobalLock(slots[h - LZ_BASE]);
    if (!s)
        *err = LZERROR_GLOBLOCK;
    return s;
}

static void unlock(HFILE h)
{
    GlobalUnlock(slots[h - LZ_BASE]);
}

/* a slot for a compressed file: the next free one after the one taken
   last (observed: 3.1 goes round the 16, it does not take the lowest) */
static HFILE slot_alloc(HFILE dos, long size)
{
    int i = 0, k;
    HGLOBAL g;
    LZS FAR *s;
    for (k = 1; k <= LZ_SLOTS; k++) {
        i = (lastslot + k) % LZ_SLOTS;
        if (!slots[i])
            break;
    }
    if (k > LZ_SLOTS)
        return LZERROR_GLOBALLOC;
    /* the library's, not the caller's: handles outlive the task that
       opened them (observed) */
    g = GlobalAlloc(GMEM_MOVEABLE | GMEM_SHARE, sizeof(LZS));
    if (!g)
        return LZERROR_GLOBALLOC;
    s = (LZS FAR *)GlobalLock(g);
    if (!s) {
        GlobalFree(g);
        return LZERROR_GLOBLOCK;
    }
    s->dos = dos;
    s->size = size;
    s->want = 0;
    s->bad = 0;
    s->eoferr = 0;
    s->inp = s->in;
    s->inmax = INBUF;
    dec_reset(s);
    GlobalUnlock(g);
    slots[i] = g;
    lastslot = i;
    return LZ_BASE + i;
}

/* ---- LZInit ---- */
HFILE LZAPI LZInit(HFILE hf)
{
    BYTE hdr[HDR_SIZE];
    UINT n;
    if (_llseek(hf, 0L, 0) == -1L)
        return LZERROR_BADINHANDLE;
    n = _lread(hf, hdr, HDR_SIZE);
    if (n != HDR_SIZE || !same(hdr, magic, 8)) {
        _llseek(hf, 0L, 0);             /* not compressed: as it was */
        return hf;
    }
    if (hdr[8] != 'A')
        return LZERROR_UNKNOWNALG;
    return slot_alloc(hf, hdr_size(hdr));
}

/* ---- LZOpenFile ---- */

/* the name COMPRESS gives a file: its extension's last character made
   '_', or '_' added to a shorter one, "._" without one (observed in the
   second try: "X.ABC" "X.AB_", "X.A" "X.A_", "X" "X._") */
static void cname(char *d, LPCSTR name)
{
    int n = 0, dot = -1;
    while (name[n] && n < 127) {
        d[n] = name[n];
        if (d[n] == '\\' || d[n] == '/' || d[n] == ':')
            dot = -1;
        else if (d[n] == '.')
            dot = n;
        n++;
    }
    if (dot < 0) {
        d[n++] = '.';
        d[n++] = '_';
    } else if (n - dot - 1 >= 3) {
        d[n - 1] = '_';
    } else {
        d[n++] = '_';
    }
    d[n] = 0;
}

HFILE LZAPI LZOpenFile(LPCSTR name, OFSTRUCT FAR *of, UINT style)
{
    HFILE h, r;
    char cn[132];
    h = OpenFile(name, of, style);
    if (h == HFILE_ERROR) {
        /* then the compressed file's name (observed: "ONLYC.TXT" opens
           ONLYC.TX_ when only that exists) */
        cname(cn, name);
        h = OpenFile(cn, of, style);
        if (h == HFILE_ERROR)
            return HFILE_ERROR;
    }
    /* SDK: OF_READ, alone or with OF_SHARE_ flags, expands */
    if ((style & ~0x0070) != OF_READ)
        return h;
    r = LZInit(h);
    if (r < 0)
        _lclose(h);
    return r;
}

/* ---- LZSeek ---- */
LONG LZAPI LZSeek(HFILE h, LONG off, int org)
{
    LZS FAR *s;
    long np;
    int err;
    if (!IS_LZ(h))
        return _llseek(h, off, org);
    s = lock(h, &err);
    if (!s)
        return err;
    if (org == 0)
        np = off;
    else if (org == 1)
        np = s->want + off;
    else if (org == 2)
        np = s->size + off;
    else
        np = -1;
    if (np < 0) {
        unlock(h);
        return LZERROR_BADVALUE;
    }
    s->want = np;
    unlock(h);
    return np;
}

/* ---- LZRead ---- */

/* copy expanded bytes [lo, hi) out of the ring */
static void out(LZS FAR *s, BYTE FAR *ub, int *got, long lo, long hi)
{
    while (lo < hi) {
        ub[*got] = RINGAT(s, lo);
        (*got)++;
        lo++;
    }
}

int LZAPI LZRead(HFILE h, void FAR *buf, int cb)
{
    LZS FAR *s;
    BYTE FAR *ub = (BYTE FAR *)buf;
    long E, A, c, lo, hi;
    int got = 0, err, r, lim;
    if (!IS_LZ(h)) {
        if (cb < 0)
            return LZERROR_BADVALUE;
        return (int)_lread(h, buf, cb);
    }
    s = lock(h, &err);
    if (!s)
        return err;
    if (cb < 0) {
        unlock(h);
        return LZERROR_BADVALUE;
    }
    if (s->want >= s->size) {
        unlock(h);
        return 0;
    }
    E = s->want + cb;
    /* after the data ended, a read from where that pass began fails
       again, and so does one from further on if the data ended between
       items (observed; what 3.1 returns otherwise then is not reproduced:
       ARCHITECTURE.md, LZEXPAND) */
    if (s->eoferr) {
        if (s->want == s->eofA || (s->eoferr == IT_END && s->want > s->eofA)) {
            unlock(h);
            return LZERROR_READ;
        }
        s->eoferr = 0;
    }
    if (s->bad) {
        dec_reset(s);
        s->bad = 0;
    }
    /* what the last pass expanded beyond the last read is still in the
       ring; before that, expanding starts again from the beginning */
    if (s->want < s->count) {
        if (s->want >= s->passA) {
            hi = s->count < E ? s->count : E;
            out(s, ub, &got, s->want, hi);
            s->want = hi;
        } else {
            dec_reset(s);
        }
    }
    A = s->count;
    while (s->want < E) {
        for (;;) {
            c = s->count;
            /* one item past the end of the request is expanded too
               (observed), not past the size the header gives */
            if (c > E || c >= s->size)
                break;
            r = item(s);
            if (r) {
                if (c >= E)
                    break;
                goto eof;
            }
            lim = s->plen > 1 ? PASS_MATCH : PASS_LIT;
            if (c < s->want)
                lim -= PASS_SKIP;
            if (c - A >= lim)
                break;
            r = s->shortnow;
            s->shortnow = 0;
            put(s);
            if (s->count - A > PASS_MAX)
                goto over;
            if (r)
                break;
        }
        if (s->count == A)
            break;
        lo = A > s->want ? A : s->want;
        hi = s->count < E ? s->count : E;
        if (hi > lo) {
            out(s, ub, &got, lo, hi);
            s->want = hi;
        }
        s->passA = A;
        if (s->want >= E)
            break;
        A = s->count;
    }
    unlock(h);
    return got;

over:
    /* a pass longer than its buffer (only matches of 17 or 18 bytes, which
       COMPRESS does not write, make one): the read fails, though 3.1
       still expands to the end of the request first (observed: its file
       position); the position is where the pass began, and the next read
       expands from the beginning again */
    while (s->count <= E && s->count < s->size && !item(s))
        put(s);
    if (A > s->want)
        s->want = A;
    s->bad = 1;
    unlock(h);
    return LZERROR_READ;

eof:
    /* the data ends before the size in the header */
    if (A > s->want)
        s->want = A;
    s->bad = 1;
    s->eoferr = r;
    s->eofA = s->want;
    unlock(h);
    return LZERROR_READ;
}

/* ---- LZClose ---- */
void LZAPI LZClose(HFILE h)
{
    LZS FAR *s;
    int err;
    if (!IS_LZ(h)) {
        _lclose(h);
        return;
    }
    s = lock(h, &err);
    if (!s)
        return;
    _lclose(s->dos);
    unlock(h);
    GlobalFree(slots[h - LZ_BASE]);
    slots[h - LZ_BASE] = 0;
}

/* ---- LZCopy, CopyLZFile ---- */

/* a copy's buffers: a decoder, 32K of input, 32K of output (observed:
   where LZCopy leaves the source, and that a stream cut inside a match
   at 17632 bytes still writes nothing); LZStart keeps them (SDK) */
#define B_DEC 0
#define B_IN  1
#define B_OUT 2
static HGLOBAL startbuf[3];
static const DWORD bufsize[3] = { sizeof(LZS), CPYIN, OUTBUF };

static void bufs_free(HGLOBAL *g)
{
    int i;
    for (i = 0; i < 3; i++)
        if (g[i]) {
            GlobalFree(g[i]);
            g[i] = 0;
        }
}

static int bufs_alloc(HGLOBAL *g, UINT fl)
{
    int i;
    for (i = 0; i < 3; i++)
        g[i] = 0;
    for (i = 0; i < 3; i++) {
        g[i] = GlobalAlloc(fl, bufsize[i]);
        if (!g[i]) {
            bufs_free(g);
            return LZERROR_GLOBALLOC;
        }
    }
    return 0;
}

static long put_out(HFILE dst, BYTE FAR *b, WORD n)
{
    UINT w;
    if (!n)
        return 0;
    w = _lwrite(dst, b, n);
    if (w == (UINT)HFILE_ERROR)
        return LZERROR_BADOUTHANDLE;
    if (w != n)
        return LZERROR_WRITE;
    return 0;
}

static long copy(HFILE src, HFILE dst)
{
    BYTE hdr[HDR_SIZE];
    HFILE dos;
    long size = 0, total = 0, e;
    int packed, err, r;
    WORD n, i, p;
    HGLOBAL own[3], *g;
    BYTE FAR *out;
    LZS FAR *s;
    if (IS_LZ(src)) {
        LZS FAR *h;
        if (!slots[src - LZ_BASE])
            return LZERROR_BADINHANDLE;
        h = lock(src, &err);
        if (!h)
            return err;
        dos = h->dos;
        size = h->size;
        unlock(src);
        packed = 1;
    } else {
        dos = src;
        if (_llseek(dos, 0L, 0) == -1L)
            return LZERROR_BADINHANDLE;
        n = _lread(dos, hdr, HDR_SIZE);
        packed = n == HDR_SIZE && same(hdr, magic, 8);
        if (packed) {
            if (hdr[8] != 'A')
                return LZERROR_UNKNOWNALG;
            size = hdr_size(hdr);
        }
    }
    g = startbuf;
    if (!g[B_DEC]) {
        g = own;
        r = bufs_alloc(g, GMEM_MOVEABLE);
        if (r)
            return r;
    }
    s = (LZS FAR *)GlobalLock(g[B_DEC]);
    out = (BYTE FAR *)GlobalLock(g[B_OUT]);
    if (!s || !out) {
        total = LZERROR_GLOBLOCK;
        goto done;
    }
    /* the destination is written from its beginning (observed: what it
       held before is overwritten) */
    _llseek(dst, 0L, 0);
    if (!packed) {
        _llseek(dos, 0L, 0);
        for (;;) {
            n = _lread(dos, out, OUTBUF);
            if (n == 0 || n == (UINT)HFILE_ERROR)
                break;
            e = put_out(dst, out, n);
            if (e) {
                total = e;
                goto done;
            }
            total += n;
        }
        goto done;
    }
    s->dos = dos;
    s->size = size;
    s->inp = (BYTE FAR *)GlobalLock(g[B_IN]);
    s->inmax = CPYIN;
    if (!s->inp) {
        total = LZERROR_GLOBLOCK;
        goto done;
    }
    dec_reset(s);
    n = 0;
    while (s->count < size) {
        r = item(s);
        if (r == IT_CUT) {
            /* the data ends inside a match: what is still in the buffer
               is not written, and the destination's pointer goes back to
               its beginning (observed) */
            _llseek(dst, 0L, 0);
            total = LZERROR_READ;
            goto done;
        }
        if (r == IT_END)
            break;
        p = s->rpos;
        i = s->plen;
        put(s);
        while (i--) {
            out[n++] = s->ring[p];
            p = (p + 1) & (RING - 1);
        }
        if (n > OUTBUF - 18) {
            e = put_out(dst, out, n);
            if (e) {
                total = e;
                goto done;
            }
            n = 0;
        }
    }
    e = put_out(dst, out, n);
    if (e)
        total = e;
    else
        total = s->count == size ? size : LZERROR_READ;
done:
    for (i = 0; i < 3; i++)
        if (g[i])
            GlobalUnlock(g[i]);
    if (g == own)
        bufs_free(own);
    return total;
}

LONG LZAPI LZCopy(HFILE src, HFILE dst)
{
    return copy(src, dst);
}

LONG LZAPI CopyLZFile(HFILE src, HFILE dst)
{
    return copy(src, dst);
}

/* ---- LZStart, LZDone: the buffers of CopyLZFile ---- */
int LZAPI LZStart(void)
{
    if (!startbuf[B_DEC] && bufs_alloc(startbuf, GMEM_MOVEABLE | GMEM_SHARE))
        return LZERROR_GLOBALLOC;
    return 1;
}

void LZAPI LZDone(void)
{
    bufs_free(startbuf);
}

/* ---- GetExpandedName ---- */
int LZAPI GetExpandedName(LPCSTR src, LPSTR dst)
{
    OFSTRUCT of;
    HFILE h;
    BYTE hdr[HDR_SIZE];
    UINT n;
    int i, len, dot = -1, lower = 0;
    char c;
    h = OpenFile(src, &of, OF_READ);
    if (h == HFILE_ERROR)
        return LZERROR_BADINHANDLE;
    n = _lread(h, hdr, HDR_SIZE);
    _lclose(h);
    lstrcpy(dst, src);
    if (n != HDR_SIZE || !same(hdr, magic, 8))
        return TRUE;
    len = lstrlen(dst);
    for (i = 0; i < len; i++) {
        if (dst[i] == '\\' || dst[i] == '/' || dst[i] == ':')
            dot = -1;
        else if (dst[i] == '.')
            dot = i;
    }
    /* only a '_' ending an extension is replaced (observed: "UNDER_"
       and "NOUND.ABC" stay as they are) */
    if (dot < 0 || dst[len - 1] != '_')
        return TRUE;
    /* the character takes the case of the name's first letter, ASCII
       letters only (observed: "C:\LZ\text.tx_" gives 'T', "c:\..." 't',
       an E9h stays E9h) */
    for (i = 0; i < len; i++) {
        if (IsCharLower(dst[i])) {
            lower = 1;
            break;
        }
        if (IsCharUpper(dst[i]))
            break;
    }
    c = (char)hdr[9];
    if (lower && c >= 'A' && c <= 'Z')
        c += 'a' - 'A';
    else if (!lower && c >= 'a' && c <= 'z')
        c -= 'a' - 'A';
    dst[len - 1] = c;
    /* without a character the name loses its '.' too (observed) */
    if (!c && dst[len - 2] == '.')
        dst[len - 2] = 0;
    return TRUE;
}
