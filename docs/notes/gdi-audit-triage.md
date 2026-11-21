# GDI audit triage

An outside, read-only audit of `gdi/` (written by another AI model; not in
the repository) listed 7 Critical, 17 High, 18 Medium and a grouped list
of Low findings (numbered L1-L12 here, in its order). Each is checked
here against the code, and against Windows 3.1 where the fix would change
what a program sees: the probe GD5 (`tests/apps/gd5.c`), run as the shell
of Windows 3.1 with Microsoft's GDI and with w16's
(`tests/gdiheap.sh [-r] -p build/dos/GD5.EXE`; the boot-test case
`gdiaudit` runs both and fails on any FAIL). Its table
`tests/apps/gd5ref.h` is `tools/obsref.py` of 3.1's log. Most of GD5 draws
into memory bitmaps and reads the pixels back, so it compares pixels
without screenshots; a few parts use a window of its own. GD6
(`tests/apps/gd6.c`, make test) checks GDI's 32-bit arithmetic. The
audit's line numbers were close but not exact.

Verdicts: **REAL** (confirmed; fixed unless noted), **FALSE** (the claim
is wrong, or GD5 shows w16 already as 3.1), **3.1-BEHAVIOUR** (3.1 does
the same: kept), **HARDENING** (only with malformed input or absurd
arguments; fixed only when cheap and invisible to working programs),
**UNSURE** (needs a probe not run yet). Where 3.1 itself ends Windows,
w16 may survive (the user's ruling); GD5 runs such steps on w16's GDI
only (`gdi_is_w16`, the module description).

Status: every finding has a verdict; "open" in the Action column marks
what is left (listed again at the end).

## Critical

| ID | Verdict | Evidence | Action | Commit |
|---|---|---|---|---|
| C1 | REAL | `__I4D` kept the signs in BP; `udiv`'s path for divisors >= 65536 clears BP and keeps the remainder's low word there, so `100001L / 70000L` gave -1. | BP saved around `call udiv`. Regression: GD6 (make test; links `gdi/i4.asm` in place of the library's: signed and unsigned division, remainder, multiply). The same code is in `ole/olecli/i4.asm` and `shell/write/i4.asm`: not GDI's, reported, not changed. | a67d15f |
| C2 | REAL | RemoveFontResource freed the FONTENT and its resource while a DC's `xFont` named it. SDK: the resource stays loaded while in use. | Removed entries leave the table at once and are freed when no DC has them realized (`font_reap`, from `font_realize` and DeleteDC); the module's FreeLibrary with its last one. GD5 "fonts": metrics, extent and TextOut after the removal, as on 3.1. | f9f3b72 |
| C3 | REAL | `spans_row`'s zero-width case let a span left or right of the single clip rectangle through inverted (a > b). | Only a zero-width span inside the clip goes through. GD5 "spans" (a thin polygon left of the clip, R2_COPYPEN and R2_NOT: bitmap untouched, as 3.1). | 068e873 |
| C4 | REAL | `bm_create`: `scan = wb * planes` in a word: 65536 gave 0 (`65536L / scan`: division by zero), more a short block. GD5: 3.1 makes a 32767 x 1 16-bit bitmap (65534-byte lines) and ends Windows in CreateBitmap(32767, 1, 4, 4) (65536). | A scan line of all planes over FFFFh bytes fails (3.1 crashes there; w16 survives). GD5 "bitmaps", "bigbitmaps" (past 65534 on w16 only). | 3f297e3 |
| C5 | REAL | Point buffers sized in a word: `(n + 1) * 4` wrapped for n >= 16383, `(4h + 8) * 4` for ellipses over 4094 rows, `2h` for round rectangles and arcs. GD5 on 3.1: Polyline of 16383 points draws; Polygon and CreatePolygonRgn of 16383 fail; Ellipse 8000 x 8000 draws, 10000 x 10000 and 20064 fails, 10000 x 64 draws; RoundRect 18064 draws. | `pts_alloc` refuses what one block cannot hold; Polyline goes in pieces; ellipse and round-rectangle outlines in pieces (`OUTLINE`); Polygon/CreatePolygonRgn of 16383 fail; Ellipse fails past half axes' product 2^24 (UNVERIFIED bound, fits every observation). GD5 "points", "shapes". | 068e873 |
| C6 | 3.1-BEHAVIOUR | GD5 on 3.1: SelectClipRgn(CreateRectRgn(0, 0, 32767, 32767)) on a window DC at (100, 80) returns NULLREGION, GetClipBox 0s, nothing drawn: 3.1 wraps too. w16 the same. | Kept (a saturating offset was tried and taken back). | - |
| C7 | REAL | DeleteDC of GetDCState's copy ran the DC teardown: `dev_close` of a device the copy holds no reference to (a printer device's count). Other DC calls on it reached `hRao` 0. GD5: 3.1 itself ends Windows in this DeleteDC. | DeleteDC frees the copy and its clip region only; output calls refuse it (`dc_out`), `dc_rao` gives it an empty region, Save/RestoreVisRgn check `hVisRgn`. GD5 "dcstate" (w16 only). | 8f6ebd4 |

## High

| ID | Verdict | Evidence | Action | Commit |
|---|---|---|---|---|
| H1 | FALSE | SelectObject realizes the new pen and font at once, so `hPPenOf`/`hFontOf` follow the selected handle; DeleteObject refuses a selected pen or font. GD5: a pen created with a deleted pen's handle draws in its own colour, as on 3.1. | None. | - |
| H2 | HARDENING | `pal_new`: `n * 4` wraps past 16383 entries (a LOGPALETTE over 64K). | Fixed: such palettes fail. | 05bac81 |
| H3 | 3.1-BEHAVIOUR | GD5 on 3.1: DeleteObject of a selected palette returns 1 and the palette is gone; SelectPalette back returns its handle. w16 the same. | Kept. | - |
| H4 | HARDENING | DeleteDC used the DC after USER's hook without checking it; Save/RestoreVisRgn on a DC with no visible region. | Fixed (checks). | 8f6ebd4 |
| H5 | REAL (by reading) | A memory DC's pen stayed realized for the previous bitmap's format (`dc_ppen` keyed on the handle only). | Realized again after a bitmap is selected. GD5 "colour" (white and red pens selected before a colour bitmap) as on 3.1. | 8f6ebd4 |
| H6 | REAL | GD5 on 3.1: with no clip region, ExcludeClipRect starts from the screen (in screen coordinates); w16 started from the device's size at the DC's origin, so a window off the screen's left lost its right part. | Starts from the screen's rectangle in the DC's device coordinates. GD5 "off-left window". (3.1 also keeps the clip region in screen coordinates, which GetClipRgn shows: open, see the end.) | 8f6ebd4 |
| H7 | 3.1-BEHAVIOUR | GD5: MM_ISOTROPIC extents on 3.1 equal w16's in every case tried, including 640/640 (no 639) and the 0 viewport extent of 32000:1 against 1:32000. | Kept. | - |
| H8 | REAL | GD5 on 3.1: ScaleWindowExt/ScaleViewportExt to 0 return 0 and keep the extents; a zero divisor gives 7FFFh (and returns the old extents). w16 set 0 (mapping then divided by 0). | As 3.1 (`scale_ext`; the sign of 7FFFh for a negative product UNVERIFIED). | 8f6ebd4 |
| H9 | REAL | CreateBitmap's initial bits: `bits + y * scan` wrapped within one segment past 64K. | Copied as SetBitmapBits copies (`bits_copy`). GD5 "huge" (8000 x 100 mono, read back equal, as 3.1). | 3f297e3 |
| H10 | UNSURE | CreateCompatibleBitmap on a memory DC sets `bmlpPDevice` to the selected bitmap's PBITMAP; the code notes 3.1 seemed to do so (GD1, UNVERIFIED). VGA.DRV does not follow it in any case GD5 tries. | open: needs a GD1 log of 3.1's CreateCompatibleBitmap on a memory DC. | - |
| H11 | FALSE | GD5: BitBlt past and before a memory bitmap source gives the same pixels on w16 as on 3.1. | None. | - |
| H12 | HARDENING | 2.x font conversion: no checks of the character range, bits offset or new size; resource kept on failure. | Fixed: such a font is not converted (no font); its blocks freed. | f9f3b72 |
| H13 | REAL | Fonts were matched by the first 12 characters of the string given (a path never matched); a second AddFontResource loaded the file again and one RemoveFontResource removed both. GD5 on 3.1: added again, a file stays until removed twice. | Matched by file name without its path; a file already in the table is counted, not loaded again. GD5 "fonts". | f9f3b72 |
| H14 | REAL | GD5 on 3.1: in MM_LOMETRIC and 2:1 anisotropic modes the font is chosen by its device size. | `font_realize` scales lfHeight/lfWidth to device units (cache keyed on them too, `fontHt`/`fontWd` in DC fields w16 did not use). | f9f3b72 |
| H15 | REAL | GD5 on 3.1: GetTextMetrics gives logical units, positive (descent = height - ascent); w16 gave a negative height and device units. | As 3.1. | f9f3b72 |
| H16 | REAL | StretchBlt compared logical sizes and BitBlt'ed with the destination's logical size in the source DC. | Each rectangle mapped by its own DC; a mirror where the device extents' signs differ. GD5 "StretchBlt x2 source" as 3.1. | 8f6ebd4 |
| H17 | REAL | GD5 on 3.1: GetDIBits without bits fills the colour table; with a core header the colours come back as RGBTRIPLEs; DIB_PAL_COLORS gives palette indices (0, 13h); SetDIBitsToDevice returns the scan lines on a window DC and FFFFh on a memory DC (the pixels the same). | All as 3.1 (colour copy, `dib_colors_back`; DIB_PAL_COLORS translated through the DC's palette both ways). The PBITMAP given to DIBScreenBlt on a memory DC gives 3.1's pixels: kept. | 05bac81 |

## Medium

| # | Verdict | Evidence | Action | Commit |
|---|---|---|---|---|
| 1 | HARDENING | NULL structure pointers to Create*Indirect, SetPaletteEntries: a program error. | Not fixed. | - |
| 2 | HARDENING | `dev_open` kept the driver's library on failure; `dev_close` trusts Disable; 15-character name. | The library freed on failure; the rest not. | 05bac81 |
| 3 | UNSURE | Escape on a memory DC passes the PBITMAP to Control. | open: needs 3.1's argument observed (GD1). | - |
| 4 | REAL | GD5 on 3.1: SetPaletteEntries on DEFAULT_PALETTE returns 0 and changes nothing. | As 3.1. | 05bac81 |
| 5 | 3.1-BEHAVIOUR (part REAL) | GD5 on 3.1: RectVisible (GDI.104) and RectInRegionOld say an empty or inverted rectangle inside is visible, as w16. But 3.1's GDI.104 returns 0101h, w16's 1. | Kept; RectVisibleOld returns 0101h. | 8f6ebd4 |
| 6 | REAL (part) | GD5 on 3.1: CreateCompatibleDC of a handle that is no DC returns 0 (w16 made a display DC). The display's count was raised twice when CreateCompatibleDC opened it. The initial visible region is clipped to the bitmap anyway (FALSE part). | Fails; counted once. CreateDC's device on OOM: not fixed (HARDENING). | 8f6ebd4 |
| 7 | FALSE | GDI's heaps are moveable segments whose selectors stay while they grow (gdi.h's heap comment); fixed blocks do not move inside them, so far pointers stay good across allocations. | None. | - |
| 8 | HARDENING | `combine` started its sweep at -32767: a band at -32768 was lost. (GD5: a region at -32768 behaves the same on both, its box from the rectangle.) | Fixed: from -32768. | 8f6ebd4 |
| 9 | HARDENING | `x - wndOrg.x` overflows in 16 bits for coordinates 32K apart. | Not fixed. | - |
| 10 | REAL (part) / 3.1-BEHAVIOUR (part) | GD5 on 3.1: a hatched brush's background follows SetBkColor after selection; w16 kept the colour of its realization. Brush origin: the SDK requires UnrealizeObject before the brush is selected again. | Brushes realized again when the physical background colour changes (SetBkColor, RestoreDC/SetDCState's refresh); origin kept. | 8f6ebd4 |
| 11 | REAL (part) | SelectClipRgn, IntersectClipRect, ExcludeClipRect, OffsetClipRgn returned the composite region before USER's DC hook updated the visible region. OOM in region code ignored: HARDENING. | The hook is called first (`dc_validate`); OOM not fixed. | 8f6ebd4 |
| 12 | HARDENING | `wide_frame` gave overlapping spans when the sides meet. | Fixed: one span. | 068e873 |
| 13 | REAL | `ellipse_row`: `102 * B * B` overflowed past 4588 rows; `r - l` in 16 bits. | 32-bit arithmetic (1.02 B^2 as B^2 + B^2 / 50, the same integers). GD5 "big Ellipse" filled. | 068e873 |
| 14 | REAL (part) | GD5 on 3.1: ExtTextOut with TA_UPDATECP and dx moves the position by the dx widths before the last character plus its own width, left for TA_RIGHT. BreakErr through `drv_extent`: UNSURE (not observed). Uninitialised `orc`: FALSE (the options are cleared without a rectangle). | Position as 3.1 (`text_width`); BreakErr open. | f9f3b72 |
| 15 | REAL | GD5 on 3.1: GetCharWidth in logical units. | As 3.1. | f9f3b72 |
| 16 | HARDENING | Leaks: FONTDIR lock and resource on failure, a font-less file's library, `ellipse_outline`'s block when h <= 0. | Fixed. | f9f3b72, 068e873 |
| 17 | REAL | GD5 on 3.1: PolyPolygon fills the polygons together and does not close them (fill nor outline); Arc/Pie/Chord with the same radial draw the whole ellipse; Rectangle with no width or height draws nothing. Negative counts: HARDENING. | All as 3.1; negative counts fail. | 068e873 |
| 18 | HARDENING | NULL pointers to Get/SetBitmapBits, CreateBitmapIndirect, FastWindowFrame, text calls: program errors. | Not fixed. | - |

## Low

| # | Verdict | Evidence | Action | Commit |
|---|---|---|---|---|
| L1 | REAL | GD5 on 3.1: DeleteMetaFile(0) returns 0. | As 3.1. | 05bac81 |
| L2 | REAL (part) | GD5 on 3.1: GetRasterizerCaps writes nSize 6, flags 3, language 0409h, at most n bytes; returns 0409h. | nSize and language as 3.1; flags 0 (w16 has no TrueType rasterizer: by design); returns 1 (nonzero as 3.1). | 05bac81 |
| L3 | FALSE | GetObject of a bitmap: every bitmap object has its PBITMAP block (`bm_create` fails otherwise). | None. | - |
| L4 | FALSE | DeleteObject of metafile objects: w16 makes none. | None. | - |
| L5 | HARDENING | `gdi_libentry` ignores LOCALINIT failing. | Not fixed. | - |
| L6 | HARDENING | `driver_stock_fonts` with face names of 32 or more characters (the display driver's own resource). | Not fixed. | - |
| L7 | FALSE | ExtendedGdiHeap's table index wraps only past 65535 objects; their stubs in DGROUP run out long before. | None. | - |
| L8 | UNSURE | `dm.CharExtra` is scaled when set, not when the mapping changes. | open. | - |
| L9 | HARDENING | Get/SetBoundsRect with a NULL rectangle (USER's calls pass one). | Not fixed. | - |
| L10 | HARDENING | NULL checks in region and DC calls. | Not fixed. | - |
| L11 | HARDENING | 1064-byte colour tables on the caller's stack (dib_bits, SetDIBitsToDevice). | Not fixed. | - |
| L12 | REAL | GD5 on 3.1: SetMapperFlags keeps the whole DWORD. | As 3.1. | f9f3b72 |

## Counts

| Severity | REAL | FALSE | 3.1-BEHAVIOUR | HARDENING | UNSURE |
|---|---|---|---|---|---|
| Critical (7) | 6: C1, C2, C3, C4, C5, C7 (all fixed) | 0 | 1: C6 | 0 | 0 |
| High (17) | 9: H5, H6, H8, H9, H13, H14, H15, H16, H17 (all fixed) | 2: H1, H11 | 2: H3, H7 | 3: H2, H4, H12 (all fixed) | 1: H10 |
| Medium (18) | 9: 4, 5, 6, 10, 11, 13, 14, 15, 17 (fixed; 5, 6, 10, 11 and 14 in part, see their rows) | 1: 7 | 0 (5 and 10 are in part) | 7: 1, 2, 8, 9, 12, 16, 18 (2 in part, 8, 12, 16 fixed) | 1: 3 |
| Low (12) | 3: L1, L2, L12 (fixed; L2 in part) | 3: L3, L4, L7 | 0 | 5: L5, L6, L9, L10, L11 | 1: L8 |

## Beyond the audit (found on the way)

- GetClipRgn (GDI.173): 3.1 keeps a DC's clip region in screen
  coordinates (GD5: IntersectClipRect(10, 10, 20, 20) on a window at
  (100, 80) gives a region at (110, 90)); w16 keeps it in the DC's device
  coordinates, so the region GetClipRgn returns is offset by the DC's
  origin. Drawing is the same. open (GD5 records it unchecked): moving
  the storage to screen coordinates touches SetDCOrg, SaveDC and USER's
  DC cache, and needs SetDCOrg with a clip region observed on 3.1 first.
- The font mapper: CreateFont(-11, ..., "Small Fonts") gives a font of
  tmHeight 12 on 3.1, 11 on w16 (the mapper's weights are UNVERIFIED, not
  part of this audit; GD5 records it unchecked).
- DIBs (2026-11-20, QuickTime's installer banner, probe GD7 and the
  boot-test case `gdidib`): StretchDIBits (GDI.439) was a stub returning
  0, so Write's and OLE's pictures were never drawn; it is SetDIBits into
  a bitmap and StretchBlt now. H17's "kept" for SetDIBitsToDevice on a
  memory DC held only with Microsoft's VGA.DRV, which draws nothing there
  as 3.1's GDI never does; with w16's driver w16 drew: GDI now draws
  nothing and returns FFFFh. On a window DC it returns the driver's value
  (the lines drawn inside the clip rectangle). RLE and DIBs over 64K are
  the driver's (drivers/vga/README.md).
- `ole/olecli/i4.asm` and `shell/write/i4.asm` have C1's bug (copies of
  GDI's file); not changed here (other areas).

## Tests

- GD5 on Windows 3.1 (Standard mode) with Microsoft's GDI and with w16's
  (boot-test case `gdiaudit`): both complete; w16's log has no FAIL
  against 3.1's table. The unchecked differences (`OBS ~`) are the ones
  listed above: GetClipRgn's coordinates, the font mapper's Small Fonts
  size, GetRasterizerCaps' TrueType flags and return value.
- Full boot test at e21d6b5 (installation-05, 13 jobs): 125 ok, 2
  failures, both known on master 429e079: ddeml (conn), lzexpand
  (quirks). gdi, gdik, usergdi, vga*, tree, write, cardfile, control and
  cplclick passed (pixel comparisons as on master).
- make test at e21d6b5: everything passed but GD6, whose table had one
  hand-computed value wrong (FFFFFFFFh / 12345h); fixed (4a968c8), make
  test passes (both kernels, HDPMI16 and DOSX; GD6 included).

## Left open

- H10 (bmlpPDevice for a memory DC's compatible bitmap), Medium 3 (Escape's
  device pointer on a memory DC), Medium 14's BreakErr, L8 (CharExtra and
  mapping changes): each needs 3.1 observed (GD1 logs or a GD5 section).
- GetClipRgn's coordinates (above).
- The HARDENING items marked "Not fixed".
