# w16 VGA display driver (`VGA.DRV`, module `DISPLAY`)

w16's own display driver for Windows 3.1: 640x480, 16 colours, 96 dpi
(ARCHITECTURE.md 6.3, 6.10), the `display.drv=vga.drv` of SYSTEM.INI's
`[boot]`. Like every w16 driver it is a genuine Windows 3.1 driver
(ARCHITECTURE.md 6.7): it implements the GDI device driver interface as
the Windows 3.1 DDK documents it, loads on real Windows 3.1 as well as on
w16, and has no w16-only interfaces. `make` builds `build/dos/VGA.DRV`
and puts it in the install tree (`build/w16/SYSTEM`).

**Status: stages 1 and 2 of 5 done** (Standard mode): Windows 3.1 boots
to Program Manager on it, and with Microsoft's resources linked in for the
comparison every screen of the boot test's `vga` scripts (twelve real-input
sessions: typing, menus, dialogs, Task List, Alt+Tab, icons, dragging,
MDI) is pixel-identical to Microsoft's driver's, as is the MS-DOS Prompt
and back (Disable/Enable) and the all-w16 stack with it. Every value
programs can see through the driver (GDIINFO, system metrics and colours
from its configuration resource, the stock fonts, GetNearestColor, the
dithering of solid brushes on colour and monochrome bitmaps, the bitmap
formats, Inquire, the VGA palette) is the same as with Microsoft's (VG1,
VG3), and so are lines of every style (VG5), text with every attribute
(VG6), all 256 raster operations, monochrome/colour conversions, DIBs
and hatches (VG7), EnumObj, the escapes and flood fills (VG8), DIBs in
every format with every colour use, RLE both ways and over 64K (GD7).
See "Stages" below.

## Layout

- `vga.h` - the interface structures (rewritten from the DDK
  documentation) and w16's own private ones (physical colours, pens,
  brushes, PDEVICE: GDI treats them as opaque, so they are w16's design).
- `entry.asm` - the library entry, the interrupt-time cursor entries
  (MoveCursor, CheckCursor, SetCursor save every register and load the
  driver's fixed data segment), small helpers, the undocumented entries.
- `vga.c` - Enable, Disable, the GDIINFO, Control (escapes), EnumObj,
  EnumDFonts, the stubs GDI never calls, UserRepaintDisable.
- `surf.c` - drawing surfaces: the screen (mode 12h, plane by plane) and
  GDI's memory bitmaps (PBITMAP, also over 64K); clipping.
- `rop.c` - the 256 ternary and 16 binary raster operations on bytes.
- `color.c` - the 16 colours, ColorInfo (nearest colour), RealizeObject
  (pens, brushes), solid-colour dithering.
- `blt.c` - BitBlt (every ROP3, mono <-> colour), FastBorder, scan-line
  fills.
- `output.c` - Output (polylines with styles, scan lines), Pixel, ScanLR.
- `text.c` - ExtTextOut, StrBlt, GetCharWidth for raster fonts.
- `dib.c` - DeviceBitmapBits (SetDIBits/GetDIBits), SetDIBitsToDevice;
  RLE4/RLE8 decoded and (GetDIBits) encoded, as 3.1's driver does (the
  file's head comment; GD7).
- `cursor.c` - the software cursor and its exclusion, Inquire,
  SaveScreenBitmap.
- `vga.lnk` - the module: name DISPLAY, Microsoft's ordinals and names,
  every segment fixed.
- `resources.md` - the inventory of the reference driver's 80 resources;
  `art/*.txt` - w16's own graphics (`tools/vgaart.py`, `make vgaart`);
  `tools/vgares.py` - all 80 resources as a .RES (art, configuration,
  stock fonts, version).

## The interface (observed on Windows 3.1)

Exports, as Microsoft's VGA.DRV's NE tables have them: 1 BitBlt,
2 ColorInfo, 3 Control, 4 Disable, 5 Enable, 6 EnumDFonts, 7 EnumObj,
8 Output, 9 Pixel, 10 RealizeObject, 11 StrBlt, 12 ScanLR, 13 DeviceMode,
14 ExtTextOut, 15 GetCharWidth, 16 DeviceBitmap, 17 FastBorder,
18 SetAttribute, 19 DeviceBitmapBits, 20 CreateBitmap, 21 DIBScreenBlt
(SetDIBitsToDevice), 90 DO_POLYLINES, 91 DO_SCANLINES, 92
SaveScreenBitmap, 101 Inquire, 102 SetCursor, 103 MoveCursor, 104
CheckCursor, 400-403 PExtTextOut, PStrBlt, RExtTextOut, RStrBlt (here
ExtTextOut and StrBlt), 500 UserRepaintDisable, 501 and 502 (no names).
The driver imports KERNEL's `__A000H`, `__AHINCR`, `__WINFLAGS`,
`__0040H`, and GetProfileInt and WriteProfileString (mouse trails' WIN.INI
setting; Microsoft's imports them too).

What crosses the interface was observed with **VGREC**
(`tests/vgaprobe.sh -l`, `make vgaprobes`): a recording DISPLAY in front
of the driver under test (renamed VGAREAL), built from GD1's sources
(`tests/gdi/gdlog.c` with `GD_VALUES` and `GD_POPS`). It logs only
values: each call's arguments, the documented structures GDI builds
(PBITMAP, DRAWMODE, TEXTXFORM, LPEN, LBRUSH, rectangles, points,
strings, BITMAPINFOHEADER), GDIINFO, the result in DX:AX, and the bytes
the callee popped (its SP on return); nothing behind a pointer the
driver owns or fills (PDEVICE, physical objects) is ever read. Seen in
the boot to Program Manager (824 calls), all as the DDK's parameter
lists: Enable pops 18 bytes, ColorInfo 12, RealizeObject 18, BitBlt 32,
ExtTextOut 40, DeviceBitmapBits 26, FastBorder 28 (and returns FFFFh),
UserRepaintDisable 2 (USER, through GetProcAddress, with 1 then 0).
Inquire (VG1, called directly) returns 4 and the CURSORINFO 1, 2 and
pops 4.

- Enable(lpDevice, style, ...): style 1 copies the GDIINFO (6Eh bytes,
  its values exactly Microsoft's: GetDeviceCaps reads them), style 0 sets
  up the PDEVICE (dpDEVICEsize 23h, as GetDeviceCaps(PDEVICESIZE) shows)
  and the hardware: mode 12h through the BIOS, then the attribute
  controller's palette registers and DAC registers 7 and 15 as Microsoft's
  driver leaves them (VG1 reads them back; a screenshot shows the same
  colours). Disable sets text mode 3, with the 8x8 font when the BIOS's
  row count (0040:0084, read at the first Enable) said more than 25 lines.
- RealizeObject: the brush origin is the fifth parameter itself, a POINT
  (x low word, y high word; the DC's origin), not a pointer. GDI passes
  the logical RGB in LBRUSH.lbColor and LPEN.lopnColor, and for
  BS_PATTERN the pattern's PBITMAP in lbColor. No device fonts (0), no
  device bitmaps (0).
- Colours: a colour's index is its pixel's four plane bits, plane 0 red,
  1 green, 2 blue, 3 intensity, which puts the 16 colours in the order of
  a 4-bit DIB's colour table (black, maroon, green, olive, navy, purple,
  teal, gray, silver, red, lime, yellow, blue, fuchsia, cyan, white):
  VG1's GetBitmapBits and GetDIBits of each colour.
- The configuration resource (OEMBIN 1) is the DDK's CONFIG.BIN, 98 bytes
  (the reference's 112 is the NE file's 16-byte rounding); its colours
  are read red first (the DDK says RGBQUAD: with blue first the active
  caption came out maroon). OEMBIN FONTS: three LOGFONTs cut after
  lfPitchAndFamily (18 bytes), each followed by its face name.

### The nearest colour (ColorInfo, GetNearestColor)

Found from VG3 (GetNearestColor of all 2^24 colours on the screen) and
reproduced for every one of them (`color.c`, rgb_index). With the
components sorted max >= mid >= min: black if max <= 64; else gray,
silver or white (max <= 160, <= 223, above) if min + mid > max and 2 min
>= mid; else the max's channel, and the mid's when 2 mid > max + min, dark
below 192 and bright from 192 on.

### Solid brushes (dithering)

Found from VG1's patterns (8 x 8, on a colour and on a monochrome bitmap)
and reproduced for every colour tried; `tests/vgacolor.sh` (VG4) compares
all 2^24 (`color.c`, dither): an exact one of the 16 colours is solid
(silver is used only so); otherwise each component c gives q = (c + 1) /
4, a dark part 2 min(q, 32) and a bright part 2 max(q - 32, 0), out of 64;
64 units each take a corner of the cube of 0, 128 and 255 per component
(255 while the unit is below the bright part, 128 from 64 minus the dark
part on); a corner that mixes 128 and 255 is shared half and half between
the colours with 0 and with 255 for its 128s; the colours, in index
order, take the thresholds of the 8 x 8 Bayer matrix in turn. On a
monochrome bitmap the white pixels are the thresholds below (R + 2G + B +
4) / 16, except levels 16 and 48, which have a diagonal pattern.

### Lines (Output OS_POLYLINE)

From VG5 (lines of every pen style in every direction and slope, opaque
and transparent, single, polylines in one Output call, separate LineTo
calls): one pixel per step of the longer axis (y at 45 degrees), the
other coordinate rounded to nearest, a half going to the side of the
line's upper end; the end point is not drawn. Styles are 8-bit masks
(dashed E7h, dotted AAh, dash-dot E4h, dash-dot-dot EAh, first bit
first); a counter goes through a bit in 12 units, 3 a pixel along x and
4 along y (bits of 4 and 3 pixels), starting at 0 for a first segment in
x and -1 in y; through a polyline the mask and counter go on, the
counter kept at a turn from y to x and converted to keep the pixels
left of the bit at a turn from x to y (4c - 12) / 3; each call starts
again. UNVERIFIED: polylines that turn from a slanted y segment into an x
one, or cross back over themselves (VG5's stars and pentagons, GD3's
styled ellipses and arcs, which GDI draws as such polylines) differ by a
pixel of phase here and there.

### Text (ExtTextOut, StrBlt, GetCharWidth)

From VG6 (raster fonts with bold, italic, underline, strike-out and
their mixtures, opaque and transparent, clipped, with widths,
justification and character extra) and VGREC: GDI asks for emboldening
with 200h in TEXTXFORM's txfAccelerator; the driver strikes each glyph
twice one pixel apart, makes each character one pixel wider and the
extent one more (GetCharWidth: two more), unless the font is bold
already (System asked for 900: plain). Italic (400h), underline (800h)
and strike-out (1000h) GDI does itself: italic by calling ExtTextOut in
bands of two rows moved right, the lines by polylines whose length is
the driver's extent. With the caller's widths the text's box (its
background, the cut of its glyphs, the extent) is the widths of all
characters but the last plus the last one's own width. Drawing returns 0
(3.1's driver does); measuring (count < 0) returns the extent in AX and
the height in DX. The fonts GDI scales by whole multiples (ARCHITECTURE.md
11.10) are 3.x fonts that can be over 64K: the character table's offsets
are taken as 32-bit, the segments __AHINCR apart.

### Mouse trails (Control MOUSETRAILS, MoveCursor, CheckCursor)

The DDK ("Mouse Trails", "MOUSETRAILS") gives the interface: escape 39,
an int in, the driver draws the trail in MoveCursor and keeps WIN.INI
[windows] MouseTrails. What Windows 3.1's VGA.DRV does was observed with
VG9 (`tests/apps/vg9.c`, `tests/vgatrails.sh`): the probe calls the
escape and the driver's own MoveCursor (103) and CheckCursor (104), and
reads the screen back from the VGA's memory (with interrupts off, so no
timer tick falls between the read map's setting and the reads) after
each step; Control Panel's Mouse applet uses the escape the same way
(ARCHITECTURE.md 14.5). Where it differs from the DDK, the observation
wins:

- **WIN.INI at boot.** The first Enable reads MouseTrails (absent: -7):
  0 or more is on, the size its absolute value cut to 1..7 (0 and a
  value that is no number: on, size 1; 9: on, 7; -5: off, 5). Nothing is
  written then.
- **The escape.** QUERYESCSUPPORT(39) answers the size, negative when
  off (-7 with no MouseTrails). Escape n, returning the same: 1..7 (more:
  7) sets the size, turns trails on and writes MouseTrails=n; 0 turns them
  off and writes -size if they were on; -1 turns them on and writes the
  size if they were off (WIN.INI is not read, unlike the DDK's text); -2
  off and -3 on without writing; below -3 as -3, but -32768 as -1.
- **The trail.** Every MoveCursor adds the position the cursor had (also
  when it did not move), keeping at most size - 1 of them; every
  CheckCursor drops the oldest, so a still pointer's trail runs down one
  image a timer tick (55 ms), as the probes see 3.1's driver do (but see
  "The run-down" below: w16's default is slower). The images are drawn
  oldest first, each
  over those before it, in the current shape; each is clipped at the
  screen's edges like the cursor.
- **Shapes.** No trail for a cursor whose AND mask has no 0 bit (the
  I-beam: XOR only, as the DDK says); the cross and the hourglass have
  one. SetCursor ends the trail and draws the new shape alone at once;
  ShowCursor(FALSE) (USER's SetCursor(NULL)) takes everything off, and
  the pointer comes back alone.
- **Drawing.** A drawing operation whose rectangle meets any image (each
  image's bytes, as for the cursor) takes the whole trail and the cursor
  off; they stay off, the trail forgotten, until the next CheckCursor or
  MoveCursor draws the cursor alone. Drawing that meets no image (also
  inside the trail's bounds) leaves everything. So drawing under a trail
  leaves no debris.
- **The escape and the screen.** An escape that turns the trail on or
  off or sets a size takes the cursor and trail off the same way; the
  query and an escape that changes nothing leave them.

- **The top row.** A cursor moved to row 0 is drawn as on row 1,
  whatever its hot spot (also with trails off; column 0 is not moved).

w16's driver does all of the above (`vga.c` Control and trail_boot,
`cursor.c`); with trails off, or a size of 1, the cursor is excluded and
drawn as before, but for the top row. One difference is kept: with
trails off, 3.1's driver too leaves the cursor off after a drawing
operation met it until the next CheckCursor (up to a tick); w16's draws
it back as the operation ends (VG9's comparison leaves that frame out). MoveCursor and CheckCursor queue what they bring (each position, each
tick, with interrupts off for the few instructions it takes) and whoever
can draw takes the whole queue, so no position or tick is lost when one
interrupt comes while another, or a drawing operation, is drawing the
cursor.

**In time, under real motion** (2026-11-15, a hand test on the install
tree: "the trails are janky and disappear too quickly"). The frame
comparison above masks the timer, so it never saw how often the stack
calls the driver. `tests/currec.sh` puts CURREC.DRV (`tests/gdi/
currec.asm`, a stand-in that forwards every call and records each
cursor call's time, caller and arguments) in front of the driver and CR1
(`tests/apps/cr1.c`) as the shell, under a steady stream of xdotool moves
(`tests/scripts/input/trailmove.txt`, inputrun's `glide`); CR1 also keeps
a frame at each timer message, and `tools/cr1stat.py` counts the arrows
in it. On all-Microsoft 3.1, on Microsoft's stack with w16's VGA.DRV and
on the tree alone (KRNL386), captured or not: MoveCursor from USER's
mouse event procedure at interrupt time, about 50 a second (DOSBox's 40
PS/2 reports a second, as the driver set them), the identical sequence of
positions on all three; CheckCursor from SYSTEM.DRV's timer, 18.3 a
second, one a tick, interrupts on, nowhere else. The calls were the
same; w16's driver was not: CheckCursor runs with interrupts on, and a
mouse interrupt in the middle of its drawing found busy set and kept
only its last position, while a tick that came during MoveCursor's
drawing was lost. The trail had gaps of two or three steps (19% of the
gaps between neighbouring images against 0.5% on 3.1) and ran down
unevenly. With the queue: 1%, and run-downs of 5-6 ticks as 3.1's. The
trail's length in motion is 6 or 7 images on both (which one a frame
shows depends on whether a move came between the tick and the timer
message). w16's immediate redraw after drawing (trails off only, above)
plays no part: with trails on a drawing operation that meets the trail
leaves it off until the next tick or move, as 3.1's. A second fault
showed on a loaded host: the tree got a sixth fewer moves than 3.1 from
the same stream and the pointer fell behind the hand. Swapping parts
(Microsoft's MOUSE.DRV, USER, SYSTEM.DRV, KEYBOARD.DRV, GDI, KRNL286 or
KRNL386 into the tree, w16's into Microsoft's stack) found it in two
places together: w16's MOUSE.DRV dropped the motion of packets that came
while USER's call was still running (its README), and only w16's VGA.DRV
with trails on made that call long enough, by drawing seven images with
the map mask set for every row. Both fixed: the mouse driver keeps that
motion, and an image is drawn plane by plane (four port writes, not a
hundred and twenty-eight) with its shifted masks made once per shape and
shift. Now the tree gets the same moves as 3.1, captured and not, under
the boot test's load. Boot-test parts `trails@timed` and
`trails@timednl` (the mouse not captured) check all of it.

**The run-down** (2026-11-15). Compared by hand with real Windows 3.1
booted interactively in DOSBox with the same settings (the mouse not
captured), 3.1's trail took about 550-600 ms to run down after the
pointer stopped, w16's about half that. That figure is the user's
observation, not a probe's: in VG9 and CR1 3.1's driver drops an image
per CheckCursor call, one a tick. So w16's driver now drops a still
trail's oldest image every second tick, the first on the first tick
after the last move (seven images gone in 11 ticks, 604 ms); while the
pointer moves (a move between ticks) it still drops one a tick, so the
trail's length in motion is unchanged. SYSTEM.INI `[w16]
MouseTrailDecay=N` sets the ticks per image (1: one a tick, as in the
probes); `tests/vgatrails.sh` runs both drivers with 1, and
`trails@timed`/`@timednl` expect a run-down from six images to one of
440-660 ms instead of 3.1's measured 275.

UNVERIFIED: 3.1's driver sometimes writes the
positive value with a leading space (`MouseTrails= 7`), depending on
something VG9 did not find (not the caller's stack; KERNEL alone writes
`7`); w16 never does. 3.1 turned trails on at 7 for an escape of 1000
without writing WIN.INI (seen once). Whether a later Enable (after a DOS
program in Standard mode) reads WIN.INI again (w16: only the first).

## Testing

- `tests/vgaprobe.sh [-w] [-m] [-l] [-p PROBE] [-s] OUTDIR`: Windows 3.1
  (Microsoft's KERNEL, GDI, USER) with Microsoft's VGA.DRV, or w16's
  (`-w`); `-m` links Microsoft's resources into a scratch copy of w16's
  (so screens compare the code, not the art); `-l` puts VGREC in front;
  `-p` runs a probe as the shell; `-s` takes a screenshot.
- VG1 (`tests/apps/vg1.c`): the values programs see (above), for a grid
  of 4913 colours or the colours of `C:\VG1.IN`; the two drivers' logs
  must be identical.
- `tests/vgacolor.sh` (VG3, VG4): the colours exhaustively; heavy, for the
  build host. VG3 (all 2^24 colours, on the screen and on a monochrome
  bitmap) gives identical files with both drivers.
- VG5 (`tests/apps/vg5.c`): lines of every style; VG6
  (`tests/apps/vg6.c`): text; VG7 (`tests/apps/vg7.c`): every ROP3 on
  colour and monochrome bitmaps with a source and a pattern brush,
  monochrome/colour BitBlts with several colour pairs, 1/4/8/24-bit DIBs
  both ways, hatches; VG8 (`tests/apps/vg8.c`): EnumObjects, the escapes
  QUERYESCSUPPORT reports, GETCOLORTABLE, EnumFonts, flood fills. Their
  logs are compared whole: identical (VG5 but for the turns noted).
- GD7 (`tests/apps/gd7.c`, `tests/gdiheap.sh -r -v build/dos/VGA.DRV -p
  build/dos/GD7.EXE`): DIBs of 1, 4, 8, 24 bits and RLE4/RLE8, RGB and
  palette colours, through SetDIBits, CreateDIBitmap, SetDIBitsToDevice,
  StretchDIBits and GetDIBits (RLE), checked against 3.1's values
  (`gd7ref.h`); boot-test case `gdidib` (with Microsoft's GDI and w16's).
- Boot-test cases (`tests/boottest.sh`): `vga` (twelve input scripts with
  w16's driver and Microsoft's resources against Microsoft's driver, every
  screen 0 pixels), `vgaboot` (plain boots: 0 pixels; with w16's own
  resources the boot completes), `vgaall` (every w16 part with it),
  `vgawoados` (the MS-DOS Prompt: Disable to text mode and Enable back),
  `vgaprobe` (VG1's logs identical), `trails` (below).
- VG9 (`tests/apps/vg9.c`, `tests/vgatrails.sh -p MODE OUTDIR`): mouse
  trails. Mode `det` is the comparison: 23 cases of MoveCursor,
  CheckCursor, SetCursor, ShowCursor, PatBlt and escape steps run with
  the timer masked and interrupts off, a frame of the screen's top 200
  rows kept after chosen steps (206 frames), so nothing depends on
  timing; `escape` the escape's results and WIN.INI after each of 37
  calls; `boot` the state WIN.INI gives at Enable and a trail. The other
  modes (`full`, `more`, `late`) are the timed observations the
  description above comes from (`tools/vg9png.py` turns VG9.BIN into
  images: `--strip BIN PHASES Y H OUT` for one phase's frames one under
  the other). Boot-test case `trails`: det, escape and boot (WIN.INI
  MouseTrails=5) identical, logs and frames; real mouse motion with trails
  on from WIN.INI (Program Manager and its Main group dragged, a menu
  tracked under the trail; `tests/scripts/input/trails.txt`) and Control
  Panel's Mouse applet turning trails on and off (`cpltrails.txt`), every
  screen 0 pixels once the trail has run down, MouseTrails the same.

## Stages

1. **Done**: the inventory and the probes (VGREC, VG1, VG3, VG4); the
   driver: every export, the GDIINFO, RealizeObject, ColorInfo and the
   dithering as 3.1's, BitBlt with every ROP3 and mono <-> colour, Output
   (polylines, scan lines), Pixel, ScanLR, ExtTextOut/StrBlt/GetCharWidth,
   DeviceBitmapBits, SetDIBitsToDevice, FastBorder, SaveScreenBitmap, the
   software cursor, Enable/Disable, all 80 resources. Boots to Program
   Manager; pixel-identical boot screen with Microsoft's resources.
2. **Done**: pixel-exact boot screens and scenarios: the boot-test cases
   above; lines and their styles (VG5) and text (VG6) as 3.1's.
3. The full ROP set and every Output style checked by probes, styled
   lines as 3.1 draws them, DIBs (RLE, over 64K: done, GD7), screen switching
   (woados, winprompt), cursors, escapes, EnumObj's order.
4. All of w16 on w16's VGA.DRV.
5. Performance.

## 386 Enhanced mode: full-screen DOS boxes

As the DDK's Display Driver Adaptation Guide ("Display hardware") asks:
Enable hooks INT 2Fh (INT 21h 35h/25h) when `__WINFLAGS` has
WF_ENHANCED and calls 4000h (Enable VM-Assisted Save/Restore: the driver
keeps SaveScreenBitmap's bits in video memory beyond the screen), Disable
calls 4007h and gives the vector back. 4001h (Notify Background Switch):
the cursor off, saved bits discarded, and nothing drawn on the screen
until 4002h (surf_get gives no screen surface; the cursor stays off).
4002h (Notify Foreground Switch): the registers as the drawing code wants
them, the cursor back, and USER's RepaintScreen (USER.275, by
GetProcAddress), postponed while UserRepaintDisable(1) holds. 4006h:
the registers reset. Without this Windows went on drawing over a
full-screen DOS box under w16's WIN386, and under Microsoft's WIN386 the
desktop where a DOS window had been was never repainted (boot-test case
`win386tree`).

## Not verified yet (UNVERIFIED)

- Styled polylines turning from slanted y segments to x ones (above).
- RLE streams whose runs go past the DIB's width (clipped here; 3.1 left
  two pixels of an absolute run out in one such stream, not followed up),
  the RLE encoder's choices beyond GD7's lines (dib.c), and GetDIBits in
  RLE of lines wider than 512 pixels (cut here).
- The dithering of all 2^24 colours (VG4, tests/vgacolor.sh: written, to
  run on the build host; VG1's 4913 colours and 2553 more are identical).
- That nothing but the driver reads OEMBIN 2 and STRINGS 1000/1010 (w16's
  hold a note).
- 386 Enhanced mode beyond full-screen switches: 4003h/4004h critical
  sections around the hardware (Microsoft's driver makes dozens; w16's
  makes none: the VDD's switches come from SHELL calls, never while the
  driver draws); 4005h is not acted on (nothing kept).
