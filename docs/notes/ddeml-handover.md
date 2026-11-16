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

- [ ] probe DM.EXE (tests/ddeml/dm.c) + harness tests/ddeml/dmrun.sh
- [ ] observations recorded (ARCHITECTURE.md new chapter "DDEML")
- [ ] shell/ddeml/ implementation
- [ ] tests/ddemlscen.sh (combos), progman scenario, boot cases
- [ ] README.TXT, make test, full boot test

## Notes

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
