/*
 * para.c - w16 Windows Help: walking a display link's items (para.h).
 *
 * LinkData1 of a text record (20h) or table (23h): compressed TopicSize
 * and TopicLength; for a table its column count, table type (0 and 2
 * variable width, with a minimum width), and each column's gap and width;
 * then paragraph info (for a table cell first its column, a word and a
 * byte); then commands. Paragraph info: two bytes, a word, the flags
 * word, and what the flags say follows: a compressed long (1), spacing
 * above (2), below (4), between lines (8), left (10h), right (20h) and
 * first-line (40h) indents, border (100h: a flags byte and a width word),
 * tabs (200h: a count, then each position, 4000h set: a type follows);
 * 400h right-aligned, 800h centred. Values in half points (3.x fonts).
 */
#include "para.h"

static BOOL room(HLPITER FAR *it, DWORD n)
{
    return (DWORD)(it->e1 - it->d1) >= n;
}

BOOL para_begin(HLPITER FAR *it, BYTE type, LPCB d1, DWORD n1, LPCB d2, DWORD n2)
{
    LPCB p = d1;
    it->type = type;
    it->d1 = d1;
    it->e1 = d1 + n1;
    it->d2 = d2;
    it->e2 = d2 + n2;
    it->done = FALSE;
    it->inpara = FALSE;
    it->pending = FALSE;
    it->table.ncols = 0;
    if (n1 < 4)
        return FALSE;
    it->topicsize = hlp_culong(&p);
    it->topiclength = 0;
    if (type == 0x20 || type == 0x23)
        it->topiclength = hlp_cuword(&p);
    if (type == 0x23) {
        WORD i;
        it->table.ncols = *p++;
        it->table.tabletype = *p++;
        it->table.minwidth = 0;
        if (it->table.tabletype == 0 || it->table.tabletype == 2) {
            it->table.minwidth = (short)hlp_rw(p);
            p += 2;
        }
        for (i = 0; i < it->table.ncols; i++) {
            short g = (short)hlp_rw(p), w = (short)hlp_rw(p + 2);
            p += 4;
            if (i < HLP_MAXCOLS) {
                it->table.gap[i] = g;
                it->table.width[i] = w;
            }
        }
        if (it->table.ncols > HLP_MAXCOLS)
            it->table.ncols = HLP_MAXCOLS;
    }
    it->d1 = p;
    return p <= it->e1;
}

static BOOL read_para(HLPITER FAR *it)
{
    LPCB p = it->d1;
    HLPPARA FAR *a = &it->para;
    WORD bits;
    a->column = 0;
    if (it->type == 0x23) {
        if (!room(it, 2))
            return FALSE;
        a->column = (short)hlp_rw(p);
        p += 2;
        if (a->column == -1) {
            it->d1 = p;
            return FALSE;
        }
        p += 3;
    }
    p += 4;                     /* two bytes and a word, unused here */
    bits = hlp_rw(p);
    p += 2;
    a->bits = bits;
    a->above = a->below = a->lines = 0;
    a->left = a->right = a->first = 0;
    a->border = 0;
    a->borderwidth = 0;
    a->ntabs = 0;
    if (bits & 0x0001)
        hlp_cslong(&p);
    if (bits & 0x0002)
        a->above = hlp_csword(&p);
    if (bits & 0x0004)
        a->below = hlp_csword(&p);
    if (bits & 0x0008)
        a->lines = hlp_csword(&p);
    if (bits & 0x0010)
        a->left = hlp_csword(&p);
    if (bits & 0x0020)
        a->right = hlp_csword(&p);
    if (bits & 0x0040)
        a->first = hlp_csword(&p);
    if (bits & 0x0100) {
        a->border = *p++;
        a->borderwidth = (short)hlp_rw(p);
        p += 2;
    }
    if (bits & 0x0200) {
        short n = hlp_csword(&p), i;
        for (i = 0; i < n; i++) {
            WORD t = hlp_cuword(&p), ty = 0;
            if (t & 0x4000)
                ty = hlp_cuword(&p);
            if (a->ntabs < HLP_MAXTABS) {
                a->tab[a->ntabs] = t & 0x3FFF;
                a->tabtype[a->ntabs] = (BYTE)ty;
                a->ntabs++;
            }
        }
    }
    it->d1 = p;
    return p <= it->e1;
}

/* the next text piece (NUL-terminated in LinkData2) */
static void piece(HLPITER FAR *it)
{
    LPCB s = it->d2;
    while (s < it->e2 && *s)
        s++;
    it->text = (const char FAR *)it->d2;
    it->len = (WORD)(s - it->d2);
    it->d2 = s < it->e2 ? s + 1 : s;
}

int para_next(HLPITER FAR *it)
{
    BYTE c;
    LPCB p;
    for (;;) {
        if (it->done)
            return PI_END;
        if (!it->inpara) {
            if (!read_para(it)) {
                it->done = TRUE;
                return PI_END;
            }
            it->inpara = TRUE;
            it->pending = TRUE;
            return PI_PARA;
        }
        if (it->pending) {
            it->pending = FALSE;
            piece(it);
            if (it->len)
                return PI_TEXT;
        }
        if (!room(it, 1)) {
            it->done = TRUE;
            return PI_END;
        }
        p = it->d1;
        c = *p++;
        it->code = c;
        it->pending = TRUE;
        switch (c) {
        case 0xFF:
            it->d1 = p;
            it->pending = FALSE;
            if (it->type == 0x23) {
                it->inpara = FALSE;
                return PI_CELLEND;
            }
            it->done = TRUE;
            return PI_END;
        case 0x20:
            it->d1 = p + 4;
            continue;
        case 0x21:
            it->d1 = p + 2;
            continue;
        case 0x80:
            it->num = hlp_rw(p);
            it->d1 = p + 2;
            return PI_FONT;
        case 0x81:
            it->d1 = p;
            return PI_BREAK;
        case 0x82:
            it->d1 = p;
            return PI_PAREND;
        case 0x83:
            it->d1 = p;
            return PI_TAB;
        case 0x86:
        case 0x87:
        case 0x88: {
            BYTE ty = *p++;
            DWORD size = (DWORD)hlp_cslong(&p);
            it->hotspots = 0;
            if (ty == 0x22)
                it->hotspots = hlp_cuword(&p);
            it->align = (BYTE)(c - 0x86);
            it->pictype = ty;
            it->data = p;
            it->dlen = size;
            it->d1 = p + size;
            return PI_PICT;
        }
        case 0x89:
            it->d1 = p;
            return PI_HOTEND;
        case 0x8B:
            it->d1 = p;
            return PI_NBSP;
        case 0x8C:
            it->d1 = p;
            return PI_NBHYPH;
        case 0xC8:
        case 0xCC: {
            WORD n = hlp_rw(p);
            it->text = (const char FAR *)(p + 2);
            it->len = hlp_strlen(it->text);
            it->d1 = p + 2 + n;
            return PI_MACRO;
        }
        case 0xE0: case 0xE1: case 0xE2: case 0xE3:
        case 0xE6: case 0xE7:
            it->off = hlp_rl(p);
            it->file = it->window = NULL;
            it->wnum = 0xFF;
            it->d1 = p + 4;
            return PI_JUMP;
        case 0xEA: case 0xEB: case 0xEE: case 0xEF: {
            WORD n = hlp_rw(p);
            LPCB q = p + 2, e = p + 2 + n;
            BYTE ty = *q++;
            it->off = hlp_rl(q);
            q += 4;
            it->file = it->window = NULL;
            it->wnum = 0xFF;
            if (ty == 1)
                it->wnum = *q++;
            if ((ty == 4 || ty == 6) && q < e) {
                it->file = (const char FAR *)q;
                q += hlp_strlen(it->file) + 1;
            }
            if (ty == 6 && q < e)
                it->window = (const char FAR *)q;
            it->d1 = e;
            return PI_JUMP;
        }
        default:
            /* unknown: nothing more can be read safely */
            it->done = TRUE;
            return PI_END;
        }
    }
}
