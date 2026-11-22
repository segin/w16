w16 - a Windows directory with w16's programs
=============================================

This directory is laid out like a Windows 3.1 directory (the one that
holds WIN.COM) and contains only what w16 builds (public domain under CC0 1.0, see
the w16 repository's LICENSE):

Start w16 with  WIN /S  (Standard mode) for now. On a 386, plain WIN
starts 386 Enhanced mode, as Windows 3.1's does; w16's 386 Enhanced mode
is newer. Neither mode needs anything of Microsoft's.

  WIN.COM                 the loader
  PROGMAN.EXE             Program Manager
  NOTEPAD.EXE             Notepad
  CLOCK.EXE               Clock
  CALC.EXE                Calculator
  CARDFILE.EXE            Cardfile (its help is CARDFILE.HLP)
  WRITE.EXE               Write (its help is WRITE.HLP)
  CONTROL.EXE             Control Panel
  WINFILE.EXE             File Manager (its help is WINFILE.HLP)
  SSSTARS.SCR             the Starfield Simulation screen saver: choose
                          it in Control Panel's Desktop (Screen Saver,
                          Name), where Setup... sets its warp speed,
                          the number of stars and a password
  REGEDIT.EXE             the Registration Info Editor: file types and
                          the registration database (REGEDIT /V shows
                          all of it; its help is REGEDIT.HLP and
                          REGEDITV.HLP); [w16] RegeditNewStyle=1 in
                          SYSTEM.INI gives it a Windows 95-like Registry
                          Editor view (View > New Style switches)
  PIFEDIT.EXE             the PIF Editor: program information files
                          (.PIF) for DOS programs, their Standard and
                          386 Enhanced mode settings (its help is
                          PIFEDIT.HLP)
  WINHELP.EXE             Help: shows Windows 3.x .HLP files, and the
                          topics programs ask for (their Help menus and
                          F1). The help files themselves are not part of
                          w16: copy them from your own Windows 3.1, or
                          use those that come with your programs
  TASKMAN.EXE             the Task List (Ctrl+Esc, or a double-click
                          on the desktop): switch to, close, cascade or
                          tile the programs' windows, arrange icons
  SYSTEM\KRNL286.EXE      KERNEL for an 80286
  SYSTEM\KRNL386.EXE      KERNEL for a 386 or later (Standard and 386
                          Enhanced mode)
  SYSTEM\USER.EXE         USER (windows, menus, dialogs, messages)
  SYSTEM\GDI.EXE          GDI (graphics)
  SYSTEM\WINOLDAP.MOD     runs DOS programs under w16
  SYSTEM\WSWAP.EXE        the task switcher's w16 side: WIN.COM starts
                          it, it starts DOSX.EXE, and it swaps w16
                          out of memory for a DOS program and back
  SYSTEM\DSWAP.EXE        the task switcher's DOS side: runs a DOS
                          program full screen (Alt+Tab, Alt+Esc and
                          Ctrl+Esc switch away from it)
  SYSTEM\VGA.DRV         the display driver (VGA, 640x480, 16 colours)
  SYSTEM\VGACOLOR.2GR     the display grabber for DOS programs in
                          Standard mode: saves and restores a DOS
                          program's screen when it is switched away
                          from and back to, and copies its text to the
                          clipboard (PrintScreen)
  SYSTEM\VGA.3GR         the display grabber for 386 Enhanced mode
  SYSTEM\WINOA386.MOD    runs DOS programs in 386 Enhanced mode, each
                          in a virtual machine of its own, full screen
                          or in a window (Alt+Enter between them)
  SYSTEM\DOSX.EXE         the Standard-mode DOS extender
  SYSTEM\WIN386.EXE       386 Enhanced mode (WIN /3): the virtual machine
                          manager and its built-in virtual devices; it
                          runs Windows and DOS programs in virtual
                          machines (DOS programs full screen or in a
                          window, Alt+Enter between them)
  SYSTEM\WIN87EM.DLL      floating-point emulation
  SYSTEM\SHELL.DLL        the shell library (drag and drop, ShellExecute,
                          the registration database REG.DAT, icons,
                          About boxes)
  SYSTEM\OLECLI.DLL       the OLE client library: objects (pictures,
                          sounds, packages) embedded in or linked to
                          documents, as Cardfile and Write keep them;
                          editing one needs its program (the server),
                          e.g. your Paintbrush
  SYSTEM\OLESVR.DLL       the OLE server library, which programs such
                          as Paintbrush use to serve those objects
  SYSTEM\DDEML.DLL        the DDE Management Library: conversations,
                          transactions and advise loops between
                          programs through dynamic data exchange, for
                          programs that use DDEML instead of DDE
                          messages
  SYSTEM\LZEXPAND.DLL     the file expansion library: lets programs and
                          installers read and copy files compressed
                          with COMPRESS (the *.??_ files on setup
                          disks)
  SYSTEM\VER.DLL          the version library: reads the version
                          information of programs and libraries, and
                          lets installers copy a file only when it is
                          not older than the one already there
  SYSTEM\TOOLHELP.DLL     the tool helper library, which debuggers and
                          system utilities use: lists of modules,
                          tasks, classes and memory blocks, stack
                          traces, notifications and fault handling
  SYSTEM\COMMDLG.DLL      the common dialogs (Open, Save As, Print,
                          Print Setup, Font, Color, Find, Replace)
  SYSTEM\MOUSE.DRV        the mouse driver, for a PS/2 mouse (with a
                          serial, bus or InPort mouse, use your own
                          Windows 3.1 MOUSE.DRV instead)
  SYSTEM\COMM.DRV         the serial (COM1-COM4) and parallel
                          (LPT1-LPT3) port driver
  SYSTEM\KEYBOARD.DRV     the keyboard driver (US layout, code page
                          437; for another layout or code page use
                          your own Windows 3.1 KEYBOARD.DRV and its
                          KBD*.DLL instead)
  SYSTEM\SYSTEM.DRV       the system driver: timers, the millisecond
                          count, coprocessor state, drive types
                          (for a standard PC/AT; on a machine that
                          needs another system driver, such as
                          HPSYSTEM.DRV, use that instead)
  SYSTEM\MMSOUND.DRV      the sound driver: MessageBeep's beep (the
                          sound WIN.INI [sounds] names for it, through
                          MMSYSTEM.DLL, when a wave driver is installed;
                          else the PC speaker) and the old SOUND
                          functions (voice queues of notes) on the PC
                          speaker
  SYSTEM\TIMER.DRV        the multimedia timer driver: the
                          millisecond timer and timer events behind the
                          time* functions of MMSYSTEM.DLL (timeGetTime,
                          timeSetEvent ...), which programs that play
                          sound, music or video use
  SYSTEM\MMSYSTEM.DLL     the multimedia system library: its timer
                          services, multimedia file I/O (mmio*), the
                          installable-driver and task services, the
                          wave, MIDI and auxiliary sound devices through
                          your sound card's Windows 3.1 drivers,
                          sndPlaySound, and the Media Control Interface
                          (MCI) over your MCI drivers (Sound Recorder,
                          Media Player, Control Panel's Sound and MIDI
                          Mapper, and QuickTime for Windows' MCI driver
                          work)
  SYSTEM\MMTASK.TSK       the multimedia library's helper task
  SYSTEM\SB16.DRV         a Sound Blaster 16 driver: wave output and
                          input (8 and 16 bits, mono and stereo, 5000
                          to 44100 Hz), MIDI out and in through the
                          card's MPU-401 port, and the mixer's volumes
                          (master, wave, MIDI, CD, line) as auxiliary
                          devices; Standard mode only. Enabled in
                          SYSTEM.INI [drivers] (see below)
  SYSTEM\MAIN.CPL         Control Panel's applets: Color, Desktop,
                          Keyboard and Mouse work; Fonts, Ports,
                          Printers, International and Date/Time are not
                          w16's yet
  SYSTEM\VGASYS.FON       System font
  SYSTEM\VGAFIX.FON       Fixedsys font
  SYSTEM\VGAOEM.FON       Terminal font
  SYSTEM\SSERIFE.FON      MS Sans Serif font
  SYSTEM\COURE.FON        Courier font
  SYSTEM\SERIFE.FON       MS Serif font
  SYSTEM\SMALLE.FON       Small Fonts (and MS Serif 6 and 7 point)
  SYSTEM\SYMBOLE.FON      Symbol font
  SYSTEM\DOSAPP.FON       Terminal sizes for DOS windows
  SYSTEM\EGA80WOA.FON, EGA40WOA.FON, CGA80WOA.FON, CGA40WOA.FON
                          the grabber's fonts for DOS windows
  SYSTEM.INI, WIN.INI     w16's default settings (only w16's drivers,
                          fonts and programs)
  CONTROL.INI             Control Panel's colour schemes (w16 Default
                          is built in; Evergreen, Ocean, Plum, Brick,
                          Midnight and High Contrast are w16's own)
  PROGMAN.INI             Program Manager's settings and its groups:
  MAIN.GRP                  Main (DOS Prompt, Read Me, Control
                            Panel, File Manager, Registry Editor, PIF
                            Editor)
  ACCESSOR.GRP              Accessories (Notepad, Cardfile, Calculator,
                            Clock)
  STARTUP.GRP               StartUp (empty)

Main differs from Windows 3.1's on purpose: its prompt is called "DOS
Prompt" (3.1: "MS-DOS Prompt"), and it has the Registry Editor
(REGEDIT.EXE), which 3.1 puts in no group. For 3.1's Main, select the
Registry Editor item and press Del, and rename the prompt with File >
Properties; Program Manager keeps the change in MAIN.GRP.

This directory is meant to be C:\W16: PROGMAN.INI names the group files
as C:\W16\MAIN.GRP and so on. Installed elsewhere, change the [Groups]
lines of PROGMAN.INI to match.

REG.DAT (the registration database) may be copied too; without one,
w16's SHELL.DLL makes an empty one and fills it from WIN.INI's
[embedding] section, as Windows 3.1 does.

This directory boots on its own (Standard mode, WIN /S): no file of
Microsoft's is needed to reach Program Manager and run the programs
above and DOS programs. w16 is not complete yet, though: programs that
need what w16 does not have yet want these from your own Windows 3.1
installation, copied into the same places (Windows directory or its
SYSTEM subdirectory, as there) without overwriting the files above:

  Windows directory:      any applications you want
  Windows directory:      MORICONS.DLL (Program Manager's extra icons)
  SYSTEM:                 the sound and MCI drivers (*.DRV named in
                          SYSTEM.INI [drivers] and [mci]),
                          the TrueType fonts (*.FOT, *.TTF) and the
                          plotter fonts (ROMAN.FON, SCRIPT.FON,
                          MODERN.FON),
                          DRIVERS.CPL and SND.CPL (Control Panel's
                          Drivers and Sound)

Do not copy your KRNL386.EXE or KRNL286.EXE over w16's: on a 386 or
later DOSX starts KRNL386.EXE when it is there and KRNL286.EXE only when
it is not, and WIN386.EXE (386 Enhanced mode) always KRNL386.EXE.

Notepad uses your NOTEPAD.HLP (Windows directory) for Help; w16 ships no
help text. Printing needs your printer's driver (installed with Control
Panel, as on Windows 3.1).

Write's new documents are in Arial, a TrueType font: copy your Windows'
*.FOT and *.TTF files into SYSTEM. Its help is your WRITE.HLP (Windows
directory).

32-bit programs (PE files) run only with Microsoft's Win32s
installed; without it Program Manager says so instead of starting
them.

SB16.DRV is enabled by these lines in SYSTEM.INI's [drivers] section, for
a Sound Blaster 16 (or a compatible card, or an emulator's); to use
another card's driver instead, change them (or number another driver's
keys "wave1", "midi1", "aux1" to keep both):

  wave=sb16.drv
  midi=sb16.drv
  aux=sb16.drv

and, when the card is not at its usual place, a section of its own
(port and mpuport in hexadecimal; the values shown are the defaults):

  [sb16.drv]
  port=220
  int=5
  dma=1
  hdma=5
  mpuport=330

A key that is missing comes from the BLASTER environment setting
(A220 I5 D1 H5 P330), else from the card's own mixer (IRQ and DMA),
else from the defaults above (Control Panel's Drivers applet, Setup,
shows and changes them). Without a card, and in 386 Enhanced mode, the
driver offers no devices and Windows starts as usual. The MIDI Mapper needs a setup that
sends to "Sound Blaster 16 MIDI" before programs that use it (Media
Player's MIDI Sequencer) play through the card.

w16's own settings are in the [w16] section of SYSTEM.INI. Each of its
additions to Windows 3.1 (the scroll-bar shortcut menu, the version
stamp on the desktop, Notepad's Ctrl+O/Ctrl+S keys and so on) can be
switched back to Windows 3.1's behaviour there; the repository's
docs/w16-settings.md lists every key.

Memory: on a 386 or later w16 uses all the extended memory the XMS
driver gives: in Standard mode all of it, in w16's 386 Enhanced mode up
to 1 GB. With an XMS 3.0 driver it takes it in blocks of any size, so
programs see one large free block (DPMI, GetFreeSpace); Windows 3.1
takes blocks of at most 64 MB, and [w16] LargeMemory=0 in SYSTEM.INI
does the same. Each block a program allocates is still at most 16 MB,
as in Windows 3.1. On an 80286 Windows can use 16 MB at most.

HIMEM.SYS (or another XMS driver) must be loaded. 386 Enhanced mode
(WIN /3, or WIN on a 386 with enough memory) uses w16's WIN386.EXE; the
devices it loads are named in SYSTEM.INI's [386Enh] section, and its
DOS programs run through SYSTEM\WINOA386.MOD. A window of a DOS program
uses the fonts EGA80WOA.FON, EGA40WOA.FON, CGA80WOA.FON and CGA40WOA.FON
(w16's own, in SYSTEM), else the OEM font.
