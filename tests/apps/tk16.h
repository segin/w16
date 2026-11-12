/*
 * tk16.h - shared by the task probe TK16.EXE (tk16.c) and its helpers
 * TK16A.EXE, TK16B.EXE and TK16C.EXE (all tk16a.c): the block in TK16's
 * DGROUP the helpers write to. Command line as for TK15A (tk15.h):
 * "<mode> <slot> <TK16's DS> <offset of tk16sh> <TK16's hTask>". Modes:
 *   R  report and exit
 *   W  report, then Yield() until TK16 sets `go`, then exit
 *   H  report, with its handle counts (W_HC0, W_HC1), and exit
 */
#define TK16_SLOTS  4
#define TK16_SLOTW  56

typedef struct {
    WORD go;
    WORD v[TK16_SLOTS][TK16_SLOTW];
} tk16sh;

enum {
    W_PRE,              /* set before InitTask */
    W_RAN,              /* set after InitTask */
    W_HPREV,
    W_TAILLEN,
    W_TAIL,             /* 12 words: the PSP's command tail */
    W_CWD = W_TAIL + 12,/* 12 words: the current directory */
    W_ENVSAME = W_CWD + 12,
    W_ENVSEL,
    W_ENVLEN,           /* up to the double NUL */
    W_ENVTAIL,          /* the word after it */
    W_ENVPATH,          /* 4 words: the bytes after that */
    W_ENVPATH_END = W_ENVPATH + 3,
    W_ENV,              /* 8 words: the start of the environment */
    W_DONE = W_ENV + 8, /* set as it returns from its main */
    W_HC0,              /* mode H: SetHandleCount(0) */
    W_HC1,              /* mode H: then SetHandleCount(30) */
    W_EBX,              /* BX at entry (stack size) */
    W_ECX,              /* CX at entry (heap size) */
    W_ICX,              /* CX after InitTask */
    W_MT10,             /* its module table: heap size */
    W_MT12,             /* its module table: stack size */
    W_SELF,             /* a static far pointer to its own data: its
                           selector is DS at entry */
    W_COUNT
};
