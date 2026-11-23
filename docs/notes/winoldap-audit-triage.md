# WINOLDAP audit triage

An outside, read-only audit of `winoldap/` (Standard mode's WINOLDAP.MOD;
written by another AI model from an older winoldap.c, not in the
repository) listed 2 Critical, 4 High, 4 Medium and 2 Low findings (C1-C2,
H1-H4, M1-M4, L1-L2 here, in its order). Each is checked against the code
as it was on master 9a43842 (line numbers below are that version's), with
the PIF reading since moved to `winoldap/pifread.c`, and against Windows
3.1 where a fix would change what is observable: new `tests/winoldap.sh`
scenarios (`tests/scripts/winoldap/`) run Microsoft's WINOLDAP.MOD and
w16's on the reference install's KERNEL, WSWAP, DSWAP and DOSX:
`longargs`, `longbat`, `longbatargs`, `lb127`, `lb128` (long tails;
LONGBAT.PIF names a batch file 61 characters long), `bigclust` (C: of
64 KB clusters, `WORUN_MOUNT=-size ...`), `nomem` (WO2.EXE's new
`eat=A,B` takes the global heap). Where 3.1 corrupts its own data or fails
and w16 deliberately does not, a scenario carries `# expect:` lines and
runs w16 alone (tests/winoldap.sh's new mode).

Verdicts: **REAL** (confirmed; fixed), **FALSE** (the claim is wrong or
cannot happen), **3.1-BEHAVIOUR** (3.1 does the same: kept),
**HARDENING** (only with input DOS or Windows cannot produce, or a
failing system call; fixed when cheap and invisible), **UNSURE**. Where
3.1 itself fails, w16 may survive (the user's ruling).

Status: every finding has a verdict and is closed (2026-11-22).

## Critical

| ID | Verdict | Evidence | Action | Commit |
|---|---|---|---|---|
| C1 | REAL (record, count); FALSE (B_PROGRAM) | winoldap.c:378-433: the first switch wrote the tail, grabber and 189h, made the Windows application's record (windows_record, 17Ch+1) and the program's (record linked after Windows', 17Ch+1), then GlobalAlloc'd the buffer; on failure it said IDS_NOMEM and ended, leaving both records in WSWAP's list with a destroyed window. Reached with a fragmented or used-up global heap (`nomem`: 256 KB left in 64 KB pieces, the buffer is block 183h x 16 + 1000 = 9DB08h, fixed). 3.1's WINOLDAP on the same heap does not stop: it switches, the program runs ("one" in WC.LOG) and Windows never comes back (3.1 fails; w16 may survive). The 189h sub-claim is FALSE: every switch writes 189h before WSWAP can use it (do_switch's first lines), and a new instance that finds a program's path there rebuilds DOSX's (setup:598-606). | The buffer is allocated before the block is touched; failure leaves the block as it was (also the IDS_TOOMANY path frees it). Test: `nomem` (expect: WO2's log at heap release, 17Ch 00, only record 15; the old module gave 17Ch 02 with the program's and Program Manager's records). | acbe96b, 9559beb |
| C2 | HARDENING | put_string(B_GRABBER, grabber, 128) (:376 then :386) could write 128 bytes into the 80-byte field 87h-D6h, over D7h (the swap directory). The grabber is OpenFile's full name of SYSTEM.INI's 286grabber=, read into 80 bytes: a DOS path is at most 79 characters with its drive and name (DOS's 64-byte directory limit; WIN.COM's own fields are 80 bytes), so 3.1's 80-byte field fits every real path, and no probe can make a longer one. | put_string bounded by the field (B_SWAPDIR - B_GRABBER), 189h's by B_RECORDS - B_PROGRAM. No behaviour change for any real path. | acbe96b, 9559beb |

## High

| ID | Verdict | Evidence | Action | Commit |
|---|---|---|---|---|
| H1 | REAL (in part 3.1-BEHAVIOUR) | write_tail (:294-301) wrote any length up to 129 (tail[130]) and the CR after it; at 129 the CR fell on 87h (put_string rewrote it, leaving the tail without its CR and a length byte DOS cannot hold). Reached with legitimate input: a PIF naming a batch file runs " /c FILE PARAMS" (:677-681): 61 + 64 characters give 131 (`longbat`), with WinExec arguments 166 (`longbatargs`). 3.1 (probes): tails of 117, 127 and 128 characters (`longargs`, `lb127`, `lb128`) get their length (75h, 7Fh, 80h) and CR in the block, but only their first 78 characters are right, the rest 3.1's own data (its title "WC1" / "Long Batch" among it: WC1 got "B2345...\00\00..." and, through the batch, " a123456789b"); 131 and 166 it writes whole (82h, A6h) over the grabber's path, and its switch fails with "Cannot find the file WINOLDAP.GRB" (the program never runs). w16 keeps the length for 127/128 as 3.1 does, with the right text; longer it cuts to 128, which fits the field, and runs the program (COMMAND.COM reads 126). The DOS 127-byte limit is DSWAP's and DOS's to apply (3.1's WINOLDAP writes 128 too). | write_tail cuts at B_GRABBER - B_TAIL - 2 = 128. Tests: `longbat`, `longbatargs`, `lb127`, `lb128`, `longargs` (expect: the block's length 80h/80h/7Fh/80h, WC1's tail). | acbe96b, 9559beb |
| H2 | HARDENING | setup (:577-583) copied args up to 129 bytes and stepped past the NUL without checking it was there. With 3.1's KERNEL the tail "args NUL path LF" is always whole (longer than 127 bytes too: `longargs`, 117 + NUL + 10). w16's KERNEL cuts the PDB's tail at 7Eh bytes (kernel/task.c:637, loader.c start_oldapp), so with 126 characters of arguments the NUL is lost and the path was read from PDB:100h, past the PDB selector's limit FFh (task.c:611). Run (SNAP as the shell, WinExec of WC1.COM with 126 characters, `-k build/dos/KRNL286.EXE`): no fault seen, "Cannot find the specified file." either way. | Reads bounded by GetSelectorLimit of the command line's selector; a missing NUL gives an empty path (the same box). Not kept as a scenario (3.1's WINOLDAP does not switch without a shell window, ARCHITECTURE.md 9.7, so it cannot be compared). KERNEL's cut tail itself is outside this audit (below). | acbe96b, 9559beb |
| H3 | HARDENING (pifpath, name); REAL (beyond it: the batch tail's buffer) | pifpath[128] = path (up to 127) + ".PIF" (:611-619) grows only for a path with no extension in its file name: WinExec adds ".EXE" to such names, so only a dotted directory and an extensionless 124+ character name reach it. name[80] holds stem(path) (an 8.3 base name, at most 8 characters) + ".PIF". Next to it, though: the batch case's `char t2[140]` took wsprintf(" /c %s %s", program, tail), up to 4 + 127 + 1 + 129 characters: `longbatargs` (166) overran it by 27 bytes on setup's stack (the old module ran on: no visible damage, but the frame was overwritten). 3.1 with the same input: see H1. | pifpath and name 4 bytes larger; t2 sized for program + tail. Test: `longbatargs`. | acbe96b, 9559beb |
| H4 | HARDENING (segsel); FALSE (GlobalLock, selbase) | woa_segsel returns 0 if DPMI 0002h fails (woasm.asm), which happens only with no descriptor left (0002h gives the same selector for the same segment every time, so starting programs does not use them up); blk would then be 0:offset. GlobalLock of a just-allocated fixed block cannot fail, nor 0006h of its selector. | A 0 selector gives the no-switcher box (IDS_NOSWITCHER), as no block. | acbe96b, 9559beb |

## Medium

| ID | Verdict | Evidence | Action | Commit |
|---|---|---|---|---|
| M1 | REAL | woa_diskfree (woasm.asm:104-105) multiplied sectors x bytes in 16 bits: 128 x 512 = 10000h gave 0 free bytes and a spurious "Insufficient disk space on drive C, Desired disk space: 603 K" box before the switch. `bigclust` (DOSBox `-size 512,128,4000,1000`): 3.1 switches with no box; w16 showed it. | 32-bit product (saturating at FFFFFFFFh). Test: `bigclust` (compared with 3.1: PASS). | acbe96b, 9559beb |
| M2 | FALSE | (a) MakeProcInstance once per instance (:664): its thunk lives in the task's TDB (kernel/procinst.c: thunks in TDB B2h-F1h, as 3.1's per Pietrek) and goes with the task, which ends after the dialog. (b) pif.directory[0] not a letter: INT 21h 0Eh with an invalid drive changes nothing, dos_chdir then fails and IDS_NODIR follows: the same as a missing directory. (c) The drive left changed after a failed chdir: the task ends right away, and KERNEL keeps a current drive and directory per task (task.c, "A task's directory ... switched with it"). | None. | - |
| M3 | REAL (narrow) | The DOSX path fallback (:598-606), used whenever 189h holds a program's path (every program started after an earlier one ended), built GetWindowsDirectory + "\system\dosx.exe": with Windows in a drive's root "C:\\system\dosx.exe"; with a Windows directory of 62-63 characters (legal: WIN.COM's own 189h, 80 bytes, holds it) GetWindowsDirectory(buf, 62) copies nothing (kernel/loader.c copy_dir), so lstrcat appended 16 bytes to the old 79-character contents, past dosx_path[80]. WIN.COM's own rule (win/win.asm try_dir): "C:\" keeps its one backslash. | The Windows directory read into its own buffer, a trailing backslash dropped, the path made only when it fits. Not observable on the reference install (C:\WINDOWS); no test. | acbe96b, 9559beb |
| M4 | FALSE | (a) `_lread` errors: the PIF is read by pifread.c now, whose readat (pifread.c:58-65) returns 0 for a failed seek or read, so the field keeps its default (the documented 3.1 behaviour for short reads). (b) WM_TIMER without a wParam check (:534): the only timer on the window is SetTimer(hwnd, 1, ...) (:767), killed at its first tick. | None. | - |

## Low

| ID | Verdict | Evidence | Action | Commit |
|---|---|---|---|---|
| L1 | FALSE | RegisterClass's failure makes CreateWindow("WOA") fail, and setup returns FALSE then (:699-700): the task ends without a switch. | None. | - |
| L2 | 3.1-BEHAVIOUR | WinOldApCall(1) before the grabber and record checks is 3.1's order (DOSBox-X file log: KERNEL's module files close after the program is checked, before the grabber is looked for; ARCHITECTURE.md 9.8 step 4). Closing KERNEL's cached files is harmless: KERNEL reopens them on demand. | Kept. | - |

## Beyond the audit

- `winoldap/pifread.c` and `pifread.h` still said "MIT licence": w16 is
  CC0 (commit 4720f50); their headers now say so (f95b856).
- w16's KERNEL cuts the tail it gives WINOLDAP ("args NUL path LF") at 7Eh
  bytes (kernel/task.c), where 3.1's keeps it whole (`longargs`: 117
  characters of arguments, NUL and the 10-character path reached 3.1's
  WINOLDAP). On w16's KERNEL a DOS program whose arguments, NUL and name
  pass 126 bytes (C:\WC1.COM: 116 characters of arguments) therefore
  gives "Cannot find the specified file." A KERNEL
  matter, outside this audit's scope; not changed here.
- Typed through xdotool, an upper-case "A" never reached Program
  Manager's Run box in DOSBox-X (both modules); the scenarios avoid it.
