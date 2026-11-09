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
 * The mapper: UNVERIFIED. 3.1's penalty weights are not observed yet; the
 * one here takes an exact face (after WIN.INI [FontSubstitutes]) and the
 * nearest height, which is what the boot needs (System, MS Sans Serif 8).
 * w16 ships no fonts: whatever .FON files the installation has are used.
 */
#include "gdi.h"

NPFONT fonts;

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
    if (!p)
        return 0;
    n = *(WORD __far *)p;
    p += 2;
    for (i = 0; i < n; i++) {
        NPFONT f = (NPFONT)LocalAlloc(LMEM_FIXED | LMEM_ZEROINIT, sizeof(FONTENT));
        BYTE __far *e = p + 2;
        const char __far *dev, *face;
        int k;
        if (!f)
            break;
        f->hMod = h;
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
            f->file[k] = file[k];
        f->next = fonts;
        fonts = f;
        added++;
        gdi_log("font: ");
        gdi_log(f->face);
        gdi_loghex(" height", f->height);
        p = (BYTE __far *)face;
        while (*p)
            p++;
        p++;
    }
    FreeResource(g);
    return added;
}

static void font_load_boot(LPCSTR file)
{
    HINSTANCE h = LoadLibrary(file);
    if (h >= 32)
        font_add_module(h, file);
}

void font_init(void)
{
    char buf[80];
    font_load_boot("FONTS.FON");
    GetPrivateProfileString("boot", "fixedfon.fon", "", buf, sizeof buf, "SYSTEM.INI");
    if (buf[0])
        font_load_boot(buf);
    font_load_boot("OEMFONTS.FON");
}

int GDIAPI AddFontResource(LPCSTR file)                            /* GDI.119 */
{
    HINSTANCE h;
    if (!file)
        return 0;
    if (!SELECTOROF(file)) {            /* a module handle */
        return font_add_module((HINSTANCE)OFFSETOF(file), 0);
    }
    h = LoadLibrary(file);
    if (h < 32)
        return 0;
    return font_add_module(h, file);
}

BOOL GDIAPI RemoveFontResource(LPCSTR file)                        /* GDI.136 */
{
    NPFONT *pp = &fonts;
    HINSTANCE h = 0;
    int found = 0;
    while (*pp) {
        NPFONT f = *pp;
        if (file && SELECTOROF(file) ? !str_icmp(f->file, file) : f->hMod == (HINSTANCE)OFFSETOF(file)) {
            *pp = f->next;
            h = f->hMod;
            if (f->hRes) {
                GlobalUnlock(f->hRes);
                FreeResource(f->hRes);
            }
            if (f->hMem) {
                GlobalUnlock(f->hMem);
                GlobalFree(f->hMem);
            }
            LocalFree((HLOCAL)f);
            found = 1;
            continue;
        }
        pp = &f->next;
    }
    if (h && SELECTOROF(file))
        FreeLibrary(h);
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

static long penalty(NPFONT f, const LOGFONT __far *lf, const char *face)
{
    long p = 0;
    int want, have, d;
    if (f->type & 1)                     /* vector fonts: not yet */
        p += 20000;
    if (f->type & 4 || !f->height)       /* TrueType (.FOT): not rendered yet */
        return 0x7FFFFFFFL;
    if (lf->lfCharSet != f->charset && !(lf->lfCharSet == 1))
        p += 65000L;
    if (face[0] && str_icmp(face, f->face))
        p += 10000;
    if (!face[0] && (lf->lfPitchAndFamily & 0xF0) && (lf->lfPitchAndFamily & 0xF0) != (f->pitch & 0xF0))
        p += 9000;
    if ((lf->lfPitchAndFamily & 3) == 1 && (f->pitch & 1))
        p += 15000;                      /* fixed asked, variable font */
    if (lf->lfHeight < 0) {
        want = -lf->lfHeight;
        have = f->height - f->il;
    } else {
        want = lf->lfHeight ? lf->lfHeight : 12;
        have = f->height;
    }
    d = have - want;
    p += d > 0 ? 600L * d : 150L * -d;
    if (lf->lfWidth) {
        d = f->avgWidth - lf->lfWidth;
        p += 50L * (d < 0 ? -d : d);
    }
    d = (lf->lfWeight ? lf->lfWeight : 400) - f->weight;
    p += 3L * (d < 0 ? -d : d) / 100;
    if (!!lf->lfItalic != !!f->italic)
        p += 4;
    return p;
}

static NPFONT font_map(const LOGFONT __far *lf)
{
    char face[LF_FACESIZE];
    NPFONT f, best = 0;
    long bp = 0x7FFFFFFFL;
    substitute(lf->lfFaceName, face);
    for (f = fonts; f; f = f->next) {
        long p = penalty(f, lf, face);
        if (p < bp) {
            bp = p;
            best = f;
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
static LPFONTINFO font_info(NPFONT f)
{
    HRSRC r;
    BYTE __far *p, __far *base;
    LPFONTINFO fi;
    if (f->fi)
        return f->fi;
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
        f->hMem = GlobalAlloc(GMEM_MOVEABLE | GMEM_ZEROINIT | GMEM_SHARE, newSize);
        q = f->hMem ? GlobalLock(f->hMem) : 0;
        if (!q)
            return 0;
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

NPFONT font_realize(NPDC dc)
{
    FONTOBJ NEAR *fo = obj_get(dc->hFont, FONT_MAGIC);
    NPFONT f;
    LPFONTINFO fi;
    LOGFONT lf;
    if (!fo)
        return 0;
    if (dc->pfont && dc->hFontOf == dc->hFont && dc->lpPDevOfFont == dc->lpPDevice)
        return dc->pfont;
    lf = fo->lf;
    /* the driver first: device fonts (none on VGA: RealizeObject gives 0) */
    ((DRVREALIZE)dc->dev->fn[DRV_REALIZEOBJECT])(dc->lpPDevice, OBJ_FONT, &lf, 0, 0);
    f = font_map(&lf);
    if (!f)
        return 0;
    gdi_log("font_realize: ");
    gdi_log(lf.lfFaceName);
    gdi_loghex(" -> height", f->height);
    gdi_log(f->face);
    gdi_log("\r\n");
    fi = font_info(f);
    if (!fi)
        return 0;
    dc->pfont = f;
    dc->hFontOf = dc->hFont;
    dc->lpPDevOfFont = dc->lpPDevice;
    mem_set(&dc->xform, 0, sizeof dc->xform);
    dc->xform.txfHeight = fi->dfPixHeight;
    dc->xform.txfWidth = fi->dfAvgWidth;
    dc->xform.txfWeight = fi->dfWeight;
    /* UNVERIFIED: simulated attributes (underline, strike-out, italic,
       bold) and their accelerator bits */
    return f;
}
