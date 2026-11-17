/*
 * lzexpand.h - w16's LZEXPAND.DLL, internal definitions (ARCHITECTURE.md,
 * LZEXPAND).
 *
 * Sources: Windows 3.1 SDK, Programmer's Reference Vol. 1 ch. 9 and Vol. 2
 * (CopyLZFile, GetExpandedName, LZClose, LZCopy, LZDone, LZInit,
 * LZOpenFile, LZRead, LZSeek, LZStart); the publicly documented "SZDD"
 * format of COMPRESS.EXE (tools/szddenc.py); everything else observed
 * black-box on Windows 3.1 with the probe tests/lzexpand/lzp.c.
 */
#ifndef W16_LZEXPAND_H
#define W16_LZEXPAND_H

#include <windows.h>

#define LZAPI __loadds FAR PASCAL        /* exported by lzexpand.lnk */

/* error values (SDK Vol. 2) */
#define LZERROR_BADINHANDLE  (-1)
#define LZERROR_BADOUTHANDLE (-2)
#define LZERROR_READ         (-3)
#define LZERROR_WRITE        (-4)
#define LZERROR_GLOBALLOC    (-5)
#define LZERROR_GLOBLOCK     (-6)
#define LZERROR_BADVALUE     (-7)
#define LZERROR_UNKNOWNALG   (-8)

/* 3.1's handles for compressed files: 400h + slot, 16 slots (SDK: "a
   maximum of 16 compressed files"; the values observed) */
#define LZ_BASE   0x400
#define LZ_SLOTS  16
#define IS_LZ(h)  ((h) >= LZ_BASE && (h) < LZ_BASE + LZ_SLOTS)

/* the format: a 14-byte header, a 4096-byte ring started at 4096 - 16 */
#define HDR_SIZE  14
#define RING      4096
#define RING_START (RING - 16)
#define INBUF     1024                  /* observed: reads of 1024 bytes */

/* LZRead expands in passes (observed, ARCHITECTURE.md): a match is
   started while fewer than PASS_MATCH bytes of the pass are out, a
   literal while fewer than PASS_LIT; a pass longer than PASS_MAX (only
   17- and 18-byte matches make one) fails the read */
#define PASS_MATCH 1009
#define PASS_LIT   1010
#define PASS_SKIP  2                    /* less while seeking forward */
#define PASS_MAX   1024

/* the decoder of one compressed file */
typedef struct {
    HFILE dos;                          /* the file */
    long size;                          /* expanded size, from the header */
    long want;                          /* the position LZSeek/LZRead see */
    long count;                         /* bytes expanded so far */
    long passA;                         /* where the last pass began */
    int bad;                            /* a failed read: start again */
    int eoferr;                         /* the data ended: reads from */
    long eofA;                          /* here on fail (observed) */
    BYTE shortnow;                      /* the first read of the file was
                                           its end: the pass ends */
    BYTE needseek;                      /* expanding starts again: set the
                                           file's pointer first */
    WORD rpos;                          /* ring index of byte `count` */
    BYTE FAR *inp;                      /* the input buffer (in, or LZCopy's) */
    WORD inmax;                         /* its size */
    long inoff;                         /* file offset of inp[0] */
    WORD inlen, inpos;                  /* bytes in it, the next one */
    BYTE flags, nflags;                 /* the current flag byte */
    BYTE pend;                          /* an item read but not used yet */
    BYTE plen;                          /* its length (1: a literal) */
    WORD poff;                          /* a match's ring position */
    BYTE plit;                          /* a literal's byte */
    BYTE ring[RING];
    BYTE in[INBUF];
} LZS;

#define OUTBUF    0x8000U               /* LZCopy's writes */
#define CPYIN     0x8000U               /* LZCopy's reads (observed: where
                                           it leaves the source) */

#endif
