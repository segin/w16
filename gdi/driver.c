/*
 * driver.c - binding a device driver (the display, later printers).
 *
 * Observed on 3.1 (GD1, tests/gdi): the display driver is enabled by the
 * first CreateDC("DISPLAY") (USER's), not by GDI's initialisation: Enable
 * is called first with style 1 and a GDIINFO buffer in GDI's data segment
 * (it returned 6Eh, the GDIINFO size), then with style 0 and a PDEVICE of
 * dpDEVICEsize bytes in its own global segment (offset 0), the type,
 * output and data pointers being NULL for the display. GDI finds the
 * driver's functions by ordinal (GetProcAddress; GDI imports nothing from
 * DISPLAY). DDK: Device Driver Adaptation Guide, "Enabling and Disabling
 * the Display Driver" and the function reference.
 */
#include "gdi.h"

NPDEVICE display;
static NPDEVICE devices;

NPDEVICE dev_open(LPCSTR name)
{
    NPDEVICE d;
    HINSTANCE h;
    char file[32];
    char key[16];
    int i, n;
    DWORD r;
    /* the table keeps 15 characters of the name: a longer one is looked
       up by as many (it was loaded again at every CreateDC) */
    for (i = 0; i < 15 && name[i]; i++)
        key[i] = name[i];
    key[i] = 0;
    for (d = devices; d; d = d->next)
        if (!str_icmp(d->name, key)) {
            d->refs++;
            return d;
        }
    /* GD2: 3.1 loads "<name>.drv" (BOOTLOG: LoadStart = DISPLAY.drv at
       USER's INIT=Display) from a copy: the caller's buffer was the same
       before and after CreateDC */
    for (n = 0; name[n] && n < 20; n++)
        file[n] = name[n];
    file[n] = 0;
    lstrcat(file, ".drv");
    h = LoadLibrary(file);
    if (h < 32)
        return 0;
    d = (NPDEVICE)LocalAlloc(LMEM_FIXED | LMEM_ZEROINIT, sizeof(DEVICE));
    if (!d) {
        FreeLibrary(h);
        return 0;
    }
    d->hDriver = h;
    for (i = 0; i < 15 && name[i]; i++)
        d->name[i] = name[i];
    for (i = 1; i < DRV_NENTRIES; i++)
        d->fn[i] = GetProcAddress(h, MAKEINTRESOURCE(i));
    gdi_loghex("dev_open: module", h);
    gdi_loghex("dev_open: Enable at offset", OFFSETOF(d->fn[DRV_ENABLE]));
    if (!d->fn[DRV_ENABLE]) {
        LocalFree((HLOCAL)d);
        FreeLibrary(h);
        return 0;
    }
    r = ((DRVENABLE)d->fn[DRV_ENABLE])(&d->info, 1, 0, 0, 0);
    gdi_loghex("dev_open: Enable(1) returned", LOWORD(r));
    if (!LOWORD(r))
        goto fail;
    d->hPDevice = GlobalAlloc(GMEM_MOVEABLE | GMEM_ZEROINIT | GMEM_SHARE,
                              d->info.dpDEVICEsize ? d->info.dpDEVICEsize : 2);
    d->lpPDevice = GlobalLock(d->hPDevice);
    if (!d->lpPDevice)
        goto fail;
    r = ((DRVENABLE)d->fn[DRV_ENABLE])(d->lpPDevice, 0, 0, 0, 0);
    gdi_loghex("dev_open: Enable(0) returned", LOWORD(r));
    if (!LOWORD(r))
        goto fail;
    d->refs = 1;
    d->next = devices;
    devices = d;
    if (!str_icmp(name, "DISPLAY"))
        display = d;
    return d;
fail:
    if (d->hPDevice)
        GlobalFree(d->hPDevice);
    LocalFree((HLOCAL)d);
    FreeLibrary(h);
    return 0;
}

void dev_close(NPDEVICE d)
{
    NPDEVICE *pp;
    if (!d || --d->refs)
        return;
    if (d == display) {                 /* the display stays enabled */
        d->refs = 1;
        return;
    }
    if (d->fn[DRV_DISABLE])
        ((DRVDISABLE)d->fn[DRV_DISABLE])(d->lpPDevice);
    for (pp = &devices; *pp; pp = &(*pp)->next)
        if (*pp == d) {
            *pp = d->next;
            break;
        }
    GlobalUnlock(d->hPDevice);
    GlobalFree(d->hPDevice);
    FreeLibrary(d->hDriver);
    LocalFree((HLOCAL)d);
}

typedef struct { BYTE peRed, peGreen, peBlue, peFlags; } PALENTRY;
WORD GDIAPI GetPaletteEntries(HPALETTE, WORD, WORD, PALENTRY __far *);

/* a logical colour to the DC's physical one (ColorInfo). On the VGA (no
   palette) PALETTEINDEX(i) is the DC's palette's entry i (entry 0 past
   its end: UNVERIFIED) and PALETTERGB the plain RGB (GD15 on 3.1: a
   source DC's background colour PALETTEINDEX(7) picks the default
   palette's silver in a colour -> monochrome BitBlt). */
DWORD dev_colorinfo(LPDC dc, COLORREF c)
{
    DWORD pc = 0;
    if ((BYTE)(c >> 24) == 1) {
        PALENTRY e;
        HPALETTE pal = dc->hPal;
        if (!GetPaletteEntries(pal, LOWORD(c), 1, &e) && !GetPaletteEntries(pal, 0, 1, &e))
            e.peRed = e.peGreen = e.peBlue = 0;
        c = RGB(e.peRed, e.peGreen, e.peBlue);
    } else if ((BYTE)(c >> 24) == 2)
        c &= 0xFFFFFFL;
    if (dc->dev->fn[DRV_COLORINFO])
        ((DRVCOLORINFO)dc->dev->fn[DRV_COLORINFO])(dc->lpPDevice, c, &pc);
    return pc;
}
