# The Cardfile .CRD format, as observed

What Windows 3.1's Cardfile writes and reads, and what w16's Cardfile
(`shell/cardfile/`) therefore writes. Everything here was observed
black-box: card files made by the original Cardfile under scripted input
(SNAP.EXE, `tests/cfrun.sh`, the probes in `tests/scripts/cfprobe/` and the
scenarios in `tests/scripts/cf/`) and compared byte by byte. It agrees with
two published descriptions, used for facts only:

- Microsoft Knowledge Base article Q99340, "Windows 3.1 Card File Format"
  (archived at ftp.microsoft.com/MISC/KB/en-us/99/340.HTM; © Microsoft, no
  licence for the text, so it is not quoted). It describes the RRG layout
  and how to find the end of an OLE object by parsing it.
- The Cardwin project's notes (cardwin.sourceforge.net; no licence stated),
  which describe both signatures.

All numbers are little-endian. "Bytes" are the ANSI character set: card
text typed as é ü ß is stored as E9 FC DF (CF3), not converted to OEM.

## Signatures: MGC and RRG

| Offset | Size | MGC (3.0; 3.1 without objects) | RRG (3.1 with objects) |
|---|---|---|---|
| 0 | 3 | `MGC` | `RRG` |
| 3 | 4 | — | the next object number |
| 3 or 7 | 2 | number of cards | number of cards |

Cardfile 3.1 writes **MGC** when no card has a picture and **RRG** as soon
as one does (CF2, CF6); a new file starts as MGC. "Save As" offers *Card
File* (the automatic choice) and *3.0 Card File* (always MGC); the choice
stays for the rest of the session (CF6, CF8). Saving a file with pictures as
3.0 first asks (string 521, Yes/No) and turns each picture into a
monochrome bitmap (below).

The card count is a WORD: **65535 cards** is the format's bound, and
w16's only one.

## The index

`count` entries of 52 bytes follow the header (at 5 or 9):

| Offset | Size | Contents |
|---|---|---|
| 0 | 6 | zero |
| 6 | 4 | file offset of the card's body |
| 10 | 1 | zero |
| 11 | 40 | the index line: text, a NUL, then whatever was in the field |
| 51 | 1 | zero |

Entries are in index order: Windows' language order ignoring case
(`lstrcmpi`: apple, Mango, Zebra; CF2).

**The 40-byte field.** The index line is at most 40 characters (the Index
and Add boxes limit their edit control to 40, and the control, which does
not scroll, refuses what does not fit: 40 i's, 39 digits, 39 b's, 23 W's;
CF3, CF4). The bytes after the line's NUL are not cleared (CF4, CF5):
- *Index* (F6) writes the line and its NUL over the start of the card's own
  field and leaves the rest: "aaaaaaaaaaaaaaaaaaaa" then "c" then "dd"
  gives `dd\0aaaaaaaaaaaaaaaaa`;
- *Add* (F7) starts the new card's field as a copy of the front card's;
- *Duplicate* copies the field whole;
- a file's fields are kept as read, and written back unchanged.
A new file's first card has an all-zero field.

## Card bodies

The bodies follow the index, in index order, each at the offset its entry
gives, with nothing between them. A blank card is 4 zero bytes in both
formats.

**MGC** (CF2, CF6):

| Size | Contents |
|---|---|
| 2 | picture size in bytes, 0 if none |
| 2, 2, 2, 2 | (if a picture) width, height, x, y in pixels |
| size | (if a picture) the bitmap's bits: monochrome, one plane, rows of whole words, top row first (GetBitmapBits) |
| 2 | text length |
| n | the text, CR LF between lines, no NUL |

**RRG** (CF6, CF7, CF8):

| Size | Contents |
|---|---|
| 2 | 1 if the card has an object, else 0 |
| 4 | (if an object) its number |
| … | (if an object) the object, as OleSaveToStream writes it |
| 2, 2 | (if an object) the character width and height it was placed with (8, 15 on VGA) |
| 8 | (if an object) its rectangle on the card body: left, top, right, bottom, pixels |
| 2 | (if an object) 0 embedded, 1 linked, 2 static |
| 2 | text length |
| n | the text |

The object's length is not stored; a reader finds its end by parsing the
OLE 1.0 stream (Q99340): version DWORD 0501h; format DWORD (1 linked,
2 embedded, 3 static); class name (DWORD length, characters with NUL);
then for a static object width and height (LONGs, HIMETRIC; height
negative) and the presentation data (DWORD size, data); for an embedded
one the topic and item names (length-prefixed), the native data (DWORD
size, data) and a presentation object; for a linked one topic, item,
network name, two DWORDs (network type, update option) and a presentation
object. A presentation object is a version DWORD, a format DWORD (0: none
follows), a class name; for METAFILEPICT, BITMAP and DIB width, height and
sized data; otherwise a clipboard format DWORD (0: a length-prefixed format
name follows) and sized data.

**Pictures on 3.1 are objects.** A bitmap pasted in Picture mode becomes a
static object of class `BITMAP` (CF6): HIMETRIC size from the bitmap
(40 x 30 pixels: 1058, -794), its presentation data a 14-byte BITMAP
structure (type 0, width, height, bytes per row, planes, bits per pixel,
bmBits 0) and the bits in the display's format (VGA: 4 planes; the row
padding bytes hold whatever was in memory). Its rectangle is 0,0,40,30, its
type 2.

**3.0 pictures in 3.1** (CF8). Opening an MGC file with a picture makes
the picture a static `BITMAP` object; saved, it has HIMETRIC size 0 x 0,
the presentation is the monochrome bitmap as it was (planes 1, bits 1), the
rectangle is x, y, x + width - 1, y + height - 1, type 2.

**Objects saved as 3.0** (CF6). The picture becomes a monochrome bitmap one
pixel wider and higher than its rectangle, the object drawn into the
rectangle; the extra row and column are not drawn and hold whatever was in
memory.

## Object numbers

The RRG header's DWORD is the next object number: a new file pasting one
picture gives object 0 and header 1 (CF6). Opening a 3.0 file with one
picture and saving it as 3.1 gave object 2 and header 3 (CF8): numbers are
also used up while a card is shown and stored. UNVERIFIED: the full rule
(w16 numbers each object it creates, paste, insert or conversion, from the
header's value).

## Round trips

Open and save without changes rewrites the same bytes for MGC files (the
index fields included; the bodies are copied). For RRG files the objects
go through OLECLI (OleLoadFromStream, OleSaveToStream) and come back as
OLECLI writes them.
