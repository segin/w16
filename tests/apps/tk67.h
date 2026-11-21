/*
 * tk67.h - shared by the WIN87EM audit probe's two-task part TK67P4.EXE
 * (tk67.c, TK67_P=4) and its helper task TK67H.EXE (tk67h.c). TK67H's
 * command line: "<TK67P4's DS> <offset of a tk67sh> <mode>", in hex; it
 * writes what it saw there, then ends after __fpMath 2. Mode 1: after
 * setting its handler it sets ready and waits for go (TK67P10's
 * exception meanwhile), then takes an exception of its own; mode 2: it
 * ends without __fpMath 2 (TK67P11).
 */
typedef struct {
    WORD done;          /* set last, with 1 */
    WORD task;          /* TK67H's task */
    WORD cw0;           /* __fpMath 5 before its first __fpMath 0 */
    WORD depth0;        /* __fpMath 10 before it */
    WORD cw1;           /* __fpMath 5 after __fpMath 0 */
    WORD depth1;        /* __fpMath 10 after it */
    WORD good;          /* of nine values pushed and popped, how many came
                           back in order */
    WORD depth2;        /* __fpMath 10 afterwards */
    WORD hcalls;        /* calls of TK67H's own handler */
    WORD ready;         /* mode 1: TK67H waits for go */
    WORD go;
    WORD hcalls1;       /* mode 1: its handler's calls after go */
    WORD hcalls2;       /* and after its own exception */
} tk67sh;
