# GDI core audit triage (Core / DC / Rgn / Obj)

An outside audit and performance review of w16's GDI listed, for the
core subsystem (`gdi/gdi.h`, `gdi.lnk`, `entry.asm`, `i4.asm`, `heap.c`,
`util.c`, `driver.c`, `misc.c`, `object.c`, `rgn.c`, `dc.c`), 2 High,
4 Medium and 4 Low defects (H1-H2, M1-M4, L1-L4) and 5 performance items
(P1-P5). Each was checked by independent readers against the current
code, ARCHITECTURE.md's GDI sections and the probes they cite, the 3.1
SDK and Schulman's Undocumented Windows. No Microsoft binary was read.

Verdicts: **REAL** (confirmed; fixed), **FALSE** (the claim is wrong),
**3.1-BEHAVIOUR** (3.1 does the same: kept), **HARDENING** (only with
malformed input; fixed when cheap and invisible to working use; where
3.1 itself faults, w16 may survive), **PARTLY** (part holds), **UNSURE**
(needs a run of 3.1; the code is left alone and the probe is described).
Performance: **WORTHWHILE** (done, output bit-identical, measured),
**NOT-WORTHWHILE** (left alone), **PARTLY**.

Status: every finding has a verdict; M1's palette half, M2's
PALETTEINDEX half and L1 wait for a 3.1 probe (2026-11-27).

## High

| ID | Verdict | Evidence | Action | Commit |
|---|---|---|---|---|
| H1 | HARDENING | CreateDIBPatternBrush accepts 16 <= GlobalSize <= FFF0h, copies the block and, for any header that is not a core one, reads biClrUsed at offset 32. GlobalSize is a multiple of 10h (KRNL286) or 20h (KRNL386), so a 16- or 32-byte block faults there (GDI's `__GP` table is empty). Only a block too small for a 40-byte BITMAPINFOHEADER does it. The WORD header size, `nc * 4` and the int casts of width and height wrap only with absurd fields; `hd < n` keeps every read inside the copy and SetDIBits writes only GDI's own bitmap. The GlobalLock NULL checks the audit asked for are not needed: the copy is moveable and not discardable, and a discarded or bad hdib gives GlobalSize 0. | Non-core headers need n >= 40 and biSize <= n; header plus colour table summed as a DWORD (biClrUsed capped at n first); width and height above 7FFFh refused. | 93c0e37 |
| H2 | HARDENING | LPtoDP and DPtoLP check only the DC, then write through the array: NULL with n > 0 faults. Schulman (DPXlate, GDI.138): 3.1's LPtoDP/DPtoLP check only the hDC too ("will only fail if the hDC is invalid"), so 3.1 faults the same way. No caller in w16 passes NULL. | `!p && n > 0` returns FALSE (3.1 faults; w16 survives). | 93c0e37 |

## Medium

| ID | Verdict | Evidence | Action | Commit |
|---|---|---|---|---|
| M1 | PARTLY: metafile half HARDENING; palette half UNSURE | (1) DeleteObject called mf_deleted before refusing a pen, brush or font still selected in a real DC (or a selected bitmap): the open metafile got META_DELETEOBJECT and a free slot (DEAD_SLOT for good when also selected in the metafile DC) for an object that lives on. Playback still draws the same (the next use writes a new creation record), but the record stream says something false. GD9 covers only objects selected in the metafile DC itself (no record, slot kept, DeleteObject succeeds), which is unchanged. (2) DeleteObject of a palette selected in a real DC frees it with no check; dc->hPal dangles. Every user of hPal validates it (obj_get with PAL_MAGIC, dib_info's zeroed entries, dev_colorinfo's black), so only colours can be wrong, never memory. What 3.1 does (refuse, reset to DEFAULT_PALETTE, or dangle) is not known. | (1) mf_deleted moved to the success paths, after the dc_uses and hdcSel checks. (2) None yet. Probe on 3.1: CreatePalette, SelectPalette into a screen DC, DeleteObject (return value), then GetObject/IsGDIObject on it, SelectPalette(hdc, DEFAULT_PALETTE)'s return value and SetTextColor(PALETTEINDEX(n)) drawing. If 3.1 refuses, refuse; if it resets, point the DC at stock DEFAULT_PALETTE in pal_free. | 93c0e37 |
| M2 | PARTLY: NULL ColorInfo HARDENING; PALETTEINDEX UNSURE; PALETTERGB FALSE | GetNearestColor calls the driver's ColorInfo unchecked and with the caller's COLORREF as is, where dev_colorinfo (SetBkColor, SetTextColor) resolves PALETTEINDEX through the DC's palette and masks PALETTERGB. (1) dev_open loads a driver without ColorInfo, but the DDK makes ColorInfo a required export; only a malformed driver reaches it. (2) PALETTERGB: the VGA's ColorInfo ignores the high byte, so the answer is already the plain RGB's: no difference. (3) PALETTEINDEX(5) gives the nearest colour to RGB(5,0,0) (black) where SetTextColor would use entry 5. VG3 drove only RGB values; the SDK does not say. GD15 shows 3.1 resolving PALETTEINDEX for a background colour, which makes translation plausible but unproven. | (1) No ColorInfo: 0. (3) None yet; an UNVERIFIED comment. Probe on 3.1: GetNearestColor(screen DC, PALETTEINDEX(n)) for several n, with the default palette and with a selected palette of distinct colours, and PALETTEINDEX past the palette's end. If 3.1 answers the entry's nearest colour, share dev_colorinfo's translation in a helper and return ColorInfo's logical result. | 93c0e37 |
| M3 | REAL | iso_fix compared and rebuilt the scales in thousandths, truncated twice: window 32767,32767 with viewport 20,-20 made the viewport's x extent 0 (every x mapped to vpOrg.x; DPtoLP divided by 0 and got mul_div's -1); 640,480 over 32767 gave 458 for 480; 641,481 over 3000 gave 480 for 481. Reachable by plain SetWindowExt/SetViewportExt/Scale*Ext in MM_ISOTROPIC. GD5's 3.1 results (gd5ref.h) bound the fix: 640/640, 1000/1000 with 300,-200 (200,-200), 1000,500 with 300,300 (300,150), 3,7 with 32000 (13714,32000), and 32000,1 with 1,32000 giving 1,0. 3.1 keeps a 0 extent, so the audit's proposed clamp to 1 was not taken. | Scales compared exactly (ax*wy against ay*wx in longs; at most 2^30), the shrunk extent ay*wx/wy (or ax*wy/wx) truncated, the sign of the extent kept. Every GD5 value is unchanged. UNVERIFIED: 3.1's rounding of a fraction of a half or more; probe: window 2,3, viewport 1,1 (x: 0 truncated, 1 rounded). | 93c0e37 |
| M4 | REAL | GetBoundsRect without 8000h returned dc->bounds (screen coordinates) minus dcOrg: device coordinates. SetBoundsRect without 8000h added dcOrg to the caller's rectangle unconverted. The 3.1 SDK says both are in logical coordinates. GD2 covers only USER's 8000h path (screen coordinates), which is unchanged; w16's USER calls neither. With SetWindowOrg(100,100), PatBlt(100,100,10,10) reported (0,0,10,10) where the SDK gives (100,100,110,110); MM_LOMETRIC also scaled and flipped y. | Without 8000h: GetBoundsRect converts with dc_dp_to_lp after taking off dcOrg, SetBoundsRect with dc_lp_to_dp before adding it; both normalise left/right and top/bottom. MM_TEXT at default origins is the identity. UNVERIFIED: 3.1's rounding when the mapping scales. | 93c0e37 |

## Low

| ID | Verdict | Evidence | Action | Commit |
|---|---|---|---|---|
| L1 | UNSURE | CreateIC sets DCF_IC, which nothing reads: LineTo, PatBlt, TextOut on an IC draw on the screen like a CreateDC("DISPLAY") DC (a real PDEVICE and visible region; no memory risk). The SDK says only that output functions "cannot be used" with an IC; Pietrek's separate IC signature shows 3.1 tells them apart, not what its output calls do. No w16 probe draws on an IC (GD4 counts heap, GD14 queries fonts). Refusing would change return values and pixels. | None yet. Probe on 3.1: CreateIC("DISPLAY", ...), then LineTo, PatBlt, Rectangle, TextOut and BitBlt to and from it; record return values and screen pixels (snapshot). If 3.1 refuses, refuse in dc_out for DCF_IC with the same returns. | - |
| L2 | HARDENING | DeleteDC, SaveVisRgn and RestoreVisRgn dereferenced obj_get of the SaveVisRgn chain unchecked. On every valid path the chain is sound: hVisRgn is set once at creation, new regions are zero-filled, rgn_copy leaves the header, only SaveVisRgn adds and only RestoreVisRgn/DeleteDC unlink, before rgn_free. Only a program (or USER) deleting a GDI-internal region (SaveVisRgn's copy, InquireVisRgn's region) breaks it; 3.1 would most likely fault too. | Every lookup checked with RGN_MAGIC: DeleteDC stops at a broken link, RestoreVisRgn returns ERROR (cutting the chain) and SaveVisRgn frees its copy and returns 0. A reused handle that is again a region is not detected (that would need DeleteObject to refuse a DC's regions: not done). | 93c0e37 |
| L3 | FALSE | dev_open's failure path frees the PDEVICE block still locked only when Enable(0) fails. w16's KERNEL counts GlobalLock only for discardable blocks (this one is not), and GlobalFree frees a locked block (as 3.1 does, the behaviour KERNEL copies), so nothing leaks. | None. | - |
| L4 | REAL | extra_logical: `v = mul_div(v, wndExt.x, vpExt.x)` already has the right sign (mul_div is signed), and the following `if signs differ v = -v` flipped it again. MM_ANISOTROPIC, window 1,1, viewport -1,1: SetTextCharacterExtra(6) stores -6 device units, GetTextCharacterExtra returned -6 (and Set's own return value was wrong the same way). GD8 used only positive extents, all unchanged. | The extra negation removed; Get is the inverse of Set's dc_scale_x. Whether 3.1 stores the device extra signed or as a magnitude on a flipped axis stays unknown (affects drawing, not this round trip). | 93c0e37 |

## Performance

| ID | Verdict | Evidence | Action | Commit |
|---|---|---|---|---|
| P1 | WORTHWHILE | mem_copy and mem_set were byte loops; wcc (-ox -2) loaded ES twice per byte in mem_copy (about 55-60 286 clocks a byte against 2 for REP MOVSW). Most of the 73 callers copy small structures, but bits_copy (GetBitmapBits, SetBitmapBits, CreateBitmap, USER's icons and cursors), metafile DIB records, StretchBlt's rows, TrueType glyph clearing and xrealloc scale with the data. No caller relies on overlapping copies; forward word moves give the same bytes. | Inline `#pragma aux` REP MOVSW/STOSW plus the odd byte, DS kept, CLD explicit, n = 0 a no-op. VX16 (temporary phase): GetBitmapBits + SetBitmapBits of a 128x128 screen bitmap 580 -> 2300 a phase (4.0x); StretchBlt +3%. | c255e00 |
| P2 | PARTLY (the change WORTHWHILE; the claimed 50-66% per-call saving FALSE) | dc_get ran obj_magic then obj_get, which runs obj_magic again: two LSL + LocalSize validations (three stub_of under ExtendedGdiHeap) on every DC call. The DC begins with its OBJHDR, so obj_get(h, 0) plus a check of its wMagic gives the same answer. The saving is per dc_get, not half of a GDI call. | dc_get: one obj_get, then the DC signature from the header. VX16 (temporary phase): four Get* attribute calls 35200 -> 63700 a phase (1.8x); GetDC/PatBlt/ReleaseDC +20%; LineTo +3%. The optional single stub_of in obj_get under ExtendedGdiHeap was not done (off by default). | c255e00 |
| P3 | NOT-WORTHWHILE | dc_lp_to_dp/dc_dp_to_lp test the mapping mode per vertex; hoisting it would be exact, but every caller passes 1 or 2 points except draw.c's to_screen, whose vertices each lead to far more rasterising work (under 1%). | None. | - |
| P4 | NOT-WORTHWHILE | PtInRegion and rect_in scan every rectangle without a box test or early exit. Both would be exact, but the hot caller (SetPixel/GetPixel against hRao) usually has a one-rectangle region, where a box test only adds compares; two far driver calls dominate. Only the early exit on `r[i].top > y` would be free if the code is touched. | None. | - |
| P5 | PARTLY (the cost WORTHWHILE; the proposed `(long)dr*dr` FALSE) | GetNearestPaletteIndex squared long differences: three __I4M far calls per entry (up to 768 a lookup), and dib_colors_back calls it per colour-table entry. OpenWatcom still calls __I4M for `(long)int*int`; `(unsigned)dr*(unsigned)dr` is exact (at most 65025) and compiles to an inline MUL. Same distances, same strict `<` tie-break. | 16-bit unsigned squares summed as a long (checked with wdis: no __I4M). VX16 (temporary phase): lookups on a 256-entry palette 3544 -> 9008 a phase (2.5x). | c255e00 |

VX16 runs: w16's tree in 386 Enhanced mode, plain DOSBox at
`DROPIN_CYCLES="fixed 30000"`, 4 s a phase, base and change run side by
side twice with the same results within 1%; the busy loop equal. The
phases `bits`, `gnpi` and `dcget` were added for the measurement only and
are not in VX16. DOSBox counts every instruction about alike, so a real
286, where each segment load costs about 17 clocks, gains more from P1
and P2.

## Tests

`make -j8` clean. `make test`: every check before run-tests passed
(ttfxtest, ttfbctest, hlpcheck, castcheck, irqcheck); in run-tests every
dosx run passed (79 under KRNL286, 79 under KRNL386), and all hdpmi runs
failed only because HDPMI16.EXE (.tools/hx) is not in this worktree.
Boot test on the build host, cases gdi, gdik, usergdi, gdiaudit (GD5's
MM_ISOTROPIC and GD8's character-extra tables among them), gdimeta,
gdidib, gdifont, gdiconv, writefonts, staroff, vga, vgaall: 12 ok, 0
failures.
