/*
 * view.c - w16 Write: the document window. Lines, painting, the caret,
 * the selection, scrolling, mouse and keyboard.
 *
 * Observed on 3.1 (WR1-WR4 screenshots): text for left indent 0 starts
 * XP0 pixels into the document window, the first line YP0 below its top;
 * the caret is Write's own (the original imports no caret calls), a
 * 2-pixel bar the height of the line, blinking at the caret blink rate;
 * the end of the document shows the end mark (character A4h in the last
 * text's font) after the last character.
 */
#include "write.h"

#define XP0         20                  /* calibrated, WR1 */
#define YP0         4
#define CARET_W     2
#define TIMER_CARET 1
#define TIMER_TYPED 2                   /* the caret goes off a moment after typing */
#define TYPED_MS    450                 /* UNVERIFIED: more than 200, less than 700 (WR23, WR24, WR25 with SNAP's pacing) */

HWND g_docwnd;
DWORD g_selFirst, g_selLim;
ATOM32 g_insChp;
BOOL g_insChpValid;

static DWORD cpTop;                     /* first cp of the top line */
static int ypTop;                       /* its band, in a picture */
static DWORD selAnchor;                 /* the fixed end while extending */
static BOOL caretOn, caretShown, caretDefer;
static BOOL typing;                     /* view_char at work */
static BOOL lastTyped;                  /* the last edit was typing */
static BOOL menuShown, deferBeforeMenu; /* a menu showed the caret */
static int caretX, caretY, caretH;
static int dpiX = 96, dpiY = 96;
static int xpScroll;                    /* horizontal scroll, pixels */
static BOOL dragging;

/* ---- fonts: a growable cache keyed by the decoded CHP ---- */
typedef struct {
    WORD ftc;
    BYTE hps, bold, italic, underline;
    HFONT h;
    int ascent, descent, height, overhang;  /* ascent includes the external leading */
    int tmAscent;
    int width[256];
} FONTC;

/* each entry its own fixed local block (never moves); the table of
   pointers grows as needed */
static FONTC **fonttab;
static int nfonts;

static FONTC *font_for(const CHPX *c)
{
    FONTC *f, **t;
    int i;
    LOGFONT lf;
    HDC dc;
    TEXTMETRIC tm;
    FFN FAR *ffn;
    for (i = 0; i < nfonts; i++) {
        f = fonttab[i];
        if (f->ftc == c->ftc && f->hps == c->hps && f->bold == c->bold &&
            f->italic == c->italic && f->underline == c->underline)
            return f;
    }
    f = (FONTC NEAR *)LocalAlloc(LMEM_FIXED | LMEM_ZEROINIT, sizeof(FONTC));
    if (!f)
        return nfonts ? fonttab[0] : NULL;
    t = (FONTC **)(fonttab ? LocalReAlloc((HLOCAL)fonttab, (nfonts + 1) * sizeof(FONTC *), LMEM_MOVEABLE)
                           : LocalAlloc(LMEM_FIXED, sizeof(FONTC *)));
    if (!t) {
        LocalFree((HLOCAL)f);
        return nfonts ? fonttab[0] : NULL;
    }
    fonttab = t;
    fonttab[nfonts++] = f;
    f->ftc = c->ftc;
    f->hps = c->hps;
    f->bold = c->bold;
    f->italic = c->italic;
    f->underline = c->underline;
    m_set(&lf, 0, sizeof lf);
    lf.lfHeight = -MulDiv(c->hps, dpiY, 144);
    lf.lfWeight = c->bold ? FW_BOLD : FW_NORMAL;
    lf.lfItalic = c->italic;
    lf.lfUnderline = c->underline;
    ffn = font_get(&g_doc, c->ftc);
    lf.lfPitchAndFamily = ffn->ffid;
    s_cpy(lf.lfFaceName, ffn->name, LF_FACESIZE);
    f->h = CreateFontIndirect(&lf);
    dc = GetDC(g_docwnd);
    SelectObject(dc, f->h);
    GetTextMetrics(dc, &tm);
    GetCharWidth(dc, 0, 255, f->width);
    SelectObject(dc, GetStockObject(SYSTEM_FONT));
    ReleaseDC(g_docwnd, dc);
    /* the external leading goes above the text (WR8: 12-point Arial,
       ascent 15 + leading 1) */
    f->ascent = tm.tmAscent + tm.tmExternalLeading;
    f->tmAscent = tm.tmAscent;
    f->descent = tm.tmDescent;
    f->height = tm.tmHeight + tm.tmExternalLeading;
    f->overhang = tm.tmOverhang;
    return f;
}

static void fonts_free(void)
{
    int i;
    for (i = 0; i < nfonts; i++) {
        DeleteObject(fonttab[i]->h);
        LocalFree((HLOCAL)fonttab[i]);
    }
    if (fonttab)
        LocalFree((HLOCAL)fonttab);
    fonttab = NULL;
    nfonts = 0;
}

/* the raise of super- and subscripts: 4 pixels for hpsPos 4 at 10 and
   at 24 points (WR8). UNVERIFIED: the formula; a pixel per half point
   at 96 dpi fits what was seen */
static int pos_px(int hpsPos)
{
    return MulDiv(hpsPos, dpiY, 96);
}

/* the width of "(page)", how a page number shows on screen */
static int page_w(const FONTC *f)
{
    static const char s[] = "(page)";
    int i, w = 0;
    if (!f)
        return 48;
    for (i = 0; s[i]; i++)
        w += f->width[(BYTE)s[i]];
    return w;
}

static int tw2px(long tw)
{
    return (int)((tw * dpiX + (tw >= 0 ? 720 : -720)) / 1440);
}

/* ---- line layout ---- */

typedef struct {
    DWORD cpFirst, cpLim;               /* cpLim includes a CR LF */
    DWORD para;
    int xLeft;                          /* first character, doc pixels */
    int width;                          /* drawn width */
    int height, ascent;
    int nspaces, extra;                 /* justification */
    BOOL endMark;                       /* the document's end is here */
    BOOL paraEnd;                       /* last line of its paragraph */
    BOOL picture;                       /* a picture paragraph */
    int yp, picH;                       /* a picture's band: its first row, the picture's height */
    BOOL pageBreak;                     /* a page break (character 12) alone */
    DWORD tabCp;                        /* after the line's last tab, 0: none */
    int tabShift;                       /* what comes after it moves by this */
    int space;                          /* 1 1/2, double spacing: rows above the text */
} LINE;

/* Tall pictures are laid out in bands, lines of BAND pixels (the last one
   the rest), so that the window can start or end inside a picture: the
   arrows scroll by a band (WR17: 16 pixels a click), and a picture's
   lower part shows at the window's top (WR10, WR16, WR17).
   UNVERIFIED: whether 16 follows a font's height. */
#define BAND        16

static SEPX sep;

static void props_at(DWORD cp, CHPX *c)
{
    DWORD f, l;
    ATOM32 a;
    run_at(&g_doc, cp, &f, &l, &a);
    chp_decode(&g_doc, a, c);
}

/* the width of one character at x (tabs need the position) */
static int tab_stop(const PAPX *p, int x, int xIndent0)
{
    int i, t;
    for (i = 0; i < p->ntabs; i++) {
        t = xIndent0 + tw2px(p->tab[i].dxa);
        if (t > x)
            return t;
    }
    /* default stops every half inch from the margin (UNVERIFIED) */
    t = tw2px(720);
    return xIndent0 + ((x - xIndent0) / t + 1) * t;
}

/* Lay out the line that starts at cp (a line start). */
static void layout_line(DWORD cp, LINE *ln)
{
    DWORD pf, pl, pi, c, brk = 0, lim;
    ATOM32 pa;
    PAPX pap;
    int x, xStart, xMax, xBrk = 0, spaces = 0, spacesBrk = 0;
    int asc = 0, desc = 0, ht = 0;
    CHPX chp;
    FONTC *f = NULL;
    DWORD runLim = 0, runFirst, tabCp = 0;
    ATOM32 ra;
    BOOL haveBrk = FALSE;
    int xTab = 0;
    pi = para_at(&g_doc, cp, &pf, &pl, &pa);
    pap_decode(&g_doc, pa, &pap);
    ln->cpFirst = cp;
    ln->para = pi;
    ln->picture = FALSE;
    ln->yp = ln->picH = 0;
    ln->pageBreak = FALSE;
    ln->tabCp = 0;
    ln->tabShift = 0;
    ln->space = 0;
    if ((pap.rhc & 6) && !hf_mode()) {
        /* a header or footer paragraph (SDK ch. 8, 8.3.1: rhc bits 1-2;
           they come before the body, 8.3.2): not shown in the document
           (UNVERIFIED: shown only in the Header/Footer views) */
        ln->cpFirst = cp;
        ln->cpLim = pl;
        ln->paraEnd = TRUE;
        ln->endMark = FALSE;
        ln->xLeft = XP0;
        ln->width = ln->height = ln->ascent = 0;
        ln->nspaces = ln->extra = 0;
        return;
    }
    if (pap.fGraphics && pl <= g_doc.cpMac) {
        /* a picture: the whole paragraph, one line its height
           (UNVERIFIED: the original splits tall pictures into bands) */
        PICT pic;
        pict_read(pf, pl, &pic);
        ln->cpFirst = pf;
        ln->cpLim = pl;
        ln->picture = TRUE;
        ln->paraEnd = TRUE;
        ln->endMark = FALSE;
        ln->xLeft = XP0 + tw2px((long)pap.dxaLeft + pic.dxaOffset);
        /* sizes rounded to pixels; pictures start a pixel left of where
           text does (WR7: 608 x 295 twips show as 41 x 20 at x 27, text
           at 28; UNVERIFIED: why) */
        ln->xLeft--;
        ln->width = tw2px(MulDiv(pic.dxaSize, pic.mx, 1000));
        ln->picH = MulDiv(MulDiv(pic.dyaSize, pic.my, 1000), dpiY, 1440);
        ln->yp = 0;
        ln->height = ln->ascent = ln->picH < BAND ? ln->picH : BAND;
        ln->nspaces = ln->extra = 0;
        return;
    }
    if (pap.rhc & 6) {
        /* a running head's indents count from the page's edges: the
           margins are taken off (WR9: indents 1800 show at indent 0) */
        pap.dxaLeft -= sep.xaLeft;
        pap.dxaRight -= sep.xaMac - sep.xaLeft - sep.dxaText;
    }
    xStart = XP0 + tw2px(pap.dxaLeft + (cp == pf ? pap.dxaLeft1 : 0));
    if (xStart < XP0)
        xStart = XP0;
    xMax = XP0 + tw2px((long)sep.dxaText - pap.dxaRight);
    x = xStart;
    lim = pl > g_doc.cpMac ? g_doc.cpMac : pl;      /* text of the paragraph */
    ln->endMark = FALSE;
    ln->paraEnd = FALSE;
    for (c = cp; c < lim; c++) {
        BYTE ch = doc_ch(&g_doc, c);
        int w;
        if (c >= runLim) {
            run_at(&g_doc, c, &runFirst, &runLim, &ra);
            chp_decode(&g_doc, ra, &chp);
            f = font_for(&chp);
            if (f) {
                int up = pos_px(chp.hpsPos);
                if (f->ascent + up > asc)
                    asc = f->ascent + up;
                if (f->descent - up > desc)
                    desc = f->descent - up;
            }
        }
        if (ch == '\r' || ch == '\n') {
            if (c + 1 < lim && ch == '\r' && doc_ch(&g_doc, c + 1) == '\n')
                c++;
            c++;
            ln->paraEnd = TRUE;
            break;
        }
        if (ch == 12) {
            /* a page break (Ctrl+Enter) ends its paragraph and has a
               line of its own: the text before it ends there (WR21) */
            if (c == cp) {
                c++;
                ln->paraEnd = TRUE;
                ln->pageBreak = TRUE;
            }
            break;
        }
        if (ch == '\t')
            w = tab_stop(&pap, x, XP0) - x;
        else
            w = ch == 1 ? page_w(f) : f ? f->width[ch] : 8;
        if (x + w > xMax && c > cp && ch != ' ') {
            if (haveBrk) {
                c = brk;
                x = xBrk;
                spaces = spacesBrk;
            }
            break;
        }
        x += w;
        if (ch == '\t') {
            tabCp = c + 1;
            xTab = x;
        }
        if (ch == ' ') {
            spaces++;
            brk = c + 1;
            xBrk = x - w;               /* trailing blanks do not count */
            spacesBrk = spaces - 1;
            haveBrk = TRUE;
        }
    }
    if (c >= lim) {
        if (pl > g_doc.cpMac) {         /* the end paragraph: end mark */
            ln->endMark = TRUE;
            c = pl;                     /* the virtual mark */
        }
        ln->paraEnd = TRUE;
    }
    if (!f) {                           /* empty line: the properties there */
        props_at(cp ? cp - (cp == g_doc.cpMac && cp > 0) : 0, &chp);
        if (g_doc.cpMac == 0)
            chp_decode(&g_doc, g_insChpValid ? g_insChp : g_doc.chpNew, &chp);
        f = font_for(&chp);
        if (f) {
            asc = f->ascent;
            desc = f->descent;
        }
    }
    ln->cpLim = c;
    /* the end mark's line is at least 13 above and 3 below its baseline
       (WR3: after 8-point text, ascent 11, the mark sits as with 10
       points; UNVERIFIED: where the minimum comes from) */
    if (ln->endMark) {
        if (asc < 13)
            asc = 13;
        if (desc < 3)
            desc = 3;
    }
    /* A line is as tall as its highest ascent (with external leading)
       plus its deepest descent; 1 1/2 and double spacing make it the
       spacing's height (360 and 480 twips: 24 and 32 pixels), the space
       added above the text (WR8 at 10 points, WR3 at 8; UNVERIFIED:
       fonts taller than the spacing) */
    ht = pap.dyaLine > 240 ? tw2px(pap.dyaLine) - asc - desc : 0;
    if (ht < 0)
        ht = 0;
    ln->ascent = asc + ht;
    ln->height = asc + desc + ht;
    ln->space = ht;
    ln->width = x - xStart;
    ln->nspaces = 0;
    ln->extra = 0;
    if (tabCp && tabCp <= c && (pap.jc == 1 || pap.jc == 2)) {
        /* with a tab in the line only what follows the last one is
           centered or set right, between its tab stop and the right
           indent; what comes before stays at the left (WR24 R3) */
        int w = x - xTab, sh = pap.jc == 1 ? (xMax - xTab - w) / 2 : xMax - xTab - w;
        ln->tabCp = tabCp;
        ln->tabShift = sh > 0 ? sh : 0;
    } else switch (pap.jc) {
    case 1:
        xStart += (xMax - xStart - ln->width) / 2;
        break;
    case 2:
        xStart = xMax - ln->width;
        break;
    case 3:
        if (!ln->paraEnd && spaces) {
            ln->nspaces = spaces;
            ln->extra = xMax - xStart - ln->width;
        }
        break;
    }
    ln->xLeft = xStart;
}

/* the start of the line before the one starting at cp */
static DWORD line_before(DWORD cp)
{
    DWORD pf, pl, s, last;
    ATOM32 pa;
    LINE ln;
    if (cp == 0)
        return 0;
    para_at(&g_doc, cp - 1, &pf, &pl, &pa);
    for (s = last = pf; s < cp;) {
        last = s;
        layout_line(s, &ln);
        if (ln.cpLim <= s)
            break;
        s = ln.cpLim;
    }
    return last;
}

/* the start of the line containing cp */
static DWORD line_start(DWORD cp)
{
    DWORD pf, pl, s;
    ATOM32 pa;
    LINE ln;
    para_at(&g_doc, cp, &pf, &pl, &pa);
    for (s = pf;;) {
        layout_line(s, &ln);
        if (ln.cpLim > cp || ln.endMark || ln.cpLim <= s)
            return s;
        s = ln.cpLim;
    }
}

/* the length of the paragraph mark that ends at lim: CR LF, or a page
   break (character 12) */
static DWORD mark_len(DWORD lim);
static BOOL visible(DWORD cp);
static int caret_mid(int h);
static void redraw(void);
static void layout_band(DWORD cp, int yp, LINE *ln);
static BOOL line_prev(DWORD cp, int yp, DWORD *pcp, int *pyp);

/* for Confirm Page Breaks (print.c): the line before and after the one
   that starts at cp, and where its text ends */
DWORD view_line_before(DWORD cp)
{
    return line_before(line_start(cp));
}

DWORD view_line_after(DWORD cp)
{
    LINE ln;
    layout_line(line_start(cp), &ln);
    return ln.endMark ? cp : ln.cpLim;
}

/* Confirm Page Breaks shows the line at cp: above the window it comes
   with 128 pixels of lines above it, below with the caret's middle rule
   (WR31: line 55 under line 47, the break after line 70 under line 61;
   UNVERIFIED: the rule for lines above) */
void view_show_line(DWORD cp)
{
    RECT r;
    int h = 0, yp = 0, ypp;
    DWORD s, p;
    LINE ln;
    if (visible(cp))
        return;
    GetClientRect(g_docwnd, &r);
    s = line_start(cp);
    while (h < (cp < cpTop ? caret_mid(r.bottom) - 32 : caret_mid(r.bottom)) &&
           line_prev(s, yp, &p, &ypp)) {
        layout_band(p, ypp, &ln);
        h += ln.height;
        s = p;
        yp = ypp;
    }
    cpTop = s;
    ypTop = yp;
    redraw();
}

DWORD view_line_end(DWORD cp)
{
    LINE ln;
    DWORD e;
    layout_line(line_start(cp), &ln);
    e = ln.cpLim > g_doc.cpMac ? g_doc.cpMac : ln.cpLim;
    if (ln.paraEnd && !ln.endMark && e > ln.cpFirst)
        e -= mark_len(e);
    return e;
}

static DWORD mark_len(DWORD lim)
{
    return lim > 0 && doc_ch(&g_doc, lim - 1) == 12 ? 1 : 2;
}

/* the line at (cp, yp): yp is the band's first row in a picture */
static void layout_band(DWORD cp, int yp, LINE *ln)
{
    layout_line(cp, ln);
    if (ln->picture && yp > 0 && yp < ln->picH) {
        ln->yp = yp;
        ln->height = ln->ascent = ln->picH - yp < BAND ? ln->picH - yp : BAND;
    }
}

/* the line after ln: FALSE at the end of the document */
static BOOL line_next(const LINE *ln, DWORD *cp, int *yp)
{
    if (ln->picture && ln->yp + ln->height < ln->picH) {
        *cp = ln->cpFirst;
        *yp = ln->yp + ln->height;
        return TRUE;
    }
    if (ln->endMark || ln->cpLim <= ln->cpFirst)
        return FALSE;
    *cp = ln->cpLim;
    *yp = 0;
    return TRUE;
}

/* the line before (cp, yp): FALSE at the start of the document */
static BOOL line_prev(DWORD cp, int yp, DWORD *pcp, int *pyp)
{
    LINE ln;
    if (yp > 0) {
        *pcp = cp;
        *pyp = yp > BAND ? yp - BAND : 0;
        return TRUE;
    }
    if (cp == 0)
        return FALSE;
    *pcp = line_before(cp);
    layout_line(*pcp, &ln);
    *pyp = ln.picture && ln.picH > BAND ? (ln.picH - 1) / BAND * BAND : 0;
    return TRUE;
}

/* ---- the visible lines ---- */

static HLOCAL hlines;
static int nlines;

static LINE *lines(void)
{
    return (LINE *)LocalLock(hlines);
}

void view_layout(void)
{
    RECT r;
    int y;
    DWORD cp = cpTop;
    int yp = ypTop;
    LINE ln, *l;
    sep_decode(&g_doc, &sep);
    GetClientRect(g_docwnd, &r);
    nlines = 0;
    for (y = YP0; y < r.bottom;) {
        HLOCAL h;
        layout_band(cp, yp, &ln);
        h = hlines ? LocalReAlloc(hlines, (nlines + 1) * sizeof(LINE), LMEM_MOVEABLE)
                   : LocalAlloc(LMEM_MOVEABLE, sizeof(LINE));
        if (!h)
            break;
        hlines = h;
        l = lines();
        l[nlines++] = ln;
        LocalUnlock(hlines);
        y += ln.height;
        if (!line_next(&ln, &cp, &yp))
            break;
    }
}

/* doc pixel x of cp within line ln */
static int x_of(const LINE *ln, DWORD cp)
{
    DWORD c, runLim = 0, rf;
    ATOM32 ra;
    CHPX chp;
    FONTC *f = NULL;
    PAPX pap;
    DWORD pf, pl;
    ATOM32 pa;
    int x = ln->xLeft, sp = 0;
    if (ln->picture)                    /* a picture is one unit */
        return cp > ln->cpFirst ? ln->xLeft + ln->width : ln->xLeft;
    para_at(&g_doc, ln->cpFirst, &pf, &pl, &pa);
    pap_decode(&g_doc, pa, &pap);
    for (c = ln->cpFirst; c < cp && c < ln->cpLim && c < g_doc.cpMac; c++) {
        BYTE ch = doc_ch(&g_doc, c);
        if (c >= runLim) {
            run_at(&g_doc, c, &rf, &runLim, &ra);
            chp_decode(&g_doc, ra, &chp);
            f = font_for(&chp);
        }
        if (ch == '\r' || ch == '\n')
            break;
        if (ch == '\t')
            x = tab_stop(&pap, x, XP0) + (c + 1 == ln->tabCp ? ln->tabShift : 0);
        else
            x += ch == 1 ? page_w(f) : f ? f->width[ch] : 8;
        if (ch == ' ' && ln->nspaces) {
            x += ln->extra / ln->nspaces + (sp < ln->extra % ln->nspaces);
            sp++;
        }
    }
    return x;
}

/* ---- the caret ---- */

static void caret_draw(void)
{
    HDC dc;
    RECT r;
    if (!caretShown)
        return;
    dc = GetDC(g_docwnd);
    SetRect(&r, caretX - xpScroll, caretY, caretX - xpScroll + CARET_W, caretY + caretH);
    InvertRect(dc, &r);
    ReleaseDC(g_docwnd, dc);
    caretOn = !caretOn;
}

static void caret_hide(void)
{
    if (caretOn)
        caret_draw();
}

/* place the caret at g_selFirst when the selection is empty */
static void caret_place(void)
{
    LINE *l;
    int i, y = YP0;
    ruler_update();
    caret_hide();
    caretShown = FALSE;
    if (g_selFirst != g_selLim || GetFocus() != g_docwnd)
        return;
    l = lines();
    for (i = 0; i < nlines; i++) {
        if (g_selFirst >= l[i].cpFirst &&
            (g_selFirst < l[i].cpLim || l[i].endMark ||
             (g_selFirst == l[i].cpLim && i + 1 == nlines && !l[i].paraEnd))) {
            caretX = x_of(&l[i], g_selFirst) - CARET_W + 1;   /* WR1 */
            /* the caret leaves out the spacing's rows (WR24 R3) */
            caretY = y + l[i].space;
            caretH = l[i].height - l[i].space;
            /* before a page break 3.1 shows no caret (WR21: after Up onto
               it and after typing there; UNVERIFIED: where it is) */
            caretShown = !l[i].pageBreak;
            break;
        }
        y += l[i].height;
    }
    LocalUnlock(hlines);
    if (caretShown && !caretDefer)
        caret_draw();
}

/* after typing the caret stays off until the next blink (WR1: the
   original's caret is not seen after text was typed) */
static void caret_defer(void)
{
    menuShown = FALSE;
    caretDefer = TRUE;
    if (GetFocus() == g_docwnd)
        SetTimer(g_docwnd, TIMER_CARET, GetCaretBlinkTime(), NULL);
}

void view_caret_defer(void)
{
    caret_defer();
}

/* ---- Move Picture and Size Picture (WR22, WR28) ----
   Edit > Move Picture / Size Picture on a selected picture: the picture
   shows as it is, not inverted, in a black frame one row taller than
   it; a gray outline the same size (its pixels XOR colour 8) follows
   the keys or the mouse; Enter or a click sets it, Escape leaves it.
   Move: the outline goes 8 pixels a Left or Right, or is centred on the
   mouse's x; the picture's offset becomes the outline's place (WR28 N2,
   N4: 123 pixels, dxaOffset 1845). Size: the first arrow key takes the
   right (Right, Left) or bottom (Down, Up) edge, the next ones move it,
   8 pixels a step across and 6 down (WR28 Z2: 3 Right and 2 Down, 80 x
   41 to 96 x 47; UNVERIFIED: the steps); with the mouse the edges on the
   mouse's side of the outline's centre follow it (N3); the page box
   shows the scale, "1.2X/1.1Y" (tenths, cut). mx and my are the
   outline's width and height (the height with its extra row) in
   thousandths of the picture's at scale 1000 (N4: 146 / 80 = 1825,
   134 / 41 = 3268). */
static int pmMode;                      /* 0, IDM_MOVEPICT, IDM_SIZEPICT */
static RECT pmOrig, pmCur;              /* frame and outline, window pixels */
static int pmGrabX, pmGrabY;            /* Size: the edge taken (1), 0 none */
static int pmW0, pmH0;                  /* the outline's size at scale 1000 */
static POINT pmMouse;                   /* the pointer when the mode began */
static BOOL pmEatChar;                  /* Enter's or Escape's WM_CHAR follows */

static void pm_edges(HDC dc, const RECT *r, DWORD rop)
{
    int w = r->right - r->left, h = r->bottom - r->top;
    PatBlt(dc, r->left, r->top, w, 1, rop);
    if (h > 1)
        PatBlt(dc, r->left, r->bottom - 1, w, 1, rop);
    if (h > 2) {
        PatBlt(dc, r->left, r->top + 1, 1, h - 2, rop);
        if (w > 1)
            PatBlt(dc, r->right - 1, r->top + 1, 1, h - 2, rop);
    }
}

static void pm_outline(HDC dc)
{
    HBRUSH b = CreateSolidBrush(RGB(128, 128, 128));
    HBRUSH o = SelectObject(dc, b);
    pm_edges(dc, &pmCur, PATINVERT);
    SelectObject(dc, o);
    DeleteObject(b);
}

BOOL view_pict_mode(void)
{
    return pmMode != 0;
}

static void redraw(void);

static void pm_redraw(void)
{
    InvalidateRect(g_docwnd, NULL, TRUE);
    UpdateWindow(g_docwnd);
    view_update_pageinfo();
}

/* the selected picture paragraph, FALSE if the selection is not one */
static BOOL sel_picture(DWORD *pf, DWORD *pl)
{
    DWORD f, l;
    ATOM32 a;
    PAPX p;
    para_at(&g_doc, g_selFirst, &f, &l, &a);
    pap_decode(&g_doc, a, &p);
    if (!p.fGraphics || g_selFirst != f || g_selLim != l)
        return FALSE;
    *pf = f;
    *pl = l;
    return TRUE;
}

BOOL view_pict_selected(void)
{
    DWORD f, l;
    return sel_picture(&f, &l);
}

static void pm_end(BOOL apply);

void view_pict_start(int mode)
{
    DWORD f, l;
    LINE *ln;
    int i, y = YP0;
    PICT pic;
    if (!sel_picture(&f, &l))
        return;
    view_scroll_to_caret();
    ln = lines();
    for (i = 0; i < nlines; i++) {
        if (ln[i].picture && ln[i].cpFirst == f)
            break;
        y += ln[i].height;
    }
    if (i == nlines) {
        LocalUnlock(hlines);
        return;
    }
    pict_read(f, l, &pic);
    /* heights in whole pixels, cut (WR28: 608 twips are 40 rows, the
       outline 41) */
    SetRect(&pmOrig, ln[i].xLeft - xpScroll, y - ln[i].yp, ln[i].xLeft - xpScroll + ln[i].width,
            y - ln[i].yp + (int)((long)pic.dyaSize * pic.my / 1000 * dpiY / 1440) + 1);
    LocalUnlock(hlines);
    pmW0 = tw2px(pic.dxaSize);
    pmH0 = (int)((long)pic.dyaSize * dpiY / 1440) + 1;
    pmCur = pmOrig;
    pmGrabX = pmGrabY = 0;
    pmMode = mode;
    caret_hide();
    GetCursorPos(&pmMouse);
    ScreenToClient(g_docwnd, &pmMouse);
    SetCapture(g_docwnd);
    pm_redraw();
}

static void pm_key(WPARAM vk)
{
    int d = vk == VK_LEFT || vk == VK_UP ? -1 : 1;
    switch (vk) {
    case VK_RETURN:
        pmEatChar = TRUE;
        pm_end(TRUE);
        return;
    case VK_ESCAPE:
        pmEatChar = TRUE;
        pm_end(FALSE);
        return;
    case VK_LEFT:
    case VK_RIGHT:
        if (pmMode == IDM_MOVEPICT)
            OffsetRect(&pmCur, 8 * d, 0);
        else if (!pmGrabX)
            pmGrabX = 1;                /* the first one takes the edge */
        else if (pmCur.right + 8 * d > pmCur.left + 1)
            pmCur.right += 8 * d;
        break;
    case VK_UP:
    case VK_DOWN:
        if (pmMode == IDM_MOVEPICT)
            return;
        if (!pmGrabY)
            pmGrabY = 1;
        else if (pmCur.bottom + 6 * d > pmCur.top + 2)
            pmCur.bottom += 6 * d;
        break;
    default:
        return;
    }
    pm_redraw();
}

static void pm_mouse(int x, int y)
{
    /* the pointer counts once it moves (WR28 M1: where it was is no
       place for the outline) */
    if (x == pmMouse.x && y == pmMouse.y)
        return;
    pmMouse.x = -32767;
    if (pmMode == IDM_MOVEPICT) {
        OffsetRect(&pmCur, x - (pmCur.right - pmCur.left) / 2 - pmCur.left, 0);
    } else {
        if (!pmGrabX)
            pmGrabX = x < (pmCur.left + pmCur.right) / 2 ? -1 : 1;
        if (!pmGrabY)
            pmGrabY = y < (pmCur.top + pmCur.bottom) / 2 ? -1 : 1;
        if (pmGrabX < 0 && x < pmCur.right - 1)
            pmCur.left = x;
        else if (pmGrabX > 0 && x > pmCur.left + 1)
            pmCur.right = x;
        if (pmGrabY > 0 && y > pmCur.top + 2)
            pmCur.bottom = y;
    }
    pm_redraw();
}

/* the scale shown in the page box while sizing: "1.2X/1.1Y" */
BOOL view_pict_scale(char *s)
{
    long mx, my;
    char n[8];
    if (pmMode != IDM_SIZEPICT || !pmW0 || !pmH0)
        return FALSE;
    mx = (long)(pmCur.right - pmCur.left) * 10 / pmW0;
    my = (long)(pmCur.bottom - pmCur.top) * 10 / pmH0;
    s_utoa(s, mx / 10);
    s_cat(s, ".", 16);
    s_utoa(n, mx % 10);
    s_cat(s, n, 16);
    s_cat(s, "X/", 16);
    s_utoa(n, my / 10);
    s_cat(s, n, 16);
    s_cat(s, ".", 16);
    s_utoa(n, my % 10);
    s_cat(s, n, 16);
    s_cat(s, "Y", 16);
    return TRUE;
}

static void pm_end(BOOL apply)
{
    DWORD f, l;
    int mode = pmMode;
    pmMode = 0;
    ReleaseCapture();
    if (apply && sel_picture(&f, &l) && !EqualRect(&pmCur, &pmOrig)) {
        WORD v[2];
        undo_save(IDS_EDITING);
        /* sizing from the left moves the picture too (WR28 N4) */
        if (mode == IDM_MOVEPICT || pmCur.left != pmOrig.left) {
            DWORD pf, pl;
            ATOM32 a;
            PAPX p;
            int px;
            para_at(&g_doc, f, &pf, &pl, &a);
            pap_decode(&g_doc, a, &p);
            px = pmCur.left + xpScroll - (XP0 - 1) - tw2px(p.dxaLeft);
            if (px < 0)
                px = 0;
            v[0] = (WORD)MulDiv(px, 1440, dpiX);
            doc_put(&g_doc, f + 8, v, 2);
        }
        if (mode == IDM_SIZEPICT) {
            v[0] = (WORD)((long)(pmCur.right - pmCur.left) * 1000 / pmW0);
            v[1] = (WORD)((long)(pmCur.bottom - pmCur.top) * 1000 / pmH0);
            doc_put(&g_doc, f + 36, v, 4);
        }
        g_doc.dirty = TRUE;
        view_changed(f);
        view_select(f, l, FALSE);
    } else {
        redraw();
        InvalidateRect(g_docwnd, NULL, FALSE);
    }
    view_update_pageinfo();
}

/* ---- painting ---- */

/* The end mark: a 6 x 6 currency-sign shape (a ring with four corner
   ticks), its bottom row 5 pixels above the baseline, at the text's x
   one pixel right of the position (observed with Arial 10, WR1, WR8;
   its place for other sizes UNVERIFIED).
   Drawn from this table: our own rendering of the sign. */
static void end_mark(HDC dc, int x, int base)
{
    static const BYTE rows[6] = { 0x33, 0x1E, 0x12, 0x12, 0x1E, 0x33 };  /* bit 5 = left */
    HBRUSH b = GetStockObject(BLACK_BRUSH);
    int r, c;
    for (r = 0; r < 6; r++)
        for (c = 0; c < 6; c++)
            if (rows[r] & (0x20 >> c)) {
                RECT q;
                SetRect(&q, x + 1 + c, base - 10 + r, x + 2 + c, base - 9 + r);
                FillRect(dc, &q, b);
            }
}

/* the page-break mark: a 7 x 5 double chevron in the left margin at the
   first line of each page in the page table, its bottom row a pixel
   above the baseline (WR2: README.WRI's page 1). Our own drawing. */
static void page_mark(HDC dc, int x, int base)
{
    static const BYTE rows[5] = { 0x6C, 0x36, 0x1B, 0x36, 0x6C };  /* bit 6 = left */
    HBRUSH b = GetStockObject(BLACK_BRUSH);
    int r, c;
    for (r = 0; r < 5; r++)
        for (c = 0; c < 7; c++)
            if (rows[r] & (0x40 >> c)) {
                RECT q;
                SetRect(&q, x + c, base - 6 + r, x + c + 1, base - 5 + r);
                FillRect(dc, &q, b);
            }
}

static BOOL page_starts(DWORD cp)
{
    DWORD i;
    for (i = 0; i < g_doc.npages; i++) {
        struct { WORD pgn, pad; DWORD cp; } e;
        hm_get(&g_doc.pgtb, i * 8, &e, 8);
        if (e.cp == cp)
            return TRUE;
        if (e.cp > cp)
            break;
    }
    return FALSE;
}

static void paint_line(HDC dc, const LINE *ln, int y)
{
    if (!ln->yp && page_starts(ln->cpFirst))
        page_mark(dc, -xpScroll, y + ln->ascent);
    if (ln->picture) {
        /* ln: the visible bands of one picture together; the picture is
           drawn whole, clipped to them */
        PICT pic;
        RECT r;
        int top = y - ln->yp;
        pict_read(ln->cpFirst, ln->cpLim, &pic);
        SaveDC(dc);
        IntersectClipRect(dc, ln->xLeft - xpScroll, y, ln->xLeft - xpScroll + ln->width, y + ln->height);
        SetRect(&r, ln->xLeft - xpScroll, top, ln->xLeft - xpScroll + ln->width, top + ln->picH);
        /* an OLE object: OLECLI draws it (an embedded Paintbrush picture
           has no picture of its own in the stream); else ours */
        if (pic.mm != 0xE4 || !ole_draw(dc, ln->cpFirst, ln->cpLim, &r))
            pict_draw(dc, &pic, ln->xLeft - xpScroll, top, ln->width, ln->picH);
        RestoreDC(dc, -1);
        return;
    }
    {
    DWORD c = ln->cpFirst, lim = ln->cpLim, rf, rl;
    ATOM32 ra;
    CHPX chp;
    FONTC *f;
    PAPX pap;
    DWORD pf, pl;
    ATOM32 pa;
    int x = ln->xLeft - xpScroll, sp = 0;
    char buf[128];
    para_at(&g_doc, c, &pf, &pl, &pa);
    pap_decode(&g_doc, pa, &pap);
    if (lim > g_doc.cpMac)
        lim = g_doc.cpMac;
    while (c < lim) {
        WORD n = 0;
        int up;
        run_at(&g_doc, c, &rf, &rl, &ra);
        chp_decode(&g_doc, ra, &chp);
        f = font_for(&chp);
        if (!f)
            return;
        SelectObject(dc, f->h);
        up = pos_px(chp.hpsPos);
        /* a piece: same run, no tab, no paragraph mark */
        while (c + n < lim && c + n < rl && n < sizeof buf) {
            BYTE ch = doc_ch(&g_doc, c + n);
            if (ch == '\t' || ch == '\r' || ch == '\n' || ch == 1 || ch == 12)
                break;
            buf[n++] = ch;
        }
        if (n) {
            int w = 0, i, nsp = 0;
            for (i = 0; i < n; i++) {
                w += f->width[(BYTE)buf[i]];
                if (buf[i] == ' ')
                    nsp++;
            }
            if (ln->nspaces && nsp) {
                int ex = 0, k;
                for (k = 0; k < nsp; k++, sp++)
                    ex += ln->extra / ln->nspaces + (sp < ln->extra % ln->nspaces);
                SetTextJustification(dc, ex, nsp);
                w += ex;
            }
            TextOut(dc, x, y + ln->ascent - f->tmAscent - up, buf, n);
            if (ln->nspaces && nsp)
                SetTextJustification(dc, 0, 0);
            x += w;
            c += n;
            continue;
        }
        {
            BYTE ch = doc_ch(&g_doc, c);
            if (ch == '\t') {
                x = tab_stop(&pap, x + xpScroll, XP0) - xpScroll + (c + 1 == ln->tabCp ? ln->tabShift : 0);
            } else if (ch == 1) {
                /* the page number: "(page)" on screen (SDK ch. 8) */
                TextOut(dc, x, y + ln->ascent - f->tmAscent - up, "(page)", 6);
                x += page_w(f);
            }
            c++;
        }
    }
    if (ln->endMark)
        end_mark(dc, x, y + ln->ascent);
    if (ln->pageBreak) {
        /* a dotted line, 2 rows of 2 pixels on and 2 off, from a pixel
           right of the text's left edge to the window's right edge, its
           rows 2 and 1 above the baseline (WR21 with Arial 10; other
           fonts UNVERIFIED) */
        RECT cr, q;
        int xx;
        HBRUSH b = GetStockObject(BLACK_BRUSH);
        GetClientRect(g_docwnd, &cr);
        for (xx = XP0 + 1 - xpScroll; xx < cr.right; xx += 4) {
            SetRect(&q, xx, y + ln->ascent - 2, xx + 2 > cr.right ? cr.right : xx + 2, y + ln->ascent);
            FillRect(dc, &q, b);
        }
    }
    }
}

/* invert the selected part of each visible line */
static void paint_sel(HDC dc)
{
    LINE *l;
    int i, y = YP0;
    RECT r, cr;
    if (g_selFirst == g_selLim)
        return;
    GetClientRect(g_docwnd, &cr);
    l = lines();
    for (i = 0; i < nlines; i++) {
        DWORD a = g_selFirst > l[i].cpFirst ? g_selFirst : l[i].cpFirst;
        DWORD b = g_selLim < l[i].cpLim ? g_selLim : l[i].cpLim;
        if (a < b) {
            int x1 = x_of(&l[i], a), x2;
            if (b >= l[i].cpLim && l[i].paraEnd && g_selLim > l[i].cpLim - 1)
                x2 = x_of(&l[i], b) + 6; /* the paragraph mark (UNVERIFIED width) */
            else
                x2 = x_of(&l[i], b);
            SetRect(&r, x1 - xpScroll, y, x2 - xpScroll, y + l[i].height);
            /* a selected page break: its whole line, from a pixel left of
               the text to the window's edge (WR31 C3) */
            if (l[i].pageBreak)
                SetRect(&r, XP0 - 1 - xpScroll, y, cr.right, y + l[i].height);
            InvertRect(dc, &r);
        }
        y += l[i].height;
    }
    LocalUnlock(hlines);
}

BOOL g_holdPaint;                       /* links updating on opening: blank (WR33 O2) */

static void paint(HWND w)
{
    PAINTSTRUCT ps;
    HDC dc = BeginPaint(w, &ps);
    LINE *l;
    int i, y = YP0;
    caretOn = FALSE;
    if (g_holdPaint) {
        EndPaint(w, &ps);
        return;
    }
    SetBkMode(dc, TRANSPARENT);
    l = lines();
    for (i = 0; i < nlines; i++) {
        LINE t = l[i];
        /* a picture's bands are drawn together */
        while (t.picture && i + 1 < nlines && l[i + 1].picture && l[i + 1].cpFirst == t.cpFirst)
            t.height += l[++i].height;
        if (t.height && y + t.height > ps.rcPaint.top && y < ps.rcPaint.bottom)
            paint_line(dc, &t, y);
        y += t.height;
    }
    LocalUnlock(hlines);
    if (pmMode) {
        pm_edges(dc, &pmOrig, BLACKNESS);
        pm_outline(dc);
    } else {
        paint_sel(dc);
    }
    SelectObject(dc, GetStockObject(SYSTEM_FONT));
    EndPaint(w, &ps);
    if (caretShown)
        caret_draw();
}

/* ---- scroll bars and the page number ---- */

/* The thumb: range 0-255, position the top line's cp in 255ths of the
   document, rounded (WR20: 80 positions in two documents; it does not
   move while the top is inside a picture, WR17). While a run of typing
   goes on, the document counts 32 characters more (WR18: 90 * 255 / 212
   and / 230 for 180 and 198 characters; UNVERIFIED: why). */
/* cp as the thumb counts it: a picture paragraph counts as its 40-byte
   header (WR10, WR16, WR17: "top" and two pictures make 85) */
static DWORD scroll_cp(DWORD cp)
{
    DWORD i, f, l, r = cp;
    ATOM32 a;
    PAPX p;
    for (i = 0; i < g_doc.nparas; i++) {
        para_get(&g_doc, i, &f, &l, &a);
        if (f >= cp)
            break;
        pap_decode(&g_doc, a, &p);
        if (l > cp)
            l = cp;
        if (p.fGraphics && l - f > 40)
            r -= l - f - 40;
    }
    return r;
}

static void vscroll_update(void)
{
    DWORD mac = scroll_cp(g_doc.cpMac) + (undo_typing() ? 32 : 0);
    DWORD top = scroll_cp(cpTop);
    int pos = mac ? (int)((top * 510 + mac) / (mac * 2)) : 0;
    if (pos > 255)
        pos = 255;
    SetScrollPos(g_vscroll, SB_CTL, pos, TRUE);
}

void view_update_pageinfo(void)
{
    InvalidateRect(g_pageinfo, NULL, TRUE);
}

/* the page number shown: from the page table (after Repaginate), else
   the first page number */
static WORD page_of(DWORD cp)
{
    DWORD i;
    WORD pgn = sep.pgnFirst;
    for (i = 0; i < g_doc.npages; i++) {
        struct { WORD pgn, pad; DWORD cp; } e;
        hm_get(&g_doc.pgtb, i * 8, &e, 8);
        if (e.cp > cp)
            break;
        pgn = e.pgn;
    }
    return pgn;
}

LRESULT FAR PASCAL __export PageInfoWndProc(HWND w, UINT msg, WPARAM wp, LPARAM lp)
{
    if (msg == WM_PAINT) {
        PAINTSTRUCT ps;
        HDC dc = BeginPaint(w, &ps);
        RECT r;
        char s[24], num[12];
        GetClientRect(w, &r);
        /* observed with other colour schemes (WR6): white text on the
           window frame colour */
        {
            HBRUSH b = CreateSolidBrush(GetSysColor(COLOR_WINDOWFRAME));
            FillRect(dc, &r, b);
            DeleteObject(b);
        }
        SetTextColor(dc, RGB(255, 255, 255));
        SetBkColor(dc, GetSysColor(COLOR_WINDOWFRAME));
        if (!view_pict_scale(s)) {
            load_str(IDS_PAGE1, s, sizeof s);       /* "Page 1" */
            if (s_len(s) > 1)
                s[s_len(s) - 1] = 0;                /* "Page " */
            s_utoa(num, page_of(cpTop));
            s_cat(s, num, sizeof s);
        }
        TextOut(dc, 6, -1, s, s_len(s));            /* calibrated, WR1 */
        EndPaint(w, &ps);
        return 0;
    }
    return DefWindowProc(w, msg, wp, lp);
}

/* ---- positions ---- */

/* the cp nearest to doc point (x, y) */
static DWORD cp_from_point(int x, int y)
{
    LINE *l;
    int i, yy = YP0;
    DWORD cp = g_doc.cpMac, c;
    l = lines();
    for (i = 0; i < nlines; i++) {
        if (y < yy + l[i].height || i + 1 == nlines) {
            DWORD lim = l[i].cpLim;
            if (l[i].paraEnd && lim > l[i].cpFirst && !l[i].endMark)
                lim -= mark_len(lim);   /* before the paragraph mark */
            if (lim > g_doc.cpMac)
                lim = g_doc.cpMac;
            cp = l[i].cpFirst;
            for (c = l[i].cpFirst; c < lim; c++) {
                int x1 = x_of(&l[i], c), x2 = x_of(&l[i], c + 1);
                if (x < (x1 + x2) / 2)
                    break;
                cp = c + 1;
            }
            if (cp > lim)
                cp = lim;
            break;
        }
        yy += l[i].height;
    }
    LocalUnlock(hlines);
    return cp;
}

/* is cp on screen? */
static BOOL visible(DWORD cp)
{
    LINE *l;
    RECT r;
    int i, y = YP0;
    BOOL in = FALSE;
    GetClientRect(g_docwnd, &r);
    l = lines();
    for (i = 0; i < nlines; i++) {
        if (y + l[i].height > r.bottom)
            break;
        if (cp >= l[i].cpFirst && (cp < l[i].cpLim || l[i].endMark)) {
            in = TRUE;
            break;
        }
        y += l[i].height;
    }
    LocalUnlock(hlines);
    return in;
}

static void redraw(void)
{
    view_layout();
    InvalidateRect(g_docwnd, NULL, TRUE);
    vscroll_update();
    view_update_pageinfo();
}

/* When the caret leaves the window below, its line goes to the middle:
   the lines above it are taken (from the caret's line back) until they
   are at least this tall: half the window's whole 16-pixel lines, in
   pixels. Window 346 pixels tall: 160 (WR10, WR16, WR17, WR18); 238: 112
   (WR19). */
static int caret_mid(int h)
{
    return (h / BAND) / 2 * BAND;
}

void view_scroll_to_caret(void)
{
    DWORD cp = g_selLim > g_selFirst && g_selLim != selAnchor ? g_selLim : g_selFirst;
    if (cp < cpTop) {
        cpTop = line_start(cp);
        ypTop = 0;
        redraw();
        return;
    }
    if (!visible(cp)) {
        /* the caret's line to the middle (caret_mid) */
        RECT r;
        int h = 0, yp = 0, ypp;
        DWORD s = line_start(cp), p;
        LINE ln;
        GetClientRect(g_docwnd, &r);
        while (h < caret_mid(r.bottom) && line_prev(s, yp, &p, &ypp)) {
            layout_band(p, ypp, &ln);
            h += ln.height;
            s = p;
            yp = ypp;
        }
        cpTop = s;
        ypTop = yp;
        redraw();
        /* a pasted picture that scrolls the window leaves the caret
           shown, one that does not leaves it off (WR16, WR17); typing
           leaves it off either way (WR18) */
        if (!typing)
            caretDefer = FALSE;
    }
}

void view_select(DWORD first, DWORD lim, BOOL show)
{
    HDC dc;
    if (lim > g_doc.cpMac)
        lim = g_doc.cpMac;
    if (first > lim)
        first = lim;
    caret_hide();
    dc = GetDC(g_docwnd);
    paint_sel(dc);                      /* remove the old highlight */
    g_selFirst = first;
    g_selLim = lim;
    g_insChpValid = FALSE;
    if (show)
        view_scroll_to_caret();
    paint_sel(dc);
    ReleaseDC(g_docwnd, dc);
    caret_place();
}

/* the first character of the body: after the header and footer
   paragraphs at the start (SDK ch. 8, 8.3.2) */
static DWORD body_start(void)
{
    DWORD i, f, l;
    ATOM32 a;
    PAPX p;
    if (hf_mode())
        return 0;
    for (i = 0; i + 1 < g_doc.nparas; i++) {
        para_get(&g_doc, i, &f, &l, &a);
        pap_decode(&g_doc, a, &p);
        if (!(p.rhc & 6))
            return f;
    }
    para_get(&g_doc, g_doc.nparas - 1, &f, &l, &a);
    return f;
}

/* entering a menu shows the caret (WR3, w16write); a dialog the menu
   leads to gives back the state from before the menu (WR1 S4, WR4 L2:
   off after typing at the end; WR23: shown) */
void view_caret_menu(void)
{
    if (!menuShown)
        deferBeforeMenu = caretDefer;
    menuShown = TRUE;
    caretDefer = FALSE;
    caret_place();
}

/* the caret shows at once (after the Header view, WR9) */
void view_caret_now(void)
{
    caretDefer = FALSE;
    caret_place();
}

void view_reset(void)
{
    cpTop = body_start();
    ypTop = 0;
    g_selFirst = g_selLim = selAnchor = cpTop;
    xpScroll = 0;
    g_insChpValid = FALSE;
    fonts_free();
    redraw();
    caret_place();
}

/* text changed from cp on: lay out again and repaint */
BOOL view_last_typed(void)
{
    return lastTyped;
}

void view_changed(DWORD cp)
{
    LINE ln;
    (void)cp;
    lastTyped = typing;
    if (cpTop > g_doc.cpMac)
        cpTop = line_start(g_doc.cpMac);
    else
        cpTop = line_start(cpTop);
    layout_line(cpTop, &ln);
    if (!ln.picture || ypTop >= ln.picH)
        ypTop = 0;
    caret_hide();
    redraw();
    view_scroll_to_caret();
    UpdateWindow(g_docwnd);
    caret_place();
}

/* ---- editing keys ---- */

void sel_props(CHPX *c)
{
    DWORD cp = g_selFirst;
    if (g_selFirst == g_selLim && g_insChpValid) {
        chp_decode(&g_doc, g_insChp, c);
        return;
    }
    if (g_selFirst == g_selLim) {
        /* typing continues the character before; at a paragraph start
           the one after (UNVERIFIED) */
        if (cp > 0 && doc_ch(&g_doc, cp - 1) != '\n')
            cp--;
        else if (cp >= g_doc.cpMac && cp > 0)
            cp--;
    }
    props_at(cp, c);
    c->special = 0;                     /* typing after "(page)" is text */
}

static ATOM32 typing_chp(void)
{
    CHPX c;
    if (g_insChpValid)
        return g_insChp;
    sel_props(&c);
    return chp_atom(&g_doc, &c);
}

static void replace_sel(const char FAR *s, DWORD n)
{
    ATOM32 a = typing_chp();
    DWORD cp = g_selFirst;
    BOOL keep = g_insChpValid;
    ATOM32 ins = g_insChp;
    if (g_selLim > g_selFirst)
        doc_delete(&g_doc, g_selFirst, g_selLim - g_selFirst);
    doc_insert(&g_doc, cp, s, n, a);
    g_selFirst = g_selLim = selAnchor = cp + n;
    g_insChpValid = keep;
    g_insChp = ins;
    view_changed(cp);
}

static void view_char2(WORD ch, char *s);

void view_char(WORD ch)
{
    char s[2];
    caret_defer();
    typing = TRUE;
    view_char2(ch, s);
    typing = FALSE;
    /* after typing the caret goes off a moment later, until it blinks,
       wherever it was typed: a menu or a key at once after the typing
       finds it on (WR23 T1, WR25 A1; WR3's old shot), half a second later
       off (WR24 R4, R5; WR1, WR18) */
    caretDefer = FALSE;
    caret_place();
    if (GetFocus() == g_docwnd)
        SetTimer(g_docwnd, TIMER_TYPED, TYPED_MS, NULL);
}

static void view_char2(WORD ch, char *s)
{
    if (ch >= ' ' || ch == '\t' || ch == '\r' || ch == '\n' || ch == '\b')
        undo_save(IDS_TYPING);
    if (ch == '\b') {
        if (g_selLim > g_selFirst) {
            doc_delete(&g_doc, g_selFirst, g_selLim - g_selFirst);
            g_selLim = selAnchor = g_selFirst;
        } else if (g_selFirst > 0) {
            DWORD n = 1;
            if (g_selFirst >= 2 && doc_ch(&g_doc, g_selFirst - 1) == '\n' &&
                doc_ch(&g_doc, g_selFirst - 2) == '\r')
                n = 2;
            g_selFirst -= n;
            doc_delete(&g_doc, g_selFirst, n);
            g_selLim = selAnchor = g_selFirst;
        }
        view_changed(g_selFirst);
        return;
    }
    if (ch == '\r') {
        replace_sel("\r\n", 2);
        return;
    }
    if (ch == '\n') {                   /* Ctrl+Enter: a page break (WR21) */
        replace_sel("\f", 1);
        return;
    }
    if (ch < ' ' && ch != '\t')
        return;
    s[0] = (char)ch;
    replace_sel(s, 1);
}

/* the picture paragraph holding cp, if any: arrows step over a picture
   as one unit (WR22, WR28: Shift+Left after a pasted picture selects it) */
static BOOL in_picture(DWORD cp, DWORD *pf, DWORD *pl)
{
    ATOM32 a;
    PAPX p;
    if (cp >= g_doc.cpMac)
        return FALSE;
    para_at(&g_doc, cp, pf, pl, &a);
    pap_decode(&g_doc, a, &p);
    return p.fGraphics;
}

static DWORD next_cp(DWORD cp)
{
    DWORD pf, pl;
    if (cp >= g_doc.cpMac)
        return g_doc.cpMac;
    if (in_picture(cp, &pf, &pl))
        return pl;
    if (doc_ch(&g_doc, cp) == '\r' && cp + 1 < g_doc.cpMac && doc_ch(&g_doc, cp + 1) == '\n')
        return cp + 2;
    return cp + 1;
}

static DWORD prev_cp(DWORD cp)
{
    DWORD pf, pl;
    if (cp == 0)
        return 0;
    if (in_picture(cp - 1, &pf, &pl))
        return pf;
    if (cp >= 2 && doc_ch(&g_doc, cp - 1) == '\n' && doc_ch(&g_doc, cp - 2) == '\r')
        return cp - 2;
    return cp - 1;
}

/* the cp in the line starting at s nearest to doc x */
static DWORD cp_at_x(DWORD s, int x)
{
    LINE ln;
    DWORD c, cp = s, lim;
    layout_line(s, &ln);
    lim = ln.cpLim;
    if (ln.paraEnd && !ln.endMark && lim >= 2)
        lim -= mark_len(lim);
    if (lim > g_doc.cpMac)
        lim = g_doc.cpMac;
    for (c = s; c < lim; c++) {
        int x1 = x_of(&ln, c), x2 = x_of(&ln, c + 1);
        if (x < (x1 + x2) / 2)
            break;
        cp = c + 1;
    }
    return cp;
}

/* A window down: the line that was only partly visible (or the first
   below) becomes the top one (WR2: Page Down in README.WRI). Up: back
   by the lines that fit in the window (UNVERIFIED). */
static void page_move(BOOL down)
{
    RECT r;
    LINE ln;
    int h = 0, yp;
    DWORD p;
    GetClientRect(g_docwnd, &r);
    for (;;) {
        if (down) {
            layout_band(cpTop, ypTop, &ln);
            if (ln.endMark || h + ln.height > r.bottom - YP0 || !line_next(&ln, &p, &yp))
                break;
        } else {
            if (!line_prev(cpTop, ypTop, &p, &yp))
                break;
            layout_band(p, yp, &ln);
            if (h + ln.height > r.bottom - YP0)
                break;
        }
        h += ln.height;
        cpTop = p;
        ypTop = yp;
    }
}

void view_key(WPARAM vk)
{
    BOOL shift = GetKeyState(VK_SHIFT) < 0, ctrl = GetKeyState(VK_CONTROL) < 0;
    DWORD cp, to, top0;
    LINE ln;
    if (g_selFirst == g_selLim)
        selAnchor = g_selFirst;
    cp = g_selFirst == selAnchor ? g_selLim : g_selFirst;
    to = cp;
    /* the arrows keep the caret's phase: off after typing it stays off
       (WR24 R5: Up after typing), unless the window scrolls; the other
       keys show it (WR18 S5: Ctrl+Home, Page Down, then Down) */
    menuShown = FALSE;
    KillTimer(g_docwnd, TIMER_TYPED);   /* a key at once after typing: on (WR25 A1) */
    if (vk != VK_LEFT && vk != VK_RIGHT && vk != VK_UP && vk != VK_DOWN)
        caretDefer = FALSE;
    top0 = cpTop;
    if (vk == VK_DELETE)
        undo_save(IDS_EDITING);
    else
        undo_end_typing();
    switch (vk) {
    case VK_LEFT:
        if (!shift && g_selFirst != g_selLim)
            to = g_selFirst;
        else
            to = prev_cp(cp);
        break;
    case VK_RIGHT:
        if (!shift && g_selFirst != g_selLim)
            to = g_selLim;
        else
            to = next_cp(cp);
        break;
    case VK_UP:
    case VK_DOWN: {
        DWORD s = line_start(cp);
        int x;
        layout_line(s, &ln);
        x = x_of(&ln, cp);
        if (vk == VK_UP) {
            if (s == 0)
                return;
            to = cp_at_x(line_before(s), x);
        } else {
            if (ln.endMark)
                return;
            to = cp_at_x(ln.cpLim, x);
        }
        break;
    }
    case VK_HOME:
        to = ctrl ? 0 : line_start(cp);
        break;
    case VK_END:
        if (ctrl) {
            to = g_doc.cpMac;
        } else {
            layout_line(line_start(cp), &ln);
            to = ln.cpLim;
            if (ln.paraEnd && !ln.endMark)
                to -= mark_len(to);
            if (to > g_doc.cpMac)
                to = g_doc.cpMac;
        }
        break;
    case VK_DELETE:
        if (g_selLim > g_selFirst) {
            if (shift) {
                cmd_cut_copy(TRUE);
                return;
            }
            doc_delete(&g_doc, g_selFirst, g_selLim - g_selFirst);
        } else if (g_selFirst < g_doc.cpMac) {
            doc_delete(&g_doc, g_selFirst, next_cp(g_selFirst) - g_selFirst);
        }
        g_selLim = selAnchor = g_selFirst;
        view_changed(g_selFirst);
        return;
    case VK_INSERT:
        if (shift)
            cmd_paste();
        else if (ctrl)
            cmd_cut_copy(FALSE);
        return;
    case VK_PRIOR:
    case VK_NEXT:
        page_move(vk == VK_NEXT);
        redraw();
        to = cpTop;
        break;
    default:
        return;
    }
    if (shift) {
        if (to < selAnchor)
            view_select(to, selAnchor, TRUE);
        else
            view_select(selAnchor, to, TRUE);
    } else {
        selAnchor = to;
        view_select(to, to, TRUE);
    }
    if (cpTop != top0 && caretDefer) {
        caretDefer = FALSE;
        caret_place();
    }
}

/* ---- the window ---- */

static void vscroll(WPARAM code, int pos)
{
    LINE ln;
    switch (code) {
    case SB_LINEUP:
        if (!line_prev(cpTop, ypTop, &cpTop, &ypTop))
            return;
        break;
    case SB_LINEDOWN:
        layout_band(cpTop, ypTop, &ln);
        if (ln.endMark || !line_next(&ln, &cpTop, &ypTop))
            return;
        break;
    case SB_PAGEUP:
    case SB_PAGEDOWN:
        page_move(code == SB_PAGEDOWN);
        break;
    case SB_THUMBPOSITION:
        cpTop = line_start((DWORD)pos * g_doc.cpMac / 255);
        if (cpTop > g_doc.cpMac)
            cpTop = line_start(g_doc.cpMac);
        ypTop = 0;
        break;
    default:
        return;
    }
    caret_hide();
    redraw();
    UpdateWindow(g_docwnd);
    caret_place();
}

LRESULT FAR PASCAL __export DocWndProc(HWND w, UINT msg, WPARAM wp, LPARAM lp)
{
    switch (msg) {
    case WM_CREATE: {
        HDC dc = GetDC(w);
        dpiX = GetDeviceCaps(dc, LOGPIXELSX);
        dpiY = GetDeviceCaps(dc, LOGPIXELSY);
        ReleaseDC(w, dc);
        g_docwnd = w;
        return 0;
    }
    case WM_PAINT:
        paint(w);
        return 0;
    case WM_SIZE:
        if (g_doc.paras.h) {
            caret_hide();
            view_layout();
            caret_place();
        }
        return 0;
    case WM_SETFOCUS:
        /* back from a dialog the caret is as it was before the menu
           that led there (view_caret_menu) */
        if (menuShown)
            caretDefer = deferBeforeMenu;
        menuShown = FALSE;
        SetTimer(w, TIMER_CARET, GetCaretBlinkTime(), NULL);
        caret_place();
        return 0;
    case WM_KILLFOCUS:
        if (pmMode)
            pm_end(FALSE);
        KillTimer(w, TIMER_CARET);
        caret_hide();
        caretShown = FALSE;
        return 0;
    case WM_TIMER:
        if (wp == TIMER_TYPED) {
            KillTimer(w, TIMER_TYPED);
            if (!menuShown && !caretDefer) {
                caret_hide();
                caretDefer = TRUE;
            }
            return 0;
        }
        caretDefer = FALSE;
        if (caretShown)
            caret_draw();
        return 0;
    case WM_KEYDOWN:
        if (pmMode)
            pm_key(wp);
        else
            view_key(wp);
        return 0;
    case WM_CHAR:
        if (pmEatChar)
            pmEatChar = FALSE;
        else if (!pmMode)
            view_char(wp);
        return 0;
    case WM_LBUTTONDBLCLK: {
        /* on an object: open it in its server */
        LINE *l = lines();
        int i, yy = YP0;
        for (i = 0; i < nlines; i++) {
            if ((int)HIWORD(lp) >= yy && (int)HIWORD(lp) < yy + l[i].height && l[i].picture) {
                RECT r;
                DWORD f = l[i].cpFirst, li = l[i].cpLim;
                SetRect(&r, l[i].xLeft, yy - l[i].yp, l[i].xLeft + l[i].width, yy - l[i].yp + l[i].picH);
                LocalUnlock(hlines);
                ole_activate(f, li, &r);
                return 0;
            }
            yy += l[i].height;
        }
        LocalUnlock(hlines);
        return 0;
    }
    case WM_LBUTTONDOWN: {
        DWORD cp;
        if (pmMode) {                   /* a click sets the picture */
            pmMouse.x = -32767;
            pm_mouse((int)LOWORD(lp), (int)HIWORD(lp));
            pm_end(TRUE);
            return 0;
        }
        cp = cp_from_point(LOWORD(lp) + xpScroll, HIWORD(lp));
        SetFocus(w);
        undo_end_typing();
        if (wp & MK_SHIFT) {
            view_select(cp < selAnchor ? cp : selAnchor, cp < selAnchor ? selAnchor : cp, FALSE);
        } else {
            selAnchor = cp;
            view_select(cp, cp, FALSE);
        }
        SetCapture(w);
        dragging = TRUE;
        return 0;
    }
    case WM_MOUSEMOVE:
        if (pmMode) {
            pm_mouse((int)LOWORD(lp), (int)HIWORD(lp));
            return 0;
        }
        if (dragging) {
            DWORD cp = cp_from_point((int)LOWORD(lp) + xpScroll, (int)HIWORD(lp));
            view_select(cp < selAnchor ? cp : selAnchor, cp < selAnchor ? selAnchor : cp, FALSE);
        }
        return 0;
    case WM_LBUTTONUP:
        if (dragging) {
            ReleaseCapture();
            dragging = FALSE;
        }
        return 0;
    case WM_DROPFILES:
        return SendMessage(g_frame, msg, wp, lp);
    case WM_VSCROLL:
        vscroll(wp, LOWORD(lp));
        return 0;
    case WM_DESTROY:
        fonts_free();
        return 0;
    }
    return DefWindowProc(w, msg, wp, lp);
}

BOOL view_register(HINSTANCE hi)
{
    WNDCLASS wc;
    m_set(&wc, 0, sizeof wc);
    wc.lpfnWndProc = DocWndProc;
    wc.hInstance = hi;
    wc.hCursor = LoadCursor(NULL, IDC_IBEAM);
    wc.hbrBackground = (HBRUSH)(COLOR_WINDOW + 1);
    wc.lpszClassName = "MSWRITE_DOC";
    wc.style = CS_DBLCLKS;
    if (!RegisterClass(&wc))
        return FALSE;
    wc.lpfnWndProc = PageInfoWndProc;
    wc.hCursor = LoadCursor(NULL, IDC_ARROW);
    wc.hbrBackground = GetStockObject(BLACK_BRUSH);
    wc.lpszClassName = "MSWRITE_PAGEINFO";
    wc.style = 0;
    return RegisterClass(&wc);
}
