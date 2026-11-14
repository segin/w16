#!/bin/sh
# Boot regression: boot real Windows 3.1 (the reference install) on w16's
# parts and compare each screen with the same boot on Microsoft's, pixel
# for pixel. `make test` does not boot Windows; run this after every
# merge (`make boot-test`).
#
# Cases (each a headless DOSBox boot through tests/dropin.sh, Program
# Manager as the shell, the screen taken after SECS seconds):
#   kernel     w16's KRNL286                       must be 0 pixels off
#   dosx       w16's KRNL286 on w16's DOSX         must be 0
#   odstring   TK56 as the shell calls OutputDebugString: with dropin.sh's
#              stock DebugToAux=1 the screen against 3.1's (which shows the
#              text: handle 3 is DOSBox's console), must be 0; with w16's
#              default DebugToAux=0 against TK56's screen without the calls
#              (the text never on the screen), must be 0
#   lastexit   the shell (TK55) ends at once: WIN's ERRORLEVEL 0 and text
#              mode 3 afterwards, as 3.1's KERNEL ends Windows through
#              USER's ExitWindows when the last task ends (ref and w16)
#   k286       on an emulated 80286 (DOSBox-X, -p 286): w16's KRNL286 on
#              Microsoft's DOSX and on w16's, against Microsoft's KRNL286
#              there                                must be 0
#   wincom     w16's WIN.COM + KRNL286             must be 0
#   allw16     w16's WIN.COM + DOSX + KRNL286      must be 0
#   user       w16's USER on Microsoft's KRNL286   must be 0
#   userk      w16's USER on w16's KRNL286         must be 0
#   gdi        w16's GDI on Microsoft's KRNL286    must be 0
#   gdik       w16's GDI on w16's KRNL286          must be 0
#   usergdi    w16's USER and GDI on w16's KRNL286 must be 0
#   shell      w16's SHELL.DLL on Microsoft's KRNL286 must be 0
#   shellk     w16's SHELL.DLL on w16's KRNL286   must be 0
#   win386     w16's WIN386.EXE, WIN /3 (386 Enhanced) must be 0
#              (tests/win386.sh: Microsoft's KRNL386 and the rest, against
#              Microsoft's WIN386.EXE; both must reach Final USER init).
#              Not in the default set yet: w16's boot is intermittent (an
#              occasional #GP in a hardware interrupt handler, ARCHITECTURE.md
#              12.3); run it by name: tests/boottest.sh win386
#   winoldap   w16's WINOLDAP.MOD on Microsoft's KRNL286: WIN.INI run= starts
#              a PIF (COMMAND.COM /C ECHO HI), the DOS program runs full
#              screen and Windows comes back; against Microsoft's WINOLDAP
#              doing the same                       must be 0
#   winoldapk  the same on w16's KRNL286            must be 0
#   winoldapw  the same with w16's WIN.COM (Microsoft's KRNL286, WINOLDAP
#              and the rest): WIN.COM runs the DOS program between two
#              runs of WSWAP                         must be 0
#   wincomswap the swap directories w16's WIN.COM gives WSWAP (switcher
#              block D7h and 11Bh, WC4 as WSWAP.EXE) in DOSBox, started
#              from C:\WINDOWS, C:\, C:\TMP with TEMP=C:\TMP and from Z:,
#              against Windows 3.1's WIN.COM: equal
#   tree       build/w16 as a whole (tests/w16tree.sh): must boot
#   allw16full every w16 part at once: WIN.COM, DOSX, KRNL286, USER, GDI,
#              PROGMAN, WINOLDAP, WIN87EM and the four fonts: must boot
#              (no pixel comparison: w16's PROGMAN and fonts differ)
#   allw16input every w16 part at once (WIN.COM, DOSX, KRNL286, USER, GDI,
#              PROGMAN, NOTEPAD, WINOLDAP, WIN87EM, fonts) with real input
#              after the boot (tests/scripts/input/menus.txt): the Help menu
#              must open from the keyboard and the pointer must move
#   allw16bootinput the same, with the mouse moved and clicked and keys
#              pressed all through the boot as well, from the logo on
#              (tests/scripts/input/bootmenus.txt)
#   allw16nolock every w16 part with DOSBox's autolock=false (motion
#              arrives without a click, as in an interactive session) and
#              heavy mouse motion from mid-boot on
#              (tests/scripts/input/bootnolock.txt), $NOLOCK_RUNS times
#              (default 6), the motion from second 10, 12, ... 20 in turn
#              (or from second $NOLOCK_BOOT every time): the gap DOSX
#              guards (a PS/2 packet before MOUSE.DRV takes INT 74h) moves
#              with the boot's timing; with the guard taken out the pointer
#              stuck at 10 and 20 (and 16 before). Each time the Help menu must open and the
#              pointer move when moved after the boot (the X pointer put
#              over the window's middle first: with autolock=false DOSBox
#              sees no motion while it is outside, so the final position
#              is not checked, only that the pointer responds)
#   exitwin    File > Exit Windows > OK in Program Manager with real keys
#              (tests/scripts/input/exitwin.txt), every w16 part (WIN.COM,
#              DOSX, KRNL286, USER, GDI, SHELL, PROGMAN) against all of
#              Microsoft's: the DOS prompt must come back, in text mode,
#              the same screen up to the cursor (40 pixels)
#   runfront   Program Manager's File > Run starts Notepad with real keys
#              (tests/scripts/input/runfront.txt): Notepad must end up the
#              active window, Program Manager's caption inactive; w16's
#              USER and GDI on w16's KRNL286
#   runfrontk  the same with Microsoft's USER on w16's KRNL286
#   tabs       Notepad from File > Run, text typed with tabs, closed
#              (tests/scripts/input/tabs.txt); w16's USER and GDI on
#              Microsoft's KRNL286 against all-Microsoft: identical screens
#   alttab     Alt+Tab between Program Manager and Notepad, Notepad closed,
#              Alt+Tab, a menu (tests/scripts/input/alttab.txt): the same
#   helpmenus  Program Manager's Help menu by Alt+H and by a click, then
#              Notepad's (tests/scripts/input/helpmenus.txt): the same
#   pmclose    Program Manager's system box double-clicked: the Exit dialog in
#              front and system modal, no menu left (pmclose.txt)
#   woados     MS-DOS Prompt from Program Manager's File > Run COMMAND.COM,
#              VER, EXIT, then Exit Windows (tests/scripts/input/woados.txt;
#              SYSTEM.INI [NonWindowsApp] SwapDisk=C:\WINDOWS\TEMP; with
#              TEMP set by a batch file around WIN instead, w16's stack
#              did not come back from the prompt, UNVERIFIED why): every w16 part but WIN.COM
#              (UNVERIFIED why: with w16's WIN.COM, Windows ends when the
#              prompt should start) against all of Microsoft's: the prompt
#              in text mode, the same screens up to the cursor (40 pixels),
#              Windows repainted as it was before, text mode at the end
#   woadosbat  the same with TEMP set by a batch file around WIN
#              (C:\WINDOWS\GO.BAT: SET TEMP=C:\WINDOWS\TEMP, WIN /S) and w16's
#              WIN.COM and DOSX as well, as an AUTOEXEC.BAT does: every w16
#              part. Text mode (640x400) after Exit Windows: w16's ended in
#              VGA graphics with the BIOS's text on it (WIN.COM set no mode
#              after WSWAP, DOSX set back the logo's mode 12h)
#   pmkeys     Program Manager's arrow keys straight after it starts, then
#              after a click on the workspace (pmkeys.txt): w16 USER+GDI and
#              w16 PROGMAN against Microsoft's; the group icons left out
#   mdimax     a maximised MDI child's controls in the frame's menu bar:
#              probe US18 as the shell on 3.1 and on w16's USER (its OBS
#              lines identical), then Program Manager's Main group
#              maximised, its system menu from the bar (Alt+-), File and
#              Left, the restore button clicked, Accessories maximised,
#              the Window menu, Ctrl+F4 (tests/scripts/input/mdimax.txt):
#              w16 USER+GDI, and with w16 PROGMAN, against Microsoft's
#   pmitems    an item dragged in Program Manager's Main group, the minimised
#              Accessories group restored (tests/scripts/input/pmitems.txt):
#              w16 USER+GDI, and with w16 PROGMAN, against Microsoft's
#   w16groups  w16's install tree alone (build/w16 as C:\W16, dropin.sh -T;
#              for now with Microsoft's WSWAP.EXE and MMSOUND.DRV, sound.drv=
#              mmsound.drv, which w16 does not have yet): its default
#              PROGMAN.INI and groups (assets/progman) under real keys
#              (tests/scripts/input/w16groups.txt): Main's Read Me opens
#              README.TXT in Notepad, Accessories restored, its Notepad and
#              Calculator run and closed, Windows ended. Microsoft's
#              PROGMAN.EXE on the tree (it must read w16's group files: every
#              item started, each screen differing from the one before) and
#              w16's: every screen identical (the minimised groups' own
#              pictures left out), and the group files and PROGMAN.INI
#              saved at the end identical
#   write      Windows Write (Microsoft's WRITE.EXE, WIN.INI run=): a line
#              typed, the Character menu, the ruler, a second line, File >
#              Exit and No (tests/scripts/input/write.txt); w16's WIN.COM,
#              DOSX, KRNL286, USER, SHELL, WINOLDAP and WIN87EM against
#              all-Microsoft (Microsoft's GDI and fonts: w16's GDI has no
#              TrueType, Write's text is Arial): identical screens, leaving
#              out the caret (it blinks) and in the save box the Yes button
#              (w16's MessageBox puts it 2 pixels right: UNVERIFIED rule)
#   cardfile   Cardfile under real keys (WIN.INI run=, tests/scripts/input/
#              cardfile.txt: typing, Index, Add, Ctrl+Shift+letter, PgUp,
#              List view, Find, Autodial, the Edit menu, Exit without
#              saving): w16's CARDFILE.EXE against Microsoft's, both on
#              all-Microsoft Windows 3.1: every screen identical
#   winfile    File Manager's drive bar and Ctrl+letter drives (real keys,
#              tests/scripts/input/winfile.txt): w16 USER+GDI vs Microsoft's,
#              then every w16 part must not fault
#   runbrowse  Program Manager's File > Run > Browse... (COMMDLG's Open box
#              on USER's DlgDirList/LB_DIR), into SYSTEM, Cancel, the Run
#              box cancelled (tests/scripts/input/runbrowse.txt): w16 USER+GDI
#              against Microsoft's, each screen within its known pixels
#              (combo boxes, edit selection, a default button) (part cmp); every
#              w16 part (COMMDLG, PROGMAN, SYSTEM.DRV, VGA.DRV, fonts ...)
#              must not fault, the list must change, Program Manager as
#              after the boot (part all; it ended Windows in LB_DIR)
#   icons      Notepad, Calculator, Clock, Character Map and Paintbrush
#              minimised, Accessories opened, Program Manager minimised,
#              Alt+Esc from icon to icon (tests/scripts/input/icons.txt):
#              w16 USER+GDI vs Microsoft's, Clock's icon left out
#   about      Help > About Program Manager (SHELL's ShellAbout) opened and
#              closed with Enter and Esc by real keys, then the File and
#              Help menus must open (tests/scripts/input/about.txt); w16's
#              USER, GDI, SHELL and PROGMAN on w16's KRNL286
#   dragoff    real input: Program Manager dragged partly off the right,
#              left and bottom edges; w16 USER+GDI vs Microsoft's: 0 pixels
#   dragoffk   the same on w16's KRNL286
#   pmdrop     real input: a file dragged from File Manager into a Program
#              Manager group, Windows ended from Program Manager
#              (tests/scripts/input/pmdrop.txt); w16's PROGMAN, then w16's
#              PROGMAN and SHELL, against all-Microsoft: the screens and the
#              saved MAIN.GRP must be identical
#   mouse      w16's MOUSE.DRV (drivers/mouse) on Microsoft's KRNL286, DOSX,
#              USER and GDI under real mouse input: the dragoff, pmitems,
#              pmclose and helpmenus scripts (drags, clicks, a double
#              click, menus), every screen against the same script with
#              Microsoft's MOUSE.DRV                must be 0
#   mouseall   every w16 part (WIN.COM, DOSX, KRNL286, USER, GDI, PROGMAN,
#              fonts) with w16's MOUSE.DRV against the same with
#              Microsoft's MOUSE.DRV, the dragoff script: must be 0
#   comm       w16's COMM.DRV (drivers/comm) on Microsoft's KRNL286, DOSX and
#              USER: CM1's exchange through USER on COM1 (tests/commprobe.sh
#              -p 4: data both ways, events, flow control, notifications
#              with tests/commhost.py at the far end of DOSBox's nullmodem
#              port) against the same with Microsoft's COMM.DRV: CM1's log
#              and the far end's records identical (not cached: the
#              far end is part of the run)
#   commdlg    w16's COMMDLG.DLL over Microsoft's in the reference install:
#              open1, find1, color1, font1, print1 of tests/commdlgscen.sh
#              (the probe's log identical, screens within the baseline)
#   commuser   w16's USER's comm functions (user/comm.c) against Windows
#              3.1's: probe CM2 with COMMREC recording USER's calls to the
#              driver (tests/commusr.sh): the logs and the far end's
#              records identical
#   commall    the comm case on every w16 part (WIN.COM, DOSX, KRNL286,
#              USER, GDI, COMM.DRV) against all of Microsoft's: CM1's
#              exchange through USER and the far end's records identical
#   termclose  Terminal closed while it sends a file over COM1 (Alt+F4,
#              Yes to stop: tests/scripts/input/termclose.txt) on every
#              w16 part: Terminal gone, the screen as all-Microsoft's
#   kbd        w16's KEYBOARD.DRV (drivers/keyboard) on Microsoft's KRNL286,
#              DOSX, USER and GDI under real key input: typing in Notepad
#              (kbtype.txt: letters, Shift, digits, punctuation, Caps Lock,
#              the keypad, arrows, Alt menus, Ctrl+C/V, Alt+Tab, Ctrl+Esc,
#              Alt+Esc) and the tabs, alttab, helpmenus, pmkeys, icons and
#              menus scripts, every screen against the same script with
#              Microsoft's KEYBOARD.DRV                must be 0
#   kbdall     every w16 part (WIN.COM, DOSX, KRNL286, USER, GDI, PROGMAN,
#              fonts) with w16's KEYBOARD.DRV against the same with
#              Microsoft's, the kbtype script: must be 0
#   kbdwrite   Windows Write's script (write) with w16's KEYBOARD.DRV on
#              Microsoft's stack: must be 0 but for the caret
#   kbdwoados  the woados script with w16's KEYBOARD.DRV on Microsoft's
#              stack (Disable and Enable around the DOS program): the
#              screens as all-Microsoft's up to the cursor
#   sysdrv     w16's SYSTEM.DRV (drivers/system) on Microsoft's stack under
#              real input: kbtype, tabs, alttab, helpmenus, dragoff,
#              pmkeys, every screen against Microsoft's SYSTEM.DRV: 0
#   sysdrvall  every w16 part with w16's SYSTEM.DRV against the same with
#              Microsoft's, kbtype and tabs        must be 0
#   sysdrvwoados  the woados script with w16's SYSTEM.DRV (system timers
#              off and on around the DOS program), as kbdwoados
#   sysboot    plain boots with w16's SYSTEM.DRV: on Microsoft's stack, on
#              w16's WIN.COM, DOSX, KRNL286, and on an emulated 80286,
#              each against the all-Microsoft boot  must be 0
#   sysclock   Clock's scenarios (tests/clockscenarios.sh) with w16's
#              SYSTEM.DRV under both Clocks: within their baseline
#   sysprobe   tests/sysprobe.sh: SY1's and SYREC's logs, w16's
#              SYSTEM.DRV against Microsoft's      identical
#   vga        w16's VGA.DRV (drivers/vga) on Microsoft's stack, linked with
#              Microsoft's resources (tests/vgamsres.sh: the art is then
#              the same on both sides, the drawing code is compared) under
#              real input: kbtype, tabs, alttab, helpmenus, pmkeys, icons,
#              menus, dragoff, pmitems, pmclose, mdimax, about, every screen
#              against Microsoft's VGA.DRV: 0 (the carets and Clock's icon
#              left out as in kbd; in about, the free memory, which is
#              more with w16's smaller driver)
#   vgaboot    plain boots with w16's VGA.DRV on Microsoft's stack: with
#              Microsoft's resources against the all-Microsoft boot, 0
#              pixels; with w16's own resources the boot must complete
#   vgaall     every w16 part (WIN.COM, DOSX, KRNL286, USER, GDI, PROGMAN,
#              fonts) with w16's VGA.DRV (Microsoft's resources) against
#              the same with Microsoft's VGA.DRV, kbtype     must be 0
#   vgawoados  the woados script with w16's VGA.DRV (Disable to text mode
#              for the DOS program, Enable back): the screens as
#              all-Microsoft's up to the cursor
#   vgaprobe   VG1 as the shell (the driver's values as programs see them:
#              tests/apps/vg1.c), w16's VGA.DRV with its own resources
#              against Microsoft's: identical logs
#   input      real mouse and keyboard input (tests/inputrun.sh: xdotool on
#              the virtual display, through the drivers' event procedures)
#              all through the boot and after, then a menu by keyboard:
#              w16's USER and GDI on w16's KRNL286 against all-Microsoft
#              under the same input; the screens after must be identical.
#              w16's run has [w16] ScrollBarMenu=0: the script right-clicks
#              wherever the boot left the pointer, which could be a scroll
#              bar, and the comparison is with 3.1
#   sbmenu     w16's scroll-bar menu (a w16 extension, user/scroll.c), probe
#              US17 as the shell, w16's USER on Microsoft's KRNL286: with
#              [w16] ScrollBarMenu=0, US17's log (every right click's
#              messages: none) and its last screen against Windows 3.1's
#              run of US17, identical; with the menu on, US17 must pass
#              (each item of each bar's menu: its items, its place, the
#              WM_VSCROLL/WM_HSCROLL it sends, no menu messages to the
#              window). No 3.1 reference can exist for the menu itself;
#              its drawing is the popup code every other menu uses (pixel
#              compared in helpmenus, pmclose ...), so its run's screen,
#              the menu left open (sbmenu-on.png), is kept to look at but
#              not compared
#   sbdrag     a scroll bar's thumb dragged with real input (the outline
#              that follows the pointer, user/scroll.c): probe US19 as the
#              shell (two bars that move with SB_THUMBTRACK, two that do
#              not) and Microsoft's Notepad (45 lines typed), each under
#              w16's USER and GDI on Microsoft's KRNL286 against
#              all-Microsoft: every screen, mid-drag too, identical
#              (Notepad's caret left out), and US19's WM_VSCROLL/WM_HSCROLL
#              log identical (parts sbdrag@us, sbdrag@np)
#   edithandle an edit control's text in the local heap its hInstance names,
#              EM_GETHANDLE, EM_SETHANDLE (user/edit.c): probe US20 as the
#              shell, its OBS lines on w16's USER as on 3.1's; Microsoft's
#              Notepad on w16's USER and GDI opening a 31500-byte file,
#              paging, typing and saving: every screen and the saved
#              bytes as all-Microsoft's (parts edithandle@us, @np)
#   capcursor  the cursor after a capture ends: probe US22 as the shell,
#              the mouse captured, moved and released still over a sizing
#              border, a corner and the client area; w16's USER and GDI
#              against all-Microsoft: every screen and US22's log of mouse
#              messages and WM_SETCURSOR identical
#   deskver    w16's desktop version stamp (a w16 extension,
#              user/deskver.c; every other case runs with it off, as
#              tests/dropin.sh's default [w16] DesktopVersion=0): with 0,
#              w16's USER and GDI on w16's KRNL286 against all-Microsoft,
#              must be 0 pixels; with every w16 part (fonts and PROGMAN
#              too), Program Manager dragged over the stamp and back
#              (tests/scripts/input/deskver.txt) with 0 and with 1: each
#              screen with 1 must be the one with 0 plus the stamp as
#              tools/deskstamp.py draws it from w16's System font (exact
#              where the corner is uncovered; only the stamp's pixels
#              where the window covers part of it; only inside its box
#              under the drag frame) (tests/scripts/deskver.py)
# The reference for each is Microsoft's KRNL286 (dropin.sh -r), DOSX,
# WIN.COM and USER. A boot whose BOOTLOG.TXT does not reach
# "INITDONE=Final USER" fails whatever the pixels say.
#
#   usage: tests/boottest.sh [-t SECS] [-o DIR] [CASE...]   (default: all)
# Screens, diffs and logs go to DIR (default build/boottest-HHMMSS, a
# new directory each run). Exit status: number of failed cases. Each
# case's time is printed at the end (and kept in DIR/times.txt).
#
# Reference cache. The all-Microsoft runs (the references: ref, woaref,
# the CASE-ref runs of the input cases, wincomswap's 3.1 side) do not
# change while nothing that makes them changes, so their results (screens,
# logs, the input run's directory) are kept in build/refcache/NAME/KEY/ and
# reused (refrun below). KEY is a hash of everything that determines them:
# the run's command line (dropin.sh/inputrun.sh options, with this run's
# paths taken out), the contents of every file named on it (input
# scripts, PIFs, WIN.INIs, and the w16 files some reference runs use,
# such as WC4.COM as WSWAP.EXE in wincomswap, and build/dos's WIN.COM,
# DOSX.EXE, KRNL286.EXE when dropin.sh -w, -x or no -r would take them;
# mouseall's "reference" is mostly w16's and is not cached), DROPIN_* and INPUTRUN_*
# in the environment, -t SECS, the reference install (each file's path,
# size and time under .tools/ref/win31/cdrive), the emulator and tools
# (path, size, time of dosbox, dosbox-x, Xvfb, magick, import, xdotool),
# the harness (tests/dropin.sh, inputrun.sh, xvfb.sh, w16tree.sh,
# scripts/*.py, and the reference-run code of this file between the
# ">>> reference-run code" marks) and the text of the case (or function)
# that makes the run. A result is stored only when it looks complete (a
# boot that completed with a screen that is not blank; an input run that
# ran its script to the end with every shot taken and none blank) and
# its case passed with it: a reference caught at a bad moment (a screen
# taken mid-repaint on a loaded host) would otherwise fail every later
# run from the cache; a failing case's references run again next time. In a
# scratch drive kept with a run, files equal to the reference install's
# are stored as links to it. Each entry has key.txt, what its key was
# made from. BOOTTEST_REFRESH=1 in the environment: run every reference
# again and replace what is stored. The w16 side of a case is never
# cached. DIR/refcache.log says which references came from the cache.
# Entries no longer used are not removed (rm -r build/refcache clears it).
#
# Waits. A boot's screen is taken when the screen is right, not after a
# fixed time: the w16 side when it equals the reference's (dropin.sh -e),
# a boot with nothing to compare with when Windows has settled (-s: Final
# USER init in BOOTLOG.TXT, then 5 seconds unchanged); SECS stays the
# limit, and a screen that never matches is taken then, as before. The
# input cases' references start their script when Windows has settled
# with Program Manager (or the program the boot starts) up (inputrun.sh
# -s: Final USER init, 5 s unchanged, not a blank capture, an active
# caption), at -b at the latest, and save that screen (boot.png); the w16
# side starts its script when its screen equals that one (-e), or when
# settled (-s) where the sides' boot screens differ by design. A shot
# that comes back blank (a failed capture) is taken again. Within the
# scripts, "match NAME MAX STILL" before a shot makes the reference wait
# for a still screen and the w16 side (-E the reference's run, -M the
# parts the case leaves out) for the reference's screen, at most MAX
# seconds. A session ends when its
# script has run, not at its time limit. DROPIN_FIXED=1 and
# INPUTRUN_FULL=1 give the old fixed timing (for debugging; they change
# the references' keys).
#
# Job pool. BOOTTEST_JOBS cases (default 3) run at a time, each in a
# subshell writing DIR/.case-N.out; the results are printed in case order.
# Each DOSBox has an X display of its own (tests/xvfb.sh: Xvfb
# -displayfd, so parallel runs never share one), and every case's files
# have names of their own; references shared by cases (ref, woaref,
# woaref2, dragoff-ref) are made once under a lock per cache key, and
# retries are logged under a lock. BOOTTEST_JOBS=1 runs one case at a time.
# The cases that took longest last time (build/boottest-times.txt, kept
# by each run) start first; the output stays in case order. Cases made of
# independent sessions run as parts (allw16nolock@1..6, pmdrop@pm/sh/us, sbdrag@us/np, edithandle@us/np,
# winfile@cmp/all, kbd@SCRIPT, mouse@SCRIPT: see caseparts), scheduled like cases and summed up in
# the case's one line; a part can be run by name (tests/boottest.sh
# allw16nolock@3).
set -eu
unset DISPLAY WAYLAND_DISPLAY     # headless only: never the user's desktop
root=$(cd "$(dirname "$0")/.." && pwd)
secs=45 out=
while getopts t:o: opt; do
    case $opt in
    t) secs=$OPTARG ;;
    o) out=$OPTARG ;;
    *) sed -n '2,/^set -eu/p' "$0" | sed '$d'; exit 2 ;;
    esac
done
shift $((OPTIND - 1))
[ -n "$out" ] || out=$root/build/boottest-$(date +%H%M%S)
[ -e "$out" ] && { echo "$out exists; choose a new directory" >&2; exit 2; }
mkdir -p "$out"
cases=${*:-kernel dosx k286 lastexit odstring wincom allw16 user userk gdi gdik usergdi winoldap winoldapk winoldapw wincomswap winprompt shell shellk tree allw16full input allw16input allw16bootinput allw16nolock exitwin runfront runfrontk about dragoff dragoffk pmdrop tabs alttab helpmenus sbmenu sbdrag edithandle capcursor deskver icons winfile runbrowse pmitems w16groups pmclose write cardfile mouse mouseall woados woadosbat pmkeys kbd kbdall kbdwrite kbdwoados comm commdlg mdimax commuser commall termclose sysdrv sysdrvall sysdrvwoados sysboot sysclock sysprobe vga vgaboot vgaall vgawoados vgaprobe}
user=$root/build/dos/USER.EXE
gdi=$root/build/dos/GDI.EXE
shell=$root/build/dos/SHELL.DLL
fails=0

# >>> reference-run code (part of every cached reference's key)
# boot NAME [dropin.sh options...]: one boot, screenshot NAME.png, log NAME.txt
# With $expect set (ref and woaref set it to their screen), the screen is
# taken as soon as it equals that one (dropin.sh -e), at SECS otherwise;
# without, once Windows has settled (dropin.sh -s)
boot() {
    name=$1; shift
    tries=0
    [ -z "${expect:-}" ] || set -- -e "$expect" "$@"
    while :; do
        "$root/tests/dropin.sh" -t "$secs" -i w16:watchdog=0 -s "$out/$name.png" "$@" \
            > "$out/$name.txt" 2>&1 || true
        # DOSBox-X ending at once at its start (dropin.sh restarts it)
        if grep -q '^== emulator restarted' "$out/$name.txt"; then
            retried "$name" emulator-start "$(grep -c '^emulator restarted' "$out/$name.txt") restart(s): Couldn't open X11 display"
        fi
        # A screenshot of one colour means the hidden X display or the
        # window capture failed (seen under heavy load), not that Windows
        # drew nothing: Windows' screen is never a single colour here.
        # Boot again, up to two more times.
        blank "$out/$name.png" || break
        tries=$((tries + 1))
        retried "$name" blank-screenshot "try $tries"
        [ $tries -le 2 ] || { echo "warning: $name: screenshot still blank after $tries tries" >&2; break; }
        mv "$out/$name.png" "$out/$name.blank$tries.png" 2>/dev/null || true
    done
}
# blank FILE: true when the image is missing or has a single colour
blank() {
    [ -f "$1" ] || return 0
    [ "$(magick identify -format '%k' "$1" 2>/dev/null)" = 1 ]
}
# <<< reference-run code
# retried CASE KIND DETAIL: record a retry. Every retry is printed, kept in
# this run's retries.txt and appended to build/boottest-retries.log, which
# outlives runs: a case that keeps needing retries is a bug to investigate
# (an intermittent real bug looks exactly like a flaky test), not noise.
retrylog=$root/build/boottest-retries.log
retried() {
    # (git's failure, outside a repository as on the remote build host,
    # made the assignment fail and set -e end the case: "the case stopped
    # before its end" in place of every retried case's result)
    line="$(date '+%Y-%m-%d %H:%M') $(git -C "$root" rev-parse --short HEAD 2>/dev/null || true) $1 $2 $3"
    echo "RETRY $1: $2 ($3)"
    # cases run in parallel: one writer at a time
    (
        flock 7
        echo "$line" >> "$out/retries.txt"
        echo "$line" >> "$retrylog"
    ) 7>> "$retrylog.lock"
}

# booted NAME: the boot completed and Windows was still running when the
# screen was taken (KERNEL prints "w16: back to DOS" when Windows ends)
booted() { grep -q '^boot: complete' "$out/$1.txt" && ! grep -q 'back to DOS' "$out/$1.txt"; }

# The reference cache (see the top of this file).
refcache=$root/build/refcache
reftree=$root/.tools/ref/win31/cdrive
# what every reference's key shares: the reference install, the emulator
# and tools, the harness scripts, the reference-run code of this file
refbase=$( {
    (cd "$reftree" && find . -type f -printf '%P %s %T@\n' | sort)
    for t in dosbox dosbox-x Xvfb magick import xdotool; do
        p=$(command -v "$t" 2>/dev/null) && stat -L -c "$t %n %s %Y" "$p"
    done
    (cd "$root/tests" && sha256sum dropin.sh inputrun.sh xvfb.sh w16tree.sh scripts/*.py 2>/dev/null)
    sed -n '/^# >>> reference-run code/,/^# <<< reference-run code/p' "$0"
} | sha256sum | cut -d' ' -f1)
# codeof NAME: the text of case NAME (its whole branch, shared with the
# cases on the same label) or of function NAME in this file
codeof() {
    awk -v n="$1" '
        fn { print; if ($0 ~ /^}/) fn = 0; next }
        index($0, n "()") == 1 { print; if ($0 !~ /}$/) fn = 1; next }
        /^    [a-z0-9|]+\)/ {
            if (on) exit
            k = split(substr($1, 1, index($1, ")") - 1), l, "|")
            for (i = 1; i <= k; i++) if (l[i] == n) on = 1
        }
        on && /^    esac/ { exit }
        on { print }' "$0"
}
# refkey NAME CODE ARGS...: the key of reference run NAME made by ARGS in
# case (or function) CODE; what it was made from goes to the file $_rkf
refkey() {
    {
        echo "refcache 1 $1"
        echo "base $refbase"
        env | grep -E '^(DROPIN|INPUTRUN)_' | sort
        echo "secs $secs"
        k=$2; shift 2
        for a in "$@"; do
            printf 'arg %s\n' "$a" | sed "s|$out|@OUT@|g; s|$root|@ROOT@|g"
            for f in "$a" "${a%%=*}"; do
                if [ -f "$f" ]; then printf 'file '; sha256sum < "$f"; break; fi
            done
        done
        # w16 files dropin.sh takes without naming them
        case " $* " in *" -w "*) printf 'WIN.COM '; sha256sum < "$root/build/dos/WIN.COM" ;; esac
        case " $* " in *" -x "*) printf 'DOSX.EXE '; sha256sum < "$root/build/dos/DOSX.EXE" ;; esac
        case " $* " in *" -r "*) ;; *) printf 'KRNL286.EXE '; sha256sum < "$root/build/dos/KRNL286.EXE" ;; esac
        echo "code $k"
        codeof "$k"
    } > "$_rkf"
    sha256sum < "$_rkf" | cut -c1-16
}
# refok NAME: the reference run's result looks complete (worth keeping)
refok() {
    [ -f "$out/$1.txt" ] || return 1
    if [ -f "$out/$1.png" ]; then booted "$1" && ! blank "$out/$1.png" || return 1; fi
    if [ -f "$out/$1.dirs" ]; then [ -s "$out/$1.dirs" ] || return 1; fi
    if [ -d "$out/$1" ] && [ -f "$out/$1/input.log" ]; then
        grep -q '^script done' "$out/$1/input.log" || return 1
        ! grep -q 'FAILED' "$out/$1/input.log" || return 1
        ls "$out/$1"/*.png > /dev/null 2>&1 || return 1
        for _rf in "$out/$1"/*.png; do ! blank "$_rf" || return 1; done
    fi
    return 0
}
# refkeep: at the end of a case, store the references it ran if it passed
# (a reference that looks complete can still be wrong, e.g. a screen
# taken mid-repaint on a loaded host: such a one would fail every run
# from the cache), drop them if not
refkeep() {
    [ -f "$out/.pending-$_bt_n" ] || return 0
    while read -r _rd _rt; do
        if [ "${fails:-1}" = 0 ]; then
            rm -rf "$_rd"; mv "$_rt" "$_rd"
            echo "$(basename "$(dirname "$_rd")") $(basename "$_rd") stored" >> "$out/refcache.log"
        else
            rm -rf "$_rt"
            echo "$(basename "$(dirname "$_rd")") $(basename "$_rd") not stored: the case failed" >> "$out/refcache.log"
        fi
    done < "$out/.pending-$_bt_n"
    rm -f "$out/.pending-$_bt_n"
}
# refrun NAME CODE boot [dropin.sh options...]
# refrun NAME CODE COMMAND [ARGS...]
#   a reference run, from the cache when it has it. NAME's results are
#   $out/NAME.png and NAME.txt (boot), or whatever COMMAND makes of
#   $out/NAME (a directory) and $out/NAME.* (its output goes to NAME.txt).
#   CODE: the case (or function) whose text is part of the key. Parallel
#   cases wanting the same run wait for one another (a lock per key); a
#   run is made once per boottest run even with BOOTTEST_REFRESH=1.
#   Its variables begin with _r, so that the case's own are left alone.
refrun() {
    _rn=$1 _rc=$2; shift 2
    _rkf=$(mktemp "$out/.key-$_rn.XXXXXX")
    _rk=$(refkey "$_rn" "$_rc" "$@")
    _rd=$refcache/$_rn/$_rk
    mkdir -p "$refcache/$_rn"
    exec 8> "$refcache/$_rn/$_rk.lock"
    flock 8
    if [ "$(cat "$out/.ref-$_rn" 2>/dev/null)" = "$_rk" ]; then
        :
    elif [ -z "${BOOTTEST_REFRESH:-}" ] && [ -d "$_rd/files" ]; then
        rm -rf "$out/$_rn"
        cp -a "$_rd/files/." "$out/"
        echo "$_rn $_rk from the cache" >> "$out/refcache.log"
    else
        rm -rf "$out/$_rn" "$out/$_rn".png "$out/$_rn".txt
        if [ "$1" = boot ]; then
            shift; _roexp=${expect:-}; expect=
            boot "$_rn" "$@"
            expect=$_roexp
        else
            "$@" > "$out/$_rn.txt" 2>&1 || true
        fi
        if refok "$_rn"; then
            _rt=$refcache/$_rn/.new-$_rk-${_bt_n:-0}-$$
            rm -rf "$_rt"; mkdir -p "$_rt/files"
            for _rf in "$out/$_rn" "$out/$_rn".*; do [ -e "$_rf" ] && cp -a "$_rf" "$_rt/files/"; done
            # the scratch drive (inputrun.sh's work/c, dropin.sh -o's c):
            # files as in the reference install become links to it
            for _rdd in "$_rt/files/$_rn/work/c" "$_rt/files/$_rn/c"; do
                [ -d "$_rdd" ] || continue
                (cd "$_rdd" && find . -type f) | while IFS= read -r _rf; do
                    _rf=${_rf#./}
                    if cmp -s "$_rdd/$_rf" "$reftree/$_rf"; then ln -sf "$reftree/$_rf" "$_rdd/$_rf"; fi
                done
            done
            cp "$_rkf" "$_rt/key.txt"
            # kept only if the case passes with it (refkeep): a reference
            # caught in a bad moment must not stay in the cache
            echo "$_rd $_rt" >> "$out/.pending-${_bt_n:-0}"
            echo "$_rn $_rk ran" >> "$out/refcache.log"
        else
            echo "$_rn $_rk ran, NOT stored: incomplete (see $out/$_rn.txt)" >> "$out/refcache.log"
        fi
    fi
    echo "$_rk" > "$out/.ref-$_rn"
    flock -u 8; exec 8>&-
    rm -f "$_rkf"
}

# check CASE W16 REF: both must boot; the screens must be identical
check() {
    c=$1 w=$2 r=$3
    if ! booted "$w"; then
        echo "FAIL $c: w16 boot did not complete (see $out/$w.txt)"; fails=$((fails + 1)); return
    fi
    if ! booted "$r"; then
        echo "FAIL $c: reference boot did not complete (see $out/$r.txt)"; fails=$((fails + 1)); return
    fi
    n=$(magick compare -metric AE "$out/$w.png" "$out/$r.png" "$out/$c-diff.png" 2>&1 >/dev/null || true)
    n=${n%% *}
    # The WINOLDAP reference itself varies between boots (Microsoft's own
    # boot sometimes leaves a stray repaint on Program Manager's caption:
    # two rows, about 350 pixels; seen 2026-11-08/09). If the case
    # differs, boot the reference once more and keep the better match:
    # a real difference shows against both reference boots.
    if [ "$n" != 0 ] && [ "$r" = woaref ]; then
        n1=$n
        refrun woaref2 check boot -r -f "$out/HI.PIF=HI.PIF" -f "$out/WIN.INI=WINDOWS/WIN.INI"
        if booted woaref2; then
            n2=$(magick compare -metric AE "$out/$w.png" "$out/woaref2.png" "$out/$c-diff2.png" 2>&1 >/dev/null || true)
            n2=${n2%% *}
            if [ "$n2" = 0 ]; then n=0; retried "$c" woaref-second-boot "first reference differed by $n1 pixels"; fi
        fi
        # the same stray caption repaint shows up in the case's own boot
        # too (it is Microsoft's USER redrawing after the DOS program
        # returns): boot the case once more and compare with both
        if [ "$n" != 0 ]; then
            mv "$out/$w.png" "$out/$w.first.png"
            mv "$out/$w.txt" "$out/$w.first.txt"
            [ "$w" = winoldapw ] && set -- -w || set -- -f "$woa=WINDOWS/SYSTEM/WINOLDAP.MOD"
            boot "$w" -f "$out/HI.PIF=HI.PIF" -f "$out/WIN.INI=WINDOWS/WIN.INI" "$@" \
                $([ "$w" != winoldapk ] && echo -r)
            for rr in woaref woaref2; do
                [ -f "$out/$rr.png" ] || continue
                m=$(magick compare -metric AE "$out/$w.png" "$out/$rr.png" null: 2>&1 || true)
                if [ "${m%% *}" = 0 ]; then n=0; fi
            done
            # (an if, not &&: a false test as check's last command made
            # check fail and set -e end the case without a FAIL line)
            if [ "$n" = 0 ]; then retried "$c" case-second-boot "first boot differed by $n1 pixels"; fi
        fi
    fi
    case $n in
    0) echo "ok   $c: 0 pixels" ;;
    *) echo "FAIL $c: $n pixels differ ($out/$c-diff.png)"; fails=$((fails + 1)) ;;
    esac
}

refdone=
ref() { [ -n "$refdone" ] || { refrun ref ref boot -r; refdone=1; }; expect=$out/ref.png; }

# the WINOLDAP cases: a PIF for COMMAND.COM /C ECHO HI, started by WIN.INI run=
woa=$root/build/dos/WINOLDAP.MOD
woaref=
woafiles() {
    exec 7> "$out/.woafiles.lock"
    flock 7
    if [ ! -f "$out/.woafiles" ]; then
        refwin=$root/.tools/ref/win31/cdrive/WINDOWS
        python3 "$root/tools/mkpif.py" "$refwin/_DEFAULT.PIF" "$out/HI.PIF" \
            'title=Echo Hi' 'program=COMMAND.COM' 'params=/C ECHO HI'
        cp "$refwin/WIN.INI" "$out/WIN.INI"
        python3 "$root/tests/scripts/iniset.py" "$out/WIN.INI" 'windows:run=C:\HI.PIF'
        touch "$out/.woafiles"
    fi
    flock -u 7; exec 7>&-
}
woaref() {
    [ -n "$woaref" ] && { expect=$out/woaref.png; return; }
    woafiles
    refrun woaref woaref boot -r -f "$out/HI.PIF=HI.PIF" -f "$out/WIN.INI=WINDOWS/WIN.INI"
    woaref=1 expect=$out/woaref.png
}

# runcase CASE: one case; run in a subshell of its own (the job pool below)
runcase() {
    c=${1%@*} part=
    case $1 in *@*) part=${1#*@} ;; esac
    case $c in
    kernel) ref; boot kernel; check kernel kernel ref ;;
    dosx)   ref; boot dosx -x; check dosx dosx ref ;;
    odstring)
        # TK56 as the shell calls OutputDebugString and waits. Windows
        # 3.1's retail KERNEL writes the text to DOS handle 3 (AUX), which
        # in DOSBox is the console: it is painted over the screen, with
        # WIN's output redirected to a file as here. Stock setting
        # (dropin.sh's DebugToAux=1): against 3.1's screen. w16's default
        # (DebugToAux=0: AUX only when it is not the console): the screen
        # must be the one TK56 leaves without the calls (tail "q")
        set -- -f "$root/build/dos/TK56.EXE=TK56.EXE" -i 'boot:shell=C:\TK56.EXE'
        refrun odsref odstring boot -r "$@"; expect=$out/odsref.png
        boot odstring "$@"; check odstring odstring odsref
        expect=
        boot odsquiet -c 'WIN /S /B q' "$@"
        boot odsdefault -i w16:DebugToAux=0 "$@"
        if booted odsquiet && booted odsdefault; then
            n=$(magick compare -metric AE "$out/odsdefault.png" "$out/odsquiet.png" "$out/odsdefault-diff.png" 2>&1 >/dev/null || true)
            n=${n%% *}
            if [ "$n" = 0 ]; then echo "ok   odstring default: the text kept off the screen (0 pixels against no calls)"
            else echo "FAIL odstring default: $n pixels differ from the screen without the calls ($out/odsdefault-diff.png)"; fails=$((fails + 1)); fi
        else
            echo "FAIL odstring default: a boot did not complete ($out/odsdefault.txt, $out/odsquiet.txt)"; fails=$((fails + 1))
        fi ;;
    lastexit)
        # the shell (TK55) ends at once with exit code 5: Windows ends
        # through USER's ExitWindows (KERNEL, last task); a batch file
        # around WIN records WIN's ERRORLEVEL and the video mode after it
        {
            printf 'WIN /S /B > C:\\OUT.TXT\r\n'
            for n in 9 8 7 6 5 4 3 2 1 0; do printf 'IF ERRORLEVEL %s GOTO E%s\r\n' $n $n; done
            for n in 9 8 7 6 5 4 3 2 1 0; do printf ':E%s\r\nECHO rc %s> C:\\EL.TXT\r\nGOTO V\r\n' $n $n; done
            printf ':V\r\nC:\\VIDMODE\r\n'
            for n in 18 16 7 3 2 1 0; do printf 'IF ERRORLEVEL %s GOTO M%s\r\n' $n $n; done
            for n in 18 16 7 3 2 1 0; do printf ':M%s\r\nECHO mode %s>> C:\\EL.TXT\r\nGOTO Z\r\n' $n $n; done
            printf ':Z\r\n'
        } > "$out/GO55.BAT"
        set -- -c GO55 -f "$out/GO55.BAT=WINDOWS/GO55.BAT" -f "$root/build/dos/VIDMODE.COM=VIDMODE.COM" \
            -f "$root/build/dos/TK55.EXE=TK55.EXE" -i 'boot:shell=C:\TK55.EXE'
        r=ok
        # both at once; each ends when the batch file's record is written
        # (-W EL.TXT), at 60 s at the latest as before
        for k in ref w16; do
            o=; [ $k = ref ] && o=-r
            "$root/tests/dropin.sh" -t 60 -W EL.TXT -o "$out/lastexit-$k" $o "$@" > "$out/lastexit-$k.txt" 2>&1 &
        done
        wait
        for k in ref w16; do
            el=$(tr -d '\r' < "$out/lastexit-$k/c/EL.TXT" 2>/dev/null | tr '\n' ' ')
            [ "$el" = "rc 0 mode 3 " ] || r="$k: $el (rc 0 mode 3 expected)"
        done
        if [ "$r" = ok ]; then echo "ok   lastexit: the last task's end ends Windows through ExitWindows (ERRORLEVEL 0, mode 3)"
        else echo "FAIL lastexit: $r ($out/lastexit-w16)"; fails=$((fails + 1)); fi ;;
    k286)   # an emulated 80286 (DOSBox-X cputype=286): w16's KRNL286 on
            # Microsoft's DOSX and on w16's, against Microsoft's KRNL286
            # there (mode switches back to real mode by processor reset)
            _s=$secs; secs=180
            refrun ref286 k286 boot -r -p 286; expect=$out/ref286.png
            boot k286 -p 286; check k286 k286 ref286
            boot k286x -x -p 286; check k286x k286x ref286
            expect= secs=$_s ;;
    wincom) ref; boot wincom -w; check wincom wincom ref ;;
    allw16) ref; boot allw16 -w -x; check allw16 allw16 ref ;;
    user)   ref; boot user -r -f "$user=WINDOWS/SYSTEM/USER.EXE"
            check user user ref ;;
    userk)  ref; boot userk -f "$user=WINDOWS/SYSTEM/USER.EXE"
            check userk userk ref ;;
    gdi)    ref; boot gdi -r -f "$gdi=WINDOWS/SYSTEM/GDI.EXE"
            check gdi gdi ref ;;
    gdik)   ref; boot gdik -f "$gdi=WINDOWS/SYSTEM/GDI.EXE"
            check gdik gdik ref ;;
    usergdi) ref; boot usergdi -f "$user=WINDOWS/SYSTEM/USER.EXE" -f "$gdi=WINDOWS/SYSTEM/GDI.EXE"
            check usergdi usergdi ref ;;
    shell)  ref; boot shell -r -f "$shell=WINDOWS/SYSTEM/SHELL.DLL"
            check shell shell ref ;;
    shellk) ref; boot shellk -f "$shell=WINDOWS/SYSTEM/SHELL.DLL"
            check shellk shellk ref ;;
    win386)
        if "$root/tests/win386.sh" -o "$out/win386" > "$out/win386.txt" 2>&1 \
           && grep -q '^INITDONE=Final USER' "$out/win386/w16-bootlog.txt" \
           && grep -q '^INITDONE=Final USER' "$out/win386/ms-bootlog.txt"; then
            echo "ok   win386: 0 pixels"
        else
            echo "FAIL win386: see $out/win386.txt and $out/win386/"; fails=$((fails + 1))
        fi ;;
    # The WINOLDAP cases run on DOSBox's plain VGA (machine=vgaonly): on
    # its default svga_s3, after the DOS program's text mode and back, the
    # displayed image now and then kept stale bytes 1000h-104Fh of the
    # planes (Program Manager's frame top in the desktop colour, 349-371
    # pixels) that were not in video memory (GDI's GetPixel read the right
    # frame), with Microsoft's WINOLDAP as with w16's (commit 7480e1c).
    # Remote runs of these three cases: svga_s3 8 of 10 with a failure,
    # vgaonly 0 of 15. Windows' display driver is VGA.DRV either way.
    winoldap)  export DROPIN_MACHINE=vgaonly; woaref; boot winoldap -r -f "$out/HI.PIF=HI.PIF" -f "$out/WIN.INI=WINDOWS/WIN.INI" \
                   -f "$woa=WINDOWS/SYSTEM/WINOLDAP.MOD"
               check winoldap winoldap woaref ;;
    winoldapk) export DROPIN_MACHINE=vgaonly; woaref; boot winoldapk -f "$out/HI.PIF=HI.PIF" -f "$out/WIN.INI=WINDOWS/WIN.INI" \
                   -f "$woa=WINDOWS/SYSTEM/WINOLDAP.MOD"
               check winoldapk winoldapk woaref ;;
    winoldapw) export DROPIN_MACHINE=vgaonly; woaref; boot winoldapw -r -w -f "$out/HI.PIF=HI.PIF" -f "$out/WIN.INI=WINDOWS/WIN.INI"
               check winoldapw winoldapw woaref ;;
    wincomswap)
        # WC4 in WSWAP's place records the block; each scenario a batch file
        # wcsrun NAME [dropin.sh options...]: run it, the directories to NAME.dirs
        wcsrun() {
            n=$1; shift
            "$root/tests/dropin.sh" -t 20 -W WC.LOG -o "$out/$n" "$@" || true
            python3 "$root/tests/scripts/swapdirs.py" "$out/$n/c/WC.LOG" > "$out/$n.dirs" 2>&1
        }
        r=ok
        for t in 'WIN /S' 'CD \;C:\WINDOWS\WIN /S' 'MD C:\TMP;SET TEMP=C:\TMP;CD \TMP;C:\WINDOWS\WIN /S' \
                 'Z:;C:\WINDOWS\WIN /S;C:'; do
            printf '%s\n' "$t" | tr ';' '\n' | sed 's/$/\r/' > "$out/WCS.BAT"
            set -- -c 'C:\WCS' -f "$out/WCS.BAT=WCS.BAT" -f "$root/build/dos/WC4.COM=WINDOWS/SYSTEM/WSWAP.EXE"
            refrun wcs-ms wincomswap wcsrun wcs-ms -r "$@"
            rm -rf "$out/wcs-w16"
            wcsrun wcs-w16 -r -w "$@" > "$out/wcs-w16.txt" 2>&1
            cmp -s "$out/wcs-ms.dirs" "$out/wcs-w16.dirs" ||
                { r="$t: $(cat "$out/wcs-w16.dirs"), 3.1: $(cat "$out/wcs-ms.dirs")"; break; }
        done
        if [ "$r" = ok ]; then echo "ok   wincomswap: swap directories as 3.1's"
        else echo "FAIL wincomswap: $r"; fails=$((fails + 1)); fi ;;
    tree)
        ref
        if "$root/tests/w16tree.sh" -t "$secs" -o "$out/tree" -R "$out/ref.png" > "$out/tree.txt" 2>&1; then
            echo "ok   tree: boots ($(sed -n "s/.*Windows 3.1.s own files: \([0-9]*\).*/\1/p" "$out/tree.txt" | tail -1) pixels off stock 3.1, expected: w16 PROGMAN, fonts and USER/GDI drawing differences)"
        else
            echo "FAIL tree: see $out/tree.txt"; fails=$((fails + 1))
        fi ;;
    allw16full)
        fon=$root/build/fonts
        boot allw16full -w -x -f "$user=WINDOWS/SYSTEM/USER.EXE" -f "$gdi=WINDOWS/SYSTEM/GDI.EXE" \
            -f "$root/build/dos/PROGMAN.EXE=WINDOWS/PROGMAN.EXE" -f "$woa=WINDOWS/SYSTEM/WINOLDAP.MOD" \
            -f "$root/build/dos/WIN87EM.DLL=WINDOWS/SYSTEM/WIN87EM.DLL" \
            -f "$fon/VGASYS.FON=WINDOWS/SYSTEM/VGASYS.FON" -f "$fon/VGAFIX.FON=WINDOWS/SYSTEM/VGAFIX.FON" \
            -f "$fon/VGAOEM.FON=WINDOWS/SYSTEM/VGAOEM.FON" -f "$fon/SSERIFE.FON=WINDOWS/SYSTEM/SSERIFE.FON"
        if booted allw16full; then echo "ok   allw16full: boots"
        else echo "FAIL allw16full: did not boot (see $out/allw16full.txt)"; fails=$((fails + 1)); fi ;;
    allw16input|allw16bootinput)
        fon=$root/build/fonts
        if [ $c = allw16input ]; then set -- -s -b 30 -t 90 "$root/tests/scripts/input/menus.txt"
        else set -- -b 1 -t 150 "$root/tests/scripts/input/bootmenus.txt"; fi
        "$root/tests/inputrun.sh" "$@" "$out/$c" \
            -w -x -f "$user=WINDOWS/SYSTEM/USER.EXE" -f "$gdi=WINDOWS/SYSTEM/GDI.EXE" \
            -f "$root/build/dos/PROGMAN.EXE=WINDOWS/PROGMAN.EXE" -f "$woa=WINDOWS/SYSTEM/WINOLDAP.MOD" \
            -f "$root/build/dos/NOTEPAD.EXE=WINDOWS/NOTEPAD.EXE" \
            -f "$root/build/dos/WIN87EM.DLL=WINDOWS/SYSTEM/WIN87EM.DLL" \
            -f "$fon/VGASYS.FON=WINDOWS/SYSTEM/VGASYS.FON" -f "$fon/VGAFIX.FON=WINDOWS/SYSTEM/VGAFIX.FON" \
            -f "$fon/VGAOEM.FON=WINDOWS/SYSTEM/VGAOEM.FON" -f "$fon/SSERIFE.FON=WINDOWS/SYSTEM/SSERIFE.FON" \
            > "$out/$c.txt" 2>&1 || true
        d=$out/$c
        if [ -f "$d/01-menu.png" ] && [ -f "$d/02-help.png" ] && [ -f "$d/03-moved.png" ]; then
            h=$(magick compare -metric AE "$d/01-menu.png" "$d/02-help.png" /dev/null 2>&1 >/dev/null || true)
            m=$(magick compare -metric AE "$d/01-menu.png" "$d/03-moved.png" /dev/null 2>&1 >/dev/null || true)
            h=${h%%[. ]*} m=${m%%[. ]*}
            if [ "${h:-0}" -gt 1000 ] && [ "${m:-0}" -gt 0 ]; then echo "ok   $c: menus and pointer respond"
            else echo "FAIL $c: input had no effect (help menu $h pixels, pointer $m; $d)"; fails=$((fails + 1)); fi
        else
            echo "FAIL $c: screens missing ($d)"; fails=$((fails + 1))
        fi ;;
    allw16nolock)
        fon=$root/build/fonts
        k=1 bad=0 runs=${NOLOCK_RUNS:-6}
        [ -z "$part" ] || k=$part runs=$part
        while [ $k -le $runs ]; do
            b=${NOLOCK_BOOT:-$((10 + (k - 1) % 6 * 2))}
            INPUTRUN_NOLOCK=1 "$root/tests/inputrun.sh" -b "$b" -t 240 \
                "$root/tests/scripts/input/bootnolock.txt" "$out/$c-$k" \
                -w -x -f "$user=WINDOWS/SYSTEM/USER.EXE" -f "$gdi=WINDOWS/SYSTEM/GDI.EXE" \
                -f "$root/build/dos/PROGMAN.EXE=WINDOWS/PROGMAN.EXE" \
                -f "$fon/VGASYS.FON=WINDOWS/SYSTEM/VGASYS.FON" -f "$fon/VGAFIX.FON=WINDOWS/SYSTEM/VGAFIX.FON" \
                -f "$fon/VGAOEM.FON=WINDOWS/SYSTEM/VGAOEM.FON" -f "$fon/SSERIFE.FON=WINDOWS/SYSTEM/SSERIFE.FON" \
                > "$out/$c-$k.txt" 2>&1 || true
            d=$out/$c-$k
            h=$(magick compare -metric AE "$d/01-help.png" "$d/02-before.png" /dev/null 2>&1 >/dev/null || true)
            p=$(magick compare -metric AE "$d/02-before.png" "$d/03-after.png" /dev/null 2>&1 >/dev/null || true)
            h=${h%%[. ]*} p=${p%%[. ]*}
            # the arrow's old and new places: 84 pixels when observed
            # (more than a blink of a caret)
            if [ "${h:-0}" -gt 1000 ] && [ "${p:-0}" -gt 40 ]; then :; else
                echo "     $c run $k (motion from second $b): help menu $h pixels, pointer moved $p pixels ($d)"; bad=$((bad + 1)); fi
            k=$((k + 1))
        done
        if [ -n "$part" ]; then partdone "$([ $bad = 0 ] && echo ok || echo "run $part failed")"
        elif [ $bad = 0 ]; then echo "ok   $c: $runs of $runs runs: menus and pointer respond"
        else echo "FAIL $c: $bad of $runs runs failed"; fails=$((fails + 1)); fi ;;
    winprompt)
        # the MS-DOS Prompt with w16's WIN.COM against 3.1's (the rest
        # Microsoft's): DSWAP's banner and text mode, back to Windows, exit
        scr=$root/tests/scripts/input/winprompt.txt
        # after WIN, WCB writes the BIOS data area: the text mode Windows
        # left (3.1: mode 3, 80 columns, 25 rows, 16-line font; w16's
        # WIN.COM once left 50 lines, the 8x8 font)
        printf 'SET TEMP=C:\\WINDOWS\\TEMP\r\nWIN /S\r\nC:\\WCB\r\n' > "$out/GOP.BAT"
        # w16's side beside the reference's (cached; their timing as before)
        set -- -c GOP -f "$out/GOP.BAT=WINDOWS/GOP.BAT" -f "$root/build/dos/WCB.COM=WCB.COM"
        INPUTRUN_STAY=1 "$root/tests/inputrun.sh" -b 30 -t 140 "$scr" "$out/winprompt-w16" -r -w "$@" \
            > "$out/winprompt-w16.txt" 2>&1 &
        refrun winprompt-ref winprompt env INPUTRUN_STAY=1 "$root/tests/inputrun.sh" -b 30 -t 140 "$scr" "$out/winprompt-ref" -r "$@"
        wait
        r=ok
        for v in ref w16; do
            m=$(python3 -c "import sys; d = open(sys.argv[1], 'rb').read(); print('mode %02X cols %d rows %d font %d' % (d[0x49], d[0x4A], d[0x84] + 1, d[0x85]))" \
                "$out/winprompt-$v/work/c/BDA.LOG" 2>/dev/null || echo none)
            eval "bda_$v=\"\$m\""
        done
        if [ "$bda_w16" != "mode 03 cols 80 rows 25 font 16" ] || [ "$bda_w16" != "$bda_ref" ]; then
            r="after Windows: $bda_w16 (3.1: $bda_ref)"
        fi
        for s in 01-dos 02-ver 03-back 04-exit; do
            [ "$r" = ok ] || break
            a=$out/winprompt-w16/$s.png b=$out/winprompt-ref/$s.png
            if [ ! -f "$a" ] || [ ! -f "$b" ]; then r="no $s screen"; break; fi
            n=$(magick compare -metric AE "$a" "$b" "$out/winprompt-$s-diff.png" 2>&1 >/dev/null || true)
            n=${n%%[. ]*}
            # the text cursor's blink: up to 16 pixels
            [ "${n:-0}" -le 16 ] || { r="$s: $n pixels differ ($out/winprompt-$s-diff.png)"; break; }
        done
        if [ "$r" = ok ]; then echo "ok   winprompt: the MS-DOS Prompt as with 3.1's WIN.COM"
        else echo "FAIL winprompt: $r"; fails=$((fails + 1)); fi ;;
    exitwin)
        scr=$root/tests/scripts/input/exitwin.txt
        refrun exitwin-ref exitwin env INPUTRUN_STAY=1 "$root/tests/inputrun.sh" -s -b 30 -t 190 "$scr" "$out/exitwin-ref" -r
        INPUTRUN_STAY=1 "$root/tests/inputrun.sh" -s -b 30 -t 190 "$scr" "$out/exitwin-w16" \
            -w -x -f "$user=WINDOWS/SYSTEM/USER.EXE" -f "$gdi=WINDOWS/SYSTEM/GDI.EXE" \
            -f "$shell=WINDOWS/SYSTEM/SHELL.DLL" -f "$root/build/dos/PROGMAN.EXE=WINDOWS/PROGMAN.EXE" \
            > "$out/exitwin-w16.txt" 2>&1 || true
        a=$out/exitwin-w16/02-dos.png b=$out/exitwin-ref/02-dos.png
        if [ -f "$a" ] && [ -f "$b" ]; then
            sz=$(magick identify -format '%wx%h' "$a" 2>/dev/null)
            n=$(magick compare -metric AE "$a" "$b" "$out/exitwin-diff.png" 2>&1 >/dev/null || true)
            n=${n%%[. ]*}
            if [ "$sz" = 640x400 ] && [ "${n:-99999}" -le 40 ]; then echo "ok   exitwin: back at the DOS prompt in text mode ($n pixels)"
            else echo "FAIL exitwin: screen $sz, $n pixels off Microsoft's ($out/exitwin-w16)"; fails=$((fails + 1)); fi
        else
            echo "FAIL exitwin: screens missing ($out/exitwin-w16)"; fails=$((fails + 1))
        fi ;;
    runfront|runfrontk)
        if [ $c = runfront ]; then set -- -f "$user=WINDOWS/SYSTEM/USER.EXE" -f "$gdi=WINDOWS/SYSTEM/GDI.EXE"
        else set --; fi
        "$root/tests/inputrun.sh" -s -b 25 -t 180 "$root/tests/scripts/input/runfront.txt" "$out/$c" "$@" \
            > "$out/$c.txt" 2>&1 || true
        # Notepad's caption (top left) the active colour, Program Manager's
        # (its right part, beyond Notepad) not
        r=$(python3 -c "
from PIL import Image
import sys
im = Image.open(sys.argv[1]).convert('RGB')
np, pm = im.getpixel((200, 8)), im.getpixel((500, 60))
navy = lambda p: p[0] < 40 and p[1] < 40 and p[2] > 100
print('ok' if navy(np) and not navy(pm) else 'notepad %s, progman %s' % (np, pm))
" "$out/$c/01-run.png" 2>&1 || echo "no screen")
        if [ "$r" = ok ]; then echo "ok   $c: Notepad in front and active"
        else echo "FAIL $c: $r ($out/$c)"; fails=$((fails + 1)); fi ;;
    tabs|alttab|helpmenus)
        refrun $c-ref $c "$root/tests/inputrun.sh" -s -b 25 -t 200 "$root/tests/scripts/input/$c.txt" "$out/$c-ref" -r
        "$root/tests/inputrun.sh" -e "$out/$c-ref/boot.png" -E "$out/$c-ref" -M "$([ $c != helpmenus ] && echo rectangle 0,0 0,0 || echo rectangle 14,42 20,61)" -b 25 -t 200 "$root/tests/scripts/input/$c.txt" "$out/$c-w16" -r \
            -f "$user=WINDOWS/SYSTEM/USER.EXE" -f "$gdi=WINDOWS/SYSTEM/GDI.EXE" > "$out/$c-w16.txt" 2>&1 || true
        r=ok
        for f in "$out/$c-ref"/[0-9]*.png; do
            s=$(basename "$f")
            [ -f "$out/$c-w16/$s" ] || { r="no $s"; break; }
            # Notepad's caret blinks on through its Help menu (helpmenus
            # 03): its place left out
            m="rectangle 0,0 0,0"
            [ "$c" = helpmenus ] && m="rectangle 14,42 20,61"
            magick "$f" -fill black -draw "$m" "$out/$c-a.png"
            magick "$out/$c-w16/$s" -fill black -draw "$m" "$out/$c-b.png"
            n=$(magick compare -metric AE "$out/$c-b.png" "$out/$c-a.png" "$out/$c-${s%.png}-diff.png" 2>&1 >/dev/null || true)
            n=${n%% *}
            [ "$n" = 0 ] || { r="$s: $n pixels differ"; break; }
        done
        ls "$out/$c-ref"/[0-9]*.png > /dev/null 2>&1 || [ "$r" != ok ] || r="no screens"
        if [ "$r" = ok ]; then echo "ok   $c: 0 pixels"
        else echo "FAIL $c: $r ($out)"; fails=$((fails + 1)); fi ;;
    write)
        s=$root/tests/scripts/input/write.txt
        cp "$root/.tools/ref/win31/cdrive/WINDOWS/WIN.INI" "$out/write-WIN.INI"
        python3 "$root/tests/scripts/iniset.py" "$out/write-WIN.INI" 'windows:run=WRITE.EXE'
        refrun write-ref write "$root/tests/inputrun.sh" -s -b 25 -t 210 "$s" "$out/write-ref" -r \
            -f "$out/write-WIN.INI=WINDOWS/WIN.INI"
        "$root/tests/inputrun.sh" -e "$out/write-ref/boot.png" -m "rectangle 27,46 28,61" -E "$out/write-ref" \
            -M "rectangle 27,46 28,61 rectangle 112,46 113,61 rectangle 112,82 113,97 rectangle 95,98 96,113 rectangle 213,280 276,311" -b 25 -t 210 "$s" "$out/write-w16" -w -x \
            -f "$user=WINDOWS/SYSTEM/USER.EXE" -f "$shell=WINDOWS/SYSTEM/SHELL.DLL" \
            -f "$woa=WINDOWS/SYSTEM/WINOLDAP.MOD" -f "$root/build/dos/WIN87EM.DLL=WINDOWS/SYSTEM/WIN87EM.DLL" \
            -f "$out/write-WIN.INI=WINDOWS/WIN.INI" > "$out/write-w16.txt" 2>&1 || true
        r=ok
        for f in "$out/write-ref"/[0-9]*.png; do
            s=$(basename "$f")
            [ -f "$out/write-w16/$s" ] || { r="no $s"; break; }
            # the caret's places (it blinks), the save box's Yes button
            case $s in
            01-*) m="rectangle 27,46 28,61" ;;
            02-*|03-*) m="rectangle 112,46 113,61" ;;
            04-*) m="rectangle 112,82 113,97" ;;
            05-*) m="rectangle 95,98 96,113" ;;
            06-*) m="rectangle 213,280 276,311" ;;
            *) m="rectangle 0,0 0,0" ;;
            esac
            magick "$f" -fill black -draw "$m" "$out/write-a.png"
            magick "$out/write-w16/$s" -fill black -draw "$m" "$out/write-b.png"
            n=$(magick compare -metric AE "$out/write-b.png" "$out/write-a.png" "$out/write-${s%.png}-diff.png" 2>&1 >/dev/null || true)
            n=${n%% *}
            [ "$n" = 0 ] || { r="$s: $n pixels differ"; break; }
        done
        ls "$out/write-ref"/[0-9]*.png > /dev/null 2>&1 || [ "$r" != ok ] || r="no screens"
        if [ "$r" = ok ]; then echo "ok   write: 0 pixels"
        else echo "FAIL write: $r ($out)"; fails=$((fails + 1)); fi ;;
    winfile)
        # File Manager (Microsoft's WINFILE.EXE): its drive bar, Ctrl+letter
        # to drives that are there and not; w16 USER+GDI against Microsoft's,
        # leaving out the tree's focus rectangle (its dots in the other
        # phase, UNVERIFIED why), the status bar's free space and byte total
        # (the scratch drives differ) and the "Drive A does not exist" box
        # (w16's own stop icon, and the box 25 pixels narrower: UNVERIFIED
        # 3.1's width rule for long captions). Then every w16 part: no
        # fault (it ended in KERNEL's lstrcat, back to DOS).
        s=$root/tests/scripts/input/winfile.txt
        r=ok
        if [ "${part:-cmp}" = cmp ]; then
        refrun winfile-ref winfile "$root/tests/inputrun.sh" -s -b 25 -t 210 "$s" "$out/winfile-ref" -r
        "$root/tests/inputrun.sh" -e "$out/winfile-ref/boot.png" -E "$out/winfile-ref" -M "rectangle 0,85 240,140 rectangle 0,455 639,479" -b 25 -t 210 "$s" "$out/winfile-w16" -r \
            -f "$user=WINDOWS/SYSTEM/USER.EXE" -f "$gdi=WINDOWS/SYSTEM/GDI.EXE" > "$out/winfile-w16.txt" 2>&1 || true
        r=ok
        for f in "$out/winfile-ref"/[0-9]*.png; do
            s2=$(basename "$f")
            [ "$s2" = 04-drive-a.png ] && continue
            [ -f "$out/winfile-w16/$s2" ] || { r="no $s2"; break; }
            m="rectangle 0,85 240,140 rectangle 0,455 639,479"
            magick "$f" -fill black -draw "$m" "$out/winfile-a.png"
            magick "$out/winfile-w16/$s2" -fill black -draw "$m" "$out/winfile-b.png"
            n=$(magick compare -metric AE "$out/winfile-b.png" "$out/winfile-a.png" "$out/winfile-${s2%.png}-diff.png" 2>&1 >/dev/null || true)
            n=${n%% *}
            [ "$n" = 0 ] || { r="$s2: $n pixels differ"; break; }
        done
        ls "$out/winfile-ref"/[0-9]*.png > /dev/null 2>&1 || [ "$r" != ok ] || r="no screens"
        fi
        # every w16 part (the part "all"; after the comparison when whole)
        if [ "$r" = ok ] && [ "${part:-all}" = all ]; then
            fon=$root/build/fonts
            INPUTRUN_STAY=1 "$root/tests/inputrun.sh" -s -b 30 -t 220 "$s" "$out/winfile-all" \
                -w -x -f "$user=WINDOWS/SYSTEM/USER.EXE" -f "$gdi=WINDOWS/SYSTEM/GDI.EXE" \
                -f "$root/build/dos/PROGMAN.EXE=WINDOWS/PROGMAN.EXE" -f "$shell=WINDOWS/SYSTEM/SHELL.DLL" \
                -f "$fon/VGASYS.FON=WINDOWS/SYSTEM/VGASYS.FON" -f "$fon/VGAFIX.FON=WINDOWS/SYSTEM/VGAFIX.FON" \
                -f "$fon/VGAOEM.FON=WINDOWS/SYSTEM/VGAOEM.FON" -f "$fon/SSERIFE.FON=WINDOWS/SYSTEM/SSERIFE.FON" \
                > "$out/winfile-all.txt" 2>&1 || true
            if grep -aq "exception" "$out/winfile-all/work/c/OUT.TXT" 2>/dev/null; then r="every w16 part: a fault ($out/winfile-all/work/c/OUT.TXT)"
            elif [ ! -f "$out/winfile-all/07-back-c.png" ]; then r="every w16 part: screens missing"; fi
        fi
        if [ -n "$part" ]; then partdone "$r"
        elif [ "$r" = ok ]; then echo "ok   winfile: 0 pixels (focus dots, status bar, error box left out); every w16 part: no fault"
        else echo "FAIL winfile: $r ($out)"; fails=$((fails + 1)); fi ;;
    runbrowse)
        # Program Manager's File > Run > Browse... (COMMDLG's Open box,
        # USER's DlgDirList and LB_DIR), into SYSTEM, Cancel, the Run box
        # cancelled (tests/scripts/input/runbrowse.txt). Part cmp: w16's
        # USER and GDI (Microsoft's COMMDLG and PROGMAN) against
        # all-Microsoft, every screen within its known differences. Part all: every w16 part
        # (COMMDLG, PROGMAN, SYSTEM.DRV, VGA.DRV ... too; its fonts and
        # art are its own, so no pixel comparison): no fault (it ended
        # Windows in LB_DIR, DOS FindFirst given the specification's
        # offset as its selector), every screen taken, the list changed
        # in SYSTEM, and Program Manager as it was after the boot
        s=$root/tests/scripts/input/runbrowse.txt
        r=ok
        if [ "${part:-cmp}" = cmp ]; then
            refrun runbrowse-ref runbrowse "$root/tests/inputrun.sh" -s -b 30 -t 200 "$s" "$out/runbrowse-ref" -r
            # the Run box's caret left out (it blinks; with the known
            # differences "matchnow" never finds the reference's phase)
            m="rectangle 114,160 119,178"
            "$root/tests/inputrun.sh" -s -E "$out/runbrowse-ref" -M "$m" -b 30 -t 200 "$s" "$out/runbrowse-w16" -r \
                -f "$user=WINDOWS/SYSTEM/USER.EXE" -f "$gdi=WINDOWS/SYSTEM/GDI.EXE" > "$out/runbrowse-w16.txt" 2>&1 || true
            # known differences, at most this many pixels per screen (each
            # its own w16 USER bug, UNVERIFIED rules, not yet fixed): the
            # Run box's disabled default OK button drawn with the default
            # border (3.1: none); in the Browse box the File Name edit's
            # selection, the Directories list box's height and the two
            # drop-down combo boxes (frame, height; Drives is owner-drawn,
            # 3.1 shows the drive's icon). Fewer is reported as better
            for sb in 01-run:226 02-browse:2427 03-system:2427 04-run:226 05-closed:0; do
                s2=${sb%:*}.png f=$out/runbrowse-ref/${sb%:*}.png
                [ -f "$f" ] || { r="reference screens missing"; break; }
                [ -f "$out/runbrowse-w16/$s2" ] || { r="no $s2"; break; }
                magick "$f" -fill black -draw "$m" "$out/runbrowse-a.png"
                magick "$out/runbrowse-w16/$s2" -fill black -draw "$m" "$out/runbrowse-b.png"
                n=$(magick compare -metric AE "$out/runbrowse-b.png" "$out/runbrowse-a.png" "$out/runbrowse-${s2%.png}-diff.png" 2>&1 >/dev/null || true)
                n=${n%%[. ]*}
                [ "${n:-99999}" -le "${sb#*:}" ] || { r="$s2: $n pixels differ (known: ${sb#*:})"; break; }
                if [ "$n" -lt "${sb#*:}" ]; then echo "     runbrowse: $s2 better than known: $n pixels (${sb#*:})"; fi
            done
        fi
        if [ "$r" = ok ] && [ "${part:-all}" = all ]; then
            b=$root/build/dos fon=$root/build/fonts
            INPUTRUN_STAY=1 "$root/tests/inputrun.sh" -s -b 40 -t 240 "$s" "$out/runbrowse-all" \
                -w -x -f "$user=WINDOWS/SYSTEM/USER.EXE" -f "$gdi=WINDOWS/SYSTEM/GDI.EXE" \
                -f "$shell=WINDOWS/SYSTEM/SHELL.DLL" -f "$b/PROGMAN.EXE=WINDOWS/PROGMAN.EXE" \
                -f "$b/COMMDLG.DLL=WINDOWS/SYSTEM/COMMDLG.DLL" -f "$b/WINOLDAP.MOD=WINDOWS/SYSTEM/WINOLDAP.MOD" \
                -f "$b/WIN87EM.DLL=WINDOWS/SYSTEM/WIN87EM.DLL" -f "$b/MOUSE.DRV=WINDOWS/SYSTEM/MOUSE.DRV" \
                -f "$b/KEYBOARD.DRV=WINDOWS/SYSTEM/KEYBOARD.DRV" -f "$b/SYSTEM.DRV=WINDOWS/SYSTEM/SYSTEM.DRV" \
                -f "$b/VGA.DRV=WINDOWS/SYSTEM/VGA.DRV" \
                -f "$fon/VGASYS.FON=WINDOWS/SYSTEM/VGASYS.FON" -f "$fon/VGAFIX.FON=WINDOWS/SYSTEM/VGAFIX.FON" \
                -f "$fon/VGAOEM.FON=WINDOWS/SYSTEM/VGAOEM.FON" -f "$fon/SSERIFE.FON=WINDOWS/SYSTEM/SSERIFE.FON" \
                > "$out/runbrowse-all.txt" 2>&1 || true
            a=$out/runbrowse-all
            if grep -aq "exception" "$a/work/c/OUT.TXT" 2>/dev/null; then r="every w16 part: a fault ($a/work/c/OUT.TXT)"
            elif [ ! -f "$a/05-closed.png" ] || [ ! -f "$a/03-system.png" ] || [ ! -f "$a/boot.png" ]; then r="every w16 part: screens missing ($a)"
            else
                n=$(magick compare -metric AE "$a/03-system.png" "$a/02-browse.png" null: 2>&1 || true); n=${n%%[. ]*}
                m=$(magick compare -metric AE "$a/05-closed.png" "$a/boot.png" "$out/runbrowse-all-diff.png" 2>&1 || true); m=${m%%[. ]*}
                if [ "${n:-0}" -lt 100 ]; then r="every w16 part: the Browse box did not change to SYSTEM ($a)"
                elif [ "$m" != 0 ]; then r="every w16 part: $m pixels differ from the boot's screen after the Run box ($a)"; fi
            fi
        fi
        if [ -n "$part" ]; then partdone "$r"
        elif [ "$r" = ok ]; then echo "ok   runbrowse: w16 USER and GDI within the known pixels; every w16 part: no fault, Browse into SYSTEM and back"
        else echo "FAIL runbrowse: $r ($out)"; fails=$((fails + 1)); fi ;;
    pmdrop)
        scr=$root/tests/scripts/input/pmdrop.txt
        refrun pmdrop-ref pmdrop "$root/tests/inputrun.sh" -s -b 40 -t 290 "$scr" "$out/pmdrop-ref" -r
        pk=${part:-pm sh us}     # the sessions this run makes
        case " $pk " in *" pm "*)
        "$root/tests/inputrun.sh" -s -E "$out/pmdrop-ref" -M "rectangle 60,310 300,342 rectangle 0,455 639,479" -b 40 -t 290 "$scr" "$out/pmdrop-pm" -r \
            -f "$root/build/dos/PROGMAN.EXE=WINDOWS/PROGMAN.EXE" > "$out/pmdrop-pm.txt" 2>&1 || true ;; esac
        case " $pk " in *" sh "*)
        "$root/tests/inputrun.sh" -s -E "$out/pmdrop-ref" -M "rectangle 60,310 300,342 rectangle 0,455 639,479" -b 40 -t 290 "$scr" "$out/pmdrop-sh" -r \
            -f "$root/build/dos/PROGMAN.EXE=WINDOWS/PROGMAN.EXE" -f "$shell=WINDOWS/SYSTEM/SHELL.DLL" \
            > "$out/pmdrop-sh.txt" 2>&1 || true ;; esac
        # w16's USER and GDI under Microsoft's File Manager and Program
        # Manager (DragObject, DumpIcon): the file's focus rectangle left
        # out, its dots in the other phase (GDI's pattern origin)
        case " $pk " in *" us "*)
        "$root/tests/inputrun.sh" -e "$out/pmdrop-ref/boot.png" -E "$out/pmdrop-ref" -M "rectangle 258,200 280,224" -b 40 -t 290 "$scr" "$out/pmdrop-us" -r \
            -f "$user=WINDOWS/SYSTEM/USER.EXE" -f "$gdi=WINDOWS/SYSTEM/GDI.EXE" > "$out/pmdrop-us.txt" 2>&1 || true ;; esac
        r=ok
        grp=work/c/WINDOWS/MAIN.GRP
        if ! grep -aq "C:.WINDOWS.APPS.HLP" "$out/pmdrop-ref/$grp" 2>/dev/null; then
            r="the reference did not add the item ($out/pmdrop-ref)"
        fi
        for k in $pk; do
            [ "$r" = ok ] || break
            cmp -s "$out/pmdrop-ref/$grp" "$out/pmdrop-$k/$grp" || { r="$k: MAIN.GRP differs"; break; }
            for s in 04-dragging 05-dropped; do
                [ -f "$out/pmdrop-$k/$s.png" ] || { r="$k: no $s"; break; }
                m="rectangle 0,0 0,0"
                [ $k = us ] && m="rectangle 258,200 280,224"
                magick "$out/pmdrop-$k/$s.png" -fill black -draw "$m" "$out/pmdrop-$k-a.png"
                magick "$out/pmdrop-ref/$s.png" -fill black -draw "$m" "$out/pmdrop-$k-b.png"
                n=$(magick compare -metric AE "$out/pmdrop-$k-a.png" "$out/pmdrop-$k-b.png" "$out/pmdrop-$k-$s-diff.png" 2>&1 >/dev/null || true)
                n=${n%% *}
                [ "$n" = 0 ] || { r="$k: $s: $n pixels differ"; break; }
            done
        done
        if [ -n "$part" ]; then partdone "$r"
        elif [ "$r" = ok ]; then echo "ok   pmdrop: 0 pixels, MAIN.GRP identical (w16 PROGMAN; w16 PROGMAN and SHELL; w16 USER and GDI)"
        else echo "FAIL pmdrop: $r"; fails=$((fails + 1)); fi ;;
    cardfile)
        s=$root/tests/scripts/input/cardfile.txt
        cp "$root/.tools/ref/win31/cdrive/WINDOWS/WIN.INI" "$out/cardfile-WIN.INI"
        python3 "$root/tests/scripts/iniset.py" "$out/cardfile-WIN.INI" 'windows:run=CARDFILE.EXE'
        refrun cardfile-ref cardfile "$root/tests/inputrun.sh" -s -b 25 -t 210 "$s" "$out/cardfile-ref" -r \
            -f "$out/cardfile-WIN.INI=WINDOWS/WIN.INI"
        "$root/tests/inputrun.sh" -e "$out/cardfile-ref/boot.png" -E "$out/cardfile-ref" -b 25 -t 210 \
            "$s" "$out/cardfile-w16" -r -f "$out/cardfile-WIN.INI=WINDOWS/WIN.INI" \
            -f "$root/build/dos/CARDFILE.EXE=WINDOWS/CARDFILE.EXE" > "$out/cardfile-w16.txt" 2>&1 || true
        r=ok
        for f in "$out/cardfile-ref"/[0-9]*.png; do
            s=$(basename "$f")
            [ -f "$out/cardfile-w16/$s" ] || { r="no $s"; break; }
            n=$(magick compare -metric AE "$out/cardfile-w16/$s" "$f" "$out/cardfile-${s%.png}-diff.png" 2>&1 >/dev/null || true)
            n=${n%% *}
            [ "$n" = 0 ] || { r="$s: $n pixels differ"; break; }
        done
        ls "$out/cardfile-ref"/[0-9]*.png > /dev/null 2>&1 || [ "$r" != ok ] || r="no screens"
        if [ "$r" = ok ]; then echo "ok   cardfile: 0 pixels"
        else echo "FAIL cardfile: $r ($out)"; fails=$((fails + 1)); fi ;;
    pmclose)
        # Program Manager's system box double-clicked: its Exit dialog in
        # front, active and system modal (a click on Program Manager does
        # nothing), no menu left behind; Esc closes it. w16 USER+GDI
        # (Microsoft's Program Manager), and w16's Program Manager on
        # them, against all-Microsoft; the dialog's inside left out (w16's
        # dialog is a pixel to the right, UNVERIFIED why), and w16's own
        # group-icon pictures; the dialog's caption must be there
        scr=$root/tests/scripts/input/pmclose.txt
        refrun pmclose-ref pmclose "$root/tests/inputrun.sh" -s -b 40 -t 190 "$scr" "$out/pmclose-ref" -r
        "$root/tests/inputrun.sh" -e "$out/pmclose-ref/boot.png" -E "$out/pmclose-ref" -M "rectangle 168,182 472,300" -b 40 -t 190 "$scr" "$out/pmclose-us" -r \
            -f "$user=WINDOWS/SYSTEM/USER.EXE" -f "$gdi=WINDOWS/SYSTEM/GDI.EXE" > "$out/pmclose-us.txt" 2>&1 || true
        "$root/tests/inputrun.sh" -s -E "$out/pmclose-ref" -M "rectangle 168,182 472,300 rectangle 60,310 300,342" -b 40 -t 190 "$scr" "$out/pmclose-pm" -r \
            -f "$user=WINDOWS/SYSTEM/USER.EXE" -f "$gdi=WINDOWS/SYSTEM/GDI.EXE" \
            -f "$root/build/dos/PROGMAN.EXE=WINDOWS/PROGMAN.EXE" > "$out/pmclose-pm.txt" 2>&1 || true
        r=ok
        for k in us pm; do
            for s in 01-dialog 02-clicked 03-cancelled; do
                [ -f "$out/pmclose-$k/$s.png" ] && [ -f "$out/pmclose-ref/$s.png" ] || { r="$k: no $s"; break; }
                m="rectangle 0,0 0,0"
                [ $s != 03-cancelled ] && m="rectangle 168,182 472,300"
                [ $k = pm ] && m="$m rectangle 60,310 300,342"
                magick "$out/pmclose-ref/$s.png" -fill black -draw "$m" "$out/pmclose-a.png"
                magick "$out/pmclose-$k/$s.png" -fill black -draw "$m" "$out/pmclose-b.png"
                n=$(magick compare -metric AE "$out/pmclose-b.png" "$out/pmclose-a.png" "$out/pmclose-$k-$s-diff.png" 2>&1 >/dev/null || true)
                n=${n%% *}
                [ "$n" = 0 ] || { r="$k: $s: $n pixels differ"; break; }
            done
            [ "$r" = ok ] || break
            # the dialog's active caption, 250 pixels or more of it
            c=$(python3 -c "
import sys
from PIL import Image
im = Image.open(sys.argv[1]).convert('RGB')
print(max(sum(1 for x in range(160, 480) if im.getpixel((x, y)) == (0, 0, 170)) for y in range(180, 200)))
" "$out/pmclose-$k/02-clicked.png" 2>/dev/null || echo 0)
            [ "${c:-0}" -ge 250 ] || { r="$k: no dialog in front after the click"; break; }
        done
        if [ "$r" = ok ]; then echo "ok   pmclose: Exit dialog in front and modal, no menu left (w16 USER+GDI; and w16 PROGMAN)"
        else echo "FAIL pmclose: $r ($out)"; fails=$((fails + 1)); fi ;;
    woados|woadosbat)
        scr=$root/tests/scripts/input/woados.txt
        fon=$root/build/fonts
        wc=                             # woadosbat: w16's WIN.COM as well
        if [ $c = woados ]; then set -- -i 'NonWindowsApp:SwapDisk=C:\WINDOWS\TEMP'
        else
            wc=-w
            printf 'SET TEMP=C:\\WINDOWS\\TEMP\r\nWIN /S\r\n' > "$out/GO.BAT"
            set -- -c GO -f "$out/GO.BAT=WINDOWS/GO.BAT"
        fi
        refrun $c-ref $c env INPUTRUN_STAY=1 "$root/tests/inputrun.sh" -b 30 -t 130 "$scr" "$out/$c-ref" -r "$@"
        INPUTRUN_STAY=1 "$root/tests/inputrun.sh" -b 30 -t 130 "$scr" "$out/$c-w16" \
            -x $wc "$@" \
            -f "$user=WINDOWS/SYSTEM/USER.EXE" -f "$gdi=WINDOWS/SYSTEM/GDI.EXE" \
            -f "$shell=WINDOWS/SYSTEM/SHELL.DLL" -f "$root/build/dos/PROGMAN.EXE=WINDOWS/PROGMAN.EXE" \
            -f "$woa=WINDOWS/SYSTEM/WINOLDAP.MOD" \
            -f "$fon/VGASYS.FON=WINDOWS/SYSTEM/VGASYS.FON" -f "$fon/VGAFIX.FON=WINDOWS/SYSTEM/VGAFIX.FON" \
            -f "$fon/VGAOEM.FON=WINDOWS/SYSTEM/VGAOEM.FON" -f "$fon/SSERIFE.FON=WINDOWS/SYSTEM/SSERIFE.FON" \
            > "$out/$c-w16.txt" 2>&1 || true
        a=$out/$c-w16 b=$out/$c-ref r=ok
        for s in 01-dos:640x400 02-ver:640x400 03-back:640x480 04-exit:640x400; do
            f=${s%%:*} want=${s#*:}
            [ -f "$a/$f.png" ] && [ -f "$b/$f.png" ] || { r="no $f"; break; }
            sz=$(magick identify -format '%wx%h' "$a/$f.png" 2>/dev/null)
            [ "$sz" = "$want" ] || { r="$f: screen $sz, not $want"; break; }
            if [ $f = 03-back ]; then       # Windows as it was before the prompt
                n=$(magick compare -metric AE "$a/$f.png" "$a/00-before.png" "$out/$c-$f-diff.png" 2>&1 >/dev/null || true)
            else
                n=$(magick compare -metric AE "$a/$f.png" "$b/$f.png" "$out/$c-$f-diff.png" 2>&1 >/dev/null || true)
            fi
            n=${n%%[. ]*}
            [ "${n:-99999}" -le 40 ] || { r="$f: $n pixels differ"; break; }
        done
        if [ $c = woadosbat ]; then
            if [ "$r" = ok ]; then echo "ok   woadosbat: every w16 part, TEMP from a batch file: the prompt and back, text mode after Exit Windows"
            else echo "FAIL woadosbat: $r ($out/$c-w16)"; fails=$((fails + 1)); fi
        elif [ "$r" = ok ]; then echo "ok   woados: the MS-DOS Prompt in text mode and back, text mode after Exit Windows"
        else echo "FAIL woados: $r ($out/woados-w16)"; fails=$((fails + 1)); fi ;;
    pmkeys)
        scr=$root/tests/scripts/input/pmkeys.txt
        refrun pmkeys-ref pmkeys "$root/tests/inputrun.sh" -s -b 40 -t 180 "$scr" "$out/pmkeys-ref" -r
        "$root/tests/inputrun.sh" -e "$out/pmkeys-ref/boot.png" -E "$out/pmkeys-ref" -b 40 -t 180 "$scr" "$out/pmkeys-us" -r \
            -f "$user=WINDOWS/SYSTEM/USER.EXE" -f "$gdi=WINDOWS/SYSTEM/GDI.EXE" > "$out/pmkeys-us.txt" 2>&1 || true
        "$root/tests/inputrun.sh" -s -E "$out/pmkeys-ref" -M "rectangle 60,310 300,342" -b 40 -t 180 "$scr" "$out/pmkeys-pm" -r \
            -f "$user=WINDOWS/SYSTEM/USER.EXE" -f "$gdi=WINDOWS/SYSTEM/GDI.EXE" \
            -f "$root/build/dos/PROGMAN.EXE=WINDOWS/PROGMAN.EXE" > "$out/pmkeys-pm.txt" 2>&1 || true
        r=ok
        for k in us pm; do
            for s in 01-keys 02-click; do
                [ -f "$out/pmkeys-$k/$s.png" ] && [ -f "$out/pmkeys-ref/$s.png" ] || { r="$k: no $s"; break; }
                m="rectangle 0,0 0,0"
                [ $k = pm ] && m="rectangle 60,310 300,342"
                magick "$out/pmkeys-ref/$s.png" -fill black -draw "$m" "$out/pmkeys-a.png"
                magick "$out/pmkeys-$k/$s.png" -fill black -draw "$m" "$out/pmkeys-b.png"
                n=$(magick compare -metric AE "$out/pmkeys-b.png" "$out/pmkeys-a.png" "$out/pmkeys-$k-$s-diff.png" 2>&1 >/dev/null || true)
                n=${n%% *}
                [ "$n" = 0 ] || { r="$k: $s: $n pixels differ"; break; }
            done
            [ "$r" = ok ] || break
        done
        if [ "$r" = ok ]; then echo "ok   pmkeys: arrow keys move Program Manager's selection from the start (w16 USER+GDI; and w16 PROGMAN)"
        else echo "FAIL pmkeys: $r ($out)"; fails=$((fails + 1)); fi ;;
    mdimax)
        # a maximised MDI child's controls in the frame's menu bar (user/mdi.c):
        # probe US18 (tests/apps/us18.c) as the shell on Windows 3.1 and on
        # w16's USER, the OBS lines of the two logs identical; then real
        # input in Program Manager (tests/scripts/input/mdimax.txt) on w16's
        # USER and GDI, with Microsoft's PROGMAN pixel for pixel and with
        # w16's (its group icons left out), against all-Microsoft
        us18() {   # LABEL dropin.sh options...: US18's run, its log in LABEL.log
            _l=$1; shift
            "$root/tests/dropin.sh" -t 150 -s "$out/$_l.png" -o "$out/$_l" \
                -f "$root/build/dos/US18.EXE=US18.EXE" -i 'boot:shell=C:\US18.EXE' "$@" \
                > "$out/$_l.txt" 2>&1 || true
            for _d in "$out/$_l/c/WINDOWS" "$out/$_l/c"; do
                [ -f "$_d/TK.LOG" ] && { tr -d '\r' < "$_d/TK.LOG" > "$out/$_l.log"; break; }
            done
        }
        us18 mdimax-us18ref -r
        us18 mdimax-us18 -r -f "$user=WINDOWS/SYSTEM/USER.EXE"
        # and marked for 3.10 (US18A: what differs for such programs)
        us18a() {
            _l=$1; shift
            "$root/tests/dropin.sh" -t 150 -s "$out/$_l.png" -o "$out/$_l" \
                -f "$root/build/dos/US18A.EXE=US18.EXE" -i 'boot:shell=C:\US18.EXE' "$@" \
                > "$out/$_l.txt" 2>&1 || true
            for _d in "$out/$_l/c/WINDOWS" "$out/$_l/c"; do
                [ -f "$_d/TK.LOG" ] && { tr -d '\r' < "$_d/TK.LOG" > "$out/$_l.log"; break; }
            done
        }
        us18a mdimax-us18aref -r
        us18a mdimax-us18a -r -f "$user=WINDOWS/SYSTEM/USER.EXE"
        scr=$root/tests/scripts/input/mdimax.txt
        refrun mdimax-ref mdimax "$root/tests/inputrun.sh" -s -b 40 -t 240 "$scr" "$out/mdimax-ref" -r
        "$root/tests/inputrun.sh" -e "$out/mdimax-ref/boot.png" -E "$out/mdimax-ref" -b 40 -t 240 "$scr" "$out/mdimax-us" -r \
            -f "$user=WINDOWS/SYSTEM/USER.EXE" -f "$gdi=WINDOWS/SYSTEM/GDI.EXE" > "$out/mdimax-us.txt" 2>&1 || true
        "$root/tests/inputrun.sh" -s -E "$out/mdimax-ref" -M "rectangle 60,310 300,342" -b 40 -t 240 "$scr" "$out/mdimax-pm" -r \
            -f "$user=WINDOWS/SYSTEM/USER.EXE" -f "$gdi=WINDOWS/SYSTEM/GDI.EXE" \
            -f "$root/build/dos/PROGMAN.EXE=WINDOWS/PROGMAN.EXE" > "$out/mdimax-pm.txt" 2>&1 || true
        r=ok
        for k in us18ref us18 us18aref us18a; do
            f=$out/mdimax-$k.log
            if [ ! -f "$f" ] || ! grep -q '^US18 end' "$f"; then r="$k: US18 did not finish ($f)"; break; fi
        done
        for k in us18 us18a; do
            [ "$r" = ok ] || break
            grep '^OBS [^~]' "$out/mdimax-${k}ref.log" > "$out/mdimax-${k}ref.obs"
            grep '^OBS [^~]' "$out/mdimax-$k.log" > "$out/mdimax-$k.obs"
            cmp -s "$out/mdimax-${k}ref.obs" "$out/mdimax-$k.obs" ||
                r="$k: $(diff "$out/mdimax-${k}ref.obs" "$out/mdimax-$k.obs" | grep -c '^<') observation(s) differ from 3.1's ($out/mdimax-${k}ref.obs, mdimax-$k.obs)"
        done
        for k in us pm; do
            [ "$r" = ok ] || break
            ls "$out/mdimax-ref"/[0-9]*.png > /dev/null 2>&1 || { r="no reference screens"; break; }
            for f in "$out/mdimax-ref"/[0-9]*.png; do
                s=$(basename "$f")
                [ -f "$out/mdimax-$k/$s" ] || { r="$k: no $s"; break; }
                m="rectangle 0,0 0,0"
                [ $k = pm ] && m="rectangle 60,310 300,342"
                magick "$f" -fill black -draw "$m" "$out/mdimax-a.png"
                magick "$out/mdimax-$k/$s" -fill black -draw "$m" "$out/mdimax-b.png"
                n=$(magick compare -metric AE "$out/mdimax-b.png" "$out/mdimax-a.png" "$out/mdimax-$k-${s%.png}-diff.png" 2>&1 >/dev/null || true)
                n=${n%% *}
                [ "$n" = 0 ] || { r="$k: $s: $n pixels differ"; break; }
            done
        done
        if [ "$r" = ok ]; then echo "ok   mdimax: US18 as 3.1; maximised groups' controls in the bar, 0 pixels (w16 USER+GDI; and w16 PROGMAN)"
        else echo "FAIL mdimax: $r ($out)"; fails=$((fails + 1)); fi ;;
    pmitems)
        # an item dragged within Program Manager's Main group, then the
        # minimised Accessories group restored (in front, active):
        # Microsoft's Program Manager on w16's USER and GDI pixel for
        # pixel; w16's Program Manager on them with its own group-icon
        # pictures left out (the row of icons at the bottom); MAIN.GRP
        # identical to all-Microsoft's in both
        scr=$root/tests/scripts/input/pmitems.txt
        refrun pmitems-ref pmitems "$root/tests/inputrun.sh" -s -b 40 -t 240 "$scr" "$out/pmitems-ref" -r
        "$root/tests/inputrun.sh" -e "$out/pmitems-ref/boot.png" -E "$out/pmitems-ref" -b 40 -t 240 "$scr" "$out/pmitems-us" -r \
            -f "$user=WINDOWS/SYSTEM/USER.EXE" -f "$gdi=WINDOWS/SYSTEM/GDI.EXE" > "$out/pmitems-us.txt" 2>&1 || true
        "$root/tests/inputrun.sh" -s -E "$out/pmitems-ref" -M "rectangle 60,310 300,342" -b 40 -t 240 "$scr" "$out/pmitems-pm" -r \
            -f "$user=WINDOWS/SYSTEM/USER.EXE" -f "$gdi=WINDOWS/SYSTEM/GDI.EXE" \
            -f "$root/build/dos/PROGMAN.EXE=WINDOWS/PROGMAN.EXE" > "$out/pmitems-pm.txt" 2>&1 || true
        r=ok
        grp=work/c/WINDOWS/MAIN.GRP
        cmp -s "$root/.tools/ref/win31/cdrive/WINDOWS/MAIN.GRP" "$out/pmitems-ref/$grp" && r="the reference did not move the item"
        for k in us pm; do
            [ "$r" = ok ] || break
            cmp -s "$out/pmitems-ref/$grp" "$out/pmitems-$k/$grp" || { r="$k: MAIN.GRP differs"; break; }
            for f in "$out/pmitems-ref"/[0-9]*.png; do
                s=$(basename "$f")
                [ -f "$out/pmitems-$k/$s" ] || { r="$k: no $s"; break; }
                m="rectangle 0,0 0,0"
                [ $k = pm ] && m="rectangle 60,310 300,342"
                magick "$f" -fill black -draw "$m" "$out/pmitems-a.png"
                magick "$out/pmitems-$k/$s" -fill black -draw "$m" "$out/pmitems-b.png"
                n=$(magick compare -metric AE "$out/pmitems-b.png" "$out/pmitems-a.png" "$out/pmitems-$k-${s%.png}-diff.png" 2>&1 >/dev/null || true)
                n=${n%% *}
                [ "$n" = 0 ] || { r="$k: $s: $n pixels differ"; break; }
            done
        done
        if [ "$r" = ok ]; then echo "ok   pmitems: item moved, group restored in front; MAIN.GRP identical (w16 USER+GDI; and w16 PROGMAN)"
        else echo "FAIL pmitems: $r ($out)"; fails=$((fails + 1)); fi ;;
    w16groups)
        # w16's install tree alone, with its default groups; Microsoft's
        # Program Manager and w16's on it (not cached: the tree is w16's)
        scr=$root/tests/scripts/input/w16groups.txt
        rsys=$root/.tools/ref/win31/cdrive/WINDOWS/SYSTEM
        m="rectangle 60,310 300,342"
        set -- -T "$root/build/w16" -f "$rsys/WSWAP.EXE=W16/SYSTEM/WSWAP.EXE" \
            -f "$rsys/MMSOUND.DRV=W16/SYSTEM/MMSOUND.DRV" -i boot:sound.drv=mmsound.drv
        "$root/tests/inputrun.sh" -s -b 40 -t 240 "$scr" "$out/w16groups-ms" "$@" \
            -f "$rsys/../PROGMAN.EXE=W16/PROGMAN.EXE" > "$out/w16groups-ms.txt" 2>&1 || true
        "$root/tests/inputrun.sh" -e "$out/w16groups-ms/boot.png" -m "$m" -E "$out/w16groups-ms" -M "$m" \
            -b 40 -t 240 "$scr" "$out/w16groups-pm" "$@" > "$out/w16groups-pm.txt" 2>&1 || true
        r=ok
        for k in ms pm; do
            d=$out/w16groups-$k
            grep -q '^script done' "$d/input.log" 2>/dev/null || { r="$k: the script did not run to its end"; break; }
            ! grep -q '^not moved' "$d/input.log" || { r="$k: a program did not come up or go ($d/input.log)"; break; }
        done
        # each step changed the screen (the reference: Microsoft's Program
        # Manager started every item of w16's groups)
        prev=boot
        for s in 01-readme 02-readme-notepad 03-back 04-accessories 05-notepad 06-back 07-calculator 08-back; do
            [ "$r" = ok ] || break
            [ -f "$out/w16groups-ms/$s.png" ] || { r="ms: no $s.png"; break; }
            case $s in 0[2457]*)
                n=$(magick compare -metric AE "$out/w16groups-ms/$prev.png" "$out/w16groups-ms/$s.png" null: 2>&1 || true)
                [ "${n%% *}" != 0 ] || r="ms: $s is the screen before it" ;;
            esac
            prev=$s
        done
        for f in "$out/w16groups-ms"/boot.png "$out/w16groups-ms"/[0-9]*.png; do
            [ "$r" = ok ] || break
            s=$(basename "$f")
            [ -f "$out/w16groups-pm/$s" ] || { r="pm: no $s"; break; }
            magick "$f" -fill black -draw "$m" "$out/w16groups-a.png"
            magick "$out/w16groups-pm/$s" -fill black -draw "$m" "$out/w16groups-b.png"
            n=$(magick compare -metric AE "$out/w16groups-b.png" "$out/w16groups-a.png" "$out/w16groups-${s%.png}-diff.png" 2>&1 >/dev/null || true)
            n=${n%% *}
            [ "$n" = 0 ] || r="pm: $s: $n pixels differ"
        done
        for f in MAIN.GRP ACCESSOR.GRP STARTUP.GRP PROGMAN.INI; do
            [ "$r" = ok ] || break
            cmp -s "$out/w16groups-ms/work/c/W16/$f" "$out/w16groups-pm/work/c/W16/$f" || r="$f saved differently"
        done
        if [ "$r" = ok ]; then echo "ok   w16groups: w16's tree alone, its groups read and run by Microsoft's and w16's Program Manager, 0 pixels, the same files saved"
        else echo "FAIL w16groups: $r ($out)"; fails=$((fails + 1)); fi ;;
    icons)
        # Clock's icon shows the time (and its digits are GDI's font
        # matching): left out of the comparison
        refrun icons-ref icons "$root/tests/inputrun.sh" -s -b 25 -t 380 "$root/tests/scripts/input/icons.txt" "$out/icons-ref" -r
        "$root/tests/inputrun.sh" -e "$out/icons-ref/boot.png" -E "$out/icons-ref" -M "rectangle 160,400 220,445" -b 25 -t 380 "$root/tests/scripts/input/icons.txt" "$out/icons-w16" -r \
            -f "$user=WINDOWS/SYSTEM/USER.EXE" -f "$gdi=WINDOWS/SYSTEM/GDI.EXE" > "$out/icons-w16.txt" 2>&1 || true
        r=ok
        for f in "$out/icons-ref"/[0-9]*.png; do
            s=$(basename "$f")
            [ -f "$out/icons-w16/$s" ] || { r="no $s"; break; }
            magick "$f" -fill black -draw "rectangle 160,400 220,445" "$out/icons-a.png"
            magick "$out/icons-w16/$s" -fill black -draw "rectangle 160,400 220,445" "$out/icons-b.png"
            n=$(magick compare -metric AE "$out/icons-b.png" "$out/icons-a.png" "$out/icons-${s%.png}-diff.png" 2>&1 >/dev/null || true)
            n=${n%% *}
            [ "$n" = 0 ] || { r="$s: $n pixels differ"; break; }
        done
        ls "$out/icons-ref"/[0-9]*.png > /dev/null 2>&1 || [ "$r" != ok ] || r="no screens"
        if [ "$r" = ok ]; then echo "ok   icons: 0 pixels (Clock's icon left out)"
        else echo "FAIL icons: $r ($out)"; fails=$((fails + 1)); fi ;;
    about)
        "$root/tests/inputrun.sh" -s -b 25 -t 200 "$root/tests/scripts/input/about.txt" "$out/about" \
            -f "$user=WINDOWS/SYSTEM/USER.EXE" -f "$gdi=WINDOWS/SYSTEM/GDI.EXE" \
            -f "$shell=WINDOWS/SYSTEM/SHELL.DLL" -f "$root/build/dos/PROGMAN.EXE=WINDOWS/PROGMAN.EXE" \
            > "$out/about.txt" 2>&1 || true
        d=$out/about
        if [ -f "$d/02-closed-enter.png" ] && [ -f "$d/03-menu.png" ] && [ -f "$d/04-after-esc.png" ]; then
            a=$(magick compare -metric AE "$d/02-closed-enter.png" "$d/03-menu.png" /dev/null 2>&1 >/dev/null || true)
            b=$(magick compare -metric AE "$d/02-closed-enter.png" "$d/04-after-esc.png" /dev/null 2>&1 >/dev/null || true)
            a=${a%%[. ]*} b=${b%%[. ]*}
            if [ "${a:-0}" -gt 1000 ] && [ "${b:-0}" -gt 1000 ]; then echo "ok   about: menus respond after the About box"
            else echo "FAIL about: menus did not open after the About box (File $a, Help $b pixels; $d)"; fails=$((fails + 1)); fi
        else
            echo "FAIL about: screens missing ($d)"; fails=$((fails + 1))
        fi ;;
    dragoff|dragoffk)
        # real input: Program Manager dragged by its caption partly off the
        # right, left and bottom edges (tests/scripts/input/dragoff.txt);
        # w16's USER and GDI (dragoffk: on w16's KRNL286 too) against
        # all-Microsoft; every screen must be identical
        scr=$root/tests/scripts/input/dragoff.txt
        refrun dragoff-ref dragoff "$root/tests/inputrun.sh" -s -b 25 -t 190 "$scr" "$out/dragoff-ref" -r
        if [ $c = dragoff ]; then k=-r; else k=; fi
        "$root/tests/inputrun.sh" -e "$out/dragoff-ref/boot.png" -E "$out/dragoff-ref" -b 25 -t 190 "$scr" "$out/$c-w16" $k \
            -f "$user=WINDOWS/SYSTEM/USER.EXE" -f "$gdi=WINDOWS/SYSTEM/GDI.EXE" > "$out/$c-w16.txt" 2>&1 || true
        r=ok
        for s in 01-right-moving 02-right 03-left-moving 04-left 05-bottom-moving 06-bottom; do
            if [ ! -f "$out/$c-w16/$s.png" ] || [ ! -f "$out/dragoff-ref/$s.png" ]; then
                r="no $s screen"; break
            fi
            n=$(magick compare -metric AE "$out/$c-w16/$s.png" "$out/dragoff-ref/$s.png" "$out/$c-$s-diff.png" 2>&1 >/dev/null || true)
            n=${n%% *}
            [ "$n" = 0 ] || { r="$s: $n pixels differ ($out/$c-$s-diff.png)"; break; }
        done
        if [ "$r" = ok ]; then echo "ok   $c: 0 pixels"
        else echo "FAIL $c: $r"; fails=$((fails + 1)); fi ;;
    input)
        scr=$root/tests/scripts/input/bootinput.txt
        refrun input-ref input "$root/tests/inputrun.sh" -b 1 -t 270 "$scr" "$out/input-ref" -r
        # (ScrollBarMenu=0: its right click lands wherever the boot left
        # the pointer; 3.1 has no scroll-bar menu)
        "$root/tests/inputrun.sh" -E "$out/input-ref" -b 1 -t 270 "$scr" "$out/input-w16" \
            -f "$user=WINDOWS/SYSTEM/USER.EXE" -f "$gdi=WINDOWS/SYSTEM/GDI.EXE" \
            -i w16:ScrollBarMenu=0 > "$out/input-w16.txt" 2>&1 || true
        r=ok
        for s in 01-booted 02-menu 03-closed; do
            if [ ! -f "$out/input-w16/$s.png" ] || [ ! -f "$out/input-ref/$s.png" ]; then
                r="no $s screen"; break
            fi
            n=$(magick compare -metric AE "$out/input-w16/$s.png" "$out/input-ref/$s.png" "$out/input-$s-diff.png" 2>&1 >/dev/null || true)
            n=${n%% *}
            [ "$n" = 0 ] || { r="$s: $n pixels differ ($out/input-$s-diff.png)"; break; }
        done
        if [ "$r" = ok ]; then echo "ok   input: 0 pixels"
        else echo "FAIL input: $r"; fails=$((fails + 1)); fi ;;
    sbmenu)
        # US17 as the shell (tests/apps/us17.c); its log is TK.LOG in
        # C:\WINDOWS (the shell's directory) or C:\
        sbm() {   # LABEL dropin.sh options...: one run, its log in LABEL.log
            _l=$1; shift
            "$root/tests/dropin.sh" -t 150 -s "$out/$_l.png" -o "$out/$_l" \
                -f "$root/build/dos/US17.EXE=US17.EXE" -i 'boot:shell=C:\US17.EXE' "$@" \
                > "$out/$_l.txt" 2>&1 || true
            for _d in "$out/$_l/c/WINDOWS" "$out/$_l/c"; do
                [ -f "$_d/TK.LOG" ] && { tr -d '\r' < "$_d/TK.LOG" > "$out/$_l.log"; break; }
            done
        }
        refrun sbmenu-ref sbmenu sh -c '"$1" -t 150 -s "$2/sbmenu-ref.png" -o "$2/sbmenu-ref" -r \
            -f "$3=US17.EXE" -i "boot:shell=C:\US17.EXE" -i w16:ScrollBarMenu=0' sh \
            "$root/tests/dropin.sh" "$out" "$root/build/dos/US17.EXE"
        for _d in "$out/sbmenu-ref/c/WINDOWS" "$out/sbmenu-ref/c"; do
            [ -f "$_d/TK.LOG" ] && { tr -d '\r' < "$_d/TK.LOG" > "$out/sbmenu-ref.log"; break; }
        done
        sbm sbmenu-stock -r -f "$user=WINDOWS/SYSTEM/USER.EXE" -i w16:ScrollBarMenu=0 -e "$out/sbmenu-ref.png"
        sbm sbmenu-on -r -f "$user=WINDOWS/SYSTEM/USER.EXE" -i w16:ScrollBarMenu=1
        r=ok
        for k in ref stock on; do
            f=$out/sbmenu-$k.log
            if [ ! -f "$f" ] || ! grep -q '^US17 end' "$f"; then r="$k: US17 did not finish ($f)"; break; fi
            if grep -q '^FAIL' "$f"; then r="$k: $(grep -c '^FAIL' "$f") check(s) failed ($f)"; break; fi
        done
        if [ "$r" = ok ]; then
            grep '^OBS [^~]' "$out/sbmenu-ref.log" > "$out/sbmenu-ref.obs"
            grep '^OBS [^~]' "$out/sbmenu-stock.log" > "$out/sbmenu-stock.obs"
            if ! cmp -s "$out/sbmenu-ref.obs" "$out/sbmenu-stock.obs"; then
                r="ScrollBarMenu=0: the messages differ from 3.1's ($out/sbmenu-ref.obs, sbmenu-stock.obs)"
            else
                n=$(magick compare -metric AE "$out/sbmenu-stock.png" "$out/sbmenu-ref.png" "$out/sbmenu-diff.png" 2>&1 >/dev/null || true)
                n=${n%% *}
                [ "$n" = 0 ] || r="ScrollBarMenu=0: the screen differs from 3.1's by $n pixels ($out/sbmenu-diff.png)"
            fi
        fi
        if [ "$r" = ok ]; then echo "ok   sbmenu: the menu's $(grep -c '^PASS' "$out/sbmenu-on.log") checks pass; ScrollBarMenu=0 as 3.1 (log and screen)"
        else echo "FAIL sbmenu: $r"; fails=$((fails + 1)); fi ;;
    capcursor)
        # the cursor after a capture ends (user/input.c ReleaseCapture,
        # user/defwnd.c WM_SETCURSOR): probe US22 as the shell, the mouse
        # captured, moved and released still over a sizing border, a
        # corner and the client area (tests/scripts/input/capcursor.txt);
        # w16's USER and GDI against all-Microsoft: every screen
        # identical (the cursor's shape after the release), and US22's
        # log of mouse messages and WM_SETCURSOR from the first press on
        scr=$root/tests/scripts/input/capcursor.txt
        refrun capcursor-ref capcursor "$root/tests/inputrun.sh" -s -b 40 -t 200 "$scr" "$out/capcursor-ref" -r \
            -f "$root/build/dos/US22.EXE=US22.EXE" -i 'boot:shell=C:\US22.EXE'
        "$root/tests/inputrun.sh" -e "$out/capcursor-ref/boot.png" -E "$out/capcursor-ref" -b 40 -t 200 "$scr" "$out/capcursor-w16" -r \
            -f "$root/build/dos/US22.EXE=US22.EXE" -i 'boot:shell=C:\US22.EXE' \
            -f "$user=WINDOWS/SYSTEM/USER.EXE" -f "$gdi=WINDOWS/SYSTEM/GDI.EXE" > "$out/capcursor-w16.txt" 2>&1 || true
        for k in ref w16; do
            f=$out/capcursor-$k/work/c/WINDOWS/TK.LOG
            if [ -f "$f" ]; then tr -d '\r' < "$f" | sed -n '/^LBUTTONDOWN/,$p' > "$out/capcursor-$k.log"
            else : > "$out/capcursor-$k.log"; fi
        done
        r=ok
        if [ "$(grep -c '^ReleaseCapture }' "$out/capcursor-ref.log")" != 3 ]; then
            r="the reference's log has not three releases ($out/capcursor-ref.log)"
        elif ! cmp -s "$out/capcursor-ref.log" "$out/capcursor-w16.log"; then
            r="US22's messages differ from 3.1's ($out/capcursor-ref.log, capcursor-w16.log)"
        fi
        for g in "$out/capcursor-ref"/[0-9]*.png; do
            [ "$r" = ok ] || break
            s=$(basename "$g" .png)
            [ -f "$out/capcursor-w16/$s.png" ] || { r="no $s"; break; }
            n=$(magick compare -metric AE "$out/capcursor-w16/$s.png" "$g" "$out/capcursor-$s-diff.png" 2>&1 >/dev/null || true)
            n=${n%% *}
            [ "$n" = 0 ] || r="$s: $n pixels differ ($out/capcursor-$s-diff.png)"
        done
        if [ "$r" = ok ]; then echo "ok   capcursor: 0 pixels after each release, US22's messages as 3.1's"
        else echo "FAIL capcursor: $r"; fails=$((fails + 1)); fi ;;
    edithandle)
        # an edit control's text in its hInstance's local heap,
        # EM_GETHANDLE and EM_SETHANDLE (user/edit.c): part us, probe US20
        # (tests/apps/us20.c) as the shell on Windows 3.1 and on w16's
        # USER, the OBS lines of the two logs identical; part np,
        # Microsoft's Notepad on w16's USER and GDI opening a 31500-byte
        # file, paging, typing at both ends and saving
        # (tests/scripts/input/npfile.txt) against all-Microsoft: every
        # screen and the saved file's bytes identical
        pk=${part:-us np}
        r=ok
        case " $pk " in *" us "*)
        for k in ref w16; do
            if [ $k = ref ]; then set -- -r; else set -- -r -f "$user=WINDOWS/SYSTEM/USER.EXE"; fi
            "$root/tests/dropin.sh" -t 150 -s "$out/edithandle-$k.png" -o "$out/edithandle-$k" \
                -f "$root/build/dos/US20.EXE=US20.EXE" -i 'boot:shell=C:\US20.EXE' "$@" \
                > "$out/edithandle-$k.txt" 2>&1 || true
            : > "$out/edithandle-$k.log"
            for _d in "$out/edithandle-$k/c/WINDOWS" "$out/edithandle-$k/c"; do
                [ -f "$_d/TK.LOG" ] && { tr -d '\r' < "$_d/TK.LOG" > "$out/edithandle-$k.log"; break; }
            done
        done
        for k in ref w16; do
            f=$out/edithandle-$k.log
            if ! grep -q '^US20 end' "$f"; then r="us: $k: US20 did not finish ($f)"; break; fi
        done
        if [ "$r" = ok ]; then
            grep '^OBS [^~]' "$out/edithandle-ref.log" > "$out/edithandle-ref.obs"
            grep '^OBS [^~]' "$out/edithandle-w16.log" > "$out/edithandle-w16.obs"
            cmp -s "$out/edithandle-ref.obs" "$out/edithandle-w16.obs" ||
                r="us: US20's observations differ from 3.1's ($out/edithandle-ref.obs, edithandle-w16.obs)"
        fi ;; esac
        case " $pk " in *" np "*)
        scr=$root/tests/scripts/input/npfile.txt
        python3 -c 'import sys
open(sys.argv[1], "w", newline="\r\n").write("".join("Line %03d: the quick brown fox jumps over the lazy dog, twice.\n" % i for i in range(1, 501)))' "$out/NPFILE.TXT"
        refrun npfile-ref edithandle "$root/tests/inputrun.sh" -s -b 40 -t 200 "$scr" "$out/npfile-ref" -r \
            -f "$out/NPFILE.TXT=NPFILE.TXT" -i 'WIN.INI:windows:run=C:\NPFILE.TXT'
        "$root/tests/inputrun.sh" -e "$out/npfile-ref/boot.png" -E "$out/npfile-ref" -b 40 -t 200 "$scr" "$out/npfile-w16" -r \
            -f "$out/NPFILE.TXT=NPFILE.TXT" -i 'WIN.INI:windows:run=C:\NPFILE.TXT' \
            -f "$user=WINDOWS/SYSTEM/USER.EXE" -f "$gdi=WINDOWS/SYSTEM/GDI.EXE" > "$out/npfile-w16.txt" 2>&1 || true
        f=work/c/NPFILE.TXT
        if [ "$r" = ok ] && ! grep -q 'Added at the end' "$out/npfile-ref/$f" 2>/dev/null; then
            r="np: the reference did not save the edited file ($out/npfile-ref)"
        elif [ "$r" = ok ] && ! cmp -s "$out/npfile-ref/$f" "$out/npfile-w16/$f"; then
            r="np: the saved file differs ($out/npfile-ref/$f, npfile-w16/$f)"
        fi
        for g in "$out/npfile-ref"/[0-9]*.png; do
            [ "$r" = ok ] || break
            s=$(basename "$g" .png)
            [ -f "$out/npfile-w16/$s.png" ] || { r="np: no $s"; break; }
            n=$(magick compare -metric AE "$out/npfile-w16/$s.png" "$g" "$out/npfile-$s-diff.png" 2>&1 >/dev/null || true)
            n=${n%% *}
            [ "$n" = 0 ] || r="np: $s: $n pixels differ ($out/npfile-$s-diff.png)"
        done ;; esac
        if [ -n "$part" ]; then partdone "$r"
        elif [ "$r" = ok ]; then echo "ok   edithandle: US20 as 3.1; Notepad opens, edits and saves a 31500-byte file as on 3.1 (0 pixels, same bytes)"
        else echo "FAIL edithandle: $r"; fails=$((fails + 1)); fi ;;
    sbdrag)
        # a scroll bar's thumb dragged with real input (user/scroll.c,
        # track_scrollbar): part us, probe US19 as the shell, its four bars
        # (tests/scripts/input/sbdrag-us.txt); part np, Microsoft's
        # Notepad (tests/scripts/input/sbdrag-np.txt); each under w16's
        # USER and GDI on Microsoft's KRNL286 against all-Microsoft. Every
        # screen, taken mid-drag, must be identical (Notepad's caret left
        # out), and US19's log of WM_VSCROLL/WM_HSCROLL (code, position,
        # the pointer then) identical to 3.1's
        pk=${part:-us np}
        r=ok
        case " $pk " in *" us "*)
        scr=$root/tests/scripts/input/sbdrag-us.txt
        refrun sbdrag-usref sbdrag "$root/tests/inputrun.sh" -s -b 40 -t 300 "$scr" "$out/sbdrag-usref" -r \
            -f "$root/build/dos/US19.EXE=US19.EXE" -i 'boot:shell=C:\US19.EXE'
        "$root/tests/inputrun.sh" -e "$out/sbdrag-usref/boot.png" -E "$out/sbdrag-usref" -b 40 -t 300 "$scr" "$out/sbdrag-us" -r \
            -f "$root/build/dos/US19.EXE=US19.EXE" -i 'boot:shell=C:\US19.EXE' \
            -f "$user=WINDOWS/SYSTEM/USER.EXE" -f "$gdi=WINDOWS/SYSTEM/GDI.EXE" > "$out/sbdrag-us.txt" 2>&1 || true
        for k in usref us; do
            f=$out/sbdrag-$k/work/c/WINDOWS/TK.LOG
            [ -f "$f" ] && tr -d '\r' < "$f" | grep '^MSG ' > "$out/sbdrag-$k.msg" || : > "$out/sbdrag-$k.msg"
        done
        if [ "$(grep -c ' 5 ' "$out/sbdrag-usref.msg")" -lt 10 ]; then
            r="us: the reference's log has too few SB_THUMBTRACKs ($out/sbdrag-usref.msg)"
        elif ! cmp -s "$out/sbdrag-usref.msg" "$out/sbdrag-us.msg"; then
            r="us: the messages differ from 3.1's ($out/sbdrag-usref.msg, sbdrag-us.msg)"
        fi
        for f in "$out/sbdrag-usref"/[0-9]*.png; do
            [ "$r" = ok ] || break
            s=$(basename "$f" .png)
            [ -f "$out/sbdrag-us/$s.png" ] || { r="us: no $s"; break; }
            n=$(magick compare -metric AE "$out/sbdrag-us/$s.png" "$f" "$out/sbdrag-us-$s-diff.png" 2>&1 >/dev/null || true)
            n=${n%% *}
            [ "$n" = 0 ] || r="us: $s: $n pixels differ ($out/sbdrag-us-$s-diff.png)"
        done ;; esac
        case " $pk " in *" np "*)
        scr=$root/tests/scripts/input/sbdrag-np.txt
        m="rectangle 14,42 18,60"       # the caret's place (it blinks)
        refrun sbdrag-npref sbdrag "$root/tests/inputrun.sh" -s -b 40 -t 300 "$scr" "$out/sbdrag-npref" -r \
            -i 'WIN.INI:windows:run=NOTEPAD.EXE'
        "$root/tests/inputrun.sh" -e "$out/sbdrag-npref/boot.png" -m "$m" -E "$out/sbdrag-npref" -M "$m" -b 40 -t 300 "$scr" "$out/sbdrag-np" -r \
            -i 'WIN.INI:windows:run=NOTEPAD.EXE' \
            -f "$user=WINDOWS/SYSTEM/USER.EXE" -f "$gdi=WINDOWS/SYSTEM/GDI.EXE" > "$out/sbdrag-np.txt" 2>&1 || true
        [ -f "$out/sbdrag-npref/04-end.png" ] || r="np: the reference has no screens ($out/sbdrag-npref)"
        for f in "$out/sbdrag-npref"/[0-9]*.png; do
            [ "$r" = ok ] || break
            s=$(basename "$f" .png)
            [ -f "$out/sbdrag-np/$s.png" ] || { r="np: no $s"; break; }
            magick "$out/sbdrag-np/$s.png" -fill black -draw "$m" "$out/sbdrag-np-a.png"
            magick "$f" -fill black -draw "$m" "$out/sbdrag-np-b.png"
            n=$(magick compare -metric AE "$out/sbdrag-np-a.png" "$out/sbdrag-np-b.png" "$out/sbdrag-np-$s-diff.png" 2>&1 >/dev/null || true)
            n=${n%% *}
            [ "$n" = 0 ] || r="np: $s: $n pixels differ ($out/sbdrag-np-$s-diff.png)"
        done ;; esac
        if [ -n "$part" ]; then partdone "$r"
        elif [ "$r" = ok ]; then echo "ok   sbdrag: 0 pixels mid-drag (US19's four bars, Notepad), US19's messages as 3.1's"
        else echo "FAIL sbdrag: $r"; fails=$((fails + 1)); fi ;;
    deskver)
        # the desktop's version stamp (a w16 extension, user/deskver.c):
        # off, w16's USER and GDI give 3.1's screen; on, every w16 part,
        # the drag script run with it off and on (the on run waits for
        # the off run's screens, the stamp's box left out), each screen as
        # tools/deskstamp.py says (tests/scripts/deskver.py)
        ref
        boot deskver-stock -f "$user=WINDOWS/SYSTEM/USER.EXE" -f "$gdi=WINDOWS/SYSTEM/GDI.EXE" \
            -i w16:DesktopVersion=0
        r=ok
        if ! booted deskver-stock; then
            r="DesktopVersion=0: the boot did not complete ($out/deskver-stock.txt)"
        else
            n=$(magick compare -metric AE "$out/deskver-stock.png" "$out/ref.png" "$out/deskver-stock-diff.png" 2>&1 >/dev/null || true)
            n=${n%% *}
            [ "$n" = 0 ] || r="DesktopVersion=0: the screen differs from 3.1's by $n pixels ($out/deskver-stock-diff.png)"
        fi
        if [ "$r" = ok ]; then
            fon=$root/build/fonts
            dvrun() {   # SETTING DIR [inputrun.sh options]
                _v=$1 _d=$2; shift 2
                "$root/tests/inputrun.sh" "$@" -s -b 30 -t 150 "$root/tests/scripts/input/deskver.txt" "$_d" \
                    -w -x -f "$user=WINDOWS/SYSTEM/USER.EXE" -f "$gdi=WINDOWS/SYSTEM/GDI.EXE" \
                    -f "$shell=WINDOWS/SYSTEM/SHELL.DLL" -f "$root/build/dos/PROGMAN.EXE=WINDOWS/PROGMAN.EXE" \
                    -f "$woa=WINDOWS/SYSTEM/WINOLDAP.MOD" -f "$root/build/dos/WIN87EM.DLL=WINDOWS/SYSTEM/WIN87EM.DLL" \
                    -f "$fon/VGASYS.FON=WINDOWS/SYSTEM/VGASYS.FON" -f "$fon/VGAFIX.FON=WINDOWS/SYSTEM/VGAFIX.FON" \
                    -f "$fon/VGAOEM.FON=WINDOWS/SYSTEM/VGAOEM.FON" -f "$fon/SSERIFE.FON=WINDOWS/SYSTEM/SSERIFE.FON" \
                    -i "w16:DesktopVersion=$_v" > "$_d.txt" 2>&1 || true
            }
            dvtext=$(python3 "$root/tools/deskstamp.py" --text Standard)
            dvbox=$(python3 "$root/tools/deskstamp.py" "$fon/VGASYS.FON" "$dvtext" --box 640 480)
            dvrun 0 "$out/deskver-off"
            # shellcheck disable=SC2086
            set -- $dvbox
            dvrun 1 "$out/deskver-on" -E "$out/deskver-off" \
                -M "rectangle $1,$2 $(($1 + $3 - 1)),$(($2 + $4 - 1))"
            if ! python3 "$root/tests/scripts/deskver.py" "$fon/VGASYS.FON" "$dvtext" \
                "$out/deskver-off" "$out/deskver-on" boot,04-back 02-down,03-back-moving 01-down-moving \
                > "$out/deskver-check.txt" 2>&1; then
                r="DesktopVersion=1: $(grep -c '^FAIL' "$out/deskver-check.txt") screen(s) wrong ($out/deskver-check.txt)"
            fi
        fi
        if [ "$r" = ok ]; then echo "ok   deskver: \"$dvtext\" as drawn by tools/deskstamp.py (5 screens); DesktopVersion=0 as 3.1"
        else echo "FAIL deskver: $r"; fails=$((fails + 1)); fi ;;
    mouse)
        # each script with Microsoft's MOUSE.DRV and with w16's; nothing
        # else differs: no part of any screen is left out but Notepad's
        # caret in helpmenus (it blinks on through the menu, its phase
        # the run's timing)
        mdrv=$root/build/dos/MOUSE.DRV
        r=ok
        for s in ${part:-dragoff pmitems pmclose helpmenus}; do
            case $s in pmitems|pmclose) b=40 t=240 ;; *) b=25 t=200 ;; esac
            scr=$root/tests/scripts/input/$s.txt
            refrun mouse-$s-ref mouse "$root/tests/inputrun.sh" -s -b $b -t $t "$scr" "$out/mouse-$s-ref" -r
            "$root/tests/inputrun.sh" -e "$out/mouse-$s-ref/boot.png" -E "$out/mouse-$s-ref" -M "$([ $s != helpmenus ] && echo rectangle 0,0 0,0 || echo rectangle 14,42 20,61)" -b $b -t $t "$scr" "$out/mouse-$s-w16" -r \
                -f "$mdrv=WINDOWS/SYSTEM/MOUSE.DRV" > "$out/mouse-$s-w16.txt" 2>&1 || true
            ls "$out/mouse-$s-ref"/[0-9]*.png > /dev/null 2>&1 || { r="$s: no screens"; break; }
            for f in "$out/mouse-$s-ref"/[0-9]*.png; do
                g=$(basename "$f")
                [ -f "$out/mouse-$s-w16/$g" ] || { r="$s: no $g"; break; }
                m="rectangle 0,0 0,0"
                [ $s = helpmenus ] && m="rectangle 14,42 20,61"
                magick "$f" -fill black -draw "$m" "$out/mouse-$s-a.png"
                magick "$out/mouse-$s-w16/$g" -fill black -draw "$m" "$out/mouse-$s-b.png"
                n=$(magick compare -metric AE "$out/mouse-$s-b.png" "$out/mouse-$s-a.png" "$out/mouse-$s-${g%.png}-diff.png" 2>&1 >/dev/null || true)
                n=${n%% *}
                [ "$n" = 0 ] || { r="$s: $g: $n pixels differ"; break; }
            done
            [ "$r" = ok ] || break
        done
        if [ -n "$part" ]; then partdone "$r"
        elif [ "$r" = ok ]; then echo "ok   mouse: 0 pixels (dragoff, pmitems, pmclose, helpmenus with w16's MOUSE.DRV)"
        else echo "FAIL mouse: $r ($out)"; fails=$((fails + 1)); fi ;;
    mouseall)
        fon=$root/build/fonts
        scr=$root/tests/scripts/input/dragoff.txt
        # (both sides are w16's but for MOUSE.DRV: not cached)
        for k in ref w16; do
            if [ $k = w16 ]; then set -- -e "$out/mouseall-ref/boot.png" -E "$out/mouseall-ref" -b 30 -t 80 "$scr" "$out/mouseall-$k" \
                -f "$root/build/dos/MOUSE.DRV=WINDOWS/SYSTEM/MOUSE.DRV"
            else set -- -s -b 30 -t 80 "$scr" "$out/mouseall-$k"; fi
            "$root/tests/inputrun.sh" "$@" \
                -w -x -f "$user=WINDOWS/SYSTEM/USER.EXE" -f "$gdi=WINDOWS/SYSTEM/GDI.EXE" \
                -f "$root/build/dos/PROGMAN.EXE=WINDOWS/PROGMAN.EXE" \
                -f "$fon/VGASYS.FON=WINDOWS/SYSTEM/VGASYS.FON" -f "$fon/VGAFIX.FON=WINDOWS/SYSTEM/VGAFIX.FON" \
                -f "$fon/VGAOEM.FON=WINDOWS/SYSTEM/VGAOEM.FON" -f "$fon/SSERIFE.FON=WINDOWS/SYSTEM/SSERIFE.FON" \
                > "$out/mouseall-$k.txt" 2>&1 || true
        done
        r=ok
        for s in 01-right-moving 02-right 03-left-moving 04-left 05-bottom-moving 06-bottom; do
            if [ ! -f "$out/mouseall-w16/$s.png" ] || [ ! -f "$out/mouseall-ref/$s.png" ]; then
                r="no $s screen"; break
            fi
            n=$(magick compare -metric AE "$out/mouseall-w16/$s.png" "$out/mouseall-ref/$s.png" "$out/mouseall-$s-diff.png" 2>&1 >/dev/null || true)
            n=${n%% *}
            [ "$n" = 0 ] || { r="$s: $n pixels differ ($out/mouseall-$s-diff.png)"; break; }
        done
        # the drags must have moved Program Manager: the screens differ
        if [ "$r" = ok ]; then
            n=$(magick compare -metric AE "$out/mouseall-w16/02-right.png" "$out/mouseall-w16/04-left.png" null: 2>&1 || true)
            [ "${n%% *}" != 0 ] || r="the drags did nothing"
        fi
        if [ "$r" = ok ]; then echo "ok   mouseall: 0 pixels (every w16 part, w16's MOUSE.DRV against Microsoft's)"
        else echo "FAIL mouseall: $r"; fails=$((fails + 1)); fi ;;
    commdlg)
        # w16's COMMDLG.DLL against Microsoft's on the reference 3.1
        # (tests/commdlgscen.sh, ARCHITECTURE.md 17): one scenario of each
        # dialog; the probe's log identical, the screens within
        # tests/scripts/commdlg.baseline (our own pictures)
        if CD_OUT="$out/commdlg" "$root/tests/commdlgscen.sh" open1 find1 color1 font1 print1 \
                > "$out/commdlg.txt" 2>&1; then
            echo "ok   commdlg: Open, Find, Color, Font, Print as the original's"
        else
            echo "FAIL commdlg: $(grep -m1 -E '^  ' "$out/commdlg.txt" || echo 'see commdlg.txt') ($out/commdlg.txt)"
            fails=$((fails + 1))
        fi ;;
    comm)
        # (both runs each time: commhost's records are part of the result)
        if "$root/tests/commprobe.sh" -p 4 -o "$out/comm" > "$out/comm.txt" 2>&1; then
            echo "ok   comm: CM1's exchange and the far end's records the same with w16's COMM.DRV"
        else
            echo "FAIL comm: $(grep -m1 -E '^FAIL' "$out/comm.txt" || echo 'see comm.txt') ($out/comm.txt)"
            fails=$((fails + 1))
        fi ;;
    commuser)
        # w16's USER's comm functions against Windows 3.1's
        # (tests/commusr.sh): probe CM2 on each, COMMREC recording every
        # call USER makes to Windows 3.1's COMM.DRV, commhost at the far
        # end; the probe's log, COMMREC's and commhost's identical
        if "$root/tests/commusr.sh" -o "$out/commuser" > "$out/commuser.txt" 2>&1; then
            echo "ok   commuser: CM2's log, USER's driver calls and the far end the same on w16's USER"
        else
            echo "FAIL commuser: $(grep -m1 -E '^FAIL' "$out/commuser.txt" || echo 'see commuser.txt') ($out/commuser.txt)"
            fails=$((fails + 1))
        fi ;;
    commall)
        # the comm case on every w16 part: CM1's exchange through USER
        # (part C) on w16's WIN.COM, DOSX, KRNL286, USER, GDI and COMM.DRV
        # against all of Microsoft's (tests/commusr.sh -a)
        if "$root/tests/commusr.sh" -e "$root/build/dos/CM1.EXE" -n -a -p 4 \
            -W "$root/build/dos/COMM.DRV" -o "$out/commall" > "$out/commall.txt" 2>&1; then
            echo "ok   commall: CM1's exchange and the far end's records the same on every w16 part"
        else
            echo "FAIL commall: $(grep -m1 -E '^FAIL' "$out/commall.txt" || echo 'see commall.txt') ($out/commall.txt)"
            fails=$((fails + 1))
        fi ;;
    termclose)
        # Terminal closed while sending a file (tests/scripts/input/
        # termclose.txt), tests/commhost.py at the far end of COM1: on
        # every w16 part (WIN.COM, DOSX, KRNL286, USER, GDI, COMM.DRV)
        # Terminal must be gone and the screen as all-Microsoft's (not
        # cached: the far end is part of the run)
        scr=$root/tests/scripts/input/termclose.txt
        python3 -c 'for i in range(400): print("line %04d of the text Terminal sends: the quick brown fox jumps over the lazy dog" % i)' > "$out/termclose-SEND.TXT"
        for k in ref w16; do
            port=$(python3 -c 'import socket; s = socket.socket(); s.bind(("127.0.0.1", 0)); print(s.getsockname()[1])')
            python3 "$root/tests/commhost.py" -t 400 -l "$out/termclose-$k.host" "$port" > "$out/termclose-$k.hostout" 2>&1 &
            hpid=$!
            sleep 0.5
            if [ $k = ref ]; then set -- -s -b 40 -t 240 "$scr" "$out/termclose-ref" -r
            else set -- -e "$out/termclose-ref/boot.png" -E "$out/termclose-ref" -b 40 -t 240 "$scr" "$out/termclose-w16" \
                -w -x -f "$user=WINDOWS/SYSTEM/USER.EXE" -f "$gdi=WINDOWS/SYSTEM/GDI.EXE" \
                -f "$root/build/dos/COMM.DRV=WINDOWS/SYSTEM/COMM.DRV"; fi
            DROPIN_CONF="[serial]
serial1=nullmodem server:127.0.0.1 port:$port" "$root/tests/inputrun.sh" "$@" -i 'WIN.INI:windows:run=TERMINAL.EXE' \
                -f "$out/termclose-SEND.TXT=WINDOWS/SEND.TXT" > "$out/termclose-$k.txt" 2>&1 || true
            kill "$hpid" 2>/dev/null || true
            wait "$hpid" 2>/dev/null || true
        done
        r=ok
        f=03-closed.png
        if [ ! -f "$out/termclose-ref/$f" ] || [ ! -f "$out/termclose-w16/$f" ]; then r="no $f"
        elif ! grep -q 'L rts=0 dtr=0' "$out/termclose-ref.host"; then r="the reference did not close the port"
        elif ! grep -q '^D ' "$out/termclose-w16.host"; then r="w16: nothing was sent"
        else
            n=$(magick compare -metric AE "$out/termclose-w16/$f" "$out/termclose-ref/$f" "$out/termclose-diff.png" 2>&1 >/dev/null || true)
            n=${n%% *}
            [ "$n" = 0 ] || r="$f: $n pixels differ (Terminal still there?)"
        fi
        if [ "$r" = ok ]; then echo "ok   termclose: Terminal closed mid-transfer on every w16 part, the screen as 3.1's"
        else echo "FAIL termclose: $r ($out)"; fails=$((fails + 1)); fi ;;
    kbd)
        # w16's KEYBOARD.DRV (drivers/keyboard) on Microsoft's KRNL286,
        # DOSX, USER and GDI under real key input: typing in Notepad
        # (kbtype) and the key scripts of tabs, alttab, helpmenus, pmkeys,
        # icons and allw16input (menus), every screen against the same script with Microsoft's
        # KEYBOARD.DRV (helpmenus: Notepad's caret left out as there;
        # icons: Clock's icon)
        kdrv=$root/build/dos/KEYBOARD.DRV
        # carets keep the phase they start in, on both sides (WIN.INI
        # CursorBlinkRate=65535, read by USER at boot, as SNAP's blinktime):
        # how long the keys took (the host's load) must not decide whether
        # Notepad's caret shows on a shot. A menu opened from the keyboard
        # froze the caret in whatever phase it had (04-searchmenu: shown on
        # one side, hidden on the other, 30 pixels)
        blink='WIN.INI:windows:CursorBlinkRate=65535'
        r=ok
        for s in ${part:-kbtype tabs alttab helpmenus pmkeys icons menus}; do
            scr=$root/tests/scripts/input/$s.txt
            case $s in
            helpmenus) m="rectangle 14,42 20,61" ;;
            icons) m="rectangle 160,400 220,445" ;;
            *) m="rectangle 0,0 0,0" ;;
            esac
            b=25 t=220
            [ $s = pmkeys ] && b=40
            [ $s = icons ] && t=380
            refrun kbd-$s-ref kbd "$root/tests/inputrun.sh" -s -b $b -t $t "$scr" "$out/kbd-$s-ref" -r -i "$blink"
            "$root/tests/inputrun.sh" -e "$out/kbd-$s-ref/boot.png" -E "$out/kbd-$s-ref" -M "$m" -b $b -t $t "$scr" "$out/kbd-$s-w16" -r -i "$blink" \
                -f "$kdrv=WINDOWS/SYSTEM/KEYBOARD.DRV" > "$out/kbd-$s-w16.txt" 2>&1 || true
            ls "$out/kbd-$s-ref"/[0-9]*.png > /dev/null 2>&1 || { r="$s: no screens"; break; }
            for f in "$out/kbd-$s-ref"/[0-9]*.png; do
                g=$(basename "$f")
                [ -f "$out/kbd-$s-w16/$g" ] || { r="$s: no $g"; break; }
                magick "$f" -fill black -draw "$m" "$out/kbd-$s-a.png"
                magick "$out/kbd-$s-w16/$g" -fill black -draw "$m" "$out/kbd-$s-b.png"
                n=$(magick compare -metric AE "$out/kbd-$s-b.png" "$out/kbd-$s-a.png" "$out/kbd-$s-${g%.png}-diff.png" 2>&1 >/dev/null || true)
                n=${n%% *}
                [ "$n" = 0 ] || { r="$s: $g: $n pixels differ"; break; }
            done
            [ "$r" = ok ] || break
        done
        if [ -n "$part" ]; then partdone "$r"
        elif [ "$r" = ok ]; then echo "ok   kbd: 0 pixels (kbtype, tabs, alttab, helpmenus, pmkeys, icons, menus with w16's KEYBOARD.DRV)"
        else echo "FAIL kbd: $r ($out)"; fails=$((fails + 1)); fi ;;
    kbdall)
        # every w16 part (WIN.COM, DOSX, KRNL286, USER, GDI, PROGMAN,
        # fonts) with w16's KEYBOARD.DRV against the same with Microsoft's
        # KEYBOARD.DRV, the kbtype script (both sides are w16's but for
        # KEYBOARD.DRV: not cached)
        fon=$root/build/fonts
        scr=$root/tests/scripts/input/kbtype.txt
        # carets keep their starting phase on both sides (as in kbd)
        blink='WIN.INI:windows:CursorBlinkRate=65535'
        for k in ref w16; do
            if [ $k = w16 ]; then set -- -e "$out/kbdall-ref/boot.png" -E "$out/kbdall-ref" -b 30 -t 240 "$scr" "$out/kbdall-$k" \
                -f "$root/build/dos/KEYBOARD.DRV=WINDOWS/SYSTEM/KEYBOARD.DRV"
            else set -- -s -b 30 -t 240 "$scr" "$out/kbdall-$k"; fi
            "$root/tests/inputrun.sh" "$@" -i "$blink" \
                -w -x -f "$user=WINDOWS/SYSTEM/USER.EXE" -f "$gdi=WINDOWS/SYSTEM/GDI.EXE" \
                -f "$root/build/dos/PROGMAN.EXE=WINDOWS/PROGMAN.EXE" \
                -f "$fon/VGASYS.FON=WINDOWS/SYSTEM/VGASYS.FON" -f "$fon/VGAFIX.FON=WINDOWS/SYSTEM/VGAFIX.FON" \
                -f "$fon/VGAOEM.FON=WINDOWS/SYSTEM/VGAOEM.FON" -f "$fon/SSERIFE.FON=WINDOWS/SYSTEM/SSERIFE.FON" \
                > "$out/kbdall-$k.txt" 2>&1 || true
        done
        r=ok
        ls "$out/kbdall-ref"/[0-9]*.png > /dev/null 2>&1 || r="no screens"
        for f in "$out/kbdall-ref"/[0-9]*.png; do
            [ "$r" = ok ] || break
            g=$(basename "$f")
            [ -f "$out/kbdall-w16/$g" ] || { r="no $g"; break; }
            n=$(magick compare -metric AE "$out/kbdall-w16/$g" "$f" "$out/kbdall-${g%.png}-diff.png" 2>&1 >/dev/null || true)
            n=${n%% *}
            [ "$n" = 0 ] || r="$g: $n pixels differ ($out/kbdall-${g%.png}-diff.png)"
        done
        # and the typing did something: the typed screen is not the boot's
        if [ "$r" = ok ]; then
            n=$(magick compare -metric AE "$out/kbdall-w16/01-typed.png" "$out/kbdall-w16/boot.png" null: 2>&1 || true)
            [ "${n%% *}" != 0 ] || r="the typing did nothing"
        fi
        if [ "$r" = ok ]; then echo "ok   kbdall: 0 pixels (every w16 part, w16's KEYBOARD.DRV against Microsoft's)"
        else echo "FAIL kbdall: $r"; fails=$((fails + 1)); fi ;;
    kbdwrite)
        # Windows Write (the write case's script) with w16's KEYBOARD.DRV
        # on Microsoft's KRNL286, DOSX, USER, GDI against all-Microsoft:
        # identical screens but for the caret's places (it blinks)
        s=$root/tests/scripts/input/write.txt
        cp "$root/.tools/ref/win31/cdrive/WINDOWS/WIN.INI" "$out/kbdwrite-WIN.INI"
        python3 "$root/tests/scripts/iniset.py" "$out/kbdwrite-WIN.INI" 'windows:run=WRITE.EXE'
        # Write's caret keeps its starting phase on both sides (as in kbd)
        python3 "$root/tests/scripts/iniset.py" "$out/kbdwrite-WIN.INI" 'windows:CursorBlinkRate=65535'
        refrun kbdwrite-ref kbdwrite "$root/tests/inputrun.sh" -s -b 25 -t 210 "$s" "$out/kbdwrite-ref" -r \
            -f "$out/kbdwrite-WIN.INI=WINDOWS/WIN.INI"
        "$root/tests/inputrun.sh" -e "$out/kbdwrite-ref/boot.png" -m "rectangle 27,46 28,61" -E "$out/kbdwrite-ref" \
            -M "rectangle 27,46 28,61 rectangle 112,46 113,61 rectangle 112,82 113,97 rectangle 95,98 96,113" -b 25 -t 210 "$s" "$out/kbdwrite-w16" -r \
            -f "$root/build/dos/KEYBOARD.DRV=WINDOWS/SYSTEM/KEYBOARD.DRV" \
            -f "$out/kbdwrite-WIN.INI=WINDOWS/WIN.INI" > "$out/kbdwrite-w16.txt" 2>&1 || true
        r=ok
        for f in "$out/kbdwrite-ref"/[0-9]*.png; do
            s=$(basename "$f")
            [ -f "$out/kbdwrite-w16/$s" ] || { r="no $s"; break; }
            case $s in
            01-*) m="rectangle 27,46 28,61" ;;
            02-*|03-*) m="rectangle 112,46 113,61" ;;
            04-*) m="rectangle 112,82 113,97" ;;
            05-*) m="rectangle 95,98 96,113" ;;
            *) m="rectangle 0,0 0,0" ;;
            esac
            magick "$f" -fill black -draw "$m" "$out/kbdwrite-a.png"
            magick "$out/kbdwrite-w16/$s" -fill black -draw "$m" "$out/kbdwrite-b.png"
            n=$(magick compare -metric AE "$out/kbdwrite-b.png" "$out/kbdwrite-a.png" "$out/kbdwrite-${s%.png}-diff.png" 2>&1 >/dev/null || true)
            n=${n%% *}
            [ "$n" = 0 ] || { r="$s: $n pixels differ"; break; }
        done
        ls "$out/kbdwrite-ref"/[0-9]*.png > /dev/null 2>&1 || [ "$r" != ok ] || r="no screens"
        if [ "$r" = ok ]; then echo "ok   kbdwrite: 0 pixels (Write with w16's KEYBOARD.DRV)"
        else echo "FAIL kbdwrite: $r ($out)"; fails=$((fails + 1)); fi ;;
    kbdwoados)
        # the MS-DOS Prompt (the woados case's script: KEYBOARD.Disable and
        # Enable around the DOS program, the BIOS's keyboard in between)
        # with w16's KEYBOARD.DRV on Microsoft's KRNL286, DOSX, USER, GDI,
        # WINOLDAP against all-Microsoft: the same screens up to the
        # cursor (40 pixels), Windows repainted as it was before
        scr=$root/tests/scripts/input/woados.txt
        set -- -i 'NonWindowsApp:SwapDisk=C:\WINDOWS\TEMP'
        refrun kbdwoados-ref kbdwoados env INPUTRUN_STAY=1 "$root/tests/inputrun.sh" -b 30 -t 130 "$scr" "$out/kbdwoados-ref" -r "$@"
        INPUTRUN_STAY=1 "$root/tests/inputrun.sh" -b 30 -t 130 "$scr" "$out/kbdwoados-w16" -r "$@" \
            -f "$root/build/dos/KEYBOARD.DRV=WINDOWS/SYSTEM/KEYBOARD.DRV" > "$out/kbdwoados-w16.txt" 2>&1 || true
        a=$out/kbdwoados-w16 b=$out/kbdwoados-ref r=ok
        for s in 01-dos:640x400 02-ver:640x400 03-back:640x480 04-exit:640x400; do
            f=${s%%:*} want=${s#*:}
            [ -f "$a/$f.png" ] && [ -f "$b/$f.png" ] || { r="no $f"; break; }
            sz=$(magick identify -format '%wx%h' "$a/$f.png" 2>/dev/null)
            [ "$sz" = "$want" ] || { r="$f: screen $sz, not $want"; break; }
            if [ $f = 03-back ]; then
                n=$(magick compare -metric AE "$a/$f.png" "$a/00-before.png" "$out/kbdwoados-$f-diff.png" 2>&1 >/dev/null || true)
            else
                n=$(magick compare -metric AE "$a/$f.png" "$b/$f.png" "$out/kbdwoados-$f-diff.png" 2>&1 >/dev/null || true)
            fi
            n=${n%%[. ]*}
            [ "${n:-99999}" -le 40 ] || { r="$f: $n pixels differ"; break; }
        done
        if [ "$r" = ok ]; then echo "ok   kbdwoados: the MS-DOS Prompt and back with w16's KEYBOARD.DRV"
        else echo "FAIL kbdwoados: $r ($out/kbdwoados-w16)"; fails=$((fails + 1)); fi ;;
    sysdrv)
        # w16's SYSTEM.DRV (drivers/system) on Microsoft's KRNL286, DOSX,
        # USER and GDI under real input: the timer-driven scripts (kbtype:
        # typematic repeats and the caret; tabs, alttab: the caret's blink
        # in dialogs; helpmenus; dragoff: the mouse; pmkeys), every screen
        # against the same script with Microsoft's SYSTEM.DRV (helpmenus:
        # Notepad's caret left out, as in the mouse and kbd cases)
        sdrv=$root/build/dos/SYSTEM.DRV
        r=ok
        for s in kbtype tabs alttab helpmenus dragoff pmkeys; do
            scr=$root/tests/scripts/input/$s.txt
            case $s in
            helpmenus) m="rectangle 14,42 20,61" ;;
            *) m="rectangle 0,0 0,0" ;;
            esac
            b=25 t=220
            [ $s = pmkeys ] && b=40
            refrun sysdrv-$s-ref sysdrv "$root/tests/inputrun.sh" -s -b $b -t $t "$scr" "$out/sysdrv-$s-ref" -r
            "$root/tests/inputrun.sh" -e "$out/sysdrv-$s-ref/boot.png" -E "$out/sysdrv-$s-ref" -M "$m" -b $b -t $t "$scr" "$out/sysdrv-$s-w16" -r \
                -f "$sdrv=WINDOWS/SYSTEM/SYSTEM.DRV" > "$out/sysdrv-$s-w16.txt" 2>&1 || true
            ls "$out/sysdrv-$s-ref"/[0-9]*.png > /dev/null 2>&1 || { r="$s: no screens"; break; }
            for f in "$out/sysdrv-$s-ref"/[0-9]*.png; do
                g=$(basename "$f")
                [ -f "$out/sysdrv-$s-w16/$g" ] || { r="$s: no $g"; break; }
                magick "$f" -fill black -draw "$m" "$out/sysdrv-a.png"
                magick "$out/sysdrv-$s-w16/$g" -fill black -draw "$m" "$out/sysdrv-b.png"
                n=$(magick compare -metric AE "$out/sysdrv-b.png" "$out/sysdrv-a.png" "$out/sysdrv-$s-${g%.png}-diff.png" 2>&1 >/dev/null || true)
                n=${n%% *}
                [ "$n" = 0 ] || { r="$s: $g: $n pixels differ"; break; }
            done
            [ "$r" = ok ] || break
        done
        if [ "$r" = ok ]; then echo "ok   sysdrv: 0 pixels (kbtype, tabs, alttab, helpmenus, dragoff, pmkeys with w16's SYSTEM.DRV)"
        else echo "FAIL sysdrv: $r ($out)"; fails=$((fails + 1)); fi ;;
    sysdrvall)
        # every w16 part (WIN.COM, DOSX, KRNL286, USER, GDI, PROGMAN,
        # fonts) with w16's SYSTEM.DRV against the same with Microsoft's
        # SYSTEM.DRV, the kbtype and tabs scripts (both sides are w16's
        # but for SYSTEM.DRV: not cached)
        fon=$root/build/fonts
        r=ok
        for s in kbtype tabs; do
            scr=$root/tests/scripts/input/$s.txt
            for k in ref w16; do
                if [ $k = w16 ]; then set -- -e "$out/sysdrvall-$s-ref/boot.png" -E "$out/sysdrvall-$s-ref" -b 30 -t 240 "$scr" "$out/sysdrvall-$s-$k" \
                    -f "$root/build/dos/SYSTEM.DRV=WINDOWS/SYSTEM/SYSTEM.DRV"
                else set -- -s -b 30 -t 240 "$scr" "$out/sysdrvall-$s-$k"; fi
                "$root/tests/inputrun.sh" "$@" \
                    -w -x -f "$user=WINDOWS/SYSTEM/USER.EXE" -f "$gdi=WINDOWS/SYSTEM/GDI.EXE" \
                    -f "$root/build/dos/PROGMAN.EXE=WINDOWS/PROGMAN.EXE" \
                    -f "$fon/VGASYS.FON=WINDOWS/SYSTEM/VGASYS.FON" -f "$fon/VGAFIX.FON=WINDOWS/SYSTEM/VGAFIX.FON" \
                    -f "$fon/VGAOEM.FON=WINDOWS/SYSTEM/VGAOEM.FON" -f "$fon/SSERIFE.FON=WINDOWS/SYSTEM/SSERIFE.FON" \
                    > "$out/sysdrvall-$s-$k.txt" 2>&1 || true
            done
            ls "$out/sysdrvall-$s-ref"/[0-9]*.png > /dev/null 2>&1 || { r="$s: no screens"; break; }
            for f in "$out/sysdrvall-$s-ref"/[0-9]*.png; do
                g=$(basename "$f")
                [ -f "$out/sysdrvall-$s-w16/$g" ] || { r="$s: no $g"; break; }
                n=$(magick compare -metric AE "$out/sysdrvall-$s-w16/$g" "$f" "$out/sysdrvall-$s-${g%.png}-diff.png" 2>&1 >/dev/null || true)
                n=${n%% *}
                [ "$n" = 0 ] || { r="$s: $g: $n pixels differ"; break; }
            done
            [ "$r" = ok ] || break
        done
        if [ "$r" = ok ]; then echo "ok   sysdrvall: 0 pixels (every w16 part, w16's SYSTEM.DRV against Microsoft's)"
        else echo "FAIL sysdrvall: $r ($out)"; fails=$((fails + 1)); fi ;;
    sysdrvwoados)
        # the MS-DOS Prompt (woados: WINOLDAP's DisableOEMLayer takes the
        # system timers off, EnableOEMLayer puts them back) with w16's
        # SYSTEM.DRV on Microsoft's stack against all-Microsoft, as kbdwoados
        scr=$root/tests/scripts/input/woados.txt
        set -- -i 'NonWindowsApp:SwapDisk=C:\WINDOWS\TEMP'
        refrun sysdrvwoados-ref sysdrvwoados env INPUTRUN_STAY=1 "$root/tests/inputrun.sh" -b 30 -t 130 "$scr" "$out/sysdrvwoados-ref" -r "$@"
        INPUTRUN_STAY=1 "$root/tests/inputrun.sh" -b 30 -t 130 "$scr" "$out/sysdrvwoados-w16" -r "$@" \
            -f "$root/build/dos/SYSTEM.DRV=WINDOWS/SYSTEM/SYSTEM.DRV" > "$out/sysdrvwoados-w16.txt" 2>&1 || true
        a=$out/sysdrvwoados-w16 b=$out/sysdrvwoados-ref r=ok
        for s in 01-dos:640x400 02-ver:640x400 03-back:640x480 04-exit:640x400; do
            f=${s%%:*} want=${s#*:}
            [ -f "$a/$f.png" ] && [ -f "$b/$f.png" ] || { r="no $f"; break; }
            sz=$(magick identify -format '%wx%h' "$a/$f.png" 2>/dev/null)
            [ "$sz" = "$want" ] || { r="$f: screen $sz, not $want"; break; }
            if [ $f = 03-back ]; then
                n=$(magick compare -metric AE "$a/$f.png" "$a/00-before.png" "$out/sysdrvwoados-$f-diff.png" 2>&1 >/dev/null || true)
            else
                n=$(magick compare -metric AE "$a/$f.png" "$b/$f.png" "$out/sysdrvwoados-$f-diff.png" 2>&1 >/dev/null || true)
            fi
            n=${n%%[. ]*}
            [ "${n:-99999}" -le 40 ] || { r="$f: $n pixels differ"; break; }
        done
        if [ "$r" = ok ]; then echo "ok   sysdrvwoados: the MS-DOS Prompt and back with w16's SYSTEM.DRV"
        else echo "FAIL sysdrvwoados: $r ($out/sysdrvwoados-w16)"; fails=$((fails + 1)); fi ;;
    sysboot) # w16's SYSTEM.DRV in plain boots: on Microsoft's stack, on
            # w16's WIN.COM, DOSX and KRNL286, and on an emulated 80286
            # (DOSBox-X: KRNL286, no coprocessor path), each against the
            # all-Microsoft boot there        must be 0
            ref
            boot sysms -r -f "$root/build/dos/SYSTEM.DRV=WINDOWS/SYSTEM/SYSTEM.DRV"; check sysms sysms ref
            boot sysallw16 -w -x -f "$root/build/dos/SYSTEM.DRV=WINDOWS/SYSTEM/SYSTEM.DRV"; check sysallw16 sysallw16 ref
            _s=$secs; secs=180
            refrun ref286 k286 boot -r -p 286; expect=$out/ref286.png
            boot sys286 -r -p 286 -f "$root/build/dos/SYSTEM.DRV=WINDOWS/SYSTEM/SYSTEM.DRV"; check sys286 sys286 ref286
            expect= secs=$_s ;;
    sysclock)
        # Clock's scenarios (tests/clockscenarios.sh: its second hand, the
        # minute and day changing, SetTimer through USER on SYSTEM.DRV's
        # timer) with w16's SYSTEM.DRV under both Clocks: within the
        # scenarios' baseline, as with Microsoft's driver
        if CK_FILES="$root/build/dos/SYSTEM.DRV=WINDOWS/SYSTEM/SYSTEM.DRV" CK_JOBS=2 \
                "$root/tests/clockscenarios.sh" -o "$out/sysclock" > "$out/sysclock.txt" 2>&1; then
            echo "ok   sysclock: Clock's scenarios with w16's SYSTEM.DRV"
        else
            echo "FAIL sysclock: $(grep -m1 -E '^  ' "$out/sysclock.txt" || echo 'see sysclock.txt') ($out/sysclock.txt)"
            fails=$((fails + 1))
        fi ;;
    sysprobe)
        # tests/sysprobe.sh: SY1 (every export, timers, the msec count,
        # Disable/Enable) and SYREC's boot record, w16's SYSTEM.DRV against
        # Microsoft's (not cached: both runs each time)
        if "$root/tests/sysprobe.sh" -o "$out/sysprobe" > "$out/sysprobe.txt" 2>&1; then
            echo "ok   sysprobe: SY1's logs and the boot's calls the same with w16's SYSTEM.DRV"
        else
            echo "FAIL sysprobe: $(grep -m1 -E '^FAIL' "$out/sysprobe.txt" || echo 'see sysprobe.txt') ($out/sysprobe.txt)"
            fails=$((fails + 1))
        fi ;;
    vga)
        # w16's VGA.DRV (drivers/vga) with Microsoft's resources linked in
        # (tests/vgamsres.sh, made in this run's directory for this part)
        # on Microsoft's KRNL286, DOSX, USER and GDI under real input,
        # every screen against the same script with Microsoft's VGA.DRV
        # (helpmenus: Notepad's caret left out; icons: Clock's icon; tabs,
        # kbtype: the caret is drawn by the driver on both sides and
        # compared)
        r=ok
        for s in ${part:-kbtype tabs alttab helpmenus pmkeys icons menus dragoff pmitems pmclose mdimax about}; do
            vdrv=$out/vga-msres-$s.drv
            "$root/tests/vgamsres.sh" "$vdrv" > "$out/vga-msres-$s.txt" 2>&1 || { r="$s: tests/vgamsres.sh failed"; break; }
            scr=$root/tests/scripts/input/$s.txt
            case $s in
            helpmenus) m="rectangle 14,42 20,61" ;;
            icons) m="rectangle 160,400 220,445" ;;
            # the About box's free memory: w16's driver is smaller
            about) m="rectangle 318,334 380,346" ;;
            *) m="rectangle 0,0 0,0" ;;
            esac
            b=25 t=220
            case $s in pmkeys|pmitems|pmclose|mdimax) b=40 t=240 ;; esac
            [ $s = icons ] && t=380
            refrun vga-$s-ref vga "$root/tests/inputrun.sh" -s -b $b -t $t "$scr" "$out/vga-$s-ref" -r
            "$root/tests/inputrun.sh" -e "$out/vga-$s-ref/boot.png" -E "$out/vga-$s-ref" -M "$m" -b $b -t $t "$scr" "$out/vga-$s-w16" -r \
                -f "$vdrv=WINDOWS/SYSTEM/VGA.DRV" > "$out/vga-$s-w16.txt" 2>&1 || true
            rm -f "$vdrv"
            ls "$out/vga-$s-ref"/[0-9]*.png > /dev/null 2>&1 || { r="$s: no screens"; break; }
            for f in "$out/vga-$s-ref"/[0-9]*.png; do
                g=$(basename "$f")
                [ -f "$out/vga-$s-w16/$g" ] || { r="$s: no $g"; break; }
                magick "$f" -fill black -draw "$m" "$out/vga-$s-a.png"
                magick "$out/vga-$s-w16/$g" -fill black -draw "$m" "$out/vga-$s-b.png"
                n=$(magick compare -metric AE "$out/vga-$s-b.png" "$out/vga-$s-a.png" "$out/vga-$s-${g%.png}-diff.png" 2>&1 >/dev/null || true)
                n=${n%% *}
                [ "$n" = 0 ] || { r="$s: $g: $n pixels differ"; break; }
            done
            [ "$r" = ok ] || break
        done
        if [ -n "$part" ]; then partdone "$r"
        elif [ "$r" = ok ]; then echo "ok   vga: 0 pixels (12 scripts with w16's VGA.DRV)"
        else echo "FAIL vga: $r ($out)"; fails=$((fails + 1)); fi ;;
    vgaboot)
        # plain boots: w16's VGA.DRV with Microsoft's resources against the
        # all-Microsoft boot (0 pixels), and with its own resources (w16's
        # art: the boot must complete)
        ref
        "$root/tests/vgamsres.sh" "$out/vgaboot-msres.drv" > "$out/vgaboot-msres.txt" 2>&1 || true
        boot vgaboot -r -f "$out/vgaboot-msres.drv=WINDOWS/SYSTEM/VGA.DRV"
        rm -f "$out/vgaboot-msres.drv"
        check vgaboot vgaboot ref
        boot vgabootown -r -f "$root/build/dos/VGA.DRV=WINDOWS/SYSTEM/VGA.DRV"
        if booted vgabootown; then echo "ok   vgaboot (own resources): the boot completes"
        else echo "FAIL vgaboot (own resources): the boot did not complete ($out/vgabootown.txt)"; fails=$((fails + 1)); fi ;;
    vgaall)
        # every w16 part (WIN.COM, DOSX, KRNL286, USER, GDI, PROGMAN,
        # fonts) with w16's VGA.DRV (Microsoft's resources) against the same
        # with Microsoft's VGA.DRV, the kbtype script (not cached: both
        # sides are w16's but for VGA.DRV)
        fon=$root/build/fonts
        scr=$root/tests/scripts/input/kbtype.txt
        "$root/tests/vgamsres.sh" "$out/vgaall-msres.drv" > "$out/vgaall-msres.txt" 2>&1 || true
        for k in ref w16; do
            if [ $k = w16 ]; then set -- -e "$out/vgaall-ref/boot.png" -E "$out/vgaall-ref" -b 30 -t 240 "$scr" "$out/vgaall-$k" \
                -f "$out/vgaall-msres.drv=WINDOWS/SYSTEM/VGA.DRV"
            else set -- -s -b 30 -t 240 "$scr" "$out/vgaall-$k"; fi
            "$root/tests/inputrun.sh" "$@" \
                -w -x -f "$user=WINDOWS/SYSTEM/USER.EXE" -f "$gdi=WINDOWS/SYSTEM/GDI.EXE" \
                -f "$root/build/dos/PROGMAN.EXE=WINDOWS/PROGMAN.EXE" \
                -f "$fon/VGASYS.FON=WINDOWS/SYSTEM/VGASYS.FON" -f "$fon/VGAFIX.FON=WINDOWS/SYSTEM/VGAFIX.FON" \
                -f "$fon/VGAOEM.FON=WINDOWS/SYSTEM/VGAOEM.FON" -f "$fon/SSERIFE.FON=WINDOWS/SYSTEM/SSERIFE.FON" \
                > "$out/vgaall-$k.txt" 2>&1 || true
        done
        rm -f "$out/vgaall-msres.drv"
        r=ok
        ls "$out/vgaall-ref"/[0-9]*.png > /dev/null 2>&1 || r="no screens"
        for f in "$out/vgaall-ref"/[0-9]*.png; do
            [ "$r" = ok ] || break
            g=$(basename "$f")
            [ -f "$out/vgaall-w16/$g" ] || { r="no $g"; break; }
            n=$(magick compare -metric AE "$out/vgaall-w16/$g" "$f" "$out/vgaall-${g%.png}-diff.png" 2>&1 >/dev/null || true)
            n=${n%% *}
            [ "$n" = 0 ] || { r="$g: $n pixels differ"; break; }
        done
        if [ "$r" = ok ]; then echo "ok   vgaall: 0 pixels (every w16 part, w16's VGA.DRV against Microsoft's)"
        else echo "FAIL vgaall: $r ($out)"; fails=$((fails + 1)); fi ;;
    vgawoados)
        # the MS-DOS Prompt (Standard mode: GDI's Disable puts the screen
        # in text mode for the DOS program, Enable sets it up again) with
        # w16's VGA.DRV (Microsoft's resources) on Microsoft's stack
        # against all-Microsoft, as kbdwoados
        scr=$root/tests/scripts/input/woados.txt
        set -- -i 'NonWindowsApp:SwapDisk=C:\WINDOWS\TEMP'
        "$root/tests/vgamsres.sh" "$out/vgawoados-msres.drv" > "$out/vgawoados-msres.txt" 2>&1 || true
        refrun vgawoados-ref vgawoados env INPUTRUN_STAY=1 "$root/tests/inputrun.sh" -b 30 -t 130 "$scr" "$out/vgawoados-ref" -r "$@"
        INPUTRUN_STAY=1 "$root/tests/inputrun.sh" -b 30 -t 130 "$scr" "$out/vgawoados-w16" -r "$@" \
            -f "$out/vgawoados-msres.drv=WINDOWS/SYSTEM/VGA.DRV" > "$out/vgawoados-w16.txt" 2>&1 || true
        rm -f "$out/vgawoados-msres.drv"
        a=$out/vgawoados-w16 b=$out/vgawoados-ref r=ok
        for s in 01-dos:640x400 02-ver:640x400 03-back:640x480 04-exit:640x400; do
            f=${s%%:*} want=${s#*:}
            [ -f "$a/$f.png" ] && [ -f "$b/$f.png" ] || { r="no $f"; break; }
            sz=$(magick identify -format '%wx%h' "$a/$f.png" 2>/dev/null)
            [ "$sz" = "$want" ] || { r="$f: screen $sz, not $want"; break; }
            if [ $f = 03-back ]; then
                n=$(magick compare -metric AE "$a/$f.png" "$a/00-before.png" "$out/vgawoados-$f-diff.png" 2>&1 >/dev/null || true)
            else
                n=$(magick compare -metric AE "$a/$f.png" "$b/$f.png" "$out/vgawoados-$f-diff.png" 2>&1 >/dev/null || true)
            fi
            n=${n%%[. ]*}
            [ "${n:-99999}" -le 40 ] || { r="$f: $n pixels differ"; break; }
        done
        if [ "$r" = ok ]; then echo "ok   vgawoados: the MS-DOS Prompt and back with w16's VGA.DRV"
        else echo "FAIL vgawoados: $r ($out/vgawoados-w16)"; fails=$((fails + 1)); fi ;;
    vgaprobe)
        # VG1 as the shell: the driver's values as programs see them, w16's
        # VGA.DRV with its own resources against Microsoft's (not cached)
        for k in ms w16; do
            set --; [ $k = w16 ] && set -- -w
            "$root/tests/vgaprobe.sh" "$@" -p "$root/build/dos/VG1.EXE" -t 300 "$out/vgaprobe-$k" \
                > "$out/vgaprobe-$k.txt" 2>&1 || true
        done
        if [ ! -s "$out/vgaprobe-ms/TK.LOG" ] || ! grep -q '^end' "$out/vgaprobe-ms/TK.LOG"; then
            echo "FAIL vgaprobe: no complete log with Microsoft's VGA.DRV ($out/vgaprobe-ms)"; fails=$((fails + 1))
        elif cmp -s "$out/vgaprobe-ms/TK.LOG" "$out/vgaprobe-w16/TK.LOG"; then
            echo "ok   vgaprobe: VG1's logs identical ($(grep -c '^c ' "$out/vgaprobe-ms/TK.LOG") colours)"
        else
            echo "FAIL vgaprobe: VG1's logs differ ($out/vgaprobe-ms, $out/vgaprobe-w16)"; fails=$((fails + 1))
        fi ;;
    *) echo "unknown case $c" >&2; exit 2 ;;
    esac
}

# The job pool: up to BOOTTEST_JOBS cases at a time (default 3), each in a
# subshell with its output in $out/.case-N.out, printed in case order as
# the cases finish. Every DOSBox runs on a display of its own
# (tests/xvfb.sh); references two cases share wait for each other
# (refrun's lock); retries are appended under a lock.
for c in $cases; do
    case $(codeof "${c%@*}" | head -1) in
    "    "*")"*) ;;
    *) echo "unknown case $c" >&2; exit 2 ;;
    esac
done
# Parts. A case made of several independent sessions is split into parts
# that are scheduled like cases (CASE@PART, run by name too): allw16nolock
# (one part per run, its deliberate timings unchanged), pmdrop (w16
# PROGMAN; PROGMAN and SHELL; USER and GDI), sbdrag (US19; Notepad), winfile and runbrowse (the comparison;
# every w16 part), kbd and mouse (one part per script). A part records its result (partdone) and prints only
# its own details; when the last part of a case is printed, the case's
# own ok or FAIL line follows (partsummary) and counts once.
caseparts() {
    case $1 in
    allw16nolock) seq -f "$1@%g" 1 "${NOLOCK_RUNS:-6}" ;;
    pmdrop) echo "$1@pm $1@sh $1@us" ;;
    sbdrag) echo "$1@us $1@np" ;;
    edithandle) echo "$1@us $1@np" ;;
    winfile) echo "$1@cmp $1@all" ;;
    runbrowse) echo "$1@cmp $1@all" ;;
    kbd) echo "$1@kbtype $1@tabs $1@alttab $1@helpmenus $1@pmkeys $1@icons $1@menus" ;;
    mouse) echo "$1@dragoff $1@pmitems $1@pmclose $1@helpmenus" ;;
    vga) for v in kbtype tabs alttab helpmenus pmkeys icons menus dragoff pmitems pmclose mdimax about; do printf '%s@%s ' "$1" $v; done ;;
    *) echo "$1" ;;
    esac
}
# partdone RESULT: this part's result, "ok" or what failed
partdone() { echo "$1" > "$out/.part-$c-$part"; }
# partsummary CASE: the case's line from its parts' results
partsummary() {
    pt=0 pbad=0 pfirst=
    for pp in $(grep "^$1@" "$out/.names"); do   # the parts this run has
        pt=$((pt + 1))
        pr=$(cat "$out/.part-$1-${pp#*@}" 2>/dev/null || echo "part ${pp#*@} stopped before its end")
        [ "$pr" = ok ] && continue
        pbad=$((pbad + 1)); [ -n "$pfirst" ] || pfirst=$pr
    done
    case $1 in
    allw16nolock)
        if [ $pbad = 0 ]; then echo "ok   $1: $pt of $pt runs: menus and pointer respond"
        else echo "FAIL $1: $pbad of $pt runs failed"; fi ;;
    pmdrop)
        if [ $pbad = 0 ]; then echo "ok   pmdrop: 0 pixels, MAIN.GRP identical (w16 PROGMAN; w16 PROGMAN and SHELL; w16 USER and GDI)"
        else echo "FAIL pmdrop: $pfirst"; fi ;;
    edithandle)
        if [ $pbad = 0 ]; then echo "ok   edithandle: US20 as 3.1; Notepad opens, edits and saves a 31500-byte file as on 3.1 (0 pixels, same bytes)"
        else echo "FAIL edithandle: $pfirst"; fi ;;
    sbdrag)
        if [ $pbad = 0 ]; then echo "ok   sbdrag: 0 pixels mid-drag (US19's four bars, Notepad), US19's messages as 3.1's"
        else echo "FAIL sbdrag: $pfirst"; fi ;;
    kbd)
        if [ $pbad = 0 ]; then echo "ok   kbd: 0 pixels (kbtype, tabs, alttab, helpmenus, pmkeys, icons, menus with w16's KEYBOARD.DRV)"
        else echo "FAIL kbd: $pfirst ($out)"; fi ;;
    mouse)
        if [ $pbad = 0 ]; then echo "ok   mouse: 0 pixels (dragoff, pmitems, pmclose, helpmenus with w16's MOUSE.DRV)"
        else echo "FAIL mouse: $pfirst ($out)"; fi ;;
    vga)
        if [ $pbad = 0 ]; then echo "ok   vga: 0 pixels ($pt scripts with w16's VGA.DRV)"
        else echo "FAIL vga: $pfirst ($out)"; fi ;;
    winfile)
        if [ $pbad = 0 ]; then echo "ok   winfile: 0 pixels (focus dots, status bar, error box left out); every w16 part: no fault"
        else echo "FAIL winfile: $pfirst ($out)"; fi ;;
    runbrowse)
        if [ $pbad = 0 ]; then echo "ok   runbrowse: w16 USER and GDI within the known pixels; every w16 part: no fault, Browse into SYSTEM and back"
        else echo "FAIL runbrowse: $pfirst ($out)"; fi ;;
    esac
    [ $pbad = 0 ]
}
expanded=
for c in $cases; do
    case $c in *@*) expanded="$expanded $c" ;; *) expanded="$expanded $(caseparts "$c")" ;; esac
done
cases=$expanded
jobs=${BOOTTEST_JOBS:-3}
n=0 next=1 pids=
# flush: print the output of the cases finished so far, in order
flush() {
    while [ -f "$out/.case-$next.done" ]; do
        cat "$out/.case-$next.out"
        if [ -f "$out/.case-$next.fails" ]; then
            fails=$((fails + $(cat "$out/.case-$next.fails")))
        else
            echo "FAIL $(cut -d' ' -f1 "$out/.case-$next.time"): the case stopped before its end"
            fails=$((fails + 1))
        fi
        cat "$out/.case-$next.time" >> "$out/times.txt"
        # the last part of a case: the case's line
        pc=$(sed -n "${next}p" "$out/.names")
        case $pc in *@*)
            if [ "$(sed -n "$((next + 1))p" "$out/.names" | sed 's/@.*//')" != "${pc%@*}" ] &&
               ! partsummary "${pc%@*}"; then fails=$((fails + 1)); fi ;;
        esac
        next=$((next + 1))
    done
}
running() {
    r=0
    for p in $pids; do kill -0 "$p" 2>/dev/null && r=$((r + 1)); done
    echo $r
}
# The longest cases start first (their times in the last run,
# build/boottest-times.txt; a case not timed yet counts as long), so that
# the run does not end with one long case alone; the output keeps the
# case order. Each case is "N CASE": N its place in that order.
timesfile=$root/build/boottest-times.txt
order=$(i=0; for c in $cases; do
    i=$((i + 1))
    t=
    [ -f "$timesfile" ] && t=$(awk -v c="$c" '$1 == c { t = $2 } END { print t }' "$timesfile")
    echo "${t:-99999} $i $c"
done | sort -k1,1nr -k2,2n | cut -d' ' -f2-)
total=$(echo "$cases" | wc -w)
for c in $cases; do echo "$c"; done > "$out/.names"
echo "$order" | while read -r n c; do echo "$n $c"; done > "$out/.order"
while read -r n c; do
    while [ "$(running)" -ge "$jobs" ]; do flush; sleep 1; done
    (
        # (cases use c, n and the like for their own purposes)
        _bt_c=$c _bt_n=$n _bt_t=$(date +%s) fails=0
        trap '[ -f "$out/.case-$_bt_n.fails" ] || fails=1; refkeep; echo "$_bt_c $(($(date +%s) - _bt_t))" > "$out/.case-$_bt_n.time"; touch "$out/.case-$_bt_n.done"' EXIT
        runcase "$c" > "$out/.case-$_bt_n.out" 2>&1
        echo "$fails" > "$out/.case-$_bt_n.fails"
        refkeep
    ) < /dev/null &
    pids="$pids $!"
done < "$out/.order"
while [ $next -le $total ]; do flush; [ $next -gt $total ] || sleep 1; done
wait
# this run's times for the next run's order (cases not run keep theirs)
if [ -f "$timesfile" ]; then
    awk 'NR == FNR { new[$1] = 1; print; next } !($1 in new)' "$out/times.txt" "$timesfile"
else
    cat "$out/times.txt"
fi > "$timesfile.new.$$" && mv "$timesfile.new.$$" "$timesfile"
# each case's time, and the references taken from the cache
echo "boottest: seconds per case:"
awk '{ printf "  %-16s %5d\n", $1, $2; t += $2 } END { printf "  %-16s %5d\n", "(all)", t }' "$out/times.txt"
if [ -s "$out/refcache.log" ]; then
    echo "boottest: references: $(grep -c 'from the cache' "$out/refcache.log") from the cache, $(grep -c ' ran' "$out/refcache.log") run, $(grep -c ' stored$' "$out/refcache.log") stored ($out/refcache.log)"
fi
# input sessions whose DOSBox-X was restarted at its start (inputrun.sh)
for f in "$out"/*/input.log; do
    [ -f "$f" ] && grep -q '^emulator restarted' "$f" || continue
    d=$(basename "$(dirname "$f")")
    retried "$d" emulator-start "$(grep -c '^emulator restarted' "$f") restart(s): Couldn't open X11 display"
done
# retries: this run's, and cases that have needed them repeatedly
if [ -s "$out/retries.txt" ]; then
    echo "boottest: $(wc -l < "$out/retries.txt") retr$( [ "$(wc -l < "$out/retries.txt")" = 1 ] && echo y || echo ies) this run (see $out/retries.txt)"
fi
if [ -s "$retrylog" ]; then
    awk '{print $4}' "$retrylog" | sort | uniq -c | while read n c; do
        if [ "$n" -ge 3 ]; then
            echo "WARNING: $c has needed a retry $n times so far ($retrylog): investigate, it may be an intermittent bug"
        fi
    done
fi
echo "boottest: $fails failure(s); results in $out"
exit "$fails"
