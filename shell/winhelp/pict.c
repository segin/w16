/*
 * pict.c - w16 Windows Help: pictures in topics (\bmc, \bml, \bmr and
 * their embedded forms).
 *
 * Format (hlp.h's sources): a picture is either embedded in the topic or
 * the internal file |bm<n>; either way it starts with the SHG/MRB header:
 * a magic word (506Ch or 706Ch), the number of pictures (more than one:
 * a multi-resolution bitmap), and each picture's offset. A picture is a
 * type byte (5 device-dependent bitmap, 6 DIB, 8 metafile), a packing
 * byte (0 none, 1 run lengths, 2 LZ77, 3 LZ77 then run lengths), the
 * header in compressed numbers (resolution in dpi, planes, bits per
 * pixel, width, height, colours used, colours important, data size, hot
 * spot data size, then the data's and the hot spots' offsets as plain
 * longs), a DIB's colour table, the data and the hot spots.
 *
 * Of several resolutions w16 takes the one whose dpi matches the
 * display's (VGA: 96) with the most colours the display has, else the
 * first. Metafile pictures (type 8): the data is a Windows metafile,
 * played (SetMetaFileBits, PlayMetaFile) in MM_ANISOTROPIC as the SDK
 * says Help does (Vol. 4, ch. 15, \bmc), its window extent the
 * picture's width and height, its viewport the picture's size in pixels
 * (those numbers taken as 0.01 mm: UNVERIFIED, no reference help file
 * has a metafile of its own; W16TEST.HLP's, an SHG holding a metafile,
 * shows 3.1 sizing them so: WS07). They draw on 3.1's GDI and on w16's
 * (gdi/meta.c).
 */
#include "winhelp.h"

typedef struct {
    BYTE type, pack;
    DWORD xdpi, ydpi;
    WORD planes, bits;
    DWORD w, h, used, important, csize, hsize, coff, hoff;
    LPCB pal;           /* DIB colour table */
    WORD mm;            /* metafile mapping mode */
    DWORD dsize;
} PHDR;

static BOOL parse(LPCB base, DWORD avail, DWORD off, PHDR *p)
{
    LPCB q = base + off;
    if (off + 2 > avail)
        return FALSE;
    p->type = q[0];
    p->pack = q[1];
    q += 2;
    if (p->type == 5 || p->type == 6) {
        p->xdpi = hlp_culong(&q);
        p->ydpi = hlp_culong(&q);
        p->planes = hlp_cuword(&q);
        p->bits = hlp_cuword(&q);
        p->w = hlp_culong(&q);
        p->h = hlp_culong(&q);
        p->used = hlp_culong(&q);
        p->important = hlp_culong(&q);
        p->csize = hlp_culong(&q);
        p->hsize = hlp_culong(&q);
        p->coff = hlp_rl(q);
        p->hoff = hlp_rl(q + 4);
        q += 8;
        p->pal = q;
    } else if (p->type == 8) {
        p->mm = hlp_cuword(&q);
        p->w = hlp_rw(q);
        p->h = hlp_rw(q + 2);
        q += 4;
        p->dsize = hlp_culong(&q);
        p->csize = hlp_culong(&q);
        p->hsize = hlp_culong(&q);
        p->coff = hlp_rl(q);
        p->hoff = hlp_rl(q + 4);
    } else {
        return FALSE;
    }
    return off + p->coff + p->csize <= avail;
}

/* unpack into a new block of n bytes */
static LPB unpack(LPCB src, DWORD cs, BYTE pack, DWORD n)
{
    LPB d = (LPB)hio_alloc(n + 16);
    if (!d)
        return NULL;
    switch (pack) {
    case 0:
        hlp_memcpy(d, src, cs < n ? cs : n);
        break;
    case 1:
        hlp_runlen(src, cs, d, n);
        break;
    case 2:
        hlp_lz77(src, cs, d, n);
        break;
    case 3: {
        DWORD max = n * 2 + 1024;
        LPB t;
        if (max > 0xFF00UL)
            max = 0xFF00UL;
        t = (LPB)hio_alloc(max);
        if (!t) {
            hio_free(d);
            return NULL;
        }
        hlp_runlen(t, hlp_lz77(src, cs, t, max), d, n);
        hio_free(t);
        break;
    }
    }
    return d;
}

extern short view_addhot(VIEW *v, BYTE code, DWORD hash, LPCSTR str);

/* the SHG hot spots: a byte (1), their number, the size of the macro
   data; per spot three type bytes, x, y, w, h and a hash; the macro
   data; per spot two strings (its name, and its context or macro) */
static void hotspots(VIEW *v, VPICT FAR *pp, LPCB q, DWORD n)
{
    WORD nh, i;
    DWORD msz;
    LPCB s, e = q + n;
    if (n < 7 || q[0] != 1 || !v)
        return;
    nh = hlp_rw(q + 1);
    msz = hlp_rl(q + 3);
    s = q + 7 + (DWORD)nh * 15 + msz;
    pp->hotfirst = v->nshg;
    for (i = 0; i < nh && s < e; i++) {
        LPCB h = q + 7 + (DWORD)i * 15;
        const char FAR *name = (const char FAR *)s, FAR *ctx;
        BYTE code = h[0];
        short hot;
        s += hlp_strlen(name) + 1;
        ctx = (const char FAR *)s;
        s += hlp_strlen(ctx) + 1;
        if (code == 0xC8 || code == 0xCC)
            hot = view_addhot(v, code, 0, ctx);
        else if (code == 0xE2 || code == 0xE3 || code == 0xE6 || code == 0xE7)
            hot = view_addhot(v, code, hlp_rl(h + 11), NULL);
        else {
            /* context>window@file */
            char c[80], f[PATHMAX + 20];
            WORD k = 0, j = 0;
            const char FAR *t = ctx;
            while (*t && *t != '>' && *t != '@' && k < sizeof c - 1)
                c[k++] = *t++;
            c[k] = 0;
            f[0] = 0;
            {
                const char FAR *at = ctx, FAR *gt = NULL;
                while (*at && *at != '@') {
                    if (*at == '>')
                        gt = at;
                    at++;
                }
                if (*at == '@')
                    for (at++; *at && j < PATHMAX; )
                        f[j++] = *at++;
                if (gt) {
                    f[j++] = '>';
                    for (gt++; *gt && *gt != '@' && j < sizeof f - 1; )
                        f[j++] = *gt++;
                }
                f[j] = 0;
            }
            hot = view_addhot(v, code, (DWORD)hlp_hash(c), f[0] ? (LPCSTR)f : (LPCSTR)NULL);
        }
        if (v->nshg < v->maxshg) {
            SHGHOT FAR *g = &v->shg[v->nshg++];
            g->x = (short)hlp_rw(h + 3);
            g->y = (short)hlp_rw(h + 5);
            g->cx = (short)hlp_rw(h + 7);
            g->cy = (short)hlp_rw(h + 9);
            if (pp->mfx && pp->mfy) {
                /* a metafile's hot spots are in its own units (WS07:
                   0-2540 across a 5080-unit picture is its left half) */
                g->x = (short)((long)g->x * pp->cx / pp->mfx);
                g->cx = (short)((long)g->cx * pp->cx / pp->mfx);
                g->y = (short)((long)g->y * pp->cy / pp->mfy);
                g->cy = (short)((long)g->cy * pp->cy / pp->mfy);
            }
            g->hot = hot;
            pp->nhot++;
        }
    }
}

static BOOL make(LPCB base, DWORD avail, DWORD off, VPICT FAR *pp, VIEW *v)
{
    PHDR p;
    HDC dc;
    DWORD stride, n;
    LPB bits;
    if (!parse(base, avail, off, &p))
        return FALSE;
    pp->cx = (short)p.w;
    pp->cy = (short)p.h;
    if (p.type == 8) {
        /* metafiles: not drawn until GDI has PlayMetaFile (see above);
           Width and Height are in the metafile's units: MM_ANISOTROPIC
           pictures are sized in 0.01 mm (HIMETRIC-like), w16 converts at
           96 dpi */
        {
            HDC sdc = GetDC(NULL);
            int dpx = GetDeviceCaps(sdc, LOGPIXELSX), dpy = GetDeviceCaps(sdc, LOGPIXELSY);
            ReleaseDC(NULL, sdc);
            pp->cx = (short)((long)p.w * dpx / 2540);
            pp->cy = (short)((long)p.h * dpy / 2540);
        }
        pp->mfx = (short)p.w;
        pp->mfy = (short)p.h;
        if (p.dsize && p.dsize < 0xFF00UL) {
            LPB bits = unpack(base + off + p.coff, p.csize, p.pack, p.dsize);
            if (bits) {
                HGLOBAL g = GlobalAlloc(GMEM_MOVEABLE, p.dsize);
                if (g) {
                    LPB d = (LPB)GlobalLock(g);
                    hlp_memcpy(d, bits, p.dsize);
                    GlobalUnlock(g);
                    pp->mf = (HGLOBAL)SetMetaFileBits(g);
                    if (!pp->mf)
                        GlobalFree(g);
                }
                hio_free(bits);
            }
        }
        if (p.hsize && p.hoff)
            hotspots(v, pp, base + off + p.hoff, p.hsize);
        return TRUE;
    }
    if (p.planes != 1 && p.type == 6)
        return FALSE;
    if (p.type == 6)
        stride = ((p.w * p.bits + 31) / 32) * 4;
    else
        stride = ((p.w * p.bits + 15) / 16) * 2 * p.planes;
    n = stride * p.h;
    if (!n || n > 0xFF00UL)
        return FALSE;
    bits = unpack(base + off + p.coff, p.csize, p.pack, n);
    if (!bits)
        return FALSE;
    dc = GetDC(NULL);
    if (p.type == 6) {
        WORD nc = p.bits <= 8 ? (WORD)(p.used ? p.used : 1u << p.bits) : 0, i;
        BITMAPINFO FAR *bi = (BITMAPINFO FAR *)hio_alloc(sizeof(BITMAPINFOHEADER) + 4 * (DWORD)(nc ? nc : 1));
        if (bi) {
            bi->bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
            bi->bmiHeader.biWidth = p.w;
            bi->bmiHeader.biHeight = p.h;
            bi->bmiHeader.biPlanes = 1;
            bi->bmiHeader.biBitCount = p.bits;
            bi->bmiHeader.biCompression = BI_RGB;
            bi->bmiHeader.biClrUsed = nc;
            for (i = 0; i < nc; i++) {
                LPCB c = p.pal + 4 * i;
                bi->bmiColors[i].rgbBlue = c[0];
                bi->bmiColors[i].rgbGreen = c[1];
                bi->bmiColors[i].rgbRed = c[2];
                bi->bmiColors[i].rgbReserved = 0;
            }
            pp->bm = CreateDIBitmap(dc, &bi->bmiHeader, CBM_INIT, bits, bi, DIB_RGB_COLORS);
            hio_free(bi);
        }
    } else {
        pp->bm = CreateBitmap((int)p.w, (int)p.h, (BYTE)p.planes, (BYTE)p.bits, bits);
    }
    ReleaseDC(NULL, dc);
    hio_free(bits);
    if (p.hsize && p.hoff && p.hoff + p.hsize + off <= avail)
        hotspots(v, pp, base + off + p.hoff, p.hsize);
    return pp->bm != NULL;
}

/* which of a multi-resolution set to take */
static WORD choose(LPCB base, DWORD avail, WORD n)
{
    HDC dc = GetDC(NULL);
    int dpi = GetDeviceCaps(dc, LOGPIXELSX);
    int colors = GetDeviceCaps(dc, BITSPIXEL) * GetDeviceCaps(dc, PLANES);
    WORD i, best = 0;
    int score = -1;
    ReleaseDC(NULL, dc);
    for (i = 0; i < n; i++) {
        PHDR p;
        int s = 0;
        if (!parse(base, avail, hlp_rl(base + 4 + 4 * i), &p))
            continue;
        if (p.type == 8)
            s = 1;
        else {
            if ((int)p.xdpi == dpi)
                s += 100;
            if ((int)(p.bits * p.planes) <= colors)
                s += 10 + p.bits * p.planes;
        }
        if (s > score) {
            score = s;
            best = i;
        }
    }
    return best;
}

BOOL pict_load(HLP FAR *h, LPCB data, DWORD n, BOOL embedded, VPICT FAR *pp, VIEW *v)
{
    LPB buf = NULL;
    LPCB base;
    DWORD avail;
    WORD cnt;
    BOOL ok;
    (void)embedded;
    if (n < 4)
        return FALSE;
    if (hlp_rw(data) == 0) {
        /* by reference: |bm<n> */
        char name[16];
        DWORD f;
        wsprintf(name, "|bm%u", hlp_rw(data + 2));
        f = hlp_file(h, name);
        if (!f) {
            wsprintf(name, "bm%u", hlp_rw(data + 2));
            f = hlp_file(h, name);
        }
        if (!f)
            return FALSE;
        avail = hlp_filesize(h, f);
        if (avail > 0xFF00UL || avail < 8)
            return FALSE;
        buf = (LPB)hio_alloc(avail);
        if (!buf)
            return FALSE;
        hlp_fread(h, f, 0, buf, (WORD)avail);
        base = buf;
    } else {
        base = data + 2;
        avail = n - 2;
    }
    cnt = hlp_rw(base + 2);
    if ((hlp_rw(base) != 0x506C && hlp_rw(base) != 0x706C) || !cnt) {
        if (buf)
            hio_free(buf);
        return FALSE;
    }
    ok = make(base, avail, hlp_rl(base + 4 + 4 * choose(base, avail, cnt)), pp, v);
    if (buf)
        hio_free(buf);
    return ok;
}

void pict_free(VPICT FAR *p)
{
    if (p->bm)
        DeleteObject(p->bm);
    if (p->mf)
        DeleteMetaFile((HMETAFILE)p->mf);
    p->bm = NULL;
    p->mf = NULL;
}

void pict_draw(HDC dc, VPICT FAR *p, int x, int y)
{
    HDC m;
    HBITMAP o;
    if (p->mf) {
        int s = SaveDC(dc);
        SetMapMode(dc, MM_ANISOTROPIC);
        SetWindowOrg(dc, 0, 0);
        SetWindowExt(dc, p->mfx, p->mfy);
        SetViewportOrg(dc, x, y);
        SetViewportExt(dc, p->cx, p->cy);
        PlayMetaFile(dc, (HMETAFILE)p->mf);
        RestoreDC(dc, s);
        return;
    }
    if (!p->bm)
        return;
    m = CreateCompatibleDC(dc);
    o = SelectObject(m, p->bm);
    BitBlt(dc, x, y, p->cx, p->cy, m, 0, 0, SRCCOPY);
    SelectObject(m, o);
    DeleteDC(m);
}
