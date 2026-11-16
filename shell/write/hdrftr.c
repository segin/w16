/*
 * hdrftr.c - w16 Write: Document > Header and Footer.
 *
 * Observed (WR12, WR13): the window's title becomes "HEADER" or
 * "FOOTER", the text window shows only that running head (with the end
 * mark after it), and the modeless Page Header / Page Footer box
 * (dialogs 17, 18) offers the distance from the top (bottom), Print on
 * First Page, Insert Page #, Clear and Return to Document. Typing goes
 * to the text. In the file a running head is a paragraph before the body
 * (SDK ch. 8, 8.3.2) whose PAP has rhc 6 (header) or 7 (footer), 8 more
 * for Print on First Page (SDK 8.3.1), the page's margins as its indents
 * (WR13: left and right indent 1800 with 1.25" margins), byte 0 60; a
 * new one goes to the start of the document (WR13: the footer, made
 * second, comes first). Insert Page # puts character 1 in the text, its
 * CHP with byte 3 bit 6 set (SDK: fSpecial, "(page)"; WR13). The SEP is
 * written then (yaHeader, yaFooter).
 *
 * While the box is up, the document being edited is a copy holding only
 * the running head; Return to Document puts it back in place.
 */
#include "write.h"

static DOC saved;                       /* the document while a head is edited */
static DWORD savedFirst, savedLim;
static int mode;                        /* 0, 1 header, 2 footer */
static DWORD headFirst, headLim;        /* its paragraphs in the document */
static BOOL headExisted;
static HWND hfDlg;

int hf_mode(void)
{
    return mode;
}

HWND hf_dialog(void)
{
    return hfDlg;
}

/* the running head's paragraphs [*f, *l) of kind footer (0/1), if any */
static BOOL find_head(DOC *d, BOOL footer, DWORD *f, DWORD *l)
{
    DWORD i, pf, pl;
    ATOM32 a;
    PAPX p;
    BOOL found = FALSE;
    for (i = 0; i + 1 < d->nparas; i++) {
        para_get(d, i, &pf, &pl, &a);
        pap_decode(d, a, &p);
        if (!(p.rhc & 6))
            break;
        if ((p.rhc & 1) == (footer ? 1 : 0)) {
            if (!found)
                *f = pf;
            *l = pl;
            found = TRUE;
        }
    }
    return found;
}

static ATOM32 head_pap(DOC *d, BOOL footer, BOOL firstPage)
{
    PAPX p;
    SEPX s;
    sep_decode(d, &s);
    pap_default(&p);
    p.b0 = 0x3C;
    p.dxaLeft = s.xaLeft;
    p.dxaRight = s.xaMac - s.xaLeft - s.dxaText;
    p.rhc = (BYTE)(6 | (footer ? 1 : 0) | (firstPage ? 8 : 0));
    return pap_atom(d, &p);
}

static void set_title_head(void)
{
    char t[16];
    load_str(mode == 2 ? IDS_FOOTERNAME : IDS_HEADERNAME, t, sizeof t);
    SetWindowText(g_frame, t);
}

static void show_distance(HWND d)
{
    SEPX s;
    long tw;
    char buf[16];
    sep_decode(&saved, &s);
    tw = mode == 2 ? (long)s.yaMac - s.yaFooter : s.yaHeader;
    fmt_measure(buf, tw);
    SetDlgItemText(d, 5, buf);
}

/* Return to Document: the edited head back into the document */
static void hf_return(HWND d)
{
    DOC t;
    DWORD i, cp, at;
    long dist;
    BOOL first = IsDlgButtonChecked(d, 6);
    char buf[32];
    SEPX s;
    t = g_doc;
    g_doc = saved;
    m_set(&saved, 0, sizeof saved);
    /* the fonts the head may have added */
    if (t.nfonts > g_doc.nfonts) {
        FFN f;
        for (i = g_doc.nfonts; i < t.nfonts; i++) {
            hm_get(&t.fonts, i * sizeof f, &f, sizeof f);
            font_code(&g_doc, f.name, f.ffid);
        }
    }
    if (headExisted)
        doc_delete(&g_doc, headFirst, headLim - headFirst);
    at = headExisted ? headFirst : 0;
    if (t.cpMac) {
        /* the text in its runs, then the paragraphs' properties */
        BYTE b[CHP_MAX + 1 + PAP_LEN];
        DWORD n = 0;
        for (cp = 0; cp < t.cpMac;) {
            DWORD rf, rl;
            ATOM32 ra, ga;
            WORD len;
            run_at(&t, cp, &rf, &rl, &ra);
            len = atom_get(&t, ra, b);
            ga = atom_intern(&g_doc, b, len);
            while (cp < rl && cp < t.cpMac) {
                char c[256];
                WORD k = (WORD)(rl - cp > sizeof c ? sizeof c : rl - cp);
                doc_get(&t, cp, c, k);
                doc_insert(&g_doc, at + cp, c, k, ga);
                cp += k;
            }
        }
        if (doc_ch(&t, t.cpMac - 1) != '\n') {
            DWORD rf, rl;
            ATOM32 ra, ga;
            WORD len;
            /* the paragraph mark in the last character's properties, but
               never as a page number (WR9) */
            {
                CHPX c;
                run_at(&t, t.cpMac - 1, &rf, &rl, &ra);
                chp_decode(&t, ra, &c);
                c.special = 0;
                ra = chp_atom(&t, &c);
            }
            len = atom_get(&t, ra, b);
            ga = atom_intern(&g_doc, b, len);
            doc_insert(&g_doc, at + t.cpMac, "\r\n", 2, ga);
            n = 2;
        }
        /* each paragraph of the head gets its properties, with the box's
           Print on First Page */
        for (i = 0; i < t.nparas; i++) {
            DWORD pf, pl, gi, gf, gl;
            ATOM32 pa, ga;
            PAPX p;
            WORD len;
            para_get(&t, i, &pf, &pl, &pa);
            if (pf >= t.cpMac + n && i + 1 == t.nparas)
                break;
            pap_decode(&t, pa, &p);
            p.rhc = (BYTE)((p.rhc & ~8) | (first ? 8 : 0));
            ga = pap_atom(&t, &p);
            len = atom_get(&t, ga, b);
            ga = atom_intern(&g_doc, b, len);
            gi = para_at(&g_doc, at + pf, &gf, &gl, &pa);
            para_set(&g_doc, gi, ga);
        }
    }
    /* the distance into the section */
    sep_decode(&g_doc, &s);
    GetDlgItemText(d, 5, buf, sizeof buf);
    if (parse_measure(buf, &dist, FALSE)) {
        if (mode == 2)
            s.yaFooter = (WORD)(s.yaMac - dist);
        else
            s.yaHeader = (WORD)dist;
    }
    sep_encode(&g_doc, &s);
    doc_free(&t);
    g_doc.dirty = TRUE;
    mode = 0;
    DestroyWindow(hfDlg);
    hfDlg = NULL;
    set_title();
    g_selFirst = savedFirst;
    g_selLim = savedLim;
    view_changed(0);
    view_select(savedFirst, savedLim, TRUE);
    SetFocus(g_docwnd);
    view_caret_now();
}

BOOL CALLBACK __export HeadDlgProc(HWND d, UINT msg, WPARAM wp, LPARAM lp)
{
    (void)lp;
    switch (msg) {
    case WM_INITDIALOG:
        return FALSE;
    case WM_COMMAND:
        switch (wp) {
        case 6:
            CheckDlgButton(d, 6, !IsDlgButtonChecked(d, 6));
            return TRUE;
        case 3: {                       /* Insert Page #: "(page)" */
            CHPX c;
            sel_props(&c);
            c.special = 1;              /* byte 0 as it was (WR9) */
            {
                DWORD cp = g_selFirst;
                if (g_selLim > g_selFirst)
                    doc_delete(&g_doc, g_selFirst, g_selLim - g_selFirst);
                doc_insert(&g_doc, cp, "\1", 1, chp_atom(&g_doc, &c));
                g_selFirst = g_selLim = cp + 1;
                view_changed(cp);
            }
            return TRUE;
        }
        case 4:                         /* Clear */
            doc_delete(&g_doc, 0, g_doc.cpMac);
            g_selFirst = g_selLim = 0;
            view_changed(0);
            return TRUE;
        case IDOK:
        case IDCANCEL:
            hf_return(d);
            return TRUE;
        }
        break;
    }
    return FALSE;
}

void cmd_header(BOOL footer)
{
    static FARPROC fp;
    DOC t;
    DWORD f = 0, l = 0;
    ATOM32 pa;
    BOOL firstPage = FALSE;
    if (mode)
        return;
    headExisted = find_head(&g_doc, footer, &f, &l);
    headFirst = f;
    headLim = l;
    if (!doc_copy(&t, &g_doc))
        return;
    if (headExisted) {
        DWORD pf, pl;
        PAPX p;
        para_at(&t, f, &pf, &pl, &pa);
        pap_decode(&t, pa, &p);
        firstPage = (p.rhc & 8) != 0;
        doc_delete(&t, l, t.cpMac - l);
        doc_delete(&t, 0, f);
        if (t.cpMac >= 2 && doc_ch(&t, t.cpMac - 1) == '\n')
            doc_delete(&t, t.cpMac - 2, 2);
    } else {
        doc_delete(&t, 0, t.cpMac);
        pa = head_pap(&t, footer, FALSE);
    }
    para_set(&t, t.nparas - 1, pa);
    saved = g_doc;
    savedFirst = g_selFirst;
    savedLim = g_selLim;
    g_doc = t;
    mode = footer ? 2 : 1;
    set_title_head();
    view_reset();
    if (!fp)
        fp = MakeProcInstance((FARPROC)HeadDlgProc, g_hinst);
    hfDlg = CreateDialog(g_hinst, MAKEINTRESOURCE(footer ? IDD_FOOTER : IDD_HEADER), g_frame,
                         (DLGPROC)fp);
    show_distance(hfDlg);
    CheckDlgButton(hfDlg, 6, firstPage);
    SetFocus(g_docwnd);
}
