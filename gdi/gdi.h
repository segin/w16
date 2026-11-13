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

/* ---- GDI objects in the local heap ---- */

/* object header (Schulman ch. 8; 3.1 retail): hNext, signature, a
   sequence count, a metafile list */
typedef struct {
    HANDLE hNext;
    WORD wMagic;
    DWORD dwCount;
    WORD wMetaList;
} OBJHDR;

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
#define OBJ_STOCK_FLAG 0x0001      /* in our own flags word */

typedef struct { WORD lopnStyle; POINT lopnWidth; COLORREF lopnColor; } LOGPEN;
typedef struct { WORD lbStyle; COLORREF lbColor; short lbHatch; } LOGBRUSH;
#define LF_FACESIZE 32
typedef struct {
    short lfHeight, lfWidth, lfEscapement, lfOrientation, lfWeight;
    BYTE lfItalic, lfUnderline, lfStrikeOut, lfCharSet, lfOutPrecision;
    BYTE lfClipPrecision, lfQuality, lfPitchAndFamily;
    char lfFaceName[LF_FACESIZE];
} LOGFONT;
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

typedef struct {
    OBJHDR hdr;
    LOGPEN lp;
    WORD gen;
} PENOBJ;

typedef struct {
    OBJHDR hdr;
    LOGBRUSH lb;
    COLORREF crHatchBk;
    HBITMAP hbmPattern;                 /* pattern brush: our own copy */
    WORD gen;                           /* bumped by UnrealizeObject */
} BRUSHOBJ;

struct FONTENT;
typedef struct {
    OBJHDR hdr;
    LOGFONT lf;
} FONTOBJ;

typedef struct {
    OBJHDR hdr;
    HGLOBAL hBits;                      /* PBITMAP header (+ bits) */
    HGLOBAL hHuge;                      /* the bits, when over 64K */
    WORD sel;                           /* hBits' selector */
    HDC hdcSel;                         /* DC it is selected into */
    WORD flags;
    short width, height;
    BYTE planes, bpp;
    WORD sizeX, sizeY;                  /* SetBitmapDimension */
} BITMAPOBJ;
#define BMF_STOCK 1
#define BMF_DISCARDABLE 2

typedef struct {
    OBJHDR hdr;
    WORD nEntries;
    PALETTEENTRY pe[1];
} PALOBJ;

/* regions: y-x banded rectangles (no overlap; within a band the same top
   and bottom, sorted by left; bands sorted by top) */
typedef struct {
    OBJHDR hdr;
    WORD n;                             /* rectangles */
    WORD cap;
    RECT box;
    RECT r[1];
} RGNOBJ;
typedef RGNOBJ NEAR *NPRGN;

/* a realized pen or brush, kept with the DC */
typedef struct {
    WORD size;
    BYTE data[1];
} PHYS;

/* the device context */
typedef struct DC {
    OBJHDR hdr;
    WORD flags;
    NPDEVICE dev;                       /* driver */
    LPVOID lpPDevice;                   /* PDEVICE, or the selected PBITMAP */
    HBITMAP hBitmap;                    /* memory DC: the selected bitmap */
    HPEN hPen;
    HBRUSH hBrush;
    HFONT hFont;
    HPALETTE hPal;
    HRGN hClipRgn;                      /* app's clip region (device coords) */
    HRGN hVisRgn;                       /* USER's visible region (screen) */
    HRGN hRao;                          /* vis & clip, screen coords */
    WORD raoValid;
    WORD saveLevel;
    DRAWMODE dm;
    /* realized objects */
    HPEN hPPenOf; WORD ppenGen; HLOCAL hPPen;
    HBRUSH hPBrushOf; WORD pbrushGen; POINT pbrushOrg; HLOCAL hPBrush;
    LPVOID lpPDevOfBrush;
    struct FONTENT NEAR *pfont;         /* realized font */
    TEXTXFORM xform;
    HFONT hFontOf;
    LPVOID lpPDevOfFont;
    POINT cur;                          /* current position (logical) */
    POINT wndOrg, wndExt, vpOrg, vpExt;
    WORD mapMode;
    WORD relAbs, polyFill, stretchMode, textAlign;
    POINT brushOrg;
    POINT dcOrg;                        /* SetDCOrg (screen) */
    FARPROC hook;
    DWORD hookData;
    WORD hookFlags;
    RECT bounds;
    WORD boundsFlags;
    short breakCount, breakExtra;
    short charExtra;
    WORD mapperFlags;
    HTASK owner;
    WORD isMemDC;
    WORD isIC;
} DC;
typedef DC NEAR *NPDC;
#define DCF_DISPLAY 0x0001
#define DCF_MEMORY 0x0002
#define DCF_IC 0x0004
#define DCF_SAVED 0x0008
#define DCF_STATE 0x0010                /* GetDCState copy */

/* deref a moveable local handle: the handle table entry's first word is
   the block's address (KERNEL local heap; Pietrek ch. 2) */
#define DEREF(h) ((void NEAR *)*(WORD NEAR *)(h))
#define POBJ(h) ((OBJHDR NEAR *)DEREF(h))

/* object.c */
HANDLE obj_alloc(WORD magic, WORD size);
void NEAR *obj_get(HANDLE h, WORD magic);
int obj_type(HANDLE h);
void obj_free(HANDLE h);
extern HANDLE stock[20];
extern DWORD obj_seq;
#define STOCK_COUNT 18
NPDC dc_get(HDC h);

/* dc.c */
void dc_rao_invalidate(NPDC dc);
NPRGN dc_rao(NPDC dc);
void dc_lp_to_dp(NPDC dc, int __far *pts, int n);
void dc_dp_to_lp(NPDC dc, int __far *pts, int n);
int dc_scale_x(NPDC dc, int x);
int dc_scale_y(NPDC dc, int y);
void dc_set_device_colors(NPDC dc);
LPVOID dc_pbrush(NPDC dc);
LPVOID dc_ppen(NPDC dc);
void dc_update_bounds(NPDC dc, int l, int t, int r, int b);

/* driver.c */
NPDEVICE dev_open(LPCSTR name);
void dev_close(NPDEVICE d);
extern NPDEVICE display;
DWORD dev_colorinfo(NPDC dc, COLORREF c);
#define DRVFN(d, n) ((d)->fn[n])

/* rgn.c */
HRGN rgn_new(int l, int t, int r, int b);
void rgn_set_rect(HRGN h, int l, int t, int r, int b);
int rgn_combine(HRGN dst, HRGN a, HRGN b, int mode);
int rgn_complexity(NPRGN r);
int rgn_copy(HRGN dst, HRGN src);
void rgn_offset(HRGN h, int dx, int dy);
NPRGN rgn_ptr(HRGN h);
int rgn_clip_rect(NPRGN r, NPRECT rc);       /* rects of r intersecting rc */
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
BYTE __far *bm_scan(LPPBITMAP pb, int y);
void bm_free(HBITMAP h);
NPDC dc_out(HDC hdc);
int dc_uses(HANDLE h);
HPALETTE pal_default(void);
void stock_init(void);
extern HBITMAP stock_bitmap;

/* font.c */
typedef struct FONTENT {
    struct FONTENT NEAR *next;
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
typedef FONTENT NEAR *NPFONT;
void font_init(void);
NPFONT font_realize(NPDC dc);
extern NPFONT fonts;

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
