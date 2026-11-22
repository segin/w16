/*
 * hlp.c - w16 Windows Help: reading .HLP files (sources in hlp.h).
 *
 * A help file is a small file system: a 16-byte header (magic 00035F3Fh,
 * the directory's offset, the free list, the size), and internal files,
 * each behind a 9-byte FILEHEADER (reserved size, used size, flags). The
 * directory is a B+ tree of names and offsets. B+ trees start with a
 * 38-byte header (magic 293Bh, flags, page size, a 16-byte structure
 * string, the root page, the number of levels and of entries); index pages
 * hold (key, page) pairs after a 6-byte header, leaf pages entries after
 * an 8-byte one (free bytes, count, previous and next leaf).
 */
#include "hlp.h"

/* ---- small helpers (no C run time in the Win16 build) ---- */

WORD hlp_rw(LPCB p) { return (WORD)(p[0] | ((WORD)p[1] << 8)); }
DWORD hlp_rl(LPCB p) { return (DWORD)hlp_rw(p) | ((DWORD)hlp_rw(p + 2) << 16); }

WORD hlp_strlen(const char FAR *s)
{
    WORD n = 0;
    while (s[n])
        n++;
    return n;
}

void hlp_strcpyn(char FAR *d, const char FAR *s, WORD n)
{
    if (!n)
        return;
    while (--n && *s)
        *d++ = *s++;
    *d = 0;
}

void hlp_memcpy(void FAR *d, const void FAR *s, DWORD n)
{
    LPB dd = (LPB)d;
    LPCB ss = (LPCB)s;
    while (n--)
        *dd++ = *ss++;
}

void hlp_memset(void FAR *d, int c, DWORD n)
{
    LPB dd = (LPB)d;
    while (n--)
        *dd++ = (BYTE)c;
}

int hlp_strcmp(const char FAR *a, const char FAR *b)
{
    for (;; a++, b++) {
        if (*a != *b)
            return (int)(BYTE)*a - (int)(BYTE)*b;
        if (!*a)
            return 0;
    }
}

static int lower(int c)
{
    return c >= 'A' && c <= 'Z' ? c + 32 : c;
}

int hlp_stricmp(const char FAR *a, const char FAR *b)
{
    for (;; a++, b++) {
        int x = lower((BYTE)*a), y = lower((BYTE)*b);
        if (x != y)
            return x - y;
        if (!x)
            return 0;
    }
}

static int strcmpb(LPCB a, LPCB b)
{
    for (;; a++, b++) {
        if (*a != *b)
            return (int)*a - (int)*b;
        if (!*a)
            return 0;
    }
}

/* ---- compressed numbers (in LinkData1 and in pictures): the low bit of
   the first byte (word) says whether a second byte (word) follows ---- */

WORD hlp_cuword(LPCB FAR *pp)
{
    LPCB p = *pp;
    WORD v;
    if (p[0] & 1) {
        v = (WORD)((p[0] >> 1) + ((WORD)p[1] << 7));
        *pp = p + 2;
    } else {
        v = p[0] >> 1;
        *pp = p + 1;
    }
    return v;
}

short hlp_csword(LPCB FAR *pp)
{
    LPCB p = *pp;
    if (p[0] & 1) {
        *pp = p + 2;
        return (short)((LONG)(p[0] >> 1) + ((LONG)p[1] << 7) - 16384L);
    }
    *pp = p + 1;
    return (short)((p[0] >> 1) - 64);
}

DWORD hlp_culong(LPCB FAR *pp)
{
    LPCB p = *pp;
    WORD w = hlp_rw(p);
    if (w & 1) {
        *pp = p + 4;
        return (DWORD)(w >> 1) + ((DWORD)hlp_rw(p + 2) << 15);
    }
    *pp = p + 2;
    return w >> 1;
}

LONG hlp_cslong(LPCB FAR *pp)
{
    LPCB p = *pp;
    WORD w = hlp_rw(p);
    if (w & 1) {
        *pp = p + 4;
        return (LONG)((DWORD)(w >> 1) + ((DWORD)hlp_rw(p + 2) << 15)) - 67108864L;
    }
    *pp = p + 2;
    return (LONG)(w >> 1) - 16384L;
}

/* ---- decompression ----
 * LZ77 as help files use it: a flag byte, then eight items, its bits from
 * the lowest up: 0 a literal byte; 1 a word whose low 12 bits are a
 * distance back (minus one) and high 4 bits a length (minus three). */
DWORD hlp_lz77(LPCB src, DWORD n, LPB dst, DWORD max)
{
    DWORD o = 0;
    LPCB end = src + n;
    while (src < end) {
        BYTE flags = *src++;
        int bit;
        for (bit = 0; bit < 8 && src < end; bit++, flags >>= 1) {
            if (flags & 1) {
                WORD w, len, dist;
                if (src + 1 >= end)
                    return o;
                w = hlp_rw(src);
                src += 2;
                dist = (w & 0x0FFF) + 1;
                len = (w >> 12) + 3;
                if (dist > o)
                    return o;           /* corrupt */
                while (len-- && o < max) {
                    dst[o] = dst[o - dist];
                    o++;
                }
            } else if (o < max) {
                dst[o++] = *src++;
            } else {
                src++;
            }
        }
    }
    return o;
}

/* run lengths (pictures): a count byte; high bit set: that many (low 7
   bits) literal bytes follow; clear: the next byte, that many times */
DWORD hlp_runlen(LPCB src, DWORD n, LPB dst, DWORD max)
{
    DWORD o = 0;
    LPCB end = src + n;
    while (src < end) {
        BYTE c = *src++;
        if (c & 0x80) {
            c &= 0x7F;
            while (c-- && src < end) {
                if (o < max)
                    dst[o++] = *src;
                src++;
            }
        } else {
            BYTE v;
            if (src >= end)
                break;
            v = *src++;
            while (c-- && o < max)
                dst[o++] = v;
        }
    }
    return o;
}

/* ---- the internal file system ---- */

static BOOL rd(HLP FAR *h, DWORD off, void FAR *buf, WORD n)
{
    return hio_read(h->fh, off, buf, n);
}

DWORD hlp_filesize(HLP FAR *h, DWORD f)
{
    BYTE b[9];
    if (!f || !rd(h, f, b, 9))
        return 0;
    return hlp_rl(b + 4);
}

BOOL hlp_fread(HLP FAR *h, DWORD f, DWORD off, void FAR *buf, WORD n)
{
    return rd(h, f + 9 + off, buf, n);
}

static BOOL bt_open(HLP FAR *h, DWORD f, HLPBT FAR *bt)
{
    BYTE b[38];
    if (!hlp_fread(h, f, 0, b, 38) || hlp_rw(b) != 0x293B)
        return FALSE;
    bt->pagesize = hlp_rw(b + 4);
    hlp_memcpy(bt->structure, b + 6, 16);
    bt->structure[16] = 0;
    bt->root = (short)hlp_rw(b + 26);
    bt->nlevels = (short)hlp_rw(b + 32);
    bt->entries = (LONG)hlp_rl(b + 34);
    bt->base = f + 9 + 38;
    if (bt->pagesize > 4096 || bt->pagesize < 64)
        return FALSE;
    return TRUE;
}

static BOOL bt_page(HLP FAR *h, HLPBT FAR *bt, short page)
{
    if (page < 0)
        return FALSE;
    return rd(h, bt->base + (DWORD)page * bt->pagesize, h->page, bt->pagesize);
}

/* key kinds of the trees */
#define KT_STR  0       /* NUL-terminated, compared byte for byte */
#define KT_LONG 1       /* signed 32-bit */
#define KT_ULONG 2      /* unsigned 32-bit */

static int keycmp(int kt, LPCB e, const void FAR *key, WORD FAR *klen)
{
    if (kt == KT_STR) {
        *klen = hlp_strlen((const char FAR *)e) + 1;
        return strcmpb(e, (LPCB)key);
    }
    *klen = 4;
    if (kt == KT_LONG) {
        LONG a = (LONG)hlp_rl(e), b = *(const LONG FAR *)key;
        return a < b ? -1 : a > b;
    } else {
        DWORD a = hlp_rl(e), b = *(const DWORD FAR *)key;
        return a < b ? -1 : a > b;
    }
}

/* the first leaf page that may hold key (loaded into h->page); -1 none */
static short bt_leaf(HLP FAR *h, HLPBT FAR *bt, int kt, const void FAR *key)
{
    short page = bt->root;
    int lev;
    for (lev = 1; lev < bt->nlevels; lev++) {
        WORD n, i, kl;
        LPCB e;
        short next;
        if (!bt_page(h, bt, page))
            return -1;
        n = hlp_rw(h->page + 2);
        next = (short)hlp_rw(h->page + 4);
        e = h->page + 6;
        for (i = 0; i < n; i++) {
            if (key && keycmp(kt, e, key, &kl) > 0)
                break;
            if (!key)
                break;
            next = (short)hlp_rw(e + kl);
            e += kl + 2;
        }
        page = next;
    }
    if (!bt_page(h, bt, page))
        return -1;
    return page;
}

/* the size of a leaf entry's data after its key, by the tree's kind */
typedef WORD (*DATALEN)(LPCB d);
static WORD dl4(LPCB d) { (void)d; return 4; }

/* look a key up: on success copies the entry's data (up to n bytes) */
static BOOL bt_find(HLP FAR *h, DWORD f, int kt, const void FAR *key, DATALEN dl,
                    void FAR *data, WORD n)
{
    HLPBT bt;
    short page;
    if (!f || !bt_open(h, f, &bt))
        return FALSE;
    page = bt_leaf(h, &bt, kt, key);
    while (page >= 0) {
        WORD cnt = hlp_rw(h->page + 2), i, kl, m;
        LPCB e = h->page + 8;
        short next = (short)hlp_rw(h->page + 6);
        for (i = 0; i < cnt; i++) {
            int c = keycmp(kt, e, key, &kl);
            m = dl(e + kl);
            if (c == 0) {
                hlp_memcpy(data, e + kl, m < n ? m : n);
                return TRUE;
            }
            if (c > 0)
                return FALSE;
            e += kl + m;
        }
        if (next < 0 || !bt_page(h, &bt, next))
            return FALSE;
        page = next;
    }
    return FALSE;
}

DWORD hlp_file(HLP FAR *h, const char FAR *name)
{
    BYTE d[4];
    HLPBT bt;
    short page;
    /* the directory: a B+ tree of names and file offsets */
    if (!bt_open(h, h->dirstart, &bt))
        return 0;
    page = bt_leaf(h, &bt, KT_STR, name);
    while (page >= 0) {
        WORD cnt = hlp_rw(h->page + 2), i, kl;
        LPCB e = h->page + 8;
        short next = (short)hlp_rw(h->page + 6);
        for (i = 0; i < cnt; i++) {
            int c = keycmp(KT_STR, e, name, &kl);
            if (c == 0) {
                hlp_memcpy(d, e + kl, 4);
                return hlp_rl(d);
            }
            if (c > 0)
                return 0;
            e += kl + 4;
        }
        if (next < 0 || !bt_page(h, &bt, next))
            return 0;
        page = next;
    }
    return 0;
}

/* ---- context strings ----
 * The hash of a context string: h = h * 43 + v(c) over its characters
 * (32-bit, wrapping), 1 for the empty string; v: '1'-'9' 1-9, '0' 10,
 * '.' 12, '_' 13, letters (either case) 17-42, others by the general rule
 * below. Checked against every |CONTEXT of the reference install's help
 * files (tests/winhelp/hlpdump.c -c), whose JumpId macros name contexts. */
static LONG ctxval(BYTE c)
{
    if (c >= 'a' && c <= 'z')
        return c - 0x50;
    if (c >= 'A' && c <= 'Z')
        return c - 0x30;
    if (c >= '1' && c <= '9')
        return c - 0x30;
    switch (c) {
    case '0': return 10;
    case '!': return 11;
    case '.': return 12;
    case '_': return 13;
    }
    if (c < 0x30)
        return (LONG)(signed char)(BYTE)(c + 0xD0);
    if (c < 0x80)
        return c - 0x30;
    return (LONG)(signed char)(BYTE)(c - 0x30);
}

LONG hlp_hash(const char FAR *s)
{
    DWORD v = 0;
    if (!*s)
        return 1;
    for (; *s; s++)
        v = v * 43 + (DWORD)ctxval((BYTE)*s);
    return (LONG)v;
}

BOOL hlp_context(HLP FAR *h, const char FAR *ctx, DWORD FAR *off)
{
    return hlp_context_hash(h, hlp_hash(ctx), off);
}

BOOL hlp_context_hash(HLP FAR *h, LONG k, DWORD FAR *off)
{
    BYTE d[4];
    if (!bt_find(h, h->f_context, KT_LONG, &k, dl4, d, 4))
        return FALSE;
    *off = hlp_rl(d);
    return TRUE;
}

/* |CTXOMAP: the [MAP] section's numbers */
BOOL hlp_mapid(HLP FAR *h, DWORD id, DWORD FAR *off)
{
    BYTE b[8];
    WORD n, i;
    if (!h->f_ctxomap || !hlp_fread(h, h->f_ctxomap, 0, b, 2))
        return FALSE;
    n = hlp_rw(b);
    for (i = 0; i < n; i++) {
        if (!hlp_fread(h, h->f_ctxomap, 2 + (DWORD)i * 8, b, 8))
            return FALSE;
        if (hlp_rl(b) == id) {
            *off = hlp_rl(b + 4);
            return TRUE;
        }
    }
    return FALSE;
}

/* ---- keywords ---- */

BOOL hlp_keywords(HLP FAR *h, HLPKWFN fn, void FAR *ctx)
{
    HLPBT bt;
    short page;
    if (!h->f_kwbtree || !bt_open(h, h->f_kwbtree, &bt))
        return FALSE;
    page = bt_leaf(h, &bt, KT_STR, NULL);
    while (page >= 0) {
        WORD cnt = hlp_rw(h->page + 2), i, kl;
        LPCB e = h->page + 8;
        short next = (short)hlp_rw(h->page + 6);
        for (i = 0; i < cnt; i++) {
            kl = hlp_strlen((const char FAR *)e) + 1;
            if (!fn(ctx, (const char FAR *)e, hlp_rw(e + kl), hlp_rl(e + kl + 2)))
                return TRUE;
            e += kl + 6;
        }
        if (next < 0 || !bt_page(h, &bt, next))
            break;
        page = next;
    }
    return TRUE;
}

BOOL hlp_kwtopic(HLP FAR *h, DWORD dataoff, WORD i, DWORD FAR *off)
{
    BYTE b[4];
    if (!h->f_kwdata || !hlp_fread(h, h->f_kwdata, dataoff + (DWORD)i * 4, b, 4))
        return FALSE;
    *off = hlp_rl(b);
    return TRUE;
}

typedef struct {
    const char FAR *kw;
    BOOL partial, found;
    WORD count;
    DWORD dataoff;
} KWFIND;

static BOOL kwfind_fn(void FAR *c, const char FAR *kw, WORD count, DWORD dataoff)
{
    KWFIND FAR *f = (KWFIND FAR *)c;
    BOOL hit;
    if (f->partial) {
        const char FAR *a = f->kw, FAR *b = kw;
        while (*a && lower((BYTE)*a) == lower((BYTE)*b))
            a++, b++;
        hit = *a == 0;
    } else {
        hit = hlp_stricmp(f->kw, kw) == 0;
    }
    if (hit) {
        f->found = TRUE;
        f->count = count;
        f->dataoff = dataoff;
        return FALSE;
    }
    return TRUE;
}

BOOL hlp_kwfind(HLP FAR *h, const char FAR *kw, BOOL partial, WORD FAR *count, DWORD FAR *dataoff)
{
    KWFIND f;
    f.kw = kw;
    f.partial = partial;
    f.found = FALSE;
    hlp_keywords(h, kwfind_fn, &f);
    if (f.found) {
        *count = f.count;
        *dataoff = f.dataoff;
    }
    return f.found;
}

/* ---- |TOPIC ----
 * Blocks of blksize bytes, each a 12-byte header (last link of the
 * previous block, first link in this one, last topic header) and data,
 * LZ77-compressed in 3.1 files (to at most 16K). A TOPICPOS is
 * 12 + block * dsize + offset in the block's decompressed data.
 * Microsoft's compilers stop a block's data at 16K and carry a link that
 * does not fit on into the next block; OpenWatcom's whc instead lets the
 * block decompress past 16K with the whole link, the next block starting
 * with the next link (its cmix.hlp and fmix.hlp: blocks of up to 19703
 * bytes). Reading on past a block's end into the next serves both. */

/* the most a 4K block can decompress to: every flag bit a 3-byte copy
   of 18 bytes */
#define MAXBLOCK(raw) ((DWORD)(raw) / 17 * 144 + 144)

static LPB topic_block(HLP FAR *h, LONG block, WORD FAR *len)
{
    int i;
    DWORD at, n;
    HLPBLK FAR *c;
    for (i = 0; i < HLP_CACHE; i++)
        if (h->cache[i].buf && h->cache[i].block == block) {
            *len = h->cache[i].len;
            return h->cache[i].buf;
        }
    at = (DWORD)block * h->blksize;
    if (at + 12 >= h->topicsize)
        return NULL;
    n = h->topicsize - at - 12;
    if (n > (DWORD)h->blksize - 12)
        n = h->blksize - 12;
    c = &h->cache[h->cachenext];
    h->cachenext = (h->cachenext + 1) % HLP_CACHE;
    if (!c->buf) {
        c->buf = (LPB)hio_alloc(h->lz ? MAXBLOCK(h->blksize) : h->dsize);
        if (!c->buf)
            return NULL;
    }
    c->block = -1;
    if (h->lz) {
        if (!hlp_fread(h, h->f_topic, at + 12, h->raw, (WORD)n))
            return NULL;
        c->len = (WORD)hlp_lz77(h->raw, n, c->buf, MAXBLOCK(h->blksize));
    } else {
        if (!hlp_fread(h, h->f_topic, at + 12, c->buf, (WORD)n))
            return NULL;
        c->len = (WORD)n;
    }
    c->block = block;
    *len = c->len;
    return c->buf;
}

/* a position's block and offset; HLP_OVERFLOW marks one past 16K in the
   block before (whc's), as hlp_nextlink finds them */
LONG hlp_posblock(HLP FAR *h, DWORD pos, WORD FAR *off)
{
    DWORD p = pos & ~HLP_OVERFLOW;
    LONG b = (LONG)((p - 12) / h->dsize);
    WORD o = (WORD)((p - 12) % h->dsize);
    if (pos & HLP_OVERFLOW) {
        b--;
        o += (WORD)h->dsize;
    }
    if (off)
        *off = o;
    return b;
}

BOOL hlp_tread(HLP FAR *h, DWORD pos, void FAR *buf, WORD n)
{
    LONG block;
    WORD o, len;
    LPB d = (LPB)buf;
    if ((pos & ~HLP_OVERFLOW) < 12)
        return FALSE;
    block = hlp_posblock(h, pos, &o);
    while (n) {
        LPB b = topic_block(h, block, &len);
        WORD k;
        if (!b)
            return FALSE;
        if (o >= len) {         /* the rest is in the next block */
            block++;
            o = 0;
            continue;
        }
        k = len - o;
        if (k > n)
            k = n;
        hlp_memcpy(d, b + o, k);
        d += k;
        n -= k;
        o += k;
    }
    return TRUE;
}

BOOL hlp_link(HLP FAR *h, DWORD pos, HLPLINK FAR *l)
{
    BYTE b[HLPLINK_SIZE];
    if (!hlp_tread(h, pos, b, HLPLINK_SIZE))
        return FALSE;
    l->blocksize = (LONG)hlp_rl(b);
    l->datalen2 = (LONG)hlp_rl(b + 4);
    l->prev = hlp_rl(b + 8);
    l->next = hlp_rl(b + 12);
    l->datalen1 = (LONG)hlp_rl(b + 16);
    l->type = b[20];
    if (l->datalen1 < HLPLINK_SIZE || l->blocksize < l->datalen1)
        return FALSE;
    return TRUE;
}

/* the next link's position; 0 at the end */
/* The next link. whc writes the position of a link that starts past
   16K in an overflowing block as if it were in the next block (block * 16K
   + offset): when the next link continues this one's bytes there and
   this block's data reaches that far, it is the overflow. */
DWORD hlp_nextlink(HLP FAR *h, DWORD pos, HLPLINK FAR *l)
{
    DWORD n;
    WORD off, noff, len;
    LONG b, nb;
    if (h->minor <= 16)
        n = (pos & ~HLP_OVERFLOW) + l->next;    /* 3.0: a distance */
    else
        n = l->next;
    if (n == 0 || n == 0xFFFFFFFFUL || n <= (pos & ~HLP_OVERFLOW) || n < 12)
        return 0;
    if (!h->lz)
        return n;
    b = hlp_posblock(h, pos, &off);
    nb = hlp_posblock(h, n, &noff);
    if (nb == b + 1 && (DWORD)off + (DWORD)l->blocksize == h->dsize + noff &&
        topic_block(h, b, &len) && (DWORD)len > h->dsize + noff)
        return n | HLP_OVERFLOW;
    return n;
}

/* expand LinkData2's phrases: a byte 1-15 starts a phrase number
   ((b - 1) * 256 + next) / 2, odd numbers followed by a space */
static DWORD phrases(HLP FAR *h, LPCB s, DWORD n, LPB d, DWORD max)
{
    DWORD o = 0;
    LPCB end = s + n;
    while (s < end && o < max) {
        BYTE c = *s++;
        if (c == 0 || c > 15 || !h->nphr) {
            d[o++] = c;
        } else {
            WORD num;
            WORD a, b;
            if (s >= end)
                break;
            num = (WORD)(((WORD)c - 1) * 256 + *s++);
            if (num / 2 >= h->nphr)
                continue;
            a = h->phroff[num / 2];
            b = h->phroff[num / 2 + 1];
            while (a < b && o < max)
                d[o++] = h->phrtext[a++];
            if ((num & 1) && o < max)
                d[o++] = ' ';
        }
    }
    return o;
}

BOOL hlp_linkdata(HLP FAR *h, DWORD pos, HLPLINK FAR *l, HLPDATA FAR *d)
{
    DWORD n1 = (DWORD)(l->datalen1 - HLPLINK_SIZE);
    DWORD raw2 = (DWORD)(l->blocksize - l->datalen1);
    DWORD n2 = (DWORD)l->datalen2;
    BOOL expand = n2 > raw2;
    LPB m;
    d->mem = NULL;
    if (n1 > 0xFF00UL || raw2 > 0xFF00UL || n2 > 0xFF00UL)
        return FALSE;
    if (!expand)
        n2 = raw2;
    m = (LPB)hio_alloc(n1 + n2 + raw2 + 4);
    if (!m)
        return FALSE;
    d->mem = m;
    d->d1 = m;
    d->n1 = n1;
    d->d2 = m + n1 + 2;
    if (!hlp_tread(h, pos + HLPLINK_SIZE, m, (WORD)(n1 + raw2))) {
        hio_free(m);
        d->mem = NULL;
        return FALSE;
    }
    if (expand) {
        /* the raw bytes after both buffers, expanded into d2 */
        LPB r = m + n1 + n2 + 4;
        hlp_memcpy(r, m + n1, raw2);
        d->n2 = phrases(h, r, raw2, d->d2, n2);
    } else {
        LPB r = m + n1;
        DWORD i;
        /* move up by two to leave room as above (a copy from the top) */
        for (i = raw2; i > 0; i--)
            d->d2[i - 1] = r[i - 1];
        d->n2 = raw2;
    }
    return TRUE;
}

/* ---- topics ---- */

BOOL hlp_topichdr(HLP FAR *h, int topic, HLPTHDR FAR *t, char FAR *title, WORD ntitle,
                  char FAR *macros, WORD nmacros)
{
    HLPLINK l;
    HLPDATA d;
    DWORD pos;
    if (topic < 0 || topic >= (int)h->ntopics)
        return FALSE;
    pos = h->topics[topic].pos;
    if (!hlp_link(h, pos, &l) || !hlp_linkdata(h, pos, &l, &d))
        return FALSE;
    if (h->minor <= 16) {
        t->browseback = t->browsefor = 0xFFFFFFFFUL;
        t->num = 0;
        t->nonscroll = 0xFFFFFFFFUL;
        t->scroll = pos;
        t->nexttopic = 0;
        if (d.n1 >= 12) {
            t->browseback = hlp_rl(d.d1 + 4);
            t->browsefor = hlp_rl(d.d1 + 8);
        }
    } else if (d.n1 >= 28) {
        t->browseback = hlp_rl(d.d1 + 4);
        t->browsefor = hlp_rl(d.d1 + 8);
        t->num = (LONG)hlp_rl(d.d1 + 12);
        t->nonscroll = hlp_rl(d.d1 + 16);
        t->scroll = hlp_rl(d.d1 + 20);
        t->nexttopic = hlp_rl(d.d1 + 24);
    }
    {
        /* the header's positions as hlp_nextlink resolves them (whc) */
        DWORD rn = hlp_nextlink(h, pos, &l);
        if (rn && t->scroll == (rn & ~HLP_OVERFLOW))
            t->scroll = rn;
        if (rn && t->nonscroll == (rn & ~HLP_OVERFLOW))
            t->nonscroll = rn;
    }
    if (title) {
        DWORD i = 0;
        while (i < d.n2 && d.d2[i] && i + 1 < ntitle) {
            title[i] = d.d2[i];
            i++;
        }
        if (ntitle)
            title[i] = 0;
    }
    if (macros && nmacros) {
        /* the strings after the title, NUL-separated, two NULs at the end */
        DWORD i = 0, o = 0;
        while (i < d.n2 && d.d2[i])
            i++;
        i++;
        while (i < d.n2 && o + 2 < nmacros)
            macros[o++] = d.d2[i++];
        macros[o] = 0;
        macros[o + 1] = 0;
    }
    hio_free(d.mem);
    return TRUE;
}

BOOL hlp_title(HLP FAR *h, int topic, char FAR *buf, WORD n)
{
    HLPTHDR t;
    return hlp_topichdr(h, topic, &t, buf, n, NULL, 0);
}

int hlp_topic_at(HLP FAR *h, DWORD off)
{
    int i, r = -1;
    for (i = 0; i < (int)h->ntopics; i++) {
        if (h->topics[i].off <= off)
            r = i;
        else
            break;
    }
    return r;
}

/* walk every link once: where each topic is, and its TOPICOFFSET (block
   of the link * 32768 + the characters of the links before it that
   started in that block: TopicLength of each display link) */
static BOOL index_topics(HLP FAR *h)
{
    DWORD pos = 12, count = 0;
    LONG block = 0;
    WORD n = 0, room = 64;
    HLPLINK l;
    HLPTOPIC FAR *t = (HLPTOPIC FAR *)hio_alloc((DWORD)room * sizeof(HLPTOPIC));
    WORD guard = 0;
    if (!t)
        return FALSE;
    while (pos && hlp_link(h, pos, &l)) {
        LONG b = hlp_posblock(h, pos, NULL);
        if (b != block) {
            block = b;
            count = 0;
        }
        if (l.type == 2) {
            if (n == room) {
                HLPTOPIC FAR *t2;
                if (room >= 0x7F00 / sizeof(HLPTOPIC))
                    break;
                t2 = (HLPTOPIC FAR *)hio_alloc((DWORD)room * 2 * sizeof(HLPTOPIC));
                if (!t2)
                    break;
                hlp_memcpy(t2, t, (DWORD)room * sizeof(HLPTOPIC));
                hio_free(t);
                t = t2;
                room *= 2;
            }
            t[n].pos = pos;
            t[n].off = ((DWORD)block << 15) + count;
            n++;
        } else if (l.type == 0x20 || l.type == 0x23) {
            BYTE b1[8];
            LPCB p = b1;
            if (hlp_tread(h, pos + HLPLINK_SIZE, b1, 8)) {
                hlp_culong(&p);
                count += hlp_cuword(&p);
            }
        }
        pos = hlp_nextlink(h, pos, &l);
        if (++guard == 0)
            break;
    }
    h->topics = t;
    h->ntopics = n;
    return TRUE;
}

/* ---- opening ---- */

static void read_system(HLP FAR *h, DWORD f)
{
    DWORD size = hlp_filesize(h, f), p;
    BYTE b[12];
    if (!hlp_fread(h, f, 0, b, 12))
        return;
    h->minor = hlp_rw(b + 2);
    h->gendate = hlp_rl(b + 6);
    h->sysflags = hlp_rw(b + 10);
    if (h->minor <= 16) {
        hlp_fread(h, f, 12, h->title, sizeof h->title - 1);
        h->title[sizeof h->title - 1] = 0;
        return;
    }
    for (p = 12; p + 4 <= size;) {
        WORD type, n, k;
        BYTE r[4];
        if (!hlp_fread(h, f, p, r, 4))
            break;
        type = hlp_rw(r);
        n = hlp_rw(r + 2);
        p += 4;
        switch (type) {
        case 1:
        case 2: {
            char FAR *d = type == 1 ? h->title : h->copyright;
            k = n < 127 ? n : 127;
            hlp_fread(h, f, p, d, k);
            d[k] = 0;
            break;
        }
        case 3:
            if (n >= 4 && hlp_fread(h, f, p, r, 4))
                h->contents = hlp_rl(r);
            break;
        case 4: {
            /* append to the NUL-separated list (old: its length up to
               the final NUL) */
            WORD old = h->configlen;
            char FAR *c;
            c = (char FAR *)hio_alloc((DWORD)old + n + 2);
            if (!c)
                break;
            if (h->config) {
                hlp_memcpy(c, h->config, old);
                hio_free(h->config);
            }
            hlp_fread(h, f, p, c + old, n);
            c[old + n] = 0;
            c[old + n + 1] = 0;
            h->config = c;
            h->configlen = old + hlp_strlen(c + old) + 1;
            break;
        }
        case 6:
            if (n >= 90 && h->nwin < HLP_MAXWIN) {
                BYTE w[90];
                HLPWIN FAR *x = &h->win[h->nwin++];
                hlp_fread(h, f, p, w, 90);
                x->flags = hlp_rw(w);
                hlp_memcpy(x->type, w + 2, 10);
                x->type[9] = 0;
                hlp_memcpy(x->name, w + 12, 9);
                x->name[8] = 0;
                hlp_memcpy(x->caption, w + 21, 51);
                x->caption[50] = 0;
                x->x = (short)hlp_rw(w + 72);
                x->y = (short)hlp_rw(w + 74);
                x->cx = (short)hlp_rw(w + 76);
                x->cy = (short)hlp_rw(w + 78);
                x->max = (short)hlp_rw(w + 80);
                x->rgb = hlp_rl(w + 82);
                x->rgbnsr = hlp_rl(w + 86);
            }
            break;
        }
        p += n;
    }
}

static void read_phrases(HLP FAR *h, DWORD f)
{
    DWORD size = hlp_filesize(h, f);
    BYTE b[8];
    WORD n, i, base;
    DWORD dsz, hdr;
    LPB raw;
    if (!size || !hlp_fread(h, f, 0, b, 8))
        return;
    n = hlp_rw(b);
    if (hlp_rw(b + 2) != 0x0100 || !n)
        return;
    if (h->minor <= 16) {
        hdr = 4;
        dsz = size - 4 - 2 * ((DWORD)n + 1);
    } else {
        hdr = 8;
        dsz = hlp_rl(b + 4);
    }
    if (dsz > 0xFF00UL || size > 0xFF00UL)
        return;
    h->phroff = (WORD FAR *)hio_alloc(2 * ((DWORD)n + 1));
    h->phrtext = (LPB)hio_alloc(dsz + 1);
    raw = (LPB)hio_alloc(size);
    if (!h->phroff || !h->phrtext || !raw) {
        if (raw)
            hio_free(raw);
        return;
    }
    hlp_fread(h, f, 0, raw, (WORD)size);
    base = hlp_rw(raw + hdr);
    for (i = 0; i <= n; i++)
        h->phroff[i] = hlp_rw(raw + hdr + 2 * i) - base;
    {
        DWORD at = hdr + 2 * ((DWORD)n + 1);
        if (h->minor <= 16)
            hlp_memcpy(h->phrtext, raw + at, dsz);
        else
            hlp_lz77(raw + at, size - at, h->phrtext, dsz);
    }
    hio_free(raw);
    h->nphr = n;
}

static void read_fonts(HLP FAR *h, DWORD f)
{
    DWORD size = hlp_filesize(h, f);
    BYTE b[8];
    WORD nfaces, ndesc, foff, doff, i;
    LPB raw;
    if (!size || size > 0xFF00UL || !hlp_fread(h, f, 0, b, 8))
        return;
    nfaces = hlp_rw(b);
    ndesc = hlp_rw(b + 2);
    foff = hlp_rw(b + 4);
    doff = hlp_rw(b + 6);
    if (!nfaces || doff < foff || doff > size)
        return;
    raw = (LPB)hio_alloc(size);
    if (!raw)
        return;
    hlp_fread(h, f, 0, raw, (WORD)size);
    h->facelen = (doff - foff) / nfaces;
    h->faces = (char FAR *)hio_alloc((DWORD)nfaces * 32);
    if (h->faces) {
        for (i = 0; i < nfaces; i++) {
            WORD k = h->facelen < 31 ? h->facelen : 31;
            hlp_memcpy(h->faces + i * 32, raw + foff + i * h->facelen, k);
            h->faces[i * 32 + k] = 0;
        }
        h->nface = nfaces;
    }
    if (ndesc > HLP_MAXFONT)
        ndesc = HLP_MAXFONT;
    h->fonts = (HLPFONT FAR *)hio_alloc((DWORD)(ndesc ? ndesc : 1) * sizeof(HLPFONT));
    if (h->fonts && foff < 12) {
        /* 3.x descriptors: 11 bytes, sizes in half points */
        for (i = 0; i < ndesc && doff + (DWORD)i * 11 + 11 <= size; i++) {
            LPB d = raw + doff + i * 11;
            h->fonts[i].attr = d[0];
            h->fonts[i].halfpts = d[1];
            h->fonts[i].family = d[2];
            h->fonts[i].face = hlp_rw(d + 3);
            h->fonts[i].fg = RGB(d[5], d[6], d[7]);
        }
        h->nfont = i;
    }
    hio_free(raw);
}

HLP FAR *hlp_open(const char FAR *path, int FAR *err)
{
    HLP FAR *h;
    BYTE b[16];
    DWORD f;
    LONG fh = hio_open(path);
    *err = HLPE_NOFILE;
    if (fh == -1)
        return NULL;
    h = (HLP FAR *)hio_alloc(sizeof(HLP));
    if (!h) {
        hio_close(fh);
        *err = HLPE_NOMEM;
        return NULL;
    }
    h->fh = fh;
    *err = HLPE_NOTHELP;
    if (!hio_read(fh, 0, b, 16) || hlp_rl(b) != 0x00035F3FUL)
        goto bad;
    h->dirstart = hlp_rl(b + 4);
    *err = HLPE_NOMEM;
    h->page = (LPB)hio_alloc(4096);
    if (!h->page)
        goto bad;
    *err = HLPE_NOTHELP;
    f = hlp_file(h, "|SYSTEM");
    if (!f) {
        /* 3.0 files name it without the bar */
        f = hlp_file(h, "SYSTEM");
        if (!f)
            goto bad;
    }
    read_system(h, f);
    h->lz = h->minor > 16 && (h->sysflags == 4 || h->sysflags == 8);
    h->blksize = h->minor <= 16 ? 2048 : h->sysflags == 8 ? 2048 : 4096;
    h->dsize = h->lz ? 16384 : h->blksize - 12;
    h->f_topic = hlp_file(h, "|TOPIC");
    if (!h->f_topic)
        goto bad;
    h->topicsize = hlp_filesize(h, h->f_topic);
    h->f_context = hlp_file(h, "|CONTEXT");
    h->f_ctxomap = hlp_file(h, "|CTXOMAP");
    h->f_kwbtree = hlp_file(h, "|KWBTREE");
    h->f_kwdata = hlp_file(h, "|KWDATA");
    h->f_kwmap = hlp_file(h, "|KWMAP");
    h->f_ttl = hlp_file(h, "|TTLBTREE");
    h->f_tomap = hlp_file(h, "|TOMAP");
    f = hlp_file(h, "|Phrases");
    if (f)
        read_phrases(h, f);
    f = hlp_file(h, "|FONT");
    if (f)
        read_fonts(h, f);
    *err = HLPE_NOMEM;
    h->raw = (LPB)hio_alloc(h->blksize);
    if (!h->raw)
        goto bad;
    if (!index_topics(h))
        goto bad;
    *err = HLPE_NOTHELP;
    if (!h->ntopics)
        goto bad;
    *err = 0;
    return h;
bad:
    hlp_close(h);
    return NULL;
}

void hlp_close(HLP FAR *h)
{
    int i;
    if (!h)
        return;
    if (h->fh != -1)
        hio_close(h->fh);
    for (i = 0; i < HLP_CACHE; i++)
        if (h->cache[i].buf)
            hio_free(h->cache[i].buf);
    if (h->config) hio_free(h->config);
    if (h->phroff) hio_free(h->phroff);
    if (h->phrtext) hio_free(h->phrtext);
    if (h->faces) hio_free(h->faces);
    if (h->fonts) hio_free(h->fonts);
    if (h->topics) hio_free(h->topics);
    if (h->page) hio_free(h->page);
    if (h->raw) hio_free(h->raw);
    hio_free(h);
}
