/*
 * vga.c - VGA.DRV: Enable, Disable, the GDIINFO, Control (escapes),
 * EnumObj, EnumDFonts and the entry points GDI does not use.
 *
 * Sources: the Windows 3.1 DDK's Device Driver Adaptation Guide (the
 * display driver chapters and each function's reference page) and
 * black-box observation of Windows 3.1's VGA.DRV through VGREC, the
 * recording stand-in (tests/vgaprobe.sh -l: every call GDI and USER make,
 * the values they pass, what comes back, the bytes each entry pops).
 */
#include "vga.h"

/* The GDIINFO, value for value as Windows 3.1's VGA.DRV gives it to GDI
   (VGREC: Enable(style 1) returned 6Eh and these 55 words; GDI's
   GetDeviceCaps reads them back, so programs see every one). */
GDIINFO gdiinfo = {
    0x030A,             /* dpVersion */
    1,                  /* dpTechnology: DT_RASDISPLAY */
    208, 156,           /* dpHorzSize, dpVertSize (mm) */
    640, 480,           /* dpHorzRes, dpVertRes */
    1, 4,               /* dpBitsPixel, dpPlanes */
    -1, 80, 0, 0,       /* dpNumBrushes, dpNumPens, futureuse, dpNumFonts */
    16,                 /* dpNumColors */
    0x23,               /* dpDEVICEsize (GetDeviceCaps(PDEVICESIZE)) */
    0,                  /* dpCurves: none (GDI makes them of scan lines) */
    0x22,               /* dpLines: LC_POLYLINE | LC_STYLED */
    0x08,               /* dpPolygonals: PC_SCANLINE only */
    0x2204,             /* dpText: TC_CP_STROKE | TC_EA_DOUBLE | TC_RA_ABLE */
    1,                  /* dpClip: CP_RECTANGLE */
    0x46D9,             /* dpRaster: BITBLT BITMAP64 GDI20_OUTPUT SAVEBITMAP
                           DI_BITMAP DIBTODEV BIGFONT OP_DX_OUTPUT */
    36, 36, 51,         /* dpAspectX, Y, XY */
    102,                /* dpStyleLen */
    { 2080, 1560 }, { 640, -480 },     /* MM_LOMETRIC */
    { 20800, 15600 }, { 640, -480 },   /* MM_HIMETRIC */
    { 325, 325 }, { 254, -254 },       /* MM_LOENGLISH */
    { 1625, 1625 }, { 127, -127 },     /* MM_HIENGLISH */
    { 2340, 2340 }, { 127, -127 },     /* MM_TWIPS */
    96, 96,             /* dpLogPixelsX, Y */
    4,                  /* dpDCManage */
    0,                  /* dpCaps1 */
    0, 0,               /* dpSpotSizeX, Y */
    0, 0, 0             /* palette: none */
};

BYTE enabled;
BYTE repaint_off;
static BYTE first_enable = 1;
static BYTE bios_rows;                  /* 0040:0084 when Windows started */

static BYTE __far *bios_data(WORD off)
{
    extern WORD __cdecl bios_sel;
    return (BYTE __far *)MK_FP(bios_sel, off);
}

int __cdecl __far vga_init(void)
{
    return 1;
}

/* The screen's colours. After the BIOS's mode 12h (whose DAC holds the
   64 EGA colours), Windows 3.1's VGA.DRV leaves the attribute
   controller's palette registers 0-15 pointing at these DAC registers
   and DAC registers 7 and 15 changed (VG1: read back from the hardware
   after Enable; a screenshot shows the same colours). These are the
   hardware values that make the screen look as 3.1's does. */
static const BYTE attr_pal[16] = {
    0x00, 0x0C, 0x0A, 0x0E, 0x01, 0x15, 0x23, 0x07,
    0x0F, 0x24, 0x12, 0x36, 0x09, 0x2D, 0x1B, 0x3F
};
static const BYTE dac7[3] = { 0x21, 0x22, 0x23 }, dac15[3] = { 0x30, 0x31, 0x32 };

static void set_palette(void)
{
    int i;
    inb(0x3DA);
    for (i = 0; i < 16; i++) {
        outb(0x3C0, (BYTE)i);
        outb(0x3C0, attr_pal[i]);
    }
    outb(0x3C0, 0x20);                  /* palette address source on */
    outb(0x3C8, 7);
    for (i = 0; i < 3; i++)
        outb(0x3C9, dac7[i]);
    outb(0x3C8, 15);
    for (i = 0; i < 3; i++)
        outb(0x3C9, dac15[i]);
}

static void hw_enable(void)
{
    int10(0x0012, 0, 0, 0);             /* 640 x 480, 16 colours */
    set_palette();
    scr_reset();
}

/* DWORD Enable(lpDevice, wStyle, lpDestDevType, lpOutputFile, lpData):
   style bit 0 set: copy the GDIINFO and return its size; else set up the
   PDEVICE and the hardware (GDI: also when Windows comes back from a DOS
   program in Standard mode). VGREC: GDI's first CreateDC calls it with
   style 1, then 0; it pops 18 bytes. */
WORD EXPORT Enable(LPVOID lpDevice, WORD style, LPCSTR type, LPCSTR file, LPVOID data)
{
    (void)type;
    (void)file;
    (void)data;
    if (style & 1) {
        memcpyf(lpDevice, &gdiinfo, sizeof gdiinfo);
        return sizeof gdiinfo;
    }
    {
        LPPDEV d = (LPPDEV)lpDevice;
        d->pdType = PDEV_TYPE;
        d->width = 640;
        d->height = 480;
        d->magic = PDEV_MAGIC;
    }
    if (first_enable) {
        /* the BIOS's row count before Windows: restored by Disable
           (WIN.COM leaves it as it was before its logo: ARCHITECTURE.md
           9.5) */
        bios_rows = *bios_data(0x84);
        first_enable = 0;
    }
    hw_enable();
    enabled = 1;
    cursor_init();
    return 1;
}

/* the text mode Windows started from: mode 3, with 50 (43) lines when the
   BIOS said so before Windows */
static void hw_disable(void)
{
    int10(0x0003, 0, 0, 0);
    if (bios_rows > 24 + 1)
        int10(0x1112, 0, 0, 0);         /* the 8 x 8 font */
}

/* void Disable(lpDevice): Windows ends, or (Standard mode) a DOS program
   gets the screen: text mode; Enable comes again on the way back */
WORD EXPORT Disable(LPVOID lpDevice)
{
    (void)lpDevice;
    cursor_off();
    save_discard();
    enabled = 0;
    hw_disable();
    return 1;
}

/* WORD Control(lpDevice, wFunction, lpInData, lpOutData): escapes */
#define QUERYESCSUPPORT 8
#define GETCOLORTABLE 5
#define MOUSETRAILS 39
WORD EXPORT Control(LPVOID lpDevice, WORD fn, LPVOID in, LPVOID out)
{
    (void)lpDevice;
    switch (fn) {
    case QUERYESCSUPPORT:
        if (!in)
            return 0;
        switch (*(LPWORD)in) {
        case QUERYESCSUPPORT:
        case GETCOLORTABLE:
            return 1;
        }
        return 0;
    case GETCOLORTABLE:
        if (!in || !out)
            return 0;
        {
            int i = *(LPSHORT)in;
            if (i < 0 || i > 15)
                return 0;
            *(DWORD __far *)out = index_rgb(dib_to_index[i]);
            return 1;
        }
    }
    return 0;
}

/* the colours EnumObj gives, in its order: the eight EGA colours the DDK
   asks for first, then the other eight (UNVERIFIED against 3.1's order) */
static const DWORD enum_colors[16] = {
    0x000000L, 0xFFFFFFL, 0x0000FFL, 0x00FF00L, 0xFF0000L, 0x00FFFFL, 0xFF00FFL, 0xFFFF00L,
    0x000080L, 0x008000L, 0x800000L, 0x008080L, 0x800080L, 0x808000L, 0x808080L, 0xC0C0C0L
};

typedef WORD (__far __pascal *ENUMPROC)(LPVOID, LPVOID);

/* WORD EnumObj(lpDevice, wStyle, lpCallback, lpClientData) */
WORD EXPORT EnumObj(LPVOID lpDevice, WORD style, FARPROC cb, LPVOID data)
{
    WORD r = 1;
    int i, h;
    (void)lpDevice;
    if (style == OBJ_PEN) {
        LPEN p;
        for (i = 0; i < 16; i++) {
            p.lopnStyle = LS_SOLID;
            p.lopnWidth.x = 1;
            p.lopnWidth.y = 0;
            p.lopnColor = enum_colors[i];
            r = ((ENUMPROC)cb)((LPVOID)&p, data);
            if (!r)
                return 0;
        }
    } else if (style == OBJ_BRUSH) {
        LBRUSH b;
        for (i = 0; i < 16; i++) {
            b.lbStyle = BS_SOLID;
            b.lbColor = enum_colors[i];
            b.lbHatch = 0;
            b.lbBkColor = 0;
            r = ((ENUMPROC)cb)((LPVOID)&b, data);
            if (!r)
                return 0;
        }
        for (h = 0; h < 6; h++)
            for (i = 0; i < 16; i++) {
                b.lbStyle = BS_HATCHED;
                b.lbColor = enum_colors[i];
                b.lbHatch = h;
                b.lbBkColor = 0;
                r = ((ENUMPROC)cb)((LPVOID)&b, data);
                if (!r)
                    return 0;
            }
    }
    return r;
}

/* WORD EnumDFonts(lpDevice, lpFaceName, lpCallback, lpClientData): the
   display has no device fonts */
WORD EXPORT EnumDFonts(LPVOID lpDevice, LPCSTR face, FARPROC cb, LPVOID data)
{
    (void)lpDevice;
    (void)face;
    (void)cb;
    (void)data;
    return 1;
}

/* the entry points GDI does not use (DDK: stubs returning 0) */
WORD EXPORT DeviceMode(WORD hwnd, WORD hinst, LPCSTR type, LPCSTR file)
{
    (void)hwnd;
    (void)hinst;
    (void)type;
    (void)file;
    return 1;
}

WORD EXPORT SetAttribute(LPVOID lpDevice, WORD state, WORD index, WORD attr)
{
    (void)lpDevice;
    (void)state;
    (void)index;
    (void)attr;
    return 0;
}

WORD EXPORT DeviceBitmap(LPVOID lpDevice, WORD cmd, LPVOID bm, LPVOID bits)
{
    (void)lpDevice;
    (void)cmd;
    (void)bm;
    (void)bits;
    return 0;
}

WORD EXPORT CreateBitmap(void)
{
    return 0;
}

/* UserRepaintDisable(BOOL): USER's (GetProcAddress ordinal 500; VGREC:
   one word popped, called with 1 then 0 around ...). Recorded only. */
WORD EXPORT UserRepaintDisable(WORD disable)
{
    repaint_off = (BYTE)disable;
    return 0;
}
