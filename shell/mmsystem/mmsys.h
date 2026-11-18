/*
 * mmsys.h - w16's MMSYSTEM.DLL, internal definitions (ARCHITECTURE.md 23).
 *
 * Sources: Windows 3.1 SDK, Multimedia Programmer's Guide (the functions,
 * structures and error codes), Programmer's Reference Vol. 1 ch. 25 and
 * Vol. 2 (installable drivers); the numeric values of constants as the
 * SDK's interface defines them (also in OpenWatcom's h/win/mmsystem.h,
 * ARCHITECTURE.md 9.3, and Wine's mmsystem.h of before 2002, X11
 * licence), each one an interface fact applications are built with.
 */
#ifndef W16_MMSYS_H
#define W16_MMSYS_H

#include <windows.h>

#define MMAPI __loadds FAR PASCAL       /* exported by mmsystem.lnk */

typedef WORD MMRESULT;

/* error codes */
#define MMSYSERR_NOERROR        0
#define MMSYSERR_ERROR          1
#define MMSYSERR_BADDEVICEID    2
#define MMSYSERR_NOTENABLED     3
#define MMSYSERR_ALLOCATED      4
#define MMSYSERR_INVALHANDLE    5
#define MMSYSERR_NODRIVER       6
#define MMSYSERR_NOMEM          7
#define MMSYSERR_NOTSUPPORTED   8
#define MMSYSERR_BADERRNUM      9
#define MMSYSERR_INVALFLAG      10
#define MMSYSERR_INVALPARAM     11
#define TIMERR_NOCANDO          97
#define TIMERR_STRUCT           129
#define JOYERR_PARMS            165
#define MCIERR_BASE             256
#define MMIOERR_BASE            256

/* installable-driver messages (Programmer's Reference Vol. 3) */
#define DRV_LOAD                0x0001
#define DRV_ENABLE              0x0002
#define DRV_OPEN                0x0003
#define DRV_CLOSE               0x0004
#define DRV_DISABLE             0x0005
#define DRV_FREE                0x0006
#define DRV_INSTALL             0x0009
#define DRV_EXITAPPLICATION     0x000C

/* mmTaskCreate's errors (Wine's mmsystem.h of before 2002) */
#define TASKERR_NOTASKSUPPORT   1
#define TASKERR_OUTOFMEMORY     2

/* mmfix.asm */
extern HDRVR __cdecl mm_tdrv;           /* the timer driver */
extern FARPROC __cdecl mm_tproc;        /* its DriverProc */
extern WORD __cdecl mm_istack;          /* the interrupt stack */

/* core.c */
extern HINSTANCE __cdecl mm_hinst;

/* dev.c */
void dev_load(void);

/* core.c */
void __cdecl __far __loadds mm_makestack(void);

/* snd.c */
void snd_init(void);
HWND __cdecl snd_window(void);

/* mci.c */
void mci_notified(UINT dev);
void mci_taskexit(void);

#endif
