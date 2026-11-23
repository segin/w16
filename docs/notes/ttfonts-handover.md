# TrueType fonts: where the work stands

State on branch `worktree-agent-a5289a38cad78f12a` (ARCHITECTURE.md 6.9.3,
fonts/tt/README.md). The fonts and their metrics are done; quality on 3.1's
screen is not yet up to Microsoft's faces.

Done:
- twelve faces (w16 Sans, Serif, Mono), CC0, every width, per-size width
  (hdmx), per-size ascent/descent (VDMX), OTM/TEXTMETRIC value and kerning
  pair equal to 3.1's (FN4, `tests/ttfonts.sh`), except 8 device widths of
  w16 Sans/Sans Italic at 252-254 ppem (no hdmx record possible);
- .FOT files byte for byte as 3.1 writes them (`tools/mkfot.py`);
- w16's own autohinter (`tools/ttautohint.py`, engine `tools/tthint.py`),
  TrueType 1.0 instructions only, run on all faces and on Liberation Sans.

Open, in order of what the user sees:
1. w16 Serif at 8 pt on 3.1: the capitals look slanted (screenshot
   7-times-vs-w16.png in the examination copy). Not from the x-direction
   "rest" links (restricted to overlapping edges, no change); look at the
   cap serifs' x placement at 11 px (flag serifs and cap serifs are not
   symmetric about the stem) and at what MDAP rounds in x.
2. Serif figures at 18 pt still darker than Times (0, 4); per-size stem
   tuning in prep (equalise stem control values below ~1.5 px) is not
   done.
3. Serif design: bowls are superellipse rings, serifs straight brackets;
   a proper design pass (bowl shapes, terminals, g, a, s) is the largest
   gain left. Sans and Mono are closer to their counterparts.
4. In the 12 pt Serif Bold paragraph (show page 2) one line broke a word
   earlier than Times New Roman Bold although all compared widths and
   extents are equal (UNVERIFIED why).

How to iterate: `python3 tools/ttgen.py --out DIR`; host-side black and
white renders `python3 tools/ttmono.py OUT.png DIR/W16SERIF.TTF`; on 3.1,
FN4's `show N` pages 4-15 under SNAP (see tests/ttfonts.sh for the
remote run pattern; screenshots via REFRUN_OUT with C:\Sn.BMP paths).
