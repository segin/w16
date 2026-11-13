w16 - a Windows directory with w16's programs
=============================================

This directory is laid out like a Windows 3.1 directory (the one that
holds WIN.COM) and contains only what w16 builds (MIT licence, see the
w16 repository's LICENSE):

  WIN.COM                 the loader
  PROGMAN.EXE             Program Manager
  NOTEPAD.EXE             Notepad
  CLOCK.EXE               Clock
  SYSTEM\KRNL286.EXE      KERNEL
  SYSTEM\DOSX.EXE         the Standard-mode DOS extender
  SYSTEM\WIN87EM.DLL      floating-point emulation
  SYSTEM\SHELL.DLL        the shell library (drag and drop, ShellExecute,
                          the registration database REG.DAT, icons,
                          About boxes)
  SYSTEM\MOUSE.DRV        the mouse driver, for a PS/2 mouse (with a
                          serial, bus or InPort mouse, use your own
                          Windows 3.1 MOUSE.DRV instead)
  SYSTEM\COMM.DRV         the serial (COM1-COM4) and parallel
                          (LPT1-LPT3) port driver
  SYSTEM\KEYBOARD.DRV     the keyboard driver (US layout, code page
                          437; for another layout or code page use
                          your own Windows 3.1 KEYBOARD.DRV and its
                          KBD*.DLL instead)
  SYSTEM\VGASYS.FON       System font
  SYSTEM\VGAFIX.FON       Fixedsys font
  SYSTEM\VGAOEM.FON       Terminal font
  SYSTEM\SSERIFE.FON      MS Sans Serif font

REG.DAT (the registration database) may be copied too; without one,
w16's SHELL.DLL makes an empty one and fills it from WIN.INI's
[embedding] section, as Windows 3.1 does.

w16 is not complete yet. To boot it (Standard mode, WIN /S),
copy these from your own Windows 3.1 installation into the same places
(Windows directory or its SYSTEM subdirectory, as there), without
overwriting the files above:

  Windows directory:      SYSTEM.INI, WIN.INI, PROGMAN.INI, the *.GRP
                          group files, and any applications you want
  SYSTEM:                 USER.EXE, GDI.EXE, MMSYSTEM.DLL,
                          the drivers named in SYSTEM.INI's [boot]
                          section (SYSTEM.DRV, VGA.DRV,
                          MMSOUND.DRV, ...), the other
                          fonts (*.FON, *.FOT, *.TTF), WSWAP.EXE (which
                          WIN.COM starts in Standard mode), and for DOS
                          programs WINOLDAP.MOD, DSWAP.EXE and the
                          VGACOLOR.2GR grabber

Do not copy KRNL386.EXE: on a 386 or later, DOSX starts KRNL386.EXE when
it is there and w16's KRNL286.EXE only when it is not.

Notepad also needs COMMDLG.DLL (SYSTEM) for its Open, Save As, Find and
Print Setup dialogs, and uses your NOTEPAD.HLP (Windows directory) for
Help; w16 ships no help text.

HIMEM.SYS (or another XMS driver) must be loaded. 386 Enhanced mode
(WIN /3) needs Microsoft's WIN386.EXE and is not w16's yet.
