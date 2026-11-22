/*
 * hfs.c - w16 Windows Help: writing the help file system, for the files
 * WINHELP keeps: WINHELP.BMK (bookmarks) and <name>.ANN (annotations).
 * Portable, as hlp.c (tests/winhelp/hfstest.c builds it on the host).
 *
 * The layout is the help file's (hlp.c; Winterhoff's helpfile.txt also
 * describes the annotation file): a 16-byte header (magic 00035F3Fh, the
 * directory's offset, the first free block or -1, the size), blocks each
 * behind a 9-byte FILEHEADER (reserved size including the header, used
 * size, flags: 4 for the directory), and the directory, a B+ tree of
 * names and offsets ("z4", flags 0402h). How 3.1's WINHELP allocates was
 * observed in the files it left (WH3, WH4):
 *  - a file is written when it is closed: at the end of the file system,
 *    its reserved size exactly its size; the directory is written after
 *    each file, in place when it still fits (the annotation file:
 *    @LINK at 10h, the directory at 1Bh, @VERSION at CAh; the bookmark
 *    file: the one bookmark file at 10h, the directory at 34h);
 *  - a file that grows past its block moves to the end; its old block
 *    goes on the free list: its first 8 bytes become the block's size and
 *    the next free block's offset (-1), the rest is left as it was;
 *  - directory pages: 128 bytes in .ANN files, 1024 in WINHELP.BMK; the
 *    directory's reserved size is 9 + 38 + its pages.
 * UNVERIFIED (not observed): which free block a new file takes (w16: the
 * first big enough, whole), and directories of more than one page (w16:
 * leaves in order under one index page).
 */
#include "hfs.h"

static void w16le(LPB p, WORD v) { p[0] = (BYTE)v; p[1] = (BYTE)(v >> 8); }
static void w32le(LPB p, DWORD v) { w16le(p, (WORD)v); w16le(p + 2, (WORD)(v >> 16)); }

static BOOL grow(HFS FAR *f, DWORD need)
{
    LPB n;
    DWORD cap = f->cap;
    if (need <= f->cap)
        return TRUE;
    while (cap < need)
        cap = cap ? cap * 2 : 1024;
    if (cap > 0xFF00UL) {
        if (need > 0xFF00UL)
            return FALSE;
        cap = 0xFF00UL;
    }
    n = (LPB)hio_alloc(cap);
    if (!n)
        return FALSE;
    if (f->buf) {
        hlp_memcpy(n, f->buf, f->size);
        hio_free(f->buf);
    }
    f->buf = n;
    f->cap = cap;
    return TRUE;
}

static void header(HFS FAR *f)
{
    w32le(f->buf, 0x00035F3FUL);
    w32le(f->buf + 4, f->dirstart);
    w32le(f->buf + 8, f->freelist);
    w32le(f->buf + 12, f->size);
}

BOOL hfs_new(HFS FAR *f, WORD pagesize)
{
    f->buf = NULL;
    f->cap = f->size = 0;
    f->pagesize = pagesize;
    f->n = 0;
    f->dirstart = 0;
    f->freelist = 0xFFFFFFFFUL;
    if (!grow(f, 16))
        return FALSE;
    f->size = 16;
    header(f);
    return TRUE;
}

void hfs_free(HFS FAR *f)
{
    if (f->buf)
        hio_free(f->buf);
    f->buf = NULL;
}

static void add_entry(HFS FAR *f, LPCB name, DWORD off)
{
    if (f->n < HFS_MAXFILES) {
        hlp_strcpyn(f->name[f->n], (const char FAR *)name, sizeof f->name[0]);
        f->off[f->n] = off;
        f->n++;
    }
}

BOOL hfs_load(HFS FAR *f, LPCB data, DWORD n)
{
    DWORD d;
    short page;
    WORD ps, lev, nlev;
    if (n < 16 || hlp_rl(data) != 0x00035F3FUL || n > 0xFF00UL)
        return FALSE;
    f->buf = NULL;
    f->cap = f->size = 0;
    f->n = 0;
    if (!grow(f, n))
        return FALSE;
    hlp_memcpy(f->buf, data, n);
    f->size = n;
    f->dirstart = hlp_rl(data + 4);
    f->freelist = hlp_rl(data + 8);
    d = f->dirstart + 9;
    if (d + 38 > n || hlp_rw(data + d) != 0x293B)
        return FALSE;
    ps = hlp_rw(data + d + 4);
    f->pagesize = ps;
    page = (short)hlp_rw(data + d + 26);
    nlev = hlp_rw(data + d + 32);
    for (lev = 1; lev < nlev; lev++)
        page = (short)hlp_rw(data + d + 38 + (DWORD)page * ps + 4);
    while (page >= 0) {
        LPCB p = data + d + 38 + (DWORD)page * ps;
        WORD cnt = hlp_rw(p + 2), i;
        LPCB e = p + 8;
        if (d + 38 + (DWORD)(page + 1) * ps > n)
            return FALSE;
        for (i = 0; i < cnt; i++) {
            WORD k = hlp_strlen((const char FAR *)e) + 1;
            add_entry(f, e, hlp_rl(e + k));
            e += k + 4;
        }
        page = (short)hlp_rw(p + 6);
    }
    return TRUE;
}

static int find(HFS FAR *f, const char FAR *name)
{
    WORD i;
    for (i = 0; i < f->n; i++)
        if (!hlp_strcmp(f->name[i], name))
            return i;
    return -1;
}

LPB hfs_get(HFS FAR *f, const char FAR *name, DWORD FAR *len)
{
    int i = find(f, name);
    if (i < 0)
        return NULL;
    *len = hlp_rl(f->buf + f->off[i] + 4);
    return f->buf + f->off[i] + 9;
}

/* a block on the free list: its size and the old list head */
static void release(HFS FAR *f, DWORD off)
{
    DWORD size = hlp_rl(f->buf + off);
    w32le(f->buf + off, size);
    w32le(f->buf + off + 4, f->freelist);
    f->freelist = off;
}

/* a block of exactly n bytes, or the first free one that is big enough */
static DWORD alloc(HFS FAR *f, DWORD n, DWORD FAR *reserved)
{
    DWORD prev = 0, cur = f->freelist, at;
    while (cur != 0xFFFFFFFFUL && cur + 8 <= f->size) {
        DWORD size = hlp_rl(f->buf + cur), next = hlp_rl(f->buf + cur + 4);
        if (size >= n) {
            if (prev)
                w32le(f->buf + prev + 4, next);
            else
                f->freelist = next;
            *reserved = size;
            return cur;
        }
        prev = cur;
        cur = next;
    }
    if (!grow(f, f->size + n))
        return 0;
    at = f->size;
    f->size += n;
    hlp_memset(f->buf + at, 0, n);
    *reserved = n;
    return at;
}

/* write a block's contents: in place when they fit, else elsewhere */
static DWORD put_block(HFS FAR *f, DWORD old, LPCB data, DWORD n, BYTE flags)
{
    DWORD at, reserved;
    if (old && hlp_rl(f->buf + old) >= n + 9) {
        at = old;
        reserved = hlp_rl(f->buf + old);
    } else {
        if (old)
            release(f, old);
        at = alloc(f, n + 9, &reserved);
        if (!at)
            return 0;
    }
    w32le(f->buf + at, reserved);
    w32le(f->buf + at + 4, n);
    f->buf[at + 8] = flags;
    hlp_memcpy(f->buf + at + 9, data, n);
    return at;
}

/* the directory: leaf pages in name order (and an index page over them
   when there is more than one) */
static BOOL write_dir(HFS FAR *f)
{
    WORD ps = f->pagesize, i, j, npages = 0, nleaves;
    WORD first[HFS_MAXFILES];
    DWORD used;
    LPB d;
    BOOL ok;
    /* sort (insertion; few entries) */
    for (i = 1; i < f->n; i++)
        for (j = i; j > 0 && hlp_strcmp(f->name[j - 1], f->name[j]) > 0; j--) {
            char t[32];
            DWORD o = f->off[j];
            hlp_strcpyn(t, f->name[j], 32);
            hlp_strcpyn(f->name[j], f->name[j - 1], 32);
            hlp_strcpyn(f->name[j - 1], t, 32);
            f->off[j] = f->off[j - 1];
            f->off[j - 1] = o;
        }
    /* count the leaves */
    {
        WORD fill = 8;
        nleaves = 1;
        first[0] = 0;
        for (i = 0; i < f->n; i++) {
            WORD k = hlp_strlen(f->name[i]) + 1 + 4;
            if (fill + k > ps) {
                first[nleaves++] = i;
                fill = 8;
            }
            fill += k;
        }
    }
    npages = nleaves > 1 ? nleaves + 1 : 1;
    used = 38 + (DWORD)npages * ps;
    d = (LPB)hio_alloc(used);
    if (!d)
        return FALSE;
    w16le(d, 0x293B);
    w16le(d + 2, 0x0402);
    w16le(d + 4, ps);
    d[6] = 'z';
    d[7] = '4';
    w16le(d + 22, 0);
    w16le(d + 24, 0);
    w16le(d + 26, nleaves > 1 ? nleaves : 0);
    w16le(d + 28, 0xFFFF);
    w16le(d + 30, npages);
    w16le(d + 32, nleaves > 1 ? 2 : 1);
    w32le(d + 34, f->n);
    for (j = 0; j < nleaves; j++) {
        LPB p = d + 38 + (DWORD)j * ps, e = p + 8;
        WORD end = j + 1 < nleaves ? first[j + 1] : f->n, cnt = 0;
        for (i = first[j]; i < end; i++) {
            WORD k = hlp_strlen(f->name[i]) + 1;
            hlp_memcpy(e, f->name[i], k);
            w32le(e + k, f->off[i]);
            e += k + 4;
            cnt++;
        }
        w16le(p, (WORD)(ps - (e - p)));
        w16le(p + 2, cnt);
        w16le(p + 4, j ? j - 1 : 0xFFFF);
        w16le(p + 6, j + 1 < nleaves ? j + 1 : 0xFFFF);
    }
    if (nleaves > 1) {
        LPB p = d + 38 + (DWORD)nleaves * ps, e = p + 6;
        for (j = 1; j < nleaves; j++) {
            WORD k = hlp_strlen(f->name[first[j]]) + 1;
            hlp_memcpy(e, f->name[first[j]], k);
            w16le(e + k, j);
            e += k + 2;
        }
        w16le(p, (WORD)(ps - (e - p)));
        w16le(p + 2, nleaves - 1);
        w16le(p + 4, 0);
    }
    f->dirstart = put_block(f, f->dirstart, d, used, 4);
    ok = f->dirstart != 0;
    hio_free(d);
    header(f);
    return ok;
}

BOOL hfs_put(HFS FAR *f, const char FAR *name, LPCB data, DWORD n)
{
    int i = find(f, name);
    DWORD at;
    LPB copy = (LPB)hio_alloc(n ? n : 1);   /* data may live in the buffer */
    if (!copy)
        return FALSE;
    hlp_memcpy(copy, data, n);
    at = put_block(f, i >= 0 ? f->off[i] : 0, copy, n, 0);
    hio_free(copy);
    if (!at)
        return FALSE;
    if (i >= 0)
        f->off[i] = at;
    else
        add_entry(f, (LPCB)name, at);
    return write_dir(f);
}

BOOL hfs_del(HFS FAR *f, const char FAR *name)
{
    int i = find(f, name);
    WORD k;
    if (i < 0)
        return FALSE;
    release(f, f->off[i]);
    for (k = (WORD)i + 1; k < f->n; k++) {
        hlp_strcpyn(f->name[k - 1], f->name[k], 32);
        f->off[k - 1] = f->off[k];
    }
    f->n--;
    return write_dir(f);
}
