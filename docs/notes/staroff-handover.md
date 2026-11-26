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

### Bug 3: the TL312MW fault: fixed (branch staroff-mdi2)

Cause: StarView makes its MDI client with MDIS_ALLCHILDSTYLES (client
style 56000001h) and each document window with WM_MDICREATE, style
42CF0000h (no WS_VISIBLE), CW_USEDEFAULT, while the client is still
0 x 0 (logged with a temporary `[w16] userlog` line in mdi_create). On
3.1 such a child is made hidden and inactive; StarView makes its
controls, then shows it. w16 ignored MDIS_ALLCHILDSTYLES, showed and
activated the child at once, and StarView's own message 046Eh, sent
through WM_MDIGETACTIVE's answer before the controls existed, faulted.

TK75 (tests/apps/tk75.c, boot-test case `mdistyle`, ARCHITECTURE.md
next to US18) probes WM_MDICREATE with and without MDIS_ALLCHILDSTYLES
(hidden, visible, maximised, minimised, placed), hidden children shown
later, ShowWindow and SetWindowPos of plain children, the focus, and the
minimum sizes. w16 now follows it (user/mdi.c, pos.c, win.c): the
style rules; maximise/minimise hidden, show, then SetWindowPos without
SWP_NOACTIVATE whose WM_CHILDACTIVATE activates through
DefMDIChildProc; WM_CHILDACTIVATE from every SetWindowPos of a child
without SWP_NOACTIVATE, not from ShowWindow's show/hide; WM_SETVISIBLE
after WM_SHOWWINDOW; the focus to the child in its activation unless the
client has it (DefFrameProc's WM_SETFOCUS now passes the focus on only
to a non-iconic active child, and a child minimised without activation
gives its focus to its top-level window, so that Program Manager's
active group still takes it); a destroyed child's focus to its parent; WM_GETMINMAXINFO
and the tracking sizes in SetWindowPos and CreateWindow; a maximised
plain child covers the screen; child icons a spacing above the bottom.
The case compares without WM_NCPAINT/WM_GETTEXT/WM_ERASEBKGND (3.1
paints while it shows a window; w16 later).

Now (tree, `WIN /3`, from WIN.INI run=, 90 s, against 3.1 with the same
install): StarWriter, StarCalc, StarDraw, StarImage, StarChart and the
StarOffice Manager all start, no fault. Differences seen on screen (not
yet looked into):
- StarWriter and StarCalc: the floating styles window ("Paragraph
  Styles", "Cell Styles") 3.1 shows at start is missing (StarDraw's
  "Graphic Objects Styles" does show);
- StarChart: the chart itself is not drawn (white area);
- disabled toolbar buttons are plain gray boxes (3.1: grayed icons);
- the document area outside the page is black (3.1: a gray dither),
  and StarWriter's ruler is drawn garbled;
- StarWriter/StarCalc/StarDraw: the style tree pane on the left starts
  about 20 lines lower than on 3.1.
Screens: installation-05 /mnt/build/w16-staroff/so2/out/{w,r}-PROG
(w16, 3.1), sw1 and r31sw for StarWriter; scripts run1.sh, run31.sh
there (DROPIN_FIXED=1 SECS=90; `-i WIN.INI:windows:run=C:\OFFICE31\X.EXE`).

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

### CD drive: checked (branch staroff-mdi2)

Runs: installation-05 /mnt/build/w16-staroff/so2/cdrun.sh ref|tree NAME
PROG SCRIPT (tests/inputrun.sh, DROPIN_CDROM; scripts fm.txt, dlg.txt,
dos2.txt beside it; results in so2/out/cd-NAME-ref|tree).
- File Manager (Ctrl+D, Ctrl+*): the tree and file list of D: as 3.1's.
  3.1 shows the CD-ROM picture and "D: CD-ROM" in the drive bar; w16's
  showed the network picture and the volume label "[960801_1625]".
  Fixed: a remote drive MSCDEX has (INT 2Fh 150Bh) is a CD-ROM drive
  (shell/winfile/drives.c, IDS_DRIVECDROM).
- Notepad's File > Open on D:: the folder list as 3.1's; 3.1 lists the
  drive as "d:", w16's COMMDLG added the label. Fixed
  (shell/commdlg/file.c).
- Command Prompt (WIN.INI run=COMMAND.COM): `dir d:\`, `dir
  d:\english`, `d:`, `cd \english` into C:\DIRD.TXT: identical on 3.1
  and the tree.
Left: w16's own pictures differ from Microsoft's (the CD-ROM drive
picture in both, yellow folders where 3.1's look white in these runs;
not looked into).
