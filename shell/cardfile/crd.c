/*
 * crd.c - w16 Cardfile: .CRD files.
 *
 * The format as observed on Windows 3.1 (docs/crd-format.md; probes CF2-
 * CF7) agrees with Microsoft's published description (KB Q99340, "Windows
 * 3.1 Card File Format", facts only) and the Cardwin project's notes:
 *   "MGC" WORD count                          (3.0, and 3.1 without objects)
 *   "RRG" DWORD next object number, WORD count (3.1 with objects)
 *   count index entries of 52 bytes: 6 zero bytes, DWORD offset of the
 *   card's body, a zero byte, the 40-byte index field, a zero byte;
 *   then the bodies in index order:
 *   MGC: WORD picture size; if not 0: WORD width, height, x, y, the bits;
 *        WORD text length, the text
 *   RRG: WORD 1 if there is an object; then DWORD its number, the object
 *        as OleSaveToStream writes it, WORD character width and height,
 *        RECT (left, top, right, bottom), WORD 0 embedded/1 linked/2
 *        static; WORD text length, the text.
 * Cardfile 3.1 writes MGC unless a card has an object (CF2, CF6).
 */
#include "cardfile.h"

char g_srcpath[144];                    /* the file bodies are read from */
static HFILE src = HFILE_ERROR;

#define ENTRY_SIZE  52

HFILE g_srcfile_handle(void)
{
    if (src == HFILE_ERROR && g_srcpath[0]) {
        OFSTRUCT of;
        src = OpenFile(g_srcpath, &of, OF_READ | OF_SHARE_DENY_NONE);
        if (src == HFILE_ERROR)
            msg_box(IDS_REOPEN, g_srcpath, MB_OK | MB_ICONEXCLAMATION);
    }
    return src;
}

static void src_close(void)
{
    if (src != HFILE_ERROR)
        _lclose(src);
    src = HFILE_ERROR;
}

static BOOL rd_word(HFILE f, WORD *w)
{
    return f_read(f, w, 2);
}

static BOOL wr_word(HFILE f, WORD w)
{
    return f_write(f, &w, 2);
}

/* ---- bodies ---- */

/* MGC pictures: WORD size, width, height, x, y, then the bitmap's bits
   (monochrome, rows of whole words: what GetBitmapBits gives for a
   one-plane bitmap). UNVERIFIED beyond the 3.0 files we made (CF6). */
static BOOL mgc_pic_read(HFILE f, WORD size, PICTURE *p)
{
    WORD hdr[4];
    HGLOBAL h;
    LPSTR bits;
    if (!f_read(f, hdr, sizeof hdr))
        return FALSE;
    h = GlobalAlloc(GMEM_MOVEABLE, size ? size : 1);
    if (!h)
        return FALSE;
    bits = GlobalLock(h);
    if (!f_read(f, bits, size)) {
        GlobalUnlock(h);
        GlobalFree(h);
        return FALSE;
    }
    p->kind = PIC_BITMAP;
    p->cx = hdr[0];
    p->cy = hdr[1];
    p->x = hdr[2];
    p->y = hdr[3];
    p->bmp = CreateBitmap(p->cx, p->cy, 1, 1, bits);
    GlobalUnlock(h);
    GlobalFree(h);
    return p->bmp != NULL;
}

static BOOL text_read(HFILE f, HGLOBAL FAR *text, WORD FAR *cb)
{
    WORD n;
    LPSTR t;
    if (!rd_word(f, &n))
        return FALSE;
    *cb = n;
    *text = GlobalAlloc(GMEM_MOVEABLE, (DWORD)n + 1);
    if (!*text)
        return FALSE;
    t = GlobalLock(*text);
    if (!f_read(f, t, n)) {
        GlobalUnlock(*text);
        GlobalFree(*text);
        *text = NULL;
        return FALSE;
    }
    t[n] = 0;
    GlobalUnlock(*text);
    return TRUE;
}

/* Read a body at pos: its text (a global block with a NUL) and picture. */
BOOL body_read(HFILE f, int fmt, DWORD pos, HGLOBAL FAR *text, WORD FAR *cb, PICTURE *p)
{
    WORD w;
    *text = NULL;
    *cb = 0;
    if (_llseek(f, pos, 0) != (LONG)pos || !rd_word(f, &w))
        return FALSE;
    if (fmt == FMT_MGC) {
        if (w && !mgc_pic_read(f, w, p))
            return FALSE;
    } else if (w) {
#ifdef W16_WIN2
        return FALSE;                   /* no OLE on Windows 2.x */
#else
        WORD tail[8];
        DWORD at;
        if (!f_read(f, &p->id, 4))
            return FALSE;
        at = _llseek(f, 0, 1);
        if (!ole_load(f, p, &at))
            return FALSE;
        _llseek(f, at, 0);
        if (!f_read(f, tail, 14))
            return FALSE;
        p->ccx = tail[0];
        p->ccy = tail[1];
        /* the rectangle was made with characters of ccx by ccy pixels */
        p->x = MulDiv(tail[2], g_cx, p->ccx ? p->ccx : g_cx);
        p->y = MulDiv(tail[3], g_cy, p->ccy ? p->ccy : g_cy);
        p->cx = MulDiv(tail[4], g_cx, p->ccx ? p->ccx : g_cx) - p->x;
        p->cy = MulDiv(tail[5], g_cy, p->ccy ? p->ccy : g_cy) - p->y;
        p->otype = tail[6];
#endif
    }
    return text_read(f, text, cb);
}

/* The picture as a monochrome bitmap, the way a 3.0 file keeps it. An
   object becomes one a pixel wider and higher than its rectangle, drawn
   into the rectangle (CF6; the original leaves the extra row and column
   as they were in memory, here black). Returns the bitmap and its size;
   *own: the caller deletes it. */
static HBITMAP pic_mono(PICTURE *p, int *w, int *h, BOOL *own)
{
    *own = FALSE;
    if (p->kind == PIC_BITMAP) {
        *w = p->cx;
        *h = p->cy;
        return p->bmp;
    }
#ifndef W16_WIN2
    if (p->kind == PIC_OBJECT && p->obj) {
        HDC scr = GetDC(NULL), mem = CreateCompatibleDC(scr);
        HBITMAP bm = CreateBitmap(p->cx + 1, p->cy + 1, 1, 1, NULL), old;
        RECT r;
        ReleaseDC(NULL, scr);
        if (!bm || !mem) {
            if (bm)
                DeleteObject(bm);
            if (mem)
                DeleteDC(mem);
            return NULL;
        }
        old = SelectObject(mem, bm);
        PatBlt(mem, 0, 0, p->cx + 1, p->cy + 1, BLACKNESS);
        SetRect(&r, 0, 0, p->cx, p->cy);
        ole_draw(p, mem, &r);
        SelectObject(mem, old);
        DeleteDC(mem);
        *w = p->cx + 1;
        *h = p->cy + 1;
        *own = TRUE;
        return bm;
    }
#endif
    return NULL;
}

/* a bitmap's bits as GetBitmapBits gives them, in a global block */
static HGLOBAL bitmap_bits(HBITMAP bm, BITMAP *info, DWORD *size)
{
    HGLOBAL h;
    GetObject(bm, sizeof *info, (LPSTR)info);
    *size = (DWORD)info->bmWidthBytes * info->bmHeight * info->bmPlanes;
    h = GlobalAlloc(GMEM_MOVEABLE, *size ? *size : 1);
    if (h)
        GetBitmapBits(bm, *size, GlobalLock(h));
    return h;
}

static BOOL write_block(HFILE f, HGLOBAL h, DWORD size)
{
    BOOL ok;
    if (size > 0xFFFFu)
        return FALSE;                   /* larger than a WORD can say */
    ok = f_write(f, GlobalLock(h), (WORD)size);
    GlobalUnlock(h);
    return ok;
}

#ifndef W16_WIN2
/* A 3.0 picture in a 3.1 file: the static BITMAP object the original
   makes of it (CF8): HIMETRIC size 0 x 0, the monochrome bitmap as
   presentation (a 14-byte BITMAP, then the bits), the rectangle x, y,
   x + width - 1, y + height - 1, type 2. */
static BOOL write_static_bitmap(HFILE f, PICTURE *p)
{
    static const BYTE head[] = {
        0x01, 0x05, 0, 0,               /* OLE 1.0 stream */
        3, 0, 0, 0,                     /* static */
        7, 0, 0, 0, 'B', 'I', 'T', 'M', 'A', 'P', 0,
        0, 0, 0, 0, 0, 0, 0, 0          /* width, height (HIMETRIC) */
    };
    BITMAP bm;
    DWORD size, total;
    WORD info[7], tail[7];
    HGLOBAL h = bitmap_bits(p->bmp, &bm, &size);
    BOOL ok;
    if (!h)
        return FALSE;
    total = size + 14;
    info[0] = 0;
    info[1] = bm.bmWidth;
    info[2] = bm.bmHeight;
    info[3] = bm.bmWidthBytes;
    info[4] = bm.bmPlanes | (bm.bmBitsPixel << 8);
    info[5] = info[6] = 0;
    tail[0] = g_cx;
    tail[1] = g_cy;
    tail[2] = p->x;
    tail[3] = p->y;
    tail[4] = p->x + p->cx - 1;
    tail[5] = p->y + p->cy - 1;
    tail[6] = 2;
    ok = f_write(f, head, sizeof head) && f_write(f, &total, 4) && f_write(f, info, 14) &&
         write_block(f, h, size) && f_write(f, tail, sizeof tail);
    GlobalUnlock(h);
    GlobalFree(h);
    return ok;
}
#endif

/* Write a body at the file's position; returns its offset or -1. */
DWORD body_write(HFILE f, int fmt, LPCSTR text, WORD cb, PICTURE *p)
{
    DWORD pos = _llseek(f, 0, 1);
    if (fmt == FMT_MGC) {
        int w, h;
        BOOL own;
        HBITMAP bm = pic_mono(p, &w, &h, &own);
        if (bm) {
            BITMAP info;
            DWORD size;
            WORD hdr[5];
            HGLOBAL bits = bitmap_bits(bm, &info, &size);
            BOOL ok;
            if (own)
                DeleteObject(bm);
            if (!bits)
                return (DWORD)-1;
            hdr[0] = (WORD)size;
            hdr[1] = w;
            hdr[2] = h;
            hdr[3] = p->x;
            hdr[4] = p->y;
            ok = size <= 0xFFFFu && f_write(f, hdr, sizeof hdr) && write_block(f, bits, size);
            GlobalUnlock(bits);
            GlobalFree(bits);
            if (!ok)
                return (DWORD)-1;
        } else if (!wr_word(f, 0)) {
            return (DWORD)-1;
        }
    } else {
#ifndef W16_WIN2
        if (p->kind == PIC_OBJECT && p->obj) {
            WORD tail[7];
            if (!wr_word(f, 1) || !f_write(f, &p->id, 4) || !ole_save(f, p))
                return (DWORD)-1;
            tail[0] = g_cx;
            tail[1] = g_cy;
            tail[2] = p->x;
            tail[3] = p->y;
            tail[4] = p->x + p->cx;
            tail[5] = p->y + p->cy;
            tail[6] = p->otype;
            if (!f_write(f, tail, sizeof tail))
                return (DWORD)-1;
        } else if (p->kind == PIC_BITMAP && p->bmp) {
            /* a 3.0 file's picture saved as 3.1 got number 2 and left
               the counter at 3, opened afresh or not (CF8, CF12).
               UNVERIFIED: with more than one picture. */
            DWORD id = g_lastobj + 2;
            g_lastobj += 3;
            if (!wr_word(f, 1) || !f_write(f, &id, 4) || !write_static_bitmap(f, p))
                return (DWORD)-1;
        } else
#endif
        if (!wr_word(f, 0))
            return (DWORD)-1;
    }
    if (!wr_word(f, cb) || !f_write(f, text, cb))
        return (DWORD)-1;
    return pos;
}

/* The length of the body at pos, found by reading it (the format stores
   no lengths; an object's is found by parsing it, as Q99340 says). */
static DWORD body_len(HFILE f, int fmt, DWORD pos)
{
    WORD w, n;
    DWORD at = pos + 2;
    if (_llseek(f, pos, 0) != (LONG)pos || !rd_word(f, &w))
        return 0;
    if (fmt == FMT_MGC) {
        if (w)
            at += 8 + (DWORD)w;
    } else if (w) {
#ifdef W16_WIN2
        return 0;
#else
        at = ole_skip(f, at + 4);
        if (!at)
            return 0;
        at += 14;
#endif
    }
    if (_llseek(f, at, 0) != (LONG)at || !rd_word(f, &n))
        return 0;
    return at + 2 + n - pos;
}

/* Copy a body from one file to another (the same format), or convert it
   when the formats differ. */
BOOL body_copy(HFILE from, int ffmt, DWORD pos, HFILE to, int tfmt, DWORD FAR *newpos)
{
    *newpos = _llseek(to, 0, 1);
    if (ffmt == tfmt) {
        DWORD n = body_len(from, ffmt, pos);
        return n && _llseek(from, pos, 0) == (LONG)pos && f_copy(from, to, n);
    } else {
        HGLOBAL text;
        WORD cb;
        PICTURE pic;
        BOOL ok;
        LPSTR t;
        mem_set(&pic, 0, sizeof pic);
        if (!body_read(from, ffmt, pos, &text, &cb, &pic))
            return FALSE;
        t = GlobalLock(text);
        ok = body_write(to, tfmt, t, cb, &pic) != (DWORD)-1;
        GlobalUnlock(text);
        GlobalFree(text);
        pic_free(&pic);
        return ok;
    }
}

/* ---- whole files ---- */

/* Read a card file: replace the cards (Open) or add its cards (Merge). */
BOOL crd_open(LPCSTR path, BOOL merge)
{
    OFSTRUCT of;
    HFILE f = OpenFile(path, &of, OF_READ | OF_SHARE_DENY_NONE);
    char sig[3];
    WORD n, i;
    DWORD next = 0;
    int fmt;
    static BYTE entry[ENTRY_SIZE];

    if (f == HFILE_ERROR) {
        msg_box(IDS_NOTFOUND, path, MB_OK | MB_ICONEXCLAMATION);
        return FALSE;
    }
    if (!f_read(f, sig, 3))
        goto bad;
    if (sig[0] == 'M' && sig[1] == 'G' && sig[2] == 'C')
        fmt = FMT_MGC;
    else if (sig[0] == 'R' && sig[1] == 'R' && sig[2] == 'G')
        fmt = FMT_RRG;
    else
        goto bad;
#ifdef W16_WIN2
    if (fmt == FMT_RRG)
        goto bad;                       /* objects need OLE: not on 2.x */
#endif
    if (fmt == FMT_RRG && !f_read(f, &next, 4))
        goto bad;
    if (!rd_word(f, &n))
        goto bad;
    if (!merge) {
        cards_reset();
        src_close();
        s_cpy(g_srcpath, of.szPathName, sizeof g_srcpath);
        g_fmt = fmt;
        g_lastobj = next;
    }
    for (i = 0; i < n; i++) {
        WORD at;
        LPCARD c;
        DWORD pos;
        if (!f_read(f, entry, ENTRY_SIZE))
            goto bad;
        at = card_insert((LPCSTR)entry + 11);
        if (at == 0xFFFF) {
            msg_box(IDS_NOMEM, NULL, MB_OK | MB_ICONEXCLAMATION);
            break;
        }
        c = card_hdr(at);
        pos = *(DWORD FAR *)(entry + 6);
        c->fmt = (BYTE)fmt;
        if (!merge) {
            c->where = WHERE_FILE;
            c->pos = pos;
        } else {
            /* merged bodies go to the scratch file: the merged file is
               not kept */
            c->where = WHERE_FILE;
            c->pos = pos;
            c->spare = 1;               /* marks: from the merged file */
        }
    }
    if (merge) {
        /* copy the new cards' bodies out of the merged file now */
        WORD k;
        for (k = 0; k < g_ncards; k++) {
            LPCARD c = card_hdr(k);
            if (c->spare) {
                c->spare = 0;
                if (!merge_body(f, c))
                    c->where = WHERE_BLANK;
            }
        }
        if (fmt == FMT_RRG && next > g_lastobj)
            g_lastobj = next;
        _lclose(f);
    } else {
        src = f;
    }
    return TRUE;
bad:
    _lclose(f);
    msg_box(IDS_BADFORMAT, path, MB_OK | MB_ICONEXCLAMATION);
    return FALSE;
}

/* Has any card a picture? (On 3.1 every picture is an object, also one
   from a 3.0 file: such a file is saved as RRG, CF8.) */
BOOL crd_has_objects(void)
{
    WORD i;
    for (i = 0; i < g_ncards; i++) {
        LPCARD c = card_hdr(i);
        if (c->where != WHERE_BLANK) {
            HFILE f = body_file(c);
            WORD w;
            if (f != HFILE_ERROR && _llseek(f, c->pos, 0) == (LONG)c->pos && rd_word(f, &w) && w)
                return TRUE;
        }
    }
    return FALSE;
}

/* Write all cards to the file f (positioned at 0) in format fmt; the
   bodies' new offsets go to each header's newpos. */
static BOOL write_cards(HFILE f, int fmt)
{
    static BYTE entry[ENTRY_SIZE];
    DWORD pos;
    WORD i;
    static const BYTE blank[4] = { 0, 0, 0, 0 };

    if (fmt == FMT_MGC) {
        if (!f_write(f, "MGC", 3))
            return FALSE;
    } else if (!f_write(f, "RRG", 3) || !f_write(f, &g_lastobj, 4)) {
        return FALSE;
    }
    if (!wr_word(f, g_ncards))
        return FALSE;
    /* the index comes first; its offsets are filled in afterwards */
    pos = (fmt == FMT_MGC ? 5 : 9) + (DWORD)g_ncards * ENTRY_SIZE;
    if (_llseek(f, pos, 0) != (LONG)pos)
        return FALSE;
    for (i = 0; i < g_ncards; i++) {
        LPCARD c = card_hdr(i);
        if (c->where == WHERE_BLANK) {
            c->newpos = _llseek(f, 0, 1);
            if (!f_write(f, blank, 4))
                return FALSE;
        } else {
            HFILE from = body_file(c);
            if (from == HFILE_ERROR || !body_copy(from, c->fmt, c->pos, f, fmt, &c->newpos))
                return FALSE;
        }
    }
    _llseek(f, fmt == FMT_MGC ? 5 : 9, 0);
    for (i = 0; i < g_ncards; i++) {
        LPCARD c = card_hdr(i);
        mem_set(entry, 0, ENTRY_SIZE);
        *(DWORD FAR *)(entry + 6) = c->newpos;
        mem_cpy(entry + 11, c->index, INDEX_BYTES);
        if (!f_write(f, entry, ENTRY_SIZE))
            return FALSE;
    }
    return TRUE;
}

/* Save the cards to path. fmt -1: RRG if a card has an object, else MGC
   (as the original chooses, CF2/CF6); FMT_MGC: the 3.0 format. */
BOOL crd_save(LPCSTR path, int fmt)
{
    OFSTRUCT of;
    char full[144], tmp[144];
    HFILE f;
    BOOL same, ok;
    WORD i;

    if (fmt < 0)
        fmt = crd_has_objects() ? FMT_RRG : FMT_MGC;
    /* is the target the file the bodies are read from? */
    if (OpenFile(path, &of, OF_PARSE) == HFILE_ERROR) {
        msg_box(IDS_BADNAME, path, MB_OK | MB_ICONEXCLAMATION);
        return FALSE;
    }
    s_cpy(full, of.szPathName, sizeof full);
    same = g_srcpath[0] && !lstrcmpi(full, g_srcpath);
    if (same) {
        GetTempFileName(0, "CRD", 0, tmp);
        f = OpenFile(tmp, &of, OF_CREATE | OF_READWRITE);
    } else {
        f = OpenFile(full, &of, OF_CREATE | OF_READWRITE);
    }
    if (f == HFILE_ERROR) {
        msg_box(IDS_CANTSAVE2, base_name(full), MB_OK | MB_ICONEXCLAMATION);
        return FALSE;
    }
    ok = write_cards(f, fmt);
    if (ok && same) {
        /* copy the new file over the old one */
        HFILE t;
        DWORD n = _llseek(f, 0, 2);
        src_close();
        t = OpenFile(full, &of, OF_CREATE | OF_WRITE);
        ok = t != HFILE_ERROR && _llseek(f, 0, 0) == 0 && f_copy(f, t, n);
        if (t != HFILE_ERROR)
            _lclose(t);
        _lclose(f);
        OpenFile(tmp, &of, OF_DELETE);
    } else {
        _lclose(f);
        if (!ok)
            OpenFile(full, &of, OF_DELETE);
    }
    if (!ok) {
        msg_box(IDS_DISKFULL, NULL, MB_OK | MB_ICONEXCLAMATION);
        return FALSE;
    }
    /* the saved file is now where the bodies are */
    src_close();
    s_cpy(g_srcpath, full, sizeof g_srcpath);
    for (i = 0; i < g_ncards; i++) {
        LPCARD c = card_hdr(i);
        if (c->where != WHERE_BLANK) {
            c->where = WHERE_FILE;
            c->pos = c->newpos;
            c->fmt = (BYTE)fmt;
        }
    }
    temp_reset();
    g_fmt = fmt;
    return TRUE;
}

/* a merged card's body: from the merged file into the scratch file */
BOOL merge_body(HFILE f, LPCARD c)
{
    HFILE t = temp_file();
    DWORD at;
    if (t == HFILE_ERROR)
        return FALSE;
    _llseek(t, 0, 2);
    if (!body_copy(f, c->fmt, c->pos, t, c->fmt, &at))
        return FALSE;
    c->where = WHERE_TEMP;
    c->pos = at;
    temp_grew();
    return TRUE;
}
