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

/* read the FONTDIR of a module: one entry per font */
static int font_add_module(HINSTANCE h, LPCSTR file)
{
    HRSRC r = FindResource(h, "FONTDIR", MAKEINTRESOURCE(RT_FONTDIR));
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
    NPFONT NEAR *pp = &fonts;
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
    int i;
    for (i = 0; i < LF_FACESIZE - 1 && face[i]; i++)
        out[i] = face[i];
    out[i] = 0;
    if (out[0])
        GetProfileString("FontSubstitutes", face, out, out, LF_FACESIZE);
}

static long penalty(NPFONT f, const LOGFONT NEAR *lf, const char *face)
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

static NPFONT font_map(const LOGFONT NEAR *lf)
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

/* the font resource, locked, dfBitsPointer set */
static LPFONTINFO font_info(NPFONT f)
{
    if (!f->fi) {
        HRSRC r = FindResource(f->hMod, MAKEINTRESOURCE(f->resId), MAKEINTRESOURCE(RT_FONT));
        BYTE __far *p;
        if (!r)
            return 0;
        f->hRes = LoadResource(f->hMod, r);
        p = f->hRes ? LockResource(f->hRes) : 0;
        if (!p)
            return 0;
        f->fi = (LPFONTINFO)(p + 0x42);
        f->fi->dfBitsPointer = MAKELP(SELECTOROF(p), OFFSETOF(p) + (WORD)f->fi->dfBitsOffset);
    }
    return f->fi;
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
