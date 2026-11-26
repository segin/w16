# OLECLI.DLL audit triage

An outside, read-only audit of w16's OLE libraries (`ole/olecli/`,
`ole/olesvr/`; not in the repository) listed findings for OLECLI.DLL in
four groups: C1-C4, H1-H7, M1-M10 and L1-L8 here, in its order. Each was
checked by an independent reader against the current code, the OLE 1.0
chapters of the Windows 3.1 SDK and what ARCHITECTURE.md 16 records of
3.1's OLECLI (the OC1/OCSPY probes). No emulator was run for the triage
itself; the fixes were then run through the OLE boot-test cases (Tests,
below). OLESVR.DLL has a triage of its own.

Verdicts: **REAL** (confirmed; fixed), **FALSE** (the claim is wrong),
**3.1-BEHAVIOUR** (3.1 does the same: kept), **HARDENING** (only with
malformed input, a misbehaving server or client, or no memory; fixed
when cheap and invisible to working use), **PARTLY** (some sub-claims
hold: those are fixed, the rest are not), **UNSURE** (needs a run of
3.1). Where 3.1 itself dies, w16 may live.

Status: every finding has a verdict; none waits for a 3.1 probe. H5
was fixed and then reverted when the boot test showed 3.1 does the
same. L7's lock is a new implementation of something never observed (ARCHITECTURE.md
16.7 lists OleLockServer as not observed) (2026-11-26).

## C

| ID | Verdict | Evidence | Action | Commit |
|---|---|---|---|---|
| C1 | REAL (overflow); HARDENING (loop) | clip.c parse_link was given `sizeof file` (260) as the size of all three buffers, but OleCreateFromClip and OleCreateLinkFromClip declare `cls[128]`: a class name of 128 or more characters in OwnerLink or ObjectLink (another process's data) wrote up to 132 bytes past `cls` on the stack. The index was a WORD compared with GlobalSize's DWORD: a block over 64K with no NUL in its first 64K looped for ever. | Each buffer has its own size; the walk stops at the block's size, at most 64K, and never wraps. | 0a2c48d |
| C2 | REAL | ST_CONNECT_SYS and ST_CONNECT_DOC call initiate_retry, which dispatches the client's messages for up to 30 s while a started server registers, before `busy` is set (run() sets it on the first wait). An OleDelete or OleRelease from a window procedure then passed finish()'s busy test, freed the object, and step_do and run() went on writing to it; OleUpdate and the like could restart the step list under run(). No probe covers this; a use after free is not fidelity. | `in_step` set around both initiate_retry calls; busy_check, finish (OleDelete/OleRelease) and OleQueryReleaseStatus answer OLE_BUSY while it is set. The lock on the step list across step_do is kept (M4: the selector stays valid). | 0a2c48d |
| C3 | PARTLY | The SDK lets a client call the library from its callback (reentrant), with only async calls discouraged on OLE_CLOSED. run(): an OleDelete from OLE_RELEASE of an object no longer open freed it synchronously, then run() read `free_after`; with the object still open, the nested delete set `free_after` and the outer run() freed the object at once, dropping the new close and its OLE_RELEASE. on_terminate(): OleDelete from OLE_CLOSED freed the object, then `closing` and `wait` were written and read. on_data() is FALSE: its object is open, so OleDelete takes the asynchronous path. | An object keeps its block while an OleWndProc call for it runs (`depth`): obj_free then frees everything else, clears the magic (the API refuses the object) and marks it `dead`; OleWndProc frees the block when the last call returns. run() decides `free_after` before the OLE_RELEASE callback and frees only when the callback started nothing; run(), on_data(), on_terminate() stop at once on a dead object; callback() skips dead objects. | 0a2c48d |
| C4 | REAL | LeActivate built `[StdDoVerbItem("<item>",...)]` with wsprintf into `char cmd[300]`; the item comes unchecked from the caller, the clipboard or a stream (get_str), so an item over about 262 characters overran the stack. | The command is built in a global block sized from the item (the rest is at most 37 characters) and copied into a block of its exact length as before, so the EXECUTE is byte for byte the same. | 0a2c48d |

## H

| ID | Verdict | Evidence | Action | Commit |
|---|---|---|---|---|
| H1 | HARDENING | stream.c get_str: a length of FFFFFFFFh made `n + 1` 0 (a NULL buffer to the client's Get and a write through it); 64K or more put the NUL at `n & 0xFFFF` of the first segment; a failed read returned the block anyway, and strm_load leaked the class name on that error. Only a malformed or truncated stream. | Lengths of FFFFh and over are a stream error; a failed read frees the block and returns NULL; strm_load frees the class name on error. | 0a2c48d |
| H2 | HARDENING | load_body's BITMAP record: CreateBitmap reads rows of its own word-aligned width (w16 GDI bm_create) times height times planes from a block of the record's size, never checked. w16 writes the size from the same bitmap, so only a corrupt stream. Comparing against the stream's bmWidthBytes would not do (GDI computes its own). | Before allocating, a non-positive size, zero planes or bits, or bits short of what CreateBitmap reads is a stream error. | 0a2c48d |
| H3 | HARDENING | pres.c: dib_colors cut biClrUsed to 16 bits, dib_palette's `4 + n * 4` wrapped in 16 bits (a NULL or short block written through), draw_dib's colour-table offset likewise; no check that the header, colours and bits fit the block for PRES_DIB or a Paintbrush .BMP; no GlobalLock checks for PRES_DIB and PRES_MF; take_picture wrote the resolution into a block not known to hold a BITMAPINFOHEADER. Only malformed pictures. | dib_info: header (biSize at least a BITMAPINFOHEADER), colour table (biClrUsed at most 256 up to 8 bits, 3FC0h above) and, uncompressed, the bits must fit the block (DWORD arithmetic), else the picture is blank (OLE_ERROR_BLANK) for bounds and drawing; dib_palette sizes in DWORD and checks its lock; NULL locks give OLE_ERROR_BLANK; take_picture checks the size first. | 0a2c48d |
| H4 | REAL | LeSetHostNames keeps the bare StdHostNames data; poked while open it goes through poke_block (DDEPOKE header), but LeActivate of a closed embedded object queued `copy_global(o->hhost)` raw. The server reads cf from bytes 2-3 and the names from byte 8: garbled host names on an ordinary path (OleSetHostNames, then OleActivate). | poke_block(cf_binary, o->hhost), as for the open case. | 0a2c48d |
| H5 | 3.1-BEHAVIOUR | dde_close_all posts `[StdExit]` and TERMINATE on the system conversation and destroys the window at once; the server's ACK, which returns the command for the client to free (DDE), then finds no window and the block is never freed. The finding's fix (keep the window until the ACK) was tried: the `errors` scenario then logged `EXECUTE OLECLI:OleSrvrWndClass -> OLESVR "[StdExit]"` where 3.1's OLECLI shows `EXECUTE dead:*`, i.e. 3.1 destroys the window before the server reads the command, and so leaks the same block. Matching the observed conversation wins over a one-block leak per delete. | Kept (a comment in dde_close_all says why). | 6df4107 |
| H6 | PARTLY | clear_steps never freed a POKE's data. Pokes never posted (queued after a StdEditDocument the server refused, or a failed launch) and a poke whose PostMessage failed leaked the block, and for a picture the handle in it: REAL. on_ack's inner-handle claim is FALSE: for a picture the server's OLESVR hands SetData the handle itself (ARCHITECTURE.md 16.9), so after the ACK it is the server's. | poke_free (the DDEPOKE block and the metafile, bitmap or palette in it); clear_steps frees every step's data except a POKE posted and waiting for its ACK; a failed post frees it; the steps on_terminate skips to ST_WAIT_DOCTERM are freed too. on_ack unchanged. | 0a2c48d |
| H7 | 3.1-BEHAVIOUR | launch() passes " -Embedding [file]" with the space as LoadModule's length byte, so KERNEL copies 32 bytes and a server's command line holds at most 21 characters of the path. Probe OC3 saw tail length 32 with Microsoft's OLECLI both for a new object (12 characters) and for a file, and TK68 that 3.1's KERNEL takes the length as given: 3.1 truncates the same way. No over-read (the block is zeroed, at least 47 bytes). | None. | - |

## M

| ID | Verdict | Evidence | Action | Commit |
|---|---|---|---|---|
| M1 | FALSE | OleRevokeClientDoc and obj_free read `hself` after GlobalUnlock to pass it to GlobalFree. The blocks are moveable, not discardable, and protected-mode selectors stay valid across moves; nothing runs in between. | None (obj_free now copies it first anyway, for C3). | - |
| M2 | PARTLY | obj_free never freed `hlinkdata`, the OwnerLink/ObjectLink block LeGetData keeps (the SDK: the library owns it): one block per object after OleGetData of a link format, REAL. `hloose` (StdCloseDocument posted after "/Close" data) leaks only if the object goes before the server's ACK, or the server never answers: HARDENING; freeing it in obj_free could race the server. | obj_free frees `hlinkdata`. `hloose` is freed at delete only when the server window it went to is gone. Freeing it on the server's document TERMINATE was tried and dropped: in card1 the server's TERMINATE crosses the command, which the server still reads afterwards (the spy logged "badcmd" where 3.1 shows "[StdCloseDocument]"). | 0a2c48d, 6df4107 |
| M3 | FALSE | `free_after` is read only on run()'s success path, but in finish()'s step lists the only step that can fail is the first ST_EXEC, which fails synchronously (finish then frees the object itself); StdCloseDocument's NACK just advances (arg 0); every server TERMINATE leads to ST_WAIT_DOCTERM or step_next. No reachable leak. | None. | - |
| M4 | PARTLY | GlobalReAlloc of the locked step list is FALSE as a defect (protected mode: the selector survives the move; ARCHITECTURE.md global heap). insert_next after a failed grow shifted over the last step and wrote one STEP past the block: HARDENING (no memory only). | dde_add returns FALSE when it cannot grow and frees the step's data; insert_next does the same and fails; step_do fails the operation with OLE_ERROR_MEMORY. | 0a2c48d |
| M5 | HARDENING | on_data and take_data used GlobalLock's result unchecked; StdDocumentName was copied with no bound; DATA without a handle neither deleted the atom nor failed a pending REQUEST (W_DATA for ever). The `GlobalSize - 4` underflow needs a bad handle (would fault earlier). Only a misbehaving server: w16 asks for no warm links. | A DATA without a handle or with one that does not lock deletes the item atom and fails a REQUEST waiting for it (OLE_ERROR_COMM); sizes below 4 give 0; StdDocumentName is copied at most to the block's end. | 0a2c48d |
| M6 | REAL | LeEqual compared static objects with same_global on `hpres`: for a bitmap that is a GDI handle (GlobalSize 0: different bitmaps equal), and for a metafile picture it compared the METAFILEPICT with its hMF, so OleClone's copy (pres_copy copies the metafile) was never equal to its source. Only embedded copies were observed (ARCHITECTURE.md 16); the SDK defines equality for embedded and linked objects only. | Static objects: the same kind and format first; metafile pictures by mapping mode, extents and the metafile's bytes; bitmaps by size, format and GetBitmapBits; DIB and generic data by bytes as before. | 0a2c48d |
| M7 | PARTLY | copy_obj copied a link's item into OleCopyFromLink's embedded object, which then sent it in `[StdDoVerbItem("<item>",...)]` and OwnerLink, where every other embedded object (from the clipboard, from a stream) has "": REAL, low (UNVERIFIED path). `hnet` on an embedded object is never read: harmless. | hitem and hnet are copied only for a link or static copy. | 0a2c48d |
| M8 | PARTLY | ST_CONNECT_DOC's system-topic fallback overwrote a live `hsys` (left by an earlier connect to a running server, ST_MAYBE_TERMINATE ending only the document): the old window and its server conversation were orphaned, its extra long pointing at the object even after it was freed: REAL. ST_CONNECT_SYS overwrites only when the server's window went without a TERMINATE: HARDENING. LeClose on its own leaks nothing (finish ends the system conversation). | ST_CONNECT_DOC uses the open system conversation again (StdOpenDocument on it), or ends a dead one first; ST_CONNECT_SYS destroys a stale window first. LeClose unchanged. | 0a2c48d |
| M9 | HARDENING | pres_save_body's bitmap record: when GlobalAlloc for the bits failed, the header promising n+10 bytes was written and OleSaveToStream still said OLE_OK. No memory only. | The bits are allocated before anything is written; failure is OLE_ERROR_STREAM (ObjQuerySize: OLE_ERROR_BLANK). | 0a2c48d |
| M10 | HARDENING | clip_copy ignored SetClipboardData's result: a refused copy (the clipboard not opened, which the SDK says the client must do, or USER out of memory) leaked. | A refused block is freed (a metafile picture with its metafile, a bitmap deleted); the result stays OLE_OK. | 0a2c48d |

## L

| ID | Verdict | Evidence | Action | Commit |
|---|---|---|---|---|
| L1 | HARDENING | SetNetName read `f[1]` of an empty topic (one byte past the string, inside the segment); only a link with no file name. | The second character only when there is a first. | 0a2c48d |
| L2 | HARDENING | ST_SYS_EXEC, ST_EXEC and ST_POKE posted their data unchecked; it is NULL only when an allocation failed while the steps were queued, and then the server got a 0 handle. | Those steps fail with OLE_ERROR_MEMORY instead (ST_POKE before its atom). | 0a2c48d |
| L3 | HARDENING | load_body: when SetMetaFileBits refused the stream's bytes (not a metafile) the block was lost. Corrupt stream only. | Freed then; the rest of the load unchanged. | 0a2c48d |
| L4 | HARDENING | DefLoadFromStream did not check the stream (OleLoadFromStream does); a handler passing NULL faulted in get(). | OLE_ERROR_STREAM, after the document check as in OleLoadFromStream. | 0a2c48d |
| L5 | HARDENING | MfCallBackFunc called the client's CallBack without the vtbl and CallBack checks callback() makes; pres_draw's g_drawing/g_records/g_abort were cleared after a draw, so an OleDraw from inside OLE_QUERY_PAINT stopped the outer draw's queries or left its abort behind. | The same checks as callback(); pres_draw saves and restores the three. Not covered (outside the finding): an OleDelete from OLE_QUERY_PAINT still frees the object pres_draw is drawing. | 0a2c48d |
| L6 | PARTLY | LeSetColorScheme's size wrapped in 16 bits for 3FFEh or more entries (a NULL or short block written through): HARDENING (no real palette has more than w16 GDI's 3FC0h). pres_copy's CF_PALETTE: FALSE (GetObject gives at most 3FC0h; a non-palette writes nothing). | More than 3FC0h entries: OLE_ERROR_PALETTE. pres_copy unchanged. | 0a2c48d |
| L7 | REAL | OleLockServer counted on the object (`locked`) and nothing read the count; the handle was the object, so OleUnlockServer after OleDelete (which the SDK says must still be called) failed with OLE_ERROR_HANDLE. The SDK: the lock keeps the server in memory, counts per server, survives the object, and may launch (OLE_ERROR_LAUNCH). Not observed (ARCHITECTURE.md 16.7). | A lock record per server class: OLECLI's own system conversation with the server ("OLEsystem", then "System"; started hidden when none answers), counted; the LHSERVER is the record; the last OleUnlockServer TERMINATEs and frees it; an unknown handle or one already unlocked is OLE_ERROR_HANDLE. | 0a2c48d |
| L8 | PARTLY | "Leaves DGROUP locked": FALSE, w16's LocalInit locks nothing. LOCALINIT's result was ignored: HARDENING (heapsize 1024 in a DGROUP that holds it cannot fail, and OLECLI never uses its local heap). | A failed LocalInit fails the load, as gdi/entry.asm does. | 0a2c48d |

## Tests

`make -j8` clean. `make test`: every check before run-tests passed; in
run-tests every dosx run passed (156), and all 156 hdpmi runs failed only
because HDPMI16.EXE (.tools/hx) is not in this worktree.

Boot test on installation-05 (tools/remote-test.sh -b): `olecli` (tree,
cardw16 and every OC1 scenario: card1, delete, embed1, embed2, errors,
fromfile, launch, link1, pbdata, sound1, srvclose, write1, each in the
w16:ms, ms:w16 and w16:w16 combinations), `olesvr` (tree and 18
scenarios) and `w16write@wr30o` (w16's Write with w16's OLECLI): all ok
on 6df4107. The two earlier runs failed on the two changes 6df4107 takes
back: card1 (the crossed [StdCloseDocument] freed) and errors (the
[StdExit] window kept).

Not covered by any scenario: OleLockServer/OleUnlockServer (L7), the
malformed-input paths (H1-H3, M5, L1-L4), out-of-memory paths (M4, M9, L2)
and the reentrancy paths (C2, C3).
