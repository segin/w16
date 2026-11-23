# SHELL.DLL audit triage

An outside, read-only audit of `shell/shelldll/` (written by another AI
model; not in the repository) listed 9 High, 7 Medium and 5 Low findings
(H1-H9, M1-M7, L1-L5 here, in its order). Each is checked against the
code as it stood at 0b3c22b (line numbers below are that version's), and
against Windows 3.1 where a fix would change what a program sees: the
new probe SH15 (`tests/apps/sh15.c`, `tests/shellprobe.sh [-w] SH15`; its
damaged files from `tools/mkshaudit.py`, made from scratch and from w16's
own PROGMAN.EXE). SH15 loads SHELL with LoadLibrary and frees it after
each section, so every section starts a SHELL that reads REG.DAT afresh;
it ran as the shell of the reference 3.1 in Standard and 386 Enhanced
mode with Microsoft's SHELL.DLL and with w16's. Microsoft's SHELL lived
through every case, so every case is compared with it.

Verdicts: **REAL** (confirmed; fixed), **FALSE** (the claim is wrong),
**3.1-BEHAVIOUR** (3.1 does the same: kept), **HARDENING** (only with
malformed files, absurd arguments or no memory; fixed where cheap, with
3.1's answer where the probe gave one), **UNSURE** (none left).

Status: every finding has a verdict and is closed (2026-11-22).

## High

| ID | Verdict | Evidence | Action | Commit |
|---|---|---|---|---|
| H1 | REAL (bitmap); HARDENING (no memory) | `mark_dead` sized the dead-string bitmap from `text_cap` at the time (reg.c:268), and `compact` read `dead[r >> 3]` for every byte up to `text_size` (reg.c:295). Strings added after a string died, while a key is open (no write in between), grow the text past the bitmap: a working program does this (open a key, replace a value, add a few KB). SH15 h (RegCreateKey, a value replaced, 60 values of 100 characters, RegCloseKey): 3.1 kept all of them; w16 then lost `w16h\k59` (ERROR_BADKEY: bits read past the bitmap counted live strings as dead) and the session died in the next section. The second claim (bitmap allocation fails after `unref` freed the entry, reg.c:276/367, so `compact` would read a freed or reused entry) needs a failed allocation of at most 8 KB. | `compact` reads no bit past the bitmap (nothing there died); when the bitmap cannot grow, the string and its entry stay as if used until the file is next read. Probe: SH15 h (R10.DAT byte-identical to 3.1's). | 54888dd, dbbccad |
| H2 | HARDENING | Every sub-claim needs a damaged REG.DAT: `r + 2 <= text_size` and `r += v` (reg.c:648, 653) wrap only with an even WORD near FFFFh in the text area; `n + 2` (reg.c:658) only with a string length past the text area; out-of-range links kept in the tree (reg.c:613-623) only from a damaged file. A file 3.1 or w16 writes has none of these. 3.1 answers ERROR_BADDB to each and leaves the file alone (SH15 r: R5 a child past the table, R7 a string FFF0h long, RC a key its own sibling, RT a free block of FFFEh at the start: 1 from every call, the file unchanged); w16 would read past its blocks or loop. | `collect` checks the whole file before using it: tree links inside the table, no key reached twice, the text area tiled exactly by strings and free blocks, every name and value a string; otherwise ERROR_BADDB, the file untouched. Reference counts are recounted; chains keep only strings of their own bucket. Probe: SH15 r, c, t (every snapshot byte-identical). | 54888dd, dbbccad |
| H3 | HARDENING; FALSE (the discarded pointer) | `collect` growing the table (reg.c:672) and `empty_db` (reg.c:586) used the table after `set_entries` failed: only when 300 bytes to 512 KB cannot be had at load. `set_entries` dropping `GlobalLock`'s result after a failed GlobalReAlloc (reg.c:169) is harmless: a moveable block keeps its selector in protected mode, so `ent_sel` stays right. | `empty_db` and `collect` check `set_entries`; no memory to grow leaves the table as it is; no memory at an empty start leaves the database unread. | 54888dd, dbbccad |
| H4 | REAL | `flush` rewrote REG.DAT in place with OF_CREATE (reg.c:127), ignored every `write_all` (reg.c:136-146), cleared `dirty` (reg.c:148) and returned nothing. 3.1 (SH15 w): REG.DAT is replaced by a new file (a handle opened before a write still sees the old file afterwards, where a file rewritten with OF_CREATE is the same file: control case `w.ctl.sameinode`); with no file handle left the write fails, the old REG.DAT stays whole, nothing is left in the Windows directory, and the call returns ERROR_CANTWRITE (5): RegSetValue, RegCloseKey, RegDeleteKey, also RegQueryValue and RegEnumKey (which still give their answer); RegOpenKey/RegCreateKey leave a key open and give 0; a call's own error wins (2, 7); every later call tries again, and the first good write carries every change. A full disk cannot be made in DOSBox 0.74 (it does not enforce free space); the no-handle case is the write failure it can show. | `flush` writes REG.TMP in the Windows directory, then deletes REG.DAT and renames (UNVERIFIED: 3.1's temporary name); any failure leaves REG.DAT and the pending changes; each call ends in `settle`, which writes when nothing is open and turns a failed write into ERROR_CANTWRITE. REG.DAT stays byte-identical in normal use (SH1, SH2, SH15). Probe: SH15 w (R11, R12 byte-identical). | 54888dd, dbbccad |
| H5 | REAL | RegEnumKey called `key_node` before checking `db_bad` (reg.c:941-942): with a REG.DAT whose header could not be read the entry table was never allocated, and `E(0)` read selector 0. A truncated or empty REG.DAT is what a crash or a full disk during a write leaves (H4), and File Manager, REGEDIT and OLE programs enumerate keys. SH15 r1 (an empty REG.DAT): 3.1 answers ERROR_CANTREAD (4) to every call; w16 ended the session at the first RegEnumKey. The second claim also holds: RegEnumKey did not run the [embedding] import, which 3.1 does (SH15 i: a database made from nothing, RegEnumKey the first call 2.5 s later: 3.1 0 and "PBrush"; w16 ERROR_BADKEY). | Every call starts with `begin`: the deferred read, then the import, for RegEnumKey too. Probe: SH15 r, i. | 54888dd, dbbccad |
| H6 | HARDENING | `n + 2` wraps for a value of 65534 or more characters (reg.c:332), and `t * 2 + 1` (reg.c:340) for a text entry at 8000h or above: a 64K string or 32768 entries (with 64K of text, only keys sharing one name nested that deep). No working program. | `intern` refuses both (ERROR_OUTOFMEMORY; the entry goes back to the free list's head). | 54888dd, dbbccad |
| H7 | HARDENING | `ne_find` returned the type's count without setting `off`/`len` when the n-th entry could not be read (icon.c:49-54), and `icon_at` used them (icon.c:187-189). The count is read from the same table, so only a file cut inside its resource table gets there. 3.1 reads the whole resource table first: a file ending one byte before the resident-name table gives the count 0 and NULL icons, one ending there 46 and NULL icons (SH15 n: NECUT, NECUT3, NECUT2); w16 gave 46 for the first. | The table must be in the file whole, else no icons (count 0); every read stays inside the table; `ne_find` returns 0 for an entry that is not there. Probe: SH15 n. | 54888dd, dbbccad |
| H8 | HARDENING (`pick`); FALSE (.ICO path) | `pick` walks the group without `GlobalSize` (icon.c:158-160), but it runs only when USER's GetIconID returns 0; Microsoft's and w16's USER both have GetIconID (the comment saying w16's did not was stale). The .ICO claims: offsets past 64K wrap inside the block's first segment (icon.c:206-216), so nothing is written outside it; entries left unset exist only when the file ends inside its directory, and then every image lies past the end too, so the icon is NULL whichever entry USER picks. SH15 n: ICOFF (FFFFh images counted, one there) gives 3.1's icon, ICOCUT (3 counted, the file ends in the directory) NULL, as 3.1. | `pick` reads no entry past its block; the comment corrected. | 54888dd, dbbccad |
| H9 | HARDENING | `key[160]` (exec.c:124) overflows only when the class name and the verb together pass 144 characters (SH15 v: verbs of 70 and 100 characters with a short class give 27 on both); `j.cmd[512]` (exec.c:370-373, 350) only with parameters of about 500 characters, which no program can start anyway: WinExec takes no more than 255 (SH15 p: "C:\SH5.EXE " and 244 characters start it, 245 give 0, from ShellExecute and from WinExec, both modes); `f.name[80]` (exec.c:384) and `filter[100]` (exec.c:268) only for a registered program whose file name passes 75 or 48 characters; `fe->found[160]` (exec.c:319) only if OpenFile accepted a path of 160 characters or more. | A verb that does not fit the key is a verb without a command (27); commands are built with `append` (cut at 511); the names bounded; `filter` and `found` sized for the longest name. | 54888dd, dbbccad |

## Medium

| ID | Verdict | Evidence | Action | Commit |
|---|---|---|---|---|
| M1 | HARDENING | `lstrcpy(sub, k)` (reg.c:489) overflows only for a WIN.INI [embedding] key name of 128 characters or more; RegSetValue refuses names over 63 anyway. | Such a key is skipped after the class's own RegSetValue (which fails as before). | 54888dd, dbbccad |
| M2 | HARDENING | `wsprintf(t, fmt, a->app)` (about.c:68, 73) into `t[400]`: an szApp of about 390 characters. | The caption and first line are formatted in a block as long as they need: nothing cut, so nothing changes for any length. | 54888dd, dbbccad |
| M3 | 3.1-BEHAVIOUR | Both claims are what 3.1 does (SH15 x): with lpDirectory C:\NODIR, ShellExecute and FindExecutable find SHAX.EXE in the current directory (C:\WINDOWS) and start it there (an instance, CWD C:\WINDOWS; FindExecutable succeeds with "C:\WINDOWS\SHAX.EXE"); with D:\SUB the program starts in D:\SUB and D:'s directory stays \SUB afterwards. w16 already gave the same. | Kept; the empty `if` replaced by a comment saying so (exec.c:69). | 54888dd, dbbccad |
| M4 | REAL | `static FINDEXE __far *fe` (exec.c:237, set at 292): SHELL's data is shared by every task, so two programs each with a Program Not Found dialog up at once (both modal to their own windows) make the first one's OK write into the second's FINDEXE on another stack. Rare, but two working programs can do it. Not observable otherwise. | Each dialog keeps its FINDEXE in its DWL_USER. | 54888dd, dbbccad |
| M5 | FALSE | `rd` treating a seek result of FFFFFFFFh as success (icon.c:33) needs an offset of FFFFFFFFh from the file, and then `_lread` reads at the old position into a fixed buffer of the right size: nothing outside it. A shift of 32 or more (icon.c:50-51) gives a meaningless offset and length; `read_block` refuses lengths over FFF0h and reads the rest into a DirectResAlloc block of exactly that size. No memory hazard, malformed files only. | None. | - |
| M6 | HARDENING | `lstrcpy(path, exe)` (icon.c:296) passes 128 characters only for a registered command whose first word does (a document's own path comes from OpenFile and fits). | Cut at 128, as the GetModuleFileName after it. | 54888dd, dbbccad |
| M7 | HARDENING | DoEnvironmentSubst(NULL, cb) wrote through NULL (env.c:95). 3.1 does not fault: it answers as for a string of two characters, (2, 1) for cb 3 or more and (2, 0) for 0-2 (SH15 e: cb 0-10 and 100, both modes); w16 gave (0, 1). | NULL gets 3.1's answer (UNVERIFIED: which two characters). Probe: SH15 e. | 54888dd, dbbccad |

## Low

| ID | Verdict | Evidence | Action | Commit |
|---|---|---|---|---|
| L1 | HARDENING (growth); FALSE (lock, WEP) | The list's doubling wraps only after 32768 registrations, and GlobalReAlloc was called before the check (hook.c:53-57). `GlobalLock(h_list)` (hook.c:61, 72, 87) cannot fail on a moveable block that is not discardable. WEP not unhooking: SH15 k (SHELL freed with its hook set, then a top-level window made and destroyed) lives on 3.1, and with w16's SHELL; USER drops a freed module's hooks (w16's USER too: `hooks_module_gone`, user/hook.c). SHELL's blocks belong to SHELL (GMEM_SHARE/DDESHARE) and go with it. | The capacity is checked first. | 54888dd, dbbccad |
| L2 | FALSE | DragQueryFile/DragQueryPoint read the drop block the drag source made (USER and File Manager, in the layout of ARCHITECTURE.md 13.3); an invalid handle fails GlobalLock and gives 0. Only a program forging WM_DROPFILES with a short block reaches it, and 3.1 does not check either. | None. | - |
| L3 | FALSE | The unchecked `_llseek` (reg.c:713) can only leave the read that follows short, which `load` already refused. `h_ent`/`h_text` after a failed load are not leaked: they are SHELL's and are reused by the next attempt (reads are now retried at each call, as 3.1 does: SH15 r, g). | None (the seeks are checked now anyway, with the error codes below). | 54888dd, dbbccad |
| L4 | FALSE | GlobalLock of a block just allocated moveable and not discardable does not fail, so none of the four leaks happens (icon.c:206-208, env.c:88-91, reg.c:465-468, util.c:85-87). | None. | - |
| L5 | HARDENING | `libentry` ignored LocalInit's result (entry.asm:36); SHELL's local heap holds the Program Not Found dialog's edit text. LocalInit fails only when the data segment cannot be grown at load. | No local heap: SHELL does not load (AX 0), as the SDK's LibEntry. | 54888dd, dbbccad |

## Beyond the audit

Found by SH15 and fixed with the findings (dbbccad; probe 54888dd):

- Damaged REG.DATs: 3.1 answers ERROR_CANTREAD (4) when the file ends in
  the header or the entry table (R1 empty, R3 48 bytes) and ERROR_BADDB
  (1) for a wrong signature (R2), a short text area (R4), no buckets (R9)
  and the damage of H2; w16 gave 1 for all. A first free entry past the
  table (R8) is taken (the free list is rebuilt). A key name past the
  table (R6) is memory-dependent on 3.1: taken in Standard mode (the key
  unnamed: lookups miss it, RegEnumKey gives "" for it), ERROR_BADDB in
  386 Enhanced mode; w16 refuses it as 386 Enhanced mode does, and
  obsref leaves r6 out of the table.
- A failed read is retried at every call: with a good REG.DAT copied over
  a damaged one, the next call reads it (SH15 r: every `.retry`).
- REG.DAT gone between SHELL's loading and its first call: 3.1 answers
  ERROR_CANTOPEN (3) and makes no file; w16 made an empty database
  (UNVERIFIED code until now). Fixed; the file is read once it is back
  (SH15 g).

Kept on purpose: the temporary file's name (REG.TMP) is w16's; 3.1's
could not be seen (no failure left one behind).
