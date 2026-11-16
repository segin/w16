# WRITE.EXE handover (branch worktree-agent-a31abeccd3aa6d661)

State on 2026-11-16, last commit before this note e0f0df1. Everything below is committed; the tree is clean.

## What w16's WRITE.EXE does now

`shell/write/` builds `build/dos/WRITE.EXE` (Windows 3.1 only; the 2.x build was dropped: the user's rule is that 2.x builds are Program Manager's alone). `make install-tree` puts it at `WINDOWS\WRITE.EXE`, and it is the first item of the Accessories group (`assets/progman/ACCESSOR.txt`, `make groups`). ARCHITECTURE.md 15.6 describes it. The file format is in `docs/wri-format.md`.

Source files:
- `main.c`: frame, menus, files (COMMDLG Open and Save As with the Backup hook), the Convert prompt, Help (WinHelp topics as the original's), the message loop.
- `doc.c`: the document: gap buffer, CHP runs, paragraphs, interned property atoms, SEP; insert, delete, formats; `doc_copy` for Undo.
- `wrifile.c`: the .WRI reader and writer, laying out FKPs exactly as 3.1 does; text load and save.
- `view.c`: layout, painting, caret, selection, keys, scrolling, the end mark and page marks, "(page)".
- `cmds.c`: Character and Paragraph menus, Indents, Tabs, Page Layout, Go To, Find/Replace, Fonts (CHOOSEFONT), clipboard text, Undo, menu states.
- `pict.c`: draws pictures itself (DIB, metafile, 3.0 bitmap mm E3h).
- `ole.c`: OLECLI: register WINWRITE, load and draw objects lazily, activate, store on OLE_CHANGED/SAVED, Insert Object (DTCREATE), paste of objects and pictures (static).
- `print.c`: Print (PrintDlg plus escapes), Print Setup, Repaginate (fills PGTB).
- `hdrftr.c`: the Header and Footer views and the modeless Page Header/Footer box.
- `util.c`, `i4.asm`: huge memory, strings, the long-arithmetic helpers, `__AHINCR`, the DOS rename.

## Verified against 3.1 (remote, installation-05)

- `tests/writescenarios.sh`, run through `tools/remote-run.sh -n agent-a31abeccd3aa6d661-scen -g build/ref/wrscen tests/writeall.sh [-u]`. The last run (20261116-074125) gives:
  - WR1-WR9: saved files identical to the original's, except for the bytes the original leaves undefined (`tools/wri.py cmp`).
  - Screens: pixel-identical except for the entries in `tests/scripts/write.baseline`: the caret after typing that follows a menu (WR3, WR9 F1), the vertical thumb (WR2 R2), and a tall object shown in bands (WR10 I3).
  - WR10 (Insert Object > Paintbrush) and WR15 (pasted DIB and metafile): files differ only in the object's shown size in twips. The conversion from OleQueryBounds' HIMETRIC is UNVERIFIED (data: 1058x529 gives 608x295, 5080x2540 gives 2874x1437, 16931x11693 gives 9600x6633).
- `tests/writehelp.sh` WR11: all 6 WinHelp requests are the same as the original's.
- Boot case `w16write` (`tools/remote-test.sh -b w16write`): 4 of 4 parts ok (ms, k286, k386, tree) in the full run boottest-080355 (run dir agent-a31abeccd3aa6d661).
- `w16groups` ok, `write` ok, `kbdwrite` ok.
- Full run boottest-080355 (at 881628a): 108 ok, 6 FAIL.
  - olecli@tree, missingdll@tree and treek386 failed because the tree scripts chose Accessories items by position, and Write is now first. Commit e0f0df1 fixes cardtree, missingdlltree and treeitems.
  - taskman@tree, cplclick@co-tree and trails@timednl: probably load or timing on a host at load 50 (trails fails in other agents' runs too). Not yet confirmed.
- make test failed only in castcheck on Write's near casts; c2e833f fixes that.
- A rerun of make test plus those cases is in progress: `tools/remote-test.sh taskman cplclick missingdll olecli treek386 trails w16write w16groups`, local log `/tmp/wrres/full2.txt`, remote results in the newest `build/boottest-*` of run dir agent-a31abeccd3aa6d661. **Check its result first.**

## The .WRI reader/writer against Programmer's Reference vol. 4, ch. 8

All of these are covered and checked against 3.1's own files:
- header (wIdent BE31/BE32, pnMac 0 = Word);
- text: CR LF paragraphs, tabs;
- FKP pages for CHP and PAP: the FPROP prefix rules; PAP default byte 0 61, and 60 once formatted;
- pictures and OLE headers (mm E3h/E4h, object type, dwObjNum);
- the picture paragraph's PAP bit;
- running heads (rhc, character 1 with fSpecial);
- SEP (cch 102 in practice; first page number FFFFh), SETB, PGTB, FFNTB including FFFFh continuation.

Places where Write's files differ from ch. 8 are listed in `docs/wri-format.md`.

Not covered yet:
- character 12 (page break);
- decimal tabs as written by 3.1;
- linked objects;
- 3.0-format pictures written by 3.1;
- Word for DOS output and the 3.0 save format.

`tools/wri.py` has `dump`, `cmp` (ignores stale bytes, word 34 and dwObjNum of objects) and `rebuild` (it reproduces all of Microsoft's .WRI files).

## Commits (this session, after the 2026-11-08 work)

- 9d93fcf: merge master; Write is 3.1 only (2.x build dropped).
- 6c9b666: SDK ch. 8 facts: PAP byte 0 61/60, mm E3h, OLE header.
- 94000a0: Accessories group item and boot case w16write.
- 7103e77: OLE through OLECLI, and Undo.
- 9146ccd: Print, Print Setup, Repaginate.
- fda5e36: converted text in Arial 10, menu states (WR7), SEP FFFFh, frame class 1023h, `tools/remote-run.sh`.
- 6f31f2a: Header and Footer views.
- fc30648: WR9/WR10 file fixes (page-number props, object props, host names).
- 53e3cbd: paste of objects and pictures; caret rules; ARCHITECTURE.md.
- 384bc90, 4a3037d: caret and selection fixes for objects and running heads.
- 30a3fa8, e8f90b9: caret after menus and dialogs; w16write robustness.
- 1119cb8: Undo label "  \tCtrl+Z"; per-part compare files; writehelp masking.
- 49dd5b5: `tests/writeall.sh`.
- 881628a: `write.baseline`.
- 8a55aac: merge master (section 15.6).
- c2e833f: castcheck.
- e0f0df1: tree scripts step over Write.

## Rerun at e0f0df1 (boottest-084318)

make test: pass. w16write, w16groups, olecli, missingdll, treek386: ok. FAIL: taskman@tree
21-dosesc 974.869 pixels (the same count as in boottest-080355, so deterministic, not load:
check whether it comes from the tree's new WRITE.EXE/ACCESSOR.GRP, e.g. against master's own
tree run), cplclick@co-tree 02-palette (6330 against 18927 before: varies), trails@timed
(MoveCursor counts: timing, fails in other agents' runs too).

## Next steps, in order

1. Investigate taskman@tree (above); then cplclick@co-tree. If taskman, cplclick or trails fail again, compare with master's own run before blaming Write. The tree contains WRITE.EXE and the new ACCESSOR.GRP, so look at any tree script that picks Accessories items by position.
2. Run one full `tools/remote-test.sh` (make test plus the full boot test) and report the result.
3. Object size in twips (WR10, WR15): find the HIMETRIC-to-twips rule (probe several sizes of pasted pictures).
4. Tall pictures in bands (WR10 I3: 3.1 scrolls inside a picture).
5. Caret rule after typing that follows a menu (WR3, WR9 F1).
6. Vertical scroll bar range (WR2 R2).
7. The ruler (Document > Ruler On; our own art for its buttons).
8. Confirm Page Breaks and the page-break character 12.
9. Headers and footers in printing.
10. Move/Size Picture.
11. Links, Paste Link, Paste Special; the Object verb menu.
12. Word for DOS and text-only save.
13. A centered or right-aligned line mixing regular and bold sits 2 px off (ARCHITECTURE.md 15.6).

## Rules recap

- All DOSBox work runs on installation-05: `tools/remote-test.sh`, or `tools/remote-run.sh -n <your run name>`. Never use the `w16` run dir.
- Facts only from the SDK, cited.
- Branding: "w16".
- About boxes through ShellAbout.
