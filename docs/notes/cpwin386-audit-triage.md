# CPWIN386.CPL audit triage

An outside, read-only audit of w16's 386 Enhanced applet
(`shell/cpl/win386/`: cpwin386.c, cpwin386.h, cpwin386.rc, cpwin386.lnk;
not in the repository) listed 2 High, 5 Medium and 4 Low findings (H1-H2,
M1-M5, L1-L4 here, in its order). Each one was checked by reading the
current code, the shared helpers (`shell/cpl/cplutil.c`, `arrow.c`) and
what ARCHITECTURE.md records of 3.1's applet. No emulator was run for
this triage.

Verdicts: **REAL** (confirmed; fixed), **FALSE** (the claim is wrong),
**3.1-BEHAVIOUR** (3.1 does the same: kept), **HARDENING** (only with
impossible or malformed input, e.g. a hand-edited SYSTEM.INI; fixed when
cheap and invisible to working use), **UNSURE** (needs a run of 3.1;
the code is left alone and the probe is described). Where 3.1 itself
dies, w16 may live.

Status: every finding has a verdict; H2 (size check), L1 and L2 wait
for a 3.1 probe (2026-11-26).

## High

| ID | Verdict | Evidence | Action | Commit |
|---|---|---|---|---|
| H1 | HARDENING | avail_kb: `char t[160]`, an empty PagingFile gets `cpl_windir(t, 160)` then `lstrcat(t, "WIN386.SWP")` unchecked. cpl_windir can fill 159 characters (158 plus the backslash), so a Windows directory over 149 characters would overflow by up to 10 bytes. A DOS path is at most about 67 characters, so it cannot happen. | The directory read into its own buffer and joined with cpl_join (bounded). | c674a64 |
| H2 | 3.1-BEHAVIOUR (the prompt); UNSURE (the size check) | Change>> sets swap_changed, and OK then always asks "Are you sure...?": ARCHITECTURE.md records the same from 3.1, and the envm/envmw scenarios match 3.1 byte for byte. Kept. On OK the New Size (get_num, cpl_atol) is written as typed, or 0 when it is empty, 0 or not a number, with no comparison against Space Available or the recommended size (MaxPagingFileSize for Temporary, PermSwapSizeK for Permanent). What 3.1 does with such a size was never observed (only None and a temporary size were; a permanent file was refused on DOSBox's drive). Nothing crashes, and w16's WIN386 only stores the keys. | None yet. Probe on 3.1: Change>>, Temporary (and Permanent where the drive allows it), New Size empty, 0, negative, letters, and above Space Available, then OK; record any message box (text, buttons, where focus goes) and SYSTEM.INI. Copy 3.1's message and limits. | - |

## Medium

| ID | Verdict | Evidence | Action | Commit |
|---|---|---|---|---|
| M1 | HARDENING | get_num returns a long; keep_port and init_enh cast it to int, so 32768 gives -32768 (shown and written as Always Warn), 40000 gives -25536, 65536 gives 0 (Never Warn). ok_enh never checks IDC_EN_IDLESEC; only spin() bounds it (1..1000). The field (0x50830000, 18 dialog units, no ES_AUTOHSCROLL) takes about 3 digits on 3.1, so only a hand-edited SYSTEM.INI, or w16's USER edit (which does not refuse characters past the width of a field without ES_AUTOHSCROLL), reaches it. What 3.1 does with such an INI value is unknown. | Values held to 32767 before the cast (keep_port, init_enh). The USER edit control's width limit is a separate USER item. | c674a64 |
| M2 | HARDENING | free_kb put `drive - 'A' + 1` in DL unchecked. Only '@' gives 0 (the current drive: its free space shown for a drive '@'); other non-letters wrap to values DOS rejects (-1, correct). Needs a malformed PagingDrive, PagingFile or PermSwapDOSDrive. Also found: read_swap worked out the temporary file's recommended size before upper-casing the letter, so PagingDrive=c gave drive 35, -1, and a recommended size of 0. | free_kb upper-cases a lowercase letter and returns -1 for anything outside A-Z; read_swap upper-cases first, then computes the size. | c674a64 |
| M3 | HARDENING | recommended(): cpl_atol accepts '-', so MaxPagingFileSize=-100 became the recommended size, shown (Recommended Maximum Size, New Size) and possibly written back. Only a hand-edited SYSTEM.INI; ARCHITECTURE.md notes w16's recommended size is its own calculation (3.1 asks its WIN386), so there is nothing of 3.1 to match. | A negative maximum ignored (`max >= 0`). | c674a64 |
| M4 | HARDENING | WM_DRAWITEM left `t[64]` uninitialised if CB_GETLBTEXT failed. Only IDC_VM_DRIVE is owner-drawn (0x58210313, CBS_HASSTRINGS); itemID is a real index or -1 (handled), and its items are at most about 16 characters. Only a forged WM_DRAWITEM reaches it. | CB_ERR gives an empty name. | c674a64 |
| M5 | FALSE | WEP calls arrow_unregister, which unregisters only when `registered` (arrow.c), set from cpl_libmain's RegisterClass; cpl_libmain sets g_hinst first. If LibMain never ran both are 0 (BSS), and UnregisterClass(name, NULL) only fails in w16's USER. w16's loader always passes WEP_FREE_DLL. MAIN.CPL does the same (MAIN.CPL triage L5). | None. | - |

## Low

| ID | Verdict | Evidence | Action | Commit |
|---|---|---|---|---|
| L1 | UNSURE | With no port in BIOS 40:0-40:7, nports is 0: the list is empty, show_port shows port_val[0] (0: Never Warn, idle 2) and the radio buttons and seconds stay enabled. Nothing is written (the save loop runs to nports) and nothing is out of bounds. ARCHITECTURE.md records only DOSBox's Com1 and Com2. | None yet. Probe on 3.1 with every serial port cleared from 40:0 (e.g. DOSBox serial1..4=disabled): open 386 Enhanced, snapshot; note whether the Device Contention buttons and seconds are disabled and which button is checked. Then disable them (and skip show_port) if 3.1 does. | - |
| L2 | UNSURE | show_new never enables, disables or hides anything: with None chosen the drive, size and New Size stay enabled and New Size shows the recommended size (the header's UNVERIFIED note on what None and Permanent show). OK with None writes Paging=0 and removes the size keys, so the value is unused. The envm probe's V2.BMP (taken after choosing None) is not in the repository. | None yet. Re-run tests/scripts/cpl/envm.txt on 3.1 and compare V2.BMP; if 3.1 greys or hides New Size (or the drive or figures) for None, do so in show_new for type 2 and undo it for 0 and 1. | - |
| L3 | HARDENING | CPL_INQUIRE wrote through l2 unchecked. Every host passes a CPLINFO (control.c passes a local's address); no applet in w16 checks, and 3.1's would fault the same way. | NULL ignored. | c674a64 |
| L4 | REAL (comment only) | `port_val[4]; /* -1 never, 0 always, n idle */`, but show_port, keep_port, init_enh, the save loop and the file header all use -1 = Always Warn, 0 = Never Warn. The code agrees with itself; nothing observable. | Comment corrected. | c674a64 |

## Tests

`make -j8` clean; `make test`: every check before run-tests passed; in
run-tests every dosx run passed (157), and all 156 hdpmi runs failed only
because HDPMI16.EXE (.tools/hx) is not in this worktree. No boot test or
screenshot: the changes are invisible to working use.
