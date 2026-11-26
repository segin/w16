/*
 * pres.c - presentations: what an object looks like, its bounds, OleDraw,
 * OleGetData's picture formats, OleEnumFormats (ARCHITECTURE.md 16.5).
 *
 * Kinds: a metafile picture (CF_METAFILEPICT, the usual one), a bitmap, a
 * DIB, or any other format (generic). Paintbrush objects are special
 * (OLECLI's built-in handler, the PBxxx exports): their Native data is a
 * DIB file, and OLECLI draws and measures it itself and asks the server
 * for no picture (observed: only Native is advised and requested).
 */
#include "olecli.h"

#define HIMETRIC_PER_M 100000L

HANDLE copy_global(HGLOBAL h)
{
    DWORD n;
    HGLOBAL c;
    if (!h)
        return NULL;
    n = GlobalSize(h);
    c = GlobalAlloc(GMEM_MOVEABLE | GMEM_DDESHARE | GMEM_ZEROINIT, n ? n : 1);
    if (!c)
        return NULL;
    hmemcpy(GlobalLock(c), GlobalLock(h), n);
    GlobalUnlock(h);
    GlobalUnlock(c);
    return c;
}

HMETAFILE copy_metafile(HMETAFILE mf)
{
    return CopyMetaFile(mf, NULL);
}

HBITMAP copy_bitmap(HBITMAP b)
{
    BITMAP bm;
    HBITMAP c;
    DWORD n;
    HGLOBAL h;
    if (!GetObject(b, sizeof bm, &bm))
        return NULL;
    n = (DWORD)bm.bmWidthBytes * bm.bmHeight * bm.bmPlanes;
    h = GlobalAlloc(GMEM_MOVEABLE, n ? n : 1);
    if (!h)
        return NULL;
    GetBitmapBits(b, n, GlobalLock(h));
    c = CreateBitmap(bm.bmWidth, bm.bmHeight, bm.bmPlanes, bm.bmBitsPixel, GlobalLock(h));
    GlobalUnlock(h);
    GlobalUnlock(h);
    GlobalFree(h);
    if (c) {
        DWORD dim = GetBitmapDimension(b);
        SetBitmapDimension(c, LOWORD(dim), HIWORD(dim));
    }
    return c;
}

HANDLE pres_copy(WORD fmt, HANDLE h)
{
    if (!h)
        return NULL;
    if (fmt == CF_METAFILEPICT) {
        HGLOBAL c = copy_global(h);
        METAFILEPICT FAR *m;
        if (!c)
            return NULL;
        m = (METAFILEPICT FAR *)GlobalLock(c);
        m->hMF = copy_metafile(m->hMF);
        GlobalUnlock(c);
        return c;
    }
    if (fmt == CF_BITMAP)
        return copy_bitmap(h);
    if (fmt == CF_PALETTE) {
        WORD n = 0;
        HGLOBAL lp;
        LOGPALETTE FAR *p;
        HPALETTE r;
        GetObject(h, sizeof n, &n);
        lp = GlobalAlloc(GMEM_MOVEABLE, 4 + n * sizeof(PALETTEENTRY));
        if (!lp)
            return NULL;
        p = (LOGPALETTE FAR *)GlobalLock(lp);
        p->palVersion = 0x300;
        p->palNumEntries = n;
        GetPaletteEntries(h, 0, n, p->palPalEntry);
        r = CreatePalette(p);
        GlobalUnlock(lp);
        GlobalFree(lp);
        return r;
    }
    return copy_global(h);
}

static void free_pres_handle(WORD pres, HANDLE h)
{
    if (!h)
        return;
    if (pres == PRES_MF) {
        METAFILEPICT FAR *m = (METAFILEPICT FAR *)GlobalLock(h);
        if (m) {
            DeleteMetaFile(m->hMF);
            GlobalUnlock(h);
        }
        GlobalFree(h);
    } else if (pres == PRES_BM)
        DeleteObject(h);
    else
        GlobalFree(h);
}

void pres_free(LPOBJ o)
{
    free_pres_handle(o->pres, o->hpres);
    o->hpres = NULL;
    o->pres = PRES_NONE;
    o->presfmt = 0;
}

void pres_set(LPOBJ o, WORD cf, HANDLE h)
{
    if (cf == CF_PALETTE) {             /* not a presentation of its own */
        DeleteObject(h);
        return;
    }
    pres_free(o);
    o->hpres = h;
    o->presfmt = cf;
    o->pres = cf == CF_METAFILEPICT ? PRES_MF : cf == CF_BITMAP ? PRES_BM
            : cf == CF_DIB ? PRES_DIB : PRES_GEN;
}

/* ---- the DIB of a Paintbrush object (its Native data: a .BMP file) ---- */

static WORD dib_colors(BITMAPINFOHEADER FAR *b)
{
    if (b->biClrUsed)
        return (WORD)b->biClrUsed;
    if (b->biBitCount <= 8)
        return 1 << b->biBitCount;
    return 0;
}

/* the header of a locked DIB of n bytes at offset off of its segment, or
   NULL when the header, the colours or (uncompressed) the bits are not
   all there: such a picture is blank */
static BITMAPINFOHEADER FAR *dib_info(BYTE FAR *p, DWORD n, WORD off)
{
    BITMAPINFOHEADER FAR *b = (BITMAPINFOHEADER FAR *)p;
    DWORD need, row, h;
    if (!p || n < sizeof *b || b->biSize < sizeof *b || b->biWidth <= 0)
        return NULL;
    if (b->biClrUsed > (b->biBitCount <= 8 ? 256 : 0x3FC0))
        return NULL;
    need = b->biSize + (DWORD)dib_colors(b) * sizeof(RGBQUAD);
    if (need > n || need + off > 0xFFFFL)
        return NULL;
    if (b->biCompression == BI_RGB) {
        if (b->biWidth > 0xFFFFL)
            return NULL;
        h = b->biHeight < 0 ? -b->biHeight : b->biHeight;
        row = ((DWORD)b->biWidth * b->biBitCount + 31) / 32 * 4;
        if (h && row > (n - need) / h)
            return NULL;
    }
    return b;
}

/* the BITMAPINFOHEADER inside a locked .BMP file, or NULL */
static BITMAPINFOHEADER FAR *bmp_info(BYTE FAR *p, DWORD n)
{
    if (!p || n < 14 + sizeof(BITMAPINFOHEADER) || p[0] != 'B' || p[1] != 'M')
        return NULL;
    return dib_info(p + 14, n - 14, 14);
}

/* a picture's size in HIMETRIC from its pixels and resolution (observed:
   640x442 at 3780 pixels per metre gives 16931 x 11693) */
static void dib_himetric(BITMAPINFOHEADER FAR *b, LONG FAR *cx, LONG FAR *cy)
{
    LONG xr = b->biXPelsPerMeter, yr = b->biYPelsPerMeter;
    if (xr <= 0)                        /* UNVERIFIED: no resolution given */
        xr = 3780;
    if (yr <= 0)
        yr = 3780;
    *cx = b->biWidth * HIMETRIC_PER_M / xr;
    *cy = b->biHeight * HIMETRIC_PER_M / yr;
}

static HGLOBAL pb_dib(LPOBJ o)
{
    return o->hnative;
}

/* ---- bounds ---- */

OLESTATUS pres_bounds(LPOBJ o, RECT FAR *r)
{
    SetRect(r, 0, 0, 0, 0);
    if (o->pb && pb_dib(o)) {
        HGLOBAL h = pb_dib(o);
        BYTE FAR *p = (BYTE FAR *)GlobalLock(h);
        BITMAPINFOHEADER FAR *b = bmp_info(p, GlobalSize(h));
        LONG cx, cy;
        if (!b) {
            if (p)
                GlobalUnlock(h);
            return OLE_ERROR_BLANK;
        }
        dib_himetric(b, &cx, &cy);
        GlobalUnlock(h);
        r->right = (int)cx;
        r->bottom = (int)-cy;
        return OLE_OK;
    }
    if (!o->hpres)
        return OLE_ERROR_BLANK;
    switch (o->pres) {
    case PRES_MF:
        {
            METAFILEPICT FAR *m = (METAFILEPICT FAR *)GlobalLock(o->hpres);
            if (!m)
                return OLE_ERROR_BLANK;
            r->right = m->xExt;
            r->bottom = -m->yExt;
            GlobalUnlock(o->hpres);
            return OLE_OK;
        }
    case PRES_DIB:
        {
            BYTE FAR *p = (BYTE FAR *)GlobalLock(o->hpres);
            BITMAPINFOHEADER FAR *b = dib_info(p, GlobalSize(o->hpres), 0);
            LONG cx, cy;
            if (!b) {
                if (p)
                    GlobalUnlock(o->hpres);
                return OLE_ERROR_BLANK;
            }
            dib_himetric(b, &cx, &cy);
            GlobalUnlock(o->hpres);
            r->right = (int)cx;
            r->bottom = (int)-cy;
            return OLE_OK;
        }
    case PRES_BM:
        {
            /* UNVERIFIED: the bitmap's dimension (0.1 mm) if set, else its
               pixels at 96 per inch */
            BITMAP bm;
            DWORD dim = GetBitmapDimension(o->hpres);
            GetObject(o->hpres, sizeof bm, &bm);
            if (dim) {
                r->right = LOWORD(dim) * 10;
                r->bottom = -(int)(HIWORD(dim) * 10);
            } else {
                r->right = MulDiv(bm.bmWidth, 2540, 96);
                r->bottom = -MulDiv(bm.bmHeight, 2540, 96);
            }
            return OLE_OK;
        }
    }
    return OLE_ERROR_GENERIC;
}

/* ---- drawing ---- */

static LPOBJ g_drawing;
static WORD g_records;
static BOOL g_abort;

/* OLECLI.102: plays a metafile record; every so many records the client
   may stop the drawing (OLE_QUERY_PAINT; UNVERIFIED how often) */
int OLEAPI MfCallBackFunc(HDC dc, HANDLETABLE FAR *t, METARECORD FAR *r, int n,
                          LPARAM lp)
{
    (void)lp;
    PlayMetaFileRecord(dc, t, r, n);
    if (++g_records % 64 == 0 && g_drawing && g_drawing->client &&
        g_drawing->client->lpvtbl && g_drawing->client->lpvtbl->CallBack) {
        if (!g_drawing->client->lpvtbl->CallBack(g_drawing->client, OLE_QUERY_PAINT,
                                                 (LPOLEOBJECT)g_drawing)) {
            g_abort = TRUE;
            return 0;
        }
    }
    return 1;
}

static HPALETTE dib_palette(BITMAPINFOHEADER FAR *b)
{
    WORD n = dib_colors(b), i;
    HGLOBAL h;
    LOGPALETTE FAR *p;
    RGBQUAD FAR *q = (RGBQUAD FAR *)((BYTE FAR *)b + b->biSize);
    HPALETTE r;
    if (!n)
        return NULL;
    h = GlobalAlloc(GMEM_MOVEABLE, 4 + (DWORD)n * sizeof(PALETTEENTRY));
    if (!h)
        return NULL;
    p = (LOGPALETTE FAR *)GlobalLock(h);
    if (!p) {
        GlobalFree(h);
        return NULL;
    }
    p->palVersion = 0x300;
    p->palNumEntries = n;
    for (i = 0; i < n; i++) {
        p->palPalEntry[i].peRed = q[i].rgbRed;
        p->palPalEntry[i].peGreen = q[i].rgbGreen;
        p->palPalEntry[i].peBlue = q[i].rgbBlue;
        p->palPalEntry[i].peFlags = 0;
    }
    r = CreatePalette(p);
    GlobalUnlock(h);
    GlobalFree(h);
    return r;
}

/* a DIB (header, colours, bits) into the rectangle, with its palette */
static void draw_dib(HDC dc, BITMAPINFOHEADER FAR *b, int x, int y, int cx, int cy)
{
    HPALETTE pal = dib_palette(b), old = NULL;
    BYTE FAR *bits = (BYTE FAR *)b + b->biSize + (DWORD)dib_colors(b) * sizeof(RGBQUAD);
    if (pal) {
        old = SelectPalette(dc, pal, FALSE);
        RealizePalette(dc);
    }
    StretchDIBits(dc, x, y, cx, cy, 0, 0, (int)b->biWidth, (int)b->biHeight, bits,
                  (BITMAPINFO FAR *)b, DIB_RGB_COLORS, SRCCOPY);
    if (pal) {
        /* the stock palette back (observed in the metafile: a second
           CreatePalette record of 20 entries), then the DIB's deleted */
        (void)old;
        SelectPalette(dc, GetStockObject(DEFAULT_PALETTE), FALSE);
        RealizePalette(dc);
        DeleteObject(pal);
    }
}

OLESTATUS pres_draw(LPOBJ o, HDC dc, const RECT FAR *r, HDC fmt)
{
    (void)fmt;
    if (o->pb && pb_dib(o)) {
        HGLOBAL h = pb_dib(o);
        BYTE FAR *p = (BYTE FAR *)GlobalLock(h);
        BITMAPINFOHEADER FAR *b = bmp_info(p, GlobalSize(h));
        if (!b) {
            if (p)
                GlobalUnlock(h);
            return OLE_ERROR_BLANK;
        }
        draw_dib(dc, b, r->left, r->top, r->right - r->left, r->bottom - r->top);
        GlobalUnlock(h);
        return OLE_OK;
    }
    if (!o->hpres)
        return OLE_ERROR_BLANK;
    switch (o->pres) {
    case PRES_MF:
        {
            METAFILEPICT FAR *m;
            OLESTATUS s = OLE_OK;
            LPOBJ prev = g_drawing;
            WORD prev_records = g_records;
            BOOL prev_abort = g_abort;
            SaveDC(dc);
            IntersectClipRect(dc, r->left, r->top, r->right, r->bottom);
            if (OleIsDcMeta(dc))
                /* observed: drawing a metafile picture into a metafile
                   fails, after the SaveDC and the clip */
                return OLE_ERROR_DRAW;
            m = (METAFILEPICT FAR *)GlobalLock(o->hpres);
            if (!m) {
                RestoreDC(dc, -1);
                return OLE_ERROR_BLANK;
            }
            SetMapMode(dc, MM_ANISOTROPIC);
            SetViewportOrg(dc, r->left, r->top);
            SetViewportExt(dc, r->right - r->left, r->bottom - r->top);
            g_drawing = o;
            g_records = 0;
            g_abort = FALSE;
            EnumMetaFile(dc, m->hMF, (MFENUMPROC)MfCallBackFunc, 0);
            if (g_abort)
                s = OLE_ERROR_ABORT;
            /* an OleDraw from the client's OLE_QUERY_PAINT has its own */
            g_drawing = prev;
            g_records = prev_records;
            g_abort = prev_abort;
            GlobalUnlock(o->hpres);
            RestoreDC(dc, -1);
            return s;
        }
    case PRES_DIB:
        {
            BYTE FAR *p = (BYTE FAR *)GlobalLock(o->hpres);
            BITMAPINFOHEADER FAR *b = dib_info(p, GlobalSize(o->hpres), 0);
            if (!b) {
                if (p)
                    GlobalUnlock(o->hpres);
                return OLE_ERROR_BLANK;
            }
            draw_dib(dc, b, r->left, r->top, r->right - r->left, r->bottom - r->top);
            GlobalUnlock(o->hpres);
            return OLE_OK;
        }
    case PRES_BM:
        {
            HDC mem = CreateCompatibleDC(dc);
            BITMAP bm;
            HBITMAP old;
            if (!mem)
                return OLE_ERROR_MEMORY;
            GetObject(o->hpres, sizeof bm, &bm);
            old = SelectObject(mem, o->hpres);
            StretchBlt(dc, r->left, r->top, r->right - r->left, r->bottom - r->top, mem, 0,
                       0, bm.bmWidth, bm.bmHeight, SRCCOPY);
            SelectObject(mem, old);
            DeleteDC(mem);
            return OLE_OK;
        }
    }
    return OLE_ERROR_GENERIC;           /* UNVERIFIED: a generic presentation */
}

/* ---- data ---- */

BOOL pres_has_picture(LPOBJ o)
{
    return (o->pb && o->hnative) || (o->hpres && o->pres != PRES_GEN);
}

/* a metafile picture of the object's DIB, as OLECLI makes it for a
   Paintbrush object (the records observed: window origin and extent,
   the DIB's palette, StretchDIBits, the stock palette back) */
static HGLOBAL pb_metafile(LPOBJ o)
{
    HGLOBAL h = pb_dib(o), r;
    BYTE FAR *p = (BYTE FAR *)GlobalLock(h);
    BITMAPINFOHEADER FAR *b = bmp_info(p, GlobalSize(h));
    HDC mdc;
    HMETAFILE mf;
    METAFILEPICT FAR *m;
    LONG cx, cy;
    int w, ht;
    if (!b || !(mdc = CreateMetaFile(NULL))) {
        if (p)
            GlobalUnlock(h);
        return NULL;
    }
    w = (int)b->biWidth;
    ht = (int)b->biHeight;
    SetWindowOrg(mdc, 0, 0);
    SetWindowExt(mdc, w, ht);
    draw_dib(mdc, b, 0, 0, w, ht);
    mf = CloseMetaFile(mdc);
    dib_himetric(b, &cx, &cy);
    GlobalUnlock(h);
    r = GlobalAlloc(GMEM_MOVEABLE | GMEM_DDESHARE, sizeof(METAFILEPICT));
    if (!r) {
        DeleteMetaFile(mf);
        return NULL;
    }
    m = (METAFILEPICT FAR *)GlobalLock(r);
    m->mm = MM_ANISOTROPIC;
    m->xExt = (int)cx;
    m->yExt = (int)cy;
    m->hMF = mf;
    GlobalUnlock(r);
    return r;
}

OLESTATUS pres_getdata(LPOBJ o, OLECLIPFORMAT f, HANDLE FAR *h)
{
    if (o->pb) {
        if (f != CF_METAFILEPICT)
            return OLE_ERROR_FORMAT;
        if (!o->hnative)
            return OLE_ERROR_BLANK;
        *h = pb_metafile(o);
        return *h ? OLE_WARN_DELETE_DATA : OLE_ERROR_MEMORY;
    }
    if (!o->hpres) {
        WORD pf = obj_advise_pres(o);
        if (pf && f == pf)
            return OLE_ERROR_BLANK;
        return OLE_ERROR_FORMAT;
    }
    if (f != o->presfmt)
        return OLE_ERROR_FORMAT;
    *h = o->hpres;
    return OLE_OK;
}

/* the formats an object offers, in order (observed: embedded "Native",
   "OwnerLink", the picture; a link "ObjectLink", "Native", the picture) */
OLECLIPFORMAT pres_enum(LPOBJ o, OLECLIPFORMAT f)
{
    WORD list[4], n = 0, i;
    WORD pf = o->pb ? CF_METAFILEPICT : o->hpres ? o->presfmt : obj_advise_pres(o);
    if (o->type == OT_EMBEDDED) {
        list[n++] = cf_native;
        list[n++] = cf_ownerlink;
    } else if (o->type == OT_LINK) {
        list[n++] = cf_objectlink;
        list[n++] = cf_native;
    }
    if (pf)
        list[n++] = pf;
    if (!f)
        return n ? list[0] : 0;
    for (i = 0; i + 1 < n; i++)
        if (list[i] == f)
            return list[i + 1];
    return 0;
}

/* a static object keeps only a picture */
void pres_make_static(LPOBJ o)
{
    if (o->pb && o->hnative) {
        HGLOBAL mf = pb_metafile(o);
        if (mf)
            pres_set(o, CF_METAFILEPICT, mf);
    }
    gfree(o->hnative);
    o->hnative = NULL;
    o->pb = FALSE;
    gfree(o->hclass);
    o->hclass = gstr_new(o->pres == PRES_MF ? "METAFILEPICT" : o->pres == PRES_BM ? "BITMAP"
                         : "DIB");
}

/* "class\0topic\0item\0\0" (OwnerLink, ObjectLink) */
HGLOBAL link_data(LPOBJ o)
{
    LPSTR c = gstr(o->hclass), t = gstr(o->htopic), i = gstr(o->hitem);
    WORD nc = slen(c), nt = slen(t), ni = slen(i);
    HGLOBAL h = GlobalAlloc(GMEM_MOVEABLE | GMEM_DDESHARE, nc + nt + ni + 4);
    if (h) {
        LPSTR p = (LPSTR)GlobalLock(h);
        hmemcpy(p, c, nc + 1);
        hmemcpy(p + nc + 1, t, nt + 1);
        hmemcpy(p + nc + nt + 2, i, ni + 1);
        p[nc + nt + ni + 3] = 0;
        GlobalUnlock(h);
    }
    gstr_unlock(o->hclass);
    gstr_unlock(o->htopic);
    gstr_unlock(o->hitem);
    return h;
}
