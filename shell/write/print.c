/*
 * print.c - w16 Write: Print, Repaginate.
 *
 * Pages are the section's text area (Page Layout's margins) at the
 * device's resolution: lines are broken with the device's own fonts
 * (GetTextExtent on that DC), and a page holds the lines that fit in the
 * text height; a picture paragraph goes to the next page when it does
 * not fit. Repaginate fills the page table with the first character of
 * each page (as README.WRI's PGTB {1, 0}), numbered from the first page
 * number; it uses the printer when one is set up (WIN.INI device=),
 * else the screen. Print sends the pages with STARTDOC, NEWFRAME and
 * ENDDOC escapes (the original imports Escape, not StartDoc), the
 * "Now Printing" box (dialog 6) cancelling through an abort procedure.
 *
 * Checked against 3.1 with the Generic / Text Only driver from the Setup
 * disks on a file port (WR29: the bytes it writes are identical): pages,
 * a page break, running heads with the page number, the first-page
 * option, the page table printing fills. Other printers UNVERIFIED;
 * Confirm Page Breaks is the plan.
 */
#include "write.h"
#include <commdlg.h>

typedef struct {
    HDC dc;
    int dpiX, dpiY;
    int width, height;                  /* text area, device pixels */
    int x0, y0;                         /* its top left on the page */
    HFONT font, oldFont;
    CHPX cur;
    BOOL haveFont;
    WORD pgn;                           /* the page being drawn */
    WORD from, to;                      /* Print's page range */
} PDEV;

/* drawing this page: Print's range (UNVERIFIED against 3.1: WR34) */
#define DRAWN (draw && p->pgn >= p->from && p->pgn <= p->to)

static BOOL aborted;
static HWND printDlg;

static void select_chp(PDEV *p, const CHPX *c)
{
    LOGFONT lf;
    FFN FAR *f;
    if (p->haveFont && !m_cmp(&p->cur, c, sizeof *c))
        return;
    m_set(&lf, 0, sizeof lf);
    lf.lfHeight = -MulDiv(c->hps, p->dpiY, 144);
    lf.lfWeight = c->bold ? FW_BOLD : FW_NORMAL;
    lf.lfItalic = c->italic;
    lf.lfUnderline = c->underline;
    f = font_get(&g_doc, c->ftc);
    lf.lfPitchAndFamily = f->ffid;
    s_cpy(lf.lfFaceName, f->name, LF_FACESIZE);
    if (p->font) {
        SelectObject(p->dc, p->oldFont);
        DeleteObject(p->font);
    }
    p->font = CreateFontIndirect(&lf);
    p->oldFont = SelectObject(p->dc, p->font);
    p->cur = *c;
    p->haveFont = TRUE;
}

static void release_font(PDEV *p)
{
    if (p->font) {
        SelectObject(p->dc, p->oldFont);
        DeleteObject(p->font);
    }
    p->font = NULL;
    p->haveFont = FALSE;
}

static int tw(PDEV *p, long t, BOOL vert)
{
    return (int)(t * (vert ? p->dpiY : p->dpiX) / 1440);
}

/* the width of the run text [c, c + n) in the current font */
static int extent(PDEV *p, DWORD c, WORD n)
{
    char buf[128];
    int w = 0;
    while (n) {
        WORD k = n > sizeof buf ? sizeof buf : n;
        doc_get(&g_doc, c, buf, k);
        w += LOWORD(GetTextExtent(p->dc, buf, k));
        c += k;
        n -= k;
    }
    return w;
}

/* One line from cp in paragraph [pf, pl): its end, height, ascent;
   drawn at (x0 + indent, y) when draw. Breaks after the last blank that
   fits, or inside a word that does not. */
static DWORD do_line(PDEV *p, DWORD cp, DWORD pf, DWORD pl, const PAPX *pap, int y, BOOL draw,
                     int *height)
{
    int xs = tw(p, (long)pap->dxaLeft + (cp == pf ? pap->dxaLeft1 : 0), FALSE);
    int xmax = p->width - tw(p, pap->dxaRight, FALSE), x, asc = 0, desc = 0;
    DWORD c, lim = pl > g_doc.cpMac ? g_doc.cpMac : pl, brk = 0, end;
    TEXTMETRIC tm;
    if (xs < 0)
        xs = 0;
    x = xs;
    for (c = cp; c < lim;) {
        DWORD rf, rl;
        ATOM32 ra;
        CHPX chp;
        BYTE ch = doc_ch(&g_doc, c);
        int w;
        if (ch == '\r' || ch == '\n' || ch == 12) {
            c = c + 1 < lim && ch == '\r' ? c + 2 : c + 1;
            brk = 0;
            break;
        }
        run_at(&g_doc, c, &rf, &rl, &ra);
        chp_decode(&g_doc, ra, &chp);
        select_chp(p, &chp);
        GetTextMetrics(p->dc, &tm);
        if (tm.tmAscent + tm.tmExternalLeading > asc)
            asc = tm.tmAscent + tm.tmExternalLeading;
        if (tm.tmDescent > desc)
            desc = tm.tmDescent;
        if (ch == '\t') {
            int t = tw(p, 720, FALSE);
            w = (x / t + 1) * t - x;
        } else if (ch == 1) {
            char n[8];
            s_utoa(n, p->pgn);
            w = LOWORD(GetTextExtent(p->dc, n, s_len(n)));
        } else {
            w = extent(p, c, 1);
        }
        if (x + w > xmax && c > cp && ch != ' ') {
            if (brk)
                c = brk;
            break;
        }
        x += w;
        c++;
        if (ch == ' ')
            brk = c;
    }
    if (!asc) {                         /* an empty line */
        CHPX chp;
        DWORD rf, rl;
        ATOM32 ra;
        run_at(&g_doc, cp ? cp - 1 : 0, &rf, &rl, &ra);
        chp_decode(&g_doc, ra, &chp);
        select_chp(p, &chp);
        GetTextMetrics(p->dc, &tm);
        asc = tm.tmAscent + tm.tmExternalLeading;
        desc = tm.tmDescent;
    }
    end = c;
    *height = asc + desc;
    if (pap->dyaLine > 240 && tw(p, pap->dyaLine, TRUE) > *height)
        *height = tw(p, pap->dyaLine, TRUE);
    if (draw) {
        int xx = p->x0 + xs, base = y + *height - desc;
        DWORD e = end;
        if (pap->jc == 1 || pap->jc == 2) {
            int wl = x - xs;
            xx += pap->jc == 1 ? (xmax - xs - wl) / 2 : xmax - xs - wl;
        }
        while (e > cp && (doc_ch(&g_doc, e - 1) == '\n' || doc_ch(&g_doc, e - 1) == '\r'))
            e--;
        for (c = cp; c < e;) {
            DWORD rf, rl;
            ATOM32 ra;
            CHPX chp;
            WORD n = 0;
            char buf[128];
            run_at(&g_doc, c, &rf, &rl, &ra);
            chp_decode(&g_doc, ra, &chp);
            select_chp(p, &chp);
            GetTextMetrics(p->dc, &tm);
            while (c + n < e && c + n < rl && n < sizeof buf) {
                BYTE ch = doc_ch(&g_doc, c + n);
                if (ch == '\t' || ch == 1 || ch == 12)
                    break;
                buf[n++] = ch;
            }
            if (!n && doc_ch(&g_doc, c) == 12) {
                c++;                    /* a page break: no ink */
                continue;
            }
            if (!n && doc_ch(&g_doc, c) == 1) {
                /* a running head's page number (WR29: "Head-2") */
                char num[8];
                int up = MulDiv(chp.hpsPos, p->dpiY, 144);
                s_utoa(num, p->pgn);
                TextOut(p->dc, xx, base - tm.tmAscent - up, num, s_len(num));
                xx += LOWORD(GetTextExtent(p->dc, num, s_len(num)));
                c++;
                continue;
            }
            if (n) {
                int up = MulDiv(chp.hpsPos, p->dpiY, 144);
                TextOut(p->dc, xx, base - tm.tmAscent - up, buf, n);
                xx += LOWORD(GetTextExtent(p->dc, buf, n));
                c += n;
            } else {
                int t = tw(p, 720, FALSE), rel = xx - p->x0;
                xx = p->x0 + (rel / t + 1) * t;
                c++;
            }
        }
    }
    return end;
}

/* The running heads on page pgn (WR29, Generic / Text Only): the header's
   top yaHeader from the page's top, the footer's bottom yaFooter from its
   bottom; their indents count from the page's edges; neither on the
   first page unless its paragraph says so (rhc bit 3, SDK ch. 8); the
   page number (character 1) printed as the number. */
static void running_heads(PDEV *p, const SEPX *s, BOOL first)
{
    DWORD i;
    int pass;
    PDEV q = *p;
    q.x0 = 0;
    q.width = tw(p, s->xaMac, FALSE);
    for (pass = 0; pass < 2; pass++) {
        int y, h, total = 0, lineH = 0;
        DWORD pf, pl, cp;
        for (i = 0; i < g_doc.nparas; i++) {
            ATOM32 a;
            PAPX pap;
            para_get(&g_doc, i, &pf, &pl, &a);
            pap_decode(&g_doc, a, &pap);
            if (!(pap.rhc & 6))
                break;
            if ((pap.rhc & 1) != pass || (first && !(pap.rhc & 8)))
                continue;
            for (cp = pf; cp < pl && cp < g_doc.cpMac;) {
                DWORD next = do_line(&q, cp, pf, pl, &pap, 0, FALSE, &h);
                total += h;
                if (!lineH)
                    lineH = h;
                if (next <= cp)
                    break;
                cp = next;
            }
        }
        /* (yaFooter counts from the page's top: 14760 for 0.75") */
        y = pass ? tw(p, s->yaFooter, TRUE) - total : tw(p, s->yaHeader, TRUE);
        /* a line lower than its distance says (WR29, Generic / Text
           Only at 6 lines an inch: the header at 0.75" prints on line
           5, the footer, bottom at 0.75" from the bottom, on line 61;
           at the distance itself TTY puts them on lines 4 and 60, half a
           line lower changes nothing; UNVERIFIED: the rule on other
           printers) */
        y += lineH;
        for (i = 0; i < g_doc.nparas; i++) {
            ATOM32 a;
            PAPX pap;
            para_get(&g_doc, i, &pf, &pl, &a);
            pap_decode(&g_doc, a, &pap);
            if (!(pap.rhc & 6))
                break;
            if ((pap.rhc & 1) != pass || (first && !(pap.rhc & 8)))
                continue;
            for (cp = pf; cp < pl && cp < g_doc.cpMac;) {
                DWORD next = do_line(&q, cp, pf, pl, &pap, y, TRUE, &h);
                y += h;
                if (next <= cp)
                    break;
                cp = next;
            }
        }
    }
    p->font = q.font;
    p->oldFont = q.oldFont;
    p->cur = q.cur;
    p->haveFont = q.haveFont;
}

/* Lay the document out in pages; page(n, cpFirst) is told each page's
   start; with draw, the pages are drawn too (and framed with NEWFRAME). */
static BOOL paginate(PDEV *p, BOOL draw, void (*page)(WORD, DWORD))
{
    SEPX s;
    DWORD i, cp;
    int y = 0;
    WORD pgn;
    sep_decode(&g_doc, &s);
    p->width = tw(p, s.dxaText, FALSE);
    p->height = tw(p, s.dyaText, TRUE);
    p->x0 = tw(p, s.xaLeft, FALSE);
    p->y0 = tw(p, s.yaTop, TRUE);
    pgn = s.pgnFirst ? s.pgnFirst : 1;
    p->pgn = pgn;
    if (page) {
        /* page 1 starts with the body, after the running heads (WR29) */
        DWORD body = 0, f, l;
        ATOM32 a;
        PAPX q;
        for (i = 0; i < g_doc.nparas; i++) {
            para_get(&g_doc, i, &f, &l, &a);
            pap_decode(&g_doc, a, &q);
            body = f;
            if (!(q.rhc & 6))
                break;
        }
        page(pgn, body);
    }
    if (DRAWN)
        running_heads(p, &s, TRUE);
    for (i = 0; i < g_doc.nparas && !aborted; i++) {
        DWORD pf, pl;
        ATOM32 a;
        PAPX pap;
        para_get(&g_doc, i, &pf, &pl, &a);
        pap_decode(&g_doc, a, &pap);
        if (pf >= g_doc.cpMac && i + 1 == g_doc.nparas)
            break;
        if (pap.rhc & 6)
            continue;                   /* running heads: on every page */
        if (pap.fGraphics) {
            PICT pic;
            int h;
            pict_read(pf, pl, &pic);
            h = tw(p, MulDiv(pic.dyaSize, pic.my, 1000), TRUE);
            if (y + h > p->height && y > 0) {
                if (DRAWN && Escape(p->dc, NEWFRAME, 0, NULL, NULL) < 0)
                    return FALSE;
                y = 0;
                p->pgn = ++pgn;
                if (page)
                    page(pgn, pf);
                if (DRAWN)
                    running_heads(p, &s, FALSE);
            }
            if (DRAWN)
                pict_draw(p->dc, &pic, p->x0 + tw(p, (long)pap.dxaLeft + pic.dxaOffset, FALSE),
                          p->y0 + y, tw(p, MulDiv(pic.dxaSize, pic.mx, 1000), FALSE), h);
            y += h;
            continue;
        }
        for (cp = pf; cp < pl && cp < g_doc.cpMac + (pf == g_doc.cpMac);) {
            int h;
            DWORD next = do_line(p, cp, pf, pl, &pap, p->y0 + y, FALSE, &h);
            if (y + h > p->height && y > 0) {
                if (DRAWN && Escape(p->dc, NEWFRAME, 0, NULL, NULL) < 0)
                    return FALSE;
                y = 0;
                p->pgn = ++pgn;
                if (page)
                    page(pgn, cp);
                if (DRAWN)
                    running_heads(p, &s, FALSE);
            }
            if (DRAWN)
                do_line(p, cp, pf, pl, &pap, p->y0 + y, TRUE, &h);
            y += h;
            if (next <= cp)
                break;
            cp = next;
        }
        /* a page break (character 12) ends its paragraph: the next one
           starts a page (UNVERIFIED against 3.1, which has no printer
           in the reference install) */
        if (pl > pf && pl <= g_doc.cpMac && doc_ch(&g_doc, pl - 1) == 12 && pl < g_doc.cpMac) {
            if (DRAWN && Escape(p->dc, NEWFRAME, 0, NULL, NULL) < 0)
                return FALSE;
            y = 0;
            p->pgn = ++pgn;
            if (page)
                page(pgn, pl);
            if (DRAWN)
                running_heads(p, &s, FALSE);
        }
    }
    release_font(p);
    if (DRAWN && !aborted && Escape(p->dc, NEWFRAME, 0, NULL, NULL) < 0)
        return FALSE;
    return TRUE;
}

/* ---- the printer ---- */

static HDC printer_dc(void)
{
    char dev[128], *drv, *port;
    int i;
    GetProfileString("windows", "device", "", dev, sizeof dev);
    for (i = 0, drv = port = NULL; dev[i]; i++)
        if (dev[i] == ',') {
            dev[i] = 0;
            if (!drv)
                drv = dev + i + 1;
            else if (!port)
                port = dev + i + 1;
        }
    if (!dev[0] || !drv || !port)
        return NULL;
    return CreateDC(drv, dev, port, NULL);
}

/* an information context for the printer set up in WIN.INI, or NULL */
HDC printer_ic(void)
{
    char dev[128], *drv, *port;
    int i;
    GetProfileString("windows", "device", "", dev, sizeof dev);
    for (i = 0, drv = port = NULL; dev[i]; i++)
        if (dev[i] == ',') {
            dev[i] = 0;
            if (!drv)
                drv = dev + i + 1;
            else if (!port)
                port = dev + i + 1;
        }
    if (!dev[0] || !drv || !port)
        return NULL;
    return CreateIC(drv, dev, port, NULL);
}

/* A new document's font: Arial, as the printer realizes it. A printer
   without it gives its own face (WR29: Generic / Text Only makes the
   font table "Roman 10cpi", family 30h, and the screen shows it so);
   with no printer, Arial (WR1). */
void printer_font(char *face, BYTE *ffid)
{
    char dev[128], *drv, *port;
    int i;
    HDC ic;
    s_cpy(face, "Arial", LF_FACESIZE);
    *ffid = FF_SWISS;
    GetProfileString("windows", "device", "", dev, sizeof dev);
    for (i = 0, drv = port = NULL; dev[i]; i++)
        if (dev[i] == ',') {
            dev[i] = 0;
            if (!drv)
                drv = dev + i + 1;
            else if (!port)
                port = dev + i + 1;
        }
    if (!dev[0] || !drv || !port)
        return;
    ic = CreateIC(drv, dev, port, NULL);
    if (ic) {
        LOGFONT lf;
        TEXTMETRIC tm;
        HFONT f, o;
        char got[LF_FACESIZE + 1];
        m_set(&lf, 0, sizeof lf);
        lf.lfHeight = -MulDiv(20, GetDeviceCaps(ic, LOGPIXELSY), 144);
        lf.lfPitchAndFamily = FF_SWISS;
        s_cpy(lf.lfFaceName, "Arial", LF_FACESIZE);
        f = CreateFontIndirect(&lf);
        o = SelectObject(ic, f);
        GetTextFace(ic, sizeof got, got);
        GetTextMetrics(ic, &tm);
        SelectObject(ic, o);
        DeleteObject(f);
        DeleteDC(ic);
        if (got[0] && s_icmp(got, "Arial")) {
            s_cpy(face, got, LF_FACESIZE);
            *ffid = (BYTE)(tm.tmPitchAndFamily & 0xF0);
        }
    }
}

static void pdev_init(PDEV *p, HDC dc)
{
    m_set(p, 0, sizeof *p);
    p->dc = dc;
    p->dpiX = GetDeviceCaps(dc, LOGPIXELSX);
    p->dpiY = GetDeviceCaps(dc, LOGPIXELSY);
    p->from = 1;
    p->to = 0xFFFF;
}

/* ---- Repaginate ---- */

static void add_page(WORD pgn, DWORD cp)
{
    struct { WORD pgn, pad; DWORD cp; } e;
    /* the table's room grows by tens when Write paginates (WR29: 2
       pages, room 10; files it loads keep theirs; UNVERIFIED past 10) */
    if (g_doc.npages + 1 > g_doc.pgtbMax)
        g_doc.pgtbMax = (WORD)((g_doc.npages + 10) / 10 * 10);
    if ((g_doc.npages + 1) * 8 > g_doc.pgtb.size &&
        !hm_resize(&g_doc.pgtb, g_doc.pgtb.size * 2 + 64))
        return;
    e.pgn = pgn;
    e.pad = 0;
    e.cp = cp;
    hm_put(&g_doc.pgtb, g_doc.npages * 8, &e, 8);
    g_doc.npages++;
}

/* ---- Confirm Page Breaks (WR31) ----
   With Repaginate's "Confirm Page Breaks" each break is shown before it is
   kept: the window shows the first line of the coming page, selected, the
   page mark beside it, and "Repaginating Document" (dialog 9) asks; Up and
   Down move the break a line (not above the page's first line, not below
   where it falls), Confirm keeps it: moved, it becomes a page break
   character (12) of its own paragraph before that line (WR31: "line 52",
   then character 12, then "line 53"); Cancel ends repagination there. A
   page break already in the text (character 12) is selected and dialog 10
   asks "Would you like to keep this existing page break?": Keep, Remove
   (the character deleted), Cancel. Afterwards the page table holds the
   pages as confirmed. UNVERIFIED: Down past a moved break, a page that
   starts with a picture. */

static DWORD cbProposed, cbFirst, cbCand;  /* the break, its page's start, the candidate */
static int cbEntry;                         /* its page table entry */

static void cb_show(void)
{
    struct { WORD pgn, pad; DWORD cp; } e;
    hm_get(&g_doc.pgtb, (DWORD)cbEntry * 8, &e, 8);
    e.cp = cbCand;
    hm_put(&g_doc.pgtb, (DWORD)cbEntry * 8, &e, 8);
    /* only the pages so far, so the page box and marks show them (WR31) */
    g_doc.npages = (DWORD)cbEntry + 1;
    view_show_line(cbCand);
    view_select(cbCand, view_line_end(cbCand), FALSE);
    InvalidateRect(g_docwnd, NULL, TRUE);
    UpdateWindow(g_docwnd);
}

BOOL CALLBACK __export ConfirmBreakDlgProc(HWND d, UINT msg, WPARAM wp, LPARAM lp)
{
    (void)lp;
    if (msg == WM_INITDIALOG)
        return TRUE;
    if (msg == WM_COMMAND) {
        if (wp == 5) {                  /* Up */
            DWORD c = view_line_before(cbCand);
            if (c > cbFirst && c < cbCand) {
                cbCand = c;
                cb_show();
            }
            return TRUE;
        }
        if (wp == 6) {                  /* Down */
            DWORD c = view_line_after(cbCand);
            if (c > cbCand && c <= cbProposed) {
                cbCand = c;
                cb_show();
            }
            return TRUE;
        }
        if (wp == IDOK || wp == IDCANCEL) {
            EndDialog(d, wp);
            return TRUE;
        }
    }
    return FALSE;
}

BOOL CALLBACK __export KeepBreakDlgProc(HWND d, UINT msg, WPARAM wp, LPARAM lp)
{
    (void)lp;
    if (msg == WM_INITDIALOG)
        return TRUE;
    if (msg == WM_COMMAND && (wp == IDOK || wp == IDCANCEL || wp == 4)) {
        EndDialog(d, wp);
        return TRUE;
    }
    return FALSE;
}

static int cb_dialog(WORD id, FARPROC proc)
{
    FARPROC fp = MakeProcInstance(proc, g_hinst);
    int r = DialogBox(g_hinst, MAKEINTRESOURCE(id), g_frame, (DLGPROC)fp);
    FreeProcInstance(fp);
    return r;
}

static void repag_confirm(PDEV *p)
{
    DWORD done = 0, i;
    for (;;) {
        struct { WORD pgn, pad; DWORD cp; } e, prev;
        int r;
        g_doc.npages = 0;
        aborted = FALSE;
        paginate(p, FALSE, add_page);
        for (i = 1; i < g_doc.npages; i++) {
            hm_get(&g_doc.pgtb, i * 8, &e, 8);
            if (e.cp > done)
                break;
        }
        if (i >= g_doc.npages)
            return;
        hm_get(&g_doc.pgtb, (i - 1) * 8, &prev, 8);
        view_changed(0);
        if (e.cp > 0 && doc_ch(&g_doc, e.cp - 1) == 12) {
            /* a page break in the text */
            g_doc.npages = i;
            view_show_line(e.cp - 1);
            view_select(e.cp - 1, e.cp, FALSE);
            view_update_pageinfo();
            InvalidateRect(g_docwnd, NULL, TRUE);
            UpdateWindow(g_docwnd);
            r = cb_dialog(IDD_KEEPBREAK, (FARPROC)KeepBreakDlgProc);
            if (r == IDCANCEL)
                return;
            if (r == 4) {
                undo_save(IDS_EDITING);
                doc_delete(&g_doc, e.cp - 1, 1);
                g_doc.dirty = TRUE;
                done = e.cp - 1;
            } else {
                done = e.cp;
            }
            continue;
        }
        cbProposed = cbCand = e.cp;
        cbFirst = prev.cp;
        cbEntry = (int)i;
        cb_show();
        r = cb_dialog(IDD_CONFIRMBREAK, (FARPROC)ConfirmBreakDlgProc);
        if (r == IDCANCEL)
            return;
        if (cbCand != cbProposed) {
            CHPX c;
            DWORD rf, rl;
            ATOM32 ra;
            undo_save(IDS_EDITING);
            run_at(&g_doc, cbCand, &rf, &rl, &ra);
            chp_decode(&g_doc, ra, &c);
            doc_insert(&g_doc, cbCand, "\f", 1, chp_atom(&g_doc, &c));
            g_doc.dirty = TRUE;
            done = cbCand + 1;
        } else {
            done = cbProposed;
        }
    }
}

static BOOL confirmBreaks;

BOOL CALLBACK __export RepagDlgProc(HWND d, UINT msg, WPARAM wp, LPARAM lp)
{
    (void)lp;
    if (msg == WM_INITDIALOG)
        return TRUE;
    if (msg == WM_COMMAND) {
        if (wp == 3) {
            CheckDlgButton(d, 3, !IsDlgButtonChecked(d, 3));
            return TRUE;
        }
        if (wp == IDOK || wp == IDCANCEL) {
            confirmBreaks = IsDlgButtonChecked(d, 3);
            EndDialog(d, wp);
            return TRUE;
        }
    }
    return FALSE;
}

void cmd_repaginate(void)
{
    FARPROC fp = MakeProcInstance((FARPROC)RepagDlgProc, g_hinst);
    int r = DialogBox(g_hinst, MAKEINTRESOURCE(IDD_REPAGINATE), g_frame, (DLGPROC)fp);
    PDEV p;
    HDC dc;
    BOOL screen = FALSE;
    FreeProcInstance(fp);
    if (r != IDOK)
        return;
    dc = printer_dc();
    if (!dc) {
        dc = GetDC(g_docwnd);
        screen = TRUE;
    }
    pdev_init(&p, dc);
    g_doc.npages = 0;
    aborted = FALSE;
    if (confirmBreaks) {
        /* afterwards the selection is as before (WR31 C5: the page break
           kept is no longer selected); breaks added before it move it */
        DWORD f = g_selFirst, l = g_selLim, mac = g_doc.cpMac;
        repag_confirm(&p);
        f += g_doc.cpMac - mac;
        l += g_doc.cpMac - mac;
        view_select(f, l, FALSE);
    } else
        paginate(&p, FALSE, add_page);
    if (screen)
        ReleaseDC(g_docwnd, dc);
    else
        DeleteDC(dc);
    g_doc.dirty = TRUE;
    view_changed(0);
}

/* ---- Print ---- */

BOOL CALLBACK __export AbortProc(HDC dc, int code)
{
    MSG m;
    (void)dc;
    (void)code;
    while (!aborted && PeekMessage(&m, NULL, 0, 0, PM_REMOVE))
        if (!printDlg || !IsDialogMessage(printDlg, &m)) {
            TranslateMessage(&m);
            DispatchMessage(&m);
        }
    return !aborted;
}

BOOL CALLBACK __export PrintingDlgProc(HWND d, UINT msg, WPARAM wp, LPARAM lp)
{
    (void)lp;
    if (msg == WM_INITDIALOG) {
        char name[PATH_MAX_DOS];
        if (g_path[0])
            s_cpy(name, g_path, sizeof name);
        else
            load_str(IDS_UNTITLED, name, sizeof name);
        SetDlgItemText(d, 100, name);
        return TRUE;
    }
    if (msg == WM_COMMAND && wp == IDCANCEL) {
        aborted = TRUE;
        return TRUE;
    }
    return FALSE;
}

void cmd_print(void)
{
    PRINTDLG pd;
    PDEV p;
    FARPROC fpAbort, fpDlg;
    char title[PATH_MAX_DOS + 8];
    BOOL ok;
    m_set(&pd, 0, sizeof pd);
    pd.lStructSize = sizeof pd;
    pd.hwndOwner = g_frame;
    /* Pages enabled, From 1 To 1 (WR29); Selection without one grayed */
    pd.Flags = PD_RETURNDC | (g_selFirst == g_selLim ? PD_NOSELECTION : 0);
    pd.nFromPage = pd.nToPage = pd.nMinPage = 1;
    pd.nMaxPage = 32767;
    pd.nCopies = 1;
    if (!PrintDlg(&pd)) {
        if (CommDlgExtendedError())
            msgbox(g_frame, IDS_CANTPRINT, NULL, MB_OK | MB_ICONEXCLAMATION);
        return;
    }
    if (!pd.hDC) {
        msgbox(g_frame, IDS_CANTPRINT, NULL, MB_OK | MB_ICONEXCLAMATION);
        return;
    }
    aborted = FALSE;
    fpAbort = MakeProcInstance((FARPROC)AbortProc, g_hinst);
    fpDlg = MakeProcInstance((FARPROC)PrintingDlgProc, g_hinst);
    EnableWindow(g_frame, FALSE);
    printDlg = CreateDialog(g_hinst, MAKEINTRESOURCE(IDD_PRINTING), g_frame, (DLGPROC)fpDlg);
    Escape(pd.hDC, SETABORTPROC, 0, (LPSTR)fpAbort, NULL);
    load_str(IDS_WRITE, title, sizeof title);
    s_cat(title, " - ", sizeof title);
    s_cat(title, g_path[0] ? g_path : "(Untitled)", sizeof title);
    ok = Escape(pd.hDC, STARTDOC, s_len(title), title, NULL) > 0;
    if (ok) {
        pdev_init(&p, pd.hDC);
        if (pd.Flags & PD_PAGENUMS) {
            p.from = pd.nFromPage;
            p.to = pd.nToPage;
        }
        /* printing every page fills the page table too (WR29: PGTB {1, 14}
           {2, 18}); a page range leaves it as it was (WR34) */
        if (pd.Flags & PD_PAGENUMS) {
            ok = paginate(&p, TRUE, NULL);
        } else {
            g_doc.npages = 0;
            ok = paginate(&p, TRUE, add_page);
            g_doc.dirty = TRUE;
        }
        if (!aborted)
            Escape(pd.hDC, ENDDOC, 0, NULL, NULL);
    }
    EnableWindow(g_frame, TRUE);
    if (printDlg)
        DestroyWindow(printDlg);
    printDlg = NULL;
    FreeProcInstance(fpAbort);
    FreeProcInstance(fpDlg);
    DeleteDC(pd.hDC);
    if (pd.hDevMode)
        GlobalFree(pd.hDevMode);
    if (pd.hDevNames)
        GlobalFree(pd.hDevNames);
    if (!ok && !aborted)
        msgbox(g_frame, IDS_CANTPRINT, NULL, MB_OK | MB_ICONEXCLAMATION);
}

void cmd_printsetup(void)
{
    PRINTDLG pd;
    m_set(&pd, 0, sizeof pd);
    pd.lStructSize = sizeof pd;
    pd.hwndOwner = g_frame;
    pd.Flags = PD_PRINTSETUP;
    if (PrintDlg(&pd)) {
        if (pd.hDevMode)
            GlobalFree(pd.hDevMode);
        if (pd.hDevNames)
            GlobalFree(pd.hDevNames);
    }
}
