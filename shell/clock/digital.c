/*
 * digital.c - the digital clock.
 *
 * Observed on Windows 3.1 (the original's GDI calls at many window
 * sizes, tests/clockscenarios.sh; ARCHITECTURE.md 15.x):
 * - The time is laid out as the string "88:88:88 MM" (intl.c) would be.
 *   With the font at height -1000 the string and the date are measured;
 *   the usable width is W = 7/8 of the client width, and the font height
 *   becomes MulDiv(1000, W, width of the string), at most 3/8 of the
 *   client height (7/8 without the date). If the string is still wider
 *   than W at that height, the height drops by 2 until it fits.
 * - Each part (hours, separators, minutes, seconds, AM/PM) has its place
 *   from the extents of the string's prefixes; the whole is centred. A
 *   one-digit hour is drawn right-aligned in the hour's place.
 * - The date's font is MulDiv(1000, W, width of the date at -1000) high,
 *   at most 3/4 of the time's; the date is centred under the time, the
 *   two together centred in the client area.
 * - Each part is drawn with ExtTextOut, ETO_OPAQUE | ETO_CLIPPED, in
 *   COLOR_BTNTEXT on COLOR_BTNFACE, and only when it changed; the date's
 *   line is cleared first.
 * - From a height of 54 the time is embossed: white at (0,0), opaque;
 *   then, transparent, button shadow at (4,4) and button face at (2,2).
 *   Parts no wider than "88" go through a bitmap that size; wider ones
 *   are drawn in place.
 * - Minimized: "88:88" (no seconds, no AM/PM, no date) in a width of 28,
 *   then the frame (util.c).
 */
#include "clock.h"

#define MAXS 32

static HFONT tfont, dfont;
static HBITMAP bm;
static BOOL valid;                      /* layout done for this size */
static int ccx, ccy;                    /* client size */
static char meas[MAXS];
static int nmeas;
static int ext[MAXS + 1];               /* extents of the prefixes */
static int tcy, dcx, dcy, x0, y0, xdate, ydate, bmcx;
static BOOL embossed, icon, withdate, withsecs;
static int height;
/* where the parts are in meas */
static int p_hh, p_s1, p_mm, p_s2, p_ss, p_ap, p_end;
static int last_hour = -1, last_min = -1, last_sec = -1;
static char last_date[32];

void digi_free(void)
{
    if (tfont)
        DeleteObject(tfont);
    if (dfont)
        DeleteObject(dfont);
    if (bm)
        DeleteObject(bm);
    tfont = dfont = NULL;
    bm = NULL;
    valid = FALSE;
}

void digi_size(HDC dc, int cx, int cy)
{
    (void)dc;
    ccx = cx;
    ccy = cy;
    valid = FALSE;
}

void digi_reset(void)
{
    valid = FALSE;
}

/* g_lf keeps the height last made: Set Font starts from it (observed:
   the size list shows the date's) */
static HFONT make_font(int h)
{
#ifndef W16_WIN2
    g_lf.lfHeight = -h;
#else
    g_lf.lfHeight = h;                  /* 2.x: the cell height */
#endif
    return CreateFontIndirect(&g_lf);
}

/* Windows 2.x: a face asked for at a height it has no raster font for
   comes out as a vector font (Modern), stretched; come down to one */
#ifdef W16_WIN2
static BOOL face_ok(HDC dc, HFONT f)
{
    char face[LF_FACESIZE];
    int i;
    HFONT old = SelectObject(dc, f);
    GetTextFace(dc, sizeof face, face);
    SelectObject(dc, old);
    for (i = 0; face[i] && (face[i] | 0x20) == (g_lf.lfFaceName[i] | 0x20); i++)
        ;
    return face[i] == g_lf.lfFaceName[i];
}
#define raster_ok(dc) face_ok(dc, tfont)
#define date_raster_ok(dc) face_ok(dc, dfont)
#else
#define raster_ok(dc) TRUE
#endif

static void layout(HDC dc, const CLOCKTIME *t)
{
    HFONT old;
    DWORD de = 0;
#ifndef W16_WIN2
    HFONT f;
    DWORD e;
#endif
    int w, h, cap, k, sl = s_len(g_sep);
    char date[32];

    icon = IsIconic(g_wnd);
    withdate = !icon && !g_opt.nodate;
    withsecs = !icon && !g_opt.noseconds;
    if (icon)
        intl_icon_measure(meas);
    else
        intl_measure(meas, withsecs);
    nmeas = s_len(meas);
    p_hh = 0;
    p_s1 = 2;
    p_mm = p_s1 + sl;
    p_s2 = p_mm + 2;
    p_ss = withsecs ? p_s2 + sl : p_s2;
    p_end = withsecs ? p_ss + 2 : p_s2;
    p_ap = p_end < nmeas ? p_end + 1 : nmeas;
    if (withdate)
        intl_date(t, date, TRUE);

    if (tfont)
        DeleteObject(tfont);
    if (dfont)
        DeleteObject(dfont);
    tfont = dfont = NULL;
    w = icon ? ccx - 8 : ccx * 7 / 8;
    cap = withdate ? ccy * 3 / 8 : ccy * 7 / 8;
#ifndef W16_WIN2
    f = make_font(1000);
    old = SelectObject(dc, f);
    e = GetTextExtent(dc, meas, nmeas);
    if (withdate)
        de = GetTextExtent(dc, date, s_len(date));
    SelectObject(dc, old);
    h = MulDiv(1000, w, LOWORD(e));
    DeleteObject(f);
    if (h > cap)
        h = cap;
#else
    /* Windows 2.x has raster fonts only: no height-1000 font to scale
       from; start from the largest height allowed and come down */
    h = cap < 24 ? cap : 24;            /* Helv's largest; GDI 2.x would stretch it */
#endif
    for (;;) {
        tfont = make_font(h);
        old = SelectObject(dc, tfont);
        ext[0] = 0;
        for (k = 1; k <= nmeas; k++) {
            DWORD x = GetTextExtent(dc, meas, k);
            ext[k] = LOWORD(x);
            tcy = HIWORD(x);
        }
        SelectObject(dc, old);
        if ((ext[nmeas] <= w && raster_ok(dc)) || h <= 2)
            break;
        DeleteObject(tfont);
        h -= 2;
    }
    height = h;
#ifndef W16_WIN2
    embossed = !icon && h >= 54;        /* UNVERIFIED: 53 is flat, 54 embossed */
#else
    embossed = FALSE;
#endif
    if (bm)
        DeleteObject(bm);
    bmcx = ext[2];
    bm = CreateDiscardableBitmap(dc, embossed ? ext[2] + 4 : ext[2], tcy);
    dcy = 0;
    if (withdate) {
#ifndef W16_WIN2
        int hd = MulDiv(1000, w, LOWORD(de));
        if (hd > h * 3 / 4)
            hd = h * 3 / 4;
        dfont = make_font(hd);
        old = SelectObject(dc, dfont);
        de = GetTextExtent(dc, date, s_len(date));
        SelectObject(dc, old);
#else
        int hd = h * 3 / 4;
        for (;;) {
            dfont = make_font(hd);
            old = SelectObject(dc, dfont);
            de = GetTextExtent(dc, date, s_len(date));
            SelectObject(dc, old);
            if (((int)LOWORD(de) <= w && date_raster_ok(dc)) || hd <= 2)
                break;
            DeleteObject(dfont);
            hd--;
        }
#endif
        dcx = LOWORD(de);
        dcy = HIWORD(de);
    }
    x0 = (ccx - ext[nmeas]) / 2;
    y0 = (ccy - tcy - dcy) / 2;
    xdate = (ccx - dcx) / 2;
    ydate = y0 + tcy;
    last_hour = last_min = last_sec = -1;
    last_date[0] = 0;
    valid = TRUE;
}

static void text(HDC dc, int x, int y, UINT opt, int l, int r, const char *s)
{
    RECT rc;
    rc.left = l;
    rc.top = y;
    rc.right = r;
    rc.bottom = y + tcy;
    ExtTextOut(dc, x, y, opt, &rc, s, s_len(s), NULL);
}

static HDC mem;                         /* embossed: the bitmap's DC */

static void emboss(HDC dc, int x, int y, const RECT *rc, const char *s)
{
    int n = s_len(s);
    SetTextColor(dc, GetSysColor(COLOR_BTNHIGHLIGHT));   /* observed */
    ExtTextOut(dc, x, y, ETO_OPAQUE | ETO_CLIPPED, rc, s, n, NULL);
    SetBkMode(dc, TRANSPARENT);
    SetTextColor(dc, GetSysColor(COLOR_BTNSHADOW));
    ExtTextOut(dc, x + 4, y + 4, ETO_CLIPPED, rc, s, n, NULL);
    SetTextColor(dc, GetSysColor(COLOR_BTNFACE));
    ExtTextOut(dc, x + 2, y + 2, ETO_CLIPPED, rc, s, n, NULL);
}

/* one part of the time, between prefixes a and b; right-aligned when it
   is one character in a two-character place. Embossed, the part grows
   by 2 pixels each way (the shadow is 4 pixels off). */
static void part(HDC dc, int a, int b, const char *s)
{
    int l = x0 + ext[a], r = x0 + ext[b];
    RECT rc;
    if (s_len(s) == 1 && b - a == 2)
        l = r - ext[1];
    if (!embossed) {
        SetTextColor(dc, GetSysColor(COLOR_BTNTEXT));
        text(dc, l, y0, ETO_OPAQUE | ETO_CLIPPED, l, r, s);
    } else if (r - l <= bmcx) {
        rc.left = 0;
        rc.top = 0;
        rc.right = r - l + 4;
        rc.bottom = tcy + 4;
        emboss(mem, 0, 0, &rc, s);
        BitBlt(dc, l - 2, y0 - 2, r - l + 4, tcy + 4, mem, 0, 0, SRCCOPY);
    } else {
        rc.left = l - 2;
        rc.top = y0 - 2;
        rc.right = r + 2;
        rc.bottom = y0 + tcy + 2;
        emboss(dc, l - 2, y0 - 2, &rc, s);
    }
}

static BOOL same(const char *a, const char *b)
{
    while (*a && *a == *b) {
        a++;
        b++;
    }
    return *a == *b;
}

/* whether a tick has anything to draw */
BOOL digi_changed(const CLOCKTIME *t)
{
    char date[32];
    if (!valid || t->hour != last_hour || t->minute != last_min ||
        (withsecs && t->second != last_sec))
        return TRUE;
    if (!withdate)
        return FALSE;
    intl_date(t, date, TRUE);
    return !same(date, last_date);
}

/* all: everything (after the background was erased); otherwise only what
   changed since the last call */
void digi_paint(HDC dc, const CLOCKTIME *t, BOOL all)
{
    char hh[4], mm[4], ss[4], ap[16], sep[4];
    HBITMAP obm = NULL;
    if (!valid || IsIconic(g_wnd) != icon)
        layout(dc, t);
    if (all)
        last_hour = last_min = last_sec = -1;
    intl_time(t, hh, mm, ss, ap);
    SelectObject(dc, tfont);
    SetBkColor(dc, GetSysColor(COLOR_BTNFACE));
    SetTextAlign(dc, TA_TOP | TA_LEFT);
    mem = NULL;
    if (embossed) {
        mem = CreateCompatibleDC(dc);
        obm = SelectObject(mem, bm);
        SelectObject(mem, tfont);
        SetBkColor(mem, GetSysColor(COLOR_BTNFACE));
        SetTextAlign(mem, TA_TOP | TA_LEFT);
    }
    if (all) {
        SelectObject(dc, g_brface);     /* for the date's line */
        s_cpy(sep, g_sep, sizeof sep);
        part(dc, p_s1, p_mm, sep);
        if (withsecs)
            part(dc, p_s2, p_ss, sep);
    }
    /* what changed: the hour counts from 0 to 23, so 10 PM to 10 AM
       redraws AM/PM and the hour (observed) */
    if (!icon && p_ap < nmeas && (last_hour < 0 || (last_hour < 12) != (t->hour < 12)))
        part(dc, p_ap, nmeas, ap);
    if (withsecs && t->second != last_sec)
        part(dc, p_ss, p_ss + 2, ss);
    if (t->minute != last_min)
        part(dc, p_mm, p_mm + 2, mm);
    if (t->hour != last_hour)
        part(dc, p_hh, p_hh + 2, hh);
    if (mem) {
        SelectObject(mem, obm);
        DeleteDC(mem);
        mem = NULL;
    }
    last_hour = t->hour;
    last_min = t->minute;
    last_sec = t->second;
    if (withdate) {
        char date[32];
        intl_date(t, date, TRUE);
        if (all || !same(date, last_date)) {
            RECT rc;
            PatBlt(dc, 0, ydate, ccx, dcy, PATCOPY);
            SelectObject(dc, dfont);
            SetTextColor(dc, GetSysColor(COLOR_BTNTEXT));
            rc.left = xdate;
            rc.top = ydate;
            rc.right = xdate + dcx;
            rc.bottom = ydate + dcy;
            ExtTextOut(dc, xdate, ydate, ETO_OPAQUE | ETO_CLIPPED, &rc, date, s_len(date), NULL);
            s_cpy(last_date, date, sizeof last_date);
            SelectObject(dc, tfont);
        }
    }
    if (icon && all)
        icon_frame(dc);
}
