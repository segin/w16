/*
 * view.c - w16 Windows Help: laying out and drawing topics (the topic
 * window MS_WINTOPIC, the non-scrolling region MS_WINNSR, popups
 * MS_WINNOTE and secondary windows' topics).
 *
 * Measured on 3.1's WINHELP (WH1, WH2 screenshots of NOTEPAD.HLP and
 * GLOSSARY.HLP's topics):
 *  - lengths in the file are half points; a pixel is hp * LOGPIXELSY / 144,
 *    truncated (VGA: 2/3); text starts 8 pixels in from the window's left
 *    edge plus the paragraph's left indent, and wraps 8 pixels plus the
 *    right indent from its right edge;
 *  - a line is as high as its tallest font's cell (tmHeight), unless the
 *    paragraph's line spacing says otherwise: negative values are exact,
 *    positive ones a minimum; spacing above a paragraph comes before its
 *    first line (not after a \line break), spacing below after its last;
 *  - jumps are green (RGB 0,128,0) and underlined one pixel below the
 *    baseline; popups green with a dotted underline; hot spots "without
 *    font change" are drawn as the text around them;
 *  - a popup (MS_WINNOTE, WS_POPUP, topmost) has a 2-pixel black frame and
 *    a 12-pixel shadow of a 50% black dither to the right and below; it is
 *    laid out two thirds of the screen wide, then narrowed to its widest
 *    line; the window (shadow included) is centred on the click, 10 pixels
 *    below it.
 */
#include "winhelp.h"

#define MARGIN      8
#define GREEN       RGB(0, 128, 0)
#define MAXITEMS    1500
#define MAXPOOL     24000
#define MAXHOTS     400
#define MAXPICTS    48
#define MAXSHG      128
#define LMAX        400     /* atoms on one line */

/* ---- fonts ---- */

static int px(int hp)
{
    static int lpy;
    if (!lpy) {
        HDC dc = GetDC(NULL);
        lpy = GetDeviceCaps(dc, LOGPIXELSY);
        ReleaseDC(NULL, dc);
    }
    return (int)((long)hp * lpy / 144);
}

void fonts_free(VIEW *v)
{
    WORD i;
    for (i = 0; i < v->nfonts; i++)
        if (v->fonts[i])
            DeleteObject(v->fonts[i]);
    v->nfonts = 0;
}

/* the view's slot for |FONT descriptor f (made on first use) */
static WORD font_slot(VIEW *v, HDC dc, WORD f)
{
    WORD i;
    LOGFONT lf;
    TEXTMETRIC tm;
    HLPFONT FAR *d;
    HFONT old;
    for (i = 0; i < v->nfonts; i++)
        if (v->fontidx[i] == f)
            return i;
    if (v->nfonts == VIEW_MAXFONT)
        return 0;
    m_set(&lf, 0, sizeof lf);
    if (v->hlp && f < v->hlp->nfont) {
        static const BYTE fam[] = { FF_DONTCARE, FF_MODERN, FF_ROMAN, FF_SWISS, FF_SCRIPT, FF_DECORATIVE };
        d = &v->hlp->fonts[f];
        lf.lfHeight = -px(d->halfpts);
        lf.lfWeight = d->attr & 1 ? FW_BOLD : FW_NORMAL;
        lf.lfItalic = (d->attr & 2) != 0;
        lf.lfUnderline = (d->attr & 0x14) != 0;
        lf.lfStrikeOut = (d->attr & 8) != 0;
        lf.lfPitchAndFamily = d->family < 6 ? fam[d->family] : FF_DONTCARE;
        if (d->face < v->hlp->nface)
            hlp_strcpyn(lf.lfFaceName, v->hlp->faces + d->face * 32, LF_FACESIZE);
    } else {
        lf.lfHeight = -px(20);
        lf.lfWeight = FW_NORMAL;
        lstrcpy(lf.lfFaceName, "Helv");
    }
    i = v->nfonts++;
    v->fontidx[i] = f;
    v->fonts[i] = CreateFontIndirect(&lf);
    old = SelectObject(dc, v->fonts[i]);
    GetTextMetrics(dc, &tm);
    SelectObject(dc, old);
    v->fascent[i] = (BYTE)tm.tmAscent;
    v->fheight[i] = (BYTE)tm.tmHeight;
    return i;
}

static COLORREF font_color(VIEW *v, WORD slot)
{
    WORD f = v->fontidx[slot];
    if (v->hlp && f < v->hlp->nfont) {
        COLORREF c = v->hlp->fonts[f].fg;
        /* 3.1's compiler writes 000101h for "auto": black */
        if (c == RGB(0, 1, 1) || c == RGB(1, 1, 0))
            return RGB(0, 0, 0);
        return c;
    }
    return RGB(0, 0, 0);
}

/* ---- the view's lists ---- */

void view_init(VIEW *v, HWND hwnd, BOOL nsr, BOOL popup)
{
    v->hwnd = hwnd;
    v->nsr = nsr;
    v->popup = popup;
    v->focus = -1;
    v->bg = GetSysColor(COLOR_WINDOW);
    if (!v->items) {
        v->items = (DITEM FAR *)hio_alloc((DWORD)MAXITEMS * sizeof(DITEM));
        v->pool = (char FAR *)hio_alloc(MAXPOOL);
        v->hots = (HOTSPOT FAR *)hio_alloc((DWORD)MAXHOTS * sizeof(HOTSPOT));
        v->picts = (VPICT FAR *)hio_alloc((DWORD)MAXPICTS * sizeof(VPICT));
        v->shg = (SHGHOT FAR *)hio_alloc((DWORD)MAXSHG * sizeof(SHGHOT));
        v->maxitems = MAXITEMS;
        v->maxpool = MAXPOOL;
        v->maxhots = MAXHOTS;
        v->maxpicts = MAXPICTS;
        v->maxshg = MAXSHG;
    }
}

void view_clear(VIEW *v)
{
    WORD i;
    for (i = 0; i < v->npicts; i++)
        pict_free(&v->picts[i]);
    v->nitems = v->npool = v->nhots = v->npicts = v->nshg = 0;
    v->height = v->widest = 0;
    v->focus = -1;
    v->annotated = FALSE;
}

static WORD pool_add(VIEW *v, const char FAR *s, WORD n)
{
    WORD at = v->npool;
    if (!v->pool || (DWORD)v->npool + n + 1 > v->maxpool)
        return 0xFFFF;
    m_cpy(v->pool + at, s, n);
    v->pool[at + n] = 0;
    v->npool += n + 1;
    return at;
}

static DITEM FAR *item_add(VIEW *v)
{
    DITEM FAR *d;
    if (!v->items || v->nitems >= v->maxitems)
        return NULL;
    d = &v->items[v->nitems++];
    m_set(d, 0, sizeof *d);
    d->hot = -1;
    return d;
}

/* ---- layout ----
 * A paragraph's text is gathered into "atoms" one line at a time: a
 * character with its font slot and hot spot, a tab, or a picture. When a
 * line overflows it is broken after its last space (or, failing that,
 * before the atom that overflowed) and the rest carried to the next. */

#define A_CHAR  0
#define A_TAB   1
#define A_PICT  2
#define A_NBSP  3

typedef struct {
    BYTE kind;
    BYTE c;
    BYTE font;          /* view font slot */
    short hot;
    short x, cx;        /* from the line's start */
    WORD pict;
} LATOM;

static LATOM line[LMAX];
static int nline;

typedef struct {
    VIEW *v;
    HDC dc;
    short width;
    short y;
    HLPPARA para;
    BOOL firstline;     /* the next line is a paragraph's first */
    BOOL newpara;       /* spacing above is due */
    WORD font;          /* current font slot */
    short hot;          /* current hot spot, -1 none */
    BOOL hotvisible;
} LAY;

/* per-font width tables, made on demand (keyed on the font handle, so
   flushed at each layout: handles are reused) */
static HFONT cached[8];
static int widths[8][256];
static int next;

static void char_widths_flush(void)
{
    int i;
    for (i = 0; i < 8; i++)
        cached[i] = NULL;
}

static int char_width(LAY *l, WORD slot, BYTE c)
{
    HFONT f = l->v->fonts[slot];
    int i;
    for (i = 0; i < 8; i++)
        if (cached[i] == f && f)
            return widths[i][c];
    i = next;
    next = (next + 1) % 8;
    {
        /* each character's advance: the extent of two of it less that
           of one (a bold font made from a regular one has an overhang
           that GetCharWidth counts in every character, but a line's
           extent only once: WS02, GLOSSARY.HLP's title) */
        HFONT old = SelectObject(l->dc, f);
        int k;
        for (k = 0; k < 256; k++) {
            char ch[2];
            ch[0] = ch[1] = (char)k;
            widths[i][k] = (int)LOWORD(GetTextExtent(l->dc, ch, 2)) - (int)LOWORD(GetTextExtent(l->dc, ch, 1));
        }
        SelectObject(l->dc, old);
    }
    cached[i] = f;
    return widths[i][c];
}

static short line_left(LAY *l)
{
    return (short)(MARGIN + px(l->para.left) + (l->firstline ? px(l->para.first) : 0));
}

/* the right edge: the window's less the right indent, no margin (WH2:
   WINHELP.HLP's "Help Basics" fits "...there may be one" to 401 pixels
   of a 417-pixel window with an 18 hp indent) */
static short line_right(LAY *l)
{
    return (short)(l->width - px(l->para.right));
}

static short tab_stop(LAY *l, short x)
{
    /* x and the result are from the window's left edge */
    WORD i;
    short t, base = MARGIN;
    for (i = 0; i < l->para.ntabs; i++) {
        t = base + px(l->para.tab[i]);
        if (t > x)
            return t;
    }
    /* default stops every half inch (36 pt) */
    t = base + px(72);
    while (t <= x)
        t += px(72);
    return t;
}

/* place line atoms [0, n) as a line; returns its height */
static void emit_line(LAY *l, int n, BOOL lastline)
{
    VIEW *v = l->v;
    short left = line_left(l), right = line_right(l);
    short asc = 0, desc = 0, h, w = 0, shift = 0;
    int i, j;
    /* the line's extent without trailing spaces */
    for (i = n - 1; i >= 0 && line[i].kind == A_CHAR && line[i].c == ' '; i--)
        ;
    if (i >= 0)
        w = line[i].x + line[i].cx;
    for (i = 0; i < n; i++) {
        short a, d;
        if (line[i].kind == A_PICT) {
            a = v->picts[line[i].pict].cy;
            d = 0;
        } else {
            a = v->fascent[line[i].font];
            d = v->fheight[line[i].font] - a;
        }
        if (a > asc) asc = a;
        if (d > desc) desc = d;
    }
    if (!n) {
        asc = v->fascent[l->font];
        desc = v->fheight[l->font] - asc;
    }
    h = asc + desc;
    if (l->para.lines < 0)
        h = (short)px(-l->para.lines);
    else if (l->para.lines > 0 && px(l->para.lines) > h)
        h = (short)px(l->para.lines);
    if (l->para.bits & PB_CENTER)
        shift = (right - left - w) / 2;
    else if (l->para.bits & PB_RIGHT)
        shift = right - left - w;
    if (shift < 0)
        shift = 0;
    /* runs of characters of one font and hot spot become text items */
    for (i = 0; i < n;) {
        if (line[i].kind == A_PICT) {
            DITEM FAR *d = item_add(v);
            if (d) {
                d->kind = DI_PICT;
                d->x = left + shift + line[i].x;
                d->y = l->y + asc - v->picts[line[i].pict].cy;
                d->cx = line[i].cx;
                d->cy = v->picts[line[i].pict].cy;
                d->font = line[i].pict;
                d->hot = line[i].hot;
            }
            i++;
            continue;
        }
        if (line[i].kind == A_TAB) {
            i++;
            continue;
        }
        for (j = i; j < n && (line[j].kind == A_CHAR || line[j].kind == A_NBSP) &&
                    line[j].font == line[i].font && line[j].hot == line[i].hot; j++)
            ;
        {
            char buf[LMAX];
            int k;
            DITEM FAR *d;
            for (k = i; k < j; k++)
                buf[k - i] = line[k].kind == A_NBSP ? ' ' : (char)line[k].c;
            d = item_add(v);
            if (d) {
                d->kind = DI_TEXT;
                d->x = left + shift + line[i].x;
                d->y = l->y + asc - v->fascent[line[i].font];
                d->cx = line[j - 1].x + line[j - 1].cx - line[i].x;
                d->cy = v->fheight[line[i].font];
                d->ascent = v->fascent[line[i].font];
                d->font = line[i].font;
                d->text = pool_add(v, buf, (WORD)(j - i));
                d->len = (WORD)(j - i);
                d->hot = line[i].hot;
                if (d->text == 0xFFFF)
                    v->nitems--;
            }
        }
        i = j;
    }
    if (left + shift + w > v->widest)
        v->widest = left + shift + w;
    l->y += h;
    (void)lastline;
}

/* add an atom to the line, breaking it when it overflows */
static void add_atom(LAY *l, LATOM *a)
{
    short left, right, x;
    for (;;) {
        left = line_left(l);
        right = line_right(l);
        x = nline ? line[nline - 1].x + line[nline - 1].cx : 0;
        if (a->kind == A_TAB) {
            a->cx = tab_stop(l, left + x) - (left + x);
            if (a->cx < 0)
                a->cx = 0;
        }
        a->x = x;
        /* a word fits when it and the space after it do (WS02: "...there
           may be one " fits 405 pixels, "...the Help window, just "
           does not) */
        if (left + x + a->cx <= right || nline == 0 || (l->para.bits & PB_NOWRAP)) {
            if (nline < LMAX)
                line[nline++] = *a;
            return;
        }
        /* overflow: break after the last space */
        {
            int b, i, k;
            BOOL space = a->kind == A_CHAR && a->c == ' ';
            for (b = nline - 1; b >= 0; b--)
                if (line[b].kind == A_CHAR && line[b].c == ' ')
                    break;
            if (space && (b < 0 || b == nline - 1)) {
                /* a word longer than the line, or spaces after spaces */
                if (nline < LMAX)
                    line[nline++] = *a;
                return;
            }
            if (b < 0)
                b = nline - 1;  /* no space: break before this atom */
            emit_line(l, b + 1, FALSE);
            l->firstline = FALSE;
            /* carry the rest */
            k = 0;
            for (i = b + 1; i < nline; i++)
                line[k++] = line[i];
            nline = 0;
            for (i = 0; i < k; i++) {
                LATOM t = line[i];
                short tx = nline ? line[nline - 1].x + line[nline - 1].cx : 0;
                if (t.kind == A_TAB) {
                    t.cx = tab_stop(l, line_left(l) + tx) - (line_left(l) + tx);
                    if (t.cx < 0)
                        t.cx = 0;
                }
                t.x = tx;
                line[nline++] = t;
            }
        }
    }
}

static void flush_line(LAY *l, BOOL parend)
{
    emit_line(l, nline, TRUE);
    nline = 0;
    l->firstline = parend;
}

static void para_start(LAY *l)
{
    if (l->newpara) {
        l->y += (short)px(l->para.above);
        l->newpara = FALSE;
    }
}

static void para_end(LAY *l)
{
    VIEW *v = l->v;
    /* borders: w16 draws the bottom and top ones as lines across the
       paragraph's indents (box: all four) */
    l->y += (short)px(l->para.below);
    l->newpara = TRUE;
    (void)v;
}

static short hot_add(VIEW *v, BYTE code, DWORD hash, LPCSTR file, LPCSTR window, LPCSTR macro, WORD mlen)
{
    HOTSPOT FAR *h;
    if (!v->hots || v->nhots >= v->maxhots)
        return -1;
    h = &v->hots[v->nhots];
    h->code = code;
    h->hash = hash;
    h->str = 0xFFFF;
    if (macro) {
        h->str = pool_add(v, macro, mlen);
    } else if (file || window) {
        char buf[200];
        int n = 0;
        if (file)
            for (; *file && n < 150; file++)
                buf[n++] = *file;
        if (window) {
            buf[n++] = '>';
            for (; *window && n < 198; window++)
                buf[n++] = *window;
        }
        h->str = pool_add(v, buf, (WORD)n);
    }
    return (short)v->nhots++;
}

/* a picture's hot spot (pict.c): str is a macro (C8h, CCh) or
   "file>window" */
short view_addhot(VIEW *v, BYTE code, DWORD hash, LPCSTR str)
{
    if (code == 0xC8 || code == 0xCC)
        return hot_add(v, code, 0, NULL, NULL, str, hlp_strlen(str));
    if (str) {
        HOTSPOT FAR *h;
        short i = hot_add(v, code, hash, NULL, NULL, NULL, 0);
        if (i >= 0) {
            h = &v->hots[i];
            h->str = pool_add(v, str, hlp_strlen(str));
        }
        return i;
    }
    return hot_add(v, code, hash, NULL, NULL, NULL, 0);
}

static void border_line(LAY *l, short y, short thick)
{
    DITEM FAR *d = item_add(l->v);
    if (!d)
        return;
    d->kind = DI_LINE;
    d->x = (short)(MARGIN + px(l->para.left));
    d->cx = (short)(line_right(l) - d->x);
    d->y = y;
    d->cy = thick;
}

static void layout_link(LAY *l, HLP FAR *h, DWORD pos, HLPLINK FAR *lk)
{
    HLPDATA d;
    HLPITER it;
    VIEW *v = l->v;
    int k;
    short ptop = 0;
    BOOL started = FALSE;
    if (!hlp_linkdata(h, pos, lk, &d))
        return;
    if (!para_begin(&it, lk->type, d.d1, d.n1, d.d2, d.n2)) {
        hio_free(d.mem);
        return;
    }
    while ((k = para_next(&it)) != PI_END) {
        LATOM a;
        a.hot = l->hotvisible || l->hot >= 0 ? l->hot : -1;
        a.font = (BYTE)l->font;
        a.pict = 0;
        switch (k) {
        case PI_PARA:
            l->para = it.para;
            if (it.type == 0x23 && it.para.column < it.table.ncols) {
                /* tables: w16 lays cells out one under the other, at
                   the column's offset (UNVERIFIED against 3.1) */
                WORD c;
                short off = 0;
                for (c = 0; c <= (WORD)it.para.column; c++)
                    off += it.table.gap[c] + (c ? it.table.width[c - 1] : 0);
                if (it.table.tabletype == 1 || it.table.tabletype == 3)
                    l->para.left += off;
            }
            l->newpara = TRUE;
            l->firstline = TRUE;
            started = FALSE;
            break;
        case PI_TEXT: {
            WORD i;
            if (!started) {
                para_start(l);
                ptop = l->y;
                if (l->para.border & 3)
                    border_line(l, l->y, l->para.border & 0x20 ? 2 : 1);
                started = TRUE;
            }
            for (i = 0; i < it.len; i++) {
                BYTE c = (BYTE)it.text[i];
                a.kind = A_CHAR;
                a.c = c;
                a.cx = (short)char_width(l, l->font, c);
                add_atom(l, &a);
            }
            break;
        }
        case PI_FONT:
            l->font = font_slot(v, l->dc, it.num);
            break;
        case PI_BREAK:
            if (!started) {
                para_start(l);
                ptop = l->y;
                started = TRUE;
            }
            flush_line(l, FALSE);
            break;
        case PI_PAREND:
            if (!started) {
                para_start(l);
                ptop = l->y;
            }
            flush_line(l, TRUE);
            if (l->para.border & 9)
                border_line(l, l->y, l->para.border & 0x20 ? 2 : 1);
            para_end(l);
            started = FALSE;
            (void)ptop;
            break;
        case PI_TAB:
            if (!started) {
                para_start(l);
                ptop = l->y;
                started = TRUE;
            }
            a.kind = A_TAB;
            a.c = '\t';
            add_atom(l, &a);
            break;
        case PI_NBSP:
            a.kind = A_NBSP;
            a.c = ' ';
            a.cx = (short)char_width(l, l->font, ' ');
            add_atom(l, &a);
            break;
        case PI_NBHYPH:
            break;              /* the hyphen itself is in the text */
        case PI_PICT:
            if (!started) {
                para_start(l);
                ptop = l->y;
                started = TRUE;
            }
            if (v->npicts < v->maxpicts && (it.pictype == 3 || it.pictype == 0x22)) {
                VPICT FAR *p = &v->picts[v->npicts];
                m_set(p, 0, sizeof *p);
                if (pict_load(h, it.data, it.dlen, FALSE, p, v)) {
                    a.kind = A_PICT;
                    a.pict = v->npicts++;
                    a.cx = p->cx;
                    add_atom(l, &a);
                }
            }
            break;
        case PI_JUMP:
            l->hot = hot_add(v, it.code, it.off, it.file, it.window, NULL, 0);
            l->hotvisible = TRUE;
            break;
        case PI_MACRO:
            l->hot = hot_add(v, it.code, 0, NULL, NULL, it.text, it.len);
            l->hotvisible = TRUE;
            break;
        case PI_HOTEND:
            l->hot = -1;
            break;
        case PI_CELLEND:
            if (nline)
                flush_line(l, TRUE);
            break;
        }
    }
    if (nline)
        flush_line(l, TRUE);
    hio_free(d.mem);
}

void view_layout(VIEW *v, HLP FAR *h, DWORD first, DWORD end, short width)
{
    LAY l;
    DWORD pos;
    HLPLINK lk;
    WORD guard = 0;
    if (v->hlp != h)
        fonts_free(v);
    view_clear(v);
    v->hlp = h;
    v->first = first;
    v->end = end;
    v->width = width;
    if (!h || !first)
        return;
    m_set(&l, 0, sizeof l);
    l.v = v;
    l.dc = GetDC(v->hwnd);
    l.width = width;
    l.y = 0;
    l.hot = -1;
    l.newpara = TRUE;
    l.firstline = TRUE;
    l.font = font_slot(v, l.dc, 0);
    nline = 0;
    char_widths_flush();
    for (pos = first; pos && pos != end && hlp_link(h, pos, &lk); pos = hlp_nextlink(h, pos, &lk)) {
        if (lk.type == 2 && pos != first)
            break;
        if (lk.type == 0x20 || lk.type == 0x23 || lk.type == 1)
            layout_link(&l, h, pos, &lk);
        if (++guard > 4000)
            break;
    }
    v->height = l.y;
    ReleaseDC(v->hwnd, l.dc);
}

/* ---- painting ---- */

/* a popup's dotted underline: two pixels on, two off, from the text's
   start (WH2) */
static void dotted(HDC dc, short x0, short x1, short y, COLORREF c)
{
    short x;
    for (x = x0; x < x1; x++)
        if (!((x - x0) & 2))
            SetPixel(dc, x, y, c);
}

void view_paint(VIEW *v, HDC dc, RECT *clip, short dx, short dy)
{
    WORD i;
    HFONT old = NULL;
    SetBkMode(dc, TRANSPARENT);
    for (i = 0; i < v->nitems; i++) {
        DITEM FAR *d = &v->items[i];
        short x = d->x + dx, y = d->y + dy;
        BYTE code = 0;
        BOOL vis = FALSE;
        if (y >= clip->bottom || y + d->cy < clip->top)
            continue;
        if (d->hot >= 0 && (WORD)d->hot < v->nhots) {
            code = v->hots[d->hot].code;
            /* "without font change" hot spots: E6, E7, EB, EF, CC */
            vis = !(code == 0xE6 || code == 0xE7 || code == 0xEB || code == 0xEF || code == 0xCC);
        }
        switch (d->kind) {
        case DI_TEXT: {
            HFONT f = v->fonts[d->font];
            HFONT o = SelectObject(dc, f);
            if (!old)
                old = o;
            SetTextColor(dc, vis ? GREEN : font_color(v, d->font));
            TextOut(dc, x, y, v->pool + d->text, d->len);
            if (vis) {
                short uy = y + d->ascent + 1;
                if (code == 0xE2 || code == 0xEA || code == 0xE0) {
                    dotted(dc, x, x + d->cx, uy, GREEN);
                } else {
                    HBRUSH b = CreateSolidBrush(GREEN);
                    RECT r;
                    SetRect(&r, x, uy, x + d->cx, uy + 1);
                    FillRect(dc, &r, b);
                    DeleteObject(b);
                }
            }
            break;
        }
        case DI_PICT:
            if (d->font < v->npicts)
                pict_draw(dc, &v->picts[d->font], x, y);
            break;
        case DI_LINE:
            PatBlt(dc, x, y, d->cx, d->cy, BLACKNESS);
            break;
        }
        if (d->hot >= 0 && d->hot == v->focus) {
            RECT r;
            SetRect(&r, x, y, x + d->cx, y + d->cy);
            InvertRect(dc, &r);
        }
    }
    if (old)
        SelectObject(dc, old);
}

short view_hit(VIEW *v, short x, short y)
{
    WORD i;
    for (i = 0; i < v->nitems; i++) {
        DITEM FAR *d = &v->items[i];
        if (x < d->x || x >= d->x + d->cx || y < d->y || y >= d->y + d->cy)
            continue;
        if (d->kind == DI_PICT && d->font < v->npicts) {
            VPICT FAR *p = &v->picts[d->font];
            WORD k;
            for (k = 0; k < p->nhot; k++) {
                SHGHOT FAR *g = &v->shg[p->hotfirst + k];
                short rx = x - d->x, ry = y - d->y;
                if (rx >= g->x && rx < g->x + g->cx && ry >= g->y && ry < g->y + g->cy)
                    return g->hot;
            }
        }
        if (d->hot >= 0)
            return d->hot;
    }
    return -1;
}

/* the visible text of the view (Copy, Annotate's Copy): lines joined
   with CR LF; a block from hio_alloc */
LPSTR view_text(VIEW *v, DWORD FAR *len)
{
    WORD i;
    DWORD n = 0;
    LPSTR s = (LPSTR)hio_alloc((DWORD)v->npool + (DWORD)v->nitems * 3 + 4);
    short lasty = -32000;
    if (!s)
        return NULL;
    for (i = 0; i < v->nitems; i++) {
        DITEM FAR *d = &v->items[i];
        if (d->kind != DI_TEXT)
            continue;
        if (n && d->y + d->ascent != lasty) {
            /* drop the space a wrapped line ended with */
            while (n && s[n - 1] == ' ')
                n--;
            s[n++] = '\r';
            s[n++] = '\n';
        } else if (n && i && v->items[i - 1].kind == DI_TEXT &&
                   d->x > v->items[i - 1].x + v->items[i - 1].cx + 2) {
            s[n++] = '\t';
        }
        lasty = d->y + d->ascent;
        m_cpy(s + n, v->pool + d->text, d->len);
        n += d->len;
    }
    while (n && s[n - 1] == ' ')
        n--;
    s[n] = 0;
    *len = n;
    return s;
}

/* ---- scrolling ---- */

static short client_h(VIEW *v)
{
    RECT r;
    GetClientRect(v->hwnd, &r);
    return (short)r.bottom;
}

void view_setscroll(VIEW *v)
{
    short ch = client_h(v), max = v->height - ch;
    if (v->nsr || v->popup)
        return;
    if (max <= 0) {
        v->scroll = 0;
        SetScrollRange(v->hwnd, SB_VERT, 0, 0, TRUE);
    } else {
        if (v->scroll > max)
            v->scroll = max;
        if (v->scroll < 0)
            v->scroll = 0;
        SetScrollRange(v->hwnd, SB_VERT, 0, max, FALSE);
        SetScrollPos(v->hwnd, SB_VERT, v->scroll, TRUE);
    }
}

static void scroll_to(VIEW *v, short pos)
{
    short ch = client_h(v), max = v->height - ch;
    if (max < 0)
        max = 0;
    if (pos > max)
        pos = max;
    if (pos < 0)
        pos = 0;
    if (pos == v->scroll)
        return;
    ScrollWindow(v->hwnd, 0, v->scroll - pos, NULL, NULL);
    v->scroll = pos;
    SetScrollPos(v->hwnd, SB_VERT, pos, TRUE);
    UpdateWindow(v->hwnd);
}

/* relayout for the window's width; a scroll bar appears when the topic
   is taller than the window (and the topic is laid out again for the
   narrower width) */
static void view_size(VIEW *v)
{
    RECT r;
    GetClientRect(v->hwnd, &r);
    if (!v->hlp || v->popup)
        return;
    if (r.right != v->width)
        view_layout(v, v->hlp, v->first, v->end, (short)r.right);
    view_setscroll(v);
}

/* ---- hot spots ---- */

HWND g_popsrc;      /* the window whose hot spot opened a popup */

void view_activate(VIEW *v, short hot, POINT pt)
{
    HOTSPOT FAR *h;
    char str[200];
    char file[PATHMAX], window[16];
    LPSTR gt;
    if (hot < 0 || (WORD)hot >= v->nhots)
        return;
    h = &v->hots[hot];
    if (!v->popup)
        g_popsrc = v->hwnd;
    str[0] = 0;
    if (h->str != 0xFFFF)
        hlp_strcpyn(str, v->pool + h->str, sizeof str);
    switch (h->code) {
    case 0xC8:
    case 0xCC:
        popup_close();
        run_macros(str);
        break;
    case 0xE0: case 0xE2: case 0xE6:
        jump_hash(v->hlp, NULL, h->hash, NULL, TRUE, pt);
        break;
    case 0xE1: case 0xE3: case 0xE7:
        popup_close();
        jump_hash(v->hlp, NULL, h->hash, NULL, FALSE, pt);
        break;
    case 0xEA: case 0xEB: case 0xEE: case 0xEF:
        /* "file>window" */
        file[0] = window[0] = 0;
        gt = str;
        while (*gt && *gt != '>')
            gt++;
        if (*gt == '>') {
            *gt = 0;
            hlp_strcpyn(window, gt + 1, sizeof window);
        }
        hlp_strcpyn(file, str, sizeof file);
        if (h->code == 0xEA || h->code == 0xEB) {
            jump_hash(v->hlp, file[0] ? file : NULL, h->hash, NULL, TRUE, pt);
        } else {
            popup_close();
            jump_hash(v->hlp, file[0] ? file : NULL, h->hash, window[0] ? window : NULL, FALSE, pt);
        }
        break;
    }
}

/* ---- the windows ---- */

static VIEW *view_of(HWND w)
{
    return (VIEW *)GetWindowWord(w, 0);
}

LRESULT view_proc(VIEW *v, HWND w, UINT msg, WPARAM wp, LPARAM lp)
{
    switch (msg) {
    case WM_ERASEBKGND: {
        RECT r;
        HBRUSH b = CreateSolidBrush(v ? v->bg : GetSysColor(COLOR_WINDOW));
        GetClientRect(w, &r);
        FillRect((HDC)wp, &r, b);
        DeleteObject(b);
        return 1;
    }
    case WM_PAINT: {
        PAINTSTRUCT ps;
        BeginPaint(w, &ps);
        if (v)
            view_paint(v, ps.hdc, &ps.rcPaint, 0, (short)-v->scroll);
        EndPaint(w, &ps);
        return 0;
    }
    case WM_SIZE:
        if (v)
            view_size(v);
        return 0;
    case WM_VSCROLL: {
        short ch = client_h(v), lineh = 16;
        if (!v)
            break;
        switch (wp) {
        case SB_LINEUP: scroll_to(v, v->scroll - lineh); break;
        case SB_LINEDOWN: scroll_to(v, v->scroll + lineh); break;
        case SB_PAGEUP: scroll_to(v, v->scroll - ch + lineh); break;
        case SB_PAGEDOWN: scroll_to(v, v->scroll + ch - lineh); break;
        case SB_THUMBTRACK:
        case SB_THUMBPOSITION: scroll_to(v, (short)LOWORD(lp)); break;
        case SB_TOP: scroll_to(v, 0); break;
        case SB_BOTTOM: scroll_to(v, v->height); break;
        }
        return 0;
    }
    case WM_KEYDOWN:
        if (!v)
            break;
        switch (wp) {
        case VK_UP: SendMessage(w, WM_VSCROLL, SB_LINEUP, 0); return 0;
        case VK_DOWN: SendMessage(w, WM_VSCROLL, SB_LINEDOWN, 0); return 0;
        case VK_PRIOR: SendMessage(w, WM_VSCROLL, SB_PAGEUP, 0); return 0;
        case VK_NEXT: SendMessage(w, WM_VSCROLL, SB_PAGEDOWN, 0); return 0;
        case VK_HOME:
            if (GetKeyState(VK_CONTROL) < 0)
                SendMessage(w, WM_VSCROLL, SB_TOP, 0);
            return 0;
        case VK_END:
            if (GetKeyState(VK_CONTROL) < 0)
                SendMessage(w, WM_VSCROLL, SB_BOTTOM, 0);
            return 0;
        case VK_TAB:
            if (v->nhots) {
                short n = (short)v->nhots;
                if (GetKeyState(VK_SHIFT) < 0)
                    v->focus = v->focus <= 0 ? n - 1 : v->focus - 1;
                else
                    v->focus = v->focus + 1 >= n ? 0 : v->focus + 1;
                InvalidateRect(w, NULL, TRUE);
            }
            return 0;
        case VK_RETURN:
            if (v->focus >= 0) {
                POINT pt;
                WORD i;
                pt.x = pt.y = 0;
                for (i = 0; i < v->nitems; i++)
                    if (v->items[i].hot == v->focus) {
                        pt.x = v->items[i].x + v->items[i].cx / 2;
                        pt.y = v->items[i].y + v->items[i].cy - v->scroll;
                        break;
                    }
                ClientToScreen(w, &pt);
                view_activate(v, v->focus, pt);
            }
            return 0;
        }
        break;
    case WM_SETCURSOR:
        if (v && LOWORD(lp) == HTCLIENT) {
            POINT pt;
            GetCursorPos(&pt);
            ScreenToClient(w, &pt);
            if (view_hit(v, (short)pt.x, (short)(pt.y + v->scroll)) >= 0) {
                SetCursor(g_hand);
                return TRUE;
            }
        }
        break;
    case WM_LBUTTONDOWN:
        if (v && !v->popup) {
            short hot = view_hit(v, (short)LOWORD(lp), (short)(HIWORD(lp) + v->scroll));
            if (hot >= 0) {
                POINT pt;
                pt.x = (short)LOWORD(lp);
                pt.y = (short)HIWORD(lp);
                ClientToScreen(w, &pt);
                view_activate(v, hot, pt);
            }
            return 0;
        }
        break;
    }
    return DefWindowProc(w, msg, wp, lp);
}

LRESULT CALLBACK __export TopicProc(HWND w, UINT msg, WPARAM wp, LPARAM lp)
{
    if (msg == WM_CREATE) {
        CREATESTRUCT FAR *cs = (CREATESTRUCT FAR *)lp;
        VIEW *v = (VIEW *)LOWORD(cs->lpCreateParams);
        SetWindowWord(w, 0, (WORD)v);
        if (v)
            v->hwnd = w;
        return 0;
    }
    return view_proc(view_of(w), w, msg, wp, lp);
}

/* ---- popups ---- */

#define SHADOW 12

void popup_close(void)
{
    if (g_note) {
        HWND n = g_note;
        g_note = NULL;
        ReleaseCapture();
        DestroyWindow(n);
    }
}

void popup_show(HLP FAR *h, int topic, POINT pt)
{
    HLPTHDR t;
    short w, cw, ch, x, y, sw, sh;
    DWORD first;
    HWND owner;
    popup_close();
    if (!hlp_topichdr(h, topic, &t, NULL, 0, NULL, 0))
        return;
    sw = (short)GetSystemMetrics(SM_CXSCREEN);
    sh = (short)GetSystemMetrics(SM_CYSCREEN);
    /* laid out as wide as the client area of the window the hot spot is
       in less 4 (its frame; WS01, WS02: 413 pixels from the main window,
       238 from the glossary's, scroll bar included); owned by it, so that it stays in front of the main
       window */
    {
        RECT r;
        HWND src = g_popsrc && IsWindow(g_popsrc) ? g_popsrc : g_topic;
        while (GetParent(src))
            src = GetParent(src);
        owner = src;
        GetClientRect(src, &r);
        w = (short)r.right - 4;
    }
    first = t.nonscroll != 0xFFFFFFFFUL ? t.nonscroll : t.scroll;
    g_vnote.hwnd = g_frame;
    view_init(&g_vnote, g_frame, FALSE, TRUE);
    g_vnote.bg = GetSysColor(COLOR_WINDOW);
    view_layout(&g_vnote, h, first, 0, w);
    /* the box: the text, 8 pixels' margin on the right and one more,
       one pixel above it and five below, a 2-pixel frame (WH2) */
    cw = g_vnote.widest + MARGIN + 1 + 2 * 2;
    ch = g_vnote.height + 1 + 5 + 2 * 2;
    if (cw > w)
        cw = w;
    x = (short)pt.x - (cw + SHADOW) / 2;
    y = (short)pt.y + 10;
    /* kept 2 pixels in from the screen's edges (WS02) */
    if (x + cw + SHADOW > sw - 2)
        x = sw - 2 - cw - SHADOW;
    if (x < 2)
        x = 2;
    if (y + ch + SHADOW > sh)
        y = (short)pt.y - ch - SHADOW - 10;
    if (y < 0)
        y = 0;
    g_note = CreateWindowEx(WS_EX_TOPMOST, "MS_WINNOTE", "", WS_POPUP | WS_CLIPSIBLINGS,
                            x, y, cw + SHADOW, ch + SHADOW, owner, NULL, g_inst, NULL);
    if (!g_note)
        return;
    g_vnote.hwnd = g_note;
    /* 3.1's popup takes the activation (the main window's caption goes
       inactive, WH2) */
    ShowWindow(g_note, SW_SHOW);
    SetActiveWindow(g_note);
    UpdateWindow(g_note);
    SetCapture(g_note);
}

LRESULT CALLBACK __export NoteProc(HWND w, UINT msg, WPARAM wp, LPARAM lp)
{
    switch (msg) {
    case WM_PAINT: {
        PAINTSTRUCT ps;
        RECT r, b;
        HBRUSH br;
        BeginPaint(w, &ps);
        GetClientRect(w, &r);
        /* the box: a 2-pixel black frame round the text */
        SetRect(&b, 0, 0, r.right - SHADOW, r.bottom - SHADOW);
        PatBlt(ps.hdc, 0, 0, b.right, 2, BLACKNESS);
        PatBlt(ps.hdc, 0, b.bottom - 2, b.right, 2, BLACKNESS);
        PatBlt(ps.hdc, 0, 0, 2, b.bottom, BLACKNESS);
        PatBlt(ps.hdc, b.right - 2, 0, 2, b.bottom, BLACKNESS);
        br = CreateSolidBrush(g_vnote.bg);
        SetRect(&b, 2, 2, r.right - SHADOW - 2, r.bottom - SHADOW - 2);
        FillRect(ps.hdc, &b, br);
        DeleteObject(br);
        /* the shadow: every other pixel black (those whose screen x + y
           is odd) over what was on the screen before (the window does
           not erase its background) */
        {
            POINT o;
            int x, y;
            o.x = o.y = 0;
            ClientToScreen(w, &o);
            for (y = SHADOW; y < r.bottom; y++)
                for (x = (y >= r.bottom - SHADOW) ? SHADOW : r.right - SHADOW; x < r.right; x++)
                    if ((o.x + x + o.y + y) & 1)
                        SetPixel(ps.hdc, x, y, RGB(0, 0, 0));
        }
        SetRect(&b, 2, 2, r.right - SHADOW - 2, r.bottom - SHADOW - 2);
        IntersectClipRect(ps.hdc, b.left, b.top, b.right, b.bottom);
        view_paint(&g_vnote, ps.hdc, &b, 3, 3);
        EndPaint(w, &ps);
        return 0;
    }
    case WM_ERASEBKGND:
        return 1;
    case WM_LBUTTONDOWN: {
        /* a hot spot in the popup works; anywhere else closes it */
        short hot = view_hit(&g_vnote, (short)LOWORD(lp) - 3, (short)HIWORD(lp) - 3);
        POINT pt;
        pt.x = (short)LOWORD(lp);
        pt.y = (short)HIWORD(lp);
        ClientToScreen(w, &pt);
        if (hot >= 0) {
            VIEW tmp = g_vnote;
            view_activate(&tmp, hot, pt);
            return 0;
        }
        popup_close();
        return 0;
    }
    case WM_RBUTTONDOWN:
    case WM_KEYDOWN:
    case WM_SYSKEYDOWN:
    case WM_CHAR:
        popup_close();
        return 0;
    case WM_SETCURSOR:
        SetCursor(LoadCursor(NULL, IDC_ARROW));
        return TRUE;
    case WM_DESTROY:
        if (g_note == w)
            g_note = NULL;
        return 0;
    }
    return DefWindowProc(w, msg, wp, lp);
}
