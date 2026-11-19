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

Status: every finding has a verdict (no UNSURE left). "open" in the Action column marks
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
| H2 | REAL | `grow(dos=1)` (both): `r.bx = (u16)(size >> 4)` truncated, and `chunk_add` was given the untruncated size. Reachable with GlobalDOSAlloc(>= 1 MB). | DOS-memory growth past FFFFh paragraphs fails. Regression: TK62 "dosalloc" (GlobalDosAlloc of 100000h and 10FFF0h fail, as on 3.1 in all three runs). |
| H3 | HARDENING | `discard` keeps a copy of the arena across the GlobalNotify callback; a callback that frees or moves memory breaks it. The SDK (NotifyProc): "should not call any routine that might move memory". | Fixed (cheap): after the procedure, a GMEM_NOTIFY block is discarded only if it is still the same unlocked discardable block, and the oldest-first scan stops when the procedure changed the LRU ring. |
| H4 | REAL | KRNL386 `take_next` ignored `carve` failing (no arena slot); GlobalReAlloc returned success at the old size. | `take_next` returns 0 and GlobalReAlloc fails. |
| H5 | FALSE | The shrink path frees the upper tiles and then asks for one descriptor (`sel_new`): with at least one just freed it cannot fail; with none freed, `n` is unchanged and nothing is inconsistent. | None. |
| H6 | HARDENING | `new_handle`: a LocalHandleDelta of 3FFFh or more wrapped the table size and wrote the handles past a tiny arena. | Fixed (cheap): such a delta makes the handle allocation fail. Regression: TK62 "hdelta" (LocalHandleDelta(4000h), then moveable blocks; the heap works afterwards). Windows 3.1 ends there itself, both kernels, so it runs last. |
| H7 | REAL | `f_read_seg` read `flen` bytes into a DGROUP clamped to FFF0h. TK62 "bigdg": 3.1 loads a DLL whose DGROUP holds FFF8h bytes of file data (both kernels), so this is no malformed file. | The DGROUP is never smaller than its file data; `f_read_seg` also refuses to read past its block. Regression: TK62 "bigdg" (TKBIGDG.DLL, `tools/mkne.py --dgroup 0xFFF8`, loads as on 3.1). |
| H8 | HARDENING | `patch`/`redo_self_fixups`: relocation chains not bounded by the segment, and a cyclic chain looped for ever. | Fixed: offsets checked against the segment's limit, chains cut at 8000h links; LOBYTE no longer reads a word. |
| H9 | REAL | `load_body`: `entry_off + entry_len` wrapped; table offsets not checked; `2 * modref_count` wrapped in `mdb_build`. | Offsets in order and below FFF0h, else LE_BADFORMAT (11); module reference size in 32 bits; alignment shift over 16 rejected. Regression: TK62 "badne" (TKBADNE.DLL, `tools/mkne.py --entry-len 0xFFF0`, fails to load, as on 3.1). |
| H10 | HARDENING | `type_first` did not check the first type's count against the table's end; loops over its entries ran past it. | Fixed: `type_fits` for every type returned. |
| H11 | REAL | `sweep` freed DLLs at usage 0 in module-list order. TK62 "wep" (TKWA.DLL imports TKWB.DLL, each WEP appends its letter to the probe's buffer): on 3.1 (Standard mode, KRNL286) TKWB's WEP runs before TKWA's whether TKWB or TKWA was loaded first. The audit's proposed fix (importers first) is the opposite of 3.1. (3.1 in 386 Enhanced mode ended Windows in these LoadLibrary calls; the section is skipped there.) | `sweep` frees an unused DLL only when it imports no other unused DLL (a cycle falls back to list order). |
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
| 12 | HARDENING | `task_thunk`: a failed `data_alias` (no selector) orphans the new thunk block. | Fixed (cheap): the alias that links the new block is taken first; a failure frees what was made. |
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
| 25 | REAL | TK62 "lock" (all modes): on 3.1 LockCurrentTask(TRUE) returns the task and IsTaskLocked gives it; while it is locked, another task that only yields gets no turn; after unlocking it runs again. w16 ignored the lock, and kept the locked task and FileCdr's task after they ended. | `reschedule` keeps a locked task that has an event; a locked task that waits lets others run (UNVERIFIED on 3.1). The task's end clears both. |
| 26 | HARDENING | `rmc` in task.c is shared by `idle` and `set_psp`; nothing calls `set_psp` at interrupt time. | Not fixed. |
| 27 | FALSE | `kernel_psp` is never nested: `kputs` and `kdbg_file` call it 1/0 around plain DOS calls. | None. |
| 28 | 3.1-BEHAVIOUR | The environment block copied for a task is KERNEL's and outlives it: TK16 (3.1) reads its GlobalSize after the task ended (10h/20h). Giving it to the task's PDB failed TK16. | Kept (the comment says why). |
| 29 | HARDENING | task_create/inittask with a malformed NE (SP < 26, SS not DGROUP); `libinit` kept on an error path. | Not fixed. |
| 30 | FALSE | "Reversed record layout": Microsoft's TOOLHELP on w16's KERNEL (toolhelp boot test, runs ms-tree286 and ms-tree386) reports `LOGPARAMERROR code=6001 addr=THP param=2222`, the same as on 3.1. So w16's {code, proc, param} is the order 3.1's TOOLHELP reads. The static record is fine: notifications are not reentrant. | None. |
| 31 | HARDENING | `notify`: a ToolHelpHook procedure that unhooks itself had the RegisterPtrace one called too. | Fixed: both hooks read before either is called. |
| 32 | HARDENING | GetCodeInfo matched an empty segment slot (selector 0) for selectors 0-7. | Fixed. |
| 33 | REAL (open) | TK62 "codehandle" and "kseg" on 3.1 (all three runs): KERNEL's code and data segments are fixed blocks of the global heap: GlobalHandle(CS) = CS (odd: fixed), GlobalFlags 0, GlobalSize covers the limit, FarGetOwner = KERNEL's module, GetCodeHandle = that handle; the module table's segment entries hold the selectors; segments 2 and 3 (discardable code) show GlobalFlags 0900h. (Also: GlobalHandle of KERNEL's module table is not the table's selector on 3.1, it is on w16: not followed up.) In w16 the segments are DOS memory the bootstrap allocated (`kernel/boot/boot.c`, one DOS block each, its MCB in the paragraph before), outside the heap, so GlobalHandle gives 0. | open, by design: not done in this round (see "Medium 33: the design" below). TK62 "kseg" records w16's values unchecked (OBS ~). |
| 34 | HARDENING | FatalExit: no guard against a FatalExitHook that calls FatalExit; DS = SS for the hook. | Fixed (cheap): the hook is not called again while it runs (DS = SS kept). |
| 35 | FALSE | `hmemcpy`'s `sl + n` cannot wrap: linear addresses and sizes are far below 4 GB. | None. |
| 36 | REAL | TK62 "files": on 3.1 OpenFile("COM1:"), ("FOO:"), ("A:B:C") with OF_PARSE fail; w16 took ':' as a name character. A NULL name is HARDENING. | ':' is a bad name character. |
| 37 | REAL | TK62 "openerr" on 3.1 (all three runs), the current directory C:\: with no DOS handle left, nErrCode 4 with a path, searched for (szPathName C:\NAME), and for a file that is nowhere; a directory of the name: 5 with a path and searched for; the search ends at the first directory where DOS fails with neither 2 nor 3, even when a later directory (the system directory) holds the file; a failed search reports the name in the current directory. w16's search reported 2 for every failure. | `search_open` stops at such an error and OpenFile reports it (`file.c`). Regression: TK62 "openerr". |
| 38 | HARDENING | `temp_dir` takes a 3-character TEMP without checking it (a bad environment). | Not fixed. |
| 39 | REAL | TK62 "drivetype" on 3.1, F: a DOSBox floppy mount (IOCTL 4409h answers, 4408h fails): GetDriveType 3 (fixed), as SYSTEM.DRV's InquireSystem(1, 5) (Schulman: GetDriveType wraps it; w16's SYSTEM.DRV already said 3); w16's KERNEL said 0. GetDriveType(0101h) gives 3 on 3.1 (w16 0: an absurd argument, logged, not matched). | A failing 4408h means fixed. Regression: TK62 "drivetype" (`tests/tk62ref.sh` and `tests/run-tests.sh` mount F: as a floppy drive). |
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
| L3 | REAL | TK62 "discsel": on 3.1 (all modes) GlobalReAlloc of a discarded block named by its selector returns its even handle; w16's KRNL286 returned the odd one. | Even. |
| L4 | REAL | TK62 "lcompact": 3.1 discards all 48 of 48 unlocked discardable blocks with LocalCompact(-1); w16 took 32 at most a call (NDISC). | `discard_all` takes them 32 at a time, each batch below the last one's lowest arena, until a batch is short. Regression: TK62 "lcompact". |
| L5 | REAL (KRNL286) / 3.1-BEHAVIOUR (KRNL386) | TK62 "freespace": 3.1's KRNL286 does not count a fixed discardable block in GetFreeSpace(0) and GlobalDiscard leaves it; 3.1's KRNL386 (Standard mode) counts it and GlobalDiscard discards it (flags 4100h), as w16's KRNL386 already did. (386 Enhanced mode: 3.1 ended Windows in this step; not observed, skipped there.) | KRNL286: such blocks no longer counted (`global.c`). Regression: TK62 "freespace" (`tk62ref286.h` for KRNL286). |
| L6 | 3.1-BEHAVIOUR | TK50 "LocalNotify kept" = 9Ch on 3.1 (SI, DI, BP, DS kept; ES not): 3.1's LocalNotify does not keep ES either. | Kept. |
| L7 | HARDENING | `module_of` matched handles 1-3 to empty segment slots. | Fixed. |
| L8 | HARDENING | LoadResource's usage count is a word. | Not fixed (atoms and events wrap the same way on 3.1). |
| L9 | HARDENING | `tb_new` ignores `gset_code` failing (DPMI failure). | Fixed (cheap): a failure frees the block. |
| L10 | REAL (cosmetic) | Unimplemented-import thunks for names over 32 characters sharing a prefix share a thunk (only the reported name is wrong). | Not fixed. |
| L11 | FALSE | `module_load_program` is never called with a task running and `last_libinit` set. | None. |
| L12 | REAL (minor) | The bootstrap's relocation records (`rel_para`, a few KB of DOS memory) were never freed. | Freed after the relocations (INT 21h AH=49h through DPMI 0300h, `kernel/boot/boot.c`). make test boots KERNEL on both DPMI hosts; boot test at the end. |
| L13 | HARDENING | Bootstrap: a missing environment, NE length wrap, LOBYTE chains, entry bounds (KERNEL's own file). | Not fixed. |
| L14 | HARDENING | Watchdog ([w16] debugging aid): `wd_limit` overflow, RTC interrupt left on, CMOS without `cli`. | Part fixed: the limit no longer wraps (clamped); the rest not. |
| L15 | HARDENING | DiagOutput(NULL), an unclamped /B command tail, `old21` set after the hook (no INT 21h can come in between). | DiagOutput(NULL) does nothing (fixed); the rest not. |
| L16 | HARDENING | Priority wrap at 7Fh (SetPriority's range keeps it far from there). | Not fixed. |
| L17 | REAL (cosmetic) | OldYield set its schedlog reason after the switch it was for. | Fixed. |
| L18 | HARDENING | `task_boot` with no task. | Not fixed. |
| L19 | REAL (minor) | Schulman: GetCurrentTask reads CURTDB from THHOOK, so on 3.1 the two cannot differ. w16's THHOOK lagged: CurTDB 0 while the boot TDB was current, possibly the freed boot TDB after it went, and 0 for the first task until its first switch-in. | `thhook_sync` in `task_boot_tdb`, `task_boot_tdb_end` and `task_boot`. Not observable by a probe that runs as a task. |
| L20 | HARDENING | `dpmi_seg_to_desc(40h)` unchecked. | Not fixed. |
| L21 | HARDENING | `int_vec_switch` keeps a vector DPMI 0205h refused. | Not fixed. |
| L22 | HARDENING | PostEvent's fast path on an expand-down bogus handle. | Not fixed. |
| L23 | FALSE | GetHeapSpaces: the bytes in use cannot exceed FFFFh less the information block's offset. | None. |
| L24 | REAL | TK62 "catchmap" (all modes): 3.1's CATCHBUF holds CS in word 1, the caller's BP in 3, DS in 6, nothing in 7 and SS in 8 (IP, SP, SI, DI by elimination in 0, 2, 4, 5); w16 had SS in 3, BP in 4 and DS in 7. | 3.1's order (Catch and Throw). |
| L25 | HARDENING | `th_loadsegs` for a segment with selector 0 (w16 loads every segment). | Not fixed. |
| L26 | HARDENING | FatalAppExit with no task (boot only). | Not fixed. |
| L27 | REAL | TK62 "directres" on 3.1: DirectResAlloc(hInstance, flags, 100h) gives a moveable block (even handle) owned by the module (FarGetOwner = hModule; Schulman: through GetExePtr) for flags 0, 10h, 1010h and 1000h, discardable (GlobalFlags 0100h) with 1000h. w16 gave the task a plain moveable block. | Owned by hInstance's module, discardable with 1000h (`kmisc.c`); an hInstance of no module: the caller's block as before (UNVERIFIED). Regression: TK62 "directres". |
| L28 | REAL | TK62 "ansi": KERNEL.79/80 also map 8Ah/9Ah, 8Ch/9Ch, 9Fh/FFh on 3.1. | Added. |
| L29 | HARDENING | `join_canon` truncates past 143 bytes (paths past OFSTRUCT's 128). | Not fixed. |
| L30 | 3.1-BEHAVIOUR | TK62 "envcase" on 3.1: with the task's PATH= renamed path= in place, OpenFile no longer finds MOUNT.COM on Z: (nor with XATH=, so 3.1 reads the environment on every call): the name is compared with its case, as `file_env` does. | Kept (TK62 checks it). |
| L31 | REAL (part) / 3.1-BEHAVIOUR (part) | TK62 "tempname" on 3.1: GetTempFileName(80h, "TKP", 1234h) gives "C:~TKP1234.TMP" (80h alone is the current drive; w16 gave "~TKP1234.TMP"). Two calls in a row give consecutive numbers; a third, with a directory of the next number's name, fails with 0 on 3.1, as on w16. | Drive 80h alone: the current drive (`file.c`). The directory case kept. Regression: TK62 "tempname". |
| L32 | REAL (part fixed) | OF_SEARCH (the same bit as OF_VERIFY, 400h): TK62 "ofsearch" on 3.1 finds "C:\TK62NO\TKDG.DLL" (no such directory) in the Windows directory; without it, error 3. OF_PROMPT and OF_CANCEL: the box is not observed (Pietrek: the loader opens DLLs with OF_PROMPT \| OF_CANCEL; w16 draws KERNEL's "Cannot find" box in the loader itself, `file_error_box`). `_hread`/`_hwrite` with negative sizes: HARDENING. | OF_SEARCH: a file not at its path is searched for by its name (`file.c`). Regression: TK62 "ofsearch". OF_PROMPT/OF_CANCEL: open (needs 3.1's box observed, e.g. as steps of TK59, boot-test case missingdll). |
| L33 | HARDENING | `write_string` with section or key names holding ']' or '='. | Not fixed. |
| L34 | HARDENING | AddAtom/FindAtom make the table before copying the name; the name must be in a locked or fixed block anyway. | Not fixed. |
| L35 | REAL (minor) | `kputs("")` wrote 0 bytes (that truncates a redirected file at its position); `regs16` fields left unset; `dbg_path` 80 bytes. | The 0-byte write skipped; the rest not. |
| L36 | FALSE | `heap_check*` prototypes outside the include guard: repeating a prototype is valid C. | None. |

## Counts

| Severity | REAL | FALSE | 3.1-BEHAVIOUR | HARDENING | UNSURE |
|---|---|---|---|---|---|
| Critical (5) | 5 (all fixed) | 0 | 0 | 0 | 0 |
| High (29) | 14: H1, H2, H4, H7, H9, H11, H13, H16, H22, H23, H24, H25, H28, H29 (all fixed) | 1: H5 | 5: H19, H20, H21, H26, H27 | 9: H3, H6, H8, H10, H12, H14, H15, H17, H18 (all fixed) | 0 |
| Medium (43) | 14: 2, 3, 4, 7, 8, 10, 11, 15, 18, 25, 33, 36, 37, 39 (all fixed but 33) | 4: 21, 27, 30, 35 | 5: 17, 28, 40, 41, 42 | 20: 1, 5, 6, 9, 12, 13, 14, 16, 19, 20, 22, 23, 24, 26, 29, 31, 32, 34, 38, 43 (1, 5, 9, 12, 13, 19, 20, 31, 32, 34, 43 fixed) | 0 |
| Low (36) | 14: L1, L3, L4, L5, L10, L12, L17, L19, L24, L27, L28, L31, L32, L35 (all fixed but L10, and L32's OF_PROMPT/OF_CANCEL) | 3: L11, L23, L36 | 2: L6, L30 | 17: L2, L7, L8, L9, L13, L14, L15, L16, L18, L20, L21, L22, L25, L26, L29, L33, L34 (L7, L9 fixed; L14, L15 in part) | 0 |

(H16 is counted as REAL: 3.1's KRNL286 makes the selector usable, which w16's did not; 3.1's KRNL386 does not, as w16's. L5 likewise: REAL for KRNL286, 3.1-BEHAVIOUR for KRNL386. L31 is counted as REAL for its drive 80h part.)

## Tests

- `make test` (installation-05, HDPMI16 and DOSX, both kernels): pass, with TK62 checking its reference tables (`tests/apps/tk62ref.h`, `tk62ref286.h`, from `tests/tk62ref.sh`). Two of this work's first fixes failed older probes and were taken back: TK16 (the environment block outlives its task on 3.1: medium 28) and TK50 (GetCodeHandle's AX is GlobalHandle's: medium 33).
- heapcheck boots (`tools/remote-test.sh -b -e DROPIN_INI=w16:heapcheck=1`, quicktime@over, quicktime@tree386, toolhelp@tree386, all KRNL386): no heapcheck report, Windows ended with code 0 each time (before, KRNL386 stopped at the first free arena after the first sentinel). The two QuickTime parts' screens differed from 3.1's because heapcheck makes everything slow, as documented; toolhelp@tree386 passed.
- Full boot test at 77756b5: 121 ok, 4 failures, all known on master 9ed316c: ddeml (conn), lzexpand (quirks), taskman (tree, 21-dosesc), mmsystem (timer). QuickTime (all parts) and toolhelp passed.

- After H11, medium 25, L3 and L24 (fe9594a, on master 14d77ae): make test pass (TK62's new sections pass on both kernels, both DOS extenders; one TK61 run failed its KRNL286/DOSX FS/GS checks once and passed when run again: intermittent, not touched here); full boot test 122 ok, 3 failures, all known on master (holdbtn, ddeml conn, lzexpand quirks).

- After the UNSURE items, L4, L12, L32 and the hardening (07940ac, on master 3befab5): make test pass (both kernels, both DOS extenders); full boot test 122 ok, 3 failures, all known on master: ddeml (conn), lzexpand (quirks), trails (timed: one run-down of 439 ms against 440-660 ms, timing). toolhelp and quicktime passed.
- The TK61 failure under KRNL286 on DOSX (FS/GS checks, once, under load): not reproduced in 12 runs of the probes; the cause, by reading: under KRNL286 the selector a call frees goes back to the DOS extender at once, and a timer tick between the free and the probe's read of FS goes to real mode; w16's DOSX (`fsgs_restore`) loads 0 for the dead selector when it comes back (Microsoft's ends Windows there: TK61 "deadfs"). Timing, not KERNEL: TK61 now runs such a measurement again when the BIOS tick count changed during it (e03be14).

## Medium 33: the design (not done)

What 3.1 shows (TK62 "kseg", all three runs): every KERNEL segment is a
block of the global heap. The code segment (1) and DGROUP (4) are fixed
(handle = selector, GlobalFlags 0); the discardable code segments (2, 3)
have GlobalFlags 0900h (discardable, GA_DISCCODE) and their handle is
their selector too; FarGetOwner is KERNEL's module; GlobalSize covers
the segment; the module table's segment entries hold the selectors.
GetCodeHandle of a KERNEL procedure is that handle (TK50: its AX is
GlobalHandle's).

What w16 has: the bootstrap (`kernel/boot/boot.c`) gives each segment
its own DOS block (INT 21h AH=48h); the paragraph in front of each is
DOS's MCB, so there is no room for an arena, and the blocks are not in
the heap's address-ordered arena list. GlobalHandle finds nothing (0).

How it could be done cleanly:

1. Bootstrap: one DOS block for all of KERNEL's segments, laid out as a
   heap chunk: the chunk's head (0x20 bytes, `chunkinfo` at +10h), then
   for each segment an arena paragraph (rounded to the heap's 0x20-byte
   granule) followed by the segment, then the closing arena. The
   segments' real-mode paragraphs (`bootinfo.seg_para`) follow from
   that layout; nothing else in the bootstrap changes.
2. KERNEL, before its first allocation (`heap_init`): add that block as
   a chunk of a new kind (`chunk_add` with the bootstrap's memory, not
   freed to DOS or DPMI) and turn each segment's place into a block:
   arena fixed, owner KERNEL's module, handle = the bootstrap's
   selector (`bootinfo.seg_sel`), the selector-table entry pointing at
   the arena; the discardable code segments flagged as 3.1 shows them
   (GlobalFlags 0900h) but never discarded (w16 has no demand loading).
   KRNL386 (`global386.c`) keeps its arenas in a table of its own: the
   same, through its arena slots.
3. The module table's segment entries already hold the selectors;
   `KGetCodeHandle`'s special case for KERNEL goes.

What it touches and must be checked: TOOLHELP's GlobalFirst/GlobalNext
(the boot-test case `toolhelp` compares w16's walks with 3.1's byte for
byte under masks: KERNEL's blocks would now appear, as on 3.1, and the
masks may need to change), `heapcheck` (a chunk that is never given
back), compaction (fixed blocks: nothing moves them), GlobalFree or
GlobalReAlloc of a KERNEL segment (3.1: not probed), TK50 and TK62
"codehandle"/"kseg" (then checked against 3.1), and the boot on both
DPMI hosts and under WIN386.

## Left (open)

- Medium 33: KERNEL's own segments in the global heap (design above).
- L32: OF_PROMPT and OF_CANCEL (3.1's box not observed yet).
- L10 (unimplemented-import thunks for names over 32 characters) is cosmetic and not fixed.
- Noticed, not looked into: under w16 (master 3befab5 too) TK62 "entries" fails to load TKENT2.DLL (12000 entries) with "out of memory"; the C5 action above says it loads. 3.1 fails it as well, and the observation is unchecked (OBS ~).
- GetDriveType(0101h): 3 on 3.1, 0 on w16 (an argument past Z:; unchecked).
- GlobalHandle of KERNEL's module table: not its selector on 3.1, its selector on w16 (TK62 "kseg", unchecked).
- Open HARDENING items not fixed: those marked "Not fixed" above (each needs malformed files, absurd arguments or a failing DPMI host).
