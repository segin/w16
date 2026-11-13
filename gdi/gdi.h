/*
 * gdi.h - w16's GDI.EXE (ARCHITECTURE.md 11).
 *
 * Built large model, SS != DS (-zu): every exported function loads GDI's
 * own DS (__loadds), where its local heap holds the GDI objects; the
 * caller's stack is elsewhere, so pointers are far by default. Objects
 * are reached through their local handles (near).
 *
 * Sources: the Windows 3.1 SDK reference (API semantics), the Windows 3.1
 * DDK documentation, Device Driver Adaptation Guide (the GDI <-> driver
 * interface: GDIINFO, DRAWMODE, PBITMAP, FONTINFO, TEXTXFORM, the driver
 * functions), Schulman et al., Undocumented Windows (1992) ch. 8 (object
 * header and signatures, the undocumented exports; facts only, each one
 * confirmed by observation with GD1/GD2, tests/gdi), and black-box
 * observation of Windows 3.1 (the GD1/GD2 logs).
 */
#ifndef W16_GDI_H
#define W16_GDI_H

typedef unsigned char BYTE;
typedef unsigned short WORD;
typedef unsigned long DWORD;
typedef short SHORT;
typedef long LONG;
typedef int BOOL;
typedef WORD HANDLE;
typedef WORD HDC, HPEN, HBRUSH, HFONT, HBITMAP, HRGN, HPALETTE, HGDIOBJ;
typedef WORD HGLOBAL, HLOCAL, HINSTANCE, HMODULE, HTASK, ATOM, HRSRC;
typedef DWORD COLORREF;
typedef void __far *LPVOID;
typedef char __far *LPSTR;
typedef const char __far *LPCSTR;
typedef int (__far __pascal *FARPROC)();

#define FALSE 0
#define TRUE 1
#define NULL 0
#define NEAR __near
#define FAR __far

/* exported API: far Pascal, loads GDI's DS */
#define GDIAPI __far __pascal __loadds
/* functions in other modules (KERNEL) */
#define WINAPI __far __pascal

#define LOWORD(l) ((WORD)(DWORD)(l))
#define HIWORD(l) ((WORD)((DWORD)(l) >> 16))
#define MAKELONG(lo, hi) ((DWORD)(((WORD)(lo)) | ((DWORD)((WORD)(hi))) << 16))
/* a far pointer from selector and offset (util.c; a cast of a long with a
   constant zero high word became DS:offset with OpenWatcom) */
void __far *mk_fp(WORD sel, WORD off);
#define MAKELP(sel, off) mk_fp(sel, off)
#define MAKEINTRESOURCE(i) ((LPCSTR)MAKELP(0, i))
#define SELECTOROF(p) ((WORD)((DWORD)(p) >> 16))
#define OFFSETOF(p) ((WORD)(DWORD)(p))
#define RGB(r, g, b) ((COLORREF)(((BYTE)(r) | ((WORD)(g) << 8)) | (((DWORD)(BYTE)(b)) << 16)))

typedef struct { int x, y; } POINT;
typedef POINT __far *LPPOINT;
typedef struct { int left, top, right, bottom; } RECT;
typedef RECT __far *LPRECT;
typedef RECT NEAR *NPRECT;

/* ---- KERNEL (imports) ---- */
#define LMEM_FIXED 0x0000
#define LMEM_MOVEABLE 0x0002
#define LMEM_ZEROINIT 0x0040
#define GMEM_FIXED 0x0000
#define GMEM_MOVEABLE 0x0002
#define GMEM_ZEROINIT 0x0040
#define GMEM_DISCARDABLE 0x0100
#define GMEM_SHARE 0x2000
#define GMEM_DDESHARE 0x2000
HLOCAL WINAPI LocalAlloc(WORD, WORD);
HLOCAL WINAPI LocalReAlloc(HLOCAL, WORD, WORD);
HLOCAL WINAPI LocalFree(HLOCAL);
WORD WINAPI LocalSize(HLOCAL);
char NEAR * WINAPI LocalLock(HLOCAL);
BOOL WINAPI LocalUnlock(HLOCAL);
BOOL WINAPI LocalInit(WORD, WORD, WORD);
WORD WINAPI LocalCompact(WORD);
HGLOBAL WINAPI GlobalAlloc(WORD, DWORD);
HGLOBAL WINAPI GlobalReAlloc(HGLOBAL, DWORD, WORD);
HGLOBAL WINAPI GlobalFree(HGLOBAL);
LPVOID WINAPI GlobalLock(HGLOBAL);
BOOL WINAPI GlobalUnlock(HGLOBAL);
DWORD WINAPI GlobalSize(HGLOBAL);
DWORD WINAPI GlobalHandle(WORD);
WORD WINAPI GlobalFlags(HGLOBAL);
HMODULE WINAPI GetModuleHandle(LPCSTR);
HINSTANCE WINAPI LoadLibrary(LPCSTR);
void WINAPI FreeLibrary(HINSTANCE);
FARPROC WINAPI GetProcAddress(HINSTANCE, LPCSTR);
HRSRC WINAPI FindResource(HINSTANCE, LPCSTR, LPCSTR);
HGLOBAL WINAPI LoadResource(HINSTANCE, HRSRC);
LPVOID WINAPI LockResource(HGLOBAL);
BOOL WINAPI FreeResource(HGLOBAL);
WORD WINAPI SizeofResource(HINSTANCE, HRSRC);
int WINAPI GetProfileString(LPCSTR, LPCSTR, LPCSTR, LPSTR, int);
int WINAPI GetPrivateProfileString(LPCSTR, LPCSTR, LPCSTR, LPSTR, int, LPCSTR);
WORD WINAPI GetProfileInt(LPCSTR, LPCSTR, int);
WORD WINAPI GetPrivateProfileInt(LPCSTR, LPCSTR, int, LPCSTR);
int WINAPI lstrlen(LPCSTR);
LPSTR WINAPI lstrcpy(LPSTR, LPCSTR);
LPSTR WINAPI lstrcat(LPSTR, LPCSTR);
HTASK WINAPI GetCurrentTask(void);
void WINAPI FatalExit(int);
WORD WINAPI GetExeVersion(void);
DWORD WINAPI GetWinFlags(void);
BOOL WINAPI IsBadReadPtr(const void __far *, WORD);
BOOL WINAPI IsBadWritePtr(void __far *, WORD);
WORD WINAPI AllocSelector(WORD);
WORD WINAPI FreeSelector(WORD);
WORD WINAPI SetErrorMode(WORD);
int WINAPI OpenFile(LPCSTR, void __far *, WORD);
int WINAPI _lclose(int);
WORD WINAPI _lread(int, void __far *, WORD);

/* ---- the display driver interface (DDK) ---- */

/* GDIINFO: DDK, Device Driver Adaptation Guide; 0x6E bytes (Enable with
   bit 0 of the style returned 6Eh on 3.1's VGA.DRV, GD1) */
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

/* DRAWMODE (DDK), passed to most output functions */
typedef struct {
    short Rop2, bkMode;
    DWORD bkColor, TextColor;          /* physical */
    short TBreakExtra, BreakExtra, BreakErr, BreakRem, BreakCount, CharExtra;
    COLORREF LbkColor, LTextColor;     /* logical */
} DRAWMODE;

/* PBITMAP (DDK): a memory bitmap as the driver sees it */
typedef struct {
    short bmType, bmWidth, bmHeight, bmWidthBytes;
    BYTE bmPlanes, bmBitsPixel;
    LPVOID bmBits;
    DWORD bmWidthPlanes;
    LPVOID bmlpPDevice;
    short bmSegmentIndex, bmScanSegment, bmFillBytes, reserved1, reserved2;
} PBITMAP;
typedef PBITMAP __far *LPPBITMAP;

/* TEXTXFORM (DDK) */
typedef struct {
    short txfHeight, txfWidth, txfEscapement, txfOrientation, txfWeight;
    char txfItalic, txfUnderline, txfStrikeOut, txfOutPrecision, txfClipPrecision;
    short txfAccelerator, txfOverhang;
} TEXTXFORM;

/* FONTINFO (DDK) = a .FNT file from dfType on */
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
    LPVOID dfBitsPointer;
    DWORD dfBitsOffset;
    BYTE dfReserved;
} FONTINFO;
#pragma pack(push, 1)
typedef struct { short dfType, dfPoints, dfVertRes, dfHorizRes, dfAscent;
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
    LPVOID dfBitsPointer;
    DWORD dfBitsOffset;
    BYTE dfReserved;
} FONTINFOP;
#pragma pack(pop)
typedef FONTINFOP __far *LPFONTINFO;

/* the driver's entry points, by ordinal (DDK; VGA.DRV's export table) */
enum {
    DRV_BITBLT = 1, DRV_COLORINFO = 2, DRV_CONTROL = 3, DRV_DISABLE = 4, DRV_ENABLE = 5,
    DRV_ENUMDFONTS = 6, DRV_ENUMOBJ = 7, DRV_OUTPUT = 8, DRV_PIXEL = 9,
    DRV_REALIZEOBJECT = 10, DRV_STRBLT = 11, DRV_SCANLR = 12, DRV_DEVICEMODE = 13,
    DRV_EXTTEXTOUT = 14, DRV_GETCHARWIDTH = 15, DRV_DEVICEBITMAP = 16, DRV_FASTBORDER = 17,
    DRV_SETATTRIBUTE = 18, DRV_DEVICEBITMAPBITS = 19, DRV_CREATEBITMAP = 20,
    DRV_SETDIBITSTODEVICE = 21, DRV_SETPALETTE = 22, DRV_GETPALETTE = 23,
    DRV_SETPALTRANS = 24, DRV_GETPALTRANS = 25, DRV_UPDATECOLORS = 26,
    DRV_STRETCHBLT = 27, DRV_STRETCHDIBITS = 28, DRV_SELECTBITMAP = 29,
    DRV_BITMAPBITS = 30, DRV_NENTRIES = 31
};

typedef WORD (__far __pascal *DRVBITBLT)(LPVOID, WORD, WORD, LPVOID, WORD, WORD, WORD, WORD,
                                         DWORD, LPVOID, DRAWMODE __far *);
typedef DWORD (__far __pascal *DRVCOLORINFO)(LPVOID, DWORD, DWORD __far *);
typedef WORD (__far __pascal *DRVCONTROL)(LPVOID, WORD, LPVOID, LPVOID);
typedef WORD (__far __pascal *DRVDISABLE)(LPVOID);
typedef WORD (__far __pascal *DRVENABLE)(LPVOID, WORD, LPCSTR, LPCSTR, LPVOID);
typedef WORD (__far __pascal *DRVOUTPUT)(LPVOID, WORD, WORD, LPPOINT, LPVOID, LPVOID,
                                         DRAWMODE __far *, LPRECT);
typedef DWORD (__far __pascal *DRVPIXEL)(LPVOID, WORD, WORD, DWORD, DRAWMODE __far *);
typedef DWORD (__far __pascal *DRVREALIZE)(LPVOID, WORD, LPVOID, LPVOID, LPVOID);
typedef WORD (__far __pascal *DRVSCANLR)(LPVOID, WORD, WORD, DWORD, WORD);
typedef DWORD (__far __pascal *DRVEXTTEXTOUT)(LPVOID, WORD, WORD, LPRECT, LPCSTR, int,
                                              LPFONTINFO, DRAWMODE __far *, TEXTXFORM __far *,
                                              short __far *, LPRECT, WORD);
typedef WORD (__far __pascal *DRVGETCHARWIDTH)(LPVOID, short __far *, WORD, WORD, LPFONTINFO,
                                               DRAWMODE __far *, TEXTXFORM __far *);
typedef WORD (__far __pascal *DRVFASTBORDER)(LPRECT, WORD, WORD, DWORD, LPVOID, LPVOID,
                                             DRAWMODE __far *, LPRECT);
typedef WORD (__far __pascal *DRVDEVBMBITS)(LPVOID, WORD, WORD, WORD, LPVOID, LPVOID,
                                            DRAWMODE __far *, LPVOID);
typedef WORD (__far __pascal *DRVSETDIBTODEV)(LPVOID, WORD, WORD, WORD, WORD, LPRECT,
                                              DRAWMODE __far *, LPVOID, LPVOID, LPVOID);
typedef WORD (__far __pascal *DRVENUMOBJ)(LPVOID, WORD, FARPROC, LPVOID);
typedef WORD (__far __pascal *DRVENUMDFONTS)(LPVOID, LPCSTR, FARPROC, LPVOID);

/* RealizeObject styles (DDK) */
#define OBJ_PEN 1
#define OBJ_BRUSH 2
#define OBJ_FONT 3
#define OBJ_PBITMAP 5

/* Output styles (DDK) */
#define OS_ARC 3
#define OS_SCANLINES 4
#define OS_RECTANGLE 6
#define OS_ELLIPSE 7
#define OS_POLYLINE 18
#define OS_ALTPOLYGON 22
#define OS_WINDPOLYGON 20
#define OS_PIE 23
#define OS_CHORD 39
#define OS_CIRCLE 55
#define OS_ROUNDRECT 72
#define OS_BEGINNSCAN 80
#define OS_ENDNSCAN 81

/* a device GDI drives: the display, or a printer driver */
typedef struct DEVICE {
    struct DEVICE NEAR *next;
    HINSTANCE hDriver;
    WORD refs;
    char name[16];                      /* module name, e.g. DISPLAY */
    FARPROC fn[DRV_NENTRIES];
    GDIINFO info;
    HGLOBAL hPDevice;
    LPVOID lpPDevice;
    WORD pbrushSize, ppenSize;          /* sizes RealizeObject asked for */
} DEVICE;
typedef DEVICE NEAR *NPDEVICE;

/* ---- GDI's own heaps (heap.c; ARCHITECTURE.md 11.7) ----

   Data programs never reach through a handle (region rectangles,
   realized pens and brushes, the font table, saved DC states, palette
   entries, temporary buffers; with ExtendedGdiHeap, object bodies) is kept
   in local heaps of GDI's own, each in a moveable global segment (GMEM_SHARE,
   owned by GDI) that grows to at most FFF0h bytes, as many segments as are
   needed. A block there is an XP: the heap's number (1..) in the high word
   and the block's offset (LMEM_FIXED: the handle is the address) in the
   low word. xptr() makes the far pointer from the heap's selector, which
   is taken again (GlobalHandle) after every allocation in that heap. A
   fixed block never moves within its heap and a heap's segment keeps its
   selector while it stays under 64K, but a far pointer is still not kept
   across a call that can allocate in GDI's heaps or call out (driver
   calls that may call GDI, DC hooks, enumeration callbacks): it is made
   again from its handle afterwards. Nothing here is used at interrupt
   time. */
typedef DWORD XP;
void xheap_init(void);
XP xalloc(WORD size);                   /* zeroed; 0: no room */
XP xrealloc(XP p, WORD size);           /* the block, perhaps elsewhere; 0: no room (p kept) */
void xfree(XP p);
WORD xsize(XP p);
void __far *xptr(XP p);
DWORD xheap_stats(void);                /* heaps (high word), KB in use (low) */
/* entry.asm: KERNEL's Local* on the heap in segment sel (DS switched) */
WORD __far __cdecl lh_alloc(WORD sel, WORD flags, WORD size);
WORD __far __cdecl lh_realloc(WORD sel, WORD h, WORD size, WORD flags);
WORD __far __cdecl lh_free(WORD sel, WORD h);
WORD __far __cdecl lh_size(WORD sel, WORD h);

/* [w16] ExtendedGdiHeap=1 in SYSTEM.INI (read at GDI's start-up; default
   0): each object's block in GDI's DGROUP is only a 4-byte stub, fixed
   (the handle is its address): an index into GDI's object table, then the
   signature at offset 2, where TOOLHELP and IsGDIObject read it. The
   object itself (header and body) is in GDI's own heaps, its header's
   wMetaList naming the stub (a handle is valid when its block, its
   table entry and the body agree). Handles stay
   offsets of blocks in GDI's DGROUP, but are no longer moveable-block
   handles (3.1's are, 4 apart), and programs that read GDI's objects
   through their handles directly (a DC's fields, a pen's LOGPEN after the
   header, the header's hNext or sequence count) see the stub and can
   break. Off: the objects are complete in DGROUP, as 3.1's. */
extern WORD gdi_extended;

/* ---- GDI objects ---- */

#pragma pack(push, 1)

/* object header (Schulman ch. 8; 3.1 retail): hNext, signature, a
   sequence count, a metafile list; 0Ah bytes */
typedef struct {
    HANDLE hNext;
    WORD wMagic;
    DWORD dwCount;
    WORD wMetaList;
} OBJHDR;
typedef OBJHDR __far *LPOBJHDR;

/* ExtendedGdiHeap's block in DGROUP, fixed (the handle is its address):
   the object's entry in GDI's object table (object.c), whose XP is the
   body (the whole object, header included), then the signature */
typedef struct {
    WORD idx;
    WORD wMagic;
} OBJSTUB;

/* signatures, 3.1 (Schulman ch. 8): 'GO' + type - 1; flag 2000h = private
   (MakeObjectPrivate), 8000h = selected (UNVERIFIED) */
#define MAGIC_BASE 0x4F46
#define PEN_MAGIC 0x4F47
#define BRUSH_MAGIC 0x4F48
#define FONT_MAGIC 0x4F49
#define PAL_MAGIC 0x4F4A
#define BITMAP_MAGIC 0x4F4B
#define RGN_MAGIC 0x4F4C
#define DC_MAGIC 0x4F4D
#define DISDC_MAGIC 0x4F4E
#define METADC_MAGIC 0x4F4F
#define META_MAGIC 0x4F50
#define METAFILEDC_MAGIC 0x4F51
#define MAGIC_MASK 0x5FFF
#define OBJ_PRIVATE 0x2000

typedef struct { WORD lopnStyle; POINT lopnWidth; COLORREF lopnColor; } LOGPEN;
typedef struct { WORD lbStyle; COLORREF lbColor; short lbHatch; } LOGBRUSH;
#define LF_FACESIZE 32
typedef struct {
    short lfHeight, lfWidth, lfEscapement, lfOrientation, lfWeight;
    BYTE lfItalic, lfUnderline, lfStrikeOut, lfCharSet, lfOutPrecision;
    BYTE lfClipPrecision, lfQuality, lfPitchAndFamily;
    char lfFaceName[LF_FACESIZE];
} LOGFONT;
#define LF_FIXEDPART 18                 /* LOGFONT before the face name */
typedef struct {
    short bmType, bmWidth, bmHeight, bmWidthBytes;
    BYTE bmPlanes, bmBitsPixel;
    LPVOID bmBits;
} BITMAP;
typedef struct { BYTE peRed, peGreen, peBlue, peFlags; } PALETTEENTRY;
typedef struct {
    short tmHeight, tmAscent, tmDescent, tmInternalLeading, tmExternalLeading;
    short tmAveCharWidth, tmMaxCharWidth, tmWeight;
    BYTE tmItalic, tmUnderlined, tmStruckOut, tmFirstChar, tmLastChar;
    BYTE tmDefaultChar, tmBreakChar, tmPitchAndFamily, tmCharSet;
    short tmOverhang, tmDigitizedAspectX, tmDigitizedAspectY;
} TEXTMETRIC;

/* The objects as Schulman ch. 8 gives 3.1's: the header, then the
   documented logical structure. 3.1's blocks as TOOLHELP showed them
   (GD4): pen 1Ah, brush 1Ah, font 2Ah (3Eh with a 31-character face),
   palette 1Ah (its entries elsewhere), bitmap 2Ah, rectangle region 2Ah,
   DC CEh. */
typedef struct {                        /* 14h */
    OBJHDR hdr;
    LOGPEN lp;
} PENOBJ;
typedef PENOBJ __far *LPPENOBJ;

typedef struct {                        /* 16h */
    OBJHDR hdr;
    LOGBRUSH lb;                        /* BS_PATTERN: lbHatch = the program's bitmap (GD4) */
    HGLOBAL hPatBits;                   /* Schulman's crHatchBk: w16 keeps a BS_PATTERN */
    WORD unused;                        /* brush's copy of the bits here (PBITMAP + bits) */
} BRUSHOBJ;
typedef BRUSHOBJ __far *LPBRUSHOBJ;

/* the header, then the LOGFONT up to its face name's end, at least 0Eh
   bytes of it (GD4: 3.1's block was 2Ah for no face and for "MS Sans
   Serif", 3Eh for 31 characters); GetObject gives the whole LOGFONT */
typedef struct {
    OBJHDR hdr;
    LOGFONT lf;
} FONTOBJ;
typedef FONTOBJ __far *LPFONTOBJ;
#define FONT_MINFACE 14

typedef struct {                        /* 1Ah */
    OBJHDR hdr;
    HGLOBAL hBits;                      /* 0Ah hmemBitmap: PBITMAP header (+ bits) */
    WORD bSelected;                     /* 0Ch */
    HDC hdcSel;                         /* 0Eh the DC it is selected into */
    HGLOBAL hHuge;                      /* the bits, when over 64K */
    WORD sel;                           /* hBits' selector */
    WORD flags;
    WORD sizeX, sizeY;                  /* SetBitmapDimension */
} BITMAPOBJ;
typedef BITMAPOBJ __far *LPBITMAPOBJ;
#define BMF_STOCK 1
#define BMF_DISCARDABLE 2

typedef struct {                        /* 12h */
    OBJHDR hdr;
    WORD palVersion;                    /* 0Ah: LOGPALETTE's (Schulman ch. 8), 300h */
    WORD nEntries;                      /* 0Ch palNumEntries */
    XP xEntries;                        /* palPalEntry: in GDI's heaps, not here */
} PALOBJ;
typedef PALOBJ __far *LPPALOBJ;

/* regions: y-x banded rectangles (no overlap; within a band the same top
   and bottom, sorted by left; bands sorted by top). The object is the
   header and the XP of the rectangles, which are in GDI's heaps. */
typedef struct {                        /* 0Eh */
    OBJHDR hdr;
    XP xData;
} RGNOBJ;
typedef RGNOBJ __far *LPRGNOBJ;
typedef struct {
    WORD n;                             /* rectangles */
    WORD cap;
    RECT box;
    RECT r[1];
} RGNDATA;
typedef RGNDATA __far *LPRGN;
#define RGNDATA_SIZE(n) (sizeof(RGNDATA) + ((n) ? (n) - 1 : 0) * sizeof(RECT))

/* the device context: CEh bytes, every field Schulman ch. 8 names for 3.1
   retail at its offset; w16 keeps its own state in the fields whose use
   there is not known (marked "w16") */
typedef struct DC {
    OBJHDR hdr;                         /* 00 hNext: the SaveDC stack's top is xSaved */
    WORD flags;                         /* 0A byFlags, byFlags2 (w16: DCF_*) */
    HANDLE hMetaFile;                   /* 0C */
    HRGN hClipRgn;                      /* 0E hrgnClip (device coordinates) */
    HDC hLink;                          /* 10 hPDevice (w16: next DC in GDI's list) */
    HPEN hPen;                          /* 12 hLPen */
    HBRUSH hBrush;                      /* 14 hLBrush */
    HFONT hFont;                        /* 16 hLFont */
    HBITMAP hBitmap;                    /* 18 */
    HPALETTE hPal;                      /* 1A dchPal */
    struct DEVICE NEAR *dev;            /* 1C hLDevice (the driver's entry points) */
    HRGN hRao;                          /* 1E hRaoClip: vis & clip, screen coordinates */
    WORD raoValid;                      /* 20 hPDeviceBlock (w16) */
    HPEN hPPenOf;                       /* 22 hPPen (w16: the pen xPPen is of) */
    HBRUSH hPBrushOf;                   /* 24 hPBrush (w16) */
    HFONT hFontOf;                      /* 26 hPFontTrans (w16) */
    WORD hookFlags;                     /* 28 hPFont (w16) */
    LPVOID lpPDevice;                   /* 2A PDEVICE, or the selected PBITMAP */
    XP xPPen;                           /* 2E pLDevice, pRaoClip (w16: realized pen) */
    XP xPBrush;                         /* 32 pPDeviceBlock, pPPen (w16) */
    XP xFont;                           /* 36 pPBrush, pPFontTrans (w16: FONTENT) */
    LPVOID lpPDevOfBrush;               /* 3A lpPFont (w16) */
    WORD boundsFlags;                   /* 3E nPFTIndex (w16) */
    LPVOID lpPDevOfFont;                /* 40 Transform (w16) */
    DRAWMODE dm;                        /* 44 */
    POINT cur;                          /* 64 LCursPos */
    POINT wndOrg, wndExt, vpOrg, vpExt; /* 68 6C 70 74 */
    XP xSaved;                          /* 78 UserVptOrg (w16: SaveDC stack, GDI's heaps) */
    WORD mapMode;                       /* 7C */
    short charExtra;                    /* 7E wXFormFlags (w16: logical extra) */
    WORD relAbs, polyFill, stretchMode; /* 80 82 84 */
    BYTE planes, bitsPixel;             /* 86 */
    WORD w88, w8A;                      /* 88 wPenWidth, wPenHeight */
    WORD textAlign;                     /* 8C */
    DWORD mapperFlags;                  /* 8E */
    POINT brushOrg;                     /* 92 */
    HTASK owner;                        /* 96 wFontAspectX (w16) */
    WORD w98, w9A;                      /* 98 wFontAspectY, hFontWeights */
    WORD saveLevel;                     /* 9C wDCSaveLevel */
    WORD w9E;                           /* 9E wcDCLocks */
    HRGN hVisRgn;                       /* A0 screen coordinates (USER's) */
    POINT dcOrg;                        /* A2 SetDCOrg (screen) */
    BYTE wA6[14];                       /* A6 lpfnPrint, atoms, PostScale */
    RECT bounds;                        /* B4 rectBounds */
    RECT rectLVB;                       /* BC */
    FARPROC hook;                       /* C4 lpfnNotify */
    DWORD hookData;                     /* C8 lpHookData */
    WORD wDCGlobFlags;                  /* CC */
} DC;                                   /* CE */
typedef DC __far *LPDC;
#define DCF_DISPLAY 0x0001
#define DCF_MEMORY 0x0002
#define DCF_IC 0x0004
#define DCF_STATE 0x0010                /* GetDCState copy */
typedef char dc_size_check[sizeof(DC) == 0xCE ? 1 : -1];   /* Schulman: CEh */

/* a saved state (SaveDC), in GDI's heaps */
typedef struct {
    DC dc;                              /* xSaved: the next one down */
    XP xClip;                           /* the clip region's rectangles (RGNDATA), 0: none */
} SAVEDDC;

/* a realized pen or brush is the driver's block as it asked for it */

#pragma pack(pop)

/* deref a moveable local handle in DGROUP: the handle table entry's first
   word is the block's address (KERNEL local heap; Pietrek ch. 2) */
#define DEREF(h) ((void NEAR *)*(WORD NEAR *)(h))

/* object.c */
HANDLE obj_alloc(WORD magic, WORD size);
void __far *obj_get(HANDLE h, WORD magic);  /* the whole object, or 0 */
WORD obj_magic(HANDLE h);               /* the signature with its flags, 0: none */
void obj_set_magic(HANDLE h, WORD m);
int obj_type(HANDLE h);
void obj_free(HANDLE h);
extern HANDLE stock[20];
extern DWORD obj_seq;
#define STOCK_COUNT 18
LPDC dc_get(HDC h);

/* dc.c */
void dc_rao_invalidate(LPDC dc);
LPRGN dc_rao(LPDC dc);
void dc_lp_to_dp(LPDC dc, int __far *pts, int n);
void dc_dp_to_lp(LPDC dc, int __far *pts, int n);
int dc_scale_x(LPDC dc, int x);
int dc_scale_y(LPDC dc, int y);
void dc_set_device_colors(LPDC dc);
LPVOID dc_pbrush(LPDC dc);
LPVOID dc_ppen(LPDC dc);
void dc_update_bounds(LPDC dc, int l, int t, int r, int b);
void dc_brush_changed(HBRUSH h);
HDC dc_first(void);

/* driver.c */

NPDEVICE dev_open(LPCSTR name);
void dev_close(NPDEVICE d);
extern NPDEVICE display;
DWORD dev_colorinfo(LPDC dc, COLORREF c);
#define DRVFN(d, n) ((d)->fn[n])

/* rgn.c */
HRGN rgn_new(int l, int t, int r, int b);
void rgn_set_rect(HRGN h, int l, int t, int r, int b);
int rgn_combine(HRGN dst, HRGN a, HRGN b, int mode);
int rgn_op_rect(HRGN h, int l, int t, int r, int b, int mode);
int rgn_complexity(LPRGN r);
int rgn_copy(HRGN dst, HRGN src);
void rgn_offset(HRGN h, int dx, int dy);
LPRGN rgn_ptr(HRGN h);
XP rgn_dup_data(HRGN h);                /* a copy of the rectangles, 0 for none */
int rgn_set_data(HRGN h, XP x);         /* take the rectangles (x is then the region's) */
void rgn_free(HRGN h);
#define NULLREGION 1
#define SIMPLEREGION 2
#define COMPLEXREGION 3
#define ERROR 0
#define RGN_AND 1
#define RGN_OR 2
#define RGN_XOR 3
#define RGN_DIFF 4
#define RGN_COPY 5

/* bitmap.c */
HBITMAP bm_create(int w, int h, int planes, int bpp, const void __far *bits, WORD flags);
LPPBITMAP bm_pbitmap(HBITMAP h);
HGLOBAL bm_pattern_copy(HBITMAP h);
BYTE __far *bm_scan(LPPBITMAP pb, int y);
void bm_free(HBITMAP h);
LPDC dc_out(HDC hdc);
int dc_uses(HANDLE h);
HPALETTE pal_default(void);
void pal_free(HPALETTE h);
void stock_init(void);
extern HBITMAP stock_bitmap;

/* font.c: the font table, in GDI's heaps */
typedef struct FONTENT {
    XP next;
    HINSTANCE hMod;                     /* the .FON module */
    WORD resId;
    HGLOBAL hRes;
    HGLOBAL hMem;                       /* a 2.x font made 3.x */
    LPFONTINFO fi;                      /* locked, dfBitsPointer set */
    char face[LF_FACESIZE];
    short height, avgWidth, weight;
    BYTE italic, underline, strikeout, charset, pitch;
    BYTE fromFile;                      /* added by AddFontResource */
    char file[13];
    short points;
    short type, il;
} FONTENT;
typedef FONTENT __far *LPFONT;
void font_init(void);
LPFONT font_realize(LPDC dc);
LPFONT dc_font(LPDC dc);                /* the DC's realized font (after font_realize) */
void font_xform(LPDC dc, LPFONT f, TEXTXFORM __far *tx);
extern XP fonts;

/* API functions used inside GDI too */
BOOL GDIAPI PtInRegion(HRGN, int, int);
BOOL GDIAPI RectInRegion(HRGN, const RECT __far *);
HGDIOBJ GDIAPI SelectObject(HDC, HGDIOBJ);
int GDIAPI SelectClipRgn(HDC, HRGN);
BOOL GDIAPI PatBlt(HDC, int, int, int, int, DWORD);
BOOL GDIAPI BitBlt(HDC, int, int, int, int, HDC, int, int, DWORD);
int GDIAPI SetDIBits(HDC, HBITMAP, WORD, WORD, const void __far *, void __far *, WORD);
HBITMAP GDIAPI CreateCompatibleBitmap(HDC, int, int);
BOOL GDIAPI DeleteObject(HGDIOBJ);
HDC GDIAPI CreateCompatibleDC(HDC);
BOOL GDIAPI DeleteDC(HDC);
HGDIOBJ GDIAPI GetStockObject(int);

/* util */
void gdi_log(const char __far *s);
void gdi_loghex(const char __far *what, WORD v);
void mem_copy(void __far *d, const void __far *s, WORD n);
void mem_set(void __far *d, BYTE v, WORD n);
int str_icmp(const char __far *a, const char __far *b);
int mul_div(int a, int b, int c);
extern HINSTANCE gdi_inst;

#define max(a, b) ((a) > (b) ? (a) : (b))
#define min(a, b) ((a) < (b) ? (a) : (b))

#endif
