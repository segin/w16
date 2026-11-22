# Program Manager audit triage

An outside, read-only audit of `shell/progman/` (PROGMAN.EXE, the 3.x
and 2.x builds; written by another AI model, not in the repository)
counted 5 Critical, 18 High, 26 Medium and 15 Low findings; its list
holds 5, 18, 20 and four bundles of Low ones (C1-C5, H1-H18, M1-M20,
L1-L19 here, in its order, bundles split: 62 in all). Verdicts by the
first of each row: REAL 20, HARDENING 30, FALSE 11, 3.1-BEHAVIOUR 1,
UNSURE 0. Each is
checked against the code, and against Windows 3.1 where a fix would
change what a user or program sees. The audit's line numbers were close.

Probes (all on the reference 3.1, Microsoft's PROGMAN against w16's):
- **DDE** (`tests/ddescenario.sh`, TDDE; boot-test case `ddescen`, one
  part per script): new scripts `args`, `spaced`, `arrange`, `badpath`,
  `exit0`-`exit3`, `grpread`, `longname`, `many`, `missno`, `missyes`,
  `order`, `reload` in `tests/scripts/dde/`. TDDE learned `windows`
  (top-level windows, a dialog's controls with their rectangles, the
  groups), `press`, `ini` and `exists`; ddescenario.sh learned `;get`
  lines, grp* scripts (tools/mkbadgrp.py's files) and the log of a run
  where Windows did not end.
- **Group files** (`tools/mkbadgrp.py`: GOOD, BADSIG, BADSUM, CBBIG,
  CBSMALL, NAMEOFF, NAMEEND, ITEMWRAP, ITEMOFF, ITEMSTR, ICONWRAP,
  ICONHDR, TAGWRAP, TAGSMALL, TAGPATH, TAGNOEND, TRUNC, EMPTY, MANY,
  HOTKEYS), each as Group5 with PROGMAN as the shell, screenshots
  (tests/dropin.sh -s), and the ones 3.1 reads through DDE (`grpread`).
- **Screens** (`tests/pmscenarios.sh`, SNAP; it learned `;ini` and `;grp`
  lines and the log of a run that did not end): `audchgicon`, `audcwd`,
  `audmaxmenu`, `audrelgrp` in `tests/scripts/pm/`.

Verdicts: **REAL** (confirmed; fixed), **FALSE** (the claim is wrong),
**3.1-BEHAVIOUR** (3.1 does the same: kept), **HARDENING** (malformed
input or out of memory only; fixed when cheap and invisible to working
programs), **UNSURE** (needs a probe not run yet). Where 3.1 itself
hangs or crashes (TAGSMALL hangs it; a 250-character AddItem title and
command ended it), w16 survives instead (the user's ruling).

Status: every finding has a verdict and is closed (2026-11-22).

## Critical

| ID | Verdict | Evidence | Action | Commit |
|---|---|---|---|---|
| C1 | REAL | Group Properties, OK, of a group named by a bare PROGMAN.INI entry: group_file_name made the full name, group_set_file wrote it and deleted the "old" bare name, which OpenFile's search resolved to the same file. 3.1 (audrelgrp, Group4=STARTUP.GRP): shows "STARTUP.GRP" in the dialog, keeps the file, and on saving settings writes GroupN= as the full name (before Order=). | read_file keeps the file's full name beside the INI's; group_set_file compares with it and with OF_PARSE's spelling (no change then) and deletes an old file by its full name; GroupN= saved as the full name before Order=. | b9dcd9c |
| C2 | HARDENING | parse's tag loop: `at + cb` wraps in 16 bits. 3.1 calls such a file damaged (TAGWRAP). | `cb > fsize - at`; and valid_file rejects it as 3.1 does. | b9dcd9c |
| C3 | HARDENING | serialize summed 16-bit terms; overlapping icon blocks of a damaged file (ICONWRAP, which 3.1 reads) wrap the sum. | Terms widened to DWORD; icon blocks larger together than the group data are dropped from the model. | b9dcd9c |
| C4 | HARDENING | ne_find (the fallback icon reader; 2.x's only one): a resource table of 1 byte, type records past the table, `8 + n*12` wrapping to 0 (a hang), shifts over 31. | Table of at least 2 bytes, each type's entries inside it, shift at most 16, counts capped. | b9dcd9c |
| C5 | HARDENING | load_mono2x: `(w + 7) / 8` wraps, `2 * rowb * ht` overflows. | Width and height at most 4000h, DWORD arithmetic. | b9dcd9c |

## High

| ID | Verdict | Evidence | Action | Commit |
|---|---|---|---|---|
| H1 | HARDENING | `it + ITEM_SIZE` wraps for an item offset near FFFFh. 3.1: damaged (ITEMWRAP). | `it > size - ITEM_SIZE`; valid_file. | b9dcd9c |
| H2 | HARDENING | make_icon gave CreateIcon cached bits shorter than the header's size; t->icon offsets could pass 64K. 3.1: a header asking for more bits than are cached is damaged (ICONHDR). | valid_file rejects it; make_icon checks the sizes too; blocks together larger than the group data dropped. | b9dcd9c |
| H3 | REAL | write_group truncates the file before writing: on a full disk the group is lost. Not observable on 3.1 without a full disk. | No rewrite without room for the file beyond the clusters it has (INT 21h 36h, `dos_freeclusters` in shell/start.asm). | b9dcd9c |
| H4 | HARDENING | An 8101h path tag without its NUL: read past the tag. 3.1 reads past it too (TAGPATH: four bytes of the end record in the command). | tag_dup reads no further than the tag. | b9dcd9c |
| H5 | HARDENING | drag_move used g after DragObject's loop (a DDE DeleteGroup meanwhile); a press in one group and a move in another started a drag there. | g looked up again; the drag only in the group pressed. | b9dcd9c |
| H6 | HARDENING | group_run_all walked items across pm_exec (error boxes let DDE in). | The group and the i-th item looked up afresh each time. | b9dcd9c |
| H7 | HARDENING | drop_file/drop_files used g across the no-association box. | Looked up again after it. | b9dcd9c |
| H8 | HARDENING | load_dib: a row of 64K truncated to 0 (no memory fault: reads stay in the row buffer); a huge header size wrapped the file offsets. | Width/height below 4000h, header size within the image. | b9dcd9c |
| H9 | HARDENING | pe_data loops forever on a cyclic resource tree. | At most 8 levels. | b9dcd9c |
| H10 | FALSE | best_entry/ico_find/group_choice: the "negative offsets" are far-pointer offsets, which wrap inside the block's 64K (a block of over 2048 entries is larger than 32K, so every offset is inside it); a WORD count read short gives a wrong choice, no fault. | None. | - |
| H11 | REAL (directory); HARDENING (drive letter) | pm_exec changed Program Manager's own drive and directory for an item's working directory and left them. 3.1 (audcwd): after an item with working directory C:\TMP ran, File > Run "NOTEPAD.EXE X.TXT" (X.TXT only in C:\TMP) did not find it; w16 opened it. A failing chdir: the program still starts (as before). A "drive" that is no letter was passed to DOS. | Drive and directory (also the item's drive's) put back after starting; only A-Z taken as a drive. | b9dcd9c |
| H12 | REAL | With a group maximised its control menu is first in the bar, so GetSubMenu(menu, 0) was that menu, not File. 3.1 (audmaxmenu, the empty StartUp maximised): Open, Move, Copy grayed, state 3 (grayed and disabled); w16 left them enabled. | The File menu kept by handle; grayed items MF_GRAYED \| MF_DISABLED as 3.1. | b9dcd9c |
| H13 | REAL | Reload. 3.1 (reload.txt): `[Reload()]` and `[Reload]` reload every group (w16 reloaded none for `()`, as one empty name); `Reload(no such group)` fails (w16: success); w16 hung reloading with minimised groups (the next window, an icon title, went with its group). Without a name the SDK has [Groups] read again; w16 did not. | Reload(name) fails for no such group; without a name every group is unloaded (each pass starting afresh) and load_groups reads Order= and [Groups]. Kept apart: after a Reload 3.1 leaves the reloaded groups out of its "Groups" answer (and later Reloads lost groups): w16 lists them (ARCHITECTURE.md 9.6). | b9dcd9c |
| H14 | REAL | 2.x Browse: a subfolder of a root got "c:\\\\dos". | No backslash after one already there. | b9dcd9c |
| H15 | REAL | 2.x Browse: "c:\dir\*.exe" did not change drive and passed "c:\dir\" to chdir; "\*.exe" skipped the root. | Drive from "x:", the directory without its last backslash, a root with it. | b9dcd9c |
| H16 | HARDENING | 2.x private profiles: int offsets in files over 32K; writing a key into such a file could write before the buffer. | Files over 7FF0h not read, and not rewritten. | b9dcd9c |
| H17 | REAL | 2.x mem_realloc: GlobalReAlloc on an unlocked block in real mode can move it on failure; the caller's pointer went stale. | 2.x: a new block, copy, free. | b9dcd9c |
| H18 | HARDENING | profile_get passed 8000h as GetPrivateProfileString's int size. | At most 7FFFh. | b9dcd9c |

## Medium

| ID | Verdict | Evidence | Action | Commit |
|---|---|---|---|---|
| M1 | HARDENING | Out of memory in parse's item loop leaked the items made (they were not yet in the data list). | Each item goes into the data list at once (by record offset), so free_group frees it. | b9dcd9c |
| M2 | HARDENING | set_fields freed t->name before its replacement existed (NULL on out of memory). The "no rollback after a failed write" part: the change stays in memory and is written by the next save; kept. | New strings made first, then swapped in. | b9dcd9c |
| M3 | HARDENING | Unchecked s_append (out of memory). | Checked. | b9dcd9c |
| M4 | FALSE | icon_bits reads HICONs ExtractIcon made: icon-sized (32 x 32), a global block always over 12 bytes. | None. | - |
| M5 | REAL | group_arrange never marked the group changed, and callers wrote before arranging: with Auto Arrange on the file held unarranged places. | Arranged before writing; changed positions mark the group. Probe arrange.txt: same file as 3.1's. | b9dcd9c |
| M6 | HARDENING | Change Icon list: an icon not listed (out of memory) leaked; DestroyIcon(0) for an icon that could not be read. | Destroyed when not listed; 0 skipped. | b9dcd9c |
| M7 | FALSE | IconListProc (2.x): nothing destroys the list inside its own procedure (EndDialog destroys the dialog after its loop), so the final GlobalUnlock never follows a GlobalFree. | None. | - |
| M8 | HARDENING | iconx get(): an offset FFFFFFFFh equals _llseek's -1, a count FFFFh _lread's -1. | Both refused. | b9dcd9c |
| M9 | HARDENING | answer_request read an unset name for an atom that is not one. | name[0] = 0 first. | b9dcd9c |
| M10 | REAL | ExitProgman. 3.1: ExitProgman(1) saves groups and settings even with Save Settings on Exit off (exit1); ExitProgman(0) saves as an ordinary exit (exit0); the rest of the block still runs ([ExitProgman(0)][CreateGroup(After)] made AFTER.GRP), no ACK is sent, and it ends even when a later command fails (exit3); [ExitProgman()] fails (exit2). w16 destroyed the frame at once (0) or closed with the setting (1) and ACKed. | exit_req: carried out after the block, no ACK; pm_exit saves (forced for 1) and destroys the frame. | b9dcd9c |
| M11 | HARDENING | The execute state (ends) was static: a command letting messages in (a message box) and another EXECUTE would mix them; it was never freed. | Saved and restored around each EXECUTE, freed after it. | b9dcd9c |
| M12 | REAL | 3.1 keeps blanks inside quotes: CreateGroup("  Spaced  ") makes "  Spaced  ", found only so (spaced.txt); w16 dropped the trailing ones. | Trailing blanks dropped only outside quotes. | b9dcd9c |
| M13 | FALSE (thunk); HARDENING (hook) | help_init's MakeProcInstance: one thunk for the task's life, freed with it. The WH_MSGFILTER hook stayed if the frame could not be made. | help_done on that path. | b9dcd9c |
| M14 | REAL | 3.1 loads every GroupN=: those in Order= first, then the rest in [Groups]' order; without Order= all (order.txt; it then saved "Order= 1 2 3 4"). w16 loaded only Order='s, none without it. | load_groups: Order=, then [Groups]' keys (profile_keys; the 2.x profile code lists keys too). | b9dcd9c |
| M15 | REAL | Group Properties renamed (and wrote) before the new file was written. | The file first. | b9dcd9c |
| M16 | 3.1-BEHAVIOUR | 3.1 names Ctrl+Alt+Home "Ctrl + Alt + Num 7" and Left "Num 4" too (HOTKEYS.GRP, Item Properties screens). | Kept. | - |
| M17 | HARDENING | 2.x Browse: CB_ADDSTRING's CB_ERRSPACE as an index; a NULL thunk as the list's procedure; s_dup NULL kept as an entry. | Checked. | b9dcd9c |
| M18 | REAL (newline); FALSE (MF_BYPOSITION); HARDENING (s_append) | 2.x WritePrivateProfileString: a key added to the last section of a file without a final line break joined its last line. pm_ModifyMenu is only called with MF_BYCOMMAND. | A line break first; s_append checked. | b9dcd9c |
| M19 | REAL (2.x MDI) / FALSE | A group destroyed with DestroyWindow left CW_ACTIVE dangling; cascade and tile moved maximised windows without restoring them; cascade and icon arranging never wrapped. destroy_child's missing WM_MDIACTIVATE(FALSE): the group only repaints on it, and it is going away: FALSE. | WM_DESTROY clears CW_ACTIVE; restore first; wrapping. | b9dcd9c |
| M20 | HARDENING | s_append with int lengths could shrink a block over 32K and write past it. | WORD lengths, at most FFF0h. | b9dcd9c |

## Low

| ID | Verdict | Evidence | Action | Commit |
|---|---|---|---|---|
| L1 | REAL | title_font (and 2.x's def_icon, its own bitmaps) never freed: GDI objects outlive a Windows 3.x program. | Freed at the end. | b9dcd9c |
| L2 | REAL | group_create destroyed a new group's window with DestroyWindow, behind MDI's back. With DDE CreateGroup of a file that cannot be written (badpath) 3.1 answers success, makes no group and the next AddItem goes to the active group; w16 failed both. | WM_MDIDESTROY; CreateGroup succeeds without a group then. | b9dcd9c |
| L3 | FALSE | GetObject of a bitmap just made cannot fail. | None. | - |
| L4 | HARDENING | group_file_name: unchecked s_append, path[-1] for an empty directory. | join_path. | b9dcd9c |
| L5 | HARDENING | Atom leaked when the negative ACK could not be posted (out of memory). | Deleted then. | b9dcd9c |
| L6 | HARDENING | answer_request copied with an int length: texts over 32K came out empty. | WORD copy; the group text grows by doubling (300 items took seconds before: grpread). | b9dcd9c |
| L7 | HARDENING | INITIATE took an invalid nonzero atom as a wildcard. | No answer. | b9dcd9c |
| L8 | REAL | 3.1 (audchgicon): Change Icon for a command typed after blanks shows C:\WINDOWS\NOTEPAD.EXE; w16 an empty name. | Blanks skipped. | b9dcd9c |
| L9 | REAL | 2.x folder list: the scroll range set before the new item was counted. | Counted first. | b9dcd9c |
| L10 | FALSE | drop_thunk: one thunk, kept and reused. | None. | - |
| L11 | HARDENING | CB_SETCURSEL below -1 (no caller passes one). | Refused. | b9dcd9c |
| L12 | FALSE | GlobalLock of a block just allocated, not discardable. | None. | - |
| L13 | REAL | 2.x: a maximised group did not follow the frame's size. | WM_SIZE moves it by the change. | b9dcd9c |
| L14 | FALSE | `mc->style & 0xFFFF`: Program Manager passes only low-word bits. | None. | - |
| L15 | HARDENING | s_cpy with max 0 wrote one byte. | Nothing written. | b9dcd9c |
| L16 | HARDENING | ask(): buffers unset if LoadString failed. | Emptied first. | b9dcd9c |
| L17 | FALSE | s_itoa(-32768): (unsigned)-32768 is 32768: "-32768". | None. | - |
| L18 | FALSE | s_atoi overflow wraps; the callers read numbers from PROGMAN.INI and DDE, where 3.1 wraps too (HotKey 99999 is -31073). | None. | - |
| L19 | FALSE | s_icmp's signed chars change only the order of names above 7Fh (2.x folder list), never equality. | None. | - |

## Beyond the audit (found by the probes)

Each observed on 3.1 and done so by w16 now (details: ARCHITECTURE.md
9.6, "The Program Manager audit"):
- Group files 3.1 calls damaged (BADSIG, BADSUM, CBBIG, CBSMALL, EMPTY,
  TRUNC, NAMEEND, ITEMOFF, ITEMSTR, ITEMWRAP, ICONHDR, TAGWRAP, TAGNOEND)
  get its system-modal "Group File Error" box (0 pixels against 3.1's,
  the groups not yet shown: every file is read before any window is
  made); its GroupN= stays. A missing file gets USER's SysErrorBox with
  3.1's question, Yes/No (0 pixels); No drops GroupN=. w16 used to load
  most damaged files and pass over missing ones silently. NAMEOFF (the
  name past cbGroup) is read as 3.1 does.
- DDE argument parsing and counts, AddItem positions and icon fallbacks
  (index 6), signed hotkeys, CreateGroup of an unwritable file, Reload,
  ExitProgman (M10, H13), quoted blanks (M12); [w16] ProgmanUnlimited
  (new, §6.8): 3.1's 30/40-character and 50-item DDE limits only when 0
  (the tests run so).
- Slot order against z-order: 3.1 writes a changed group in slot order
  and saves on exit in z-order, new items on top (many, arrange); w16
  kept one order.
- The 51st refused AddItem still makes a slot.
- The DDE listing of 300 items was too slow for a client's 5-second wait
  (grpread).

Seen and kept (ARCHITECTURE.md 9.6): after Reload 3.1 leaves reloaded
groups out of "Groups" (and further Reloads lost groups), w16 lists
them; 3.1 hangs on TAGSMALL and ended on a 250-character AddItem; 3.1's
handling of commands past DOS's path length; 3.1 listing AddItems with
icon index -1 or 70000 last; Program Item Properties' icon (the file's
on 3.1, the cached one on w16).

## Left open

Nothing.
