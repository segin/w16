/*
 * iconx.c - w16 Program Manager: reading icons out of files ourselves.
 *
 * Program Manager normally asks SHELL.DLL (ExtractIcon). This is the
 * fallback, and on Windows 2.x (no SHELL.DLL) the only way:
 *   - .ICO files;
 *   - NE files with Windows 3.x icons (RT_GROUP_ICON directories naming
 *     RT_ICON images, DIB format);
 *   - NE files with Windows 2.x icons (RT_ICON alone; observed in the
 *     Windows/386 2.11 programs: a 14-byte header -- word 0101h, hotspot
 *     x and y, cx, cy (64, 64), bytes per row (8), a zero word -- then a
 *     monochrome AND mask and XOR image, top row first);
 *   - PE files (Win32: the same group/icon resources in the PE resource
 *     tree), so Win32 programs show their icons as they do on Windows 3.1
 *     with Win32s.
 * The file formats are the published ones (Microsoft's NE and PE
 * specifications, the .ICO format).
 *
 * The result is the icon in the display's own format, laid out as .GRP
 * files cache it and HICONs hold it (ARCHITECTURE.md 6.11): a 12-byte
 * header (hotspot x, y, cx, cy, bytes per row, planes, bits per pixel),
 * the AND mask (word-aligned rows), the XOR bits. The display format comes
 * from GDI itself: the image is drawn into a bitmap compatible with the
 * screen and read back with GetBitmapBits.
 *
 * No size limits of our own: any image size and colour depth the formats
 * allow is read (row by row, so nothing has to fit in one segment) and
 * scaled to the system's icon size.
 */
#include "progman.h"

#define GROUP_ICON  14                  /* resource types */
#define ICON        3

typedef struct {
    DWORD off, len;                     /* the image's bytes in the file */
    BOOL mono2x;                        /* Windows 2.x format */
} image;

static WORD rw(const BYTE FAR *p) { return p[0] | (p[1] << 8); }
static DWORD rd(const BYTE FAR *p) { return rw(p) | ((DWORD)rw(p + 2) << 16); }

static BOOL get(int fd, DWORD off, void FAR *buf, WORD n)
{
    if (_llseek(fd, off, 0) != (LONG)off)
        return FALSE;
    return _lread(fd, buf, n) == n;
}

/* ---- choosing among an icon's images ---- */

/* Bits per pixel of the display. */
static int display_bits(void)
{
    HDC dc = GetDC(NULL);
    int n = GetDeviceCaps(dc, PLANES) * GetDeviceCaps(dc, BITSPIXEL);
    ReleaseDC(NULL, dc);
    return n;
}

/* How well an image suits (lower is better): the right size first, then
   the most colours the display can show. UNVERIFIED: the original's rule. */
static DWORD fitness(int w, int bits, int want, int disp)
{
    DWORD d = (DWORD)(w > want ? w - want : want - w) << 16;
    if (bits <= disp)
        return d + (64 - bits);
    return d + 64 + bits;
}

/* A directory entry's colour depth: its bit count, or from its colours. */
static int entry_bits(const BYTE FAR *e)
{
    int bits = rw(e + 4) * rw(e + 6);
    if (bits)
        return bits;
    switch (e[2]) {
    case 2: return 1;
    case 16: return 4;
    default: return 8;
    }
}

/* The best of n directory entries (16-byte .ICO entries or 14-byte group
   entries, `stride`). */
static int best_entry(const BYTE FAR *e, int n, int stride)
{
    int want = GetSystemMetrics(SM_CXICON), disp = display_bits(), i, best = 0;
    DWORD f, bestf = 0xFFFFFFFFUL;
    for (i = 0; i < n; i++, e += stride) {
        f = fitness(e[0] ? e[0] : 256, entry_bits(e), want, disp);
        if (f < bestf) {
            bestf = f;
            best = i;
        }
    }
    return best;
}

/* Read a group directory of `len` bytes at `off`; the chosen icon ID. */
static BOOL group_choice(int fd, DWORD off, DWORD len, WORD FAR *id)
{
    BYTE head[6];
    LPSTR dir;
    WORD n;
    if (len < 6 || !get(fd, off, head, 6) || rw(head + 2) != 1)
        return FALSE;
    n = rw(head + 4);
    if (!n || 6 + (DWORD)n * 14 > len)
        return FALSE;
    dir = mem_alloc((DWORD)n * 14);
    if (!dir)
        return FALSE;
    if (!get(fd, off + 6, dir, n * 14)) {
        mem_free(dir);
        return FALSE;
    }
    *id = rw((BYTE FAR *)dir + best_entry((BYTE FAR *)dir, n, 14) * 14 + 12);
    mem_free(dir);
    return TRUE;
}

/* ---- .ICO ---- */

static int ico_find(int fd, int index, image FAR *im)
{
    BYTE head[6];
    LPSTR dir;
    WORD n;
    int b;
    if (!get(fd, 0, head, 6) || rw(head) != 0 || rw(head + 2) != 1 || !(n = rw(head + 4)))
        return 0;
    if (index < 0)
        return 1;                       /* a .ICO file is one icon */
    if (index > 0 || !(dir = mem_alloc((DWORD)n * 16)))
        return 0;
    if (!get(fd, 6, dir, n * 16)) {
        mem_free(dir);
        return 0;
    }
    b = best_entry((BYTE FAR *)dir, n, 16);
    im->len = rd((BYTE FAR *)dir + b * 16 + 8);
    im->off = rd((BYTE FAR *)dir + b * 16 + 12);
    im->mono2x = FALSE;
    mem_free(dir);
    return 1;
}

/* ---- NE ---- */

static int ne_find(int fd, DWORD ne, int index, image FAR *im)
{
    BYTE h[0x40];
    LPSTR tab;
    BYTE FAR *p, FAR *end;
    WORD size, shift, t, n, k;
    int groups = 0, icons = 0, found = -1;
    WORD want = 0;
    BOOL old;

    if (!get(fd, ne, h, sizeof h) || rw(h + 0x24) >= rw(h + 0x26))
        return 0;                       /* no resource table */
    size = rw(h + 0x26) - rw(h + 0x24);
    if (!(tab = mem_alloc(size)))
        return 0;
    if (!get(fd, ne + rw(h + 0x24), tab, size)) {
        mem_free(tab);
        return 0;
    }
    shift = rw((BYTE FAR *)tab);
    end = (BYTE FAR *)tab + size;

    /* First pass: count the group icons (Windows 3.x) and plain icons. */
    for (p = (BYTE FAR *)tab + 2; p + 8 <= end && (t = rw(p)) != 0; p += 8 + n * 12) {
        n = rw(p + 2);
        if (t == (0x8000 | GROUP_ICON))
            groups += n;
        else if (t == (0x8000 | ICON))
            icons += n;
    }
    old = groups == 0;                  /* only Windows 2.x icons */
    if (index < 0) {
        mem_free(tab);
        return old ? icons : groups;
    }

    /* Second pass: the index-th group (or 2.x icon) in table order. */
    for (p = (BYTE FAR *)tab + 2; p + 8 <= end && (t = rw(p)) != 0; p += 8 + n * 12) {
        n = rw(p + 2);
        if (t != (0x8000 | (old ? ICON : GROUP_ICON)))
            continue;
        if (index < n) {
            BYTE FAR *e = p + 8 + index * 12;
            im->off = (DWORD)rw(e) << shift;
            im->len = (DWORD)rw(e + 2) << shift;
            found = 0;
            break;
        }
        index -= n;
    }
    if (found < 0) {
        mem_free(tab);
        return 0;
    }
    if (old) {
        im->mono2x = TRUE;
        mem_free(tab);
        return 1;
    }

    /* The group's chosen image, by ID. */
    if (!group_choice(fd, im->off, im->len, &want)) {
        mem_free(tab);
        return 0;
    }
    found = -1;
    for (p = (BYTE FAR *)tab + 2; p + 8 <= end && (t = rw(p)) != 0; p += 8 + n * 12) {
        n = rw(p + 2);
        if (t != (0x8000 | ICON))
            continue;
        for (k = 0; k < n; k++) {
            BYTE FAR *e = p + 8 + k * 12;
            if (rw(e + 6) == (0x8000 | want)) {
                im->off = (DWORD)rw(e) << shift;
                im->len = (DWORD)rw(e + 2) << shift;
                found = 0;
            }
        }
    }
    mem_free(tab);
    im->mono2x = FALSE;
    return found < 0 ? 0 : 1;
}

/* ---- PE ---- */

typedef struct {
    int fd;
    DWORD base;                         /* file offset of the resource tree */
    DWORD rva;                          /* its RVA */
    DWORD nsec, sec;                    /* section table */
} pe;

/* An RVA as a file offset (0 if outside every section). */
static DWORD pe_off(pe FAR *x, DWORD rva)
{
    BYTE s[40];
    DWORD i;
    for (i = 0; i < x->nsec; i++) {
        if (!get(x->fd, x->sec + i * 40, s, 40))
            return 0;
        if (rva >= rd(s + 12) && rva < rd(s + 12) + rd(s + 16))
            return rva - rd(s + 12) + rd(s + 20);
    }
    return 0;
}

/* Entries of the resource directory at `dir` (an offset in the tree). */
static WORD pe_count(pe FAR *x, DWORD dir)
{
    BYTE d[16];
    if (!get(x->fd, x->base + dir, d, 16))
        return 0;
    return rw(d + 12) + rw(d + 14);
}

/* Entry i of a directory: its name/ID and where it points. */
static BOOL pe_entry(pe FAR *x, DWORD dir, WORD i, DWORD FAR *name, DWORD FAR *to)
{
    BYTE e[8];
    if (!get(x->fd, x->base + dir + 16 + (DWORD)i * 8, e, 8))
        return FALSE;
    *name = rd(e);
    *to = rd(e + 4);
    return TRUE;
}

/* The subdirectory of `dir` for numeric ID id (0xFFFFFFFF if none). */
static DWORD pe_sub(pe FAR *x, DWORD dir, WORD id)
{
    WORD n = pe_count(x, dir), i;
    DWORD name, to;
    for (i = 0; i < n; i++)
        if (pe_entry(x, dir, i, &name, &to) && name == id && (to & 0x80000000UL))
            return to & 0x7FFFFFFFUL;
    return 0xFFFFFFFFUL;
}

/* A name-level entry's data (its first language): file offset, size. */
static BOOL pe_data(pe FAR *x, DWORD to, DWORD FAR *off, DWORD FAR *len)
{
    DWORD name;
    BYTE d[8];
    while (to & 0x80000000UL)            /* down to the first language */
        if (!pe_count(x, to & 0x7FFFFFFFUL) || !pe_entry(x, to & 0x7FFFFFFFUL, 0, &name, &to))
            return FALSE;
    if (!get(x->fd, x->base + to, d, 8))
        return FALSE;
    *off = pe_off(x, rd(d));
    *len = rd(d + 4);
    return *off != 0;
}

static int pe_find(int fd, DWORD hdr, int index, image FAR *im)
{
    BYTE h[24 + 112 + 16 * 3];
    pe x;
    DWORD opt, dirs, ndirs, groups, icons, name, to, off, len;
    WORD want, n, i;

    if (!get(fd, hdr, h, sizeof h))
        return 0;
    opt = 24;
    if (rw(h + opt) == 0x10B) {         /* PE32 */
        ndirs = rd(h + opt + 92);
        dirs = opt + 96;
    } else if (rw(h + opt) == 0x20B) {  /* PE32+ */
        ndirs = rd(h + opt + 108);
        dirs = opt + 112;
    } else {
        return 0;
    }
    if (ndirs < 3 || !rd(h + dirs + 16))
        return 0;                       /* no resources */
    x.fd = fd;
    x.nsec = rw(h + 6);
    x.sec = hdr + 24 + rw(h + 20);
    x.rva = rd(h + dirs + 16);
    x.base = 0;
    x.base = pe_off(&x, x.rva);
    if (!x.base)
        return 0;

    groups = pe_sub(&x, 0, GROUP_ICON);
    if (groups == 0xFFFFFFFFUL)
        return 0;
    n = pe_count(&x, groups);
    if (index < 0)
        return n;
    if ((WORD)index >= n || !pe_entry(&x, groups, (WORD)index, &name, &to) ||
        !pe_data(&x, to, &off, &len) || !group_choice(fd, off, len, &want))
        return 0;
    icons = pe_sub(&x, 0, ICON);
    if (icons == 0xFFFFFFFFUL)
        return 0;
    n = pe_count(&x, icons);
    for (i = 0; i < n; i++)
        if (pe_entry(&x, icons, i, &name, &to) && name == want &&
            pe_data(&x, to, &im->off, &im->len)) {
            im->mono2x = FALSE;
            return 1;
        }
    return 0;
}

/* ---- any file ---- */

/* The number of icons in an open file (index < 0), or find icon index. */
static int find(int fd, int index, image FAR *im)
{
    BYTE mz[0x40], sig[4];
    DWORD hdr;
    if (!get(fd, 0, mz, sizeof mz))
        return ico_find(fd, index, im);
    if (mz[0] != 'M' || mz[1] != 'Z')
        return ico_find(fd, index, im);
    hdr = rd(mz + 0x3C);
    if (!hdr || !get(fd, hdr, sig, 4))
        return 0;
    if (sig[0] == 'N' && sig[1] == 'E')
        return ne_find(fd, hdr, index, im);
    if (sig[0] == 'P' && sig[1] == 'E' && !sig[2] && !sig[3])
        return pe_find(fd, hdr, index, im);
    return 0;
}

int iconx_count(const char FAR *path)
{
    int fd = _lopen(path, OF_READ), n;
    if (fd == -1)
        return 0;
    n = find(fd, -1, NULL);
    _lclose(fd);
    return n;
}

/* ---- the image ---- */

/* A neutral copy: cx * cy colours, and which pixels are transparent. */
typedef struct {
    int cx, cy;
    COLORREF FAR *px;
    BYTE FAR *clear;
} picture;

/* Windows 2.x: 64x64 (say) monochrome, reduced block by block: a block
   is transparent if all of it is, else black if any of its opaque pixels
   is black (keeping thin lines), else white. UNVERIFIED: how Windows 3.1
   shows these. */
static BOOL load_mono2x(int fd, const image FAR *im, picture FAR *pic)
{
    BYTE h[14];
    WORD w, ht, rowb, x, y, sx, sy;
    LPSTR andr = NULL, xorr = NULL;
    BOOL ok = FALSE;
    if (!get(fd, im->off, h, 14) || rw(h) != 0x0101)
        return FALSE;
    w = rw(h + 6);
    ht = rw(h + 8);
    rowb = rw(h + 10);
    if (!w || !ht || rowb < (w + 7) / 8 || 14 + 2UL * rowb * ht > im->len)
        return FALSE;
    andr = mem_alloc(rowb);
    xorr = mem_alloc(rowb);
    if (!andr || !xorr)
        goto done;
    for (y = 0; y < (WORD)pic->cy; y++) {
        WORD y0 = (WORD)((DWORD)y * ht / pic->cy), y1 = (WORD)((DWORD)(y + 1) * ht / pic->cy);
        if (y1 <= y0)
            y1 = y0 + 1;
        for (x = 0; x < (WORD)pic->cx; x++) {
            pic->clear[y * pic->cx + x] = 1;
            pic->px[y * pic->cx + x] = RGB(255, 255, 255);
        }
        for (sy = y0; sy < y1; sy++) {
            if (!get(fd, im->off + 14 + (DWORD)sy * rowb, andr, rowb) ||
                !get(fd, im->off + 14 + (DWORD)(ht + sy) * rowb, xorr, rowb))
                goto done;
            for (x = 0; x < (WORD)pic->cx; x++) {
                WORD x0 = (WORD)((DWORD)x * w / pic->cx), x1 = (WORD)((DWORD)(x + 1) * w / pic->cx);
                if (x1 <= x0)
                    x1 = x0 + 1;
                for (sx = x0; sx < x1; sx++) {
                    BYTE bit = 0x80 >> (sx & 7);
                    if (andr[sx >> 3] & bit)
                        continue;       /* transparent */
                    pic->clear[y * pic->cx + x] = 0;
                    if (!(xorr[sx >> 3] & bit))
                        pic->px[y * pic->cx + x] = RGB(0, 0, 0);
                }
            }
        }
    }
    ok = TRUE;
done:
    mem_free(andr);
    mem_free(xorr);
    return ok;
}

/* A DIB-format icon image (BITMAPINFOHEADER, colour table, XOR rows, AND
   rows; bottom row first; height doubled), scaled by sampling. */
static BOOL load_dib(int fd, const image FAR *im, picture FAR *pic)
{
    BYTE bi[40];
    DWORD w, ht, bits, ncol, xstride, astride, xorpos, andpos;
    LPSTR pal = NULL, row = NULL, arow = NULL;
    int x, y;
    BOOL ok = FALSE;
    if (!get(fd, im->off, bi, 40) || rd(bi) < 40 || rd(bi + 16) != 0)
        return FALSE;                   /* not an uncompressed DIB (PNG?) */
    w = rd(bi + 4);
    ht = rd(bi + 8) / 2;
    bits = rw(bi + 14);
    if (!w || !ht || w > 0x4000 || ht > 0x4000 ||
        (bits != 1 && bits != 4 && bits != 8 && bits != 16 && bits != 24 && bits != 32))
        return FALSE;
    ncol = rd(bi + 32);
    if (!ncol && bits <= 8)
        ncol = 1UL << bits;
    if (ncol > 256)
        return FALSE;
    xstride = ((w * bits + 31) / 32) * 4;
    astride = ((w + 31) / 32) * 4;
    xorpos = im->off + rd(bi) + ncol * 4;
    andpos = xorpos + xstride * ht;
    if (andpos + astride * ht - im->off > im->len)
        return FALSE;
    if ((ncol && !(pal = mem_alloc(ncol * 4))) || !(row = mem_alloc(xstride)) ||
        !(arow = mem_alloc(astride)))
        goto done;
    if (ncol && !get(fd, im->off + rd(bi), pal, (WORD)(ncol * 4)))
        goto done;
    for (y = 0; y < pic->cy; y++) {
        DWORD sy = ht - 1 - (DWORD)y * ht / pic->cy;    /* rows bottom-up */
        if (!get(fd, xorpos + sy * xstride, row, (WORD)xstride) ||
            !get(fd, andpos + sy * astride, arow, (WORD)astride))
            goto done;
        for (x = 0; x < pic->cx; x++) {
            WORD sx = (WORD)((DWORD)x * w / pic->cx);
            BYTE FAR *r = (BYTE FAR *)row;
            BYTE FAR *c;
            DWORD i;
            COLORREF col;
            switch ((WORD)bits) {
            case 1: i = (r[sx >> 3] >> (7 - (sx & 7))) & 1; break;
            case 4: i = (r[sx >> 1] >> ((sx & 1) ? 0 : 4)) & 15; break;
            case 8: i = r[sx]; break;
            default: i = 0; break;
            }
            if (bits <= 8) {
                c = (BYTE FAR *)pal + (i < ncol ? i : 0) * 4;
                col = RGB(c[2], c[1], c[0]);
            } else if (bits == 16) {
                WORD v = rw(r + sx * 2);
                col = RGB(((v >> 10) & 31) << 3, ((v >> 5) & 31) << 3, (v & 31) << 3);
            } else {
                c = r + (DWORD)sx * (bits / 8);
                col = RGB(c[2], c[1], c[0]);
            }
            pic->px[y * pic->cx + x] = col;
            pic->clear[y * pic->cx + x] =
                ((BYTE FAR *)arow)[sx >> 3] & (0x80 >> (sx & 7)) ? 1 : 0;
        }
    }
    ok = TRUE;
done:
    mem_free(pal);
    mem_free(row);
    mem_free(arow);
    return ok;
}

/* The picture in the display's format, as a header + AND + XOR block. */
static LPSTR to_device(const picture FAR *pic, WORD FAR *cband, WORD FAR *cbxor)
{
    HDC dc = GetDC(NULL), mdc = CreateCompatibleDC(dc);
    HBITMAP bm = CreateCompatibleBitmap(dc, pic->cx, pic->cy), old;
    BITMAP b;
    WORD arow = ((pic->cx + 15) / 16) * 2;
    DWORD xsize;
    BYTE FAR *out = NULL;
    int x, y;

    ReleaseDC(NULL, dc);
    if (!mdc || !bm)
        goto done;
    old = SelectObject(mdc, bm);
    for (y = 0; y < pic->cy; y++)
        for (x = 0; x < pic->cx; x++)
            SetPixel(mdc, x, y, pic->clear[y * pic->cx + x] ? RGB(0, 0, 0)
                                                           : pic->px[y * pic->cx + x]);
    SelectObject(mdc, old);
    GetObject(bm, sizeof b, (LPSTR)&b);
    xsize = (DWORD)b.bmWidthBytes * b.bmHeight * b.bmPlanes;
    if (12 + (DWORD)arow * pic->cy + xsize > 0xFFF0UL)
        goto done;                      /* a .GRP must hold it in 64K */
    out = (BYTE FAR *)mem_alloc(12 + (DWORD)arow * pic->cy + xsize);
    if (!out)
        goto done;
    out[0] = (BYTE)(pic->cx / 2);       /* hotspot: the centre (observed */
    out[1] = (BYTE)(pic->cx / 2 >> 8);  /* in Windows 3.1's icons, 16,16) */
    out[2] = (BYTE)(pic->cy / 2);
    out[3] = (BYTE)(pic->cy / 2 >> 8);
    out[4] = (BYTE)pic->cx;
    out[5] = (BYTE)(pic->cx >> 8);
    out[6] = (BYTE)pic->cy;
    out[7] = (BYTE)(pic->cy >> 8);
    out[8] = (BYTE)b.bmWidthBytes;
    out[9] = (BYTE)(b.bmWidthBytes >> 8);
    out[10] = b.bmPlanes;
    out[11] = b.bmBitsPixel;
    for (y = 0; y < pic->cy; y++)
        for (x = 0; x < pic->cx; x++)
            if (pic->clear[y * pic->cx + x])
                out[12 + y * arow + (x >> 3)] |= 0x80 >> (x & 7);
    GetBitmapBits(bm, xsize, (LPSTR)out + 12 + arow * pic->cy);
    *cband = arow * pic->cy;
    *cbxor = (WORD)xsize;
done:
    if (bm)
        DeleteObject(bm);
    if (mdc)
        DeleteDC(mdc);
    return (LPSTR)out;
}

/* Icon `index` of file `path` (a full path) in the display's format: a
   new header + AND + XOR block, or NULL. */
LPSTR iconx_bits(const char FAR *path, int index, WORD FAR *cband, WORD FAR *cbxor)
{
    int fd = _lopen(path, OF_READ);
    image im;
    picture pic;
    LPSTR out = NULL;
    BOOL ok;
    if (fd == -1)
        return NULL;
    if (index >= 0 && find(fd, index, &im)) {
        pic.cx = GetSystemMetrics(SM_CXICON);
        pic.cy = GetSystemMetrics(SM_CYICON);
        pic.px = (COLORREF FAR *)mem_alloc((DWORD)pic.cx * pic.cy * sizeof(COLORREF));
        pic.clear = (BYTE FAR *)mem_alloc((DWORD)pic.cx * pic.cy);
        if (pic.px && pic.clear) {
            ok = im.mono2x ? load_mono2x(fd, &im, &pic) : load_dib(fd, &im, &pic);
            if (ok)
                out = to_device(&pic, cband, cbxor);
        }
        mem_free((LPSTR)pic.px);
        mem_free((LPSTR)pic.clear);
    }
    _lclose(fd);
    return out;
}

/* An HICON for a header + AND + XOR block (not freed). */
HICON iconx_icon(const char FAR *bits)
{
    const BYTE FAR *h = (const BYTE FAR *)bits;
    WORD arow = ((rw(h + 4) + 15) / 16) * 2;
    return CreateIcon(g_hinst, rw(h + 4), rw(h + 6), h[10], h[11],
                      h + 12, h + 12 + arow * rw(h + 6));
}

/* ---- what the rest of Program Manager calls ---- */

/* How many icons `path` (a full path) offers: SHELL's count, else ours
   (a PE file on Windows 3.1 without Win32s; every file on 2.x). */
int icon_count(const char FAR *path)
{
#ifndef W16_WIN2
    int n = (int)(UINT)ExtractIcon(g_hinst, path, (UINT)-1);
    if (n > 0)
        return n;
#endif
    return iconx_count(path);
}

/* Icon `index` of `path` (a full path), or NULL; the caller destroys it. */
HICON icon_extract(const char FAR *path, int index)
{
    WORD cband, cbxor;
    LPSTR bits;
    HICON h;
#ifndef W16_WIN2
    h = ExtractIcon(g_hinst, path, index);
    if ((UINT)h > 1)
        return h;
#endif
    bits = iconx_bits(path, index, &cband, &cbxor);
    if (!bits)
        return NULL;
    h = iconx_icon(bits);
    mem_free(bits);
    return h;
}
