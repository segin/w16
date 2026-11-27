# GDI audit triage: blt, bitmaps, drawing, metafiles

A second outside, read-only audit of `gdi/` (written by another AI model;
not in the repository) covered correctness and speed. Its findings for
this part of GDI (`gdi/blt.c`, `bitmap.c`, `draw.c`, `meta.c`) were each
checked by independent readers against the code; this file has their
verdicts, condensed, and what was done. The audit's other parts of GDI are
triaged separately.

Verdicts: **REAL** (confirmed; fixed), **FALSE** (the claim is wrong),
**3.1-BEHAVIOUR** (3.1 does the same: kept), **HARDENING** (only with
malformed input or absurd arguments; fixed when cheap and invisible to
working programs), **PARTLY** (some sub-claims hold: each part's verdict
in its row), **UNSURE** (needs a probe on 3.1 not run yet). Speed
findings: **WORTHWHILE** (done), **NOT-WORTHWHILE** (left). Where 3.1
itself crashes, w16 may survive. The audit's line numbers were close but
not exact.

## Defects

| ID | Verdict | Evidence | Action | Commit |
|---|---|---|---|---|
| C3 | REAL | `write_file` advanced with `hp(p, k)`, which keeps only `k` as the offset: past the first 64K every 32K piece wrote bytes 8000h-FFFFh of the first segment again. The file's length was right, its contents past 64K wrong, the write reported success. Reached by CloseMetaFile of a disk metafile over 64K and by CopyMetaFile to a file. | Pieces from the block's base and a running DWORD offset (32K pieces never cross 64K). | fb430a0 |
| C4 | PARTLY: REAL (wrong result), FALSE (the #GP) | The record size `*(DWORD __far *)hp(mh, at * 2)` at an offset ending in FFFEh read its high word from offset 0 of the same selector (16-bit wrap, no fault). A junk high word made the size huge: PlayMetaFile and EnumMetaFile stopped silently at that record, CopyMetaFile to a file gave the whole size. Reached by any metafile over 64K whose record starts at FFFEh. | Both words through `hp()` (`hp_dword`) in `words_to_eof` and `play`. | fb430a0 |
| C5 | REAL (Polygon/Polyline 32766-32767 points, ExtTextOut 32767 characters with widths), HARDENING (PolyPolygon's negative counts) | Word counts computed in 16 bits before `mf_begin`'s DWORD: `1 + 2 * n` wrapped to 0 or FFFFFFFFh, `(n + 1) / 2` overflowed; the scratch block was sized from the wrapped value and the copies overran it (GDI's heap). Negative PolyPolygon counts sign-extended into the total. | Sizes in DWORD (`mf_poly`, `mf_exttext`, `mf_polypoly`); PolyPolygon refuses negative counts; `mf_begin` checks the record's size first. Such records fail as every record over 64K does (11.8, UNVERIFIED on 3.1). | fb430a0 |
| C6 | FALSE | `blt_out` and `stretch_blt` pass the source unclipped, but w16's VGA.DRV BitBlt clips the source itself (in `long`: a negative or past-the-edge source moves the destination and shrinks the width), `stretch_row_1bpp` reads only inside the bitmap, and the screen row path zeroes its buffer first. No read outside a bitmap is reachable with w16's driver. Microsoft's VGA.DRV (comparison builds only) is UNVERIFIED there; clipping in GDI would only be hardening, the same pixels. | None. | - |
| H7 | HARDENING | PlayMetaFileRecord indexed its parameters without checking `rdSize` (fixed-shape records, the point counts of POLYGON/POLYLINE/POLYPOLYGON, ESCAPE's count, the palette records' counts, the DIB records' headers; TEXTOUT's and EXTTEXTOUT's word counts wrapped at FFFFh). `play` never passes NULL and copies each record into a block of its size, so only a damaged metafile or a program's own record reaches it. | A NULL record returns; a record shorter than its function's parameters, counts or DIB header and colour table is passed over as an unknown one is (`rec_min`, `rec_fits`, `dib_fits`); the counts in 32 bits (also CREATEREGION's band). A DIB's bits are not checked (an RLE DIB's size is not known from its header). | fb430a0 |
| H8 | HARDENING | `mtSize * 2 > GlobalSize` wrapped for an mtSize of 2 GB or more (only a crafted block or file): `play` and `words_to_eof` then read past the block. The missing header check on a disk file's own header is as 3.1 (GD9: GetMetaFile does not check it): kept. | Compared as `mtSize > GlobalSize / 2`; also `play`'s record-size check without a multiplication. | fb430a0 |
| H9 | PARTLY: HARDENING (the read), FALSE (the buffer write), 3.1-BEHAVIOUR (the write-back) | `dib_info` read `1 << bpp` entries of a DIB_PAL_COLORS table whatever biClrUsed said (an overread of the bits after a short table, a fault only at a segment's end; colours right since the driver uses biClrUsed entries). The buffer holds 256 entries: no overflow. `dib_colors_back` writes the full table as the driver's GetDIBits does (VG7): kept. | `dib_info` reads biClrUsed entries of a BITMAPINFOHEADER's table, zeros the rest. | fb430a0 |
| H10 | PARTLY: HARDENING (NULL), REAL (64K crossing) | On a metafile DC SetDIBitsToDevice and StretchDIBits recorded before checking for a NULL BITMAPINFO or bits (a fault at selector 0). The bits were copied with `mem_copy`, which wraps inside one selector: a band of a DIB over 64K given as a huge pointer at, say, offset 9000h recorded the wrong scan lines. | NULL fails (0) in `mf_dibtodev`/`mf_stretchdib`; the bits copied with `huge_copy` from the huge pointer (`dib_bits_copy`). The record's bytes are what 3.1 records. | fb430a0 |
| M9 | REAL | `ellipse_row`: `l + r - 1` added in 16 bits (Ellipse(1000, 0, 32000, 400): the middle row empty, it is full); `B * B` and the A side's second scaling overflowed 32 bits past about 45900 (boxes the 2^24 product limit lets through, e.g. 2 x 64000). Wrong pixels only (spans are clamped and clipped). No probe shows 3.1 misdrawing these. | The centre and the clamps in 32 bits; a side over 45000 computed at half size (B with Y; A with the result doubled: a pixel or two at that size). Every row of 20000 boxes up to 30000 wide at random places the same as before where the old code did not overflow (host comparison). | 8a6f176 |
| M10 | REAL | `fill_polys` and CreatePolygonRgn computed an edge's x with `y - a.y`, `b.x - a.x` and `b.y - a.y` in 16 bits: points over 32767 apart (MM_TEXT (0,-20000)-(100,20000)) gave x -78 for 50 on row 0. `wide_polyline`'s direction likewise. The old Medium 9 (16-bit mapping arithmetic, as 3.1) is a different place: these are device coordinates already mapped. | `edge_x`: the differences in 32 bits, the product as an unsigned magnitude (it fits: under 2^32), rounded as before (halves away from the edge's start), clamped to the int range. Wide pens: the direction in 32 bits, halved until its squares fit, the corners clamped. 2 million random edges within 16K the same as before (host). | 8a6f176 |
| M11 | PARTLY: FALSE (records over 64K), HARDENING (scratch OOM) | A record over 64K is not written, by design (ARCHITECTURE.md 11.8, UNVERIFIED on 3.1); failing the whole metafile there would be no closer to 3.1. No room for the scratch record dropped one record and kept the metafile, while no room for the metafile's block fails it. | The scratch block's failure fails the metafile too (`m->failed`). The over-64K limit kept. | fb430a0 |
| L7 | PARTLY: REAL (a), UNSURE (b, c), FALSE (d) | (a) ExtFloodFill/FloodFill did nothing on a real DC (documented open work; 3.1 fills through the driver's ScanLR). (b) GetDIBits with biBitCount 0 or negative sizes: tagged UNVERIFIED; no corruption reachable. (c) GetMetaFileBits of a disk metafile: IsValidMetaFile accepts mtType 2 as 3.1 (probe-confirmed); the SDK says the function is for memory metafiles but nothing shows 3.1 refusing. (d) `stretch_blt` uses the destination's device info for a device source: the same driver in every real case, and the destination driver's BitBlt is used anyway. | (a) Implemented (see "FloodFill" below): VG8 on 3.1 gives the same pixels and return values with w16's GDI as with Microsoft's. (b), (c): no change; to probe on 3.1: GetDIBits with a BITMAPINFOHEADER of biBitCount 0 (what it fills, its return) and with negative biWidth/biHeight; GetMetaFileBits on GetMetaFile's handle (its return, and whether the handle still plays). (d) none. | 8a6f176 |

## Speed

| ID | Verdict | Evidence | Action | Commit |
|---|---|---|---|---|
| P6 | PARTLY (row reuse WORTHWHILE; midpoint stepping not) | `ellipse_row` (two 32-bit scalings and a binary search: some thousands of cycles on a 286) ran about 4 times a row for a filled, outlined ellipse (fill, both outline sides, the neighbouring rows), 3 times a row for RoundRect's corners, twice for arcs; row y and its mirror t + b - 1 - y are the same. Replacing the fitted 1.02 predicate with midpoint stepping would change GD3-matched pixels: not done. | `EROWS`: the rows t .. middle computed once per shape into a table (POINT pairs from GDI's heap; direct calls if no room or past PTS_MAX), used by Ellipse, RoundRect (every middle row is one entry), Arc/Pie/Chord and CreateEllipticRgn. Host: Ellipse's row work 200 x 200 0.061 -> 0.011 ms, 640 x 480 0.168 -> 0.028 ms (5.5-6x); the same spans and polylines (hash of 11 sizes including GD3's 60 x 30, 7 x 5, 20 x 20). | 8a6f176 |
| P7 | NOT-WORTHWHILE | StretchBlt's per-pixel fallback runs only for 8- or 24-bit memory bitmaps, which VGA (w16's target) never makes; `bm_scan`'s multiply is under 1% of a row's work. | None. | - |
| P8 | WORTHWHILE | CreateEllipticRgn and CreatePolygonRgn OR'ed each row (each span) into the region: every OR merges the whole region again and reallocates it, O(H^2) rectangle visits (about 115000 for a 480-row ellipse). | `RBUILD` in `draw.c` (rgn.c is another part's): rows appended in rgn.c's banded form (a row's touching spans one rectangle; a row equal to the band ending at its top extends it), the box as `rgn_fix_box` makes it, handed over with `rgn_set_data`. Host (rgn.c itself): CreateEllipticRgn 400 x 400 1.34 -> 0.016 ms, 640 x 480 2.25 -> 0.021 ms, CreatePolygonRgn of a 200-row star 0.69 -> 0.015 ms; the rectangles identical for 6000 random polygons and ellipses and the fixed cases. META_CREATEREGION's playback (`region_of`) is left: its bands are already merged. | 8a6f176 |
| P9 | PARTLY (the row clamp WORTHWHILE; the rest not) | `fill_polys` walked every row of the polygon's extent; rows outside the composite region's box make no driver call. Caching brush, pen and region pointers would break the heap rule (no far pointer across a driver call) for little; one bounds union and a DDA: marginal or risky. | Rows clamped to the composite region's box (the same driver calls). Host: a polygon from y -20000 to 20000 on a 480-row region 1.39 -> 0.021 ms. | 8a6f176 |

## FloodFill (L7 a)

ExtFloodFill on a real DC finds the area 4-connected to the point (up to
the colour for FLOODFILLBORDER, of the colour for FLOODFILLSURFACE) with
the driver's ScanLR (DDK: style 1 a pixel of the colour, 2 leftwards; -1
none, 8000h off the surface), marking it in a bitmap of the composite
region's box, then paints the marked runs as scan lines with the brush,
clipped to the region; so the brush's own colours never stop or extend
the fill. FALSE when the point is outside the clip region, not in such an
area, the brush is hollow, the driver has no ScanLR, or no memory.
Probe VG8 (`tests/apps/vg8.c`: FloodFill inside a black outline with a
diagonal inside it, then ExtFloodFill FLOODFILLSURFACE of a gray area,
each result read back pixel by pixel), run on Windows 3.1 as the shell
(`tests/vgaprobe.sh -p build/dos/VG8.EXE`, 2026-11-27) with Microsoft's
GDI and VGA.DRV, with w16's GDI and Microsoft's VGA.DRV (`-f
build/dos/GDI.EXE=WINDOWS/SYSTEM/GDI.EXE`), and with w16's GDI and VGA.DRV
(`-w` as well): the three logs' fill sections are identical, both return
values 1 and every pixel. So Microsoft's ScanLR counts the start pixel as
w16's does. UNVERIFIED still: 3.1's fill where the area touches the clip
region's edge (here the search covers the region's box) and fills through
diagonal gaps (VG8's areas have none).

## Tests

- `make test` (worktree, 2026-11-27): every check before the program
  runs passed; the DOSX runs all ok on both kernels (79 each, GD6
  included); the HDPMI16 runs fail only because `.tools/hx` is absent in
  a worktree.
- Boot test on installation-05 at 8a6f176, cases gdi, gdik, usergdi,
  gdiaudit (GD5, GD8 against 3.1's tables), gdimeta (GD9: every record's
  bytes, disk files, playback), gdidib, gdifont, gdiconv, writefonts,
  staroff, vga, vgaall: 12 ok, 0 failures.
- Host comparisons (a harness compiling rgn.c and the changed draw.c
  functions with a stand-in gdi.h, the target's 32-bit long, old against
  new; not kept): the same rectangles, spans and polylines as listed in
  the M9, M10, P6, P8 and P9 rows.
- VG8 on 3.1 (FloodFill, above).

## Left open

UNSURE L7 (b), (c): the probes listed in its row. Records over 64K in
metafiles (11.8). The bits of DIB records in PlayMetaFileRecord are not
checked against `rdSize`.
