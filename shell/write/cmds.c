/*
 * cmds.c - w16 Write: the Character, Paragraph, Document and Find menus,
 * their dialogs, and the clipboard.
 *
 * What each command stores is the original's (WR3: Bold etc. on an
 * insertion point set the properties for typing; Regular sets CHP byte 0
 * to 1, any other character format to 0; paragraph formats on the last
 * paragraph first give it a paragraph mark; WR4: Tabs apply to every
 * paragraph; Indents and alignment to the selected ones).
 */
#include "write.h"
#include <commdlg.h>

/* ---- Undo: one level, a copy of the document taken before the change
   (typing: before the first character of a run of typing); Undo swaps
   it with the document, so a second Undo redoes. The Edit menu names the
   kind: "&Undo Typing", "&Undo Editing", "&Undo Formatting" (the
   original's strings; WR12 saw "Undo Typing" after typing). UNVERIFIED:
   which commands are which kind beyond typing. ---- */

static DOC undoDoc;
static WORD undoKind;                   /* 0: nothing to undo */
static DWORD undoFirst, undoLim;
static BOOL typingRun;

void undo_save(WORD kind)
{
    if (kind == IDS_TYPING && typingRun && undoKind == IDS_TYPING)
        return;
    doc_free(&undoDoc);
    undoKind = doc_copy(&undoDoc, &g_doc) ? kind : 0;
    undoFirst = g_selFirst;
    undoLim = g_selLim;
    typingRun = kind == IDS_TYPING;
}

void undo_end_typing(void)
{
    typingRun = FALSE;
}

void undo_clear(void)
{
    doc_free(&undoDoc);
    undoKind = 0;
    typingRun = FALSE;
}

void cmd_undo(void)
{
    DOC t;
    DWORD f = g_selFirst, l = g_selLim;
    if (!undoKind)
        return;
    t = g_doc;
    g_doc = undoDoc;
    undoDoc = t;
    g_doc.dirty = TRUE;
    typingRun = FALSE;
    g_selFirst = g_selLim = 0;
    view_changed(0);
    view_select(undoFirst, undoLim, TRUE);
    undoFirst = f;
    undoLim = l;
}

/* ---- character formatting ---- */

/* Reduce and Enlarge Font step through a list of sizes: observed 8, 10,
   12, 14, 18, 24 points (WR3, WR8: 10 -> 12 -> 14 -> 18 -> 24, 12 -> 10
   -> 8). UNVERIFIED: the sizes below 8 and above 24. Super- and
   subscript take the next smaller size (10 -> 8, 24 -> 18) and position
   +-4 half points; turning them off and Regular take the next larger. */
static const BYTE sizes[] = { 4, 6, 8, 10, 12, 14, 18, 24, 30, 36, 48, 60, 72, 96, 127 };

static BYTE size_step(BYTE hps, int dir)
{
    int i, pt = hps / 2;
    if (dir > 0) {
        for (i = 0; i < sizeof sizes; i++)
            if (sizes[i] > pt)
                return (BYTE)(sizes[i] * 2);
        return hps;
    }
    for (i = sizeof sizes; i-- > 0;)
        if (sizes[i] < pt || (sizes[i] == pt && hps & 1))
            return (BYTE)(sizes[i] * 2);
    return hps;
}

static ATOM32 chp_apply(DOC *d, ATOM32 a, WORD id)
{
    CHPX c;
    chp_decode(d, a, &c);
    switch (id) {
    case IDM_REGULAR:
        if (c.hpsPos)
            c.hps = size_step(c.hps, 1);
        c.bold = c.italic = c.underline = 0;
        c.hpsPos = 0;
        c.b0 = 1;
        return chp_atom(d, &c);
    case IDM_BOLD | 0x8000:
        c.bold = 1;
        break;
    case IDM_BOLD:
        c.bold = 0;
        break;
    case IDM_ITALIC | 0x8000:
        c.italic = 1;
        break;
    case IDM_ITALIC:
        c.italic = 0;
        break;
    case IDM_UNDERLINE | 0x8000:
        c.underline = 1;
        break;
    case IDM_UNDERLINE:
        c.underline = 0;
        break;
    case IDM_SUPER | 0x8000:
    case IDM_SUB | 0x8000:
        if (!c.hpsPos)
            c.hps = size_step(c.hps, -1);
        c.hpsPos = id == (IDM_SUPER | 0x8000) ? 4 : -4;
        break;
    case IDM_SUPER:
    case IDM_SUB:
        if (c.hpsPos)
            c.hps = size_step(c.hps, 1);
        c.hpsPos = 0;
        break;
    case IDM_REDUCE:
        c.hps = size_step(c.hps, -1);
        break;
    case IDM_ENLARGE:
        c.hps = size_step(c.hps, 1);
        break;
    default:
        if (id >= 0x4000 && id < 0x4000 + 256) {        /* set size */
            c.hps = (BYTE)(id - 0x4000);
            break;
        }
        return a;
    }
    c.b0 = 0;
    return chp_atom(d, &c);
}

static WORD font_ftc;
static BYTE font_hps;

static ATOM32 chp_setfont(DOC *d, ATOM32 a, WORD unused)
{
    CHPX c;
    (void)unused;
    chp_decode(d, a, &c);
    c.ftc = font_ftc;
    if (font_hps)
        c.hps = font_hps;
    c.b0 = 0;
    return chp_atom(d, &c);
}

static void apply_chp(ATOM32 (*fn)(DOC *, ATOM32, WORD), WORD arg)
{
    if (g_selFirst != g_selLim)
        undo_save(IDS_FORMATTING);
    if (g_selFirst == g_selLim) {
        CHPX c;
        ATOM32 a;
        sel_props(&c);
        a = fn(&g_doc, chp_atom(&g_doc, &c), arg);
        g_insChp = a;
        g_insChpValid = TRUE;
        if (g_doc.cpMac == 0)
            view_changed(0);            /* the end mark shows the font */
        return;
    }
    doc_set_chp(&g_doc, g_selFirst, g_selLim, fn, arg);
    view_changed(g_selFirst);
}

void cmd_char(WORD id)
{
    CHPX c;
    sel_props(&c);
    switch (id) {
    case IDM_BOLD:
        id |= c.bold ? 0 : 0x8000;
        break;
    case IDM_ITALIC:
        id |= c.italic ? 0 : 0x8000;
        break;
    case IDM_UNDERLINE:
        id |= c.underline ? 0 : 0x8000;
        break;
    case IDM_SUPER:
        id |= c.hpsPos > 0 ? 0 : 0x8000;
        break;
    case IDM_SUB:
        id |= c.hpsPos < 0 ? 0 : 0x8000;
        break;
    }
    apply_chp(chp_apply, id);
}

/* ---- paragraph formatting ---- */

static void sel_paras(DWORD *first, DWORD *last)
{
    DWORD f, l;
    ATOM32 a;
    *first = para_at(&g_doc, g_selFirst, &f, &l, &a);
    *last = para_at(&g_doc, g_selLim > g_selFirst ? g_selLim - 1 : g_selLim, &f, &l, &a);
}

/* the paragraphs [pi, pl] get fn applied; the last paragraph with text
   gets its mark first */
static void apply_pap(void (*fn)(PAPX *, WORD), WORD arg, BOOL all, BOOL normal)
{
    DWORD pi, pl, i, f, l;
    ATOM32 a;
    PAPX p;
    undo_save(IDS_FORMATTING);
    if (all) {
        pi = 0;
        pl = g_doc.nparas - 1;
    } else {
        sel_paras(&pi, &pl);
    }
    /* not for Tabs, which go to every paragraph and the end one too
       without a new mark (WR4) */
    if (pl == g_doc.nparas - 1 && !all) {
        CHPX c;
        sel_props(&c);
        doc_terminate_last(&g_doc, chp_atom(&g_doc, &c));
    }
    for (i = pi; i <= pl; i++) {
        para_get(&g_doc, i, &f, &l, &a);
        pap_decode(&g_doc, a, &p);
        fn(&p, arg);
        if (normal) {
            /* Normal: the default bytes, stored as none (WR3); tab stops
               stay (they belong to the document; UNVERIFIED) */
            PAPX q;
            pap_default(&q);
            q.ntabs = p.ntabs;
            m_cpy(q.tab, p.tab, sizeof q.tab);
            para_set(&g_doc, i, pap_atom(&g_doc, &q));
        } else {
            p.b0 = 0x3C;                /* formatted (WR5) */
            para_set(&g_doc, i, pap_atom(&g_doc, &p));
        }
    }
    /* the end paragraph mirrors the last one when that was formatted */
    if (pl + 2 == g_doc.nparas && !all) {
        para_get(&g_doc, pl, &f, &l, &a);
        if (l == g_doc.cpMac)
            para_set(&g_doc, pl + 1, a);
    }
    view_changed(0);
}

static void pap_cmd(PAPX *p, WORD id)
{
    switch (id) {
    case IDM_LEFT: p->jc = 0; break;
    case IDM_CENTER: p->jc = 1; break;
    case IDM_RIGHT: p->jc = 2; break;
    case IDM_JUSTIFY: p->jc = 3; break;
    case IDM_SINGLE: p->dyaLine = 240; break;
    case IDM_ONEHALF: p->dyaLine = 360; break;
    case IDM_DOUBLE: p->dyaLine = 480; break;
    }
}

void cmd_para(WORD id)
{
    apply_pap(pap_cmd, id, FALSE, id == IDM_NORMAL);
}

/* ---- measurements: "1.5", "1.5"", "3.8 cm", "12 pt", "6 p10",
   "8 p12", "2 li"; shown as inches with two decimals, e.g. 0.50" ---- */

BOOL parse_measure(const char *s, long *tw, BOOL allowNeg)
{
    long whole = 0, frac = 0, div = 1, unit = 1440;
    BOOL neg = FALSE, any = FALSE;
    while (*s == ' ')
        s++;
    if (*s == '-') {
        neg = TRUE;
        s++;
    }
    for (; *s >= '0' && *s <= '9'; s++, any = TRUE)
        whole = whole * 10 + (*s - '0');
    if (*s == '.')
        for (s++; *s >= '0' && *s <= '9'; s++, any = TRUE)
            if (div < 10000) {
                frac = frac * 10 + (*s - '0');
                div *= 10;
            }
    if (!any)
        return FALSE;
    while (*s == ' ')
        s++;
    if (*s == '"' || (s[0] == 'i' && s[1] == 'n'))
        unit = 1440;
    else if (s[0] == 'c' && s[1] == 'm')
        unit = 567;
    else if (s[0] == 'p' && s[1] == 't')
        unit = 20;
    else if (s[0] == 'p' && s[1] == '1' && s[2] == '0')
        unit = 144;
    else if (s[0] == 'p' && s[1] == '1' && s[2] == '2')
        unit = 120;
    else if (s[0] == 'l' && s[1] == 'i')
        unit = 240;
    else if (*s)
        return FALSE;
    *tw = whole * unit + (frac * unit + div / 2) / div;
    if (neg) {
        if (!allowNeg)
            return FALSE;
        *tw = -*tw;
    }
    return TRUE;
}

void fmt_measure(char *s, long tw)
{
    long h;
    char d[12];
    BOOL neg = tw < 0;
    if (neg)
        tw = -tw;
    h = (tw * 100 + 720) / 1440;        /* hundredths of an inch */
    s[0] = 0;
    if (neg)
        s_cat(s, "-", 16);
    s_utoa(d, h / 100);
    s_cat(s, d, 16);
    s_cat(s, ".", 16);
    d[0] = (char)('0' + (h % 100) / 10);
    d[1] = (char)('0' + h % 10);
    d[2] = '"';
    d[3] = 0;
    s_cat(s, d, 16);
}

static BOOL get_measure(HWND d, int id, long *tw, BOOL allowNeg)
{
    char s[32];
    GetDlgItemText(d, id, s, sizeof s);
    if (!parse_measure(s, tw, allowNeg)) {
        msgbox(d, IDS_BADMEASURE, NULL, MB_OK | MB_ICONEXCLAMATION);
        SetFocus(GetDlgItem(d, id));
        SendDlgItemMessage(d, id, EM_SETSEL, 0, MAKELPARAM(0, -1));
        return FALSE;
    }
    return TRUE;
}

static void set_measure(HWND d, int id, long tw)
{
    char s[16];
    fmt_measure(s, tw);
    SetDlgItemText(d, id, s);
}

static int run_dialog(WORD id, DLGPROC proc)
{
    FARPROC fp = MakeProcInstance((FARPROC)proc, g_hinst);
    int r = DialogBox(g_hinst, MAKEINTRESOURCE(id), g_frame, (DLGPROC)fp);
    FreeProcInstance(fp);
    return r;
}

/* ---- Indents ---- */

static long ind[3];

static void pap_indents(PAPX *p, WORD unused)
{
    (void)unused;
    p->dxaLeft = (int)ind[0];
    p->dxaLeft1 = (int)ind[1];
    p->dxaRight = (int)ind[2];
}

BOOL CALLBACK __export IndentsDlgProc(HWND d, UINT msg, WPARAM wp, LPARAM lp)
{
    (void)lp;
    if (msg == WM_INITDIALOG) {
        DWORD f, l;
        ATOM32 a;
        PAPX p;
        para_at(&g_doc, g_selFirst, &f, &l, &a);
        pap_decode(&g_doc, a, &p);
        set_measure(d, 3, p.dxaLeft);
        set_measure(d, 4, p.dxaLeft1);
        set_measure(d, 5, p.dxaRight);
        SendDlgItemMessage(d, 3, EM_SETSEL, 0, MAKELPARAM(0, -1));
        return TRUE;
    }
    if (msg == WM_COMMAND) {
        if (wp == IDOK) {
            if (!get_measure(d, 3, &ind[0], FALSE) || !get_measure(d, 4, &ind[1], TRUE) ||
                !get_measure(d, 5, &ind[2], FALSE))
                return TRUE;
            EndDialog(d, IDOK);
            return TRUE;
        }
        if (wp == IDCANCEL) {
            EndDialog(d, IDCANCEL);
            return TRUE;
        }
    }
    return FALSE;
}

void cmd_indents(void)
{
    if (run_dialog(IDD_INDENTS, IndentsDlgProc) == IDOK)
        apply_pap(pap_indents, 0, FALSE, FALSE);
}

/* ---- Tabs: twelve positions, each with a Decimal box; the document's
   paragraphs all get them (WR4) ---- */

static PAPX tabs;

static void pap_tabs(PAPX *p, WORD unused)
{
    (void)unused;
    p->ntabs = tabs.ntabs;
    m_cpy(p->tab, tabs.tab, sizeof p->tab);
}

BOOL CALLBACK __export TabsDlgProc(HWND d, UINT msg, WPARAM wp, LPARAM lp)
{
    int i;
    (void)lp;
    if (msg == WM_INITDIALOG) {
        DWORD f, l;
        ATOM32 a;
        PAPX p;
        para_at(&g_doc, g_selFirst, &f, &l, &a);
        pap_decode(&g_doc, a, &p);
        for (i = 0; i < p.ntabs && i < 12; i++) {
            set_measure(d, 4 + i, p.tab[i].dxa);
            CheckDlgButton(d, 16 + i, p.tab[i].jc == 3);
        }
        return TRUE;
    }
    if (msg == WM_COMMAND) {
        if (wp >= 16 && wp < 28) {
            CheckDlgButton(d, wp, !IsDlgButtonChecked(d, wp));
            return TRUE;
        }
        if (wp == 3) {                  /* Clear All */
            for (i = 0; i < 12; i++) {
                SetDlgItemText(d, 4 + i, "");
                CheckDlgButton(d, 16 + i, 0);
            }
            return TRUE;
        }
        if (wp == IDOK) {
            tabs.ntabs = 0;
            for (i = 0; i < 12; i++) {
                char s[32];
                long tw;
                int k, j;
                GetDlgItemText(d, 4 + i, s, sizeof s);
                if (!s[0])
                    continue;
                if (!get_measure(d, 4 + i, &tw, FALSE))
                    return TRUE;
                /* sorted, no duplicates (UNVERIFIED) */
                for (k = 0; k < tabs.ntabs && tabs.tab[k].dxa < tw; k++)
                    ;
                if (k < tabs.ntabs && tabs.tab[k].dxa == tw)
                    continue;
                for (j = tabs.ntabs; j > k; j--)
                    tabs.tab[j] = tabs.tab[j - 1];
                tabs.tab[k].dxa = (int)tw;
                tabs.tab[k].jc = IsDlgButtonChecked(d, 16 + i) ? 3 : 0;
                tabs.ntabs++;
            }
            EndDialog(d, IDOK);
            return TRUE;
        }
        if (wp == IDCANCEL) {
            EndDialog(d, IDCANCEL);
            return TRUE;
        }
    }
    return FALSE;
}

void cmd_tabs(void)
{
    if (run_dialog(IDD_TABS, TabsDlgProc) == IDOK)
        apply_pap(pap_tabs, 0, TRUE, FALSE);
}

/* ---- Page Layout ---- */

static SEPX newsep;

BOOL CALLBACK __export LayoutDlgProc(HWND d, UINT msg, WPARAM wp, LPARAM lp)
{
    (void)lp;
    if (msg == WM_INITDIALOG) {
        char s[12];
        sep_decode(&g_doc, &newsep);
        s_utoa(s, newsep.pgnFirst);
        SetDlgItemText(d, 3, s);
        set_measure(d, 4, newsep.xaLeft);
        set_measure(d, 5, (long)newsep.xaMac - newsep.xaLeft - newsep.dxaText);
        set_measure(d, 6, newsep.yaTop);
        set_measure(d, 7, (long)newsep.yaMac - newsep.yaTop - newsep.dyaText);
        CheckRadioButton(d, 8, 9, 8);
        SendDlgItemMessage(d, 3, EM_SETSEL, 0, MAKELPARAM(0, -1));
        return TRUE;
    }
    if (msg == WM_COMMAND) {
        if (wp == 8 || wp == 9) {
            CheckRadioButton(d, 8, 9, wp);
            return TRUE;
        }
        if (wp == IDOK) {
            long l, r, t, b;
            BOOL ok;
            UINT pg = GetDlgItemInt(d, 3, &ok, FALSE);
            if (!ok || pg < 1 || pg > 32767) {
                msgbox(d, IDS_WHOLENUMBER, NULL, MB_OK | MB_ICONEXCLAMATION);
                return TRUE;
            }
            if (!get_measure(d, 4, &l, FALSE) || !get_measure(d, 5, &r, FALSE) ||
                !get_measure(d, 6, &t, FALSE) || !get_measure(d, 7, &b, FALSE))
                return TRUE;
            if (l + r >= newsep.xaMac || t + b >= newsep.yaMac) {
                msgbox(d, IDS_MARGINSBIG, NULL, MB_OK | MB_ICONEXCLAMATION);
                return TRUE;
            }
            /* UNVERIFIED: an unchanged 1 is kept as FFFFh */
            if (pg != newsep.pgnFirst)
                newsep.pgnRaw = pg;
            newsep.pgnFirst = pg;
            newsep.xaLeft = (WORD)l;
            newsep.dxaText = (WORD)(newsep.xaMac - l - r);
            newsep.yaTop = (WORD)t;
            newsep.dyaText = (WORD)(newsep.yaMac - t - b);
            EndDialog(d, IDOK);
            return TRUE;
        }
        if (wp == IDCANCEL) {
            EndDialog(d, IDCANCEL);
            return TRUE;
        }
    }
    return FALSE;
}

void cmd_pagelayout(void)
{
    if (run_dialog(IDD_PAGELAYOUT, LayoutDlgProc) == IDOK) {
        undo_save(IDS_FORMATTING);
        sep_encode(&g_doc, &newsep);
        g_doc.dirty = TRUE;
        view_changed(0);
    }
}

/* ---- Go To Page ---- */

static WORD gotoPage;

BOOL CALLBACK __export GotoDlgProc(HWND d, UINT msg, WPARAM wp, LPARAM lp)
{
    (void)lp;
    if (msg == WM_COMMAND) {
        if (wp == IDOK) {
            BOOL ok;
            gotoPage = GetDlgItemInt(d, 3, &ok, FALSE);
            if (!ok) {
                msgbox(d, IDS_NOTNUMBER, NULL, MB_OK | MB_ICONEXCLAMATION);
                return TRUE;
            }
            EndDialog(d, IDOK);
            return TRUE;
        }
        if (wp == IDCANCEL) {
            EndDialog(d, IDCANCEL);
            return TRUE;
        }
    }
    return msg == WM_INITDIALOG;
}

void cmd_goto(void)
{
    DWORD i, cp = 0;
    BOOL found = FALSE;
    SEPX s;
    if (run_dialog(IDD_GOTO, GotoDlgProc) != IDOK)
        return;
    sep_decode(&g_doc, &s);
    for (i = 0; i < g_doc.npages; i++) {
        struct { WORD pgn, pad; DWORD cp; } e;
        hm_get(&g_doc.pgtb, i * 8, &e, 8);
        if (e.pgn == gotoPage) {
            cp = e.cp;
            found = TRUE;
            break;
        }
    }
    if (!found && gotoPage == s.pgnFirst)
        found = TRUE;
    if (!found) {
        msgbox(g_frame, IDS_NOPAGE, NULL, MB_OK | MB_ICONEXCLAMATION);
        return;
    }
    view_select(cp, cp, TRUE);
}

/* ---- Find and Replace (modeless) ---- */

static char findWhat[256], replWith[256];  /* the original's bound: 255 (IDS 12289) */
static BOOL matchWord, matchCase;
static HWND findDlg;

static BOOL match_at(DWORD cp, int n)
{
    int i;
    for (i = 0; i < n; i++) {
        char a = (char)doc_ch(&g_doc, cp + i), b = findWhat[i];
        if (!matchCase) {
            a = (char)(DWORD)AnsiUpper((LPSTR)(DWORD)(BYTE)a);
            b = (char)(DWORD)AnsiUpper((LPSTR)(DWORD)(BYTE)b);
        }
        if (a != b)
            return FALSE;
    }
    if (matchWord) {
        if (cp > 0 && IsCharAlphaNumeric(doc_ch(&g_doc, cp - 1)))
            return FALSE;
        if (cp + n < g_doc.cpMac && IsCharAlphaNumeric(doc_ch(&g_doc, cp + n)))
            return FALSE;
    }
    return TRUE;
}

static BOOL find_next(void)
{
    int n = s_len(findWhat);
    DWORD cp, start = g_selLim;
    if (!n)
        return FALSE;
    for (cp = start; cp + n <= g_doc.cpMac; cp++)
        if (match_at(cp, n)) {
            view_select(cp, cp + n, TRUE);
            return TRUE;
        }
    for (cp = 0; cp < start && cp + n <= g_doc.cpMac; cp++)
        if (match_at(cp, n)) {
            view_select(cp, cp + n, TRUE);
            return TRUE;
        }
    msgbox(findDlg ? findDlg : g_frame, IDS_NOTFOUND, NULL, MB_OK | MB_ICONASTERISK);
    return FALSE;
}

static void read_find(HWND d)
{
    GetDlgItemText(d, 7, findWhat, sizeof findWhat);
    if (GetDlgItem(d, 8))
        GetDlgItemText(d, 8, replWith, sizeof replWith);
    matchWord = IsDlgButtonChecked(d, 5);
    matchCase = IsDlgButtonChecked(d, 6);
}

static void replace_sel_text(void)
{
    CHPX c;
    ATOM32 a;
    DWORD cp = g_selFirst;
    undo_save(IDS_EDITING);
    sel_props(&c);
    a = chp_atom(&g_doc, &c);
    doc_delete(&g_doc, g_selFirst, g_selLim - g_selFirst);
    doc_insert(&g_doc, cp, replWith, s_len(replWith), a);
    g_selFirst = g_selLim = cp + s_len(replWith);
    view_changed(cp);
}

BOOL CALLBACK __export FindDlgProc(HWND d, UINT msg, WPARAM wp, LPARAM lp)
{
    (void)lp;
    switch (msg) {
    case WM_INITDIALOG:
        SetDlgItemText(d, 7, findWhat);
        if (GetDlgItem(d, 8))
            SetDlgItemText(d, 8, replWith);
        CheckDlgButton(d, 5, matchWord);
        CheckDlgButton(d, 6, matchCase);
        return TRUE;
    case WM_COMMAND:
        switch (wp) {
        case IDOK:
            read_find(d);
            find_next();
            return TRUE;
        case 9:                         /* Replace */
            read_find(d);
            if (g_selLim - g_selFirst == (DWORD)s_len(findWhat) &&
                match_at(g_selFirst, s_len(findWhat)))
                replace_sel_text();
            find_next();
            return TRUE;
        case 4: {                       /* Replace All */
            DWORD n = 0;
            read_find(d);
            g_selFirst = g_selLim = 0;
            for (;;) {
                int k = s_len(findWhat);
                DWORD cp;
                BOOL hit = FALSE;
                for (cp = g_selLim; k && cp + k <= g_doc.cpMac; cp++)
                    if (match_at(cp, k)) {
                        g_selFirst = cp;
                        g_selLim = cp + k;
                        hit = TRUE;
                        break;
                    }
                if (!hit)
                    break;
                replace_sel_text();
                n++;
            }
            if (!n)
                msgbox(d, IDS_NOREPLACE, NULL, MB_OK | MB_ICONASTERISK);
            return TRUE;
        }
        case IDCANCEL:
            DestroyWindow(d);
            findDlg = NULL;
            return TRUE;
        case 5:
        case 6:
            CheckDlgButton(d, wp, !IsDlgButtonChecked(d, wp));
            return TRUE;
        }
        break;
    }
    return FALSE;
}

HWND find_dialog(void)
{
    return findDlg;
}

void cmd_find(BOOL replace)
{
    static FARPROC fp;
    if (findDlg)
        DestroyWindow(findDlg);
    if (!fp)
        fp = MakeProcInstance((FARPROC)FindDlgProc, g_hinst);
    findDlg = CreateDialog(g_hinst, MAKEINTRESOURCE(replace ? IDD_REPLACE : IDD_FIND),
                           g_frame, (DLGPROC)fp);
}

void cmd_findagain(void)
{
    find_next();
}

/* ---- Fonts ---- */


void cmd_fonts(void)
{
    CHOOSEFONT cf;
    LOGFONT lf;
    CHPX c;
    FFN FAR *f;
    sel_props(&c);
    f = font_get(&g_doc, c.ftc);
    m_set(&lf, 0, sizeof lf);
    s_cpy(lf.lfFaceName, f->name, LF_FACESIZE);
    lf.lfHeight = -MulDiv(c.hps, 96, 144);
    lf.lfWeight = c.bold ? FW_BOLD : FW_NORMAL;
    lf.lfItalic = c.italic;
    m_set(&cf, 0, sizeof cf);
    cf.lStructSize = sizeof cf;
    cf.hwndOwner = g_frame;
    cf.lpLogFont = &lf;
    cf.Flags = CF_SCREENFONTS | CF_INITTOLOGFONTSTRUCT;    /* UNVERIFIED flags */
    if (!ChooseFont(&cf))
        return;
    font_ftc = font_code(&g_doc, lf.lfFaceName, (BYTE)(lf.lfPitchAndFamily & 0xF0));
    font_hps = (BYTE)(cf.iPointSize / 5);
    apply_chp(chp_setfont, 0);
}

/* ---- Header and Footer: plan (docs/wri-format.md has the format) ---- */


/* ---- the clipboard: plain text for now ---- */

void cmd_cut_copy(BOOL cut)
{
    DWORD n = g_selLim - g_selFirst;
    HGLOBAL h;
    if (!n)
        return;
    if (cut)
        undo_save(IDS_EDITING);
    h = GlobalAlloc(GMEM_MOVEABLE | GMEM_DDESHARE, n + 1);
    if (!h)
        return;
    {
        HMEM m;
        m.h = h;
        m.p = (BYTE FAR *)GlobalLock(h);
        m.size = n + 1;
        {
            DWORD o;
            char buf[256];
            for (o = 0; o < n;) {
                WORD k = (WORD)doc_get(&g_doc, g_selFirst + o, buf, n - o > sizeof buf ? sizeof buf : n - o);
                hm_put(&m, o, buf, k);
                o += k;
            }
            *hm_ptr(&m, n) = 0;
        }
        GlobalUnlock(h);
    }
    if (OpenClipboard(g_frame)) {
        EmptyClipboard();
        SetClipboardData(CF_TEXT, h);
        CloseClipboard();
    } else {
        GlobalFree(h);
    }
    if (cut) {
        doc_delete(&g_doc, g_selFirst, n);
        g_selLim = g_selFirst;
        view_changed(g_selFirst);
    }
}

void cmd_paste(void)
{
    HGLOBAL h;
    if (!OpenClipboard(g_frame))
        return;
    h = GetClipboardData(CF_TEXT);
    if (h) {
        HMEM m;
        DWORD n = 0, size = GlobalSize(h);
        CHPX c;
        ATOM32 a;
        DWORD cp = g_selFirst;
        undo_save(IDS_EDITING);
        m.h = h;
        m.p = (BYTE FAR *)GlobalLock(h);
        m.size = size;
        while (n < size && *hm_ptr(&m, n))
            n++;
        sel_props(&c);
        a = chp_atom(&g_doc, &c);
        if (g_selLim > g_selFirst)
            doc_delete(&g_doc, g_selFirst, g_selLim - g_selFirst);
        {
            DWORD o;
            char buf[256];
            for (o = 0; o < n;) {
                WORD k = (WORD)(n - o > sizeof buf ? sizeof buf : n - o);
                hm_get(&m, o, buf, k);
                doc_insert(&g_doc, cp + o, buf, k, a);
                o += k;
            }
        }
        GlobalUnlock(h);
        g_selFirst = g_selLim = cp + n;
        view_changed(cp);
    }
    CloseClipboard();
}

/* ---- menu states ---- */

void menu_update(HMENU m)
{
    CHPX c;
    DWORD f, l;
    ATOM32 a;
    PAPX p;
    BOOL sel = g_selLim > g_selFirst;
    sel_props(&c);
    para_at(&g_doc, g_selFirst, &f, &l, &a);
    pap_decode(&g_doc, a, &p);
    /* Regular is not checked even for plain text (WR7: a new document;
       UNVERIFIED whether it ever is) */
    CheckMenuItem(m, IDM_REGULAR, MF_UNCHECKED);
    CheckMenuItem(m, IDM_BOLD, c.bold ? MF_CHECKED : MF_UNCHECKED);
    CheckMenuItem(m, IDM_ITALIC, c.italic ? MF_CHECKED : MF_UNCHECKED);
    CheckMenuItem(m, IDM_UNDERLINE, c.underline ? MF_CHECKED : MF_UNCHECKED);
    CheckMenuItem(m, IDM_SUPER, c.hpsPos > 0 ? MF_CHECKED : MF_UNCHECKED);
    CheckMenuItem(m, IDM_SUB, c.hpsPos < 0 ? MF_CHECKED : MF_UNCHECKED);
    CheckMenuItem(m, IDM_LEFT, p.jc == 0 ? MF_CHECKED : MF_UNCHECKED);
    CheckMenuItem(m, IDM_CENTER, p.jc == 1 ? MF_CHECKED : MF_UNCHECKED);
    CheckMenuItem(m, IDM_RIGHT, p.jc == 2 ? MF_CHECKED : MF_UNCHECKED);
    CheckMenuItem(m, IDM_JUSTIFY, p.jc == 3 ? MF_CHECKED : MF_UNCHECKED);
    CheckMenuItem(m, IDM_SINGLE, p.dyaLine <= 240 ? MF_CHECKED : MF_UNCHECKED);
    CheckMenuItem(m, IDM_ONEHALF, p.dyaLine == 360 ? MF_CHECKED : MF_UNCHECKED);
    CheckMenuItem(m, IDM_DOUBLE, p.dyaLine == 480 ? MF_CHECKED : MF_UNCHECKED);
    EnableMenuItem(m, IDM_CUT, sel ? MF_ENABLED : MF_GRAYED);
    EnableMenuItem(m, IDM_COPY, sel ? MF_ENABLED : MF_GRAYED);
    EnableMenuItem(m, IDM_PASTE, IsClipboardFormatAvailable(CF_TEXT) ? MF_ENABLED : MF_GRAYED);
    {
        char s[64], k[24];
        if (undoKind) {
            load_str(IDS_UNDO, s, sizeof s);
            load_str(undoKind, k, sizeof k);
            s_cat(s, k, sizeof s);
            s_cat(s, "\tCtrl+Z", sizeof s);
            ModifyMenu(m, IDM_UNDO, MF_BYCOMMAND | MF_STRING, IDM_UNDO, s);
        }
        EnableMenuItem(m, IDM_UNDO, undoKind ? MF_ENABLED : MF_GRAYED);
    }
    EnableMenuItem(m, IDM_PASTESPECIAL, MF_GRAYED);
    EnableMenuItem(m, IDM_PASTELINK, MF_GRAYED);
    EnableMenuItem(m, IDM_LINKS, MF_ENABLED);   /* enabled with no links too (WR12) */
    {
        /* with no object selected the item reads "&Object", grayed (WR7;
           the original's string 32) */
        char s[32];
        load_str(IDS_OBJECTNONE, s, sizeof s);
        ModifyMenu(m, IDM_OBJECT, MF_BYCOMMAND | MF_STRING | MF_GRAYED, IDM_OBJECT, s);
    }
    EnableMenuItem(m, IDM_OBJECT, MF_GRAYED);
    {
        /* an empty document: nothing to find or go to; Print needs a
           printer (WR7: all grayed in a new document, none set up) */
        char dev[8];
        GetProfileString("windows", "device", "", dev, sizeof dev);
        EnableMenuItem(m, IDM_FIND, g_doc.cpMac ? MF_ENABLED : MF_GRAYED);
        EnableMenuItem(m, IDM_REPLACE, g_doc.cpMac ? MF_ENABLED : MF_GRAYED);
        EnableMenuItem(m, IDM_GOTO, g_doc.cpMac ? MF_ENABLED : MF_GRAYED);
        EnableMenuItem(m, IDM_PRINT, dev[0] ? MF_ENABLED : MF_GRAYED);
    }
    EnableMenuItem(m, IDM_MOVEPICT, MF_GRAYED);
    EnableMenuItem(m, IDM_SIZEPICT, MF_GRAYED);
    EnableMenuItem(m, IDM_FINDAGAIN, findWhat[0] ? MF_ENABLED : MF_GRAYED);
}
