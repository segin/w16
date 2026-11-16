/*
 * doc.c - w16 Write: the document in memory.
 *
 * Text is one gap buffer of bytes, paragraphs ending in CR LF. Character
 * properties are runs {cpLim, atom}; paragraph properties are one entry
 * per paragraph {cpLim, atom}, the last being Write's end paragraph whose
 * cpLim is cpMac + 2 (the file's last PAP FOD reaches fcMac + 2, WR1).
 * An atom is an interned property byte string exactly as an FPROP
 * stores it (docs/wri-format.md), so saving writes what was read and
 * equal properties compare as equal numbers.
 *
 * No fixed limits (ZOI): every table is a huge growable block.
 */
#include "write.h"

DOC g_doc;

typedef struct {
    DWORD lim;
    DWORD atom;
} ENT;                                  /* 8 bytes: never straddles 64K */

/* ---- tables of ENT ---- */

static void ent_get(HMEM *m, DWORD i, ENT *e)
{
    *e = *(ENT FAR *)hm_ptr(m, i << 3);
}

static void ent_put(HMEM *m, DWORD i, const ENT *e)
{
    *(ENT FAR *)hm_ptr(m, i << 3) = *e;
}

static BOOL ent_insert(HMEM *m, DWORD *n, DWORD i, const ENT *e)
{
    if ((*n + 1) << 3 > m->size) {
        DWORD sz = m->size + (m->size >> 1) + 64;
        if (!hm_resize(m, sz))
            return FALSE;
    }
    hm_move(m, (i + 1) << 3, i << 3, (*n - i) << 3);
    ent_put(m, i, e);
    (*n)++;
    return TRUE;
}

static void ent_remove(HMEM *m, DWORD *n, DWORD i, DWORD k)
{
    hm_move(m, i << 3, (i + k) << 3, (*n - i - k) << 3);
    *n -= k;
}

/* first entry whose lim > cp (n if none) */
static DWORD ent_find(HMEM *m, DWORD n, DWORD cp)
{
    DWORD lo = 0, hi = n;
    while (lo < hi) {
        DWORD mid = (lo + hi) >> 1;
        ENT e;
        ent_get(m, mid, &e);
        if (e.lim > cp)
            hi = mid;
        else
            lo = mid + 1;
    }
    return lo;
}

/* ---- atoms ---- */

ATOM32 atom_intern(DOC *d, const BYTE FAR *b, WORD len)
{
    DWORD i, off;
    BYTE tmp[CHP_MAX + 1];
    for (i = 0; i < d->natoms; i++) {
        off = *(DWORD FAR *)hm_ptr(&d->atoms, i << 2);
        hm_get(&d->pool, off, tmp, 1);
        if (tmp[0] != len)
            continue;
        hm_get(&d->pool, off + 1, tmp, len);
        if (!m_cmp(tmp, b, len))
            return i;
    }
    if ((d->natoms + 1) << 2 > d->atoms.size && !hm_resize(&d->atoms, d->atoms.size * 2 + 64))
        return 0;
    if (d->poolLen + len + 1 > d->pool.size && !hm_resize(&d->pool, d->pool.size * 2 + len + 256))
        return 0;
    *(DWORD FAR *)hm_ptr(&d->atoms, d->natoms << 2) = d->poolLen;
    tmp[0] = (BYTE)len;
    hm_put(&d->pool, d->poolLen, tmp, 1);
    hm_put(&d->pool, d->poolLen + 1, b, len);
    d->poolLen += len + 1;
    return d->natoms++;
}

WORD atom_get(DOC *d, ATOM32 a, BYTE FAR *b)
{
    DWORD off;
    BYTE n;
    if (a >= d->natoms)
        return 0;
    off = *(DWORD FAR *)hm_ptr(&d->atoms, a << 2);
    hm_get(&d->pool, off, &n, 1);
    hm_get(&d->pool, off + 1, b, n);
    return n;
}

/* ---- character properties ----
 * Bytes (observed, WR3): 0: 1 after Regular (and in new documents), 0
 * once any formatting was applied; 1: bit 0 bold, bit 1 italic, bits 2-7
 * font code (low 6 bits); 2: size in half points; 3: bit 0 underline;
 * 4: bits 0-2 font code bits 6-8 (UNVERIFIED: no document seen with more
 * than 64 fonts); 5: vertical position, signed half points (> 0 super).
 * The FPROP keeps bytes up to the last one that differs from the default
 * {1, 0, 24, 0, 0, 0}. */

static const BYTE chp_def[6] = { 1, 0, 24, 0, 0, 0 };

void chp_default(CHPX *c)
{
    c->ftc = 0;
    c->hps = 24;
    c->bold = c->italic = c->underline = 0;
    c->hpsPos = 0;
    c->b0 = 1;
}

void chp_decode(DOC *d, ATOM32 a, CHPX *c)
{
    BYTE b[CHP_MAX + 1];
    WORD n = atom_get(d, a, b);
    for (; n < 6; n++)
        b[n] = chp_def[n];
    c->b0 = b[0];
    c->bold = b[1] & 1;
    c->italic = (b[1] >> 1) & 1;
    c->ftc = (b[1] >> 2) | ((WORD)(b[4] & 7) << 6);
    c->hps = b[2];
    c->underline = b[3] & 1;
    c->hpsPos = (signed char)b[5];
}

ATOM32 chp_atom(DOC *d, const CHPX *c)
{
    BYTE b[6];
    int n;
    b[0] = c->b0;
    b[1] = (BYTE)((c->bold ? 1 : 0) | (c->italic ? 2 : 0) | ((c->ftc & 0x3F) << 2));
    b[2] = c->hps;
    b[3] = c->underline ? 1 : 0;
    b[4] = (BYTE)((c->ftc >> 6) & 7);
    b[5] = (BYTE)c->hpsPos;
    for (n = 6; n > 0 && b[n - 1] == chp_def[n - 1]; n--)
        ;
    return atom_intern(d, b, (WORD)n);
}

/* ---- paragraph properties ----
 * Bytes (observed, WR3/WR4): 0: 3Ch (3Dh in some of Microsoft's files);
 * 1: alignment; 2-3: 30; 4-5 right indent, 6-7 left indent, 8-9 first
 * line (relative to the left indent), 10-11 line spacing (240 single),
 * all twips; 16: running-head bits; 22 on: tab stops {dxa, jc, 0}.
 * The FPROP keeps bytes up to the last one that differs from the default;
 * with tab stops, up to and including an empty stop after the last one.
 * A paragraph that was formatted at all stores at least 1 byte (cch 1
 * "3C" for an explicit Left, WR5); Normal stores none (WR3). */

static const BYTE pap_def[PAP_TABS] = {
    0x3C, 0, 30, 0, 0, 0, 0, 0, 0, 0, 240, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0
};

void pap_default(PAPX *p)
{
    m_set(p, 0, sizeof *p);
    p->dyaLine = 240;
}

void pap_decode(DOC *d, ATOM32 a, PAPX *p)
{
    BYTE b[CHP_MAX + 1 + PAP_LEN];
    WORD n = atom_get(d, a, b), i;
    for (i = n; i < PAP_TABS; i++)
        b[i] = pap_def[i];
    for (i = n > PAP_TABS ? n : PAP_TABS; i < PAP_LEN; i++)
        b[i] = 0;
    p->jc = b[1] & 3;
    p->dxaRight = *(int *)(b + 4);
    p->dxaLeft = *(int *)(b + 6);
    p->dxaLeft1 = *(int *)(b + 8);
    p->dyaLine = *(int *)(b + 10);
    p->rhc = b[16];
    p->fGraphics = (b[16] & 0x10) != 0;     /* UNVERIFIED */
    for (i = 0; i < NTABS; i++) {
        int dxa = *(int *)(b + PAP_TABS + 4 * i);
        if (!dxa)
            break;
        p->tab[i].dxa = dxa;
        p->tab[i].jc = b[PAP_TABS + 4 * i + 2];
    }
    p->ntabs = i;
}

/* explicit: the paragraph was formatted (stores at least 1 byte) */
ATOM32 pap_atom_x(DOC *d, const PAPX *p, BOOL expl)
{
    BYTE b[PAP_LEN];
    int n, i;
    m_cpy(b, pap_def, PAP_TABS);
    m_set(b + PAP_TABS, 0, PAP_LEN - PAP_TABS);
    b[1] = p->jc;
    *(int *)(b + 4) = p->dxaRight;
    *(int *)(b + 6) = p->dxaLeft;
    *(int *)(b + 8) = p->dxaLeft1;
    *(int *)(b + 10) = p->dyaLine;
    b[16] = p->rhc;
    for (i = 0; i < p->ntabs; i++) {
        *(int *)(b + PAP_TABS + 4 * i) = p->tab[i].dxa;
        b[PAP_TABS + 4 * i + 2] = p->tab[i].jc;
    }
    if (p->ntabs) {
        n = PAP_TABS + 4 * (p->ntabs + 1);
        if (n > PAP_LEN)
            n = PAP_LEN;
    } else {
        for (n = PAP_TABS; n > 0 && b[n - 1] == pap_def[n - 1]; n--)
            ;
    }
    if (expl && n == 0)
        n = 1;
    return atom_intern(d, b, (WORD)n);
}

ATOM32 pap_atom(DOC *d, const PAPX *p)
{
    return pap_atom_x(d, p, TRUE);
}

/* ---- section ----
 * SEP bytes after cch (observed, WR4, README.WRI): 0-1: 0, 2; then words
 * yaMac, xaMac, pgnFirst, yaTop, dyaText, xaLeft, dxaText; 0, 1 (bytes);
 * yaHeader, yaFooter; then 720, 0, 1080, 10440 (constant in every file
 * seen; meaning UNVERIFIED); zeros to cch 102. */

static const WORD sep_template[] = {
    0x0200, 15840, 12240, 1, 1440, 12960, 1800, 8640, 0x0100, 1080, 14760,
    720, 0, 1080, 10440
};

void sep_decode(DOC *d, SEPX *s)
{
    const BYTE *b = d->sep + 1;
    WORD v[15];
    int i;
    for (i = 0; i < 15; i++)
        v[i] = (d->hasSep && d->sep[0] >= 2 * i + 2) ? *(WORD *)(b + 2 * i) : sep_template[i];
    s->yaMac = v[1];
    s->xaMac = v[2];
    s->pgnFirst = v[3];
    s->yaTop = v[4];
    s->dyaText = v[5];
    s->xaLeft = v[6];
    s->dxaText = v[7];
    s->yaHeader = v[9];
    s->yaFooter = v[10];
}

void sep_encode(DOC *d, const SEPX *s)
{
    WORD *w = (WORD *)(d->sep + 1);
    int i;
    if (!d->hasSep) {
        m_set(d->sep, 0, sizeof d->sep);
        d->sep[0] = 102;
        for (i = 0; i < 15; i++)
            w[i] = sep_template[i];
        d->hasSep = TRUE;
        d->sedFn = 1;
    }
    w[1] = s->yaMac;
    w[2] = s->xaMac;
    w[3] = s->pgnFirst;
    w[4] = s->yaTop;
    w[5] = s->dyaText;
    w[6] = s->xaLeft;
    w[7] = s->dxaText;
    w[9] = s->yaHeader;
    w[10] = s->yaFooter;
}

/* ---- fonts ---- */

FFN FAR *font_get(DOC *d, WORD ftc)
{
    static FFN f;
    if (ftc < d->nfonts) {
        hm_get(&d->fonts, (DWORD)ftc * sizeof(FFN), &f, sizeof f);
    } else {
        /* a document without a font table (Write 2.x's, e.g. PRACTICE.WRI):
           font codes name families; 0 shows fixed pitch, 1 as sans serif
           (observed on 3.1; other codes UNVERIFIED) */
        f.name[0] = 0;
        f.ffid = ftc == 0 ? FF_MODERN : ftc == 1 ? FF_SWISS : FF_ROMAN;
    }
    return &f;
}

WORD font_code(DOC *d, const char FAR *name, BYTE ffid)
{
    FFN f;
    DWORD i;
    for (i = 0; i < d->nfonts; i++) {
        hm_get(&d->fonts, i * sizeof(FFN), &f, sizeof f);
        if (!s_icmp(f.name, name))
            return (WORD)i;
    }
    if ((d->nfonts + 1) * sizeof(FFN) > d->fonts.size &&
        !hm_resize(&d->fonts, d->fonts.size * 2 + 8 * sizeof(FFN)))
        return 0;
    s_cpy(f.name, name, sizeof f.name);
    f.ffid = ffid;
    hm_put(&d->fonts, d->nfonts * sizeof(FFN), &f, sizeof f);
    d->hasFfntb = TRUE;
    return (WORD)d->nfonts++;
}

/* ---- the document ---- */

void doc_free(DOC *d)
{
    hm_free(&d->text);
    hm_free(&d->runs);
    hm_free(&d->paras);
    hm_free(&d->atoms);
    hm_free(&d->pool);
    hm_free(&d->fonts);
    hm_free(&d->pgtb);
    m_set(d, 0, sizeof *d);
}

/* an empty document: the end paragraph only, the default properties */
void doc_init(DOC *d)
{
    ENT e;
    doc_free(d);
    d->ident = 0xBE31;
    hm_resize(&d->text, 1024);
    d->gapLen = 1024;
    hm_resize(&d->runs, 256);
    hm_resize(&d->paras, 256);
    hm_resize(&d->atoms, 256);
    hm_resize(&d->pool, 1024);
    hm_resize(&d->fonts, 8 * sizeof(FFN));
    hm_resize(&d->pgtb, 64);
    atom_intern(d, (const BYTE FAR *)"", 0);    /* atom 0: the default */
    e.lim = 2;
    e.atom = 0;
    ent_insert(&d->paras, &d->nparas, 0, &e);
}

/* a new document: one font, Arial (FF_SWISS), and 10-point text
   (observed: a new document's first run is {1, 0, 20}, WR1). */
void doc_new(DOC *d)
{
    CHPX c;
    doc_init(d);
    font_code(d, "Arial", FF_SWISS);
    chp_default(&c);
    c.hps = 20;
    d->chpNew = chp_atom(d, &c);
    d->dirty = FALSE;
}

static void gap_to(DOC *d, DWORD cp)
{
    if (cp < d->gap)
        hm_move(&d->text, cp + d->gapLen, cp, d->gap - cp);
    else if (cp > d->gap)
        hm_move(&d->text, d->gap, d->gap + d->gapLen, cp - d->gap);
    d->gap = cp;
}

BYTE doc_ch(DOC *d, DWORD cp)
{
    if (cp >= d->gap)
        cp += d->gapLen;
    return *hm_ptr(&d->text, cp);
}

DWORD doc_get(DOC *d, DWORD cp, char FAR *buf, DWORD n)
{
    DWORD k = 0;
    if (cp > d->cpMac)
        return 0;
    if (n > d->cpMac - cp)
        n = d->cpMac - cp;
    if (cp < d->gap) {
        k = d->gap - cp;
        if (k > n)
            k = n;
        hm_get(&d->text, cp, buf, k);
    }
    if (k < n)
        hm_get(&d->text, cp + k + d->gapLen, buf + (WORD)k, n - k);
    return n;
}

void run_get(DOC *d, DWORD i, DWORD *cpLim, ATOM32 *a)
{
    ENT e;
    ent_get(&d->runs, i, &e);
    *cpLim = e.lim;
    *a = e.atom;
}

/* the run holding cp: its index, bounds and atom. At cpMac (and in an
   empty document) the last run, or the default if none. */
DWORD run_at(DOC *d, DWORD cp, DWORD *cpFirst, DWORD *cpLim, ATOM32 *a)
{
    DWORD i = ent_find(&d->runs, d->nruns, cp);
    ENT e;
    if (d->nruns == 0) {
        *cpFirst = *cpLim = 0;
        *a = d->chpNew;
        return 0;
    }
    if (i >= d->nruns)
        i = d->nruns - 1;
    ent_get(&d->runs, i, &e);
    *cpLim = e.lim;
    *a = e.atom;
    if (i) {
        ent_get(&d->runs, i - 1, &e);
        *cpFirst = e.lim;
    } else {
        *cpFirst = 0;
    }
    return i;
}

void para_get(DOC *d, DWORD i, DWORD *cpFirst, DWORD *cpLim, ATOM32 *a)
{
    ENT e;
    ent_get(&d->paras, i, &e);
    *cpLim = e.lim;
    *a = e.atom;
    if (i) {
        ent_get(&d->paras, i - 1, &e);
        *cpFirst = e.lim;
    } else {
        *cpFirst = 0;
    }
}

DWORD para_at(DOC *d, DWORD cp, DWORD *cpFirst, DWORD *cpLim, ATOM32 *a)
{
    DWORD i = ent_find(&d->paras, d->nparas, cp);
    if (i >= d->nparas)
        i = d->nparas - 1;
    para_get(d, i, cpFirst, cpLim, a);
    return i;
}

void para_set(DOC *d, DWORD i, ATOM32 a)
{
    ENT e;
    ent_get(&d->paras, i, &e);
    e.atom = a;
    ent_put(&d->paras, i, &e);
    d->dirty = TRUE;
}

/* merge run i with run i + 1 where both have the same atom */
static void run_merge(DOC *d, DWORD i)
{
    ENT a, b;
    if (i + 1 >= d->nruns)
        return;
    ent_get(&d->runs, i, &a);
    ent_get(&d->runs, i + 1, &b);
    if (a.atom == b.atom) {
        a.lim = b.lim;
        ent_put(&d->runs, i, &a);
        ent_remove(&d->runs, &d->nruns, i + 1, 1);
    }
}

/* make cp a run boundary */
static BOOL run_split(DOC *d, DWORD cp)
{
    DWORD first, lim, i;
    ATOM32 a;
    ENT e;
    if (cp == 0 || cp >= d->cpMac)
        return TRUE;
    i = run_at(d, cp, &first, &lim, &a);
    if (first == cp)
        return TRUE;
    e.lim = cp;
    e.atom = a;
    return ent_insert(&d->runs, &d->nruns, i, &e);
}

BOOL doc_insert(DOC *d, DWORD cp, const char FAR *s, DWORD n, ATOM32 chp)
{
    DWORD i, k, first, lim, pi;
    ATOM32 pa;
    ENT e;
    if (!n)
        return TRUE;
    /* text */
    if (d->gapLen < n) {
        DWORD grow = n + (d->cpMac >> 1) + 1024, tail = d->cpMac - d->gap;
        DWORD old = d->text.size;
        if (!hm_resize(&d->text, old + grow))
            return FALSE;
        hm_move(&d->text, d->gap + d->gapLen + grow, d->gap + d->gapLen, tail);
        d->gapLen += grow;
    }
    gap_to(d, cp);
    hm_put(&d->text, cp, s, n);
    d->gap += n;
    d->gapLen -= n;
    /* character runs: make cp a boundary, then extend the run before
       or after when its properties are chp, else a new run */
    if (d->nruns == 0) {
        e.lim = n;
        e.atom = chp;
        ent_insert(&d->runs, &d->nruns, 0, &e);
    } else {
        ENT p;
        if (!run_split(d, cp))
            return FALSE;
        i = ent_find(&d->runs, d->nruns, cp);
        if (i > 0 && (ent_get(&d->runs, i - 1, &p), p.atom == chp)) {
            i--;
        } else if (i < d->nruns && (ent_get(&d->runs, i, &p), p.atom == chp)) {
            /* run i starts at cp */
        } else {
            e.lim = cp;                 /* + n below */
            e.atom = chp;
            if (!ent_insert(&d->runs, &d->nruns, i, &e))
                return FALSE;
        }
        for (k = i; k < d->nruns; k++) {
            ent_get(&d->runs, k, &e);
            e.lim += n;
            ent_put(&d->runs, k, &e);
        }
        /* the split may have left equal neighbours */
        run_merge(d, i);
        if (i > 0)
            run_merge(d, i - 1);
    }
    /* paragraphs: shift, then split at each CR LF inserted */
    pi = para_at(d, cp, &first, &lim, &pa);
    for (k = pi; k < d->nparas; k++) {
        ent_get(&d->paras, k, &e);
        e.lim += n;
        ent_put(&d->paras, k, &e);
    }
    for (k = 0; k + 1 < n; k++) {
        if (s[(WORD)k] == '\r' && s[(WORD)k + 1] == '\n') {
            e.lim = cp + k + 2;
            e.atom = pa;
            if (!ent_insert(&d->paras, &d->nparas, pi, &e))
                return FALSE;
            pi++;
        }
    }
    d->cpMac += n;
    d->dirty = TRUE;
    return TRUE;
}

static DWORD map_cp(DWORD x, DWORD cp, DWORD n)
{
    if (x <= cp)
        return x;
    if (x >= cp + n)
        return x - n;
    return cp;
}

BOOL doc_delete(DOC *d, DWORD cp, DWORD n)
{
    DWORD i;
    ENT e, p;
    if (!n)
        return TRUE;
    gap_to(d, cp);
    d->gapLen += n;
    if (n == d->cpMac && d->nruns) {    /* typing goes on in these */
        ent_get(&d->runs, 0, &e);
        d->chpNew = e.atom;
    }
    /* runs: map the limits, drop the emptied ones, merge */
    for (i = 0; i < d->nruns; i++) {
        ent_get(&d->runs, i, &e);
        e.lim = map_cp(e.lim, cp, n);
        ent_put(&d->runs, i, &e);
    }
    for (i = 0; i < d->nruns;) {
        ent_get(&d->runs, i, &e);
        if (i)
            ent_get(&d->runs, i - 1, &p);
        if (e.lim == (i ? p.lim : 0))
            ent_remove(&d->runs, &d->nruns, i, 1);
        else
            i++;
    }
    for (i = 0; i + 1 < d->nruns;) {
        DWORD before = d->nruns;
        run_merge(d, i);
        if (d->nruns == before)
            i++;
    }
    /* paragraphs: one whose mark was deleted joins the next; the end
       paragraph stays */
    for (i = 0; i < d->nparas;) {
        ent_get(&d->paras, i, &e);
        if (i + 1 < d->nparas && e.lim - 1 >= cp && e.lim - 1 < cp + n) {
            ent_remove(&d->paras, &d->nparas, i, 1);
            continue;
        }
        e.lim = map_cp(e.lim, cp, n);
        ent_put(&d->paras, i, &e);
        i++;
    }
    d->cpMac -= n;
    d->dirty = TRUE;
    return TRUE;
}

/* apply fn to the character properties of [cp, cpLim) */
BOOL doc_set_chp(DOC *d, DWORD cp, DWORD cpLim, ATOM32 (*fn)(DOC *, ATOM32, WORD), WORD arg)
{
    DWORD i, first, lim;
    ATOM32 a;
    ENT e;
    if (cp >= cpLim)
        return TRUE;
    if (!run_split(d, cp) || !run_split(d, cpLim))
        return FALSE;
    i = run_at(d, cp, &first, &lim, &a);
    for (; i < d->nruns; i++) {
        ent_get(&d->runs, i, &e);
        e.atom = fn(d, e.atom, arg);
        ent_put(&d->runs, i, &e);
        if (e.lim >= cpLim)
            break;
    }
    for (i = 0; i + 1 < d->nruns;) {
        DWORD before = d->nruns;
        run_merge(d, i);
        if (d->nruns == before)
            i++;
    }
    d->dirty = TRUE;
    return TRUE;
}

/* Formatting Write's last paragraph gives it a paragraph mark first
   (WR3, WR4, WR5): CR LF at the end of the text, even when that
   paragraph is empty (the caret stays before it, so what is typed next
   goes into the formatted paragraph); the end paragraph keeps the same
   properties. The mark has the properties typing would have there
   (chp; WR8: the size just chosen with Enlarge, not the last
   character's). */
BOOL doc_terminate_last(DOC *d, ATOM32 chp)
{
    return doc_insert(d, d->cpMac, "\r\n", 2, chp);
}
