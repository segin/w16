/*
 * tk64.h - what TK64 (tests/apps/tk64.c, KERNEL's FileCdr) and its helper
 * task TK64A (tk64a.c) share: TK64 passes the address of this record on
 * TK64A's command line (DS and offset in hex), then a letter:
 *   F  file operations only (TK64's callback hears them from TK64A)
 *   R  FileCdr while TK64 holds it: its own callback (r1), -1 (r2), NULL
 *      (r3); then the file operations of F
 *   O  FileCdr(its own callback) (r1), then end
 *   P  FileCdr(cb, the far address after the letter: TK64's) (r1), the
 *      file operations of F, then end
 *   Q  as O, M as P, after USER's InitApp (when USER is loaded): USER
 *      then sees the task end (Pietrek, Windows Internals ch. 3: its
 *      AppExit removes a FileCdr callback in the ending program's module)
 */
typedef struct tk64sh {
    WORD done;          /* TK64A got to its end */
    WORD task;          /* its hTask */
    WORD app;           /* InitApp's result (Q, M) */
    DWORD r1, r2, r3;   /* FileCdr's results, as above */
} tk64sh;

/* the files TK64A makes and deletes (its F operations) */
#define TK64A_FILE "C:\\TK64D\\HELPER.TXT"
