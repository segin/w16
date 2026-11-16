# The Write (.WRI) file format, as observed

What Windows 3.1's Write reads and writes, worked out black-box: the
original Write was driven by scripted input on the reference install
(probes WR1-WR5, `tests/wrobs.sh`, `tests/scripts/write/`), and the
documents it saved were compared with each other and with Microsoft's own
.WRI files (README.WRI, NETWORKS.WRI, PRINTERS.WRI, SYSINI.WRI,
WININI.WRI from Windows 3.1, PRACTICE.WRI from Windows/386 2.11). No
Windows code was read (ARCHITECTURE.md 3).

The format is also documented in the Windows 3.1 SDK: Microsoft
Corporation, *Microsoft Windows Programmer's Reference, Volume 4:
Resources* (Microsoft Press, 1992), chapter 8 "Write File Format" ("SDK
ch. 8" below); the field names here are its names where it has one. That
chapter was used for facts only, and each was checked against files 3.1's
Write writes; where Write's files differ from the chapter, this document
says what Write does. Most of what follows was observed first and agrees
with the chapter; what the chapter added is marked "SDK ch. 8".

`tools/wri.py` implements everything here: `dump` decodes a file, `cmp`
compares two files ignoring the bytes the original leaves undefined, and
its `rebuild` writes the FKP pages again from the decoded runs. The
rebuild reproduces all six Microsoft files and every document the probes
saved, byte for byte outside those undefined bytes. That is the check
that the FKP layout rules below are complete.

Everything is little-endian. Measurements are in twips (1/1440 inch);
"cp" is a character position in the text, "fc" a file offset.

## Pages

A file is a sequence of 128-byte pages:

| pages | contents |
|---|---|
| 0 | header |
| 1 .. pnChar-1 | text, from fc 128 to fcMac |
| pnChar .. pnPara-1 | character properties: FKP pages |
| pnPara .. pnFntb-1 | paragraph properties: FKP pages |
| pnFntb .. pnSep-1 | footnote table (always empty in Write) |
| pnSep .. pnSetb-1 | section properties (SEP), 0 or 1 page |
| pnSetb .. pnPgtb-1 | section table (SETB), 0 or 1 page |
| pnPgtb .. pnFfntb-1 | page table (PGTB), 0 or more pages |
| pnFfntb .. pnMac-1 | font table (FFNTB), 0 or more pages |

pnChar is not stored: it is (fcMac + 127) / 128. An empty table is
written as no pages (its pn equals the next one).

## Header (page 0)

| offset | size | value |
|---|---|---|
| 0 | word | wIdent: BE31h (BE32h for a document with OLE objects, UNVERIFIED until the OLE probes) |
| 2 | word | 0 |
| 4 | word | AB00h |
| 6-13 | | 0 |
| 14 | dword | fcMac: 128 + the text length |
| 18 | word | pnPara |
| 20 | word | pnFntb |
| 22 | word | pnSep |
| 24 | word | pnSetb |
| 26 | word | pnPgtb |
| 28 | word | pnFfntb |
| 30-95 | | 0 |
| 96 | word | pnMac: the file's length in pages |
| 98-127 | | 0 |

A Word for DOS file has the same identification words but pnMac 0; Write
treats it, like any non-Write file, as one to convert.

## Text

The text bytes, in the Windows character set. Paragraphs end in CR LF;
SDK ch. 8 adds character 12 for an explicit page break and 9 for a tab
(page breaks: not yet observed).
The rest of the last text page is undefined: the original leaves stale
memory there (WR2: re-saving WININI.WRI changed those bytes).

Write's document always ends with an implicit end paragraph: the last
paragraph FOD reaches fcMac + 2, as if a CR LF followed the text. If the
text does not end in CR LF, the last paragraph's text belongs to that
end paragraph (WR1: "Hello world", one FOD to fcMac + 2). Applying any
paragraph format to that last paragraph first gives it a real CR LF (in
the properties typing would have there, WR8); the end paragraph then has
the same properties as it (WR3, WR4). (SDK ch. 8 says the last
paragraph FOD ends at fcMac; Write's own files end it at fcMac + 2.)

## FKP pages

Both property tables are runs of FODs in FKP pages:

| offset | size | |
|---|---|---|
| 0 | dword | fcFirst: the first fc the page describes |
| 4 | 6 x cfod | FODs: dword fcLim, word bfprop |
| ... | | free (undefined: stale memory in the original's files) |
| ... | | FPROPs, packed up to byte 126 |
| 127 | byte | cfod |

bfprop is the offset of the FOD's FPROP from byte 4, or FFFFh for the
default properties. An FPROP is a count byte cch and the first cch bytes
of the property structure; the bytes not stored have their default
values. A page's fcFirst is the previous page's last fcLim.

How the original lays them out (observed on every file, `wri.py
rebuild`):

- CHP runs: neighbours with equal properties are merged; every FOD has
  its own FPROP, even when an earlier FOD of the page has the same bytes.
- PAP runs: one FOD per paragraph (equal neighbours are not merged). A
  FOD whose FPROP bytes equal the previous FOD's shares its FPROP; equal
  bytes further back, or after a default (FFFFh) FOD, are stored again
  (NETWORKS.WRI, WR6).
- CHP runs are merged as they stand in the original's piece table, which
  is not always the merged form: a document saved right after pasting a
  picture has one run where the re-saved document has three (WR6). On
  loading, the 40-byte header of each picture paragraph gets the default
  character properties (FFFFh) when the document is saved again.
- FODs fill from byte 4 up, FPROPs from byte 126 down; the next FOD goes
  to a new page when its 6 bytes plus its FPROP (1 + cch, unless shared)
  no longer fit between them.

## Character properties (CHP)

| byte | bits | |
|---|---|---|
| 0 | | 1 after Regular and in new text; 0 once any other character format was applied (WR3) |
| 1 | 0 | bold |
| 1 | 1 | italic |
| 1 | 2-7 | font code, low 6 bits (index into the font table) |
| 2 | | size in half points (default 24) |
| 3 | 0 | underline |
| 4 | 0-2 | font code bits 6-8 (UNVERIFIED: no document with more than 64 fonts seen) |
| 5 | | vertical position, signed half points: > 0 superscript, < 0 subscript |

Default (FFFFh): {1, 0, 24, 0, 0, 0}; an FPROP keeps the bytes up to the
last one that differs from it. Observed values: a new document's text is
{1, 0, 20} (font 0 = Arial, 10 points); Bold on gives {0, 1, 20};
superscript at 10 points gives size 16 and position +4, subscript size 16
and position -4 (WR3; other sizes UNVERIFIED); Enlarge Font from 10 points
gives 12, Reduce from 12 gives 10, then 8.

## Paragraph properties (PAP)

| byte | size | |
|---|---|---|
| 0 | byte | 61 (3Dh) by default (SDK ch. 8, 8.3.1: "reserved"); Write sets 60 (3Ch) on a paragraph that was given any format but Normal |
| 1 | byte | alignment: 0 left, 1 centered, 2 right, 3 justified |
| 2 | word | 30 |
| 4 | word | right indent |
| 6 | word | left indent |
| 8 | word | first-line indent, relative to the left indent |
| 10 | word | line spacing: 240 single, 360 1 1/2, 480 double |
| 12 | 4 | 0 |
| 16 | byte | SDK ch. 8: bit 0 footer (0 header), bits 1-2 nonzero for a header or footer paragraph, bit 3 print on the first page, bit 4 picture (observed, WR7) |
| 17 | 5 | 0 |
| 22 | 4 x n | tab stops: word position (from the left margin), byte 0 left / 3 decimal (SDK ch. 8; decimal not yet observed), byte 0 |

The default is {61, 0, 30, 0, 0, 0, 240, 0...} (SDK ch. 8). An FPROP
keeps the bytes up to the last one that differs from the default; with
tab stops it keeps them all plus one empty (zero) stop (WR4: two stops
give cch 34; README.WRI's eleven give 70). Since any format but Normal
sets byte 0 to 60, a formatted paragraph keeps at least that byte: Left
on a centered paragraph is stored as cch 1, {3Ch} (WR5); Normal gives the
default, stored as none (FFFFh, WR3), which the SDK forbids ("cch greater
than or equal to 1") but Write itself writes. New paragraphs (Enter) take
the properties of the paragraph they were split from. Tab stops set with
Document > Tabs go to every paragraph (WR4); indents and alignment to the
selected ones.

## Section properties (SEP)

One page, written only once the page layout was set (WR4; the SDK: when
the section properties differ from the defaults). cch is 102 in every
file seen (the SDK says at most 16); after it:

| offset | size | |
|---|---|---|
| 0 | word | 0200h (SDK ch. 8: "reserved; must be zero") |
| 2 | word | page height (15840) |
| 4 | word | page width (12240) |
| 6 | word | first page number (1; Page Layout's "Start Page Numbers At"; the SDK: "reserved, must be FFFFh") |
| 8 | word | top margin (1440) |
| 10 | word | text height (12960 = height - top - bottom margin) |
| 12 | word | left margin (1800) |
| 14 | word | text width (8640 = width - left - right margin) |
| 16 | word | 0100h |
| 18 | word | header distance from the top (1080) |
| 20 | word | footer position from the top (14760) |
| 22 | 8 | 720, 0, 1080, 10440: the same in every file; meaning UNVERIFIED |
| 30 | | zeros to cch |

The rest of the page is undefined (stale memory).

## Section table (SETB)

Present with a SEP: word count 2, word count 2 (csedMax, "undefined" in
the SDK), then two 10-byte entries
{dword cp, word fn, dword fc}: (text length, fn, the SEP page's fc) and
(text length + 1, 7FFFh, FFFFFFFFh). fn is 1 for a new document, 2 for
one Write read from a file (WR4, WR5: SYSINI.WRI's 4 became 2; the SDK
calls it "undefined"). The rest of the
page is undefined.

## Page table (PGTB)

Written by Repaginate (and printing, UNVERIFIED): word count, word count
(the same), then per page {word page number, dword cp of its first
character}. Spans pages when long. The rest is undefined.

## Font table (FFNTB)

Word count, then per font {word cb, byte family, name, NUL}, cb counting
the family byte, the name and its NUL; a word 0 ends the table. An entry
does not cross a page: FFFFh in place of cb sends the reader to the next
page (UNVERIFIED: exactly when the original breaks). The family byte is
the font's lfPitchAndFamily family (Arial 20h; Times New Roman and Symbol
10h in Microsoft's files). A file without a font table (Write 2.x's,
PRACTICE.WRI) stays without one when 3.1 re-saves it; its font codes then
name families (0 shows as fixed pitch, 1 as sans serif on 3.1).

## Undefined bytes

The original builds pages in memory it does not clear. These bytes differ
between saves of the same document, so no writer can match them, and
`wri.py cmp` ignores them: the rest of the last text page; the free
middle of every FKP page; each SEP, SETB, PGTB and FFNTB page after its
structure. w16's Write writes zeros there.

## Headers and footers

(WR13, SDK ch. 8 8.3.1-8.3.2.) A running head is one or more paragraphs at
the very start of the text, before the body: PAP byte 16 (rhc) 6 for a
header, 7 for a footer, plus 8 for Print on First Page; byte 0 60; the
page's left and right margins as its left and right indents (1800 and
1800 with Page Layout's 1.25" margins); cch 17. A new one goes to the
start of the text: a footer made after a header comes before it. Insert
Page # puts character 1 in the text, its CHP with byte 3 bit 6 set
(fSpecial; shown as "(page)"). Making a running head writes the SEP
(with the first page number FFFFh) and SETB. Write shows the heads only
in the Header and Footer views (window title HEADER or FOOTER, the
modeless Page Header / Page Footer box).

## Pictures and OLE objects

A picture is a paragraph of its own: PAP byte 0 3Dh, byte 16 bit 10h
set (cch 17 in WR7). Its text is a 40-byte header followed by the data,
with no CR LF: the PAP FOD alone ends it. Pasting a bitmap (CF_DIB and
CF_BITMAP on the clipboard) or a metafile picture into 3.1's Write stores
an OLE object even when no server is involved (WR7), and the header's
wIdent becomes BE32h. Header, as seen for objects:

| offset | size | |
|---|---|---|
| 0 | word | mm: E4h for an OLE object; for a picture (SDK ch. 8, 8.2.2) the header starts with a METAFILEPICT whose mm is E3h for a bitmap, else the metafile's mapping mode |
| 2 | word | xExt (0 for objects) |
| 4 | word | yExt (0) |
| 6 | word | object type: 1 static (WR7; SDK ch. 8: 2 embedded, 3 link) |
| 8 | word | horizontal offset from the left indent (0) |
| 10 | word | width shown, twips: the object's HIMETRIC width (OleQueryBounds) turned into screen pixels and back as the mapping modes MM_HIMETRIC and MM_TWIPS do, from HORZSIZE and HORZRES: px = round(hm * HORZRES / (100 * HORZSIZE)), twips = round(px * HORZSIZE * 1440 / (25.4 * HORZRES)). On VGA (208 mm, 640 pixels): 1058 HIMETRIC (a 40-pixel DIB) is 33 pixels, 608 twips; 5080 (2 inches) 2874; 16931 (WR10) 9600; DIBs 123 and 250 pixels wide 1843 and 3740 (WR16) |
| 12 | word | height shown, twips, the same way with VERTSIZE and VERTRES (VGA: 156 mm, 480): 529 HIMETRIC is 16 pixels, 295 twips; 2540 is 1437; 11693 is 6633; DIBs 77, 301 and 3 pixels tall 1161, 4514 and 37 (WR16) |
| 14 | word | 0 |
| 16 | dword | size of the data after the header |
| 20 | dword | 0 |
| 24 | dword | dwObjNum: the object's name as 8 hex digits (SDK ch. 8); a new number in every session (6AF16C57h, then +4 for the next object), kept on re-saving |
| 28 | word | 0 |
| 30 | word | header size, 40 |
| 32 | word | 0 |
| 34 | word | varies between objects (10BFh, 10B7h; UNVERIFIED: a handle) |
| 36 | word | horizontal scale, per mille (1000) |
| 38 | word | vertical scale (1000) |

The data is what OLECLI's OleSaveToStream writes: dword 0501h (OLE
version), dword format (3 static; 2 embedded and 1 linked, UNVERIFIED
until the Insert Object probe), dword length and the class name with its
NUL ("DIB", "METAFILEPICT"), dword width and dword height in HIMETRIC
(height negative: 1058 x -529 for 40 x 20 pixels), dword size, then the
data: a packed DIB (its size rounded up to the global block's, 512 for
504 bytes of DIB, the bytes after the DIB stale memory: 24 of them in
every DIB of WR16); for METAFILEPICT the 16-bit METAFILEPICT (mm 8, xExt
5080, yExt 2540, a stale hMF word) and the metafile's bits. On screen the
picture is as wide and tall as the twips say (rounded to pixels), a
pixel left of where text starts.

## Word for DOS output (WR23)

Save As > "Word for DOS (*.DOC)" writes the same header, text and CHP and
PAP FKP pages as a .WRI, then nothing more: no SEP, SETB, PGTB or FFNTB
page (pnFntb, pnSep, pnSetb, pnPgtb and pnFfntb all equal the page after
the PAP FKPs) and pnMac (word 48, offset 96) is 0; wIdent stays BE31h,
wTool AB00h. "Word for DOS/Txt Only (*.DOC)" writes the text alone, as
Text Files does. Observed for a new document without page layout,
repagination or pictures; the rest UNVERIFIED.

## Not yet observed (plan)

Linked objects (Paste Link), Write 3.0 pictures, page breaks inserted by
Repaginate (a manual page break, Ctrl+Enter, is character 12 ending its
paragraph, the paragraph's fcLim right after it: WR21),
decimal tabs and the 3.0 Write save format. Each
gets a WR probe; this file grows with them.
