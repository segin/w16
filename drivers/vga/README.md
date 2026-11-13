# w16 VGA display driver (`VGA.DRV`, module `DISPLAY`)

w16's own display driver for Windows 3.1: 640x480, 16 colours, 96 dpi
(ARCHITECTURE.md 6.3, 6.10), the `display.drv=vga.drv` of SYSTEM.INI's
`[boot]`. Like every w16 driver it is a genuine Windows 3.1 driver
(ARCHITECTURE.md 6.7): it implements the GDI device driver interface as
the Windows 3.1 DDK documents it, loads on real Windows 3.1 as well as on
w16, and has no w16-only interfaces. `make` builds `build/dos/VGA.DRV`
and puts it in the install tree (`build/w16/SYSTEM`).

**Status: stage 1 of 5 done** (Standard mode): Windows 3.1 boots to
Program Manager on it, and with Microsoft's resources linked in for the
comparison the screen is pixel-identical to Microsoft's driver's
(`tests/vgaprobe.sh`); every value programs can see through the driver
(GDIINFO, system metrics and colours from its configuration resource,
the stock fonts, GetNearestColor, the dithering of solid brushes on colour
and monochrome bitmaps, the bitmap formats, Inquire, the VGA palette) is
the same as with Microsoft's (VG1, VG3, VG4). See "Stages" below.

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
- `dib.c` - DeviceBitmapBits (SetDIBits/GetDIBits), SetDIBitsToDevice.
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
`__0040H` only.

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
  build host.

## Stages

1. **Done**: the inventory and the probes (VGREC, VG1, VG3, VG4); the
   driver: every export, the GDIINFO, RealizeObject, ColorInfo and the
   dithering as 3.1's, BitBlt with every ROP3 and mono <-> colour, Output
   (polylines, scan lines), Pixel, ScanLR, ExtTextOut/StrBlt/GetCharWidth,
   DeviceBitmapBits, SetDIBitsToDevice, FastBorder, SaveScreenBitmap, the
   software cursor, Enable/Disable, all 80 resources. Boots to Program
   Manager; pixel-identical boot screen with Microsoft's resources.
2. Pixel-exact boot screens and scenarios (boot-test cases `vga`,
   `vgaall`; `tests/gdiscenarios.sh`-style runs on both drivers).
3. The full ROP set and every Output style checked by probes, styled
   lines as 3.1 draws them, DIBs (RLE, over 64K), screen switching
   (woados, winprompt), cursors, escapes, EnumObj's order.
4. All of w16 on w16's VGA.DRV.
5. Performance.

## Not verified yet (UNVERIFIED)

- Line pixels (Bresenham's choice between two pixels), styled lines'
  lengths, hatch patterns, the monochrome value of a colour on a
  monochrome bitmap (rgb_mono), ScanLR's start pixel, emboldening
  (TC_EA_DOUBLE), colour -> monochrome BitBlt's background rule, EnumObj's
  order, the escapes Control supports.
- That nothing but the driver reads OEMBIN 2 and STRINGS 1000/1010 (w16's
  hold a note).
- 386 Enhanced mode (INT 2Fh 4000h-4007h, the grabber).
