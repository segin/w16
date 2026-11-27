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
#   dosxaudit  the DOSX audit's probes (ARCHITECTURE.md 9.6):
#              DX9 (tests/dos/dx9.asm, in the KERNEL slot, tests/dosxtest.sh)
#              under Microsoft's DOSX and w16's: the OBS lines and any
#              "Standard Mode:" message the same (runs 01, 02, 08-16, 23,
#              24; 17 and 0E under DOSBox-X too); runs 03-07, 20 and 21,
#              which Windows 3.1's DOSX does not survive or does not check,
#              must end under w16's without a fault; DX10 (INT 21h AH=00h
#              from a Windows program: Windows 3.1 hangs) on w16's KRNL386
#              and KRNL286 and DOSX: the task ends, Windows goes on
#   odstring   TK56 as the shell calls OutputDebugString: with dropin.sh's
#              stock DebugToAux=1 the screen against 3.1's (which shows the
#              text: handle 3 is DOSBox's console), must be 0; with w16's
#              default DebugToAux=0 against TK56's screen without the calls
#              (the text never on the screen), must be 0
#   lastexit   the shell (TK55) ends at once: WIN's ERRORLEVEL 0 and text
#              mode 3 afterwards, as 3.1's KERNEL ends Windows through
#              USER's ExitWindows when the last task ends (ref and w16)
#   kexedir    TK69 (tests/apps/tk69.c) as the shell: where KERNEL finds
#              the DLLs a program in a directory of its own imports, what
#              LoadModule and WinExec make of OS/2 and other foreign NE,
#              LX, LE and PE files, KERNEL's constant exports; Microsoft's
#              KRNL386, w16's KRNL286 and KRNL386: the logs the same
#   wpregs     TK71 (tests/apps/tk71.c) as the shell on 3.1 and on w16's
#              tree: AX, CX, ES and DS a window procedure is called with
#              (AX the window's instance's data segment), and the messages
#              CreateWindow sends a hidden child and popup (WM_SIZE and
#              WM_MOVE right after WM_CREATE); the rest of TK71's log
#              (other registers, shown windows, WM_MDICREATE) is not
#              compared yet
#   mdistyle   TK75 (tests/apps/tk75.c) as the shell on 3.1 and on w16's
#              tree: what WM_MDICREATE makes of the child's style (with
#              and without MDIS_ALLCHILDSTYLES, hidden, maximised,
#              minimised), when it shows and activates the child, where the
#              focus goes; ShowWindow and SetWindowPos of a plain child
#              (WM_CHILDACTIVATE, WM_SETVISIBLE); the minimum size at
#              CreateWindow and SetWindowPos; BringWindowToTop of children,
#              SW_SHOWMAXIMIZED of a hidden child in a 0 x 0 client, owned
#              popups sent to the bottom and the top (Z lines). The messages are compared
#              without WM_NCPAINT, WM_GETTEXT, WM_ERASEBKGND and
#              WM_ISACTIVEICON (3.1 paints a window as it is shown, w16
#              when it next paints), and without the WM_WINDOWPOSCHANGED
#              3.1 sends a minimised child last, after its activation
#              (UNVERIFIED what moves it)
#   staroff    StarOffice 3.1's CD (tests/staroff.sh; skipped without
#              $W16_STAROFF_ISO): Microsoft's OLE 2 from it starting up,
#              and GetDriveType of the CD drive, on 3.1 and w16's tree
#   kcmdline   long WinExec command lines (TK68 as the shell, TK68W in
#              WINOLDAP.MOD's and WINOA386.MOD's place and run as a
#              Windows program): what KERNEL gives WINOLDAP and a program
#              (the PDB's tail) and WinExec's results, w16's KRNL286 and
#              KRNL386 (Standard) and KRNL386 (Enhanced) against
#              Microsoft's KRNL386, the PDB selector's limit aside; then,
#              w16 alone, the tails of FEh and FFh bytes 3.1 fails with
#              (Windows hangs or ends) must reach WINOLDAP whole
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
#   gdiaudit   the GDI audit's probes GD5 and GD8 with Microsoft's GDI and w16's: no FAIL
#   gdimeta    GD9 (metafiles: every record's bytes, disk files, playback,
#              EnumMetaFile, errors, the clipboard) with Microsoft's GDI and
#              w16's: no FAIL
#   gdidib     GD7 (DIBs: every format, colour use and call; RLE) with
#              Microsoft's GDI and VGA.DRV, w16's GDI, w16's VGA.DRV, both: no FAIL
#   gdiconv    GD15 (colour <-> monochrome BitBlt, StarView's disabled icons,
#              pattern and DIB pattern brushes, brush origins) with Microsoft's
#              GDI and VGA.DRV, w16's GDI, w16's VGA.DRV, both: no FAIL
#   gdifont    GD10 (raster font simulations: italic, underline, strike-out,
#              emboldening, integer scaling; metrics and pixels) and GD11
#              (the font mapper: every mapping and metric as in 3.1's log,
#              tests/gdi/gd11ref.txt) and GD14 (font enumeration: its
#              log as 3.1's, tests/gdi/gd14ref.txt) with Microsoft's GDI
#              and VGA.DRV,
#              w16's GDI, w16's VGA.DRV and both: no FAIL
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
#   win386tree  the install tree alone (C:\W16) in 386 Enhanced mode, its own
#              WIN386.EXE, VGA.DRV, VGA.3GR, with Microsoft's WINOA386.MOD
#              and DOS-box fonts added (the user's set-up, 2026-11-20): the
#              MS-DOS Prompt full screen, Alt+Enter to a window and back,
#              EXIT (tests/scripts/input/dosbox386tree.txt), against the
#              same tree under Microsoft's WIN386.EXE (DOSBox-X)
#   win386plain  the user's way: the install tree alone in plain DOSBox
#              (DOSBox's own DOS), `WIN /3` (WIN.COM's logo, no /B), the DOS
#              Prompt full screen must be a text screen, EXIT, then Exit
#              Windows must bring the DOS prompt back (dosbox386plain.txt)
#   win386audit  the WIN386 audit's probes (its triage file, in git history):
#              VX11 (tests/apps/vx11.c) as the shell in 386 Enhanced mode
#              under Windows 3.1's WIN386 and under w16's, the OBS lines
#              identical (runs main, E, L, M); runs P and S, which 3.1 does
#              not survive, must finish under w16; VXP.386's two
#              VPICD_Call_When_Hw_Int callbacks both called and chained as
#              under 3.1, its DDB_Next set (DOSBox-X, COM1)
#   vsd        the Virtual Sound Device (win386/vxd/vsd.c): tests/vsdprobe.sh
#              fg under w16's WIN386 and Microsoft's (DOSBox-X, the sound
#              recorded): the probe VxD VXV.386's VSD_Get_Version and
#              VSD_Bell results, SPK.COM's tones in a full-screen box, a
#              bell cutting one, the box silent while Alt+Tab has stopped
#              it (tools/vsdcmp.py); then Reboot's "not responding" screen
#              (vsdhung.txt): keys it has no use for each beep a bell of
#              Windows 3.1's tone and length
#   vdmad      the Virtual DMA Device (win386/vxd/vdmad.c): tests/vdmadprobe.sh
#              under w16's WIN386 and Microsoft's (DOSBox-X as a Sound
#              Blaster 16, the sound recorded): SBD.COM in a full-screen
#              box reads the 8237s' registers back, calls the Virtual DMA
#              Services, plays single-cycle and auto-initialised 8-bit and
#              16-bit DMA and a transfer through VDMAD's buffer; the probe
#              VxD VXM.386's VDMAD service results, trapped ports and
#              channel callbacks (tools/vdmadcmp.py: the logs, the tones)
#   win386vxc  the probe VxD VXC.386 (tests/msvcvxd.sh vxc) under w16's
#              WIN386 and Microsoft's: Get/Set_PM_Int_Vector,
#              Allocate_PM_Call_Back, Log_Proc_Call, the page services,
#              hooks of INT 21h, 2Fh and 31h, the GDT's selector 40h; and
#              VXCU.386, which calls a VMM service w16 lacks: WIN ends with
#              a message naming the device and the service
#   msvc       Visual C++ 1.52's DOSXNT, MMD, CVW1 and VMB.386 loaded from
#              C:\MSVC\BIN (tests/msvcvxd.sh boot cvw wx; skipped without
#              $W16_MSVC_DIR): w16's tree comes up with them as without;
#              CodeView for Windows comes up as under Windows 3.1; with
#              WXSRVR.EXE on VMB.386 Program Manager is painted and its
#              menus open as under 3.1
#   msvccl     Visual C++ 1.52's CL.EXE (Phar Lap's DOSXNT, a DPMI client)
#              builds and runs hello world in a DOS box under w16's WIN386
#              as under Microsoft's, with DOSXNT.386 and without
#              (tests/msvcvxd.sh cl; skipped without $W16_MSVC_DIR)
#   win386doscad  a DOS program's #GP (VX12 G: the stop box) and Ctrl+Alt+Del
#              with the MS-DOS Prompt full screen (its screen; Return closes
#              the box, vx12gc.txt; Escape goes back, dosbox386cad.txt)
#              against Microsoft's WIN386
#   win386dospif  the MS-DOS Prompt from a PIF that starts windowed (mkpif.py
#              windowed=1): the window, VER, full screen, VER, window, EXIT
#   win386hdd  w16's WIN386 on MS-DOS 4.00 (INPUTRUN_HDD), WIN /3 with VX11
#              as the shell: Windows starts (MS-DOS 4.00 takes interrupts
#              inside DOS calls, unlike DOSBox's DOS) and VX11's observations,
#              440Dh's track I/O (M8) among them, are Windows 3.1's there
#   win386dosprobe  VX12 in the MS-DOS Prompt (V86 API entries, 80x50 and
#              40x25 windows) against Microsoft's WIN386; A20 off through
#              the 8042 and port 92h must not end w16 (it ends 3.1)
#   win386idle w16's WIN386: Windows' speed beside 4 DOS boxes polling the
#              keyboard (VMPOLL finds them idle: at least 90 per cent) and
#              beside a busy one (it gets its share), measured by VX13
#              (tests/vmpollrun.sh), Microsoft's WIN386 alongside
#   win386xms  w16's WIN386: VX15's XMS and EMS calls in a DOS box (3 PIFs)
#              give the same log as under Microsoft's WIN386
#   win386memload  w16's WIN386: MEMLOAD's XMS and EMS workloads, XMS 3.0 calls
#              and three boxes at once, as under Microsoft's WIN386
#   win386mcb  the DOS memory arena in a DOS box (MCBCHK.EXE, tests/
#              mcbrun.sh): the UMB link's MCB at 9FFFh, UMBs linked, high
#              and low first allocations, the chain walked; on DOSBox-X's
#              and DOSBox's own DOS, w16's WIN386 on 3.1 and the tree alone
#              against Microsoft's WIN386 (w16 lost the 9FFFh MCB and
#              DOSBox-X stopped with "Corrupt MCB chain")
#   win386dpmi DPMI in DOS boxes (DPMIPRB, tests/dpmirun.sh; ARCHITECTURE.md
#              12.16): a raw 16- and 32-bit client's every answer, a
#              nested client, two boxes' clients at once and the end of a
#              box on a fault no handler takes, under w16's WIN386 as under
#              Microsoft's; the probe's lines on the tree alone as well
#   win386pifwin  plain DOSBox, the tree alone, WIN /3: a PIF that starts
#              its program windowed (made at run time with tools/mkpif.py),
#              then VER typed: the window shows the text (not black), as
#              with Microsoft's WINOA386 on the same tree
#   cmdprompt  the install tree's Main group's "Command Prompt" (CMD.PIF),
#              plain DOSBox: @e386 (WIN /3) a window showing the prompt and
#              VER; @std (WIN /S) full screen text; EXIT back to Windows
#   win386woa  w16's WINOA386.MOD with w16's WIN386 on Windows 3.1:
#              switching (dosbox386s.txt) and Exit Windows with a box
#              open (dosbox386x.txt), against Microsoft's WINOA386 and
#              WIN386 (the icon, the banner and the "still active" box's
#              branded text masked)
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
#              combination, the logs equal; part term: the termination
#              dialog (tests/ddeml/dmshot.sh on a virtual X display, Retry
#              then Close; tests/ddeml/termcmp.py: when it comes, the
#              return, its picture but for the rebranded words)
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
#              over the static). The tree's SB16.DRV gives QuickTime a
#              sound device in parts tree, tree386 and cpl; their stock
#              3.1 runs get Windows 3.1's SNDBLST2.DRV to match. Part enh:
#              the tree alone in 386 Enhanced mode (its WIN386 and
#              KRNL386, WIN /3; qtplay.sh -T 386 -E) against stock 3.1's
#              WIN /3: until 2026-11-22 the movie ended Windows with a
#              #GP in TIMER:1 (a nested interrupt's frame put on WIN386's
#              host stack over TIMER's words while its callback ran on
#              MMSYSTEM's stack; win386/vmm/dpmi.c nest_stack); every
#              check, the movie's length too, since 2026-11-23 (with its
#              sound: ARCHITECTURE.md 12.14)
#   mmsystem  w16's MMSYSTEM.DLL and MMTASK.TSK (ARCHITECTURE.md 23)
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
#              Media Player over it: every screen as over SNDBLST2); enh
#              (sbaudio.sh in 386 Enhanced mode with w16's VSBD.386 under
#              Microsoft's WIN386 and w16's, QuickTime's movie too) and
#              cont (a DOS box and Windows sharing the card: as Windows
#              3.1's SNDBLST2.DRV and VSBD.386, the contention message too)
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
#   nametable  FindResource by the names of a name table (resource type
#              15): TK74 as the shell (tests/apps/tk74.c), its log on
#              w16's KRNL286 (part k286) and KRNL386 (k386) the same as
#              on Microsoft's KRNL386
#   dlgfocus   the focus after CreateDialog of a dialog not shown or shown
#              without activation, Return on a focused push button,
#              DM_GETDEFID: US37 as the shell (tests/apps/us37.c), its log
#              with w16's USER the same as with Microsoft's
#   installers third-party installers on w16's install tree in 386
#              Enhanced mode, against Windows 3.1 (tests/installers.sh):
#              the files each installs the same as on 3.1; one part per
#              installer (WEP1 WEP2 WEP3 BW W4 JOHNNY), each skipped
#              when $W16_INSTDIR (default ~/Downloads/w16-installers) does
#              not hold it: they are never part of w16
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
#   uaudit     the USER audit (docs/notes/user-audit-triage.md, in git history): probes US29
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
#   combo      a combo box's layout (user/combo.c; Control Panel's Desktop
#              dialog drew its drop-down list boxes taller than the edit
#              fields beside them): probe US34 as the shell on 3.1 and on
#              w16's USER, their OBS lines identical (each style in five
#              fonts and in dialogs: the closed height, the edit field,
#              the item heights, the dropped list, and their pixels by
#              checksum; owner-drawn ones' WM_MEASUREITEMs)
#   menutab    menu items' accelerator text after '\t' and 08h (user/menu.c;
#              Microsoft Visual C++ 1.52's IDE showed a box for its File
#              menu's 08h): probe US38 as the shell on 3.1 and on w16's
#              USER, their OBS lines identical (GetMenuString, each
#              popup's size) and the screens it saves (C:\US38_nn.BMP:
#              popups from resource menus, AppendMenu, InsertMenu,
#              ModifyMenu and ChangeMenu, with tabs, 08h, ampersands, a
#              submenu, and menu bars with them) identical pixel for pixel
#   editwrap   a multi-line edit control's word wrap and undo
#              (user/edit.c): probe US35 as the shell on 3.1, on w16's USER
#              and on w16's USER and GDI, their OBS lines identical (line
#              breaks in three fonts and widths, EM_FMTLINES, the caret
#              over wrapped lines, scrolling, resizing, the word-break
#              procedure's calls, ES_CENTER/ES_RIGHT, EM_SETRECT, undo
#              sequences, the clipboard keys; the last run without the
#              pixel checksums)
#   edit32k    text past 32767 characters (user/edit.c, user/win.c): probe
#              US36 as the shell on 3.1 and on w16's USER, their OBS lines
#              identical (GetWindowText and WM_GETTEXT with counts to
#              65535, EM_SETSEL and EM_GETSEL past 32767 and with -1,
#              EM_LINEFROMCHAR/LINEINDEX/LINELENGTH/GETLINE there,
#              EM_LIMITTEXT, changes past 32767; a single-line control,
#              static controls, buttons, combo boxes)
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
#   pmarrange  Program Manager's Arrange Icons saved: an item dragged and the
#              Main group arranged, the Games icon dragged and the group
#              icons arranged, a click on an icon (its system menu), Windows
#              ended and started again (tests/scripts/input/pmarrange.txt):
#              w16 USER+GDI, and with w16 PROGMAN, against Microsoft's; the
#              group files and PROGMAN.INI identical
#   w16groups  w16's install tree alone (build/w16 as C:\W16, dropin.sh -T;
#              no file of Microsoft's but, in its first run, PROGMAN.EXE;
#              WSWAP is w16's): its default
#              PROGMAN.INI and groups (assets/progman) under real keys
#              (tests/scripts/input/w16groups.txt): Main's PIF Editor
#              run and closed, Main's Read Me opens
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
#   scrnsave   the screen savers (tests/ssscenarios.sh, a part per
#              tests/scripts/ss/*.txt): w16's SSSTARS.SCR on the reference
#              install against Microsoft's: the Setup and password dialogs,
#              Control Panel's Desktop with it (w16's MAIN.CPL), the running
#              saver (stars), USER starting it (w16's USER: time-out,
#              SC_SCREENSAVE); the INI files identical; tree286, tree386:
#              w16's install tree alone (tests/sstree.sh): USER starts the
#              saver after the time-out and on SC_SCREENSAVE, Desktop's Test
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
#   writefonts Write's Character > Fonts... (the user's report "Write says
#              that no fonts are installed": w16's GDI had no EnumFonts or
#              EnumFontFamilies), real keys (tests/scripts/input/
#              writefonts.txt: the dialog, Courier typed, Escape, Exit), no
#              printer installed, carets not blinking: Microsoft's Write on
#              all-Microsoft 3.1 against w16's Write there (part ms, 0
#              pixels) and against w16's Write, GDI, COMMDLG and USER there
#              (part w16: 0 pixels but for the list's TrueType pictures,
#              w16's own, and the TrueType sample in the first dialog
#              screen); w16's install tree alone in 386 Enhanced mode
#              (part tree, the user's setup): the dialog's screens against
#              w16's approved ones in tests/scripts/input/writefonts-tree/
#              (WRFONTS_TREEREF=update rewrites them)
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
#   kbdsnap    Print Screen and Alt+Print Screen with w16's KEYBOARD.DRV
#              under KB2, on Microsoft's USER and on w16's: the clipboard's
#              bitmap 640x480 (the screen), then 400x300 (KB2's window)
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
cases=${*:-kernel dosx dosxaudit kexedir wpregs mdistyle staroff kcmdline k286 k386 k386all k386e k386cad k386hung lastexit odstring wincom allw16 user userk gdi gdik usergdi gdiaudit gdimeta gdidib gdiconv gdifont winoldap winoldapk winoldapw wincomswap winprompt shell shellk tree allw16full input allw16input allw16bootinput allw16nolock exitwin exittree runfront runfrontk about dragoff dragoffk pmdrop tabs alttab helpmenus sbmenu sbdrag mmerge holdbtn edithandle capcursor trackmove gpuser deskver icons winfile runbrowse pmitems pmarrange w16groups ddescen scrnsave zipsetup pmclose write w16write writefonts w16winfile cardfile control taskman cplclick cpclose mousespeed missingdll runassoc runpe olecli olesvr ddeml lzexpand ver drivers quicktime timer mmsystem sb16 toolhelp treek386 mouse mouseall woados woadosbat swapdos swapalt swapall swapesc pmkeys kbd kbdall kbdwrite kbdwoados kbdsnap comm commdlg mdimax uaudit lbclip lbdraw combo menutab editwrap edit32k commuser commall termclose sysdrv sysdrvall sysdrvwoados sysboot sysclock sysprobe sound soundtime soundrec soundbeep soundboot treealone vga vgaboot vgaall vgawoados vgaprobe trails win386 win386input win386cad win386close win386hung win386dos win386dosw win386switch win386two win386exitbox win386w16dos win386tree win386woa win386plain win386audit win386doscad win386dosprobe win386dospif win386hdd win386idle win386xms win386memload win386mcb win386dpmi vsd vdmad win386vxc msvc msvccl bigmem filecdr nametable dlgfocus installers}
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
        _rp=
        for a in "$@"; do
            printf 'arg %s\n' "$a" | sed "s|$out|@OUT@|g; s|$root|@ROOT@|g"
            for f in "$a" "${a%%=*}"; do
                if [ -f "$f" ]; then printf 'file '; sha256sum < "$f"; break; fi
            done
            # -T DIR: the tree laid down (e.g. build/w16): every file in
            # it, so a changed group file or driver makes a new reference
            if [ "$_rp" = -T ] && [ -d "$a" ]; then
                (cd "$a" && find . -type f | LC_ALL=C sort | xargs -d '\n' sha256sum) | sha256sum | sed 's/^/tree /'
            fi
            _rp=$a
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
    dosxaudit)
        # DX9 runs: under Windows 3.1's DOSX through the reference cache,
        # under w16's every time, in four lanes
        da=$out/dosxaudit
        mkdir -p "$da"
        dx9=$root/build/dos/DX9.COM
        dxa_ms() {      # dxa_ms RUN [CPU]: Windows 3.1's log -> $da/RUN.CPU.ms
            _dc=${2:-386}
            refrun "dosxaudit-ms$_dc-$1" dosxaudit sh -c '"$@"' "$dx9" \
                "$root/tests/dosxtest.sh" -m ms -c "$_dc" -t 120 -a "$1" \
                -o "$da/ms$_dc-$1" DX9
            tr -d '\r' < "$out/dosxaudit-ms$_dc-$1.txt" > "$da/$1.$_dc.ms" 2>/dev/null || :
        }
        dxa_w16() {     # dxa_w16 RUN [CPU]: w16's log -> $da/RUN.w16
            _dc=${2:-386}
            "$root/tests/dosxtest.sh" -m w16 -q -c "$_dc" -t 120 -a "$1" \
                -o "$da/w16$_dc-$1" DX9 > /dev/null 2>&1 || :
            tr -d '\r' < "$da/w16$_dc-$1/DX9.w16.log" > "$da/$1.$_dc.w16" 2>/dev/null || :
        }
        dxa_same="1 2 8 9 a b c d e f 10 11 12 13 14 15 16 23 24"
        dxa_surv="3 4 5 6 7 20 21"
        (for t in 1 9 d 11 15 24 3 7; do dxa_ms "$t" 2>/dev/null; dxa_w16 "$t"; done) &
        (for t in 2 a e 12 16 4 20; do dxa_ms "$t" 2>/dev/null; dxa_w16 "$t"; done) &
        (for t in 8 b f 13 23 5 21; do dxa_ms "$t" 2>/dev/null; dxa_w16 "$t"; done) &
        (for t in c 10 14 6; do dxa_ms "$t" 2>/dev/null; dxa_w16 "$t"; done
         for t in 17 e; do dxa_ms "$t" 386x 2>/dev/null; dxa_w16 "$t" 386x; done) &
        wait
        r=ok
        # what is compared: OBS lines but the informational ones (OBS ~),
        # the "Standard Mode:" message's first line, the errorlevel line
        dxa_f() { grep -E '^OBS [^~]|Standard Mode:|ERRORLEVEL|^DX9 end' "$1" 2>/dev/null | sed 's/^ *//'; }
        for t in $dxa_same 17:386x e:386x; do
            c=${t#*:}; [ "$c" = "$t" ] && c=386; t=${t%:*}
            dxa_f "$da/$t.$c.ms" > "$da/$t.$c.ms.obs"
            dxa_f "$da/$t.$c.w16" > "$da/$t.$c.w16.obs"
            grep -q '^DX9 end\|Standard Mode' "$da/$t.$c.ms.obs" ||
                [ "$r" != ok ] || r="DX9 $t ($c): no reference log ($da/$t.$c.ms)"
            cmp -s "$da/$t.$c.ms.obs" "$da/$t.$c.w16.obs" || [ "$r" != ok ] ||
                r="DX9 $t ($c): $(diff "$da/$t.$c.ms.obs" "$da/$t.$c.w16.obs" | grep -c '^[<>]') line(s) differ from Windows 3.1's ($da/$t.$c.ms.obs, $t.$c.w16.obs)"
        done
        for t in $dxa_surv; do
            if ! grep -q '^WINEXIT' "$da/$t.386.w16" 2>/dev/null ||
                grep -q 'Standard Mode' "$da/$t.386.w16"; then
                [ "$r" != ok ] || r="DX9 $t did not end cleanly under w16's DOSX ($da/$t.386.w16)"
            fi
            case $t in 7) ;; *)
                grep -q '^DX9 end' "$da/$t.386.w16" 2>/dev/null || [ "$r" != ok ] ||
                    r="DX9 $t did not finish under w16's DOSX ($da/$t.386.w16)" ;;
            esac
        done
        # DX10: a task's INT 21h AH=00h, on w16's KRNL386 and KRNL286
        for k in 386 286; do
            if [ $k = 286 ]; then kv=REFRUN_KRNL286=1 kf=KRNL286; else kv=DX10K=386 kf=KRNL386; fi
            env $kv REFRUN_FILES="$root/build/dos/$kf.EXE=WINDOWS/SYSTEM/$kf.EXE $root/build/dos/DOSX.EXE=WINDOWS/SYSTEM/DOSX.EXE" \
                "$root/tests/refrun.sh" "$root/build/dos/DX10.EXE" > "$da/dx10-$k.txt" 2>&1 || :
            tr -d '\r' < "$da/dx10-$k.txt" | grep -q '^OBS child_ended=0001' &&
                tr -d '\r' < "$da/dx10-$k.txt" | grep -q '^OBS child_after=0000' &&
                tr -d '\r' < "$da/dx10-$k.txt" | grep -q '^DX10 done' ||
                [ "$r" != ok ] || r="DX10 on w16's KRNL$k: the task did not end cleanly ($da/dx10-$k.txt)"
        done
        if [ "$r" = ok ]; then echo "ok   dosxaudit: DX9 as under Windows 3.1's DOSX, its runs 03-07, 20, 21 survived; DX10's AH=00h ended its task on both KERNELs"
        else echo "FAIL dosxaudit: $r"; fails=$((fails + 1)); fi ;;
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
    kexedir)
        # TK69 (tests/apps/tk69.c): Microsoft's KRNL386, w16's KRNL286 and
        # KRNL386 (Standard mode), the logs the same
        d=$root/build/dos
        set -- -f "$d/TK69.EXE=TK69.EXE" -f "$d/TKID.TPL=TKID.TPL" -f "$d/TK69A.EXE=TK69X/TK69A.EXE" \
            -f "$d/TK69L.DLL=TK69X/TK69L.DLL" -i 'boot:shell=C:\TK69.EXE'
        for k in ref k286 k386; do
            case $k in ref) o="-r -3" ;; k286) o= ;; k386) o=-3 ;; esac
            # shellcheck disable=SC2086
            "$root/tests/dropin.sh" -t 120 -W TK69.LOG -o "$out/kexedir-$k" $o "$@" > "$out/kexedir-$k.txt" 2>&1 &
        done
        wait
        r=ok
        for k in ref k286 k386; do
            tr -d '\r' < "$out/kexedir-$k/c/TK69.LOG" 2>/dev/null | sed '/^T start/d' > "$out/kexedir-$k.log"
        done
        grep -q '^T done$' "$out/kexedir-ref.log" || r="ref did not finish ($out/kexedir-ref.log)"
        for k in k286 k386; do
            [ "$r" = ok ] || break
            diff "$out/kexedir-ref.log" "$out/kexedir-$k.log" > "$out/kexedir-$k.diff" ||
                r="$k: differs from 3.1 ($out/kexedir-$k.diff)"
        done
        if [ "$r" = ok ]; then echo "ok   kexedir: DLLs from the program's directory, foreign executables, constant exports as 3.1's"
        else echo "FAIL kexedir: $r"; fails=$((fails + 1)); fi ;;
    wpregs)
        # TK71: Microsoft's USER (3.1) against w16's tree
        for k in ref tree; do
            if [ $k = ref ]; then o="-r -3"; else o="-T $root/build/w16 -3"; fi
            # shellcheck disable=SC2086
            "$root/tests/dropin.sh" $o -c 'WIN /3' -t 120 -W TK71.LOG -o "$out/wpregs-$k" \
                -f "$root/build/dos/TK71.EXE=TK71.EXE" -i 'boot:shell=C:\TK71.EXE' > "$out/wpregs-$k.txt" 2>&1 &
        done
        wait
        for k in ref tree; do
            tr -d '\r' < "$out/wpregs-$k/c/TK71.LOG" 2>/dev/null |
                sed -n 's/^\(M [0-9A-F]*\) AX=\([^ ]*\) BX=[^ ]* CX=\([^ ]*\) .* ES=\([^ ]*\) DS=\([^ ]*\)$/\1 AX=\2 CX=\3 ES=\4 DS=\5/p; /hidden$/p; /^T done/p' > "$out/wpregs-$k.log"
        done
        r=ok
        grep -q '^T done' "$out/wpregs-ref.log" || r="ref did not finish ($out/wpregs-ref.log)"
        [ "$r" != ok ] || diff "$out/wpregs-ref.log" "$out/wpregs-tree.log" > "$out/wpregs.diff" ||
            r="differs from 3.1 ($out/wpregs.diff)"
        if [ "$r" = ok ]; then echo "ok   wpregs: a window procedure's AX, CX, ES, DS; WM_SIZE and WM_MOVE in CreateWindow as 3.1's"
        else echo "FAIL wpregs: $r"; fails=$((fails + 1)); fi ;;
    mdistyle)
        # TK75: Microsoft's USER (3.1) against w16's tree
        for k in ref tree; do
            if [ $k = ref ]; then o="-r -3"; else o="-T $root/build/w16 -3"; fi
            # shellcheck disable=SC2086
            "$root/tests/dropin.sh" $o -c 'WIN /3' -t 120 -W TK75.LOG -o "$out/mdistyle-$k" \
                -f "$root/build/dos/TK75.EXE=TK75.EXE" -i 'boot:shell=C:\TK75.EXE' > "$out/mdistyle-$k.txt" 2>&1 &
        done
        wait
        for k in ref tree; do
            tr -d '\r' < "$out/mdistyle-$k/c/TK75.LOG" 2>/dev/null |
                sed -e '/^S/s/ \(0085\|000D\|0014\|0035\)//g' -e '/^S.*min$/s/ 0047 |/ |/' > "$out/mdistyle-$k.log"
        done
        r=ok
        grep -q '^T done' "$out/mdistyle-ref.log" || r="ref did not finish ($out/mdistyle-ref.log)"
        [ "$r" != ok ] || diff "$out/mdistyle-ref.log" "$out/mdistyle-tree.log" > "$out/mdistyle.diff" ||
            r="differs from 3.1 ($out/mdistyle.diff)"
        if [ "$r" = ok ]; then echo "ok   mdistyle: WM_MDICREATE's styles, showing, activation and focus as 3.1's"
        else echo "FAIL mdistyle: $r"; fails=$((fails + 1)); fi ;;
    staroff)
        # StarOffice 3.1's CD (tests/staroff.sh; skip without the image)
        "$root/tests/staroff.sh" -o "$out/staroff" > "$out/staroff.txt" 2>&1 || true
        if grep -q '^skip' "$out/staroff.txt"; then echo "ok   staroff: $(head -1 "$out/staroff.txt")"
        elif ! grep -q FAIL "$out/staroff.txt" && grep -q '^ok' "$out/staroff.txt"; then echo "ok   staroff: OLE 2 from the CD as on 3.1, the CD drive DRIVE_REMOTE"
        else echo "FAIL staroff: $(grep -m1 FAIL "$out/staroff.txt" || echo 'no result') ($out/staroff.txt)"; fails=$((fails + 1)); fi ;;
    kcmdline)
        # TK68 (tests/apps/tk68.c) makes the calls C:\TK68.TXT names; the
        # w16 runs make four more, whose tails 3.1 fails with
        common='c0 c89 c116 c117 c126 c127 c140 c200 c240 c245 c300 b117 b140 p117 p140 w126 w127 w200 w243'
        extra='c243 c244 b244 p244'
        printf '%s\r\n' "$common" > "$out/kcmdref.txt"
        printf '%s %s\r\n' "$common" "$extra" > "$out/kcmdw16.txt"
        set -- -f "$root/build/dos/TK68.EXE=TK68.EXE" -f "$root/build/dos/TK68W.EXE=TK68W.EXE" \
            -f "$root/build/dos/TK68W.EXE=WINDOWS/SYSTEM/WINOLDAP.MOD" \
            -f "$root/build/dos/TK68W.EXE=WINDOWS/SYSTEM/WINOA386.MOD" -i 'boot:shell=C:\TK68.EXE'
        for k in ref k286 k386 k386e; do
            case $k in
            ref)   o="-r -3" t=$out/kcmdref.txt ;;
            k286)  o= t=$out/kcmdw16.txt ;;
            k386)  o=-3 t=$out/kcmdw16.txt ;;
            k386e) o=-3 t=$out/kcmdw16.txt ;;
            esac
            # shellcheck disable=SC2086
            if [ $k = k386e ]; then
                "$root/tests/dropin.sh" -t 120 -o "$out/kcmdline-$k" $o -c 'WIN /3 /B' -f "$t=TK68.TXT" "$@" > "$out/kcmdline-$k.txt" 2>&1 &
            else
                "$root/tests/dropin.sh" -t 120 -o "$out/kcmdline-$k" $o -f "$t=TK68.TXT" "$@" > "$out/kcmdline-$k.txt" 2>&1 &
            fi
        done
        wait
        # the log without the mode and the PDB selector's limit
        kclog() { tr -d '\r' < "$out/kcmdline-$1/c/TK68.LOG" 2>/dev/null | sed '/^C start/d; s/ lim=[0-9A-F]*//'; }
        kclog ref | sed '/^C done/d' > "$out/kcmdline-ref.log"
        n=$(wc -l < "$out/kcmdline-ref.log")
        r=ok
        [ "$n" -gt 60 ] || r="ref: $n lines"
        for k in k286 k386 k386e; do
            [ "$r" = ok ] || break
            kclog $k > "$out/kcmdline-$k.log"
            head -n "$n" "$out/kcmdline-$k.log" | diff "$out/kcmdline-ref.log" - > "$out/kcmdline-$k.diff" ||
                { r="$k: differs from 3.1 ($out/kcmdline-$k.diff)"; break; }
            tail -n +"$((n + 1))" "$out/kcmdline-$k.log" > "$out/kcmdline-$k.extra"
            [ "$(grep -c '^C result=FFFF$' "$out/kcmdline-$k.extra")" = 4 ] &&
            [ "$(grep -c ' len=F[EF]$' "$out/kcmdline-$k.extra")" = 4 ] &&
            [ "$(grep -Ec '\\0D(C:\\5CT68\.(COM|BAT|PIF))\\0A\] n=0(0FF|100)$' "$out/kcmdline-$k.extra")" = 4 ] &&
            grep -q '^C done$' "$out/kcmdline-$k.extra" ||
                { r="$k: the FEh/FFh tails ($out/kcmdline-$k.extra)"; break; }
        done
        if [ "$r" = ok ]; then echo "ok   kcmdline: WinExec's long command lines, WINOLDAP's and a program's tails as 3.1's (KRNL286, KRNL386, Enhanced mode)"
        else echo "FAIL kcmdline: $r"; fails=$((fails + 1)); fi ;;
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
        # the GDI audit's probes GD5 and GD8 (tests/apps/gd5.c, gd8.c;
        # ARCHITECTURE.md 11.4) as the shell of Windows 3.1 with
        # Microsoft's GDI and with w16's: every log must end with "GDn done"
        # and have no FAIL (their tables, gd5ref.h and gd8ref.h, hold 3.1's
        # values)
        r=ok
        for p in GD5 GD8; do
            for k in ms w16; do
                o=; [ $k = ms ] && o=-r
                "$root/tests/gdiheap.sh" $o -p "$root/build/dos/$p.EXE" > "$out/gdiaudit-$p-$k.log" 2>&1 &
            done
        done
        wait
        for p in GD5 GD8; do
            for k in ms w16; do
                f=$out/gdiaudit-$p-$k.log
                grep -q "^$p done" "$f" || { r="$k: $p did not finish ($f)"; break 2; }
                if grep -q '^FAIL' "$f"; then r="$k: $(grep -m1 '^FAIL' "$f" | tr -d '\r') ($f)"; break 2; fi
            done
        done
        if [ "$r" = ok ]; then echo "ok   gdiaudit: GD5 and GD8 with w16's GDI as with Microsoft's (the GDI audit's observations)"
        else echo "FAIL gdiaudit: $r"; fails=$((fails + 1)); fi ;;
    gdimeta)
        # GD9 (tests/apps/gd9.c; ARCHITECTURE.md 11.8) as the shell of
        # Windows 3.1 with Microsoft's GDI (the table's own run: gd9ref.h
        # holds 3.1's values) and with w16's: each log must end with
        # "GD9 done" and have no FAIL
        r=ok
        for k in ms w16; do
            o=; [ $k = ms ] && o=-r
            "$root/tests/gdiheap.sh" $o -p "$root/build/dos/GD9.EXE" > "$out/gdimeta-$k.log" 2>&1 &
        done
        wait
        for k in ms w16; do
            f=$out/gdimeta-$k.log
            grep -q '^GD9 done' "$f" || { r="$k: GD9 did not finish ($f)"; break; }
            if grep -q '^FAIL' "$f"; then r="$k: $(grep -m1 '^FAIL' "$f" | tr -d '\r') ($f)"; break; fi
        done
        if [ "$r" = ok ]; then echo "ok   gdimeta: GD9's metafiles with w16's GDI as with Microsoft's"
        else echo "FAIL gdimeta: $r"; fails=$((fails + 1)); fi ;;
    gdiconv)
        # GD15 (tests/apps/gd15.c: colour <-> monochrome BitBlt, StarView's
        # disabled toolbar icons, pattern and DIB pattern brushes, brush
        # origins; StarOffice 3.1's toolbars and page surround) as the shell
        # of Windows 3.1: Microsoft's GDI and VGA.DRV (the table's own run),
        # w16's GDI, w16's VGA.DRV and both; every log must end with
        # "GD15 done" and have no FAIL
        r=ok
        for k in ms gdi vga both; do
            case $k in ms) set -- -r ;; gdi) set -- ;; vga) set -- -r -v "$root/build/dos/VGA.DRV" ;;
                both) set -- -v "$root/build/dos/VGA.DRV" ;; esac
            "$root/tests/gdiheap.sh" "$@" -p "$root/build/dos/GD15.EXE" > "$out/gdiconv-$k.log" 2>&1 &
        done
        wait
        for k in ms gdi vga both; do
            f=$out/gdiconv-$k.log
            grep -q '^GD15 done' "$f" || { r="$k: GD15 did not finish ($f)"; break; }
            if grep -q '^FAIL' "$f"; then r="$k: $(grep -m1 '^FAIL' "$f" | tr -d '\r') ($f)"; break; fi
        done
        if [ "$r" = ok ]; then echo "ok   gdiconv: GD15's colour conversions and brushes with w16's GDI and VGA.DRV as with Microsoft's"
        else echo "FAIL gdiconv: $r"; fails=$((fails + 1)); fi ;;
    gdidib)
        # GD7 (tests/apps/gd7.c: DIBs drawn as programs draw them, read
        # back pixel by pixel; QuickTime's installer banner, an RLE4 DIB,
        # was black on w16) as the shell of Windows 3.1: Microsoft's GDI
        # and VGA.DRV (the table's own run), w16's GDI, w16's VGA.DRV and
        # both; every log must end with "GD7 done" and have no FAIL
        r=ok
        for k in ms gdi vga both; do
            case $k in ms) set -- -r ;; gdi) set -- ;; vga) set -- -r -v "$root/build/dos/VGA.DRV" ;;
                both) set -- -v "$root/build/dos/VGA.DRV" ;; esac
            "$root/tests/gdiheap.sh" "$@" -p "$root/build/dos/GD7.EXE" > "$out/gdidib-$k.log" 2>&1 &
        done
        wait
        for k in ms gdi vga both; do
            f=$out/gdidib-$k.log
            grep -q '^GD7 done' "$f" || { r="$k: GD7 did not finish ($f)"; break; }
            if grep -q '^FAIL' "$f"; then r="$k: $(grep -m1 '^FAIL' "$f" | tr -d '\r') ($f)"; break; fi
        done
        if [ "$r" = ok ]; then echo "ok   gdidib: GD7's DIBs with w16's GDI and VGA.DRV as with Microsoft's"
        else echo "FAIL gdidib: $r"; fails=$((fails + 1)); fi ;;
    gdifont)
        # GD10 (tests/apps/gd10.c: text with italic, underline, strike-out
        # and emboldening a raster font lacks, in several faces and sizes,
        # drawn into memory bitmaps and read back; ARCHITECTURE.md 11.9)
        # as the shell of Windows 3.1: Microsoft's GDI and VGA.DRV (the
        # table's own run), w16's GDI, w16's VGA.DRV and both; every log
        # must end with "GD10 done" and have no FAIL. And GD11
        # (tests/apps/gd11.c: the font mapper and integer scaling,
        # ARCHITECTURE.md 11.10): every mapping and metric as 3.1's own
        # run gave them (tests/gdi/gd11ref.txt; tools/gd11.py leaves out
        # the TrueType and vector results w16 cannot give yet). And GD14
        # (tests/apps/gd14.c: font enumeration's returns, faces, DCs and
        # mapping modes, ARCHITECTURE.md 11.12): its log as 3.1's
        # (tests/gdi/gd14ref.txt)
        r=ok
        for k in ms gdi vga both; do
            case $k in ms) set -- -r ;; gdi) set -- ;; vga) set -- -r -v "$root/build/dos/VGA.DRV" ;;
                both) set -- -v "$root/build/dos/VGA.DRV" ;; esac
            "$root/tests/gdiheap.sh" "$@" -p "$root/build/dos/GD10.EXE" > "$out/gdifont-$k.log" 2>&1 &
            # (GD11's 5138 requests take over 10 minutes on a loaded host)
            REFRUN_TIMEOUT=1500 "$root/tests/gdiheap.sh" "$@" -p "$root/build/dos/GD11.EXE" > "$out/gdifont-map-$k.log" 2>&1 &
            "$root/tests/gdiheap.sh" "$@" -p "$root/build/dos/GD14.EXE" > "$out/gdifont-enum-$k.log" 2>&1 &
        done
        wait
        for k in ms gdi vga both; do
            f=$out/gdifont-$k.log
            grep -q '^GD10 done' "$f" || { r="$k: GD10 did not finish ($f)"; break; }
            if grep -q '^FAIL' "$f"; then r="$k: $(grep -m1 '^FAIL' "$f" | tr -d '\r') ($f)"; break; fi
            f=$out/gdifont-map-$k.log
            grep -q '^GD11 done' "$f" || { r="$k: GD11 did not finish ($f)"; break; }
            python3 "$root/tools/gd11.py" cmp "$root/tests/gdi/gd11ref.txt" "$f" > "$f.cmp" ||
                { r="$k: GD11 differs from 3.1 ($f.cmp)"; break; }
            f=$out/gdifont-enum-$k.log
            sed -n '/^GD14 start/,/^GD14 done/p' "$f" | tr -d '\r' | diff "$root/tests/gdi/gd14ref.txt" - > "$f.diff" ||
                { r="$k: GD14 differs from 3.1 ($f.diff)"; break; }
        done
        if [ "$r" = ok ]; then echo "ok   gdifont: GD10 (simulations, integer scaling), GD11 (font mapping) and GD14 (enumeration) with w16's GDI and VGA.DRV as with Microsoft's"
        else echo "FAIL gdifont: $r"; fails=$((fails + 1)); fi ;;
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
    win386dosw|win386switch|win386two|win386exitbox|win386dospif)
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
        elif [ $c = win386dospif ]; then
            # the MS-DOS Prompt from a PIF that starts it in a window
            # (tools/mkpif.py windowed=1, started by WIN.INI run=): VER in
            # the window, Alt+Enter to full screen (a VM that never had the
            # display: the WIN386 audit's H18), VER, back, EXIT
            scr=$root/tests/scripts/input/dosbox386win.txt
            shots="01-window 02-ver 03-full 04-ver 05-window 06-end"
            python3 "$root/tools/mkpif.py" "$root/.tools/ref/win31/cdrive/WINDOWS/DOSPRMPT.PIF" "$out/WINPRMPT.PIF" windowed=1
            cp "$root/.tools/ref/win31/cdrive/WINDOWS/WIN.INI" "$out/winprmpt.ini"
            python3 -I "$root/tests/scripts/iniset.py" "$out/winprmpt.ini" 'windows:run=C:\WINDOWS\WINPRMPT.PIF'
            set -- -f "$out/WINPRMPT.PIF=WINDOWS/WINPRMPT.PIF" -f "$out/winprmpt.ini=WINDOWS/WIN.INI"
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
            case $c:$(magick identify -format '%wx%h' "$b") in
            win386dospif:640x480) m="$m rectangle 96,216 104,240" ;;      # the window at the top: its caret lower
            esac
            magick "$a" -fill black -draw "$m" "$out/$c-a.png"
            magick "$b" -fill black -draw "$m" "$out/$c-b.png"
            n=$(magick compare -metric AE "$out/$c-a.png" "$out/$c-b.png" "$out/$c-$s-diff.png" 2>&1 >/dev/null || true)
            n=${n%%[. ]*}
            [ "${n:-1}" = 0 ] || { r="$s: $n pixels differ ($out/$c-$s-diff.png)"; break; }
        done
        if [ "$r" = ok ]; then echo "ok   $c: as with Microsoft's WIN386"
        else echo "FAIL $c: $r ($out/$c-w16)"; fails=$((fails + 1)); fi ;;
    vsd)
        # the Virtual Sound Device: tests/vsdprobe.sh fg (VXV.386's bells
        # and ports, SPK.COM's tones in a full-screen box, Alt+Tab
        # stopping it) and hung (Reboot's screen B: a bell for each key it
        # has no use for), under w16's WIN386 against Microsoft's
        set -- -k "$root/build/dos/VXV.386" -k "$root/build/dos/SPK.COM" -k "$root/build/dos/VX8.EXE" \
            -k "$root/tests/scripts/input/vsdfg.txt" -k "$root/tests/scripts/input/vsdhung.txt"
        refrun vsd-ref vsd "$root/tests/vsdprobe.sh" -o "$out/vsd-ref" "$@" ms fg
        refrun vsdh-ref vsd "$root/tests/vsdprobe.sh" -o "$out/vsdh-ref" "$@" ms hung
        "$root/tests/vsdprobe.sh" -o "$out/vsd-w16" "$@" w16 fg > "$out/vsd-w16.txt" 2>&1 || true
        "$root/tests/vsdprobe.sh" -o "$out/vsdh-w16" "$@" w16 hung > "$out/vsdh-w16.txt" 2>&1 || true
        r=$(python3 "$root/tools/vsdcmp.py" "$out/vsd-ref" "$out/vsd-w16" 2>&1) || r="FAIL $r"
        h=$(python3 "$root/tools/vsdcmp.py" -b "$out/vsdh-ref" "$out/vsdh-w16" 2>&1) || r="FAIL $r; $h"
        case $r in
        FAIL*) echo "FAIL vsd: ${r#FAIL } ($out/vsd-w16, $out/vsdh-w16)"; fails=$((fails + 1)) ;;
        *) echo "ok   vsd: $r; $h" ;;
        esac ;;
    win386vxc)
        # VXC.386 under both WIN386s, VXCU.386's missing service (tests/msvcvxd.sh)
        "$root/tests/msvcvxd.sh" -o "$out/win386vxc" vxc > "$out/win386vxc.txt" 2>&1 || true
        if grep -q '^ok' "$out/win386vxc.txt" && ! grep -q '^FAIL' "$out/win386vxc.txt"; then
            echo "ok   win386vxc: PM vectors and callbacks, Log_Proc_Call, pages, selector 40h as 3.1's; a missing service named"
        else echo "FAIL win386vxc: $(grep -m1 '^FAIL' "$out/win386vxc.txt" || echo 'no result') ($out/win386vxc.txt)"; fails=$((fails + 1)); fi ;;
    msvccl)
        # the user's report (2026-11-26): CL.EXE stopped with "Phar Lap err
        # 35" in a w16 box (tests/msvcvxd.sh cl; skip without $W16_MSVC_DIR)
        "$root/tests/msvcvxd.sh" -o "$out/msvccl" cl > "$out/msvccl.txt" 2>&1 || true
        if grep -q '^FAIL' "$out/msvccl.txt"; then
            echo "FAIL msvccl: $(grep -m1 '^FAIL' "$out/msvccl.txt") ($out/msvccl.txt)"; fails=$((fails + 1))
        elif grep -q '^skip' "$out/msvccl.txt"; then echo "ok   msvccl: $(grep -m1 '^skip' "$out/msvccl.txt")"
        elif grep -q '^ok' "$out/msvccl.txt"; then echo "ok   msvccl: $(grep -m1 '^ok' "$out/msvccl.txt" | sed 's/^ok *cl: //')"
        else echo "FAIL msvccl: no result ($out/msvccl.txt)"; fails=$((fails + 1)); fi ;;
    msvc)
        # Visual C++ 1.52's devices (tests/msvcvxd.sh; skip without $W16_MSVC_DIR)
        "$root/tests/msvcvxd.sh" -o "$out/msvc" boot cvw wx > "$out/msvc.txt" 2>&1 || true
        if grep -q '^FAIL' "$out/msvc.txt"; then
            echo "FAIL msvc: $(grep -m1 '^FAIL' "$out/msvc.txt") ($out/msvc.txt)"; fails=$((fails + 1))
        elif grep -q '^skip' "$out/msvc.txt"; then echo "ok   msvc: $(grep -m1 '^skip' "$out/msvc.txt")"
        elif [ "$(grep -c '^ok' "$out/msvc.txt")" = 3 ]; then
            echo "ok   msvc: w16's tree with Visual C++'s four devices comes up; CodeView, WXSRVR on VMB and Program Manager as under 3.1"
        else echo "FAIL msvc: no result ($out/msvc.txt)"; fails=$((fails + 1)); fi ;;
    vdmad)
        # the Virtual DMA Device: tests/vdmadprobe.sh (SBD.COM's DMA in a
        # box, VXM.386's service results), w16's WIN386 against Microsoft's
        set -- -k "$root/build/dos/VXM.386" -k "$root/build/dos/SBD.COM" \
            -k "$root/tests/scripts/input/vdmadbox.txt"
        refrun vdmad-ref vdmad "$root/tests/vdmadprobe.sh" -o "$out/vdmad-ref" "$@" ms
        "$root/tests/vdmadprobe.sh" -o "$out/vdmad-w16" "$@" w16 > "$out/vdmad-w16.txt" 2>&1 || true
        if r=$(python3 "$root/tools/vdmadcmp.py" "$out/vdmad-ref" "$out/vdmad-w16" 2>&1); then
            echo "ok   vdmad: $r"
        else
            echo "FAIL vdmad: $r ($out/vdmad-w16)"; fails=$((fails + 1))
        fi ;;
    win386dosprobe)
        # VX12 (tests/dos/vx12.asm) in the MS-DOS Prompt under w16's WIN386
        # against Microsoft's (DOSBox-X's own DOS): the V86 API entries
        # (INT 2Fh 1684h: the devices and their function 0, the addresses
        # left out), 80x50 and 40x25 text full screen, in a window and back
        # (tests/scripts/input/vx12v.txt; up to 40 pixels for the blinking
        # cursor on text screens); then A20 switched off through the 8042
        # and through port 92h (vx12a.txt, vx12b.txt), which ends Windows
        # 3.1 and must not end w16
        k386=$root/.tools/ref/win31/cdrive/WINDOWS/SYSTEM/KRNL386.EXE
        set -- -r -p 386 -c "WIN /3 /B" -f "$k386=WINDOWS/SYSTEM/KRNL386.EXE" -f "$root/build/dos/VX12.COM=WINDOWS/VX12.COM"
        refrun win386dosprobe-ref win386dosprobe env DROPIN_NOREDIR=1 INPUTRUN_KEYDELAY=150 INPUTRUN_STAY=1 \
            "$root/tests/inputrun.sh" -s -b 200 -t 400 "$root/tests/scripts/input/vx12v.txt" "$out/win386dosprobe-ref" "$@"
        for p in v a b; do
            DROPIN_NOREDIR=1 INPUTRUN_KEYDELAY=150 INPUTRUN_STAY=1 "$root/tests/inputrun.sh" -s -b 200 -t 400 \
                "$root/tests/scripts/input/vx12$p.txt" "$out/win386dosprobe-$p" "$@" \
                -f "$root/build/win386/WIN386.EXE=WINDOWS/SYSTEM/WIN386.EXE" > "$out/win386dosprobe-$p.txt" 2>&1 || true
        done
        r=ok
        for k in ref v; do
            d=$out/win386dosprobe-$k; [ $k = ref ] || d=$out/win386dosprobe-v
            find "$d/work" -iname vx12.log -exec cat {} \; 2>/dev/null | tr -d '\r' | grep '^VX12 V dev' | sed 's/ entry [0-9A-F:]*//' > "$out/win386dosprobe-$k.v"
        done
        [ -s "$out/win386dosprobe-ref.v" ] || r="no V86 API entries in the reference"
        [ "$r" != ok ] || cmp -s "$out/win386dosprobe-ref.v" "$out/win386dosprobe-v.v" ||
            r="the V86 API entries differ ($out/win386dosprobe-ref.v, win386dosprobe-v.v)"
        for s in 02-t50 03-t50w 04-t50f 05-f40 06-f40w 07-f40f 08-end 09-back; do
            [ "$r" = ok ] || break
            a=$out/win386dosprobe-v/$s.png b=$out/win386dosprobe-ref/$s.png
            if [ ! -f "$a" ] || [ ! -f "$b" ]; then r="no $s screen"; break; fi
            n=$(magick compare -metric AE "$a" "$b" "$out/win386dosprobe-$s-diff.png" 2>&1 >/dev/null || true)
            n=${n%%[. ]*}
            lim=0; [ "$(magick identify -format '%wx%h' "$b")" = 720x400 ] && lim=40
            [ "${n:-99999}" -le $lim ] || r="$s: $n pixels differ ($out/win386dosprobe-$s-diff.png)"
        done
        for p in a b; do
            [ "$r" = ok ] || break
            l=$(find "$out/win386dosprobe-$p/work" -iname vx12.log -exec cat {} \; 2>/dev/null | tr -d '\r')
            case $p in a) w='8042 on wrap=0' ;; b) w='92h back wrap=0' ;; esac
            printf '%s\n' "$l" | grep -q "$w" || r="VX12 ${p}: w16 did not go on ($out/win386dosprobe-$p)"
        done
        if [ "$r" = ok ]; then echo "ok   win386dosprobe: V86 API entries, 80x50 and 40x25 windows as 3.1's; A20 kept on"
        else echo "FAIL win386dosprobe: $r"; fails=$((fails + 1)); fi ;;
    win386doscad)
        # DOS programs that go wrong, under w16's WIN386 against Microsoft's
        # (Microsoft's WINOA386 and the rest; DOSBox-X's own DOS): VX12 G
        # makes a #GP in a full-screen box (the stop box, the box gone),
        # then Ctrl+Alt+Del in a second box shows the screen for a DOS
        # program and Return closes it (tests/scripts/input/vx12gc.txt);
        # and Ctrl+Alt+Del, Escape, VER, EXIT (dosbox386cad.txt). Masked:
        # the banner's "w16" rows and the blinking cursors
        k386=$root/.tools/ref/win31/cdrive/WINDOWS/SYSTEM/KRNL386.EXE
        r=ok
        for p in vx12gc dosbox386cad; do
            set -- -r -p 386 -c "WIN /3 /B" -f "$k386=WINDOWS/SYSTEM/KRNL386.EXE" -f "$root/build/dos/VX12.COM=WINDOWS/VX12.COM"
            refrun win386doscad-$p-ref win386doscad env DROPIN_NOREDIR=1 INPUTRUN_KEYDELAY=150 INPUTRUN_STAY=1 \
                "$root/tests/inputrun.sh" -s -b 200 -t 400 "$root/tests/scripts/input/$p.txt" "$out/win386doscad-$p-ref" "$@"
            DROPIN_NOREDIR=1 INPUTRUN_KEYDELAY=150 INPUTRUN_STAY=1 "$root/tests/inputrun.sh" -s -b 200 -t 400 \
                "$root/tests/scripts/input/$p.txt" "$out/win386doscad-$p-w16" "$@" \
                -f "$root/build/win386/WIN386.EXE=WINDOWS/SYSTEM/WIN386.EXE" > "$out/win386doscad-$p-w16.txt" 2>&1 || true
            for a in "$out/win386doscad-$p-w16"/[0-9]*.png; do
                [ -f "$a" ] || { r="$p: no screens"; break; }
                s=$(basename "$a" .png) b=$out/win386doscad-$p-ref/$s.png
                [ -f "$b" ] || { r="$p: no reference $s"; break; }
                m="rectangle 0,0 0,0"
                case $s in
                *-cad) m="rectangle 522,288 530,319 rectangle 477,304 485,319" ;;    # the cursor under OK / after the prompt
                *) [ "$(magick identify -format '%wx%h' "$b")" = 720x400 ] && m="rectangle 72,48 655,79 rectangle 99,0 107,399" ;;
                esac
                magick "$a" -fill black -draw "$m" "$out/win386doscad-a.png"
                magick "$b" -fill black -draw "$m" "$out/win386doscad-b.png"
                n=$(magick compare -metric AE "$out/win386doscad-a.png" "$out/win386doscad-b.png" "$out/win386doscad-$p-$s-diff.png" 2>&1 >/dev/null || true)
                n=${n%%[. ]*}
                [ "${n:-1}" = 0 ] || { r="$p $s: $n pixels differ ($out/win386doscad-$p-$s-diff.png)"; break; }
            done
            [ "$r" = ok ] || break
        done
        if [ "$r" = ok ]; then echo "ok   win386doscad: a DOS program's fault and Ctrl+Alt+Del (Escape, Return) as with Microsoft's WIN386"
        else echo "FAIL win386doscad: $r"; fails=$((fails + 1)); fi ;;
    win386idle)
        # VMPOLL (ARCHITECTURE.md 12.12; the user's report 2026-11-22: DOS
        # boxes made Windows janky, more so as more were open): VX13
        # (tests/apps/vx13.c, tests/vmpollrun.sh) measures Windows' busy
        # loop with 0 and 4 windowed Background boxes that poll the
        # keyboard (VX14 P), then 0 and 1 box busy counting (VX14 B), in
        # DOSBox-X at a fixed 30,000 cycles. Windows 3.1's VMPOLL found the
        # polling boxes idle (Windows kept 96 per cent of its speed) and
        # gave the busy box its share (Windows at 67 per cent). w16's must
        # keep at least 90 per cent with the polling boxes and give the
        # busy box at least 20 per cent (its VX14.LOG has counts)
        r=ok
        for k in p b; do
            c=0,4; [ $k = b ] && c=0,1
            refrun win386idle-ref$k win386idle env VMPOLL_DOSBOX=x VMPOLL_COUNTS=$c \
                sh "$root/tests/vmpollrun.sh" "$out/win386idle-ref$k" $k ms \
                "$root/build/dos/VX13.EXE" "$root/build/dos/VX14.COM" &
            rm -rf "$out/win386idle-w16$k"
            VMPOLL_DOSBOX=x VMPOLL_COUNTS=$c sh "$root/tests/vmpollrun.sh" "$out/win386idle-w16$k" $k w16 \
                > "$out/win386idle-w16$k.txt" 2>&1 &
        done
        wait
        # loop counts per number of boxes: "0 L0" "N LN"
        for k in p b; do
            for w in ref w16; do
                f=$out/win386idle-$w$k/VX13.LOG
                l0=$(sed -n 's/^VX13 boxes=0 loop=\([0-9]*\).*/\1/p' "$f" 2>/dev/null)
                ln=$(sed -n 's/^VX13 boxes=[1-9][0-9]* loop=\([0-9]*\).*/\1/p' "$f" 2>/dev/null | tail -1)
                if [ -z "$l0" ] || [ -z "$ln" ] || [ "$l0" = 0 ]; then r="$w $k: no measurement ($f)"; break 2; fi
                eval "pct_$w$k=$((ln * 100 / l0))"
            done
        done
        if [ "$r" = ok ]; then
            [ "$pct_w16p" -ge 90 ] || r="Windows at $pct_w16p% beside 4 polling boxes (Windows 3.1: $pct_refp%)"
        fi
        if [ "$r" = ok ]; then
            [ "$pct_w16b" -le 80 ] || r="the busy box got too little: Windows at $pct_w16b% (Windows 3.1: $pct_refb%)"
            grep -q 'VX14 B n=' "$out/win386idle-w16b/VX14.LOG" 2>/dev/null || r="the busy box never counted ($out/win386idle-w16b)"
        fi
        if [ "$r" = ok ]; then echo "ok   win386idle: Windows at $pct_w16p% beside 4 polling boxes (3.1: $pct_refp%), $pct_w16b% beside a busy one (3.1: $pct_refb%)"
        else echo "FAIL win386idle: $r"; fails=$((fails + 1)); fi ;;
    win386xms)
        # V86MMGR (ARCHITECTURE.md 12.13): VX15 (tests/dos/vx15.asm,
        # tests/v86mmrun.sh) in a DOS box calls the XMS driver and INT 67h
        # (versions, free memory, blocks, moves, locks, the HMA, A20, EMS
        # pages, mapping through the frame, save/restore, names, the
        # mappable pages, errors) with the default PIF, with limits and
        # required amounts (and Uses High Memory Area off), and with no XMS
        # or EMS: its log must equal the one under Microsoft's WIN386
        # (plain DOSBox, whose own EMS gives the frame E000h)
        r=ok
        for k in def lim zero; do
            case $k in
            def) p= ;;
            lim) p="emslim=256 emsreq=128 xmslim=512 xmsreq=256 hma=0" ;;
            zero) p="xmslim=0 emslim=0" ;;
            esac
            refrun win386xms-ref$k win386xms env "V86MM_PIF=$p" \
                sh "$root/tests/v86mmrun.sh" "$out/win386xms-ref$k" ms "$root/build/dos/VX15.COM" &
            V86MM_PIF=$p sh "$root/tests/v86mmrun.sh" "$out/win386xms-w16$k" w16 > "$out/win386xms-w16$k.txt" 2>&1 &
        done
        wait
        for k in def lim zero; do
            a=$out/win386xms-ref$k/VX15.LOG b=$out/win386xms-w16$k/VX15.LOG
            if ! grep -q '^VX15 end' "$a" 2>/dev/null; then r="$k: no reference log ($a)"; break; fi
            if ! grep -q '^VX15 end' "$b" 2>/dev/null; then r="$k: VX15 did not finish under w16 ($b)"; break; fi
            cmp -s "$a" "$b" || { r="$k: the logs differ (diff $a $b)"; break; }
        done
        if [ "$r" = ok ]; then echo "ok   win386xms: XMS and EMS in a DOS box as under Microsoft's WIN386 (3 PIFs)"
        else echo "FAIL win386xms: $r"; fails=$((fails + 1)); fi ;;
    win386memload)
        # V86MMGR under load (ARCHITECTURE.md 12.13): MEMLOAD.EXE
        # (tests/dos/memload.c, tests/memloadrun.sh), XMS and EMS used as
        # real programs use them: the XMS workload (blocks filled through
        # moves, block-to-block and overlapping moves, everything read
        # back, the HMA), the EMS workload (pages over three handles,
        # random remapping, 47h/48h, 4Eh, 50h, 57h), the XMS 3.0 and UMB
        # calls, and three Background boxes running both workloads at once
        # (each checking its own data), and a box seeing a file another box
        # makes while it waits (f). Every log under w16's WIN386 must
        # equal Microsoft's but for the RATE lines, with no FAIL
        r=ok
        for k in x e 3 c f; do
            refrun win386memload-ref$k win386memload \
                sh "$root/tests/memloadrun.sh" "$out/win386memload-ref$k" $k ms "$root/build/dos/MEMLOAD.EXE" &
            sh "$root/tests/memloadrun.sh" "$out/win386memload-w16$k" $k w16 > "$out/win386memload-w16$k.txt" 2>&1 &
        done
        wait
        rates=
        for k in x e 3 c f; do
            for t in A B C; do
                [ $k = c ] || [ $t = A ] || { [ $k = f ] && [ $t = B ]; } || continue
                a=$out/win386memload-ref$k/ML$t.LOG b=$out/win386memload-w16$k/ML$t.LOG
                grep -q '^MEMLOAD end, 0 failure' "$a" 2>/dev/null || { r="$k $t: the reference did not pass ($a)"; break 2; }
                grep -q '^MEMLOAD end, 0 failure' "$b" 2>/dev/null || { r="$k $t: w16's did not pass ($b)"; break 2; }
                grep -v '^RATE\|^INFO' "$a" > "$out/win386memload-a.txt"
                grep -v '^RATE\|^INFO' "$b" > "$out/win386memload-b.txt"
                cmp -s "$out/win386memload-a.txt" "$out/win386memload-b.txt" || { r="$k $t: the logs differ (diff $a $b)"; break 2; }
            done
        done
        for k in x e; do
            rates="$rates $k: $(grep '^RATE' "$out/win386memload-w16$k/MLA.LOG" 2>/dev/null | awk '{print $2"="$3}' | tr '\n' ' ')(3.1: $(grep '^RATE' "$out/win386memload-ref$k/MLA.LOG" 2>/dev/null | awk '{print $2"="$3}' | tr '\n' ' '))"
        done
        if [ "$r" = ok ]; then echo "ok   win386memload: XMS, EMS, XMS 3.0 and three boxes at once as under Microsoft's WIN386; KB/s$rates"
        else echo "FAIL win386memload: $r"; fails=$((fails + 1)); fi ;;
    win386mcb)
        # the user's report (2026-11-23): Norton Utilities' NCC.EXE in a
        # DOS box under w16's WIN386 on DOSBox-X ended the emulator with
        # "Corrupt MCB chain": a new VM's private pages started as zeros,
        # so the MCB after the free arena (9FFFh, the UMB link) was gone
        # (win386/vmm/vm.c). MCBCHK (tests/dos/mcbchk.c) must pass and
        # its log (but for the INFO lines) equal Microsoft's WIN386's, on
        # DOSBox-X and plain DOSBox; also on w16's tree alone (DOSBox-X)
        r=ok
        for e in x dosbox; do
            refrun win386mcb-ref$e win386mcb \
                sh "$root/tests/mcbrun.sh" "$out/win386mcb-ref$e" ms $e "$root/build/dos/MCBCHK.EXE" "$root/tests/mcbrun.sh" &
            sh "$root/tests/mcbrun.sh" "$out/win386mcb-w16$e" w16 $e > "$out/win386mcb-w16$e.txt" 2>&1 &
        done
        sh "$root/tests/mcbrun.sh" "$out/win386mcb-tree" tree x > "$out/win386mcb-tree.txt" 2>&1 &
        wait
        for k in w16x:refx w16dosbox:refdosbox tree:refx; do
            a=$out/win386mcb-${k#*:}/MCA.LOG b=$out/win386mcb-${k%:*}/MCA.LOG
            grep -q '^MCBCHK end, 0 failures' "$a" 2>/dev/null || { r="${k#*:}: the reference did not pass ($a)"; break; }
            grep -q '^MCBCHK end, 0 failures' "$b" 2>/dev/null || { r="${k%:*}: w16's did not pass ($b; DOSBox-X: $(grep -i 'mcb chain' "$out/win386mcb-${k%:*}/emu.log" 2>/dev/null | head -1))"; break; }
            grep -v '^INFO' "$a" > "$out/win386mcb-a.txt"
            grep -v '^INFO' "$b" > "$out/win386mcb-b.txt"
            cmp -s "$out/win386mcb-a.txt" "$out/win386mcb-b.txt" || { r="${k%:*}: the logs differ (diff $a $b)"; break; }
        done
        if [ "$r" = ok ]; then echo "ok   win386mcb: the MCB chain, UMB link and strategies in a DOS box as under Microsoft's WIN386 (DOSBox-X, DOSBox, the tree)"
        else echo "FAIL win386mcb: $r"; fails=$((fails + 1)); fi ;;
    win386dpmi)
        # the user's report (2026-11-26): Visual C++ 1.52's CL.EXE stopped
        # in a w16 box with "Phar Lap err 35": w16's boxes had no DPMI
        # host. DPMIPRB's logs (tests/dos/dpmiprb.asm) under w16's WIN386,
        # and on the tree alone, as under Microsoft's (tools/dpmicmp.sh)
        for k in p g c; do
            refrun win386dpmi-ref$k win386dpmi \
                sh "$root/tests/dpmirun.sh" "$out/win386dpmi-ref$k" $k ms x "$root/build/dos/DPMIPRB.COM" "$root/tests/dpmirun.sh" &
            sh "$root/tests/dpmirun.sh" "$out/win386dpmi-w16$k" $k w16 > "$out/win386dpmi-w16$k.txt" 2>&1 &
        done
        sh "$root/tests/dpmirun.sh" "$out/win386dpmi-treep" p tree > "$out/win386dpmi-treep.txt" 2>&1 &
        wait
        r=ok
        for k in w16p:refp w16g:refg w16c:refc treep:refp; do
            m=$(sh "$root/tools/dpmicmp.sh" "$(printf '%s' "${k%:*}" | sed 's/^.*\(.\)$/\1/')" \
                "$out/win386dpmi-${k#*:}" "$out/win386dpmi-${k%:*}") || { r="${k%:*}: ${m#FAIL }"; break; }
        done
        if [ "$r" = ok ]; then echo "ok   win386dpmi: DPMI clients in DOS boxes (16- and 32-bit, nested, two boxes, a fault) as under Microsoft's WIN386; the tree too"
        else echo "FAIL win386dpmi: $r"; fails=$((fails + 1)); fi ;;
    cmdprompt)
        # Main's first item runs CMD.PIF (assets/CMD.PIF: COMMAND.COM,
        # windowed in 386 Enhanced mode; Standard mode's WINOLDAP reads its
        # 286 section and runs it full screen)
        k=${part:-e386}
        case $k in e386) wc="WIN /3" ;; *) wc="WIN /S" ;; esac
        rm -rf "$out/cmdprompt-$k"
        DROPIN_NOREDIR=1 DROPIN_CPUTYPE=386 INPUTRUN_KEYDELAY=150 INPUTRUN_STAY=1 "$root/tests/inputrun.sh" -b 5 -t 400 \
            "$root/tests/scripts/input/dosbox386cmd.txt" "$out/cmdprompt-$k" -T "$root/build/w16" -3 -c "$wc" \
            > "$out/cmdprompt-$k.txt" 2>&1 || true
        r=ok
        if [ $k = e386 ]; then want="01-start:640x480 02-ver:640x480 03-back:640x480"
        else want="01-start:640x400 02-ver:640x400 03-back:640x480"; fi
        for w in $want; do
            f=$out/cmdprompt-$k/${w%%:*}.png
            g=$(magick identify -format '%wx%h' "$f" 2>/dev/null || echo none)
            [ "$g" = "${w#*:}" ] || { r="${w%%:*} is $g, not ${w#*:}"; break; }
            if [ $k = e386 ] && [ "${w%%:*}" != 03-back ]; then
                # the window shows the DOS screen (white text: DOSBox's
                # banner box, the prompt), not a black client area
                n=$(magick "$f" -crop 600x200+10+30 -format '%c' histogram:info:- | grep -i '#FFFFFF' | awk '{print $1}' | tr -d :)
                [ "${n:-0}" -ge 1000 ] || { r="${w%%:*}: no text in the window (${n:-0} white pixels)"; break; }
            fi
        done
        if [ "$r" = ok ]; then echo "ok   cmdprompt@$k: Main's Command Prompt ($wc): $( [ $k = e386 ] && echo 'a window with the prompt' || echo 'full screen text'), VER, EXIT back to Windows"
        else echo "     cmdprompt@$k: $r ($out/cmdprompt-$k)"; fi
        partdone "$r" ;;
    win386pifwin)
        # the user's report (2026-11-22): a PIF made with the PIF Editor
        # ("Command Prompt", windowed) opened a window whose client area
        # stayed black, with Microsoft's WINOA386 too: DOSBox's BIOS keeps
        # the video mode in the emulator, and a VM started windowed never
        # set one (win386/vmm/vdd.c vdd_vm_init); then w16's WINOA386
        # painted the VM's changes only within the last WM_PAINT's
        # rectangle (winoa386.c update). The PIF: the reference install's
        # MS-DOS Prompt PIF with another title and windowed, made here
        pif=$out/win386pifwin-CMD.PIF
        python3 "$root/tools/mkpif.py" "$root/.tools/ref/win31/cdrive/WINDOWS/DOSPRMPT.PIF" "$pif" \
            "title=Command Prompt" program=COMMAND.COM windowed=1
        scr=$root/tests/scripts/input/dosbox386pifwin.txt
        set -- -b 5 -t 400 "$scr"
        refrun win386pifwin-ref win386pifwin env DROPIN_NOREDIR=1 DROPIN_CPUTYPE=386 INPUTRUN_KEYDELAY=150 INPUTRUN_STAY=1 \
            "$root/tests/inputrun.sh" "$@" "$out/win386pifwin-ref" -T "$root/build/w16" -3 -c "WIN /3" \
            -f "$pif=W16/CMD.PIF" -f "$root/.tools/ref/win31/cdrive/WINDOWS/SYSTEM/WINOA386.MOD=W16/SYSTEM/WINOA386.MOD"
        rm -rf "$out/win386pifwin-w16"
        DROPIN_NOREDIR=1 DROPIN_CPUTYPE=386 INPUTRUN_KEYDELAY=150 INPUTRUN_STAY=1 "$root/tests/inputrun.sh" "$@" \
            "$out/win386pifwin-w16" -T "$root/build/w16" -3 -c "WIN /3" -f "$pif=W16/CMD.PIF" \
            > "$out/win386pifwin-w16.txt" 2>&1 || true
        r=ok
        for s in 01-window 02-ver; do
            a=$out/win386pifwin-w16/$s.png b=$out/win386pifwin-ref/$s.png
            if [ ! -f "$a" ] || [ ! -f "$b" ]; then r="no $s screen"; break; fi
            # text in the client area (white on DOSBox's blue box, the
            # prompt): not a black window
            c=$(magick "$a" -crop 600x200+10+30 -format '%c' histogram:info:- | grep -i '#FFFFFF' | awk '{print $1}' | tr -d :)
            [ "${c:-0}" -ge 1000 ] || { r="$s: no text in the window (${c:-0} white pixels)"; break; }
            n=$(magick compare -metric AE "$a" "$b" "$out/win386pifwin-$s-diff.png" 2>&1 >/dev/null || true)
            n=${n%%[. ]*}
            # the caret's blink (up to 2 x 8 x 3 pixels)
            [ "${n:-1}" -le 60 ] || { r="$s: $n pixels differ from Microsoft's WINOA386 ($out/win386pifwin-$s-diff.png)"; break; }
        done
        if [ "$r" = ok ]; then echo "ok   win386pifwin: a windowed PIF's window shows the DOS screen and VER, as Microsoft's WINOA386"
        else echo "FAIL win386pifwin: $r ($out/win386pifwin-w16)"; fails=$((fails + 1)); fi ;;
    win386plain)
        # the user's report (2026-11-21): after a DOS box, Exit Windows
        # printed KERNEL's "back to DOS" and hung (the DOS VM's first DOS
        # calls saved its stack in the loader's PSP, where DOS returned to
        # when KERNEL ended: win386/ldr/ldrasm.asm vm_stub), and the box
        # stayed in WIN.COM's logo's 640x480 graphics mode (the VM's BIOS
        # data as Windows started: win386/vmm/vdd.c bios_mode). No /B: the
        # logo is shown then, and no BOOTLOG.TXT (the boot is waited for by
        # the script)
        rm -rf "$out/win386plain"
        DROPIN_NOREDIR=1 DROPIN_CPUTYPE=386 INPUTRUN_KEYDELAY=150 INPUTRUN_STAY=1 "$root/tests/inputrun.sh" -b 5 -t 420 \
            "$root/tests/scripts/input/dosbox386plain.txt" "$out/win386plain" -T "$root/build/w16" -3 -c "WIN /3" \
            > "$out/win386plain.txt" 2>&1 || true
        r=ok
        for s in 01-full:640x400 02-back:640x480 03-dos:640x400; do
            g=$(magick identify -format '%wx%h' "$out/win386plain/${s%%:*}.png" 2>/dev/null || echo none)
            [ "$g" = "${s#*:}" ] || { r="${s%%:*} is $g, not ${s#*:}"; break; }
        done
        if [ "$r" = ok ] && ! grep -q '^Windows ended' "$out/win386plain/input.log"; then
            r="WIN did not return to the DOS prompt ($(grep -E '^(Windows|DOSBox)' "$out/win386plain/input.log" | tail -1))"
        fi
        if [ "$r" = ok ]; then echo "ok   win386plain: plain DOSBox, WIN /3: the DOS box in text mode, EXIT, Exit Windows back to the DOS prompt"
        else echo "FAIL win386plain: $r ($out/win386plain)"; fails=$((fails + 1)); fi ;;
    win386audit)
        w386=$root/build/win386/WIN386.EXE
        mkdir -p "$out/w386a"
        r=ok
        for m in "" E L M P S; do
            cp "$root/build/dos/VX11.EXE" "$out/w386a/VX11$m.EXE"
            REFRUN_FILES="$w386=WINDOWS/SYSTEM/WIN386.EXE" "$root/tests/refrun.sh" "$out/w386a/VX11$m.EXE" /3 \
                > "$out/win386audit-w16$m.txt" 2>&1 || true
            tr -d '\r' < "$out/win386audit-w16$m.txt" > "$out/win386audit-w16$m.log"
            grep -q '^VX11 done' "$out/win386audit-w16$m.log" ||
                [ "$r" != ok ] || r="VX11$m did not finish under w16's WIN386 ($out/win386audit-w16$m.log)"
            case $m in P|S) continue ;; esac            # Windows 3.1 ends in these
            refrun win386audit-ref$m win386audit "$root/tests/refrun.sh" "$out/w386a/VX11$m.EXE" /3
            tr -d '\r' < "$out/win386audit-ref$m.txt" | grep '^OBS [^~]' > "$out/win386audit-ref$m.obs" || true
            grep '^OBS [^~]' "$out/win386audit-w16$m.log" > "$out/win386audit-w16$m.obs" || true
            [ -s "$out/win386audit-ref$m.obs" ] || [ "$r" != ok ] || r="no reference observations for VX11$m"
            cmp -s "$out/win386audit-ref$m.obs" "$out/win386audit-w16$m.obs" || [ "$r" != ok ] ||
                r="VX11$m: $(diff "$out/win386audit-ref$m.obs" "$out/win386audit-w16$m.obs" | grep -c '^[<>]') observation line(s) differ from 3.1's ($out/win386audit-ref$m.obs, win386audit-w16$m.obs)"
        done
        # VXP.386 through a session under w16's WIN386 (tests/vxpprobe.sh's
        # run): Windows 3.1 gave 1 1 1 1 and DDB_Next set
        k386=$root/.tools/ref/win31/cdrive/WINDOWS/SYSTEM/KRNL386.EXE
        rm -rf "$out/win386audit-vxp" "$out/win386audit-vxp.com1"
        DROPIN_MACHINE=svga_s3 DROPIN_CONF="[serial]
serial1=file file:$out/win386audit-vxp.com1" timeout 400 "$root/tests/inputrun.sh" -s -b 200 -t 200 \
            "$root/tests/scripts/input/exitwin.txt" "$out/win386audit-vxp" -r -p 386 -c "WIN /3 /B" \
            -f "$k386=WINDOWS/SYSTEM/KRNL386.EXE" -f "$root/build/dos/VXP.386=WINDOWS/SYSTEM/VXP.386" \
            -i "386Enh:+device=vxp.386" -f "$w386=WINDOWS/SYSTEM/WIN386.EXE" > "$out/win386audit-vxp.txt" 2>&1 || true
        if [ "$r" = ok ] && ! tr -d '\r' < "$out/win386audit-vxp.com1" 2>/dev/null |
            grep -q 'Call_When_Hw_Int.*: 00000001 00000001 00000001 00000001 DDB_Next set: 00000001 time-out after STI: 00000001'; then
            r="VXP.386's VPICD_Call_When_Hw_Int line is not 3.1's ($out/win386audit-vxp.com1)"
        fi
        if [ "$r" = ok ]; then echo "ok   win386audit: VX11 as under Windows 3.1's WIN386, its P and S runs survived; VXP's callback chain as 3.1's"
        else echo "FAIL win386audit: $r"; fails=$((fails + 1)); fi ;;
    win386hdd)
        k386=$root/.tools/ref/win31/cdrive/WINDOWS/SYSTEM/KRNL386.EXE
        sed 's/^run=\r\{0,1\}$/run=C:\\VX11.EXE\r/' "$root/.tools/ref/win31/cdrive/WINDOWS/WIN.INI" > "$out/win386hdd.ini"
        # TK.LOG and BOOTLOG.TXT's last line to the output, not the 64 MB image
        hddvx11() {
            _hn=$1; shift
            rm -rf "$out/$_hn"
            INPUTRUN_HDD=1 INPUTRUN_KEYDELAY=150 DROPIN_MACHINE=svga_s3 timeout 400 "$root/tests/inputrun.sh" -b 120 -t 200 \
                "$root/tests/scripts/input/wait40.txt" "$out/$_hn" -r -p 386 -c "WIN /3 /B" \
                -f "$k386=WINDOWS/SYSTEM/KRNL386.EXE" -f "$root/build/dos/VX11.EXE=VX11.EXE" \
                -f "$out/win386hdd.ini=WINDOWS/WIN.INI" "$@" > "$out/$_hn.run" 2>&1 || true
            tr -d '\r' < "$out/$_hn/work/c/TK.LOG" 2>/dev/null
            grep -a '^INITDONE=Final USER' "$out/$_hn/work/c/WINDOWS/BOOTLOG.TXT" 2>/dev/null | tr -d '\r'
            rm -f "$out/$_hn/work/hdd.img"
        }
        refrun win386hdd-ref win386hdd hddvx11 win386hdd-ref
        hddvx11 win386hdd-w16 -f "$root/build/win386/WIN386.EXE=WINDOWS/SYSTEM/WIN386.EXE" > "$out/win386hdd-w16.txt"
        grep '^OBS [^~]' "$out/win386hdd-ref.txt" > "$out/win386hdd-ref.obs" || true
        grep '^OBS [^~]' "$out/win386hdd-w16.txt" > "$out/win386hdd-w16.obs" || true
        r=ok
        if ! grep -q '^INITDONE=Final USER' "$out/win386hdd-w16.txt"; then
            r="Windows did not start under w16's WIN386 on MS-DOS 4.00 ($out/win386hdd-w16.run)"
        elif ! grep -q '^VX11 done' "$out/win386hdd-w16.txt"; then
            r="VX11 did not finish ($out/win386hdd-w16.txt)"
        elif ! grep -q '^OBS io.61.data=0001' "$out/win386hdd-w16.obs"; then
            r="440Dh 61h read no data ($out/win386hdd-w16.obs)"
        elif [ ! -s "$out/win386hdd-ref.obs" ]; then
            r="no reference observations ($out/win386hdd-ref.txt)"
        elif ! cmp -s "$out/win386hdd-ref.obs" "$out/win386hdd-w16.obs"; then
            r="$(diff "$out/win386hdd-ref.obs" "$out/win386hdd-w16.obs" | grep -c '^[<>]') observation line(s) differ from 3.1's ($out/win386hdd-ref.obs, win386hdd-w16.obs)"
        fi
        if [ "$r" = ok ]; then echo "ok   win386hdd: Windows on MS-DOS 4.00 under w16's WIN386, VX11 (440Dh track I/O too) as under 3.1's"
        else echo "FAIL win386hdd: $r"; fails=$((fails + 1)); fi ;;
    win386woa)
        # w16's WINOA386.MOD (and WIN386) on Windows 3.1 against Microsoft's
        # (the same references as win386switch and win386exitbox)
        k386=$root/.tools/ref/win31/cdrive/WINDOWS/SYSTEM/KRNL386.EXE
        r=ok
        for c in win386switch win386exitbox; do
            if [ $c = win386switch ]; then
                scr=$root/tests/scripts/input/dosbox386s.txt
                shots="01-full 02-alttab 03-back 04-altesc 05-back 06-ctrlesc 07-esc 08-box 09-end"
            else
                scr=$root/tests/scripts/input/dosbox386x.txt
                shots="01-full 02-windows 03-exitbox 04-refused 05-after 06-box 07-end"
            fi
            set -- -r -p 386 -c "WIN /3 /B" -f "$k386=WINDOWS/SYSTEM/KRNL386.EXE"
            refrun $c-ref $c env DROPIN_NOREDIR=1 INPUTRUN_KEYDELAY=150 INPUTRUN_STAY=1 \
                "$root/tests/inputrun.sh" -s -b 200 -t 300 "$scr" "$out/$c-ref" "$@"
            DROPIN_NOREDIR=1 INPUTRUN_KEYDELAY=150 INPUTRUN_STAY=1 "$root/tests/inputrun.sh" -s -b 200 -t 300 "$scr" \
                "$out/win386woa-$c" "$@" -f "$root/build/win386/WIN386.EXE=WINDOWS/SYSTEM/WIN386.EXE" \
                -f "$root/build/dos/WINOA386.MOD=WINDOWS/SYSTEM/WINOA386.MOD" > "$out/win386woa-$c.txt" 2>&1 || true
            for s in $shots; do
                a=$out/win386woa-$c/$s.png b=$out/$c-ref/$s.png
                if [ ! -f "$a" ] || [ ! -f "$b" ]; then r="$c: no $s screen"; break; fi
                case $(magick identify -format '%wx%h' "$b") in
                720x400) m="rectangle 72,48 655,79 rectangle 99,0 107,399" ;;
                # the icon (w16's artwork), the "still active" box (its
                # width follows the branded text)
                *) m="rectangle 66,40 590,114 rectangle 96,140 104,215 rectangle 0,400 90,479 rectangle 130,160 520,320" ;;
                esac
                magick "$a" -fill black -draw "$m" "$out/win386woa-a.png"
                magick "$b" -fill black -draw "$m" "$out/win386woa-b.png"
                n=$(magick compare -metric AE "$out/win386woa-a.png" "$out/win386woa-b.png" "$out/win386woa-$c-$s-diff.png" 2>&1 >/dev/null || true)
                n=${n%%[. ]*}
                [ "${n:-1}" = 0 ] || { r="$c $s: $n pixels differ ($out/win386woa-$c-$s-diff.png)"; break; }
            done
            [ "$r" = ok ] || break
        done
        if [ "$r" = ok ]; then echo "ok   win386woa: w16's WINOA386 switching and Exit Windows with a box open, as Microsoft's"
        else echo "FAIL win386woa: $r"; fails=$((fails + 1)); fi ;;
    win386tree)
        # the user's report (2026-11-20): with w16's tree, WIN386, VGA.DRV
        # and VGA.3GR, Microsoft's WINOA386.MOD added, a DOS box did not go
        # full screen (from a window it became an icon, the display stayed
        # Windows': SHELL function 6, win386/vmm/shell.c), and Windows went
        # on drawing over a full-screen box (VGA.DRV ignored INT 2Fh
        # 4001h/4002h: drivers/vga/vga.c scr_switch). 01-full and 03-full
        # must be text screens (720x400) equal to Microsoft's WIN386's but
        # for the banner and the cursor, 02-window and 04-back Windows
        # screens equal to them (04-back: repainted, no trace of the box)
        scr=$root/tests/scripts/input/dosbox386tree.txt
        set -- -p 386 -T "$root/build/w16" -3 -c "WIN /3 /B"
        for f in WINOA386.MOD EGA80WOA.FON EGA40WOA.FON CGA80WOA.FON CGA40WOA.FON; do
            set -- "$@" -f "$root/.tools/ref/win31/cdrive/WINDOWS/SYSTEM/$f=W16/SYSTEM/$f"
        done
        refrun win386tree-ref win386tree env DROPIN_NOREDIR=1 INPUTRUN_KEYDELAY=150 INPUTRUN_STAY=1 \
            "$root/tests/inputrun.sh" -s -b 200 -t 300 "$scr" "$out/win386tree-ref" "$@" \
            -f "$root/.tools/ref/win31/cdrive/WINDOWS/SYSTEM/WIN386.EXE=W16/SYSTEM/WIN386.EXE"
        DROPIN_NOREDIR=1 INPUTRUN_KEYDELAY=150 INPUTRUN_STAY=1 "$root/tests/inputrun.sh" -s -b 200 -t 300 "$scr" \
            "$out/win386tree-w16" "$@" > "$out/win386tree-w16.txt" 2>&1 || true
        r=ok
        for s in 01-full 02-window 03-full 04-back; do
            a=$out/win386tree-w16/$s.png b=$out/win386tree-ref/$s.png
            if [ ! -f "$a" ] || [ ! -f "$b" ]; then r="no $s screen"; break; fi
            g=$(magick identify -format '%wx%h' "$a")
            # masked: the banner's "w16"/"Windows" rows; the text screens'
            # prompt row (the cursor's blink), the window's caret
            case $s in
            0?-full) [ "$g" = 720x400 ] || { r="$s: not a text screen ($g)"; break; }
                     m="rectangle 72,48 655,79 rectangle 0,176 719,191" ;;
            *) [ "$g" = 640x480 ] || { r="$s: not Windows ($g)"; break; }
               m="rectangle 66,40 590,114 rectangle 56,150 80,175" ;;
            esac
            magick "$a" -fill black -draw "$m" "$out/win386tree-a.png"
            magick "$b" -fill black -draw "$m" "$out/win386tree-b.png"
            n=$(magick compare -metric AE "$out/win386tree-a.png" "$out/win386tree-b.png" "$out/win386tree-$s-diff.png" 2>&1 >/dev/null || true)
            n=${n%%[. ]*}
            [ "${n:-1}" = 0 ] || { r="$s: $n pixels differ ($out/win386tree-$s-diff.png)"; break; }
        done
        if [ "$r" = ok ]; then echo "ok   win386tree: the tree's DOS box full screen, windowed, full screen, EXIT: as under Microsoft's WIN386"
        else echo "FAIL win386tree: $r ($out/win386tree-w16)"; fails=$((fails + 1)); fi ;;
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
                    magick "$f" -fill black -draw "$clkmask" "$out/.clk-a.png"
                    magick "$2/$sn" -fill black -draw "$clkmask" "$out/.clk-b.png"
                    n=$(magick compare -metric AE "$out/.clk-b.png" "$out/.clk-a.png" "$out/$3-${sn%.png}-diff.png" 2>&1 >/dev/null || true)
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
            # Desktop: the cursor blink sample blinks, and once the bar
            # has moved its focused thumb's caret blinks at the new rate
            # (as on 3.1, 2026-11-22): both left out of the screens
            clkmask="rectangle 0,0 0,0"
            [ "$ap" = dk ] && clkmask="rectangle 205,346 332,366 rectangle 339,334 348,356"
            case $st in
            tree)
                set -- -T "$root/build/w16" -i 'WIN.INI:windows:run=CONTROL.EXE' \
                    -i 'WIN.INI:windows:MouseSpeed=0'
                rm -rf "$out/cplclick-$ap-t286" "$out/cplclick-$ap-t386"
                "$root/tests/inputrun.sh" -s -b 40 -t 300 "$s" "$out/cplclick-$ap-t286" "$@" \
                    > "$out/cplclick-$ap-t286.txt" 2>&1 || true
                "$root/tests/inputrun.sh" -e "$out/cplclick-$ap-t286/boot.png" -E "$out/cplclick-$ap-t286" \
                    -M "$clkmask" -b 40 -t 300 "$s" "$out/cplclick-$ap-t386" "$@" -3 > "$out/cplclick-$ap-t386.txt" 2>&1 || true
                clkcmp "$out/cplclick-$ap-t286" "$out/cplclick-$ap-t386" "$pk" 1 W16 ;;
            *)
                cp "$root/.tools/ref/win31/cdrive/WINDOWS/WIN.INI" "$out/cplclick-$ap-WIN.INI"
                python3 "$root/tests/scripts/iniset.py" "$out/cplclick-$ap-WIN.INI" 'windows:run=CONTROL.EXE' \
                    'windows:MouseSpeed=0'
                set -- -f "$out/cplclick-$ap-WIN.INI=WINDOWS/WIN.INI"
                w16cp="-f $root/build/dos/CONTROL.EXE=WINDOWS/CONTROL.EXE -f $root/build/dos/MAIN.CPL=WINDOWS/SYSTEM/MAIN.CPL"
                if [ "$st" = us ]; then
                    # shellcheck disable=SC2086
                    refrun cplclick-$ap-ref cplclick "$root/tests/inputrun.sh" -s -b 25 -t 300 "$s" \
                        "$out/cplclick-$ap-ref" -r "$@" $w16cp
                    rm -rf "$out/cplclick-$ap-us"
                    # shellcheck disable=SC2086
                    "$root/tests/inputrun.sh" -e "$out/cplclick-$ap-ref/boot.png" -E "$out/cplclick-$ap-ref" \
                        -M "$clkmask" -b 25 -t 300 "$s" "$out/cplclick-$ap-us" -r "$@" $w16cp \
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
    writefonts)
        s=$root/tests/scripts/input/writefonts.txt
        r=ok
        # the TrueType and device pictures of the face list (w16's own);
        # the sample, Arial drawn by w16's rasteriser, in the first shot
        wfmask="rectangle 49,171 67,236"
        wfcmp() {   # wfcmp REFDIR W16DIR NAME [MASK]
            for f in "$1"/[0-9]*.png; do
                sn=$(basename "$f")
                [ -f "$2/$sn" ] || { r="$3: no $sn"; return; }
                d="point 639,479"
                if [ -n "${4:-}" ]; then
                    d="$d $4"
                    [ "$sn" = 02-fonts.png ] && d="$d rectangle 204,263 478,323"
                fi
                magick "$f" -fill black -draw "$d" "$out/writefonts-$3-a.png"
                magick "$2/$sn" -fill black -draw "$d" "$out/writefonts-$3-b.png"
                n=$(magick compare -metric AE "$out/writefonts-$3-b.png" "$out/writefonts-$3-a.png" "$out/writefonts-$3-${sn%.png}-diff.png" 2>&1 >/dev/null || true)
                n=${n%% *}
                [ "$n" = 0 ] || { r="$3: $sn: $n pixels differ"; return; }
            done
            ls "$1"/[0-9]*.png > /dev/null 2>&1 || r="$3: no screens"
        }
        for pk in ${part:-ms w16 tree}; do
            case $pk in
            tree)
                tref=$root/tests/scripts/input/writefonts-tree
                rm -rf "$out/writefonts-tree" "$out/writefonts-tref"
                "$root/tests/inputrun.sh" -s -b 60 -t 300 "$s" "$out/writefonts-tree" -T "$root/build/w16" -3 \
                    -i 'WIN.INI:windows:run=WRITE.EXE' -i 'WIN.INI:windows:CursorBlinkRate=65535' \
                    > "$out/writefonts-tree.txt" 2>&1 || true
                if [ "${WRFONTS_TREEREF:-}" = update ]; then
                    mkdir -p "$tref"
                    cp "$out/writefonts-tree/02-fonts.png" "$out/writefonts-tree/03-courier.png" "$tref/"
                fi
                mkdir -p "$out/writefonts-tref"
                cp "$out/writefonts-tree/02-fonts.png" "$out/writefonts-tree/03-courier.png" "$out/writefonts-tref/" 2>/dev/null || true
                wfcmp "$tref" "$out/writefonts-tref" tree ;;
            *)
                cp "$root/.tools/ref/win31/cdrive/WINDOWS/WIN.INI" "$out/writefonts-WIN.INI"
                python3 "$root/tests/scripts/iniset.py" "$out/writefonts-WIN.INI" 'windows:run=WRITE.EXE' 'windows:CursorBlinkRate=65535'
                refrun writefonts-ref writefonts "$root/tests/inputrun.sh" -s -b 25 -t 240 "$s" "$out/writefonts-ref" -r \
                    -f "$out/writefonts-WIN.INI=WINDOWS/WIN.INI"
                set -- -r -f "$out/writefonts-WIN.INI=WINDOWS/WIN.INI" -f "$root/build/dos/WRITE.EXE=WINDOWS/WRITE.EXE"
                m=
                if [ $pk = w16 ]; then
                    set -- "$@" -f "$root/build/dos/GDI.EXE=WINDOWS/SYSTEM/GDI.EXE" \
                        -f "$root/build/dos/COMMDLG.DLL=WINDOWS/SYSTEM/COMMDLG.DLL" \
                        -f "$root/build/dos/USER.EXE=WINDOWS/SYSTEM/USER.EXE"
                    m=$wfmask
                fi
                rm -rf "$out/writefonts-$pk"
                "$root/tests/inputrun.sh" -e "$out/writefonts-ref/boot.png" -E "$out/writefonts-ref" ${m:+-M "$m"} \
                    -b 25 -t 240 "$s" "$out/writefonts-$pk" "$@" > "$out/writefonts-$pk.txt" 2>&1 || true
                wfcmp "$out/writefonts-ref" "$out/writefonts-$pk" "$pk" "$m" ;;
            esac
            [ "$r" = ok ] || break
        done
        if [ -n "$part" ]; then partdone "$r"
        elif [ "$r" = ok ]; then echo "ok   writefonts: 0 pixels"
        else echo "FAIL writefonts: $r ($out)"; fails=$((fails + 1)); fi ;;
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
        # error mode 0 and 8000h, OpenFile with OF_PROMPT) under both of
        # w16's kernels against Microsoft's KRNL386: every box (shots)
        # and every return code
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
        term)
            # the termination dialog: DdeUninitialize with a partner that
            # never answers, Retry, then Close (tests/ddeml/dmshot.sh on a
            # virtual X display; tests/ddeml/termcmp.py: when the dialogs
            # come, the return, the dialog's picture)
            d=$out/ddeml-term
            for l in ms w16; do
                rm -rf "$d-$l"
                "$root/tests/ddeml/dmshot.sh" xterm "$root/tests/ddeml/scripts/xtermr.in" \
                    "$d-$l" "$l" 240 > "$d-$l.txt" 2>&1 || true
            done
            python3 "$root/tests/ddeml/termcmp.py" "$d-ms" "$d-w16" > "$d.txt" 2>&1 ||
                r="term: $(grep -m1 'DIFFERENT\|differs\|expected' "$d.txt" || echo 'no result') ($d.txt)" ;;
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
    nametable|dlgfocus)
        # a probe as the shell on 3.1 and with w16's KERNEL or USER: the
        # logs (TK.LOG) must be equal
        r=ok
        if [ "$c" = nametable ]; then pr=TK74 p=${part:-k286}; else pr=US37 p=${part:-user}; fi
        # pbrun NAME [dropin.sh options]: the probe's TK.LOG in NAME.log
        pbrun() {
            _pn=$1; shift
            rm -rf "$out/$_pn"
            "$root/tests/dropin.sh" -t 120 -o "$out/$_pn" -f "$root/build/dos/$pr.EXE=$pr.EXE" \
                -i "boot:shell=C:\\$pr.EXE" -W WINDOWS/TK.LOG "$@" > "$out/$_pn.out" 2>&1 || true
            tr -d '\r' < "$out/$_pn/c/WINDOWS/TK.LOG" > "$out/$_pn.log" 2>/dev/null || : > "$out/$_pn.log"
            rm -rf "$out/$_pn/c"
        }
        case $p in
        k386) set -- -3 ;;
        user) set -- -r -f "$user=WINDOWS/SYSTEM/USER.EXE" ;;
        *) set -- ;;
        esac
        pbrun "$c-$p-ref" -r
        pbrun "$c-$p-w16" "$@"
        for l in ref w16; do
            grep -q "^$pr end" "$out/$c-$p-$l.log" || { r="$p: $pr did not end ($l: $out/$c-$p-$l.log)"; break; }
        done
        if [ "$r" = ok ] && ! diff "$out/$c-$p-ref.log" "$out/$c-$p-w16.log" > "$out/$c-$p.diff"; then
            r="$p: $(grep -m1 '^>' "$out/$c-$p.diff") ($out/$c-$p.diff)"
        fi
        if [ -n "$part" ]; then partdone "$r"
        elif [ "$r" = ok ]; then echo "ok   $c"
        else echo "FAIL $c: $r ($out)"; fails=$((fails + 1)); fi ;;
    installers)
        # third-party installers on the install tree against 3.1
        # (tests/installers.sh); skipped where $W16_INSTDIR lacks them
        p=${part:-WEP1}
        "$root/tests/installers.sh" "$out/installers-$p" "$p" > "$out/installers-$p.txt" 2>&1 || true
        if grep -q "^PASS $p" "$out/installers-$p.txt"; then r=ok
        elif grep -q "^skip $p" "$out/installers-$p.txt"; then r=ok
        else r="$p: $(grep -m1 "^FAIL" "$out/installers-$p.txt" || echo 'no result') ($out/installers-$p.txt)"; fi
        if [ -n "$part" ]; then partdone "$r"
        elif [ "$r" = ok ]; then echo "ok   installers"
        else echo "FAIL installers: $r ($out)"; fails=$((fails + 1)); fi ;;
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
        enh) qto="-T 386 -E" ;;
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
    combo)
        # a combo box's layout (tests/apps/us34.c)
        cb34() {   # LABEL dropin.sh options...: US34's run, its log in LABEL.log
            _l=$1; shift
            "$root/tests/dropin.sh" -t 300 -W TK.LOG -o "$out/$_l" \
                -f "$root/build/dos/US34.EXE=US34.EXE" -i 'boot:shell=C:\US34.EXE' -r "$@" \
                > "$out/$_l.txt" 2>&1 || true
            : > "$out/$_l.log"
            for _d in "$out/$_l/c/WINDOWS" "$out/$_l/c"; do
                [ -f "$_d/TK.LOG" ] && { tr -d '\r' < "$_d/TK.LOG" > "$out/$_l.log"; break; }
            done
        }
        DROPIN_WSECS=30 cb34 combo-ref &
        DROPIN_WSECS=30 cb34 combo-w16 -f "$user=WINDOWS/SYSTEM/USER.EXE" &
        DROPIN_WSECS=30 cb34 combo-gdi -f "$user=WINDOWS/SYSTEM/USER.EXE" -f "$gdi=WINDOWS/SYSTEM/GDI.EXE" &
        wait
        r=ok
        for k in ref w16 gdi; do
            grep -q '^US34 end' "$out/combo-$k.log" ||
                { [ "$r" = ok ] && r="US34 did not finish ($k: $out/combo-$k.log)"; }
        done
        # still different, left out (ARCHITECTURE.md, US34): a drop-down
        # box's text wider than its field, unfocused (font 7); 6-pixel
        # Small Fonts boxes 18 high (font 5); a simple box given a second
        # font (2to4); a simple box's pixels right after CB_SETITEMHEIGHT
        cskip='^OBS (w\.0\.[0-9]+\.7\.[ec]?sum|w\.[0-9]\.18\.5\.|w\.2\.[0-9]+\.2to4|sih\.103\.i0h\.e?sum)'
        if [ "$r" = ok ]; then
            grep '^OBS [^~]' "$out/combo-ref.log" | grep -Ev "$cskip" > "$out/combo-ref.obs"
            grep '^OBS [^~]' "$out/combo-w16.log" | grep -Ev "$cskip" > "$out/combo-w16.obs"
            cmp -s "$out/combo-ref.obs" "$out/combo-w16.obs" ||
                r="$(diff "$out/combo-ref.obs" "$out/combo-w16.obs" | grep -c '^<') observation(s) differ from 3.1's ($out/combo-ref.obs, combo-w16.obs)"
        fi
        if [ "$r" = ok ]; then
            # w16's GDI too: all but the checksums, whose glyphs are w16's,
            # and the TrueType Arial fonts (6, 7), whose metrics are w16's
            tt='^OBS (font\.[67]|w\.[0-9]\.[0-9]+\.[67][.=])'
            grep -v 'sum=' "$out/combo-ref.obs" | grep -Ev "$tt" > "$out/combo-ref.nosum"
            grep '^OBS [^~]' "$out/combo-gdi.log" | grep -Ev "$cskip" | grep -Ev "$tt" | grep -v 'sum=' > "$out/combo-gdi.nosum"
            cmp -s "$out/combo-ref.nosum" "$out/combo-gdi.nosum" ||
                r="w16 USER+GDI: $(diff "$out/combo-ref.nosum" "$out/combo-gdi.nosum" | grep -c '^<') observation(s) differ from 3.1's ($out/combo-ref.nosum, combo-gdi.nosum)"
        fi
        if [ "$r" = ok ]; then
            # the screens it saved (C:\US34_nn.BMP), byte for byte: all
            # but 08 (the wide text unfocused) and 15 (a simple box given
            # a second font), left out above too
            n=0
            for b in "$out/combo-ref/c"/US34_*.BMP; do
                f=${b##*/}
                case $f in US34_08.BMP|US34_15.BMP) continue ;; esac
                n=$((n + 1))
                cmp -s "$b" "$out/combo-w16/c/$f" ||
                    { r="screen $f differs from 3.1's ($out/combo-ref/c, combo-w16/c)"; break; }
            done
            [ "$r" = ok ] && [ "$n" -lt 30 ] && r="only $n screens saved"
        fi
        if [ "$r" = ok ]; then echo "ok   combo: US34 as 3.1 (combo boxes' heights, fields, lists and pixels in five fonts and in dialogs; w16 USER, and USER+GDI)"
        else echo "FAIL combo: $r"; fails=$((fails + 1)); fi ;;
    menutab)
        # menu accelerators after '\t' and 08h (tests/apps/us38.c)
        mt38() {   # LABEL dropin.sh options...: US38's run, its log in LABEL.log
            _l=$1; shift
            "$root/tests/dropin.sh" -t 200 -W TK.LOG -o "$out/$_l" \
                -f "$root/build/dos/US38.EXE=US38.EXE" -i 'boot:shell=C:\US38.EXE' -r "$@" \
                > "$out/$_l.txt" 2>&1 || true
            : > "$out/$_l.log"
            for _d in "$out/$_l/c/WINDOWS" "$out/$_l/c"; do
                [ -f "$_d/TK.LOG" ] && { tr -d '\r' < "$_d/TK.LOG" > "$out/$_l.log"; break; }
            done
        }
        DROPIN_WSECS=30 mt38 menutab-ref &
        DROPIN_WSECS=30 mt38 menutab-w16 -f "$user=WINDOWS/SYSTEM/USER.EXE" &
        wait
        r=ok
        for k in ref w16; do
            grep -q '^US38 end' "$out/menutab-$k.log" ||
                { [ "$r" = ok ] && r="US38 did not finish ($k: $out/menutab-$k.log)"; }
        done
        if [ "$r" = ok ]; then
            grep '^OBS ' "$out/menutab-ref.log" > "$out/menutab-ref.obs"
            grep '^OBS ' "$out/menutab-w16.log" > "$out/menutab-w16.obs"
            cmp -s "$out/menutab-ref.obs" "$out/menutab-w16.obs" ||
                r="$(diff "$out/menutab-ref.obs" "$out/menutab-w16.obs" | grep -c '^<') observation(s) differ from 3.1's ($out/menutab-ref.obs, menutab-w16.obs)"
            grep -q '^FAIL' "$out/menutab-w16.log" && [ "$r" = ok ] &&
                r="$(grep -c '^FAIL' "$out/menutab-w16.log") FAIL line(s) ($out/menutab-w16.log)"
        fi
        if [ "$r" = ok ]; then
            # the screens it saved, pixel for pixel (not byte for byte:
            # GetDIBits leaves the padding bits at a row's end as they were)
            n=0
            for b in "$out/menutab-ref/c"/US38_*.BMP; do
                f=${b##*/}
                n=$((n + 1))
                px=$(magick compare -metric AE "$b" "$out/menutab-w16/c/$f" "$out/menutab-${f%.BMP}-diff.png" 2>&1 >/dev/null || true)
                [ "${px%% *}" = 0 ] ||
                    { r="screen $f differs from 3.1's by ${px%% *} pixel(s) ($out/menutab-${f%.BMP}-diff.png)"; break; }
                rm -f "$out/menutab-${f%.BMP}-diff.png"
            done
            [ "$r" = ok ] && [ "$n" -lt 23 ] && r="only $n screens saved"
        fi
        if [ "$r" = ok ]; then echo "ok   menutab: US38 as 3.1 (accelerators after tabs and 08h in popups and bars, ampersands, GetMenuString; 23 screens)"
        else echo "FAIL menutab: $r"; fails=$((fails + 1)); fi ;;
    editwrap)
        # a multi-line edit control's word wrap and undo (tests/apps/us35.c)
        ew35() {   # LABEL dropin.sh options...: US35's run, its log in LABEL.log
            _l=$1; shift
            "$root/tests/dropin.sh" -t 400 -W TK.LOG -o "$out/$_l" \
                -f "$root/build/dos/US35.EXE=US35.EXE" -i 'boot:shell=C:\US35.EXE' -r "$@" \
                > "$out/$_l.txt" 2>&1 || true
            : > "$out/$_l.log"
            for _d in "$out/$_l/c/WINDOWS" "$out/$_l/c"; do
                [ -f "$_d/TK.LOG" ] && { tr -d '\r' < "$_d/TK.LOG" > "$out/$_l.log"; break; }
            done
        }
        DROPIN_WSECS=30 ew35 editwrap-ref &
        DROPIN_WSECS=30 ew35 editwrap-w16 -f "$user=WINDOWS/SYSTEM/USER.EXE" &
        DROPIN_WSECS=30 ew35 editwrap-gdi -f "$user=WINDOWS/SYSTEM/USER.EXE" -f "$gdi=WINDOWS/SYSTEM/GDI.EXE" &
        wait
        r=ok
        for k in ref w16 gdi; do
            grep -q '^US35 end' "$out/editwrap-$k.log" ||
                { [ "$r" = ok ] && r="US35 did not finish ($k: $out/editwrap-$k.log)"; }
        done
        # still different, left out (ARCHITECTURE.md, US35): text with a
        # lone CR or LF (text 4); how far ES_AUTOHSCROLL scrolls right
        # (autoh, autohw); WM_HSCROLL's page and thumb in a control with a
        # horizontal bar (scrollh 3-5); Page Down with the caret above the
        # view (scroll 15, 16); EM_SETWORDBREAKPROC's answer (3.1's not 0,
        # what it is unknown)
        ewskip='^OBS (w\.[0-9]\.[0-9]+\.4\.|autohw?\.|scrollh\.[345]=|scroll\.1[56]=|wb[a-z]*\.[12]=Y )'
        if [ "$r" = ok ]; then
            grep '^OBS [^~]' "$out/editwrap-ref.log" | grep -Ev "$ewskip" > "$out/editwrap-ref.obs"
            grep '^OBS [^~]' "$out/editwrap-w16.log" | grep -Ev "$ewskip" > "$out/editwrap-w16.obs"
            cmp -s "$out/editwrap-ref.obs" "$out/editwrap-w16.obs" ||
                r="$(diff "$out/editwrap-ref.obs" "$out/editwrap-w16.obs" | grep -c '^<') observation(s) differ from 3.1's ($out/editwrap-ref.obs, editwrap-w16.obs)"
        fi
        if [ "$r" = ok ]; then
            # w16's GDI too: all but the checksums (".sum=", the scripts'
            # Q steps), whose glyphs are w16's
            ewsum='\.sum=|^OBS [a-z0-9]+\.[0-9]+=Q '
            grep -Ev "$ewsum" "$out/editwrap-ref.obs" > "$out/editwrap-ref.nosum"
            grep '^OBS [^~]' "$out/editwrap-gdi.log" | grep -Ev "$ewskip" | grep -Ev "$ewsum" > "$out/editwrap-gdi.nosum"
            cmp -s "$out/editwrap-ref.nosum" "$out/editwrap-gdi.nosum" ||
                r="w16 USER+GDI: $(diff "$out/editwrap-ref.nosum" "$out/editwrap-gdi.nosum" | grep -c '^<') observation(s) differ from 3.1's ($out/editwrap-ref.nosum, editwrap-gdi.nosum)"
        fi
        if [ "$r" = ok ]; then echo "ok   editwrap: US35 as 3.1 (word wrap, EM_FMTLINES, the caret, scrolling, the word-break procedure, undo; w16 USER, and USER+GDI)"
        else echo "FAIL editwrap: $r"; fails=$((fails + 1)); fi ;;
    edit32k)
        # text past 32767 characters (tests/apps/us36.c)
        e36() {   # LABEL dropin.sh options...: US36's run, its log in LABEL.log
            _l=$1; shift
            "$root/tests/dropin.sh" -t 300 -W TK.LOG -o "$out/$_l" \
                -f "$root/build/dos/US36.EXE=US36.EXE" -i 'boot:shell=C:\US36.EXE' -r "$@" \
                > "$out/$_l.txt" 2>&1 || true
            : > "$out/$_l.log"
            for _d in "$out/$_l/c/WINDOWS" "$out/$_l/c"; do
                [ -f "$_d/TK.LOG" ] && { tr -d '\r' < "$_d/TK.LOG" > "$out/$_l.log"; break; }
            done
        }
        DROPIN_WSECS=30 e36 edit32k-ref &
        DROPIN_WSECS=30 e36 edit32k-w16 -f "$user=WINDOWS/SYSTEM/USER.EXE" &
        wait
        r=ok
        for k in ref w16; do
            grep -q '^US36 end' "$out/edit32k-$k.log" ||
                { [ "$r" = ok ] && r="US36 did not finish ($k: $out/edit32k-$k.log)"; }
        done
        if [ "$r" = ok ]; then
            grep '^OBS [^~]' "$out/edit32k-ref.log" > "$out/edit32k-ref.obs"
            grep '^OBS [^~]' "$out/edit32k-w16.log" > "$out/edit32k-w16.obs"
            cmp -s "$out/edit32k-ref.obs" "$out/edit32k-w16.obs" ||
                r="$(diff "$out/edit32k-ref.obs" "$out/edit32k-w16.obs" | grep -c '^<') observation(s) differ from 3.1's ($out/edit32k-ref.obs, edit32k-w16.obs)"
        fi
        if [ "$r" = ok ]; then echo "ok   edit32k: US36 as 3.1 (text counts and positions past 32767: GetWindowText, WM_GETTEXT, EM_SETSEL, the line messages, changes; w16 USER)"
        else echo "FAIL edit32k: $r"; fails=$((fails + 1)); fi ;;
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
    pmarrange)
        # Arrange Icons in a group and of the group icons, a click on an
        # icon, Windows ended with Save Settings on Exit and started again
        # (INPUTRUN_STAY: the script types WIN at the DOS prompt):
        # Microsoft's Program Manager on w16's USER and GDI pixel for
        # pixel; w16's on them with the pictures of its own group and
        # program icons left out (the icon row and Program Manager's own
        # icon); the group files and PROGMAN.INI identical to all-
        # Microsoft's in both
        scr=$root/tests/scripts/input/pmarrange.txt
        m="rectangle 60,305 460,342"
        refrun pmarrange-ref pmarrange env INPUTRUN_STAY=1 "$root/tests/inputrun.sh" -s -b 40 -t 300 "$scr" "$out/pmarrange-ref" -r
        INPUTRUN_STAY=1 "$root/tests/inputrun.sh" -e "$out/pmarrange-ref/boot.png" -E "$out/pmarrange-ref" -b 40 -t 300 "$scr" "$out/pmarrange-us" -r \
            -f "$user=WINDOWS/SYSTEM/USER.EXE" -f "$gdi=WINDOWS/SYSTEM/GDI.EXE" > "$out/pmarrange-us.txt" 2>&1 || true
        INPUTRUN_STAY=1 "$root/tests/inputrun.sh" -s -E "$out/pmarrange-ref" -M "$m" -b 40 -t 300 "$scr" "$out/pmarrange-pm" -r \
            -f "$user=WINDOWS/SYSTEM/USER.EXE" -f "$gdi=WINDOWS/SYSTEM/GDI.EXE" \
            -f "$root/build/dos/PROGMAN.EXE=WINDOWS/PROGMAN.EXE" > "$out/pmarrange-pm.txt" 2>&1 || true
        r=ok
        win=work/c/WINDOWS
        grep -q '^script done' "$out/pmarrange-ref/input.log" 2>/dev/null || r="the reference's script did not run to its end"
        [ "$r" = ok ] && cmp -s "$root/.tools/ref/win31/cdrive/WINDOWS/GAMES.GRP" "$out/pmarrange-ref/$win/GAMES.GRP" && r="the reference saved no icon place"
        for k in us pm; do
            [ "$r" = ok ] || break
            grep -q '^script done' "$out/pmarrange-$k/input.log" 2>/dev/null || { r="$k: the script did not run to its end"; break; }
            for f in MAIN.GRP ACCESSOR.GRP GAMES.GRP STARTUP.GRP PROGMAN.INI; do
                cmp -s "$out/pmarrange-ref/$win/$f" "$out/pmarrange-$k/$win/$f" || { r="$k: $f differs"; break; }
            done
            [ "$r" = ok ] || break
            for f in "$out/pmarrange-ref"/[0-9]*.png; do
                s=$(basename "$f")
                [ -f "$out/pmarrange-$k/$s" ] || { r="$k: no $s"; break; }
                mk="rectangle 0,0 0,0"
                # w16's pictures: the group icons, the dragged one's
                # pointer, Program Manager's own icon after the restart
                [ $k = pm ] && mk="$m rectangle 0,400 70,445"
                magick "$f" -fill black -draw "$mk" "$out/pmarrange-a.png"
                magick "$out/pmarrange-$k/$s" -fill black -draw "$mk" "$out/pmarrange-b.png"
                n=$(magick compare -metric AE "$out/pmarrange-b.png" "$out/pmarrange-a.png" "$out/pmarrange-$k-${s%.png}-diff.png" 2>&1 >/dev/null || true)
                n=${n%% *}
                [ "$n" = 0 ] || { r="$k: $s: $n pixels differ"; break; }
            done
        done
        if [ "$r" = ok ]; then echo "ok   pmarrange: arranged items and group icons saved, icon menu on a click; files identical (w16 USER+GDI; and w16 PROGMAN)"
        else echo "FAIL pmarrange: $r ($out)"; fails=$((fails + 1)); fi ;;
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
        for s in 00a-pifitem 00b-pifedit 00c-back 01-readme 02-readme-notepad 03-back 04-accessories 05-notepad 06-back 07-calculator 08-back; do
            [ "$r" = ok ] || break
            [ -f "$out/w16groups-ms/$s.png" ] || { r="ms: no $s.png"; break; }
            case $s in 00b*|0[2457]*)
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
    scrnsave)
        # the screen savers (tests/ssscenarios.sh): one scenario a part;
        # tree286, tree386: w16's install tree alone (tests/sstree.sh)
        r=ok
        case $part in
        tree*)
            "$root/tests/sstree.sh" "$out/scrnsave-$part" "${part#tree}" > "$out/scrnsave-$part.txt" 2>&1 ||
                r="$part: $(grep -m1 'sstree:' "$out/scrnsave-$part.txt" || echo 'no result') ($out/scrnsave-$part)" ;;
        *)
            SS_OUT=$out/scrnsave "$root/tests/ssscenarios.sh" "$part" > "$out/scrnsave-$part.txt" 2>&1 || true
            grep -q "^== $part: ok" "$out/scrnsave-$part.txt" ||
                r="$part: $(grep -v '^==' "$out/scrnsave-$part.txt" | head -3 | tr '\n' ' ') ($out/scrnsave/$part)" ;;
        esac
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
    kbdsnap)
        # Print Screen and Alt+Print Screen with w16's KEYBOARD.DRV (KB2,
        # tests/apps/kb2.c, as the shell, its 400 x 300 window active;
        # tests/scripts/input/kbsnap.txt): the clipboard's CF_BITMAP is the
        # whole screen (640 x 480), then the active window (400 x 300), as
        # on Windows 3.1, and KB2 gets VK_SNAPSHOT's key-ups only, as
        # there. On Microsoft's KRNL286, DOSX and USER (ms: all Microsoft's,
        # which shows the harness sent the keys), then with w16's USER and
        # GDI. In DOSBox-X (-p 386): DOSBox 0.74 sends no
        # Print Screen scan codes
        scr=$root/tests/scripts/input/kbsnap.txt
        want='P 1 640 480|P 2 400 300'
        r=ok
        for k in ms drv usr; do
            set -- -f "$root/build/dos/KB2.EXE=KB2.EXE" -i 'boot:shell=C:\KB2.EXE' -i w16:kb2secs=60
            [ $k = ms ] || set -- "$@" -f "$root/build/dos/KEYBOARD.DRV=WINDOWS/SYSTEM/KEYBOARD.DRV"
            [ $k = usr ] && set -- "$@" -f "$user=WINDOWS/SYSTEM/USER.EXE" -f "$gdi=WINDOWS/SYSTEM/GDI.EXE"
            "$root/tests/inputrun.sh" -b 30 -t 120 "$scr" "$out/kbdsnap-$k" -r -p 386 "$@" > "$out/kbdsnap-$k.txt" 2>&1 || true
            # the bitmaps, and the VK_SNAPSHOT messages KB2 got (3.1: the
            # key-ups only, scan code 01) as all-Microsoft's
            got=$(tr -d '\r' < "$out/kbdsnap-$k/work/c/WINDOWS/TK.LOG" 2>/dev/null | grep -E '^(P |M 010. 002C )' | paste -sd'|' -)
            [ $k = ms ] && want="$want|$(echo "$got" | tr '|' '\n' | grep '^M' | paste -sd'|' -)"
            echo "kbdsnap $k: ${got:-no bitmap}" >> "$out/kbdsnap.log"
            [ "$r" = ok ] && [ "$got" != "$want" ] && r="$k: ${got:-no bitmap}, not $want"
        done
        if [ "$r" = ok ]; then echo "ok   kbdsnap: Print Screen 640x480, Alt+Print Screen 400x300 (w16's KEYBOARD.DRV, Microsoft's and w16's USER)"
        else echo "FAIL kbdsnap: $r ($out/kbdsnap.log)"; fails=$((fails + 1)); fi ;;
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
        # the tree's DOS box in 386 Enhanced mode, nothing of Microsoft's
        # (its WIN386.EXE, WINOA386.MOD, VGA.3GR; no DOS-box fonts: the
        # OEM font): the Main group's MS-DOS Prompt full screen, Alt+Enter
        # to a window and back, EXIT (tests/scripts/input/dosbox386tree.txt,
        # DOSBox-X): text screen, Windows with the window, text screen,
        # Windows again
        treedos() {
            DROPIN_NOREDIR=1 INPUTRUN_KEYDELAY=150 INPUTRUN_STAY=1 "$root/tests/inputrun.sh" -s -b 200 -t 300 \
                "$root/tests/scripts/input/dosbox386tree.txt" "$out/treealone3e-dos" -p 386 -T "$root/build/w16" -3 \
                -c "WIN /3 /B" > "$out/treealone3e-dos.txt" 2>&1 || true
            for _s in 01-full:720x400 02-window:640x480 03-full:720x400 04-back:640x480; do
                _g=$(magick identify -format '%wx%h' "$out/treealone3e-dos/${_s%%:*}.png" 2>/dev/null || echo none)
                [ "$_g" = "${_s#*:}" ] || { echo "${_s%%:*} is $_g, not ${_s#*:}"; return; }
            done
            echo ok
        }
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
        elif r=$(treedos) && [ "$r" != ok ]; then
            echo "FAIL treealone: 386 Enhanced mode's DOS box: $r ($out/treealone3e-dos)"; fails=$((fails + 1))
        else
            echo "ok   treealone: build/w16 alone, no file of Microsoft's: boots to its shell under KRNL286, KRNL386 and in 386 Enhanced mode (w16's WIN386), where its DOS box goes full screen, to a window and back"
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
WRSCEN_BOOT=${WRSCEN_BOOT:-wr24 wr25 wr26 wr28 wr29 wr30 wr30o wr31 wr32 wr33 wr34 wr35 wr36 wr37 wr38}
caseparts() {
    case $1 in
    allw16nolock) seq -f "$1@%g" 1 "${NOLOCK_RUNS:-6}" ;;
    pmdrop) echo "$1@pm $1@sh $1@us" ;;
    sbdrag) echo "$1@us $1@np $1@fast $1@npw" ;;
    edithandle) echo "$1@us $1@np" ;;
    gpuser) echo "$1@u286 $1@u386 $1@t286 $1@t386" ;;
    winfile) echo "$1@cmp $1@all" ;;
    exittree) echo "$1@e386 $1@e386ms $1@std" ;;
    cmdprompt) echo "$1@e386 $1@std" ;;
    swapesc) echo "$1@k386 $1@k286 $1@mswoa" ;;
    runbrowse) echo "$1@cmp $1@all" ;;
    missingdll) echo "$1@pm $1@tk59 $1@tree" ;;
    runassoc) echo "$1@probe $1@pm $1@pmk286 $1@pmk386 $1@tree $1@dir" ;;
    runpe) echo "$1@k286 $1@k386 $1@on286 $1@on386" ;;
    olecli) printf '%s@tree %s@cardw16 ' "$1" "$1"; for v in $(cd "$root/tests/ole/obs" && ls *.txt | sed 's/\.txt$//'); do printf '%s@%s ' "$1" "$v"; done ;;
    olesvr) printf '%s@tree ' "$1"; for v in $(cd "$root/tests/olesvr/scripts" && ls *.os2 | sed 's/\.os2$//'); do printf '%s@%s ' "$1" "$v"; done ;;
    ddeml) printf '%s@tree286 %s@tree386 %s@term ' "$1" "$1" "$1"; for v in $(cd "$root/tests/ddeml/scripts" && ls *.dm | grep -v '^x' | sed 's/\.dm$//'); do printf '%s@%s ' "$1" "$v"; grep -q '^start ' "$root/tests/ddeml/scripts/$v.dm" && printf '%s@%s+x ' "$1" "$v"; done ;;
    lzexpand) echo "$1@s $1@3 $1@tree286 $1@tree386 $1@quirks" ;;
    ver) echo "$1@s $1@3 $1@x $1@tree286 $1@tree386 $1@mods $1@fonts $1@fontsnew" ;;
    timer) echo "$1@s $1@3 $1@qt" ;;
    quicktime) echo "$1@over $1@tree $1@tree386 $1@enh $1@cpl" ;;
    mmsystem) echo "$1@core $1@mmio $1@timer $1@qt $1@dev $1@mci $1@prog" ;;
    sb16) echo "$1@dev $1@audio $1@prog $1@enh $1@cont" ;;
    bigmem) echo "$1@s128 $1@s256 $1@s256ms $1@e128 $1@e256 $1@stock $1@ref" ;;
    filecdr) echo "$1@k286 $1@k386 $1@userk" ;;
    nametable) echo "$1@k286 $1@k386" ;;
    installers) echo "$1@WEP1 $1@WEP2 $1@WEP3 $1@BW $1@W4 $1@JOHNNY" ;;
    toolhelp) echo "$1@ref286 $1@ref386 $1@ref3 $1@tree286 $1@tree386 $1@watson $1@qt" ;;
    treek386) echo "$1@items $1@desk" ;;
    zipsetup) echo "$1@pm $1@tree" ;;
    ddescen) for v in $(cd "$root/tests/scripts/dde" && ls *.txt | sed 's/\.txt$//'); do printf '%s@%s ' "$1" "$v"; done ;;
    scrnsave) for v in $(cd "$root/tests/scripts/ss" && ls *.txt | sed 's/\.txt$//') tree286 tree386; do printf '%s@%s ' "$1" "$v"; done ;;
    control) echo "$1@ms $1@k286 $1@k386 $1@tree $1@resize" ;;
    writefonts) echo "$1@ms $1@w16 $1@tree" ;;
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
    cmdprompt)
        if [ $pbad = 0 ]; then echo "ok   cmdprompt: Main's Command Prompt (CMD.PIF): a window with the prompt in 386 Enhanced mode, full screen in Standard mode"
        else echo "FAIL cmdprompt: $pfirst ($out)"; fi ;;
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
    nametable)
        if [ $pbad = 0 ]; then echo "ok   nametable: TK74's FindResource through a name table as on 3.1 (w16's KRNL286, KRNL386)"
        else echo "FAIL nametable: $pfirst ($out)"; fi ;;
    installers)
        if [ $pbad = 0 ]; then echo "ok   installers: each installer at hand installed the same files on w16's tree as on 3.1 (those missing skipped)"
        else echo "FAIL installers: $pfirst ($out)"; fi ;;
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
        if [ $pbad = 0 ]; then echo "ok   ddeml: DDEML in the tree under both kernels as with Microsoft's DDEML, the termination dialog as Microsoft's; $((pt - 3)) DDEML scenarios as with Microsoft's DDEML in every client/server combination"
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
