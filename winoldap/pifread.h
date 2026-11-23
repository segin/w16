/*
 * pifread.h - a .PIF read as Windows 3.1's WINOLDAP.MOD (Standard mode)
 * and WINOA386.MOD (386 Enhanced mode) read it: winoldap/pifread.c, used
 * by both modules (ARCHITECTURE.md 9.8, 12.11; the format: 15.11 and
 * shell/pifedit/pif.c). Public domain (CC0 1.0, LICENSE).
 */
#ifndef PIFREAD_H
#define PIFREAD_H

#define PIF_286     0           /* Standard mode: "WINDOWS 286 3.0" */
#define PIF_386     1           /* 386 Enhanced mode: "WINDOWS 386 3.0" */

/* the 386 section's data as WINOA386 reads it (+00h-+27h) */
#define D386_MAX    0x00        /* KB Desired */
#define D386_MIN    0x02        /* KB Required */
#define D386_FG     0x04
#define D386_BG     0x06
#define D386_EMSLIM 0x08
#define D386_EMSREQ 0x0A
#define D386_XMSLIM 0x0C
#define D386_XMSREQ 0x0E
#define D386_F0     0x10        /* 01h close when active, 02h background,
                                   04h exclusive, 08h full screen, E0h
                                   reserved Alt+Tab, Alt+Esc, Alt+Space */
#define D386_F1     0x11
#define D386_F2     0x12
#define D386_VIDEO  0x14
#define D386_HOTKEY 0x18        /* +18h-+27h */
#define D386_SIZE   0x28

/* the base section's 63h flags */
#define PIFF_GRAPHICS   0x02
#define PIFF_NOSWITCH   0x04    /* prevent program switch */
#define PIFF_NOSCREEN   0x08    /* no screen exchange */
#define PIFF_CLOSE      0x10    /* close window on exit */
#define PIFF_COM2       0x40
#define PIFF_COM1       0x80

typedef struct {
    BOOL opened;                /* the file was there */
    BOOL found;                 /* the mode's section was found */
    char title[31];             /* 02h, blanks at its end cut */
    char program[64];           /* 24h */
    char directory[65];         /* 65h */
    char params[65];            /* the mode's parameters (A5h, or the 386 section's +28h) */
    BYTE flags;                 /* 63h */
    WORD maxmem, minmem;        /* 20h, 22h (KB) */
    WORD w_ed, w_16f;           /* EDh, 16Fh */
    BYTE d386[D386_SIZE];       /* PIF_386: the 386 data (or what stands for it) */
    WORD xmslim, xmsreq;        /* PIF_286: the 286 section */
    WORD flags286;
} PIFDATA;

BOOL pif_read(const char *path, PIFDATA *p, int mode);

#endif
