/*
 * tk15.h - shared by the task probe TK15.EXE (tk15.c) and its helper
 * TK15A.EXE (tk15a.c): the block in TK15's DGROUP that the helper's
 * instances write their observations to (through TK15's DS, which they
 * get on the command line), and the trace both append to.
 *
 * Command line of TK15A: "<mode> <slot> <TK15's DS> <offset of tk15sh>
 * <TK15's hTask>", numbers in hex. Modes:
 *   A  observe, WaitEvent(0), Yield(), change directory, GlobalAlloc,
 *      LoadLibrary("TKID.TPL"), Yield(), exit
 *   B  observe, then Yield() until TK15 sets `go`, then exit
 *   C  observe, then WaitEvent(0) three times (events probe), exit
 */
#define TK15_SLOTS   3
#define TK15_SLOTW   128
#define TK15_TRACE   96

typedef struct {
    WORD ntrace;
    WORD trace[TK15_TRACE];
    WORD go;
    WORD v[TK15_SLOTS][TK15_SLOTW];
} tk15sh;

/* trace codes: TK15 0xA0nn, TK15A 0xC<slot>nn */
#define TR_P(n)         (0xA000 | (n))
#define TR_C(slot, n)   (0xC000 | ((slot) << 8) | (n))

/* indices into v[slot] */
enum {
    V_DONE,             /* set last, with 1 */
    V_HTASK, V_EV_ENTRY, V_PEV_ENTRY, V_DLL_PRE, V_DLB_PRE,
    V_SS_PRE, V_SP_PRE,
    V_E_AX, V_E_BX, V_E_CX, V_E_DX, V_E_SI, V_E_DI, V_E_BP, V_E_SP,
    V_E_DS, V_E_ES, V_E_SS,
    V_I_AX, V_I_BX, V_I_CX, V_I_DX, V_I_SI, V_I_DI, V_I_ES,
    V_DLL_CALLS, V_DLB_CALLS, V_DLL_SS, V_DLL_SP, V_DLB_SS, V_DLB_SP,
    V_DLL_DI, V_DLL_ES, V_DLL_CMDLEN, V_DLL_SAWDLB, V_DLL_DSNOW,
    V_T00, V_T02, V_T04, V_T06, V_T08, V_T0C, V_T1A, V_T1C, V_T1E,
    V_T20, V_T22, V_T60, V_T62, V_T64, V_T66, V_TAA, V_TAC, V_TAE,
    V_TB0, V_TB2, V_TB4, V_TB8, V_TFA,
    V_PDB, V_PDB_DX, V_I21_62, V_I21_51,
    V_PSP16, V_PSP2C, V_PSP80, V_TAIL /* 8 words */,
    V_ENV = V_TAIL + 8, V_ENV_SAMESEL, V_ENV_SAMETEXT,
    V_NTASKS, V_USAGE, V_HMOD, V_CS, V_DS,
    V_MPI_SEG, V_MPI_OFF, V_CMD /* 8 words */,
    V_PREV_DATA = V_CMD + 8, V_GID, V_EV_A1, V_EV_A2,
    V_GBLOCK, V_TKID, V_CWDOK, V_CWD2OK, V_WAITRET, V_DGSIZE, V_PHEAP,
    V_COUNT
};

/* KERNEL exports, imported by ordinal under our own names (tk15k.lnk), so
   that undocumented ones need no prototypes from windows.h */
extern DWORD FAR PASCAL K_GETCURRENTTASK(void);         /* 36: DX = head */
extern DWORD FAR PASCAL K_GETCURRENTPDB(void);          /* 37 */
extern WORD FAR PASCAL K_GETNUMTASKS(void);             /* 152 */
extern WORD FAR PASCAL K_ISTASK(WORD h);                /* 320 */
extern WORD FAR PASCAL K_GETTASKDS(void);               /* 155 */
extern WORD FAR PASCAL K_GETTASKQUEUE(WORD t);          /* 35 */
extern WORD FAR PASCAL K_SETTASKQUEUE(WORD t, WORD q);  /* 34 */
extern WORD FAR PASCAL K_POSTEVENT(WORD t);             /* 31 */
extern WORD FAR PASCAL K_WAITEVENT(WORD t);             /* 30 */
extern void FAR PASCAL K_YIELD(void);                   /* 29 */
extern void FAR PASCAL K_OLDYIELD(void);                /* 117 */
extern void FAR PASCAL K_DIRECTEDYIELD(WORD t);         /* 150 */
extern WORD FAR PASCAL K_GETEXEPTR(WORD h);             /* 133 */
extern DWORD FAR PASCAL K_GETSELECTORBASE(WORD sel);    /* 186 */
extern DWORD FAR PASCAL K_GETDOSENVIRONMENT(void);      /* 131 */
extern WORD FAR PASCAL K_LOADMODULE(const char FAR *name, void FAR *params); /* 45 */
extern WORD FAR PASCAL K_WINEXEC(const char FAR *cmd, WORD show);  /* 166 */

static WORD tdbw(WORD t, WORD off)
{
    return *(WORD FAR *)MAKELP(t, off);
}

static BYTE tdbb(WORD t, WORD off)
{
    return *(BYTE FAR *)MAKELP(t, off);
}

#ifdef TK15_HELPER
/* a hex number of the command line; *pp goes past it and the blanks */
static WORD hexw(const char FAR **pp)
{
    const char FAR *p = *pp;
    WORD v = 0;
    while (*p == ' ')
        p++;
    for (;;) {
        char c = *p;
        if (c >= '0' && c <= '9')
            v = v * 16 + (c - '0');
        else if (c >= 'A' && c <= 'F')
            v = v * 16 + (c - 'A' + 10);
        else if (c >= 'a' && c <= 'f')
            v = v * 16 + (c - 'a' + 10);
        else
            break;
        p++;
    }
    while (*p == ' ')
        p++;
    *pp = p;
    return v;
}
#endif

static WORD cur_cs(void);
#pragma aux cur_cs = "mov ax, cs" value [ax];
static WORD cur_ss(void);
#pragma aux cur_ss = "mov ax, ss" value [ax];
static WORD cur_sp(void);
#pragma aux cur_sp = "mov ax, sp" value [ax];
/* (lstrcmp is USER's, which w16 does not have yet) */
static int str_eq(const char FAR *a, const char FAR *b)
{
    while (*a && *a == *b) {
        a++;
        b++;
    }
    return *a == *b;
}

static WORD cur_ds(void);
#pragma aux cur_ds = "mov ax, ds" value [ax];
