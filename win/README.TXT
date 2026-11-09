w16 - a Windows directory with w16's programs
=============================================

This directory is laid out like a Windows 3.1 directory (the one that
holds WIN.COM) and contains only what w16 builds (MIT licence, see the
w16 repository's LICENSE):

  WIN.COM                 the loader
  PROGMAN.EXE             Program Manager
  NOTEPAD.EXE             Notepad
  SYSTEM\KRNL286.EXE      KERNEL
  SYSTEM\DOSX.EXE         the Standard-mode DOS extender
  SYSTEM\WIN87EM.DLL      floating-point emulation
  SYSTEM\VGASYS.FON       System font
  SYSTEM\VGAFIX.FON       Fixedsys font
  SYSTEM\VGAOEM.FON       Terminal font
  SYSTEM\SSERIFE.FON      MS Sans Serif font

It is not a complete Windows yet. To boot it (Standard mode, WIN /S),
copy these from your own Windows 3.1 installation into the same places
(Windows directory or its SYSTEM subdirectory, as there), without
overwriting the files above:

  Windows directory:      SYSTEM.INI, WIN.INI, PROGMAN.INI, the *.GRP
                          group files, and any applications you want
  SYSTEM:                 USER.EXE, GDI.EXE, SHELL.DLL, MMSYSTEM.DLL,
                          the drivers named in SYSTEM.INI's [boot]
                          section (SYSTEM.DRV, KEYBOARD.DRV, MOUSE.DRV,
                          VGA.DRV, MMSOUND.DRV, COMM.DRV, ...), the other
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
