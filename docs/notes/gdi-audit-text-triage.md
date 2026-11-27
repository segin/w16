# GDI audit triage: text, fonts and the TrueType bridge

An audit of w16's GDI (`gdi/`, written by another AI model, not in the
repository) was split by subsystem; this note covers the text subsystem:
`gdi/text.c` (TextOut, ExtTextOut, extents, metrics), `gdi/font.c` (the
font table, mapping, scaled raster copies, enumeration) and
`gdi/ttfont.c` (the bridge to the TrueType rasteriser in `gdi/ttf/`).
Each finding was checked against the code by an independent reviewer and
again here; IDs are the audit's (C critical, H high, M medium, L low, P
performance).

Verdicts: **REAL** (confirmed on a reachable path; fixed), **HARDENING**
(only with a malformed argument or font; fixed because the fix is cheap
and changes nothing 3.1 shows), **PARTLY** (a part holds: that part
fixed), **FALSE** (the claim is wrong), **UNSURE** (needs a probe on
3.1). Performance: **WORTHWHILE** (done, output bit for bit the same,
measured) or **NOT-WORTHWHILE** (left). Where 3.1 would crash, w16 may
survive without a probe.

Status: every finding has a verdict and is closed (2026-11-27).

## Defects

| ID | Verdict | Evidence | Action | Commit |
|---|---|---|---|---|
| C1 | REAL | ttfont.c tt_bc_get kept each size's bytecode state (fpgm/prep run) in a GDI-wide cache of four, keyed by (TTFILE, ppem, xppem), but allocated it `GMEM_MOVEABLE` without `GMEM_SHARE`. w16's KERNEL (as 3.1's) gives such a block to the current task's PDB and frees it when the task ends (gfree_owner). Task A draws a size and ends; task B draws the same size, hits the cache and locks a freed (or reused) handle: hinting silently falls back, or ttfx_bcUse reads another task's block; eviction then GlobalFrees a handle that may be someone else's. On by default (TrueTypeHinting 29 has the bytecode bit). SHELL had the same pitfall (its tables now GMEM_DDESHARE). | Allocated `GMEM_MOVEABLE \| GMEM_SHARE`, as GDI's other long-lived blocks (the file cache, the font blocks, the heaps). The arena (tt_arena_h) is freed within the call: not affected. | 3175aa3 |
| C2 | PARTLY: REAL (realloc), HARDENING (lock) | font_scale and tt_info allocate only the header, table and face; the table's glyph offsets already run from there to the full size, which the block gets at first draw (Clock's 1000-pixel font would need 800K). When font_bits' or tt_glyphs' GlobalReAlloc failed they re-locked the small block and returned; ExtTextOut called the driver anyway, which reads glyph bytes at offsets past the selector's limit (VGA.DRV build_row): a #GP, not the "drawn blank" the comment said. Out of memory with a large raster scale or TrueType size reaches it. GlobalLock of a valid, moveable, non-discardable block cannot fail in protected mode: the NULL check is only defensive. | font_bits and tt_glyphs return BOOL (gdi.h): FALSE when the block cannot grow (tried again next draw) or the lock fails; ExtTextOut returns FALSE before the driver call. tt_glyphs' later early return (the file or arena not opened) comes after the block has grown and is zeroed: TRUE, really drawn blank. | 3175aa3 |
| H3 | PARTLY: REAL (no clamp), FALSE (#GP) | GetFontData read in pieces of up to 8000h bytes to font_huge(buf, done) without limiting a piece to the room left in its segment. A huge buffer at a nonzero offset (sel:0004) with more than FFFCh bytes asked: the second piece starts at sel:8004 and runs 4 bytes past the segment. No #GP: KERNEL's _lread stops at the segment's end, so ttfx_Read zero-fills the shortfall through a far (not huge) pointer, whose offset wraps: sel:0000-0003, before the caller's buffer, are zeroed, and the next tile's first 4 bytes are never written; the return still says every byte was copied. | Each piece limited to `0x10000 - OFFSETOF(dest)` (and 8000h). Any buffer now gets exactly the bytes asked; nothing 3.1 shows changes. | 3175aa3 |
| H4 | HARDENING | CreateScalableFontResource builds the .FOT in a fixed 600h-byte block; the CCh resource is the .TTF name rounded up to 16 (at least 128) at 400h, the FONTDIR after it (up to 310 bytes with 63-character names): a name of about 192 characters or more overruns the block (the name's copy, the FONTDIR, then _lwrite reads past it). Reaching it needs tt_open to succeed on the path cut at 127 characters, i.e. a valid .TTF name padded with junk: no real Win16 path is that long, and 3.1's OpenFile takes 128 bytes at most. | FALSE for a name of 128 characters or more. | 3175aa3 |
| H5 | PARTLY: REAL (truncation), FALSE (#GP) | font_scale read the base font's face at `src + (WORD)bf->dfFace`. In a 3.x raster font over 64K (what the 3.x format is for, and font_bits already reads such fonts' bits through font_huge) the face follows the bits past 64K, so the cast pointed into tile 0 (header, table, bits): the scaled copy got a garbage face string, and a long garbage run could make `0x94 + n*6 + flen` pass 64K and wrap the copy over the new header. Not visible through the API (GetTextFace and the mapper use the FONTDIR's face; VGA.DRV does not read dfFace). No #GP: tile 0 of a block over 64K has limit FFFFh and the far offset wraps inside it; the header has NUL bytes, so the scan ends. | The face read through font_huge at its 32-bit offset, at most LF_FACESIZE - 1 characters, copied byte by byte. | 3175aa3 |
| H6 | REAL | A font added by module handle (AddFontResource(MAKELP(0, hInst))) has an empty file name. AddFontResource("") or a path ending in a separator gives base_name "" (non-null), which matched every such entry: a nonzero count without loading anything, and the entries' add counts raised. RemoveFontResource("") unlinked them with `unload` set, so font_free called FreeLibrary on the caller's module, which GDI never loaded: its usage count dropped and it could unload while in use. Valid API calls, no validation. | An empty name matches no entry (font_count and RemoveFontResource require `f->file[0]`): Add then tries LoadLibrary("") and fails with 0, Remove returns FALSE; `unload` can no longer be set on a module-handle entry. | 3175aa3 |
| M5 | FALSE | ExtTextOut used the LPDC fetched before `xalloc(n*2)` in the dx copy loop. The DC body is a fixed block (LMEM_FIXED in GDI's heaps, or in DGROUP with ExtendedGdiHeap=0), xalloc only adds blocks, and a heap's segment keeps its selector while it stays under 64K: the pointer stays valid; dc_scale_x only reads. | None (P13 moved that code anyway). | - |
| M6 | HARDENING | tt_ppem divides by `winA + winD + gap`: 0 only for a font whose OS/2 usWin values and hhea metrics are all zero (a divide fault in GDI). With usWin values that add to 0 (or negative, a value of 8000h or more) and a normal hhea, tt_vert gives a height of 0 at every size and the search `p++` runs about 2^31 times (GDI hangs). A simulation of the loops at every height 32000-32767 for Arial, Times New Roman and Courier New metrics ends quickly: no runaway for a conforming font. Malformed fonts only (AddFontResource accepts them). | tt_load: usWin values that add to 0 or less are replaced by hhea's, else by the em and 0, so the divisor is positive and heights grow with the size; tt_ppem bounds the size at 7FFFh in both directions of the search. Conforming fonts unchanged. | 3175aa3 |
| M7 | REAL | GetCharABCWidths and GetKerningPairs returned device pixels whatever the mapping mode. SDK vol. 2 (GetCharABCWidths: "The widths are returned in logical units") and vol. 3 (KERNINGPAIR.iKernAmount: "in logical units-that is, it depends on the current mapping mode"); w16's GetCharWidth, GetTextExtent and GetTextMetrics already convert (GD5 on 3.1); the code's own comment marked device units UNVERIFIED. GD13 and FN4 run in MM_TEXT, where the values were right. | Both convert along x when the mapping mode is not MM_TEXT, keeping the sign (A, C and kerning amounts can be negative): `v * \|wndExt.x\| / \|vpExt.x\|` rounded (mul_div). C is the converted advance less the converted A and B, so A + B + C is GetCharWidth's value. MM_TEXT unchanged. | 3175aa3 |
| M8 | PARTLY: REAL (invalid DC), UNSURE (empty or NULL string) | GetTextExtentPoint returned TRUE with 0 by 0 when GetTextExtent failed for a handle that is not a DC (SDK: zero when it fails); a metafile DC too (ARCHITECTURE.md's metafile probe has the queries fail on one; GetTextExtentPoint itself not probed). For n <= 0 or a NULL string no document or probe says what 3.1 returns (GetTextExtent with a count of 0 returns 0 without a driver call; an empty string's extent as 0 by 0 with TRUE is plausible). | FALSE when there is no DC (dc_get, which also rejects a metafile DC). n <= 0 and NULL kept as they were. To probe on 3.1: GetTextExtentPoint(hdc, "x", 0, &sz) and (hdc, NULL, 1, &sz) on a screen DC (the return and sz), and on a metafile DC. | 3175aa3 |
| L5 | HARDENING | GetCharWidth's italic-overhang and logical-unit loops used `WORD i; i <= last - first`: with first 0 and last FFFFh the test is always true and the loop never ends, writing over the buffer. That call needs a buffer of 65536 shorts, which no far pointer addresses: a broken call only (3.1 would overrun too). | The loops count a DWORD `last - first + 1`. Valid calls unchanged. | 3175aa3 |
| L6 | REAL | enum_fonts followed `x->next` after the app's callback. A callback calling RemoveFontResource for the font it was just given (its last add) unlinked the entry, and font_reap freed it at once unless a DC had it realized (xfree, possibly the secondary heap's segment with it): the walk then read a freed block (a #GP, or garbage handed to more callbacks). When a DC kept it alive, `next` led into fonts_gone and removed fonts were listed. Documented APIs with valid arguments (a font manager). | No font is freed while an enumeration's callback runs (font_reap deferred, then run when the outermost enumeration ends); after a callback the walk checks its entry is still in the table, else goes on with the entry now at its place. Callbacks that remove nothing see the same calls in the same order. | 3175aa3 |

## Performance

| ID | Verdict | Evidence | Action | Commit |
|---|---|---|---|---|
| P10 | NOT-WORTHWHILE | font_bits' per-byte font_huge calls run once per scaled raster copy (cached, SCALED_KEEP unused copies kept for Clock), not per draw; a typical copy is a few KB. Hoisting the huge-pointer arithmetic needs a second path for columns crossing 64K, where a mistake corrupts glyph bits. | None. | - |
| P11 | NOT-WORTHWHILE | tt_draw's copy loop runs once per glyph per size (the `done` bits), after rasterisation that costs orders of magnitude more; the proposed per-column hoist would be wrong for a column crossing a 64K boundary of the font block. | None. | - |
| P12 | WORTHWHILE | tt_info called tt_width for each of about 226 characters, and tt_width scanned hdmx's records from the first for the size, each record (2 + numGlyphs bytes, 196 in w16 Sans) on another 512-byte page of ttfx's 8-page cache, which the cmap pages share: past about 6 records every character re-read the pages from the file (_llseek and _lread). w16 Sans has records for 6-251 pixels: at 60 pixels 54 records a character. | The record found once per size (tt_hdmx) and passed to tt_width; the same record and the same byte for every glyph, so the widths are bit for bit the same. Measured: see below. | 68e10d6 |
| P13 | WORTHWHILE | ExtTextOut with lpDx allocated a copy (xalloc, LocalAlloc in GDI's heaps, zero-filled), converted each width and freed it, even in MM_TEXT, where dc_scale_x is the identity. | MM_TEXT passes the caller's widths as they are (VGA.DRV only reads them); other mapping modes copy as before. Also, the widths are no longer dropped when the copy cannot be allocated in MM_TEXT. Measured: see below. | 68e10d6 |

## Measurements

VX16 (`tests/apps/vx16.c`) has two new phases: `textdx` (the `text` phase's
line of 54 characters with lpDx widths, MM_TEXT, screen DC) and `ttsize`
(CreateFont of Arial at character heights 9 to 72 pixels in turn, selected,
GetTextMetrics, deleted: a new TrueType size realized each step). Counts
in 4 seconds, `tests/vx16run.sh OUT tree` with `DROPIN_CYCLES="fixed
30000"` (the emulated machine's speed alone), GDI.EXE before (3175aa3, the
fixes alone) and after (68e10d6), three runs each, alternating:

| Phase | Before | After | Change |
|---|---|---|---|
| ttsize (P12) | 20, 20, 20 | 88, 88, 88 | 4.4 times as many sizes |
| textdx (P13) | 952, 956, 952 | 996, 992, 992 | +4% |
| text (control: no lpDx) | 1020, 1016, 1016 | 1020, 1016, 1016 | none |

Tests: `make test` (every dosx run passes; the hdpmi runs need .tools/hx,
absent in a worktree) and the boot-test cases gdi, gdik, usergdi,
gdiaudit, gdimeta, gdidib, gdifont, gdiconv, writefonts, staroff, vga and
vgaall on the remote host: 12 of 12.
