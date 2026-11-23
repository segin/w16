/*
 * sb16.h - w16's SB16.DRV: the shared definitions (drivers/sb16/README.md,
 * ARCHITECTURE.md 24).
 *
 * Sources: the Windows 3.1 SDK (Multimedia Programmer's Guide ch. 4-5,
 * Programmer's Reference Vol. 1 ch. 25: installable drivers) for the
 * structures, flags, error codes and callback messages; the device-driver
 * messages' numbers and the order MMSYSTEM sends them in as recorded in
 * ARCHITECTURE.md 23.2 (observed with tests/mmsystem/mds.c between
 * Microsoft's MMSYSTEM and Windows 3.1's SNDBLST2.DRV); the card's ports,
 * registers and commands from Creative's Sound Blaster Series Hardware
 * Programming Guide (facts only) and the 8237/8259 data sheets' register
 * maps. w16's own definitions: nothing is copied from a header.
 */
#ifndef W16_SB16_H
#define W16_SB16_H

#include <windows.h>

#define ENTRY __loadds FAR PASCAL       /* exported, DS loaded */

/* ---- MMSYSTEM's results ---- */
#define MMSYSERR_NOERROR        0
#define MMSYSERR_ERROR          1
#define MMSYSERR_BADDEVICEID    2
#define MMSYSERR_NOTENABLED     3
#define MMSYSERR_ALLOCATED      4
#define MMSYSERR_NOMEM          7
#define MMSYSERR_NOTSUPPORTED   8
#define MMSYSERR_INVALPARAM     11
#define WAVERR_BADFORMAT        32
#define WAVERR_STILLPLAYING     33
#define MIDIERR_UNPREPARED      64
#define MIDIERR_STILLPLAYING    65

/* the installable-driver messages (DRV_*, DRVCNF_*): windows.h */

/* ---- the device messages (ARCHITECTURE.md 23.2) ---- */
#define DRVM_INIT               100
#define WODM_GETNUMDEVS         3
#define WODM_GETDEVCAPS         4
#define WODM_OPEN               5
#define WODM_CLOSE              6
#define WODM_PREPARE            7
#define WODM_UNPREPARE          8
#define WODM_WRITE              9
#define WODM_PAUSE              10
#define WODM_RESTART            11
#define WODM_RESET              12
#define WODM_GETPOS             13
#define WODM_GETPITCH           14
#define WODM_SETPITCH           15
#define WODM_GETVOLUME          16
#define WODM_SETVOLUME          17
#define WODM_GETPLAYBACKRATE    18
#define WODM_SETPLAYBACKRATE    19
#define WODM_BREAKLOOP          20
#define WIDM_GETNUMDEVS         50
#define WIDM_GETDEVCAPS         51
#define WIDM_OPEN               52
#define WIDM_CLOSE              53
#define WIDM_PREPARE            54
#define WIDM_UNPREPARE          55
#define WIDM_ADDBUFFER          56
#define WIDM_START              57
#define WIDM_STOP               58
#define WIDM_RESET              59
#define WIDM_GETPOS             60
#define MODM_GETNUMDEVS         1
#define MODM_GETDEVCAPS         2
#define MODM_OPEN               3
#define MODM_CLOSE              4
#define MODM_PREPARE            5
#define MODM_UNPREPARE          6
#define MODM_DATA               7
#define MODM_LONGDATA           8
#define MODM_RESET              9
#define MODM_GETVOLUME          10
#define MODM_SETVOLUME          11
#define MIDM_GETNUMDEVS         53
#define MIDM_GETDEVCAPS         54
#define MIDM_OPEN               55
#define MIDM_CLOSE              56
#define MIDM_PREPARE            57
#define MIDM_UNPREPARE          58
#define MIDM_ADDBUFFER          59
#define MIDM_START              60
#define MIDM_STOP               61
#define MIDM_RESET              62
#define AUXDM_GETNUMDEVS        3
#define AUXDM_GETDEVCAPS        4
#define AUXDM_GETVOLUME         5
#define AUXDM_SETVOLUME         6

/* ---- callback messages and flags (Programmer's Guide ch. 5) ---- */
#define MM_WOM_OPEN             0x3BB
#define MM_WOM_CLOSE            0x3BC
#define MM_WOM_DONE             0x3BD
#define MM_WIM_OPEN             0x3BE
#define MM_WIM_CLOSE            0x3BF
#define MM_WIM_DATA             0x3C0
#define MM_MIM_OPEN             0x3C1
#define MM_MIM_CLOSE            0x3C2
#define MM_MIM_DATA             0x3C3
#define MM_MIM_LONGDATA         0x3C4
#define MM_MIM_ERROR            0x3C5
#define MM_MOM_OPEN             0x3C7
#define MM_MOM_CLOSE            0x3C8
#define MM_MOM_DONE             0x3C9
#define DCB_NOSWITCH            8
#define WAVE_FORMAT_QUERY       1

#define WHDR_DONE               0x00000001L
#define WHDR_PREPARED           0x00000002L
#define WHDR_BEGINLOOP          0x00000004L
#define WHDR_ENDLOOP            0x00000008L
#define WHDR_INQUEUE            0x00000010L
#define WHDR_STARTED            0x80000000L     /* the driver's own: begun (MMSYSTEM
                                                   lets drivers use bit 31) */
#define MHDR_DONE               0x00000001L
#define MHDR_PREPARED           0x00000002L
#define MHDR_INQUEUE            0x00000004L

#define TIME_MS                 1
#define TIME_SAMPLES            2
#define TIME_BYTES              4

/* the SDK's structures, as the device driver sees them (packed, as
   MMSYSTEM.H has them) */
#pragma pack(__push, 1)
typedef struct whdr {                   /* WAVEHDR */
    LPSTR lpData;
    DWORD dwBufferLength;
    DWORD dwBytesRecorded;
    DWORD dwUser;
    DWORD dwFlags;
    DWORD dwLoops;
    struct whdr FAR *lpNext;            /* the driver's: the queue */
    DWORD reserved;                     /* the driver's: the stream position
                                           after the header's last byte */
} WHDR;
typedef WHDR FAR *LPWHDR;

typedef struct mhdr {                   /* MIDIHDR */
    LPSTR lpData;
    DWORD dwBufferLength;
    DWORD dwBytesRecorded;
    DWORD dwUser;
    DWORD dwFlags;
    struct mhdr FAR *lpNext;
    DWORD reserved;
} MHDR;
typedef MHDR FAR *LPMHDR;

typedef struct {                        /* PCMWAVEFORMAT */
    WORD wFormatTag;
    WORD nChannels;
    DWORD nSamplesPerSec;
    DWORD nAvgBytesPerSec;
    WORD nBlockAlign;
    WORD wBitsPerSample;
} PCMFMT;

typedef struct {                        /* WAVEOPENDESC */
    WORD hWave;
    PCMFMT FAR *lpFormat;
    DWORD dwCallback;
    DWORD dwInstance;
} WOPENDESC;

typedef struct {                        /* MIDIOPENDESC */
    WORD hMidi;
    DWORD dwCallback;
    DWORD dwInstance;
} MOPENDESC;

typedef struct {                        /* MMTIME (the parts used) */
    WORD wType;
    DWORD value;
} MMT;

#pragma pack(__pop)

/* a client's callback */
struct cbinfo {
    WORD h;                             /* the handle MMSYSTEM gave */
    WORD flags;                         /* DriverCallback's: type | NOSWITCH */
    DWORD cb;
    DWORD inst;
};

/* ---- imports ---- */
BOOL FAR PASCAL DRIVERCALLBACK(DWORD cb, WORD flags, WORD h, WORD msg, DWORD user, DWORD dw1,
                               DWORD dw2);                      /* MMSYSTEM.31 */
DWORD FAR PASCAL TIMEGETTIME(void);                             /* MMSYSTEM.607 */

/* KERNEL's (Vol. 2; windows.h has them only for later versions) */
DWORD FAR PASCAL GlobalDOSAlloc(DWORD size);
UINT FAR PASCAL GlobalDOSFree(UINT sel);

/* ---- sb16a.asm ---- */
WORD __cdecl __far sb_cli(void);                /* interrupts off; the old flags */
void __cdecl __far sb_sti(WORD flags);          /* back on when they were */
WORD __cdecl __far sb_ahincr(void);             /* KERNEL's __AHINCR */
int __cdecl __far sb_hook(WORD irq);            /* the card's IRQ ours */
void __cdecl __far sb_unhook(void);
/* VSBD.386's API (386 Enhanced mode, sb16a.asm): the entry (INT 2Fh 1684h,
   device 0445h), 0 if there is none; a call (DX the function, AX the
   port): AX back, bit 16 the carry */
DWORD __cdecl __far sb_vxd_entry(void);
DWORD __cdecl __far sb_vxd(WORD fn, WORD ax);
int sb_take(void);                  /* the card ours (always in Standard mode) */
void sb_give(void);
void __cdecl __far sb_hcopy(LPSTR dst, LPSTR src, DWORD off, WORD n, WORD dir);
                                        /* dir 0: dst <- src+off (huge); 1: src+off <- dst */
extern WORD sb_hinst;

/* ---- the configuration and the hardware (sb16.c) ---- */
extern WORD sb_port, sb_irq, sb_dma8, sb_dma16, sb_mpu;
extern WORD sb_dspver;                  /* 0: no DSP */
extern BYTE sb_ok;                      /* a DSP 4.xx found, the IRQ ours */
extern BYTE mpu_ok;                     /* the MPU-401 answered, UART mode */
extern volatile BYTE ev8, ev16;         /* interrupts not yet handled (sb16a.asm) */
extern volatile BYTE mpuq[64];          /* MPU-401 input bytes (sb16a.asm) */
extern volatile BYTE mpuqh, mpuqt;

int dsp_reset(void);
int dsp_write(BYTE b);
int dsp_read(void);
void mix_write(BYTE reg, BYTE v);
BYTE mix_read(BYTE reg);
void dma_start(WORD ch, DWORD phys, WORD len, int rec);
void dma_stop(WORD ch);
WORD dma_left(WORD ch);
void notify(struct cbinfo *c, WORD msg, DWORD dw1, DWORD dw2);
void copycaps(LPVOID dst, const void *src, WORD size, DWORD want);

/* the DMA buffer: conventional memory (GlobalDOSAlloc), physically
   contiguous in Standard mode, not crossing a 64 KB boundary */
#define DMABUF  8192
extern LPSTR dmabuf;
extern DWORD dmaphys;

/* ---- wave.c ---- */
DWORD ENTRY WODMESSAGE(WORD dev, WORD msg, DWORD user, DWORD p1, DWORD p2);
DWORD ENTRY WIDMESSAGE(WORD dev, WORD msg, DWORD user, DWORD p1, DWORD p2);
void wave_irq(int sixteen);
void wave_init(void);

/* ---- midi.c ---- */
DWORD ENTRY MODMESSAGE(WORD dev, WORD msg, DWORD user, DWORD p1, DWORD p2);
DWORD ENTRY MIDMESSAGE(WORD dev, WORD msg, DWORD user, DWORD p1, DWORD p2);
int mpu_reset(void);
void midi_input(void);

/* ---- aux.c ---- */
DWORD ENTRY AUXMESSAGE(WORD dev, WORD msg, DWORD user, DWORD p1, DWORD p2);
DWORD vol_get(int which);
void vol_set(int which, DWORD v);
void vol_init(void);
#define VOL_MASTER 0
#define VOL_WAVE   1
#define VOL_MIDI   2
#define VOL_CD     3
#define VOL_LINE   4
#define NVOL       5

/* the work at interrupt time, after the hardware is acknowledged
   (called by sb16a.asm's handler on MMSYSTEM's interrupt stack) */
void __cdecl __far sb_work(void);

#endif
