/*
 * vga.h - w16's VGA display driver (VGA.DRV, module DISPLAY).
 *
 * The GDI <-> display driver interface: structures and constants as the
 * Windows 3.1 DDK documentation (Device Driver Adaptation Guide) describes
 * them, rewritten here; the driver's own private structures (PDEVICE,
 * physical pens, brushes and colours) are w16's design: GDI treats them as
 * opaque (README.md).
 *
 * Built large model, SS != DS, every exported entry __loadds: DS is the
 * driver's one fixed data segment (DGROUP).
 */
#ifndef VGA_H
#define VGA_H

typedef unsigned char BYTE;
typedef unsigned short WORD;
typedef unsigned long DWORD;
typedef short SHORT;
typedef long LONG;
typedef int BOOL;
typedef WORD HANDLE;
typedef DWORD COLORREF;
typedef void __far *LPVOID;
typedef char __far *LPSTR;
typedef const char __far *LPCSTR;
typedef BYTE __far *LPBYTE;
typedef WORD __far *LPWORD;
typedef short __far *LPSHORT;
typedef int (__far __pascal *FARPROC)();

#define NULL 0
#define TRUE 1
#define FALSE 0
#define FAR __far
#define NEAR __near
#define EXPORT __far __pascal __loadds

#define MAKELONG(lo, hi) (((DWORD)(WORD)(hi) << 16) | (WORD)(lo))
#define LOWORD(l) ((WORD)(DWORD)(l))
#define HIWORD(l) ((WORD)((DWORD)(l) >> 16))
#define SELECTOROF(p) ((WORD)((DWORD)(p) >> 16))
#define OFFSETOF(p) ((WORD)(DWORD)(p))
#define MK_FP(s, o) ((void __far *)(((DWORD)(WORD)(s) << 16) | (WORD)(o)))
#define GetRValue(c) ((BYTE)(c))
#define GetGValue(c) ((BYTE)((WORD)(c) >> 8))
#define GetBValue(c) ((BYTE)((c) >> 16))
#define RGB(r, g, b) ((DWORD)(((BYTE)(r) | ((WORD)(g) << 8)) | (((DWORD)(BYTE)(b)) << 16)))

#pragma pack(push, 1)
typedef struct { short x, y; } POINT;
typedef POINT __far *LPPOINT;
typedef struct { short left, top, right, bottom; } RECT;
typedef RECT __far *LPRECT;

/* GDIINFO (DDK): what Enable(style 1) copies to GDI; 6Eh bytes */
typedef struct {
    short dpVersion, dpTechnology, dpHorzSize, dpVertSize, dpHorzRes, dpVertRes;
    short dpBitsPixel, dpPlanes, dpNumBrushes, dpNumPens, futureuse, dpNumFonts;
    short dpNumColors;
    WORD dpDEVICEsize, dpCurves, dpLines, dpPolygonals, dpText, dpClip, dpRaster;
    short dpAspectX, dpAspectY, dpAspectXY, dpStyleLen;
    POINT dpMLoWin, dpMLoVpt, dpMHiWin, dpMHiVpt, dpELoWin, dpELoVpt;
    POINT dpEHiWin, dpEHiVpt, dpTwpWin, dpTwpVpt;
    short dpLogPixelsX, dpLogPixelsY, dpDCManage, dpCaps1;
    long dpSpotSizeX, dpSpotSizeY;
    short dpPalColors, dpPalReserved, dpPalResolution;
} GDIINFO;

/* PBITMAP (DDK): a memory bitmap; GDI makes and owns these (bmType 0) */
typedef struct {
    short bmType, bmWidth, bmHeight, bmWidthBytes;
    BYTE bmPlanes, bmBitsPixel;
    LPVOID bmBits;
    DWORD bmWidthPlanes;
    LPVOID bmlpPDevice;
    short bmSegmentIndex, bmScanSegment, bmFillBytes, reserved1, reserved2;
} PBITMAP;
typedef PBITMAP __far *LPPBITMAP;

/* DRAWMODE (DDK) */
typedef struct {
    short Rop2, bkMode;
    DWORD bkColor, TextColor;           /* physical */
    short TBreakExtra, BreakExtra, BreakErr, BreakRem, BreakCount, CharExtra;
    COLORREF LbkColor, LTextColor;      /* logical */
} DRAWMODE;
typedef DRAWMODE __far *LPDRAWMODE;

/* TEXTXFORM (DDK) */
typedef struct {
    short txfHeight, txfWidth, txfEscapement, txfOrientation, txfWeight;
    char txfItalic, txfUnderline, txfStrikeOut, txfOutPrecision, txfClipPrecision;
    short txfAccelerator, txfOverhang;
} TEXTXFORM;
typedef TEXTXFORM __far *LPTEXTXFORM;

/* FONTINFO (DDK): a raster font's header from dfType on, as GDI hands it
   to the driver (Windows 3.x layout: GDI converts 2.x fonts, RC_BIGFONT) */
typedef struct {
    short dfType, dfPoints, dfVertRes, dfHorizRes, dfAscent;
    short dfInternalLeading, dfExternalLeading;
    char dfItalic, dfUnderline, dfStrikeOut;
    short dfWeight;
    BYTE dfCharSet;
    short dfPixWidth, dfPixHeight;
    BYTE dfPitchAndFamily;
    short dfAvgWidth, dfMaxWidth;
    BYTE dfFirstChar, dfLastChar, dfDefaultChar, dfBreakChar;
    short dfWidthBytes;
    DWORD dfDevice, dfFace;
    DWORD dfBitsPointer;
    DWORD dfBitsOffset;
    BYTE dfReserved;
    DWORD dfFlags;
    short dfAspace, dfBspace, dfCspace;
    DWORD dfColorPointer;
    DWORD dfReserved1[4];
    /* the character table follows: 3.x raster fonts 6 bytes an entry
       (width, 32-bit offset of the glyph from the font's start) */
} FONTINFO;
typedef FONTINFO __far *LPFONTINFO;
#define FONTINFO_OFFSET 0x42            /* dfType's offset in the font */

/* LBRUSH, LPEN (DDK) */
typedef struct { short lbStyle; DWORD lbColor; short lbHatch; DWORD lbBkColor; } LBRUSH;
typedef struct { short lopnStyle; POINT lopnWidth; DWORD lopnColor; } LPEN;

/* CURSORSHAPE (DDK) */
typedef struct {
    short csHotX, csHotY, csWidth, csHeight, csWidthBytes, csColor;
    BYTE csBits[1];
} CURSORSHAPE;
typedef CURSORSHAPE __far *LPCURSORSHAPE;

/* BITMAPINFOHEADER, RGBQUAD (SDK) */
typedef struct {
    DWORD biSize;
    LONG biWidth, biHeight;
    WORD biPlanes, biBitCount;
    DWORD biCompression, biSizeImage;
    LONG biXPelsPerMeter, biYPelsPerMeter;
    DWORD biClrUsed, biClrImportant;
} BITMAPINFOHEADER;
typedef BITMAPINFOHEADER __far *LPBITMAPINFOHEADER;
typedef struct { BYTE rgbBlue, rgbGreen, rgbRed, rgbReserved; } RGBQUAD;
typedef struct { BYTE rgbtBlue, rgbtGreen, rgbtRed; } RGBTRIPLE;
#define BI_RGB 0
#define BI_RLE8 1
#define BI_RLE4 2

/* LOGFONT and TEXTMETRIC (SDK; EnumDFonts) */
typedef struct {
    short lfHeight, lfWidth, lfEscapement, lfOrientation, lfWeight;
    BYTE lfItalic, lfUnderline, lfStrikeOut, lfCharSet, lfOutPrecision;
    BYTE lfClipPrecision, lfQuality, lfPitchAndFamily;
    char lfFaceName[32];
} LOGFONT;
#pragma pack(pop)

/* ---- w16's own physical objects (opaque to GDI) ---- */

/* a physical colour (ColorInfo): w16's layout. Bits 0-3 the colour index
   (the four planes' bits: 0 blue, 1 green, 2 red, 3 intensity), bit 8
   the colour's value on a monochrome bitmap (1 white). Both are kept
   whatever device the colour was realized for: a colour-to-monochrome
   BitBlt compares colour pixels with the destination DC's background. */
#define PC_INDEX(pc) ((BYTE)(pc) & 15)
#define PC_MONO(pc) (((WORD)(pc) >> 8) & 1)
#define MKPC(idx, mono) ((DWORD)((idx) & 15) | ((DWORD)((mono) & 1) << 8))

/* a physical brush */
typedef struct {
    WORD style;                         /* BS_* */
    WORD flags;
    BYTE color[4][8];                   /* per plane, rows 0-7, aligned to x/y 0 */
    BYTE mono[8];                       /* the pattern on a mono bitmap */
    BYTE mask[8];                       /* hatched: 1 = foreground bit */
    BYTE fg, bg;                        /* hatched: colour indices */
    BYTE fgmono, bgmono;
} PBRUSH;
typedef PBRUSH __far *LPPBRUSH;
#define PBF_MONOPAT 1                   /* pattern from a mono bitmap: the
                                           DRAWMODE's colours (0 text, 1 bk) */
#define PBF_HATCH   2
#define PBF_HOLLOW  4

/* a physical pen */
typedef struct {
    WORD style;                         /* LS_* */
    BYTE color, mono;                   /* colour index, mono value */
} PPEN;
typedef PPEN __far *LPPPEN;

/* the PDEVICE: pdType nonzero (0 is a PBITMAP); the rest is w16's */
typedef struct {
    short pdType;
    short width, height;
    WORD magic;
} PDEV;
typedef PDEV __far *LPPDEV;
#define PDEV_TYPE 0x2000
#define PDEV_MAGIC 0x5657               /* 'VW' */

/* object styles (RealizeObject, EnumObj; DDK) */
#define OBJ_PEN 1
#define OBJ_BRUSH 2
#define OBJ_FONT 3
#define OBJ_PBITMAP 5
#define BS_SOLID 0
#define BS_HOLLOW 1
#define BS_HATCHED 2
#define BS_PATTERN 3
#define LS_SOLID 0
#define LS_DASHED 1
#define LS_DOTTED 2
#define LS_DOTDASHED 3
#define LS_DASHDOTDOT 4
#define LS_NOLINE 5
#define LS_INSIDEFRAME 6

#define TRANSPARENT 1
#define OPAQUE 2

#define OS_ARC 3
#define OS_SCANLINES 4
#define OS_RECTANGLE 6
#define OS_ELLIPSE 7
#define OS_POLYLINE 18
#define OS_WINDPOLYGON 20
#define OS_ALTPOLYGON 22
#define OS_PIE 23
#define OS_CHORD 39
#define OS_CIRCLE 55
#define OS_ROUNDRECT 72
#define OS_BEGINNSCAN 80
#define OS_ENDNSCAN 81

#define ETO_OPAQUE 2
#define ETO_CLIPPED 4

/* ---- a drawing surface: the screen or a memory bitmap ---- */
typedef struct {
    BYTE planes;                        /* 1 or 4 */
    BYTE screen;                        /* the VGA's memory */
    short width, height;
    WORD wbytes;                        /* bytes in one plane's scan line */
    WORD sel, off;                      /* the bits */
    WORD scanseg;                       /* lines per 64K segment (0: one segment) */
    WORD fill;                          /* bytes left at a segment's end */
} SURF;

/* globals (vga.c, entry.asm) */
extern WORD vga_sel;                    /* __A000H */
extern WORD ahincr;                     /* __AHINCR */
extern WORD winflags;                   /* __WINFLAGS */
extern WORD hInst;
extern GDIINFO gdiinfo;
extern BYTE enabled;
extern volatile BYTE busy;              /* drawing on the screen: no cursor */
extern BYTE repaint_off;

/* entry.asm */
void __cdecl __far int10(WORD ax, WORD bx, WORD cx, WORD dx);
void __cdecl __far memcpyf(void __far *d, const void __far *s, WORD n);
void __cdecl __far memsetf(void __far *d, BYTE v, WORD n);
void __cdecl __far outw(WORD port, WORD v);
void __cdecl __far outb(WORD port, BYTE v);
BYTE __cdecl __far inb(WORD port);

/* surf.c */
int surf_get(SURF *s, LPVOID dev);
BYTE __far *surf_line(SURF *s, int y, int plane);
void scr_plane(int plane);              /* map mask and read map */
void scr_reset(void);                   /* the VGA's registers as drawing expects */
void scr_begin(int x0, int y0, int x1, int y1);
void scr_end(void);
int clip_rect(RECT *r, LPRECT clip, SURF *s);

/* rop.c */
BYTE rop3_apply(BYTE rop, BYTE p, BYTE s, BYTE d);
BYTE rop2_to_rop3(int rop2);
void rop_run(BYTE rop, BYTE __far *d, const BYTE *src, BYTE pat, WORD n, BYTE lmask, BYTE rmask);

/* color.c */
DWORD rgb_to_pc(COLORREF c);
COLORREF index_rgb(int i);
int rgb_index(COLORREF c);
int rgb_mono(COLORREF c);
void dither(COLORREF c, BYTE pat[4][8], BYTE mono[8]);
extern const BYTE dib_to_index[16], index_to_dib[16];

/* blt.c */
void pattern_planes(LPPBRUSH b, LPDRAWMODE dm, int mono, BYTE pat[4][8], BYTE *mask, BYTE *transp);
void fill_rect(SURF *s, int x0, int y0, int x1, int y1, BYTE rop, LPPBRUSH b, LPDRAWMODE dm);
void fill_solid(SURF *s, int x0, int y0, int x1, int y1, BYTE rop, int idx, int mono);
void row_op(SURF *D, int y, int x0, int x1, BYTE rop, BYTE pat[4][8], int smask);

/* cursor (cursor.c, entry.asm) */
void cursor_exclude(int x0, int y0, int x1, int y1);
void cursor_unexclude(void);
void cursor_init(void);
void cursor_off(void);

/* save screen (misc.c) */
void save_discard(void);

#endif
