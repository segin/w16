/*
 * meta.c - metafiles: the metafile DC (CreateMetaFile, CloseMetaFile and
 * the recording every GDI call on it does), the metafile handle
 * (GetMetaFile, CopyMetaFile, GetMetaFileBits, SetMetaFileBits(Better),
 * DeleteMetaFile, IsValidMetaFile), and playback (PlayMetaFile,
 * PlayMetaFileRecord, EnumMetaFile).
 *
 * Sources: the Windows 3.1 SDK, Programmer's Reference vol. 1 2.15 (what
 * a metafile is, which calls can be recorded, the handle table), vol. 2
 * (each metafile function), vol. 4 ch. 3 (METAHEADER, the record format,
 * the function numbers and the function-specific records, the placeable
 * header); Schulman et al., Undocumented Windows ch. 8 (the metafile DC
 * is a GDI object of type 11, signature 'QO'; a metafile is a global
 * block, not a GDI object; IsValidMetaFile checks its header); and what
 * Windows 3.1's GDI does, observed with the probe GD9 (tests/apps/gd9.c,
 * ARCHITECTURE.md 11.8): every byte of what it records, compared byte for
 * byte, and how it plays. The facts the code follows are noted where
 * they are used ("GD9").
 *
 * The metafile being recorded is a global block owned by GDI (GD9: the
 * handle CloseMetaFile returns is GDI's, GMEM_MOVEABLE, no other flag):
 * the METAHEADER (mtType 1, 9 words, version 300h) and the records; a
 * disk metafile is written to its file as it is closed.
 */
#include "gdi.h"

#define META_EOF 0x0000
#define META_SETBKCOLOR 0x0201
#define META_SETBKMODE 0x0102
#define META_SETMAPMODE 0x0103
#define META_SETROP2 0x0104
#define META_SETRELABS 0x0105
#define META_SETPOLYFILLMODE 0x0106
#define META_SETSTRETCHBLTMODE 0x0107
#define META_SETTEXTCHAREXTRA 0x0108
#define META_SETTEXTCOLOR 0x0209
#define META_SETTEXTJUSTIFICATION 0x020A
#define META_SETWINDOWORG 0x020B
#define META_SETWINDOWEXT 0x020C
#define META_SETVIEWPORTORG 0x020D
#define META_SETVIEWPORTEXT 0x020E
#define META_OFFSETWINDOWORG 0x020F
#define META_SCALEWINDOWEXT 0x0410
#define META_OFFSETVIEWPORTORG 0x0211
#define META_SCALEVIEWPORTEXT 0x0412
#define META_LINETO 0x0213
#define META_MOVETO 0x0214
#define META_EXCLUDECLIPRECT 0x0415
#define META_INTERSECTCLIPRECT 0x0416
#define META_ARC 0x0817
#define META_ELLIPSE 0x0418
#define META_FLOODFILL 0x0419
#define META_PIE 0x081A
#define META_RECTANGLE 0x041B
#define META_ROUNDRECT 0x061C
#define META_PATBLT 0x061D
#define META_SAVEDC 0x001E
#define META_SETPIXEL 0x041F
#define META_OFFSETCLIPRGN 0x0220
#define META_TEXTOUT 0x0521
#define META_BITBLT 0x0922
#define META_STRETCHBLT 0x0B23
#define META_POLYGON 0x0324
#define META_POLYLINE 0x0325
#define META_ESCAPE 0x0626
#define META_RESTOREDC 0x0127
#define META_FILLREGION 0x0228
#define META_FRAMEREGION 0x0429
#define META_INVERTREGION 0x012A
#define META_PAINTREGION 0x012B
#define META_SELECTCLIPREGION 0x012C
#define META_SELECTOBJECT 0x012D
#define META_SETTEXTALIGN 0x012E
#define META_DRAWTEXT 0x062F
#define META_CHORD 0x0830
#define META_SETMAPPERFLAGS 0x0231
#define META_EXTTEXTOUT 0x0A32
#define META_SETDIBTODEV 0x0D33
#define META_SELECTPALETTE 0x0234
#define META_REALIZEPALETTE 0x0035
#define META_ANIMATEPALETTE 0x0436
#define META_SETPALENTRIES 0x0037
#define META_POLYPOLYGON 0x0538
#define META_RESIZEPALETTE 0x0139
#define META_DIBBITBLT 0x0940
#define META_DIBSTRETCHBLT 0x0B41
#define META_DIBCREATEPATTERNBRUSH 0x0142
#define META_STRETCHDIB 0x0F43
#define META_EXTFLOODFILL 0x0548
#define META_DELETEOBJECT 0x01F0
#define META_CREATEPALETTE 0x00F7
#define META_CREATEBRUSH 0x00F8
#define META_CREATEPATTERNBRUSH 0x01F9
#define META_CREATEPENINDIRECT 0x02FA
#define META_CREATEFONTINDIRECT 0x02FB
#define META_CREATEBRUSHINDIRECT 0x02FC
#define META_CREATEBITMAPINDIRECT 0x02FD
#define META_CREATEBITMAP 0x06FE
#define META_CREATEREGION 0x06FF

#define MH_WORDS 9                      /* METAHEADER, in words */
/* GD9: a metafile's block is 18 bytes (a METAHEADER's size) longer than
   its words (GlobalSize, with KRNL386's 32-byte steps, matched 3.1's for
   every metafile the probe made only so) */
#define MH_SLACK 18
#define DEAD_SLOT 0xFFFF                /* an object deleted while selected */

void WINAPI FarSetOwner(HANDLE, HANDLE);
int WINAPI _lcreat(LPCSTR, int);
WORD WINAPI _lwrite(int, const void __far *, WORD);
long WINAPI _llseek(int, long, int);

#pragma pack(push, 1)
typedef struct {
    WORD mtType, mtHeaderSize, mtVersion;
    DWORD mtSize;
    WORD mtNoObjects;
    DWORD mtMaxRecord;
    WORD mtNoParameters;
} METAHEADER;
typedef METAHEADER __far *LPMH;

typedef struct {
    DWORD rdSize;
    WORD rdFunction;
    WORD rdParm[1];
} METARECORD;
typedef METARECORD __far *LPMR;

/* the metafile DC (Schulman: a DC with the signature 'QO', type 11) */
typedef struct {
    OBJHDR hdr;
    HGLOBAL hMem;                       /* the METAHEADER and the records */
    DWORD used;                         /* bytes of hMem in use */
    HDC hNextMf;                        /* the next open metafile DC */
    HPEN hPen;                          /* what SelectObject answers (GD9: the */
    HBRUSH hBrush;                      /* stock defaults at first) */
    HFONT hFont;
    HPALETTE hPal;
    HGLOBAL hSlots;                     /* the object table: a handle per slot */
    WORD nSlots;
    WORD maxObj;                        /* mtNoObjects: the most slots used */
    DWORD maxRec;                       /* mtMaxRecord */
    WORD failed;                        /* a record could not be stored */
    HGLOBAL hName;                      /* a disk metafile's file name, 0: memory */
} MFDC;
typedef MFDC __far *LPMFDC;
#pragma pack(pop)

HPALETTE GDIAPI GDISelectPalette(HDC, HPALETTE, BOOL);
DWORD GDIAPI GDIRealizePalette(HDC);
BOOL GDIAPI FloodFill(HDC, int, int, COLORREF);
BOOL GDIAPI ExtFloodFill(HDC, int, int, COLORREF, WORD);
int GDIAPI SetDIBitsToDevice(HDC, int, int, int, int, int, int, WORD, WORD, void __far *,
                             void __far *, WORD);
int GDIAPI StretchDIBits(HDC, int, int, int, int, int, int, int, int, const void __far *,
                         void __far *, WORD, DWORD);
HBITMAP GDIAPI CreateDIBitmap(HDC, const void __far *, DWORD, const void __far *,
                              void __far *, WORD);
HBRUSH GDIAPI CreateDIBPatternBrush(HGLOBAL, WORD);
HPALETTE GDIAPI CreatePalette(const void __far *);
HRGN GDIAPI CreateRectRgn(int, int, int, int);
void GDIAPI AnimatePalette(HPALETTE, WORD, WORD, const PALETTEENTRY __far *);
WORD GDIAPI SetPaletteEntries(HPALETTE, WORD, WORD, const PALETTEENTRY __far *);
BOOL GDIAPI ResizePalette(HPALETTE, WORD);
HBITMAP GDIAPI CreateBitmap(int, int, BYTE, BYTE, const void __far *);
COLORREF GDIAPI SetBkColor(HDC, COLORREF);
int GDIAPI SetBkMode(HDC, int);
int GDIAPI SetMapMode(HDC, int);
int GDIAPI SetROP2(HDC, int);
int GDIAPI SetRelAbs(HDC, int);
int GDIAPI SetPolyFillMode(HDC, int);
int GDIAPI SetStretchBltMode(HDC, int);
int GDIAPI SetTextCharacterExtra(HDC, int);
COLORREF GDIAPI SetTextColor(HDC, COLORREF);
int GDIAPI SetTextJustification(HDC, int, int);
DWORD GDIAPI SetWindowOrg(HDC, int, int);
DWORD GDIAPI SetWindowExt(HDC, int, int);
DWORD GDIAPI SetViewportOrg(HDC, int, int);
DWORD GDIAPI SetViewportExt(HDC, int, int);
DWORD GDIAPI OffsetWindowOrg(HDC, int, int);
DWORD GDIAPI ScaleWindowExt(HDC, int, int, int, int);
DWORD GDIAPI OffsetViewportOrg(HDC, int, int);
DWORD GDIAPI ScaleViewportExt(HDC, int, int, int, int);
BOOL GDIAPI LineTo(HDC, int, int);
DWORD GDIAPI MoveTo(HDC, int, int);
int GDIAPI ExcludeClipRect(HDC, int, int, int, int);
int GDIAPI IntersectClipRect(HDC, int, int, int, int);
BOOL GDIAPI Arc(HDC, int, int, int, int, int, int, int, int);
BOOL GDIAPI Ellipse(HDC, int, int, int, int);
BOOL GDIAPI Pie(HDC, int, int, int, int, int, int, int, int);
BOOL GDIAPI Chord(HDC, int, int, int, int, int, int, int, int);
BOOL GDIAPI Rectangle(HDC, int, int, int, int);
BOOL GDIAPI RoundRect(HDC, int, int, int, int, int, int);
int GDIAPI SaveDC(HDC);
BOOL GDIAPI RestoreDC(HDC, int);
COLORREF GDIAPI SetPixel(HDC, int, int, COLORREF);
int GDIAPI OffsetClipRgn(HDC, int, int);
BOOL GDIAPI TextOut(HDC, int, int, LPCSTR, int);
BOOL GDIAPI ExtTextOut(HDC, int, int, WORD, const RECT __far *, LPCSTR, int, const short __far *);
BOOL GDIAPI Polygon(HDC, const POINT __far *, int);
BOOL GDIAPI Polyline(HDC, const POINT __far *, int);
BOOL GDIAPI PolyPolygon(HDC, const POINT __far *, const int __far *, int);
int GDIAPI Escape(HDC, int, int, LPCSTR, void __far *);
WORD GDIAPI SetTextAlign(HDC, WORD);
DWORD GDIAPI SetMapperFlags(HDC, DWORD);
BOOL GDIAPI FillRgn(HDC, HRGN, HBRUSH);
BOOL GDIAPI FrameRgn(HDC, HRGN, HBRUSH, int, int);
BOOL GDIAPI InvertRgn(HDC, HRGN);
BOOL GDIAPI PaintRgn(HDC, HRGN);
HPEN GDIAPI CreatePenIndirect(const LOGPEN __far *);
HBRUSH GDIAPI CreateBrushIndirect(const LOGBRUSH __far *);
HFONT GDIAPI CreateFontIndirect(const LOGFONT __far *);
HBRUSH GDIAPI CreatePatternBrush(HBITMAP);
int GDIAPI GetDIBits(HDC, HBITMAP, WORD, WORD, void __far *, void __far *, WORD);
int GDIAPI SetDIBits(HDC, HBITMAP, WORD, WORD, const void __far *, void __far *, WORD);

static HDC mf_list;                     /* the open metafile DCs */
static void palref_add(HPALETTE pal, HDC dc);
static void palref_remove(HPALETTE pal, HDC dc);
static HMODULE gdi_module;

static void gdi_owns(HGLOBAL h)
{
    if (!gdi_module)
        gdi_module = GetModuleHandle("GDI");
    FarSetOwner(h, gdi_module);
}

/* (every metafile DC is on mf_list: while none is open no call pays for
   the check) */
int mf_dc(HDC h)
{
    return mf_list && h && (obj_magic(h) & MAGIC_MASK) == METAFILEDC_MAGIC;
}

static LPMFDC mf_get(HDC h)
{
    return (LPMFDC)obj_get(h, METAFILEDC_MAGIC);
}

/* ---- memory over 64K: a global block's bytes at an offset (the block
   locked at offset 0; KERNEL's __AHINCR between its selectors) ---- */

extern WORD __cdecl gdi_ahincr;         /* entry.asm */

static void __far *hp(void __far *base, DWORD off)
{
    return MAKELP(SELECTOROF(base) + (WORD)(off >> 16) * gdi_ahincr, (WORD)off);
}

/* n bytes from s to d, either of which may cross 64K boundaries (each
   given as a block's base and an offset) */
static void huge_copy(void __far *db, DWORD doff, void __far *sb, DWORD soff, DWORD n)
{
    while (n) {
        WORD dl = (WORD)(0x10000L - (WORD)doff), sl = (WORD)(0x10000L - (WORD)soff), k;
        DWORD m = n;
        if ((WORD)doff && m > dl)
            m = dl;
        if ((WORD)soff && m > sl)
            m = sl;
        if (m > 0x8000L)
            m = 0x8000L;
        k = (WORD)m;
        mem_copy(hp(db, doff), hp(sb, soff), k);
        doff += k;
        soff += k;
        n -= k;
    }
}

/* ---- the records ---- */

/* a record is put together here (it may cross 64K in the metafile) */
static HGLOBAL scratch;
static HDC scratch_dc;

/* room for a record of n parameter words; its parameters to be written
   at the pointer returned (the block locked until mf_end), 0 if no room */
static WORD __far *mf_begin(HDC h, WORD fn, DWORD n)
{
    LPMFDC m = mf_get(h);
    DWORD need, sz;
    WORD __far *p;
    HGLOBAL g;
    if (!m || m->failed)
        return 0;
    need = m->used + (n + 3) * 2;
    sz = GlobalSize(m->hMem);
    if (need > sz) {
        g = GlobalReAlloc(m->hMem, need + 512, GMEM_MOVEABLE);
        m = mf_get(h);
        if (!g) {
            m->failed = 1;
            return 0;
        }
        m->hMem = g;
    }
    if ((n + 3) * 2 > 0xFFF0L)
        return 0;                       /* UNVERIFIED: a record over 64K */
    if (!scratch || GlobalSize(scratch) < (n + 3) * 2) {
        HGLOBAL t = scratch ? GlobalReAlloc(scratch, (n + 3) * 2, GMEM_MOVEABLE)
                            : GlobalAlloc(GMEM_MOVEABLE, (n + 3) * 2);
        if (!t)
            return 0;
        scratch = t;
        gdi_owns(scratch);
    }
    p = GlobalLock(scratch);
    *(DWORD __far *)p = n + 3;
    p[2] = fn;
    scratch_dc = h;
    return p + 3;
}

/* the record put together goes into the metafile */
static void mf_end(HDC h)
{
    LPMFDC m = mf_get(h);
    WORD __far *p = GlobalLock(scratch);
    DWORD n = *(DWORD __far *)p * 2;
    GlobalUnlock(scratch);
    if (m && scratch_dc == h) {
        huge_copy(GlobalLock(m->hMem), m->used, p, 0, n);
        GlobalUnlock(m->hMem);
        m->used += n;
        if (n / 2 > m->maxRec)
            m->maxRec = n / 2;
    }
    GlobalUnlock(scratch);
    scratch_dc = 0;
}

/* a record of n words given in the record's order */
static BOOL mf_words(HDC h, WORD fn, WORD n, const WORD __far *w)
{
    WORD __far *p = mf_begin(h, fn, n);
    if (!p)
        return FALSE;
    mem_copy(p, w, n * 2);
    mf_end(h);
    return TRUE;
}

BOOL mf_rec0(HDC h, WORD fn)
{
    return mf_words(h, fn, 0, 0);
}

BOOL mf_rec1(HDC h, WORD fn, WORD a)
{
    return mf_words(h, fn, 1, &a);
}

BOOL mf_rec2(HDC h, WORD fn, WORD a, WORD b)
{
    WORD w[2];
    w[0] = a; w[1] = b;
    return mf_words(h, fn, 2, w);
}

BOOL mf_rec4(HDC h, WORD fn, WORD a, WORD b, WORD c, WORD d)
{
    WORD w[4];
    w[0] = a; w[1] = b; w[2] = c; w[3] = d;
    return mf_words(h, fn, 4, w);
}

BOOL mf_rec6(HDC h, WORD fn, WORD a, WORD b, WORD c, WORD d, WORD e, WORD f)
{
    WORD w[6];
    w[0] = a; w[1] = b; w[2] = c; w[3] = d; w[4] = e; w[5] = f;
    return mf_words(h, fn, 6, w);
}

/* any number of words, in the record's order */
BOOL mf_recw(HDC h, WORD fn, WORD n, const WORD __far *w)
{
    return mf_words(h, fn, n, w);
}

/* ---- the object table ---- */

static HANDLE __far *slots_lock(LPMFDC m)
{
    return m->hSlots ? (HANDLE __far *)GlobalLock(m->hSlots) : 0;
}

static int slot_of(HDC h, HANDLE obj)
{
    LPMFDC m = mf_get(h);
    HANDLE __far *s = slots_lock(m);
    WORD i;
    int r = -1;
    if (!s)
        return -1;
    for (i = 0; i < m->nSlots; i++)
        if (s[i] == obj) {
            r = i;
            break;
        }
    GlobalUnlock(m->hSlots);
    return r;
}

/* the lowest free slot, now holding obj; -1: no memory */
static int slot_new(HDC h, HANDLE obj)
{
    LPMFDC m = mf_get(h);
    HANDLE __far *s;
    WORD i;
    if (!m->hSlots) {
        HGLOBAL g = GlobalAlloc(GMEM_MOVEABLE | GMEM_ZEROINIT, 16 * sizeof(HANDLE));
        m = mf_get(h);
        if (!g)
            return -1;
        m->hSlots = g;
        m->nSlots = 16;
    }
    s = slots_lock(m);
    for (i = 0; i < m->nSlots && s[i]; i++)
        ;
    GlobalUnlock(m->hSlots);
    if (i == m->nSlots) {
        HGLOBAL g = GlobalReAlloc(m->hSlots, (DWORD)(m->nSlots * 2) * sizeof(HANDLE),
                                  GMEM_MOVEABLE | GMEM_ZEROINIT);
        m = mf_get(h);
        if (!g)
            return -1;
        m->hSlots = g;
        m->nSlots *= 2;
    }
    s = slots_lock(m);
    s[i] = obj;
    GlobalUnlock(m->hSlots);
    if (i + 1 > m->maxObj)
        m->maxObj = i + 1;
    return i;
}

static void slot_set(HDC h, int i, HANDLE v)
{
    LPMFDC m = mf_get(h);
    HANDLE __far *s = slots_lock(m);
    if (s) {
        s[i] = v;
        GlobalUnlock(m->hSlots);
    }
}

/* ---- DIBs of bitmaps (BitBlt, StretchBlt and pattern brush records) ---- */

#pragma pack(push, 1)
typedef struct {
    DWORD biSize;
    long biWidth, biHeight;
    WORD biPlanes, biBitCount;
    DWORD biCompression, biSizeImage;
    long biXPelsPerMeter, biYPelsPerMeter;
    DWORD biClrUsed, biClrImportant;
} BMIH;
#pragma pack(pop)
typedef BMIH __far *LPBMIH;


/* the size in bytes of a packed DIB's header and colour table */
static WORD dib_head(const void __far *bmi, WORD usage)
{
    const BMIH __far *bi = bmi;
    DWORD n;
    if (bi->biSize == 12) {             /* BITMAPCOREHEADER */
        WORD bpp = ((const WORD __far *)bmi)[5];
        n = bpp <= 8 ? 1 << bpp : 0;
        return (WORD)(12 + n * (usage == 1 ? 2 : 3));
    }
    n = bi->biClrUsed;
    if (!n && bi->biBitCount <= 8)
        n = 1 << bi->biBitCount;
    return (WORD)(bi->biSize + n * (usage == 1 ? 2 : 4));
}

/* the bytes of n scan lines of a DIB */
static DWORD dib_lines(const void __far *bmi, DWORD n)
{
    const BMIH __far *bi = bmi;
    DWORD w, bpp;
    if (bi->biSize == 12) {
        w = ((const WORD __far *)bmi)[2];
        bpp = ((const WORD __far *)bmi)[5];
    } else {
        if (bi->biCompression && bi->biSizeImage)
            return bi->biSizeImage;
        w = bi->biWidth;
        bpp = bi->biBitCount * bi->biPlanes;
    }
    return ((w * bpp + 31) >> 5 << 2) * n;
}

static DWORD dib_height(const void __far *bmi)
{
    const BMIH __far *bi = bmi;
    long y = bi->biSize == 12 ? ((const short __far *)bmi)[3] : bi->biHeight;
    return y < 0 ? -y : y;
}

/* GD9: a bitmap is recorded as a DIB of the whole bitmap, 1 bit a pixel
   (black and white) for a monochrome one, else the device's 4 bits, its
   size in biSizeImage, the other fields 0. hbm's DIB goes at p (0: only
   its size is returned) */
static DWORD bm_dib(HBITMAP hbm, WORD __far *p)
{
    LPPBITMAP pb = bm_pbitmap(hbm);
    BMIH __far *bi;
    WORD bpp, nc;
    DWORD line, size;
    HDC t;
    if (!pb)
        return 0;
    bpp = pb->bmPlanes * pb->bmBitsPixel;
    if (bpp != 1 && bpp != 4 && bpp != 8 && bpp != 24)
        bpp = 4;
    nc = bpp <= 8 ? 1 << bpp : 0;
    line = ((DWORD)pb->bmWidth * bpp + 31) >> 5 << 2;
    size = sizeof(BMIH) + nc * 4 + line * pb->bmHeight;
    if (!p)
        return size;
    bi = (BMIH __far *)p;
    mem_set(bi, 0, sizeof(BMIH));
    bi->biSize = sizeof(BMIH);
    bi->biWidth = pb->bmWidth;
    bi->biHeight = pb->bmHeight;
    bi->biPlanes = 1;
    bi->biBitCount = bpp;
    t = CreateCompatibleDC(0);
    if (t) {
        bi = (BMIH __far *)p;
        GetDIBits(t, hbm, 0, bi->biHeight, (BYTE __far *)p + sizeof(BMIH) + nc * 4, p, 0);
        DeleteDC(t);
    }
    bi = (BMIH __far *)p;
    bi->biSize = sizeof(BMIH);
    bi->biPlanes = 1;
    bi->biBitCount = bpp;
    bi->biCompression = 0;
    bi->biSizeImage = line * bi->biHeight;
    bi->biXPelsPerMeter = bi->biYPelsPerMeter = 0;
    bi->biClrUsed = bi->biClrImportant = 0;
    return size;
}

/* ---- creating an object in the metafile ---- */

static int rgn_words(HRGN r, WORD __far *p);

/* the object's slot, made with its creation record if it has none; -1:
   it cannot be recorded */
static int mf_object(HDC h, HANDLE obj)
{
    int i = slot_of(h, obj);
    WORD m = obj_magic(obj) & MAGIC_MASK;
    WORD __far *p;
    if (i >= 0)
        return i;
    switch (m) {
    case PEN_MAGIC: {
        LOGPEN lp = ((LPPENOBJ)obj_get(obj, PEN_MAGIC))->lp;
        if (!mf_words(h, META_CREATEPENINDIRECT, 5, (WORD __far *)&lp))
            return -1;
        break;
    }
    case BRUSH_MAGIC: {
        LPBRUSHOBJ b = obj_get(obj, BRUSH_MAGIC);
        LOGBRUSH lb = b->lb;
        if (lb.lbStyle == 3) {          /* BS_PATTERN: the bitmap as a DIB */
            DWORD n = bm_dib(lb.lbHatch, 0);
            if (!n || !(p = mf_begin(h, META_DIBCREATEPATTERNBRUSH, 2 + (n + 1) / 2)))
                return -1;
            p[0] = 3;
            p[1] = 0;
            bm_dib(lb.lbHatch, p + 2);
            mf_end(h);
        } else if (lb.lbStyle == 5) {   /* BS_DIBPATTERN: the packed DIB's block (GD9: its GlobalSize) */
            HGLOBAL d = lb.lbHatch;
            DWORD n = GlobalSize(d);
            if (!n || !(p = mf_begin(h, META_DIBCREATEPATTERNBRUSH, 2 + (n + 1) / 2)))
                return -1;
            p[0] = 5;
            p[1] = (WORD)lb.lbColor;
            mem_copy(p + 2, GlobalLock(d), (WORD)n);
            GlobalUnlock(d);
            mf_end(h);
        } else if (!mf_words(h, META_CREATEBRUSHINDIRECT, 4, (WORD __far *)&lb))
            return -1;
        break;
    }
    case FONT_MAGIC: {
        /* GD9: the LOGFONT and its face name with the terminating zero
           and at least one byte more, to a word ("Helv" 6 bytes, "System"
           8, "Courier" 10), as the font object holds them */
        LPFONTOBJ f = obj_get(obj, FONT_MAGIC);
        WORD n = 0;
        while (n < LF_FACESIZE - 1 && f->lf.lfFaceName[n])
            n++;
        n = (n + 3) & ~1;
        if (n > LF_FACESIZE)
            n = LF_FACESIZE;
        if (!(p = mf_begin(h, META_CREATEFONTINDIRECT, (LF_FIXEDPART + n) / 2)))
            return -1;
        f = obj_get(obj, FONT_MAGIC);
        mem_copy(p, &f->lf, LF_FIXEDPART + n);
        mf_end(h);
        break;
    }
    case PAL_MAGIC: {
        WORD GDIAPI GetPaletteEntries(HPALETTE, WORD, WORD, PALETTEENTRY __far *);
        WORD n = GetPaletteEntries(obj, 0, 0, 0);
        if (!(p = mf_begin(h, META_CREATEPALETTE, 2 + 2 * n)))
            return -1;
        p[0] = 0x300;
        p[1] = n;
        GetPaletteEntries(obj, 0, n, (PALETTEENTRY __far *)(p + 2));
        mf_end(h);
        break;
    }
    case RGN_MAGIC: {
        int n = rgn_words(obj, 0);
        if (!(p = mf_begin(h, META_CREATEREGION, n)))
            return -1;
        rgn_words(obj, p);
        mf_end(h);
        break;
    }
    default:
        return -1;
    }
    return slot_new(h, obj);
}

/* a region as a metafile holds it (GD9, as 3.1's region object): 0,
   6 (the object type), the region's sequence count (a DWORD), the size
   in bytes (the words that follow the size, plus 2... as 3.1 counts it:
   all the words, plus 2), the number of scans, the most x values in one
   scan, the bounding box, then each scan: the number of x values, top,
   bottom, the x values (left, right pairs), the number again. Returns
   the words. */
static int rgn_words(HRGN r, WORD __far *p)
{
    LPRGN d = rgn_ptr(r);
    LPOBJHDR o = obj_get(r, RGN_MAGIC);
    WORD i, n = 0, scans = 0, maxs = 0, k;
    WORD w = 11;
    if (!d)
        return w;
    /* bands: rectangles with the same top */
    for (i = 0; i < d->n; i = k) {
        for (k = i; k < d->n && d->r[k].top == d->r[i].top; k++)
            ;
        scans++;
        if ((k - i) * 2 > maxs)
            maxs = (k - i) * 2;
        w += 3 + (k - i) * 2 + 1;
    }
    if (!p)
        return w;
    p[0] = 0;
    p[1] = 6;
    *(DWORD __far *)(p + 2) = o ? o->dwCount : 0;
    p[4] = w * 2 + 2;
    p[5] = scans;
    p[6] = maxs;
    if (d->n) {
        p[7] = d->box.left;
        p[8] = d->box.top;
        p[9] = d->box.right;
        p[10] = d->box.bottom;
    } else
        p[7] = p[8] = p[9] = p[10] = 0;
    n = 11;
    for (i = 0; i < d->n; i = k) {
        WORD j;
        for (k = i; k < d->n && d->r[k].top == d->r[i].top; k++)
            ;
        p[n++] = (k - i) * 2;
        p[n++] = d->r[i].top;
        p[n++] = d->r[i].bottom;
        for (j = i; j < k; j++) {
            p[n++] = d->r[j].left;
            p[n++] = d->r[j].right;
        }
        p[n++] = (k - i) * 2;
    }
    return w;
}

/* ---- the recording calls GDI's other files make ---- */

/* SelectObject on a metafile DC (GD9): the object's creation record the
   first time, then META_SELECTOBJECT with its slot; the answer is the
   previous object of its kind (a stock object at first); a region is
   selected as the clip region by the same record, answering 1; a
   bitmap cannot be selected (0) but takes a slot all the same */
HGDIOBJ mf_select(HDC h, HGDIOBJ obj)
{
    WORD m = obj_magic(obj) & MAGIC_MASK;
    LPMFDC d;
    HANDLE old;
    int i;
    if (m == BITMAP_MAGIC) {
        i = slot_new(h, obj);
        if (i >= 0)
            slot_set(h, i, 0);
        return 0;
    }
    if (m != PEN_MAGIC && m != BRUSH_MAGIC && m != FONT_MAGIC && m != RGN_MAGIC)
        return 0;
    i = mf_object(h, obj);
    if (i < 0 || !mf_rec1(h, META_SELECTOBJECT, i))
        return 0;
    d = mf_get(h);
    switch (m) {
    case PEN_MAGIC: old = d->hPen; d->hPen = obj; return old;
    case BRUSH_MAGIC: old = d->hBrush; d->hBrush = obj; return old;
    case FONT_MAGIC: old = d->hFont; d->hFont = obj; return old;
    }
    return 1;
}

HPALETTE mf_select_palette(HDC h, HPALETTE pal)
{
    LPMFDC d;
    HPALETTE old;
    int i = mf_object(h, pal);
    if (i < 0 || !mf_rec1(h, META_SELECTPALETTE, i))
        return 0;
    d = mf_get(h);
    old = d->hPal;
    d->hPal = pal;
    palref_remove(old, h);              /* (the list: see below) */
    palref_add(pal, h);
    return 1;                           /* GD9: not the previous palette */
}

/* an object used by a region call (FillRgn, ...): its slot */
int mf_slot(HDC h, HANDLE obj)
{
    return mf_object(h, obj);
}

/* DeleteObject: in every open metafile that has the object, a
   META_DELETEOBJECT record and its slot free again; GD9: an object
   selected in the metafile DC goes without a record and keeps its slot */
void mf_deleted(HANDLE obj)
{
    HDC h;
    for (h = mf_list; h; ) {
        LPMFDC d = mf_get(h);
        HDC next;
        int i;
        if (!d)
            break;
        next = d->hNextMf;
        i = slot_of(h, obj);
        if (i >= 0) {
            d = mf_get(h);
            if (d->hPen == obj || d->hBrush == obj || d->hFont == obj || d->hPal == obj)
                slot_set(h, i, DEAD_SLOT);
            else {
                mf_rec1(h, META_DELETEOBJECT, i);
                slot_set(h, i, 0);
            }
        }
        h = next;
    }
}

/* ---- a palette's metafile list (Schulman ch. 8: the object header's
   wMetaList). What GD9 saw on 3.1: each SelectPalette into a metafile DC
   adds the DC to the palette's list, and selecting another palette there
   takes one entry of the DC off the previous palette's list; closing the
   metafile takes nothing off. AnimatePalette and ResizePalette write
   their record once for every entry, into the DC the entry names: an
   entry left by a closed metafile names whatever metafile DC has that
   handle now (in GD9, the next metafile, which got the same handle: one
   change came out as many records as the palette had had selections).
   w16 keeps the list here (pairs of palette and DC; wMetaList is
   ExtendedGdiHeap's) and writes into an entry's DC when it is an open
   metafile DC. ---- */

typedef struct { HPALETTE pal; HDC dc; } PALREF;
static HGLOBAL palrefs;                 /* PALREF[] */
static WORD npalrefs;

static void palref_add(HPALETTE pal, HDC dc)
{
    PALREF __far *r;
    if (!palrefs || GlobalSize(palrefs) < (DWORD)(npalrefs + 1) * sizeof(PALREF)) {
        DWORD sz = (DWORD)(npalrefs + 16) * sizeof(PALREF);
        HGLOBAL g = palrefs ? GlobalReAlloc(palrefs, sz, GMEM_MOVEABLE)
                            : GlobalAlloc(GMEM_MOVEABLE, sz);
        if (!g)
            return;
        if (!palrefs)
            gdi_owns(g);
        palrefs = g;
    }
    r = GlobalLock(palrefs);
    r[npalrefs].pal = pal;
    r[npalrefs].dc = dc;
    npalrefs++;
    GlobalUnlock(palrefs);
}

/* one entry (pal, dc) off the list; dc 0: every entry of pal (deleted) */
static void palref_remove(HPALETTE pal, HDC dc)
{
    PALREF __far *r;
    WORD i, k;
    if (!palrefs)
        return;
    r = GlobalLock(palrefs);
    for (i = 0, k = 0; i < npalrefs; i++) {
        if (r[i].pal == pal && (!dc || r[i].dc == dc)) {
            if (dc)
                dc = 0xFFFF;            /* (one only: the rest are kept) */
            continue;
        }
        r[k++] = r[i];
    }
    npalrefs = k;
    GlobalUnlock(palrefs);
}

void mf_palette_gone(HPALETTE pal)
{
    palref_remove(pal, 0);
}

/* AnimatePalette, ResizePalette: the record (its words given) for every
   entry of the palette's list whose DC is an open metafile DC */
void mf_palette_changed(HPALETTE pal, WORD fn, WORD n, const WORD __far *w)
{
    WORD i;
    for (i = 0; i < npalrefs; i++) {
        PALREF __far *r = GlobalLock(palrefs);
        HPALETTE p = r[i].pal;
        HDC dc = r[i].dc;
        GlobalUnlock(palrefs);
        if (p == pal && mf_dc(dc))
            mf_words(dc, fn, n, w);
    }
}

/* ---- the recording functions with more than a few words ---- */

BOOL mf_text(HDC h, int x, int y, LPCSTR s, int n)
{
    WORD k = n > 0 ? (n + 1) / 2 : 0;
    WORD __far *p = mf_begin(h, META_TEXTOUT, 3 + k);
    if (!p)
        return FALSE;
    p[0] = n;
    if (k) {
        p[k] = 0;
        mem_copy(p + 1, s, n);
    }
    p[1 + k] = y;
    p[2 + k] = x;
    mf_end(h);
    return TRUE;
}

/* GD9: y, x, the count, the options, the rectangle only when there are
   options, the string (to a word), the widths when given */
BOOL mf_exttext(HDC h, int x, int y, WORD opt, const RECT __far *rc, LPCSTR s, int n,
                const short __far *dx)
{
    WORD k = n > 0 ? (n + 1) / 2 : 0, at, words;
    WORD __far *p;
    if (n < 0)
        n = 0;
    words = 4 + (opt ? 4 : 0) + k + (dx ? n : 0);
    p = mf_begin(h, META_EXTTEXTOUT, words);
    if (!p)
        return FALSE;
    p[0] = y;
    p[1] = x;
    p[2] = n;
    p[3] = opt;
    at = 4;
    if (opt) {
        if (rc)
            mem_copy(p + 4, rc, 8);
        else
            mem_set(p + 4, 0, 8);       /* (3.1 reads a rectangle at NULL) */
        at = 8;
    }
    if (k) {
        p[at + k - 1] = 0;
        mem_copy(p + at, s, n);
        at += k;
    }
    if (dx)
        mem_copy(p + at, dx, n * 2);
    mf_end(h);
    return TRUE;
}

/* Polygon, Polyline: the count and the points */
BOOL mf_poly(HDC h, WORD fn, const POINT __far *pts, int n)
{
    WORD __far *p;
    if (n < 0)
        return FALSE;
    p = mf_begin(h, fn, 1 + 2 * n);
    if (!p)
        return FALSE;
    p[0] = n;
    mem_copy(p + 1, pts, n * 4);
    mf_end(h);
    return TRUE;
}

BOOL mf_polypoly(HDC h, const POINT __far *pts, const int __far *counts, int n)
{
    WORD __far *p;
    DWORD total = 0;
    int i;
    for (i = 0; i < n; i++)
        total += counts[i];
    p = mf_begin(h, META_POLYPOLYGON, 1 + n + 2 * total);
    if (!p)
        return FALSE;
    p[0] = n;
    mem_copy(p + 1, counts, n * 2);
    mem_copy(p + 1 + n, pts, (WORD)total * 4);
    mf_end(h);
    return TRUE;
}

/* GD9: the escape's number, the count, the data to a word */
int mf_escape(HDC h, int fn, int n, LPCSTR in)
{
    WORD k = n > 0 ? (n + 1) / 2 : 0;
    WORD __far *p = mf_begin(h, META_ESCAPE, 2 + k);
    if (!p)
        return 0;
    p[0] = fn;
    p[1] = n;
    if (k) {
        p[1 + k] = 0;
        if (in)
            mem_copy(p + 2, in, n);
    }
    mf_end(h);
    return 1;
}

/* BitBlt and StretchBlt (GD9): META_DIBBITBLT / META_DIBSTRETCHBLT with
   the raster operation (low word first), the source's then the
   destination's rectangle as reversed parameters, then the whole source
   bitmap as a DIB; with no source (a pattern operation) BitBlt records
   the source DC's 0 in its place and no bitmap, and StretchBlt records
   nothing (and fails). A source that is not a memory DC: nothing, 0. */
BOOL mf_blt(HDC h, int x, int y, int w, int hh, HDC hsrc, int xs, int ys, int ws, int hs,
            DWORD rop, int stretch)
{
    WORD __far *p;
    WORD hw;
    LPDC sdc;
    DWORD n;
    int needsrc = ((rop >> 2) ^ rop) & 0x330000L ? 1 : 0;
    if (!needsrc) {
        if (stretch)
            return FALSE;
        p = mf_begin(h, META_DIBBITBLT, 9);
        if (!p)
            return FALSE;
        p[0] = LOWORD(rop); p[1] = HIWORD(rop);
        p[2] = ys; p[3] = xs; p[4] = 0;
        p[5] = hh; p[6] = w; p[7] = y; p[8] = x;
        mf_end(h);
        return TRUE;
    }
    sdc = dc_get(hsrc);
    if (!sdc || !(sdc->flags & DCF_MEMORY) || !sdc->hBitmap)
        return FALSE;
    n = bm_dib(sdc->hBitmap, 0);
    hw = stretch ? 10 : 8;
    if (!n || !(p = mf_begin(h, stretch ? META_DIBSTRETCHBLT : META_DIBBITBLT, hw + (n + 1) / 2)))
        return FALSE;
    p[0] = LOWORD(rop); p[1] = HIWORD(rop);
    if (stretch) {
        p[2] = hs; p[3] = ws; p[4] = ys; p[5] = xs;
        p[6] = hh; p[7] = w; p[8] = y; p[9] = x;
    } else {
        p[2] = ys; p[3] = xs;
        p[4] = hh; p[5] = w; p[6] = y; p[7] = x;
    }
    sdc = dc_get(hsrc);
    bm_dib(sdc->hBitmap, p + hw);
    mf_end(h);
    return TRUE;
}

/* SetDIBitsToDevice (GD9): usage, the scans, the first scan, the source's
   y, x, height and width, the destination's y and x, then the
   BITMAPINFO and the scans given */
BOOL mf_dibtodev(HDC h, int x, int y, int cx, int cy, int xs, int ys, WORD start, WORD n,
                 const void __far *bits, const void __far *bmi, WORD usage)
{
    WORD hd = dib_head(bmi, usage);
    DWORD nb = dib_lines(bmi, n);
    WORD __far *p = mf_begin(h, META_SETDIBTODEV, 9 + (hd + nb + 1) / 2);
    if (!p)
        return FALSE;
    p[0] = usage; p[1] = n; p[2] = start; p[3] = ys; p[4] = xs;
    p[5] = cy; p[6] = cx; p[7] = y; p[8] = x;
    mem_copy(p + 9, bmi, hd);
    mem_copy((BYTE __far *)(p + 9) + hd, bits, (WORD)nb);
    mf_end(h);
    return TRUE;
}

/* StretchDIBits (GD9): the raster operation, usage, the source's height,
   width, y, x, the destination's height, width, y, x, the BITMAPINFO and
   the whole DIB's bits */
BOOL mf_stretchdib(HDC h, int xd, int yd, int wd, int hd, int xs, int ys, int ws, int hs,
                   const void __far *bits, const void __far *bmi, WORD usage, DWORD rop)
{
    WORD head = dib_head(bmi, usage);
    DWORD nb = dib_lines(bmi, dib_height(bmi));
    WORD __far *p = mf_begin(h, META_STRETCHDIB, 11 + (head + nb + 1) / 2);
    if (!p)
        return FALSE;
    p[0] = LOWORD(rop); p[1] = HIWORD(rop); p[2] = usage;
    p[3] = hs; p[4] = ws; p[5] = ys; p[6] = xs;
    p[7] = hd; p[8] = wd; p[9] = yd; p[10] = xd;
    mem_copy(p + 11, bmi, head);
    mem_copy((BYTE __far *)(p + 11) + head, bits, (WORD)nb);
    mf_end(h);
    return TRUE;
}

/* ---- CreateMetaFile, CloseMetaFile ---- */

HDC GDIAPI CreateMetaFile(LPCSTR file)                             /* GDI.125 */
{
    HDC h;
    LPMFDC m;
    HGLOBAL g, name = 0;
    if (file) {
        /* the file is made now: a name it cannot have fails here */
        int fh = _lcreat(file, 0);
        WORD n = lstrlen(file) + 1;
        if (fh == -1)
            return 0;
        _lclose(fh);
        name = GlobalAlloc(GMEM_MOVEABLE, n);
        if (!name)
            return 0;
        mem_copy(GlobalLock(name), file, n);
        GlobalUnlock(name);
        gdi_owns(name);
    }
    g = GlobalAlloc(GMEM_MOVEABLE, 512);
    if (!g) {
        if (name)
            GlobalFree(name);
        return 0;
    }
    gdi_owns(g);
    h = obj_alloc(METAFILEDC_MAGIC, sizeof(MFDC));
    if (!h) {
        GlobalFree(g);
        if (name)
            GlobalFree(name);
        return 0;
    }
    m = mf_get(h);
    m->hMem = g;
    m->used = MH_WORDS * 2;
    m->maxRec = 3;                      /* the end record */
    m->hPen = stock[7];                 /* BLACK_PEN */
    m->hBrush = stock[0];               /* WHITE_BRUSH */
    m->hFont = stock[14];               /* GD9: DEVICE_DEFAULT_FONT */
    m->hPal = stock[15];                /* DEFAULT_PALETTE */
    m->hName = name;
    m->hNextMf = mf_list;
    mf_list = h;
    return h;
}

static void mf_unlink(HDC h)
{
    HDC cur = mf_list, prev = 0;
    while (cur) {
        LPMFDC d = mf_get(cur);
        if (!d)
            break;
        if (cur == h) {
            if (prev)
                mf_get(prev)->hNextMf = d->hNextMf;
            else
                mf_list = d->hNextMf;
            return;
        }
        prev = cur;
        cur = d->hNextMf;
    }
}

static BOOL write_file(LPCSTR name, const void __far *p, DWORD n);
static HGLOBAL disk_handle(const METAHEADER __far *mh, LPCSTR name, DWORD size);

HANDLE GDIAPI CloseMetaFile(HDC h)                                 /* GDI.126 */
{
    LPMFDC m = mf_get(h);
    HGLOBAL g, r;
    LPMH mh;
    BOOL ok;
    if (!m)
        return 0;
    ok = mf_rec0(h, META_EOF);
    m = mf_get(h);
    mf_unlink(h);
    g = m->hMem;
    if (!ok || m->failed) {
        GlobalFree(g);
        r = 0;
    } else {
        mh = (LPMH)GlobalLock(g);
        mh->mtType = 1;
        mh->mtHeaderSize = MH_WORDS;
        mh->mtVersion = 0x300;
        mh->mtSize = m->used / 2;
        mh->mtNoObjects = m->maxObj;
        mh->mtMaxRecord = m->maxRec;
        mh->mtNoParameters = 0;
        GlobalUnlock(g);
        r = GlobalReAlloc(g, m->used + MH_SLACK, GMEM_MOVEABLE);
        if (!r)
            r = g;
        m = mf_get(h);
        if (m->hName) {
            /* a disk metafile: the file, and a handle naming it */
            LPCSTR name = GlobalLock(m->hName);
            /* GD9: the file's header says mtType 1, the handle's 2 */
            mh = (LPMH)GlobalLock(r);
            ok = write_file(name, mh, m->used);
            g = ok ? disk_handle(mh, name, 0) : 0;
            GlobalUnlock(r);
            GlobalFree(r);
            m = mf_get(h);
            GlobalUnlock(m->hName);
            r = g;
        }
    }
    m = mf_get(h);
    if (m->hName)
        GlobalFree(m->hName);
    if (m->hSlots)
        GlobalFree(m->hSlots);
    obj_free(h);
    return r;
}

/* ---- the metafile's handle ---- */

static BOOL write_file(LPCSTR name, const void __far *p, DWORD n)
{
    int fh = _lcreat(name, 0);
    BOOL ok;
    if (fh == -1)
        return FALSE;
    ok = TRUE;
    while (n && ok) {
        WORD k = n > 0x8000L ? 0x8000 : (WORD)n;
        ok = _lwrite(fh, p, k) == k;
        p = hp((void __far *)p, k);
        n -= k;
    }
    _lclose(fh);
    return ok;
}

/* a disk metafile's handle (GD9: GDI's block of C0h bytes as KRNL386
   gives it): the header with mtType 2, a word, the file's size (a DWORD,
   as GetMetaFile leaves it), then the file's OFSTRUCT from KERNEL's
   OpenFile (its path at offset 20h) */
#define DH_OF 24
#define DH_SIZE 0xC0
#define DH_NAME(p) ((LPCSTR)(p) + DH_OF + 8)

static HGLOBAL disk_handle(const METAHEADER __far *mh, LPCSTR name, DWORD size)
{
    HGLOBAL g = GlobalAlloc(GMEM_MOVEABLE | GMEM_ZEROINIT, DH_SIZE);
    BYTE __far *p;
    int fh;
    if (!g)
        return 0;
    gdi_owns(g);
    p = GlobalLock(g);
    mem_copy(p, mh, sizeof(METAHEADER));
    ((LPMH)p)->mtType = 2;
    *(DWORD __far *)(p + 20) = size;
    fh = OpenFile(name, p + DH_OF, 0x4000);     /* OF_EXIST */
    if (fh == -1)
        lstrcpy((LPSTR)DH_NAME(p), name);
    GlobalUnlock(g);
    return g;
}

/* Schulman: the header's first fields are checked */
static BOOL header_ok(const METAHEADER __far *mh)
{
    return (mh->mtType == 1 || mh->mtType == 2) && mh->mtHeaderSize == MH_WORDS &&
           (mh->mtVersion == 0x300 || mh->mtVersion == 0x100);
}

BOOL GDIAPI IsValidMetaFile(HANDLE h)                              /* GDI.410 */
{
    LPMH mh;
    BOOL ok;
    if (!h || GlobalSize(h) < sizeof(METAHEADER))
        return FALSE;
    mh = (LPMH)GlobalLock(h);
    if (!mh)
        return FALSE;
    ok = header_ok(mh);
    GlobalUnlock(h);
    return ok;
}

BOOL GDIAPI DeleteMetaFile(HANDLE h)                               /* GDI.127 */
{
    if (!IsValidMetaFile(h))
        return FALSE;                   /* GD5: 3.1's answer for 0 */
    return GlobalFree(h) == 0;
}

/* a disk metafile's contents, read into a new block (0: none) */
static DWORD read_size;                 /* read_file's bytes */

static HGLOBAL read_file(LPCSTR name)
{
    char of[136];
    int fh = OpenFile(name, of, 0);
    long n;
    HGLOBAL g;
    BYTE __far *p;
    DWORD at = 0;
    if (fh == -1)
        return 0;
    n = _llseek(fh, 0, 2);
    read_size = n;
    _llseek(fh, 0, 0);
    g = n >= (long)sizeof(METAHEADER) ? GlobalAlloc(GMEM_MOVEABLE, n) : 0;
    if (g) {
        p = GlobalLock(g);
        while (n > 0) {
            WORD k = n > 0x8000L ? 0x8000 : (WORD)n;
            if (_lread(fh, hp(p, at), k) != k)
                break;
            at += k;
            n -= k;
        }
        GlobalUnlock(g);
        if (n) {
            GlobalFree(g);
            g = 0;
        }
    }
    _lclose(fh);
    return g;
}

HANDLE GDIAPI GetMetaFile(LPCSTR name)                             /* GDI.124 */
{
    HGLOBAL g = read_file(name), r;
    LPMH mh;
    if (!g)
        return 0;
    mh = GlobalLock(g);
    /* GD9: the header is not checked (a placeable file gives a handle
       IsValidMetaFile refuses) */
    r = read_size >= sizeof(METAHEADER) ? disk_handle(mh, name, read_size) : 0;
    GlobalUnlock(g);
    GlobalFree(g);
    return r;
}

/* the metafile's records in memory: the handle itself (memory), or a new
   block with the file's contents (disk; *tmp set) */
static HGLOBAL mf_bits(HANDLE h, int *tmp)
{
    LPMH mh;
    HGLOBAL g = 0;
    *tmp = 0;
    if (!IsValidMetaFile(h))
        return 0;
    mh = GlobalLock(h);
    if (mh->mtType == 2) {
        g = read_file(DH_NAME(mh));
        *tmp = 1;
    } else
        g = h;
    GlobalUnlock(h);
    return g;
}

/* the words of a metafile up to and with its first end record */
static DWORD words_to_eof(LPMH mh, DWORD size)
{
    DWORD at = mh->mtHeaderSize;
    while (at + 3 <= size) {
        DWORD rs = *(DWORD __far *)hp(mh, at * 2);
        WORD fn = *(WORD __far *)hp(mh, at * 2 + 4);
        if (!fn || rs < 3)
            return at + 3;
        at += rs;
    }
    return size;
}

/* GD9, what 3.1's copies hold: memory to memory, and a file to a file,
   the same words; a file to memory 3 words more (mtSize too; 3.1 leaves
   whatever was in memory there, w16 zeros); memory to a file the words up
   to the end record, the header (and the handle) saying 3 words more than
   the source */
HANDLE GDIAPI CopyMetaFile(HANDLE h, LPCSTR name)                  /* GDI.151 */
{
    int tmp;
    HGLOBAL src = mf_bits(h, &tmp), r = 0;
    DWORD n, words;
    LPMH mh;
    if (!src)
        return 0;
    mh = GlobalLock(src);
    words = mh->mtSize;
    if (words * 2 > GlobalSize(src))
        words = GlobalSize(src) / 2;
    if (name) {
        WORD t = mh->mtType;
        DWORD sz = mh->mtSize;
        n = tmp ? words * 2 : words_to_eof(mh, words) * 2;
        mh->mtType = 1;
        if (!tmp)
            mh->mtSize = sz + 3;
        if (write_file(name, mh, n))
            r = disk_handle(mh, name, 0);
        mh->mtType = t;
        mh->mtSize = sz;
    } else {
        n = words * 2 + (tmp ? 6 : 0);
        r = GlobalAlloc(GMEM_MOVEABLE | GMEM_ZEROINIT, n + MH_SLACK);
        if (r) {
            BYTE __far *d = GlobalLock(r);
            gdi_owns(r);
            huge_copy(d, 0, mh, 0, words * 2);
            ((LPMH)d)->mtType = 1;
            ((LPMH)d)->mtSize = n / 2;
            GlobalUnlock(r);
        }
    }
    GlobalUnlock(src);
    if (tmp)
        GlobalFree(src);
    return r;
}

HGLOBAL GDIAPI GetMetaFileBits(HANDLE h)                           /* GDI.159 */
{
    return IsValidMetaFile(h) ? h : 0;
}

HANDLE GDIAPI SetMetaFileBits(HGLOBAL h)                           /* GDI.160 */
{
    return IsValidMetaFile(h) ? h : 0;
}

/* the block becomes GDI's (SDK: so that OLE's metafiles outlive the
   program) */
HANDLE GDIAPI SetMetaFileBitsBetter(HGLOBAL h)                     /* GDI.196 */
{
    if (!IsValidMetaFile(h))
        return 0;
    gdi_owns(h);
    return h;
}

/* ---- playback ---- */

typedef int (__far __pascal *MFENUMPROC)(HDC, HANDLE __far *, LPMR, int, DWORD);

/* the slot a created object goes into: the lowest free one (-1: none) */
static int ht_free(HANDLE __far *ht, WORD n)
{
    WORD i;
    for (i = 0; i < n; i++)
        if (!ht[i])
            return i;
    return -1;
}

static void ht_put(HANDLE __far *ht, WORD n, HANDLE obj)
{
    int i = ht_free(ht, n);
    if (i >= 0)
        ht[i] = obj;
    else if (obj)
        DeleteObject(obj);
}



/* the selected object of h of the kind of obj (0: none, or not a DC) */
static HANDLE selected_kind(HDC h, HANDLE obj)
{
    LPDC dc = dc_get(h);
    WORD m = obj_magic(obj) & MAGIC_MASK;
    if (!dc)
        return 0;
    switch (m) {
    case PEN_MAGIC: return dc->hPen;
    case BRUSH_MAGIC: return dc->hBrush;
    case FONT_MAGIC: return dc->hFont;
    case PAL_MAGIC: return dc->hPal;
    }
    return 0;
}

/* a DIB record's bitmap: a monochrome bitmap for a 1-bit DIB (so that the
   DC's colours apply as for a monochrome source, GD9's playback), else
   one of the device's format */
static HDC dib_source(HDC h, const void __far *bmi, const void __far *bits, HBITMAP *hbm,
                      HBITMAP *old)
{
    const BMIH __far *bi = bmi;
    HDC s;
    int w = bi->biSize == 12 ? ((const short __far *)bmi)[2] : (int)bi->biWidth;
    int y = (int)dib_height(bmi);
    WORD bpp = bi->biSize == 12 ? ((const WORD __far *)bmi)[5] : bi->biBitCount;
    (void)h;
    *hbm = 0;
    s = CreateCompatibleDC(0);
    if (!s)
        return 0;
    *hbm = bpp == 1 ? CreateBitmap(w, y, 1, 1, 0) : bm_screen(w, y);
    if (*hbm)
        SetDIBits(s, *hbm, 0, y, bits, (void __far *)bmi, 0);
    if (!*hbm) {
        DeleteDC(s);
        return 0;
    }
    *old = SelectObject(s, *hbm);
    return s;
}

static void dib_source_free(HDC s, HBITMAP hbm, HBITMAP old)
{
    SelectObject(s, old);
    DeleteObject(hbm);
    DeleteDC(s);
}

static HRGN region_of(const WORD __far *p, WORD words)
{
    /* the bands back into a region: each scan's rectangles OR'ed in */
    HRGN r = CreateRectRgn(0, 0, 0, 0), t;
    WORD scans, s, at = 11;
    if (!r || words < 11)
        return r;
    scans = p[5];
    t = CreateRectRgn(0, 0, 0, 0);
    for (s = 0; s < scans && at + 3 <= words; s++) {
        WORD cnt = p[at], top = p[at + 1], bottom = p[at + 2], j;
        if (at + 3 + cnt + 1 > words)
            break;
        for (j = 0; j + 1 < cnt; j += 2) {
            rgn_set_rect(t, p[at + 3 + j], top, p[at + 4 + j], bottom);
            rgn_combine(r, r, t, RGN_OR);
        }
        at += 3 + cnt + 1;
    }
    DeleteObject(t);
    return r;
}

/* one record (also for PlayMetaFile and EnumMetaFile) */
void GDIAPI PlayMetaFileRecord(HDC h, HANDLE __far *ht, METARECORD __far *mr, WORD n) /* GDI.176 */
{
    const WORD __far *p = mr->rdParm;
    WORD words = mr->rdSize > 3 ? (WORD)(mr->rdSize - 3) : 0;
    switch (mr->rdFunction) {
    case META_SETBKCOLOR: SetBkColor(h, MAKELONG(p[0], p[1])); break;
    case META_SETBKMODE: SetBkMode(h, p[0]); break;
    case META_SETMAPMODE: SetMapMode(h, p[0]); break;
    case META_SETROP2: SetROP2(h, p[0]); break;
    case META_SETRELABS: SetRelAbs(h, p[0]); break;
    case META_SETPOLYFILLMODE: SetPolyFillMode(h, p[0]); break;
    case META_SETSTRETCHBLTMODE: SetStretchBltMode(h, p[0]); break;
    case META_SETTEXTCHAREXTRA: SetTextCharacterExtra(h, p[0]); break;
    case META_SETTEXTCOLOR: SetTextColor(h, MAKELONG(p[0], p[1])); break;
    case META_SETTEXTJUSTIFICATION: SetTextJustification(h, p[1], p[0]); break;
    case META_SETWINDOWORG: SetWindowOrg(h, p[1], p[0]); break;
    case META_SETWINDOWEXT: SetWindowExt(h, p[1], p[0]); break;
    case META_SETVIEWPORTORG: SetViewportOrg(h, p[1], p[0]); break;
    case META_SETVIEWPORTEXT: SetViewportExt(h, p[1], p[0]); break;
    case META_OFFSETWINDOWORG: OffsetWindowOrg(h, p[1], p[0]); break;
    case META_SCALEWINDOWEXT: ScaleWindowExt(h, p[3], p[2], p[1], p[0]); break;
    case META_OFFSETVIEWPORTORG: OffsetViewportOrg(h, p[1], p[0]); break;
    case META_SCALEVIEWPORTEXT: ScaleViewportExt(h, p[3], p[2], p[1], p[0]); break;
    case META_LINETO: LineTo(h, p[1], p[0]); break;
    case META_MOVETO: MoveTo(h, p[1], p[0]); break;
    case META_EXCLUDECLIPRECT: ExcludeClipRect(h, p[3], p[2], p[1], p[0]); break;
    case META_INTERSECTCLIPRECT: IntersectClipRect(h, p[3], p[2], p[1], p[0]); break;
    case META_ARC: Arc(h, p[7], p[6], p[5], p[4], p[3], p[2], p[1], p[0]); break;
    case META_ELLIPSE: Ellipse(h, p[3], p[2], p[1], p[0]); break;
    case META_FLOODFILL: FloodFill(h, p[3], p[2], MAKELONG(p[0], p[1])); break;
    case META_EXTFLOODFILL: ExtFloodFill(h, p[4], p[3], MAKELONG(p[1], p[2]), p[0]); break;
    case META_PIE: Pie(h, p[7], p[6], p[5], p[4], p[3], p[2], p[1], p[0]); break;
    case META_CHORD: Chord(h, p[7], p[6], p[5], p[4], p[3], p[2], p[1], p[0]); break;
    case META_RECTANGLE: Rectangle(h, p[3], p[2], p[1], p[0]); break;
    case META_ROUNDRECT: RoundRect(h, p[5], p[4], p[3], p[2], p[1], p[0]); break;
    case META_PATBLT: PatBlt(h, p[5], p[4], p[3], p[2], MAKELONG(p[0], p[1])); break;
    case META_SAVEDC:                   /* GD9: as the DC has them; a level */
        SaveDC(h);                      /* saved stays when the play ends */
        break;
    case META_RESTOREDC:                /* GD9: also below the metafile's own */
        RestoreDC(h, (short)p[0]);
        break;
    case META_SETPIXEL: SetPixel(h, p[3], p[2], MAKELONG(p[0], p[1])); break;
    case META_OFFSETCLIPRGN: OffsetClipRgn(h, p[1], p[0]); break;
    case META_TEXTOUT: {
        WORD k = (p[0] + 1) / 2;
        if (words >= 3 + k)
            TextOut(h, p[2 + k], p[1 + k], (LPCSTR)(p + 1), p[0]);
        break;
    }
    case META_EXTTEXTOUT: {
        WORD cnt = p[2], opt = p[3], at = opt ? 8 : 4, k = (cnt + 1) / 2;
        const short __far *dx = words >= at + k + cnt && cnt ? (const short __far *)(p + at + k) : 0;
        ExtTextOut(h, p[1], p[0], opt, opt ? (const RECT __far *)(p + 4) : 0,
                   (LPCSTR)(p + at), cnt, dx);
        break;
    }
    case META_POLYGON: Polygon(h, (const POINT __far *)(p + 1), p[0]); break;
    case META_POLYLINE: Polyline(h, (const POINT __far *)(p + 1), p[0]); break;
    case META_POLYPOLYGON:
        PolyPolygon(h, (const POINT __far *)(p + 1 + p[0]), (const int __far *)(p + 1), p[0]);
        break;
    case META_ESCAPE: Escape(h, p[0], p[1], (LPCSTR)(p + 2), 0); break;
    case META_SETTEXTALIGN: SetTextAlign(h, p[0]); break;
    case META_SETMAPPERFLAGS: SetMapperFlags(h, MAKELONG(p[0], p[1])); break;
    case META_SELECTOBJECT:
    case META_SELECTCLIPREGION:
        if (p[0] < n && ht[p[0]])
            SelectObject(h, ht[p[0]]);
        break;
    case META_SELECTPALETTE:
        if (p[0] < n && ht[p[0]])
            GDISelectPalette(h, ht[p[0]], 0);
        break;
    case META_REALIZEPALETTE: GDIRealizePalette(h); break;
    case META_FILLREGION:
        if (p[0] < n && p[1] < n && ht[p[0]] && ht[p[1]])
            FillRgn(h, ht[p[0]], ht[p[1]]);
        break;
    case META_FRAMEREGION:
        if (p[0] < n && p[1] < n && ht[p[0]] && ht[p[1]])
            FrameRgn(h, ht[p[0]], ht[p[1]], p[3], p[2]);
        break;
    case META_INVERTREGION:
        if (p[0] < n && ht[p[0]])
            InvertRgn(h, ht[p[0]]);
        break;
    case META_PAINTREGION:
        if (p[0] < n && ht[p[0]])
            PaintRgn(h, ht[p[0]]);
        break;
    case META_DELETEOBJECT:
        if (p[0] < n && ht[p[0]]) {
            /* GD9: the object goes even when it is selected */
            HANDLE obj = ht[p[0]];
            if (selected_kind(h, obj) == obj) {
                switch (obj_magic(obj) & MAGIC_MASK) {
                case PEN_MAGIC: SelectObject(h, stock[7]); break;
                case BRUSH_MAGIC: SelectObject(h, stock[0]); break;
                case FONT_MAGIC: SelectObject(h, stock[13]); break;
                case PAL_MAGIC: GDISelectPalette(h, stock[15], 0); break;
                }
            }
            DeleteObject(obj);
            ht[p[0]] = 0;
        }
        break;
    case META_CREATEPENINDIRECT:
        ht_put(ht, n, CreatePenIndirect((const LOGPEN __far *)p));
        break;
    case META_CREATEBRUSHINDIRECT: {
        LOGBRUSH lb = *(const LOGBRUSH __far *)p;
        if (lb.lbStyle == 3 || lb.lbStyle == 5)  /* (no bitmap here) */
            lb.lbStyle = 0;
        ht_put(ht, n, CreateBrushIndirect(&lb));
        break;
    }
    case META_CREATEFONTINDIRECT: {
        LOGFONT lf;
        WORD k = words * 2 < sizeof lf ? words * 2 : sizeof lf;
        mem_set(&lf, 0, sizeof lf);
        mem_copy(&lf, p, k);
        lf.lfFaceName[LF_FACESIZE - 1] = 0;
        ht_put(ht, n, CreateFontIndirect(&lf));
        break;
    }
    case META_CREATEPALETTE:
        ht_put(ht, n, CreatePalette(p));
        break;
    case META_CREATEREGION:
        ht_put(ht, n, region_of(p, words));
        break;
    case META_DIBCREATEPATTERNBRUSH: {
        HANDLE b = 0;
        if (p[0] == 3) {
            /* a bitmap's pattern: the bitmap made again */
            HBITMAP hbm, old;
            const void __far *bmi = p + 2;
            HDC s = dib_source(h, bmi, (const BYTE __far *)bmi + dib_head(bmi, 0), &hbm, &old);
            if (s) {
                SelectObject(s, old);
                b = CreatePatternBrush(hbm);
                DeleteObject(hbm);
                DeleteDC(s);
            }
        } else {
            DWORD sz = words > 2 ? (DWORD)(words - 2) * 2 : 0;
            HGLOBAL g = sz ? GlobalAlloc(GMEM_MOVEABLE, sz) : 0;
            if (g) {
                mem_copy(GlobalLock(g), p + 2, (WORD)sz);
                GlobalUnlock(g);
                b = CreateDIBPatternBrush(g, p[1]);
                GlobalFree(g);
            }
        }
        ht_put(ht, n, b);
        break;
    }
    case META_DIBBITBLT:
    case META_DIBSTRETCHBLT: {
        int st = mr->rdFunction == META_DIBSTRETCHBLT, hw = st ? 10 : 8;
        DWORD rop = MAKELONG(p[0], p[1]);
        if (!st && words == 9) {        /* no source */
            PatBlt(h, p[8], p[7], p[6], p[5], rop);
        } else if (words > hw) {
            HBITMAP hbm, old;
            const void __far *bmi = p + hw;
            HDC s = dib_source(h, bmi, (const BYTE __far *)bmi + dib_head(bmi, 0), &hbm, &old);
            if (s) {
                if (st)
                    StretchBlt(h, p[9], p[8], p[7], p[6], s, p[5], p[4], p[3], p[2], rop);
                else
                    BitBlt(h, p[7], p[6], p[5], p[4], s, p[3], p[2], rop);
                dib_source_free(s, hbm, old);
            }
        }
        break;
    }
    case META_SETDIBTODEV: {
        const void __far *bmi = p + 9;
        SetDIBitsToDevice(h, p[8], p[7], p[6], p[5], p[4], p[3], p[2], p[1],
                          (BYTE __far *)bmi + dib_head(bmi, p[0]), (void __far *)bmi, p[0]);
        break;
    }
    case META_STRETCHDIB: {
        const void __far *bmi = p + 11;
        StretchDIBits(h, p[10], p[9], p[8], p[7], p[6], p[5], p[4], p[3],
                      (const BYTE __far *)bmi + dib_head(bmi, p[2]), (void __far *)bmi, p[2],
                      MAKELONG(p[0], p[1]));
        break;
    }
    case META_ANIMATEPALETTE: {
        HPALETTE pal = selected_kind(h, stock[15]);
        if (pal)
            AnimatePalette(pal, p[0], p[1], (const PALETTEENTRY __far *)(p + 2));
        break;
    }
    case META_SETPALENTRIES: {
        HPALETTE pal = selected_kind(h, stock[15]);
        if (pal)
            SetPaletteEntries(pal, p[0], p[1], (const PALETTEENTRY __far *)(p + 2));
        break;
    }
    case META_RESIZEPALETTE: {
        HPALETTE pal = selected_kind(h, stock[15]);
        if (pal)
            ResizePalette(pal, p[0]);
        break;
    }
    default:                            /* not known: passed over */
        break;
    }
}

/* the records of a metafile, one by one; proc 0: played. GD9: the
   objects made are deleted at the end, and the DC's pen, brush, font and
   palette as they were before are selected again; a record is given to
   the callback as a copy (not in the metafile's own memory) */
static BOOL play(HDC h, HANDLE hmf, MFENUMPROC proc, DWORD lp)
{
    int tmp;
    HGLOBAL bits = mf_bits(hmf, &tmp), hht, hrec = 0;
    LPMH mh;
    HANDLE __far *ht;
    WORD n, i;
    DWORD at, size;
    BOOL ok = TRUE;
    HANDLE pen = 0, brush = 0, font = 0, pal = 0;
    LPDC dc;
    HRGN clip = 0;
    if (!bits)                          /* GD9: a disk metafile whose file */
        return IsValidMetaFile(hmf);    /* cannot be read plays (nothing) */
    mh = GlobalLock(bits);
    n = mh->mtNoObjects;
    size = mh->mtSize;
    if (size * 2 > GlobalSize(bits))
        size = GlobalSize(bits) / 2;
    hht = GlobalAlloc(GMEM_MOVEABLE | GMEM_ZEROINIT, (n ? n : 1) * sizeof(HANDLE));
    if (!hht) {
        GlobalUnlock(bits);
        if (tmp)
            GlobalFree(bits);
        return FALSE;
    }
    ht = GlobalLock(hht);
    dc = dc_get(h);
    if (dc && dc->hClipRgn) {
        /* GD9: the clip region as it was before is the DC's again after
           the play (the other attributes keep what the metafile set) */
        clip = rgn_new(0, 0, 0, 0);
        dc = dc_get(h);
        if (clip)
            rgn_copy(clip, dc->hClipRgn);
    }
    dc = dc_get(h);
    if (dc) {
        pen = dc->hPen;
        brush = dc->hBrush;
        font = dc->hFont;
        pal = dc->hPal;
    }
    at = mh->mtHeaderSize;
    while (at + 3 <= size) {
        /* each record as a copy (it may cross 64K; GD9: EnumMetaFile's
           callback gets a copy, not the metafile's memory) */
        DWORD rs = *(DWORD __far *)hp(mh, at * 2);
        WORD fn = *(WORD __far *)hp(mh, at * 2 + 4);
        LPMR c;
        if (!fn || rs < 3 || at + rs > size || rs * 2 > 0xFFF0L)
            break;
        if (!hrec || GlobalSize(hrec) < rs * 2) {
            if (hrec)
                GlobalFree(hrec);
            hrec = GlobalAlloc(GMEM_MOVEABLE, rs * 2);
            if (!hrec) {
                ok = FALSE;
                break;
            }
        }
        c = GlobalLock(hrec);
        huge_copy(c, 0, mh, at * 2, rs * 2);
        if (proc) {
            if (!proc(h, ht, c, n, lp)) {
                GlobalUnlock(hrec);
                ok = FALSE;
                break;
            }
        } else
            PlayMetaFileRecord(h, ht, c, n);
        GlobalUnlock(hrec);
        at += rs;
    }
    if (hrec)
        GlobalFree(hrec);
    /* the DC's own objects back, the metafile's deleted */
    dc = dc_get(h);
    if (dc) {
        if (dc->hPen != pen && obj_magic(pen))
            SelectObject(h, pen);
        if (dc->hBrush != brush && obj_magic(brush))
            SelectObject(h, brush);
        if (dc->hFont != font && obj_magic(font))
            SelectObject(h, font);
        dc = dc_get(h);
        if (dc->hPal != pal && obj_magic(pal))
            GDISelectPalette(h, pal, 0);
        dc = dc_get(h);
        if (dc->hClipRgn)
            rgn_free(dc->hClipRgn);
        dc->hClipRgn = clip;
        dc_rao_invalidate(dc);
    } else if (clip)
        rgn_free(clip);
    for (i = 0; i < n; i++)
        if (ht[i]) {
            DeleteObject(ht[i]);
            ht[i] = 0;
        }
    GlobalUnlock(hht);
    GlobalFree(hht);
    GlobalUnlock(bits);
    if (tmp)
        GlobalFree(bits);
    return ok;
}

BOOL GDIAPI PlayMetaFile(HDC h, HANDLE hmf)                        /* GDI.123 */
{
    if (!dc_get(h) && !mf_dc(h))
        return FALSE;
    return play(h, hmf, 0, 0);
}

BOOL GDIAPI EnumMetaFile(HDC h, HANDLE hmf, FARPROC proc, DWORD lp) /* GDI.175 */
{
    if (!proc)
        return FALSE;
    return play(h, hmf, (MFENUMPROC)proc, lp);
}

int GDIAPI FixupBogusPublisherMetafile(HANDLE h, DWORD d)          /* GDI.464 */
{
    (void)h;
    (void)d;
    return 0;
}
