/*
 * ttfbc.h - the TrueType bytecode interpreter's state (ttfbc.c), for
 * ttfbc.c itself and its unit tests (tests/ttf/ttfbctest.c). Programs
 * use ttfx.h's API (ttfx_bcBytes, ttfx_bcPrepare, ttfx_LoadOutline).
 *
 * Written for w16 from the TrueType specification (Apple's TrueType
 * Reference Manual and Microsoft's OpenType specification: the
 * instruction set, the graphics state, the scan converter); public
 * domain like the rest of w16 (LICENSE).
 */
#ifndef TTFBC_H
#define TTFBC_H

#include "ttfx.h"

/* a point's flags (zone flags[]) */
#define BC_ON 1                         /* on the curve */
#define BC_TX 2                         /* touched in x */
#define BC_TY 4                         /* touched in y */

/* the round states (round_state) */
enum { BC_RTHG, BC_RTG, BC_RTDG, BC_RDTG, BC_RUTG, BC_ROFF, BC_SROUND };

/* the graphics state (the specification's table of variables): vectors
   2.14, distances 26.6 */
typedef struct {
    ttfx_i16 pv[2], fv[2], dv[2];       /* projection, freedom, dual projection */
    ttfx_i32 min_dist, cvt_cutin, sw_cutin, sw_value;
    ttfx_i32 period, phase, threshold;  /* SROUND, S45ROUND (26.6) */
    ttfx_i16 round_state, loop, auto_flip, delta_base, delta_shift;
    ttfx_u16 rp[3];
    ttfx_u8 zp[3];
    ttfx_u8 instctrl;                   /* INSTCTRL flags: 1 no glyph programs, 2 defaults for them */
    ttfx_u16 scan_control;              /* SCANCTRL's value */
    ttfx_i16 scan_type;                 /* SCANTYPE: 0, 1 or 2 */
} ttfx__gs;

/* a zone: the points' grid-fitted (cur) and original (org) positions,
   26.6 with y up, their positions in font units (ous) for measuring the
   original outline exactly, the flags; the contours' last points */
typedef struct {
    ttfx_point TTFX_FAR *cur, TTFX_FAR *org, TTFX_FAR *ous;
    ttfx_u8 TTFX_FAR *flags;
    ttfx_u16 TTFX_FAR *ends;
    ttfx_u16 n, nc;                     /* points (with the phantoms), contours */
    ttfx_u8 ous_px;                     /* ous are 26.6 pixels (a compound's), not font units */
} ttfx__zone;

/* a function or instruction definition: in which program, where */
typedef struct {
    ttfx_u8 prog, defined;              /* 0 fpgm, 1 prep */
    ttfx_u16 start, end;                /* the body: after FDEF/IDEF up to ENDF */
    ttfx_u16 opcode;                    /* (IDEF) */
} ttfx__def;

/* a size's state, at the start of the block ttfx_bcPrepare fills: the
   arrays at offsets from the block's start (the block may move between
   uses, so it holds no pointers to itself) */
typedef struct {
    ttfx_u32 bytes;
    ttfx_scale sx, sy;
    ttfx_u16 ppemx, ppemy;
    ttfx_u16 ncvt, nstor, nfdef, nidef, ntw, maxstack, fpgmlen, preplen, maxpts, maxcont;
    ttfx_u16 o_cvt, o_stor, o_fdef, o_idef, o_tw, o_fpgm, o_prep;
    ttfx__gs gs;                        /* the state glyph programs start with */
    ttfx_u8 ok;                         /* fpgm and prep ran */
    ttfx_u8 maxdepth;
} ttfx__bcsize;

#define BC_MAXCALL 32                   /* nested CALLs */

typedef struct {
    const ttfx_u8 TTFX_FAR *code;
    ttfx_u16 ip, end;                   /* the return address and its program's length */
    ttfx_u16 start;                     /* the function's start (LOOPCALL repeats it) */
    ttfx_i32 count;                     /* LOOPCALL's calls left */
    ttfx_u8 prog;
} ttfx__frame;

/* the interpreter */
typedef struct {
    const ttfx_font *f;
    ttfx_u8 TTFX_FAR *base;             /* the size's block */
    ttfx__bcsize TTFX_FAR *sz;
    ttfx__gs gs;
    ttfx__zone z[2];                    /* 0 the twilight zone, 1 the glyph */
    ttfx_i32 TTFX_FAR *stack;
    ttfx_u16 sp, maxsp;
    ttfx_i32 TTFX_FAR *cvt;
    ttfx_i32 TTFX_FAR *stor;
    ttfx_u16 ncvt, nstor;
    ttfx__def TTFX_FAR *fdef;
    ttfx__def TTFX_FAR *idef;
    ttfx_u16 nfdef, nidef;
    const ttfx_u8 TTFX_FAR *code[3];    /* fpgm, prep, the glyph program */
    ttfx_u16 len[3];
    ttfx_u8 prog;                       /* the program running: 0 fpgm, 1 prep, 2 a glyph's */
    ttfx_i32 fdotp;                     /* freedom . projection, 2.14 */
    ttfx_u16 ppemx, ppemy;
    ttfx_scale sx, sy;
    ttfx_u8 stretched;
    ttfx_u32 budget;                    /* instructions left (a loop's end) */
    int error;                          /* the first error: BC_E* */
} ttfx__bc;

enum { BC_EOK, BC_ESTACK, BC_EPOINT, BC_ECVT, BC_ESTOR, BC_EFUNC, BC_ECODE, BC_EDIV, BC_ELOOP, BC_EOPCODE, BC_ECALL };

/* run program prog (its code and length in bc->code[], len[]) from the
   start; 0 on an error (bc->error says which) */
int ttfx__bcRun(ttfx__bc *bc, int prog);
/* the last error, the program and where (for tests and debugging) */
extern int ttfx__bcLastError, ttfx__bcLastProg, ttfx__bcTrace;
extern ttfx_u16 ttfx__bcLastIp;
/* the graphics state's defaults (the specification's table) */
void ttfx__bcDefaults(ttfx__gs *gs);
/* the scan converter's rules (TTFX_DROP_*) a glyph's scan control and
   scan type ask for at ppem */
int ttfx__bcDropout(const ttfx__gs TTFX_FAR *gs, ttfx_u16 ppem, int stretched);
/* (for tests) the rounding of value d by the current round state */
ttfx_i32 ttfx__bcRound(const ttfx__bc *bc, ttfx_i32 d);

#endif
