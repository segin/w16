/*
 * ttfbctest.c - the TrueType bytecode interpreter (gdi/ttf/ttfbc.c), one
 * instruction at a time, against results worked out by hand from the
 * specification's descriptions (Apple's TrueType Reference Manual,
 * Microsoft's OpenType "TrueType instructions"): the stack, arithmetic,
 * rounding, control flow, functions, the vectors, every point move, IUP,
 * the DELTAs, GETINFO and the scan-control decision. Each case runs a
 * small program over a small glyph zone (points in 26.6, y up, measured
 * in pixels: the font units are the pixels here) and a twilight zone.
 *
 *   usage: ttfbctest [-v]
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include "../../gdi/ttf/ttfx.h"
#include "../../gdi/ttf/ttfbc.h"

void TTFX_FAR *ttfx_alloc(void *alloc, ttfx_u32 n)
{
    (void)alloc;
    return n > 65000 ? 0 : malloc(n ? n : 1);
}

void ttfx_free(void *alloc, void TTFX_FAR *p)
{
    (void)alloc;
    free(p);
}

#define NP 12                           /* glyph points (10, then the phantoms) */
#define NT 4                            /* twilight points */

static ttfx__bc bc;
static ttfx_i32 stack[256], cvt[16], stor[16];
static ttfx__def fdef[8], idef[4];
static ttfx_point cur[NP], org[NP], ous[NP], tcur[NT], torg[NT], tous[NT];
static ttfx_u8 flags[NP], tflags[NT];
static ttfx_u16 ends[2];
static int fails, cases, verbose;
static const char *what;

/* the glyph zone: points 0-3 a square contour from (1,1) to (5,5) pixels
   (0: (1,1), 1: (1,5), 2: (5,5), 3: (5,1)), 4-9 a second contour along
   x at y = 2: (2,2) (3,2) (4,2) (6,2) (7,2) (8,2); 10, 11 the phantoms
   (0,0), (10,0). All at their original places, nothing touched; 12
   pixels to the em; CVT[i] = i pixels + 0.25; storage zero; the default
   graphics state */
static void setup(void)
{
    static const int px[10] = { 1, 1, 5, 5, 2, 3, 4, 6, 7, 8 }, py[10] = { 1, 5, 5, 1, 2, 2, 2, 2, 2, 2 };
    int i;
    memset(&bc, 0, sizeof bc);
    for (i = 0; i < 10; i++) {
        org[i].x = px[i] * 64;
        org[i].y = py[i] * 64;
        flags[i] = BC_ON;
    }
    org[10].x = org[10].y = org[11].y = 0;
    org[11].x = 640;
    flags[10] = flags[11] = 0;
    for (i = 0; i < NP; i++)
        cur[i] = ous[i] = org[i];
    for (i = 0; i < NT; i++) {
        tcur[i].x = tcur[i].y = torg[i].x = torg[i].y = 0;
        tflags[i] = 0;
    }
    ends[0] = 3;
    ends[1] = 9;
    bc.z[1].cur = cur;
    bc.z[1].org = org;
    bc.z[1].ous = ous;
    bc.z[1].flags = flags;
    bc.z[1].ends = ends;
    bc.z[1].n = NP;
    bc.z[1].nc = 2;
    bc.z[1].ous_px = 1;
    bc.z[0].cur = tcur;
    bc.z[0].org = torg;
    bc.z[0].ous = tous;
    bc.z[0].flags = tflags;
    bc.z[0].n = NT;
    bc.stack = stack;
    bc.maxsp = 256;
    for (i = 0; i < 16; i++) {
        cvt[i] = i * 64 + 16;
        stor[i] = 0;
    }
    bc.cvt = cvt;
    bc.ncvt = 16;
    bc.stor = stor;
    bc.nstor = 16;
    memset(fdef, 0, sizeof fdef);
    memset(idef, 0, sizeof idef);
    bc.fdef = fdef;
    bc.nfdef = 8;
    bc.idef = idef;
    bc.nidef = 4;
    bc.ppemx = bc.ppemy = 12;
    bc.sx = bc.sy = TTFX_ONE_SCALE;
    ttfx__bcDefaults(&bc.gs);
}

/* run code as program prog (0 the font program: FDEF allowed; 2 a
   glyph's); 1 if it ran without an error */
static int run(int prog, const ttfx_u8 *code, unsigned len)
{
    bc.code[prog] = code;
    bc.len[prog] = (ttfx_u16)len;
    bc.budget = 100000;
    bc.error = 0;
    return ttfx__bcRun(&bc, prog);
}

#define RUN(...) do { static const ttfx_u8 c_[] = { __VA_ARGS__ }; run(2, c_, sizeof c_); } while (0)
#define RUN0(...) do { static const ttfx_u8 c_[] = { __VA_ARGS__ }; run(0, c_, sizeof c_); } while (0)

static void check(const char *name, long got, long want)
{
    cases++;
    if (got != want) {
        fails++;
        printf("FAIL %s: %s: %ld, not %ld\n", what, name, got, want);
    } else if (verbose)
        printf("ok   %s: %s = %ld\n", what, name, got);
}

/* the stack, top last */
static void stk(int n, ...)
{
    va_list ap;
    int i;
    char nm[32];
    va_start(ap, n);
    check("depth", bc.sp, n);
    for (i = 0; i < n; i++) {
        long v = va_arg(ap, long);
        sprintf(nm, "stack[%d]", i);
        check(nm, i < (int)bc.sp ? (long)stack[i] : -999999L, v);
    }
    va_end(ap);
    check("error", bc.error, 0);
}

static void pt(int i, long x, long y)
{
    char nm[24];
    sprintf(nm, "point %d x", i);
    check(nm, cur[i].x, x);
    sprintf(nm, "point %d y", i);
    check(nm, cur[i].y, y);
}

/* opcodes */
enum {
    SVTCA_Y = 0x00, SVTCA_X = 0x01, SPVTCA_Y = 0x02, SPVTCA_X = 0x03, SFVTCA_Y = 0x04, SFVTCA_X = 0x05,
    SPVTL0 = 0x06, SPVTL1 = 0x07, SFVTL0 = 0x08, SPVFS = 0x0A, SFVFS = 0x0B, GPV = 0x0C, GFV = 0x0D,
    SFVTPV = 0x0E, ISECT = 0x0F, SRP0 = 0x10, SRP1 = 0x11, SRP2 = 0x12, SZP0 = 0x13, SZP1 = 0x14, SZP2 = 0x15,
    SZPS = 0x16, SLOOP = 0x17, RTG = 0x18, RTHG = 0x19, SMD = 0x1A, ELSE = 0x1B, JMPR = 0x1C, SCVTCI = 0x1D,
    SSWCI = 0x1E, SSW = 0x1F, DUP = 0x20, POP = 0x21, CLEAR = 0x22, SWAP = 0x23, DEPTH = 0x24, CINDEX = 0x25,
    MINDEX = 0x26, ALIGNPTS = 0x27, UTP = 0x29, LOOPCALL = 0x2A, CALL = 0x2B, FDEF = 0x2C, ENDF = 0x2D,
    MDAP0 = 0x2E, MDAP1 = 0x2F, IUP_Y = 0x30, IUP_X = 0x31, SHP0 = 0x32, SHP1 = 0x33, SHC0 = 0x34, SHZ0 = 0x36,
    SHPIX = 0x38, IP = 0x39, MSIRP0 = 0x3A, MSIRP1 = 0x3B, ALIGNRP = 0x3C, RTDG = 0x3D, MIAP0 = 0x3E,
    MIAP1 = 0x3F, NPUSHB = 0x40, NPUSHW = 0x41, WS = 0x42, RS = 0x43, WCVTP = 0x44, RCVT = 0x45, GC0 = 0x46,
    GC1 = 0x47, SCFS = 0x48, MD0 = 0x49, MD1 = 0x4A, MPPEM = 0x4B, MPS = 0x4C, FLIPON = 0x4D, FLIPOFF = 0x4E,
    LT = 0x50, LTEQ = 0x51, GT = 0x52, GTEQ = 0x53, EQ = 0x54, NEQ = 0x55, ODD = 0x56, EVEN = 0x57, IF = 0x58,
    EIF = 0x59, AND = 0x5A, OR = 0x5B, NOT = 0x5C, DELTAP1 = 0x5D, SDB = 0x5E, SDS = 0x5F, ADD = 0x60,
    SUB = 0x61, DIV = 0x62, MUL = 0x63, ABS = 0x64, NEG = 0x65, FLOOR = 0x66, CEILING = 0x67, ROUND0 = 0x68,
    NROUND0 = 0x6C, WCVTF = 0x70, DELTAP2 = 0x71, DELTAC1 = 0x73, SROUND = 0x76, S45ROUND = 0x77, JROT = 0x78,
    JROF = 0x79, ROFF = 0x7A, RUTG = 0x7C, RDTG = 0x7D, FLIPPT = 0x80, FLIPRGON = 0x81, FLIPRGOFF = 0x82,
    SCANCTRL = 0x85, SDPVTL0 = 0x86, GETINFO = 0x88, IDEF = 0x89, ROLL = 0x8A, MAX = 0x8B, MIN = 0x8C,
    SCANTYPE = 0x8D, INSTCTRL = 0x8E, PUSHB1 = 0xB0, PUSHB2 = 0xB1, PUSHB3 = 0xB2, PUSHB4 = 0xB3,
    PUSHB5 = 0xB4, PUSHW1 = 0xB8, PUSHW2 = 0xB9, MDRP = 0xC0, MIRP = 0xE0
};
/* MDRP/MIRP flags */
#define RP0 16
#define MINDIST 8
#define RND 4


static void t_stack(void)
{
    what = "push";
    setup();
    RUN(NPUSHB, 3, 1, 200, 255, PUSHW2, 0xFF, 0xFE, 0x12, 0x34, NPUSHW, 1, 0x80, 0x00);
    stk(6, 1L, 200L, 255L, -2L, 0x1234L, -32768L);
    what = "DUP SWAP POP DEPTH";
    setup();
    RUN(PUSHB2, 7, 9, DUP, SWAP, POP, DEPTH);
    stk(3, 7L, 9L, 2L);
    what = "CLEAR";
    setup();
    RUN(PUSHB3, 1, 2, 3, CLEAR, PUSHB1, 4);
    stk(1, 4L);
    what = "ROLL";                      /* a b c -> b c a */
    setup();
    RUN(PUSHB3, 1, 2, 3, ROLL);
    stk(3, 2L, 3L, 1L);
    what = "CINDEX";                    /* copies the kth: 1 the top */
    setup();
    RUN(PUSHB4, 10, 20, 30, 3, CINDEX);
    stk(4, 10L, 20L, 30L, 10L);
    what = "MINDEX";                    /* moves it */
    setup();
    RUN(PUSHB4, 10, 20, 30, 3, MINDEX);
    stk(3, 20L, 30L, 10L);
    what = "underflow";
    setup();
    RUN(POP);
    check("error", bc.error, BC_ESTACK);
}

static void t_arith(void)
{
    what = "ADD SUB";
    setup();
    RUN(PUSHB3, 100, 30, 5, SUB, ADD);  /* 100 + (30 - 5) */
    stk(1, 125L);
    what = "MUL";                       /* 2.0 * 3.0 = 6.0; -1.5 * 0.5 = -0.75 */
    setup();
    RUN(PUSHB2, 128, 192, MUL, PUSHW2, 0xFF, 0xA0, 0x00, 0x20, MUL);
    stk(2, 384L, -48L);
    what = "DIV";                       /* 1.0 / 3.0 = 0.328125 (21/64, truncated); -1.0 / 3.0 */
    setup();
    RUN(PUSHB2, 64, 192, DIV, PUSHW2, 0xFF, 0xC0, 0x00, 0xC0, DIV);
    stk(2, 21L, -21L);
    what = "DIV by 0";
    setup();
    RUN(PUSHB2, 64, 0, DIV);
    check("error", bc.error, BC_EDIV);
    what = "ABS NEG FLOOR CEILING";     /* -1.5: 1.5; floor(-0.5) = -1; ceiling(0.25) = 1 */
    setup();
    RUN(PUSHW1, 0xFF, 0xA0, ABS, PUSHW1, 0xFF, 0xE0, FLOOR, PUSHB1, 16, CEILING, PUSHB1, 5, NEG);
    stk(4, 96L, -64L, 64L, -5L);
    what = "MAX MIN";
    setup();
    RUN(PUSHW2, 0xFF, 0xFF, 0x00, 0x03, MAX, PUSHW2, 0xFF, 0xFF, 0x00, 0x03, MIN);
    stk(2, 3L, -1L);
    what = "comparisons";               /* e1 (deeper) op e2 */
    setup();
    RUN(PUSHB2, 1, 2, LT, PUSHB2, 2, 2, LTEQ, PUSHB2, 1, 2, GT, PUSHB2, 2, 2, GTEQ, PUSHB2, 3, 3, EQ,
        PUSHB2, 3, 3, NEQ);
    stk(6, 1L, 1L, 0L, 1L, 1L, 0L);
    what = "AND OR NOT";
    setup();
    RUN(PUSHB2, 5, 0, AND, PUSHB2, 5, 0, OR, PUSHB1, 0, NOT);
    stk(3, 0L, 1L, 1L);
    what = "ODD EVEN";                  /* rounded first (RTG): 1.5 -> 2 even; 1.25 -> 1 odd */
    setup();
    RUN(PUSHB1, 96, ODD, PUSHB1, 96, EVEN, PUSHB1, 80, ODD);
    stk(3, 0L, 1L, 1L);
}

/* ROUND[] of v in each round state */
static long rnd(ttfx_u8 state, long v)
{
    ttfx_u8 c[8];
    setup();
    c[0] = state;
    c[1] = PUSHW1;
    c[2] = (ttfx_u8)((v >> 8) & 0xFF);
    c[3] = (ttfx_u8)(v & 0xFF);
    c[4] = ROUND0;
    run(2, c, 5);
    return bc.sp == 1 ? stack[0] : -99999;
}

static long sround(ttfx_u8 op, ttfx_u8 mode, long v)
{
    ttfx_u8 c[8] = { PUSHB1, 0, 0, PUSHW1, 0, 0, ROUND0 };
    setup();
    c[1] = mode;
    c[2] = op;
    c[4] = (ttfx_u8)((v >> 8) & 0xFF);
    c[5] = (ttfx_u8)(v & 0xFF);
    run(2, c, 7);
    return bc.sp == 1 ? stack[0] : -99999;
}

static void t_round(void)
{
    what = "RTG";                       /* to the nearest pixel, the sign kept */
    check("1.4", rnd(RTG, 90), 64);
    check("1.5", rnd(RTG, 96), 128);
    check("-1.5", rnd(RTG, -96), -128);
    check("0.4", rnd(RTG, 26), 0);
    what = "RTHG";                      /* to the nearest half pixel */
    check("1.2", rnd(RTHG, 77), 96);
    check("0", rnd(RTHG, 0), 32);
    check("-1.9", rnd(RTHG, -122), -96);
    what = "RTDG";                      /* to the nearest half or whole */
    check("1.2", rnd(RTDG, 77), 64);
    check("1.3", rnd(RTDG, 83), 96);
    what = "RDTG";
    check("1.9", rnd(RDTG, 122), 64);
    check("-1.9", rnd(RDTG, -122), -64);
    what = "RUTG";
    check("1.1", rnd(RUTG, 70), 128);
    check("-1.1", rnd(RUTG, -70), -128);
    what = "ROFF";
    check("1.1", rnd(ROFF, 70), 70);
    /* the specification's example: SROUND(01:01:1000), period 1, phase
       1/4, threshold 4/8: [-0.25, 0.75) to 0.25, [0.75, 1.75) to 1.25 */
    what = "SROUND 01:01:1000";
    check("0.5", sround(SROUND, 0x58, 32), 16);
    check("0.75", sround(SROUND, 0x58, 48), 80);
    check("1.5", sround(SROUND, 0x58, 96), 80);
    check("-0.5", sround(SROUND, 0x58, -32), -16);
    /* threshold 0 (period - 1): as CEILING; period 2 */
    what = "SROUND 10:00:0000";
    check("0.1", sround(SROUND, 0x80, 6), 128);
    check("2.0", sround(SROUND, 0x80, 128), 128);
    /* S45ROUND: the grid sqrt(2)/2 (45/64 here); period 1, phase 0,
       threshold 1/2: 1.0 -> 45/64 */
    what = "S45ROUND 01:00:1000";
    check("1.0", sround(S45ROUND, 0x48, 64), 45);
}

static void t_flow(void)
{
    what = "IF ELSE EIF";
    setup();
    RUN(PUSHB1, 1, IF, PUSHB1, 10, ELSE, PUSHB1, 20, EIF, PUSHB1, 0, IF, PUSHB1, 30, ELSE, PUSHB1, 40, EIF);
    stk(2, 10L, 40L);
    what = "nested IF";                 /* the inner IF skipped whole, PUSHB data and all */
    setup();
    RUN(PUSHB1, 0, IF, PUSHB1, 1, IF, PUSHB2, 0x59, 0x1B, EIF, ELSE, PUSHB1, 7, EIF);
    stk(1, 7L);
    what = "JMPR";                      /* from the JMPR itself: over PUSHB1 1 */
    setup();
    RUN(PUSHB1, 3, JMPR, PUSHB1, 1, PUSHB1, 2);
    stk(1, 2L);
    what = "JROT JROF";
    setup();
    RUN(PUSHB2, 3, 1, JROT, PUSHB1, 1, PUSHB2, 4, 1, JROF, PUSHB1, 2);
    stk(1, 2L);
    what = "FDEF CALL LOOPCALL";
    setup();
    RUN0(PUSHB1, 3, FDEF, PUSHB1, 1, ADD, ENDF, PUSHB2, 10, 3, CALL, PUSHB2, 4, 3, LOOPCALL);
    stk(1, 15L);
    what = "IDEF";                      /* an undefined opcode, 0x91, defined */
    setup();
    RUN0(PUSHB1, 0x91, IDEF, PUSHB1, 42, ENDF, 0x91, 0x91);
    stk(2, 42L, 42L);
    what = "FDEF in a glyph program";
    setup();
    RUN(PUSHB1, 1, FDEF, ENDF);
    check("error", bc.error, BC_EFUNC);
    what = "undefined opcode";
    setup();
    RUN(0x83);
    check("error", bc.error, BC_EOPCODE);
    what = "WS RS";
    setup();
    RUN(PUSHB2, 5, 99, WS, PUSHB1, 5, RS);
    stk(1, 99L);
    what = "WCVTP RCVT WCVTF";          /* WCVTF in font units: 1 unit = 1 pixel here */
    setup();
    RUN(PUSHB2, 2, 77, WCVTP, PUSHB1, 2, RCVT, PUSHB2, 3, 5, WCVTF, PUSHB1, 3, RCVT);
    stk(2, 77L, 320L);
    what = "MPPEM MPS GETINFO";         /* GETINFO 1: version 3 (Windows 3.1); 6: not rotated or stretched */
    setup();
    RUN(MPPEM, MPS, PUSHB1, 1, GETINFO, PUSHB1, 6, GETINFO);
    stk(4, 12L, 9L * 64, 3L, 0L);
    what = "INSTCTRL";                  /* only in prep */
    setup();
    bc.prog = 1;
    {
        static const ttfx_u8 c[] = { PUSHB2, 1, 1, INSTCTRL };
        run(1, c, sizeof c);
    }
    check("instctrl", bc.gs.instctrl, 1);
}

static void t_vectors(void)
{
    what = "SVTCA SPVTCA SFVTCA GPV GFV";
    setup();
    RUN(SVTCA_Y, GPV, GFV, SPVTCA_X, SFVTCA_Y, GPV, GFV);
    stk(8, 0L, 0x4000L, 0L, 0x4000L, 0x4000L, 0L, 0L, 0x4000L);
    what = "SPVTL";                     /* from point 0 (1,1) to 2 (5,5): 45 degrees */
    setup();
    RUN(PUSHB2, 2, 0, SPVTL0, GPV);
    stk(2, 0x2D41L, 0x2D41L);
    what = "SPVTL[1]";                  /* turned counter-clockwise: (-0.707, 0.707) */
    setup();
    RUN(PUSHB2, 2, 0, SPVTL1, GPV);
    stk(2, -0x2D41L, 0x2D41L);
    what = "SPVTL of -2^31 each way";   /* a line no glyph has: halved first, not doubled to 0 for ever */
    setup();
    cur[0].x = cur[0].y = 0;
    cur[2].x = cur[2].y = -0x7FFFFFFFL - 1;
    RUN(PUSHB2, 2, 0, SPVTL0, GPV);
    stk(2, -0x2D41L, -0x2D41L);
    what = "SPVFS SFVTPV";
    setup();
    RUN(PUSHW2, 0x2D, 0x41, 0x2D, 0x41, SPVFS, SFVTPV, GFV);
    stk(2, 0x2D41L, 0x2D41L);
    what = "GC along 45 degrees";       /* point 2 (5,5): 5 * sqrt 2 = 7.07 (each term rounded) */
    setup();
    RUN(PUSHB2, 2, 0, SPVTL0, PUSHB1, 2, GC0);
    stk(1, 452L);
    what = "SDPVTL";                    /* the dual vector from the original positions */
    setup();
    cur[2].x = 9 * 64;                  /* moved: the projection vector from the current */
    RUN(PUSHB2, 2, 0, SDPVTL0, PUSHB1, 2, GC1);
    stk(1, 452L);
}

static void t_moves(void)
{
    what = "MDAP[1]";                   /* x of point 2 (5,5) moved to 5.3, rounded back */
    setup();
    cur[2].x = 339;
    RUN(SVTCA_X, PUSHB1, 2, MDAP1);
    pt(2, 320, 320);
    check("touched x", flags[2] & BC_TX, BC_TX);
    check("rp0", bc.gs.rp[0], 2);
    what = "MDAP[0]";                   /* touched, not moved */
    setup();
    cur[2].y = 339;
    RUN(SVTCA_Y, PUSHB1, 2, MDAP0);
    pt(2, 320, 339);
    check("touched y", flags[2] & BC_TY, BC_TY);
    what = "MIAP[1]";                   /* CVT 4: 4.25, within the cut-in of y 5: 4 */
    setup();
    RUN(SVTCA_Y, PUSHB2, 2, 4, MIAP1);
    pt(2, 320, 256);
    what = "MIAP[1] past the cut-in";   /* CVT 1: 1.25 is 3.75 from 5: 5 rounded */
    setup();
    cur[2].y = 330;
    RUN(SVTCA_Y, PUSHB2, 2, 1, MIAP1);
    pt(2, 320, 320);
    what = "MIAP[0]";                   /* CVT 1: 1.25, unrounded, no cut-in */
    setup();
    RUN(SVTCA_Y, PUSHB2, 2, 1, MIAP0);
    pt(2, 320, 80);
    what = "MIAP in the twilight zone"; /* creates the point: original too */
    setup();
    RUN(SVTCA_X, PUSHB1, 0, SZP0, PUSHB2, 1, 3, MIAP0);
    check("twilight cur x", tcur[1].x, 208);
    check("twilight org x", torg[1].x, 208);
    what = "MDRP";                      /* point 5 (3,2) from rp0 = 4 (2,2) moved to 2.4: 3.4 */
    setup();
    cur[4].x = 154;
    RUN(SVTCA_X, PUSHB2, 5, 4, SRP0, MDRP);
    pt(5, 218, 128);
    check("rp1", bc.gs.rp[1], 4);
    check("rp2", bc.gs.rp[2], 5);
    what = "MDRP[rnd]";                 /* 1 pixel rounded, from 2.4: 3.4 */
    setup();
    cur[4].x = 154;
    RUN(SVTCA_X, PUSHB2, 5, 4, SRP0, MDRP | RND);
    pt(5, 218, 128);
    what = "MDRP[min]";                 /* original distance 0 (y of 4 and 5): at least 1 */
    setup();
    RUN(SVTCA_Y, PUSHB2, 5, 4, SRP0, MDRP | MINDIST | RP0);
    pt(5, 192, 192);
    check("rp0", bc.gs.rp[0], 5);
    what = "MDRP single width";         /* 1 px, within the cut-in (1.25) of the single width 2: 2 */
    setup();
    RUN(SVTCA_X, PUSHB2, 2, 80, SSWCI, SSW, PUSHB2, 5, 4, SRP0, MDRP);
    pt(5, 256, 128);
    what = "MIRP";                      /* CVT 1 (1.25) from rp0 4: x 3.25 */
    setup();
    RUN(SVTCA_X, PUSHB1, 4, SRP0, PUSHB2, 5, 1, MIRP);
    pt(5, 208, 128);
    what = "MIRP[rnd]";                 /* 1.25 rounded: 1 */
    setup();
    RUN(SVTCA_X, PUSHB1, 4, SRP0, PUSHB2, 5, 1, MIRP | RND);
    pt(5, 192, 128);
    what = "MIRP[rnd] past the cut-in"; /* point 9 (8,2) from 4: 6 px, CVT 1 1.25: the original */
    setup();
    RUN(SVTCA_X, PUSHB1, 4, SRP0, PUSHB2, 9, 1, MIRP | RND);
    pt(9, 512, 128);
    what = "MIRP auto-flip";            /* point 4 from 9: -6 px; CVT 5 (5.25) flipped: -5.25 */
    setup();
    RUN(SVTCA_X, PUSHB1, 9, SRP0, PUSHB2, 4, 5, MIRP);
    pt(4, 512 - 336, 128);
    what = "MIRP without auto-flip";    /* +5.25 */
    setup();
    RUN(FLIPOFF, SVTCA_X, PUSHB1, 9, SRP0, PUSHB2, 4, 5, MIRP);
    pt(4, 512 + 336, 128);
    what = "MIRP[min]";                 /* CVT 0 (0.25) from 4, at least 1 */
    setup();
    RUN(SVTCA_X, PUSHB1, 4, SRP0, PUSHB2, 5, 0, MIRP | MINDIST);
    pt(5, 192, 128);
    what = "MSIRP[1]";                  /* 2.5 from rp0 4 */
    setup();
    RUN(SVTCA_X, PUSHB1, 4, SRP0, PUSHB2, 5, 160, MSIRP1);
    pt(5, 288, 128);
    check("rp0", bc.gs.rp[0], 5);
    what = "MSIRP into the twilight zone";      /* the specification's example */
    setup();
    cur[7].x = org[7].x = 3 * 64;
    cur[7].y = org[7].y = 4 * 64;
    RUN(SVTCA_X, PUSHB1, 7, SRP0, PUSHB1, 0, SZP1, PUSHB2, 1, 48, MSIRP0);
    check("twilight org x", torg[1].x, 3 * 64 + 48);
    check("twilight org y", torg[1].y, 4 * 64);
    what = "ALIGNRP";                   /* points 5 and 6 to x of rp0 4, SLOOP 2 */
    setup();
    RUN(SVTCA_X, PUSHB1, 4, SRP0, PUSHB3, 5, 6, 2, SLOOP, ALIGNRP);
    pt(5, 128, 128);
    pt(6, 128, 128);
    check("loop", bc.gs.loop, 1);
    what = "ALIGNPTS";                  /* 4 (2,2) and 5 (3,2) meet at 2.5 */
    setup();
    RUN(SVTCA_X, PUSHB2, 4, 5, ALIGNPTS);
    pt(4, 160, 128);
    pt(5, 160, 128);
    what = "IP";                        /* rp1 4 (2) to 6, rp2 9 (8) to 10: point 5 (3), 1/6 along, to 6 2/3 */
    setup();
    cur[4].x = 6 * 64;
    cur[9].x = 10 * 64;
    RUN(SVTCA_X, PUSHB2, 4, 9, SRP2, SRP1, PUSHB1, 5, IP);
    pt(5, 427, 128);
    what = "SHP[0]";                    /* rp2 moved +1 in x: point 5 too */
    setup();
    cur[4].x += 64;
    RUN(SVTCA_X, PUSHB2, 5, 4, SRP2, SHP0);
    pt(5, 256, 128);
    what = "SHP[1]";                    /* rp1 */
    setup();
    cur[4].y += 32;
    RUN(SVTCA_Y, PUSHB2, 5, 4, SRP1, SHP1);
    pt(5, 192, 160);
    what = "SHC";                       /* contour 0 shifted with rp2 4 (not in it) */
    setup();
    cur[4].x += 64;
    RUN(SVTCA_X, PUSHB2, 0, 4, SRP2, SHC0);
    pt(0, 128, 64);
    pt(3, 384, 64);
    pt(5, 192, 128);
    what = "SHZ";                       /* the zone shifted, not the reference point, untouched */
    setup();
    cur[4].y += 64;
    RUN(SVTCA_Y, PUSHB2, 1, 4, SRP2, SHZ0);
    pt(0, 64, 128);
    pt(4, 128, 192);
    pt(9, 512, 192);
    check("point 0 untouched", flags[0] & BC_TY, 0);
    what = "SHPIX";                     /* 80/64 along the freedom vector (x), two points */
    setup();
    RUN(SVTCA_X, PUSHB3, 5, 6, 2, SLOOP, PUSHB1, 80, SHPIX);
    pt(5, 192 + 80, 128);
    pt(6, 256 + 80, 128);
    what = "SHPIX along y";             /* the projection vector does not matter */
    setup();
    RUN(SPVTCA_X, SFVTCA_Y, PUSHB2, 5, 64, SHPIX);
    pt(5, 192, 192);
    what = "ISECT";                     /* line 0-2 (the diagonal) and 1-3: (3,3) */
    setup();
    RUN(PUSHB5, 9, 0, 2, 1, 3, ISECT);
    pt(9, 192, 192);
    what = "ISECT parallel";            /* lines 4-5 and 6-7: their middle */
    setup();
    RUN(PUSHB5, 9, 4, 5, 6, 7, ISECT);
    pt(9, (128 + 192 + 256 + 384) / 4, 128);
    what = "SCFS";
    setup();
    RUN(SVTCA_Y, PUSHB2, 5, 100, SCFS);
    pt(5, 192, 100);
    what = "MD";                        /* p2 (popped second, 4) less p1 (9) */
    setup();
    cur[9].x = 600;
    RUN(SVTCA_X, PUSHB2, 4, 9, MD0, PUSHB2, 4, 9, MD1);
    stk(2, 128L - 600, 128L - 512);
    what = "UTP";
    setup();
    RUN(SVTCA_X, PUSHB1, 4, MDAP0, PUSHB1, 4, UTP);
    check("touched", flags[4] & BC_TX, 0);
    what = "FLIPPT FLIPRGOFF FLIPRGON";
    setup();
    RUN(PUSHB1, 4, FLIPPT, PUSHB2, 5, 7, FLIPRGOFF, PUSHB2, 6, 6, FLIPRGON);
    check("4", flags[4] & BC_ON, 0);
    check("5", flags[5] & BC_ON, 0);
    check("6", flags[6] & BC_ON, BC_ON);
    check("7", flags[7] & BC_ON, 0);
    what = "bad point";
    setup();
    RUN(PUSHB1, 40, MDAP0);
    check("error", bc.error, BC_EPOINT);
}

static void t_iup(void)
{
    /* the specification's example: touched points 4 and 9 moved; 5-8
       between them in x interpolated (5 at 3 of 2..8: 1/6 of the way),
       none outside */
    what = "IUP[x] interpolates";
    setup();
    cur[4].x = 4 * 64;                  /* 2 -> 4 */
    cur[9].x = 16 * 64;                 /* 8 -> 16 */
    flags[4] |= BC_TX;
    flags[9] |= BC_TX;
    RUN(IUP_X);
    pt(5, 6 * 64, 128);                 /* 3: 4 + 1/6 * 12 = 6 */
    pt(8, 14 * 64, 128);                /* 7: 4 + 5/6 * 12 = 14 */
    /* a point outside its neighbours' range shifts with the nearer one:
       contour 0's point 1 (1,5) and 2 (5,6 here) touched in y, 0 and 3
       (y 1) below both: they shift as 1, the lower, does */
    what = "IUP[y] shifts";
    setup();
    org[2].y = ous[2].y = 6 * 64;
    cur[1].y = 6 * 64;                  /* +1 */
    cur[2].y = 8 * 64;                  /* +2 */
    flags[1] |= BC_TY;
    flags[2] |= BC_TY;
    RUN(IUP_Y);
    check("point 0 y", cur[0].y, 64 + 64);
    check("point 3 y", cur[3].y, 64 + 64);
    what = "IUP one touched point";     /* the whole contour moves with it */
    setup();
    cur[5].x += 32;
    flags[5] |= BC_TX;
    RUN(IUP_X);
    pt(4, 128 + 32, 128);
    pt(9, 512 + 32, 128);
    pt(0, 64, 64);                      /* (the other contour: nothing touched) */
}

static void t_delta(void)
{
    /* the specification's example: at 12 ppem (delta base 9), point 15
       1/8 pixel along the freedom vector: arg 56; here point 5 */
    what = "DELTAP1";
    setup();
    RUN(SVTCA_X, PUSHB3, 56, 5, 1, DELTAP1);
    pt(5, 192 + 8, 128);
    what = "DELTAP1 at another size";
    setup();
    bc.ppemx = bc.ppemy = 13;
    RUN(SVTCA_X, PUSHB3, 56, 5, 1, DELTAP1);
    pt(5, 192, 128);
    what = "DELTAP2";                   /* 9 + 16 + 3 = 28 ppem, -1 step (exception 7) at shift 0 */
    setup();
    bc.ppemx = bc.ppemy = 28;
    RUN(SVTCA_X, PUSHB1, 0, SDS, PUSHB3, 0x37, 5, 1, DELTAP2);
    pt(5, 192 - 64, 128);
    what = "DELTAC1";                   /* CVT 2 (2.25) + 1/8 at 12 */
    setup();
    RUN(PUSHB3, 56, 2, 1, DELTAC1, PUSHB1, 2, RCVT);
    stk(1, 144L + 8);
    what = "SDB";                       /* base 10: 12 ppem is 2 */
    setup();
    RUN(SVTCA_X, PUSHB1, 10, SDB, PUSHB3, 0x28, 5, 1, DELTAP1);
    pt(5, 192 + 8, 128);
}

static void t_scan(void)
{
    ttfx__gs g;
    ttfx__bcDefaults(&g);
    what = "SCANCTRL";
    g.scan_control = 0x01FF;            /* always */
    check("0x01FF at 100", ttfx__bcDropout(&g, 100, 0), TTFX_DROP_SIMPLE);
    g.scan_control = 0x0110;            /* up to 16 ppem */
    check("0x0110 at 16", ttfx__bcDropout(&g, 16, 0), TTFX_DROP_SIMPLE);
    check("0x0110 at 17", ttfx__bcDropout(&g, 17, 0), TTFX_DROP_NONE);
    g.scan_control = 0x0A10;            /* rotated and up to 16: never here */
    check("0x0A10 at 12", ttfx__bcDropout(&g, 12, 0), TTFX_DROP_NONE);
    g.scan_control = 0x0400;            /* stretched */
    check("0x0400 stretched", ttfx__bcDropout(&g, 12, 1), TTFX_DROP_SIMPLE);
    check("0x0400 square", ttfx__bcDropout(&g, 12, 0), TTFX_DROP_NONE);
    g.scan_control = 0;
    check("0", ttfx__bcDropout(&g, 12, 0), TTFX_DROP_NONE);
    what = "SCANTYPE";
    g.scan_control = 0x01FF;
    g.scan_type = 1;
    check("1", ttfx__bcDropout(&g, 12, 0), TTFX_DROP_NOSTUBS);
    g.scan_type = 2;
    check("2", ttfx__bcDropout(&g, 12, 0), TTFX_DROP_NONE);
    setup();
    RUN(PUSHB1, 1, SCANTYPE, PUSHB1, 4, SCANTYPE);  /* 4: a later mode, ignored */
    check("type after 1, 4", bc.gs.scan_type, 1);
    setup();
    RUN(PUSHW1, 0x01, 0xFF, SCANCTRL);
    check("scan control", bc.gs.scan_control, 0x1FF);
}

int main(int argc, char **argv)
{
    verbose = argc > 1 && !strcmp(argv[1], "-v");
    t_stack();
    t_arith();
    t_round();
    t_flow();
    t_vectors();
    t_moves();
    t_iup();
    t_delta();
    t_scan();
    printf("ttfbctest: %d checks, %d failed\n", cases, fails);
    return fails != 0;
}
