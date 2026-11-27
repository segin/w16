# TrueType: handover (stage 2, GDI integration)

What is done is in ARCHITECTURE.md 11.11 (the fixed-point rasteriser,
TTVIEW) and 11.12 (GDI). This note lists what is left, in the order it
should be picked up, and how to measure it.

## Tools

- `tests/gdiheap.sh -p build/dos/GD11.EXE [-r]` then
  `tools/gd11.py cmp tests/gdi/gd11ref.txt LOG` (raster results) and
  `tools/gd11.py tt [-v [-v]] tests/gdi/gd11ref.txt LOG` (the 1089
  TrueType results: face, then each metric).
- `tests/gd13.sh [-r] OUT` (probe GD13: CreateScalableFontResource, the
  .FOT, metrics, widths, ABC widths, kerning pairs, GetFontData, pictures)
  and `tools/gd13.py REF LOG`.
- Arial, Times New Roman, Courier New, Symbol and Wingdings of the
  reference install may be read at test time (never committed). The
  fixture is `tests/fixtures/fonts/LiberationSans-Regular.ttf`; the
  font-design agent's CC0 "w16 Sans/Serif/Mono" faces (branch
  worktree-agent-a5289a38cad78f12a, `fonts/tt`) are further test inputs.

## Left in stage 2

1. **EnumObjects**: not done. (EnumFonts and EnumFontFamilies are done:
   ARCHITECTURE.md 11.12, probe GD14; printer DCs' own fonts are not
   listed.)
2. **GetOutlineTextMetrics (GDI.308)**: OUTLINETEXTMETRIC from OS/2,
   head, hhea, post and the name table at the DC's size (the
   TEXTMETRIC part as GetTextMetrics); the four name strings after the
   structure. Probe the 16-bit layout and the rounding on 3.1.
3. **GetGlyphOutline (GDI.309)**: GGO_METRICS, GGO_BITMAP (ttfx's
   rasteriser into a DWORD-aligned 1-bit bitmap), GGO_NATIVE (the
   outline as TTPOLYGONHEADER/TTPOLYCURVE in FIXED 16.16, from
   ttfx_GetGlyphShape scaled); the MAT2 transformation.
4. **[TrueType] TTOnly=1**: only TrueType fonts in the mapper and in
   enumeration (the system fonts excepted); UNVERIFIED how 3.1 does it.
   TTEnable=0: w16 keeps the .FOT entries out of the mapper and answers
   GetRasterizerCaps without TT_ENABLED; what 3.1's AddFontResource
   returns then is not probed.
5. **The mapper's remaining differences** (GD11): two 2-pixel requests
   with no face (3.1: Small Fonts' 3-pixel font, w16 Arial at 2 ppem; a
   TrueType minimum size?), and the cell-height rule's 10 of 227 misses
   (very small sizes; Times New Roman 12). See `tt_ppem`.
6. **Width scaling (lfWidth)**: the size across is lfWidth x em /
   xAvgCharWidth; tmMaxCharWidth and the widths are a pixel or two off
   3.1's (GD11's 264 MW differences are these).
7. **Simulated italic**: w16 slants the outline by a quarter (a guess)
   and reports tmItalic FFh with no overhang as 3.1 does; 3.1's slant and
   pixels are not compared yet (GD13 has no italic pictures: add them).
8. **Glyphs wider than their advance** are cut at the cell's edges (the
   cell is the advance width); 3.1's handling of negative A and C widths
   in the bitmap it hands the driver is not observed. GetCharABCWidths'
   values are not compared yet beyond GD13's four characters.
9. **Big glyphs**: a glyph whose cell is over 30000 bytes is drawn blank
   (`tt_draw`); band it.

## Later stages

- Stage 3 (done): the Liberation fonts (OFL) in the install tree with
  [FontSubstitutes] and w16's font aliases (ARCHITECTURE.md 11.12,
  probe GD16).
- Stage 4: hinting by the fonts' own instructions (the bytecode
  interpreter). The autohinter (`gdi/ttf/ttfhint.c`) is on in GDI
  (SYSTEM.INI [w16] TrueTypeHinting, default 5: vertical and dropout);
  it moves no advance width. GDI opens and initialises the font for each
  batch of glyphs, so the autohinter's blue zones are worked out again
  each time: keep them per TTFILE if that shows up on a 286. The
  character widths 3.1 gets from hinting (Arial's W is 79 at 79 ppem,
  not 75) need it; until then w16 uses hdmx where the font has the size
  and the linear width elsewhere.
- Printing.
