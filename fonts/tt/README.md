# w16's TrueType fonts

Twelve original TrueType faces, dedicated to the public domain under CC0 1.0
(the `name` table says so: copyright notice, licence description and licence
URL, IDs 0, 13, 14). They are metric-compatible with Windows 3.1's TrueType
core set: every advance width, the hinted device widths at 6-255 pixels per
em, the vertical metrics, underline and strike-out, and the kerning pairs are
3.1's, so text set in them breaks and lays out as it does in Microsoft's
faces. The outlines are w16's own. Design and method: ARCHITECTURE.md 6.9.3.

| Family | Files (.TTF, .FOT) | Widths of | Design |
|---|---|---|---|
| w16 Sans | `W16SANS` `W16SANBD` `W16SANIT` `W16SANBI` | Arial | neo-grotesque; italic oblique |
| w16 Serif | `W16SERIF` `W16SERBD` `W16SERIT` `W16SERBI` | Times New Roman | transitional roman, bracketed serifs, ball terminals; cursive italic lowercase (a, f, n, u) |
| w16 Mono | `W16MONO` `W16MONBD` `W16MONIT` `W16MONBI` | Courier New | typewriter slab; italic oblique |

Repertoire: ISO 8859-15 (Latin-9), plus `.notdef`, `.null` and CR; one
(3,1) format 4 cmap, which also maps U+2219 to the middle dot (Windows 3.1
looks up ANSI 0xB7 as U+2219). No hinting: the only bytecode is a
7-byte `prep` that turns on dropout control (so thin strokes survive in
3.1's monochrome rendering; `W16TT_PREP=0` leaves it out); gasp says to
smooth and grid-fit at every size; `hdmx` and `VDMX` hold 3.1's device
widths and per-size ascent/descent, so layout matches at screen sizes too.

Files here:

- `design.py`: the glyph designs, as geometry (stems, bars, superelliptic
  bowls and arcs, stroked splines, serifs) in parameters of each face.
- built by `tools/ttgen.py` (with `tools/ttgeom.py`, the outline engine,
  and `tools/w16ttf.py`, the TrueType writer and checker); the .FOT files
  by `tools/mkfot.py`.

```sh
python3 tools/ttgen.py                       # build/ttfonts/*.TTF, checked
python3 tools/mkfot.py build/ttfonts         # build/ttfonts/*.FOT
python3 tools/ttgen.py --specimen build/ttfonts build/ttfonts/specimen
tests/ttfonts.sh                             # on Windows 3.1 (reference install)
```

`[FontSubstitutes]` in WIN.INI can later map Arial, Times New Roman and
Courier New to these families (`Arial=w16 Sans`, ...) where Microsoft's
fonts are absent. Not done: Symbol and Wingdings equivalents.
