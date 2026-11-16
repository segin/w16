/*
 * wrifile.c - w16 Write: reading and writing .WRI files.
 *
 * The layout is docs/wri-format.md, observed black-box: documents saved
 * by the original Write under scripted input (probes WR1-WR5) and
 * Microsoft's own .WRI files. The writer lays out the FKP pages exactly
 * as the original does (tools/wri.py rebuilds every reference file
 * byte for byte with the same rules); bytes the original leaves as stale
 * memory (FKP gaps, the ends of pages) are written as zeros.
 */
#include "write.h"

#define PAGE 128

/* ---- reading ---- */

static HMEM fbuf;
static DWORD flen;

static WORD rw(DWORD off)
{
    WORD v = 0;
    if (off + 2 <= flen)
        hm_get(&fbuf, off, &v, 2);
    return v;
}

static DWORD rdw(DWORD off)
{
    DWORD v = 0;
    if (off + 4 <= flen)
        hm_get(&fbuf, off, &v, 4);
    return v;
}

static BYTE rb(DWORD off)
{
    return off < flen ? *hm_ptr(&fbuf, off) : 0;
}

static int read_file(LPCSTR path)
{
    OFSTRUCT of;
    HFILE f = OpenFile(path, &of, OF_READ | OF_SHARE_DENY_WRITE);
    DWORD off = 0;
    if (f == HFILE_ERROR)
        f = OpenFile(path, &of, OF_READ);
    if (f == HFILE_ERROR)
        return LOAD_NOFILE;
    flen = _llseek(f, 0, 2);
    _llseek(f, 0, 0);
    if (!hm_resize(&fbuf, flen + 16)) {
        _lclose(f);
        return LOAD_NOMEM;
    }
    while (off < flen) {
        WORD k = hm_span(off, flen - off);
        if (_lread(f, hm_ptr(&fbuf, off), k) != k)
            break;
        off += k;
    }
    _lclose(f);
    flen = off;
    return LOAD_OK;
}

/* the FODs of FKP pages first..last-1, each as (lim relative to text,
   atom); emit() appends one */
typedef void (*EMITFN)(DOC *d, DWORD lim, ATOM32 a);

static void read_fkps(DOC *d, WORD first, WORD last, EMITFN emit)
{
    WORD pn, i;
    BYTE pr[CHP_MAX + 1];
    for (pn = first; pn < last; pn++) {
        DWORD pg = (DWORD)pn * PAGE;
        BYTE cfod = rb(pg + 127);
        for (i = 0; i < cfod && 4 + 6 * i + 6 <= 127; i++) {
            DWORD fcl = rdw(pg + 4 + 6 * i);
            WORD bf = rw(pg + 4 + 6 * i + 4);
            ATOM32 a = 0;
            if (bf != 0xFFFF && 4 + bf < PAGE) {
                BYTE cch = rb(pg + 4 + bf);
                WORD k;
                for (k = 0; k < cch; k++)
                    pr[k] = rb(pg + 4 + bf + 1 + k);
                a = atom_intern(d, pr, cch);
            }
            emit(d, fcl >= PAGE ? fcl - PAGE : 0, a);
        }
    }
}

static HMEM *emit_tab;
static DWORD *emit_n;

static void emit_ent(DOC *d, DWORD lim, ATOM32 a)
{
    struct { DWORD lim, atom; } e;
    (void)d;
    e.lim = lim;
    e.atom = a;
    if ((*emit_n + 1) * 8 > emit_tab->size && !hm_resize(emit_tab, emit_tab->size * 2 + 64))
        return;
    hm_put(emit_tab, *emit_n * 8, &e, 8);
    (*emit_n)++;
}

static ATOM32 chp_default_atom(DOC *d, ATOM32 a, WORD unused)
{
    (void)d;
    (void)a;
    (void)unused;
    return 0;                           /* atom 0: no bytes, FFFFh */
}

int doc_load(DOC *d, LPCSTR path, BOOL *isText)
{
    WORD pnPara, pnFntb, pnSep, pnSetb, pnPgtb, pnFfntb, pnMac, pnChar;
    DWORD fcMac, i, cp;
    int r = read_file(path);
    *isText = FALSE;
    if (r != LOAD_OK)
        return r;
    if (flen < PAGE || (rw(0) != 0xBE31 && rw(0) != 0xBE32) || rw(4) != 0xAB00 || rw(96) == 0) {
        /* a text file, or a Word for DOS one (pnMac 0): the caller asks
           whether to convert */
        hm_free(&fbuf);
        *isText = TRUE;
        return LOAD_NOTWRI;
    }
    fcMac = rdw(14);
    pnPara = rw(18);
    pnFntb = rw(20);
    pnSep = rw(22);
    pnSetb = rw(24);
    pnPgtb = rw(26);
    pnFfntb = rw(28);
    pnMac = rw(96);
    pnChar = (WORD)((fcMac + PAGE - 1) / PAGE);
    if (fcMac < PAGE || pnChar > pnPara || pnPara > pnFntb || pnFntb > pnSep ||
        pnSep > pnSetb || pnSetb > pnPgtb || pnPgtb > pnFfntb || pnFfntb > pnMac ||
        (DWORD)pnMac * PAGE > flen) {
        hm_free(&fbuf);
        return LOAD_CORRUPT;
    }
    doc_init(d);
    d->ident = rw(0);
    /* text */
    d->cpMac = fcMac - PAGE;
    if (!hm_resize(&d->text, d->cpMac + 1024)) {
        hm_free(&fbuf);
        return LOAD_NOMEM;
    }
    for (cp = 0; cp < d->cpMac;) {
        WORD k = hm_span(cp, d->cpMac - cp);
        k = hm_span(PAGE + cp, k);
        hm_get(&fbuf, PAGE + cp, hm_ptr(&d->text, cp), k);
        cp += k;
    }
    d->gap = d->cpMac;
    d->gapLen = 1024;
    /* properties */
    d->nruns = 0;
    emit_tab = &d->runs;
    emit_n = &d->nruns;
    read_fkps(d, pnChar, pnPara, emit_ent);
    d->nparas = 0;
    emit_tab = &d->paras;
    emit_n = &d->nparas;
    read_fkps(d, pnPara, pnFntb, emit_ent);
    /* make the tables consistent with the text: runs end at cpMac,
       paragraphs at cpMac + 2 */
    {
        struct { DWORD lim, atom; } e;
        if (d->nruns) {
            hm_get(&d->runs, (d->nruns - 1) * 8, &e, 8);
            if (e.lim != d->cpMac) {
                e.lim = d->cpMac;
                hm_put(&d->runs, (d->nruns - 1) * 8, &e, 8);
            }
        } else if (d->cpMac) {
            e.lim = d->cpMac;
            e.atom = 0;
            emit_tab = &d->runs;
            emit_n = &d->nruns;
            emit_ent(d, d->cpMac, 0);
        }
        if (d->nparas)
            hm_get(&d->paras, (d->nparas - 1) * 8, &e, 8);
        if (!d->nparas || e.lim < d->cpMac + 2) {
            emit_tab = &d->paras;
            emit_n = &d->nparas;
            emit_ent(d, d->cpMac + 2, d->nparas ? e.atom : 0);
        }
    }
    /* section */
    if (pnSep < pnSetb) {
        DWORD pg = (DWORD)pnSep * PAGE;
        BYTE cch = rb(pg);
        for (i = 0; i <= cch && i < PAGE; i++)
            d->sep[(WORD)i] = rb(pg + i);
        d->hasSep = TRUE;
        /* the SETB's file number: the original writes 2 for a document it
           read from a file (WR2, WR5: SYSINI.WRI's 4 became 2), 1 for a
           new one (WR4). UNVERIFIED: Write's own file numbering. */
        d->sedFn = 2;
    }
    /* page table */
    if (pnPgtb < pnFfntb) {
        DWORD pg = (DWORD)pnPgtb * PAGE;
        WORD n = rw(pg);
        if (hm_resize(&d->pgtb, (DWORD)n * 8 + 8)) {
            for (i = 0; i < n; i++) {
                struct { WORD pgn, pad; DWORD cp; } e;
                e.pgn = rw(pg + 4 + 6 * i);
                e.pad = 0;
                e.cp = rdw(pg + 4 + 6 * i + 2);
                hm_put(&d->pgtb, i * 8, &e, 8);
            }
            d->npages = n;
        }
    }
    /* fonts */
    if (pnFfntb < pnMac) {
        DWORD o = (DWORD)pnFfntb * PAGE, end = (DWORD)pnMac * PAGE;
        WORD n = rw(o), k;
        o += 2;
        for (k = 0; k < n && o + 2 <= end;) {
            WORD cb = rw(o);
            FFN f;
            WORD j;
            if (cb == 0xFFFF) {         /* continued on the next page */
                o = (o / PAGE + 1) * PAGE;
                continue;
            }
            if (cb == 0)
                break;
            f.ffid = rb(o + 2);
            for (j = 0; j < sizeof f.name - 1 && j + 1 < cb; j++)
                if ((f.name[j] = rb(o + 3 + j)) == 0)
                    break;
            f.name[j] = 0;
            if ((d->nfonts + 1) * sizeof(FFN) > d->fonts.size)
                hm_resize(&d->fonts, d->fonts.size * 2 + 8 * sizeof(FFN));
            hm_put(&d->fonts, d->nfonts * sizeof(FFN), &f, sizeof f);
            d->nfonts++;
            o += 2 + cb;
            k++;
        }
        d->hasFfntb = TRUE;
    }
    hm_free(&fbuf);
    /* the header of every picture has the default character properties
       once loaded (WR6: the original's re-save writes FFFFh for those 40
       bytes even when the file had them in a text run) */
    for (i = 0; i < d->nparas; i++) {
        DWORD pf, pl;
        ATOM32 pa;
        PAPX p;
        para_get(d, i, &pf, &pl, &pa);
        pap_decode(d, pa, &p);
        if (p.fGraphics && pl <= d->cpMac && pl >= pf + 40) {
            WORD cbh = 0;
            doc_get(d, pf + 30, (char FAR *)&cbh, 2);
            if (cbh < 40 || pf + cbh > pl)
                cbh = 40;
            doc_set_chp(d, pf, pf + cbh, chp_default_atom, 0);
        }
    }
    d->dirty = FALSE;
    return LOAD_OK;
}

/* A text file (or a Word document's text, UNVERIFIED) as a Write
   document: paragraphs at CR LF, default properties. */
int doc_load_text(DOC *d, LPCSTR path)
{
    DWORD i, start = 0;
    int r = read_file(path);
    if (r != LOAD_OK)
        return r;
    doc_init(d);
    font_code(d, "Arial", FF_SWISS);    /* UNVERIFIED: the font converted
                                           text gets */
    if (!hm_resize(&d->text, flen + 1024)) {
        hm_free(&fbuf);
        return LOAD_NOMEM;
    }
    d->gapLen = d->text.size;
    for (i = 0; i <= flen; i++) {
        if (i == flen || (i + 1 < flen && rb(i) == '\r' && rb(i + 1) == '\n')) {
            DWORD lim = i == flen ? flen : i + 2;
            DWORD o;
            for (o = start; o < lim;) {
                char buf[256];
                WORD k = (WORD)(lim - o > sizeof buf ? sizeof buf : lim - o);
                hm_get(&fbuf, o, buf, k);
                doc_insert(d, d->cpMac, buf, k, 0);
                o += k;
            }
            start = lim;
            if (i < flen)
                i++;
        }
    }
    hm_free(&fbuf);
    d->dirty = FALSE;
    return LOAD_OK;
}

/* ---- writing ---- */

static HFILE wf;
static BOOL werr;
static BYTE page[PAGE];
static WORD pn;                         /* pages written */

static void put_page(void)
{
    if (_lwrite(wf, (LPCSTR)page, PAGE) != PAGE)
        werr = TRUE;
    m_set(page, 0, PAGE);
    pn++;
}

/* FKP pages for (lim, atom) entries of a table: equal neighbours of CHP
   runs are merged already; a PAP FOD shares the page's last FPROP when
   the bytes are the same (NETWORKS.WRI), each CHP FOD has its own; a new
   page when the next FOD and its FPROP no longer fit (byte 127 is cfod) */
static void write_fkps(DOC *d, HMEM *tab, DWORD n, BOOL chp)
{
    DWORD i = 0, fcFirst = PAGE;
    BYTE pr[CHP_MAX + 1];
    while (i < n) {
        WORD top = 127, cfod = 0;
        int lastLen = -1;
        WORD lastBf = 0;
        static BYTE last[CHP_MAX + 1];
        m_set(page, 0, PAGE);
        *(DWORD *)page = fcFirst;
        while (i < n) {
            struct { DWORD lim, atom; } e;
            WORD len, need, bf;
            BOOL share;
            hm_get(tab, i * 8, &e, 8);
            len = atom_get(d, e.atom, pr);
            share = !chp && len && (int)len == lastLen && !m_cmp(pr, last, len);
            need = (len && !share) ? 6 + 1 + len : 6;
            if (4 + 6 * cfod + need > top)
                break;
            if (!len) {
                bf = 0xFFFF;
                lastLen = -1;           /* a default FOD ends the sharing (WR6) */
            } else if (share) {
                bf = lastBf;
            } else {
                top -= 1 + len;
                page[top] = (BYTE)len;
                m_cpy(page + top + 1, pr, len);
                bf = top - 4;
                m_cpy(last, pr, len);
                lastLen = len;
                lastBf = bf;
            }
            *(DWORD *)(page + 4 + 6 * cfod) = e.lim + PAGE;
            *(WORD *)(page + 4 + 6 * cfod + 4) = bf;
            cfod++;
            fcFirst = e.lim + PAGE;
            i++;
        }
        if (!cfod) {                    /* cannot happen: a FOD fits */
            werr = TRUE;
            return;
        }
        page[127] = (BYTE)cfod;
        put_page();
    }
}

/* the bytes of a table that may span pages, appended at *o */
static void put_bytes(WORD *o, const void *src, WORD n)
{
    const BYTE *s = (const BYTE *)src;
    while (n--) {
        page[(*o)++] = *s++;
        if (*o == PAGE) {
            put_page();
            *o = 0;
        }
    }
}

int doc_save(DOC *d, LPCSTR path, int format)
{
    OFSTRUCT of;
    DWORD cp;
    WORD pnChar, pnPara, pnFntb, pnSep, pnSetb, pnPgtb, pnFfntb;
    WORD o;
    char buf[PAGE];
    wf = OpenFile(path, &of, OF_CREATE | OF_WRITE);
    if (wf == HFILE_ERROR)
        return 0;
    werr = FALSE;
    pn = 0;
    if (format == FMT_TEXT) {
        for (cp = 0; cp < d->cpMac && !werr;) {
            WORD k = (WORD)doc_get(d, cp, buf, sizeof buf);
            if (_lwrite(wf, buf, k) != k)
                werr = TRUE;
            cp += k;
        }
        _lclose(wf);
        return !werr;
    }
    /* header last; text from page 1 */
    m_set(page, 0, PAGE);
    put_page();
    for (cp = 0; cp < d->cpMac;) {
        WORD k = (WORD)doc_get(d, cp, (char FAR *)page, PAGE);
        cp += k;
        if (k == PAGE || cp >= d->cpMac) {
            if (k < PAGE)
                m_set(page + k, 0, PAGE - k);
            put_page();
        }
    }
    pnChar = pn;
    write_fkps(d, &d->runs, d->nruns, TRUE);
    pnPara = pn;
    write_fkps(d, &d->paras, d->nparas, FALSE);
    pnFntb = pnSep = pn;                /* Write has no footnotes */
    if (d->hasSep) {
        m_cpy(page, d->sep, d->sep[0] + 1 > PAGE ? PAGE : d->sep[0] + 1);
        put_page();
    }
    pnSetb = pn;
    if (d->hasSep) {
        WORD *w = (WORD *)page;
        w[0] = 2;                       /* cSed, cSedMax */
        w[1] = 2;
        *(DWORD *)(page + 4) = d->cpMac;
        *(WORD *)(page + 8) = d->sedFn;
        *(DWORD *)(page + 10) = (DWORD)pnSep * PAGE;
        *(DWORD *)(page + 14) = d->cpMac + 1;
        *(WORD *)(page + 18) = 0x7FFF;
        *(DWORD *)(page + 20) = 0xFFFFFFFFL;
        put_page();
    }
    pnPgtb = pn;
    if (d->npages) {
        DWORD i;
        WORD n = (WORD)d->npages;
        o = 0;
        put_bytes(&o, &n, 2);
        put_bytes(&o, &n, 2);
        for (i = 0; i < d->npages; i++) {
            struct { WORD pgn, pad; DWORD cp; } e;
            hm_get(&d->pgtb, i * 8, &e, 8);
            put_bytes(&o, &e.pgn, 2);
            put_bytes(&o, &e.cp, 4);
        }
        if (o)
            put_page();
    }
    pnFfntb = pn;
    if (d->hasFfntb) {
        DWORD i;
        WORD n = (WORD)d->nfonts, cb, z = 0, ff = 0xFFFF;
        o = 0;
        put_bytes(&o, &n, 2);
        for (i = 0; i < d->nfonts; i++) {
            FFN f;
            hm_get(&d->fonts, i * sizeof f, &f, sizeof f);
            cb = 1 + s_len(f.name) + 1;
            /* an entry does not cross pages: FFFFh sends the reader on
               (UNVERIFIED: where exactly the original breaks) */
            if (o + 2 + cb > PAGE - 2) {
                put_bytes(&o, &ff, 2);
                if (o)
                    put_page();
                o = 0;
            }
            put_bytes(&o, &cb, 2);
            put_bytes(&o, &f.ffid, 1);
            put_bytes(&o, f.name, cb - 1);
        }
        put_bytes(&o, &z, 2);
        if (o)
            put_page();
    }
    /* header */
    m_set(page, 0, PAGE);
    *(WORD *)(page + 0) = d->ident;
    *(WORD *)(page + 4) = 0xAB00;
    *(DWORD *)(page + 14) = d->cpMac + PAGE;
    *(WORD *)(page + 18) = pnPara;
    *(WORD *)(page + 20) = pnFntb;
    *(WORD *)(page + 22) = pnSep;
    *(WORD *)(page + 24) = pnSetb;
    *(WORD *)(page + 26) = pnPgtb;
    *(WORD *)(page + 28) = pnFfntb;
    *(WORD *)(page + 96) = pn;
    (void)pnChar;
    _llseek(wf, 0, 0);
    if (_lwrite(wf, (LPCSTR)page, PAGE) != PAGE)
        werr = TRUE;
    _lclose(wf);
    if (!werr)
        d->dirty = FALSE;
    return !werr;
}
