# w16 VGA display driver (`VGA.DRV`, module `DISPLAY`)

w16's own display driver for Windows 3.1: 640x480, 16 colours, 96 dpi
(ARCHITECTURE.md 6.3, 6.10). Like every w16 driver it is to be a genuine
Windows 3.1 driver (ARCHITECTURE.md 6.7): it implements the GDI device
driver interface as the Windows 3.1 DDK documents it, loads on real
Windows 3.1 as well as on w16, and has no w16-only interfaces.

**Status: not implemented.** w16 still runs Microsoft's `VGA.DRV`. What is
here so far is the driver's **graphics**: the bitmaps, icons and cursors a
Windows 3.1 display driver carries as resources and that USER draws the
whole interface with (window frames, caption buttons, scroll bars, check
boxes, message-box icons, the mouse pointers). USER finds them in the
display driver (`user/icon.c`: `LoadBitmap(NULL, OBM_...)` and the system
icons and cursors go to the module `DISPLAY`), so while w16 runs
Microsoft's driver, everything of that kind on screen is Microsoft's art.

## Layout

- `resources.md` - every resource of the reference driver: type, ID, SDK
  name, size, colour depth, header kind, cell layout, hot spot, load
  flags, and what it is for. Format facts only, read from the resource
  headers by `tools/vgainv.py`.
- `art/*.txt` - the source art, one file per resource, drawn as text
  (format: `tools/vgaart.py`). All of it is w16's own design; nothing was
  taken from Microsoft's pixels (ARCHITECTURE.md 3).
- `make vgaart` builds `build/vga/*.bmp`, `*.ico`, `*.cur` (what a resource
  script will take once the driver exists). `tools/vgaart.py sheet` and
  `mockup` draw a contact sheet and a 3.1-style screen made with the art.

## The look

One style across the set:

- **Colour frame pieces** (caption buttons, scroll arrows, the system-menu
  box): 4-bit DIBs in the 3.1 button convention - silver face, white light
  on the top and left, a two-pixel gray shadow on the right and bottom,
  black glyphs; pressed: a one-pixel gray edge on the top and left and the
  glyph moved one pixel down and right; disabled: the glyph etched in gray
  over white.
- **Glyphs:** scroll arrows are arrows with a head and a stem; minimise is
  a triangle down onto a bar, maximise a triangle up to a bar, restore the
  two triangles apart; the system-menu box keeps the long bar (Alt+Space)
  and, for document windows, the short one (Alt+-). The cascade arrow,
  combo arrow and size boxes use the same shapes in black on white.
- **Monochrome pieces** (check mark, check boxes, radio buttons, size
  boxes, the 2.x-style frame pieces): black on white; USER draws the 0
  bits in the text colour and the 1 bits in the background colour.
  Checked boxes hold a check mark, pushed ones a doubled frame, the
  three-state "grayed" state a dithered check mark.
- **Icons:** a black outline and a one-pixel gray shadow down and to the
  right: a red octagon with a white bar (stop), a speech balloon with a
  blue question mark, a yellow triangle with an exclamation mark, a blue
  disc with a white i; each also as 8 colours and black and white.
- **Cursors:** black with a white outline, so they show on any
  background; the I-beam and nothing else inverts the screen.

## Not yet done

- The driver itself (the DDI entry points, `GDIINFO`, the VGA code).
- The non-graphic resources: `OEMBIN 1` and `OEMBIN 2` (112 and 208
  bytes), `OEMBIN FONTS` (80 bytes, the stock fonts' LOGFONT data),
  `STRINGS 1000` and `1010`, and the version resource. These are data,
  to be worked out from documentation and black-box observation.
- A check on Windows 3.1 that USER draws the art as intended (the
  cell meanings of OBM_CHECKBOXES and OBM_BTNCORNERS, the 8-colour icon
  images: `resources.md`).
