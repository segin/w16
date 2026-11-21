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
static BYTE background;                 /* 386 Enhanced mode: a DOS VM has the screen */
static BYTE repaint_due;                /* RepaintScreen postponed (UserRepaintDisable) */
static BYTE first_enable = 1;
static void trail_boot(void);          /* mouse trails: below */
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
        trail_boot();
    }
    hw_enable();
    enabled = 1;
    background = 0;
    cursor_init();
    if (winflags & WF_ENHANCED)
        hook2f();
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
    unhook2f();
    cursor_off();
    save_discard();
    enabled = 0;
    background = 0;
    hw_disable();
    return 1;
}

/* WORD Control(lpDevice, wFunction, lpInData, lpOutData): escapes */
#define QUERYESCSUPPORT 8
#define GETCOLORTABLE 5
#define MOUSETRAILS 39

/* ---- Mouse trails (DDK "Mouse Trails", MOUSETRAILS; the drawing is
   cursor.c's). Observed on Windows 3.1's VGA.DRV with VG9
   (tests/apps/vg9.c, drivers/vga/README.md):
   - at the first Enable WIN.INI [windows] MouseTrails is read (absent:
     -7): on when 0 or more, the size its absolute value cut to 1..7 (0
     and words that are no number: on, size 1; 9: on, 7; -5: off, 5); the
     driver writes nothing then;
   - QUERYESCSUPPORT(MOUSETRAILS): the size, negative when off;
   - MOUSETRAILS n, the result the same: 1..7 (more: 7) the size, on, and
     MouseTrails=n written; 0 off, and MouseTrails=-size written when it
     was on; -1 on, and MouseTrails=size written when it was off (WIN.INI
     is not read: the size is the driver's); -2 off, -3 on, neither
     writing; below -3 as -3, but -32768 as -1. Contrary to the DDK, -1
     does not read WIN.INI and 0 writes nothing when already off.
   - the cursor and trail come off the screen when the escape turns the
     trail on or off or sets a size (cursor_trail); QUERYESCSUPPORT and an
     escape that changes nothing leave them.
   UNVERIFIED: 3.1's driver sometimes writes the positive value with a
   leading space (" 7"), depending on something VG9 did not find (not the
   caller's stack); w16 never does. 1000 (seen once) turned trails on at 7
   without writing WIN.INI there. ---- */
static BYTE trail_on;
static BYTE trail_size = 7;

static int trail_state(void)
{
    return trail_on ? trail_size : -(int)trail_size;
}

static void trail_write(int v)
{
    char b[4];
    int i = 0;
    if (v < 0) {
        b[i++] = '-';
        v = -v;
    }
    b[i++] = (char)('0' + v);
    b[i] = 0;
    WriteProfileString("windows", "MouseTrails", b);
}

static void trail_set(int on, int size)
{
    trail_on = (BYTE)on;
    trail_size = (BYTE)size;
    cursor_trail(on, size);
}

static void trail_boot(void)
{
    int v = GetProfileInt("windows", "MouseTrails", -7);
    /* a still trail runs down an image every 2 ticks (the first on the
       first tick): about 600 ms for seven images, as Windows 3.1's trail
       looked to the user, booted interactively in DOSBox (550-600 ms; set
       from that observation, not measured by a probe: VG9 and CR1 see
       3.1's driver drop one image per CheckCursor call). SYSTEM.INI [w16]
       MouseTrailDecay=1: one a tick, what the probes compare with 3.1. */
    int on = v >= 0;
    cursor_decay(GetPrivateProfileInt("w16", "MouseTrailDecay", 2, "SYSTEM.INI"));
    if (v < 0)
        v = v == -32768 ? TRAIL_MAX : -v;
    if (v < 1)
        v = 1;
    if (v > TRAIL_MAX)
        v = TRAIL_MAX;
    trail_set(on, v);
}

static int mouse_trails(int n)
{
    if (n >= 1) {
        if (n > TRAIL_MAX)
            n = TRAIL_MAX;
        trail_set(1, n);
        trail_write(n);
    } else if (n == 0 || n == -2) {
        if (trail_on) {
            trail_set(0, trail_size);
            if (n == 0)
                trail_write(-(int)trail_size);
        }
    } else if (!trail_on) {
        /* -1 (and -32768) write WIN.INI; -3 and below do not */
        trail_set(1, trail_size);
        if (n == -1 || n == -32768)
            trail_write(trail_size);
    }
    return trail_state();
}
WORD EXPORT Control(LPVOID lpDevice, WORD fn, LPVOID in, LPVOID out)
{
    (void)lpDevice;
    switch (fn) {
    case QUERYESCSUPPORT:
        if (!in)
            return 0;
        /* VG8: GETCOLORTABLE and QUERYESCSUPPORT 1; MOUSETRAILS the
           trail's state (VG9: the size, negative when off) */
        switch (*(LPWORD)in) {
        case QUERYESCSUPPORT:
        case GETCOLORTABLE:
            return 1;
        case MOUSETRAILS:
            return (WORD)trail_state();
        }
        return 0;
    case MOUSETRAILS:
        if (!in)
            return 0;
        return (WORD)mouse_trails(*(LPSHORT)in);
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

typedef WORD (__far __pascal *ENUMPROC)(LPVOID, LPVOID);

/* WORD EnumObj(lpDevice, wStyle, lpCallback, lpClientData): what
   Windows 3.1's driver enumerates (VG8, EnumObjects):
   - pens: styles 0-4 (solid ... dash-dot-dot), each in the 16 colours
     from white down to black (index order backwards), width 0;
   - brushes: solid ones in every colour whose red, green and blue are
     each 255, 192, 128, 64 or 0 (blue changing fastest, from 255 down),
     then hatched ones, hatch 5 down to 0, each in the 16 colours from
     white down to black.
   The last callback's answer is returned (1 when it never said stop). */
WORD EXPORT EnumObj(LPVOID lpDevice, WORD style, FARPROC cb, LPVOID data)
{
    static const BYTE levels[5] = { 255, 192, 128, 64, 0 };
    WORD r = 1;
    int i, h, rr, gg, bb;
    (void)lpDevice;
    if (style == OBJ_PEN) {
        LPEN p;
        for (h = LS_SOLID; h <= LS_DASHDOTDOT; h++)
            for (i = 15; i >= 0; i--) {
                p.lopnStyle = h;
                p.lopnWidth.x = 0;
                p.lopnWidth.y = 0;
                p.lopnColor = index_rgb(i);
                r = ((ENUMPROC)cb)((LPVOID)&p, data);
                if (!r)
                    return 0;
            }
    } else if (style == OBJ_BRUSH) {
        LBRUSH b;
        for (rr = 0; rr < 5; rr++)
            for (gg = 0; gg < 5; gg++)
                for (bb = 0; bb < 5; bb++) {
                    b.lbStyle = BS_SOLID;
                    b.lbColor = RGB(levels[rr], levels[gg], levels[bb]);
                    b.lbHatch = 0;
                    b.lbBkColor = 0;
                    r = ((ENUMPROC)cb)((LPVOID)&b, data);
                    if (!r)
                        return 0;
                }
        for (h = 5; h >= 0; h--)
            for (i = 15; i >= 0; i--) {
                b.lbStyle = BS_HATCHED;
                b.lbColor = index_rgb(i);
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

/* ---- 386 Enhanced mode's screen switches (the DDK's Display Driver
   Adaptation Guide, "Display hardware": the driver hooks INT 2Fh when GDI
   calls Enable; Notify Background Switch (4001h) comes before a
   full-screen DOS VM takes the screen, Notify Foreground Switch (4002h)
   when Windows has it back, and the driver then has USER repaint the
   whole screen with RepaintScreen (USER.275), unless USER has postponed
   that with UserRepaintDisable). While in the background nothing is drawn
   on the screen (surf_get gives no screen surface, the cursor is off):
   the DOS VM's screen is on the VGA, and Windows' is drawn again from
   nothing (the driver called 4000h: entry.asm). ---- */
static void repaint(void)
{
    static FARPROC rs;
    repaint_due = 0;
    if (!rs) {
        WORD h = GetModuleHandle("USER");
        if (h)
            rs = GetProcAddress(h, (LPCSTR)MK_FP(0, 275));
    }
    if (rs)
        rs();
}

/* the INT 2Fh hook (entry.asm), AX the function; DS ours, any stack */
void __cdecl __far scr_switch(WORD ax)
{
    if (ax == 0x4001) {
        if (!enabled)
            return;
        cursor_off();
        save_discard();
        enabled = 0;
        background = 1;
    } else if (ax == 0x4002) {
        /* also without a 4001h before it (the DDK: after errors) */
        if (!enabled && !background)
            return;
        background = 0;
        enabled = 1;
        scr_reset();
        cursor_init();
        if (repaint_off)
            repaint_due = 1;
        else
            repaint();
    } else if (ax == 0x4006 && enabled) {
        /* the VDD used the registers (4005h, nothing kept: every
           drawing operation sets what it needs): as the drawing code
           expects them */
        scr_reset();
    }
}

/* UserRepaintDisable(BOOL): USER's (GetProcAddress ordinal 500; VGREC:
   one word popped, called with 1 then 0 around ...). Recorded only. */
WORD EXPORT UserRepaintDisable(WORD disable)
{
    repaint_off = (BYTE)disable;
    if (!disable && repaint_due)
        repaint();
    return 0;
}
