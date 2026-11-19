/*
 * tk62.h - what TK62 (tests/apps/tk62.c) and its helper task TK62A
 * (tk62a.c) share: TK62 passes the address of this record on TK62A's
 * command line (DS and offset in hex).
 */
typedef struct tk62sh {
    WORD done;          /* TK62A got this far */
    WORD big;           /* its 1 MB block was allocated */
    WORD hit;           /* its second block lies right after its first */
    WORD task;          /* its hTask */
} tk62sh;
