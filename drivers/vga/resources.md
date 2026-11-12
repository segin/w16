# VGA.DRV resources

The resources of Windows 3.1's `WINDOWS\SYSTEM\VGA.DRV` (module `DISPLAY`,
"Windows VGA Display Driver" 3.10), from its NE resource table and the
headers in front of each image (`python3 tools/vgainv.py VGA.DRV`; the
pixels were never read). IDs and names: the Windows 3.1 SDK's `windows.h`
(`OBM_*`, `OCR_*`, `OIC_*`; OpenWatcom `h/win/win16.h`). These are format
facts (ARCHITECTURE.md 3); w16's art for each is `art/<name>.txt`.

**80 resource-table entries:** 34 bitmaps; 11 group cursors and their
11 cursor images; 5 group icons and their 13 icon images; 3 `OEMBIN`,
2 `STRINGS`, 1 version resource.
Graphics: 34 bitmaps + 11 cursors + 5 icons = 50 art files.

Load flags: `0C70` moveable, pure, preload; `0C30` moveable, pure, loaded
on demand; `0C40` fixed, preload; `1C50` moveable, preload, discardable;
`1C30` moveable, pure, discardable; `1C10` moveable, discardable.

## Formats

- **Bitmaps** are DIBs, 1 or 4 bits per pixel, uncompressed, with either a
  `BITMAPCOREHEADER` ("core") or a `BITMAPINFOHEADER` ("info"); the
  reference mixes the two, and w16 keeps each resource's kind. 4-bit colour
  tables have the 16 colours in the order black, maroon, green, olive,
  navy, purple, teal, **gray (128), silver (192)**, red, lime, yellow,
  blue, fuchsia, cyan, white; 1-bit tables are black, white. USER blits the
  1-bit ones with the 0 bits in the text colour and the 1 bits in the
  background colour.
- **Icons** (`RT_ICON`) are `BITMAPINFOHEADER` images with doubled height
  (XOR then AND mask). Each 32x32 group lists three: 4 bpp marked
  "8 colours", 1 bpp ("2 colours"), 4 bpp ("16 colours"), in that order.
  OIC_SAMPLE is one 64x64 1-bpp image.
- **Cursors** (`RT_CURSOR`): the hot spot (two words), then a 32x32 1-bpp
  `BITMAPINFOHEADER` image with black/white colours, XOR then AND; the
  group lists it as 32x64, 1 bpp.

## Bitmaps

| ID | Name | Size | bpp | Header | Flags | Cells | What it is |
|---|---|---|---|---|---|---|---|
| 32754 | OBM_CLOSE | 36x18 | 4 | core | 0C70 | 2 x 18x18 | system-menu box: left cell for a top-level window, right cell for an MDI child; USER blits 18 wide (SM_CXSIZE) from x 0 or 18 and draws the black line after it |
| 32753 | OBM_UPARROW | 17x17 | 4 | core | 0C70 | | scroll-bar arrow, up (its own black edge all round) |
| 32752 | OBM_DNARROW | 17x17 | 4 | core | 0C70 | | scroll-bar arrow, down |
| 32751 | OBM_RGARROW | 17x17 | 4 | core | 0C70 | | scroll-bar arrow, right |
| 32750 | OBM_LFARROW | 17x17 | 4 | core | 0C70 | | scroll-bar arrow, left |
| 32743 | OBM_UPARROWD | 17x17 | 4 | core | 0C70 | | the same, pressed |
| 32742 | OBM_DNARROWD | 17x17 | 4 | info | 0C70 | | |
| 32741 | OBM_RGARROWD | 17x17 | 4 | core | 0C70 | | |
| 32740 | OBM_LFARROWD | 17x17 | 4 | core | 0C70 | | |
| 32737 | OBM_UPARROWI | 17x17 | 4 | info | 0C70 | | the same, disabled (3.1 only) |
| 32736 | OBM_DNARROWI | 17x17 | 4 | info | 0C70 | | |
| 32735 | OBM_RGARROWI | 17x17 | 4 | info | 0C70 | | |
| 32734 | OBM_LFARROWI | 17x17 | 4 | info | 0C70 | | |
| 32749 | OBM_REDUCE | 19x18 | 4 | core | 0C70 | | minimise button; column 0 is the black line on its left (USER blits SM_CXSIZE + 1 = 19 wide, 18 high inside the caption's black lines) |
| 32748 | OBM_ZOOM | 19x18 | 4 | core | 0C70 | | maximise button |
| 32747 | OBM_RESTORE | 19x18 | 4 | core | 0C70 | | restore button (maximised window) |
| 32746 | OBM_REDUCED | 19x18 | 4 | info | 0C70 | | minimise, pressed |
| 32745 | OBM_ZOOMD | 19x18 | 4 | info | 0C70 | | maximise, pressed |
| 32744 | OBM_RESTORED | 19x18 | 4 | info | 0C70 | | restore, pressed |
| 32739 | OBM_MNARROW | 7x11 | 1 | info | 0C70 | | menu arrow: item opening a cascading menu |
| 32738 | OBM_COMBO | 7x9 | 1 | info | 0C70 | | combo-box drop-down arrow, centred on the button |
| 32760 | OBM_CHECK | 14x14 | 1 | core | 0C70 | | menu check mark (also the default for SetMenuItemBitmaps; USER's GetMenuCheckMarkDimensions is 14x14) |
| 32759 | OBM_CHECKBOXES | 56x39 | 1 | core | 0C70 | 4 x 3 of 14x13 | check boxes and radio buttons; USER blits 13x13 from each cell. Rows: check box, radio button, three-state box; columns: off, on, pushed off, pushed on (three-state row: column 1 the indeterminate "grayed" state). Cell meanings as `user/button.c` uses them; UNVERIFIED against the reference beyond what screenshots show of 3.1 dialogs |
| 32761 | OBM_BTSIZE | 15x15 | 1 | core | 0C70 | | size box: the square where a window's two scroll bars meet |
| 32766 | OBM_SIZE | 13x14 | 1 | core | 0C70 | | size box, Windows 2.x style |
| 32758 | OBM_BTNCORNERS | 30x10 | 1 | core | 0C70 | 3 x 10x10 (w16's guess) | push-button corners. **Cell layout not classified**: nothing in the SDK says how 30x10 divides; w16 draws three 10x10 circles (outline, thick default-button outline, filled mask) whose quarters are the four corners |
| 32767 | OBM_OLD_CLOSE | 50x19 | 1 | info | 0C30 | 2 x 25x19 | system-menu box for Windows 2.x-style applications (window, MDI child) |
| 32765 | OBM_OLD_UPARROW | 15x15 | 1 | core | 0C30 | | 2.x-style scroll arrow, up |
| 32764 | OBM_OLD_DNARROW | 15x15 | 1 | core | 0C30 | | 2.x-style scroll arrow, down |
| 32763 | OBM_OLD_RGARROW | 15x15 | 1 | core | 0C30 | | 2.x-style scroll arrow, right |
| 32762 | OBM_OLD_LFARROW | 15x15 | 1 | core | 0C30 | | 2.x-style scroll arrow, left |
| 32757 | OBM_OLD_REDUCE | 25x19 | 1 | core | 0C30 | | 2.x-style minimise button |
| 32756 | OBM_OLD_ZOOM | 25x19 | 1 | core | 0C30 | | 2.x-style maximise button |
| 32755 | OBM_OLD_RESTORE | 25x19 | 1 | core | 0C30 | | 2.x-style restore button |

The OBM_OLD_* IDs are the values the 2.x SDK gave OBM_CLOSE, OBM_UPARROW
... (win16.h keeps both sets): applications built for Windows 2.x get the
flat monochrome pieces.

## Icons

| Group ID | Name | Images (RT_ICON ID) | Flags | What it is |
|---|---|---|---|---|
| 32512 | OIC_SAMPLE / IDI_APPLICATION | 24: 64x64, 1 bpp | 1C30 (group), 1C10 | default application icon |
| 32513 | OIC_HAND / IDI_HAND | 12: 32x32 4 bpp "8 colours"; 13: 1 bpp; 14: 4 bpp 16 colours | 1C50 | stop (MB_ICONSTOP) |
| 32514 | OIC_QUES / IDI_QUESTION | 15, 16, 17 (as above) | 1C50 | question (MB_ICONQUESTION) |
| 32515 | OIC_BANG / IDI_EXCLAMATION | 18, 19, 20 | 1C50 | warning (MB_ICONEXCLAMATION) |
| 32516 | OIC_NOTE / IDI_ASTERISK | 21, 22, 23 | 1C50 | information (MB_ICONINFORMATION) |

w16's sources hold the 16-colour and the 1-bit image; the "8 colours"
image is derived by `tools/vgaart.py` (each colour to the nearest of black,
red, lime, yellow, blue, fuchsia, cyan, white: gray to black, silver to
white). UNVERIFIED which 8 colours the reference's image is limited to,
and when USER picks it.

## Cursors

| Group ID | Name | RT_CURSOR ID | Hot spot | Flags (group / image) | What it is |
|---|---|---|---|---|---|
| 32512 | OCR_NORMAL / IDC_ARROW | 1 | 0,0 | 0C40 / 0C40 | the arrow |
| 32513 | OCR_IBEAM / IDC_IBEAM | 7 | 15,16 | 1C50 / 1C50 | text I-beam |
| 32514 | OCR_WAIT / IDC_WAIT | 2 | 16,16 | 0C40 / 0C40 | hourglass |
| 32515 | OCR_CROSS / IDC_CROSS | 10 | 15,16 | 1C30 / 1C10 | cross hair |
| 32516 | OCR_UP / IDC_UPARROW | 8 | 7,0 | 1C50 / 1C50 | vertical arrow |
| 32640 | OCR_SIZE / IDC_SIZE | 11 | 14,16 | 1C30 / 1C10 | move/size (four-headed arrow) |
| 32641 | OCR_ICON / IDC_ICON | 9 | 8,8 | 1C50 / 1C50 | dragging an icon |
| 32642 | OCR_SIZENWSE / IDC_SIZENWSE | 3 | 7,6 | 1C50 / 1C50 | sizing, top-left / bottom-right corner |
| 32643 | OCR_SIZENESW / IDC_SIZENESW | 4 | 24,6 | 1C50 / 1C50 | sizing, top-right / bottom-left corner |
| 32644 | OCR_SIZEWE / IDC_SIZEWE | 5 | 17,8 | 1C50 / 1C50 | sizing, left / right edge |
| 32645 | OCR_SIZENS / IDC_SIZENS | 6 | 14,10 | 1C50 / 1C50 | sizing, top / bottom edge |

OCR_SIZEALL (32646) and the drag cursor 100 are USER's own resources, not
the driver's. w16's cursors are drawn centred on these hot spots.

## Other resources (not graphics; not done)

| Type | ID | Size | Flags | Notes |
|---|---|---|---|---|
| OEMBIN | 1 | 112 | 0C70 | driver configuration data USER reads (believed to be system metrics and default colours; to be established) |
| OEMBIN | FONTS | 80 | 0C70 | the stock fonts' LOGFONT headers (OEM, ANSI fixed and variable) |
| OEMBIN | 2 | 208 | 0C30 | not yet identified |
| STRINGS | 1000 | 128 | 0C30 | not yet identified |
| STRINGS | 1010 | 304 | 0C30 | not yet identified |
| VERSION | 1 | 464 | 0C30 | version resource ("DISPLAY", "VGA.DRV", 3.10); w16 writes its own strings |
