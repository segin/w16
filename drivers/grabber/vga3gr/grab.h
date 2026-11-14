/*
 * grab.h - w16's VGA.3GR, the 386 Enhanced-mode display grabber
 * (drivers/grabber/README.md "VGA.3GR", docs/notes/grabber-handover.md).
 *
 * The EXTPAINTSTRUC WINOLDAP (WINOA386.MOD) passes to every entry. Field
 * names and order: the Windows 3.1 DDK documentation (EXTPAINTSTRUC); the
 * DDK does not give the array sizes, so the offsets below are the ones
 * observed with the recorder GR3 (tests/apps/gr3.asm, tests/grabber3.sh):
 * which offsets WINOLDAP fills, which ones the VDD writes through ES:EDI,
 * which ones change across grabber calls. The areas the VDD fills
 * (VDD_Get_State, VDD_Get_Mod, the version string) are laid out as the
 * VDD returned them at run time; the rest of those areas is the grabber's
 * own scratch space (its uses below are w16's, chosen to keep the values
 * observed at the same offsets).
 */
#ifndef GRAB_H
#define GRAB_H

#include <windows.h>

#define EXPORT __far __pascal __loadds

#pragma pack(1)

typedef struct {
    WORD hFont;                 /* a font WINOLDAP created */
    WORD height;                /* its cell height and width */
    WORD width;
} PNTFONT;

/* VDD_Get_State's buffer (1Ch bytes; Client_CX 1Ch) */
typedef struct {
    BYTE mode;                  /* +00 the BIOS video mode */
    BYTE b1;                    /* +01 (0 in every case seen) */
    BYTE flags;                 /* +02 bit 1: cursor tracking wanted */
    BYTE rows;                  /* +03 text rows (19h) */
    WORD curx;                  /* +04 cursor column */
    WORD cury;                  /* +06 cursor row */
    WORD curstart;              /* +08 cursor start scan line */
    WORD curend;                /* +0A cursor end scan line */
    BYTE pal[16];               /* +0C attribute controller palette */
} VDDSTATE;

/* VDD_Get_Mod's buffer: flags, count, then (text) one entry of four
   bytes the VDD writes and the changed rectangle the grabber stores
   (VDD registers AL, AH, BL, BH: left and top, right and bottom cell,
   inclusive) or, in graphics modes, a bit per 4 KB page */
typedef struct {
    WORD flags;                 /* +00 fDisp_Ctlr 8, VRAM 10h, Curs 20h */
    WORD count;                 /* +02 */
    WORD left, top, right, bottom;  /* +04 */
} VDDMOD;

typedef struct {
    WORD l, t, r, b;
} WRECT;

typedef struct {
    /* 000 */ PAINTSTRUCT ps;
    /* 020 */ WRECT PGVDRect;   /* the area painted, in client pixels */
    /* 028 */ WORD HScrlPos, VScrlPos;
    /* 02C */ WORD ColOrg, RowOrg;  /* client (0,0) in screen pixels */
    /* 030 */ DWORD VM1_Hnd;
    /* 034 */ PNTFONT DefFont;
    /* 03A */ PNTFONT AltFnt[6];
    /* 05E */ WORD WhtBrshH, BlkBrshH;
    /* 062 */ WORD RNG1DSSEL;
    /* 064 */ DWORD ShellCall;
    /* 068 */ BYTE GGrbArea[12];
    /* 074 */ WRECT PTVDRect;   /* the area painted, in cells */
    /* 07C */ WORD TDXpos, TDYpos;
    /* 080 */ WORD FntHgt, FntWid;
    /* 084 */ DWORD ThisVMHand;
    /* 088 */ WORD WindHand, BoxPrntHand, hThisInst;
    /* 08E */ WRECT SelctSRect; /* the selection, screen pixels */
    /* 096 */ WRECT GrabArea;
    /* 09E */ BYTE SelCells[6];     /* RenderSelection: cells l t r b w h */
    /* 0A4 */ WORD SelPad;
    /* 0A6 */ WORD SelecFlags;      /* SELECTON 1 */
    /* 0A8 */ BYTE SelRest[0x1B6 - 0xA8];
    /* 1B6 */ VDDSTATE state;
    /* 1D2 */ WORD caretstart, caretend;    /* the caret's shape (FFFFh:
                                               none) */
    /* 1D6 */ VDDMOD mod;
    /* 1E2 */ BYTE modrest[0x21A - 0x1E2];
    /* 21A */ WORD lastmode;    /* low byte: the mode painted; FFh: caret
                                   off */
    /* 21C */ WORD hMemDC, hBitmap, hBits;  /* graphics painting */
    /* 222 */ BYTE scratch[0x236 - 0x222];
    /* 236 */ DWORD EPStatusFlags;
    /* 23A */ char VerStrng[8];
} EPS;

#pragma pack()

typedef EPS __far *LPEPS;

#define SELECTON        1
#define fDisp_Ctlr      0x08
#define fDisp_VRAM      0x10
#define fDisp_Curs      0x20
#define fDisp_HCurTrack 0x80
#define EPS_STATEVALID  0x00020000L /* EPStatusFlags: state[] is current */

/* the VDD's protected-mode API (INT 2Fh AX=1684h BX=000Ah), functions as
   observed: EAX = function, EBX = VM handle, ES:EDI = buffer, ECX = its
   size */
#define VDD_GET_VERSION 0
#define VDD_GET_MEM     1
#define VDD_GET_STATE   2
#define VDD_GET_MOD     3
#define VDD_CLEAR_MOD   4
#define VDD_FREE_MEM    5
#define VDD_GET_GRBMEM  6
#define VDD_FREE_GRAB   7
#define VDD_GET_GRBSTATE 8
#define VDD_UNLOCK_APP  9

typedef struct {
    DWORD eax, ebx, ecx, edx, esi, edi;
    WORD es, flags;
} VREGS;

/* vddcall.asm */
extern int __far __cdecl vdd_find(void);
extern void __far __cdecl vdd_call(VREGS __far *r);

#endif
