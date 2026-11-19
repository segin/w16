# KERNEL audit triage

An outside, read-only audit of `kernel/` (written by another AI model;
not in the repository) listed 5 Critical, 29 High, 43 Medium and 36 Low
findings. Each is checked here against the code, and against Windows 3.1
where the fix would change what a program sees (the probe TK62,
`tests/apps/tk62.c`, `tests/tk62ref.sh`). The audit's line numbers were
close but not exact.

Verdicts: **REAL** (confirmed; fixed unless noted), **FALSE** (the claim
is wrong), **3.1-BEHAVIOUR** (3.1 does the same: kept), **HARDENING**
(only with malformed input or absurd arguments 3.1 does not defend
against either; fixed only when cheap and invisible to working
programs), **UNSURE** (needs a probe not run yet).

Status: every finding has a verdict. "open" in the Action column marks
what is left (listed again at the end).

## Critical

| ID | Verdict | Evidence | Action |
|---|---|---|---|
| C1 | REAL | `gfree_owner` (both heaps) restarted from the arena before the freed block; when that was a free arena it merged with the block and `chunk_check_empty` could give the piece back, freeing that arena's selector (KRNL286) or slot (KRNL386) before `AR(n)->next` read it. | Restart from the first non-free arena before the block (a free one's predecessor; 0 if that is the piece's head or the first sentinel). Regression: TK62 "chunk" (helper TK62A ends with [free][block] alone in a piece). |
| C2 | REAL | KRNL286 `chunk_add` for the largest block (`paras_for` FFFEh paragraphs): the piece is 101000h bytes, and `(u16)((len - 40h) >> 4) - 1` wrapped to FBh. | The piece's free arena is cut at FFFEh paragraphs (the rest of the DPMI block unused). Regression: TK62 "big" (GlobalAlloc(FFFD0h), both ends written). |
| C3 | REAL | KRNL386 `chunk_add` grew `chunk_sel` with GlobalReAlloc, which can grow the heap and re-enter `chunk_add` with the same full table: the inner call moved the block the outer GlobalReAlloc held (fixed blocks are not held by the count), and both added 16 to `chunk_cap`. | No growing of the records from inside a growth (`chunk_growing`); that inner piece goes unrecorded (never given back, as before the records existed). Not reproducible in a probe without exhausting DOS memory in a particular way; by reading. |
| C4 | REAL | KRNL286 `carve` returned the whole free arena when `sel_new` failed (no selector for the rest); GlobalReAlloc in place, `grow_tiles`, `make_room_after`, `compact`, `GlobalWire` then had blocks larger than their selector runs and `set_tiles`/`free_block` touched other selectors. | `carve` returns 0 then (as KRNL386's already did), f untouched; every caller handles 0. Regression: TK62 "ldt" (LDT exhausted, GlobalReAlloc in place; the size must not become the free space). |
| C5 | REAL | `entry_size` summed in a word: a file with over ~13,100 fixed/constant entries (5 bytes in memory, 3 on file) wrapped it and `entry_convert` wrote past the module table. | 32-bit sum; a bundle running past the table's end fails; `mdb_build` fails past FFF0h (LoadLibrary returns 8). Regression: TK62 "entries" with TKENT.DLL (13300 entries, fails) and TKENT2.DLL (12000, loads), made by `tools/mkne.py --entries`. |

## High

| ID | Verdict | Evidence | Action |
|---|---|---|---|
| H1 | REAL | KRNL286 `paras_for`: `n + 15` wraps for n >= FFFFFFF1h and gave a 2-paragraph block. | Sizes past FFFF0h fail first (results below that unchanged). TK62 "big" observes FFFFFFFFh and FFFFFFF1h on 3.1. |
| H2 | REAL | `grow(dos=1)` (both): `r.bx = (u16)(size >> 4)` truncated, and `chunk_add` was given the untruncated size. Reachable with GlobalDOSAlloc(>= 1 MB). | DOS-memory growth past FFFFh paragraphs fails. |
| H3 | HARDENING | `discard` keeps a copy of the arena across the GlobalNotify callback; a callback that frees or moves memory breaks it. The SDK (NotifyProc): "should not call any routine that might move memory". | Not fixed. |
| H4 | REAL | KRNL386 `take_next` ignored `carve` failing (no arena slot); GlobalReAlloc returned success at the old size. | `take_next` returns 0 and GlobalReAlloc fails. |
| H5 | FALSE | The shrink path frees the upper tiles and then asks for one descriptor (`sel_new`): with at least one just freed it cannot fail; with none freed, `n` is unchanged and nothing is inconsistent. | None. |
| H6 | HARDENING | `new_handle`: a LocalHandleDelta of 3FFFh or more wrapped the table size and wrote the handles past a tiny arena. | Fixed (cheap): such a delta makes the handle allocation fail. |
| H7 | HARDENING | `f_read_seg` read `flen` bytes into a DGROUP clamped to FFF0h (a file whose DGROUP data is over FFF0h bytes). | Fixed: such a segment fails the load. |
| H8 | HARDENING | `patch`/`redo_self_fixups`: relocation chains not bounded by the segment, and a cyclic chain looped for ever. | Fixed: offsets checked against the segment's limit, chains cut at 8000h links; LOBYTE no longer reads a word. |
| H9 | REAL | `load_body`: `entry_off + entry_len` wrapped; table offsets not checked; `2 * modref_count` wrapped in `mdb_build`. | Offsets in order and below FFF0h, else LE_BADFORMAT (11); module reference size in 32 bits; alignment shift over 16 rejected. |
| H10 | HARDENING | `type_first` did not check the first type's count against the table's end; loops over its entries ran past it. | Fixed: `type_fits` for every type returned. |
| H11 | UNSURE | `sweep` frees DLLs at usage 0 in list order; a DLL loaded before its importer goes first, and the importer's WEP runs after it is gone. Pietrek does not say in which order 3.1's DecExeUsage/DelModule free an import closure. | open: probe WEP order (a WEP that calls into its import). |
| H12 | HARDENING | Circular imports; a module importing itself under another name recursed until the stack ran out. | Recursion capped at 32 nested loads (LE_NOMEM). Circular LibMain order not changed. |
| H13 | REAL | `defer_add` grew `defer_sel` (a fixed block) with GlobalReAlloc(.., 0): it could not move, so a program importing over 30 DLLs to initialise failed to load when the block could not grow in place. | GMEM_MOVEABLE (a fixed block keeps its selector when it moves). |
| H14 | HARDENING | DefResourceHandler: a failed re-read of a discarded resource left it allocated with garbage. Needs a read error. | Fixed: discarded again. |
| H15 | HARDENING | SwitchStackTo/Back keep their scratch in statics (they are not called at interrupt time: non-preemptive tasks) and ran `sti` unconditionally. | The `cli`/`sti` pairs removed (a move to SS already holds interrupts for the next instruction), so the caller's IF is kept. Statics kept. |
| H16 | REAL (KRNL286) / 3.1-BEHAVIOUR (KRNL386) | TK62 "rights": AllocSelector(0), base, limit, SelectorAccessRights(s, 1, 12h): 3.1's KRNL386 selector stays unusable (IsBadReadPtr 1), KRNL286's becomes readable. w16's never did. | KRNL286 build: a fresh selector (not present, AVL clear) given segment rights becomes present. KRNL386 kept. |
| H17 | HARDENING | KRNL386 `list_take`: `e + 8 * i` wrapped past FFF8h for runs at the LDT's top. | Fixed: a run may not wrap. |
| H18 | HARDENING | FreeSelector with an RPL other than KERNEL's missed the block; a 4 GB limit wrapped the run length to 0. | Fixed: the RPL is replaced by KERNEL's; runs over 2000h selectors are taken as 1. |
| H19 | 3.1-BEHAVIOUR | TK62 "sigproc": on 3.1 SetTaskSignalProc on a plain block returns its dword at 2Ah and writes the new one there; GetAppCompatFlags returns its dword at 4Eh. No check. | Kept (TK62 checks it). |
| H20 | 3.1-BEHAVIOUR | The task's blocks are freed and the task unlinked before its module and DLLs (WEPs) go: Pietrek's ExitCall does GlobalFreeAll, DeleteTask, TerminatePDB, then FreeModule. THHOOK's current TDB pointed at the freed TDB while KERNEL idled. | Order kept; `thhook_sync` after the task is gone. |
| H21 | 3.1-BEHAVIOUR | TK62 "events": 10001h PostEvents leave the count 1 on 3.1 (a word that wraps). OldYield's `--` could underflow only if another task called WaitEvent on it; a single `inc`/`dec` is not torn by interrupts. | Kept. |
| H22 | REAL | TK62 "badptr" on 3.1: IsBadReadPtr/IsBadWritePtr(NULL, 0) = 0 (w16: 1); an expand-down copy of DS with limit FFFh is good at 10h, 2000h and FFF0h (w16 bad at the last two). Also IsBadStringPtr: the NUL must be in the first max bytes ("abc" bad with 3, good with 4; max 0 always bad), max FFFFh sets no bound (w16 took any string running past max as good). `huge_bad`'s selector wrap needs absurd sizes (HARDENING, not fixed). | 0 bytes are good; expand-down segments good at every offset; IsBadStringPtr as observed. |
| H23 | REAL | TK62 "files": on 3.1 OpenFile("1:\\TK62.TXT", OF_PARSE) and "@:TK62.TXT" fail with 2; "z:FOO" works. w16 took them as drives. | Only a letter is a drive. |
| H24 | REAL | Profile `load`: every open error was "no file", and a seek error "empty file"; a write then replaced the whole file with the one key (data loss with no handle left or a sharing error). | Only DOS errors 2 and 3 mean no file; others and seek errors fail the call (the cache is left empty). |
| H25 | REAL | WritePrivateProfileString(NULL, NULL, NULL, file) dropped the private cache whatever the file; WIN.INI's copy stayed. | Drops the named file's copy (WIN.INI's when it names WIN.INI). |
| H26 | 3.1-BEHAVIOUR | `flush_out` rewrites the file in place; a full disk leaves it short. 3.1 writes in place too (the audit says so itself). | Kept. |
| H27 | 3.1-BEHAVIOUR | TK62 "atoms": on 3.1, 10001h AddAtom calls and one DeleteAtom remove the atom (a word count that wrapped); 101h adds survive a delete (not a byte). w16 the same. | Kept. |
| H28 | REAL | lstrlen/lstrcpy/lstrcat on a string with no NUL before its segment's end: IsBadStringPtr(s, FFFFh) passes it, then `kstrlen` counted round a 64K segment for ever or faulted in KERNEL. 3.1 faults and returns 0 through `__GP`. | A NUL must come before the segment's end (`str_len`); otherwise 0, as for a bad pointer. |
| H29 | REAL | lstrcpy/lstrcat with a source the copy overwrites (lstrcat(buf, buf)) ran past the checked length into KERNEL's fault. | The copy stops at the checked length and returns 0 when the source changed under it (3.1: fault, 0; UNVERIFIED what 3.1 leaves in the buffer). |

## Medium

| # | Verdict | Evidence | Action |
|---|---|---|---|
| 1 | HARDENING | KRNL386: `tiles()` wraps for sizes past FFFF0000h (GlobalAlloc then discarded every block before failing); GlobalReAlloc past FFh tiles truncated `selcount`. | Fixed: blocks over FF0000h bytes fail (MAX_BLOCK) in GlobalAlloc, GlobalReAlloc and a discarded block's reallocation. |
| 2 | REAL | KRNL386 (Enhanced mode, where `page_lock` calls DPMI 0600h): a page-locked block freed or moved left its old pages locked. GlobalDOSAlloc's `pglock = 1` without a lock is DOS memory, locked anyway. | `free_block` unlocks; `transplant` moves the lock. |
| 3 | REAL | `galias_code` (both): the block's arena was found before growing the alias records, which can compact and move it. | The block is found again afterwards. |
| 4 | REAL | KRNL386 `global_check`: the first free arena, right after the first sentinel (size 0), shares its address, and `addr <= prev addr` failed. This is the "bad free list" false alarm that made `[w16] heapcheck=1` unusable under KRNL386. | Equal addresses allowed after the first sentinel. Checked with heapcheck boots (Tests). |
| 5 | HARDENING | LocalHandle(p) read the word before p without checking p against DS's limit. | Fixed (cheap). |
| 6 | HARDENING | LocalInit with start 0-4, or (0, 0) on a 64K segment, overlaps the instance words; 3.1 is no safer. | Not fixed. |
| 7 | REAL | TK62 "lockseg": on 3.1 LockSegment(-1)/UnlockSegment(-1) return the caller's DS handle; w16's `__loadds` had lost DS, so -1 was looked up as a selector (0). | `kentry.asm` SEGLOCKAPI passes the caller's DS (ES still 0 on return). |
| 8 | REAL | KRNL286 GlobalWire moved a block that was already fixed (lock count > 0); KRNL386 checks `movable`. | A fixed block stays where it is. |
| 9 | HARDENING | Alignment shifts over 16 (loader, resources). | Fixed: the load fails / the shift is clamped. |
| 10 | REAL | A DLL whose defer_add or LibMain fails was destroyed without USER's 80h signal (it had 40h before its init). | 80h after WEP, as `sweep` and Pietrek's DelModule. |
| 11 | REAL | `find_handle` scanned modules still loading, whose handle words are the file's. | Skipped until `rsrc_ready`. |
| 12 | HARDENING | `task_thunk`: a failed `data_alias` (no selector) orphans the new thunk block. | Not fixed. |
| 13 | HARDENING | GetInstanceData copied without checking either segment's limit. | Fixed: 0 when outside. |
| 14 | HARDENING | `trace_note` reads 6 words above the return address ([w16] trace, a debugging aid only). | Not fixed. |
| 15 | REAL | Module paths with '/' ("C:/DIR/X.DLL") got the current directory put in front. | '/' taken as '\' and stored as '\'. |
| 16 | HARDENING | Name tables' ends not checked (malformed files). | Not fixed. |
| 17 | 3.1-BEHAVIOUR / HARDENING | TK62 "winexec": WinExec("") and WinExec("   ") return 2 on 3.1, as on w16 (".EXE" not found). WinExec(NULL) faults on both. | Kept. |
| 18 | REAL | A task ending (or faulting, the watchdog, a DOS exit) with DF set ran KERNEL's C code (forward copies) backwards. | `cld` at those entries. |
| 19 | HARDENING | `gp_find` read a "__GP" entry before checking it was inside the segment. | Fixed: bounds first. |
| 20 | HARDENING | VERR/LAR pass a not-present selector; reloading it into ES (task switch, INT call return) or DS/ES (fault recovery) faults again. | Fixed: the present bit is checked too. |
| 21 | FALSE | "Freeing a locked DS fails": GlobalFree frees locked blocks too (observed, returns 0); it fails only for the Burgermaster. | None. |
| 22 | HARDENING | dosx.c's emulation (Microsoft's DOSX only): 0-paragraph requests, unchecked `dpmi_set_desc`. KERNEL never asks for those. | Not fixed. |
| 23 | HARDENING | Selector 0 maps to LDT slot 0; AllocCStoDSAlias of a conforming code segment gives expand-down data; GDT selectors. None of these come from working programs. | Not fixed. |
| 24 | HARDENING | `_seg_move` ignores DPMI 0007h failing (it cannot for the block's own selector) and relies on INT 31h keeping IF (DPMI hosts do). | Not fixed. |
| 25 | REAL (part) | LockCurrentTask is not honoured by the scheduler (known: ARCHITECTURE.md 9.1, not yet). The locked task and FileCdr's task stayed set after that task ended. | The task's end clears them (`kmisc_task_end`). Honouring the lock: open (it changes scheduling). |
| 26 | HARDENING | `rmc` in task.c is shared by `idle` and `set_psp`; nothing calls `set_psp` at interrupt time. | Not fixed. |
| 27 | FALSE | `kernel_psp` is never nested: `kputs` and `kdbg_file` call it 1/0 around plain DOS calls. | None. |
| 28 | 3.1-BEHAVIOUR | The environment block copied for a task is KERNEL's and outlives it: TK16 (3.1) reads its GlobalSize after the task ended (10h/20h). Giving it to the task's PDB failed TK16. | Kept (the comment says why). |
| 29 | HARDENING | task_create/inittask with a malformed NE (SP < 26, SS not DGROUP); `libinit` kept on an error path. | Not fixed. |
| 30 | FALSE | "Reversed record layout": Microsoft's TOOLHELP on w16's KERNEL (toolhelp boot test, runs ms-tree286 and ms-tree386) reports `LOGPARAMERROR code=6001 addr=THP param=2222`, the same as on 3.1. So w16's {code, proc, param} is the order 3.1's TOOLHELP reads. The static record is fine: notifications are not reentrant. | None. |
| 31 | HARDENING | `notify`: a ToolHelpHook procedure that unhooks itself had the RegisterPtrace one called too. | Fixed: both hooks read before either is called. |
| 32 | HARDENING | GetCodeInfo matched an empty segment slot (selector 0) for selectors 0-7. | Fixed. |
| 33 | REAL (open) | TK62 "codehandle": 3.1's GetCodeHandle of a KERNEL procedure gives its segment's handle; w16's gives 0. But TK50 shows 3.1's AX equals GlobalHandle(KERNEL's CS), which is 0 in w16 because KERNEL's segments are not blocks of its heap. | open: needs KERNEL's segments in the selector table (GlobalHandle). TK62 records it without checking. |
| 34 | HARDENING | FatalExit: no guard against a FatalExitHook that calls FatalExit; DS = SS for the hook. | Not fixed. |
| 35 | FALSE | `hmemcpy`'s `sl + n` cannot wrap: linear addresses and sizes are far below 4 GB. | None. |
| 36 | REAL | TK62 "files": on 3.1 OpenFile("COM1:"), ("FOO:"), ("A:B:C") with OF_PARSE fail; w16 took ':' as a name character. A NULL name is HARDENING. | ':' is a bad name character. |
| 37 | UNSURE | OpenFile reports DOS error 2 for any open failure. TK62's read-only file opened for writing on 3.1 too: DOSBox does not enforce the attribute, so nothing was learned. | open: probe a real error (sharing, no handles). |
| 38 | HARDENING | `temp_dir` takes a 3-character TEMP without checking it (a bad environment). | Not fixed. |
| 39 | UNSURE | GetDriveType returns 0 where IOCTL 4408h fails (RAM drives, SUBST). | open: not probed. |
| 40 | 3.1-BEHAVIOUR | TK62 "profile": 3.1 strips the quotes of a default (`"dq"` gives dq) as of a file value. | Kept (TK62 checks it). |
| 41 | 3.1-BEHAVIOUR | TK62 "profile": on 3.1 a value written with leading blanks reads back with them from the cache ("  two") and without them after a flush ("two"); a file value loses them ("lead"). | Kept (TK62 checks it). |
| 42 | 3.1-BEHAVIOUR | TK62 "profile": a line "=v" gives an empty key in 3.1's key list (`\0k2\0\0`, 4). Negative sizes are HARDENING. | Kept (TK62 checks it). |
| 43 | HARDENING | Atoms: a table whose bucket count was overwritten with 0 divided by 0; KLstrcmp with NULL. | The division fixed (lookup and AddAtom fail); KLstrcmp not. |

## Low

The audit's Low list is numbered here in its order (36 items).

| # | Verdict | Evidence | Action |
|---|---|---|---|
| L1 | REAL | A tile limit past 1 MB must be page-granular; DPMI refused the byte limit and the tile was cut to 64K (both heaps on a 386 host). | Such limits rounded up to the page (heapcheck accepts them). |
| L2 | HARDENING | GlobalReAlloc of the Burgermaster. | Not fixed. |
| L3 | UNSURE | KRNL286 `realloc_discarded` returns the odd handle when named by its selector. | open: not probed. |
| L4 | REAL (minor) | LocalCompact(-1) discards at most 32 blocks a call (NDISC). | open. |
| L5 | UNSURE | GetFreeSpace counts fixed discardable blocks. | open. |
| L6 | UNSURE | LOCALNOTIFY's thunk does not keep ES. | open. |
| L7 | HARDENING | `module_of` matched handles 1-3 to empty segment slots. | Fixed. |
| L8 | HARDENING | LoadResource's usage count is a word. | Not fixed (atoms and events wrap the same way on 3.1). |
| L9 | HARDENING | `tb_new` ignores `gset_code` failing (DPMI failure). | Not fixed. |
| L10 | REAL (cosmetic) | Unimplemented-import thunks for names over 32 characters sharing a prefix share a thunk (only the reported name is wrong). | Not fixed. |
| L11 | FALSE | `module_load_program` is never called with a task running and `last_libinit` set. | None. |
| L12 | REAL (minor) | The bootstrap's relocation records (`rel_para`, a few KB of DOS memory) are never freed. | open. |
| L13 | HARDENING | Bootstrap: a missing environment, NE length wrap, LOBYTE chains, entry bounds (KERNEL's own file). | Not fixed. |
| L14 | HARDENING | Watchdog ([w16] debugging aid): `wd_limit` overflow, RTC interrupt left on, CMOS without `cli`. | Not fixed. |
| L15 | HARDENING | DiagOutput(NULL), an unclamped /B command tail, `old21` set after the hook (no INT 21h can come in between). | Not fixed. |
| L16 | HARDENING | Priority wrap at 7Fh (SetPriority's range keeps it far from there). | Not fixed. |
| L17 | REAL (cosmetic) | OldYield set its schedlog reason after the switch it was for. | Fixed. |
| L18 | HARDENING | `task_boot` with no task. | Not fixed. |
| L19 | UNSURE | `task_boot_tdb`/`_end` do not sync THHOOK. | open. |
| L20 | HARDENING | `dpmi_seg_to_desc(40h)` unchecked. | Not fixed. |
| L21 | HARDENING | `int_vec_switch` keeps a vector DPMI 0205h refused. | Not fixed. |
| L22 | HARDENING | PostEvent's fast path on an expand-down bogus handle. | Not fixed. |
| L23 | FALSE | GetHeapSpaces: the bytes in use cannot exceed FFFFh less the information block's offset. | None. |
| L24 | UNSURE | Catch, TK62 "catch": 3.1 writes 8 words ending at the ninth (index 8), w16 8 words ending at the eighth. What each holds is not probed. | open. |
| L25 | HARDENING | `th_loadsegs` for a segment with selector 0 (w16 loads every segment). | Not fixed. |
| L26 | HARDENING | FatalAppExit with no task (boot only). | Not fixed. |
| L27 | UNSURE | DirectResAlloc ignores hInstance and flags. | open. |
| L28 | REAL | TK62 "ansi": KERNEL.79/80 also map 8Ah/9Ah, 8Ch/9Ch, 9Fh/FFh on 3.1. | Added. |
| L29 | HARDENING | `join_canon` truncates past 143 bytes (paths past OFSTRUCT's 128). | Not fixed. |
| L30 | UNSURE | `file_env` compares variable names case-sensitively. | open. |
| L31 | UNSURE | GetTempFileName with drive 80h; a directory or locked file of the same name. | open. |
| L32 | REAL (missing) | OF_PROMPT, OF_CANCEL, OF_SEARCH not implemented; `_hread`/`_hwrite` sizes unsigned (negative sizes are HARDENING). | open (missing functionality). |
| L33 | HARDENING | `write_string` with section or key names holding ']' or '='. | Not fixed. |
| L34 | HARDENING | AddAtom/FindAtom make the table before copying the name; the name must be in a locked or fixed block anyway. | Not fixed. |
| L35 | REAL (minor) | `kputs("")` wrote 0 bytes (that truncates a redirected file at its position); `regs16` fields left unset; `dbg_path` 80 bytes. | The 0-byte write skipped; the rest not. |
| L36 | FALSE | `heap_check*` prototypes outside the include guard: repeating a prototype is valid C. | None. |

## Counts

| Severity | REAL | FALSE | 3.1-BEHAVIOUR | HARDENING | UNSURE |
|---|---|---|---|---|---|
| Critical (5) | 5 (all fixed) | 0 | 0 | 0 | 0 |
| High (29) | 12: H1, H2, H4, H9, H13, H16, H22, H23, H24, H25, H28, H29 (all fixed) | 1: H5 | 5: H19, H20, H21, H26, H27 | 10: H3, H6, H7, H8, H10, H12, H14, H15, H17, H18 (all but H3 fixed) | 1: H11 |
| Medium (43) | 12: 2, 3, 4, 7, 8, 10, 11, 15, 18, 25, 33, 36 (all fixed but 33 and 25's scheduling) | 4: 21, 27, 30, 35 | 5: 17, 28, 40, 41, 42 | 20: 1, 5, 6, 9, 12, 13, 14, 16, 19, 20, 22, 23, 24, 26, 29, 31, 32, 34, 38, 43 (1, 5, 9, 13, 19, 20, 31, 32, 43 fixed) | 2: 37, 39 |
| Low (36) | 8: L1, L4, L10, L12, L17, L28, L32, L35 (L1, L17, L28, L35 fixed) | 3: L11, L23, L36 | 0 | 17: L2, L7, L8, L9, L13, L14, L15, L16, L18, L20, L21, L22, L25, L26, L29, L33, L34 (L7 fixed) | 8: L3, L5, L6, L19, L24, L27, L30, L31 |

(H16 is counted as REAL: 3.1's KRNL286 makes the selector usable, which w16's did not; 3.1's KRNL386 does not, as w16's.)

## Tests

- `make test` (installation-05, HDPMI16 and DOSX, both kernels): pass, with TK62 checking its reference tables (`tests/apps/tk62ref.h`, `tk62ref286.h`, from `tests/tk62ref.sh`). Two of this work's first fixes failed older probes and were taken back: TK16 (the environment block outlives its task on 3.1: medium 28) and TK50 (GetCodeHandle's AX is GlobalHandle's: medium 33).
- heapcheck boots (`tools/remote-test.sh -b -e DROPIN_INI=w16:heapcheck=1`, quicktime@over, quicktime@tree386, toolhelp@tree386, all KRNL386): no heapcheck report, Windows ended with code 0 each time (before, KRNL386 stopped at the first free arena after the first sentinel). The two QuickTime parts' screens differed from 3.1's because heapcheck makes everything slow, as documented; toolhelp@tree386 passed.
- Full boot test at 77756b5: 121 ok, 4 failures, all known on master 9ed316c: ddeml (conn), lzexpand (quirks), taskman (tree, 21-dosesc), mmsystem (timer). QuickTime (all parts) and toolhelp passed.

## Left (open)

- H11: the order in which 3.1 frees an import closure (probe: a DLL loaded before its importer, both freed at once; does the importer's WEP run first?).
- Medium 25: the scheduler does not honour LockCurrentTask.
- Medium 33: GetCodeHandle/GlobalHandle of KERNEL's own segments (put them in the selector table).
- Medium 37, 39; Low L3, L5, L6, L19, L24 (Catch's layout), L27, L30, L31: not probed.
- Low L4 (LocalCompact(-1) past 32 blocks), L12 (the bootstrap's relocation records), L32 (OF_PROMPT, OF_CANCEL, OF_SEARCH).
