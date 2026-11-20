# USER audit triage

A hand audit of `user/` (by reading only; nothing reproduced) listed 6
High, 8 Medium and 5 Low findings. Each is checked here against the code
and, where the fix changes what a program sees, against Windows 3.1 with
two new probes:

- **US29** (`tests/apps/us29.c`, helper task `us29a.c`, DLL `us29d.c`):
  one section per finding. Run as the shell; the program's file name picks
  the run (`US29.EXE` the main run; `US29DW`, `CR`, `SE`, `SD`, `SS`, `SC`,
  `HM.EXE` the runs Windows 3.1 itself does not survive).
- **US30** (`tests/apps/us30.c`): where 3.1 calls the hooks w16 did not.

The boot-test case `uaudit` runs both on 3.1 and on w16's USER and wants
the OBS lines identical, and the runs 3.1 dies in to finish on w16.
`build/uarun.sh`-style ad-hoc runs on the remote host gave the values
quoted below.

Verdicts: **REAL** (confirmed; fixed unless noted), **FALSE** (the claim
is wrong), **3.1-BEHAVIOUR** (3.1 does the same: kept or matched),
**HARDENING** (only reachable with what 3.1 does not survive either, or
with absurd input; fixed when cheap and invisible to working programs).

## High

| # | Verdict | Evidence | Fix |
|---|---|---|---|
| 1 | REAL | `receive_sent` wrote `s->result` and read `s->done` after the call; a ReplyMessage in the call had let the sender wake and LocalFree the SMSG. | SMSG records have `held` (the receiver is in the call) and `orphan` (the sender left while held): whoever leaves last frees it; neither side touches it after the other may have freed it. ReplyMessage reads the record only while it is held. (`user/msg.c`) |
| 2 | REAL | Nothing completed in-flight SMSGs at a task's end; `send_message` read `q_of(to)` of a freed queue; a receiver replied with `set_wakebit` on a dead sender's freed queue. US29 se1 on 3.1: a task that ends inside a message sent to it gives the sender 0. US29 se (that task also blocked in its own SendMessage, the message nested): **3.1 does not survive it**; w16 now does (sender gets 0, the reply to the dead sender is dropped). | Every SMSG in flight is on one list; `smsg_queue_gone` (at AppExit's start, as Pietrek's FlushSentMessages, and in `destroy_queue`) answers what was sent to the dying queue with 0 and lets go of what it sent (the receiver frees it after its call). The send loop yields to `s->to`, which is cleared when the receiver goes. SetMessageQueue moves both kinds to the new queue. |
| 3 | REAL | US29 tf: on 3.1 a timer passed over by a filtered PeekMessage comes later; on w16 it never came again (`ntimer` zeroed, timers left ready). | `timer_message` recounts the ready timers when none matches and only clears QS_TIMER (so a filtered GetMessage still sleeps); WaitMessage tests the QS_TIMER bit, not the count. |
| 4 | REAL | Both loops restarted from the top after each destroy and found a WF_DESTROYING window again for ever. US29 dw on 3.1: owned-before-owner order `W2 W82 O2 O82` (w16 the same); an owned window that destroys its owner in its WM_DESTROY makes **3.1 recurse** (WM_DESTROY to it again and again until its stack is gone). | `destroy_window` marks the window WF_DESTROYING before its owned windows go, so an inner DestroyWindow of it returns at once; both loops pass over windows already being destroyed; `app_exit` then frees what a task left half destroyed (`destroy_abandoned`: no messages, its task is gone). |
| 5 | REAL | No class was ever removed. US29 cl on 3.1: a program's classes (CS_GLOBALCLASS too) last while another instance runs and go with the last one; a second instance's RegisterClass of the same name fails; the program run again registers them again; a DLL's classes go when it is freed (Pietrek ch. 3: ModuleUnload's PurgeClass). | `purge_classes(module)` at a task's end when its module's usage is 1, and at SignalProc 80h (a DLL freed). A class with live windows stays. `free_class` also gives back a CS_CLASSDC DC. |
| 6 | REAL (cleanup) / FALSE (`call_from` reading `next` after the call) | `call_from` returned after the first matching hook; CallNextHookEx read `next` before calling. The real case: a procedure that unhooks itself and then calls CallNextHookEx with its own handle read a freed block. US29 hk on 3.1: a system-wide hook in a DLL set by a task that ended is still called while the DLL stays loaded; hm: freeing the DLL with a system hook set does not crash 3.1. | Hooks record their installer's queue and their procedure's module. A task's end takes its task-local hooks (Pietrek's UnhookHooks); a module's end (last instance, a fault, or SignalProc 80h) the hooks whose procedure is in it. A hook unhooked while its procedure runs stays in the chain, passed over, and is freed when the call returns. |

## Medium

| # | Verdict | Evidence | Fix |
|---|---|---|---|
| 7 | REAL | US29 cb: a 300-character item selected in a combo box crashed w16 (log ends). 3.1: CBS_DROPDOWNLIST's WM_GETTEXTLENGTH is -1 whatever is selected; WM_GETTEXT copies what fits and returns the text's length, or the buffer size when cut (20 for 19 characters). CBS_DROPDOWN answers through its edit. | `item_text` (LB_GETTEXTLEN, a global block) in `sel_to_edit`, the drop-down list's paint and WM_GETTEXT; WM_GETTEXTLENGTH -1, WM_GETTEXT's result as 3.1's. |
| 8 | REAL | US30 on 3.1 (see "Hooks" below). | WH_CBT (CREATEWND, DESTROYWND, ACTIVATE, MINMAX, SETFOCUS, SYSCOMMAND, MOVESIZE), WH_KEYBOARD, WH_MOUSE, WH_JOURNALRECORD, and WH_MSGFILTER from DialogBox/MessageBox (MSGF_DIALOGBOX), menus (MSGF_MENU) and scroll-bar tracking (MSGF_SCROLLBAR, UNVERIFIED). Not WH_DEBUG (3.1 died with one set), WH_HARDWARE (no such events), CLICKSKIPPED/KEYSKIPPED/QS (not observed). With them, activation and focus moved to 3.1's order: SW_SHOWMAXIMIZED activates; minimising the active window takes the focus away first. |
| 9 | REAL | `valid_wnd` took any block of WND size with a nonzero word at 1Ch. | The class pointer must be a DGROUP block of CLS size with the 'NK' signature (classes with windows are never freed, so no list walk). |
| 10 | REAL | `create_window` used `w` after WM_GETMINMAXINFO, WM_SIZE and the parent's `pw` after any send. US29 cr: **3.1 returns a handle to the dead window when WM_NCCREATE destroys it (`81 2 82`) and then dies**; w16 returns 0 and goes on. | `w` and `pw` looked up again after every message. |
| 11 | REAL | US29 en on 3.1: `4 2 1` (E3 destroyed, E1 brought to the top during the callback for E4) for EnumWindows and EnumTaskWindows; EnumChildWindows depth first, `1 3 g 4`. A list made first, dead windows passed over. | `list_windows` snapshots into a global block; `enum_list` calls back for each window still valid. |
| 12 | REAL | US29 wsp on 3.1: `%.2d` 5 "05", `%.5u` "00042", `%.4x` "00ab", `%.3d` -5 "-005", `%05.3d` "  007" (0 flag ignored with a precision), `%#.4x` "0x001f", `%.0d` 0 "0", `%.3c` "x", `%.2s` "ab". NULL `%s`: 3.1 prints two bytes it finds (60h 10h). | Precision as minimum digits, zero flag off with it; precisions clamped to 64 (UNVERIFIED past that). NULL `%s` prints nothing, as a bad pointer (3.1's bytes are whatever is there; not compared). |
| 13 | REAL (w16 hang) / 3.1 dies | US29 on 3.1: SetParent(desktop, w) **ends 3.1** (sd); SetParent(w, w) returns the old parent, GetParent(w) is then w, and **3.1 hangs** (ss); SetParent(w, its child) is done too (a cycle, sc). w16 hung in the last two. | Refused (0) for the desktop, the window itself and its descendants. |
| 14 | REAL (more than reported) | `free_window_dcs` was never called: a DC not released stayed out for good, and a CS_OWNDC window's DC went to the next window given the same handle. US29 dc on 3.1: after DestroyWindow without ReleaseDC, no cache DC has the pen or mode left. CS_CLASSDC: one DC for the class's windows, its state kept, its origin the asking window's, not from the cache, GetWindowDC a cache DC. CS_PARENTDC: clip box -10 -10 388 269 for a child at 10,10 (the parent's client area), origin the child's. | `free_window` calls `free_window_dcs`, which resets every DC it gives back; CS_CLASSDC (`CLS.hdce`) and CS_PARENTDC in `get_dc`/`setup_dc`. |

## Low

| # | Verdict | Evidence | Fix |
|---|---|---|---|
| 15 | REAL (w16 crash) / 3.1 differs | US29 mq: w16 died after SetMessageQueue with a child window (GetWindowTask read the freed queue). 3.1: returns 1467h, leaves the child window on the freed queue (GetWindowTask another task's, PostMessage to it fails, its timer stops). | Every window of the old queue (children too), its timers and its sent messages move to the new queue. A deliberate difference (3.1 leaves dangling queue handles); the probe logs these as `~` (not compared). |
| 16 | 3.1-BEHAVIOUR (but w16's limit was wrong) | US29 fw on 3.1: titles of up to 79 characters are found, 80 and more never (a 150-character title is not found by itself, nor by its first 127 or 126 characters). w16's 128-byte buffer found up to 127. | The buffer is 80 bytes, as 3.1's limit. Not "compared in full". |
| 17 | REAL | US29 sq on 3.1: 100 key presses and releases (and 100 left clicks) with nobody reading leave 59 downs and 61 ups, the last an up: 120 events, and a down needs two places free. w16 kept 60 and 60 and could drop an up. | NSYSQ 120; a key or button down is queued only with two places free. Whether 3.1 beeps: not observed. |
| 18 | REAL | US29 nc on 3.1: WM_NCCREATE returning 0 gives `81 82` (WM_NCDESTROY only); WM_CREATE -1 gives `81 83 1 2 82`. Also: WM_GETMINMAXINFO (24h) goes to overlapped windows and to thick-frame ones (popup or child), not to other popups; w16 sent it to every popup. | WM_NCCREATE failing: WM_NCDESTROY and the window freed; WM_GETMINMAXINFO's condition as 3.1's. |
| 19 | REAL | `set_text` failing freed the WND but not a class menu just loaded. | The menu is destroyed too; WM_NCCREATE failing frees through `free_window` (menu, text, properties). |

## Hooks (item 8)

US30 on 3.1, task-local hooks, each call recorded:

- **WH_CBT**: HCBT_CREATEWND before the window exists for anyone (also
  for the icon title window when a window is first minimised);
  ShowWindow(SW_SHOWNORMAL) of a new window: HCBT_ACTIVATE (fMouse 0,
  previous active), HCBT_SETFOCUS (new, old); SetFocus: HCBT_SETFOCUS;
  SetActiveWindow: ACTIVATE then SETFOCUS; ShowWindow minimise, restore,
  maximise: HCBT_MINMAX with the SW_ value (6, 9, 3), not for a show that
  changes nothing; WM_SYSCOMMAND: HCBT_SYSCOMMAND with SC_*, then MINMAX;
  minimising the active window: SETFOCUS (0, it), then ACTIVATE of the
  next; MoveWindow: nothing (HCBT_MOVESIZE is for the user's tracking);
  DestroyWindow: HCBT_DESTROYWND for the window and its owned windows, not
  its children. A nonzero return: DESTROYWND keeps the window, ACTIVATE,
  SETFOCUS, MINMAX and SYSCOMMAND do nothing.
- **WH_KEYBOARD**: HC_NOREMOVE (3) for each PM_NOREMOVE look at a key
  message, HC_ACTION when it is removed; wParam the virtual key, lParam
  the key data; nonzero on HC_ACTION throws the message away.
- **WH_MOUSE**: HC_ACTION before each mouse message is removed, wParam the
  message, MOUSEHOOKSTRUCT with the window and hit-test code; nonzero
  throws it away.
- **WH_MSGFILTER**: MSGF_DIALOGBOX (0) for every message of DialogBox's
  and MessageBox's loops, MSGF_MENU (2) in TrackPopupMenu's; none in the
  keyboard move and size loops (SC_MOVE, SC_SIZE ended by Escape), though
  the SDK lists MSGF_MOVE and MSGF_SIZE: w16 calls none there either.
- **WH_JOURNALRECORD** (system-wide): HC_ACTION with WM_KEYDOWN, WM_KEYUP
  and the virtual key in paramL's low byte, as the messages are removed.
- **WH_DEBUG**: 3.1 died at the first PeekMessage with a task-local one
  set: not implemented, not probed.
- A refused HCBT_CREATEWND: 3.1 died (the probe no longer asks); w16's
  CreateWindow returns 0 with nothing sent.

- A filter's, CBT's, keyboard's and mouse's answer is a BOOL: AX alone.
  Program Manager's message filter returns 0 in AX and whatever is in DX;
  CallMsgFilter took the DWORD, so with USER's loops calling it the
  menus and dialogs threw away every key (the full boot test then failed
  runfront, tabs, helpmenus and 14 more; found with `[w16] userlog`).
- WH_MSGFILTER is task-specific however it is installed (3.1 SDK,
  SetWindowsHook: "the only exception is WH_MSGFILTER, which is
  task-specific"; WH_SYSMSGFILTER is the system-wide one); w16 kept a
  task-less one system-wide.

What US30 compares (`OBS` lines identical on 3.1 and w16) covers all of the
above except two differences left (logged as `~`, see "Left").

## Sweep of the files the audit did not read in full

| Where | Verdict | Evidence | Fix |
|---|---|---|---|
| `listbox.c` `find` | REAL | LB_FINDSTRING/LB_SELECTSTRING (and CB_ forms) copied an item's first `len` characters into a 256-byte stack buffer, `len` the search string's length: a longer search string overran the stack. | Prefixes of 256 characters or more go through a global block. |
| `edit.c` `replace_sel` | REAL | The selection's offsets were kept across EN_MAXTEXT; a parent that changed the text there made the move below copy about 64K over the heap; one that destroyed the control left a freed record in use. | Everything worked out again after the notification; the control looked up again. |
| `edit.c` EM_SETTABSTOPS | HARDENING | `n * sizeof(int)` wrapped for counts of 8000h and more. | Refused. |
| `paint.c` `update_window` | REAL | The child loop read the link of a child its WM_PAINT had destroyed. | The next sibling taken first; the walk ends if it is gone or moved. |
| `driver.c` `to_firsts`, `drivers_exit_session` | REAL | A driver closing itself in DRV_EXITAPPLICATION/DRV_EXITSESSION ended the walk of the live list. | The drivers listed first. |
| `button.c` auto radio buttons | REAL | With more than 64 controls before the button in its group, GetNextDlgGroupItem never came back to it: an endless loop of BM_SETCHECK. | At most 1024 steps. |
| `combo.c` drop-down click | REAL | The record was used after the parent destroyed the combo box on CBN_SELCHANGE. | Looked up again after each send. |
| `pos.c` `set_window_pos`, `ShowOwnedPopups`; `defwnd.c` HTSYSMENU, WM_SYSCOMMAND | REAL (low) | A WND used after WM_KILLFOCUS, an activation, ShowWindow or the CBT hook could have destroyed it. | Looked up again. |
| `menu.c` `find_cmd` | HARDENING | A menu inside itself recursed until the stack ran out. | 16 levels at most. |
| `listbox.c` paint after WM_DRAWITEM, LBN_DBLCLK after LBN_SELCHANGE; `pos.c` reads after WM_SHOWWINDOW; `drag.c` after WM_QUERYDROPOBJECT; `listbox.c` a failed string copy | not fixed (low) | Reported by the sweep; each needs the owner to destroy the control inside its own notification, or no memory. | Left (see below). |
| interrupt-time data (`tools/irqcheck.py`) | FALSE | Nothing new: the build's irqcheck passes; the new `is_down` in `input.c` reads no data. | None. |
| other fixed buffers (`button.c`, `defwnd.c`, `menu.c` item text, `mdi.c` frame title, `dlg.c` GetNextDlgGroupItem's 64, `edit.c` password runs, `deskver.c`, `dirlist.c`, `driver.c`) | FALSE | Each copy is bounded by the buffer's size. | None. |

## Left

- **Icon title windows**: w16 destroys a window's icon title when the
  window is restored and makes a new one at the next minimise; 3.1 keeps
  one until the window goes. Visible to WH_CBT (CREATEWND/DESTROYWND for
  it) and to EnumWindows; US30 leaves those calls out of its record.
- **SW_RESTORE/SW_SHOWNORMAL of a visible normal inactive window**: 3.1
  does not activate it (US30 cbt, the refused-MINMAX run), w16 does (an
  old, pixel-tested behaviour of `pos.c`); US30 no longer asks.
- **The activation after DestroyWindow of the active window**: 3.1
  activates the next window before the window goes (its HCBT_ACTIVATE
  names the dying window as the active one, then SETFOCUS from it); w16
  activates afterwards, the focus taken away first. Doing it 3.1's way
  broke Task List's End Task and tiling (boot test taskman), so it is
  left; US30 stops its record at the window's HCBT_DESTROYWND.
- **A refused HCBT_SETFOCUS during an activation**: 3.1 then names the
  activated window as the old focus in the next SETFOCUS, w16 none (US30
  `cbt.deny9`, logged as `~`).
- The low sweep findings marked "not fixed".
- WH_HARDWARE, WH_DEBUG, HCBT_CLICKSKIPPED/KEYSKIPPED/QS: not called.
- `[w16]` keys: none added. Every change either matches 3.1 or only
  changes what a working program cannot see (the user's rulings).

## Tests

- `make test` (installation-05): pass.
- Full boot test on 573d08a: 122 ok, 4 failures. Three are known on
  master 429e079 (`ddeml` conn, `lzexpand` quirks, `cplclick` co-tree).
  The fourth, `toolhelp`, failed in its all-Microsoft `ref286` part (see
  the rerun note in the report). `uaudit` passes: US29 and US30 identical
  to 3.1, and every run 3.1 does not survive finishes on w16.
- On the way: the first full run on d79f4ea had 17 new failures (the
  DWORD filter answer, then the activation before a destroy), found by
  bisecting on `runfront` and `taskman` and fixed in 573d08a.

## Commits (branch worktree-agent-a71dae5a4ea67062b, from master 429e079)

| Commit | What |
|---|---|
| a7a3b3c | probes US29 (US29A, US29D) and US30; boot-test case `uaudit` |
| d35b3dd | items 1-3, 5, 6, 15 (`msg.c`, `timer.c`, `misc.c`, `class.c`, `hook.c`) |
| 753047e | items 4, 7, 9-14, 16-19 (`win.c`, `dc.c`, `combo.c`, `str.c`, `input.c`, `listbox.c`) |
| d79f4ea | item 8 (hook calls) and the sweep's fixes |
| 310a151 | US30: what 3.1 does not survive left out |
| 573d08a | hook answers as BOOLs, WH_MSGFILTER task-specific, no activation before a destroy, CS_PARENTDC for applications' classes |
