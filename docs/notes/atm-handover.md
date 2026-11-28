# Adobe Type Manager 3.0 on w16: handover (2026-11-28)

## Where it stands

- Fixed and tested: ATM 3.0's installer runs on w16's tree and installs the
  same files, ATM.INI and SYSTEM.INI as on Windows 3.1 (boot-test case `atm`,
  part install; `tests/atm.sh`). Causes found on the way: KERNEL's lstrcpy
  used more of its caller's stack than 3.1's (TK84, case `strstack`), USER's
  LB_DIR gave capitals and sorted bracketed items first (US42, case `lbdir`).
- Not working: ATM itself. After the restart ATMSYS.DRV loads as the system
  driver and loads the real SYSTEM.DRV, but ATM never hooks anything, and
  Acrobat Reader 2.0 says "Adobe Type Manager was not found." (part acro
  reports `xfail`; on 3.1 Reader shows HELP_R.PDF).

## What ATMSYS.DRV does at load (observed with `[w16] trace=KERNEL.*`,
`tracefrom=ATMSYS`, `traceret=1`)

GetVersion; GetPrivateProfileString(boot, atm.system.drv); LoadLibrary
(system.drv); GetProcAddress of SYSTEM's exports; ATM.INI [Settings]
LanguageCode; WIN.INI [Intl] sLanguage; FindResource/LoadResource/
LockResource of one of its own RCDATA resources; GetModuleHandle("KERNEL");
GetProcAddress(KERNEL, 45 = LoadModule); AllocSelector(0);
PrestoChangoSelector(LoadModule's code selector, new) (a data alias of
KERNEL's code); FreeSelector. Nothing after: on w16 it writes nothing
(LoadModule's bytes unchanged afterwards, checked with a probe that dumps
w16's own code). Note: with `trace=KERNEL.*` GetProcAddress returns a trace
thunk, so traced runs show ATMSYS reading the thunk, not LoadModule.

Schulman (Undocumented Windows ch. 9): ATM patches KERNEL's LoadModule,
watches for the load of GDI and then patches GDI.

## Tried (none made ATMSYS patch)

w16's LoadModule export given a stub entry (then calling the C LoadModule),
with the boot's loads going through it (winboot.c's boot_load calling the
export with lpParameterBlock -1, which w16 treats as LoadLibrary):
`B8 dgroup 45 55 8B EC 1E 8E D8`, `8C D8 90 45 55 8B EC 1E 8E D8`,
`90 90 90 45 ...` (did not boot: DS), `1E 58 90 45 55 8B EC 1E 8E D8`,
`45 55 8B EC 1E B8 dgroup 8E D8`. Also: 3.1 with Microsoft's KERNEL and
w16's GDI: ATM not found either, so GDI has a check of its own.
The experiment code was not committed (KLOADMODULE in a kpatch.asm, the
export line `LOADMODULE.45=KLOADMODULE` in kernel.lnk).

## Next step (approved: interoperability, learned at run time only)

w16 may carry the minimal bytes ATMSYS is observed to check, with w16's own
code behind them. Never disassemble, hex-dump or read Microsoft's KERNEL or
Adobe's ATMSYS.DRV / ATM16.DLL code (nor ATMSYS's RCDATA, which may hold
code bytes). A plan:

1. Read extent oracle: w16 owns PrestoChangoSelector. When its source is
   the code selector holding LoadModule, give the alias a limit just past
   LoadModule's entry and, in KERNEL's exception 0Dh handler, when DS or ES
   of the faulting context is that alias, raise its limit by one byte and
   restart the instruction (count the raises). The final limit is the
   highest byte read: with a compare that stops at the first difference it
   tells how many leading bytes matched.
2. Fast trials in one session: boot w16's tree without ATMSYS as the system
   driver and let a probe LoadLibrary("ATMSYS.DRV") / FreeLibrary in a loop
   (its LibMain repeats the check), changing LoadModule's entry bytes
   between calls through an alias (w16's own code), one byte position at a
   time, keeping the value that makes the read extent grow. A [w16] setting
   or a KERNEL debug export can report the extent and whether ATMSYS wrote
   (compare the entry before and after).
3. Once ATMSYS writes its patch, see where it jumps and what it expects of
   the code after the patched bytes (the hook later calls the original), and
   make w16's entry carry exactly those bytes, then its code. Route the
   loads ATM must see (the boot's GDI at least) through the export entry.
4. Then the same for GDI (its check runs when GDI loads): its exported
   font/text entries already start `B8 seg 45 55 89 E5 1E 8E D8` (OpenWatcom
   encodes mov bp,sp as 89 E5, MASM as 8B EC).
5. Turn part acro of `tests/atm.sh` into a real check; document the
   interface in ARCHITECTURE.md (what is checked, how observed, what w16
   carries). Stop and report if more than a minimal sequence is needed.

Inputs on installation-05: /mnt/build/w16-atm-in (DISK1.IMG, DISK2.IMG),
/mnt/build/w16-acro-in/acroread (Reader, with FONTS/ holding Reader's own
ATM fonts). An installed drive to boot from: run `tests/atm.sh -o DIR install`
and use DIR/install-w16/work/c with DROPIN_COVER (replace the tree's own
binaries in its W16 directory with the build's first).
