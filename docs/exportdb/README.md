# Export database

Every export of every NE module in the reference installs, from the NE tables (`tools/exportdb.py`; regenerate after building w16). `exports.csv` has one row per ordinal (name, residency, code/data/constant, how many files in the same install import it and which, whether w16 exports it); `modules.csv` one row per module. `tools/exportdb.py --sqlite` also writes `build/exportdb.sqlite`. "w16 exports" counts exported ordinals, not implemented ones: w16's USER and GDI export stubs for the rest.

## win31

| Module | File | Type | Exports | Imported | Importers | w16 exports |
|---|---|---|---|---|---|---|
| KERNEL | KRNL286.EXE | library | 233 | 168 | 66 | 181 (build/dos/KRNL286.EXE) |
| KERNEL | KRNL386.EXE | library | 233 | 168 | 66 | 181 (build/dos/KRNL286.EXE) |
| USER | USER.EXE | library | 413 | 303 | 55 | 412 (build/dos/USER.EXE) |
| GDI | GDI.EXE | library | 283 | 156 | 43 | 283 (build/dos/GDI.EXE) |
| KEYBOARD | KEYBOARD.DRV | driver | 21 | 15 | 26 |  |
| SHELL | SHELL.DLL | library | 51 | 20 | 26 |  |
| COMMDLG | COMMDLG.DLL | library | 48 | 6 | 18 |  |
| MMSYSTEM | MMSYSTEM.DLL | library | 136 | 72 | 7 |  |
| LZEXPAND | LZEXPAND.DLL | library | 30 | 8 | 4 |  |
| OLECLI | OLECLI.DLL | library | 259 | 40 | 3 |  |
| OLESVR | OLESVR.DLL | library | 24 | 8 | 3 |  |
| SOUND | MMSOUND.DRV | driver | 18 | 8 | 3 |  |
| SOUND | SOUND.DRV | driver | 18 | 8 | 3 |  |
| SYSTEM | SYSTEM.DRV | driver | 11 | 6 | 3 |  |
| VER | VER.DLL | library | 18 | 6 | 3 |  |
| COMM | COMM.DRV | driver | 22 | 17 | 2 |  |
| TOOLHELP | TOOLHELP.DLL | library | 35 | 17 | 2 |  |
| DISPLAY | VGA.DRV | driver | 35 | 4 | 1 |  |
| MOUSE | MOUSE.DRV | driver | 8 | 3 | 1 |  |
| PBRUSH | PBRUSH.DLL | library | 8 | 7 | 1 |  |
| RECORDER | RECORDER.DLL | library | 17 | 13 | 1 |  |
| WIN87EM | WIN87EM.DLL | library | 5 | 1 | 1 | 5 (build/dos/WIN87EM.DLL) |
| ARIAL | ARIAL.FOT | font | 0 | 0 | 0 |  |
| ARIALBD | ARIALBD.FOT | font | 0 | 0 | 0 |  |
| ARIALBI | ARIALBI.FOT | font | 0 | 0 | 0 |  |
| ARIALI | ARIALI.FOT | font | 0 | 0 | 0 |  |
| CALC | CALC.EXE | program | 99 | 0 | 0 |  |
| CALENDAR | CALENDAR.EXE | program | 185 | 0 | 0 |  |
| CARDFILE | CARDFILE.EXE | program | 132 | 0 | 0 |  |
| CGA40WOA | CGA40WOA.FON | font | 0 | 0 | 0 |  |
| CGA80WOA | CGA80WOA.FON | font | 0 | 0 | 0 |  |
| CHARMAP | CHARMAP.EXE | program | 3 | 0 | 0 |  |
| CLIPBRD | CLIPBRD.EXE | program | 4 | 0 | 0 |  |
| CLOCK | CLOCK.EXE | program | 1 | 0 | 0 |  |
| CONTROL | CONTROL.EXE | program | 4 | 0 | 0 |  |
| COUR | COUR.FOT | font | 0 | 0 | 0 |  |
| COURBD | COURBD.FOT | font | 0 | 0 | 0 |  |
| COURBI | COURBI.FOT | font | 0 | 0 | 0 |  |
| COURE | COURE.FON | font | 0 | 0 | 0 |  |
| COURI | COURI.FOT | font | 0 | 0 | 0 |  |
| CPWIN386 | CPWIN386.CPL | control panel | 36 | 0 | 0 |  |
| DDEML | DDEML.DLL | library | 38 | 0 | 0 |  |
| DOSAPP | DOSAPP.FON | font | 0 | 0 | 0 |  |
| DRIVERS | DRIVERS.CPL | control panel | 2 | 0 | 0 |  |
| DRWATSON | DRWATSON.EXE | program | 5 | 0 | 0 |  |
| EGA40WOA | EGA40WOA.FON | font | 0 | 0 | 0 |  |
| EGA80WOA | EGA80WOA.FON | font | 0 | 0 | 0 |  |
| FIXFONTS | VGAFIX.FON | font | 0 | 0 | 0 | 0 (build/w16/SYSTEM/VGAFIX.FON) |
| FONTS | VGASYS.FON | font | 0 | 0 | 0 | 0 (build/w16/SYSTEM/VGASYS.FON) |
| MAINCPL | MAIN.CPL | control panel | 2 | 0 | 0 |  |
| MCISEQ | MCISEQ.DRV | driver | 2 | 0 | 0 |  |
| MCIWAVE | MCIWAVE.DRV | driver | 2 | 0 | 0 |  |
| MIDI | MIDIMAP.DRV | driver | 4 | 0 | 0 |  |
| MMTASK | MMTASK.TSK | program | 0 | 0 | 0 |  |
| MODERN | MODERN.FON | font | 0 | 0 | 0 |  |
| MORICONS | MORICONS.DLL | library | 0 | 0 | 0 |  |
| MPLAYER | MPLAYER.EXE | program | 6 | 0 | 0 |  |
| NOTEPAD | NOTEPAD.EXE | program | 24 | 0 | 0 |  |
| OEMFONTS | VGAOEM.FON | font | 0 | 0 | 0 | 0 (build/w16/SYSTEM/VGAOEM.FON) |
| PACKAGER | PACKAGER.EXE | program | 39 | 0 | 0 |  |
| PBRUSHX | PBRUSH.EXE | program | 245 | 0 | 0 |  |
| PIFEDIT | PIFEDIT.EXE | program | 4 | 0 | 0 |  |
| PRINTMAN | PRINTMAN.EXE | program | 5 | 0 | 0 |  |
| PROGMAN | PROGMAN.EXE | program | 14 | 0 | 0 | 10 (build/dos/PROGMAN.EXE) |
| RECMAIN | RECORDER.EXE | program | 97 | 0 | 0 |  |
| REGEDIT | REGEDIT.EXE | program | 9 | 0 | 0 |  |
| ROMAN | ROMAN.FON | font | 0 | 0 | 0 |  |
| SCRIPT | SCRIPT.FON | font | 0 | 0 | 0 |  |
| SCRNSAVE | SCRNSAVE.SCR | program | 1 | 0 | 0 |  |
| SERIFE | SERIFE.FON | font | 0 | 0 | 0 |  |
| SMALLE | SMALLE.FON | font | 0 | 0 | 0 |  |
| SND | SND.CPL | control panel | 2 | 0 | 0 |  |
| SOL | SOL.EXE | program | 99 | 0 | 0 |  |
| SOUNDREC | SOUNDREC.EXE | program | 30 | 0 | 0 |  |
| SSERIFE | SSERIFE.FON | font | 0 | 0 | 0 | 0 (build/w16/SYSTEM/SSERIFE.FON) |
| SSFLYWIN | SSFLYWIN.SCR | program | 9 | 0 | 0 |  |
| SSMARQUE | SSMARQUE.SCR | program | 9 | 0 | 0 |  |
| SSMYST | SSMYST.SCR | program | 9 | 0 | 0 |  |
| SSSTARS | SSSTARS.SCR | program | 9 | 0 | 0 |  |
| SYMBOL | SYMBOL.FOT | font | 0 | 0 | 0 |  |
| SYMBOLE | SYMBOLE.FON | font | 0 | 0 | 0 |  |
| SYSEDIT | SYSEDIT.EXE | program | 21 | 0 | 0 |  |
| TASKMAN | TASKMAN.EXE | program | 1 | 0 | 0 |  |
| TERMINAL | TERMINAL.EXE | program | 254 | 0 | 0 |  |
| TIMER | TIMER.DRV | driver | 2 | 0 | 0 |  |
| TIMES | TIMES.FOT | font | 0 | 0 | 0 |  |
| TIMESBD | TIMESBD.FOT | font | 0 | 0 | 0 |  |
| TIMESBI | TIMESBI.FOT | font | 0 | 0 | 0 |  |
| TIMESI | TIMESI.FOT | font | 0 | 0 | 0 |  |
| WINFILE | WINFILE.EXE | program | 29 | 0 | 0 |  |
| WINGDING | WINGDING.FOT | font | 0 | 0 | 0 |  |
| WINHELP | WINHELP.EXE | program | 674 | 0 | 0 |  |
| WINMINE | WINMINE.EXE | program | 4 | 0 | 0 |  |
| WINOLDAP | WINOA386.MOD | program | 6 | 0 | 0 |  |
| WINOLDAP | WINOLDAP.MOD | program | 2 | 0 | 0 |  |
| WINSETUP | SETUP.EXE | program | 41 | 0 | 0 |  |
| WINTUTOR | WINTUTOR.EXE | program | 192 | 0 | 0 |  |
| WINVER | WINVER.EXE | program | 1 | 0 | 0 |  |
| WRITE | WRITE.EXE | program | 31 | 0 | 0 |  |

99 modules, 4314 exports.

## wfw311a

| Module | File | Type | Exports | Imported | Importers | w16 exports |
|---|---|---|---|---|---|---|
| KERNEL | KRNL386.EXE | library | 233 | 163 | 61 | 181 (build/dos/KRNL286.EXE) |
| USER | USER.EXE | library | 413 | 307 | 51 | 412 (build/dos/USER.EXE) |
| GDI | GDI.EXE | library | 283 | 170 | 29 | 283 (build/dos/GDI.EXE) |
| KEYBOARD | KEYBOARD.DRV | driver | 21 | 15 | 27 |  |
| SHELL | SHELL.DLL | library | 48 | 20 | 16 |  |
| COMMDLG | COMMDLG.DLL | library | 26 | 5 | 12 |  |
| DEMILAYR | DEMILAYR.DLL | library | 199 | 129 | 10 |  |
| MAILMGR | MAILMGR.DLL | library | 135 | 91 | 7 |  |
| STORE | STORE.DLL | library | 167 | 109 | 7 |  |
| COMMCTRL | COMMCTRL.DLL | library | 21 | 12 | 6 |  |
| LZEXPAND | LZEXPAND.DLL | library | 67 | 8 | 5 |  |
| MMSYSTEM | MMSYSTEM.DLL | library | 136 | 65 | 5 |  |
| VER | VER.DLL | library | 17 | 7 | 5 |  |
| FRAMEWRK | FRAMEWRK.DLL | library | 969 | 344 | 4 |  |
| AB | AB.DLL | library | 31 | 13 | 3 |  |
| SCONFIG | SCONFIG.DLL | library | 20 | 6 | 3 |  |
| WFWSETUP | WFWSETUP.DLL | library | 159 | 17 | 3 |  |
| DDEML | DDEML.DLL | library | 39 | 20 | 2 |  |
| NCDW | NCDW.DLL | library | 7 | 4 | 2 |  |
| NDDEAPI | NDDEAPI.DLL | library | 23 | 9 | 2 |  |
| OLECLI | OLECLI.DLL | library | 260 | 39 | 2 |  |
| SYSTEM | SYSTEM.DRV | driver | 11 | 6 | 2 |  |
| VFORMS | VFORMS.DLL | library | 141 | 33 | 2 |  |
| COMM | COMM.DRV | driver | 19 | 15 | 1 |  |
| DISPLAY | VGA.DRV | driver | 35 | 4 | 1 |  |
| MOUSE | MOUSE.DRV | driver | 8 | 3 | 1 |  |
| SOUND | MMSOUND.DRV | driver | 18 | 1 | 1 |  |
| SOUND | SOUND.DRV | driver | 18 | 1 | 1 |  |
| ARIAL | ARIAL.FOT | font | 0 | 0 | 0 |  |
| ARIALBD | ARIALBD.FOT | font | 0 | 0 | 0 |  |
| ARIALBI | ARIALBI.FOT | font | 0 | 0 | 0 |  |
| ARIALI | ARIALI.FOT | font | 0 | 0 | 0 |  |
| CGA40WOA | CGA40WOA.FON | font | 0 | 0 | 0 |  |
| CGA80WOA | CGA80WOA.FON | font | 0 | 0 | 0 |  |
| CLIPBOOK | CLIPBRD.EXE | program | 5 | 0 | 0 |  |
| CLIPSRV | CLIPSRV.EXE | program | 2 | 0 | 0 |  |
| CMC | CMC.DLL | library | 11 | 0 | 0 |  |
| CONTROL | CONTROL.EXE | program | 4 | 0 | 0 |  |
| COUR | COUR.FOT | font | 0 | 0 | 0 |  |
| COURBD | COURBD.FOT | font | 0 | 0 | 0 |  |
| COURBI | COURBI.FOT | font | 0 | 0 | 0 |  |
| COURE | COURE.FON | font | 0 | 0 | 0 |  |
| COURI | COURI.FOT | font | 0 | 0 | 0 |  |
| CPWIN386 | CPWIN386.CPL | control panel | 9 | 0 | 0 |  |
| DOSAPP | DOSAPP.FON | font | 0 | 0 | 0 |  |
| DRIVERS | DRIVERS.CPL | control panel | 2 | 0 | 0 |  |
| EGA40WOA | EGA40WOA.FON | font | 0 | 0 | 0 |  |
| EGA80WOA | EGA80WOA.FON | font | 0 | 0 | 0 |  |
| FIXFONTS | VGAFIX.FON | font | 0 | 0 | 0 | 0 (build/w16/SYSTEM/VGAFIX.FON) |
| FONTS | VGASYS.FON | font | 0 | 0 | 0 | 0 (build/w16/SYSTEM/VGASYS.FON) |
| MAILSPL | MAILSPL.EXE | program | 2 | 0 | 0 |  |
| MAINCPL | MAIN.CPL | control panel | 2 | 0 | 0 |  |
| MAPI | MAPI.DLL | library | 22 | 0 | 0 |  |
| MCISEQ | MCISEQ.DRV | driver | 2 | 0 | 0 |  |
| MCIWAVE | MCIWAVE.DRV | driver | 2 | 0 | 0 |  |
| MIDI | MIDIMAP.DRV | driver | 4 | 0 | 0 |  |
| MMTASK | MMTASK.TSK | program | 0 | 0 | 0 |  |
| MODERN | MODERN.FON | font | 0 | 0 | 0 |  |
| MORICONS | MORICONS.DLL | library | 0 | 0 | 0 |  |
| MSSFS | MSSFS.DLL | library | 57 | 0 | 0 |  |
| OEMFONTS | VGAOEM.FON | font | 0 | 0 | 0 | 0 (build/w16/SYSTEM/VGAOEM.FON) |
| OLESVR | OLESVR.DLL | library | 24 | 0 | 0 |  |
| PABNSP | PABNSP.DLL | library | 189 | 0 | 0 |  |
| PIFEDIT | PIFEDIT.EXE | program | 4 | 0 | 0 |  |
| PRINTMAN | PRINTMAN.EXE | program | 0 | 0 | 0 |  |
| PROGMAN | PROGMAN.EXE | program | 14 | 0 | 0 | 10 (build/dos/PROGMAN.EXE) |
| REGEDIT | REGEDIT.EXE | program | 9 | 0 | 0 |  |
| ROMAN | ROMAN.FON | font | 0 | 0 | 0 |  |
| SCRIPT | SCRIPT.FON | font | 0 | 0 | 0 |  |
| SENDFILE | SENDFILE.DLL | library | 12 | 0 | 0 |  |
| SERIFE | SERIFE.FON | font | 0 | 0 | 0 |  |
| SMALLE | SMALLE.FON | font | 0 | 0 | 0 |  |
| SND | SND.CPL | control panel | 2 | 0 | 0 |  |
| SSERIFE | SSERIFE.FON | font | 0 | 0 | 0 | 0 (build/w16/SYSTEM/SSERIFE.FON) |
| SYMBOL | SYMBOL.FOT | font | 0 | 0 | 0 |  |
| SYMBOLE | SYMBOLE.FON | font | 0 | 0 | 0 |  |
| SYSEDIT | SYSEDIT.EXE | program | 7 | 0 | 0 |  |
| TASKMAN | TASKMAN.EXE | program | 1 | 0 | 0 |  |
| TIMER | TIMER.DRV | driver | 2 | 0 | 0 |  |
| TIMES | TIMES.FOT | font | 0 | 0 | 0 |  |
| TIMESBD | TIMESBD.FOT | font | 0 | 0 | 0 |  |
| TIMESBI | TIMESBI.FOT | font | 0 | 0 | 0 |  |
| TIMESI | TIMESI.FOT | font | 0 | 0 | 0 |  |
| TOOLHELP | TOOLHELP.DLL | library | 35 | 0 | 0 |  |
| UNIDRV | UNIDRV.DLL | library | 117 | 0 | 0 |  |
| WIN87EM | WIN87EM.DLL | library | 5 | 0 | 0 | 5 (build/dos/WIN87EM.DLL) |
| WINFILE | WINFILE.EXE | program | 30 | 0 | 0 |  |
| WINGDING | WINGDING.FOT | font | 0 | 0 | 0 |  |
| WINHELP | WINHELP.EXE | program | 674 | 0 | 0 |  |
| WINOLDAP | WINOA386.MOD | program | 6 | 0 | 0 |  |
| WINSETUP | WINSETUP.EXE | program | 37 | 0 | 0 |  |
| WINVER | WINVER.EXE | program | 1 | 0 | 0 |  |
| WRITE | WRITE.EXE | program | 31 | 0 | 0 |  |

93 modules, 4848 exports.
