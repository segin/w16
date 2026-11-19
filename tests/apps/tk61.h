/*
 * tk61.h - shared by the freed-selector probe TK61.EXE (tk61.c) and its
 * helper task TK61A.EXE (tk61a.c). TK61A's command line: "<TK61's DS>
 * <offset of a tk61sh>", in hex; it writes what it made there, then ends
 * with its fixed block's selector in FS and its moveable block's in GS.
 */
typedef struct {
    WORD done;          /* set last, with 1 */
    WORD task;          /* TK61A's task */
    WORD fixed;         /* the selector of a GMEM_FIXED block of 100h */
    WORD moveable;      /* the selector of a GMEM_MOVEABLE block of 100h */
    WORD ds;            /* TK61A's DGROUP */
    WORD cs;            /* TK61A's code segment */
} tk61sh;
