w16 - a Windows directory with w16's programs
=============================================

This directory is laid out like a Windows 3.1 directory (the one that
holds WIN.COM) and contains only what w16 builds (MIT licence, see the
w16 repository's LICENSE):

  WIN.COM                 the loader
  PROGMAN.EXE             Program Manager
  NOTEPAD.EXE             Notepad
  CLOCK.EXE               Clock
  CALC.EXE                Calculator
  CARDFILE.EXE            Cardfile (its help is CARDFILE.HLP)
  WRITE.EXE               Write (its help is WRITE.HLP)
  CONTROL.EXE             Control Panel
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
  SYSTEM\DOSX.EXE         the Standard-mode DOS extender
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
  SYSTEM\MMSOUND.DRV      the sound driver: MessageBeep's beep and the
                          old SOUND functions (voice queues of notes)
                          on the PC speaker
  SYSTEM\MAIN.CPL         Control Panel's applets: Color, Desktop,
                          Keyboard and Mouse work; Fonts, Ports,
                          Printers, International and Date/Time are not
                          w16's yet
  SYSTEM\VGASYS.FON       System font
  SYSTEM\VGAFIX.FON       Fixedsys font
  SYSTEM\VGAOEM.FON       Terminal font
  SYSTEM\SSERIFE.FON      MS Sans Serif font
  SYSTEM.INI, WIN.INI     w16's default settings (only w16's drivers,
                          fonts and programs)
  CONTROL.INI             Control Panel's colour schemes (w16 Default
                          is built in; Evergreen, Ocean, Plum, Brick,
                          Midnight and High Contrast are w16's own)
  PROGMAN.INI             Program Manager's settings and its groups:
  MAIN.GRP                  Main (MS-DOS Prompt, Read Me, Control
                            Panel)
  ACCESSOR.GRP              Accessories (Notepad, Cardfile, Calculator,
                            Clock)
  STARTUP.GRP               StartUp (empty)

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
  SYSTEM:                 MMSYSTEM.DLL (multimedia),
                          TOOLHELP.DLL (used by debuggers and system
                          utilities),
                          the other fonts (*.FON, *.FOT, *.TTF),
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

w16's own settings are in the [w16] section of SYSTEM.INI. Each of its
additions to Windows 3.1 (the scroll-bar shortcut menu, the version
stamp on the desktop, Notepad's Ctrl+O/Ctrl+S keys and so on) can be
switched back to Windows 3.1's behaviour there; the repository's
docs/w16-settings.md lists every key.

HIMEM.SYS (or another XMS driver) must be loaded. 386 Enhanced mode
(WIN /3) needs Microsoft's WIN386.EXE and is not w16's yet.
