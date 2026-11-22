/*
 * para.h - w16 Windows Help: the items of a display link (|TOPIC record
 * types 20h and 23h: text and tables; 1: Windows 3.0 text). Portable, as
 * hlp.h. Format: LinkData1 holds the paragraph's formatting and a list of
 * commands; LinkData2 the text, as NUL-terminated pieces: a piece, then a
 * command, then the next piece, until the command FFh (sources: hlp.h).
 */
#ifndef W16_PARA_H
#define W16_PARA_H
#include "hlp.h"

#define HLP_MAXTABS 32
#define HLP_MAXCOLS 32

typedef struct {
    WORD bits;              /* the flags word as stored */
    short above, below, lines;  /* spacing */
    short left, right, first;   /* indents */
    BYTE border;            /* 1 box, 2 top, 4 left, 8 bottom, 10h right, 20h thick, 40h double */
    short borderwidth;
    WORD ntabs;
    WORD tab[HLP_MAXTABS];      /* position */
    BYTE tabtype[HLP_MAXTABS];  /* 0 left, 1 right, 2 centre */
    short column;           /* tables: the cell's column, -1 the end */
} HLPPARA;

#define PB_RIGHT    0x0400
#define PB_CENTER   0x0800
#define PB_NOWRAP   0x1000

typedef struct {
    BYTE ncols, tabletype;
    short minwidth;
    short gap[HLP_MAXCOLS], width[HLP_MAXCOLS];
} HLPTABLE;

/* items */
#define PI_END      0       /* the link is done */
#define PI_PARA     1       /* a new paragraph format (it.para); tables: a new cell */
#define PI_TEXT     2       /* it.text, it.len */
#define PI_FONT     3       /* it.num */
#define PI_BREAK    4       /* line break (\line) */
#define PI_PAREND   5       /* end of paragraph (\par) */
#define PI_TAB      6
#define PI_PICT     7       /* it.align (0 char, 1 left, 2 right), it.pictype, it.data/it.dlen */
#define PI_HOTEND   8       /* end of a hot spot */
#define PI_JUMP     9       /* it.code (E0h-EFh), it.off; external: it.file, it.window */
#define PI_MACRO    10      /* it.code (C8h, CCh), it.text */
#define PI_NBSP     11
#define PI_NBHYPH   12
#define PI_CELLEND  13      /* tables: a cell's commands end (FFh), more follow */

typedef struct {
    /* the link */
    BYTE type;
    LPCB d1, e1;            /* LinkData1, read point and end */
    LPCB d2, e2;            /* LinkData2 */
    HLPTABLE table;
    DWORD topicsize;
    WORD topiclength;
    BOOL pending;           /* a text piece is due before the next command */
    BOOL inpara;            /* paragraph info read for this run */
    BOOL done;
    /* the item */
    HLPPARA para;
    const char FAR *text;
    WORD len;
    WORD num;
    BYTE code, align, pictype;
    DWORD off;
    LPCB data;
    DWORD dlen;
    WORD hotspots;
    const char FAR *file, FAR *window;
    BYTE wnum;
} HLPITER;

BOOL para_begin(HLPITER FAR *it, BYTE type, LPCB d1, DWORD n1, LPCB d2, DWORD n2);
int para_next(HLPITER FAR *it);

#endif
