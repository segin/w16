# GDI audit triage: the TrueType engine (gdi/ttf)

A read-only audit of `gdi/` (defects and performance; written by another
AI model, not in the repository), each finding then checked by an
independent reviewer against the code. This file is the part for w16's
TrueType engine: `gdi/ttf/ttfx.c` (reading, outlines, the rasteriser),
`ttfbc.c` (the bytecode interpreter), `ttfhint.c` (the autohinter) and
their headers, with the one caller that matters here, `gdi/ttfont.c`.
The other GDI subsystems have their own triage files. Line numbers below
are approximate.

Verdicts: **REAL** (confirmed; fixed), **FALSE** (the claim is wrong),
**HARDENING** (only with malformed input; fixed where cheap and invisible
to valid fonts), **PARTLY** (some sub-claims hold: those fixed or kept as
noted), and for performance **WORTHWHILE** / **NOT-WORTHWHILE**. Every
fix here changes nothing Windows 3.1 shows for a well-formed font: where
3.1's own rasteriser would crash on bad data, w16 may survive (the
fidelity rule), so none needed a probe. All of GDI is built `-ml` for an
80286: `int` and `unsigned` are 16 bits, far pointers add to the offset
modulo 64K, and the TrueType buffers come from one 64000-byte
GlobalAlloc arena (`ttfx_alloc`, `gdi/ttfont.c`).

## Defects

| ID | Verdict | Evidence | Action |
|---|---|---|---|
| C7 | REAL | `ttfx_GetFontNameString` kept a name record's u16 length in an `int`: 8000h and up went negative, passed `len < size`, and `ttfx_Read` (which zero-fills to n) wrote 32-64K into `tt_name`'s 128-byte stack buffer (CreateScalableFontResource of a font with such a record). The negative return also sent `tt_name` to its second lookup. | The length is a `ttfx_u16`; at most `size` bytes copied; the return clamped to 7FFFh so no caller sees a long string as "not found". |
| C8 | PARTLY | Simple glyph in `bc_glyph`: `s + np + 2 > L->maxn` in 16 bits wraps when the last end point is near FFFFh (65533 with s = 0, less once components are loaded), and the point loops then write far past the workspace (heap corruption). Needs a glyph of over 65000 points against maxp: malformed only, so HARDENING in kind, but a write. The compound branch's check: FALSE (`np = L->n - s`, already bounded by maxn). | The simple-glyph check in 32 bits. End point FFFFh (np wrapping to 0) already fails the contour test. |
| H11 | HARDENING | `ins + inslen > end` and `q + inslen > end` add a u16 length to a far pointer: a length past the glyph wraps the offset below `end` and passes; `bc_hint` then fetches code up to 64K bytes from `ins`, past the arena segment (a GP fault). In `ttfx__shape` `points` was never checked: the wrap there only reads arena bytes (a wrong outline from a bad glyph). | Lengths compared with the bytes left (`inslen > end - ins`) in both `bc_glyph` branches; `ttfx__shape` fails a glyph whose instructions pass `glen`. |
| H12 | REAL | `bc_abs(-2^31)` stays negative, so in `unit_vector` the pair (-2^31, -2^31) skipped the halving loop and the doubling loop turned both to 0 and never ended; nothing there counts the instruction budget. Reachable from valid bytecode: SCFS can put a point anywhere (DUP/ADD build -2^31), then SPVTL/SFVTL/SDPVTL. In GDI that hangs the session (cooperative multitasking). Shown on the host: the new ttfbctest case runs for ever with the old `ttfbc.c`. | The halving loop tests the signed bounds itself, so -2^31 is halved first; ttfbctest "SPVTL of -2^31 each way" (make test). |
| H13 | FALSE | `numberOfContours * 2` and `2 * nc` wrap as `int` for 16384 contours and up, but a far pointer adds modulo 64K on the offset, which is the same byte the unwrapped sum names; the guards before them are 32-bit (`10 + 2UL * nc + 2 > glen`), and the whole glyph is in one block under 64000. On the host nothing wraps. Only formal signed overflow; Watcom emits a plain 16-bit add. | None. |
| M12 | PARTLY | (1) REAL: `ttfx__shape`'s compound loop skipped arg1/arg2 only when flag 2 (offsets) was set, but the two arguments are always present (flag 1 gives their size): a component placed by matching points left the parse 2 or 4 bytes short, reading them as the transform and the next component's flags and glyph from the wrong place. A valid font's wrong outline on the unhinted and autohinted paths (stb_truetype's omission; `ttfbc.c` was right). (2) HARDENING: the scale/matrix (up to 8 bytes) read before any check, in both files; an overread of a truncated glyph inside the arena. | The arguments always skipped; point matching still places at offset 0 in `ttfx.c` (as stb_truetype; `ttfbc.c` matches points). The transform's bytes checked before they are read in both. |
| M13 | PARTLY | The 16-bit wrap of `n` (FALSE): only a last end point FFFFh wraps, to n = 0, which builds an empty glyph in bounds. Non-monotonic end points (HARDENING): a contour that starts off the curve at point n-1 read `vertices[off + n]`, one vertex past the block (a read; malformed glyphs only). | The start's look-ahead also needs `i + 1 < n`. |
| L8 | HARDENING | `ttfx_GetGlyphHMetrics` with hhea's numberOfHMetrics 0 read the advance at `hmtx + 4 * (n - 1)` (an offset far past or before the table). Every read goes through the page cache, which returns zeros outside the file: a wrong width only, for a font the spec forbids. | n = 0 gives advance and bearing 0. |

## Performance

| ID | Verdict | Evidence | Action |
|---|---|---|---|
| P14 | PARTLY | `ttfx__render_tt`'s copy of r and d into the glyph's bitmap computed `(ttfx_u32)y * rs` (a far call to `__U4M`) for each pixel's test and again for each set pixel's store, and clipped x per pixel. Not "3x" as claimed (the compiler shared one product), but the per-pixel helper call is real. Runs once per glyph per realised size (the bytecode path), not per TextOut. Word or byte-at-a-time merging: not worth the edge masks. | Row pointers and the x range once per row (every buffer here is one block under 64K: 16-bit products are exact); r and d zeroed a word at a time. No `__U4M` left in the function. Output bit for bit the same (below). |
| P15 | PARTLY | `ttfx__bcRun` tested `bc->budget--` (a 32-bit field through a far pointer) for each instruction, and POP/PUSH loaded `bc->stack` and `bc->maxsp` through `bc` each time. Budget, stack and maxsp do not change while it runs (set only outside it; no recursion). Caching `sp` (FALSE as a safe change): the helpers and CLEAR/DEPTH/MINDEX/CINDEX change it, each would need a sync. | Budget a local (written back at the end, so BC_ELOOP comes at the same instruction), stack and maxsp locals in POP/PUSH; `sp` stays in `bc`. |
| P16 | NOT-WORTHWHILE | `e[act[i]]` in the scanners crosses two far segments, but only over the active edges (2-10) once a scan line, beside 32-bit divides per edge; a stack copy of `act` would still be a far pointer unless every loop were duplicated, and the proposed `ne <= 64` gate counts all edges, not the active. About 1 ms a glyph at most, on a cached path. | None. |
| P17 | FALSE | Casting the operands to `ttfx_u16` does not give a single MUL: compiled with GDI's flags and disassembled, both forms call `__U4M` (the cast only zeroes the high words first), and `__U4M` costs the same either way. Some of the sites are not 16x16 at all (`(ua >> 14) * uv` in `mul14`). | None. |
| P18 | NOT-WORTHWHILE | `ttfx__copy` (a byte loop) runs only when a compound glyph's components are merged, a few KB at most, after four 32-bit transforms a vertex: a word copy would not show in a glyph's time. | None. |

## Measurements

TTVIEW's specimen (922 glyphs at 8-24 pixels, the font's instructions;
TTVIEW links `ttfx.c`, `ttfbc.c` and `ttfhint.c` as GDI does) on an
emulated 80286 (`tests/ttview.sh -2 286`: DOSBox-X cputype=286 at a fixed
2000 instructions a millisecond, Windows 3.1 Standard mode), on
installation-05. The fixed rate makes the figure repeat to the
millisecond (master gave the 87.1 s recorded in ARCHITECTURE.md 11.11.2).

| Build | Hinted | Unhinted |
|---|---|---|
| master | 87111 ms | 28726 ms |
| the defect fixes and P14 | 84859 ms (-2.6%) | 28726 ms |
| and P15 | 84530 ms (-3.0%) | 28726 ms |

P14 saves about 2.4 ms a glyph, P15 about 0.4 ms more (small, but
measured and free). The unhinted path does not use `ttfx__render_tt` or
the interpreter. Both pages were identical to the host's drawing in every
run, and the host's pages (`ttvhost`, hinted, autohinted and unhinted,
both TTVIEW pages) are byte for byte master's for w16's twelve faces,
the Liberation faces and Hack. On the host (`ttfxtest`) the difference is
lost in the noise: these were 80286 costs (helper calls, far loads).

## Tests

- make test: ttfxtest ok, ttfbctest 289 checks (with the new H12 case,
  which never ends with the old `ttfbc.c`), the specimen against 3.1's
  as before (86.1% of glyph runs identical), every DOSX run of both
  kernels ok.
- The host pages above: byte for byte master's after the defect fixes
  too (no valid font in that set reached a changed path).
- Boot test on installation-05 (13 jobs): gdi, gdik, usergdi, gdiaudit,
  gdimeta, gdidib, gdifont, gdiconv, staroff, vga, vgaall ok; writefonts'
  tree part once caught the Font dialog half painted under the load,
  then passed twice alone (as master does).

## Left open

Nothing. Kept on purpose: H13, P16, P17, P18 (above). `ttfx.c` still
places a component given by matching points at offset 0 (stb_truetype's
way; the bytecode path matches them).
