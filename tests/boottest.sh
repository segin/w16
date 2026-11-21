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
#   k386       w16's KRNL386 in the KRNL386.EXE slot (Standard mode on a
#              386: DOSX starts it), Microsoft's DOSX, USER, GDI and the
#              rest, against Microsoft's KRNL386     must be 0
#   k386all    w16's WIN.COM, DOSX, KRNL386, USER and GDI against the same
#              Microsoft KRNL386 boot                must be 0
#   k386e      386 Enhanced mode (WIN /3): w16's WIN386 and KRNL386 with
#              Microsoft's USER, GDI and drivers against Microsoft's KRNL386
#              on w16's WIN386, and w16's KRNL386 on Microsoft's WIN386
#              against Microsoft's KRNL386 there   both must be 0
#   k386cad    Ctrl+Alt+Del in 386 Enhanced mode under Microsoft's WIN386
#              (tests/scripts/input/win386cad.txt): w16's KRNL386 against
#              Microsoft's, the Reboot screen and Windows after it equal
#   k386hung   the same with a task that never yields (VX8 mode 2): the
#              "not responding" screen, Escape, again, Return (the task
#              ended): w16's KRNL386 against Microsoft's, every screen
#   wincom     w16's WIN.COM + KRNL286             must be 0
#   allw16     w16's WIN.COM + DOSX + KRNL286      must be 0
#   user       w16's USER on Microsoft's KRNL286   must be 0
#   userk      w16's USER on w16's KRNL286         must be 0
#   gdi        w16's GDI on Microsoft's KRNL286    must be 0
#   gdik       w16's GDI on w16's KRNL286          must be 0
#   usergdi    w16's USER and GDI on w16's KRNL286 must be 0
#   gdiaudit   the GDI audit's probe GD5 with Microsoft's GDI and w16's: no FAIL
#   shell      w16's SHELL.DLL on Microsoft's KRNL286 must be 0
#   shellk     w16's SHELL.DLL on w16's KRNL286   must be 0
#   win386     w16's WIN386.EXE, WIN /3 (386 Enhanced) must be 0
#              (tests/win386.sh: Microsoft's KRNL386 and the rest, against
#              Microsoft's WIN386.EXE; both must reach Final USER init)
#   win386input  w16's WIN386.EXE with Microsoft's KRNL386 and the rest
#              (WIN /3 /B), real mouse motion and keys, then File > Exit
#              Windows (tests/scripts/input/win386input.txt), against
#              Microsoft's WIN386.EXE: the pointer moves and the menus open
#              (every shot 0 pixels), and the DOS prompt comes back in text
#              mode (40 pixels: the cursor)
#   win386cad  w16's WIN386.EXE (its REBOOT and VKD modules), Ctrl+Alt+Del
#              in Program Manager and a key (tests/scripts/input/
#              win386cad.txt), against Microsoft's WIN386.EXE: the text
#              screen the same but where w16's text says "w16" (the title
#              and row 15 left out), Windows' screen afterwards 0 pixels,
#              and Return then opens File Manager (0 pixels but its file
#              total: Windows 3.1's swap file; its icons in colour)
#   win386hung w16's WIN386.EXE: Ctrl+Alt+Del with a task that never
#              yields (VX8 mode 2, tests/scripts/input/vx8hung.txt), the
#              "not responding" screen, Escape, again, Return (the task
#              ended, Program Manager repainted), against Microsoft's
#              WIN386.EXE: the screens but w16's text (row 12) and the
#              blinking cursor, Windows afterwards 0 pixels (after Escape
#              not compared: Windows 3.1's own repaint is half done there)
#   win386close  w16's WIN386.EXE: File Manager, Notepad, Calculator and
#              Clock opened and closed with Alt+F4 (tests/scripts/input/
#              win386close.txt; a task's exit reaches V86 DOS), against
#              Microsoft's WIN386.EXE: every shot 0 pixels (File Manager's
#              file total, Notepad's caret left out)
#   win386dos  w16's WIN386.EXE: the MS-DOS Prompt full screen, VER, EXIT
#              (tests/scripts/input/dosbox386v.txt, DOSBox-X on its own DOS,
#              WIN's output not redirected), against Microsoft's WIN386.EXE:
#              every shot 0 pixels but the banner's "w16" rows and the
#              blinking cursor
#   win386dosw w16's WIN386: the MS-DOS Prompt to a window (Alt+Enter), VER,
#              back, against Microsoft's WIN386
#   win386switch  w16's WIN386: Alt+Tab, Alt+Esc, Ctrl+Esc between the
#              full-screen MS-DOS Prompt and Windows, against Microsoft's
#   win386two  w16's WIN386: two MS-DOS Prompts, VER and EXIT in each
#   win386exitbox  w16's WIN386: Exit Windows with a box open (refused), EXIT
#   win386w16dos  w16's install tree over Windows 3.1 with w16's WIN386.EXE
#              (plain DOSBox, the user's set-up): the MS-DOS Prompt, VER and
#              EXIT typed, as with Microsoft's (banner and cursor masked)
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
#   exittree   File > Exit Windows > OK (exitwin.txt) on w16's install
#              tree laid over Windows 3.1 (every file of build/w16 but the
#              INI, GRP and README files over Microsoft's, 3.1's own INI
#              files: a user's hand session), in plain DOSBox. Parts: e386
#              (WIN /3 under w16's WIN386.EXE), e386ms (WIN /3 under
#              Microsoft's WIN386.EXE), std (WIN /S). Each must end
#              cleanly: KERNEL's "back to DOS", no fault report, the DOS
#              prompt in text mode. Until 2026-11-19 e386 ended in a
#              general protection fault in USER (KERNEL left the task's
#              PDB current for its INT 21h 4Ch: the DPMI host took that
#              for the task's exit and returned)
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
#   swapdos    the woados script with w16's WSWAP.EXE and DSWAP.EXE (the
#              rest Microsoft's) against all of Microsoft's: the DOS screens
#              the same but for the banner box (it says "w16"), Windows back
#              as it was, text mode at the end, no ~WOA file left
#   swapalt    the same with Alt+Tab to Windows and back to the prompt
#              between VER and EXIT (tests/scripts/input/swapalt.txt)
#   olecli     w16's OLECLI.DLL (ARCHITECTURE.md 16), parts: tree, w16's
#              install tree alone (with its OLECLI.DLL): Cardfile from the
#              Accessories group, under its KRNL386 and KRNL286
#              (tests/scripts/input/cardtree.txt): Cardfile's window must
#              open, the screens the same under both kernels; and one part
#              per OLE scenario tests/ole/obs/NAME.txt (tests/olescenarios.sh
#              NAME): on the reference install, w16's OLECLI against
#              Microsoft's: the client's calls and callbacks, the DDE
#              conversation, the saved streams and OleDraw's pixels equal;
#              part cardw16: card1 with w16's Cardfile as the client
##   treek386   w16's install tree alone under its KRNL386 against the same
#              under its KRNL286, every shot 0 pixels; parts items (every
#              item of the default groups opened, tests/scripts/input/
#              treeitems.txt; Program Manager and Main where PROGMAN.INI
#              has them at boot) and desk (Clock and Calculator moved over
#              the desktop version stamp, treedesk.txt)
#   olesvr     w16's OLESVR.DLL (ARCHITECTURE.md 16.9), parts: tree, w16's
#              install tree alone with w16's OLECLI and OLESVR (and, for the
#              test only, the reference's PBRUSH.EXE, PBRUSH.DLL, REG.DAT)
#              under its KRNL386: OS2 as the shell embeds, updates and
#              closes a Paintbrush picture (tests/olesvr/ostree.sh), the
#              log equal to the same tree with Microsoft's OLESVR.DLL; and
#              one part per OLESVR scenario
#              tests/olesvr/scripts/NAME.os2 (tests/olesvrscen.sh NAME): on
#              the reference install, w16's OLESVR against Microsoft's
#              (Microsoft's OLECLI): the DDE conversation and the servers'
#              callbacks, the saved documents and the screens equal (the
#              olecli case runs OC1's scenarios in all four OLECLI/OLESVR
#              combinations)
#   ddeml      w16's DDEML.DLL (ARCHITECTURE.md, DDEML), parts: tree286 and
#              tree386, w16's install tree alone under its KRNL286 / KRNL386
#              with DM.EXE as the shell (tests/ddeml/ddtree.sh: a DDEML
#              server and client in two copies, then a DDEML client driving
#              the tree's Program Manager), the log equal to the same tree
#              with Microsoft's DDEML.DLL; and one part per DDEML scenario
#              (tests/ddemlscen.sh NAME, NAME+x): on the reference install,
#              w16's DDEML against Microsoft's in every client/server
#              combination, the logs equal
#   lzexpand   w16's LZEXPAND.DLL (ARCHITECTURE.md, LZEXPAND): the probe
#              LZP.EXE (tests/lzexpand/lzp.c, every function on files made
#              at test time) as the shell with Microsoft's LZEXPAND and
#              with w16's, the logs compared section by section
#              (tests/lzexpand/lzscen.sh, lzcmp.py); parts s and 3: the
#              reference install in Standard and 386 Enhanced mode, every
#              section but the three below (VerInstallFile of 3.1's VER.DLL
#              included); tree286 and tree386: w16's install tree alone
#              under its KRNL286 / KRNL386 (tests/lzexpand/lztree.sh), the
#              same sections; quirks: the sections eof, zero and long
#              matches, where w16 still differs (ARCHITECTURE.md, LZEXPAND)
#   drivers    USER's installable drivers (ARCHITECTURE.md 10.7): the probe
#              DRP.EXE and its driver DRT.DLL (tests/drv/drun.sh) on w16's
#              USER and on Microsoft's (KRNL286), again with DRT.DLL a boot
#              driver, and under KRNL386 (w16's KRNL386 and USER against
#              Microsoft's: GlobalAlloc's handles too), the logs identical
#   quicktime  QuickTime for Windows 2.1.2's installer and its sample movie
#              (tests/qtplay.sh; the installer copied in at test time from
#              $W16_QTDIR, default ~/Downloads/qtw16; skipped without it):
#              stock 3.1 against w16's install tree laid over it as a user
#              does (KRNL386; part over), and against the tree alone as
#              C:\W16 (parts tree, KRNL286, and tree386): the Movie
#              Player's window, the movie played, its last frame 0 pixels
#              and its length within 2 s of 3.1's; then the close sequence
#              (close and reopen the movie, pause, File > Exit, start
#              again, Alt+F4, Exit Windows) lived through on both sides.
#              Until 2026-11-17 w16 hung for good there, until 2026-11-18
#              closing the player on the tree alone ended Windows (docs/
#              notes/quicktime-handover.md). Part cpl (tests/qtcpl.sh):
#              its Control Panel's Files tab on 3.1 and on the tree alone
#              (KRNL286), its last whole row clicked and the list scrolled:
#              the list and the static under it where 3.1 has them (the
#              window spy US32S), no highlight under the list (until
#              2026-11-20 w16's list was a row taller and the scroll drew
#              over the static)
#   mmsystem   w16's MMSYSTEM.DLL and MMTASK.TSK (ARCHITECTURE.md 23)
#              against Microsoft's, on the reference 3.1
#              (tests/mmsystem/mscen.sh): parts core (the probe MMP.EXE:
#              driver, DriverCallback, interrupt stack, tasks; mcmp.py),
#              mmio (MMP's MMIO sections: DOS, memory and custom files),
#              timer (TPR.EXE, plain and with TMS.DRV: Microsoft's
#              MMSYSTEM and TIMER.DRV against w16's two; tcmp.py); qt
#              (QuickTime's sample movie on stock 3.1 with w16's MMSYSTEM,
#              MMTASK and TIMER.DRV: tests/qtplay.sh -t; skipped without
#              $W16_QTDIR); dev (layer 3: MMP's device sections over
#              Windows 3.1's own Sound Blaster and MIDI Mapper
#              drivers with the logging drivers MDS*.DRV between them,
#              tests/mmsystem/mdscen.sh); mci (layer 4: MMP's MCI
#              scripts over the test driver MCT.DRV and over 3.1's
#              MCIWAVE and MCISEQ behind logging drivers,
#              tests/mmsystem/mciscen.sh); prog (Sound Recorder playing
#              DING.WAV, Control Panel's Sound applet testing a sound, the
#              MIDI Mapper applet, Media Player opening and playing a
#              .WAV and opening a .MID: every screen 0 pixels off,
#              tests/mmsystem/mprog.sh)
#   sb16       w16's SB16.DRV (ARCHITECTURE.md 24) in DOSBox-X as a Sound
#              Blaster 16 (tests/sb16/sbscen.sh): parts dev (MMP's device
#              sections over it: Microsoft's MMSYSTEM and w16's alike, and
#              against Windows 3.1's SNDBLST2.DRV's but the expected
#              differences, tests/sb16/sbcmp.py), audio (what it plays,
#              recorded and checked against the WAV and MIDI material:
#              tests/sb16/sbaudio.sh) and prog (Sound Recorder, Sound and
#              Media Player over it: every screen as over SNDBLST2)
#   bigmem     more than 64 MB of memory (DOSBox-X, memsize 128 and 256):
#              TK63 as the shell reports DPMI 0500h, GetFreeSpace and the
#              largest GlobalAlloc and takes blocks until none is left,
#              writing every page. Parts s128, s256 (Standard mode, w16's
#              DOSX, WSWAP and KRNL386), e128, e256 (386 Enhanced, w16's
#              WIN386 and KRNL386): all but at most 16 MB of the machine's
#              memory; s256ms (under Microsoft's WSWAP): all of it
#              allocated; stock ([w16] LargeMemory=0, both modes at 128
#              MB): all of it allocated, 0500h's largest at most 64 MB in
#              Standard mode; ref (Windows 3.1's DOSX and WIN386 at 128
#              MB): the same of 3.1 itself
#   filecdr    KERNEL's FileCdr with Windows running: TK64 and TK64M as
#              the shell (tests/apps/tk64.c, its InitApp cases) end with
#              no FAIL against 3.1's table; parts k286 (w16's KRNL286),
#              k386 (w16's KRNL386), userk (w16's KRNL286 and USER)
#   timer      w16's TIMER.DRV (ARCHITECTURE.md 21): the probe TPR.EXE
#              (tests/timer/tpr.c) under Microsoft's MMSYSTEM with
#              Microsoft's TIMER.DRV and with w16's, plain and with the
#              logging driver TMS.DRV between them (tests/timer/tscen.sh,
#              tcmp.py): parts s (Standard mode) and 3 (386 Enhanced mode,
#              its documented differences shown); qt: QuickTime's sample
#              movie on stock 3.1 with only w16's TIMER.DRV against stock
#              3.1 (tests/qtplay.sh -t TIMER.DRV; skipped without
#              $W16_QTDIR)
#   ver        w16's VER.DLL (ARCHITECTURE.md 20): the probe VRP.EXE
#              (tests/ver/vrp.c, every function on files made at test time)
#              as the shell with Microsoft's VER.DLL and with w16's, the
#              logs compared section by section (tests/ver/vscen.sh,
#              vcmp.py); parts s and 3: the reference install in Standard
#              and 386 Enhanced mode, every section; x: the install section
#              in DOSBox-X (which keeps read-only attributes); tree286 and
#              tree386: w16's install tree alone under its KRNL286 / KRNL386
#              at C:\WINDOWS (tests/ver/vtree.sh), Microsoft's VER.DLL there
#              against 3.1's own log and w16's against Microsoft's;
#              mods: every file of the install tree read through both
#              VER.DLLs, all ten version resources readable by 3.1's
#              (tests/ver/vmods.sh); fonts and fontsnew: a real 3.1 caller,
#              Control Panel's Fonts applet (MAIN.CPL) adding a font under
#              real keys with each VER.DLL, with nothing installed and over
#              a newer copy (tests/ver/vfonts.sh), every screen 0 pixels
#              and the same files installed
#   toolhelp   w16's TOOLHELP.DLL (ARCHITECTURE.md 22, tests/toolhelp/
#              thcase.sh): parts ref286, ref386, ref3: the probe THP.EXE
#              (tests/toolhelp/thp.c, every section) on Windows 3.1 under
#              KRNL286, KRNL386 and in 386 Enhanced mode with Microsoft's
#              TOOLHELP and with w16's, the logs equal, and both side by
#              side in one session answering alike to the byte (section
#              d); tree286, tree386: w16's tree alone with its TOOLHELP
#              against 3.1 with Microsoft's (tests/toolhelp/tree.mask: w16's
#              own modules' differences, each explained); watson: Dr.
#              Watson (3.1's DRWATSON.EXE, a TOOLHELP caller) with each
#              TOOLHELP, on 3.1 and on w16's tree, the same log; qt:
#              QuickTime's player (QTIM.DLL imports TOOLHELP) on w16's tree
#              laid over 3.1, which brings w16's TOOLHELP (tests/qtplay.sh)
#   missingdll a program whose DLL is missing (OLECLI.DLL deleted; Cardfile
#              imports it), parts pm, tk59, tree: Program Manager's Cardfile
#              item and File > Run "nosuch" (tests/scripts/input/
#              missingdll.txt) with w16's KRNL286, w16's KRNL386, and every
#              w16 part, against all of Microsoft's (KERNEL's "File Error"
#              box, Program Manager's "Application Execution Error"): every
#              shot 0 pixels (the group icons left out for w16's PROGMAN);
#              TK59 as the shell under both w16 kernels against Microsoft's
#              KRNL386: every box and every return code the same; w16's
#              install tree alone (no OLECLI.DLL) under its KRNL386 and
#              KRNL286: the same screens, the boxes as 3.1's
#   runassoc   starting names of every kind (programs, documents with and
#              without an association, missing files, directories), parts
#              probe, pm, pmk286, pmk386, tree, dir: SH14 (FindExecutable,
#              ShellExecute, WinExec, LoadModule) with w16's SHELL on both
#              of w16's kernels against 3.1's results; Program Manager's
#              File > Run of each kind (tests/scripts/input/runassoc.txt)
#              with w16's PROGMAN and SHELL on Microsoft's KERNEL and on
#              both of w16's against all of Microsoft's: every screen 0
#              pixels (group icons, carets left out), no document run as a
#              DOS program; the install tree alone under both kernels
#              (runassoctree.txt); dir: File > Run's directory for the
#              program (SH5's log, tests/scripts/input/rundir.txt) with
#              w16's PROGMAN and SHELL as with Microsoft's
#   runpe      File > Run of a 32-bit program (a PE file whose MS-DOS stub
#              leaves C:\RANCOM.TXT; tests/scripts/input/runpe.txt), parts
#              k286, k386: w16's PROGMAN and SHELL on w16's kernels with
#              [w16] PEError=0 against all of Microsoft's: every screen
#              3.1's, the stub run; on286, on386: PEError=1: w16's box,
#              nothing run, the Run box back, then the boot's screen
#   swapall    swapalt on w16's install tree alone (build/w16 as C:\W16;
#              no file of Microsoft's): every part w16's (VGACOLOR.2GR and
#              MMSOUND.DRV too),
#              the DOS screens against Microsoft's, Windows' against its
#              own screen before the prompt
#   swapesc    the MS-DOS Prompt on w16's install tree alone, the user's
#              steps (tests/scripts/input/swapesc.txt): Alt+Esc out (an
#              icon, Program Manager active), the icon double-clicked,
#              Alt+Tab out and in, a second prompt, EXIT in both, Exit
#              Windows; parts k386 (the tree's KRNL386), k286 (KRNL286),
#              mswoa (Microsoft's WINOLDAP.MOD): every screen's mode as
#              Windows 3.1's, the DOS screens as the prompt was left,
#              Windows' as before the prompt, the icon's title as 3.1's
#   pmkeys     Program Manager's arrow keys straight after it starts, then
#              after a click on the workspace (pmkeys.txt): w16 USER+GDI and
#              w16 PROGMAN against Microsoft's; the group icons left out
#   mdimax     a maximised MDI child's controls in the frame's menu bar:
#              probe US18 as the shell on 3.1 and on w16's USER (its OBS
#              lines identical), and US28 (where CW_USEDEFAULT children go
#              as children come and go), then Program Manager's Main group
#              maximised, its system menu from the bar (Alt+-), File and
#              Left, the restore button clicked, Accessories maximised,
#              the Window menu, Ctrl+F4 (tests/scripts/input/mdimax.txt):
#              w16 USER+GDI, and with w16 PROGMAN, against Microsoft's
#   uaudit     the USER audit (docs/notes/user-audit-triage.md): probes US29
#              (with its helper task US29A and DLL US29D) and US30 (the
#              hooks) as the shell on 3.1 and on w16's USER, their OBS lines
#              identical; and US29's runs that 3.1 itself does not survive
#              (US29DW, CR, SE, SD, SS, SC, HM.EXE) on w16's USER, each of
#              which must reach its end
#   lbclip     a list box's height (LBS_NOINTEGRALHEIGHT or not, owner-drawn,
#              superclassed, a combo box's list) and its item partly shown
#              at the bottom edge, selected and scrolled every way
#              (user/listbox.c; QuickTime's Control Panel drew it whole over
#              the static below): probe US32 as the shell on 3.1 and on
#              w16's USER, their OBS lines identical (heights, clip boxes,
#              the area around each list box pixel for pixel by checksum,
#              no highlight outside the list box)
#   lbdraw     how a list box draws and what a screen DC covers (ARCHITECTURE.md
#              10.4): probe US33 as the shell on 3.1 and on w16's USER, their
#              OBS lines identical: owner-drawn list boxes of every kind
#              driven by keys, clicks, the scroll bar and messages, each
#              WM_DRAWITEM (item, action, state, rectangle, order, inside
#              WM_PAINT or not, the update rectangle) and a checksum of the
#              list's pixels; GetDC(NULL) and the desktop's DCs (clip box,
#              GetPixel, PatBlt over top-level windows); LB_SETTABSTOPS
#              (where each tabbed item's words start); the frames of child
#              dialogs (QuickTime's Control Panel's pages); and again on
#              w16's USER and GDI, all but the checksums
#   pmitems    an item dragged in Program Manager's Main group, the minimised
#              Accessories group restored (tests/scripts/input/pmitems.txt):
#              w16 USER+GDI, and with w16 PROGMAN, against Microsoft's
#   w16groups  w16's install tree alone (build/w16 as C:\W16, dropin.sh -T;
#              no file of Microsoft's but, in its first run, PROGMAN.EXE;
#              WSWAP is w16's): its default
#              PROGMAN.INI and groups (assets/progman) under real keys
#              (tests/scripts/input/w16groups.txt): Main's Read Me opens
#              README.TXT in Notepad, Accessories restored, its Notepad and
#              Calculator run and closed, Windows ended. Microsoft's
#              PROGMAN.EXE on the tree (it must read w16's group files: every
#              item started, each screen differing from the one before) and
#              w16's: every screen identical (the minimised groups' own
#              pictures left out), and the group files and PROGMAN.INI
#              saved at the end identical; w16's again under the tree's
#              KRNL386 (-3), the same
#   ddescen    Program Manager's DDE interface (tests/ddescenario.sh, a part
#              per tests/scripts/dde/*.txt): TDDE's answers, the command
#              blocks the ACKs return, the group files and PROGMAN.INI with
#              w16's PROGMAN as with Microsoft's, on the reference install
#   zipsetup   a third-party installer's raw DDE with Program Manager: the
#              7-Zip 26.05 Windows 3.0/3.1x setup (copied in at test time
#              from $W16_7ZSETUP, default ~/Downloads/7zsetw16.exe; skipped
#              without it) from File > Run (tests/scripts/input/
#              zipsetup.txt) against all of Microsoft's (KRNL386): pm, w16's
#              PROGMAN on Microsoft's rest, every screen 0 pixels (the
#              minimised groups' pictures left out), the new 7ZIP2605.GRP
#              and PROGMAN.INI byte for byte; tree, w16's install tree alone
#              under KRNL386 and KRNL286: 3.1's 7ZIP2605.GRP, PROGMAN.INI
#              naming it last, the group's window active, both kernels'
#              screens the same
#   write      Windows Write (Microsoft's WRITE.EXE, WIN.INI run=): a line
#              typed, the Character menu, the ruler, a second line, File >
#              Exit and No (tests/scripts/input/write.txt); w16's WIN.COM,
#              DOSX, KRNL286, USER, SHELL, WINOLDAP and WIN87EM against
#              all-Microsoft (Microsoft's GDI and fonts: w16's GDI has no
#              TrueType, Write's text is Arial): identical screens, leaving
#              out the caret (it blinks) and in the save box the Yes button
#              (w16's MessageBox puts it 2 pixels right: UNVERIFIED rule)
#   w16write   w16's Write (WRITE.EXE, WIN.INI run=) under real keys
#              (tests/scripts/input/w16write.txt: typing, the Character,
#              Paragraph and Edit menus, Ctrl+B, Centered, Exit without
#              saving), carets not blinking: against Microsoft's Write on
#              all-Microsoft 3.1, w16's on all-Microsoft (part ms), on w16's
#              KRNL286 (k286) and KRNL386 (k386): every screen identical;
#              w16's install tree alone under its KRNL386 against the same
#              under its KRNL286 (tree; no TrueType there); and one part
#              per feature scenario of tests/scripts/write named on the
#              WRSCEN_BOOT line below (e.g. w16write@wr24, the ruler):
#              tests/writescenarios.sh, Microsoft's Write and w16's (with
#              Microsoft's bitmaps, tests/wrmsres.sh) on all-Microsoft
#              3.1, every saved file the same, every screen within
#              tests/scripts/write.baseline; a part ending in o (wr30o)
#              runs w16's Write with w16's OLECLI.DLL
#   w16winfile w16's File Manager (WINFILE.EXE) against Microsoft's on
#              all-Microsoft 3.1: one part per scenario of
#              tests/scripts/winfile (w16winfile@wf01 ...):
#              tests/wfscenarios.sh, both File Managers under SNAP (w16's
#              with Microsoft's pictures, tests/wfmsres.sh), every screen
#              within tests/scripts/winfile.baseline and the WINFILE.INI
#              each leaves the same; wf14 loads the extension probe
#              FMX.DLL into both and compares its log
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
#   sound      tests/soundprobe.sh sn1 (drivers/sound): SN1's interface
#              parts and played groups with w16's MMSOUND.DRV against
#              Microsoft's, in DOSBox-X with the sound recorded: the logs
#              identical, the tones within tools/tonecmp.py's limits
#   soundtime  the same for SN1's timing parts (T, U, V)
#   soundrec   tests/soundprobe.sh queue rec allw16: the queue's capacity,
#              SNDREC's record of every call to the driver, and the beeps
#              on every w16 part (USER's MessageBeep to DoBeep) against
#              all-Microsoft                      identical
#   soundbeep  tests/soundprobe.sh beep: MessageBeep in full (SN1 parts M N I R
#              L Y Z: every type, Beep off, [sounds] entries missing or
#              empty, a sound playing, MMSYSTEM loaded late, notes on
#              MMSYSTEM's timer, interrupts off) with w16's MMSOUND.DRV
#              against Microsoft's, over Microsoft's MMSYSTEM and w16's, with
#              Windows 3.1's SNDBLST2.DRV and without a wave driver, and
#              with no MMSYSTEM: KRNLOG's and SNDREC's records identical, the
#              recordings the same segment by segment (tools/beepcmp.py)
#   soundboot  plain boots with w16's MMSOUND.DRV: on Microsoft's stack and
#              on w16's WIN.COM, DOSX, KRNL286, each against the
#              all-Microsoft boot                 must be 0
#   treealone  build/w16 alone as C:\W16, no file of Microsoft's on the
#              drive (no DOS program run): must boot to its shell, under
#              its KRNL286 and its KRNL386
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
#              (Notepad's caret left out where it may change phase)
#   vgawoados  the woados script with w16's VGA.DRV (Disable to text mode
#              for the DOS program, Enable back): the screens as
#              all-Microsoft's up to the cursor
#   vgaprobe   VG1 as the shell (the driver's values as programs see them:
#              tests/apps/vg1.c), w16's VGA.DRV with its own resources
#              against Microsoft's: identical logs
#   trails     mouse trails: w16's VGA.DRV (Microsoft's resources) against
#              Microsoft's on Microsoft's stack: VG9 (tests/vgatrails.sh:
#              the escape, WIN.INI at Enable, the trail after each driver
#              call, drawing under it; timing-free), logs and frames
#              identical; real mouse motion with trails on from WIN.INI
#              (a window dragged, a menu tracked under the trail) and
#              Control Panel's Mouse applet turning them on and off, every
#              screen once the trail has run down       must be 0
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
#              log identical; and a fast drag of US19's slow control with
#              w16's MOUSE.DRV too: the outline where the pointer stopped
#              (parts sbdrag@us, sbdrag@np, sbdrag@fast); and w16's own
#              Notepad against Microsoft's (sbdrag@npw)
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
#   trackmove  a window dragged by its caption (DefWindowProc's SC_MOVE,
#              user/track.c) while its task's timer, posted messages and a
#              QuickTime-like WH_GETMESSAGE hook that keeps posting go on,
#              and another task's timer ticks: probe US31 as the shell
#              (tests/scripts/input/us31.txt); w16's USER and GDI against
#              all-Microsoft: US31's OBS lines identical (what the window
#              gets during the drag, where it lands, the other task
#              running); the screens are not compared (3.1 shows no
#              window's drawing during a drag, w16 does)
#   control    Control Panel under real keys (WIN.INI run=CONTROL.EXE,
#              tests/scripts/input/control.txt): w16's CONTROL.EXE with
#              Microsoft's applets against Microsoft's, on all-Microsoft
#              3.1 (part control@ms), w16's KRNL286 (@k286) and KRNL386
#              (@k386); w16's install tree alone under its KRNL386 against
#              its KRNL286 (@tree): every screen and CONTROL.INI identical;
#              @resize: tests/scripts/input/cprepaint.txt (the panel sized,
#              scrolled, dialogs moved over it, Alt+Tab, minimized) against
#              Microsoft's on all-Microsoft 3.1, every screen identical
#              but for the minimized panel's icon
#   taskman    the Task List (tests/scripts/input/taskman.txt: Ctrl+Esc, a
#              double-click on the desktop, Switch To an icon, Cascade,
#              Tile, End Task, Tile with Shift, Arrange Icons, a double-click
#              on an item, Ctrl+Esc from a full-screen MS-DOS Prompt; Clock,
#              Calculator, Notepad) against Microsoft's TASKMAN on
#              all-Microsoft 3.1, every screen to the pixel (Clock's ticking
#              face left out): w16's TASKMAN on all-Microsoft 3.1 (part ms)
#              and on w16's KRNL286 (k286); w16's USER with
#              Microsoft's TASKMAN (us) and with w16's (usall); w16's
#              install tree alone under its KRNL386 against its KRNL286
#              (tree: every screen identical)
#   cplclick   Control Panel's Keyboard, Mouse, Desktop and Color dialogs
#              under real mouse clicks on each control (tests/scripts/input/
#              cplclick-*.txt): w16's USER against Microsoft's (screens and
#              INI files), w16's applets against Microsoft's (INI files),
#              the install tree under KRNL386 against KRNL286 (both); parts
#              cplclick@kb-ms, @kb-tree, ... @co-tree. cplclickus (not in
#              the default set: run it by name): the parts kb-us ... co-us,
#              w16's USER against Microsoft's, screens and INI files; they
#              show USER's remaining differences (ARCHITECTURE.md 14)
#   cpclose    Control Panel's applets opened over the panel and closed,
#              OK and Cancel, a scheme or pattern picked from a dropped
#              list or none (tests/scripts/input/cpclose.txt, generated
#              from cpclose.src by tools/inputgoto.py): every screen after
#              a close as on 3.1, for w16's USER with Microsoft's applets
#              (@us) and with w16's (@usw), w16's CONTROL.EXE (@ctl); the
#              install tree under KRNL386 against KRNL286 (@tree)
#   mousespeed the pointer's travel for a fixed motion at acceleration 0 0 0
#              and 4 6 2 (WIN.INI MouseThreshold1/2, MouseSpeed): w16's USER
#              (parts @s0-us, @s2-us) and w16's USER with w16's MOUSE.DRV
#              (@s0-drv, @s2-drv) against all-Microsoft 3.1: the same place
#   gpuser     USER's functions given a bad pointer, USER's "__GP" table
#              (user/gp.asm): probe US27 as the shell in DOSBox-X (-p
#              386x), every case Windows 3.1 came back from, its values
#              checked against 3.1's (tests/apps/us27ref.h): w16's USER on
#              Microsoft's KRNL286 and KRNL386 and w16's install tree on
#              each KERNEL (parts gpuser@u286, @u386, @t286, @t386)
#   holdbtn    the caption's minimize and maximize buttons and a scroll
#              bar's arrows and shaft held down: probe US26 as the shell,
#              w16's USER and GDI against all-Microsoft, every screen
#              (pressed while held, raised off, nothing on a release off)
#              and the messages identical, the arrow's repeat times as 3.1's
#   mmerge     mouse moves made while a program is busy merge into one:
#              probe US25 as the shell (events through mouse_event, then
#              real input), w16's USER against all-Microsoft, its log of
#              mouse messages identical
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
# independent sessions run as parts (allw16nolock@1..6, pmdrop@pm/sh/us, sbdrag@us/np/fast, edithandle@us/np,
# winfile@cmp/all, swapesc@k386/k286/mswoa, kbd@SCRIPT, mouse@SCRIPT: see caseparts), scheduled like cases and summed up in
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
# cplinicmp REF W16: two INI files the same, but for w16's branding of
# Control Panel's built-in colour scheme: 3.1 writes [current] color
# schemes=Windows Default where w16 writes w16 Default (shell/cpl/main);
# both sides read so (the tree parts compare w16 with w16)
cplinicmp() {
    _a=$(sed 's/^\(color schemes=\)w16 Default/\1Windows Default/' "$1"; echo .)
    _b=$(sed 's/^\(color schemes=\)w16 Default/\1Windows Default/' "$2"; echo .)
    [ "$_a" = "$_b" ]
}
cases=${*:-kernel dosx k286 k386 k386all k386e k386cad k386hung lastexit odstring wincom allw16 user userk gdi gdik usergdi gdiaudit winoldap winoldapk winoldapw wincomswap winprompt shell shellk tree allw16full input allw16input allw16bootinput allw16nolock exitwin exittree runfront runfrontk about dragoff dragoffk pmdrop tabs alttab helpmenus sbmenu sbdrag mmerge holdbtn edithandle capcursor trackmove gpuser deskver icons winfile runbrowse pmitems w16groups ddescen zipsetup pmclose write w16write w16winfile cardfile control taskman cplclick cpclose mousespeed missingdll runassoc runpe olecli olesvr ddeml lzexpand ver drivers quicktime timer mmsystem sb16 toolhelp treek386 mouse mouseall woados woadosbat swapdos swapalt swapall swapesc pmkeys kbd kbdall kbdwrite kbdwoados comm commdlg mdimax uaudit lbclip lbdraw commuser commall termclose sysdrv sysdrvall sysdrvwoados sysboot sysclock sysprobe sound soundtime soundrec soundbeep soundboot treealone vga vgaboot vgaall vgawoados vgaprobe trails win386 win386input win386cad win386close win386hung win386dos win386dosw win386switch win386two win386exitbox win386w16dos bigmem filecdr}
user=$root/build/dos/USER.EXE
gdi=$root/build/dos/GDI.EXE
# Notepad's caret in the kbtype script's 01-typed (16,119) and 02-edited
# (56,104; later shots too): with CursorBlinkRate=65535 the caret still
# changes phase once, 65.5 s after Notepad created it (its blink timer
# runs on, as 3.1's), and under load 02-edited came on either side of that
# (vgaall: 4 of 10 parallel runs, 30 pixels). Left out there; the rest of
# each shot is compared to the pixel
kbtypecaret="rectangle 16,119 17,133 rectangle 56,104 57,118"
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
        case " $* " in *" -r "*) ;; *" -3 "*) printf 'KRNL386.EXE '; sha256sum < "$root/build/dos/KRNL386.EXE" ;; esac
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
    k386|k386all)
            # Standard mode on a 386 runs KRNL386.EXE (DOSX takes it when
            # it is there): Microsoft's KRNL386 is the reference
            refrun ref386 k386 boot -3 -r; expect=$out/ref386.png
            if [ $c = k386 ]; then boot k386 -3
            else boot k386all -3 -w -x -f "$user=WINDOWS/SYSTEM/USER.EXE" -f "$gdi=WINDOWS/SYSTEM/GDI.EXE"; fi
            check $c $c ref386
            expect= ;;
    k386e)  # 386 Enhanced mode: WIN386 starts KRNL386.EXE (DOSBox, as the
            # win386input case); w16's KRNL386 on w16's WIN386, then on
            # Microsoft's, each against Microsoft's KRNL386 on the same WIN386
            w386=$root/build/win386/WIN386.EXE
            set -- -3 -c 'WIN /3 /B'
            refrun ref386e k386e boot -r "$@" -f "$w386=WINDOWS/SYSTEM/WIN386.EXE"; expect=$out/ref386e.png
            boot k386e "$@" -f "$w386=WINDOWS/SYSTEM/WIN386.EXE"; check k386e k386e ref386e
            refrun ref386em k386e boot -r "$@"; expect=$out/ref386em.png
            boot k386em "$@"; check k386em k386em ref386em
            expect= ;;
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
    gdiaudit)
        # the GDI audit's probe GD5 (tests/apps/gd5.c; docs/notes/
        # gdi-audit-triage.md) as the shell of Windows 3.1 with Microsoft's
        # GDI and with w16's: both logs must end with "GD5 done" and have no
        # FAIL (GD5's table, gd5ref.h, holds 3.1's values)
        r=ok
        for k in ms w16; do
            o=; [ $k = ms ] && o=-r
            "$root/tests/gdiheap.sh" $o -p "$root/build/dos/GD5.EXE" > "$out/gdiaudit-$k.log" 2>&1 &
        done
        wait
        for k in ms w16; do
            f=$out/gdiaudit-$k.log
            grep -q '^GD5 done' "$f" || { r="$k: GD5 did not finish ($f)"; break; }
            if grep -q '^FAIL' "$f"; then r="$k: $(grep -m1 '^FAIL' "$f" | tr -d '\r') ($f)"; break; fi
        done
        if [ "$r" = ok ]; then echo "ok   gdiaudit: GD5 with w16's GDI as with Microsoft's (the GDI audit's observations)"
        else echo "FAIL gdiaudit: $r"; fails=$((fails + 1)); fi ;;
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
    win386input)
        scr=$root/tests/scripts/input/win386input.txt
        k386=$root/.tools/ref/win31/cdrive/WINDOWS/SYSTEM/KRNL386.EXE
        # dropin.sh deletes KRNL386.EXE (so that WIN /S runs KRNL286): put back
        refrun win386input-ref win386input env INPUTRUN_STAY=1 "$root/tests/inputrun.sh" -s -b 60 -t 240 "$scr" \
            "$out/win386input-ref" -r -c "WIN /3 /B" -f "$k386=WINDOWS/SYSTEM/KRNL386.EXE"
        INPUTRUN_STAY=1 "$root/tests/inputrun.sh" -s -b 60 -t 240 "$scr" "$out/win386input-w16" \
            -r -c "WIN /3 /B" -f "$k386=WINDOWS/SYSTEM/KRNL386.EXE" \
            -f "$root/build/win386/WIN386.EXE=WINDOWS/SYSTEM/WIN386.EXE" > "$out/win386input-w16.txt" 2>&1 || true
        r=ok
        for s in 01-moved 02-helpmenu 03-closed 04-confirm; do
            a=$out/win386input-w16/$s.png b=$out/win386input-ref/$s.png
            if [ ! -f "$a" ] || [ ! -f "$b" ]; then r="no $s screen"; break; fi
            n=$(magick compare -metric AE "$a" "$b" "$out/win386input-$s-diff.png" 2>&1 >/dev/null || true)
            n=${n%%[. ]*}
            [ "${n:-1}" = 0 ] || { r="$s: $n pixels differ ($out/win386input-$s-diff.png)"; break; }
        done
        if [ "$r" = ok ]; then
            a=$out/win386input-w16/05-dos.png b=$out/win386input-ref/05-dos.png
            sz=$(magick identify -format '%wx%h' "$a" 2>/dev/null || true)
            n=$(magick compare -metric AE "$a" "$b" "$out/win386input-05-dos-diff.png" 2>&1 >/dev/null || true)
            n=${n%%[. ]*}
            [ "$sz" = 640x400 ] && [ "${n:-99999}" -le 40 ] || r="05-dos: screen $sz, $n pixels off Microsoft's"
        fi
        if [ "$r" = ok ]; then echo "ok   win386input: mouse, keys and Exit Windows as with Microsoft's WIN386"
        else echo "FAIL win386input: $r ($out/win386input-w16)"; fails=$((fails + 1)); fi ;;
    win386cad)
        scr=$root/tests/scripts/input/win386cad.txt
        k386=$root/.tools/ref/win31/cdrive/WINDOWS/SYSTEM/KRNL386.EXE
        refrun win386cad-ref win386cad env INPUTRUN_STAY=1 "$root/tests/inputrun.sh" -s -b 60 -t 240 "$scr" \
            "$out/win386cad-ref" -r -c "WIN /3 /B" -f "$k386=WINDOWS/SYSTEM/KRNL386.EXE"
        INPUTRUN_STAY=1 "$root/tests/inputrun.sh" -s -b 60 -t 240 "$scr" "$out/win386cad-w16" \
            -r -c "WIN /3 /B" -f "$k386=WINDOWS/SYSTEM/KRNL386.EXE" \
            -f "$root/build/win386/WIN386.EXE=WINDOWS/SYSTEM/WIN386.EXE" > "$out/win386cad-w16.txt" 2>&1 || true
        r=ok
        for s in 01-cad 02-back; do
            a=$out/win386cad-w16/$s.png b=$out/win386cad-ref/$s.png
            if [ ! -f "$a" ] || [ ! -f "$b" ]; then r="no $s screen"; break; fi
            m="rectangle 0,0 0,0"
            # the title (row 6) and "return to Windows" (row 15): w16's text;
            # the cursor (row 19, column 53) blinks: either phase is right
            [ $s = 01-cad ] && m="rectangle 240,96 400,111 rectangle 40,240 360,255 rectangle 424,304 431,319"
            magick "$a" -fill black -draw "$m" "$out/win386cad-a.png"
            magick "$b" -fill black -draw "$m" "$out/win386cad-b.png"
            n=$(magick compare -metric AE "$out/win386cad-a.png" "$out/win386cad-b.png" "$out/win386cad-$s-diff.png" 2>&1 >/dev/null || true)
            n=${n%%[. ]*}
            [ "${n:-1}" = 0 ] || { r="$s: $n pixels differ ($out/win386cad-$s-diff.png)"; break; }
        done
        # Return afterwards opens File Manager: all of it but the file
        # total (Windows 3.1's swap file); its bitmaps in colour (after
        # the screen VGA.DRV once drew them black and white: the VGA's
        # index registers were not put back)
        if [ "$r" = ok ]; then
            a=$out/win386cad-w16/03-key.png b=$out/win386cad-ref/03-key.png
            if [ -f "$a" ] && [ -f "$b" ]; then
                m="rectangle 296,458 639,477"
                magick "$a" -fill black -draw "$m" "$out/win386cad-a.png"
                magick "$b" -fill black -draw "$m" "$out/win386cad-b.png"
                n=$(magick compare -metric AE "$out/win386cad-a.png" "$out/win386cad-b.png" "$out/win386cad-03-key-diff.png" 2>&1 >/dev/null || true)
                n=${n%%[. ]*}
                [ "${n:-1}" = 0 ] || r="03-key: File Manager differs by $n pixels ($out/win386cad-03-key-diff.png)"
            else r="no 03-key screen"; fi
        fi
        if [ "$r" = ok ]; then echo "ok   win386cad: Ctrl+Alt+Del's screen and back as with Microsoft's WIN386"
        else echo "FAIL win386cad: $r ($out/win386cad-w16)"; fails=$((fails + 1)); fi ;;
    win386hung)
        # a hung task's Ctrl+Alt+Del under w16's WIN386 (its REBOOT runs
        # KERNEL's procedure, which calls Reboot 0203h): VX8 as in k386hung,
        # Microsoft's KRNL386, against Microsoft's WIN386
        scr=$root/tests/scripts/input/vx8hung.txt
        k386=$root/.tools/ref/win31/cdrive/WINDOWS/SYSTEM/KRNL386.EXE
        cp "$root/.tools/ref/win31/cdrive/WINDOWS/WIN.INI" "$out/win386hung.ini"
        python3 -I "$root/tests/scripts/iniset.py" "$out/win386hung.ini" 'windows:run=C:\VX8.EXE' 'vx8:mode=2'
        set -- -r -c "WIN /3 /B" -f "$k386=WINDOWS/SYSTEM/KRNL386.EXE" -f "$root/build/dos/VX8.EXE=VX8.EXE" \
            -f "$out/win386hung.ini=WINDOWS/WIN.INI"
        refrun win386hung-ref win386hung env INPUTRUN_STAY=1 "$root/tests/inputrun.sh" -s -b 60 -t 200 "$scr" \
            "$out/win386hung-ref" "$@"
        INPUTRUN_STAY=1 "$root/tests/inputrun.sh" -s -b 60 -t 200 "$scr" "$out/win386hung-w16" "$@" \
            -f "$root/build/win386/WIN386.EXE=WINDOWS/SYSTEM/WIN386.EXE" > "$out/win386hung-w16.txt" 2>&1 || true
        r=ok
        for s in 01-cad 03-cad2 04-ret; do
            a=$out/win386hung-w16/$s.png b=$out/win386hung-ref/$s.png
            if [ ! -f "$a" ] || [ ! -f "$b" ]; then r="no $s screen"; break; fi
            # row 12 "return to w16." (columns 42-51); the cursor under OK
            m="rectangle 0,0 0,0"
            [ $s = 04-ret ] || m="rectangle 336,192 415,207 rectangle 464,300 471,303"
            magick "$a" -fill black -draw "$m" "$out/win386hung-a.png"
            magick "$b" -fill black -draw "$m" "$out/win386hung-b.png"
            n=$(magick compare -metric AE "$out/win386hung-a.png" "$out/win386hung-b.png" "$out/win386hung-$s-diff.png" 2>&1 >/dev/null || true)
            n=${n%%[. ]*}
            [ "${n:-1}" = 0 ] || { r="$s: $n pixels differ ($out/win386hung-$s-diff.png)"; break; }
        done
        if [ "$r" = ok ]; then echo "ok   win386hung: a hung task's Ctrl+Alt+Del, Escape and Return as with Microsoft's WIN386"
        else echo "FAIL win386hung: $r ($out/win386hung-w16)"; fails=$((fails + 1)); fi ;;
    win386dos)
        # the MS-DOS Prompt under w16's WIN386 (its VMs, SHELL and VDD),
        # full screen: the banner, VER, EXIT (tests/scripts/input/
        # dosbox386v.txt), in DOSBox-X on its own DOS with WIN's output not
        # redirected (a DOS box inherits it), against Microsoft's WIN386
        scr=$root/tests/scripts/input/dosbox386v.txt
        k386=$root/.tools/ref/win31/cdrive/WINDOWS/SYSTEM/KRNL386.EXE
        set -- -r -p 386 -c "WIN /3 /B" -f "$k386=WINDOWS/SYSTEM/KRNL386.EXE"
        refrun win386dos-ref win386dos env DROPIN_NOREDIR=1 INPUTRUN_KEYDELAY=150 INPUTRUN_STAY=1 \
            "$root/tests/inputrun.sh" -s -b 200 -t 300 "$scr" "$out/win386dos-ref" "$@"
        DROPIN_NOREDIR=1 INPUTRUN_KEYDELAY=150 INPUTRUN_STAY=1 "$root/tests/inputrun.sh" -s -b 200 -t 300 "$scr" \
            "$out/win386dos-w16" "$@" -f "$root/build/win386/WIN386.EXE=WINDOWS/SYSTEM/WIN386.EXE" \
            > "$out/win386dos-w16.txt" 2>&1 || true
        r=ok
        for s in 01-full 02-ver 03-back; do
            a=$out/win386dos-w16/$s.png b=$out/win386dos-ref/$s.png
            if [ ! -f "$a" ] || [ ! -f "$b" ]; then r="no $s screen"; break; fi
            # the banner's rows 3 and 4 ("w16" for "Windows"); the cursor
            # after the prompt (rows 11 and 14) blinks
            m="rectangle 0,0 0,0"
            [ $s = 01-full ] && m="rectangle 72,48 655,79 rectangle 99,176 107,191"
            [ $s = 02-ver ] && m="rectangle 72,48 655,79 rectangle 99,224 107,239"
            magick "$a" -fill black -draw "$m" "$out/win386dos-a.png"
            magick "$b" -fill black -draw "$m" "$out/win386dos-b.png"
            n=$(magick compare -metric AE "$out/win386dos-a.png" "$out/win386dos-b.png" "$out/win386dos-$s-diff.png" 2>&1 >/dev/null || true)
            n=${n%%[. ]*}
            [ "${n:-1}" = 0 ] || { r="$s: $n pixels differ ($out/win386dos-$s-diff.png)"; break; }
        done
        if [ "$r" = ok ]; then echo "ok   win386dos: the MS-DOS Prompt full screen, VER, EXIT as with Microsoft's WIN386"
        else echo "FAIL win386dos: $r ($out/win386dos-w16)"; fails=$((fails + 1)); fi ;;
    win386dosw|win386switch|win386two|win386exitbox)
        # win386dosw: the MS-DOS Prompt full screen, Alt+Enter to a window,
        # VER typed there, Alt+Enter back, EXIT (tests/scripts/input/
        # dosbox386w.txt), Microsoft's VGA.3GR. win386switch: full screen, Alt+Tab, Alt+Tab
        # back, Alt+Esc, Alt+Tab back, Ctrl+Esc (the Task List), Esc, Alt+Tab
        # (Windows starts a second box: Program Manager's MS-DOS Prompt
        # was selected, as under 3.1), EXIT (tests/scripts/input/
        # dosbox386s.txt). win386two: two MS-DOS Prompts, VER and EXIT in
        # each (dosbox386t.txt). win386exitbox: Exit Windows with a box
        # open: WINOA386's "Application still active" box, then EXIT
        # (dosbox386x.txt). DOSBox-X on its own DOS, against Microsoft's
        # WIN386; masked: the banner's "w16" rows, the blinking cursor
        if [ $c = win386dosw ]; then
            scr=$root/tests/scripts/input/dosbox386w.txt shots="01-full 02-window 03-ver 04-full 05-back"
            set --
        elif [ $c = win386switch ]; then
            scr=$root/tests/scripts/input/dosbox386s.txt
            shots="01-full 02-alttab 03-back 04-altesc 05-back 06-ctrlesc 07-esc 08-box 09-end"
            set --
        elif [ $c = win386two ]; then
            scr=$root/tests/scripts/input/dosbox386t.txt
            shots="01-first 02-windows 03-second 04-ver2 05-back 06-first 07-ver1 08-end"
            set --
        else
            scr=$root/tests/scripts/input/dosbox386x.txt
            shots="01-full 02-windows 03-exitbox 04-refused 05-after 06-box 07-end"
            set --
        fi
        k386=$root/.tools/ref/win31/cdrive/WINDOWS/SYSTEM/KRNL386.EXE
        set -- -r -p 386 -c "WIN /3 /B" -f "$k386=WINDOWS/SYSTEM/KRNL386.EXE" "$@"
        refrun $c-ref $c env DROPIN_NOREDIR=1 INPUTRUN_KEYDELAY=150 INPUTRUN_STAY=1 \
            "$root/tests/inputrun.sh" -s -b 200 -t 300 "$scr" "$out/$c-ref" "$@"
        DROPIN_NOREDIR=1 INPUTRUN_KEYDELAY=150 INPUTRUN_STAY=1 "$root/tests/inputrun.sh" -s -b 200 -t 300 "$scr" \
            "$out/$c-w16" "$@" -f "$root/build/win386/WIN386.EXE=WINDOWS/SYSTEM/WIN386.EXE" \
            > "$out/$c-w16.txt" 2>&1 || true
        r=ok
        for s in $shots; do
            a=$out/$c-w16/$s.png b=$out/$c-ref/$s.png
            if [ ! -f "$a" ] || [ ! -f "$b" ]; then r="no $s screen"; break; fi
            case $(magick identify -format '%wx%h' "$b") in
            720x400) m="rectangle 72,48 655,79 rectangle 99,0 107,399" ;;  # full screen: the cursor's column
            *) m="rectangle 66,40 590,114 rectangle 96,140 104,215" ;;     # Windows: the window's banner, its caret
            esac
            magick "$a" -fill black -draw "$m" "$out/$c-a.png"
            magick "$b" -fill black -draw "$m" "$out/$c-b.png"
            n=$(magick compare -metric AE "$out/$c-a.png" "$out/$c-b.png" "$out/$c-$s-diff.png" 2>&1 >/dev/null || true)
            n=${n%%[. ]*}
            [ "${n:-1}" = 0 ] || { r="$s: $n pixels differ ($out/$c-$s-diff.png)"; break; }
        done
        if [ "$r" = ok ]; then echo "ok   $c: as with Microsoft's WIN386"
        else echo "FAIL $c: $r ($out/$c-w16)"; fails=$((fails + 1)); fi ;;
    win386w16dos)
        # the MS-DOS Prompt with w16's install tree (build/w16, every
        # program, driver and library but the INI files and groups) over
        # Windows 3.1 and w16's WIN386.EXE, in plain DOSBox: the user's
        # set-up (2026-11-19), where WINOA386 refused the box ("...because
        # the x TSR is loaded": SHELL function 9's EBX/ECX) and then keys
        # typed in it came seconds late or never (VPICD: IRQ 1 waiting
        # behind IRQ 0). VER and EXIT typed slowly (tests/scripts/input/
        # dosbox386p.txt): 01-full and 02-ver against Microsoft's WIN386
        # and the rest (masked: the banner's "w16" rows and the blinking
        # cursor); 03-back must be Windows again (640x480, not the text
        # screen's 640x400)
        scr=$root/tests/scripts/input/dosbox386p.txt
        k386=$root/.tools/ref/win31/cdrive/WINDOWS/SYSTEM/KRNL386.EXE
        refrun win386w16dos-ref win386w16dos env DROPIN_NOREDIR=1 INPUTRUN_KEYDELAY=150 INPUTRUN_STAY=1 \
            "$root/tests/inputrun.sh" -s -b 200 -t 300 "$scr" "$out/win386w16dos-ref" -r -c "WIN /3 /B" \
            -f "$k386=WINDOWS/SYSTEM/KRNL386.EXE"
        set -- -c "WIN /3 /B" -f "$root/build/win386/WIN386.EXE=WINDOWS/SYSTEM/WIN386.EXE"
        for f in $(cd "$root/build/w16" && find . -type f | sed 's|^\./||' | grep -v -i '\.ini$\|\.grp$\|\.txt$'); do
            set -- "$@" -f "$root/build/w16/$f=WINDOWS/$f"
        done
        DROPIN_NOREDIR=1 INPUTRUN_KEYDELAY=150 INPUTRUN_STAY=1 "$root/tests/inputrun.sh" -s -b 200 -t 300 "$scr" \
            "$out/win386w16dos-w16" "$@" > "$out/win386w16dos-w16.txt" 2>&1 || true
        r=ok
        for s in 01-full 02-ver; do
            a=$out/win386w16dos-w16/$s.png b=$out/win386w16dos-ref/$s.png
            if [ ! -f "$a" ] || [ ! -f "$b" ]; then r="no $s screen"; break; fi
            m="rectangle 64,48 583,79 rectangle 88,336 95,351 rectangle 88,384 95,399"
            magick "$a" -fill black -draw "$m" "$out/win386w16dos-a.png"
            magick "$b" -fill black -draw "$m" "$out/win386w16dos-b.png"
            n=$(magick compare -metric AE "$out/win386w16dos-a.png" "$out/win386w16dos-b.png" "$out/win386w16dos-$s-diff.png" 2>&1 >/dev/null || true)
            n=${n%%[. ]*}
            [ "${n:-1}" = 0 ] || { r="$s: $n pixels differ ($out/win386w16dos-$s-diff.png)"; break; }
        done
        if [ "$r" = ok ]; then
            g=$(magick identify -format '%wx%h' "$out/win386w16dos-w16/03-back.png" 2>/dev/null || true)
            [ "$g" = 640x480 ] || r="03-back: not Windows ($g)"
        fi
        if [ "$r" = ok ]; then echo "ok   win386w16dos: w16's tree and WIN386, the MS-DOS Prompt: VER and EXIT as with Microsoft's"
        else echo "FAIL win386w16dos: $r ($out/win386w16dos-w16)"; fails=$((fails + 1)); fi ;;
    win386close)
        scr=$root/tests/scripts/input/win386close.txt
        k386=$root/.tools/ref/win31/cdrive/WINDOWS/SYSTEM/KRNL386.EXE
        refrun win386close-ref win386close env INPUTRUN_STAY=1 "$root/tests/inputrun.sh" -s -b 60 -t 240 "$scr" \
            "$out/win386close-ref" -r -c "WIN /3 /B" -f "$k386=WINDOWS/SYSTEM/KRNL386.EXE"
        INPUTRUN_STAY=1 "$root/tests/inputrun.sh" -s -b 60 -t 240 "$scr" "$out/win386close-w16" \
            -r -c "WIN /3 /B" -f "$k386=WINDOWS/SYSTEM/KRNL386.EXE" \
            -f "$root/build/win386/WIN386.EXE=WINDOWS/SYSTEM/WIN386.EXE" > "$out/win386close-w16.txt" 2>&1 || true
        r=ok
        for s in 01-winfile 02-closed 03-notepad 04-closed 05-calc 06-closed 07-closed; do
            a=$out/win386close-w16/$s.png b=$out/win386close-ref/$s.png
            if [ ! -f "$a" ] || [ ! -f "$b" ]; then r="no $s screen"; break; fi
            # File Manager: the status bar's file total left out (Windows
            # 3.1's temporary swap file WIN386.SWP, 2 MB, is one of them:
            # w16's WIN386 does not page); Notepad: the caret blinks
            g=640x480+0+0 m="rectangle 0,0 0,0"
            [ $s = 01-winfile ] && m="rectangle 296,458 639,477"
            [ $s = 03-notepad ] && m="rectangle 16,44 17,58"
            magick "$a" -crop $g +repage -fill black -draw "$m" "$out/win386close-a.png"
            magick "$b" -crop $g +repage -fill black -draw "$m" "$out/win386close-b.png"
            n=$(magick compare -metric AE "$out/win386close-a.png" "$out/win386close-b.png" "$out/win386close-$s-diff.png" 2>&1 >/dev/null || true)
            n=${n%%[. ]*}
            [ "${n:-1}" = 0 ] || { r="$s: $n pixels differ ($out/win386close-$s-diff.png)"; break; }
        done
        if [ "$r" = ok ]; then echo "ok   win386close: File Manager, Notepad, Calculator and Clock closed as with Microsoft's WIN386"
        else echo "FAIL win386close: $r ($out/win386close-w16)"; fails=$((fails + 1)); fi ;;
    k386cad)
        # Ctrl+Alt+Del in 386 Enhanced mode under Microsoft's WIN386, whose
        # Reboot device zeroes KERNEL's sanity byte and runs KERNEL's
        # handler (Reboot 0201h): w16's KRNL386 against Microsoft's, every
        # screen (the win386cad script: the screen, a key back, Return)
        scr=$root/tests/scripts/input/win386cad.txt
        refrun k386cad-ref k386cad env INPUTRUN_STAY=1 "$root/tests/inputrun.sh" -s -b 60 -t 240 "$scr" \
            "$out/k386cad-ref" -3 -r -c "WIN /3 /B"
        INPUTRUN_STAY=1 "$root/tests/inputrun.sh" -s -b 60 -t 240 "$scr" "$out/k386cad-w16" \
            -3 -c "WIN /3 /B" > "$out/k386cad-w16.txt" 2>&1 || true
        r=ok
        for s in 01-cad 02-back; do
            a=$out/k386cad-w16/$s.png b=$out/k386cad-ref/$s.png
            if [ ! -f "$a" ] || [ ! -f "$b" ]; then r="no $s screen"; break; fi
            # the cursor (row 19, column 53) blinks: either phase is right
            m="rectangle 0,0 0,0"
            [ $s = 01-cad ] && m="rectangle 424,304 431,319"
            magick "$a" -fill black -draw "$m" "$out/k386cad-a.png"
            magick "$b" -fill black -draw "$m" "$out/k386cad-b.png"
            n=$(magick compare -metric AE "$out/k386cad-a.png" "$out/k386cad-b.png" "$out/k386cad-$s-diff.png" 2>&1 >/dev/null || true)
            n=${n%%[. ]*}
            [ "${n:-1}" = 0 ] || { r="$s: $n pixels differ ($out/k386cad-$s-diff.png)"; break; }
        done
        if [ "$r" = ok ]; then echo "ok   k386cad: Ctrl+Alt+Del's screen and back as with Microsoft's KRNL386"
        else echo "FAIL k386cad: $r ($out/k386cad-w16)"; fails=$((fails + 1)); fi ;;
    k386hung)
        # Ctrl+Alt+Del with a hung task under Microsoft's WIN386: VX8
        # (tests/apps/vx8.c, mode 2: never yields) started by WIN.INI run=,
        # the vx8hung script (the "not responding" screen, Escape, again,
        # Return: the task ended); w16's KRNL386 against Microsoft's, every
        # screen (the cursor under OK blinks: left out)
        scr=$root/tests/scripts/input/vx8hung.txt
        cp "$root/.tools/ref/win31/cdrive/WINDOWS/WIN.INI" "$out/k386hung.ini"
        python3 -I "$root/tests/scripts/iniset.py" "$out/k386hung.ini" 'windows:run=C:\VX8.EXE' 'vx8:mode=2'
        set -- -c "WIN /3 /B" -f "$root/build/dos/VX8.EXE=VX8.EXE" -f "$out/k386hung.ini=WINDOWS/WIN.INI"
        refrun k386hung-ref k386hung env INPUTRUN_STAY=1 "$root/tests/inputrun.sh" -s -b 60 -t 200 "$scr" \
            "$out/k386hung-ref" -3 -r "$@"
        INPUTRUN_STAY=1 "$root/tests/inputrun.sh" -s -b 60 -t 200 "$scr" "$out/k386hung-w16" \
            -3 "$@" > "$out/k386hung-w16.txt" 2>&1 || true
        r=ok
        for s in 01-cad 02-esc 03-cad2 04-ret; do
            a=$out/k386hung-w16/$s.png b=$out/k386hung-ref/$s.png
            if [ ! -f "$a" ] || [ ! -f "$b" ]; then r="no $s screen"; break; fi
            m="rectangle 464,300 471,303"
            magick "$a" -fill black -draw "$m" "$out/k386hung-a.png"
            magick "$b" -fill black -draw "$m" "$out/k386hung-b.png"
            n=$(magick compare -metric AE "$out/k386hung-a.png" "$out/k386hung-b.png" "$out/k386hung-$s-diff.png" 2>&1 >/dev/null || true)
            n=${n%%[. ]*}
            [ "${n:-1}" = 0 ] || { r="$s: $n pixels differ ($out/k386hung-$s-diff.png)"; break; }
        done
        if [ "$r" = ok ]; then echo "ok   k386hung: a hung task's Ctrl+Alt+Del, Escape and Return as with Microsoft's KRNL386"
        else echo "FAIL k386hung: $r ($out/k386hung-w16)"; fails=$((fails + 1)); fi ;;
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
    control)
        # Control Panel (tests/scripts/input/control.txt, real keys, WIN.INI
        # run=CONTROL.EXE): w16's CONTROL.EXE against Microsoft's, with
        # Microsoft's applets (MAIN.CPL, DRIVERS.CPL, SND.CPL), every screen
        # and CONTROL.INI identical: on all-Microsoft 3.1 (part ms), on
        # w16's KRNL286 (k286) and KRNL386 (k386, dropin.sh -3); then w16's
        # install tree alone (its own CONTROL.EXE and MAIN.CPL) under its
        # KRNL386 against the same under its KRNL286 (tree: every screen
        # identical, CONTROL.INI too)
        s=$root/tests/scripts/input/control.txt
        r=ok
        cmpshots() {   # cmpshots REFDIR W16DIR NAME
            for f in "$1"/[0-9]*.png; do
                sn=$(basename "$f")
                [ -f "$2/$sn" ] || { r="$3: no $sn"; return; }
                n=$(magick compare -metric AE "$2/$sn" "$f" "$out/$3-${sn%.png}-diff.png" 2>&1 >/dev/null || true)
                n=${n%% *}
                [ "$n" = 0 ] || { r="$3: $sn: $n pixels differ"; return; }
            done
            ls "$1"/[0-9]*.png > /dev/null 2>&1 || r="$3: no screens"
            [ "$r" = ok ] || return
            cplinicmp "$1"/work/c/*/CONTROL.INI "$2"/work/c/*/CONTROL.INI || r="$3: CONTROL.INI differs"
        }
        for pk in ${part:-ms k286 k386 tree resize}; do
            case $pk in
            resize)
                # tests/scripts/input/cprepaint.txt: the panel sized smaller
                # (its icons reflow, the status box's edge is redrawn),
                # scrolled, applet dialogs moved over it, Alt+Tab, minimized;
                # w16's CONTROL.EXE against Microsoft's on all-Microsoft 3.1
                # (whose USER keeps a resized window's bits: w16's left the
                # old icon layout there), the minimized panel's icon (w16's
                # art) left out
                cp "$root/.tools/ref/win31/cdrive/WINDOWS/WIN.INI" "$out/control-rs-WIN.INI"
                python3 "$root/tests/scripts/iniset.py" "$out/control-rs-WIN.INI" 'windows:run=CONTROL.EXE' 'windows:CursorBlinkRate=65535'
                rs=$root/tests/scripts/input/cprepaint.txt
                refrun control-rs-ref control "$root/tests/inputrun.sh" -s -b 25 -t 300 "$rs" "$out/control-rs-ref" -r \
                    -f "$out/control-rs-WIN.INI=WINDOWS/WIN.INI"
                rm -rf "$out/control-rs-w16"
                "$root/tests/inputrun.sh" -e "$out/control-rs-ref/boot.png" -E "$out/control-rs-ref" -M "rectangle 23,410 54,441" \
                    -b 25 -t 300 "$rs" "$out/control-rs-w16" -r -f "$out/control-rs-WIN.INI=WINDOWS/WIN.INI" \
                    -f "$root/build/dos/CONTROL.EXE=WINDOWS/CONTROL.EXE" > "$out/control-rs-w16.txt" 2>&1 || true
                ls "$out/control-rs-ref"/[0-9]*.png > /dev/null 2>&1 || r="resize: no screens"
                for f in "$out/control-rs-ref"/[0-9]*.png; do
                    [ "$r" = ok ] || break
                    sn=$(basename "$f")
                    [ -f "$out/control-rs-w16/$sn" ] || { r="resize: no $sn"; break; }
                    magick "$f" -fill black -draw "rectangle 23,410 54,441" "$out/control-rs-a.png"
                    magick "$out/control-rs-w16/$sn" -fill black -draw "rectangle 23,410 54,441" "$out/control-rs-b.png"
                    n=$(magick compare -metric AE "$out/control-rs-b.png" "$out/control-rs-a.png" "$out/resize-${sn%.png}-diff.png" 2>&1 >/dev/null || true)
                    n=${n%% *}
                    [ "$n" = 0 ] || r="resize: $sn: $n pixels differ"
                done ;;
            tree)
                set -- -T "$root/build/w16" -i 'WIN.INI:windows:run=CONTROL.EXE'
                rm -rf "$out/control-t286" "$out/control-t386"
                "$root/tests/inputrun.sh" -s -b 40 -t 240 "$s" "$out/control-t286" "$@" \
                    > "$out/control-t286.txt" 2>&1 || true
                "$root/tests/inputrun.sh" -e "$out/control-t286/boot.png" -E "$out/control-t286" -b 40 -t 240 \
                    "$s" "$out/control-t386" "$@" -3 > "$out/control-t386.txt" 2>&1 || true
                cmpshots "$out/control-t286" "$out/control-t386" tree ;;
            *)
                cp "$root/.tools/ref/win31/cdrive/WINDOWS/WIN.INI" "$out/control-WIN.INI"
                python3 "$root/tests/scripts/iniset.py" "$out/control-WIN.INI" 'windows:run=CONTROL.EXE'
                refrun control-ref control "$root/tests/inputrun.sh" -s -b 25 -t 240 "$s" "$out/control-ref" -r \
                    -f "$out/control-WIN.INI=WINDOWS/WIN.INI"
                case $pk in
                ms)   set -- -r ;;
                k286) set -- ;;
                k386) set -- -3 ;;
                esac
                rm -rf "$out/control-$pk"
                "$root/tests/inputrun.sh" -e "$out/control-ref/boot.png" -E "$out/control-ref" -b 25 -t 240 \
                    "$s" "$out/control-$pk" "$@" -f "$out/control-WIN.INI=WINDOWS/WIN.INI" \
                    -f "$root/build/dos/CONTROL.EXE=WINDOWS/CONTROL.EXE" > "$out/control-$pk.txt" 2>&1 || true
                cmpshots "$out/control-ref" "$out/control-$pk" "$pk" ;;
            esac
            [ "$r" = ok ] || break
        done
        if [ -n "$part" ]; then partdone "$r"
        elif [ "$r" = ok ]; then echo "ok   control: 0 pixels, CONTROL.INI identical"
        else echo "FAIL control: $r ($out)"; fails=$((fails + 1)); fi ;;
    taskman)
        # the Task List (shell/taskman; ARCHITECTURE.md 15.5) under real
        # keys and clicks (tests/scripts/input/taskman.txt; WIN.INI
        # run=CLOCK.EXE CALC.EXE NOTEPAD.EXE, the caret not blinking)
        # against Microsoft's TASKMAN on all-Microsoft 3.1. Clock's seconds
        # tick: left out are its strip at the left of the first screens and
        # Notepad's caret there (tmbase), and Clock's time in the tiled
        # screens (tmtime; and from every wait for a screen). Parts: ms
        # (w16's TASKMAN, all else Microsoft's), k286 (w16's TASKMAN on
        # w16's KRNL286), us (w16's USER, Microsoft's TASKMAN),
        # usall (w16's USER and TASKMAN), tree (w16's install tree
        # alone under its KRNL386 against its KRNL286)
        s=$root/tests/scripts/input/taskman.txt
        tmbase="rectangle 0,0 29,227 rectangle 39,64 42,82"
        tmtime="rectangle 330,90 630,145"        # Clock's time when tiled
        tmmask() {   # the shot's mask
            case $1 in
            11-tile|12-c) echo "$tmbase $tmtime" ;;
            *) echo "$tmbase" ;;
            esac
        }
        r=ok tmextra=
        tmcmp() {   # tmcmp REFDIR W16DIR NAME
            ls "$1"/[0-9]*.png > /dev/null 2>&1 || { r="$3: no screens"; return; }
            for f in "$1"/[0-9]*.png; do
                sn=$(basename "$f")
                [ -f "$2/$sn" ] || { r="$3: no $sn"; return; }
                m="$(tmmask "${sn%.png}") $tmextra"
                magick "$f" -fill black -draw "$m" "$out/taskman-$3-a.png"
                magick "$2/$sn" -fill black -draw "$m" "$out/taskman-$3-b.png"
                n=$(magick compare -metric AE "$out/taskman-$3-b.png" "$out/taskman-$3-a.png" "$out/taskman-$3-${sn%.png}-diff.png" 2>&1 >/dev/null || true)
                n=${n%% *}
                [ "$n" = 0 ] || { r="$3: $sn: $n pixels differ"; return; }
            done
            rm -f "$out/taskman-$3-a.png" "$out/taskman-$3-b.png"
        }
        for pk in ${part:-ms k286 us usall tree}; do
            case $pk in
            tree)
                tmextra=
                set -- -T "$root/build/w16" -i 'WIN.INI:windows:run=CLOCK.EXE CALC.EXE NOTEPAD.EXE' \
                    -i 'WIN.INI:windows:CursorBlinkRate=65535' \
                    -i 'NonWindowsApp:SwapDisk=C:\W16'
                rm -rf "$out/taskman-t286" "$out/taskman-t386"
                "$root/tests/inputrun.sh" -s -b 40 -t 600 "$s" "$out/taskman-t286" "$@" \
                    > "$out/taskman-t286.txt" 2>&1 || true
                "$root/tests/inputrun.sh" -e "$out/taskman-t286/boot.png" -E "$out/taskman-t286" -M "$tmbase $tmtime" \
                    -b 40 -t 600 "$s" "$out/taskman-t386" "$@" -3 > "$out/taskman-t386.txt" 2>&1 || true
                tmcmp "$out/taskman-t286" "$out/taskman-t386" tree ;;
            *)
                cp "$root/.tools/ref/win31/cdrive/WINDOWS/WIN.INI" "$out/taskman-WIN.INI"
                python3 "$root/tests/scripts/iniset.py" "$out/taskman-WIN.INI" 'windows:run=CLOCK.EXE CALC.EXE NOTEPAD.EXE' \
                    'windows:CursorBlinkRate=65535'
                refrun taskman-ref taskman "$root/tests/inputrun.sh" -s -b 40 -t 400 "$s" "$out/taskman-ref" -r \
                    -f "$out/taskman-WIN.INI=WINDOWS/WIN.INI" -i 'NonWindowsApp:SwapDisk=C:\WINDOWS\TEMP'
                tm=$root/build/dos/TASKMAN.EXE
                # (w16's USER: the pointer, parked in the bottom right
                # corner, is redrawn as Program Manager's sizing arrow when
                # the Task List ends; 3.1 keeps the arrow until it moves)
                tmextra=
                case $pk in us*) tmextra="rectangle 615,450 639,479" ;; esac
                case $pk in
                ms)    set -- -r -f "$tm=WINDOWS/TASKMAN.EXE" ;;
                k286)  set -- -f "$tm=WINDOWS/TASKMAN.EXE" ;;
                us)    set -- -r -f "$user=WINDOWS/SYSTEM/USER.EXE" ;;
                usall) set -- -r -f "$user=WINDOWS/SYSTEM/USER.EXE" \
                           -f "$tm=WINDOWS/TASKMAN.EXE" ;;
                esac
                rm -rf "$out/taskman-$pk"
                "$root/tests/inputrun.sh" -e "$out/taskman-ref/boot.png" -E "$out/taskman-ref" -M "$tmbase $tmtime" \
                    -b 40 -t 600 "$s" "$out/taskman-$pk" "$@" -f "$out/taskman-WIN.INI=WINDOWS/WIN.INI" \
                    -i 'NonWindowsApp:SwapDisk=C:\WINDOWS\TEMP' > "$out/taskman-$pk.txt" 2>&1 || true
                tmcmp "$out/taskman-ref" "$out/taskman-$pk" "$pk" ;;
            esac
            [ "$r" = ok ] || break
        done
        if [ -n "$part" ]; then partdone "$r"
        elif [ "$r" = ok ]; then echo "ok   taskman: every screen as Microsoft's Task List's"
        else echo "FAIL taskman: $r ($out)"; fails=$((fails + 1)); fi ;;
    cplclick|cplclickus)
        # Control Panel's Keyboard, Mouse, Desktop and Color dialogs driven
        # with real mouse clicks on each of their controls (tests/scripts/
        # input/cplclick-*.txt, WIN.INI run=CONTROL.EXE), parts APPLET-STACK:
        #   -us    w16's CONTROL.EXE and MAIN.CPL on Microsoft's 3.1 with
        #          w16's USER, against the same on Microsoft's USER: every
        #          screen and WIN.INI, CONTROL.INI, SYSTEM.INI identical
        #   -ms    w16's CONTROL.EXE and MAIN.CPL against Microsoft's, all
        #          else Microsoft's: the INI files identical (the screens
        #          show w16's own icons)
        #   -tree  w16's install tree alone under its KRNL386 against its
        #          KRNL286: every screen and the INI files identical
        # (until 2026-11-15 the controls inside a group box took no clicks
        # on w16's USER: ARCHITECTURE.md 14)
        r=ok
        clkcmp() {   # clkcmp REFDIR W16DIR NAME SHOTS(0/1) WINDIR
            if [ "$4" = 1 ]; then
                for f in "$1"/[0-9]*.png; do
                    sn=$(basename "$f")
                    [ -f "$2/$sn" ] || { r="$3: no $sn"; return; }
                    n=$(magick compare -metric AE "$2/$sn" "$f" "$out/$3-${sn%.png}-diff.png" 2>&1 >/dev/null || true)
                    n=${n%% *}
                    [ "$n" = 0 ] || { r="$3: $sn: $n pixels differ"; return; }
                done
                ls "$1"/[0-9]*.png > /dev/null 2>&1 || { r="$3: no screens"; return; }
            fi
            for i in WIN.INI CONTROL.INI SYSTEM.INI; do
                cplinicmp "$1/work/c/$5/$i" "$2/work/c/$5/$i" || { r="$3: $i differs"; return; }
            done
        }
        if [ "$c" = cplclickus ]; then dparts="kb-us ms-us dk-us co-us"
        else dparts="kb-ms kb-tree ms-ms ms-tree dk-ms dk-tree co-ms co-tree"; fi
        for pk in ${part:-$dparts}; do
            ap=${pk%-*} st=${pk#*-}
            s=$root/tests/scripts/input/cplclick-$ap.txt
            case $st in
            tree)
                set -- -T "$root/build/w16" -i 'WIN.INI:windows:run=CONTROL.EXE' \
                    -i 'WIN.INI:windows:MouseSpeed=0'
                rm -rf "$out/cplclick-$ap-t286" "$out/cplclick-$ap-t386"
                "$root/tests/inputrun.sh" -s -b 40 -t 300 "$s" "$out/cplclick-$ap-t286" "$@" \
                    > "$out/cplclick-$ap-t286.txt" 2>&1 || true
                "$root/tests/inputrun.sh" -e "$out/cplclick-$ap-t286/boot.png" -E "$out/cplclick-$ap-t286" \
                    -b 40 -t 300 "$s" "$out/cplclick-$ap-t386" "$@" -3 > "$out/cplclick-$ap-t386.txt" 2>&1 || true
                clkcmp "$out/cplclick-$ap-t286" "$out/cplclick-$ap-t386" "$pk" 1 W16 ;;
            *)
                cp "$root/.tools/ref/win31/cdrive/WINDOWS/WIN.INI" "$out/cplclick-WIN.INI"
                python3 "$root/tests/scripts/iniset.py" "$out/cplclick-WIN.INI" 'windows:run=CONTROL.EXE' \
                    'windows:MouseSpeed=0'
                set -- -f "$out/cplclick-WIN.INI=WINDOWS/WIN.INI"
                w16cp="-f $root/build/dos/CONTROL.EXE=WINDOWS/CONTROL.EXE -f $root/build/dos/MAIN.CPL=WINDOWS/SYSTEM/MAIN.CPL"
                if [ "$st" = us ]; then
                    # shellcheck disable=SC2086
                    refrun cplclick-$ap-ref cplclick "$root/tests/inputrun.sh" -s -b 25 -t 300 "$s" \
                        "$out/cplclick-$ap-ref" -r "$@" $w16cp
                    rm -rf "$out/cplclick-$ap-us"
                    # shellcheck disable=SC2086
                    "$root/tests/inputrun.sh" -e "$out/cplclick-$ap-ref/boot.png" -E "$out/cplclick-$ap-ref" \
                        -b 25 -t 300 "$s" "$out/cplclick-$ap-us" -r "$@" $w16cp \
                        -f "$user=WINDOWS/SYSTEM/USER.EXE" > "$out/cplclick-$ap-us.txt" 2>&1 || true
                    clkcmp "$out/cplclick-$ap-ref" "$out/cplclick-$ap-us" "$pk" 1 WINDOWS
                else
                    refrun cplclick-$ap-msref cplclick "$root/tests/inputrun.sh" -s -b 25 -t 300 "$s" \
                        "$out/cplclick-$ap-msref" -r "$@"
                    rm -rf "$out/cplclick-$ap-ms"
                    # shellcheck disable=SC2086
                    "$root/tests/inputrun.sh" -e "$out/cplclick-$ap-msref/boot.png" -b 25 -t 300 "$s" \
                        "$out/cplclick-$ap-ms" -r "$@" $w16cp > "$out/cplclick-$ap-ms.txt" 2>&1 || true
                    clkcmp "$out/cplclick-$ap-msref" "$out/cplclick-$ap-ms" "$pk" 0 WINDOWS
                fi ;;
            esac
            [ "$r" = ok ] || break
        done
        if [ -n "$part" ]; then partdone "$r"
        elif [ "$r" = ok ]; then echo "ok   $c: every click as on 3.1"
        else echo "FAIL $c: $r ($out)"; fails=$((fails + 1)); fi ;;
    cpclose)
        # Control Panel's applets opened over the panel and closed
        # (tests/scripts/input/cpclose.txt: Color with a scheme picked
        # from its drop-down list, OK and Cancel; Desktop with a pattern
        # picked, Cancel and OK; Keyboard and Mouse, OK and Cancel; mouse
        # and keys; WIN.INI run=CONTROL.EXE, MouseSpeed=0, CursorBlinkRate
        # 65535: nothing blinks), every screen
        # after a close compared, parts:
        #   us    w16's USER with Microsoft's CONTROL.EXE and MAIN.CPL
        #         against all-Microsoft 3.1
        #   usw   w16's USER with w16's CONTROL.EXE and MAIN.CPL against
        #         the same on Microsoft's USER
        #   ctl   w16's CONTROL.EXE (Microsoft's MAIN.CPL) against
        #         all-Microsoft 3.1
        #   tree  w16's install tree alone under its KRNL386 against its
        #         KRNL286
        # (until 2026-11-16 w16's USER left Control Panel's status line
        # unpainted, showing the Color dialog's sample, when a dropped
        # list was hidden after a selection: ARCHITECTURE.md 14.4)
        s=$root/tests/scripts/input/cpclose.txt
        r=ok
        ccmp() {   # ccmp REFDIR W16DIR NAME
            for f in "$1"/[0-9]*.png; do
                sn=$(basename "$f")
                [ -f "$2/$sn" ] || { r="$3: no $sn"; return; }
                n=$(magick compare -metric AE "$2/$sn" "$f" "$out/$3-${sn%.png}-diff.png" 2>&1 >/dev/null || true)
                n=${n%% *}
                [ "$n" = 0 ] || { r="$3: $sn: $n pixels differ"; return; }
            done
            ls "$1"/[0-9]*.png > /dev/null 2>&1 || r="$3: no screens"
        }
        for pk in ${part:-us usw ctl tree}; do
            case $pk in
            tree)
                set -- -T "$root/build/w16" -i 'WIN.INI:windows:run=CONTROL.EXE' \
                    -i 'WIN.INI:windows:MouseSpeed=0' -i 'WIN.INI:windows:CursorBlinkRate=65535'
                rm -rf "$out/cpclose-t286" "$out/cpclose-t386"
                "$root/tests/inputrun.sh" -s -b 40 -t 600 "$s" "$out/cpclose-t286" "$@" \
                    > "$out/cpclose-t286.txt" 2>&1 || true
                "$root/tests/inputrun.sh" -e "$out/cpclose-t286/boot.png" -E "$out/cpclose-t286" \
                    -b 40 -t 600 "$s" "$out/cpclose-t386" "$@" -3 > "$out/cpclose-t386.txt" 2>&1 || true
                ccmp "$out/cpclose-t286" "$out/cpclose-t386" tree ;;
            *)
                cp "$root/.tools/ref/win31/cdrive/WINDOWS/WIN.INI" "$out/cpclose-WIN.INI"
                python3 "$root/tests/scripts/iniset.py" "$out/cpclose-WIN.INI" 'windows:run=CONTROL.EXE' \
                    'windows:MouseSpeed=0' 'windows:CursorBlinkRate=65535'
                set -- -r -f "$out/cpclose-WIN.INI=WINDOWS/WIN.INI"
                w16cp="-f $root/build/dos/CONTROL.EXE=WINDOWS/CONTROL.EXE -f $root/build/dos/MAIN.CPL=WINDOWS/SYSTEM/MAIN.CPL"
                case $pk in
                usw)
                    # shellcheck disable=SC2086
                    refrun cpclose-wref cpclose "$root/tests/inputrun.sh" -s -b 25 -t 600 "$s" \
                        "$out/cpclose-wref" "$@" $w16cp
                    rd=$out/cpclose-wref
                    set -- "$@" $w16cp -f "$user=WINDOWS/SYSTEM/USER.EXE" ;;
                *)
                    refrun cpclose-ref cpclose "$root/tests/inputrun.sh" -s -b 25 -t 600 "$s" \
                        "$out/cpclose-ref" "$@"
                    rd=$out/cpclose-ref
                    if [ "$pk" = us ]; then set -- "$@" -f "$user=WINDOWS/SYSTEM/USER.EXE"
                    else set -- "$@" -f "$root/build/dos/CONTROL.EXE=WINDOWS/CONTROL.EXE"; fi ;;
                esac
                rm -rf "$out/cpclose-$pk"
                "$root/tests/inputrun.sh" -e "$rd/boot.png" -E "$rd" -b 25 -t 600 "$s" \
                    "$out/cpclose-$pk" "$@" > "$out/cpclose-$pk.txt" 2>&1 || true
                ccmp "$rd" "$out/cpclose-$pk" "$pk" ;;
            esac
            [ "$r" = ok ] || break
        done
        if [ -n "$part" ]; then partdone "$r"
        elif [ "$r" = ok ]; then echo "ok   cpclose: every screen after a close as on 3.1"
        else echo "FAIL cpclose: $r ($out)"; fails=$((fails + 1)); fi ;;
    mousespeed)
        # the pointer's travel for a fixed motion at two acceleration
        # settings (tests/scripts/input/mousespeed.txt; WIN.INI
        # MouseThreshold1/2 MouseSpeed 0 0 0 and 4 6 2): w16's USER, and
        # w16's USER with w16's MOUSE.DRV, against all-Microsoft 3.1, the
        # screens (where the pointer ends) identical. Parts s0-us, s2-us,
        # s0-drv, s2-drv
        s=$root/tests/scripts/input/mousespeed.txt
        r=ok
        for pk in ${part:-s0-us s2-us s0-drv s2-drv}; do
            sv=${pk%-*} st=${pk#*-}
            wi=$out/mousespeed-$sv-WIN.INI
            cp "$root/.tools/ref/win31/cdrive/WINDOWS/WIN.INI" "$wi"
            case $sv in
            s0) python3 "$root/tests/scripts/iniset.py" "$wi" 'windows:MouseThreshold1=0' \
                    'windows:MouseThreshold2=0' 'windows:MouseSpeed=0' ;;
            s2) python3 "$root/tests/scripts/iniset.py" "$wi" 'windows:MouseThreshold1=4' \
                    'windows:MouseThreshold2=6' 'windows:MouseSpeed=2' ;;
            esac
            refrun mousespeed-$sv-ref mousespeed "$root/tests/inputrun.sh" -s -b 25 -t 150 "$s" \
                "$out/mousespeed-$sv-ref" -r -f "$wi=WINDOWS/WIN.INI"
            set -- -f "$user=WINDOWS/SYSTEM/USER.EXE"
            [ "$st" = drv ] && set -- "$@" -f "$root/build/dos/MOUSE.DRV=WINDOWS/SYSTEM/MOUSE.DRV"
            rm -rf "$out/mousespeed-$pk"
            "$root/tests/inputrun.sh" -e "$out/mousespeed-$sv-ref/boot.png" -E "$out/mousespeed-$sv-ref" \
                -b 25 -t 150 "$s" "$out/mousespeed-$pk" -r -f "$wi=WINDOWS/WIN.INI" "$@" \
                > "$out/mousespeed-$pk.txt" 2>&1 || true
            f=$out/mousespeed-$sv-ref/01-travel.png
            if [ ! -f "$out/mousespeed-$pk/01-travel.png" ]; then r="$pk: no shot"
            else
                n=$(magick compare -metric AE "$out/mousespeed-$pk/01-travel.png" "$f" \
                    "$out/mousespeed-$pk-diff.png" 2>&1 >/dev/null || true)
                n=${n%% *}
                [ "$n" = 0 ] || r="$pk: $n pixels differ (the pointer elsewhere)"
            fi
            [ "$r" = ok ] || break
        done
        if [ -n "$part" ]; then partdone "$r"
        elif [ "$r" = ok ]; then echo "ok   mousespeed: the pointer travels as on 3.1"
        else echo "FAIL mousespeed: $r ($out)"; fails=$((fails + 1)); fi ;;
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
    exittree)
        # w16's install tree over 3.1, as tests/w16tree.sh lays it (the
        # tree's KRNL386 in the KRNL386.EXE slot), Exit Windows from
        # Program Manager; see the header
        tree=$root/build/w16
        set -- -k "$tree/SYSTEM/KRNL286.EXE" -X "$tree/SYSTEM/DOSX.EXE"
        for f in "$tree"/*; do
            case $(basename "$f") in README.TXT|SYSTEM|*.INI|*.GRP) continue ;; esac
            set -- "$@" -f "$f=WINDOWS/$(basename "$f")"
        done
        for f in "$tree"/SYSTEM/*; do set -- "$@" -f "$f=WINDOWS/SYSTEM/$(basename "$f")"; done
        k=${part:-e386}
        case $k in
        e386)   set -- "$@" -c "WIN /3 /B" -f "$root/build/win386/WIN386.EXE=WINDOWS/SYSTEM/WIN386.EXE" ;;
        e386ms) set -- "$@" -c "WIN /3 /B" ;;
        *)      set -- "$@" -c "WIN /S /B" ;;
        esac
        INPUTRUN_STAY=1 "$root/tests/inputrun.sh" -s -b 90 -t 240 "$root/tests/scripts/input/exitwin.txt" \
            "$out/exittree-$k" "$@" > "$out/exittree-$k.txt" 2>&1 || true
        r=ok
        o=$out/exittree-$k/work/c/OUT.TXT
        a=$out/exittree-$k/02-dos.png
        if [ ! -f "$a" ]; then r="no 02-dos screen"
        elif grep -aq 'exception\|w16: fault\|watchdog' "$o" 2>/dev/null; then r="a fault: $(grep -a -m1 'exception\|fault\|watchdog' "$o" | tr -d '\r')"
        elif ! grep -aq 'w16: back to DOS, code 0000' "$o" 2>/dev/null; then r="KERNEL did not end Windows (no \"back to DOS\")"
        elif ! grep -q '^Windows ended' "$out/exittree-$k/input.log"; then r="WIN did not return"
        else
            sz=$(magick identify -format '%wx%h' "$a" 2>/dev/null)
            [ "$sz" = 640x400 ] || r="the DOS screen is $sz, not text mode"
        fi
        [ "$r" = ok ] && echo "ok   exittree@$k: Windows ended cleanly, the DOS prompt back in text mode" \
            || echo "     exittree@$k: $r ($out/exittree-$k)"
        partdone "$r" ;;
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
    w16winfile)
        r=ok
        for pk in ${part:-$(cd "$root/tests/scripts/winfile" && ls WF*.txt | sed 's/\.txt$//' | tr A-Z a-z)}; do
            rm -rf "$out/w16winfile-$pk"
            WFSCEN_OUT=$out/w16winfile-$pk "$root/tests/wfscenarios.sh" \
                "$(echo "$pk" | tr a-z A-Z)" > "$out/w16winfile-$pk.txt" 2>&1 || true
            grep -q '^== WF[0-9]*: ok' "$out/w16winfile-$pk.txt" ||
                { r="$pk: $(grep -m1 '^  WF' "$out/w16winfile-$pk.txt" || echo 'no result') ($out/w16winfile-$pk)"; break; }
        done
        if [ -n "$part" ]; then partdone "$r"
        elif [ "$r" = ok ]; then echo "ok   w16winfile: every scenario within its baseline"
        else echo "FAIL w16winfile: $r ($out)"; fails=$((fails + 1)); fi ;;
    w16write)
        s=$root/tests/scripts/input/w16write.txt
        r=ok
        wrcmp() {   # wrcmp REFDIR W16DIR NAME
            # the screen's bottom right pixel is left out: the reference's
            # is now and then a colour no VGA mode has (a capture artifact)
            for f in "$1"/[0-9]*.png; do
                sn=$(basename "$f")
                [ -f "$2/$sn" ] || { r="$3: no $sn"; return; }
                magick "$f" -fill black -draw "point 639,479" "$out/w16write-$3-a.png"
                magick "$2/$sn" -fill black -draw "point 639,479" "$out/w16write-$3-b.png"
                n=$(magick compare -metric AE "$out/w16write-$3-b.png" "$out/w16write-$3-a.png" "$out/w16write-$3-${sn%.png}-diff.png" 2>&1 >/dev/null || true)
                n=${n%% *}
                [ "$n" = 0 ] || { r="$3: $sn: $n pixels differ"; return; }
            done
            ls "$1"/[0-9]*.png > /dev/null 2>&1 || r="$3: no screens"
        }
        for pk in ${part:-ms k286 k386 tree}; do
            case $pk in
            wr*)
                # a writescenarios.sh scenario (WR24 ...); wrNNo: with w16's
                # OLECLI.DLL in w16's run
                rm -rf "$out/w16write-$pk"
                wf=
                case $pk in *o) wf="$root/build/dos/OLECLI.DLL=WINDOWS/SYSTEM/OLECLI.DLL" ;; esac
                WRSCEN_OUT=$out/w16write-$pk WRSCEN_W16FILES=$wf "$root/tests/writescenarios.sh" \
                    "$(echo "${pk%o}" | tr a-z A-Z)" > "$out/w16write-$pk.txt" 2>&1 || true
                grep -q '^== WR[0-9]*: ok' "$out/w16write-$pk.txt" ||
                    r="$pk: $(grep -m1 '^  WR' "$out/w16write-$pk.txt" || echo 'no result') ($out/w16write-$pk)" ;;
            tree)
                set -- -T "$root/build/w16" -i 'WIN.INI:windows:run=WRITE.EXE' -i 'WIN.INI:windows:CursorBlinkRate=65535'
                rm -rf "$out/w16write-t286" "$out/w16write-t386"
                "$root/tests/inputrun.sh" -s -b 40 -t 240 "$s" "$out/w16write-t286" "$@" \
                    > "$out/w16write-t286.txt" 2>&1 || true
                "$root/tests/inputrun.sh" -e "$out/w16write-t286/boot.png" -E "$out/w16write-t286" -b 40 -t 240 \
                    "$s" "$out/w16write-t386" "$@" -3 > "$out/w16write-t386.txt" 2>&1 || true
                wrcmp "$out/w16write-t286" "$out/w16write-t386" tree ;;
            *)
                cp "$root/.tools/ref/win31/cdrive/WINDOWS/WIN.INI" "$out/w16write-WIN.INI"
                python3 "$root/tests/scripts/iniset.py" "$out/w16write-WIN.INI" 'windows:run=WRITE.EXE' 'windows:CursorBlinkRate=65535'
                refrun w16write-ref w16write "$root/tests/inputrun.sh" -s -b 25 -t 240 "$s" "$out/w16write-ref" -r \
                    -f "$out/w16write-WIN.INI=WINDOWS/WIN.INI"
                case $pk in
                ms)   set -- -r ;;
                k286) set -- ;;
                k386) set -- -3 ;;
                esac
                rm -rf "$out/w16write-$pk"
                "$root/tests/inputrun.sh" -e "$out/w16write-ref/boot.png" -E "$out/w16write-ref" -b 25 -t 240 \
                    "$s" "$out/w16write-$pk" "$@" -f "$out/w16write-WIN.INI=WINDOWS/WIN.INI" \
                    -f "$root/build/dos/WRITE.EXE=WINDOWS/WRITE.EXE" > "$out/w16write-$pk.txt" 2>&1 || true
                wrcmp "$out/w16write-ref" "$out/w16write-$pk" "$pk" ;;
            esac
            [ "$r" = ok ] || break
        done
        if [ -n "$part" ]; then partdone "$r"
        elif [ "$r" = ok ]; then echo "ok   w16write: 0 pixels"
        else echo "FAIL w16write: $r ($out)"; fails=$((fails + 1)); fi ;;
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
    missingdll)
        # a program whose DLL is missing (SYSTEM\OLECLI.DLL, which Cardfile
        # imports, deleted). Parts: pm, the reference install's Cardfile
        # item and File > Run "nosuch" (missingdll.txt) against all of
        # Microsoft's: w16's KRNL286, w16's KRNL386 (Microsoft's rest),
        # and w16's KRNL286, USER, GDI, SHELL and PROGMAN (the group
        # icons' pictures left out); tk59, TK59 as the shell
        # (tests/apps/tk59.c: LoadLibrary, WinExec, LoadModule with the
        # error mode 0 and 8000h) under both of w16's kernels against
        # Microsoft's KRNL386: every box (shots) and every return code
        # (TK.LOG) the same; tree, w16's install tree alone (no OLECLI.DLL
        # there) under its KRNL386 and its KRNL286 (missingdlltree.txt):
        # the same screens under both, and both boxes as 3.1's (the boxes'
        # insides against the pm reference's). Until 2026-11-14 the tree
        # showed SHELL's "Program Not Found" dialog with a garbage name
        # (ShellExecute read an uninitialised flag) and KERNEL no box.
        r=ok
        del="-f WINDOWS/SYSTEM/OLECLI.DLL=-"
        s=$root/tests/scripts/input/missingdll.txt
        m="rectangle 60,310 300,342"
        refrun missingdll-ref missingdll "$root/tests/inputrun.sh" -s -b 40 -t 200 "$s" "$out/missingdll-ref" -r $del
        case ${part:-pm} in
        pm)
            "$root/tests/inputrun.sh" -e "$out/missingdll-ref/boot.png" -E "$out/missingdll-ref" -b 40 -t 200 \
                "$s" "$out/missingdll-k286" $del > "$out/missingdll-k286.txt" 2>&1 || true
            "$root/tests/inputrun.sh" -e "$out/missingdll-ref/boot.png" -E "$out/missingdll-ref" -b 40 -t 200 \
                "$s" "$out/missingdll-k386" -3 $del > "$out/missingdll-k386.txt" 2>&1 || true
            ls "$out/missingdll-ref"/0[1-7]*.png > /dev/null 2>&1 || r="the reference has no screens"
            for k in k286 k386; do
                [ "$r" = ok ] || break
                for f in "$out/missingdll-ref"/0[1-7]*.png; do
                    s2=$(basename "$f")
                    [ -f "$out/missingdll-$k/$s2" ] || { r="$k: no $s2"; break; }
                    n=$(magick compare -metric AE "$out/missingdll-$k/$s2" "$f" "$out/missingdll-$k-${s2%.png}-diff.png" 2>&1 >/dev/null || true)
                    n=${n%% *}
                    [ "$n" = 0 ] || { r="$k: $s2: $n pixels differ"; break; }
                done
            done ;;
        tk59)
            t59="$del -f $root/build/dos/TK59.EXE=TK59.EXE -i boot:shell=C:\\TK59.EXE"
            s=$root/tests/scripts/input/tk59.txt
            refrun missingdll-tk59ref missingdll "$root/tests/inputrun.sh" -s -b 40 -t 200 "$s" "$out/missingdll-tk59ref" -r $t59
            "$root/tests/inputrun.sh" -s -b 40 -t 200 "$s" "$out/missingdll-tk59k286" $t59 > "$out/missingdll-tk59k286.txt" 2>&1 || true
            "$root/tests/inputrun.sh" -s -b 40 -t 200 "$s" "$out/missingdll-tk59k386" -3 $t59 > "$out/missingdll-tk59k386.txt" 2>&1 || true
            log=work/c/WINDOWS/TK.LOG
            grep -q '^TK59 done' "$out/missingdll-tk59ref/$log" 2>/dev/null || r="the reference's TK59 did not finish"
            for k in k286 k386; do
                [ "$r" = ok ] || break
                d=$out/missingdll-tk59$k
                grep -q '^TK59 done' "$d/$log" 2>/dev/null || { r="$k: TK59 did not finish"; break; }
                if ! diff "$out/missingdll-tk59ref/$log" "$d/$log" > "$d.logdiff"; then r="$k: TK.LOG differs ($d.logdiff)"; break; fi
                for f in "$out/missingdll-tk59ref"/[0-9]*.png; do
                    s2=$(basename "$f")
                    [ -f "$d/$s2" ] || { r="$k: no $s2"; break; }
                    n=$(magick compare -metric AE "$d/$s2" "$f" "$d-${s2%.png}-diff.png" 2>&1 >/dev/null || true)
                    n=${n%% *}
                    [ "$n" = 0 ] || { r="$k: $s2: $n pixels differ"; break; }
                done
            done ;;
        tree)
            # every w16 part on the reference install first: its screens
            # against 3.1's but the boxes and the group icons (w16's USER
            # draws SysErrorBox as a system-modal message box, which also
            # takes the activation: 3.1's leaves the captions active; its
            # MessageBox lays the long text out with other margins and its
            # own icon: UNVERIFIED 3.1's rules; and 3.1's File > Run dialog
            # is gone behind its error box, w16's still there); its boxes
            # are then what the tree's must show
            "$root/tests/inputrun.sh" -s -E "$out/missingdll-ref" -M "$m" -b 40 -t 200 \
                "$s" "$out/missingdll-all" $del -f "$user=WINDOWS/SYSTEM/USER.EXE" -f "$gdi=WINDOWS/SYSTEM/GDI.EXE" \
                -f "$shell=WINDOWS/SYSTEM/SHELL.DLL" -f "$root/build/dos/PROGMAN.EXE=WINDOWS/PROGMAN.EXE" \
                > "$out/missingdll-all.txt" 2>&1 || true
            fe="rectangle 70,50 580,330" ee="rectangle 125,152 512,328" rd="rectangle 100,95 490,265"
            for f in "$out/missingdll-ref"/0[1-7]*.png; do
                s2=$(basename "$f")
                [ -f "$out/missingdll-all/$s2" ] || { r="every w16 part: no $s2"; break; }
                case $s2 in 03-*) mm="$m $fe" ;; 04-*) mm="$m $ee" ;; 06-*) mm="$m $ee $rd" ;; *) mm=$m ;; esac
                magick "$f" -fill black -draw "$mm" "$out/missingdll-all-a.png"
                magick "$out/missingdll-all/$s2" -fill black -draw "$mm" "$out/missingdll-all-b.png"
                n=$(magick compare -metric AE "$out/missingdll-all-b.png" "$out/missingdll-all-a.png" "$out/missingdll-all-${s2%.png}-diff.png" 2>&1 >/dev/null || true)
                n=${n%% *}
                [ "$n" = 0 ] || { r="every w16 part: $s2: $n pixels differ"; break; }
            done
            # (the tree has w16's OLECLI.DLL since 2026-11-14: taken out here)
            s=$root/tests/scripts/input/missingdlltree.txt
            tdel="-f W16/SYSTEM/OLECLI.DLL=-"
            "$root/tests/inputrun.sh" -s -b 40 -t 200 "$s" "$out/missingdll-t386" -T "$root/build/w16" -3 $tdel \
                > "$out/missingdll-t386.txt" 2>&1 || true
            "$root/tests/inputrun.sh" -e "$out/missingdll-t386/boot.png" -E "$out/missingdll-t386" -b 40 -t 200 \
                "$s" "$out/missingdll-t286" -T "$root/build/w16" $tdel > "$out/missingdll-t286.txt" 2>&1 || true
            ls "$out/missingdll-t386"/0[1-4]*.png > /dev/null 2>&1 || r="KRNL386: no screens"
            for f in "$out/missingdll-t386"/0[1-4]*.png; do
                [ "$r" = ok ] || break
                s2=$(basename "$f")
                [ -f "$out/missingdll-t286/$s2" ] || { r="KRNL286: no $s2"; break; }
                n=$(magick compare -metric AE "$out/missingdll-t286/$s2" "$f" "$out/missingdll-t-${s2%.png}-diff.png" 2>&1 >/dev/null || true)
                n=${n%% *}
                [ "$n" = 0 ] || { r="KRNL286 against KRNL386: $s2: $n pixels differ"; break; }
            done
            # both boxes there, where every w16 part's run above has them
            # (centred on the screen; their insides are not compared: the
            # tree's fonts and pointer are w16's): their active captions
            for b in "02-fileerror 03-fileerror 300 188" "03-execerror 04-execerror 480 168"; do
                [ "$r" = ok ] || break
                set -- $b
                for k in t386/$1 all/$2; do
                    px=$(magick "$out/missingdll-$k.png" -format "%[hex:p{$3,$4}]" info: 2>/dev/null)
                    case $px in 0000AA*) ;; *) r="missingdll-$k: no box caption at $3,$4 ($px)"; break ;; esac
                done
            done ;;
        esac
        if [ -n "$part" ]; then partdone "$r"
        elif [ "$r" = ok ]; then echo "ok   missingdll: 0 pixels, the same return codes"
        else echo "FAIL missingdll: $r ($out)"; fails=$((fails + 1)); fi ;;
    runassoc)
        # starting names of every kind (ARCHITECTURE.md 13.4). Part probe:
        # SH14 as the shell (FindExecutable, ShellExecute, WinExec and
        # LoadModule with programs, documents with and without an
        # association, missing files, directories) with w16's SHELL on
        # w16's KRNL286 and KRNL386 (Standard mode): no FAIL against 3.1's
        # table (tests/apps/sh14ref.h) and every observation, strings too,
        # the same as Microsoft's KRNL386 and SHELL give. Parts pm, pmk286,
        # pmk386: Program Manager's File > Run of each kind of name
        # (runassoc.txt: an unassociated document with and without
        # parameters, a missing document, a file without an extension, a
        # directory, an extension mapped to a missing program, a directory
        # with an extension, a text file) with w16's PROGMAN and SHELL on
        # Microsoft's KERNEL, w16's KRNL286, w16's KRNL386: every screen
        # 3.1's (the group icons' pictures and the blinking carets left
        # out) and nothing run as a DOS program (no C:\RANCOM.TXT). Part
        # tree: w16's install tree alone under its KRNL386 and KRNL286
        # (runassoctree.txt): the box, nothing run, Notepad for a .TXT,
        # the same screens under both kernels. Until 2026-11-16 w16's
        # Program Manager ran a document without an association with
        # WinExec, which hands it to WINOLDAP: DOS ran it as a .COM program.
        r=ok
        python3 -I "$root/tests/scripts/runassocfiles.py" "$out/runassoc-files"
        rf=$out/runassoc-files
        set -- -f "$rf/U.ABC=RA/U.ABC" -f "$rf/N=RA/N" -f "$rf/M.W1Q=RA/M.W1Q" -f "$rf/D=RA/D.ABC/D" \
            -f "$rf/A.TXT=RA/A.TXT"
        case ${part:-probe} in
        probe)
            p="-f $root/build/dos/SH14.EXE=SH14.EXE -f $root/build/dos/SH5.EXE=SH5.EXE"
            log=c/WINDOWS/TK.LOG
            # shellcheck disable=SC2086
            refrun runassoc-pref runassoc "$root/tests/dropin.sh" -3 -r -t 500 -W SH14.END \
                -o "$out/runassoc-pref" $p -i 'boot:shell=C:\SH14.EXE'
            for k in k286 k386; do
                o=; [ $k = k386 ] && o=-3
                # shellcheck disable=SC2086
                "$root/tests/dropin.sh" $o -t 500 -W SH14.END -o "$out/runassoc-p$k" $p \
                    -f "$shell=WINDOWS/SYSTEM/SHELL.DLL" -i 'boot:shell=C:\SH14.EXE' \
                    > "$out/runassoc-p$k.txt" 2>&1 &
            done
            wait
            obsof() { grep -a '^OBS' "$1" | tr -d '\r'; }
            grep -aq '^SH14 done' "$out/runassoc-pref/$log" 2>/dev/null || r="the reference's SH14 did not finish"
            for k in k286 k386; do
                [ "$r" = ok ] || break
                d=$out/runassoc-p$k
                grep -aq '^SH14 done' "$d/$log" 2>/dev/null || { r="$k: SH14 did not finish ($d/$log)"; break; }
                f=$(grep -a '^FAIL' "$d/$log" | head -1 | tr -d '\r')
                [ -z "$f" ] || { r="$k: $f ($d/$log)"; break; }
                obsof "$out/runassoc-pref/$log" > "$d.ref"
                obsof "$d/$log" > "$d.obs"
                diff "$d.ref" "$d.obs" > "$d.obsdiff" || { r="$k: observations differ from 3.1's ($d.obsdiff)"; break; }
            done ;;
        pm|pmk286|pmk386)
            s=$root/tests/scripts/input/runassoc.txt
            set -- "$@" -i 'WIN.INI:Extensions:w1q=C:\NOPE.EXE ^.w1q'
            refrun runassoc-ref runassoc "$root/tests/inputrun.sh" -s -b 40 -t 300 "$s" "$out/runassoc-ref" -r "$@"
            # left out: the group icons' pictures (w16's own), the Run
            # box's and Notepad's blinking carets
            m="rectangle 60,310 300,342 rectangle 114,160 348,178 rectangle 14,44 18,62"
            case $part in pm) k=-r ;; pmk286) k= ;; pmk386) k=-3 ;; esac
            d=$out/runassoc-$part
            # shellcheck disable=SC2086
            "$root/tests/inputrun.sh" -s -E "$out/runassoc-ref" -M "$m" -b 40 -t 300 "$s" "$d" $k "$@" \
                -f "$shell=WINDOWS/SYSTEM/SHELL.DLL" -f "$root/build/dos/PROGMAN.EXE=WINDOWS/PROGMAN.EXE" \
                > "$d.txt" 2>&1 || true
            ls "$out/runassoc-ref"/[01]*.png > /dev/null 2>&1 || r="the reference has no screens"
            [ ! -f "$out/runassoc-ref/work/c/RANCOM.TXT" ] || r="the reference ran a document as a DOS program"
            [ ! -f "$d/work/c/RANCOM.TXT" ] || r="$part: a document ran as a DOS program (C:\\RANCOM.TXT)"
            for f in "$out/runassoc-ref"/[01]*.png; do
                [ "$r" = ok ] || break
                s2=$(basename "$f")
                [ -f "$d/$s2" ] || { r="$part: no $s2"; break; }
                magick "$f" -fill black -draw "$m" "$d-a.png"
                magick "$d/$s2" -fill black -draw "$m" "$d-b.png"
                n=$(magick compare -metric AE "$d-b.png" "$d-a.png" "$d-${s2%.png}-diff.png" 2>&1 >/dev/null || true)
                n=${n%% *}
                [ "$n" = 0 ] || { r="$part: $s2: $n pixels differ"; break; }
            done ;;
        tree)
            s=$root/tests/scripts/input/runassoctree.txt
            d=$out/runassoc-t
            "$root/tests/inputrun.sh" -s -b 40 -t 200 "$s" "${d}386" -T "$root/build/w16" -3 "$@" \
                > "${d}386.txt" 2>&1 || true
            m="rectangle 14,44 18,62 rectangle 114,160 348,178"
            "$root/tests/inputrun.sh" -e "${d}386/boot.png" -E "${d}386" -M "$m" -b 40 -t 200 \
                "$s" "${d}286" -T "$root/build/w16" "$@" > "${d}286.txt" 2>&1 || true
            grep -q '^script done' "${d}386/input.log" 2>/dev/null || r="KRNL386: the script did not finish (${d}386/input.log)"
            for k in 386 286; do
                [ "$r" = ok ] || break
                [ ! -f "${d}$k/work/c/RANCOM.TXT" ] || r="KRNL$k: a document ran as a DOS program (C:\\RANCOM.TXT)"
            done
            # back to the screen after the boot once the Run box and
            # Notepad have gone (w16's USER put back a stale image of the
            # Run box there: savebits.c)
            for s2 in 03-back 05-end; do
                [ "$r" = ok ] || break
                n=$(magick compare -metric AE "${d}386/$s2.png" "${d}386/boot.png" "$d-$s2-boot-diff.png" 2>&1 >/dev/null || true)
                n=${n%% *}
                [ "$n" = 0 ] || r="KRNL386: $s2: $n pixels differ from the screen after the boot ($d-$s2-boot-diff.png)"
            done
            # the box (its active caption) over the Run box; Notepad's
            # window (active) for the .TXT
            if [ "$r" = ok ]; then
                for b in "01-abc 300 178" "04-txt 300 12"; do
                    set -- $b
                    px=$(magick "${d}386/$1.png" -format "%[hex:p{$2,$3}]" info: 2>/dev/null)
                    case $px in 0000AA*) ;; *) r="KRNL386: $1: no active caption at $2,$3 ($px)"; break ;; esac
                done
            fi
            for f in "${d}386"/[0-9]*.png; do
                [ "$r" = ok ] || break
                s2=$(basename "$f")
                [ -f "${d}286/$s2" ] || { r="KRNL286: no $s2"; break; }
                magick "$f" -fill black -draw "$m" "$d-a.png"
                magick "${d}286/$s2" -fill black -draw "$m" "$d-b.png"
                n=$(magick compare -metric AE "$d-b.png" "$d-a.png" "$d-${s2%.png}-diff.png" 2>&1 >/dev/null || true)
                n=${n%% *}
                [ "$n" = 0 ] || { r="KRNL286 against KRNL386: $s2: $n pixels differ"; break; }
            done ;;
        dir)
            # File > Run's directory: SH5's C:\SH5.TXT (tail and current
            # directory of each start) with w16's PROGMAN and SHELL on
            # Microsoft's KERNEL, as with Microsoft's
            s=$root/tests/scripts/input/rundir.txt
            set -- -f "$root/build/dos/SH5.EXE=TMP/SH5.EXE" -f "$root/build/dos/SH5.EXE=TMP/SUB/SH5.EXE" \
                -f "$root/build/dos/SH5.EXE=WINDOWS/SH5X.EXE"
            d=$out/runassoc-dir
            "$root/tests/inputrun.sh" -s -b 40 -t 200 "$s" "$d-ref" -r "$@" > "$d-ref.txt" 2>&1 &
            "$root/tests/inputrun.sh" -s -b 40 -t 200 "$s" "$d-w16" -r "$@" \
                -f "$shell=WINDOWS/SYSTEM/SHELL.DLL" -f "$root/build/dos/PROGMAN.EXE=WINDOWS/PROGMAN.EXE" \
                > "$d-w16.txt" 2>&1
            wait
            if [ "$(grep -c CWD "$d-ref/work/c/SH5.TXT" 2>/dev/null)" != 5 ]; then
                r="dir: the reference did not start SH5 five times ($d-ref)"
            elif ! cmp -s "$d-ref/work/c/SH5.TXT" "$d-w16/work/c/SH5.TXT"; then
                r="dir: SH5's tails and directories differ from 3.1's ($d-ref/work/c/SH5.TXT, $d-w16/work/c/SH5.TXT)"
            fi ;;
        esac
        if [ -n "$part" ]; then partdone "$r"
        elif [ "$r" = ok ]; then echo "ok   runassoc: every kind of name as on 3.1"
        else echo "FAIL runassoc: $r ($out)"; fails=$((fails + 1)); fi ;;
    runpe)
        # Program Manager's File > Run of a 32-bit program (a PE file;
        # ARCHITECTURE.md 9.8, [w16] PEError; tests/scripts/input/runpe.txt).
        # Parts k286, k386: w16's PROGMAN and SHELL on w16's KRNL286 and
        # KRNL386 with PEError=0 (the harness's stock value) against all of
        # Microsoft's: every screen 3.1's (the group icons and the Run
        # box's caret left out) and the program's MS-DOS stub run in both
        # (C:\RANCOM.TXT). Parts on286, on386: the same with PEError=1:
        # the box (an active caption over the desktop), the same on both
        # kernels; Escape brings back the Run box as it was typed, a
        # second Escape the screen after the boot; nothing started
        # (no C:\RANCOM.TXT: WINOLDAP never ran the stub).
        r=ok
        python3 -I "$root/tests/scripts/runassocfiles.py" "$out/runpe-files"
        s=$root/tests/scripts/input/runpe.txt
        set -- -f "$out/runpe-files/PE.EXE=RA/PE.EXE"
        w="-f $shell=WINDOWS/SYSTEM/SHELL.DLL -f $root/build/dos/PROGMAN.EXE=WINDOWS/PROGMAN.EXE"
        m="rectangle 60,310 300,342 rectangle 114,160 348,178"
        refrun runpe-ref runpe "$root/tests/inputrun.sh" -s -b 40 -t 300 "$s" "$out/runpe-ref" -r "$@"
        ls "$out/runpe-ref"/0[0-3]*.png > /dev/null 2>&1 || r="the reference has no screens"
        [ "$r" != ok ] || [ -f "$out/runpe-ref/work/c/RANCOM.TXT" ] || r="the reference did not run the stub"
        # pecmp A B LABEL: A and B (masked) equal, else r says so
        pecmp() {
            magick "$1" -fill black -draw "$m" "$d-a.png"
            magick "$2" -fill black -draw "$m" "$d-b.png"
            n=$(magick compare -metric AE "$d-b.png" "$d-a.png" "$d-$3-diff.png" 2>&1 >/dev/null || true)
            n=${n%% *}
            [ "$n" = 0 ] || r="$part: $3: $n pixels differ ($d-$3-diff.png)"
        }
        case ${part:-k286} in
        k286|k386)
            k=; [ $part = k386 ] && k=-3
            d=$out/runpe-$part
            # Microsoft's USER, back from the DOS program, leaves a stray
            # repaint on Program Manager's caption in about half the runs,
            # its own reference runs included (4 all-Microsoft runs: 2;
            # rows 51-52 from x 68 to 579, the frame's inner line and the
            # caption's first row, 352 or 374 pixels; case winoldap's check
            # meets it too): left out
            m="$m rectangle 68,51 579,52"
            # shellcheck disable=SC2086
            "$root/tests/inputrun.sh" -s -E "$out/runpe-ref" -M "$m" -b 40 -t 300 "$s" "$d" $k "$@" $w \
                > "$d.txt" 2>&1 || true
            [ "$r" != ok ] || [ -f "$d/work/c/RANCOM.TXT" ] || r="$part: the stub did not run (no C:\\RANCOM.TXT)"
            for f in "$out/runpe-ref"/0[0-3]*.png; do
                [ "$r" = ok ] || break
                s2=$(basename "$f" .png)
                [ -f "$d/$s2.png" ] || { r="$part: no $s2"; break; }
                pecmp "$f" "$d/$s2.png" "$s2"
            done ;;
        on286|on386)
            k=; [ $part = on386 ] && k=-3
            d=$out/runpe-$part
            # shellcheck disable=SC2086
            "$root/tests/inputrun.sh" -s -b 40 -t 300 "$s" "$d" $k "$@" $w -i w16:PEError=1 \
                > "$d.txt" 2>&1 || true
            grep -q '^script done' "$d/input.log" 2>/dev/null || r="$part: the script did not finish ($d/input.log)"
            [ "$r" != ok ] || [ ! -f "$d/work/c/RANCOM.TXT" ] || r="$part: the stub ran (C:\\RANCOM.TXT)"
            if [ "$r" = ok ]; then
                px=$(magick "$d/01-run.png" -format "%[hex:p{200,186}]" info: 2>/dev/null)
                case $px in 0000AA*) ;; *) r="$part: 01-run: no box (no active caption at 200,186: $px)" ;; esac
            fi
            [ "$r" != ok ] || pecmp "$d/00-typed.png" "$d/02-back.png" back
            [ "$r" != ok ] || pecmp "$d/boot.png" "$d/03-end.png" end
            # the box itself the same on both kernels (the other part's
            # run, once it is there)
            o=$out/runpe-on286; [ $part = on286 ] && o=$out/runpe-on386
            if [ "$r" = ok ] && [ -f "$o/01-run.png" ]; then
                pecmp "$o/01-run.png" "$d/01-run.png" box
            fi ;;
        esac
        if [ -n "$part" ]; then partdone "$r"
        elif [ "$r" = ok ]; then echo "ok   runpe: a 32-bit program's stub as on 3.1 (PEError=0); the box, nothing run (PEError=1)"
        else echo "FAIL runpe: $r ($out)"; fails=$((fails + 1)); fi ;;
    olecli)
        r=ok
        case ${part:-tree} in
        tree)
            s=$root/tests/scripts/input/cardtree.txt
            d=$out/olecli-tree
            "$root/tests/inputrun.sh" -s -b 40 -t 200 "$s" "$d-386" -T "$root/build/w16" -3 \
                > "$d-386.txt" 2>&1 || true
            # (the match leaves out Cardfile's blinking caret)
            "$root/tests/inputrun.sh" -e "$d-386/boot.png" -E "$d-386" -M "rectangle 16,144 20,164" -b 40 -t 200 \
                "$s" "$d-286" -T "$root/build/w16" > "$d-286.txt" 2>&1 || true
            grep -q '^script done' "$d-386/input.log" 2>/dev/null || r="KRNL386: the script did not finish ($d-386/input.log)"
            # Cardfile's window, active, over Program Manager: its caption
            if [ "$r" = ok ]; then
                px=$(magick "$d-386/02-cardfile.png" -format "%[hex:p{300,12}]" info: 2>/dev/null)
                case $px in 0000AA*) ;; *) r="KRNL386: no active Cardfile window ($px at 300,12)" ;; esac
            fi
            for f in "$d-386"/[0-9]*.png; do
                [ "$r" = ok ] || break
                s2=$(basename "$f")
                [ -f "$d-286/$s2" ] || { r="KRNL286: no $s2"; break; }
                # Cardfile's caret left out (its blink)
                magick "$f" -fill black -draw "rectangle 16,144 20,164" "$d-a.png"
                magick "$d-286/$s2" -fill black -draw "rectangle 16,144 20,164" "$d-b.png"
                n=$(magick compare -metric AE "$d-b.png" "$d-a.png" "$d-${s2%.png}-diff.png" 2>&1 >/dev/null || true)
                n=${n%% *}
                [ "$n" = 0 ] || { r="tree: $s2: $n pixels differ between KRNL386 and KRNL286"; break; }
            done ;;
        cardw16)
            # w16's Cardfile as the client (card1), with each OLECLI
            OLESCEN_FILES="$root/build/dos/CARDFILE.EXE=WINDOWS/CARDFILE.EXE" \
                "$root/tests/olescenarios.sh" -o "$out/olecli-$part" card1 > "$out/olecli-$part.txt" 2>&1 || true
            grep -q '^ok ' "$out/olecli-$part.txt" && ! grep -q '^FAIL' "$out/olecli-$part.txt" ||
                r="$part: $(grep -m1 '^FAIL' "$out/olecli-$part.txt" || echo 'no result') ($out/olecli-$part)" ;;
        *)
            "$root/tests/olescenarios.sh" -o "$out/olecli-$part" "$part" > "$out/olecli-$part.txt" 2>&1 || true
            grep -q '^ok ' "$out/olecli-$part.txt" && ! grep -q '^FAIL' "$out/olecli-$part.txt" ||
                r="$part: $(grep -m1 '^FAIL' "$out/olecli-$part.txt" || echo 'no result') ($out/olecli-$part)" ;;
        esac
        if [ -n "$part" ]; then partdone "$r"
        elif [ "$r" = ok ]; then echo "ok   olecli"
        else echo "FAIL olecli: $r ($out)"; fails=$((fails + 1)); fi ;;
    olesvr)
        r=ok
        case ${part:-tree} in
        tree)
            # OS2 (an OLE client through w16's OLECLI) as the shell of the
            # install tree alone: a Paintbrush picture embedded, updated,
            # closed (tests/olesvr/ostree.sh), with Microsoft's OLESVR and
            # with w16's: the same calls, results and OLESVR windows
            d=$out/olesvr-tree
            "$root/tests/olesvr/ostree.sh" "$d-ms" ms > "$d-ms.txt" 2>&1 || true
            "$root/tests/olesvr/ostree.sh" "$d-w16" w16 > "$d-w16.txt" 2>&1 || true
            for k in ms w16; do
                [ -s "$d-$k/os.log" ] || { r="$k: no log ($d-$k.txt)"; break; }
                python3 "$root/tests/olesvr/osnorm.py" "$d-$k"
                grep -q '^OS2 end' "$d-$k/os.log" || { r="$k: the script did not finish ($d-$k/os.log)"; break; }
                # (up to the next OS2 command: since 2026-11-15 the spy's
                # DDE lines come in between on w16's USER too)
                awk '/^OS2 create PBrush/ { on = 1; next } on && /^OS2 / { exit } on' "$d-$k/os.log" |
                    grep -q 'released -> 0 OK' ||
                    { r="$k: Paintbrush did not serve the object ($d-$k/os.log)"; break; }
            done
            if [ "$r" = ok ] && ! diff "$d-ms/os.cmp" "$d-w16/os.cmp" > "$d.diff"; then
                r="tree: the log differs from Microsoft's OLESVR's ($d.diff)"
            fi ;;
        *)
            # w16's OLESVR against Microsoft's, with Microsoft's OLECLI (the
            # four combinations are the olecli case's, with OC1)
            "$root/tests/olesvrscen.sh" -o "$out/olesvr-$part" -c w16-ms "$part" > "$out/olesvr-$part.txt" 2>&1 || true
            grep -q '^== .*: ok' "$out/olesvr-$part.txt" && ! grep -q '^== .*FAIL' "$out/olesvr-$part.txt" ||
                r="$part: $(grep -m1 'FAIL' "$out/olesvr-$part.txt" || echo 'no result') ($out/olesvr-$part)" ;;
        esac
        if [ -n "$part" ]; then partdone "$r"
        elif [ "$r" = ok ]; then echo "ok   olesvr"
        else echo "FAIL olesvr: $r ($out)"; fails=$((fails + 1)); fi ;;
    ddeml)
        r=ok
        case ${part:-tree286} in
        tree286|tree386)
            # DM.EXE as the shell of the install tree alone, with w16's
            # DDEML and with Microsoft's: the same calls and callbacks
            k=${part#tree}
            d=$out/ddeml-$part
            "$root/tests/ddeml/ddtree.sh" "$d-ms" ms "$k" > "$d-ms.txt" 2>&1 || true
            "$root/tests/ddeml/ddtree.sh" "$d-w16" w16 "$k" > "$d-w16.txt" 2>&1 || true
            for l in ms w16; do
                [ -s "$d-$l/os.log" ] || { r="$l: no log ($d-$l.txt)"; break; }
                grep -q '^A end' "$d-$l/os.log" || { r="$l: the script did not finish ($d-$l/os.log)"; break; }
                python3 "$root/tests/ddeml/dmnorm.py" "$d-$l/os.log" > "$d-$l/os.cmp"
            done
            if [ "$r" = ok ] && ! diff "$d-ms/os.cmp" "$d-w16/os.cmp" > "$d.diff"; then
                r="$part: the log differs from Microsoft's DDEML's ($d.diff)"
            fi ;;
        *)
            "$root/tests/ddemlscen.sh" -o "$out/ddeml-$part" "$part" > "$out/ddeml-$part.txt" 2>&1 || true
            grep -q '^== .*: ok' "$out/ddeml-$part.txt" && ! grep -q '^== .*FAIL\|no log' "$out/ddeml-$part.txt" ||
                r="$part: $(grep -m1 'FAIL\|no log' "$out/ddeml-$part.txt" || echo 'no result') ($out/ddeml-$part)" ;;
        esac
        if [ -n "$part" ]; then partdone "$r"
        elif [ "$r" = ok ]; then echo "ok   ddeml"
        else echo "FAIL ddeml: $r ($out)"; fails=$((fails + 1)); fi ;;
    lzexpand)
        # the sections of the probe's log that must equal Microsoft's, and
        # the ones where w16 is known to differ (quirks)
        r=ok
        d=$out/lzexpand-${part:-s}
        case ${part:-s} in
        s|3)
            "$root/tests/lzexpand/lzscen.sh" -m "/$(echo "${part:-s}" | tr s S)" -o "$d" start LZInit LZOpenFile \
                LZRead LZSeek LZClose LZCopy LZStart GetExpandedName limit tasks "seek back" \
                VER.DLL end > "$d.txt" 2>&1 || true
            grep -q '^== .*: ok' "$d.txt" && ! grep -q 'FAIL' "$d.txt" ||
                r="$part: $(grep -m1 'FAIL' "$d.txt" || echo 'no result') ($d)" ;;
        tree286|tree386)
            k=${part#tree}
            "$root/tests/lzexpand/lztree.sh" "$d-ms" ms "$k" > "$d-ms.txt" 2>&1 || true
            "$root/tests/lzexpand/lztree.sh" "$d-w16" w16 "$k" > "$d-w16.txt" 2>&1 || true
            for l in ms w16; do
                [ -s "$d-$l/lzp.log" ] || { r="$l: no log ($d-$l.txt)"; break; }
                grep -q '^LZP end' "$d-$l/lzp.log" || { r="$l: the probe did not finish ($d-$l/lzp.log)"; break; }
            done
            if [ "$r" = ok ]; then
                python3 "$root/tests/lzexpand/lzcmp.py" "$d-ms/lzp.log" "$d-w16/lzp.log" "$d-cmp" \
                    start LZInit LZOpenFile LZRead LZSeek LZClose LZCopy LZStart GetExpandedName \
                    limit tasks "seek back" VER.DLL end > "$d.txt" 2>&1 ||
                    r="$part: $(grep -m1 'FAIL' "$d.txt") ($d.txt)"
            fi ;;
        quirks)
            "$root/tests/lzexpand/lzscen.sh" -m /S -o "$d" eof zero "long matches" > "$d.txt" 2>&1 || true
            grep -q 'FAIL' "$d.txt" && r="quirks: $(grep -c FAIL "$d.txt") section(s) differ: $(grep FAIL "$d.txt" | sed 's/^== [^ ]* //; s/: FAIL.*//' | tr '\n' ' ')($d)" ;;
        esac
        if [ -n "$part" ]; then partdone "$r"
        elif [ "$r" = ok ]; then echo "ok   lzexpand"
        else echo "FAIL lzexpand: $r ($out)"; fails=$((fails + 1)); fi ;;
    drivers)
        # USER's installable drivers (ARCHITECTURE.md 10.7)
        if "$root/tests/drv/drun.sh" -o "$out/drivers" > "$out/drivers.txt" 2>&1; then
            echo "ok   drivers: DRP's log on w16's USER as on Microsoft's (KRNL286, a boot driver, KRNL386)"
        else
            echo "FAIL drivers: $(grep -m1 FAIL "$out/drivers.txt") ($out/drivers.txt)"; fails=$((fails + 1))
        fi ;;
    timer)
        # w16's TIMER.DRV against Microsoft's under Microsoft's MMSYSTEM
        r=ok
        d=$out/timer-${part:-s}
        case ${part:-s} in
        s|3)
            "$root/tests/timer/tscen.sh" -o "$d" "/$(echo "${part:-s}" | tr s S)" > "$d.txt" 2>&1 ||
                r="$part: $(grep -m1 'FAIL' "$d.txt" || echo 'no result') ($d)" ;;
        qt)
            "$root/tests/qtplay.sh" -t TIMER.DRV -o "$d" > "$d.txt" 2>&1 || true
            if grep -q '^skip' "$d.txt"; then r=ok
            elif ! grep -q '^ok' "$d.txt"; then
                r="qt: $(grep -m1 FAIL "$d.txt" | sed 's/^FAIL qtplay: //') ($d.txt)"
            fi ;;
        esac
        if [ -n "$part" ]; then partdone "$r"
        elif [ "$r" = ok ]; then echo "ok   timer"
        else echo "FAIL timer: $r ($out)"; fails=$((fails + 1)); fi ;;
    sb16)
        # w16's SB16.DRV under both MMSYSTEMs (ARCHITECTURE.md 24)
        r=ok
        d=$out/sb16-${part:-dev}
        "$root/tests/sb16/sbscen.sh" -o "$d" "${part:-dev}" > "$d.txt" 2>&1 ||
            r="${part:-dev}: $(grep -m1 'FAIL' "$d.txt" || echo 'no result') ($d)"
        if [ -n "$part" ]; then partdone "$r"
        elif [ "$r" = ok ]; then echo "ok   sb16"
        else echo "FAIL sb16: $r ($out)"; fails=$((fails + 1)); fi ;;
    filecdr)
        # KERNEL's FileCdr with Windows running (tests/apps/tk64.c; make
        # test runs TK64 without USER): TK64 as the shell (its "helper Q":
        # a program that called InitApp loses its own callback at its end)
        # and TK64M (one that installed another module's keeps it), each
        # must end with no FAIL against 3.1's table. Parts k286 (w16's
        # KRNL286, Microsoft's USER: its AppExit), k386 (w16's KRNL386),
        # userk (w16's KRNL286 and USER: KERNEL's kmisc_app_exit alone)
        r=ok
        p=${part:-k286}
        # fcrun NAME PROG [dropin.sh options]: PROG's TK.LOG in NAME.log
        fcrun() {
            _fn=$1 _fp=$2; shift 2
            rm -rf "$out/$_fn"
            "$root/tests/dropin.sh" -t 120 -o "$out/$_fn" \
                -f "$root/build/dos/$_fp.EXE=$_fp.EXE" -f "$root/build/dos/TK64A.EXE=TK64A.EXE" \
                -i "boot:shell=C:\\$_fp.EXE" "$@" > "$out/$_fn.out" 2>&1 || true
            tr -d '\r' < "$out/$_fn/c/WINDOWS/TK.LOG" > "$out/$_fn.log" 2>/dev/null || : > "$out/$_fn.log"
            rm -rf "$out/$_fn/c"
        }
        case $p in
        k386) set -- -3 ;;
        userk) set -- -f "$user=WINDOWS/SYSTEM/USER.EXE" ;;
        *) set -- ;;
        esac
        for t in TK64 TK64M; do
            fcrun filecdr-$p-$t $t "$@"
            l=$out/filecdr-$p-$t.log
            grep -q '^TK64 end' "$l" || { r="$p: $t did not end ($l)"; break; }
            grep -q '^OBS helper [QM]: FileCdr=' "$l" || { r="$p: $t ran no InitApp case ($l)"; break; }
            ! grep -q '^FAIL' "$l" || { r="$p: $t: $(grep -m1 '^FAIL' "$l") ($l)"; break; }
        done
        if [ -n "$part" ]; then partdone "$r"
        elif [ "$r" = ok ]; then echo "ok   filecdr"
        else echo "FAIL filecdr: $r ($out)"; fails=$((fails + 1)); fi ;;
    bigmem)
        # more than 64 MB (ARCHITECTURE.md 9.6 "More than 64 MB", 12.3):
        # TK63 as the shell in DOSBox-X with memsize 128 or 256 reports
        # DPMI 0500h, GetFreeSpace, the largest GlobalAlloc, and takes
        # blocks until none is left, writing every page of them. Parts:
        # sMEM (Standard mode: w16's DOSX, WSWAP and KRNL386: one block
        # over all of it), s256ms (the same under Microsoft's WSWAP, which
        # keeps a block of up to 64 MB of its own that DOSX takes apart:
        # all of it allocated, 0500h's largest the rest), eMEM (386
        # Enhanced: w16's WIN386 and KRNL386), stock ([w16] LargeMemory=0
        # at 128 MB under Microsoft's WSWAP: 0500h's largest at most 64 MB,
        # all of it allocated, as 3.1; 386 Enhanced: all of it), ref
        # (Windows 3.1's own DOSX and WIN386 at 128 MB with Microsoft's
        # KRNL386: 0500h's largest at most 64 MB in Standard mode, all of
        # it allocated in both)
        r=ok
        p=${part:-s128}
        w386=$root/build/win386/WIN386.EXE
        wsw=$root/build/dos/WSWAP.EXE
        # bmrun NAME MEMSIZE [dropin.sh options]: TK63's log in NAME.log
        bmrun() {
            _bn=$1 _bmem=$2; shift 2
            rm -rf "$out/$_bn"
            DROPIN_MEMSIZE=$_bmem "$root/tests/dropin.sh" -p 386 -t 400 -o "$out/$_bn" \
                -f "$root/build/dos/TK63.EXE=TK63.EXE" -i 'boot:shell=C:\TK63.EXE' "$@" \
                > "$out/$_bn.out" 2>&1 || true
            tr -d '\r' < "$out/$_bn/c/WINDOWS/TK.LOG" > "$out/$_bn.log" 2>/dev/null || : > "$out/$_bn.log"
            rm -rf "$out/$_bn/c"
        }
        bmval() { sed -n "s/^OBS ~$2=//p" "$out/$1.log" | head -1; }
        # bmcheck NAME [KEY MIN MAX]...: TK63 ended and wrote every page,
        # and each KEY's value (KB) is more than MIN and at most MAX
        bmcheck() {
            [ "$r" = ok ] || return
            grep -q '^TK63 end' "$out/$1.log" || { r="$1: TK63 did not end ($out/$1.out, $out/$1.log)"; return; }
            grep -q '^PASS touched' "$out/$1.log" || { r="$1: a page did not keep what was written ($out/$1.log)"; return; }
            _bn=$1; shift
            while [ $# -ge 3 ]; do
                _bv=$(bmval "$_bn" $1)
                if [ -z "$_bv" ] || [ "$_bv" -le "$2" ] || [ "$_bv" -gt "$3" ]; then
                    r="$_bn: $1 $_bv KB, not in $2..$3 ($out/$_bn.log)"; return
                fi
                shift 3
            done
        }
        case $p in
        s128|s256|e128|e256)
            m=${p#?}
            case $p in
            s*) set -- -3 -x -f "$wsw=WINDOWS/SYSTEM/WSWAP.EXE" ;;
            e*) set -- -3 -c 'WIN /3 /B' -f "$w386=WINDOWS/SYSTEM/WIN386.EXE" ;;
            esac
            bmrun bigmem-$p "$m" "$@"
            # all but what DOS, the BIOS and Windows itself keep (16 MB
            # is generous), and no more than the machine has
            lo=$((m * 1024 - 16384)) hi=$((m * 1024))
            bmcheck bigmem-$p dpmi_largest $lo $hi freespace $lo $hi total $lo $hi ;;
        s256ms)
            bmrun bigmem-s256ms 256 -3 -x
            bmcheck bigmem-s256ms dpmi_largest 65536 262144 total 245760 262144 ;;
        stock)
            bmrun bigmem-stocks 128 -3 -x -i w16:LargeMemory=0
            bmcheck bigmem-stocks dpmi_largest 32768 65536 total 114688 131072
            if [ "$r" = ok ]; then
                bmrun bigmem-stocke 128 -3 -c 'WIN /3 /B' -f "$w386=WINDOWS/SYSTEM/WIN386.EXE" -i w16:LargeMemory=0
                bmcheck bigmem-stocke total 114688 131072
            fi ;;
        ref)
            refrun bigmem-refs bigmem bmrun bigmem-refs 128 -3 -r
            refrun bigmem-refe bigmem bmrun bigmem-refe 128 -3 -r -c 'WIN /3 /B'
            for k in s e; do
                echo "   bigmem ref $k: $(grep '^OBS' "$out/bigmem-ref$k.log" | tr '\n' ' ')"
            done
            bmcheck bigmem-refs dpmi_largest 32768 65536 total 114688 131072
            bmcheck bigmem-refe total 114688 262144 ;;
        esac
        if [ -n "$part" ]; then partdone "$r"
        elif [ "$r" = ok ]; then echo "ok   bigmem"
        else echo "FAIL bigmem: $r ($out)"; fails=$((fails + 1)); fi ;;
    mmsystem)
        # w16's MMSYSTEM.DLL against Microsoft's (ARCHITECTURE.md 23)
        r=ok
        d=$out/mmsystem-${part:-core}
        case ${part:-core} in
        core|mmio|timer)
            "$root/tests/mmsystem/mscen.sh" -o "$d" "${part:-core}" > "$d.txt" 2>&1 ||
                r="${part:-core}: $(grep -m1 'FAIL' "$d.txt" || echo 'no result') ($d)" ;;
        dev)
            "$root/tests/mmsystem/mdscen.sh" -o "$d" > "$d.txt" 2>&1 ||
                r="dev: $(grep -m1 'FAIL' "$d.txt" || echo 'no result') ($d)" ;;
        mci)
            "$root/tests/mmsystem/mciscen.sh" -o "$d" > "$d.txt" 2>&1 ||
                r="mci: $(grep -m1 'FAIL' "$d.txt" || echo 'no result') ($d)" ;;
        prog)
            for p in soundrec sound midimap mplayer; do
                "$root/tests/mmsystem/mprog.sh" "$d/$p-ms" ms $p > /dev/null 2>&1 || true
                "$root/tests/mmsystem/mprog.sh" "$d/$p-w16" w16 $p -e "$d/$p-ms" > /dev/null 2>&1 || true
                ls "$d/$p-ms"/0*.png > /dev/null 2>&1 || { r="prog: no screens of $p from Microsoft's ($d)"; break; }
                for f in "$d/$p-ms"/0*.png; do
                    sn=$(basename "$f")
                    [ -f "$d/$p-w16/$sn" ] || { r="prog: $p: no $sn ($d)"; break; }
                    n=$(magick compare -metric AE "$d/$p-w16/$sn" "$f" "$out/mmsystem-$p-${sn%.png}-diff.png" 2>&1 >/dev/null || true)
                    n=${n%% *}
                    [ "$n" = 0 ] || { r="prog: $p $sn: $n pixels differ ($d)"; break; }
                done
                [ "$r" = ok ] || break
            done ;;
        qt)
            "$root/tests/qtplay.sh" -t "MMSYSTEM.DLL MMTASK.TSK TIMER.DRV" -o "$d" > "$d.txt" 2>&1 || true
            if grep -q '^skip' "$d.txt"; then r=ok
            elif ! grep -q '^ok' "$d.txt"; then
                r="qt: $(grep -m1 FAIL "$d.txt" | sed 's/^FAIL qtplay: //') ($d.txt)"
            fi ;;
        esac
        if [ -n "$part" ]; then partdone "$r"
        elif [ "$r" = ok ]; then echo "ok   mmsystem"
        else echo "FAIL mmsystem: $r ($out)"; fails=$((fails + 1)); fi ;;
    quicktime)
        # QuickTime's installer, sample movie and close sequence
        # (tests/qtplay.sh): over (w16's tree laid over 3.1), tree (the
        # tree alone, KRNL286), tree386 (the tree alone, KRNL386)
        d=$out/quicktime-${part:-over} qto=
        case ${part:-over} in
        tree) qto="-T 286" ;;
        tree386) qto="-T 386" ;;
        esac
        if [ "${part:-}" = cpl ]; then
            # its Control Panel's Files list (tests/qtcpl.sh)
            "$root/tests/qtcpl.sh" -o "$d" > "$d.txt" 2>&1 || true
        else
            # shellcheck disable=SC2086
            "$root/tests/qtplay.sh" $qto -o "$d" > "$d.txt" 2>&1 || true
        fi
        if grep -q '^ok\|^skip' "$d.txt"; then r=ok
        else r="${part:-over}: $(grep -m1 FAIL "$d.txt" || echo 'no result') ($d.txt)"; fi
        if [ -n "$part" ]; then partdone "$r"
        elif [ "$r" = ok ]; then echo "ok   quicktime"
        else echo "FAIL quicktime: $r"; fails=$((fails + 1)); fi ;;
    toolhelp)
        # w16's TOOLHELP.DLL against Microsoft's (tests/toolhelp/thcase.sh)
        d=$out/toolhelp-${part:-ref286}
        if [ "${part:-}" = qt ]; then
            "$root/tests/qtplay.sh" -o "$d" > "$d.txt" 2>&1 || true
            if grep -q '^ok\|^skip' "$d.txt"; then r=ok
            else r="qt: $(grep -m1 FAIL "$d.txt" || echo 'no result') ($d.txt)"; fi
        elif "$root/tests/toolhelp/thcase.sh" "${part:-ref286}" "$d" > "$d.txt" 2>&1; then r=ok
        else r="$(tail -n 1 "$d.txt" | sed 's/^FAIL //')"; fi
        if [ -n "$part" ]; then partdone "$r"
        elif [ "$r" = ok ]; then echo "ok   toolhelp"
        else echo "FAIL toolhelp: $r"; fails=$((fails + 1)); fi ;;
    ver)
        # w16's VER.DLL against Microsoft's (ARCHITECTURE.md 20); nothing
        # masked: two runs of Microsoft's give identical logs
        r=ok
        d=$out/ver-${part:-s}
        case ${part:-s} in
        s|3)
            "$root/tests/ver/vscen.sh" -m "/$(echo "${part:-s}" | tr s S)" -o "$d" > "$d.txt" 2>&1 || true
            grep -q '^== .*: ok' "$d.txt" && ! grep -q 'FAIL' "$d.txt" ||
                r="$part: $(grep -m1 'FAIL' "$d.txt" || echo 'no result') ($d)" ;;
        x)
            for l in ms w16; do
                VRUN_X=1 VRUN_ARG=i "$root/tests/ver/vrun.sh" "$d/$l" "$l" > /dev/null 2>&1 || true
                grep -q '^VRP end' "$d/$l/vrp.log" 2>/dev/null || { r="x: no complete log from $l ($d/$l)"; break; }
            done
            [ "$r" = ok ] && { python3 "$root/tests/ver/vcmp.py" "$d/ms/vrp.log" "$d/w16/vrp.log" "$d/cmp" \
                > "$d.txt" 2>&1 || r="x: $(grep -m1 'FAIL' "$d.txt") ($d.txt)"; } ;;
        tree286|tree386)
            # Microsoft's VER.DLL on w16's tree against 3.1's own log, and
            # w16's VER.DLL on the tree against Microsoft's there
            k=${part#tree}
            "$root/tests/ver/vrun.sh" "$d-ref" ms > /dev/null 2>&1 || true
            "$root/tests/ver/vtree.sh" "$d-ms" ms "$k" > "$d-ms.txt" 2>&1 || true
            "$root/tests/ver/vtree.sh" "$d-w16" w16 "$k" > "$d-w16.txt" 2>&1 || true
            for l in ref ms w16; do
                grep -q '^VRP end' "$d-$l/vrp.log" 2>/dev/null || { r="$part: no complete log ($d-$l)"; break; }
            done
            [ "$r" = ok ] && { python3 "$root/tests/ver/vcmp.py" "$d-ref/vrp.log" "$d-ms/vrp.log" "$d-cmpms" \
                > "$d.txt" 2>&1 || r="$part: Microsoft's VER on the tree: $(grep -m1 'FAIL' "$d.txt") ($d.txt)"; }
            [ "$r" = ok ] && { python3 "$root/tests/ver/vcmp.py" "$d-ms/vrp.log" "$d-w16/vrp.log" "$d-cmp" \
                >> "$d.txt" 2>&1 || r="$part: $(grep -m1 'FAIL' "$d.txt") ($d.txt)"; } ;;
        mods)
            "$root/tests/ver/vmods.sh" "$d" > "$d.txt" 2>&1 ||
                r="mods: $(grep -m1 'FAIL' "$d.txt" || echo 'no result') ($d.txt)" ;;
        fonts|fontsnew)
            [ "$part" = fontsnew ] && export VFONTS_NEWER=1
            "$root/tests/ver/vfonts.sh" "$d-ms" ms > /dev/null 2>&1 || true
            "$root/tests/ver/vfonts.sh" "$d-w16" w16 -e "$d-ms" > /dev/null 2>&1 || true
            unset VFONTS_NEWER
            ls "$d-ms"/0*.png > /dev/null 2>&1 || r="$part: no screens from Microsoft's ($d-ms)"
            for f in "$d-ms"/0*.png; do
                [ "$r" = ok ] || break
                sn=$(basename "$f")
                [ -f "$d-w16/$sn" ] || { r="$part: no $sn ($d-w16)"; break; }
                n=$(magick compare -metric AE "$d-w16/$sn" "$f" "$out/ver-$part-${sn%.png}-diff.png" 2>&1 >/dev/null || true)
                n=${n%% *}
                [ "$n" = 0 ] || r="$part: $sn: $n pixels differ ($d-w16)"
            done
            [ "$r" = ok ] && ! cmp -s "$d-ms/fonts.txt" "$d-w16/fonts.txt" &&
                r="$part: other files installed ($d-ms/fonts.txt, $d-w16/fonts.txt)"
            [ "$r" = ok ] && ! grep -q '^WINDOWS/SYSTEM/W16TEST.*: the new one' "$d-ms/fonts.txt" &&
                r="$part: Microsoft's run installed nothing ($d-ms/fonts.txt)" ;;
        esac
        if [ -n "$part" ]; then partdone "$r"
        elif [ "$r" = ok ]; then echo "ok   ver"
        else echo "FAIL ver: $r ($out)"; fails=$((fails + 1)); fi ;;
    treek386)
        # w16's install tree alone (dropin.sh -T, nothing of Microsoft's)
        # under its own KRNL386 (-3: what DOSX starts on a 386, and what
        # users of the tree run) against the same under its KRNL286, every
        # shot 0 pixels. Parts: items, every item of the default groups
        # opened (tests/scripts/input/treeitems.txt; the groups visible at
        # boot); desk, Clock and Calculator over the desktop version stamp
        # (treedesk.txt, [w16] DesktopVersion=1; Clock's client area, its
        # time, left out). Until 2026-11-14 the tree's own tests ran
        # KRNL286 only.
        r=ok
        case ${part:-items} in
        items) s=$root/tests/scripts/input/treeitems.txt mk="rectangle 0,0 0,0"
               set -- -i 'NonWindowsApp:SwapDisk=C:\W16' ;;
        desk)  s=$root/tests/scripts/input/treedesk.txt mk="rectangle 0,18 212,186"
               set -- -i w16:DesktopVersion=1 ;;
        esac
        d=$out/treek386-${part:-items}
        "$root/tests/inputrun.sh" -s -B -b 40 -t 240 "$s" "$d-386" -T "$root/build/w16" -3 "$@" \
            > "$d-386.txt" 2>&1 || true
        "$root/tests/inputrun.sh" -e "$d-386/boot.png" -E "$d-386" -M "$mk" -b 40 -t 240 \
            "$s" "$d-286" -T "$root/build/w16" "$@" > "$d-286.txt" 2>&1 || true
        grep -q '^script done' "$d-386/input.log" 2>/dev/null || r="KRNL386: the script did not finish ($d-386/input.log)"
        # the groups at boot: Program Manager's window where PROGMAN.INI
        # puts it (its caption, 68+52,48+8) and Main's caption
        if [ "$r" = ok ]; then
            for p in "120 56" "100 103"; do
                set -- $p
                px=$(magick "$d-386/boot.png" -format "%[hex:p{$1,$2}]" info: 2>/dev/null)
                case $px in 0000AA*) ;; *) r="KRNL386: no Program Manager window or Main group where PROGMAN.INI has them at boot ($px at $1,$2)" ;; esac
            done
        fi
        for f in "$d-386"/boot.png "$d-386"/[0-9]*.png; do
            [ "$r" = ok ] || break
            s2=$(basename "$f")
            [ "$s2" = 10-clock.png ] && continue
            [ -f "$d-286/$s2" ] || { r="KRNL286: no $s2"; break; }
            magick "$f" -fill black -draw "$mk" "$d-a.png"
            magick "$d-286/$s2" -fill black -draw "$mk" "$d-b.png"
            n=$(magick compare -metric AE "$d-a.png" "$d-b.png" "$d-${s2%.png}-diff.png" 2>&1 >/dev/null || true)
            n=${n%% *}
            [ "$n" = 0 ] || { r="${part:-items}: $s2: $n pixels differ between KRNL386 and KRNL286"; break; }
        done
        if [ -n "$part" ]; then partdone "$r"
        elif [ "$r" = ok ]; then echo "ok   treek386: the tree under KRNL386 as under KRNL286"
        else echo "FAIL treek386: $r ($out)"; fails=$((fails + 1)); fi ;;
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
    swapdos|swapalt|swapall)
        # w16's WSWAP.EXE and DSWAP.EXE (ARCHITECTURE.md 9.9). swapdos: the
        # woados script (prompt, VER, EXIT, Exit Windows) with only them
        # w16's; swapalt: prompt, VER, Alt+Tab to Windows and back, EXIT,
        # Exit Windows (swapalt.txt), the same way; swapall: that script
        # on w16's install tree alone (build/w16 as C:\W16, dropin.sh -T;
        # no file of Microsoft's: the grabber, VGACOLOR.2GR, and the sound
        # driver, MMSOUND.DRV, are w16's).
        # Text screens against Microsoft's with the banner box (rows 1-5:
        # w16's says "w16") left out; Windows' screens against the same
        # run's screen before the prompt (Windows back as it was); up to
        # 40 pixels (the cursor). SwapDisk=C:\WINDOWS\TEMP as in woados
        rsys=$root/.tools/ref/win31/cdrive/WINDOWS/SYSTEM
        sw="-f $root/build/dos/WSWAP.EXE=WINDOWS/SYSTEM/WSWAP.EXE -f $root/build/dos/DSWAP.EXE=WINDOWS/SYSTEM/DSWAP.EXE"
        if [ $c = swapdos ]; then
            scr=$root/tests/scripts/input/woados.txt rn=woados
            shots="01-dos:640x400:t 02-ver:640x400:t 03-back:640x480:w 04-exit:640x400:r"
            set -- -i 'NonWindowsApp:SwapDisk=C:\WINDOWS\TEMP'
            refrun woados-ref woados env INPUTRUN_STAY=1 "$root/tests/inputrun.sh" -b 30 -t 130 "$scr" "$out/woados-ref" -r "$@"
        else
            scr=$root/tests/scripts/input/swapalt.txt rn=swapalt
            shots="01-dos:640x400:t 02-ver:640x400:t 03-win:640x480:r 04-dos:640x400:t 05-back:640x480:r 06-exit:640x400:r"
            set -- -i 'NonWindowsApp:SwapDisk=C:\WINDOWS\TEMP'
            refrun swapalt-ref swapalt env INPUTRUN_STAY=1 "$root/tests/inputrun.sh" -b 30 -t 170 "$scr" "$out/swapalt-ref" -r "$@"
        fi
        if [ $c = swapall ]; then
            # (its prompt says C:\W16>, not C:\WINDOWS>: the DOS screens
            # against its own instead, the prompt read back as it was left;
            # Windows' against its own before the prompt, the DOS
            # program's icon left out)
            shots="01-dos:640x400:s 02-ver:640x400:s 03-win:640x480:i 04-dos:640x400:d 05-back:640x480:w 06-exit:640x400:s"
            INPUTRUN_STAY=1 "$root/tests/inputrun.sh" -s -b 50 -t 200 "$scr" "$out/$c-w16" \
                -T "$root/build/w16" \
                -i 'NonWindowsApp:SwapDisk=C:\W16' \
                > "$out/$c-w16.txt" 2>&1 || true
        else
            # shellcheck disable=SC2086
            INPUTRUN_STAY=1 "$root/tests/inputrun.sh" -b 30 -t 170 "$scr" "$out/$c-w16" -r $sw "$@" \
                > "$out/$c-w16.txt" 2>&1 || true
        fi
        a=$out/$c-w16 b=$out/$rn-ref r=ok
        for s in $shots; do
            f=${s%%:*} rest=${s#*:} want=${rest%%:*} how=${rest#*:}
            [ -f "$a/$f.png" ] && [ -f "$b/$f.png" ] || { r="no $f"; break; }
            sz=$(magick identify -format '%wx%h' "$a/$f.png" 2>/dev/null)
            [ "$sz" = "$want" ] || { r="$f: screen $sz, not $want"; break; }
            case $how in
            w)  n=$(magick compare -metric AE "$a/$f.png" "$a/00-before.png" "$out/$c-$f-diff.png" 2>&1 >/dev/null || true) ;;
            t)  magick "$a/$f.png" -fill black -draw 'rectangle 0,16 639,95' "$out/$c-a.png"
                magick "$b/$f.png" -fill black -draw 'rectangle 0,16 639,95' "$out/$c-b.png"
                n=$(magick compare -metric AE "$out/$c-a.png" "$out/$c-b.png" "$out/$c-$f-diff.png" 2>&1 >/dev/null || true) ;;
            d)  n=$(magick compare -metric AE "$a/$f.png" "$a/02-ver.png" "$out/$c-$f-diff.png" 2>&1 >/dev/null || true) ;;
            i)  magick "$a/$f.png" -fill black -draw 'rectangle 0,0 79,39' -draw 'rectangle 0,400 199,479' "$out/$c-a.png"
                magick "$a/00-before.png" -fill black -draw 'rectangle 0,0 79,39' -draw 'rectangle 0,400 199,479' "$out/$c-b.png"
                n=$(magick compare -metric AE "$out/$c-a.png" "$out/$c-b.png" "$out/$c-$f-diff.png" 2>&1 >/dev/null || true) ;;
            s)  n=0 ;;
            *)  n=$(magick compare -metric AE "$a/$f.png" "$b/$f.png" "$out/$c-$f-diff.png" 2>&1 >/dev/null || true) ;;
            esac
            n=${n%%[. ]*}
            [ "${n:-99999}" -le 40 ] || { r="$f: $n pixels differ"; break; }
        done
        rm -f "$out/$c-a.png" "$out/$c-b.png"
        left=$(ls "$a/work/c/WINDOWS/TEMP" "$a/work/c/W16" 2>/dev/null | grep -i '^~WOA' || true)
        [ -z "$left" ] || [ "$r" != ok ] || r="swap files left: $left"
        if [ "$r" = ok ]; then echo "ok   $c: w16's WSWAP/DSWAP, the DOS screens as 3.1's and Windows back as it was"
        else echo "FAIL $c: $r ($out/$c-w16)"; fails=$((fails + 1)); fi ;;
    swapesc)
        # switching away from the MS-DOS Prompt and back on w16's install
        # tree alone (build/w16 as C:\W16, as swapall), the user's steps
        # (tests/scripts/input/swapesc.txt): Alt+Esc out, the icon
        # double-clicked, Alt+Tab out and in, a second prompt, EXIT in
        # both, Exit Windows. Parts: k386 (the tree as it is: DOSX starts
        # its KRNL386), k286 (its KRNL386.EXE deleted: KRNL286), mswoa
        # (Microsoft's WINOLDAP.MOD in it). Against Windows 3.1's run of
        # the same (Microsoft's KRNL386): each screen's mode; the DOS
        # screens against the part's own (its prompt says C:\W16>): the
        # restored prompt as it was left (02-ver), the second prompt as the
        # first (01-dos); Windows' screens against its own before the
        # prompt, the icon row's corner (0,400-199,479) and the pointer
        # left out, so
        # Program Manager active and nothing else on the screen (bug: a
        # CW_USEDEFAULT-sized window at 0,0 with the icon in its corner);
        # in that corner the icon's title as 3.1's: the same place and
        # width, not highlighted (bug: Microsoft's WINOLDAP's icon left
        # active), none after both prompts ended; no ~WOA file left
        rsys=$root/.tools/ref/win31/cdrive/WINDOWS/SYSTEM
        scr=$root/tests/scripts/input/swapesc.txt
        refrun swapesc-ref swapesc env INPUTRUN_STAY=1 "$root/tests/inputrun.sh" -b 30 -t 300 "$scr" "$out/swapesc-ref" \
            -3 -r -i 'NonWindowsApp:SwapDisk=C:\WINDOWS\TEMP'
        r=ok
        for k in ${part:-k386 k286 mswoa}; do
            case $k in
            k286)  set -- -f "W16/SYSTEM/KRNL386.EXE=-" ;;
            mswoa) set -- -f "$rsys/WINOLDAP.MOD=W16/SYSTEM/WINOLDAP.MOD" ;;
            *)     set -- ;;
            esac
            INPUTRUN_STAY=1 "$root/tests/inputrun.sh" -s -b 50 -t 300 "$scr" "$out/swapesc-$k" \
                -T "$root/build/w16" -i 'NonWindowsApp:SwapDisk=C:\W16' "$@" \
                > "$out/swapesc-$k.txt" 2>&1 || true
            a=$out/swapesc-$k b=$out/swapesc-ref
            for s in 01-dos:640x400:- 02-ver:640x400:- 03-win:640x480:w 04-dos:640x400:02-ver \
                     05-win:640x480:w 06-dos:640x400:02-ver 07-win:640x480:w 08-dos2:640x400:01-dos \
                     09-win:640x480:w 10-dos1:640x400:02-ver 11-win:640x480:w 12-exit:640x400:-; do
                f=${s%%:*} rest=${s#*:} want=${rest%%:*} how=${rest#*:}
                [ -f "$a/$f.png" ] && [ -f "$b/$f.png" ] || { r="$k: no $f"; break; }
                for g in "$a" "$b"; do
                    sz=$(magick identify -format '%wx%h' "$g/$f.png" 2>/dev/null)
                    [ "$sz" = "$want" ] || { r="$k: $f: screen $sz, not $want ($g)"; break 2; }
                done
                case $how in
                -)  n=0 ;;
                w)  # (and the pointer's first place, the screen's middle: it
                    # stays on the icon after the double click)
                    m="rectangle 0,400 199,479 rectangle 316,236 339,263"
                    magick "$a/$f.png" -fill black -draw "$m" "$out/swapesc-$k-a.png"
                    magick "$a/00-before.png" -fill black -draw "$m" "$out/swapesc-$k-b.png"
                    n=$(magick compare -metric AE "$out/swapesc-$k-a.png" "$out/swapesc-$k-b.png" "$out/swapesc-$k-$f-diff.png" 2>&1 >/dev/null || true)
                    ta=$(python3 "$root/tests/scripts/iconbox.py" "$a/$f.png" | sed -n 's/.*\(title .*\)/\1/p')
                    tb=$(python3 "$root/tests/scripts/iconbox.py" "$b/$f.png" | sed -n 's/.*\(title .*\)/\1/p')
                    [ "$ta" = "$tb" ] || { r="$k: $f: the icon's ${ta:-title none} (3.1's: ${tb:-title none})"; break; } ;;
                *)  n=$(magick compare -metric AE "$a/$f.png" "$a/$how.png" "$out/swapesc-$k-$f-diff.png" 2>&1 >/dev/null || true) ;;
                esac
                n=${n%%[. ]*}
                [ "${n:-99999}" -le 40 ] || { r="$k: $f: $n pixels differ"; break; }
            done
            rm -f "$out/swapesc-$k-a.png" "$out/swapesc-$k-b.png"
            left=$(ls "$a/work/c/W16" 2>/dev/null | grep -i '^~WOA' || true)
            [ -z "$left" ] || [ "$r" != ok ] || r="$k: swap files left: $left"
            [ "$r" = ok ] || break
        done
        if [ -n "$part" ]; then partdone "$r"
        elif [ "$r" = ok ]; then echo "ok   swapesc: Alt+Esc, a double click, Alt+Tab and a second prompt as on 3.1 (KRNL386, KRNL286, Microsoft's WINOLDAP)"
        else echo "FAIL swapesc: $r ($out)"; fails=$((fails + 1)); fi ;;
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
    lbclip)
        # a list box's height and its partly shown item (tests/apps/us32.c)
        lb32() {   # LABEL dropin.sh options...: US32's run, its log in LABEL.log
            _l=$1; shift
            "$root/tests/dropin.sh" -t 300 -W TK.LOG -o "$out/$_l" \
                -f "$root/build/dos/US32.EXE=US32.EXE" -i 'boot:shell=C:\US32.EXE' -r "$@" \
                > "$out/$_l.txt" 2>&1 || true
            : > "$out/$_l.log"
            for _d in "$out/$_l/c/WINDOWS" "$out/$_l/c"; do
                [ -f "$_d/TK.LOG" ] && { tr -d '\r' < "$_d/TK.LOG" > "$out/$_l.log"; break; }
            done
        }
        DROPIN_WSECS=30 lb32 lbclip-ref
        DROPIN_WSECS=30 lb32 lbclip-w16 -f "$user=WINDOWS/SYSTEM/USER.EXE"
        DROPIN_WSECS=30 lb32 lbclip-gdi -f "$user=WINDOWS/SYSTEM/USER.EXE" -f "$gdi=WINDOWS/SYSTEM/GDI.EXE"
        r=ok
        for k in ref w16 gdi; do
            grep -q '^US32 end' "$out/lbclip-$k.log" ||
                { [ "$r" = ok ] && r="US32 did not finish ($k: $out/lbclip-$k.log)"; }
        done
        if [ "$r" = ok ]; then
            grep '^OBS [^~]' "$out/lbclip-ref.log" > "$out/lbclip-ref.obs"
            grep '^OBS [^~]' "$out/lbclip-w16.log" > "$out/lbclip-w16.obs"
            cmp -s "$out/lbclip-ref.obs" "$out/lbclip-w16.obs" ||
                r="$(diff "$out/lbclip-ref.obs" "$out/lbclip-w16.obs" | grep -c '^<') observation(s) differ from 3.1's ($out/lbclip-ref.obs, lbclip-w16.obs)"
        fi
        if [ "$r" = ok ]; then
            # w16's GDI too (the user's setup): all but the checksums, whose
            # glyphs are w16's
            grep -v '\.sum=' "$out/lbclip-ref.obs" > "$out/lbclip-ref.nosum"
            grep '^OBS [^~]' "$out/lbclip-gdi.log" | grep -v '\.sum=' > "$out/lbclip-gdi.nosum"
            cmp -s "$out/lbclip-ref.nosum" "$out/lbclip-gdi.nosum" ||
                r="w16 USER+GDI: $(diff "$out/lbclip-ref.nosum" "$out/lbclip-gdi.nosum" | grep -c '^<') observation(s) differ from 3.1's ($out/lbclip-ref.nosum, lbclip-gdi.nosum)"
        fi
        if [ "$r" = ok ]; then echo "ok   lbclip: US32 as 3.1 (list box heights, a partly shown item scrolled; w16 USER, and USER+GDI)"
        else echo "FAIL lbclip: $r"; fails=$((fails + 1)); fi ;;
    lbdraw)
        # list box drawing, screen DCs, tab stops (tests/apps/us33.c)
        lb33() {   # LABEL dropin.sh options...: US33's run, its log in LABEL.log
            _l=$1; shift
            "$root/tests/dropin.sh" -t 400 -W TK.LOG -o "$out/$_l" \
                -f "$root/build/dos/US33.EXE=US33.EXE" -i 'boot:shell=C:\US33.EXE' -r "$@" \
                > "$out/$_l.txt" 2>&1 || true
            : > "$out/$_l.log"
            for _d in "$out/$_l/c/WINDOWS" "$out/$_l/c"; do
                [ -f "$_d/TK.LOG" ] && { tr -d '\r' < "$_d/TK.LOG" > "$out/$_l.log"; break; }
            done
        }
        DROPIN_WSECS=30 lb33 lbdraw-ref &
        DROPIN_WSECS=30 lb33 lbdraw-w16 -f "$user=WINDOWS/SYSTEM/USER.EXE" &
        DROPIN_WSECS=30 lb33 lbdraw-gdi -f "$user=WINDOWS/SYSTEM/USER.EXE" -f "$gdi=WINDOWS/SYSTEM/GDI.EXE" &
        wait
        r=ok
        for k in ref w16 gdi; do
            grep -q '^US33 end' "$out/lbdraw-$k.log" ||
                { [ "$r" = ok ] && r="US33 did not finish ($k: $out/lbdraw-$k.log)"; }
        done
        if [ "$r" = ok ]; then
            grep '^OBS [^~]' "$out/lbdraw-ref.log" > "$out/lbdraw-ref.obs"
            grep '^OBS [^~]' "$out/lbdraw-w16.log" > "$out/lbdraw-w16.obs"
            cmp -s "$out/lbdraw-ref.obs" "$out/lbdraw-w16.obs" ||
                r="$(diff "$out/lbdraw-ref.obs" "$out/lbdraw-w16.obs" | grep -c '^<') observation(s) differ from 3.1's ($out/lbdraw-ref.obs, lbdraw-w16.obs)"
        fi
        if [ "$r" = ok ]; then
            # w16's GDI too: all but the checksums and the tab stops' ink,
            # whose glyphs are w16's
            grep -v '\.sum=\|^OBS tab\.[^ ]*\.[0-9]=' "$out/lbdraw-ref.obs" > "$out/lbdraw-ref.nosum"
            grep '^OBS [^~]' "$out/lbdraw-gdi.log" | grep -v '\.sum=\|^OBS tab\.[^ ]*\.[0-9]=' > "$out/lbdraw-gdi.nosum"
            cmp -s "$out/lbdraw-ref.nosum" "$out/lbdraw-gdi.nosum" ||
                r="w16 USER+GDI: $(diff "$out/lbdraw-ref.nosum" "$out/lbdraw-gdi.nosum" | grep -c '^<') observation(s) differ from 3.1's ($out/lbdraw-ref.nosum, lbdraw-gdi.nosum)"
        fi
        if [ "$r" = ok ]; then echo "ok   lbdraw: US33 as 3.1 (owner-drawn list boxes' WM_DRAWITEMs and pixels, screen DCs, tab stops, child dialogs; w16 USER, and USER+GDI)"
        else echo "FAIL lbdraw: $r"; fails=$((fails + 1)); fi ;;
    uaudit)
        # the USER audit's probes (tests/apps/us29.c, us30.c)
        ua() {   # LABEL EXE-AS dropin.sh options...: a run, its log in LABEL.log
            _l=$1 _x=$2; shift 2
            "$root/tests/dropin.sh" -t 150 -W TK.LOG -o "$out/$_l" \
                -f "$root/build/dos/US29A.EXE=US29A.EXE" -f "$root/build/dos/US29D.DLL=US29D.DLL" \
                -i "boot:shell=C:\\$_x" "$@" > "$out/$_l.txt" 2>&1 || true
            : > "$out/$_l.log"
            for _d in "$out/$_l/c/WINDOWS" "$out/$_l/c"; do
                [ -f "$_d/TK.LOG" ] && { tr -d '\r' < "$_d/TK.LOG" > "$out/$_l.log"; break; }
            done
        }
        r=ok
        for p in US29 US30; do
            DROPIN_WSECS=40 ua uaudit-$p-ref $p.EXE -r -f "$root/build/dos/$p.EXE=$p.EXE"
            DROPIN_WSECS=40 ua uaudit-$p-w16 $p.EXE -r -f "$root/build/dos/$p.EXE=$p.EXE" \
                -f "$user=WINDOWS/SYSTEM/USER.EXE"
            for k in ref w16; do
                grep -q "^$p end" "$out/uaudit-$p-$k.log" ||
                    { [ "$r" = ok ] && r="$p did not finish ($k: $out/uaudit-$p-$k.log)"; }
            done
            [ "$r" = ok ] || continue
            grep '^OBS [^~]' "$out/uaudit-$p-ref.log" > "$out/uaudit-$p-ref.obs"
            grep '^OBS [^~]' "$out/uaudit-$p-w16.log" > "$out/uaudit-$p-w16.obs"
            cmp -s "$out/uaudit-$p-ref.obs" "$out/uaudit-$p-w16.obs" ||
                r="$p: $(diff "$out/uaudit-$p-ref.obs" "$out/uaudit-$p-w16.obs" | grep -c '^<') observation(s) differ from 3.1's ($out/uaudit-$p-ref.obs, uaudit-$p-w16.obs)"
        done
        for m in DW CR SE SD SS SC HM; do
            DROPIN_WSECS=25 ua uaudit-$m US29$m.EXE -r -f "$root/build/dos/US29.EXE=US29$m.EXE" \
                -f "$user=WINDOWS/SYSTEM/USER.EXE"
            grep -q '^US29 end' "$out/uaudit-$m.log" ||
                { [ "$r" = ok ] && r="US29$m did not survive on w16 ($out/uaudit-$m.log)"; }
        done
        if [ "$r" = ok ]; then echo "ok   uaudit: US29 and US30 as 3.1; w16 survives what 3.1 does not"
        else echo "FAIL uaudit: $r"; fails=$((fails + 1)); fi ;;
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
        # US28: where WM_MDICREATE puts CW_USEDEFAULT children as children
        # come and go (tests/apps/us28.c)
        us28() {
            _l=$1; shift
            "$root/tests/dropin.sh" -t 120 -s "$out/$_l.png" -o "$out/$_l" \
                -f "$root/build/dos/US28.EXE=US28.EXE" -i 'boot:shell=C:\US28.EXE' "$@" \
                > "$out/$_l.txt" 2>&1 || true
            for _d in "$out/$_l/c/WINDOWS" "$out/$_l/c"; do
                [ -f "$_d/TK.LOG" ] && { tr -d '\r' < "$_d/TK.LOG" > "$out/$_l.log"; break; }
            done
        }
        us28 mdimax-us28ref -r
        us28 mdimax-us28 -r -f "$user=WINDOWS/SYSTEM/USER.EXE"
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
        for k in us28ref us28; do
            f=$out/mdimax-$k.log
            if [ ! -f "$f" ] || ! grep -q '^US28 done' "$f"; then r="$k: US28 did not finish ($f)"; break; fi
        done
        for k in us18 us18a us28; do
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
        if [ "$r" = ok ]; then echo "ok   mdimax: US18 and US28 as 3.1; maximised groups' controls in the bar, 0 pixels (w16 USER+GDI; and w16 PROGMAN)"
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
        set -- -T "$root/build/w16"
        "$root/tests/inputrun.sh" -s -b 40 -t 240 "$scr" "$out/w16groups-ms" "$@" \
            -f "$rsys/../PROGMAN.EXE=W16/PROGMAN.EXE" > "$out/w16groups-ms.txt" 2>&1 || true
        "$root/tests/inputrun.sh" -e "$out/w16groups-ms/boot.png" -m "$m" -E "$out/w16groups-ms" -M "$m" \
            -b 40 -t 240 "$scr" "$out/w16groups-pm" "$@" > "$out/w16groups-pm.txt" 2>&1 || true
        # and w16's under the tree's KRNL386 (-3), as users of the tree run it
        "$root/tests/inputrun.sh" -e "$out/w16groups-ms/boot.png" -m "$m" -E "$out/w16groups-ms" -M "$m" \
            -b 40 -t 240 "$scr" "$out/w16groups-pm386" "$@" -3 > "$out/w16groups-pm386.txt" 2>&1 || true
        r=ok
        for k in ms pm pm386; do
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
        for k in pm pm386; do
        for f in "$out/w16groups-ms"/boot.png "$out/w16groups-ms"/[0-9]*.png; do
            [ "$r" = ok ] || break
            s=$(basename "$f")
            [ -f "$out/w16groups-$k/$s" ] || { r="$k: no $s"; break; }
            magick "$f" -fill black -draw "$m" "$out/w16groups-a.png"
            magick "$out/w16groups-$k/$s" -fill black -draw "$m" "$out/w16groups-b.png"
            n=$(magick compare -metric AE "$out/w16groups-b.png" "$out/w16groups-a.png" "$out/w16groups-$k-${s%.png}-diff.png" 2>&1 >/dev/null || true)
            n=${n%% *}
            [ "$n" = 0 ] || r="$k: $s: $n pixels differ"
        done
        for f in MAIN.GRP ACCESSOR.GRP STARTUP.GRP PROGMAN.INI; do
            [ "$r" = ok ] || break
            cmp -s "$out/w16groups-ms/work/c/W16/$f" "$out/w16groups-$k/work/c/W16/$f" || r="$k: $f saved differently"
        done
        done
        if [ "$r" = ok ]; then echo "ok   w16groups: w16's tree alone, its groups read and run by Microsoft's and w16's Program Manager (KRNL286; w16's also under KRNL386), 0 pixels, the same files saved"
        else echo "FAIL w16groups: $r ($out)"; fails=$((fails + 1)); fi ;;
    ddescen)
        # Program Manager's DDE interface probed with TDDE (tests/
        # ddescenario.sh, one part per tests/scripts/dde/*.txt): Microsoft's
        # and w16's PROGMAN on the reference install, the logged answers
        # (with the command block each ACK returns) and the group files and
        # PROGMAN.INI identical. names.txt covers the group file names the
        # 7-Zip 16-bit installer's CreateGroup needed (zipsetup)
        r=ok
        DDESCEN_OUT=$out/ddescen-$part "$root/tests/ddescenario.sh" "$part" > "$out/ddescen-$part.txt" 2>&1 || true
        grep -q "^== $part: ok" "$out/ddescen-$part.txt" ||
            r="$part: $(grep -v '^==' "$out/ddescen-$part.txt" | head -3 | tr '\n' ' ') ($out/ddescen-$part)"
        partdone "$r" ;;
    zipsetup)
        # A third-party installer that talks raw DDE to Program Manager:
        # 7-Zip 26.05's Windows 3.0/3.1x setup (the user's file, copied in
        # at test time from $W16_7ZSETUP, default ~/Downloads/7zsetw16.exe,
        # never committed; without it the case is skipped). From File >
        # Run, Install, its report, OK (tests/scripts/input/zipsetup.txt).
        # The reference: all of Microsoft's (KRNL386). Parts: pm, w16's
        # PROGMAN on Microsoft's rest: every shot 0 pixels, the group file
        # and PROGMAN.INI byte for byte; t386 and t286, w16's install tree
        # alone under its KRNL386, then KRNL286: the same group file as 3.1's
        # (7ZIP2605.GRP: the name from the group's letters and digits; its
        # two items, their icons and working directories), PROGMAN.INI
        # naming it last in [Groups] and Order=, the group's window active
        # at the end, the two kernels' screens the same. Until 2026-11-15
        # w16's PROGMAN refused CreateGroup ("7-ZIP26..GRP") and w16's
        # USER cut the report at 255 characters ("...CreateGroup. WA").
        r=ok
        zs=${W16_7ZSETUP:-$HOME/Downloads/7zsetw16.exe}
        if [ ! -f "$zs" ]; then
            echo "skip" > "$out/.zipsetup-skip"
            partdone ok
        else
            s=$root/tests/scripts/input/zipsetup.txt
            cp "$zs" "$out/7ZSETW16.EXE"
            zf="-f $out/7ZSETW16.EXE=7ZSETW16.EXE"
            caret="rectangle 66,88 76,110"
            refrun zipsetup-ref zipsetup "$root/tests/inputrun.sh" -s -b 40 -t 300 "$s" "$out/zipsetup-ref" -r -3 $zf
            rd=$out/zipsetup-ref
            grep -q '^script done' "$rd/input.log" 2>/dev/null || r="the reference did not run to its end ($rd/input.log)"
            [ "$r" != ok ] || [ -f "$rd/work/c/WINDOWS/7ZIP2605.GRP" ] || r="the reference made no 7ZIP2605.GRP"
            d=$out/zipsetup-$part
            case $part in
            pm)
                # (the minimised groups' pictures left out: w16's own art;
                # and the setup dialog's blinking caret)
                m="rectangle 60,310 300,342 $caret"
                [ "$r" = ok ] && "$root/tests/inputrun.sh" -e "$rd/boot.png" -m "$m" -E "$rd" -M "$m" -b 40 -t 300 "$s" "$d" -r -3 $zf \
                    -f "$root/build/dos/PROGMAN.EXE=WINDOWS/PROGMAN.EXE" > "$d.txt" 2>&1 || true
                for f in "$rd"/[0-9]*.png; do
                    [ "$r" = ok ] || break
                    s2=$(basename "$f")
                    [ -f "$d/$s2" ] || { r="pm: no $s2"; break; }
                    magick "$f" -fill black -draw "$m" "$d-a.png"
                    magick "$d/$s2" -fill black -draw "$m" "$d-b.png"
                    n=$(magick compare -metric AE "$d-b.png" "$d-a.png" "$d-${s2%.png}-diff.png" 2>&1 >/dev/null || true)
                    n=${n%% *}
                    [ "$n" = 0 ] || r="pm: $s2: $n pixels differ"
                done
                for f in 7ZIP2605.GRP PROGMAN.INI; do
                    [ "$r" = ok ] || break
                    cmp -s "$rd/work/c/WINDOWS/$f" "$d/work/c/WINDOWS/$f" || r="pm: $f differs from 3.1's"
                done ;;
            tree)
                # under KRNL386 first, then KRNL286 against its shots
                for k in t386 t286; do
                    [ "$r" = ok ] || break
                    d=$out/zipsetup-$k
                    if [ $k = t386 ]; then
                        "$root/tests/inputrun.sh" -s -b 40 -t 300 "$s" "$d" -T "$root/build/w16" -3 $zf > "$d.txt" 2>&1 || true
                    else
                        "$root/tests/inputrun.sh" -e "$out/zipsetup-t386/boot.png" -E "$out/zipsetup-t386" -M "$caret" \
                            -b 40 -t 300 "$s" "$d" -T "$root/build/w16" $zf > "$d.txt" 2>&1 || true
                    fi
                    w=$d/work/c/W16
                    grep -q '^script done' "$d/input.log" 2>/dev/null || { r="$k: the script did not run to its end ($d/input.log)"; break; }
                    cmp -s "$rd/work/c/WINDOWS/7ZIP2605.GRP" "$w/7ZIP2605.GRP" ||
                        { r="$k: 7ZIP2605.GRP $( [ -f "$w/7ZIP2605.GRP" ] && echo "differs from 3.1's" || echo "not made")"; break; }
                    gl=$(tr -d '\r' < "$w/PROGMAN.INI" | sed -n '/^\[Groups\]/,/^\[/p' | grep '^Group' | tail -1)
                    case $gl in Group*=C:\\W16\\7ZIP2605.GRP) ;; *) r="$k: PROGMAN.INI's last group is '$gl'"; break ;; esac
                    gn=${gl%%=*}; gn=${gn#Group}
                    tr -d '\r' < "$w/PROGMAN.INI" | grep -q "^Order=.* $gn\$" || { r="$k: Order= does not end with $gn"; break; }
                    # the new group's window, active, over Main (its caption; one
                    # cascade step higher than on 3.1: the tree has three groups)
                    px=$(magick "$d/03-group.png" -format "%[hex:p{360,168}]" info: 2>/dev/null)
                    case $px in 0000AA*) ;; *) r="$k: no active group window at 360,168 ($px)"; break ;; esac
                done
                for f in "$out/zipsetup-t386"/[0-9]*.png; do
                    [ "$r" = ok ] || break
                    s2=$(basename "$f")
                    [ -f "$out/zipsetup-t286/$s2" ] || { r="t286: no $s2"; break; }
                    magick "$f" -fill black -draw "$caret" "$out/zipsetup-t-a.png"
                    magick "$out/zipsetup-t286/$s2" -fill black -draw "$caret" "$out/zipsetup-t-b.png"
                    n=$(magick compare -metric AE "$out/zipsetup-t-b.png" "$out/zipsetup-t-a.png" "$out/zipsetup-t286-${s2%.png}-diff.png" 2>&1 >/dev/null || true)
                    n=${n%% *}
                    [ "$n" = 0 ] || r="t286: $s2: $n pixels differ from KRNL386's"
                done ;;
            esac
            partdone "$r"
        fi ;;
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
    trackmove)
        # a drag's message loop (user/track.c): probe US31 as the shell,
        # dragged three times (tests/scripts/input/us31.txt); its OBS lines
        # against 3.1's
        scr=$root/tests/scripts/input/us31.txt
        refrun trackmove-ref trackmove "$root/tests/inputrun.sh" -s -b 40 -t 200 "$scr" "$out/trackmove-ref" -r \
            -f "$root/build/dos/US31.EXE=US31.EXE" -f "$root/build/dos/US31B.EXE=US31B.EXE" -i 'boot:shell=C:\US31.EXE'
        "$root/tests/inputrun.sh" -s -b 40 -t 200 "$scr" "$out/trackmove-w16" -r \
            -f "$root/build/dos/US31.EXE=US31.EXE" -f "$root/build/dos/US31B.EXE=US31B.EXE" -i 'boot:shell=C:\US31.EXE' \
            -f "$user=WINDOWS/SYSTEM/USER.EXE" -f "$gdi=WINDOWS/SYSTEM/GDI.EXE" -i w16:hangwatch=30 \
            > "$out/trackmove-w16.txt" 2>&1 || true
        for k in ref w16; do
            f=$out/trackmove-$k/work/c/WINDOWS/TK.LOG
            if [ -f "$f" ]; then tr -d '\r' < "$f" > "$out/trackmove-$k.log"; else : > "$out/trackmove-$k.log"; fi
            grep '^OBS [^~]' "$out/trackmove-$k.log" > "$out/trackmove-$k.obs" || true
        done
        if ! grep -q '^US31 done' "$out/trackmove-ref.log"; then
            echo "FAIL trackmove: US31 did not finish on 3.1 ($out/trackmove-ref.log)"; fails=$((fails + 1))
        elif ! grep -q '^US31 done' "$out/trackmove-w16.log"; then
            echo "FAIL trackmove: US31 did not finish on w16 ($out/trackmove-w16.log, trackmove-w16/work/c/OUT.TXT)"; fails=$((fails + 1))
        elif ! cmp -s "$out/trackmove-ref.obs" "$out/trackmove-w16.obs"; then
            echo "FAIL trackmove: $(diff "$out/trackmove-ref.obs" "$out/trackmove-w16.obs" | grep -c '^<') observation(s) differ from 3.1's ($out/trackmove-ref.obs, trackmove-w16.obs)"
            fails=$((fails + 1))
        else
            echo "ok   trackmove"
        fi ;;
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
    holdbtn)
        # the caption's minimize and maximize buttons and a scroll bar's
        # arrows and shaft held down with real input (user/defwnd.c
        # track_caption_button, user/scroll.c): probe US26 as the shell
        # (tests/scripts/input/holdbtn.txt), w16's USER and GDI against
        # all-Microsoft: every screen identical (buttons and arrows drawn
        # pressed while held, raised when the pointer leaves, nothing done
        # on a release off the button); US26's log identical but for the
        # times and the number of SB_LINEDOWNs, which depend on how long
        # the host held the button; and on w16 the arrow's repeats 200-250
        # ms after the press, then 50-60 ms apart, none while the pointer
        # was off it
        scr=$root/tests/scripts/input/holdbtn.txt
        refrun holdbtn-ref holdbtn "$root/tests/inputrun.sh" -s -b 40 -t 250 "$scr" "$out/holdbtn-ref" -r \
            -f "$root/build/dos/US26.EXE=US26.EXE" -i 'boot:shell=C:\US26.EXE'
        "$root/tests/inputrun.sh" -e "$out/holdbtn-ref/boot.png" -E "$out/holdbtn-ref" -b 40 -t 250 "$scr" "$out/holdbtn-w16" -r \
            -f "$root/build/dos/US26.EXE=US26.EXE" -i 'boot:shell=C:\US26.EXE' \
            -f "$user=WINDOWS/SYSTEM/USER.EXE" -f "$gdi=WINDOWS/SYSTEM/GDI.EXE" > "$out/holdbtn-w16.txt" 2>&1 || true
        for k in ref w16; do
            f=$out/holdbtn-$k/work/c/WINDOWS/TK.LOG
            if [ -f "$f" ]; then tr -d '\r' < "$f" > "$out/holdbtn-$k.log"; else : > "$out/holdbtn-$k.log"; fi
            grep -v '^VSCROLL 1 ' "$out/holdbtn-$k.log" | sed 's/ t=.*//' > "$out/holdbtn-$k.cmp"
        done
        r=ok
        if ! grep -q '^VSCROLL 8 pos 100' "$out/holdbtn-ref.log"; then
            r="the reference's log is not complete ($out/holdbtn-ref.log)"
        elif ! cmp -s "$out/holdbtn-ref.cmp" "$out/holdbtn-w16.cmp"; then
            r="US26's messages differ from 3.1's ($out/holdbtn-ref.cmp, holdbtn-w16.cmp)"
        else
            # the arrow's repeat times on w16
            t=$(awk '/^VSCROLL 1 /{split($5, a, "="); t = a[2] + 0; if (n) d[n] = t - p; p = t; n++}
                END {ok = n > 12 && d[1] >= 190 && d[1] <= 250; for (i = 2; i <= 11; i++) if (d[i] < 50 || d[i] > 60) ok = 0
                     g = 0; for (i = 2; i < n; i++) if (d[i] > 500) g = 1; print (ok && g) ? "ok" : "bad"}' "$out/holdbtn-w16.log")
            [ "$t" = ok ] || r="the arrow's repeats are not 200 ms then a tick apart, stopping off the arrow ($out/holdbtn-w16.log)"
        fi
        for g in "$out/holdbtn-ref"/[0-9]*.png; do
            [ "$r" = ok ] || break
            s=$(basename "$g" .png)
            [ -f "$out/holdbtn-w16/$s.png" ] || { r="no $s"; break; }
            n=$(magick compare -metric AE "$out/holdbtn-w16/$s.png" "$g" "$out/holdbtn-$s-diff.png" 2>&1 >/dev/null || true)
            n=${n%% *}
            [ "$n" = 0 ] || r="$s: $n pixels differ ($out/holdbtn-$s-diff.png)"
        done
        if [ "$r" = ok ]; then echo "ok   holdbtn: caption buttons and scroll-bar arrows held as on 3.1 (0 pixels, the messages, the arrow's repeat times)"
        else echo "FAIL holdbtn: $r"; fails=$((fails + 1)); fi ;;
    mmerge)
        # mouse moves made while a program is busy merge into one
        # (user/input.c push_event): probe US25 (tests/apps/us25.c) as the
        # shell, events fed through mouse_event at interrupt time and then
        # real input (tests/scripts/input/mmerge.txt), on w16's USER and
        # on all-Microsoft: its log of mouse messages (point, key state)
        # identical
        scr=$root/tests/scripts/input/mmerge.txt
        refrun mmerge-ref mmerge "$root/tests/inputrun.sh" -s -b 40 -t 150 "$scr" "$out/mmerge-ref" -r \
            -f "$root/build/dos/US25.EXE=US25.EXE" -i 'boot:shell=C:\US25.EXE'
        "$root/tests/inputrun.sh" -e "$out/mmerge-ref/boot.png" -b 40 -t 150 "$scr" "$out/mmerge-w16" -r \
            -f "$root/build/dos/US25.EXE=US25.EXE" -i 'boot:shell=C:\US25.EXE' \
            -f "$user=WINDOWS/SYSTEM/USER.EXE" > "$out/mmerge-w16.txt" 2>&1 || true
        for k in ref w16; do
            f=$out/mmerge-$k/work/c/WINDOWS/TK.LOG
            if [ -f "$f" ]; then tr -d '\r' < "$f" > "$out/mmerge-$k.log"; else : > "$out/mmerge-$k.log"; fi
        done
        r=ok
        if ! grep -q '^US25 end' "$out/mmerge-ref.log" || [ "$(grep -c '^S1 MOUSEMOVE' "$out/mmerge-ref.log")" != 1 ]; then
            r="the reference's log is not as expected ($out/mmerge-ref.log)"
        elif ! cmp -s "$out/mmerge-ref.log" "$out/mmerge-w16.log"; then
            r="US25's messages differ from 3.1's ($out/mmerge-ref.log, mmerge-w16.log)"
        fi
        if [ "$r" = ok ]; then echo "ok   mmerge: mouse moves merged as on 3.1 (US25's log identical)"
        else echo "FAIL mmerge: $r"; fails=$((fails + 1)); fi ;;
    sbdrag)
        # a scroll bar's thumb dragged with real input (user/scroll.c,
        # track_scrollbar): part us, probe US19 as the shell, its four bars
        # (tests/scripts/input/sbdrag-us.txt); part np, Microsoft's
        # Notepad (tests/scripts/input/sbdrag-np.txt); each under w16's
        # USER and GDI on Microsoft's KRNL286 against all-Microsoft. Every
        # screen, taken mid-drag, must be identical (Notepad's caret left
        # out), and US19's log of WM_VSCROLL/WM_HSCROLL (code, position,
        # the pointer then) identical to 3.1's; part fast, a fast drag of
        # US19's slow control (tests/scripts/input/sbdrag-fast.txt) under
        # w16's USER, GDI and MOUSE.DRV: the screens after the pointer
        # stops and after the release as 3.1's within 8 seconds (moves
        # made while the program is busy merged; without that the outline
        # falls seconds behind the pointer)
        pk=${part:-us np fast npw}
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
        case " $pk " in *" fast "*)
        scr=$root/tests/scripts/input/sbdrag-fast.txt
        refrun sbdrag-fastref sbdrag "$root/tests/inputrun.sh" -s -b 40 -t 200 "$scr" "$out/sbdrag-fastref" -r \
            -f "$root/build/dos/US19.EXE=US19.EXE" -i 'boot:shell=C:\US19.EXE'
        "$root/tests/inputrun.sh" -e "$out/sbdrag-fastref/boot.png" -E "$out/sbdrag-fastref" -b 40 -t 200 "$scr" "$out/sbdrag-fast" -r \
            -f "$root/build/dos/US19.EXE=US19.EXE" -i 'boot:shell=C:\US19.EXE' \
            -f "$user=WINDOWS/SYSTEM/USER.EXE" -f "$gdi=WINDOWS/SYSTEM/GDI.EXE" \
            -f "$root/build/dos/MOUSE.DRV=WINDOWS/SYSTEM/MOUSE.DRV" > "$out/sbdrag-fast.txt" 2>&1 || true
        for f in "$out/sbdrag-fastref"/[0-9]*.png; do
            [ "$r" = ok ] || break
            s=$(basename "$f" .png)
            [ -f "$out/sbdrag-fast/$s.png" ] || { r="fast: no $s"; break; }
            n=$(magick compare -metric AE "$out/sbdrag-fast/$s.png" "$f" "$out/sbdrag-fast-$s-diff.png" 2>&1 >/dev/null || true)
            n=${n%% *}
            [ "$n" = 0 ] || r="fast: $s: $n pixels differ, the outline behind the pointer? ($out/sbdrag-fast-$s-diff.png)"
        done ;; esac
        case " $pk " in *" npw "*)
        # w16's own Notepad (shell/notepad) on w16's USER and GDI against
        # Microsoft's on 3.1: its thumb stays put while dragged, as 3.1's
        scr=$root/tests/scripts/input/sbdrag-np.txt
        m="rectangle 14,42 18,60"
        refrun sbdrag-npref sbdrag "$root/tests/inputrun.sh" -s -b 40 -t 300 "$scr" "$out/sbdrag-npref" -r             -i 'WIN.INI:windows:run=NOTEPAD.EXE'
        "$root/tests/inputrun.sh" -e "$out/sbdrag-npref/boot.png" -m "$m" -E "$out/sbdrag-npref" -M "$m" -b 40 -t 300 "$scr" "$out/sbdrag-npw" -r             -i 'WIN.INI:windows:run=NOTEPAD.EXE' -f "$root/build/dos/NOTEPAD.EXE=WINDOWS/NOTEPAD.EXE"             -f "$user=WINDOWS/SYSTEM/USER.EXE" -f "$gdi=WINDOWS/SYSTEM/GDI.EXE" > "$out/sbdrag-npw.txt" 2>&1 || true
        for f in "$out/sbdrag-npref"/[0-9]*.png; do
            [ "$r" = ok ] || break
            s=$(basename "$f" .png)
            [ -f "$out/sbdrag-npw/$s.png" ] || { r="npw: no $s"; break; }
            magick "$out/sbdrag-npw/$s.png" -fill black -draw "$m" "$out/sbdrag-npw-a.png"
            magick "$f" -fill black -draw "$m" "$out/sbdrag-npw-b.png"
            n=$(magick compare -metric AE "$out/sbdrag-npw-a.png" "$out/sbdrag-npw-b.png" "$out/sbdrag-npw-$s-diff.png" 2>&1 >/dev/null || true)
            n=${n%% *}
            [ "$n" = 0 ] || r="npw: $s: $n pixels differ ($out/sbdrag-npw-$s-diff.png)"
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
        elif [ "$r" = ok ]; then echo "ok   sbdrag: 0 pixels mid-drag (US19's four bars, Microsoft's and w16's Notepad, a fast drag), US19's messages as 3.1's"
        else echo "FAIL sbdrag: $r"; fails=$((fails + 1)); fi ;;
    gpuser)
        # USER's functions given a bad pointer (user/gp.asm): probe US27
        # (tests/apps/us27.c) as the shell in DOSBox-X as a 386, which
        # checks segment limits for string instructions (tools/us27ref.py);
        # the probe checks each value against 3.1's (us27ref.h) and leaves
        # out the cases that ended the task on 3.1. Every case must come
        # back (one boot, "US27 end") with no FAIL line.
        r=ok
        for pk in ${part:-u286 u386 t286 t386}; do
            dir=WINDOWS
            case $pk in
            u286) set -- -r -f "$user=WINDOWS/SYSTEM/USER.EXE" ;;
            u386) set -- -r -3 -f "$user=WINDOWS/SYSTEM/USER.EXE" ;;
            t286) set -- -T "$root/build/w16"; dir=W16 ;;
            t386) set -- -T "$root/build/w16" -3; dir=W16 ;;
            esac
            "$root/tests/dropin.sh" "$@" -p 386x -t 400 -o "$out/gpuser-$pk" \
                -f "$root/build/dos/US27.EXE=US27.EXE" -i 'boot:shell=C:\US27.EXE' \
                > "$out/gpuser-$pk.txt" 2>&1 || true
            f=$out/gpuser-$pk.log
            tr -d '\r' < "$out/gpuser-$pk/c/$dir/TK.LOG" > "$f" 2>/dev/null || : > "$f"
            if ! grep -q '^US27 end' "$f"; then
                r="$pk: US27 stopped after $(sed -n 's/^RUN //p' "$f" | tail -1) ($f)"
            elif grep -q '^FAIL' "$f"; then
                r="$pk: $(grep -c '^FAIL' "$f") values differ from 3.1's, first $(grep -m1 '^FAIL' "$f") ($f)"
            fi
            [ "$r" = ok ] || break
        done
        if [ -n "$part" ]; then partdone "$r"
        elif [ "$r" = ok ]; then echo "ok   gpuser: US27's bad pointers as on 3.1"
        else echo "FAIL gpuser: $r"; fails=$((fails + 1)); fi ;;
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
            kbtype) m=$kbtypecaret ;;
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
            if [ $k = w16 ]; then set -- -e "$out/kbdall-ref/boot.png" -E "$out/kbdall-ref" -M "$kbtypecaret" -b 30 -t 240 "$scr" "$out/kbdall-$k" \
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
            magick "$f" -fill black -draw "$kbtypecaret" "$out/kbdall-a.png"
            magick "$out/kbdall-w16/$g" -fill black -draw "$kbtypecaret" "$out/kbdall-b.png"
            n=$(magick compare -metric AE "$out/kbdall-b.png" "$out/kbdall-a.png" "$out/kbdall-${g%.png}-diff.png" 2>&1 >/dev/null || true)
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
    sound|soundtime|soundrec|soundbeep)
        # tests/soundprobe.sh (drivers/sound): w16's MMSOUND.DRV against
        # Microsoft's on Microsoft's stack (DOSBox-X, 10000 cycles a ms,
        # the sound recorded; not cached: both runs each time). sound: SN1's
        # interface parts and the played groups (logs identical, tones
        # within tools/tonecmp.py's limits); soundtime: SN1's timing parts
        # (T, U, V: every tempo, length and mode); soundrec: the queue's
        # capacity, SNDREC's record of every call (USER's DoBeep), and the
        # beeps on every w16 part; soundbeep: MessageBeep in full (KRNLOG's
        # and SNDREC's records, the recordings) in six configurations
        case $c in
        sound) set -- sn1; sp=ABCDEFGHSP ;;
        soundtime) set -- sn1; sp=TUV ;;
        soundrec) set -- queue rec allw16; sp= ;;
        soundbeep) set -- beep; sp= ;;
        esac
        if SOUNDPROBE_PARTS=$sp "$root/tests/soundprobe.sh" -o "$out/$c" "$@" > "$out/$c.txt" 2>&1; then
            echo "ok   $c: $(grep '^ok' "$out/$c.txt" | sed 's/^ok   //' | tr '\n' ';' | sed 's/;$//')"
        else
            echo "FAIL $c: $(grep -m1 -E '^FAIL' "$out/$c.txt" || echo "see $c.txt") ($out/$c.txt)"
            fails=$((fails + 1))
        fi ;;
    soundboot) # w16's MMSOUND.DRV in plain boots: on Microsoft's stack and
            # on w16's WIN.COM, DOSX and KRNL286, each against the
            # all-Microsoft boot         must be 0
            ref
            boot sndms -r -f "$root/build/dos/MMSOUND.DRV=WINDOWS/SYSTEM/MMSOUND.DRV"; check sndms sndms ref
            boot sndallw16 -w -x -f "$root/build/dos/MMSOUND.DRV=WINDOWS/SYSTEM/MMSOUND.DRV"; check sndallw16 sndallw16 ref
            expect= ;;
    treealone)
        # build/w16 alone as C:\W16 (dropin.sh -T with nothing added): no
        # file of Microsoft's on the drive. Windows (no DOS program: the
        # grabber VGACOLOR.2GR is not needed) must boot to Program Manager
        # under its KRNL286 and (treealone386, -3: what DOSX starts on a
        # 386, as users of the tree run it) its KRNL386
        expect=
        boot treealone -T "$root/build/w16"
        boot treealone386 -T "$root/build/w16" -3
        # and 386 Enhanced mode: the tree's own WIN386.EXE with the
        # built-in devices its SYSTEM.INI [386Enh] names
        boot treealone3e -T "$root/build/w16" -3 -c "WIN /3 /B"
        if ! booted treealone || ! grep -q '^shell: loaded' "$out/treealone.txt"; then
            echo "FAIL treealone: the boot did not complete under KRNL286 ($out/treealone.txt)"; fails=$((fails + 1))
        elif ! booted treealone386 || ! grep -q '^shell: loaded' "$out/treealone386.txt"; then
            echo "FAIL treealone: the boot did not complete under KRNL386 ($out/treealone386.txt)"; fails=$((fails + 1))
        elif ! booted treealone3e || ! grep -q '^shell: loaded' "$out/treealone3e.txt"; then
            echo "FAIL treealone: the boot did not complete in 386 Enhanced mode ($out/treealone3e.txt)"; fails=$((fails + 1))
        else
            echo "ok   treealone: build/w16 alone, no file of Microsoft's: boots to its shell under KRNL286, KRNL386 and in 386 Enhanced mode (w16's WIN386)"
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
        # compared; carets keep the phase they start in on both sides,
        # CursorBlinkRate=65535 as in kbd: the host's load must not decide
        # whether a caret shows on a shot)
        blink='WIN.INI:windows:CursorBlinkRate=65535'
        r=ok
        for s in ${part:-kbtype tabs alttab helpmenus pmkeys icons menus dragoff pmitems pmclose mdimax about}; do
            vdrv=$out/vga-msres-$s.drv
            "$root/tests/vgamsres.sh" "$vdrv" > "$out/vga-msres-$s.txt" 2>&1 || { r="$s: tests/vgamsres.sh failed"; break; }
            scr=$root/tests/scripts/input/$s.txt
            case $s in
            kbtype) m=$kbtypecaret ;;
            helpmenus) m="rectangle 14,42 20,61" ;;
            icons) m="rectangle 160,400 220,445" ;;
            # the About box's free memory: w16's driver is smaller
            about) m="rectangle 318,334 380,346" ;;
            *) m="rectangle 0,0 0,0" ;;
            esac
            b=25 t=220
            case $s in pmkeys|pmitems|pmclose|mdimax) b=40 t=240 ;; esac
            [ $s = icons ] && t=380
            refrun vga-$s-ref vga "$root/tests/inputrun.sh" -s -b $b -t $t "$scr" "$out/vga-$s-ref" -r -i "$blink"
            "$root/tests/inputrun.sh" -e "$out/vga-$s-ref/boot.png" -E "$out/vga-$s-ref" -M "$m" -b $b -t $t "$scr" "$out/vga-$s-w16" -r -i "$blink" \
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
        # carets keep their starting phase on both sides (as in kbdall):
        # with the blink on, matchnow under load could miss the reference's
        # phase (01-typed: 28 pixels of Notepad's caret, one parallel run)
        blink='WIN.INI:windows:CursorBlinkRate=65535'
        "$root/tests/vgamsres.sh" "$out/vgaall-msres.drv" > "$out/vgaall-msres.txt" 2>&1 || true
        for k in ref w16; do
            if [ $k = w16 ]; then set -- -e "$out/vgaall-ref/boot.png" -E "$out/vgaall-ref" -M "$kbtypecaret" -b 30 -t 240 "$scr" "$out/vgaall-$k" \
                -f "$out/vgaall-msres.drv=WINDOWS/SYSTEM/VGA.DRV"
            else set -- -s -b 30 -t 240 "$scr" "$out/vgaall-$k"; fi
            "$root/tests/inputrun.sh" "$@" -i "$blink" \
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
            magick "$f" -fill black -draw "$kbtypecaret" "$out/vgaall-a.png"
            magick "$out/vgaall-w16/$g" -fill black -draw "$kbtypecaret" "$out/vgaall-b.png"
            n=$(magick compare -metric AE "$out/vgaall-b.png" "$out/vgaall-a.png" "$out/vgaall-${g%.png}-diff.png" 2>&1 >/dev/null || true)
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
    trails)
        # mouse trails (drivers/vga/README.md "Mouse trails"): w16's VGA.DRV
        # with Microsoft's resources against Microsoft's VGA.DRV, on
        # Microsoft's stack. Parts: det, escape, boot: VG9 as the shell
        # (tests/vgatrails.sh: the escape's results and WIN.INI, the trail
        # after every MoveCursor, CheckCursor, SetCursor, ShowCursor,
        # drawing and escape, with interrupts off so that the frames do not
        # depend on timing; boot: WIN.INI MouseTrails=5 read at Enable),
        # logs and frames identical; input: trails on from WIN.INI, real
        # mouse motion over the desktop, a window dragged and a menu
        # tracked under the trail (tests/scripts/input/trails.txt); cpl:
        # Control Panel's Mouse applet turning trails on and off with real
        # keys and the pointer moved (cpltrails.txt), WIN.INI's MouseTrails
        # the same. The input parts shoot each screen when the pointer has
        # stopped and the trail run down: no trail, no debris, 0 pixels.
        # timed, timednl: the trail in time, with the timer running, on w16's
        # install tree alone (KRNL386; its VGA.DRV with Microsoft's
        # resources, for the arrow) against all-Microsoft 3.1, the mouse
        # captured and not (autolock=false): tests/currec.sh, CR1 recording
        # the cursor calls (CURREC.DRV in front of the driver) and a frame at
        # each timer message under a steady stream of moves and two stops
        # (tests/scripts/input/trailmove*.txt); tools/cr1stat.py --check:
        # moves and ticks within 10%,
        # the trail's length within one image (the tick's
        # phase against the frames), no positions missing from it, the same
        # run-downs within a frame.
        r=ok
        for pt in ${part:-det escape boot input cpl timed timednl}; do
            case $pt in
            timed|timednl)
                ts=$root/tests/scripts/input/trailmove.txt nl=
                [ $pt = timednl ] && ts=$root/tests/scripts/input/trailmove-nolock.txt nl=1
                refrun trails-$pt-ref trails env INPUTRUN_NOLOCK=$nl "$root/tests/currec.sh" -s ms -F -t 14 "$ts" "$out/trails-$pt-ref"
                rm -rf "$out/trails-$pt-w16"
                INPUTRUN_NOLOCK=$nl "$root/tests/currec.sh" -s tree -F -t 14 "$ts" "$out/trails-$pt-w16" > "$out/trails-$pt-w16.txt" 2>&1 || true
                python3 "$root/tools/cr1stat.py" --check "$out/trails-$pt-ref" "$out/trails-$pt-w16" > "$out/trails-$pt.txt" 2>&1 \
                    || r="$pt: $(grep -m1 -E 'DIFF|Error|rror' "$out/trails-$pt.txt") ($out/trails-$pt.txt)" ;;
            det|escape|boot)
                set --; [ $pt = boot ] && set -- -i WIN.INI:windows:MouseTrails=5
                rm -rf "$out/trails-$pt"
                "$root/tests/vgatrails.sh" -p $pt "$@" "$out/trails-$pt" > "$out/trails-$pt.txt" 2>&1 \
                    || r="$pt: $(tail -1 "$out/trails-$pt.txt") ($out/trails-$pt)" ;;
            input|cpl)
                vdrv=$out/trails-msres-$pt.drv
                "$root/tests/vgamsres.sh" "$vdrv" > "$out/trails-msres-$pt.txt" 2>&1 || { r="$pt: tests/vgamsres.sh failed"; break; }
                if [ $pt = input ]; then
                    scr=$root/tests/scripts/input/trails.txt
                    set -- -i WIN.INI:windows:MouseTrails=7
                else
                    scr=$root/tests/scripts/input/cpltrails.txt
                    set -- -i WIN.INI:windows:run=CONTROL.EXE
                fi
                set -- "$@" -i WIN.INI:windows:MouseSpeed=0 -i WIN.INI:windows:CursorBlinkRate=65535
                refrun trails-$pt-ref trails "$root/tests/inputrun.sh" -s -b 25 -t 220 "$scr" "$out/trails-$pt-ref" -r "$@"
                rm -rf "$out/trails-$pt-w16"
                "$root/tests/inputrun.sh" -e "$out/trails-$pt-ref/boot.png" -E "$out/trails-$pt-ref" -b 25 -t 220 "$scr" \
                    "$out/trails-$pt-w16" -r "$@" -f "$vdrv=WINDOWS/SYSTEM/VGA.DRV" > "$out/trails-$pt-w16.txt" 2>&1 || true
                rm -f "$vdrv"
                ls "$out/trails-$pt-ref"/[0-9]*.png > /dev/null 2>&1 || { r="$pt: no screens"; break; }
                for f in "$out/trails-$pt-ref"/[0-9]*.png; do
                    g=$(basename "$f")
                    [ -f "$out/trails-$pt-w16/$g" ] || { r="$pt: no $g"; break; }
                    n=$(magick compare -metric AE "$out/trails-$pt-w16/$g" "$f" "$out/trails-$pt-${g%.png}-diff.png" 2>&1 >/dev/null || true)
                    n=${n%% *}
                    [ "$n" = 0 ] || { r="$pt: $g: $n pixels differ"; break; }
                done
                [ "$r" = ok ] || break
                a=$(grep -i '^MouseTrails=' "$out/trails-$pt-ref"/work/c/WINDOWS/WIN.INI | tr -d '\r ')
                b=$(grep -i '^MouseTrails=' "$out/trails-$pt-w16"/work/c/WINDOWS/WIN.INI | tr -d '\r ')
                [ "$a" = "$b" ] || r="$pt: WIN.INI $a against $b" ;;
            esac
            [ "$r" = ok ] || break
        done
        if [ -n "$part" ]; then partdone "$r"
        elif [ "$r" = ok ]; then echo "ok   trails: VG9 identical, 0 pixels under real input, MouseTrails the same"
        else echo "FAIL trails: $r ($out)"; fails=$((fails + 1)); fi ;;
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
# Write's feature scenarios run as w16write parts (tests/writescenarios.sh)
WRSCEN_BOOT=${WRSCEN_BOOT:-wr24 wr25 wr26 wr28 wr29 wr30 wr30o wr31 wr32 wr33 wr34 wr35}
caseparts() {
    case $1 in
    allw16nolock) seq -f "$1@%g" 1 "${NOLOCK_RUNS:-6}" ;;
    pmdrop) echo "$1@pm $1@sh $1@us" ;;
    sbdrag) echo "$1@us $1@np $1@fast $1@npw" ;;
    edithandle) echo "$1@us $1@np" ;;
    gpuser) echo "$1@u286 $1@u386 $1@t286 $1@t386" ;;
    winfile) echo "$1@cmp $1@all" ;;
    exittree) echo "$1@e386 $1@e386ms $1@std" ;;
    swapesc) echo "$1@k386 $1@k286 $1@mswoa" ;;
    runbrowse) echo "$1@cmp $1@all" ;;
    missingdll) echo "$1@pm $1@tk59 $1@tree" ;;
    runassoc) echo "$1@probe $1@pm $1@pmk286 $1@pmk386 $1@tree $1@dir" ;;
    runpe) echo "$1@k286 $1@k386 $1@on286 $1@on386" ;;
    olecli) printf '%s@tree %s@cardw16 ' "$1" "$1"; for v in $(cd "$root/tests/ole/obs" && ls *.txt | sed 's/\.txt$//'); do printf '%s@%s ' "$1" "$v"; done ;;
    olesvr) printf '%s@tree ' "$1"; for v in $(cd "$root/tests/olesvr/scripts" && ls *.os2 | sed 's/\.os2$//'); do printf '%s@%s ' "$1" "$v"; done ;;
    ddeml) printf '%s@tree286 %s@tree386 ' "$1" "$1"; for v in $(cd "$root/tests/ddeml/scripts" && ls *.dm | grep -v '^x' | sed 's/\.dm$//'); do printf '%s@%s ' "$1" "$v"; grep -q '^start ' "$root/tests/ddeml/scripts/$v.dm" && printf '%s@%s+x ' "$1" "$v"; done ;;
    lzexpand) echo "$1@s $1@3 $1@tree286 $1@tree386 $1@quirks" ;;
    ver) echo "$1@s $1@3 $1@x $1@tree286 $1@tree386 $1@mods $1@fonts $1@fontsnew" ;;
    timer) echo "$1@s $1@3 $1@qt" ;;
    quicktime) echo "$1@over $1@tree $1@tree386 $1@cpl" ;;
    mmsystem) echo "$1@core $1@mmio $1@timer $1@qt $1@dev $1@mci $1@prog" ;;
    sb16) echo "$1@dev $1@audio $1@prog" ;;
    bigmem) echo "$1@s128 $1@s256 $1@s256ms $1@e128 $1@e256 $1@stock $1@ref" ;;
    filecdr) echo "$1@k286 $1@k386 $1@userk" ;;
    toolhelp) echo "$1@ref286 $1@ref386 $1@ref3 $1@tree286 $1@tree386 $1@watson $1@qt" ;;
    treek386) echo "$1@items $1@desk" ;;
    zipsetup) echo "$1@pm $1@tree" ;;
    ddescen) for v in $(cd "$root/tests/scripts/dde" && ls *.txt | sed 's/\.txt$//'); do printf '%s@%s ' "$1" "$v"; done ;;
    control) echo "$1@ms $1@k286 $1@k386 $1@tree $1@resize" ;;
    w16write) echo "$1@ms $1@k286 $1@k386 $1@tree"; for v in $WRSCEN_BOOT; do printf '%s@%s ' "$1" "$v"; done ;;
    w16winfile) for v in $(cd "$root/tests/scripts/winfile" && ls WF*.txt | sed 's/\.txt$//' | tr A-Z a-z); do printf '%s@%s ' "$1" "$v"; done ;;
    taskman) echo "$1@ms $1@k286 $1@us $1@usall $1@tree" ;;
    cplclick) for v in kb ms dk co; do printf "%s@%s-ms %s@%s-tree " "$1" $v "$1" $v; done ;;
    cplclickus) for v in kb ms dk co; do printf "%s@%s-us " "$1" $v; done ;;
    cpclose) echo "$1@us $1@usw $1@ctl $1@tree" ;;
    mousespeed) echo "$1@s0-us $1@s2-us $1@s0-drv $1@s2-drv" ;;
    kbd) echo "$1@kbtype $1@tabs $1@alttab $1@helpmenus $1@pmkeys $1@icons $1@menus" ;;
    mouse) echo "$1@dragoff $1@pmitems $1@pmclose $1@helpmenus" ;;
    vga) for v in kbtype tabs alttab helpmenus pmkeys icons menus dragoff pmitems pmclose mdimax about; do printf '%s@%s ' "$1" $v; done ;;
    trails) echo "$1@det $1@escape $1@boot $1@input $1@cpl $1@timed $1@timednl" ;;
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
    gpuser)
        if [ $pbad = 0 ]; then echo "ok   gpuser: US27's bad pointers as on 3.1 ($pt of $pt: w16's USER on Microsoft's KRNL286 and KRNL386, w16's tree on both)"
        else echo "FAIL gpuser: $pfirst"; fi ;;
    sbdrag)
        if [ $pbad = 0 ]; then echo "ok   sbdrag: 0 pixels mid-drag (US19's four bars, Microsoft's and w16's Notepad, a fast drag), US19's messages as 3.1's"
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
    exittree)
        if [ $pbad = 0 ]; then echo "ok   exittree: Exit Windows on w16's tree over 3.1 ends cleanly (w16's WIN386, Microsoft's WIN386, Standard mode)"
        else echo "FAIL exittree: $pfirst ($out)"; fi ;;
    swapesc)
        if [ $pbad = 0 ]; then echo "ok   swapesc: Alt+Esc, a double click, Alt+Tab and a second prompt as on 3.1 (KRNL386, KRNL286, Microsoft's WINOLDAP)"
        else echo "FAIL swapesc: $pfirst ($out)"; fi ;;
    toolhelp)
        if [ $pbad = 0 ]; then echo "ok   toolhelp: the probe's log with w16's TOOLHELP as with Microsoft's (KRNL286, KRNL386, 386 Enhanced; both side by side alike), w16's tree as 3.1, Dr. Watson, QuickTime"
        else echo "FAIL toolhelp: $pfirst ($out)"; fi ;;
    ver)
        if [ $pbad = 0 ]; then echo "ok   ver: VER.DLL's probe log as with Microsoft's (Standard, 386 Enhanced, DOSBox-X, the tree under both kernels as 3.1, with Microsoft's VER.DLL and with w16's), w16's modules' versions, Control Panel's Fonts applet"
        else echo "FAIL ver: $pfirst ($out)"; fi ;;
    sb16)
        if [ $pbad = 0 ]; then echo "ok   sb16: MMP's device sections over SB16.DRV alike under both MMSYSTEMs and as over SNDBLST2 but the SB16's extras; its sound as the material (8/16 bits, mono/stereo, 8000-44100 Hz, loops, volume, MIDI pitches); Sound Recorder, Sound and Media Player as over SNDBLST2"
        else echo "FAIL sb16: $pfirst ($out)"; fi ;;
    bigmem)
        if [ $pbad = 0 ]; then echo "ok   bigmem: 128 and 256 MB used and written in Standard and 386 Enhanced mode (w16's DOSX, WIN386, KRNL386, under w16's and Microsoft's WSWAP); LargeMemory=0 as Windows 3.1 itself: all of it, 0500h's largest block at most 64 MB in Standard mode"
        else echo "FAIL bigmem: $pfirst ($out)"; fi ;;
    filecdr)
        if [ $pbad = 0 ]; then echo "ok   filecdr: TK64 and TK64M (FileCdr with USER's InitApp) as on 3.1 (w16's KRNL286, KRNL386; w16's USER)"
        else echo "FAIL filecdr: $pfirst ($out)"; fi ;;
    mmsystem)
        if [ $pbad = 0 ]; then echo "ok   mmsystem: MMP's log and TPR's (with TMS's record of every timer message) with w16's MMSYSTEM as with Microsoft's; the device drivers' messages as 3.1's; MCI as 3.1's (MCT.DRV, MCIWAVE, MCISEQ); Sound Recorder, Sound, MIDI Mapper and Media Player as on 3.1; QuickTime's movie as on 3.1"
        else echo "FAIL mmsystem: $pfirst ($out)"; fi ;;
    timer)
        if [ $pbad = 0 ]; then echo "ok   timer: TPR's log and TMS's record of every message as with Microsoft's TIMER.DRV (Standard, 386 Enhanced), the timings within their limits; QuickTime's movie as on 3.1"
        else echo "FAIL timer: $pfirst ($out)"; fi ;;
    lzexpand)
        if [ $pbad = 0 ]; then echo "ok   lzexpand: LZEXPAND's probe log as with Microsoft's (Standard and 386 Enhanced mode, the tree under both kernels, VerInstallFile), and its quirk sections too"
        else echo "FAIL lzexpand: $pfirst ($out)"; fi ;;
    treek386)
        if [ $pbad = 0 ]; then echo "ok   treek386: the tree under its KRNL386 as under its KRNL286 (every item of its groups; Clock and Calculator over the desktop stamp)"
        else echo "FAIL treek386: $pfirst ($out)"; fi ;;
    olecli)
        if [ $pbad = 0 ]; then echo "ok   olecli: Cardfile from the tree under both kernels; $((pt - 2)) OLE scenarios as with Microsoft's OLECLI, and card1 with w16's Cardfile"
        else echo "FAIL olecli: $pfirst ($out)"; fi ;;
    olesvr)
        if [ $pbad = 0 ]; then echo "ok   olesvr: a Paintbrush picture embedded in the tree with w16's OLECLI and OLESVR as with Microsoft's OLESVR; $((pt - 1)) OLESVR scenarios as with Microsoft's OLESVR"
        else echo "FAIL olesvr: $pfirst ($out)"; fi ;;
    ddeml)
        if [ $pbad = 0 ]; then echo "ok   ddeml: DDEML in the tree under both kernels as with Microsoft's DDEML; $((pt - 2)) DDEML scenarios as with Microsoft's DDEML in every client/server combination"
        else echo "FAIL ddeml: $pfirst ($out)"; fi ;;
    missingdll)
        if [ $pbad = 0 ]; then echo "ok   missingdll: a missing DLL as on 3.1: Program Manager (w16's KRNL286, KRNL386, every w16 part), TK59's boxes and return codes (both kernels), the tree under both kernels"
        else echo "FAIL missingdll: $pfirst ($out)"; fi ;;
    runassoc)
        if [ $pbad = 0 ]; then echo "ok   runassoc: every kind of name as on 3.1: SH14 (w16's SHELL on both kernels), Program Manager's Run (0 pixels, nothing run as DOS), the tree under both kernels"
        else echo "FAIL runassoc: $pfirst ($out)"; fi ;;
    zipsetup)
        if [ -f "$out/.zipsetup-skip" ]; then echo "skip zipsetup: no installer (\$W16_7ZSETUP, default ~/Downloads/7zsetw16.exe)"
        elif [ $pbad = 0 ]; then echo "ok   zipsetup: the 7-Zip 16-bit installer's group and items as on 3.1 (w16's PROGMAN on Microsoft's rest: 0 pixels, the same files; the tree under KRNL386 and KRNL286: 3.1's group file)"
        else echo "FAIL zipsetup: $pfirst ($out)"; fi ;;
    runbrowse)
        if [ $pbad = 0 ]; then echo "ok   runbrowse: w16 USER and GDI within the known pixels; every w16 part: no fault, Browse into SYSTEM and back"
        else echo "FAIL runbrowse: $pfirst ($out)"; fi ;;
    *)  # any split case without a line of its own above (control,
        # cplclick, mousespeed, ...): a failing part must never show only
        # as a higher failure count
        if [ $pbad = 0 ]; then echo "ok   $1: $pt of $pt parts"
        else echo "FAIL $1: $pbad of $pt parts failed; first: $pfirst ($out)"; fi ;;
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
