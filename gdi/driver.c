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
    int i, n;
    DWORD r;
    for (d = devices; d; d = d->next)
        if (!str_icmp(d->name, name)) {
            d->refs++;
            return d;
        }
    h = GetModuleHandle(name);
    if (!h) {
        for (n = 0; name[n] && n < 20; n++)
            file[n] = name[n];
        file[n] = 0;
        lstrcat(file, ".DRV");
        h = LoadLibrary(file);
        if (h < 32)
            return 0;
    }
    d = (NPDEVICE)LocalAlloc(LMEM_FIXED | LMEM_ZEROINIT, sizeof(DEVICE));
    if (!d)
        return 0;
    d->hDriver = h;
    for (i = 0; i < 15 && name[i]; i++)
        d->name[i] = name[i];
    for (i = 1; i < DRV_NENTRIES; i++)
        d->fn[i] = GetProcAddress(h, MAKEINTRESOURCE(i));
    gdi_loghex("dev_open: module", h);
    gdi_loghex("dev_open: Enable at offset", OFFSETOF(d->fn[DRV_ENABLE]));
    if (!d->fn[DRV_ENABLE]) {
        LocalFree((HLOCAL)d);
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

/* a logical colour to the DC's physical one (ColorInfo) */
DWORD dev_colorinfo(NPDC dc, COLORREF c)
{
    DWORD pc = 0;
    ((DRVCOLORINFO)dc->dev->fn[DRV_COLORINFO])(dc->lpPDevice, c, &pc);
    return pc;
}
