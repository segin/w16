# The Write (.WRI) file format, as observed

What Windows 3.1's Write reads and writes, worked out black-box: the
original Write was driven by scripted input on the reference install
(probes WR1-WR5, `tests/wrobs.sh`, `tests/scripts/write/`), and the
documents it saved were compared with each other and with Microsoft's own
.WRI files (README.WRI, NETWORKS.WRI, PRINTERS.WRI, SYSINI.WRI,
WININI.WRI from Windows 3.1, PRACTICE.WRI from Windows/386 2.11). No
Windows code was read (ARCHITECTURE.md 3). No published specification
was found to cite (searches for one turned up only identification data,
e.g. PRONOM's signature entries x-fmt/4 and x-fmt/371); the field names
below are w16's own.

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

The text bytes, in the Windows character set. Paragraphs end in CR LF.
The rest of the last text page is undefined: the original leaves stale
memory there (WR2: re-saving WININI.WRI changed those bytes).

Write's document always ends with an implicit end paragraph: the last
paragraph FOD reaches fcMac + 2, as if a CR LF followed the text. If the
text does not end in CR LF, the last paragraph's text belongs to that
end paragraph (WR1: "Hello world", one FOD to fcMac + 2). Applying any
paragraph format to that last paragraph first gives it a real CR LF (in
the last character's properties); the end paragraph then has the same
properties as it (WR3, WR4).

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
  FOD whose FPROP bytes equal the page's last stored FPROP shares it;
  equal bytes further back are stored again (NETWORKS.WRI). UNVERIFIED:
  whether a default FOD in between ends the sharing.
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
| 0 | byte | 3Ch (3Dh in some of Microsoft's files; meaning UNVERIFIED) |
| 1 | byte | alignment: 0 left, 1 centered, 2 right, 3 justified |
| 2 | word | 30 |
| 4 | word | right indent |
| 6 | word | left indent |
| 8 | word | first-line indent, relative to the left indent |
| 10 | word | line spacing: 240 single, 360 1 1/2, 480 double |
| 12 | 4 | 0 |
| 16 | byte | running-head bits (headers and footers; plan: the header probes) |
| 17 | 5 | 0 |
| 22 | 4 x n | tab stops: word position (from the left margin), byte 0 left / 3 decimal (UNVERIFIED: decimal not yet observed), byte 0 |

The default is {3Ch, 0, 30, 0, 0, 0, 240, ...}. An FPROP keeps the bytes
up to the last one that differs from the default; with tab stops it keeps
them all plus one empty (zero) stop (WR4: two stops give cch 34;
README.WRI's eleven give 70). A paragraph that was formatted at all keeps
at least one byte: Left on a centered paragraph is stored as cch 1, {3Ch}
(WR5); only Normal stores none (FFFFh, WR3). New paragraphs (Enter) take
the properties of the paragraph they were split from. Tab stops set with
Document > Tabs go to every paragraph (WR4); indents and alignment to the
selected ones.

## Section properties (SEP)

One page, written only once the page layout was set (WR4; UNVERIFIED:
whether OK on an unchanged Page Layout writes it). cch is 102 in every
file seen; after it:

| offset | size | |
|---|---|---|
| 0 | word | 0200h |
| 2 | word | page height (15840) |
| 4 | word | page width (12240) |
| 6 | word | first page number (1) |
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

Present with a SEP: word count 2, word count 2, then two 10-byte entries
{dword cp, word fn, dword fc}: (text length, fn, the SEP page's fc) and
(text length + 1, 7FFFh, FFFFFFFFh). fn is 1 for a new document, 2 for
one Write read from a file (WR4, WR5: SYSINI.WRI's 4 became 2;
UNVERIFIED: presumably Write's internal file number). The rest of the
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

## Not yet observed (plan)

Pictures (metafile, bitmap), OLE objects, headers and footers, page
breaks inserted by Repaginate, decimal tabs, the 3.0 Write save format and
Word for DOS output. Each gets a WR probe; this file grows with them.
