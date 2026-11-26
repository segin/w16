# StarOffice 3.1 on w16: handover

User reports (2026-11-25): StarOffice 3.1 (OEM English, CD image mounted
as D: in plain DOSBox 0.74-3, `memsize=32`, `cputype=386`, `cycles=max`,
w16's tree booted with `WIN /3`):

1. its Setup did not make its Program Manager group (DDE with PROGMAN);
2. StarWriter said "Error initializing object-system.";
3. then w16 ended with exception 0Dh in TL312MW:7 (also StarCalc in
   TL312MW:3, StarChart in TL312MW:7);
4. an OS/2 program from the CD crashed the session.

StarOffice and Microsoft's OLE 2 are not w16's: never commit any of
them, treat them as black boxes (run them, log their calls with w16's
tracing; no disassembly).

## Done (commits a9283e5, 844c372, 390fa53)

- **DLLs from the program's directory** (kernel/loader.c, `prog_dir`).
  Started from WIN.INI `run=` (current directory C:\W16), StarWriter
  said "Cannot find SB312MW.DLL". TK69 on 3.1: a program's imports are
  looked for in the current, Windows and system directories and then the
  program's own directory, not the caller's; w16 used the caller's.
  (3.1 quirk not copied: from C:\TK69C it also missed C:\TK69X; noted in
  tk69.c and loader.c.)
- **Bug 2, "Error initializing object-system."**: Microsoft's COMPOBJ
  makes its task allocator with `LocalInit(sel, 22h, 1000h)` on a
  1000h-byte block; w16's LocalInit refused an end equal to the block's
  size, CoInitialize returned E_OUTOFMEMORY (80000002). Found with
  `[w16] trace=` of COMPOBJ's KERNEL imports and `tracefrom=COMPOBJ`.
  Fixed in kernel/local.c; TK66I's new LocalInit cases carry 3.1's values.
- **SHARE**: Microsoft's WIN386 answers INT 2Fh 1000h with AL FFh in the
  System VM (no SHARE in DOSBox); w16's WIN386 now does too
  (win386/vmm/dpmi.c). STORAGE.DLL's StgCreateDocfile still says
  STG_E_SHAREREQUIRED (80030106) on 3.1 and w16 alike in DOSBox: equal.
- **Bug 4 (part)**: NE files for other systems: LoadModule/WinExec give
  3.1's 12 (OS/2), 13, 14 (TK69 "foreign"); before, w16 loaded them as
  Windows modules. LX/LE files give 11 from LoadModule as on 3.1.
- **CD drive**: GetDriveType of the MSCDEX drive is DRIVE_REMOTE (4) on
  3.1 and w16 alike (TK70's first line).
- Tests: TK69 + boot-test case `kexedir`; TK70 + `tests/staroff.sh` +
  boot-test case `staroff` ($W16_STAROFF_ISO; on installation-05 the
  image is /mnt/build/w16-staroff/STAROFF.ISO); `tests/dropin.sh`
  DROPIN_CDROM, DROPIN_COVER.
- Debugging aids: fault reports print the faulting stack and BP chain;
  `[w16] traceret=1` records traced calls' results; `[w16] profilelog=1`.

## Still open

### Bug 3: the TL312MW fault (reproduces on the tree): w16's USER

Fixed on the way (commit 390fa53): w16's KERNEL could not grow a local
heap in a wired segment; Microsoft's USER wires its data segment, so
under w16's KERNEL USER's heap stayed at 8K ("System resources
exhausted" after ~60 windows). With that, StarWriter runs fully on 3.1
with w16's KRNL386, and on w16's tree with Microsoft's USER.EXE swapped
in (GDI differences show: a black area, toolbar bitmaps missing). With
w16's own USER it still faults: the remaining cause is w16's USER.

Reproduce on installation-05: the installed copy is
/mnt/build/w16-staroff/cover-w16 (C:\OFFICE31 and what Setup put in
C:\W16; the user's install, never commit), then

    DROPIN_MEMSIZE=32 DROPIN_CDROM=/mnt/build/w16-staroff/STAROFF.ISO \
    DROPIN_COVER=/mnt/build/w16-staroff/cover-w16 DROPIN_FIXED=1 \
    tests/dropin.sh -T build/w16 -3 -c 'WIN /3' -t 70 -s OUT.png -o DIR \
      -i 'WIN.INI:windows:run=C:\OFFICE31\SWRITER3.EXE' -i w16:watchdog=0

StarWriter shows its window, then `exception 000D at xxxx:0EF8
(TL312MW:7)`, err 0200 (the user: 01D8, 2600, F000). The error codes
have TI=0: the value loaded into a segment register is a number, not a
selector (the report's "ar" and module are err|7's, unrelated). The
fault report's stack: TL312MW:7 was called with ES:BX = a far pointer
into a 4K block, from SV312MW:35 inside a window procedure (USER:2's
dispatch). Tracing SV312MW's GetWindowLong (`trace=USER.135
tracefrom=SV312MW traceret=1`): the window's object is
GetWindowLong(hwnd, 4) = 2C07:0A04 (SI=DI=0A04 at the fault), and the
messages handled just before were 0046h, 0047h, 001Ch, 0086h, 0006h,
0007h, 0222h, then 046Eh (WM_USER+6Eh, StarView's own) during which it
faults. Suspect the order or timing of a posted private message (its
lParam an event object) against w16's USER: delivered when 3.1 would
not (before or after the object it names is valid). Next: trace USER's
PostMessage/PeekMessage/GetMessage (USER.110, 109, 108) from SV312MW
with traceret on the tree and on the tree with Microsoft's USER, and
compare the sequences around 046Eh.

### Bug 1: Setup's Program Manager group

Not reproduced yet. Setup is C:\OFFICE31\SETUP.EXE on the installed
copy (imports DDEML, SHELL, TOOLHELP, VER, TL312MW, SV312MW) and
D:\ENGLISH\PROD_W31\SETUP.EXE on the CD. Its SETUP.LOG (in the user's
install) shows no group step. Next: run it on 3.1 and the tree with
tests/inputrun.sh (DROPIN_CDROM; answer its dialogs with keys), trace
DDEML/USER DDE calls (trace=DDEML.x with traceret), compare with 3.1's
groups (tools/mkgrp.py / tests/grpref.py read .GRP files). Note SVT312MW
imports DDEML: Setup likely talks to PROGMAN through DDEML, so
tests/ddemlscen.sh and tests/ddescenario.sh are the places to add a case.

### Bug 4: the OS/2 programs on the CD

D:\ENGLISH\PROD_OS2\SETUP.EXE and D:\CSERVE.OS2\SETUP.EXE are LX files.
From WIN.INI run= on 3.1 nothing is seen (WINOA386 runs the stub, the
window closes); on the tree a minimised "SETUP" DOS box stays, and the
stub's "This program must be run under OS/2." went to WIN's redirected
standard output. The tree has no _DEFAULT.PIF (3.1's has): compare with
3.1 without _DEFAULT.PIF before changing anything; then try Program
Manager File > Run and File Manager double-click (what the user did) on
both, and CSERVE.OS2's 330K file.

### CD drive, not yet checked

File Manager's drive icon and tree for D:, the common dialogs' drive
list, a DOS box's access to D: (`dir d:` in Command Prompt) against 3.1.
