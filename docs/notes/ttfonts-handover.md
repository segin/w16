# TrueType fonts: where the work stands

w16 Sans, Serif and Mono (ARCHITECTURE.md 6.9.3, fonts/tt/README.md),
twelve CC0 faces metric-compatible with Arial, Times New Roman and
Courier New. Not installed into w16's tree (the user examines them
first). State after the hinting and Serif pass of 2026-11-26.

Done:
- every face carries its own TrueType instructions from
  `tools/tthint.py` (fpgm, prep, cvt, glyph programs; TrueType 1.0
  only): zones, stems, crossbars, serif tips, diagonal strokes a stem
  wide across each row, the ink kept inside its advance, the advance
  width point hinted to hdmx's widths (head flags bit 4, LTSH);
- prep sets, at 8-48 pixels per em, the x-height, cap height,
  ascender, descender and stem widths the matching Microsoft face
  shows (`fonts/tt/screen.txt`, observed from their renderings);
- w16 Serif: vertical stress, square serifs as thick as the serif;
  w16 Mono: w/W redrawn, no dot in the zero, shorter slabs; all:
  diagonals a little lighter than stems;
- on 3.1 (FN4 show pages under SNAP, Write, the Font dialog): the
  lines of text set as long as Microsoft's at 8-24 pt; the faces are
  listed and chosen in the Font dialog;
- .FOT files byte for byte as 3.1 writes them (`tools/mkfot.py`).

2026-11-27 (after the user's look in w16's Write on w16's tree):
diagonal strokes keep their outer edge, level ends at the stem's
control value, S spine level, rounder Sans bowls, steeper Mono w;
`tests/ttshow.sh` compares w16's GDI with 3.1 (1.0-10.4%). Seen on the
way: w16's Font dialog (COMMDLG/USER on w16's tree) did not move the
focus with Tab from the face box, so a script's "Tab Tab 12" went into
the face name ("w16 Serif12", not installed: w16 Sans); not fixed
here (not font work).

Open:
1. FN4 (`tests/ttfonts.sh`): w16 Mono equals 3.1's every compared value;
   Sans and Serif differ in 0.3-2% of the per-size widths (mostly at 27
   pixels per em and in Serif Italic; e.g. Times '&' at 83 ppem 65
   against 61, which is the unhinted width, so 3.1 may have abandoned
   that glyph's program there). UNVERIFIED why. Before head flags bit 4
   all were equal, but then 3.1's TextOut set w16's text by the rounded
   scaled advances, wider than Microsoft's lines.
2. Small sizes still weaker than Microsoft's hand hinting: Serif's x,
   W, M at 8 pt; Mono's m, n, e at 8-9 pt; Sans's k, w spacing at 9 pt.
3. `screen.txt` was observed with w16's own interpreter (branch of the
   bytecode-interpreter work); a probe recording the same through 3.1's
   GetGlyphOutline (GGO_BITMAP) per size would make it 3.1's own.
4. The specimen tool used (a host program drawing lines with
   gdi/ttf/ttfx.c and ttfbc.c, placed by hdmx) is not in the tree yet:
   it needs the interpreter on master.

How to iterate: `python3 tools/ttgen.py --out DIR [FACE...]`;
`python3 tools/mkfot.py DIR`; on 3.1, FN4's `show N` pages 4-15 under
SNAP (`run C:\FN4.EXE show N`, `snap C:\Sn.BMP`, the .TTFs in
WINDOWS\SYSTEM: tests/refrun.sh with REFRUN_FILES/REFRUN_OUT), Write
through tests/inputrun.sh with a WIN.INI [fonts] entry per face
("w16 Serif (TrueType)=W16SERIF.FOT") and the .TTF and .FOT files in
WINDOWS\SYSTEM.

Replacing Liberation later: install the twelve .TTF/.FOT files, add
the [fonts] entries, and in WIN.INI [FontSubstitutes] map
`Arial=w16 Sans`, `Times New Roman=w16 Serif`, `Courier New=w16 Mono`
(and `Helvetica`, `Times`, `Courier` likewise) in place of the
Liberation names; metrics are the Microsoft faces', so documents lay
out alike.
