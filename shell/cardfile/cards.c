/*
 * cards.c - w16 Cardfile: the cards in memory.
 *
 * Every card has a 64-byte header (CARDHDR): its index line and where its
 * body is. Headers live in 64K blocks of 1024, so the number of cards is
 * bounded only by the file format's card count (a WORD: 65535) and by
 * memory. The 40 bytes of an index field are kept whole, including what
 * follows the line's NUL: the original writes that back unchanged (CF4,
 * CF5), and so do we.
 *
 * Bodies stay in files: the open .CRD file, or the scratch file (made by
 * GetTempFileName, as the original's messages about a "temporary file"
 * suggest) where a card's body goes when it was changed. Only the front
 * card's body is in memory: its text in the edit control, its picture in
 * g_pic.
 */
#include "cardfile.h"

#define PER_SEG     1024                /* 64K / sizeof(CARDHDR) */

WORD g_ncards;
WORD g_cur;

static LPCARD *segs;                    /* locked blocks of PER_SEG headers */
static HLOCAL hsegs;
static WORD nsegs;

static char temp_path[144];             /* the scratch file */
static HFILE temp = HFILE_ERROR;
static DWORD temp_end;

LPCARD card_hdr(WORD i)
{
    return segs[i / PER_SEG] + i % PER_SEG;
}

/* room for n cards */
static BOOL reserve(DWORD n)
{
    while ((DWORD)nsegs * PER_SEG < n) {
        HGLOBAL h;
        HLOCAL nh = hsegs ? LocalReAlloc(hsegs, (nsegs + 1) * sizeof(LPCARD), LMEM_MOVEABLE)
                          : LocalAlloc(LMEM_MOVEABLE, sizeof(LPCARD));
        if (!nh)
            return FALSE;
        hsegs = nh;
        h = GlobalAlloc(GMEM_MOVEABLE | GMEM_ZEROINIT, (DWORD)PER_SEG * sizeof(CARDHDR));
        if (!h)
            return FALSE;
        segs = (LPCARD *)LocalLock(hsegs);
        segs[nsegs++] = (LPCARD)GlobalLock(h);
        LocalUnlock(hsegs);
    }
    segs = (LPCARD *)LocalLock(hsegs);  /* stays locked: a fixed local view */
    return TRUE;
}

BOOL cards_init(void)
{
    return reserve(1);
}

/* no cards, no files (New starts from here) */
void cards_reset(void)
{
    g_ncards = 0;
    g_cur = 0;
    temp_close();
}

/* The order of the index: Windows' language order, ignoring case (CF2:
   apple, Mango, Zebra); lstrcmpi gives that. */
int index_cmp(LPCSTR a, LPCSTR b)
{
    return lstrcmpi(a, b);
}

static void move_hdr(WORD to, WORD from)
{
    mem_cpy(card_hdr(to), card_hdr(from), sizeof(CARDHDR));
}

/* Where a card with this index goes: after the cards that sort before
   or equal to it. UNVERIFIED: the place among equal index lines. */
static WORD sorted_pos(LPCSTR index, WORD skip)
{
    WORD lo = 0, hi = g_ncards;
    while (lo < hi) {
        WORD mid = lo + (hi - lo) / 2;
        if (mid == skip || index_cmp(card_hdr(mid)->index, index) <= 0)
            lo = mid + 1;
        else
            hi = mid;
    }
    return lo;
}

/* A new card with this 40-byte index field (copied whole); its body is
   blank. Returns its position, or 0xFFFF when there is no room. */
WORD card_insert(LPCSTR index)
{
    WORD at, i;
    LPCARD c;
    if (g_ncards == 0xFFFF || !reserve((DWORD)g_ncards + 1))
        return 0xFFFF;
    at = sorted_pos(index, 0xFFFF);
    for (i = g_ncards; i > at; i--)
        move_hdr(i, i - 1);
    g_ncards++;
    c = card_hdr(at);
    mem_set(c, 0, sizeof(CARDHDR));
    mem_cpy(c->index, index, INDEX_BYTES);
    c->index[INDEX_BYTES] = 0;
    c->where = WHERE_BLANK;
    return at;
}

void card_remove(WORD i)
{
    for (; i + 1 < g_ncards; i++)
        move_hdr(i, i + 1);
    g_ncards--;
}

/* Card i's index changed: move it to its place; returns the new one. */
WORD card_resort(WORD i)
{
    CARDHDR save;
    WORD to, k;
    mem_cpy(&save, card_hdr(i), sizeof save);
    to = sorted_pos(save.index, i);
    if (to > i)
        to--;                           /* counted with i still in place */
    if (to > i)
        for (k = i; k < to; k++)
            move_hdr(k, k + 1);
    else
        for (k = i; k > to; k--)
            move_hdr(k, k - 1);
    mem_cpy(card_hdr(to), &save, sizeof save);
    return to;
}

/* ---- the scratch file ---- */

static BOOL temp_open(void)
{
    if (temp != HFILE_ERROR)
        return TRUE;
    {
        OFSTRUCT of;
        GetTempFileName(0, "CRD", 0, temp_path);
        temp = OpenFile(temp_path, &of, OF_CREATE | OF_READWRITE);
    }
    temp_end = 0;
    return temp != HFILE_ERROR;
}

HFILE temp_file(void)
{
    return temp_open() ? temp : HFILE_ERROR;
}

void temp_grew(void)
{
    temp_end = _llseek(temp, 0, 2);
}

void temp_close(void)
{
    if (temp != HFILE_ERROR) {
        OFSTRUCT of;
        _lclose(temp);
        OpenFile(temp_path, &of, OF_DELETE);
        temp = HFILE_ERROR;
    }
}

BOOL temp_reset(void)
{
    temp_close();
    return TRUE;
}

/* the file a card's body is in (positioned nowhere in particular) */
HFILE body_file(LPCARD c)
{
    if (c->where == WHERE_TEMP)
        return temp;
    if (c->where == WHERE_FILE)
        return g_srcfile_handle();
    return HFILE_ERROR;
}

/* ---- the front card's body ---- */

void pic_free(PICTURE *p)
{
    if (p->kind == PIC_BITMAP && p->bmp)
        DeleteObject(p->bmp);
#ifndef W16_WIN2
    if (p->kind == PIC_OBJECT)
        ole_free(p);
#endif
    mem_set(p, 0, sizeof *p);
}

/* Card i becomes the front card: its text into the edit control, its
   picture into g_pic. */
BOOL body_load(WORD i)
{
    LPCARD c = card_hdr(i);
    HGLOBAL text = NULL;
    WORD cb = 0;
    BOOL ok = TRUE;

    pic_free(&g_pic);
    if (c->where != WHERE_BLANK) {
        HFILE f = body_file(c);
        ok = f != HFILE_ERROR && body_read(f, c->fmt, c->pos, &text, &cb, &g_pic);
        if (!ok)
            msg_box(IDS_NOMEMREAD, NULL, MB_OK | MB_ICONEXCLAMATION);
    }
    if (text) {
        LPSTR p = GlobalLock(text);
        SetWindowText(g_edit, p);
        GlobalUnlock(text);
        GlobalFree(text);
    } else {
        SetWindowText(g_edit, "");
    }
    SendMessage(g_edit, EM_SETMODIFY, FALSE, 0);
    g_carddirty = FALSE;
    return ok;
}

/* The front card changed (its text or picture): write its body to the
   scratch file and point the header there. */
BOOL body_store(void)
{
    LPCARD c;
    HLOCAL h;
    WORD cb;
    LPSTR text;
    DWORD pos;
    int fmt;

    if (!g_ncards)
        return TRUE;
    if (SendMessage(g_edit, EM_GETMODIFY, 0, 0))
        g_carddirty = TRUE;
    if (!g_carddirty)
        return TRUE;
    c = card_hdr(g_cur);
    cb = GetWindowTextLength(g_edit);
    if (cb == 0 && g_pic.kind == PIC_NONE) {
        c->where = WHERE_BLANK;
        c->fmt = (BYTE)g_fmt;
        g_carddirty = FALSE;
        SendMessage(g_edit, EM_SETMODIFY, FALSE, 0);
        g_dirty = TRUE;
        return TRUE;
    }
    h = LocalAlloc(LMEM_MOVEABLE, cb + 1);
    if (!h) {
        msg_box(IDS_NOMEMCARD, NULL, MB_OK | MB_ICONEXCLAMATION);
        return FALSE;
    }
    text = (LPSTR)LocalLock(h);
    GetWindowText(g_edit, text, cb + 1);
    if (!temp_open()) {
        LocalUnlock(h);
        LocalFree(h);
        msg_box(IDS_NOTEMP, NULL, MB_OK | MB_ICONEXCLAMATION);
        return FALSE;
    }
    _llseek(temp, temp_end, 0);
    fmt = g_pic.kind == PIC_BITMAP ? FMT_MGC : FMT_RRG;
    pos = body_write(temp, fmt, text, cb, &g_pic);
    LocalUnlock(h);
    LocalFree(h);
    if (pos == (DWORD)-1) {
        msg_box(IDS_DISKFULL, NULL, MB_OK | MB_ICONEXCLAMATION);
        return FALSE;
    }
    c->where = WHERE_TEMP;
    c->fmt = (BYTE)fmt;
    c->pos = temp_end;
    temp_end = _llseek(temp, 0, 1);
    g_carddirty = FALSE;
    SendMessage(g_edit, EM_SETMODIFY, FALSE, 0);
    g_dirty = TRUE;
    return TRUE;
}
