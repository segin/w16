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
 * UNVERIFIED: the reference install has no printer driver, so how the
 * original's printed page looks (and its page breaks) could not be
 * observed; headers, footers and Confirm Page Breaks are the plan.
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
} PDEV;

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
                if (ch == '\t')
                    break;
                buf[n++] = ch;
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
    if (page)
        page(pgn, 0);
    for (i = 0; i < g_doc.nparas && !aborted; i++) {
        DWORD pf, pl;
        ATOM32 a;
        PAPX pap;
        para_get(&g_doc, i, &pf, &pl, &a);
        pap_decode(&g_doc, a, &pap);
        if (pf >= g_doc.cpMac && i + 1 == g_doc.nparas)
            break;
        if (pap.fGraphics) {
            PICT pic;
            int h;
            pict_read(pf, pl, &pic);
            h = tw(p, MulDiv(pic.dyaSize, pic.my, 1000), TRUE);
            if (y + h > p->height && y > 0) {
                if (draw && Escape(p->dc, NEWFRAME, 0, NULL, NULL) < 0)
                    return FALSE;
                y = 0;
                if (page)
                    page(++pgn, pf);
            }
            if (draw)
                pict_draw(p->dc, &pic, p->x0 + tw(p, (long)pap.dxaLeft + pic.dxaOffset, FALSE),
                          p->y0 + y, tw(p, MulDiv(pic.dxaSize, pic.mx, 1000), FALSE), h);
            y += h;
            continue;
        }
        for (cp = pf; cp < pl && cp < g_doc.cpMac + (pf == g_doc.cpMac);) {
            int h;
            DWORD next = do_line(p, cp, pf, pl, &pap, p->y0 + y, FALSE, &h);
            if (y + h > p->height && y > 0) {
                if (draw && Escape(p->dc, NEWFRAME, 0, NULL, NULL) < 0)
                    return FALSE;
                y = 0;
                if (page)
                    page(++pgn, cp);
            }
            if (draw)
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
            if (draw && Escape(p->dc, NEWFRAME, 0, NULL, NULL) < 0)
                return FALSE;
            y = 0;
            if (page)
                page(++pgn, pl);
        }
    }
    release_font(p);
    if (draw && !aborted && Escape(p->dc, NEWFRAME, 0, NULL, NULL) < 0)
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

static void pdev_init(PDEV *p, HDC dc)
{
    m_set(p, 0, sizeof *p);
    p->dc = dc;
    p->dpiX = GetDeviceCaps(dc, LOGPIXELSX);
    p->dpiY = GetDeviceCaps(dc, LOGPIXELSY);
}

/* ---- Repaginate ---- */

static void add_page(WORD pgn, DWORD cp)
{
    struct { WORD pgn, pad; DWORD cp; } e;
    if ((g_doc.npages + 1) * 8 > g_doc.pgtb.size &&
        !hm_resize(&g_doc.pgtb, g_doc.pgtb.size * 2 + 64))
        return;
    e.pgn = pgn;
    e.pad = 0;
    e.cp = cp;
    hm_put(&g_doc.pgtb, g_doc.npages * 8, &e, 8);
    g_doc.npages++;
}

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
    pd.Flags = PD_RETURNDC | PD_NOSELECTION | PD_NOPAGENUMS;   /* UNVERIFIED flags */
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
        ok = paginate(&p, TRUE, NULL);
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
