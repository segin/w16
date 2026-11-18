/*
 * mdl.h - MMSYSLOG.DLL (tests/mmsystem/mdl.c), the record keeper of the
 * logging pass-through device drivers MDS*.DRV (tests/mmsystem/mds.c):
 * what MMSYSTEM sends a wave, MIDI or auxiliary driver and what the
 * driver hands back through DriverCallback (ARCHITECTURE.md 23.1).
 */
#ifndef MDL_H
#define MDL_H

/* which entry of a driver a record is about */
#define MDK_DRV 0       /* DriverProc */
#define MDK_WOD 1       /* wodMessage */
#define MDK_WID 2       /* widMessage */
#define MDK_MOD 3       /* modMessage */
#define MDK_MID 4       /* midMessage */
#define MDK_AUX 5       /* auxMessage */
#define MDK_NUM 6

/* the arguments as they are on the stack (Pascal: the last one lowest) */
struct mdmsgargs {      /* xxxMessage(UINT dev, UINT msg, DWORD user, DWORD p1, DWORD p2) */
    DWORD p2, p1, user;
    WORD msg, dev;
};
struct mddrvargs {      /* DriverProc(DWORD id, HDRVR h, UINT msg, LPARAM p1, LPARAM p2) */
    DWORD p2, p1;
    WORD msg, h;
    DWORD id;
};

typedef DWORD (FAR PASCAL *MDPROC)(WORD, WORD, DWORD, DWORD, DWORD);
typedef DWORD (FAR PASCAL *MDDRVPROC)(DWORD, WORD, WORD, DWORD, DWORD);

/* MMSYSLOG's interface */
WORD FAR PASCAL MlRegister(LPCSTR shim);
DWORD FAR PASCAL MlCall(WORD shim, WORD kind, FARPROC real, WORD flags, WORD retcs,
                        void FAR *args);
void FAR PASCAL MlDump(LPCSTR file);
void FAR PASCAL MlName(DWORD value, LPCSTR name);
void FAR PASCAL MlProbe(WORD ss, HTASK task);
void FAR PASCAL MlQuiet(WORD on);
WORD FAR PASCAL MlCount(void);
#endif
