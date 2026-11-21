# DDEML handover (w16's DDEML.DLL)

Task: w16's own DDEML.DLL, a drop-in for 3.1's SYSTEM\DDEML.DLL (38
exports, module DDEML), probed black-box on 3.1; scenario harness
comparing Microsoft's and w16's DDEML in all client/server combinations;
a DDEML client driving Program Manager; boot cases.

Rules: all DOSBox work on installation-05 (`tools/remote-run.sh -n
ddeml-agent ...` or ssh to /mnt/build/w16-runs/ddeml-agent), never local;
no subagents; commit in pieces ("Co-Authored-By: Claude Opus 5.5
<noreply@anthropic.com>"), no push.

## Status

- [x] probe DM.EXE (tests/ddeml/dm.c) + tests/ddeml/dmrun.sh + tests/ddemlscen.sh
  (commits d19c0c2, fc59623). Run: `tools/remote-run.sh -n ddeml-agent -g
  build/dmscen tests/ddemlscen.sh -o build/dmscen [NAME...]` (-n: ms only).
  Scripts in tests/ddeml/scripts; NAME+x = other copies on the srv library
  (DMX.EXE/DDEMX.DLL renamed copies). x*.dm = raw-value explorations, not
  compared.
- [x] observations recorded: ARCHITECTURE.md §18 (with what still differs, 18.4/18.5)
- [x] shell/ddeml/ first implementation (fc59623): inst.c hsz.c data.c conv.c
  mon.c entry.asm ddeml.rc ddeml.lnk; builds, exports match (+ linker's
  unnamed 40-42). NOT yet run: iterate on tests/ddemlscen.sh diffs.
- [x] tests/ddemlscen.sh (combos), progman scenario, boot case ddeml (tree286, tree386, scenarios)
- [x] README.TXT; [x] make test: pass; full boot test 114 ok, 3 FAIL: ddeml (parts conn, conn+x, monitor, monitor+x, progman, tree286, tree386 - the known gaps; tree parts differ only in the "Progman" atom), holdbtn (scroll-arrow repeat timing) and taskman@tree (21-dosesc.png 974 px) - neither uses DDEML, not investigated

## Notes

- PROBE BUG FIXED: OpenWatcom's ddeml.h declares DdeAccessData with a near
  result; the probe's monitor/access output before fc59623 was garbage
  (read its own DS). The probe now imports DDEACCESSDATAF = DDEML.17.
- Status mask on callback results: r & 0xC00F (80F7->8007, 4155->4005).

### Observed on 3.1 (explore1, raw values)
- idInst = MAKELONG(near ptr/handle of instance data, counter) e.g. 000202C0,
  00030300, 00040340 (counter across tasks: 2,3,4). DMGFrame/DMGClass
  window words hold LOWORD(id).
- HSZ = global atom in LOWORD, HIWORD 0. Instance-specific service name
  hsz = MAKELONG(atom of base name, 14C8 = ?).
- Per DdeInitialize: top-level DMGFrame (WS_POPUP|WS_DISABLED, 4 extra
  bytes: id-low, 0) and DMGClass (style 04C00000, 6 extra: id-low,0,0).
  Client conv: DMGClientClass child of DMGClass (WS_CHILD, 6 extra:
  far ptr to conv data, seq). Server conv: DMGServerClass child of a
  DMGHoldingClass (8 extra) child of the server's DMGClass.
- HCONV = MAKELONG(conv hwnd?, seq word in the window's 3rd word).
- HDDEDATA made by app = far ptr into instance block; received in
  callbacks = MAKELONG(?, selector of the DDE block). Access ptr = DDE
  block + 4 (past flags,cf). Size = GlobalSize - 4 (EXECUTE: GlobalSize).
- lpuResult: only LOWORD written (POKE/EXEC: 8000), untouched for REQUEST.
- ADVISE options flags 2000 (!) for a plain advstart; ADVSTOP cb d2 = hszItem.
- Request DATA flags 3000; advise DATA 2000. Async id = 0001005C.
- DdeDisconnect: posts TERMINATE, returns at once.
- REGISTER/UNREGISTER callbacks: own instance first, then others, inside
  DdeNameService.

### Observed (dmobs run: strings data conn xact advise flags; logs were in build/dmobs)
- One global counter (starts so that the first DdeInitialize gets 2) gives
  HIWORD(idInst) and the seq word of every client/server/holding window;
  HCONV = MAKELONG(conv hwnd, seq). Async xact id = MAKELONG(near addr,
  per-conversation counter from 1).
- Instance name: hsz MAKELONG(base atom, DMGFrame hwnd), string
  "Svc:(143c)" (lower-case hex of the frame hwnd).
- Strings: id 0 -> atom created anyway, err DLL_NOT_INITIALIZED; stale id
  -> 0, INVALIDPARAMETER (DdeGetLastError(stale) gives it too). cp 0,
  1004, GetKBCodePage ok; 850/1200 INVALIDPARAMETER. "" -> 0, no error.
  Keep returns the atom (LOWORD). Free/Keep(0) -> 1. Uninit deletes the
  atoms the instance still holds (per-instance counts). QueryString:
  max 0 -> 0 untouched; max n -> n-1 chars; NULL buf -> length; hsz 0 -> 0, "".
  Cmp: by value, 0 smallest.
- Data: block GlobalAlloc(cb+4) NOT zeroed; size = GlobalSize-4. GetData
  off>size -> 0 INVALIDPARAMETER; returns min(max, size-off). AddData past
  end: GlobalReAlloc (gap zero), same handle. afCmd other than 0/1 ->
  INVALIDPARAMETER. Double DdeFreeDataHandle CRASHES 3.1 (never script it).
- Poke with a data handle: the block as is (its own fmt word wins over uFmt,
  item not checked), flags |= 2000. EXECUTE with hszItem != 0: block with
  header sent as command. EXECUTE NULL data: uninitialised block.
- Server cb return masked: 8012 -> ACK 8002, 0034 -> 0004 (keeps low 4
  bits? check more). lpuResult LOWORD = ACK status.
- Request returning 0 -> ACK 0000 -> NOTPROCESSED. busy -> BUSY.
- CONVINFO after a sync transaction: item/fmt/type garbage (freed record):
  never compare `info` after a sync xact. Fresh client conv: fmt CF_TEXT,
  type 1062; server: fmt 0 type 0, status 0001 (no ISLOCAL), partner 0.
- Block: sync request timed out -> DATAACKTIMEOUT; server status |8.
  enable one: one queued cb inside DdeEnableCallback. EnableCallback
  inside a callback works (no REENTRANCY). Sync xact inside XACT_COMPLETE
  works. abandon c 0 / abandon 0 0 -> 1.
- SetUserHandle(id 0) -> 1 but hUser stays 0 (?); unknown id ->
  UNFOUND_QUEUE_ID.
- XACT_COMPLETE: d1 = id, d2 = ack status (0 for request); no-data
  completions get a handle that is not 1 (size 0).
- Advise: ADVISE options always |2000 (plain 2000, nodata 6000, ackreq
  A000). Links LIFO per conv; DdePostAdvise walks server convs newest
  first, links newest first; ADVREQ d1 = remaining links with same
  topic/item/fmt; nodata links: DATA hData 0 without ADVREQ; client ACKs
  a NULL DATA always. Ackreq link with unacked DATA: no ADVREQ, later the
  ACK triggers ADVREQ d1=FFFF (once). ADVSTOP unknown -> NACK w/o cb.
  ADVSTOP cb d2 = hszItem.
- Local partner (same library): client DDEML fails REQUEST/POKE/EXEC/
  ADVSTART at once (no message, return 0, NO_ERROR) when the server has
  the matching CBF_FAIL_ flag. Find out how "local" is decided (hInstance?)
- Connect: single DdeConnect: later servers skip an INITIATE once the
  client has its conversation; second ACK during the same INITIATE ->
  TERMINATE sent (SendMessage) at once, server gets DISCONNECT then
  CONNECT_CONFIRM conv=0. ConnectList: extra ACKs move into new client
  windows; old list given: duplicates (same svc/topic) of the old list's
  convs are terminated, old convs kept, old list handle dead.
- REGISTER/UNREGISTER go to every instance in z-order of DMGClass windows
  (newest first); uninit excludes itself. Uninit: UNREGISTER, then
  TERMINATE every conv (creation order).
- DdeNameService(0, FILTERON/OFF) returns 0. init with any afCmd works
  (no validation seen); reinit too. CLIENTONLY: ns reg DLL_USAGE.

### Status 2026-11-16 (late)
- After fixes, ddemlscen: strings, data, xact, advise, flags, raw equal in
  all combinations. Open: conn (ConnectList with several servers: 3.1's
  client, on a 2nd ACK from a NON-local server, TERMINATEs it at once and
  re-INITIATEs that server from a new client window; a P TERMINATE goes
  between its own client windows; local servers: ACKs kept, new windows),
  monitor (event sequence: 3.1 sends 2 MF_HSZ_INFO per string create etc.,
  internal atom traffic), progman (3.1's DDEML at DdeInitialize seems to
  create global atoms for the top-level windows' classes (InitEnum?):
  "Progman" atom exists before the probe's create in the ms run; w16
  shows "PROGMAN"; also the cause of atom values being 14 lower in w16),
  uninit (fixed?: request XACT_COMPLETE d2 0).
- Boot case ddeml added (tests/boottest.sh), README.TXT updated.
- ARCHITECTURE.md chapter 18 drafted in the scratchpad (arch.md): append
  it to ARCHITECTURE.md (after §17) with the final results.
- Still to do: make test + full boot test (tools/remote-test.sh -n
  ddeml-agent), report.

### Next steps (if continuing)
- conn: DdeConnectList orders (see ARCHITECTURE.md 18.4) - compare
  build/dmscen/conn*/os.diff after `tests/ddemlscen.sh conn conn+x`.
- monitor: mirror 3.1's MF_HSZ_INFO traffic (xmon.dm with raw values now
  that the probe reads the blocks correctly).
- progman: find which global atoms 3.1's DdeInitialize makes (atomq for
  window class names before/after init).

### Task 2 (2026-11-17): no masking of atoms and frame handles
- Found (xatoms.dm): first DdeInitialize adds atoms "Progman", "Who",
  "Made me?" and registers six classes (lazily, before the atoms).
  w16 does the same (inst.c); dmnorm no longer masks ":(hwnd)", the probe
  prints atom values again. progman, tree parts fixed by this.
- Remaining masking: data block tails after the first NUL only.
- Still differing: conn (lists with several servers), monitor (event
  stream) - not attempted in task 2.
- Also fixed in task 2: "Svc:(%04x)" four digits; EC_ENABLEONE re-disables;
  EXECUTE without data still gets a block; CONVINFO of a pending xact gives
  XST_*SENT; WM_TIMER on server windows (a server's own sync xact).
  xact.dm renamed transact.dm (the x prefix had kept it out of every run).
- Boot case ddeml now: all parts ok except conn, conn+x, monitor, monitor+x.

### Task 3 (2026-11-19): conn and monitor equal (branch from master 429e079)
Run name on installation-05: `-n w16-ddeml2`. Explorations (raw values,
not compared): xconn, xconn2 (lists), xmon, xmon2 (monitor events, which
INITIATE sends a monitor reports), xstr (MH_CLEANUP order), xun1-xun5
(DdeUninitialize order, queue places). `tests/ddeml/xrun.sh OUT "COMBOS"
NAME...` runs scripts in combinations and diffs each against ms-ms.
Found (all in ARCHITECTURE.md 18.2; code in conv.c / mon.c / hsz.c):
- INITIATE goes to each top-level window (EnumWindows/InitEnum), not a
  broadcast; DdeConnect stops at the window that answered.
- A list is a DMGHoldingClass child of DMGClass (words id,0,seq,0); its
  conversations are its children; order = z-order (creation); old list:
  dup terminations, then SetParent of the old windows (order reversed,
  on top). No answer: returns 0, no error, window left. DdeDisconnectList
  leaves the window (word 6 = 2000), destroys it when called on an empty
  one.
- Extra answer in a list from a non-local server: TERMINATE (sent) and a
  new client window INITIATEs that service/topic to the same top window;
  CONNECT_CONFIRM's hconv = MAKELONG(hwnd, GetWindowWord(hwnd, 4)) read
  after the ACK (the new client window reuses the server's handle).
- Self conversations: ST_ISSELF (0115).
- DdeUninitialize: TERMINATE in window order (holding children inline),
  yield first, take own messages one at a time (yield after each one a
  client window gets); TERMINATE answers are posted before
  XTYP_DISCONNECT and retried (Yield) while the target queue (8) is full;
  a server window taking the answer to its own TERMINATE leaves one
  invisible message in the queue (w16: WM_DMG_NOP to DMGClass).
- Monitor: DMGMonitorClass window (the sixth class); event blocks without
  header, size rounded to 32, one block per monitor; HSZ cb = 18+len;
  internal atom actions 5/6/7 (where: 18.2); CONV/LINK name both sides;
  INITIATE sends to DMGClass windows reported after handling (rules in
  18.2); monitors' own callbacks reported; NOTPROCESSED not reported;
  per-instance string entries (append, swap-remove, cleanup from 0).
- DDE blocks are GMEM_FIXED on 3.1 (selector handles).
- Probe: monitor lines name handles (lParam handles h1.. by first sight,
  CB data handles new names, dwData1 "cc"); `iwatch on`.
Not done: bitmap/metafile data, 386 Enhanced mode, WfW 3.11.

### Task 4 (2026-11-20): the termination dialog
3.1 shows TERMDIALOG (system-modal) when a partner has not answered
DdeUninitialize's TERMINATE after 3 s; Retry waits 30 s more, Continue
for ever, Close gives up (returns 1, conversations freed); no TERMINATE
resent; ExitWindows never brings it. Probe: `rnoterm N` (raw window
leaves N TERMINATEs unanswered), `ticker` (gaps between 100 ms ticks:
when a system-modal box stopped the task), `watch`, `exitwin`;
tests/ddeml/dmshot.sh runs a script on Xvfb with tests/inputrun.sh input
(shots, keys); scripts xterm.dm (+ xterm.in Close, xtermr.in Retry then
Close), xterm3.dm + xtermc.in (Continue). w16 (inst.c) waits 3000 ms,
then DialogBox(TERMDIALOG); Retry 30000 ms. Boot-case part ddeml@term
(termcmp.py): times within 300 ms, return 1, picture equal but for the
rebranded words.
Full boot test (97389e8 on master 2fe5dbd, installation-05): ddeml ok in
every part (term included); failing only known ones: lzexpand@quirks,
trails@timed, taskman@tree, cplclick@co-tree, soundtime.
