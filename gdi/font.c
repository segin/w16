/*
 * font.c - raster fonts: the font table, AddFontResource, the font mapper
 * and realizing a DC's font.
 *
 * Fonts are RT_FONT resources (.FNT, DDK "Font File Format") of .FON
 * modules, listed by the module's FONTDIR resource. GDI's initialisation
 * loads FONTS.FON (the module KERNEL loaded from SYSTEM.INI [boot]
 * fonts.fon), the fixedfon.fon file and OEMFONTS.FON (3.1's BOOTLOG.TXT
 * shows LoadStart/LoadSuccess for these three inside GDI's load,
 * ARCHITECTURE.md 9.4); USER adds WIN.INI's [fonts] with AddFontResource,
 * which returned the number of fonts in the file (6 for SSERIFE.FON, 1 for
 * a TrueType .FOT; GD2).
 *
 * A realized font is the font resource itself, locked, with dfBitsPointer
 * set to its bits (GD1: the driver got selector:0042h, the FONTINFO part
 * of the .FNT image, with dfBitsPointer = selector:dfBitsOffset). The
 * display driver's RealizeObject is asked first and returns 0 (no device
 * fonts) on VGA.
 *
 * The mapper (font_map) is 3.1's as probe GD11 found it: every raster
 * font is a candidate at the integer scale its size gives, and a font
 * that has to be scaled is realized as a scaled copy of its own
 * (font_scale), which is what the driver gets (ARCHITECTURE.md 11.10).
 * TrueType and vector fonts are not mapped to yet. w16 ships no fonts:
 * whatever .FON files the installation has are used.
 */
#include "gdi.h"

/* the font table: FONTENTs in GDI's heaps (heap.c), linked by XP */
XP fonts;

#define RT_FONTDIR 7
#define RT_FONT 8

/* the module's FONTDIR resource. Its name varies (VGASYS.FON: the
   integer 40h), so look it up in the resource table of the module
   database, KERNEL's in-memory copy of the NE header (Pietrek ch. 3; the
   NE format) */
static HRSRC find_fontdir(HINSTANCE h)
{
    HMODULE hm = GetModuleHandle((LPCSTR)MAKELP(0, h));
    BYTE __far *ne = hm ? GlobalLock(hm) : 0;
    BYTE __far *rt, *p;
    HRSRC r = 0;
    if (ne && ne[0] == 'N' && ne[1] == 'E' && *(WORD __far *)(ne + 0x24) != *(WORD __far *)(ne + 0x26)) {
        rt = ne + *(WORD __far *)(ne + 0x24);
        p = rt + 2;
        while (*(WORD __far *)p) {
            WORD type = *(WORD __far *)p, cnt = *(WORD __far *)(p + 2);
            if (type == (0x8000 | RT_FONTDIR) && cnt) {
                WORD id = *(WORD __far *)(p + 8 + 6);
                if (id & 0x8000)
                    r = FindResource(h, MAKEINTRESOURCE(id & 0x7FFF), MAKEINTRESOURCE(RT_FONTDIR));
                else {
                    char name[64];
                    BYTE __far *q = rt + id;
                    int k, n = q[0] < 63 ? q[0] : 63;
                    for (k = 0; k < n; k++)
                        name[k] = q[1 + k];
                    name[n] = 0;
                    r = FindResource(h, name, MAKEINTRESOURCE(RT_FONTDIR));
                }
                break;
            }
            p += 8 + 12 * cnt;
        }
    }
    if (ne)
        GlobalUnlock(hm);
    if (!r)
        r = FindResource(h, "FONTDIR", MAKEINTRESOURCE(RT_FONTDIR));
    return r;
}

/* a path's file name (after its last '\\', '/' or ':') */
static LPCSTR base_name(LPCSTR file)
{
    LPCSTR b = file;
    for (; *file; file++)
        if (*file == '\\' || *file == '/' || *file == ':')
            b = file + 1;
    return b;
}

/* read the FONTDIR of a module: one entry per font */
static int font_add_module(HINSTANCE h, LPCSTR file)
{
    HRSRC r = find_fontdir(h);
    HGLOBAL g;
    BYTE __far *p;
    WORD n, i;
    int added = 0;
    if (!r)
        return 0;
    g = LoadResource(h, r);
    p = g ? LockResource(g) : 0;
    if (!p) {
        if (g)
            FreeResource(g);
        return 0;
    }
    n = *(WORD __far *)p;
    p += 2;
    for (i = 0; i < n; i++) {
        XP xf = xalloc(sizeof(FONTENT));
        LPFONT f = xptr(xf);
        BYTE __far *e = p + 2;
        const char __far *dev, *face;
        int k;
        if (!xf)
            break;
        f->hMod = h;
        f->adds = 1;
        f->resId = *(WORD __far *)p;
        f->points = *(short __far *)(e + 68);
        f->height = *(short __far *)(e + 88);
        f->avgWidth = *(short __far *)(e + 91);
        f->weight = *(short __far *)(e + 83);
        f->italic = e[80];
        f->underline = e[81];
        f->strikeout = e[82];
        f->charset = e[85];
        f->pitch = e[90];
        f->type = *(short __far *)(e + 66);
        f->il = *(short __far *)(e + 76);
        dev = (const char __far *)e + 0x71;
        face = dev;
        while (*face)
            face++;
        face++;
        for (k = 0; k < LF_FACESIZE - 1 && face[k]; k++)
            f->face[k] = face[k];
        for (k = 0; file && k < 12 && file[k]; k++)
            f->file[k] = file[k];           /* (file: base_name's) */
        f->sx = f->sy = 1;
        /* at the table's end: the mapper takes the first of equal
           candidates, in the order the fonts were added (GD11's test
           fonts on 3.1, ARCHITECTURE.md 11.10) */
        {
            XP __far *pp = &fonts;
            while (*pp)
                pp = &((LPFONT)xptr(*pp))->next;
            f = xptr(xf);
            *pp = xf;
        }
        added++;
        f = xptr(xf);
        gdi_log("font: ");
        gdi_log(f->face);
        gdi_loghex(" height", f->height);
        p = (BYTE __far *)face;
        while (*p)
            p++;
        p++;
    }
    GlobalUnlock(g);
    FreeResource(g);
    return added;
}

static void font_load_boot(LPCSTR file)
{
    HINSTANCE h = LoadLibrary(file);
    if (h >= 32)
        font_add_module(h, base_name(file));
}

/* the face of the fixed system font (SYSTEM.INI [boot] fixedfon.fon):
   "System" asked with FIXED_PITCH is mapped as that face (GD11 on 3.1:
   Fixedsys, also where Courier's size is nearer) */
static char fixed_face[LF_FACESIZE];

void font_init(void)
{
    char buf[80];
    XP x;
    font_load_boot("FONTS.FON");
    GetPrivateProfileString("boot", "fixedfon.fon", "", buf, sizeof buf, "SYSTEM.INI");
    if (buf[0]) {
        for (x = fonts; x && ((LPFONT)xptr(x))->next; x = ((LPFONT)xptr(x))->next)
            ;
        font_load_boot(buf);
        x = x ? ((LPFONT)xptr(x))->next : fonts;
        if (x) {
            LPFONT f = xptr(x);
            int i;
            for (i = 0; i < LF_FACESIZE - 1 && f->face[i]; i++)
                fixed_face[i] = f->face[i];
        }
    }
    font_load_boot("OEMFONTS.FON");
}

/* the entries of a file (by name) or a module (file's selector 0) in the
   table; with add, each counts one more AddFontResource */
static int font_count(LPCSTR file, int add)
{
    LPCSTR name = SELECTOROF(file) ? base_name(file) : 0;
    XP x;
    int n = 0;
    for (x = fonts; x; x = ((LPFONT)xptr(x))->next) {
        LPFONT f = xptr(x);
        if (name ? !str_icmp(f->file, name) : f->hMod == (HINSTANCE)OFFSETOF(file)) {
            n++;
            if (add && f->adds < 0xFF)
                f->adds++;
        }
    }
    return n;
}

int GDIAPI AddFontResource(LPCSTR file)                            /* GDI.119 */
{
    HINSTANCE h;
    int n;
    if (!file)
        return 0;
    /* GD5 on 3.1: a file added again is not loaded again; it stays until
       removed as many times */
    n = font_count(file, 1);
    if (n)
        return n;
    if (!SELECTOROF(file)) {            /* a module handle */
        return font_add_module((HINSTANCE)OFFSETOF(file), 0);
    }
    /* GD11 on 3.1: a file that is not there gives 0, without KERNEL's
       "Cannot find" box */
    {
        WORD em = SetErrorMode(0x8000);     /* SEM_NOOPENFILEERRORBOX */
        h = LoadLibrary(file);
        SetErrorMode(em);
    }
    if (h < 32)
        return 0;
    n = font_add_module(h, base_name(file));
    if (!n)
        FreeLibrary(h);                 /* no fonts in it */
    return n;
}

/* fonts RemoveFontResource took out of the table while a DC still had
   them realized: they go when no DC uses them (SDK: the resource stays
   loaded while it is in use) */
static XP fonts_gone;

/* the integer-scaled copies DCs have realized (ARCHITECTURE.md 11.10):
   entries of their own, each with its scaled image, kept while a DC
   uses them */
static XP scaled;

static int dc_has(XP x)
{
    HDC l;
    LPDC dc;
    for (l = dc_first(); l && (dc = dc_get(l)) != 0; l = dc->hLink)
        if (dc->xFont == x)
            return 1;
    return 0;
}

/* a real font is in use while a DC has it or a scaled copy of it */
static int font_in_use(XP x)
{
    XP s;
    if (dc_has(x))
        return 1;
    for (s = scaled; s; s = ((LPFONT)xptr(s))->next)
        if (((LPFONT)xptr(s))->base == x)
            return 1;
    return 0;
}

/* scaled copies no DC uses any more go */
static void scaled_reap(void)
{
    XP __far *pp = &scaled;
    while (*pp) {
        XP xs = *pp;
        LPFONT s = xptr(xs);
        if (!dc_has(xs)) {
            *pp = s->next;
            if (s->hMem) {
                GlobalUnlock(s->hMem);
                GlobalFree(s->hMem);
            }
            xfree(xs);
            pp = &scaled;               /* (xfree may have moved the heap) */
            continue;
        }
        pp = &s->next;
    }
}

/* the font's resources, its entry, and its module when it was the
   removed module's last font */
static void font_free(XP xf)
{
    LPFONT f = xptr(xf), g;
    HINSTANCE h = f->hMod;
    int unload = f->unload;
    XP x;
    if (f->hRes) {
        GlobalUnlock(f->hRes);
        FreeResource(f->hRes);
    }
    if (f->hMem) {
        GlobalUnlock(f->hMem);
        GlobalFree(f->hMem);
    }
    xfree(xf);
    if (!unload)
        return;
    for (x = fonts_gone; x; x = g->next) {
        g = xptr(x);
        if (g->hMod == h)
            return;                     /* the module's last font frees it */
    }
    FreeLibrary(h);
}

void font_reap(void)
{
    XP __far *pp = &fonts_gone;
    scaled_reap();
    while (*pp) {
        XP xf = *pp;
        LPFONT f = xptr(xf);
        if (!font_in_use(xf)) {
            *pp = f->next;
            font_free(xf);
            pp = &fonts_gone;           /* (font_free may have changed the list's heap) */
            continue;
        }
        pp = &f->next;
    }
}

BOOL GDIAPI RemoveFontResource(LPCSTR file)                        /* GDI.136 */
{
    XP __far *pp = &fonts;
    XP gone = 0;
    LPCSTR name = file && SELECTOROF(file) ? base_name(file) : 0;
    int found = 0;
    /* the file's entries out of the table, onto a list of their own,
       when this undoes the last AddFontResource */
    while (*pp) {
        XP xf = *pp;
        LPFONT f = xptr(xf);
        if (name ? !str_icmp(f->file, name) : f->hMod == (HINSTANCE)OFFSETOF(file)) {
            if (f->adds > 1) {
                f->adds--;
                found = 1;
                pp = &f->next;
                continue;
            }
            *pp = f->next;
            f->next = gone;
            f->unload = name != 0;
            gone = xf;
            found = 1;
            continue;
        }
        pp = &f->next;
    }
    /* each goes now, or with the last DC that has it realized */
    while (gone) {
        XP xf = gone;
        LPFONT f = xptr(xf);
        gone = f->next;
        f->next = fonts_gone;
        fonts_gone = xf;
    }
    font_reap();
    return found;
}

/* ---- mapping ---- */

static void substitute(LPCSTR face, char *out)
{
    char name[LF_FACESIZE];
    int i;
    for (i = 0; i < LF_FACESIZE - 1 && face[i]; i++)
        name[i] = face[i];
    name[i] = 0;
    out[0] = 0;
    if (name[0])
        GetProfileString("FontSubstitutes", name, name, out, LF_FACESIZE);
}

/* The mapper's rules for raster fonts, as GD11 found them on 3.1
   (ARCHITECTURE.md 11.10; black-box, 3.1's own fonts and test fonts of
   our own): every font of the table is one candidate, at the integer
   scale its own size gives; the lowest penalty wins, the first in the
   table on a tie.

   The height factor y: the requested height over the font's (cell
   heights for a positive lfHeight, character heights for a negative
   one), rounded up from a quarter below the next whole number
   (want / ref + 1/4, down), 1 to 8. A font smaller than asked whose
   factor would be more than its own height less 3 is no candidate. The
   width factor x: lfWidth over dfAvgWidth to the nearest whole number,
   1 to 5, a half up except from 1 to 2; with lfWidth 0, y (5 at most).

   Penalties: the scaled height smaller than asked 150 a pixel (and 1
   more for an unscaled font), bigger 600 and 150 a pixel; with lfWidth,
   50 a pixel of the scaled average width's difference; scaling across
   60 + 15 x, down 11 + 20 y (y 2 to 5), 600, 1100, 1600 (y 6, 7, 8); x
   and y unequal 410 * the larger / the smaller - 10. Character set 65000
   (not for DEFAULT_CHARSET), face 10000, family 9000, a fixed pitch
   asked of a variable font 15000, a variable one of a fixed font 350.
   The scaling terms are a fit (every request on 3.1's fonts, all but 1
   of 12525 on test fonts: ARCHITECTURE.md 11.10). UNVERIFIED (not probed
   yet): the weight (3 a hundred) and italic (4) penalties. */
static int xfor(int w, int aw)
{
    int x, bx = 1;
    long bp = -1;
    for (x = 1; x <= 5; x++) {
        long d = (long)aw * x - w, p;
        if (d < 0)
            d = -d;
        p = 2 * d + (x > 1);
        if (bp < 0 || p <= bp) {
            bp = p;
            bx = x;
        }
    }
    return bx;
}

static long penalty(LPFONT f, const LOGFONT __far *lf, const char *face, int h, int *px, int *py)
{
    long p = 0, d;
    int ref, want, k, x, w = lf->lfWidth < 0 ? -lf->lfWidth : lf->lfWidth;
    *px = *py = 1;
    if (f->type & 4 || !f->height)       /* TrueType (.FOT): not rendered yet */
        return 0x7FFFFFFFL;
    if (f->type & 1)                     /* vector fonts: not drawn yet */
        p += 20000;
    if (lf->lfCharSet != f->charset && !(lf->lfCharSet == 1))
        p += 65000L;
    if (face[0] && str_icmp(face, f->face))
        p += 10000;
    if ((lf->lfPitchAndFamily & 0xF0) && (lf->lfPitchAndFamily & 0xF0) != (f->pitch & 0xF0))
        p += 9000;
    if ((lf->lfPitchAndFamily & 3) == 1 && (f->pitch & 1))
        p += 15000;                      /* fixed asked, variable font */
    if ((lf->lfPitchAndFamily & 3) == 2 && !(f->pitch & 1))
        p += 350;                        /* variable asked, fixed font */
    if (h > 0) {
        want = h;
        ref = f->height;
    } else {
        want = -h;
        ref = f->height - f->il;
    }
    if (ref < 1)
        ref = 1;
    k = 1;
    if (!(f->type & 1)) {
        k = (int)((4L * want + ref) / (4L * ref));
        if (k < 1)
            k = 1;
        if (k > 8)
            k = 8;
        if (want > ref && k > ref - 3)
            return 0x7FFFFFFFL;          /* no candidate */
    }
    d = (long)ref * k - want;
    if (d < 0)
        p += 150L * -d + (k == 1);
    else if (d > 0)
        p += 600 + 150L * d;
    if (w)
        x = (f->type & 1) ? 1 : xfor(w, f->avgWidth);
    else
        x = k < 5 ? k : 5;
    if (w) {
        d = (long)f->avgWidth * x - w;
        p += 50L * (d < 0 ? -d : d);
    }
    if (x > 1)
        p += 60 + 15 * x;
    if (k > 1)
        p += k <= 5 ? 11 + 20 * k : k == 6 ? 600 : k == 7 ? 1100 : 1600;
    if (x != k)
        p += (x > k ? 410L * x / k : 410L * k / x) - 10;
    d = (lf->lfWeight ? lf->lfWeight : 400) - f->weight;
    p += 3L * (d < 0 ? -d : d) / 100;
    if (!!lf->lfItalic != !!f->italic)
        p += 4;
    *px = x;
    *py = k;
    return p;
}

/* the best font for lf (device units), with its factors */
static XP font_map(const LOGFONT __far *lf, int h0, int *psx, int *psy)
{
    char face[LF_FACESIZE];
    XP x, best = 0;
    long bp = 0x7FFFFFFFL;
    int h = lf->lfHeight ? lf->lfHeight : h0, sx, sy;
    substitute(lf->lfFaceName, face);
    if ((lf->lfPitchAndFamily & 3) == 1 && fixed_face[0] && !str_icmp(face, "System")) {
        int i;
        for (i = 0; i < LF_FACESIZE; i++)
            face[i] = fixed_face[i];
    }
    *psx = *psy = 1;
    for (x = fonts; x; x = ((LPFONT)xptr(x))->next) {
        long p = penalty(xptr(x), lf, face, h, &sx, &sy);
        if (p < bp) {
            bp = p;
            best = x;
            *psx = sx;
            *psy = sy;
        }
    }
    return best;
}

/* The font as the driver gets it: a .FNT image at offset 0 of its own
   segment, FONTINFO at 42h, dfBitsPointer = selector:dfBitsOffset.
   VGA.DRV sets RC_BIGFONT, so GDI hands it Windows 3.x fonts only (DDK,
   GDIINFO): a 2.x raster font is converted. Observed (GD1, the System
   font): dfFlags 2 (DFF_PROPORTIONAL), the 3.x header fields zero, the
   character table of 3.x entries (width, 32-bit offset) at 94h, the bits
   right after it (dfBitsOffset 5DAh for 225 + 1 characters) and the face
   name moved by as much (dfFace 171Ah). */
static LPFONTINFO font_scale(LPFONT s, LPFONTINFO bf);

static LPFONTINFO font_info(LPFONT f)
{
    HRSRC r;
    BYTE __far *p, __far *base;
    LPFONTINFO fi;
    if (f->fi)
        return f->fi;
    if (f->base) {                      /* a scaled copy */
        LPFONTINFO bf = font_info(xptr(f->base));
        f->fi = bf ? font_scale(f, bf) : 0;
        return f->fi;
    }
    r = FindResource(f->hMod, MAKEINTRESOURCE(f->resId), MAKEINTRESOURCE(RT_FONT));
    if (!r)
        return 0;
    f->hRes = LoadResource(f->hMod, r);
    p = f->hRes ? LockResource(f->hRes) : 0;
    if (!p)
        return 0;
    base = p;
    fi = (LPFONTINFO)(p + 0x42);
    if (*(WORD __far *)p < 0x300 && !(fi->dfType & 1)) {
        WORD n = fi->dfLastChar - fi->dfFirstChar + 2, i;
        DWORD oldBits = fi->dfBitsOffset, size = SizeofResource(f->hMod, r);
        DWORD newBits = 0x94 + (DWORD)n * 6, newSize;
        long delta = (long)newBits - (long)oldBits;
        WORD __far *ot = (WORD __far *)(p + 0x76);
        BYTE __far *q;
        LPFONTINFO nf;
        if (*(DWORD __far *)(p + 2) && *(DWORD __far *)(p + 2) < size)
            size = *(DWORD __far *)(p + 2);
        newSize = size + delta;
        /* a malformed file (characters, bits or the new size out of
           range) is not converted: no font */
        if (fi->dfLastChar < fi->dfFirstChar || oldBits > size || oldBits < 0x76 + 4L * n ||
            newSize > 0xFFF0L)
            q = 0;
        else {
            f->hMem = GlobalAlloc(GMEM_MOVEABLE | GMEM_ZEROINIT | GMEM_SHARE, newSize);
            q = f->hMem ? GlobalLock(f->hMem) : 0;
        }
        if (!q) {
            if (f->hMem)
                GlobalFree(f->hMem);
            f->hMem = 0;
            GlobalUnlock(f->hRes);
            FreeResource(f->hRes);
            f->hRes = 0;
            return 0;
        }
        mem_copy(q, p, 0x76);
        *(WORD __far *)q = 0x300;
        *(DWORD __far *)(q + 2) = newSize;
        nf = (LPFONTINFO)(q + 0x42);
        *(DWORD __far *)(q + 0x76) = (fi->dfPitchAndFamily & 1) ? 2 : 1;
        for (i = 0; i < n; i++) {
            *(WORD __far *)(q + 0x94 + i * 6) = ot[i * 2];
            *(DWORD __far *)(q + 0x94 + i * 6 + 2) = ot[i * 2 + 1] + delta;
        }
        mem_copy(q + (WORD)newBits, p + (WORD)oldBits, (WORD)(size - oldBits));
        nf->dfBitsOffset = newBits;
        if (nf->dfFace >= oldBits)
            nf->dfFace += delta;
        if (nf->dfDevice >= oldBits)
            nf->dfDevice += delta;
        GlobalUnlock(f->hRes);
        FreeResource(f->hRes);
        f->hRes = 0;
        base = q;
        fi = nf;
    }
    fi->dfBitsPointer = MAKELP(SELECTOROF(base), OFFSETOF(base) + (WORD)fi->dfBitsOffset);
    f->fi = fi;
    return fi;
}

extern WORD __cdecl gdi_ahincr;         /* entry.asm: KERNEL's __AHINCR */

/* a byte of a block over 64K (selectors __AHINCR apart) */
static BYTE __far *huge_at(BYTE __far *base, DWORD off)
{
    off += OFFSETOF(base);
    return (BYTE __far *)MAKELP(SELECTOROF(base) + (WORD)(off >> 16) * gdi_ahincr, (WORD)off);
}

/* The scaled copy of a font as 3.1's GDI hands it to the driver (GD12,
   GD1's log, ARCHITECTURE.md 11.10): a 3.x font of its own; dfPoints,
   dfVertRes, dfAscent, the leadings and dfPixHeight times y, dfHorizRes,
   dfPixWidth, the widths and dfWidthBytes times x, dfDevice 0; the
   character table at 94h, the face name right after it, the bits after
   the name (at an even offset); each character's bitmap as many columns
   as the unscaled one's times x (8 pixels each, top first), every pixel
   an x by y block. Unscaled fields (dfFlags and on) are the font's. */
static LPFONTINFO font_scale(LPFONT s, LPFONTINFO bf)
{
    BYTE __far *src = (BYTE __far *)bf - 0x42, __far *q;
    WORD n = bf->dfLastChar - bf->dfFirstChar + 2, i, sx = s->sx, sy = s->sy;
    WORD bh = bf->dfPixHeight, h = bh * sy, flen, r, c, dy, j;
    const char __far *face = (const char __far *)src + (WORD)bf->dfFace;
    DWORD off, size, bits;
    LPFONTINFO fi;
    BYTE row[64];
    for (flen = 0; face[flen]; flen++)
        ;
    bits = (0x94 + (DWORD)n * 6 + flen + 2) & ~1UL;
    size = bits;
    for (i = 0; i < n; i++) {
        WORD w = *(WORD __far *)(src + 0x94 + i * 6);
        size += (DWORD)((w + 7) >> 3) * sx * h;
    }
    s->hMem = GlobalAlloc(GMEM_MOVEABLE | GMEM_ZEROINIT | GMEM_SHARE, size);
    q = s->hMem ? GlobalLock(s->hMem) : 0;
    if (!q) {
        if (s->hMem)
            GlobalFree(s->hMem);
        s->hMem = 0;
        return 0;
    }
    mem_copy(q, src, 0x94);
    *(WORD __far *)q = 0x300;
    *(DWORD __far *)(q + 2) = size;
    fi = (LPFONTINFO)(q + 0x42);
    fi->dfPoints *= sy;
    fi->dfVertRes *= sy;
    fi->dfHorizRes *= sx;
    fi->dfAscent *= sy;
    fi->dfInternalLeading *= sy;
    fi->dfExternalLeading *= sy;
    fi->dfPixWidth *= sx;
    fi->dfPixHeight = h;
    fi->dfAvgWidth *= sx;
    fi->dfMaxWidth *= sx;
    fi->dfWidthBytes *= sx;
    fi->dfDevice = 0;
    fi->dfFace = 0x94 + (DWORD)n * 6;
    mem_copy(q + (WORD)fi->dfFace, face, flen + 1);
    fi->dfBitsOffset = bits;
    off = bits;
    for (i = 0; i < n; i++) {
        BYTE __far *e = src + 0x94 + i * 6;
        WORD w = *(WORD __far *)e, cols = (w + 7) >> 3, ncols = cols * sx;
        DWORD goff = *(DWORD __far *)(e + 2);
        *(WORD __far *)(q + 0x94 + i * 6) = w * sx;
        *(DWORD __far *)(q + 0x94 + i * 6 + 2) = off;
        if (ncols > sizeof row)
            ncols = sizeof row;         /* (a glyph over 512 pixels wide) */
        for (r = 0; r < bh; r++) {
            /* the source row's pixels, each x times */
            for (c = 0; c < ncols; c++)
                row[c] = 0;
            for (j = 0; j < w; j++) {
                BYTE b = *huge_at(src, goff + (DWORD)(j >> 3) * bh + r);
                if (b & (0x80 >> (j & 7))) {
                    WORD p = j * sx, e2 = p + sx;
                    for (; p < e2 && (p >> 3) < ncols; p++)
                        row[p >> 3] |= (BYTE)(0x80 >> (p & 7));
                }
            }
            for (c = 0; c < ncols; c++) {
                if (!row[c])
                    continue;
                for (dy = 0; dy < sy; dy++)
                    *huge_at(q, off + (DWORD)c * h + r * sy + dy) = row[c];
            }
        }
        off += (DWORD)cols * sx * h;
    }
    fi->dfBitsPointer = MAKELP(SELECTOROF(q), OFFSETOF(q) + (WORD)bits);
    return fi;
}

/* the entry of base font xb scaled sx by sy: one a DC has already, or a
   new one */
static XP font_scaled(XP xb, int sx, int sy)
{
    XP xs;
    LPFONT s, b;
    for (xs = scaled; xs; xs = s->next) {
        s = xptr(xs);
        if (s->base == xb && s->sx == sx && s->sy == sy)
            return xs;
    }
    xs = xalloc(sizeof(FONTENT));
    if (!xs)
        return 0;
    s = xptr(xs);
    b = xptr(xb);
    mem_copy(s, b, sizeof(FONTENT));
    s->hRes = 0;
    s->hMem = 0;
    s->fi = 0;
    s->base = xb;
    s->sx = (BYTE)sx;
    s->sy = (BYTE)sy;
    s->next = scaled;
    scaled = xs;
    return xs;
}

LPFONT dc_font(LPDC dc)
{
    return dc->xFont ? (LPFONT)xptr(dc->xFont) : (LPFONT)0;
}

LPFONT font_realize(LPDC dc)
{
    LPFONTOBJ fo = obj_get(dc->hFont, FONT_MAGIC);
    XP x;
    LPFONT f;
    LOGFONT lf;
    int i, dh, dw, sx, sy;
    if (!fo)
        return 0;
    /* the height and width in device units (GD5 on 3.1: in MM_LOMETRIC
       and anisotropic modes the font is chosen by its size on the
       device; a negative height stays a character height) */
    dh = fo->lf.lfHeight;
    dw = fo->lf.lfWidth;
    /* a stock font keeps its size in device units (GD8 on 3.1: the
       default System font at 2:1 is System 16, not 16 x 2; GD5: MM_LOMETRIC
       too). UNVERIFIED for the stock fonts other than SYSTEM_FONT */
    for (i = 10; i < 20 && stock[i] != dc->hFont; i++)
        ;
    /* ... and its LOGFONT's width is not asked for (FN1 on 3.1:
       ANSI_VAR_FONT, Helv 12 x 9, is MS Sans Serif's 13-pixel font,
       average width 5, not one scaled to 9) */
    if (i < 20)
        dw = 0;
    if (dc->mapMode != 1 && i == 20) {
        int v = dc_scale_y(dc, dh);
        v = v < 0 ? -v : v;
        dh = dh < 0 ? -v : v;
        v = dc_scale_x(dc, dw);
        dw = v < 0 ? -v : v;
    }
    if (dc->xFont && dc->hFontOf == dc->hFont && dc->lpPDevOfFont == dc->lpPDevice &&
        dc->fontHt == dh && dc->fontWd == dw)
        return xptr(dc->xFont);
    /* the object's block ends after the face name */
    mem_set(&lf, 0, sizeof lf);
    mem_copy(&lf, &fo->lf, LF_FIXEDPART);
    lf.lfHeight = dh;
    lf.lfWidth = dw;
    for (i = 0; i < LF_FACESIZE - 1 && fo->lf.lfFaceName[i]; i++)
        lf.lfFaceName[i] = fo->lf.lfFaceName[i];
    /* the driver first: device fonts (none on VGA: RealizeObject gives 0) */
    ((DRVREALIZE)dc->dev->fn[DRV_REALIZEOBJECT])(dc->lpPDevice, OBJ_FONT, &lf, 0, 0);
    /* lfHeight 0: a 12-point character height (GD11 on 3.1: 16 pixels
       on VGA, MS Sans Serif's 20-pixel font) */
    x = font_map(&lf, -mul_div(12, dc->dev->info.dpLogPixelsY, 72), &sx, &sy);
    if (!x)
        return 0;
    if (sx > 1 || sy > 1) {
        x = font_scaled(x, sx, sy);
        if (!x)
            return 0;
    }
    f = xptr(x);
    gdi_log("font_realize: ");
    gdi_log(lf.lfFaceName);
    gdi_loghex(" -> height", f->height);
    gdi_loghex(" x", sx);
    gdi_loghex(" y", sy);
    gdi_log(f->face);
    gdi_log("\r\n");
    if (!font_info(f)) {
        if (f->base)
            scaled_reap();
        return 0;
    }
    dc->xFont = x;
    dc->hFontOf = dc->hFont;
    dc->lpPDevOfFont = dc->lpPDevice;
    dc->fontHt = dh;
    dc->fontWd = dw;
    /* perhaps the last DC of a removed font or of a scaled copy */
    font_reap();
    return xptr(x);
}

/* the TEXTXFORM the driver gets for the DC's realized font f */
void font_xform(LPDC dc, LPFONT f, TEXTXFORM __far *tx)
{
    LPFONTOBJ fo = obj_get(dc->hFont, FONT_MAGIC);
    LPFONTINFO fi = f->fi;
    mem_set(tx, 0, sizeof *tx);
    tx->txfHeight = fi->dfPixHeight;
    tx->txfWidth = fi->dfAvgWidth;
    tx->txfWeight = fi->dfWeight;
    /* bold by the driver (dpText TC_EA_DOUBLE: doubling the weight):
       3.1's dialogs show MS Sans Serif 8 bold, which SSERIFE.FON lacks.
       GD11 on 3.1: from a weight 200 above the font's (600 for a 400
       font, 900 for System's 700), the weight then the font's + 300
       (700, 1000), scaled or not. UNVERIFIED: the TEXTXFORM's weight
       (tmWeight shows it) */
    if (fo && fo->lf.lfWeight >= fi->dfWeight + 200 &&
        (dc->dev->info.dpText & 0x0200)) {
        tx->txfWeight = fi->dfWeight + 300;
        tx->txfAccelerator |= 0x0200;
        tx->txfOverhang = 1;
    }
    /* GD10 (GD1's log) on 3.1: italic, underline and strike-out the font
       lacks are asked with 400h, 800h and 1000h in txfAccelerator (VGA's
       dpText has none of them, so GDI draws them itself: text.c);
       txfItalic is 1, txfUnderline and txfStrikeOut FFh, and the italic
       overhang, (dfPixHeight - 1) / 2, is added to the emboldening's */
    if (fo && fo->lf.lfItalic && !fi->dfItalic) {
        tx->txfItalic = 1;
        tx->txfAccelerator |= 0x0400;
        tx->txfOverhang += (fi->dfPixHeight - 1) / 2;
    }
    if (fo && fo->lf.lfUnderline && !fi->dfUnderline) {
        tx->txfUnderline = (char)0xFF;
        tx->txfAccelerator |= 0x0800;
    }
    if (fo && fo->lf.lfStrikeOut && !fi->dfStrikeOut) {
        tx->txfStrikeOut = (char)0xFF;
        tx->txfAccelerator |= 0x1000;
    }
}

/* the italic overhang GDI adds to extents and character widths (0: not
   simulated) */
int font_italic_overhang(LPFONTINFO fi, const TEXTXFORM __far *tx)
{
    return (tx->txfAccelerator & 0x0400) ? (fi->dfPixHeight - 1) / 2 : 0;
}
