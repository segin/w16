/*
 * th.h - w16's TOOLHELP.DLL, internal definitions (ARCHITECTURE.md 22).
 *
 * Sources: Windows 3.1 SDK, Programmer's Reference Vol. 1 ch. 8, Vol. 2
 * (the functions, their flags and results), Vol. 3 (the structures and
 * their constants); the values not given there observed with
 * tests/toolhelp/thp.c on Windows 3.1. The layouts read here (THHOOK,
 * the global and local heaps, module and task databases, USER's
 * classes) are Windows 3.1's, as w16's KERNEL and USER keep them
 * (kernel/global.c, global386.c, local.c, task.c, mdb.c, toolhelp.c;
 * user/class.c), from Schulman et al., Undocumented Windows (1992), ch.
 * 5, 6 and 8 and Pietrek, Windows Internals (1993), ch. 2 and 3, each
 * fact confirmed by a probe.
 */
#ifndef W16_TH_H
#define W16_TH_H

#include <windows.h>

#define THAPI __loadds FAR PASCAL       /* exported by toolhelp.lnk */

#pragma pack(push, 1)
typedef struct { DWORD dwSize; WORD wcItems, wcItemsFree, wcItemsLRU; } GLOBALINFO;
typedef struct {
    DWORD dwSize, dwAddress, dwBlockSize;
    WORD hBlock, wcLock, wcPageLock, wFlags, wHeapPresent, hOwner, wType, wData;
    DWORD dwNext, dwNextAlt;
} GLOBALENTRY;
typedef struct { DWORD dwSize; WORD wcItems; } LOCALINFO;
typedef struct {
    DWORD dwSize;
    WORD hHandle, wAddress, wSize, wFlags, wcLock, wType, hHeap, wHeapType, wNext;
} LOCALENTRY;
typedef struct {
    DWORD dwSize;
    WORD hTask, wSS, wBP, wCS, wIP, hModule, wSegment, wFlags;
} STACKTRACEENTRY;
#define MAX_MODULE_NAME 9
#define MAX_PATH        255
#define MAX_CLASSNAME   255
typedef struct {
    DWORD dwSize;
    char szModule[MAX_MODULE_NAME + 1];
    WORD hModule, wcUsage;
    char szExePath[MAX_PATH + 1];
    WORD wNext;
} MODULEENTRY;
typedef struct {
    DWORD dwSize;
    WORD hTask, hTaskParent, hInst, hModule, wSS, wSP, wStackTop, wStackMinimum,
         wStackBottom, wcEvents, hQueue;
    char szModule[MAX_MODULE_NAME + 1];
    WORD wPSPOffset, hNext;
} TASKENTRY;
typedef struct {
    DWORD dwSize;
    WORD hInst;
    char szClassName[MAX_CLASSNAME + 1];
    WORD wNext;
} CLASSENTRY;
typedef struct {
    DWORD dwSize, dwLargestFreeBlock, dwMaxPagesAvailable, dwMaxPagesLockable,
          dwTotalLinearSpace, dwTotalUnlockedPages, dwFreePages, dwTotalPages,
          dwFreeLinearSpace, dwSwapFilePages;
    WORD wPageSize;
} MEMMANINFO;
typedef struct { DWORD dwSize; WORD wUserFreePercent, wGDIFreePercent, hUserSegment, hGDISegment; } SYSHEAPINFO;
typedef struct { DWORD dwSize, dwmsSinceStart, dwmsThisVM; } TIMERINFO;
typedef struct { DWORD dwSize; WORD wSelector, wSegNum, wType, wcInstance; LPCSTR lpstrModuleName; } NFYLOADSEG;
typedef struct { DWORD dwSize; WORD hModule, wCS, wIP; } NFYSTARTDLL;
typedef struct { DWORD dwSize; WORD wIP, wCS, wSS, wBP, wExitCode; } NFYRIP;
typedef struct { DWORD dwSize; WORD wErrCode; void FAR *lpInfo; } NFYLOGERROR;
typedef struct { DWORD dwSize; WORD wErrCode; FARPROC lpfnErrorAddr; void FAR * FAR *lpBadParam; } NFYLOGPARAMERROR;
#pragma pack(pop)

/* GLOBALENTRY */
#define GLOBAL_ALL      0
#define GLOBAL_LRU      1
#define GLOBAL_FREE     2
#define GT_UNKNOWN      0
#define GT_DGROUP       1
#define GT_DATA         2
#define GT_CODE         3
#define GT_TASK         4
#define GT_RESOURCE     5
#define GT_MODULE       6
#define GT_FREE         7
#define GT_INTERNAL     8
#define GT_SENTINEL     9
#define GT_BURGERMASTER 10
#define GF_PDB_OWNER    0x0100
/* LOCALENTRY */
#define NORMAL_HEAP     0
#define USER_HEAP       1
#define GDI_HEAP        2
#define LF_FIXED        1
#define LF_FREE         2
#define LF_MOVEABLE     4
#define LT_NORMAL       0
#define LT_FREE         0xFF
/* STACKTRACEENTRY */
#define FRAME_FAR       0
#define FRAME_NEAR      1
/* NotifyRegister */
#define NFY_UNKNOWN     0
#define NFY_LOADSEG     1
#define NFY_FREESEG     2
#define NFY_STARTDLL    3
#define NFY_STARTTASK   4
#define NFY_EXITTASK    5
#define NFY_DELMODULE   6
#define NFY_RIP         7
#define NFY_TASKIN      8
#define NFY_TASKOUT     9
#define NFY_INCHAR      10
#define NFY_OUTSTR      11
#define NFY_LOGERROR    12
#define NFY_LOGPARAMERROR 13
#define NF_NORMAL       0
#define NF_TASKSWITCH   1
#define NF_RIP          2
/* TerminateApp */
#define UAE_BOX         0
#define NO_UAE_BOX      1

/* toolhelp.c: shared helpers and KERNEL's entries */
#define PW(s, o)        (*(WORD FAR *)MAKELP((s), (o)))
#define PB(s, o)        (*(BYTE FAR *)MAKELP((s), (o)))
extern HMODULE hk, hu, hg;
extern WORD FAR *thhook;
extern DWORD (FAR PASCAL *kGlobalMasterHandle)(void);
extern WORD (FAR PASCAL *kGetExePtr)(WORD);
extern WORD dsel, scratch;
WORD lsl(WORD sel);
WORD lar(WORD sel);
BOOL readable(WORD sel, WORD off, WORD n);
void copy(void FAR *d, const void FAR *s, WORD n);
BOOL good(void FAR *p, WORD size);
BOOL lread(DWORD lin, void FAR *d, WORD n);
BOOL is_module(WORD h);
BOOL is_task(WORD t);
WORD dgroup_of(WORD hmod);

#endif
